/* main_menu.c — title screen. */
#include "wl/menus.h"
#include "wl/game.h"
#include "wl/audio.h"
#include "wl/tiles.h"

/* The background is gradient + a grid of ~400 tinted stone tiles. It is
 * static, so render it once per window size into a texture instead of
 * redrawing hundreds of quads every frame. */
static void draw_background(WlGame *g)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    if (g->menu_bg.id == 0 || sw != g->menu_bg_w || sh != g->menu_bg_h) {
        if (g->menu_bg.id != 0) UnloadRenderTexture(g->menu_bg);
        g->menu_bg = LoadRenderTexture(sw, sh);
        g->menu_bg_w = sw;
        g->menu_bg_h = sh;

        BeginTextureMode(g->menu_bg);
        ClearBackground((Color){ 12, 16, 24, 255 });
        DrawRectangleGradientV(0, 0, sw, sh, (Color){ 24, 34, 48, 255 },
                               (Color){ 12, 16, 24, 255 });
        Texture atlas = wl_assets_tex(g->assets, WL_TEX_ATLAS);
        int tc = TILE_STONE % WL_ATLAS_GRID, tr = TILE_STONE / WL_ATLAS_GRID;
        Rectangle src = { tc * 16.0f, tr * 16.0f, 16, 16 };
        int tile = 48;
        for (int y = 0; y < sh; y += tile)
            for (int x = 0; x < sw; x += tile) {
                Rectangle dst = { (float)x, (float)y, (float)tile, (float)tile };
                DrawTexturePro(atlas, src, dst, (Vector2){ 0, 0 }, 0.0f,
                               (Color){ 255, 255, 255, 18 });
            }
        EndTextureMode();
    }

    DrawTextureRec(g->menu_bg.texture,
                   (Rectangle){ 0, 0, (float)sw, -(float)sh },
                   (Vector2){ 0, 0 }, WHITE);
}

void wl_menu_main(WlGame *g)
{
    draw_background(g);

    int sw = GetScreenWidth(), sh = GetScreenHeight();

    Texture logo = wl_assets_tex(g->assets, WL_TEX_GUI_LOGO);
    float lw = (float)logo.width * 1.6f;
    float lh = (float)logo.height * 1.6f;
    Rectangle src = { 0, 0, (float)logo.width, (float)logo.height };
    Rectangle dst = { (sw - lw) / 2, sh * 0.16f, lw, lh };
    DrawTexturePro(logo, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);

    Font f = wl_ui_font(&g->ui);
    const char *ver = WL_GAME_NAME " " WL_VERSION_STRING " — an original voxel sandbox";
    float fs = 22.0f * g->ui.scale;
    Vector2 ts = MeasureTextEx(f, ver, fs, 1.0f);
    DrawTextEx(f, ver, (Vector2){ (sw - ts.x) / 2, dst.y + lh + 8 }, fs, 1.0f,
               (Color){ 150, 158, 168, 255 });

    Rectangle b = wl_ui_centered(&g->ui, 340, 46, 0.58f);
    if (wl_ui_button(&g->ui, b, "Singleplayer")) {
        g->menus.selected = -1;
        g->menus.confirm_delete = false;
        g->menus.world_count = wl_worlds_list(g->menus.worlds, WL_MENUS_MAX_WORLDS);
        g->state = WL_STATE_WORLD_SELECT;
    }
    b.y += 58 * g->ui.scale;
    if (wl_ui_button(&g->ui, b, "Mods")) {
        g->state = WL_STATE_MODS;
    }
    b.y += 58 * g->ui.scale;
    if (wl_ui_button(&g->ui, b, "Settings")) {
        g->cfg_entry = g->cfg;
        g->settings_tab = 0;
        g->rebinding_action = -1;
        g->settings_return = WL_STATE_MAIN_MENU;
        g->state = WL_STATE_SETTINGS;
    }
    b.y += 58 * g->ui.scale;
    if (wl_ui_button(&g->ui, b, "Quit")) {
        g->quit = true;
    }

    const char *foot = "MIT licensed - raylib 6.2 - original assets";
    ts = MeasureTextEx(f, foot, fs, 1.0f);
    DrawTextEx(f, foot, (Vector2){ (sw - ts.x) / 2, sh - 30 * g->ui.scale }, fs, 1.0f,
               (Color){ 110, 116, 126, 255 });

    wl_audio_update(&g->audio, GetFrameTime(), 0, false, false);
}
