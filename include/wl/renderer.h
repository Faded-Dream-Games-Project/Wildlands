/* renderer.h — camera, sky, frustum-culled chunk rendering, clouds, water. */
#ifndef WL_RENDERER_H
#define WL_RENDERER_H

#include "wl/wl_common.h"
#include "raylib.h"
#include "wl/assets.h"

struct WlWorld;
struct WlPlayer;
struct WlConfig;

#define WL_CLOUD_HEIGHT 118.0f

typedef struct WlRenderer {
    Camera3D cam;
    Material mat_opaque;   /* atlas + chunk shader (solid + cutout) */
    Material mat_water;    /* atlas + chunk shader (alpha blend pass) */
    Material mat_cloud;    /* cloud texture + chunk shader */
    Texture2D atlas;
    WlAssets *assets;

    Vector4 frustum[6];
    Color sky_top, sky_horizon, fog_color;
    float fog_near, fog_far;
    float light_scale;

    Mesh cloud_mesh;
    bool cloud_ok;

    int drawn_chunks;
    long drawn_tris;
} WlRenderer;

void wl_renderer_init(WlRenderer *r, WlAssets *assets);
void wl_renderer_shutdown(WlRenderer *r);

/* Updates camera and sky/fog for this frame. */
void wl_renderer_begin(WlRenderer *r, const struct WlPlayer *p,
                       const struct WlConfig *cfg, uint32_t ticks,
                       bool cam_underwater, int render_distance);

/* Full world pass: call between BeginDrawing() and EndDrawing().
 * Draws the sky gradient, celestial bodies, chunks, clouds and water. */
void wl_renderer_draw_world(WlRenderer *r, struct WlWorld *w,
                            const struct WlConfig *cfg, uint32_t ticks,
                            bool cam_underwater);

Vector3 wl_renderer_cam_pos(const WlRenderer *r);
/* Current light multiplier for UI effects (darken HUD at night). */
float wl_renderer_light_scale(const WlRenderer *r);

#endif /* WL_RENDERER_H */
