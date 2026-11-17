/* light.c — flood-fill voxel lighting (skylight + block light).
 *
 * Light is stored per chunk as one byte per voxel: high nibble skylight,
 * low nibble block light, each 0..15. It is derived data — torches and
 * lit furnaces are blocks, so nothing light-related is persisted.
 *
 * Each chunk relight is a full recompute: seed sky columns and emissive
 * voxels, import the four side neighbors' border light, then run two BFS
 * passes. Border checksums detect whether the relight changed what
 * neighbors can see; if so they are marked light+mesh dirty and pick the
 * change up on their next mesh pass. */
#include "wl/light.h"
#include "wl/world.h"
#include "wl/chunk.h"
#include "wl/block.h"
#include "wl/log.h"

#include <string.h>

/* BFS queue: indices into the chunk's voxel array. A full relight can
 * touch every voxel a few times over; the ring simply overflows-safe
 * stops (light is monotonic — a dropped re-visit is re-enqueued by a
 * later wave in practice; worst case one frame of slightly stale light). */
#define LIGHT_QUEUE_CAP (WL_CHUNK_VOL * 2)
static int32_t s_queue[LIGHT_QUEUE_CAP];
static int s_qhead, s_qtail;
static bool s_queue_overflow;

static void q_reset(void)
{
    s_qhead = s_qtail = 0;
    s_queue_overflow = false;
}

static void q_push(int32_t v)
{
    if (s_qtail >= LIGHT_QUEUE_CAP) {
        if (s_qhead == 0) { s_queue_overflow = true; return; } /* full */
        /* compact */
        memmove(s_queue, s_queue + s_qhead, (size_t)(s_qtail - s_qhead) * sizeof(int32_t));
        s_qtail -= s_qhead;
        s_qhead = 0;
    }
    s_queue[s_qtail++] = v;
}

static bool q_pop(int32_t *out)
{
    if (s_qhead >= s_qtail) return false;
    *out = s_queue[s_qhead++];
    return true;
}

static inline uint8_t sky_of(uint8_t l) { return (uint8_t)(l >> 4); }
static inline uint8_t block_of(uint8_t l) { return (uint8_t)(l & 0x0F); }
static inline uint8_t pack(uint8_t sky, uint8_t blk)
{
    return (uint8_t)((sky << 4) | blk);
}

/* ------------------------------------------------------------------ */
/* chunk relight                                                       */
/* ------------------------------------------------------------------ */

/* border checksums: N (z=0), E (x=15), S (z=15), W (x=0) */
static uint32_t border_checksum(const WlChunk *c, int side)
{
    uint32_t sum = 2166136261u;
    for (int a = 0; a < WL_CHUNK_W; a++) {
        for (int y = 0; y < WL_CHUNK_H; y += 2) { /* stride 2: cheap and stable */
            int x, z;
            switch (side) {
            case 0: x = a;      z = 0;      break;
            case 1: x = 15;     z = a;      break;
            case 2: x = a;      z = 15;     break;
            default: x = 0;     z = a;      break;
            }
            sum = (sum ^ c->light[wl_chunk_index(x, y, z)]) * 16777619u;
        }
    }
    return sum;
}

void wl_light_chunk(struct WlWorld *w, WlChunk *c)
{
    if (!w || !c || !c->generated) return;

    memset(c->light, 0, sizeof(c->light));
    q_reset();

    /* ---- seeds: sky columns ---- */
    for (int z = 0; z < WL_CHUNK_W; z++) {
        for (int x = 0; x < WL_CHUNK_W; x++) {
            for (int y = WL_CHUNK_H - 1; y >= 0; y--) {
                uint8_t id = c->blocks[wl_chunk_index(x, y, z)];
                if (wl_block_is_opaque(id)) break;
                c->light[wl_chunk_index(x, y, z)] = pack(15, 0);
            }
        }
    }

    /* ---- seeds: emissive blocks (light themselves; opaque emitters
     * also seed their non-opaque neighbors directly) ---- */
    for (int y = 0; y < WL_CHUNK_H; y++) {
        for (int z = 0; z < WL_CHUNK_W; z++) {
            for (int x = 0; x < WL_CHUNK_W; x++) {
                int i = wl_chunk_index(x, y, z);
                uint8_t id = c->blocks[i];
                const WlBlock *def = wl_block_get(id);
                if (!def || def->light == 0) continue;

                c->light[i] = pack(sky_of(c->light[i]), def->light);
                if (wl_block_is_opaque(id)) {
                    static const int dirs[6][3] = {
                        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
                    };
                    for (int d = 0; d < 6; d++) {
                        int nx = x + dirs[d][0], ny = y + dirs[d][1],
                            nz = z + dirs[d][2];
                        if (nx < 0 || nx >= WL_CHUNK_W || nz < 0 ||
                            nz >= WL_CHUNK_W || ny < 0 || ny >= WL_CHUNK_H)
                            continue;
                        int ni = wl_chunk_index(nx, ny, nz);
                        if (wl_block_is_opaque(c->blocks[ni])) continue;
                        uint8_t nl = def->light > 1 ? (uint8_t)(def->light - 1) : 0;
                        if (block_of(c->light[ni]) < nl) {
                            c->light[ni] = pack(sky_of(c->light[ni]), nl);
                        }
                    }
                }
            }
        }
    }

    /* ---- seeds: border imports from side neighbors ---- */
    static const int side_dx[4] = { 0, 1, 0, -1 };
    static const int side_dz[4] = { -1, 0, 1, 0 };
    for (int s = 0; s < 4; s++) {
        WlChunk *n = wl_world_chunk_at(w, c->cx + side_dx[s], c->cz + side_dz[s]);
        if (!n || !n->generated) continue;
        for (int a = 0; a < WL_CHUNK_W; a++) {
            for (int y = 0; y < WL_CHUNK_H; y++) {
                /* neighbor border voxel -> my border voxel */
                int nx, nz, mx, mz;
                switch (s) {
                case 0: nx = a;  nz = 15; mx = a;  mz = 0;  break; /* my -Z side */
                case 1: nx = 0;  nz = a;  mx = 15; mz = a;  break; /* my +X side */
                case 2: nx = a;  nz = 0;  mx = a;  mz = 15; break; /* my +Z side */
                default: nx = 15; nz = a; mx = 0;  mz = a;  break; /* my -X side */
                }
                int mi = wl_chunk_index(mx, y, mz);
                if (wl_block_is_opaque(c->blocks[mi])) continue;
                uint8_t nl = n->light[wl_chunk_index(nx, y, nz)];
                uint8_t ns = sky_of(nl), nb = block_of(nl);
                uint8_t ms = ns > 1 ? (uint8_t)(ns - 1) : 0;
                uint8_t mb = nb > 1 ? (uint8_t)(nb - 1) : 0;
                uint8_t cur = c->light[mi];
                if (ms > sky_of(cur) || mb > block_of(cur)) {
                    c->light[mi] = pack(ms > sky_of(cur) ? ms : sky_of(cur),
                                        mb > block_of(cur) ? mb : block_of(cur));
                }
            }
        }
    }

    /* ---- BFS: spread both channels in one pass ---- */
    for (int i = 0; i < WL_CHUNK_VOL; i++) q_push((int32_t)i);

    static const int dirs[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
    };
    int32_t vi;
    while (q_pop(&vi)) {
        int x = vi % WL_CHUNK_W;
        int z = (vi / WL_CHUNK_W) % WL_CHUNK_W;
        int y = vi / (WL_CHUNK_W * WL_CHUNK_W);
        uint8_t l = c->light[vi];
        uint8_t s = sky_of(l), b = block_of(l);
        if (s <= 1 && b <= 1) continue; /* nothing to give */

        for (int d = 0; d < 6; d++) {
            int nx = x + dirs[d][0], ny = y + dirs[d][1], nz = z + dirs[d][2];
            if (nx < 0 || nx >= WL_CHUNK_W || nz < 0 || nz >= WL_CHUNK_W ||
                ny < 0 || ny >= WL_CHUNK_H)
                continue;
            int ni = wl_chunk_index(nx, ny, nz);
            if (wl_block_is_opaque(c->blocks[ni])) continue;

            uint8_t cur = c->light[ni];
            uint8_t cs = sky_of(cur), cb = block_of(cur);
            /* skylight going straight down at full strength stays full */
            uint8_t give_s = (d == 3 && s == 15) ? 15 : (uint8_t)(s > 1 ? s - 1 : 0);
            uint8_t give_b = (uint8_t)(b > 1 ? b - 1 : 0);

            if (give_s > cs || give_b > cb) {
                c->light[ni] = pack(give_s > cs ? give_s : cs,
                                    give_b > cb ? give_b : cb);
                q_push((int32_t)ni);
            }
        }
    }

    if (s_queue_overflow)
        WL_LOGD("light: queue overflow in chunk %d,%d (one frame stale)",
                c->cx, c->cz);

    /* ---- cascade: mark neighbors when their view of us changed ---- */
    for (int s = 0; s < 4; s++) {
        uint32_t sum = border_checksum(c, s);
        if (sum != c->border_sum[s]) {
            c->border_sum[s] = sum;
            WlChunk *n = wl_world_chunk_at(w, c->cx + side_dx[s],
                                           c->cz + side_dz[s]);
            if (n && n->generated) {
                n->light_dirty = true;
                n->dirty_mesh = true;
            }
        }
    }

    c->light_dirty = false;
}

/* ------------------------------------------------------------------ */
/* world-space getters                                                 */
/* ------------------------------------------------------------------ */

static uint8_t light_at(struct WlWorld *w, int x, int y, int z, bool sky)
{
    if (y < 0) return 0;
    if (y >= WL_CHUNK_H) return sky ? 15 : 0;
    WlChunk *c = wl_world_chunk_at(w, x >> 4, z >> 4);
    if (!c || !c->generated) return sky ? 15 : 0; /* unloaded: assume lit */
    uint8_t l = c->light[wl_chunk_index(x & 15, y, z & 15)];
    return sky ? sky_of(l) : block_of(l);
}

uint8_t wl_light_level(struct WlWorld *w, int x, int y, int z)
{
    uint8_t s = light_at(w, x, y, z, true);
    uint8_t b = light_at(w, x, y, z, false);
    return s > b ? s : b;
}

uint8_t wl_light_sky(struct WlWorld *w, int x, int y, int z)
{
    return light_at(w, x, y, z, true);
}

uint8_t wl_light_block(struct WlWorld *w, int x, int y, int z)
{
    return light_at(w, x, y, z, false);
}
