/* noise.h — deterministic, seedable value noise, fBm and a seeded RNG.
 *
 * All functions are pure (no global state) and therefore thread-safe:
 * the world generation thread may call them freely. Integer hashing keeps
 * results identical across runs and platforms. */
#ifndef WL_NOISE_H
#define WL_NOISE_H

#include "wl/wl_common.h"

/* lattice hashes -> [0, 1) */
double wl_noise_hash2(uint64_t seed, int32_t x, int32_t y);
double wl_noise_hash3(uint64_t seed, int32_t x, int32_t y, int32_t z);

/* smooth value noise -> [-1, 1] */
float wl_noise_value2(uint64_t seed, float x, float y);
float wl_noise_value3(uint64_t seed, float x, float y, float z);

/* fractal Brownian motion -> roughly [-1, 1] */
float wl_noise_fbm2(uint64_t seed, float x, float y, int octaves);
float wl_noise_fbm3(uint64_t seed, float x, float y, float z, int octaves);

/* seeded RNG for scattered features (trees, ore veins) */
typedef struct {
    uint64_t state;
} WlRng;

void     wl_rng_seed(WlRng *rng, uint64_t seed);
uint32_t wl_rng_u32(WlRng *rng);
float    wl_rng_float(WlRng *rng);              /* [0, 1) */
int      wl_rng_range(WlRng *rng, int lo, int hi); /* inclusive [lo, hi] */

#endif /* WL_NOISE_H */
