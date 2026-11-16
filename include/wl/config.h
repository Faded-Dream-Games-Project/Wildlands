/* config.h — all user settings, persisted to config/options.txt. */
#ifndef WL_CONFIG_H
#define WL_CONFIG_H

#include "wl/wl_common.h"

typedef struct WlConfig {
    /* video */
    int   window_width;
    int   window_height;
    bool  fullscreen;
    bool  borderless;
    bool  vsync;
    bool  msaa;               /* 4x MSAA; applied on next start (window hint) */
    int   fps_limit;          /* 0 = unlimited */
    int   render_distance;    /* chunk radius */
    int   graphics_quality;   /* 0 = low, 1 = medium, 2 = high */
    int   fov;                /* degrees */
    bool  view_bobbing;
    float brightness;         /* 0.5 .. 1.5 */
    float ui_scale;           /* 0.75 .. 2.0 */
    /* audio */
    float vol_master;
    float vol_music;
    float vol_sfx;
    float vol_ambient;
    /* controls */
    float mouse_sensitivity;  /* 0.2 .. 3.0 */
    bool  invert_mouse;
    bool  mouse_smoothing;
    /* gameplay */
    int   difficulty;         /* 0 peaceful, 1 easy, 2 normal, 3 hard */
    bool  smooth_camera;      /* eased eye-height transitions */
    int   autosave_interval;  /* seconds; 0 = off */
    bool  show_debug_info;
    bool  pause_on_focus_loss;
} WlConfig;

typedef enum { WL_VF_INT, WL_VF_FLOAT, WL_VF_BOOL } WlValFormat;

/* Field description, shared by the loader/saver and the settings UI. */
typedef struct {
    const char *key;      /* key in options.txt            */
    const char *label;    /* label shown in the settings UI */
    const char *section;  /* video | audio | controls | gameplay */
    WlValFormat fmt;
    size_t off;
    int   imin, imax;     /* for WL_VF_INT  */
    float fmin, fmax;     /* for WL_VF_FLOAT */
} WlConfigSpec;

const WlConfigSpec *wl_config_specs(size_t *count);

void wl_config_defaults(WlConfig *cfg);
/* Clamps every field to its valid range (also fixes hand-edited files). */
void wl_config_validate(WlConfig *cfg);
/* A missing file is not an error (fresh install) — cfg keeps defaults.
 * Unknown keys are logged and skipped; invalid values fall back per-key. */
bool wl_config_load(WlConfig *cfg, const char *path);
bool wl_config_save(const WlConfig *cfg, const char *path);

#endif /* WL_CONFIG_H */
