/* world.h — chunk map, streaming, background generation, time of day. */
#ifndef WL_WORLD_H
#define WL_WORLD_H

#include "wl/wl_common.h"
#include "wl/item.h"    /* WlItemId for drops */
#include "wl/player.h"  /* drop pickup target */
#include "wl/chunk.h"
#include "wl/worldgen.h"

#define WL_WORLD_GAMEMODE_SURVIVAL 0
#define WL_WORLD_GAMEMODE_CREATIVE 1

/* Day length: 24000 ticks at 20 ticks/second = 20 minutes. */
#define WL_DAY_TICKS 24000
#define WL_TICKS_PER_SECOND 20.0f

typedef struct WlWorld WlWorld;

typedef struct {
    char name[WL_NAME_MAX];
    uint64_t seed;
    int type;      /* WlWorldType */
    int gamemode;
} WlWorldParams;

/* A stack of items lying in the world (death drops). */
typedef struct WlDrop {
    WlItemId item;   /* what to pick up                      */
    uint8_t count;   /* stack size                           */
    uint16_t dur;    /* tool durability (0 = fresh)          */
    Vector3 pos;     /* center position, world space         */
    Vector3 vel;     /* physics velocity                     */
    float age;       /* seconds since spawn (pickup delay)   */
    uint32_t seed;   /* per-drop spin/bob phase              */
} WlDrop;

#define WL_DROPS_MAX 256

typedef struct {
    int loaded;        /* chunks in memory          */
    int generated;     /* chunks with terrain data  */
    int meshed;        /* chunks with a fresh mesh  */
    int queued_gen;
    long tris;
    /* timing diagnostics (worker/main-thread measured) */
    double gen_ms_total;  /* terrain generation, worker thread */
    int gen_count;
    float mesh_ms_ema;    /* chunk meshing, main thread */
    float mesh_ms_last;
} WlWorldStats;

WlWorld *wl_world_create(const WlWorldParams *params, const char *save_dir);
/* Frees the world WITHOUT saving (the caller saves explicitly first). */
void     wl_world_destroy(WlWorld *w);

/* Per-frame streaming around the player. */
void wl_world_update(WlWorld *w, float player_x, float player_z);
/* Loading-screen variant with bigger budgets. */
void wl_world_update_ex(WlWorld *w, float player_x, float player_z,
                        int gen_budget, int mesh_budget);
/* Chunks within render distance that still need generation/meshing. */
int  wl_world_pending(WlWorld *w, float player_x, float player_z);

void wl_world_set_render_distance(WlWorld *w, int chunks);
int  wl_world_render_distance(const WlWorld *w);

/* --- block access (world coordinates) --- */
uint8_t wl_world_get_block(WlWorld *w, int x, int y, int z);
/* y < 0 reads bedrock, y >= WL_CHUNK_H reads air; returns air for unloaded
 * chunks. Edits mark the chunk (and border neighbors) for remeshing+saving. */
bool    wl_world_set_block(WlWorld *w, int x, int y, int z, uint8_t id);
bool    wl_world_solid_at(WlWorld *w, int x, int y, int z);
/* Highest opaque block y + 1 in this column (0 when unknown/unloaded). */
int     wl_world_top_at(WlWorld *w, int x, int z);

WlChunk *wl_world_chunk_at(WlWorld *w, int cx, int cz); /* existing only */

/* Dense chunk list for the renderer; pointers stay valid until the next
 * wl_world_update call. */
WlChunk *const *wl_world_chunk_list(WlWorld *w, int *count);

/* --- time of day --- */
uint32_t wl_world_time(const WlWorld *w);
void     wl_world_advance_time(WlWorld *w, float dt);

/* --- persistence --- */
/* Saves every modified chunk into save_dir. Clears the modified flags on
 * success. Returns false if any chunk failed (kept for retry). */
bool wl_world_save_modified(WlWorld *w);
/* Loads a saved chunk over the chunk's generated data (marks it clean).
 * Returns false when no save exists (fresh generation stays). */
bool wl_world_load_saved_chunk(WlWorld *w, WlChunk *c);

/* --- dropped items (death drops) --- */
/* Spawns one drop entity with a small random throw velocity. Returns
 * false when the drop list is full (caller retries later). */
bool wl_world_add_drop(WlWorld *w, WlItemId item, uint8_t count, uint16_t dur,
                       Vector3 pos);
/* Physics + pickup against the player. `pickup` may be NULL to only
 * simulate. Adds what fits back into the inventory and keeps the rest in
 * the world. */
void wl_world_update_drops(WlWorld *w, WlPlayer *p, float dt);
/* Dense list of active drops (valid until the next update call). */
const WlDrop *wl_world_drops(const WlWorld *w, int *count);
/* Saves/loads drops.dat in the world's save dir. */
bool wl_world_save_drops(const WlWorld *w);
void wl_world_load_drops(WlWorld *w);

WlWorldStats wl_world_stats(const WlWorld *w);
int wl_world_gamemode(const WlWorld *w);
const WlWorldGen *wl_world_gen(const WlWorld *w);
const char *wl_world_name(const WlWorld *w);
const char *wl_world_save_dir(const WlWorld *w);

#endif /* WL_WORLD_H */
