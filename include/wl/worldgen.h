/* worldgen.h — deterministic procedural terrain generation.
 *
 * Thread-safety: WlWorldGen is immutable after init; every random decision
 * is derived from (seed, world coordinates), so chunks generate identically
 * regardless of order, thread or session. */
#ifndef WL_WORLDGEN_H
#define WL_WORLDGEN_H

#include "wl/wl_common.h"
#include "wl/chunk.h"

#define WL_SEA_LEVEL 62
#define WL_WORLD_HEIGHT WL_CHUNK_H /* 128 */

typedef enum {
    WL_BIOME_OCEAN = 0,
    WL_BIOME_BEACH,
    WL_BIOME_PLAINS,
    WL_BIOME_FOREST,
    WL_BIOME_DESERT,
    WL_BIOME_MOUNTAINS,
    WL_BIOME_SNOWFIELD,
    WL_BIOME_COUNT
} WlBiome;

typedef enum {
    WL_WORLDTYPE_STANDARD = 0,
    WL_WORLDTYPE_SUPERFLAT
} WlWorldType;

typedef struct {
    uint64_t seed;
    int type; /* WlWorldType */
} WlWorldGen;

const char *wl_biome_name(WlBiome b);

void wl_worldgen_init(WlWorldGen *wg, uint64_t seed, int type);

/* Generates a full chunk. blocks/biome are caller-owned (chunk-sized).
 * Only touches [0, WL_CHUNK_VOL) / [0, WL_CHUNK_AREA). */
void wl_worldgen_chunk(const WlWorldGen *wg, int cx, int cz,
                       uint8_t blocks[WL_CHUNK_VOL], uint8_t biome[WL_CHUNK_AREA]);

/* Surface height (topmost solid block y) — identical to what chunk gen uses. */
int wl_worldgen_height_at(const WlWorldGen *wg, int x, int z);
WlBiome wl_worldgen_biome_at(const WlWorldGen *wg, int x, int z);

/* Finds a land spawn near the origin (returns false if none within range). */
bool wl_worldgen_find_spawn(const WlWorldGen *wg, int *out_x, int *out_z);

#endif /* WL_WORLDGEN_H */
