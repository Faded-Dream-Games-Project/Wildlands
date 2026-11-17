/* world.c — chunk streaming with a background generation thread.
 *
 * Threading model: the worker thread only ever allocates plain block/biome
 * buffers, fills them via the stateless worldgen, and hands them to the main
 * thread through a queue. All chunk/hash/GPU work happens on the main thread.
 * If thread creation fails, generation silently falls back to inline
 * (budgeted) generation in wl_world_update. */
#include "wl/world.h"
#include "wl/mesher.h"
#include "wl/light.h"
#include "wl/entities.h"
#include "wl/world_store.h"
#include "wl/block.h"
#include "wl/player.h"
#include "wl/util.h"
#include "wl/paths.h" /* wl_path_join */
#include "wl/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

/* monotonic clock for timing diagnostics */
static double wl_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

#define WL_HASH_BITS 10
#define WL_HASH_SIZE (1 << WL_HASH_BITS)
#define WL_GEN_QUEUE_CAP 512
#define WL_DONE_QUEUE_CAP 256

typedef struct {
    int32_t cx, cz;
    uint8_t *blocks; /* WL_CHUNK_VOL, NULL on out-of-memory */
    uint8_t *biome;  /* WL_CHUNK_AREA */
} WlGenResult;

typedef struct {
    int32_t cx, cz;
} WlGenJob;

/* private argument block for the generation worker (embedded in WlWorld) */
typedef struct {
    WlWorldGen gen; /* private copy; never touched by the main thread */
    WlWorld *w;
} WlGenThreadArg;

struct WlWorld {
    WlWorldGen gen;
    char name[WL_NAME_MAX];
    int gamemode;
    uint32_t time_ticks;
    float time_frac;
    int render_distance;
    char save_dir[WL_PATH_MAX];

    WlChunk *hash[WL_HASH_SIZE];
    int chunk_count;
    WlChunk **list;
    int list_count, list_cap;

    /* dropped items (compacted: active drops live at [0, drop_count)) */
    WlDrop drops[WL_DROPS_MAX];
    int drop_count;

    /* living entities (compacted like drops) */
    WlEntity ents[WL_ENTITIES_MAX];
    int ent_count;
    uint32_t next_ent_id;

    /* furnace block entities (keyed by position, holes are inactive) */
    WlFurnace furnaces[WL_FURNACES_MAX];
    int furnace_count;

    /* sound events raised inside world code that has no audio access;
     * drained by the game loop every frame */
    int pickup_sfx_pending;

    /* scheduled water updates: positions with a due time; processed a
     * bounded number per frame so propagation never stalls the loop */
    struct {
        int32_t x, y, z;
        float due;
    } water_q[WL_WATER_QUEUE_MAX];
    int water_qn;
    int water_dropped;
    float water_time;

    /* generation worker */
    bool thread_ok;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cv_work;   /* signaled when jobs are pushed or on stop */
    pthread_cond_t cv_space;  /* signaled when the done queue drains */
    volatile bool stop;
    WlGenJob gen_q[WL_GEN_QUEUE_CAP];
    int gen_q_head, gen_q_count;
    WlGenResult done_q[WL_DONE_QUEUE_CAP];
    int done_q_head, done_q_count;

    /* timing diagnostics (exported through wl_world_stats) */
    double gen_ms_total;           /* worker thread, written under lock */
    int gen_count;
    float mesh_ms_ema, mesh_ms_last; /* main thread only */

    /* worker-thread argument (embedded: no allocation to manage) */
    WlGenThreadArg thread_arg;
};

/* ------------------------------------------------------------------ */
/* hash table                                                          */
/* ------------------------------------------------------------------ */

static uint32_t coord_hash(int cx, int cz)
{
    uint32_t x = (uint32_t)cx, z = (uint32_t)cz;
    uint32_t h = x * 0x9E3779B1u ^ z * 0x85EBCA77u;
    h ^= h >> 13;
    h *= 0xC2B2AE3Du;
    h ^= h >> 16;
    return h;
}

static void hash_insert(WlWorld *w, WlChunk *c)
{
    uint32_t b = coord_hash(c->cx, c->cz) & (WL_HASH_SIZE - 1);
    c->hash_next = w->hash[b];
    w->hash[b] = c;
    w->chunk_count++;
}

static void hash_remove(WlWorld *w, WlChunk *c)
{
    uint32_t b = coord_hash(c->cx, c->cz) & (WL_HASH_SIZE - 1);
    WlChunk **pp = &w->hash[b];
    while (*pp) {
        if (*pp == c) {
            *pp = c->hash_next;
            w->chunk_count--;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

WlChunk *wl_world_chunk_at(WlWorld *w, int cx, int cz)
{
    if (!w) return NULL;
    uint32_t b = coord_hash(cx, cz) & (WL_HASH_SIZE - 1);
    for (WlChunk *c = w->hash[b]; c; c = c->hash_next)
        if (c->cx == cx && c->cz == cz) return c;
    return NULL;
}

static WlChunk *chunk_ensure(WlWorld *w, int cx, int cz)
{
    WlChunk *c = wl_world_chunk_at(w, cx, cz);
    if (c) return c;
    c = wl_chunk_alloc(cx, cz);
    if (!c) return NULL;
    hash_insert(w, c);
    if (w->list_count == w->list_cap) {
        int cap = w->list_cap ? w->list_cap * 2 : 256;
        WlChunk **nl = realloc(w->list, (size_t)cap * sizeof(WlChunk *));
        if (!nl) return c; /* stays in hash but off the list: rare OOM */
        w->list = nl;
        w->list_cap = cap;
    }
    w->list[w->list_count++] = c;
    return c;
}

/* ------------------------------------------------------------------ */
/* generation worker                                                    */
/* ------------------------------------------------------------------ */

static void *gen_thread_main(void *argp)
{
    WlGenThreadArg *arg = argp;
    for (;;) {
        WlGenJob job;
        pthread_mutex_lock(&arg->w->lock);
        while (arg->w->gen_q_count == 0 && !arg->w->stop)
            pthread_cond_wait(&arg->w->cv_work, &arg->w->lock);
        if (arg->w->gen_q_count == 0) { /* stop requested and nothing left */
            pthread_mutex_unlock(&arg->w->lock);
            break;
        }
        job = arg->w->gen_q[arg->w->gen_q_head];
        arg->w->gen_q_head = (arg->w->gen_q_head + 1) % WL_GEN_QUEUE_CAP;
        arg->w->gen_q_count--;
        pthread_mutex_unlock(&arg->w->lock);

        WlGenResult r;
        r.cx = job.cx;
        r.cz = job.cz;
        r.blocks = malloc(WL_CHUNK_VOL);
        r.biome = malloc(WL_CHUNK_AREA);
        double gen_ms = 0.0;
        if (r.blocks && r.biome) {
            double t0 = wl_monotonic_ms();
            wl_worldgen_chunk(&arg->gen, job.cx, job.cz, r.blocks, r.biome);
            gen_ms = wl_monotonic_ms() - t0;
        } else {
            WL_LOGE("gen thread: out of memory for chunk %d,%d", job.cx, job.cz);
            free(r.blocks);
            free(r.biome);
            r.blocks = NULL;
            r.biome = NULL;
        }

        pthread_mutex_lock(&arg->w->lock);
        arg->w->gen_ms_total += gen_ms;
        arg->w->gen_count++;
        while (arg->w->done_q_count == WL_DONE_QUEUE_CAP && !arg->w->stop)
            pthread_cond_wait(&arg->w->cv_space, &arg->w->lock);
        if (arg->w->stop) {
            pthread_mutex_unlock(&arg->w->lock);
            free(r.blocks);
            free(r.biome);
            break;
        }
        int tail = (arg->w->done_q_head + arg->w->done_q_count) % WL_DONE_QUEUE_CAP;
        arg->w->done_q[tail] = r;
        arg->w->done_q_count++;
        pthread_mutex_unlock(&arg->w->lock);
    }
    return NULL;
}

static void apply_gen_result(WlWorld *w, const WlGenResult *r)
{
    WlChunk *c = wl_world_chunk_at(w, r->cx, r->cz);
    if (!c) return; /* unloaded while generating */

    if (!r->blocks) { /* worker OOM: allow a retry later */
        c->queued_gen = false;
        return;
    }

    memcpy(c->blocks, r->blocks, WL_CHUNK_VOL);
    memcpy(c->biome, r->biome, WL_CHUNK_AREA);
    wl_chunk_compute_top(c);

    /* overlay saved player edits from a previous session, if any */
    uint8_t saved[WL_CHUNK_VOL];
    if (w->save_dir[0] != '\0' &&
        wl_store_load_chunk(w->save_dir, c->cx, c->cz, saved)) {
        memcpy(c->blocks, saved, WL_CHUNK_VOL);
        wl_chunk_compute_top(c);
    }

    c->generated = true;
    c->queued_gen = false;
    c->dirty_mesh = true;
    c->light_dirty = true;

    /* border faces of already-meshed neighbors must be rebuilt; their
     * light imports change too, so they relight as well */
    static const int8_t dirs[8][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
        { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }
    };
    for (int i = 0; i < 8; i++) {
        WlChunk *n = wl_world_chunk_at(w, c->cx + dirs[i][0], c->cz + dirs[i][1]);
        if (n && n->generated) {
            n->dirty_mesh = true;
            n->light_dirty = true;
        }
    }
}

static void integrate_done(WlWorld *w, int budget)
{
    WlGenResult batch[64];
    while (budget > 0) {
        int n = budget < 64 ? budget : 64;
        pthread_mutex_lock(&w->lock);
        if (w->done_q_count == 0) {
            pthread_mutex_unlock(&w->lock);
            break;
        }
        if (n > w->done_q_count) n = w->done_q_count;
        for (int i = 0; i < n; i++) {
            batch[i] = w->done_q[(w->done_q_head + i) % WL_DONE_QUEUE_CAP];
        }
        w->done_q_head = (w->done_q_head + n) % WL_DONE_QUEUE_CAP;
        w->done_q_count -= n;
        pthread_cond_broadcast(&w->cv_space);
        pthread_mutex_unlock(&w->lock);

        for (int i = 0; i < n; i++) {
            apply_gen_result(w, &batch[i]);
            free(batch[i].blocks);
            free(batch[i].biome);
        }
        budget -= n;
    }
}

/* ------------------------------------------------------------------ */
/* public API                                                           */
/* ------------------------------------------------------------------ */

WlWorld *wl_world_create(const WlWorldParams *params, const char *save_dir)
{
    WlWorld *w = calloc(1, sizeof(WlWorld));
    if (!w) {
        WL_LOGE("out of memory creating world");
        return NULL;
    }
    wl_worldgen_init(&w->gen, params->seed, params->type);
    wl_scpy(w->name, params->name, sizeof(w->name));
    w->gamemode = params->gamemode;
    w->render_distance = 8;
    wl_scpy(w->save_dir, save_dir ? save_dir : "", sizeof(w->save_dir));
    wl_world_load_drops(w); /* existing death drops, if any */
    wl_world_load_entities(w);
    wl_world_load_furnaces(w);

    pthread_mutex_init(&w->lock, NULL);
    pthread_cond_init(&w->cv_work, NULL);
    pthread_cond_init(&w->cv_space, NULL);

    w->thread_arg.gen = w->gen; /* private copy for the worker */
    w->thread_arg.w = w;
    if (pthread_create(&w->thread, NULL, gen_thread_main, &w->thread_arg) == 0) {
        w->thread_ok = true;
    } else {
        WL_LOGW("generation thread failed to start; using inline generation");
    }
    WL_LOGI("world '%s' created (seed %llu, type %d, gamemode %d, thread %s)",
            w->name, (unsigned long long)params->seed, params->type,
            params->gamemode, w->thread_ok ? "on" : "off");
    return w;
}

void wl_world_destroy(WlWorld *w)
{
    if (!w) return;

    if (w->thread_ok) {
        pthread_mutex_lock(&w->lock);
        w->stop = true;
        pthread_cond_broadcast(&w->cv_work);
        pthread_cond_broadcast(&w->cv_space);
        pthread_mutex_unlock(&w->lock);
        pthread_join(w->thread, NULL);
    }
    /* discard anything the worker still queued */
    pthread_mutex_lock(&w->lock);
    while (w->done_q_count > 0) {
        WlGenResult *r = &w->done_q[w->done_q_head];
        w->done_q_head = (w->done_q_head + 1) % WL_DONE_QUEUE_CAP;
        w->done_q_count--;
        free(r->blocks);
        free(r->biome);
    }
    pthread_mutex_unlock(&w->lock);

    for (int i = 0; i < w->list_count; i++)
        wl_chunk_free(w->list[i]);
    free(w->list);

    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->cv_work);
    pthread_cond_destroy(&w->cv_space);
    free(w);
    WL_LOGI("world destroyed");
}

void wl_world_set_render_distance(WlWorld *w, int chunks)
{
    if (!w) return;
    w->render_distance = wl_clampi(chunks, 2, 16);
}

int wl_world_render_distance(const WlWorld *w) { return w ? w->render_distance : 8; }

uint8_t wl_world_get_block(WlWorld *w, int x, int y, int z)
{
    if (y < 0) return WL_BLOCK_BEDROCK; /* safety floor for physics */
    if (y >= WL_CHUNK_H) return WL_BLOCK_AIR;
    WlChunk *c = wl_world_chunk_at(w, x >> 4, z >> 4);
    if (!c || !c->generated) return WL_BLOCK_AIR;
    return c->blocks[wl_chunk_index(x & 15, y, z & 15)];
}

bool wl_world_solid_at(WlWorld *w, int x, int y, int z)
{
    return wl_block_is_solid(wl_world_get_block(w, x, y, z));
}

/* ------------------------------------------------------------------ */
/* water propagation                                                    */
/* ------------------------------------------------------------------ */

static int water_level_of(uint8_t id)
{
    if (id == WL_BLOCK_WATER) return 8;
    return wl_water_flow_level(id); /* 1..7, or 0 */
}

/* blocks that flowing water may replace (air + small plants) */
static bool water_replaceable(uint8_t id)
{
    return id == WL_BLOCK_AIR || id == WL_BLOCK_TALLGRASS ||
           id == WL_BLOCK_FLOWER_RED || id == WL_BLOCK_FLOWER_YELLOW;
}

void wl_world_schedule_water(WlWorld *w, int x, int y, int z, float delay)
{
    if (!w || y < 0 || y >= WL_CHUNK_H) return;
    if (w->water_qn >= WL_WATER_QUEUE_MAX) {
        w->water_dropped++;
        return; /* drop: bounded */
    }
    /* coalesce repeats for the same cell */
    for (int i = 0; i < w->water_qn; i++) {
        if (w->water_q[i].x == x && w->water_q[i].y == y &&
            w->water_q[i].z == z) {
            if (w->water_time + delay < w->water_q[i].due)
                w->water_q[i].due = w->water_time + delay;
            return;
        }
    }
    w->water_q[w->water_qn].x = (int32_t)x;
    w->water_q[w->water_qn].y = (int32_t)y;
    w->water_q[w->water_qn].z = (int32_t)z;
    w->water_q[w->water_qn].due = w->water_time + delay;
    w->water_qn++;
}

/* processes one water cell: dries orphaned flows, pours down, spreads
 * sideways with decreasing level. Levels decrease monotonically along
 * any spread path, so propagation is finite by construction. */
static void water_process(WlWorld *w, int x, int y, int z)
{
    uint8_t id = wl_world_get_block(w, x, y, z);
    int lvl = water_level_of(id);
    if (lvl == 0) return;

    /* flow without a supplier dries up (source removal) */
    if (lvl < 8) {
        bool supplied = water_level_of(wl_world_get_block(w, x, y + 1, z)) > 0;
        if (!supplied) {
            int best = 0;
            static const int sdx[4] = { 1, -1, 0, 0 };
            static const int sdz[4] = { 0, 0, 1, -1 };
            for (int i = 0; i < 4; i++) {
                int nl = water_level_of(
                    wl_world_get_block(w, x + sdx[i], y, z + sdz[i]));
                if (nl > best) best = nl;
            }
            supplied = best > lvl;
        }
        if (!supplied) {
            wl_world_set_block(w, x, y, z, WL_BLOCK_AIR);
            wl_world_schedule_water(w, x + 1, y, z, WL_WATER_DELAY);
            wl_world_schedule_water(w, x - 1, y, z, WL_WATER_DELAY);
            wl_world_schedule_water(w, x, y, z + 1, WL_WATER_DELAY);
            wl_world_schedule_water(w, x, y, z - 1, WL_WATER_DELAY);
            wl_world_schedule_water(w, x, y - 1, z, WL_WATER_DELAY);
            return;
        }
    }

    /* pour down first (falling water keeps full strength) */
    if (y > 0) {
        uint8_t below = wl_world_get_block(w, x, y - 1, z);
        if (water_replaceable(below)) {
            wl_world_set_block(w, x, y - 1, z, wl_water_flow_id(7));
            wl_world_schedule_water(w, x, y - 1, z, WL_WATER_DELAY);
            return;
        }
    }

    /* spread sideways with a weaker level */
    if (lvl > 1) {
        static const int sdx[4] = { 1, -1, 0, 0 };
        static const int sdz[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4; i++) {
            int nx = x + sdx[i], nz = z + sdz[i];
            uint8_t nb = wl_world_get_block(w, nx, y, nz);
            if (water_replaceable(nb)) {
                wl_world_set_block(w, nx, y, nz, wl_water_flow_id(lvl - 1));
                wl_world_schedule_water(w, nx, y, nz, WL_WATER_DELAY);
            }
        }
    }
}

int wl_world_water_dropped(const WlWorld *w)
{
    return w ? w->water_dropped : 0;
}

int wl_world_water_pending(const WlWorld *w)
{
    return w ? w->water_qn : 0;
}

void wl_world_water_tick(WlWorld *w, float dt)
{
    if (!w) return;
    w->water_time += dt;
    int budget = 96; /* bounded work per frame */
    int i = 0;
    while (i < w->water_qn && budget > 0) {
        if (w->water_q[i].due <= w->water_time) {
            int x = w->water_q[i].x, y = w->water_q[i].y, z = w->water_q[i].z;
            w->water_q[i] = w->water_q[--w->water_qn]; /* swap-remove */
            water_process(w, x, y, z);
            budget--;
        } else {
            i++;
        }
    }
}

bool wl_world_set_block(WlWorld *w, int x, int y, int z, uint8_t id)
{
    if (y < 0 || y >= WL_CHUNK_H) return false;
    int cx = x >> 4, cz = z >> 4;
    WlChunk *c = wl_world_chunk_at(w, cx, cz);
    if (!c || !c->generated) return false;

    int lx = x & 15, lz = z & 15;
    if (c->blocks[wl_chunk_index(lx, y, lz)] == id) return true;
    if (!wl_chunk_set(c, lx, y, lz, id)) return false;

    c->modified = true;
    c->dirty_mesh = true;
    c->light_dirty = true; /* torch placed/broken, skylight column changed */

    /* water reacts to the change: any liquid neighbour (and the cell
     * itself when it became liquid) re-evaluates on the next tick */
    {
        static const int d6[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
        for (int i = 0; i < 6; i++) {
            int nx = x + d6[i][0], ny = y + d6[i][1], nz = z + d6[i][2];
            if (wl_blocks_liquid(wl_world_get_block(w, nx, ny, nz)))
                wl_world_schedule_water(w, nx, ny, nz, WL_WATER_DELAY);
        }
        if (wl_blocks_liquid(id))
            wl_world_schedule_water(w, x, y, z, WL_WATER_DELAY);
    }

    if (lx == 0) {
        WlChunk *n = wl_world_chunk_at(w, cx - 1, cz);
        if (n) { n->dirty_mesh = true; n->light_dirty = true; }
    }
    if (lx == 15) {
        WlChunk *n = wl_world_chunk_at(w, cx + 1, cz);
        if (n) { n->dirty_mesh = true; n->light_dirty = true; }
    }
    if (lz == 0) {
        WlChunk *n = wl_world_chunk_at(w, cx, cz - 1);
        if (n) { n->dirty_mesh = true; n->light_dirty = true; }
    }
    if (lz == 15) {
        WlChunk *n = wl_world_chunk_at(w, cx, cz + 1);
        if (n) { n->dirty_mesh = true; n->light_dirty = true; }
    }
    return true;
}

int wl_world_top_at(WlWorld *w, int x, int z)
{
    WlChunk *c = wl_world_chunk_at(w, x >> 4, z >> 4);
    if (!c || !c->generated) return 0;
    return c->top[(z & 15) * WL_CHUNK_W + (x & 15)];
}

WlChunk *const *wl_world_chunk_list(WlWorld *w, int *count)
{
    if (count) *count = w ? w->list_count : 0;
    return w ? w->list : NULL;
}

uint32_t wl_world_time(const WlWorld *w) { return w ? w->time_ticks : 0; }

void wl_world_set_time(WlWorld *w, uint32_t ticks)
{
    if (!w) return;
    w->time_ticks = ticks % WL_DAY_TICKS;
    w->time_frac = 0.0f;
}

void wl_world_advance_time(WlWorld *w, float dt)
{
    if (!w) return;
    w->time_frac += dt * WL_TICKS_PER_SECOND;
    while (w->time_frac >= 1.0f) {
        w->time_frac -= 1.0f;
        w->time_ticks = (w->time_ticks + 1) % WL_DAY_TICKS;
    }
}

bool wl_world_save_modified(WlWorld *w)
{
    if (!w || w->save_dir[0] == '\0') return false;
    bool ok = true;
    int saved = 0;
    for (int i = 0; i < w->list_count; i++) {
        WlChunk *c = w->list[i];
        if (!c->modified) continue;
        if (wl_store_save_chunk(w->save_dir, c->cx, c->cz, c->blocks)) {
            c->modified = false;
            saved++;
        } else {
            ok = false;
        }
    }
    if (saved > 0) WL_LOGI("saved %d modified chunks", saved);
    return ok;
}

/* ------------------------------------------------------------------ */
/* dropped items                                                       */
/* ------------------------------------------------------------------ */

bool wl_world_add_drop_thrown(WlWorld *w, WlItemId item, uint8_t count,
                              uint16_t dur, Vector3 pos, Vector3 vel,
                              float pickup_delay)
{
    if (!w || item == WL_ITEM_NONE || count == 0) return false;
    if (w->drop_count >= WL_DROPS_MAX) return false; /* caller retries */

    /* deterministic pseudo-random from the world time so repeated runs
     * of the same session scatter drops identically */
    uint32_t s = w->time_ticks * 2654435761u + (uint32_t)w->drop_count * 40503u + 1;
    s ^= s >> 13; s *= 0x5bd1e995u; s ^= s >> 15;

    WlDrop *d = &w->drops[w->drop_count++];
    d->item = item;
    d->count = count;
    d->dur = dur;
    d->pos = pos;
    d->vel = vel;
    d->age = pickup_delay > 0.0f ? -pickup_delay : 0.0f;
    d->seed = s;
    return true;
}

bool wl_world_add_drop(WlWorld *w, WlItemId item, uint8_t count, uint16_t dur,
                       Vector3 pos)
{
    /* small random throw */
    uint32_t s = w->time_ticks * 2654435761u + (uint32_t)w->drop_count * 40503u + 1;
    s ^= s >> 13; s *= 0x5bd1e995u; s ^= s >> 15;
    Vector3 vel = { ((float)(s & 15) - 7.5f) * 0.25f,
                    2.0f + (float)((s >> 4) & 7) * 0.25f,
                    ((s >> 7) & 15) * 0.25f - 1.9f };
    return wl_world_add_drop_thrown(w, item, count, dur, pos, vel, 0.5f);
}

void wl_world_update_drops(WlWorld *w, struct WlPlayer *p, float dt)
{
    if (!w) return;
    const float pickup_radius = 1.4f;

    for (int i = w->drop_count - 1; i >= 0; i--) {
        WlDrop *d = &w->drops[i];
        d->age += dt;

        /* gravity + ground collision */
        d->vel.y -= 22.0f * dt;
        d->pos.x += d->vel.x * dt;
        d->pos.y += d->vel.y * dt;
        d->pos.z += d->vel.z * dt;
        int bx = (int)floorf(d->pos.x);
        int by = (int)floorf(d->pos.y - 0.12f);
        int bz = (int)floorf(d->pos.z);
        if (wl_world_solid_at(w, bx, by, bz)) {
            d->pos.y = (float)(by + 1) + 0.12f;
            if (d->vel.y < 0.0f) d->vel.y = 0.0f;
            d->vel.x *= 0.72f;
            d->vel.z *= 0.72f;
        }

        /* pickup: player center within radius, after a short delay so
         * freshly dropped stacks do not teleport back instantly */
        if (p && !p->dead && d->age > 0.5f) {
            Vector3 pc = p->pos;
            float dx = d->pos.x - pc.x;
            float dy = d->pos.y - (pc.y + 0.9f);
            float dz = d->pos.z - pc.z;
            if (dx * dx + dy * dy + dz * dz < pickup_radius * pickup_radius) {
                int taken = wl_inv_add_partial(&p->inv, d->item, d->count, d->dur);
                if (taken > 0) {
                    w->pickup_sfx_pending++;
                    d->count = (uint8_t)(d->count - taken);
                    if (d->count == 0) {
                        w->drops[i] = w->drops[--w->drop_count];
                        continue;
                    }
                }
            }
        }
    }
}

int wl_world_consume_pickups(WlWorld *w)
{
    if (!w) return 0;
    int n = w->pickup_sfx_pending;
    w->pickup_sfx_pending = 0;
    return n;
}

WlEntity *wl_world_spawn_entity(WlWorld *w, WlEntityType type, Vector3 pos)
{
    if (!w || type <= WL_ENT_NONE || type >= WL_ENT_TYPE_COUNT) return NULL;
    if (w->ent_count >= WL_ENTITIES_MAX) return NULL;

    WlEntity *e = &w->ents[w->ent_count++];
    memset(e, 0, sizeof(*e));
    e->active = true;
    e->type = type;
    e->pos = pos;
    e->id = w->next_ent_id++;
    wl_entity_defaults(e); /* health + type defaults (entities.c) */
    return e;
}

WlEntity *wl_world_entities(WlWorld *w, int *count)
{
    if (count) *count = w ? w->ent_count : 0;
    return w ? w->ents : NULL;
}

void wl_world_note_entity_id(WlWorld *w, uint32_t id)
{
    if (w && w->next_ent_id <= id) w->next_ent_id = id + 1;
}

WlFurnace *wl_world_get_furnace(WlWorld *w, int x, int y, int z, bool create)
{
    if (!w) return NULL;
    for (int i = 0; i < w->furnace_count; i++)
        if (w->furnaces[i].active && w->furnaces[i].x == x &&
            w->furnaces[i].y == y && w->furnaces[i].z == z)
            return &w->furnaces[i];
    if (!create) return NULL;
    if (w->furnace_count >= WL_FURNACES_MAX) return NULL;

    /* reuse an inactive hole when present */
    for (int i = 0; i < w->furnace_count; i++) {
        if (!w->furnaces[i].active) {
            WlFurnace *f = &w->furnaces[i];
            memset(f, 0, sizeof(*f));
            /* slot empties are WL_ITEM_NONE, never raw 0 (inventory
             * convention; keeps can_output honest) */
            f->input.item = WL_ITEM_NONE;
            f->fuel.item = WL_ITEM_NONE;
            f->output.item = WL_ITEM_NONE;
            f->active = true;
            f->x = x;
            f->y = y;
            f->z = z;
            return f;
        }
    }
    WlFurnace *f = &w->furnaces[w->furnace_count++];
    memset(f, 0, sizeof(*f));
    f->input.item = WL_ITEM_NONE;
    f->fuel.item = WL_ITEM_NONE;
    f->output.item = WL_ITEM_NONE;
    f->active = true;
    f->x = x;
    f->y = y;
    f->z = z;
    return f;
}

bool wl_world_remove_furnace(WlWorld *w, int x, int y, int z,
                             WlSlot out_stacks[3])
{
    if (!w) return false;
    for (int i = 0; i < w->furnace_count; i++) {
        WlFurnace *f = &w->furnaces[i];
        if (!f->active || f->x != x || f->y != y || f->z != z) continue;
        if (out_stacks) {
            out_stacks[0] = f->input;
            out_stacks[1] = f->fuel;
            out_stacks[2] = f->output;
        }
        f->active = false;
        return true;
    }
    return false;
}

const WlFurnace *wl_world_furnaces(const WlWorld *w, int *count)
{
    if (count) *count = w ? w->furnace_count : 0;
    return w ? w->furnaces : NULL;
}

WlFurnace *wl_world_furnaces_mutable(WlWorld *w, int *count)
{
    if (count) *count = w ? w->furnace_count : 0;
    return w ? w->furnaces : NULL;
}

const WlDrop *wl_world_drops(const WlWorld *w, int *count)
{
    if (count) *count = w ? w->drop_count : 0;
    return w ? w->drops : NULL;
}

bool wl_world_save_drops(const WlWorld *w)
{
    if (!w || w->save_dir[0] == '\0') return false;
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), w->save_dir, "drops.dat"))
        return false;

    WlStrBuf sb;
    if (!wl_sb_init(&sb, 512)) return false;
    bool ok = wl_sb_printf(&sb, "# Wildlands dropped items\n");
    for (int i = 0; i < w->drop_count && ok; i++) {
        const WlDrop *d = &w->drops[i];
        ok = wl_sb_printf(&sb, "drop = %u %u %u %.2f %.2f %.2f\n",
                          (unsigned)d->item, (unsigned)d->count, (unsigned)d->dur,
                          d->pos.x, d->pos.y, d->pos.z);
    }
    if (ok) ok = wl_write_file_atomic(path, sb.buf, sb.len);
    wl_sb_free(&sb);
    if (!ok) WL_LOGE("failed to write %s", path);
    return ok;
}

void wl_world_load_drops(WlWorld *w)
{
    if (!w || w->save_dir[0] == '\0') return;
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), w->save_dir, "drops.dat")) return;
    if (!wl_file_exists(path)) return;

    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[128];
    w->drop_count = 0;
    while (w->drop_count < WL_DROPS_MAX && fgets(line, sizeof(line), f)) {
        unsigned item, count, dur;
        float x, y, z;
        if (sscanf(line, "drop = %u %u %u %f %f %f",
                   &item, &count, &dur, &x, &y, &z) == 6 &&
            item != WL_ITEM_NONE && count > 0) {
            WlDrop *d = &w->drops[w->drop_count++];
            memset(d, 0, sizeof(*d));
            d->item = (WlItemId)item;
            d->count = (uint8_t)(count > 64 ? 64 : count);
            d->dur = (uint16_t)dur;
            d->pos = (Vector3){ x, y, z };
            d->age = 10.0f; /* old enough for instant pickup */
        }
    }
    fclose(f);
    WL_LOGI("loaded %d dropped items", w->drop_count);
}

bool wl_world_load_saved_chunk(WlWorld *w, WlChunk *c)
{
    if (!w || !c || w->save_dir[0] == '\0') return false;
    if (!c->generated) return false;
    uint8_t saved[WL_CHUNK_VOL];
    if (!wl_store_load_chunk(w->save_dir, c->cx, c->cz, saved)) return false;
    memcpy(c->blocks, saved, WL_CHUNK_VOL);
    wl_chunk_compute_top(c);
    c->dirty_mesh = true;
    return true;
}

WlWorldStats wl_world_stats(const WlWorld *w)
{
    WlWorldStats s = { 0 };
    if (!w) return s;
    WlWorld *mw = (WlWorld *)w; /* const-cast: brief lock for worker counters */
    s.loaded = w->list_count;
    s.queued_gen = w->gen_q_count;
    for (int i = 0; i < w->list_count; i++) {
        const WlChunk *c = w->list[i];
        if (c->generated) s.generated++;
        if (c->generated && !c->dirty_mesh) s.meshed++;
        s.tris += c->mesh_opaque.tri_count + c->mesh_transparent.tri_count;
    }
    pthread_mutex_lock(&mw->lock);
    s.gen_ms_total = mw->gen_ms_total;
    s.gen_count = mw->gen_count;
    pthread_mutex_unlock(&mw->lock);
    s.mesh_ms_ema = mw->mesh_ms_ema;
    s.mesh_ms_last = mw->mesh_ms_last;
    return s;
}

int wl_world_gamemode(const WlWorld *w) { return w ? w->gamemode : 0; }
const WlWorldGen *wl_world_gen(const WlWorld *w) { return w ? &w->gen : NULL; }
const char *wl_world_name(const WlWorld *w) { return w ? w->name : ""; }
const char *wl_world_save_dir(const WlWorld *w) { return w ? w->save_dir : ""; }

/* ------------------------------------------------------------------ */
/* streaming                                                            */
/* ------------------------------------------------------------------ */

static bool neighbors_ready(const WlWorld *w, int cx, int cz)
{
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dz == 0) continue;
            const WlChunk *n = wl_world_chunk_at((WlWorld *)w, cx + dx, cz + dz);
            if (!n || !n->generated) return false;
        }
    return true;
}

static void generate_inline(WlWorld *w, WlChunk *c)
{
    uint8_t *blocks = malloc(WL_CHUNK_VOL);
    uint8_t *biome = malloc(WL_CHUNK_AREA);
    if (!blocks || !biome) {
        WL_LOGE("inline gen: out of memory for chunk %d,%d", c->cx, c->cz);
        free(blocks);
        free(biome);
        c->queued_gen = false;
        return;
    }
    wl_worldgen_chunk(&w->gen, c->cx, c->cz, blocks, biome);
    WlGenResult r = { c->cx, c->cz, blocks, biome };
    apply_gen_result(w, &r);
    free(blocks);
    free(biome);
}

void wl_world_update_ex(WlWorld *w, float player_x, float player_z,
                        int gen_budget, int mesh_budget)
{
    if (!w) return;
    int pcx = (int)floorf(player_x / (float)WL_CHUNK_W);
    int pcz = (int)floorf(player_z / (float)WL_CHUNK_W);
    int load_r = w->render_distance + 1;
    int unload_r = w->render_distance + 2;

    /* 1. adopt finished chunks from the worker */
    if (w->thread_ok) integrate_done(w, gen_budget);

    /* 2. request/generate chunks ring by ring (near first) */
    int queued = 0;
    int inline_left = w->thread_ok ? 0 : gen_budget;
    for (int r = 0; r <= load_r && (queued < 8 || inline_left > 0); r++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx != -r && dx != r && dz != -r && dz != r) continue; /* ring */
                int cx = pcx + dx, cz = pcz + dz;
                WlChunk *c = wl_world_chunk_at(w, cx, cz);
                if (c && (c->generated || c->queued_gen)) continue;

                if (!w->thread_ok) {
                    if (inline_left <= 0) continue;
                    c = chunk_ensure(w, cx, cz);
                    if (!c) continue;
                    c->queued_gen = true;
                    generate_inline(w, c);
                    inline_left--;
                } else {
                    pthread_mutex_lock(&w->lock);
                    bool full = w->gen_q_count == WL_GEN_QUEUE_CAP;
                    pthread_mutex_unlock(&w->lock);
                    if (full) continue;
                    c = chunk_ensure(w, cx, cz);
                    if (!c) continue;
                    c->queued_gen = true;
                    pthread_mutex_lock(&w->lock);
                    int tail = (w->gen_q_head + w->gen_q_count) % WL_GEN_QUEUE_CAP;
                    w->gen_q[tail].cx = cx;
                    w->gen_q[tail].cz = cz;
                    w->gen_q_count++;
                    pthread_cond_signal(&w->cv_work);
                    pthread_mutex_unlock(&w->lock);
                    queued++;
                }
            }
        }
    }

    /* 3. drop far chunks (saving modified ones) */
    for (int i = w->list_count - 1; i >= 0; i--) {
        WlChunk *c = w->list[i];
        int dx = c->cx - pcx, dz = c->cz - pcz;
        int cheb = (dx < 0 ? -dx : dx);
        int cheb_z = (dz < 0 ? -dz : dz);
        if (cheb_z > cheb) cheb = cheb_z;
        if (cheb <= unload_r) continue;

        if (c->modified && w->save_dir[0] != '\0')
            wl_store_save_chunk(w->save_dir, c->cx, c->cz, c->blocks);
        hash_remove(w, c);
        w->list[i] = w->list[w->list_count - 1];
        w->list_count--;
        wl_chunk_free(c);
    }

    /* 4. mesh dirty chunks that have all their neighbors */
    for (int i = 0; i < w->list_count && mesh_budget > 0; i++) {
        WlChunk *c = w->list[i];
        if (!c->generated || !c->dirty_mesh) continue;
        int dx = c->cx - pcx, dz = c->cz - pcz;
        int cheb = (dx < 0 ? -dx : dx);
        int cheb_z = (dz < 0 ? -dz : dz);
        if (cheb_z > cheb) cheb = cheb_z;
        if (cheb > w->render_distance) continue;
        if (!neighbors_ready(w, c->cx, c->cz)) continue;
        /* relight anything stale in the 3x3 neighborhood first: the mesher
         * bakes light from the chunk and its side neighbors */
        if (c->light_dirty) wl_light_chunk(w, c);
        for (int dz2 = -1; dz2 <= 1; dz2++)
            for (int dx2 = -1; dx2 <= 1; dx2++) {
                if (!dx2 && !dz2) continue;
                WlChunk *nc = wl_world_chunk_at(w, c->cx + dx2, c->cz + dz2);
                if (nc && nc->generated && nc->light_dirty)
                    wl_light_chunk(w, nc);
            }
        double t0 = wl_monotonic_ms();
        wl_mesher_build(w, c);
        w->mesh_ms_last = (float)(wl_monotonic_ms() - t0);
        w->mesh_ms_ema += (w->mesh_ms_last - w->mesh_ms_ema) * 0.2f;
        mesh_budget--;
    }
}

void wl_world_update(WlWorld *w, float player_x, float player_z)
{
    wl_world_update_ex(w, player_x, player_z, 4, 3);
}

int wl_world_pending(WlWorld *w, float player_x, float player_z)
{
    if (!w) return 0;
    int pcx = (int)floorf(player_x / (float)WL_CHUNK_W);
    int pcz = (int)floorf(player_z / (float)WL_CHUNK_W);
    int pending = 0;
    for (int i = 0; i < w->list_count; i++) {
        WlChunk *c = w->list[i];
        int dx = c->cx - pcx, dz = c->cz - pcz;
        int cheb = (dx < 0 ? -dx : dx);
        int cheb_z = (dz < 0 ? -dz : dz);
        if (cheb_z > cheb) cheb = cheb_z;
        if (cheb > w->render_distance) continue;
        if (!c->generated || c->dirty_mesh) pending++;
    }
    return pending;
}
