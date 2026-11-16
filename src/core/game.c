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
                   "  --overlay <inv|craft> test: open the inventory overlay in-world\n",
                   WL_VERSION_STRING);
            exit(0);
        }
    }
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
    parse_args(g, argc, argv);

    if (!wl_paths_init(argv[0])) {
        fprintf(stderr, "Wildlands: cannot locate the project root (missing assets/)\n");
        return false;
    }
    if (!wl_paths_ensure_dirs()) {
        fprintf(stderr, "Wildlands: cannot create runtime directories\n");
        return false;
    }

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

    /* config + keybindings */
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

    /* window */
    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (g->cfg.vsync) flags |= FLAG_VSYNC_HINT;
    if (g->cfg.msaa) flags |= FLAG_MSAA_4X_HINT;
    SetConfigFlags(flags);
    InitWindow(g->cfg.window_width, g->cfg.window_height,
               WL_GAME_NAME " " WL_VERSION_STRING);
    if (g->cfg.borderless) SetWindowState(FLAG_WINDOW_UNDECORATED);
    SetTargetFPS(g->cfg.fps_limit > 0 ? (unsigned)g->cfg.fps_limit : 0);

    /* registries + assets */
    wl_blocks_init();
    wl_items_init();
    wl_crafting_init();

    char assets_root[WL_PATH_MAX];
    wl_path_assets(assets_root, sizeof(assets_root), "");
    g->assets = wl_assets_create(assets_root);
    if (!g->assets) {
        WL_LOGE("asset manager failed to initialize");
        return false;
    }
    /* mods register blocks/items/textures before the atlas is finalized */
    char mods_root[WL_PATH_MAX];
    wl_path_mods(mods_root, sizeof(mods_root), "");
    wl_mods_init(&g->mods, g->assets, mods_root);
    wl_assets_finalize_atlas(g->assets);

    wl_audio_init(&g->audio, g->assets);
    wl_renderer_init(&g->rend, g->assets);
    memset(&g->ui, 0, sizeof(g->ui));
    g->ui_held.item = WL_ITEM_NONE;

    /* demo mode: jump straight into a world, or park on a menu screen */
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
        return true;
    }

    if (g->demo_world[0] != '\0') {
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

    if (g->demo_frames > 0) EnableCursor();
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
    if (g->world) wl_world_destroy(g->world);
    CloseWindow();
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

    SetTargetFPS(c->fps_limit > 0 ? (unsigned)c->fps_limit : 0);

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
        if (g->demo_frames <= 0) DisableCursor();

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
    int w = GetScreenWidth(), h = GetScreenHeight();    BeginDrawing();
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
        if (diag++ % 30 == 0)
            WL_LOGD("demo pos %.1f,%.1f,%.1f drawn %d tris %ld pending %d",
                    p->pos.x, p->pos.y, p->pos.z, g->rend.drawn_chunks,
                    g->rend.drawn_tris,
                    wl_world_pending(g->world, p->pos.x, p->pos.z));
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
    wl_ui_begin(&g->ui, g->assets, &g->cfg);

    switch (g->state) {
    case WL_STATE_MAIN_MENU:    wl_menu_main(g); break;
    case WL_STATE_WORLD_SELECT: wl_menu_world_select(g); break;
    case WL_STATE_WORLD_CREATE: wl_menu_world_create(g); break;
    case WL_STATE_SETTINGS:     wl_menu_settings(g); break;
    case WL_STATE_MODS:         wl_menu_mods(g); break;
    case WL_STATE_LOADING:      loading_frame(g); break;
    case WL_STATE_PLAYING:      play_frame(g); break;
    case WL_STATE_PAUSED:       paused_frame(g); break;
    default:                    g->state = WL_STATE_MAIN_MENU; break;
    }

    wl_ui_end(&g->ui);

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
