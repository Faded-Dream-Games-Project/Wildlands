/* interact.h — block targeting (voxel DDA) and break/place/use logic. */
#ifndef WL_INTERACT_H
#define WL_INTERACT_H

#include "wl/wl_common.h"
#include "raylib.h"

struct WlPlayer;
struct WlWorld;
struct WlConfig;
struct WlAudio;

typedef struct {
    bool valid;
    int x, y, z;    /* targeted block cell */
    int px, py, pz; /* adjacent cell for placement */
    uint8_t id;
} WlTarget;

/* Amanatides & Woo grid traversal. Liquids are not targetable. */
bool wl_raycast(struct WlWorld *w, Vector3 origin, Vector3 dir, float max_dist,
                WlTarget *out);

/* Result codes for the game loop. */
enum {
    WL_INTERACT_NONE = 0,
    WL_INTERACT_OPEN_CRAFT, /* right-clicked a crafting table */
};

int wl_interact_update(struct WlPlayer *p, struct WlWorld *w,
                       const struct WlConfig *cfg, struct WlAudio *audio,
                       float dt, bool input_active);

/* What the player is looking at (for the HUD highlight). */
WlTarget wl_interact_query(struct WlWorld *w, const struct WlPlayer *p);

/* Test seam used by --selftest: while enabled, these values replace the
 * physical mouse-button state. Not used in normal play. */
void wl_interact_debug_mouse(bool left, bool right, bool enable);

#endif /* WL_INTERACT_H */
