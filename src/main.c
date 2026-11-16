/* main.c — Wildlands entry point: boot sequence and the play loop.
 *
 * Command line (used by the automated smoke test and power users):
 *   wildlands [--world <name>] [--seed <n>] [--gamemode survival|creative]
 *             [--type standard|superflat] [--frames <n>] [--screenshot <path>]
 *             [--width <w>] [--height <h>]
 * --frames runs a headless-friendly demo that exits after N frames and
 * optionally saves a screenshot.
 */
#include "wl/wl_common.h"
#include "wl/log.h"
#include "wl/paths.h"
#include "wl/util.h"
#include "wl/config.h"
#include "wl/keybinds.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/assets.h"
#include "wl/audio.h"
#include "wl/world.h"
#include "wl/worlds.h"
#include "wl/worldgen.h"
#include "wl/player.h"
#include "wl/interact.h"
#include "wl/renderer.h"
#include "wl/tiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "raylib.h"

typedef struct {
    const char *world_name;
    const char *screenshot;
    int frames;
    uint64_t seed;
    bool seed_given;
    int gamemode;   /* -1 = from world */
    int type;       /* -1 = from world */
    int width, height;
} WlArgs;

static void parse_args(int argc, char **argv, WlArgs *a)
{
    memset(a, 0, sizeof(*a));
    a->gamemode = -1;
    a->type = -1;
    for (int i = 1; i < argc; i++) {
        if (wl_str_eq(argv[i], "--world") && i + 1 < argc) a->world_name = argv[++i];
        else if (wl_str_eq(argv[i], "--screenshot") && i + 1 < argc) a->screenshot = argv[++i];
        else if (wl_str_eq(argv[i], "--frames") && i + 1 < argc) a->frames = atoi(argv[++i]);
        else if (wl_str_eq(argv[i], "--seed") && i + 1 < argc) {
            a->seed = (uint64_t)strtoull(argv[++i], NULL, 10);
            a->seed_given = true;
        }
        else if (wl_str_eq(argv[i], "--gamemode") && i + 1 < argc)
            a->gamemode = wl_str_eq(argv[++i], "creative") ? WL_WORLD_GAMEMODE_CREATIVE
                                                           : WL_WORLD_GAMEMODE_SURVIVAL;
        else if (wl_str_eq(argv[i], "--type") && i + 1 < argc)
            a->type = wl_str_eq(argv[++i], "superflat") ? WL_WORLDTYPE_SUPERFLAT
                                                        : WL_WORLDTYPE_STANDARD;
        else if (wl_str_eq(argv[i], "--width") && i + 1 < argc) a->width = atoi(argv[++i]);
        else if (wl_str_eq(argv[i], "--height") && i + 1 < argc) a->height = atoi(argv[++i]);
        else if (wl_str_eq(argv[i], "--help") || wl_str_eq(argv[i], "-h")) {
            printf("Wildlands %s\n"
                   "  --world <name>        play this world (created if missing)\n"
                   "  --seed <n>            seed for new worlds\n"
                   "  --gamemode <m>        survival | creative (new worlds)\n"
                   "  --type <t>            standard | superflat (new worlds)\n"
                   "  --frames <n>          demo mode: exit after n frames\n"
                   "  --screenshot <path>   save a screenshot when exiting\n"
                   "  --width/--height <n>  window size override\n",
                   WL_VERSION_STRING);
            exit(0);
        }
    }
}

static void draw_loading_screen(WlAssets *assets, const WlWorldMeta *meta,
                                int pending, int initial_pending)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    BeginDrawing();
    ClearBackground((Color){ 16, 18, 24, 255 });

    Texture logo = wl_assets_tex(assets, WL_TEX_GUI_LOGO);
    float lw = (float)logo.width * 1.5f;
    float lh = (float)logo.height * 1.5f;
    Rectangle src = { 0, 0, (float)logo.width, (float)logo.height };
    Rectangle dst = { (w - lw) / 2.0f, h * 0.30f, lw, lh };
    DrawTexturePro(logo, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);

    Font font = wl_assets_font(assets, WL_FONT_UI);
    const char *line = TextFormat("Preparing %s ...", meta->name);
    float fs = 28.0f;
    Vector2 ts = MeasureTextEx(font, line, fs, 1.0f);
    DrawTextEx(font, line, (Vector2){ (w - ts.x) / 2, h * 0.55f }, fs, 1.0f, RAYWHITE);

    int bar_w = (int)(w * 0.5f);
    int bar_x = (w - bar_w) / 2;
    int bar_y = (int)(h * 0.65f);
    float frac = initial_pending > 0 ? 1.0f - (float)pending / (float)initial_pending : 1.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    DrawRectangle(bar_x, bar_y, bar_w, 18, (Color){ 40, 44, 52, 255 });
    DrawRectangle(bar_x + 2, bar_y + 2, (int)((bar_w - 4) * frac), 14,
                  (Color){ 106, 170, 64, 255 });
    DrawRectangleLines(bar_x, bar_y, bar_w, 18, (Color){ 70, 76, 86, 255 });

    const char *tip = "Tip: hold Left Ctrl to sprint";
    ts = MeasureTextEx(font, tip, 20.0f, 1.0f);
    DrawTextEx(font, tip, (Vector2){ (w - ts.x) / 2, h * 0.72f }, 20.0f, 1.0f,
               (Color){ 150, 156, 165, 255 });
    EndDrawing();
}

static void draw_hud_basic(WlAssets *assets, const WlPlayer *p, const WlConfig *cfg,
                           WlWorld *w, const WlRenderer *rend)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    /* crosshair */
    Texture ch = wl_assets_tex(assets, WL_TEX_GUI_CROSSHAIR);
    DrawTexture(ch, sw / 2 - ch.width / 2, sh / 2 - ch.height / 2, WHITE);

    /* hotbar */
    Texture slot = wl_assets_tex(assets, WL_TEX_GUI_SLOT);
    Texture sel = wl_assets_tex(assets, WL_TEX_GUI_SLOT_SEL);
    int slot_size = 40;
    int hb_w = slot_size * WL_HOTBAR_SIZE + 8;
    int hb_x = (sw - hb_w) / 2;
    int hb_y = sh - slot_size - 12;
    for (int i = 0; i < WL_HOTBAR_SIZE; i++) {
        Rectangle r = { hb_x + i * (slot_size + 1), hb_y, (float)slot_size, (float)slot_size };
        Texture t = (i == p->selected_slot) ? sel : slot;
        Rectangle src = { 0, 0, (float)t.width, (float)t.height };
        DrawTexturePro(t, src, r, (Vector2){ 0, 0 }, 0.0f, WHITE);

        const WlSlot *s = &p->inv.slots[i];
        if (s->item != WL_ITEM_NONE) {
            /* item icon: block items use their side tile from the atlas */
            const WlItem *item = wl_item_get(s->item);
            if (item) {
                Texture atlas = wl_assets_tex(assets, WL_TEX_ATLAS);
                int tile_idx = item->is_block ? wl_block_get(item->block_id)->tex[0]
                                              : item->tile;
                int tc = tile_idx % WL_ATLAS_GRID, tr = tile_idx / WL_ATLAS_GRID;
                Rectangle icon_src = { tc * 16.0f, tr * 16.0f, 16, 16 };
                Rectangle icon_dst = { r.x + 10, r.y + 10, 20, 20 };
                DrawTexturePro(atlas, icon_src, icon_dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
                if (s->count > 1) {
                    Font f = wl_assets_font(assets, WL_FONT_UI);
                    DrawTextEx(f, TextFormat("%d", s->count),
                               (Vector2){ r.x + slot_size - 22, r.y + slot_size - 22 },
                               18.0f, 1.0f, RAYWHITE);
                }
            }
        }
    }

    /* debug overlay */
    if (cfg->show_debug_info) {
        Font f = wl_assets_font(assets, WL_FONT_UI);
        const char *lines[8];
        int n = 0;
        lines[n++] = TextFormat("Wildlands %s  %.0f fps", WL_VERSION_STRING, GetFPS());
        lines[n++] = TextFormat("xyz %.1f / %.1f / %.1f", p->pos.x, p->pos.y, p->pos.z);
        int cx = (int)floorf(p->pos.x) >> 4, cz = (int)floorf(p->pos.z) >> 4;
        lines[n++] = TextFormat("chunk %d,%d  render dist %d", cx, cz,
                                wl_world_render_distance(w));
        lines[n++] = TextFormat("biome %s  time %u",
                                wl_biome_name(wl_worldgen_biome_at(wl_world_gen(w),
                                                                    (int)floorf(p->pos.x),
                                                                    (int)floorf(p->pos.z))),
                                wl_world_time(w));
        lines[n++] = TextFormat("chunks %d drawn, %ld tris", rend->drawn_chunks, rend->drawn_tris);
        for (int i = 0; i < n; i++)
            DrawTextEx(f, lines[i], (Vector2){ 8, 8 + i * 22 }, 20.0f, 1.0f,
                       (Color){ 230, 230, 230, 235 });
    }

    /* damage / death overlays */
    if (p->damage_flash > 0.0f) {
        float a = (float)(p->damage_flash / 0.45) * 110.0f;
        DrawRectangle(0, 0, sw, sh, (Color){ 200, 30, 30, (unsigned char)a });
    }
    if (p->dead) {
        DrawRectangle(0, 0, sw, sh, (Color){ 120, 0, 0, 140 });
        Font f = wl_assets_font(assets, WL_FONT_TITLE);
        const char *msg = "You died";
        float fs = 40.0f;
        Vector2 ts = MeasureTextEx(f, msg, fs, 2.0f);
        DrawTextEx(f, msg, (Vector2){ (sw - ts.x) / 2, sh * 0.4f }, fs, 2.0f, RAYWHITE);
        Font f2 = wl_assets_font(assets, WL_FONT_UI);
        const char *msg2 = "Press R to respawn";
        Vector2 ts2 = MeasureTextEx(f2, msg2, 26.0f, 1.0f);
        DrawTextEx(f2, msg2, (Vector2){ (sw - ts2.x) / 2, sh * 0.4f + 60 }, 26.0f, 1.0f,
                   (Color){ 220, 220, 220, 255 });
    }
}

int main(int argc, char **argv)
{
    WlArgs args;
    parse_args(argc, argv, &args);

    /* ---------- boot ---------- */
    if (!wl_paths_init(argv[0])) {
        fprintf(stderr, "Wildlands: cannot locate the project root (missing assets/)\n");
        return 1;
    }
    if (!wl_paths_ensure_dirs()) {
        fprintf(stderr, "Wildlands: cannot create runtime directories\n");
        return 1;
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
        wl_path_join(logpath, sizeof(logpath), logs, TextFormat("wildlands-%s.log", stamp));
    }
    wl_log_init(logpath);
    wl_paths_prune_logs(8);
    WL_LOGI("=== " WL_GAME_NAME " %s ===", WL_VERSION_STRING);
    WL_LOGI("project root: %s", wl_paths_root());

    WlConfig cfg;
    wl_config_defaults(&cfg);
    char cfgpath[WL_PATH_MAX];
    wl_path_config(cfgpath, sizeof(cfgpath), "options.txt");
    wl_config_load(&cfg, cfgpath);
    if (!wl_file_exists(cfgpath)) wl_config_save(&cfg, cfgpath);
    if (args.width > 0 && args.height > 0) {
        cfg.window_width = args.width;
        cfg.window_height = args.height;
    }

    WlKeybinds kb;
    char kbpath[WL_PATH_MAX];
    wl_path_config(kbpath, sizeof(kbpath), "keybindings.txt");
    wl_keybinds_load(&kb, kbpath);
    if (!wl_file_exists(kbpath)) wl_keybinds_save(&kb, kbpath);

    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (cfg.vsync) flags |= FLAG_VSYNC_HINT;
    if (cfg.msaa) flags |= FLAG_MSAA_4X_HINT;
    SetConfigFlags(flags);
    InitWindow(cfg.window_width, cfg.window_height,
               WL_GAME_NAME " " WL_VERSION_STRING);
    if (cfg.borderless) SetWindowState(FLAG_WINDOW_UNDECORATED);
    SetTargetFPS(cfg.fps_limit > 0 ? (unsigned)cfg.fps_limit : 0);

    wl_blocks_init();
    wl_items_init();

    char assets_root[WL_PATH_MAX];
    wl_path_assets(assets_root, sizeof(assets_root), "");
    WlAssets *assets = wl_assets_create(assets_root);
    if (!assets) {
        WL_LOGE("asset manager failed to initialize");
        CloseWindow();
        return 1;
    }
    wl_assets_finalize_atlas(assets); /* mods will register tiles before this */

    WlAudio audio;
    wl_audio_init(&audio, assets);
    wl_audio_set_volumes(&audio, cfg.vol_master, cfg.vol_music, cfg.vol_sfx,
                         cfg.vol_ambient);

    WlRenderer rend;
    wl_renderer_init(&rend, assets);

    /* ---------- world ---------- */
    const char *world_name = args.world_name ? args.world_name : "Demo";
    WlWorldEntry entry;
    bool existing = false;
    {
        WlWorldEntry list[64];
        int n = wl_worlds_list(list, 64);
        for (int i = 0; i < n; i++) {
            if (wl_str_eq(list[i].meta.name, world_name) ||
                wl_str_eq(list[i].dir_name, world_name)) {
                entry = list[i];
                existing = true;
                break;
            }
        }
    }
    if (!existing) {
        uint64_t seed = args.seed_given ? args.seed : 0;
        int gm = args.gamemode >= 0 ? args.gamemode : WL_WORLD_GAMEMODE_SURVIVAL;
        int ty = args.type >= 0 ? args.type : WL_WORLDTYPE_STANDARD;
        if (!wl_worlds_create(world_name, seed, ty, gm, &entry)) {
            WL_LOGE("failed to create world '%s'", world_name);
            return 1;
        }
    }

    char wdir[WL_PATH_MAX];
    wl_worlds_dir_path(wdir, sizeof(wdir), entry.dir_name);
    WlWorldParams params;
    memset(&params, 0, sizeof(params));
    wl_scpy(params.name, entry.meta.name, sizeof(params.name));
    params.seed = entry.meta.seed;
    params.type = entry.meta.type;
    params.gamemode = entry.meta.gamemode;
    WlWorld *world = wl_world_create(&params, wdir);
    if (!world) return 1;
    wl_world_set_render_distance(world, cfg.render_distance);

    /* spawn point */
    Vector3 spawn = { entry.meta.spawn_x, entry.meta.spawn_y, entry.meta.spawn_z };
    bool need_spawn = (spawn.x == 0.0f && spawn.y == 0.0f && spawn.z == 0.0f);
    if (need_spawn) {
        int sx = 0, sz = 0;
        const WlWorldGen *gen = wl_world_gen(world);
        wl_worldgen_find_spawn(gen, &sx, &sz);
        spawn = (Vector3){ sx + 0.5f, (float)wl_worldgen_height_at(gen, sx, sz) + 1.2f,
                           sz + 0.5f };
    }

    WlPlayer player;
    wl_player_init(&player, spawn);
    {
        bool found = false;
        wl_worlds_load_player(entry.dir_name, &player, &found);
        if (found) need_spawn = false;
    }

    /* prewarm chunks with the loading screen */
    {
        int initial = wl_world_pending(world, player.pos.x, player.pos.z);
        double t0 = GetTime();
        while (!WindowShouldClose()) {
            int pending = wl_world_pending(world, player.pos.x, player.pos.z);
            if (pending == 0) break;
            wl_world_update_ex(world, player.pos.x, player.pos.z, 24, 12);
            draw_loading_screen(assets, &entry.meta, pending, initial);
        }
        WL_LOGI("world ready in %.1fs (%d chunks)", GetTime() - t0,
                wl_world_stats(world).loaded);
    }

    /* if the player somehow spawned inside terrain, lift them out */
    {
        int px = (int)floorf(player.pos.x), pz = (int)floorf(player.pos.z);
        int py = (int)floorf(player.pos.y);
        int guard = 0;
        while (guard++ < 130 &&
               (wl_world_solid_at(world, px, py, pz) ||
                wl_world_solid_at(world, px, py + 1, pz))) {
            py++;
        }
        if (player.pos.y < (float)py) player.pos.y = (float)py + 0.01f;
    }

    EnableCursor();
    if (args.frames <= 0) DisableCursor();

    /* ---------- main loop ---------- */
    double session_start = GetTime();
    double autosave_timer = 0.0;
    int frames_left = args.frames;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;

        /* look (skipped in demo mode: no real mouse under a headless server) */
        if (args.frames <= 0) {
            Vector2 md = GetMouseDelta();
            wl_player_apply_look(&player, md.x, md.y);
        } else {
            player.yaw = 0.8f;
            player.pitch = -0.28f;
        }

        /* hotbar selection */
        int wheel = (int)GetMouseWheelMove();
        if (wheel != 0)
            player.selected_slot = ((player.selected_slot - wheel) % WL_HOTBAR_SIZE +
                                    WL_HOTBAR_SIZE) % WL_HOTBAR_SIZE;
        for (int i = 0; i < WL_HOTBAR_SIZE; i++)
            if (IsKeyPressed(KEY_ONE + i)) player.selected_slot = i;

        if (IsKeyPressed(kb.key[WL_ACT_DEBUG]))
            cfg.show_debug_info = !cfg.show_debug_info;
        if (IsKeyPressed(kb.key[WL_ACT_SCREENSHOT])) {
            char path[WL_PATH_MAX];
            wl_path_screenshots(path, sizeof(path),
                                TextFormat("wildlands-%ld.png", (long)time(NULL)));
            TakeScreenshot(path);
            WL_LOGI("screenshot saved: %s", path);
        }
        if (player.dead && IsKeyPressed(KEY_R)) {
            wl_player_respawn(&player, spawn);
            need_spawn = false;
        }

        bool playing = !player.dead && args.frames <= 0;
        if (IsKeyPressed(kb.key[WL_ACT_PAUSE])) break;

        wl_player_update(&player, world, &kb, &cfg, &audio, dt, playing,
                         wl_world_gamemode(world));
        wl_interact_update(&player, world, &cfg, &audio, dt, playing);
        wl_world_advance_time(world, dt);
        wl_world_update(world, player.pos.x, player.pos.z);
        wl_audio_update(&audio, dt, wl_world_time(world), true);

        /* autosave */
        autosave_timer += dt;
        if (cfg.autosave_interval > 0 && autosave_timer >= (double)cfg.autosave_interval) {
            autosave_timer = 0.0;
            double now = GetTime();
            wl_worlds_save_session(world, &player, spawn, now - session_start);
            session_start = now;
        }

        /* draw */
        wl_renderer_begin(&rend, &player, &cfg, wl_world_time(world),
                          player.head_in_water, wl_world_render_distance(world));
        BeginDrawing();
        wl_renderer_draw_world(&rend, world, &cfg, wl_world_time(world),
                               player.head_in_water);
        draw_hud_basic(assets, &player, &cfg, world, &rend);
        EndDrawing();

        if (args.frames > 0) { /* demo diagnostics for automated runs */
            static int diag = 0;
            if (diag++ % 30 == 0)
                WL_LOGD("demo pos %.1f,%.1f,%.1f yaw %.2f pitch %.2f drawn %d tris %ld pending %d",
                        player.pos.x, player.pos.y, player.pos.z, player.yaw,
                        player.pitch, rend.drawn_chunks, rend.drawn_tris,
                        wl_world_pending(world, player.pos.x, player.pos.z));
        }

        if (args.frames > 0 && --frames_left <= 0) break;
    }

    /* ---------- shutdown ---------- */
    if (args.screenshot) {
        TakeScreenshot(args.screenshot);
        WL_LOGI("screenshot saved: %s", args.screenshot);
    }
    wl_worlds_save_session(world, &player, spawn, GetTime() - session_start);
    WL_LOGI("session ended");

    wl_config_save(&cfg, cfgpath);
    wl_keybinds_save(&kb, kbpath);
    wl_renderer_shutdown(&rend);
    wl_audio_shutdown(&audio);
    wl_assets_destroy(assets);
    wl_world_destroy(world);
    CloseWindow();
    wl_log_close();
    return 0;
}
