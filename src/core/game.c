/* game.c — application lifecycle, state machine, world session management. */
#include "wl/game.h"
#include "wl/menus.h"
#include "wl/hud.h"
#include "wl/inv_ui.h"
#include "wl/crafting.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/log.h"
#include "wl/paths.h"
#include "wl/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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
        } else if (wl_str_eq(argv[i], "--menu") && i + 1 < argc) {
            /* test automation: show a menu for --frames, no world needed */
            g->demo_menu = true;
            wl_scpy(g->demo_start_menu, argv[++i], sizeof(g->demo_start_menu));
        } else if (wl_str_eq(argv[i], "--overlay") && i + 1 < argc) {
            g->demo_overlay = wl_str_eq(argv[++i], "craft") ? 3 : 2;
        } else if (wl_str_eq(argv[i], "--selftest")) {
            g->selftest = true;
        } else if (wl_str_eq(argv[i], "--help") || wl_str_eq(argv[i], "-h")) {
            printf("Wildlands %s\n"
                   "  --world <name>        play this world (created if missing)\n"
                   "  --seed <n>            seed for new worlds\n"
                   "  --gamemode <m>        survival | creative (new worlds)\n"
                   "  --type <t>            standard | superflat (new worlds)\n"
                   "  --frames <n>          demo mode: exit after n frames\n"
                   "  --screenshot <path>   save a screenshot when exiting\n"
                   "  --width/--height <n>  window size override\n"
                   "  --menu <screen>       test: show menu (main|worlds|create|settings)\n"
                   "  --overlay <inv|craft> test: open the inventory overlay in-world\n"
                   "  --selftest            runtime self test: mine/place/save/exit\n",
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
    }
    return true;
}

void wl_game_save_session(WlGame *g)
{
    if (!g->world) return;
    double now = GetTime();
    wl_worlds_save_session(g->world, &g->player, g->spawn, now - g->session_start);
    g->session_start = now;
}

void wl_game_exit_world(WlGame *g)
{
    if (!g->world) return;
    wl_game_save_session(g);
    wl_world_destroy(g->world);
    g->world = NULL;
    g->state = WL_STATE_MAIN_MENU;
    g->overlay = WL_OVERLAY_NONE;
    EnableCursor();
    wl_audio_stop_music(&g->audio);
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

    /* 7. return to the main menu */
    wl_game_exit_world(g);
    selftest_check(g, "exit to main menu",
                   g->state == WL_STATE_MAIN_MENU && g->world == NULL);

    WL_LOGI("SELFTEST %s", g->selftest_failed ? "FAILED" : "PASSED");
    g->quit = true;
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
    EndDrawing();

    wl_audio_update(&g->audio, GetFrameTime(), 0, false);
}

static void play_frame(WlGame *g)
{
    float dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;
    WlPlayer *p = &g->player;

    bool overlay_open = (g->overlay == WL_OVERLAY_INVENTORY ||
                         g->overlay == WL_OVERLAY_CRAFTING);

    /* pause when the window loses focus */
    if (g->cfg.pause_on_focus_loss && !IsWindowFocused() && g->demo_frames <= 0) {
        g->state = WL_STATE_PAUSED;
        EnableCursor();
        return;
    }

    /* global keys */
    if (IsKeyPressed(g->keys.key[WL_ACT_PAUSE])) {
        if (overlay_open) {
            wl_inv_ui_close(g);
            DisableCursor();
        } else if (!p->dead) {
            g->state = WL_STATE_PAUSED;
            EnableCursor();
            return;
        }
    }
    if (IsKeyPressed(g->keys.key[WL_ACT_INVENTORY]) && !p->dead) {
        if (overlay_open) {
            wl_inv_ui_close(g);
            DisableCursor();
        } else {
            wl_inv_open_craft(&p->inv, 2);
            g->overlay = WL_OVERLAY_INVENTORY;
            EnableCursor();
        }
    }
    if (IsKeyPressed(g->keys.key[WL_ACT_DEBUG])) g->cfg.show_debug_info = !g->cfg.show_debug_info;
    if (IsKeyPressed(g->keys.key[WL_ACT_SCREENSHOT])) {
        char path[WL_PATH_MAX];
        wl_path_screenshots(path, sizeof(path),
                            TextFormat("wildlands-%ld.png", (long)time(NULL)));
        TakeScreenshot(path);
        WL_LOGI("screenshot saved: %s", path);
    }
    if (p->dead) {
        g->overlay = WL_OVERLAY_DEATH;
        if (IsKeyPressed(KEY_R)) {
            wl_player_respawn(p, g->spawn);
            g->overlay = WL_OVERLAY_NONE;
        }
    } else if (g->overlay == WL_OVERLAY_DEATH) {
        g->overlay = WL_OVERLAY_NONE;
    }

    bool input_active = !overlay_open && !p->dead;

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

        Vector2 md = GetMouseDelta();
        wl_player_apply_look(p, md.x, md.y);
    }
    if (g->item_toast > 0.0f) g->item_toast -= dt;

    /* simulation */
    wl_player_update(p, g->world, &g->keys, &g->cfg, &g->audio, dt, input_active,
                     wl_world_gamemode(g->world));
    if (!p->dead) {
        int act = wl_interact_update(p, g->world, &g->cfg, &g->audio, dt, input_active);
        if (act == WL_INTERACT_OPEN_CRAFT && input_active) {
            wl_inv_open_craft(&p->inv, 3);
            g->overlay = WL_OVERLAY_CRAFTING;
            EnableCursor();
        }
    }
    wl_world_advance_time(g->world, dt);
    wl_world_update(g->world, p->pos.x, p->pos.z);
    wl_audio_update(&g->audio, dt, wl_world_time(g->world), true);

    /* autosave */
    g->autosave_timer += dt;
    if (g->cfg.autosave_interval > 0 &&
        g->autosave_timer >= (double)g->cfg.autosave_interval) {
        g->autosave_timer = 0.0;
        wl_game_save_session(g);
    }

    /* draw */
    wl_renderer_begin(&g->rend, p, &g->cfg, wl_world_time(g->world),
                      p->head_in_water, wl_world_render_distance(g->world));
    BeginDrawing();
    wl_renderer_draw_world(&g->rend, g->world, &g->cfg, wl_world_time(g->world),
                           p->head_in_water);
    wl_hud_draw(g);
    wl_inv_ui_draw(g);
    EndDrawing();

    /* demo diagnostics + frame countdown */
    if (g->demo_frames > 0) {
        static int diag = 0;
        if (diag++ % 30 == 0) {
            WlWorldStats st = wl_world_stats(g->world);
            WL_LOGD("demo pos %.1f,%.1f,%.1f drawn %d tris %ld pending %d | "
                    "%.0f fps, %.2f ms avg, worst %.1f ms, rss %d MiB | "
                    "gen %d chks %.2f ms avg, mesh %.2f ms avg",
                    p->pos.x, p->pos.y, p->pos.z, g->rend.drawn_chunks,
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
    wl_hud_draw(g);
    wl_menu_pause(g);
    EndDrawing();
    wl_audio_update(&g->audio, GetFrameTime(), wl_world_time(g->world), false);
}

void wl_game_run_frame(WlGame *g)
{
    if (g->quit) return;

    /* deferred audio init: the first frame is already on screen, so even a
     * slow audio device probe cannot make the game look like it hangs */
    if (g->audio_pending) {
        g->audio_pending = false;
        double ta = stage_mark();
        wl_audio_init(&g->audio, g->assets);
        wl_audio_set_volumes(&g->audio, g->cfg.vol_master, g->cfg.vol_music,
                             g->cfg.vol_sfx, g->cfg.vol_ambient);
        WL_LOGI("startup: audio device %s (%.1f ms)",
                g->audio.enabled ? "ready" : "unavailable, running silent",
                stage_mark() - ta);
    }

    wl_ui_begin(&g->ui, g->assets, &g->cfg);

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
        EndDrawing();
        break;
    }
    case WL_STATE_LOADING:      loading_frame(g); break;
    case WL_STATE_PLAYING:      play_frame(g); break;
    case WL_STATE_PAUSED:       paused_frame(g); break;
    default:                    g->state = WL_STATE_MAIN_MENU; break;
    }

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
        g->demo_frames > 0) {
        /* in-game demo only runs the play state; menus mean something broke */
        g->demo_frames = 0;
        g->quit = true;
    }
}
