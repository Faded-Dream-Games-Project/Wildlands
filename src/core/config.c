/* config.c — table-driven load/save/validate of user settings. */
#include "wl/config.h"
#include "wl/util.h"
#include "wl/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

static const WlConfigSpec k_specs[] = {
    /* key,               label,                section,    format,       offset,                          imin, imax, fmin, fmax */
    { "window_width",     "Resolution Width",   "video",    WL_VF_INT,   offsetof(WlConfig, window_width),    640, 3840,     0, 0 },
    { "window_height",    "Resolution Height",  "video",    WL_VF_INT,   offsetof(WlConfig, window_height),   480, 2160,     0, 0 },
    { "fullscreen",       "Fullscreen",         "video",    WL_VF_BOOL,  offsetof(WlConfig, fullscreen),        0,    0,     0, 0 },
    { "borderless",       "Borderless Window",  "video",    WL_VF_BOOL,  offsetof(WlConfig, borderless),        0,    0,     0, 0 },
    { "vsync",            "VSync",              "video",    WL_VF_BOOL,  offsetof(WlConfig, vsync),             0,    0,     0, 0 },
    { "msaa",             "Anti-Aliasing (4x)", "video",    WL_VF_BOOL,  offsetof(WlConfig, msaa),              0,    0,     0, 0 },
    { "fps_limit",        "FPS Limit (0 = off)","video",   WL_VF_INT,   offsetof(WlConfig, fps_limit),         0, 1000,     0, 0 },
    { "render_distance",  "Render Distance",    "video",    WL_VF_INT,   offsetof(WlConfig, render_distance),   2,   16,     0, 0 },
    { "graphics_quality", "Graphics Quality",   "video",    WL_VF_INT,   offsetof(WlConfig, graphics_quality),  0,    2,     0, 0 },
    { "fov",              "Field of View",      "video",    WL_VF_INT,   offsetof(WlConfig, fov),              50,  110,     0, 0 },
    { "view_bobbing",     "View Bobbing",       "video",    WL_VF_BOOL,  offsetof(WlConfig, view_bobbing),      0,    0,     0, 0 },
    { "brightness",       "Brightness",         "video",    WL_VF_FLOAT, offsetof(WlConfig, brightness),        0,    0,  0.5f, 1.5f },
    { "ui_scale",         "UI Scale",           "video",    WL_VF_FLOAT, offsetof(WlConfig, ui_scale),          0,    0, 0.75f, 2.0f },

    { "vol_master",       "Master Volume",      "audio",    WL_VF_FLOAT, offsetof(WlConfig, vol_master),        0,    0, 0.0f, 1.0f },
    { "vol_music",        "Music Volume",       "audio",    WL_VF_FLOAT, offsetof(WlConfig, vol_music),         0,    0, 0.0f, 1.0f },
    { "vol_sfx",          "Sound Effects",      "audio",    WL_VF_FLOAT, offsetof(WlConfig, vol_sfx),           0,    0, 0.0f, 1.0f },
    { "vol_ambient",      "Ambient Volume",     "audio",    WL_VF_FLOAT, offsetof(WlConfig, vol_ambient),       0,    0, 0.0f, 1.0f },

    { "mouse_sensitivity","Mouse Sensitivity",  "controls", WL_VF_FLOAT, offsetof(WlConfig, mouse_sensitivity),0,    0, 0.2f, 3.0f },
    { "invert_mouse",     "Invert Mouse Y",     "controls", WL_VF_BOOL,  offsetof(WlConfig, invert_mouse),      0,    0,     0, 0 },
    { "mouse_smoothing",  "Mouse Smoothing",    "controls", WL_VF_BOOL,  offsetof(WlConfig, mouse_smoothing),   0,    0,     0, 0 },

    { "difficulty",       "Difficulty",         "gameplay", WL_VF_INT,   offsetof(WlConfig, difficulty),        0,    3,     0, 0 },
    { "smooth_camera",    "Smooth Camera",      "gameplay", WL_VF_BOOL,  offsetof(WlConfig, smooth_camera),     0,    0,     0, 0 },
    { "autosave_interval","Autosave (seconds)", "gameplay", WL_VF_INT,   offsetof(WlConfig, autosave_interval), 0, 1200,    0, 0 },
    { "show_debug_info",  "Debug Overlay (F3)", "gameplay", WL_VF_BOOL,  offsetof(WlConfig, show_debug_info),   0,    0,     0, 0 },
    { "pause_on_focus_loss","Pause on Focus Loss","gameplay",WL_VF_BOOL, offsetof(WlConfig, pause_on_focus_loss),0,   0,     0, 0 },
};

const WlConfigSpec *wl_config_specs(size_t *count)
{
    if (count) *count = WL_ARRAY_COUNT(k_specs);
    return k_specs;
}

void wl_config_defaults(WlConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->window_width = 1280;
    cfg->window_height = 720;
    cfg->fullscreen = false;
    cfg->borderless = false;
    cfg->vsync = true;
    cfg->msaa = false;
    cfg->fps_limit = 0;
    cfg->render_distance = 8;
    cfg->graphics_quality = 1;
    cfg->fov = 75;
    cfg->view_bobbing = true;
    cfg->brightness = 1.0f;
    cfg->ui_scale = 1.0f;

    cfg->vol_master = 0.8f;
    cfg->vol_music = 0.55f;
    cfg->vol_sfx = 1.0f;
    cfg->vol_ambient = 0.7f;

    cfg->mouse_sensitivity = 1.0f;
    cfg->invert_mouse = false;
    cfg->mouse_smoothing = false;

    cfg->difficulty = 2;
    cfg->smooth_camera = true;
    cfg->autosave_interval = 120;
    cfg->show_debug_info = false;
    cfg->pause_on_focus_loss = true;
}

static void clamp_field(WlConfig *cfg, const WlConfigSpec *s)
{
    char *field = (char *)cfg + s->off;
    switch (s->fmt) {
    case WL_VF_INT:
        *(int *)field = wl_clampi(*(int *)field, s->imin, s->imax);
        break;
    case WL_VF_FLOAT:
        *(float *)field = wl_clampf(*(float *)field, s->fmin, s->fmax);
        break;
    case WL_VF_BOOL:
        break; /* any bool value is valid */
    }
}

void wl_config_validate(WlConfig *cfg)
{
    for (size_t i = 0; i < WL_ARRAY_COUNT(k_specs); i++)
        clamp_field(cfg, &k_specs[i]);
}

static bool apply_value(WlConfig *cfg, const WlConfigSpec *s, const char *val)
{
    char *field = (char *)cfg + s->off;
    switch (s->fmt) {
    case WL_VF_INT: {
        long v;
        if (!wl_parse_long(val, &v)) return false;
        *(int *)field = wl_clampi((int)v, s->imin, s->imax);
        return true;
    }
    case WL_VF_FLOAT: {
        float v;
        if (!wl_parse_float(val, &v)) return false;
        *(float *)field = wl_clampf(v, s->fmin, s->fmax);
        return true;
    }
    case WL_VF_BOOL: {
        bool v;
        if (!wl_parse_bool(val, &v)) return false;
        *(bool *)field = v;
        return true;
    }
    }
    return false;
}

bool wl_config_load(WlConfig *cfg, const char *path)
{
    if (!wl_file_exists(path)) {
        WL_LOGD("no options file at %s; using defaults", path);
        return true;
    }

    char *data = NULL;
    size_t len = 0;
    if (!wl_read_file(path, &data, &len)) {
        WL_LOGE("cannot read config file %s", path);
        return false;
    }

    char *save = NULL;
    size_t found = 0, unknown = 0;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char key[64], val[192];
        if (!wl_kv_line(line, key, sizeof(key), val, sizeof(val))) continue;

        const WlConfigSpec *spec = NULL;
        for (size_t i = 0; i < WL_ARRAY_COUNT(k_specs); i++) {
            if (strcmp(k_specs[i].key, key) == 0) { spec = &k_specs[i]; break; }
        }
        if (!spec) {
            unknown++;
            WL_LOGW("unknown option '%s' in %s (ignored)", key, path);
            continue;
        }
        if (!apply_value(cfg, spec, val)) {
            WL_LOGW("invalid value for option '%s': '%s' (kept default)", key, val);
            continue;
        }
        found++;
    }
    free(data);

    wl_config_validate(cfg);
    WL_LOGI("loaded %d options (%d unknown) from %s", (int)found, (int)unknown, path);
    return true;
}

bool wl_config_save(const WlConfig *cfg, const char *path)
{
    WlStrBuf sb;
    if (!wl_sb_init(&sb, 2048)) return false;

    bool ok = wl_sb_printf(&sb, "# Wildlands options (key = value)\n");
    ok = ok && wl_sb_printf(&sb, "# Format version %d. Lines starting with # are comments.\n",
                            WL_CONFIG_FORMAT_VERSION);
    const char *last_section = NULL;
    for (size_t i = 0; i < WL_ARRAY_COUNT(k_specs) && ok; i++) {
        const WlConfigSpec *s = &k_specs[i];
        if (!last_section || strcmp(last_section, s->section) != 0) {
            ok = ok && wl_sb_printf(&sb, "\n# --- %s ---\n", s->section);
            last_section = s->section;
        }
        const char *field = (const char *)cfg + s->off;
        switch (s->fmt) {
        case WL_VF_INT:
            ok = ok && wl_sb_printf(&sb, "%s = %d\n", s->key, *(const int *)field);
            break;
        case WL_VF_FLOAT:
            ok = ok && wl_sb_printf(&sb, "%s = %.3f\n", s->key, *(const float *)field);
            break;
        case WL_VF_BOOL:
            ok = ok && wl_sb_printf(&sb, "%s = %s\n", s->key,
                                    *(const bool *)field ? "true" : "false");
            break;
        }
    }

    if (ok) ok = wl_write_file_atomic(path, sb.buf, sb.len);
    wl_sb_free(&sb);

    if (ok) WL_LOGI("saved options to %s", path);
    else    WL_LOGE("failed to save options to %s", path);
    return ok;
}
