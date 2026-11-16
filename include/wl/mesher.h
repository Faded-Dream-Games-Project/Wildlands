/* mesher.h — chunk mesh building (face culling, AO, baked shading). */
#ifndef WL_MESHER_H
#define WL_MESHER_H

#include "wl/wl_common.h"

struct WlWorld;
struct WlChunk;

/* Builds opaque+cutout and transparent (water) meshes for the chunk.
 * Reads neighbor blocks through the world for correct border culling.
 * Main thread only (uploads GPU resources). */
void wl_mesher_build(struct WlWorld *w, struct WlChunk *c);

/* UV rectangle of an atlas tile (16x16 grid, v grows downward). */
void wl_tile_uv(int tile, float *u0, float *v0, float *u1, float *v1);

#endif /* WL_MESHER_H */
