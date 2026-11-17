/* game.c — application lifecycle, state machine, world session management. */
#include "wl/game.h"
#include "wl/menus.h"
#include "wl/hud.h"
#include "wl/inv_ui.h"
#include "wl/crafting.h"
#include "wl/block.h"
#include "wl/light.h"
#include "wl/entities.h"
#include "wl/furnace_ui.h"
#include "wl/item.h"
#include "wl/log.h"
#include "wl/paths.h"
#include "wl/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "rlgl.h"

/* ------------------------------------------------------------------ */
/* argument parsing (demo/test automation)                             */
/* ------------------------------------------------------------------ */

static void parse_args(WlGame *g, int argc, char **argv)
{
    g->demo_gamemode = -1;
    g->demo_type = -1;
    for (int i = 1; i < argc; i++) {
        if (wl_str_eq(argv[i], "--world") && i + 1 < argc) {
            wl_scpy(g->demo_world, argv[++i], sizeof(g->demo_world));
        } else if (wl_str_eq(argv[i], "--screenshot") && i + 1 < argc) {
            wl_scpy(g->demo_screenshot, argv[++i], sizeof(g->demo_screenshot));
        } else if (wl_str_eq(argv[i], "--frames") && i + 1 < argc) {
            g->demo_frames = atoi(argv[++i]);
        } else if (wl_str_eq(argv[i], "--seed") && i + 1 < argc) {
            g->demo_seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (wl_str_eq(argv[i], "--gamemode") && i + 1 < argc) {
            g->demo_gamemode = wl_str_eq(argv[++i], "creative")
                ? WL_WORLD_GAMEMODE_CREATIVE : WL_WORLD_GAMEMODE_SURVIVAL;
        } else if (wl_str_eq(argv[i], "--type") && i + 1 < argc) {
            g->demo_type = wl_str_eq(argv[++i], "superflat")
                ? WL_WORLDTYPE_SUPERFLAT : WL_WORLDTYPE_STANDARD;
        } else if (wl_str_eq(argv[i], "--width") && i + 1 < argc) {
            g->demo_width = atoi(argv[++i]);
        } else if (wl_str_eq(argv[i], "--height") && i + 1 < argc) {
            g->demo_height = atoi(argv[++i]);
        } else if (wl_str_eq(argv[i], "--lookspin") && i + 1 < argc) {
            /* scripted camera rotation (degrees of yaw per frame) */
            g->demo_lookspin = (float)atof(argv[++i]);
        } else if (wl_str_eq(argv[i], "--looknod") && i + 1 < argc) {
            /* scripted pitch oscillation amplitude in degrees */
            g->demo_looknod = (float)atof(argv[++i]);
        } else if (wl_str_eq(argv[i], "--walk")) {
            /* hold Walk Forward (pairs with --lookspin/--looknod) */
            g->demo_walk = true;
        } else if (wl_str_eq(argv[i], "--menu") && i + 1 < argc) {
            /* test automation: show a menu for --frames, no world needed */
            g->demo_menu = true;
            wl_scpy(g->demo_start_menu, argv[++i], sizeof(g->demo_start_menu));
        } else if (wl_str_eq(argv[i], "--overlay") && i + 1 < argc) {
            g->demo_overlay = wl_str_eq(argv[++i], "craft") ? 3 : 2;
        } else if (wl_str_eq(argv[i], "--selftest")) {
            g->selftest = true;
        } else if (wl_str_eq(argv[i], "--uitest") && i + 1 < argc) {
            const char *s = argv[++i];
            g->uitest = wl_str_eq(s, "create") ? 2 : (wl_str_eq(s, "pause") ? 3 : 1);
        } else if (wl_str_eq(argv[i], "--yaw") && i + 1 < argc) {
            g->demo_dir_set = true;
            g->demo_yaw = (float)atof(argv[++i]) * 0.017453293f;
        } else if (wl_str_eq(argv[i], "--itemgallery")) {
            g->demo_itemgallery = true;
        } else if (wl_str_eq(argv[i], "--zoo")) {
            g->demo_zoo = true;
        } else if (wl_str_eq(argv[i], "--time") && i + 1 < argc) {
            g->demo_time = atoi(argv[++i]);
        } else if (wl_str_eq(argv[i], "--pitch") && i + 1 < argc) {
            g->demo_dir_set = true;
            float p = (float)atof(argv[++i]) * 0.017453293f;
            if (p > 1.55f) p = 1.55f;
            if (p < -1.55f) p = -1.55f;
            g->demo_pitch = p;
        } else if (wl_str_eq(argv[i], "--help") || wl_str_eq(argv[i], "-h")) {
            printf("Wildlands %s\n"
                   "  --world <name>        play this world (created if missing)\n"
                   "  --seed <n>            seed for new worlds\n"
                   "  --gamemode <m>        survival | creative (new worlds)\n"
                   "  --type <t>            standard | superflat (new worlds)\n"
                   "  --frames <n>          demo mode: exit after n frames\n"
                   "  --screenshot <path>   save a screenshot when exiting\n"
                   "  --width/--height <n>  window size override\n"
                   "  --lookspin <deg>      test: rotate yaw by deg per frame\n"
                   "  --looknod <deg>       test: oscillate pitch by +-deg\n"
                   "  --walk                test: hold Walk Forward\n"
                   "  --menu <screen>       test: show menu (main|worlds|create|settings)\n"
                   "  --overlay <inv|craft> test: open the inventory overlay in-world\n"
                   "  --selftest            runtime self test: mine/place/save/exit\n"
                   "  --uitest <s>          UI test: settings | create | pause\n"
                   "  --yaw/--pitch <deg>   demo camera direction at world entry\n",
                   WL_VERSION_STRING);
            exit(0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* startup timing + frame pacing                                       */
/* ------------------------------------------------------------------ */

static double stage_mark(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void stage_done(const char *name, double t0)
{
    WL_LOGI("startup: %-26s %7.1f ms", name, stage_mark() - t0);
}

/* Effective FPS target. fps_limit = 0 means Auto: a 60 fps cap. VSync
 * still paces to the display when it engages (the target only throttles,
 * never accelerates); when vsync is a no-op (Xvfb, some compositors or
 * drivers) the cap prevents uncapped rendering from pinning the CPU/GPU.
 * Users wanting more can set the slider up to 240. */
static int effective_fps_limit(const WlConfig *c)
{
    if (c->fps_limit > 0) return c->fps_limit;
    return 60;
}

/* ------------------------------------------------------------------ */
/* init / shutdown                                                     */
/* ------------------------------------------------------------------ */

bool wl_game_init(WlGame *g, int argc, char **argv)
{
    memset(g, 0, sizeof(*g));
    g->demo_time = -1;
    g->state = WL_STATE_MAIN_MENU;
    g->rebinding_action = -1;
    g->menus.selected = -1;
    g->audio_pending = true;
    parse_args(g, argc, argv);
    if (g->selftest && g->demo_world[0] == '\0') {
        /* fixed world + seed so the self test is reproducible (the world
         * is deleted after the runtime dirs exist, see below) */
        wl_scpy(g->demo_world, "SelfTest", sizeof(g->demo_world));
        g->demo_seed = 4242;
    }

    double t_total = stage_mark();
    g->init_started_ms = t_total;
    double t = t_total;

    /* --- 1. project root + runtime directories --- */
    if (!wl_paths_init(argv[0])) {
        fprintf(stderr, "Wildlands: FATAL: cannot locate the project root "
                        "(no assets/ next to the executable or the cwd)\n");
        return false;
    }
    if (!wl_paths_ensure_dirs()) {
        fprintf(stderr, "Wildlands: FATAL: cannot create runtime directories under %s\n",
                wl_paths_root());
        return false;
    }
    stage_done("paths + runtime dirs", t);
    t = stage_mark();

    /* --- 2. logging --- */
    char logpath[WL_PATH_MAX];
    {
        time_t now = time(NULL);
        struct tm tm_buf;
        struct tm *tm = localtime_r(&now, &tm_buf);
        char stamp[32];
        if (tm) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm);
        else snprintf(stamp, sizeof(stamp), "session");
        char logs[WL_PATH_MAX];
        wl_path_logs(logs, sizeof(logs), "");
        wl_path_join(logpath, sizeof(logpath), logs,
                     TextFormat("wildlands-%s.log", stamp));
    }
    wl_log_init(logpath);
    wl_paths_prune_logs(8);
    WL_LOGI("=== " WL_GAME_NAME " %s ===", WL_VERSION_STRING);
    WL_LOGI("project root: %s", wl_paths_root());
    stage_done("logging", t);
    t = stage_mark();

    /* --- 3. configuration + keybindings --- */
    int user_w = 0, user_h = 0; /* user's saved window size (demo restore) */
    char cfgpath[WL_PATH_MAX], kbpath[WL_PATH_MAX];
    wl_path_config(cfgpath, sizeof(cfgpath), "options.txt");
    wl_path_config(kbpath, sizeof(kbpath), "keybindings.txt");
    wl_config_defaults(&g->cfg);
    wl_config_load(&g->cfg, cfgpath);
    if (!wl_file_exists(cfgpath)) wl_config_save(&g->cfg, cfgpath);
    wl_keybinds_defaults(&g->keys);
    wl_keybinds_load(&g->keys, kbpath);
    if (!wl_file_exists(kbpath)) wl_keybinds_save(&g->keys, kbpath);

    if (g->demo_width > 0 && g->demo_height > 0) {
        /* --width/--height is a session-only override for the window;
         * the user's saved resolution must not be clobbered on save */
        user_w = g->cfg.window_width;
        user_h = g->cfg.window_height;
        g->cfg.window_width = g->demo_width;
        g->cfg.window_height = g->demo_height;
    }
    stage_done("config + keybindings", t);
    t = stage_mark();

    /* --- 4. display pre-flight: fail with an actionable message instead
     *        of crashing later inside texture loading --- */
    const char *disp = getenv("DISPLAY");
    const char *wayland = getenv("WAYLAND_DISPLAY");
    if ((disp == NULL || disp[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0')) {
        WL_LOGE("FATAL: no display available (DISPLAY and WAYLAND_DISPLAY are unset)");
        WL_LOGE("  - run the game inside an X11 or Wayland session, or");
        WL_LOGE("  - headless testing:  Xvfb :99 &  DISPLAY=:99 ./bin/wildlands");
        WL_LOGE("  - or simply:  make run  (auto-starts Xvfb when available)");
        fprintf(stderr,
                "Wildlands: FATAL: no display available. See logs/wildlands-*.log\n");
        return false;
    }

    /* --- 5. window + GL context --- */
    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (g->cfg.vsync) flags |= FLAG_VSYNC_HINT;
    if (g->cfg.msaa) flags |= FLAG_MSAA_4X_HINT;
    SetConfigFlags(flags);
    InitWindow(g->cfg.window_width, g->cfg.window_height,
               WL_GAME_NAME " " WL_VERSION_STRING);
    if (!IsWindowReady()) {
        WL_LOGE("FATAL: window/OpenGL initialization failed (raylib output above)");
        WL_LOGE("  Wildlands needs an OpenGL 3.3 context. On old GPUs try");
        WL_LOGE("  software rendering:  LIBGL_ALWAYS_SOFTWARE=1 ./bin/wildlands");
        fprintf(stderr, "Wildlands: FATAL: window/OpenGL initialization failed.\n");
        return false;
    }
    if (g->cfg.borderless) SetWindowState(FLAG_WINDOW_UNDECORATED);
    /* the window keeps the demo size for this session, but the config
     * (and everything saved from it) keeps the user's resolution */
    if (user_w > 0 && user_h > 0) {
        g->cfg.window_width = user_w;
        g->cfg.window_height = user_h;
    }
    /* ESC is the pause key, not the quit key: raylib's default exit key is
     * ESC and would make WindowShouldClose() kill the whole game the
     * moment the player pauses. Unbind it; the window's close button and
     * the menus still quit through the normal paths. */
    SetExitKey(KEY_NULL);
    /* log the actual GL device once (vendor / renderer / version) */
    wl_scpy(g->gpu_info, wl_renderer_gpu_info(), sizeof(g->gpu_info));
    SetTargetFPS((unsigned)effective_fps_limit(&g->cfg));
    WL_LOGI("frame pacing: vsync=%s, fps limit=%s (%d fps)",
            g->cfg.vsync ? "on" : "off",
            g->cfg.fps_limit > 0 ? "user" : "auto", effective_fps_limit(&g->cfg));
    stage_done("window + GL context", t);
    t = stage_mark();

    /* --- 6. registries + assets + mods --- */
    wl_blocks_init();
    wl_items_init();
    wl_crafting_init();

    char assets_root[WL_PATH_MAX];
    wl_path_assets(assets_root, sizeof(assets_root), "");
    g->assets = wl_assets_create(assets_root);
    if (!g->assets) {
        WL_LOGE("FATAL: asset manager failed to initialize");
        fprintf(stderr, "Wildlands: FATAL: asset initialization failed.\n");
        return false;
    }
    /* mods register blocks/items/textures before the atlas is finalized */
    char mods_root[WL_PATH_MAX];
    wl_path_mods(mods_root, sizeof(mods_root), "");
    wl_mods_init(&g->mods, g->assets, mods_root);
    wl_assets_finalize_atlas(g->assets);
    stage_done("assets + mods", t);
    t = stage_mark();

    /* --- 7. renderer (audio is deferred to the first frame so a slow
     *        audio device probe cannot blank the window) --- */
    wl_renderer_init(&g->rend, g->assets);
    memset(&g->ui, 0, sizeof(g->ui));
    g->ui_held.item = WL_ITEM_NONE;
    stage_done("renderer", t);

    /* --- 8. entry point for demos/tests --- */
    if (g->uitest) {
        if (g->uitest == 3) {
            /* pause-flow test: enters a world through the normal --world
             * path and drives ESC / buttons over the real menus */
            if (g->demo_world[0] == '\0')
                wl_scpy(g->demo_world, "PauseUITest", sizeof(g->demo_world));
            wl_worlds_delete("PauseUITest"); /* always start fresh */
        } else {
            g->demo_menu = true;
            wl_scpy(g->demo_start_menu, g->uitest == 2 ? "create" : "settings",
                    sizeof(g->demo_start_menu));
        }
        g->uitest_cfg = g->cfg; /* restored when the UI test finishes */
        if (g->uitest == 2) wl_worlds_delete("My World"); /* reproducible */
        WL_LOGI("UITEST: begin (%s)",
                g->uitest == 3 ? "pause menu" :
                g->uitest == 2 ? "world creation" : "settings menu");
    }
    if (g->demo_menu) {
        if (wl_str_eq(g->demo_start_menu, "worlds") ||
            wl_str_eq(g->demo_start_menu, "create")) {
            g->menus.world_count = wl_worlds_list(g->menus.worlds, WL_MENUS_MAX_WORLDS);
            g->menus.selected = g->menus.world_count > 0 ? 0 : -1;
            g->state = wl_str_eq(g->demo_start_menu, "create")
                ? WL_STATE_WORLD_CREATE : WL_STATE_WORLD_SELECT;
            wl_scpy(g->menus.new_name, "Test World", sizeof(g->menus.new_name));
        } else if (wl_str_eq(g->demo_start_menu, "settings")) {
            g->cfg_entry = g->cfg;
            g->settings_return = WL_STATE_MAIN_MENU;
            g->state = WL_STATE_SETTINGS;
        }
        EnableCursor();
        g->init_done_ms = stage_mark() - t_total;
        WL_LOGI("startup complete in %.1f ms", g->init_done_ms);
        return true;
    }

    if (g->demo_world[0] != '\0') {
        if (g->selftest)
            wl_worlds_delete("SelfTest"); /* always run from a clean state */
        WlWorldEntry list[64];
        int n = wl_worlds_list(list, 64);
        bool found = false;
        for (int i = 0; i < n; i++) {
            if (wl_str_eq(list[i].meta.name, g->demo_world) ||
                wl_str_eq(list[i].dir_name, g->demo_world)) {
                wl_game_enter_world(g, &list[i]);
                found = true;
                break;
            }
        }
        if (!found) {
            WlWorldEntry entry;
            int gm = g->demo_gamemode >= 0 ? g->demo_gamemode : WL_WORLD_GAMEMODE_SURVIVAL;
            int ty = g->demo_type >= 0 ? g->demo_type : WL_WORLDTYPE_STANDARD;
            if (wl_worlds_create(g->demo_world, g->demo_seed, ty, gm, &entry))
                wl_game_enter_world(g, &entry);
        }
    }

    if (g->demo_frames > 0 || g->selftest) EnableCursor();
    g->init_done_ms = stage_mark() - t_total;
    WL_LOGI("startup complete in %.1f ms", g->init_done_ms);
    return true;
}

void wl_game_save_config(WlGame *g)
{
    char cfgpath[WL_PATH_MAX], kbpath[WL_PATH_MAX];
    wl_path_config(cfgpath, sizeof(cfgpath), "options.txt");
    wl_path_config(kbpath, sizeof(kbpath), "keybindings.txt");
    wl_config_validate(&g->cfg);
    wl_config_save(&g->cfg, cfgpath);
    wl_keybinds_save(&g->keys, kbpath);
}

void wl_game_shutdown(WlGame *g)
{
    if (g->world) wl_game_save_session(g);
    wl_game_save_config(g);
    if (g->demo_screenshot[0] != '\0') {
        TakeScreenshot(g->demo_screenshot);
        WL_LOGI("screenshot saved: %s", g->demo_screenshot);
    }
    wl_renderer_shutdown(&g->rend);
    wl_audio_shutdown(&g->audio);
    wl_mods_shutdown(&g->mods);
    wl_assets_destroy(g->assets);
    if (g->menu_bg.id != 0) UnloadRenderTexture(g->menu_bg);
    if (g->world) wl_world_destroy(g->world);
    if (IsWindowReady()) CloseWindow();
    wl_log_close();
}

/* ------------------------------------------------------------------ */
/* settings application                                                */
/* ------------------------------------------------------------------ */

void wl_game_apply_settings(WlGame *g)
{
    WlConfig *c = &g->cfg;

    /* window toggles are stateful: only act on changes */
    static bool s_full = false, s_border = false, s_vsync = false;
    static int s_w = 0, s_h = 0;
    static bool s_init = false;
    if (!s_init) {
        s_full = IsWindowFullscreen();
        s_border = c->borderless;
        s_vsync = c->vsync;
        s_w = c->window_width;
        s_h = c->window_height;
        s_init = true;
    }

    if (s_full != c->fullscreen) {
        ToggleFullscreen();
        s_full = c->fullscreen;
    }
    if (s_border != c->borderless) {
        if (c->borderless) SetWindowState(FLAG_WINDOW_UNDECORATED);
        else ClearWindowState(FLAG_WINDOW_UNDECORATED);
        s_border = c->borderless;
    }
    if (s_vsync != c->vsync) {
        if (c->vsync) SetWindowState(FLAG_VSYNC_HINT);
        else ClearWindowState(FLAG_VSYNC_HINT);
        s_vsync = c->vsync;
    }
    if (s_w != c->window_width || s_h != c->window_height) {
        SetWindowSize(c->window_width, c->window_height);
        s_w = c->window_width;
        s_h = c->window_height;
    }

    /* frame pacing: honor the user limit; Auto keeps a safety cap so a
     * non-engaging vsync cannot render uncapped */
    SetTargetFPS((unsigned)effective_fps_limit(c));

    wl_audio_set_volumes(&g->audio, c->vol_master, c->vol_music, c->vol_sfx,
                         c->vol_ambient);
    if (g->world) wl_world_set_render_distance(g->world, c->render_distance);
}

/* ------------------------------------------------------------------ */
/* world session                                                       */
/* ------------------------------------------------------------------ */

bool wl_game_enter_world(WlGame *g, const WlWorldEntry *entry)
{
    char wdir[WL_PATH_MAX];
    if (!wl_worlds_dir_path(wdir, sizeof(wdir), entry->dir_name)) return false;

    WlWorldParams params;
    memset(&params, 0, sizeof(params));
    wl_scpy(params.name, entry->meta.name, sizeof(params.name));
    params.seed = entry->meta.seed;
    params.type = entry->meta.type;
    params.gamemode = entry->meta.gamemode;

    g->world = wl_world_create(&params, wdir);
    if (!g->world) return false;
    wl_world_set_render_distance(g->world, g->cfg.render_distance);

    g->entry = *entry;

    /* spawn point */
    Vector3 spawn = { entry->meta.spawn_x, entry->meta.spawn_y, entry->meta.spawn_z };
    bool fresh = (spawn.x == 0.0f && spawn.y == 0.0f && spawn.z == 0.0f);
    if (fresh) {
        const WlWorldGen *gen = wl_world_gen(g->world);
        int sx = 0, sz = 0;
        if (wl_worldgen_find_spawn(gen, &sx, &sz)) {
            spawn = (Vector3){ sx + 0.5f,
                               (float)wl_worldgen_height_at(gen, sx, sz) + 1.2f,
                               sz + 0.5f };
        } else {
            /* endless ocean around the origin: float on the surface */
            WL_LOGW("no dry land near the origin; spawning over open water");
            spawn = (Vector3){ 0.5f, (float)WL_SEA_LEVEL + 2.0f, 0.5f };
        }
    }
    g->spawn = spawn;

    wl_player_init(&g->player, spawn);
    bool found = false;
    wl_worlds_load_player(entry->dir_name, &g->player, &found);

    g->state = WL_STATE_LOADING;
    g->overlay = WL_OVERLAY_NONE;
    g->session_start = GetTime();
    g->autosave_timer = 0.0;
    g->load_initial_pending = -1;
    g->death_dropped = false;
    g->look_guard = 0;
    g->ui_held.item = WL_ITEM_NONE;
    EnableCursor();

    /* test automation: give the player a kit so the overlay has content */
    if (g->demo_overlay > 0) {
        wl_inv_add(&g->player.inv, WL_BLOCK_GRASS, 32, 0);
        wl_inv_add(&g->player.inv, WL_BLOCK_LOG, 12, 0);
        wl_inv_add(&g->player.inv, WL_BLOCK_COBBLE, 64, 0);
        wl_inv_add(&g->player.inv, WL_ITEM_STICK, 17, 0);
        wl_inv_add(&g->player.inv, WL_ITEM_PICK_IRON, 1, 120);
        wl_inv_add(&g->player.inv, WL_ITEM_APPLE, 5, 0);
        wl_inv_add(&g->player.inv, WL_ITEM_SWORD_DIAMOND, 1, 900);
        wl_inv_add(&g->player.inv, WL_BLOCK_TORCH, 9, 0);
        wl_inv_add(&g->player.inv, WL_BLOCK_FURNACE, 1, 0);
        wl_inv_add(&g->player.inv, WL_ITEM_COAL, 8, 0);
        wl_inv_add(&g->player.inv, WL_BLOCK_IRON_ORE, 4, 0);
    }
    return true;
}

bool wl_game_save_session(WlGame *g)
{
    if (!g->world) return true; /* nothing to save is not a failure */
    double now = GetTime();
    bool ok = wl_worlds_save_session(g->world, &g->player, g->spawn,
                                     now - g->session_start);
    if (!ok) {
        g->last_save_failed = true;
        WL_LOGE("session save FAILED for world '%s' (progress may be lost; "
                "check disk space and permissions)",
                wl_world_name(g->world));
    } else {
        g->last_save_failed = false;
    }
    g->session_start = now;
    return ok;
}

bool wl_game_exit_world(WlGame *g)
{
    if (!g->world) return true;
    /* save first; on failure keep the session open so the player can see
     * the error and retry instead of silently losing progress */
    if (!wl_game_save_session(g)) return false;
    wl_world_destroy(g->world);
    g->world = NULL;
    g->state = WL_STATE_MAIN_MENU;
    g->overlay = WL_OVERLAY_NONE;
    EnableCursor();
    /* world sounds (steps, mobs, furnaces) must not outlive the world */
    wl_audio_stop_sfx(&g->audio);
    wl_audio_stop_music(&g->audio);
    return true;
}

bool wl_game_quit_to_desktop(WlGame *g)
{
    /* save the session, then leave through the normal shutdown path
     * (config save, asset/audio/world teardown, window close); if the
     * save fails the game stays open and the error is reported */
    if (!wl_game_save_session(g)) return false;
    g->quit = true;
    return true;
}

bool wl_game_drop_inventory(WlGame *g)
{
    if (!g->world) return true; /* nothing to drop into */
    bool all_dropped = true;
    Vector3 pos = { g->player.pos.x, g->player.pos.y + 0.9f, g->player.pos.z };

    /* drop every occupied source: hotbar+main slots, the crafting grid
     * and the stack held by the mouse — cleared only after the drop
     * entity exists, so a full drop list retries next frame without
     * losing or duplicating anything */
    WlSlot *sources[WL_INV_SIZE + WL_CRAFT_GRID_MAX + 1];
    int n = 0;
    for (int i = 0; i < WL_INV_SIZE; i++) sources[n++] = &g->player.inv.slots[i];
    for (int i = 0; i < WL_CRAFT_GRID_MAX; i++) sources[n++] = &g->player.inv.craft[i];
    sources[n++] = &g->ui_held;

    int dropped = 0;
    for (int i = 0; i < n; i++) {
        WlSlot *s = sources[i];
        if (s->item == WL_ITEM_NONE) continue;
        if (wl_world_add_drop(g->world, s->item, s->count, s->dur, pos)) {
            WL_LOGD("death drop: %d x item %u", (int)s->count, (unsigned)s->item);
            s->item = WL_ITEM_NONE;
            s->count = 0;
            s->dur = 0;
            dropped++;
        } else {
            all_dropped = false; /* drop list full: retry the rest later */
        }
    }
    if (dropped > 0) WL_LOGI("dropped %d stacks on death", dropped);
    return all_dropped;
}

/* ------------------------------------------------------------------ */
/* runtime self test (--selftest / make selftest)                      */
/* Drives the real gameplay paths: physics, raycast, mining, placing,  */
/* session save, disk round-trip and the return to the main menu.      */
/* ------------------------------------------------------------------ */

static void selftest_check(WlGame *g, const char *name, bool ok)
{
    WL_LOGI("SELFTEST %-4s %s", ok ? "PASS" : "FAIL", name);
    if (!ok) g->selftest_failed = true;
}

static void run_selftest(WlGame *g)
{
    WlWorld *w = g->world;
    WlPlayer *p = &g->player;
    const float dt = 1.0f / 60.0f;

    WL_LOGI("SELFTEST: begin (world '%s', seed %llu)", wl_world_name(w),
            (unsigned long long)wl_world_gen(w)->seed);

    /* 1. world streaming finished */
    WlWorldStats st = wl_world_stats(w);
    selftest_check(g, "chunks generated", st.generated > 50);
    selftest_check(g, "chunks meshed", st.meshed > 20);

    /* 1b. atlas is minification-filtered: without mipmaps, faces past
     * ~30 blocks sample below one texel per pixel and the terrain
     * shimmers on every camera move (regression guard) */
    Texture2D atlas_tex = wl_assets_tex(g->assets, WL_TEX_ATLAS);
    selftest_check(g, "atlas mipmapped for distance sampling",
                   atlas_tex.id != 0 && atlas_tex.mipmaps > 1);

    /* 2. gravity + landing */
    p->pos.y += 6.0f;
    p->vel = (Vector3){ 0 };
    bool landed = false;
    for (int i = 0; i < 300 && !landed; i++) {
        wl_player_update(p, w, &g->keys, &g->cfg, &g->audio, dt, false,
                         wl_world_gamemode(w));
        landed = p->on_ground;
    }
    selftest_check(g, "physics: gravity + landing", landed);

    int px = (int)floorf(p->pos.x);
    int py = (int)floorf(p->pos.y);
    int pz = (int)floorf(p->pos.z);

    /* build a deterministic arena around the player: uniform floor, air
     * above — so the checks below do not depend on spawn terrain or on
     * leftover edits from a previous session */
    for (int x = px - 1; x <= px + 7; x++) {
        for (int z = pz - 3; z <= pz + 1; z++) {
            wl_world_set_block(w, x, py - 1, z, WL_BLOCK_STONE);
            for (int y = py; y <= py + 4; y++)
                wl_world_set_block(w, x, y, z, WL_BLOCK_AIR);
        }
    }
    p->pos = (Vector3){ (float)px + 0.5f, (float)py + 0.01f, (float)pz + 0.5f };
    p->vel = (Vector3){ 0 };

    /* 3. horizontal movement + wall collision */
    for (int i = 2; i <= 4; i++)
        for (int dy = 0; dy < 3; dy++)
            wl_world_set_block(w, px + i, py + dy, pz, WL_BLOCK_STONE);
    {
        float x0 = p->pos.x;
        bool penetrated = false;
        for (int i = 0; i < 180; i++) {
            p->vel.x = 6.0f; /* keep pushing into the wall */
            wl_player_update(p, w, &g->keys, &g->cfg, &g->audio, dt, false,
                             wl_world_gamemode(w));
            if (p->pos.x > (float)px + 1.8f) { penetrated = true; break; }
        }
        selftest_check(g, "movement + wall collision",
                       !penetrated && p->pos.x - x0 > 0.5f);
    }

    /* 3b. voxel lighting: torch light in a sealed room, skylight outside */
    {
        /* build a sealed 5x4x5 room around a known air pocket */
        int rx = px + 6, ry = py + 6, rz = pz - 6;
        for (int dx = 0; dx < 5; dx++)
            for (int dy = 0; dy < 4; dy++)
                for (int dz = 0; dz < 5; dz++) {
                    bool shell = (dx == 0 || dx == 4 || dy == 0 || dy == 3 ||
                                  dz == 0 || dz == 4);
                    wl_world_set_block(w, rx + dx, ry + dy, rz + dz,
                                       shell ? WL_BLOCK_STONE : WL_BLOCK_AIR);
                }
        /* force a relight of the affected chunk(s) the way the mesh
         * step would */
        WlChunk *lc = wl_world_chunk_at(w, rx >> 4, rz >> 4);
        if (lc) wl_light_chunk(w, lc);

        int cx = rx + 2, cy = ry + 2, cz = rz + 2; /* room center */
        selftest_check(g, "sealed room is dark",
                       wl_light_level(w, cx, cy, cz) == 0);

        wl_world_set_block(w, cx, cy, cz, WL_BLOCK_TORCH);
        if (lc) wl_light_chunk(w, lc);
        selftest_check(g, "torch lights its own cell",
                       wl_light_level(w, cx, cy, cz) >= 13);
        selftest_check(g, "torch light reaches 3 blocks",
                       wl_light_level(w, cx, cy, cz - 3) >= 10);
        /* the block-light channel must not pass the stone wall (the cell
         * outside is open sky, so only the torch channel is checked) */
        selftest_check(g, "torch light stops at walls",
                       wl_light_block(w, rx - 1, cy, cz) == 0);

        wl_world_set_block(w, cx, cy, cz, WL_BLOCK_AIR);
        if (lc) wl_light_chunk(w, lc);
        selftest_check(g, "removing torch removes light",
                       wl_light_level(w, cx, cy, cz) == 0);

        /* skylight above the room, dark below its roof */
        selftest_check(g, "skylight above terrain",
                       wl_light_sky(w, rx + 2, WL_CHUNK_H - 1, rz + 2) == 15);
        selftest_check(g, "no skylight under stone roof",
                       wl_light_sky(w, cx, cy, cz) == 0);
    }

    /* 4. mining through the real interact path (mouse test seam) */
    p->yaw = 0.0f;
    p->pitch = -1.45f; /* straight down */
    {
        WlTarget t = wl_interact_query(w, p);
        selftest_check(g, "raycast finds target", t.valid);
        bool mined = false, dropped = false;
        if (t.valid) {
            /* deterministic regardless of spawn biome: dirt drops dirt */
            wl_world_set_block(w, t.x, t.y, t.z, WL_BLOCK_DIRT);
            int before = wl_inv_count(&p->inv, WL_BLOCK_DIRT);
            wl_interact_debug_mouse(true, false, true);
            for (int i = 0; i < 900; i++) {
                wl_interact_update(p, w, &g->cfg, &g->audio, dt, true);
                if (wl_world_get_block(w, t.x, t.y, t.z) == WL_BLOCK_AIR) break;
            }
            wl_interact_debug_mouse(false, false, false);
            mined = wl_world_get_block(w, t.x, t.y, t.z) == WL_BLOCK_AIR;
            dropped = wl_inv_count(&p->inv, WL_BLOCK_DIRT) > before;
        }
        selftest_check(g, "mining breaks block", mined);
        selftest_check(g, "mining yields drop", dropped);
    }

    /* 5. placing through the real interact path */
    {
        /* deterministic target: a stone pad two blocks ahead, air above */
        for (int dx = -1; dx <= 1; dx++) {
            wl_world_set_block(w, px + dx, py, pz - 2, WL_BLOCK_STONE);
            wl_world_set_block(w, px + dx, py + 1, pz - 2, WL_BLOCK_AIR);
            wl_world_set_block(w, px + dx, py + 2, pz - 2, WL_BLOCK_AIR);
        }
        wl_inv_add(&p->inv, WL_BLOCK_COBBLE, 8, 0);
        int slot = -1;
        for (int i = 0; i < WL_INV_SIZE; i++)
            if (p->inv.slots[i].item == WL_BLOCK_COBBLE) { slot = i; break; }
        if (slot >= 0 && slot < WL_HOTBAR_SIZE) p->selected_slot = slot;

        p->yaw = 0.0f;   /* looking -Z */
        p->pitch = -0.75f;
        WlTarget t = wl_interact_query(w, p);
        bool placed = false;
        if (t.valid) {
            int cx = t.px, cy = t.py, cz = t.pz;
            wl_interact_debug_mouse(false, true, true);
            for (int i = 0; i < 40; i++)
                wl_interact_update(p, w, &g->cfg, &g->audio, dt, true);
            wl_interact_debug_mouse(false, false, false);
            placed = wl_world_get_block(w, cx, cy, cz) == WL_BLOCK_COBBLE;
        }
        selftest_check(g, "placing block", placed);
    }

    /* 6. session save + disk round-trip */
    {
        int sx = px + 6, sy = py + 1, sz = pz;
        wl_world_set_block(w, sx, sy, sz, WL_BLOCK_SNOW);
        wl_game_save_session(g);
        wl_world_set_block(w, sx, sy, sz, WL_BLOCK_AIR); /* wipe in memory */
        WlChunk *ch = wl_world_chunk_at(w, sx >> 4, sz >> 4);
        bool rt = ch && wl_world_load_saved_chunk(w, ch) &&
                  wl_world_get_block(w, sx, sy, sz) == WL_BLOCK_SNOW;
        selftest_check(g, "save + reload round-trip", rt);

        char path[WL_PATH_MAX];
        const char *dir = wl_world_save_dir(w);
        bool files = wl_path_join(path, sizeof(path), dir, "level.dat") &&
                     wl_file_exists(path);
        files = files && wl_path_join(path, sizeof(path), dir, "player.dat") &&
                wl_file_exists(path);
        selftest_check(g, "session files written", files);
    }

    /* 5b. food: hold right-click with an apple through the real eat path */
    {
        wl_inv_init(&p->inv);
        p->inv.slots[0] = (WlSlot){ WL_ITEM_APPLE, 2, 0 };
        p->selected_slot = 0;
        p->hunger = 10.0f;
        p->eat_timer = 0.0;
        wl_interact_debug_mouse(false, true, true);
        for (int i = 0; i < 120; i++)
            wl_interact_update(p, w, &g->cfg, &g->audio, dt, true);
        wl_interact_debug_mouse(false, false, false);
        selftest_check(g, "eating apple restores hunger", p->hunger == 14.0f);
        selftest_check(g, "eating consumes the item",
                       p->inv.slots[0].count == 1);
    }

    /* 6a. entities: spawn, chase, die with drops */
    {
        int n = 0;
        wl_world_entities(w, &n);
        int before_ents = n;
        WlEntity *z = wl_world_spawn_entity(w, WL_ENT_ZOMBIE,
                                            (Vector3){ px + 8.0f, py + 1.0f,
                                                       pz + 0.5f });
        selftest_check(g, "zombie spawns", z && z->active);

        /* zombies chase: run updates with the player standing still */
        float d0 = 8.0f;
        for (int i = 0; i < 120; i++)
            wl_world_update_entities(w, p, &g->cfg, &g->audio, dt);
        float d1 = sqrtf((z->pos.x - p->pos.x) * (z->pos.x - p->pos.x) +
                         (z->pos.z - p->pos.z) * (z->pos.z - p->pos.z));
        selftest_check(g, "zombie chases the player", d1 < d0 - 1.0f);

        /* cow death drops beef through the real hurt path */
        int drops_before = 0;
        wl_world_drops(w, &drops_before);
        WlEntity *cow = wl_world_spawn_entity(w, WL_ENT_COW,
                                              (Vector3){ px + 2.0f, py + 1.0f,
                                                         pz + 2.0f });
        selftest_check(g, "cow spawns", cow && cow->active);
        wl_entity_hurt(w, cow, 100.0f, (Vector3){ 1, 0, 0 }, &g->audio);
        /* death is a short fall-over animation, then deactivation */
        selftest_check(g, "cow enters death animation when killed",
                       cow->active && cow->death_timer > 0.0f);
        int drops_after = 0;
        wl_world_drops(w, &drops_after);
        selftest_check(g, "cow drops loot", drops_after > drops_before);
        for (int i = 0; i < 30; i++)
            wl_world_update_entities(w, p, &g->cfg, &g->audio, 0.05f);
        selftest_check(g, "cow deactivates after the death animation",
                       !cow->active);
        (void)before_ents;

        /* arrow damages the player through the real projectile path */
        float hp0 = p->health;
        WlEntity *ar = wl_world_spawn_entity(w, WL_ENT_ARROW,
                                             (Vector3){ p->pos.x, p->pos.y + 1.0f,
                                                        p->pos.z });
        if (ar) {
            ar->vel = (Vector3){ 0, 0, 0 };
            wl_world_update_entities(w, p, &g->cfg, &g->audio, dt);
        }
        selftest_check(g, "arrow hits and damages the player",
                       p->health < hp0);
        if (!p->dead && p->health < hp0) p->health = hp0; /* restore */
    }

    /* 6b. furnace: smelt iron ore -> ingot with coal, lit/unlit swap,
     * and persistence */
    {
        int fx = px - 4, fy = py + 1, fz = pz - 4;
        wl_world_set_block(w, fx, fy, fz, WL_BLOCK_FURNACE);
        WlFurnace *f = wl_world_get_furnace(w, fx, fy, fz, true);
        selftest_check(g, "furnace record created", f != NULL);
        if (f) {
            f->input = (WlSlot){ WL_BLOCK_IRON_ORE, 2, 0 };
            f->fuel = (WlSlot){ WL_ITEM_COAL, 1, 0 };
            wl_furnaces_update(w, 5.6f); /* one smelt takes 5s */
            selftest_check(g, "furnace ignited (lit block)",
                           wl_world_get_block(w, fx, fy, fz) ==
                               WL_BLOCK_FURNACE_LIT);
            selftest_check(g, "iron ore smelted to ingot",
                           f->output.item == WL_ITEM_IRON_INGOT &&
                               f->output.count == 1);
            selftest_check(g, "input consumed", f->input.count == 1);
            wl_furnaces_update(w, 40.0f); /* burn the coal out */
            selftest_check(g, "furnace unlit when out of fuel",
                           wl_world_get_block(w, fx, fy, fz) ==
                               WL_BLOCK_FURNACE);
        }
        /* 6c. water: a source flows into a space opened beside it and
         * dries again when dammed off (real game loop path) */
        {
            int wx = px + 3, wy = py + 4, wz = pz + 3;
            /* small basin: floor + walls, water source in one corner */
            for (int x = 0; x <= 3; x++)
                for (int z = 0; z <= 3; z++) {
                    wl_world_set_block(w, wx + x, wy, wz + z, WL_BLOCK_STONE);
                    if (x == 0 || x == 3 || z == 0 || z == 3)
                        for (int y = wy + 1; y <= wy + 2; y++)
                            wl_world_set_block(w, wx + x, y, wz + z,
                                               WL_BLOCK_STONE);
                }
            wl_world_set_block(w, wx + 1, wy + 1, wz + 1, WL_BLOCK_WATER);
            for (int i = 0; i < 40; i++) wl_world_water_tick(w, 0.3f);
            selftest_check(g, "water source spreads in the basin",
                           wl_blocks_liquid(
                               wl_world_get_block(w, wx + 2, wy + 1, wz + 1)));
            /* break the divider: water pours through the gap */
            wl_world_set_block(w, wx + 2, wy + 1, wz + 2, WL_BLOCK_AIR);
            for (int i = 0; i < 40; i++) wl_world_water_tick(w, 0.3f);
            selftest_check(g, "water flows through an opened gap",
                           wl_blocks_liquid(
                               wl_world_get_block(w, wx + 2, wy + 1, wz + 2)));
            /* remove the source: everything dries */
            wl_world_set_block(w, wx + 1, wy + 1, wz + 1, WL_BLOCK_AIR);
            for (int i = 0; i < 40; i++) wl_world_water_tick(w, 0.3f);
            selftest_check(g, "basin dries after source removal",
                           !wl_blocks_liquid(
                               wl_world_get_block(w, wx + 2, wy + 1, wz + 1)));
        }

        /* cooked-meat recipe lookup */
        selftest_check(g, "raw beef smelts to steak",
                       wl_furnace_smelt_result(WL_ITEM_BEEF_RAW) ==
                           WL_ITEM_BEEF_COOKED);
    }

    /* 7. death drops the whole inventory into the world */
    wl_inv_init(&p->inv); /* deterministic: earlier steps mined blocks */
    wl_inv_add(&p->inv, WL_BLOCK_LOG, 12, 0);
    wl_inv_add(&p->inv, WL_ITEM_APPLE, 5, 0);
    int save_difficulty = g->cfg.difficulty;
    if (save_difficulty == 0) g->cfg.difficulty = 2; /* peaceful blocks damage */
    wl_player_damage(p, 1000.0f, &g->audio, g->cfg.difficulty);
    selftest_check(g, "player dies", p->dead);
    g->cfg.difficulty = save_difficulty;
    int drops_before_death = 0;
    wl_world_drops(w, &drops_before_death); /* earlier tests may have left loot */
    bool dropped = wl_game_drop_inventory(g);
    selftest_check(g, "death drop reports complete", dropped);
    int drops = 0;
    wl_world_drops(w, &drops);
    selftest_check(g, "death spawned 2 drop stacks",
                   drops - drops_before_death == 2);
    selftest_check(g, "inventory emptied on death",
                   wl_inv_count(&p->inv, WL_BLOCK_LOG) == 0 &&
                   wl_inv_count(&p->inv, WL_ITEM_APPLE) == 0);

    /* 8. respawn + pickup: no loss, no duplication. Let the stacks
     * settle first (physics only), then stand the player on them. */
    wl_player_respawn(p, g->spawn);
    selftest_check(g, "player respawns", !p->dead);
    for (int i = 0; i < 12; i++) wl_world_update_drops(w, NULL, 0.1f);
    const WlDrop *dl = wl_world_drops(w, &drops);
    selftest_check(g, "drops settled and still present", drops >= 1);
    /* the stacks scatter apart when thrown; stand on each in turn */
    for (int guard = 0; guard < 8 && drops > 0; guard++) {
        dl = wl_world_drops(w, &drops);
        p->pos = (Vector3){ dl[0].pos.x, dl[0].pos.y - 0.9f, dl[0].pos.z };
        wl_world_update_drops(w, p, 0.1f);
    }
    selftest_check(g, "dropped items collected",
                   wl_inv_count(&p->inv, WL_BLOCK_LOG) == 12 &&
                   wl_inv_count(&p->inv, WL_ITEM_APPLE) == 5);
    wl_world_drops(w, &drops);
    selftest_check(g, "no leftover or duplicated drops", drops == 0);

    /* 9. drops survive a save + reload round-trip */
    wl_world_add_drop(w, WL_BLOCK_COBBLE, 7, 0,
                      (Vector3){ p->pos.x, p->pos.y + 1.0f, p->pos.z });
    wl_game_save_session(g);
    {
        char path[WL_PATH_MAX], wdir[WL_PATH_MAX];
        bool ok = wl_worlds_dir_path(wdir, sizeof(wdir), wl_world_name(w)) &&
                  wl_path_join(path, sizeof(path), wdir, "drops.dat") &&
                  wl_file_exists(path);
        selftest_check(g, "drops.dat written", ok);
    }
    wl_game_exit_world(g);
    selftest_check(g, "exit to main menu",
                   g->state == WL_STATE_MAIN_MENU && g->world == NULL);
    if (wl_game_enter_world(g, &g->entry)) {
        w = g->world;
        wl_world_drops(w, &drops);
        selftest_check(g, "drops survive save+reload", drops == 1);
        /* collect them so the follow-up exit save is clean */
        for (int i = 0; i < 12; i++) wl_world_update_drops(w, NULL, 0.1f);
        const WlDrop *rd = wl_world_drops(w, &drops);
        for (int guard = 0; guard < 8 && drops > 0; guard++) {
            rd = wl_world_drops(w, &drops);
            g->player.pos = (Vector3){ rd[0].pos.x, rd[0].pos.y - 0.9f, rd[0].pos.z };
            wl_world_update_drops(w, &g->player, 0.1f);
        }
        wl_game_exit_world(g);
    } else {
        selftest_check(g, "re-enter world after death test", false);
    }

    WL_LOGI("SELFTEST %s", g->selftest_failed ? "FAILED" : "PASSED");
    g->quit = true;
}

/* renderer flush + deferred screenshot (--uitest captures mid-frame, so
 * the queued shot shows exactly what this frame presents) */
static void uitest_maybe_shot(WlGame *g)
{
    if (g->uitest_shot[0] == '\0') return;
    rlDrawRenderBatchActive();
    TakeScreenshot(g->uitest_shot);
    g->uitest_shot[0] = '\0';
}

/* ------------------------------------------------------------------ */
/* UI runtime test (--uitest): drives the real menus through the       */
/* wl_ui_debug_input seam, with logged assertions.                     */
/* ------------------------------------------------------------------ */

static void uitest_check(WlGame *g, const char *name, bool ok)
{
    WL_LOGI("UITEST %-4s %s", ok ? "PASS" : "FAIL", name);
    if (!ok) g->uitest_failed = true;
}

static void uitest_finish(WlGame *g)
{
    /* restore any settings the test touched, then stop */
    g->cfg = g->uitest_cfg;
    wl_game_apply_settings(g);
    wl_game_save_config(g);
    wl_ui_debug_input(false, 0, 0, false, false, 0, NULL);
    WL_LOGI("UITEST %s", g->uitest_failed ? "FAILED" : "PASSED");
    g->uitest = 0;
    g->quit = true;
}

static void uitest_settings(WlGame *g)
{
    float s = g->ui.scale;
    int sw = GetScreenWidth();
    float pw = 640 * s;
    float panel_x = (sw - pw) / 2, panel_y = 76 * s;
    float ph = 520 * s; /* mirrors the settings menu layout */
    float max_ph = (float)GetScreenHeight() - panel_y - 8;
    if (ph > max_ph) ph = max_ph;
    float tabs_y = panel_y + 12 * s;
    float area_y = tabs_y + 56 * s;
    float area_h = ph - 190 * s;
    float area_w = pw - 48 * s;
    float ctrl_cx = panel_x + 24 * s + area_w * 0.55f + area_w * 0.225f;
    float row_cy = area_y + 20.0f; /* row 0 control center */

    switch (g->uitest_phase) {
    case 0: /* settle: menu is up */
        uitest_check(g, "settings screen shown", g->state == WL_STATE_SETTINGS);
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        g->uitest_phase = 1;
        break;
    case 1: /* click the resolution dropdown */
        wl_ui_debug_input(true, ctrl_cx, row_cy, true, true, 0, NULL);
        g->uitest_phase = 2;
        break;
    case 2: /* the list must be open now, on top of the rows below */
        uitest_check(g, "dropdown list opens", g->ui.overlay_open);
        wl_scpy(g->uitest_shot, "/tmp/uitest-settings-dd-open.png",
                sizeof(g->uitest_shot));
        wl_ui_debug_input(true, ctrl_cx, row_cy, false, false, 0, NULL);
        g->uitest_phase = 3;
        break;
    case 3: /* click where the Fullscreen toggle sits UNDER the list: the
             * click must pick a list row (row 1), never the toggle
             * underneath it */
        wl_ui_debug_input(true, ctrl_cx, row_cy + 2 + 36 * s + 32 * s,
                          true, true, 0, NULL);
        g->uitest_phase = 4;
        break;
    case 4: /* the click selected list row 1; the toggle must NOT fire */
        uitest_check(g, "dropdown closes after row click", !g->ui.overlay_open);
        uitest_check(g, "row under list not activated",
                     g->cfg.fullscreen == g->uitest_cfg.fullscreen);
        uitest_check(g, "row click selects that row",
                     g->res_index == 1 && g->cfg.window_width == 1600);
        wl_ui_debug_input(true, ctrl_cx, row_cy, true, true, 0, NULL);
        g->uitest_phase = 5;
        break;
    case 5: /* reopened; now click a neutral spot outside the list */
        uitest_check(g, "dropdown reopens", g->ui.overlay_open);
        wl_ui_debug_input(true, panel_x + 40, area_y + 300, true, true, 0, NULL);
        g->uitest_phase = 6;
        break;
    case 6: /* closed again; scroll the list to the bottom (wheel down) */
        uitest_check(g, "click outside closes dropdown", !g->ui.overlay_open);
        wl_ui_debug_input(true, panel_x + 100, area_y + 100, false, false, -8, NULL);
        g->uitest_phase = 7;
        break;
    case 7: /* scrolled: the View Bobbing row (9) must now be reachable */
        uitest_check(g, "settings list scrolls", g->settings_scroll > 0.0f);
        {
            float vy = area_y + 9 * 52.0f + 20.0f - g->settings_scroll;
            uitest_check(g, "bottom rows visible in panel",
                         vy > area_y && vy < area_y + area_h);
            wl_scpy(g->uitest_shot, "/tmp/uitest-settings-scrolled.png",
                    sizeof(g->uitest_shot));
            wl_ui_debug_input(true, ctrl_cx, vy, true, true, 0, NULL);
        }
        g->uitest_phase = 8;
        break;
    case 8: /* the toggle under the pointer must have flipped */
        uitest_check(g, "off-panel setting toggleable",
                     g->cfg.view_bobbing != g->uitest_cfg.view_bobbing);
        { /* switch to the Controls tab */
            float tab_w = (pw - 24 * s) / 4;
            float tx = panel_x + 12 * s + tab_w * 2 + tab_w / 2;
            wl_ui_debug_input(true, tx, tabs_y + 20 * s, true, true, 0, NULL);
        }
        g->uitest_phase = 9;
        break;
    case 9:
        uitest_check(g, "controls tab opens", g->settings_tab == 2);
        wl_ui_debug_input(true, panel_x + 100, area_y + 100, false, false, -10, NULL);
        g->uitest_phase = 10;
        break;
    case 10: /* all bindings reachable by scrolling */
        wl_scpy(g->uitest_shot, "/tmp/uitest-settings-controls.png",
                sizeof(g->uitest_shot));
        { /* Done button */
            float bx = panel_x + 16 + (pw - 64 * s) / 8;
            float by = panel_y + ph - 60 * s + 23 * s;
            wl_ui_debug_input(true, bx, by, true, true, 0, NULL);
        }
        g->uitest_phase = 11;
        break;
    case 11:
        uitest_check(g, "done returns to main menu",
                     g->state == WL_STATE_MAIN_MENU);
        uitest_finish(g);
        break;
    default:
        uitest_finish(g);
        break;
    }
}

static void uitest_create(WlGame *g)
{
    float s = g->ui.scale;
    static int s_last_phase = -1;
    if (s_last_phase != g->uitest_phase) {
        s_last_phase = g->uitest_phase;
        WL_LOGD("uitest-create: phase=%d name='%s' seed='%s' focus=%d",
                g->uitest_phase, g->menus.new_name, g->menus.new_seed,
                g->ui.focus_textfield);
    }
    int sw = GetScreenWidth();
    float pw = 460 * s;
    float content_h = (4.0f * (34.0f + 44.0f) + 3.0f * 16.0f) * s;
    float btn_h = 46 * s, pad = 24 * s;
    float ph = pad + content_h + 16 * s + btn_h + pad;
    float sh = GetScreenHeight();
    float max_ph = sh - 108 * s - 8;
    if (ph > max_ph) ph = max_ph;
    float panel_x = (sw - pw) / 2, panel_y = 100 * s;
    float x = panel_x + 24 * s, w = pw - 48 * s;
    float cx = x + w / 2;
    float vy0 = panel_y + pad; /* viewport top (scroll is 0 during test) */
    /* group stride: label(34) + control(44) + gap(16) */
    float group_y = vy0 + 34 * s + 22 * s; /* group 0 (name) control center */
    float wt_y = vy0 + (3 * 94 + 34) * s + 22 * s; /* world type control */

    switch (g->uitest_phase) {
    case 0: /* settle; start from empty fields (fixture) */
        uitest_check(g, "create screen shown", g->state == WL_STATE_WORLD_CREATE);
        g->menus.new_name[0] = '\0';
        g->menus.new_seed[0] = '\0';
        g->menus.create_scroll = 0;
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        g->uitest_phase = 1;
        break;
    case 1: /* click the world-name field once */
        wl_ui_debug_input(true, cx, group_y, true, true, 0, NULL);
        g->uitest_phase = 2;
        break;
    case 2: /* focus must SURVIVE the click (regression) */
        uitest_check(g, "name field keeps focus", g->ui.focus_textfield == 1001);
        wl_ui_debug_input(true, cx, group_y, false, false, 0, "My World");
        g->uitest_phase = 3;
        break;
    case 3:
        uitest_check(g, "typed characters appear",
                     wl_str_eq(g->menus.new_name, "My World"));
        wl_ui_debug_input(true, cx, group_y + 94 * s, true, true, 0, NULL);
        g->uitest_phase = 4;
        break;
    case 4: /* the seed field must steal focus only because it was clicked */
        uitest_check(g, "seed field takes focus", g->ui.focus_textfield == 1002);
        wl_ui_debug_input(true, cx, group_y + 94 * s, false, false, 0, "12345");
        g->uitest_phase = 5;
        break;
    case 5:
        uitest_check(g, "seed text entered",
                     wl_str_eq(g->menus.new_seed, "12345"));
        wl_ui_debug_input(true, cx, wt_y, true, true, 0, NULL);
        g->uitest_phase = 6;
        break;
    case 6: /* world type dropdown: open, and no longer overlapped */
        uitest_check(g, "world type dropdown opens", g->ui.overlay_open);
        {
            Rectangle wt = { x, wt_y - 22 * s, w, 44 * s };
            Rectangle back = { panel_x + pw / 2 + 12 * s,
                               panel_y + ph - pad - btn_h,
                               (pw - 72 * s) / 2, btn_h };
            uitest_check(g, "back button does not overlap world type",
                         !CheckCollisionRecs(wt, back));
        }
        wl_scpy(g->uitest_shot, "/tmp/uitest-create-dd-open.png",
                sizeof(g->uitest_shot));
        wl_ui_debug_input(true, cx, wt_y, false, false, 0, NULL);
        g->uitest_phase = 7;
        break;
    case 7: /* pick "Superflat" (row 1): list starts below the control */
        wl_ui_debug_input(true, cx, wt_y + 22 * s + 2 + 32 * s * 1.5f,
                          true, true, 0, NULL);
        g->uitest_phase = 8;
        break;
    case 8:
        uitest_check(g, "world type selected",
                     g->menus.new_type == WL_WORLDTYPE_SUPERFLAT);
        uitest_check(g, "dropdown closed after selection", !g->ui.overlay_open);
        { /* Create World button */
            float bx = panel_x + 24 * s + (pw - 72 * s) / 4;
            float by = panel_y + ph - pad - btn_h / 2;
            wl_ui_debug_input(true, bx, by, true, true, 0, NULL);
        }
        g->uitest_phase = 9;
        break;
    case 9: /* the world must be created and load */
        uitest_check(g, "create world enters loading",
                     g->state == WL_STATE_LOADING || g->state == WL_STATE_PLAYING);
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        g->uitest_wait = 0;
        g->uitest_phase = 10;
        break;
    case 10: /* wait for the world to finish streaming */
        if (g->state == WL_STATE_PLAYING || ++g->uitest_wait > 900) {
            uitest_check(g, "world loads and plays",
                         g->state == WL_STATE_PLAYING &&
                         wl_str_eq(wl_world_name(g->world), "My World"));
            wl_scpy(g->uitest_shot, "/tmp/uitest-create-world.png",
                    sizeof(g->uitest_shot));
            uitest_finish(g);
        } else {
            wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        }
        break;
    default:
        uitest_finish(g);
        break;
    }
}

/* --uitest pause: drives ESC + the pause-menu buttons through the real
 * handlers: open/close via ESC and Resume, simulation freeze while
 * paused, Options round-trip (world kept), Save and Quit to Title. */

/* center Y of pause-menu button i, mirroring wl_menu_pause()'s layout */
static float uitest_pause_btn_y(const WlGame *g, int i)
{
    float s = g->ui.scale;
    float btn = 46.0f * s, gap = 12.0f * s, pad = 24.0f * s;
    float py = GetScreenHeight() * 0.30f;
    return py + pad + i * (btn + gap) + btn / 2;
}

static void uitest_pause(WlGame *g)
{
    float s = g->ui.scale;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    /* mirrors wl_menu_pause() geometry */
    float pw = 380.0f * s;
    float px = (sw - pw) / 2;
    float bx = px + pw / 2;
    float by0 = uitest_pause_btn_y(g, 0);
    static uint32_t t0 = 0;
    static Vector3 p0 = { 0 };

    switch (g->uitest_phase) {
    case 0: /* wait for the world to finish streaming */
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        if (g->state == WL_STATE_PLAYING || ++g->uitest_wait > 900) {
            uitest_check(g, "world enters playing", g->state == WL_STATE_PLAYING);
            uitest_check(g, "cursor captured while playing", IsCursorHidden());
            g->inject_key = g->keys.key[WL_ACT_PAUSE]; /* ESC pauses */
            g->uitest_phase = 1;
        }
        break;
    case 1: /* the pause menu must be up, cursor visible, world frozen */
        uitest_check(g, "ESC opens the pause menu", g->state == WL_STATE_PAUSED);
        uitest_check(g, "cursor shown while paused", !IsCursorHidden());
        uitest_check(g, "ESC did not close the game", !g->quit);
        t0 = wl_world_time(g->world);
        p0 = g->player.pos;
        wl_scpy(g->uitest_shot, "/tmp/uitest-pause-menu.png", sizeof(g->uitest_shot));
        wl_ui_debug_input(true, bx, by0, true, true, 0, NULL); /* Resume */
        g->uitest_phase = 2;
        break;
    case 2:
        uitest_check(g, "Resume Game returns to playing",
                     g->state == WL_STATE_PLAYING);
        uitest_check(g, "cursor recaptured on resume", IsCursorHidden());
        g->inject_key = g->keys.key[WL_ACT_PAUSE];
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        g->uitest_phase = 3;
        break;
    case 3: /* paused again; the simulation must be frozen for a while */
        uitest_check(g, "ESC re-opens the pause menu", g->state == WL_STATE_PAUSED);
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        if (++g->uitest_wait > 30) {
            uitest_check(g, "world time frozen while paused",
                         wl_world_time(g->world) == t0);
            uitest_check(g, "player frozen while paused",
                         g->player.pos.x == p0.x && g->player.pos.y == p0.y &&
                         g->player.pos.z == p0.z);
            g->inject_key = g->keys.key[WL_ACT_PAUSE]; /* ESC resumes */
            g->uitest_phase = 4;
        }
        break;
    case 4:
        uitest_check(g, "ESC resumes from pause", g->state == WL_STATE_PLAYING);
        g->inject_key = g->keys.key[WL_ACT_PAUSE];
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        g->uitest_phase = 5;
        break;
    case 5: /* open Options from the pause menu */
        uitest_check(g, "pause menu up again", g->state == WL_STATE_PAUSED);
        wl_ui_debug_input(true, bx, uitest_pause_btn_y(g, 1), true, true, 0, NULL); /* Options */
        g->uitest_phase = 6;
        break;
    case 6: /* settings open over the kept world session */
        uitest_check(g, "Options opens settings", g->state == WL_STATE_SETTINGS);
        uitest_check(g, "world kept while in options", g->world != NULL);
        { /* click the settings Done button (mirrors wl_menu_settings) */
            float spw = 640.0f * s, sph = 520.0f * s;
            float max_ph = (float)sh - 76.0f * s - 8;
            if (sph > max_ph) sph = max_ph;
            float dx = (sw - spw) / 2 + 16.0f + (spw - 64.0f * s) / 4 / 2;
            float dy = 76.0f * s + sph - 60.0f * s + 23.0f * s;
            wl_ui_debug_input(true, dx, dy, true, true, 0, NULL);
        }
        g->uitest_phase = 7;
        break;
    case 7: /* Done must return to the pause menu, not the title */
        uitest_check(g, "Done returns to the pause menu", g->state == WL_STATE_PAUSED);
        uitest_check(g, "world still loaded after options", g->world != NULL);
        wl_ui_debug_input(true, bx, uitest_pause_btn_y(g, 2), true, true, 0, NULL); /* Save+Quit */
        g->uitest_phase = 8;
        break;
    case 8: /* saved, unloaded, back on the title screen */
        uitest_check(g, "Save and Quit to Title returns to the main menu",
                     g->state == WL_STATE_MAIN_MENU && g->world == NULL);
        uitest_check(g, "session save succeeded", !g->last_save_failed);
        if (g->state != WL_STATE_MAIN_MENU || g->world != NULL) {
            /* save failed: the session must have been KEPT (not silently
             * discarded) — that is the behaviour under test */
            uitest_check(g, "failed save keeps the world session", g->world != NULL);
            uitest_finish(g);
            break;
        }
        {
            char path[WL_PATH_MAX], wdir[WL_PATH_MAX];
            bool files = wl_worlds_dir_path(wdir, sizeof(wdir), "PauseUITest") &&
                         wl_path_join(path, sizeof(path), wdir, "level.dat") &&
                         wl_file_exists(path);
            files = files && wl_path_join(path, sizeof(path), wdir, "player.dat") &&
                    wl_file_exists(path);
            uitest_check(g, "pause quit wrote session files", files);
        }
        /* re-enter for the Quit to Desktop leg */
        if (wl_game_enter_world(g, &g->entry)) {
            g->uitest_wait = 0;
            g->uitest_phase = 9;
        } else {
            uitest_check(g, "re-enter world for quit test", false);
            uitest_finish(g);
        }
        break;
    case 9: /* wait for the re-entered world */
        wl_ui_debug_input(true, 0, 0, false, false, 0, NULL);
        if (g->state == WL_STATE_PLAYING || ++g->uitest_wait > 900) {
            uitest_check(g, "world re-enters playing", g->state == WL_STATE_PLAYING);
            g->inject_key = g->keys.key[WL_ACT_PAUSE];
            g->uitest_phase = 10;
        }
        break;
    case 10:
        uitest_check(g, "pause menu up for quit test", g->state == WL_STATE_PAUSED);
        wl_ui_debug_input(true, bx, uitest_pause_btn_y(g, 3), true, true, 0, NULL);
        g->uitest_phase = 11;
        break;
    /* phase 11 is verified post-dispatch by uitest_pause_quit_checks():
     * the Quit to Desktop button sets g->quit during the frame, so the
     * main loop would exit before this driver ran again */
    default:
        uitest_finish(g);
        break;
    }
}

/* Quit-to-desktop leg, checked right after the button fired this frame */
static void uitest_pause_quit_checks(WlGame *g)
{
    g->uitest_phase = 12; /* one shot */
    uitest_check(g, "Quit to Desktop saved the session", !g->last_save_failed);
    uitest_check(g, "Quit to Desktop requests shutdown", g->quit);
    {
        char path[WL_PATH_MAX], wdir[WL_PATH_MAX];
        bool files = wl_worlds_dir_path(wdir, sizeof(wdir), "PauseUITest") &&
                     wl_path_join(path, sizeof(path), wdir, "player.dat") &&
                     wl_file_exists(path);
        uitest_check(g, "quit-to-desktop wrote player data", files);
    }
    uitest_finish(g);
}

static void uitest_frame(WlGame *g)
{
    if (g->uitest == 3) uitest_pause(g);
    else if (g->uitest == 2) uitest_create(g);
    else uitest_settings(g);
}

bool wl_game_key_pressed(WlGame *g, int key)
{
    if (IsKeyPressed(key)) return true;
    if (key > 0 && g->inject_key == key) {
        g->inject_key = 0; /* one shot */
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* per-state frames                                                    */
/* ------------------------------------------------------------------ */

static void loading_frame(WlGame *g)
{
    /* stream first, then measure: on the very first frame no chunks exist
     * yet, so a pending check before updating would report a false zero */
    wl_world_update_ex(g->world, g->player.pos.x, g->player.pos.z, 24, 12);

    int pending = wl_world_pending(g->world, g->player.pos.x, g->player.pos.z);
    if (g->load_initial_pending < 0)
        g->load_initial_pending = pending > 0 ? pending : 1;

    if (pending == 0) {
        /* lift the player out of terrain if a save placed them inside */
        int px = (int)floorf(g->player.pos.x), pz = (int)floorf(g->player.pos.z);
        int py = (int)floorf(g->player.pos.y);
        int guard = 0;
        while (guard++ < 130 &&
               (wl_world_solid_at(g->world, px, py, pz) ||
                wl_world_solid_at(g->world, px, py + 1, pz)))
            py++;
        if (g->player.pos.y < (float)py) g->player.pos.y = (float)py + 0.01f;

        g->state = WL_STATE_PLAYING;
        if (g->demo_zoo) {
            /* test automation: one of each animal in a fixed ring,
             * placed on the generated surface; the cow sits straight
             * ahead as a deterministic attack target */
            g->demo_zoo = false; /* once */
            float zx = g->player.pos.x, zz = g->player.pos.z;
            struct { WlEntityType t; float dx, dz; } ring[] = {
                { WL_ENT_COW, 0.0f, 5.0f },
                { WL_ENT_PIG, -4.0f, 3.0f },
                { WL_ENT_SHEEP, 4.0f, 3.0f },
                { WL_ENT_CHICKEN, 1.5f, 6.0f },
                { WL_ENT_SPIDER, -7.0f, 4.0f },
            };
            for (size_t i = 0; i < sizeof(ring) / sizeof(ring[0]); i++) {
                int ex = (int)(zx + ring[i].dx), ez = (int)(zz + ring[i].dz);
                int top = wl_world_top_at(g->world, ex, ez);
                if (top <= 0) continue;
                /* skip water columns: entities spawned over water sink */
                if (wl_blocks_liquid(wl_world_get_block(g->world, ex, top, ez)))
                    continue;
                WlEntity *ze = wl_world_spawn_entity(g->world, ring[i].t,
                                      (Vector3){ zx + ring[i].dx,
                                                 (float)top + 1.2f,
                                                 zz + ring[i].dz });
                WL_LOGD("zoo: type %d at %.1f,%.1f,%.1f (top=%d)",
                        (int)ring[i].t, zx + ring[i].dx, (float)top + 1.2f,
                        zz + ring[i].dz, top);
                (void)ze;
            }
            WL_LOGI("zoo spawned around the player");
        }
        if (g->demo_time >= 0 && g->state == WL_STATE_PLAYING) {
            /* test automation: jump to a time of day (13300 = night) */
            wl_world_set_time(g->world, (uint32_t)g->demo_time);
            g->demo_time = -1;
        }
        if (g->demo_dir_set) { /* test automation: fixed camera direction */
            g->player.yaw = g->demo_yaw;
            g->player.pitch = g->demo_pitch;
        }
        if (g->demo_frames <= 0 && !g->selftest) DisableCursor();

        /* runtime self test takes over from here */
        if (g->selftest) {
            run_selftest(g);
            return;
        }

        /* test automation: open the inventory/crafting overlay */
        if (g->demo_overlay >= 2) {
            wl_inv_open_craft(&g->player.inv, g->demo_overlay);
            g->overlay = g->demo_overlay == 3 ? WL_OVERLAY_CRAFTING
                                              : WL_OVERLAY_INVENTORY;
            EnableCursor();
        }
        return;
    }

    /* draw loading screen */
    int w = GetScreenWidth(), h = GetScreenHeight();
    BeginDrawing();
    ClearBackground((Color){ 16, 18, 24, 255 });
    Texture logo = wl_assets_tex(g->assets, WL_TEX_GUI_LOGO);
    float lw = (float)logo.width * 1.5f, lh = (float)logo.height * 1.5f;
    Rectangle src = { 0, 0, (float)logo.width, (float)logo.height };
    DrawTexturePro(logo, src, (Rectangle){ (w - lw) / 2, h * 0.30f, lw, lh },
                   (Vector2){ 0, 0 }, 0.0f, WHITE);

    Font font = wl_ui_font(&g->ui);
    const char *line = TextFormat("Preparing %s ...", g->entry.meta.name);
    float fs = 28.0f * g->ui.scale;
    Vector2 ts = MeasureTextEx(font, line, fs, 1.0f);
    DrawTextEx(font, line, (Vector2){ (w - ts.x) / 2, h * 0.55f }, fs, 1.0f, RAYWHITE);

    int bar_w = (int)(w * 0.5f), bar_x = (w - bar_w) / 2, bar_y = (int)(h * 0.65f);
    float frac = g->load_initial_pending > 0
        ? 1.0f - (float)pending / (float)g->load_initial_pending : 1.0f;
    if (frac < 0) frac = 0;
    DrawRectangle(bar_x, bar_y, bar_w, 18, (Color){ 40, 44, 52, 255 });
    DrawRectangle(bar_x + 2, bar_y + 2, (int)((bar_w - 4) * frac), 14,
                  (Color){ 106, 170, 64, 255 });
    DrawRectangleLines(bar_x, bar_y, bar_w, 18, (Color){ 70, 76, 86, 255 });
    uitest_maybe_shot(g);
    EndDrawing();

    wl_audio_update(&g->audio, GetFrameTime(), 0, false, false);
}

static void play_frame(WlGame *g)
{
    float dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;
    WlPlayer *p = &g->player;

    bool overlay_open = (g->overlay == WL_OVERLAY_INVENTORY ||
                         g->overlay == WL_OVERLAY_CRAFTING ||
                         g->overlay == WL_OVERLAY_FURNACE);

    /* Pausing must NOT return early: raylib polls input inside
     * EndDrawing(), so a frame that skips it never sees the key release
     * and the very same ESC edge is still "pressed" on the next frame —
     * where the pause menu's own ESC handler would instantly resume.
     * Instead we open the pause state, skip input+simulation and still
     * present this frame. */
    bool pause_now = false;

    /* pause when the window loses focus */
    if (g->cfg.pause_on_focus_loss && !IsWindowFocused() && g->demo_frames <= 0) {
        g->state = WL_STATE_PAUSED;
        EnableCursor();
        pause_now = true;
    }

    /* global keys */
    if (!pause_now && wl_game_key_pressed(g, g->keys.key[WL_ACT_PAUSE])) {
        if (overlay_open) {
            /* ESC closes any overlay first; it must not open the pause
             * menu (or anything else) in the same press */
            if (g->overlay == WL_OVERLAY_FURNACE) wl_furnace_ui_close(g);
            else wl_inv_ui_close(g);
            DisableCursor();
        } else if (!p->dead) {
            g->state = WL_STATE_PAUSED;
            EnableCursor();
            pause_now = true;
        }
    }
    if (!pause_now && IsKeyPressed(g->keys.key[WL_ACT_INVENTORY]) && !p->dead) {
        if (overlay_open) {
            if (g->overlay == WL_OVERLAY_FURNACE) wl_furnace_ui_close(g);
            else wl_inv_ui_close(g);
            DisableCursor();
        } else {
            wl_inv_open_craft(&p->inv, 2);
            g->overlay = WL_OVERLAY_INVENTORY;
            EnableCursor();
        }
    }
    if (p->dead) {
        g->overlay = WL_OVERLAY_DEATH;
        /* survival: drop the whole inventory into the world exactly once
         * per death (creative keeps it). Partial failures retry next
         * frame with only the remaining stacks, so nothing is lost or
         * duplicated. */
        if (!g->death_dropped &&
            wl_world_gamemode(g->world) == WL_WORLD_GAMEMODE_SURVIVAL)
            g->death_dropped = wl_game_drop_inventory(g);
        if (IsKeyPressed(KEY_R)) {
            wl_player_respawn(p, g->spawn);
            g->overlay = WL_OVERLAY_NONE;
            g->death_dropped = false;
        }
    } else if (g->overlay == WL_OVERLAY_DEATH) {
        g->overlay = WL_OVERLAY_NONE;
    }

    bool input_active = !overlay_open && !p->dead && !pause_now;

    /* hotbar */
    if (input_active) {
        int prev_sel = p->selected_slot;
        int wheel = (int)GetMouseWheelMove();
        if (wheel != 0)
            p->selected_slot = ((p->selected_slot - wheel) % WL_HOTBAR_SIZE +
                                WL_HOTBAR_SIZE) % WL_HOTBAR_SIZE;
        for (int i = 0; i < WL_HOTBAR_SIZE; i++)
            if (IsKeyPressed(KEY_ONE + i)) p->selected_slot = i;
        if (p->selected_slot != prev_sel) {
            g->item_toast = 1.6f;
            const WlItem *it = wl_item_get(p->inv.slots[p->selected_slot].item);
            wl_scpy(g->item_toast_name, it ? it->display : "", sizeof(g->item_toast_name));
        }

        /* Q: drop one item from the selected stack; hold Shift to throw
         * the whole stack (documented in Settings > Controls) */
        if (wl_game_key_pressed(g, g->keys.key[WL_ACT_DROP])) {
            WlSlot *slot = &p->inv.slots[p->selected_slot];
            if (slot->item != WL_ITEM_NONE) {
                bool whole_stack = IsKeyDown(KEY_LEFT_SHIFT) ||
                                   IsKeyDown(KEY_RIGHT_SHIFT);
                uint8_t n = whole_stack ? slot->count : 1;
                Vector3 eye = wl_player_eye(p);
                Vector3 look = wl_player_look_dir(p);
                Vector3 pos = { eye.x + look.x * 0.9f, eye.y - 0.25f,
                                eye.z + look.z * 0.9f };
                Vector3 vel = { look.x * 5.0f, 1.6f + look.y * 4.0f, look.z * 5.0f };
                if (wl_world_add_drop_thrown(g->world, slot->item, n,
                                             whole_stack ? slot->dur : 0,
                                             pos, vel, 1.3f)) {
                    const WlItem *it = wl_item_get(slot->item);
                    WL_LOGD("dropped %d x %s%s", (int)n,
                            it ? it->name : "?",
                            whole_stack ? " (stack)" : "");
                    slot->count = (uint8_t)(slot->count - n);
                    if (slot->count == 0) {
                        slot->item = WL_ITEM_NONE;
                        slot->dur = 0;
                    }
                    wl_audio_play_v(&g->audio, WL_SND_THROW, 0.10f);
                }
            }
        }

        Vector2 md = GetMouseDelta();

        /* raylib's EnableCursor()/DisableCursor() both warp the cursor to
         * the window center; the first deltas after such a warp ARE the
         * warp, and applying them would snap the camera in a random
         * direction whenever a menu closes. Swallow them. */
        bool cursor_hidden = IsCursorHidden();
        if (cursor_hidden != g->cursor_hidden_last) g->look_guard = 2;
        g->cursor_hidden_last = cursor_hidden;
        if (g->look_guard > 0) {
            md.x = 0.0f;
            md.y = 0.0f;
            g->look_guard--;
        }
        wl_player_apply_look(p, md.x, md.y);

        /* scripted camera motion (--lookspin/--looknod): synthetic deltas
         * through the real mouse-look path, so automated runs reproduce
         * movement-triggered rendering behavior exactly */
        if (g->demo_lookspin != 0.0f || g->demo_looknod != 0.0f) {
            float sens = 0.0022f * g->cfg.mouse_sensitivity;
            if (sens < 0.0001f) sens = 0.0001f;
            if (g->demo_lookspin != 0.0f) {
                float rad = g->demo_lookspin * 0.017453293f;
                wl_player_apply_look(p, -rad / sens, 0.0f);
            }
            if (g->demo_looknod != 0.0f) {
                /* pitch oscillates +-amp: the per-frame delta is the
                 * derivative d/dt[amp*cos(2*pi*t/period)] */
                float t = (float)(g->demo_frames % 120) * 0.0523599f; /* 6° steps */
                float rad = -sinf(t) * g->demo_looknod * 0.017453293f * 0.10472f;
                wl_player_apply_look(p, 0.0f, -rad / sens);
            }
        }
    }
    if (g->item_toast > 0.0f) g->item_toast -= dt;

    /* simulation (skipped entirely on the frame that opens the pause
     * menu; paused_frame keeps the world frozen from the next frame on) */
    if (!pause_now) {
        if (g->demo_walk) wl_keybinds_debug_hold(WL_ACT_FORWARD, true);
        wl_player_update(p, g->world, &g->keys, &g->cfg, &g->audio, dt, input_active,
                         wl_world_gamemode(g->world));
        if (!p->dead) {
            int act = wl_interact_update(p, g->world, &g->cfg, &g->audio, dt, input_active);
            if (act == WL_INTERACT_OPEN_CRAFT && input_active) {
                wl_inv_open_craft(&p->inv, 3);
                g->overlay = WL_OVERLAY_CRAFTING;
                EnableCursor();
            } else if (act == WL_INTERACT_OPEN_FURNACE && input_active) {
                WlTarget ft = wl_interact_query(g->world, p);
                if (ft.valid)
                    wl_furnace_ui_open(g, ft.x, ft.y, ft.z);
            }
        }
        wl_world_advance_time(g->world, dt);
        wl_world_update(g->world, p->pos.x, p->pos.z);
        wl_world_water_tick(g->world, dt);
        wl_world_update_drops(g->world, p, dt);
        if (wl_world_consume_pickups(g->world) > 0)
            wl_audio_play_v(&g->audio, WL_SND_PICKUP, 0.18f);
        wl_world_update_entities(g->world, p, &g->cfg, &g->audio, dt);
        wl_furnaces_update(g->world, dt);
        {
            int fx, fy, fz, kind;
            while (wl_furnace_pop_event(&fx, &fy, &fz, &kind)) {
                Vector3 fpos = { fx + 0.5f, fy + 0.5f, fz + 0.5f };
                Vector3 ear = wl_player_eye(p);
                wl_audio_play_at(&g->audio,
                                 kind == 1 ? WL_SND_FURNACE_IGNITE
                                           : WL_SND_FURNACE_DONE,
                                 fpos, ear, 28.0f);
            }
        }
        /* underground = no skylight on the player and below cave depth:
         * drives the cave ambience instead of surface birds/crickets */
        bool underground =
            p->pos.y < 56.0f &&
            wl_light_sky(g->world, (int)p->pos.x, (int)(p->pos.y + 0.5f),
                         (int)p->pos.z) == 0;
        wl_audio_update(&g->audio, dt, wl_world_time(g->world), true,
                        underground);

        /* autosave */
        g->autosave_timer += dt;
        if (g->cfg.autosave_interval > 0 &&
            g->autosave_timer >= (double)g->cfg.autosave_interval) {
            g->autosave_timer = 0.0;
            wl_game_save_session(g);
        }
    }

    /* draw */
    wl_renderer_begin(&g->rend, p, &g->cfg, wl_world_time(g->world),
                      p->head_in_water, wl_world_render_distance(g->world));
    BeginDrawing();
    if (g->demo_itemgallery) {
        /* test automation: replace the world view with the item audit
         * (keeps the demo-frame bookkeeping below running so --frames
         * still ends the run) */
        wl_renderer_draw_item_gallery(&g->rend);
        wl_hud_draw(g);
        wl_inv_ui_draw(g);
        uitest_maybe_shot(g);
        EndDrawing();
        wl_audio_update(&g->audio, GetFrameTime(), 0, false, false);
        if (g->demo_frames > 0 && --g->demo_frames <= 0) g->quit = true;
        return;
    }
    wl_renderer_draw_world(&g->rend, g->world, &g->cfg, wl_world_time(g->world),
                           p->head_in_water);
    wl_renderer_draw_drops(&g->rend, g->world, wl_world_time(g->world));
    wl_renderer_draw_entities(&g->rend, g->world);
    if (p->break_progress > 0.0)
        wl_renderer_draw_break_overlay(&g->rend, g->world,
                                       p->break_x, p->break_y, p->break_z,
                                       (float)p->break_progress);
    if (!p->dead) wl_renderer_draw_held_item(&g->rend, g->world, p);
    wl_hud_draw(g);
    wl_inv_ui_draw(g);
    wl_furnace_ui_draw(g);
    uitest_maybe_shot(g);
    EndDrawing();

    /* demo diagnostics + frame countdown */
    if (g->demo_frames > 0) {
        static int diag = 0;
        if (diag++ % 30 == 0) {
            Vector2 dbg_md = GetMouseDelta();
            WL_LOGD("demo mouse: delta %.1f,%.1f cursor_hidden=%d overlay=%d "
                    "sfx played=%u rejected=%u",
                    dbg_md.x, dbg_md.y, (int)IsCursorHidden(), (int)g->overlay,
                    g->audio.play_count, g->audio.play_reject);
            {
                int en = 0;
                const WlEntity *ee = wl_world_entities(g->world, &en);
                int alive = 0;
                for (int q = 0; q < en; q++)
                    if (ee && ee[q].active) alive++;
                WL_LOGD("demo ents: total=%d alive=%d", en, alive);
            }
            WlWorldStats st = wl_world_stats(g->world);
            WL_LOGD("demo pos %.1f,%.1f,%.1f yaw %.2f pitch %.2f drawn %d tris %ld pending %d | "
                    "%.0f fps, %.2f ms avg, worst %.1f ms, rss %d MiB | "
                    "gen %d chks %.2f ms avg, mesh %.2f ms avg",
                    p->pos.x, p->pos.y, p->pos.z, p->yaw, p->pitch,
                    g->rend.drawn_chunks,
                    g->rend.drawn_tris,
                    wl_world_pending(g->world, p->pos.x, p->pos.z),
                    (float)GetFPS(), g->ft_ema_ms, g->ft_worst_ms,
                    wl_util_rss_mb(),
                    st.gen_count,
                    st.gen_count > 0 ? st.gen_ms_total / (double)st.gen_count : 0.0,
                    st.mesh_ms_ema);
        }
        if (--g->demo_frames <= 0) g->quit = true;
    }
}

static void paused_frame(WlGame *g)
{
    /* draw the frozen world behind the menu */
    wl_renderer_begin(&g->rend, &g->player, &g->cfg, wl_world_time(g->world),
                      g->player.head_in_water, wl_world_render_distance(g->world));
    BeginDrawing();
    wl_renderer_draw_world(&g->rend, g->world, &g->cfg, wl_world_time(g->world),
                           g->player.head_in_water);
    wl_renderer_draw_drops(&g->rend, g->world, wl_world_time(g->world));
    wl_renderer_draw_entities(&g->rend, g->world);
    if (!g->player.dead) wl_renderer_draw_held_item(&g->rend, g->world, &g->player);
    wl_hud_draw(g);
    wl_menu_pause(g);
    uitest_maybe_shot(g);
    EndDrawing();
    wl_audio_update(&g->audio, GetFrameTime(), wl_world_time(g->world), false, false);
}

void wl_game_run_frame(WlGame *g)
{
    if (g->quit) return;

    /* UI runtime test: drives the menus through the input seam */
    if (g->uitest) uitest_frame(g);

    /* deferred audio init: the first frame is already on screen, so even a
     * slow audio device probe cannot make the game look like it hangs */
    if (g->audio_pending) {
        g->audio_pending = false;
        double ta = stage_mark();
        wl_audio_init(&g->audio, g->assets);
        if (g->audio.enabled) {
            /* sounds were loaded during startup, BEFORE the device
             * existed (deferred init): every LoadSound came back empty.
             * Reload them now that the device is up, or the game stays
             * completely silent forever. */
            wl_assets_reload_audio(g->assets);
        }
        wl_audio_set_volumes(&g->audio, g->cfg.vol_master, g->cfg.vol_music,
                             g->cfg.vol_sfx, g->cfg.vol_ambient);
        WL_LOGI("startup: audio device %s (%.1f ms); volumes "
                "master %.2f music %.2f sfx %.2f ambient %.2f",
                g->audio.enabled ? "ready" : "unavailable, running silent",
                stage_mark() - ta, g->cfg.vol_master, g->cfg.vol_music,
                g->cfg.vol_sfx, g->cfg.vol_ambient);
    }

    wl_ui_begin(&g->ui, g->assets, &g->cfg);

    /* global utility keys work in every state (F3 debug overlay, F2
     * screenshot — capturing the pause menu or overlays must work too) */
    if (g->world && wl_game_key_pressed(g, g->keys.key[WL_ACT_DEBUG]))
        g->cfg.show_debug_info = !g->cfg.show_debug_info;
    if (g->world && wl_game_key_pressed(g, g->keys.key[WL_ACT_SCREENSHOT])) {
        char path[WL_PATH_MAX];
        wl_path_screenshots(path, sizeof(path),
                            TextFormat("wildlands-%ld.png", (long)time(NULL)));
        TakeScreenshot(path);
        WL_LOGI("screenshot saved: %s", path);
    }

    /* menu states present their own frame (the in-game states wrap their
     * drawing in BeginDrawing/EndDrawing internally) */
    switch (g->state) {
    case WL_STATE_MAIN_MENU:
    case WL_STATE_WORLD_SELECT:
    case WL_STATE_WORLD_CREATE:
    case WL_STATE_SETTINGS:
    case WL_STATE_MODS: {
        BeginDrawing();
        ClearBackground((Color){ 12, 16, 24, 255 });
        switch (g->state) {
        case WL_STATE_MAIN_MENU:    wl_menu_main(g); break;
        case WL_STATE_WORLD_SELECT: wl_menu_world_select(g); break;
        case WL_STATE_WORLD_CREATE: wl_menu_world_create(g); break;
        case WL_STATE_SETTINGS:     wl_menu_settings(g); break;
        default:                    wl_menu_mods(g); break;
        }
        /* the open dropdown list renders above every other control */
        wl_ui_draw_overlay(&g->ui);
        uitest_maybe_shot(g);
        EndDrawing();
        break;
    }
    case WL_STATE_LOADING:      loading_frame(g); break;
    case WL_STATE_PLAYING:      play_frame(g); break;
    case WL_STATE_PAUSED:       paused_frame(g); break;
    default:                    g->state = WL_STATE_MAIN_MENU; break;
    }
    g->inject_key = 0; /* drop injections no handler consumed this frame */

    /* pause-uitest quit leg: the button fires during dispatch above and
     * sets g->quit, so the main loop would exit before the driver could
     * observe it — verify here, same frame */
    if (g->uitest == 3 && g->uitest_phase == 11) uitest_pause_quit_checks(g);

    wl_ui_end(&g->ui);

    /* frame-time statistics (skip the first frame: it includes init) */
    if (g->first_frame_done) {
        float ft = GetFrameTime() * 1000.0f;
        g->ft_ema_ms += (ft - g->ft_ema_ms) * 0.08f;
        g->ft_hist[g->ft_hist_next] = ft;
        g->ft_hist_next = (g->ft_hist_next + 1) % 128;
        if (g->ft_hist_count < 128) g->ft_hist_count++;
        float worst = 0.0f;
        for (int i = 0; i < g->ft_hist_count; i++)
            if (g->ft_hist[i] > worst) worst = g->ft_hist[i];
        g->ft_worst_ms = worst;
    } else {
        g->first_frame_done = true;
        g->first_frame_ms = stage_mark() - g->init_started_ms;
        WL_LOGI("startup: first frame presented after %.1f ms", g->first_frame_ms);
    }

    if (g->demo_frames > 0 && g->demo_menu) {
        if (--g->demo_frames <= 0) g->quit = true;
        return;
    }
    if (g->state != WL_STATE_PLAYING && g->state != WL_STATE_LOADING &&
        g->state != WL_STATE_PAUSED && g->demo_frames > 0) {
        /* in-game demo runs play/loading/pause only; other menus mean
         * something broke. PAUSED is legitimate (ESC via real or scripted
         * input) and freezes the frame budget until resumed. */
        g->demo_frames = 0;
        g->quit = true;
    }
}
