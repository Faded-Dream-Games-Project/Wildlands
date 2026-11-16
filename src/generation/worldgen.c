/* worldgen.c — terrain, biomes, rivers, caves, ores, trees, decoration. */
#include "wl/worldgen.h"
#include "wl/block.h"
#include "wl/noise.h"
#include "wl/log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *wl_biome_name(WlBiome b)
{
    switch (b) {
    case WL_BIOME_OCEAN:     return "Ocean";
    case WL_BIOME_BEACH:     return "Beach";
    case WL_BIOME_PLAINS:    return "Plains";
    case WL_BIOME_FOREST:    return "Forest";
    case WL_BIOME_DESERT:    return "Desert";
    case WL_BIOME_MOUNTAINS: return "Mountains";
    case WL_BIOME_SNOWFIELD: return "Snowfield";
    default:                 return "Unknown";
    }
}

void wl_worldgen_init(WlWorldGen *wg, uint64_t seed, int type)
{
    wg->seed = seed;
    wg->type = type;
}

/* ------------------------------------------------------------------ */
/* column shaping                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int height;    /* topmost solid block y */
    WlBiome biome;
} WlColumn;

static float terrain_height_raw(const WlWorldGen *wg, int x, int z)
{
    if (wg->type == WL_WORLDTYPE_SUPERFLAT)
        return 4.0f;

    float fx = (float)x, fz = (float)z;

    /* continents: big slow variation */
    float cont = wl_noise_fbm2(wg->seed ^ 0xA17A5EEDull, fx * 0.0009f, fz * 0.0009f, 4);
    /* rolling hills */
    float hills = wl_noise_fbm2(wg->seed ^ 0xB2C4D6E8ull, fx * 0.0045f, fz * 0.0045f, 4);
    /* ridged mountain mask */
    float mtn_mask = wl_noise_fbm2(wg->seed ^ 0xD4E5F607ull, fx * 0.0007f, fz * 0.0007f, 3);
    float ridge = 1.0f - fabsf(wl_noise_fbm2(wg->seed ^ 0xC3D4E5F6ull, fx * 0.0016f, fz * 0.0016f, 4));

    float h = 66.0f + cont * 24.0f + hills * 6.0f;

    float mtn = mtn_mask - 0.22f;
    if (mtn > 0.0f)
        h += mtn * ridge * 95.0f;

    /* winding rivers: carve toward just below sea level */
    float riv = fabsf(wl_noise_fbm2(wg->seed ^ 0x5157E5ull, fx * 0.0013f, fz * 0.0013f, 3));
    if (riv < 0.045f && h < WL_SEA_LEVEL + 26.0f) {
        float d = (0.045f - riv) / 0.045f;   /* 0..1 across the river band */
        float depth = d * 1.7f;
        if (depth > 1.0f) depth = 1.0f;
        h = h + (WL_SEA_LEVEL - 2.0f - h) * depth;
    }

    return h;
}

static WlColumn shape_column(const WlWorldGen *wg, int x, int z)
{
    WlColumn col;
    float h = terrain_height_raw(wg, x, z);
    int hi = (int)floorf(h);
    if (hi < 1) hi = 1;
    if (hi > WL_WORLD_HEIGHT - 8) hi = WL_WORLD_HEIGHT - 8;
    col.height = hi;

    if (wg->type == WL_WORLDTYPE_SUPERFLAT) {
        col.biome = WL_BIOME_PLAINS;
        return col;
    }

    float fx = (float)x, fz = (float)z;
    float temp = wl_noise_fbm2(wg->seed ^ 0xE5F60715ull, fx * 0.0008f, fz * 0.0008f, 3);
    float moist = wl_noise_fbm2(wg->seed ^ 0xF60715E5ull, fx * 0.0011f, fz * 0.0011f, 3);

    if (hi < WL_SEA_LEVEL - 1) {
        col.biome = WL_BIOME_OCEAN;
    } else if (hi <= WL_SEA_LEVEL + 1) {
        /* shore: sand unless it is cold */
        col.biome = (temp < -0.35f) ? WL_BIOME_SNOWFIELD : WL_BIOME_BEACH;
    } else if (hi > 92) {
        col.biome = WL_BIOME_MOUNTAINS;
    } else if (temp < -0.35f) {
        col.biome = WL_BIOME_SNOWFIELD;
    } else if (temp > 0.32f && moist < -0.08f) {
        col.biome = WL_BIOME_DESERT;
    } else if (moist > 0.03f) {
        col.biome = WL_BIOME_FOREST;
    } else {
        col.biome = WL_BIOME_PLAINS;
    }
    return col;
}

int wl_worldgen_height_at(const WlWorldGen *wg, int x, int z)
{
    return shape_column(wg, x, z).height;
}

WlBiome wl_worldgen_biome_at(const WlWorldGen *wg, int x, int z)
{
    return shape_column(wg, x, z).biome;
}

/* ------------------------------------------------------------------ */
/* caves                                                               */
/* ------------------------------------------------------------------ */

static bool is_cave(const WlWorldGen *wg, int x, int y, int z, int surface)
{
    if (wg->type == WL_WORLDTYPE_SUPERFLAT) return false;
    if (y <= 3 || y > surface) return false;

    /* keep ocean/river floors sealed so they do not drain into caves */
    if (surface < WL_SEA_LEVEL + 2 && y > surface - 6) return false;

    float fx = (float)x, fy = (float)y, fz = (float)z;

    /* spaghetti tunnels: intersection of two low-frequency noise tubes */
    float c1 = wl_noise_fbm3(wg->seed ^ 0xCA11AB1Eull, fx * 0.017f, fy * 0.031f, fz * 0.017f, 2);
    float c2 = wl_noise_fbm3(wg->seed ^ 0xCE11BA22ull, fx * 0.017f, fy * 0.031f, fz * 0.017f, 2);
    if (c1 * c1 + c2 * c2 < 0.010f) return true;

    /* cheese caverns, deep only */
    if (y < 42) {
        float c3 = wl_noise_fbm3(wg->seed ^ 0xC0FFEE66ull, fx * 0.021f, fy * 0.026f, fz * 0.021f, 2);
        if (c3 > 0.58f) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* feature placement (trees, cacti, plants) — all deterministic        */
/* ------------------------------------------------------------------ */

/* Margin needed around a chunk to stamp features rooted in neighbors. */
#define FEATURE_MARGIN 3

typedef struct {
    bool present;
    int base_y;
    int height;      /* trunk height */
    bool birch;
    bool cactus;
} WlFeature;

static WlFeature feature_at(const WlWorldGen *wg, int x, int z)
{
    WlFeature f = { 0 };
    if (wg->type == WL_WORLDTYPE_SUPERFLAT) return f;

    WlColumn col = shape_column(wg, x, z);
    if (col.height <= WL_SEA_LEVEL) return f; /* no trees on beaches/underwater */

    WlRng rng;
    wl_rng_seed(&rng, wg->seed ^ 0x7EE7EE11ull ^ ((uint64_t)(uint32_t)x << 32) ^ (uint32_t)z);
    (void)wl_rng_u32(&rng); /* decorrelate adjacent columns */

    switch (col.biome) {
    case WL_BIOME_FOREST: {
        int chance = 14;
        if (wl_rng_range(&rng, 0, chance) != 0) return f;
        f.present = true;
        f.birch = wl_rng_range(&rng, 0, 3) == 0;
        f.base_y = col.height + 1;
        f.height = f.birch ? wl_rng_range(&rng, 5, 7) : wl_rng_range(&rng, 4, 6);
        break;
    }
    case WL_BIOME_PLAINS: {
        if (wl_rng_range(&rng, 0, 110) != 0) return f;
        f.present = true;
        f.birch = false;
        f.base_y = col.height + 1;
        f.height = wl_rng_range(&rng, 4, 5);
        break;
    }
    case WL_BIOME_SNOWFIELD: {
        if (wl_rng_range(&rng, 0, 48) != 0) return f;
        f.present = true;
        f.birch = true;
        f.base_y = col.height + 1;
        f.height = wl_rng_range(&rng, 5, 7);
        break;
    }
    case WL_BIOME_DESERT: {
        if (wl_rng_range(&rng, 0, 90) != 0) return f;
        f.present = true;
        f.cactus = true;
        f.base_y = col.height + 1;
        f.height = wl_rng_range(&rng, 1, 3);
        break;
    }
    default:
        break;
    }
    return f;
}

/* Writes a block only if the target is air (terrain wins over leaves). */
static void stamp_soft(uint8_t blocks[WL_CHUNK_VOL], int lx, int ly, int lz, uint8_t id)
{
    if (lx < 0 || lx >= WL_CHUNK_W || lz < 0 || lz >= WL_CHUNK_W ||
        ly < 0 || ly >= WL_CHUNK_H)
        return;
    int idx = wl_chunk_index(lx, ly, lz);
    if (blocks[idx] == WL_BLOCK_AIR) blocks[idx] = id;
}

static void stamp_hard(uint8_t blocks[WL_CHUNK_VOL], int lx, int ly, int lz, uint8_t id)
{
    if (lx < 0 || lx >= WL_CHUNK_W || lz < 0 || lz >= WL_CHUNK_W ||
        ly < 0 || ly >= WL_CHUNK_H)
        return;
    blocks[wl_chunk_index(lx, ly, lz)] = id;
}

static void stamp_tree(uint8_t blocks[WL_CHUNK_VOL], int wx, int wz, int chunk_x0,
                       int chunk_z0, const WlFeature *f)
{
    int lx = wx - chunk_x0;
    int lz = wz - chunk_z0;
    uint8_t log = f->birch ? WL_BLOCK_BIRCH_LOG : WL_BLOCK_LOG;
    uint8_t leaves = f->birch ? WL_BLOCK_BIRCH_LEAVES : WL_BLOCK_LEAVES;

    /* trunk (overwrites anything soft like decor) */
    for (int i = 0; i < f->height; i++)
        stamp_hard(blocks, lx, f->base_y + i, lz, log);

    /* canopy: two wide layers, then a small cap */
    int top = f->base_y + f->height - 1;
    for (int dy = -2; dy <= 0; dy++) {
        int r = (dy <= -1) ? 2 : 1;
        for (int dx = -r; dx <= r; dx++) {
            for (int dz = -r; dz <= r; dz++) {
                if (dx == 0 && dz == 0 && dy < 0) continue; /* trunk space */
                /* round corners off */
                if (abs(dx) == r && abs(dz) == r && (dy == 0 || ((dx + dz) & 1)))
                    continue;
                stamp_soft(blocks, lx + dx, top + dy, lz + dz, leaves);
            }
        }
    }
    stamp_soft(blocks, lx, top + 1, lz, leaves);
}

static void stamp_cactus(uint8_t blocks[WL_CHUNK_VOL], int wx, int wz, int chunk_x0,
                         int chunk_z0, const WlFeature *f)
{
    for (int i = 0; i < f->height; i++)
        stamp_hard(blocks, wx - chunk_x0, f->base_y + i, wz - chunk_z0, WL_BLOCK_CACTUS);
}

/* ------------------------------------------------------------------ */
/* ores                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t block;
    int tries;
    int y_min, y_max;
    int blob_min, blob_max;
} WlOreDef;

static const WlOreDef k_ores[] = {
    { WL_BLOCK_COAL_ORE,    14, 5, 92, 4, 8 },
    { WL_BLOCK_IRON_ORE,     9, 5, 56, 3, 6 },
    { WL_BLOCK_GOLD_ORE,     3, 5, 30, 2, 4 },
    { WL_BLOCK_DIAMOND_ORE,  2, 5, 16, 1, 3 },
};

static void gen_ores(const WlWorldGen *wg, uint8_t blocks[WL_CHUNK_VOL], int cx, int cz)
{
    for (size_t o = 0; o < WL_ARRAY_COUNT(k_ores); o++) {
        const WlOreDef *def = &k_ores[o];
        WlRng rng;
        wl_rng_seed(&rng, wg->seed ^ 0x0EE5EE5Eull ^ ((uint64_t)(uint32_t)cx << 40) ^
                             ((uint64_t)(uint32_t)cz << 8) ^ (uint64_t)o);

        for (int t = 0; t < def->tries; t++) {
            int x = wl_rng_range(&rng, 0, WL_CHUNK_W - 1);
            int z = wl_rng_range(&rng, 0, WL_CHUNK_W - 1);
            int y = wl_rng_range(&rng, def->y_min, def->y_max);
            int blob = wl_rng_range(&rng, def->blob_min, def->blob_max);

            for (int b = 0; b < blob; b++) {
                if (x >= 0 && x < WL_CHUNK_W && z >= 0 && z < WL_CHUNK_W &&
                    y >= 1 && y < WL_CHUNK_H) {
                    int idx = wl_chunk_index(x, y, z);
                    if (blocks[idx] == WL_BLOCK_STONE)
                        blocks[idx] = def->block;
                }
                /* short random walk */
                switch (wl_rng_range(&rng, 0, 5)) {
                case 0: x += 1; break;
                case 1: x -= 1; break;
                case 2: y += 1; break;
                case 3: y -= 1; break;
                case 4: z += 1; break;
                default: z -= 1; break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* chunk assembly                                                      */
/* ------------------------------------------------------------------ */

void wl_worldgen_chunk(const WlWorldGen *wg, int cx, int cz,
                       uint8_t blocks[WL_CHUNK_VOL], uint8_t biome[WL_CHUNK_AREA])
{
    const int chunk_x0 = cx * WL_CHUNK_W;
    const int chunk_z0 = cz * WL_CHUNK_W;

    /* extended column map including the feature margin */
    const int ext = WL_CHUNK_W + 2 * FEATURE_MARGIN;
    WlColumn cols[ext * ext];
    for (int z = 0; z < ext; z++)
        for (int x = 0; x < ext; x++)
            cols[z * ext + x] = shape_column(wg, chunk_x0 + x - FEATURE_MARGIN,
                                             chunk_z0 + z - FEATURE_MARGIN);

    memset(blocks, 0, (size_t)WL_CHUNK_VOL * sizeof(uint8_t));

    /* --- terrain fill --- */
    for (int z = 0; z < WL_CHUNK_W; z++) {
        for (int x = 0; x < WL_CHUNK_W; x++) {
            const WlColumn *col = &cols[(z + FEATURE_MARGIN) * ext + (x + FEATURE_MARGIN)];
            int h = col->height;
            biome[z * WL_CHUNK_W + x] = (uint8_t)col->biome;

            for (int y = 0; y <= h; y++) {
                uint8_t id = WL_BLOCK_STONE;

                if (y == 0 || (y == 1 && ((x * 7 + z * 13 + cx * 3 + cz * 5) & 3) == 0))
                    id = WL_BLOCK_BEDROCK;
                else if (col->biome == WL_BIOME_DESERT && y > h - 3)
                    id = WL_BLOCK_SAND;
                else if (col->biome == WL_BIOME_DESERT && y > h - 7)
                    id = WL_BLOCK_SANDSTONE;
                else if (col->biome == WL_BIOME_BEACH && y > h - 3)
                    id = WL_BLOCK_SAND;
                else if (col->biome == WL_BIOME_OCEAN && y > h - 3)
                    id = (h > WL_SEA_LEVEL - 8) ? WL_BLOCK_SAND : WL_BLOCK_GRAVEL;
                else if (col->biome == WL_BIOME_SNOWFIELD && y > h - 4)
                    id = WL_BLOCK_DIRT;
                else if (col->biome == WL_BIOME_MOUNTAINS && y > h - 3)
                    id = (h > 106) ? WL_BLOCK_SNOW : (h > 96 ? WL_BLOCK_STONE : WL_BLOCK_DIRT);
                else if (y > h - 4)
                    id = WL_BLOCK_DIRT;

                /* surface block */
                if (y == h && h > 0) {
                    switch (col->biome) {
                    case WL_BIOME_DESERT: id = WL_BLOCK_SAND; break;
                    case WL_BIOME_BEACH:  id = WL_BLOCK_SAND; break;
                    case WL_BIOME_OCEAN:  id = WL_BLOCK_SAND; break;
                    case WL_BIOME_SNOWFIELD: id = WL_BLOCK_SNOW_GRASS; break;
                    case WL_BIOME_MOUNTAINS:
                        id = (h > 106) ? WL_BLOCK_SNOW : (h > 96 ? WL_BLOCK_STONE : WL_BLOCK_GRASS);
                        break;
                    default: id = WL_BLOCK_GRASS; break;
                    }
                }

                blocks[wl_chunk_index(x, y, z)] = id;
            }

            /* water fill (superflat worlds have no ocean) */
            if (wg->type != WL_WORLDTYPE_SUPERFLAT && h < WL_SEA_LEVEL) {
                for (int y = h + 1; y <= WL_SEA_LEVEL; y++)
                    blocks[wl_chunk_index(x, y, z)] = WL_BLOCK_WATER;
                /* frozen surface in cold biomes */
                if (col->biome == WL_BIOME_SNOWFIELD)
                    blocks[wl_chunk_index(x, WL_SEA_LEVEL, z)] = WL_BLOCK_ICE;
            }
        }
    }

    /* --- caves --- */
    if (wg->type != WL_WORLDTYPE_SUPERFLAT) {
        for (int z = 0; z < WL_CHUNK_W; z++) {
            for (int x = 0; x < WL_CHUNK_W; x++) {
                const WlColumn *col = &cols[(z + FEATURE_MARGIN) * ext + (x + FEATURE_MARGIN)];
                for (int y = 4; y <= col->height; y++) {
                    if (is_cave(wg, chunk_x0 + x, y, chunk_z0 + z, col->height))
                        blocks[wl_chunk_index(x, y, z)] = WL_BLOCK_AIR;
                }
            }
        }
    }

    /* --- ores (only replace stone, so caves stay clean) --- */
    if (wg->type != WL_WORLDTYPE_SUPERFLAT)
        gen_ores(wg, blocks, cx, cz);

    /* --- features (trees/cacti) incl. neighbor margin --- */
    for (int z = 0; z < ext; z++) {
        for (int x = 0; x < ext; x++) {
            int wx = chunk_x0 + x - FEATURE_MARGIN;
            int wz = chunk_z0 + z - FEATURE_MARGIN;
            WlFeature f = feature_at(wg, wx, wz);
            if (!f.present) continue;
            if (f.cactus) stamp_cactus(blocks, wx, wz, chunk_x0, chunk_z0, &f);
            else          stamp_tree(blocks, wx, wz, chunk_x0, chunk_z0, &f);
        }
    }

    /* --- ground decor on grass (flowers, tall grass) --- */
    for (int z = 0; z < WL_CHUNK_W; z++) {
        for (int x = 0; x < WL_CHUNK_W; x++) {
            const WlColumn *col = &cols[(z + FEATURE_MARGIN) * ext + (x + FEATURE_MARGIN)];
            if (col->biome != WL_BIOME_PLAINS && col->biome != WL_BIOME_FOREST)
                continue;
            int h = col->height;
            if (h + 1 >= WL_CHUNK_H) continue;
            int idx_top = wl_chunk_index(x, h, z);
            int idx_dec = wl_chunk_index(x, h + 1, z);
            if (blocks[idx_top] != WL_BLOCK_GRASS || blocks[idx_dec] != WL_BLOCK_AIR)
                continue;

            WlRng rng;
            wl_rng_seed(&rng, wg->seed ^ 0xDEC0A7Eull ^ ((uint64_t)(uint32_t)(chunk_x0 + x) << 32) ^
                                 (uint32_t)(chunk_z0 + z));
            uint32_t roll = wl_rng_u32(&rng);
            if (roll % 100u < 12u)
                blocks[idx_dec] = WL_BLOCK_TALLGRASS;
            else if (roll % 1000u < 16u)
                blocks[idx_dec] = WL_BLOCK_FLOWER_RED;
            else if (roll % 1000u < 32u)
                blocks[idx_dec] = WL_BLOCK_FLOWER_YELLOW;
        }
    }
}

bool wl_worldgen_find_spawn(const WlWorldGen *wg, int *out_x, int *out_z)
{
    /* spiral out from the origin until we find dry, walkable land */
    for (int r = 0; r <= 96; r += 4) {
        for (int a = 0; a < 16; a++) {
            float ang = (float)a * (6.2831853f / 16.0f);
            int x = (int)(cosf(ang) * r);
            int z = (int)(sinf(ang) * r);
            WlColumn col = shape_column(wg, x, z);
            if (col.height > WL_SEA_LEVEL + 1 &&
                col.biome != WL_BIOME_OCEAN && col.biome != WL_BIOME_BEACH) {
                *out_x = x;
                *out_z = z;
                return true;
            }
        }
    }
    *out_x = 0;
    *out_z = 0;
    return false;
}
