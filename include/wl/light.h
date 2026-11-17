/* light.h — voxel lighting: skylight + block light, BFS propagation. */
#ifndef WL_LIGHT_H
#define WL_LIGHT_H

#include "wl/wl_common.h"

struct WlWorld;
struct WlChunk;

/* Recomputes the chunk's light array from scratch:
 *  - skylight columns (15 down to the first opaque block), spread by BFS
 *  - block light from emissive blocks (torches, lit furnaces)
 *  - border imports from the four side neighbors' current light
 * When a border's light changed, the neighbor chunks get light_dirty +
 * dirty_mesh so the propagation cascades across chunk borders. */
void wl_light_chunk(struct WlWorld *w, struct WlChunk *c);

/* Combined light level 0..15 (max of sky and block channel) at a world
 * position; 15 for unloaded columns (fail bright, never fail dark). */
uint8_t wl_light_level(struct WlWorld *w, int x, int y, int z);

/* Sky/block channels separately (for shading that must respect time of
 * day on the sky channel only). */
uint8_t wl_light_sky(struct WlWorld *w, int x, int y, int z);
uint8_t wl_light_block(struct WlWorld *w, int x, int y, int z);

#endif /* WL_LIGHT_H */
