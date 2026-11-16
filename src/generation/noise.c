/* noise.c — integer-hash value noise (quintic fade), fBm, splitmix64 RNG. */
#include "wl/noise.h"

#include <math.h>

/* --- hashing --- */

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t hash_coords(uint64_t seed, int32_t a, int32_t b, int32_t c)
{
    uint32_t h = (uint32_t)(seed ^ (seed >> 32));
    h = mix32(h ^ (uint32_t)a);
    h = mix32(h ^ (uint32_t)b);
    h = mix32(h ^ (uint32_t)c);
    return h;
}

double wl_noise_hash2(uint64_t seed, int32_t x, int32_t y)
{
    return (double)(hash_coords(seed, x, y, 0) >> 8) * (1.0 / 16777216.0);
}

double wl_noise_hash3(uint64_t seed, int32_t x, int32_t y, int32_t z)
{
    return (double)(hash_coords(seed, x, y, z) >> 8) * (1.0 / 16777216.0);
}

/* --- value noise --- */

static inline float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float wl_noise_value2(uint64_t seed, float x, float y)
{
    int32_t xi = (int32_t)floorf(x);
    int32_t yi = (int32_t)floorf(y);
    float tx = fade(x - (float)xi);
    float ty = fade(y - (float)yi);

    float v00 = (float)wl_noise_hash2(seed, xi, yi) * 2.0f - 1.0f;
    float v10 = (float)wl_noise_hash2(seed, xi + 1, yi) * 2.0f - 1.0f;
    float v01 = (float)wl_noise_hash2(seed, xi, yi + 1) * 2.0f - 1.0f;
    float v11 = (float)wl_noise_hash2(seed, xi + 1, yi + 1) * 2.0f - 1.0f;

    return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

float wl_noise_value3(uint64_t seed, float x, float y, float z)
{
    int32_t xi = (int32_t)floorf(x);
    int32_t yi = (int32_t)floorf(y);
    int32_t zi = (int32_t)floorf(z);
    float tx = fade(x - (float)xi);
    float ty = fade(y - (float)yi);
    float tz = fade(z - (float)zi);

    float v000 = (float)wl_noise_hash3(seed, xi, yi, zi) * 2.0f - 1.0f;
    float v100 = (float)wl_noise_hash3(seed, xi + 1, yi, zi) * 2.0f - 1.0f;
    float v010 = (float)wl_noise_hash3(seed, xi, yi + 1, zi) * 2.0f - 1.0f;
    float v110 = (float)wl_noise_hash3(seed, xi + 1, yi + 1, zi) * 2.0f - 1.0f;
    float v001 = (float)wl_noise_hash3(seed, xi, yi, zi + 1) * 2.0f - 1.0f;
    float v101 = (float)wl_noise_hash3(seed, xi + 1, yi, zi + 1) * 2.0f - 1.0f;
    float v011 = (float)wl_noise_hash3(seed, xi, yi + 1, zi + 1) * 2.0f - 1.0f;
    float v111 = (float)wl_noise_hash3(seed, xi + 1, yi + 1, zi + 1) * 2.0f - 1.0f;

    float x00 = lerp(v000, v100, tx);
    float x10 = lerp(v010, v110, tx);
    float x01 = lerp(v001, v101, tx);
    float x11 = lerp(v011, v111, tx);

    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

/* --- fBm --- */

float wl_noise_fbm2(uint64_t seed, float x, float y, int octaves)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = 1.0f;

    for (int i = 0; i < octaves; i++) {
        sum += wl_noise_value2(seed + (uint64_t)i * 0x9e3779b9u, x * freq, y * freq) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

float wl_noise_fbm3(uint64_t seed, float x, float y, float z, int octaves)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = 1.0f;

    for (int i = 0; i < octaves; i++) {
        sum += wl_noise_value3(seed + (uint64_t)i * 0x9e3779b9u,
                               x * freq, y * freq, z * freq) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

/* --- RNG (splitmix64) --- */

void wl_rng_seed(WlRng *rng, uint64_t seed)
{
    rng->state = seed;
}

uint32_t wl_rng_u32(WlRng *rng)
{
    rng->state += 0x9e3779b97f4a7c15ull;
    uint64_t z = rng->state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z = z ^ (z >> 31);
    return (uint32_t)(z >> 32);
}

float wl_rng_float(WlRng *rng)
{
    return (float)(wl_rng_u32(rng) >> 8) * (1.0f / 16777216.0f);
}

int wl_rng_range(WlRng *rng, int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(wl_rng_u32(rng) % (uint32_t)(hi - lo + 1));
}
