/* pause_menu.c — in-game pause. */
#include "wl/menus.h"
#include "wl/game.h"

void wl_menu_pause(WlGame *g)
{
    /* the world stays visible behind the dim */
    wl_ui_dim(0.55f);

    int sw = GetScreenWidth();

    Font tf = wl_ui_title_font(&g->ui);
    const char *title = "Paused";
    float fs = 30.0f * g->ui.scale;
    Vector2 ts = MeasureTextEx(tf, title, fs, 2.0f);
    DrawTextEx(tf, title, (Vector2){ (sw - ts.x) / 2, GetScreenHeight() * 0.18f },
               fs, 2.0f, RAYWHITE);

    Rectangle b = wl_ui_centered(&g->ui, 340, 46, 0.42f);
    if (wl_ui_button(&g->ui, b, "Resume")) {
        g->state = WL_STATE_PLAYING;
        DisableCursor();
    }
    b.y += 58 * g->ui.scale;
    if (wl_ui_button(&g->ui, b, "Settings")) {
        g->cfg_entry = g->cfg;
        g->settings_tab = 0;
        g->rebinding_action = -1;
        g->settings_return = WL_STATE_PAUSED;
        g->state = WL_STATE_SETTINGS;
        EnableCursor();
    }
    b.y += 58 * g->ui.scale;
    if (wl_ui_button(&g->ui, b, "Save and Quit to Title")) {
        wl_game_exit_world(g);
    }

    if (IsKeyPressed(g->keys.key[WL_ACT_PAUSE])) {
        g->state = WL_STATE_PLAYING;
        DisableCursor();
    }
}
