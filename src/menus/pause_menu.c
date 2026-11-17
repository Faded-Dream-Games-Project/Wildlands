/* pause_menu.c — in-game pause. */
#include "wl/menus.h"
#include "wl/game.h"
#include "wl/log.h"

void wl_menu_pause(WlGame *g)
{
    WlUi *ui = &g->ui;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    /* the frozen world stays visible behind the dim */
    wl_ui_dim(0.55f);

    Font tf = wl_ui_title_font(ui);
    const char *title = "Game Paused";
    float fs = 34.0f * ui->scale;
    Vector2 ts = MeasureTextEx(tf, title, fs, 2.0f);
    DrawTextEx(tf, title, (Vector2){ (sw - ts.x) / 2, sh * 0.16f },
               fs, 2.0f, RAYWHITE);

    /* centered panel with the four actions */
    float pw = 380.0f * ui->scale;
    float btn_h = 46.0f * ui->scale;
    float gap = 12.0f * ui->scale;
    float pad = 24.0f * ui->scale;
    float ph = pad + 4.0f * btn_h + 3.0f * gap + pad;
    Rectangle panel = { (sw - pw) / 2, sh * 0.30f, pw, ph };
    wl_ui_panel(ui, panel);

    Rectangle b = { panel.x + pad, panel.y + pad, pw - 2 * pad, btn_h };
    if (wl_ui_button(ui, b, "Resume Game")) {
        g->state = WL_STATE_PLAYING;
        DisableCursor();
    }
    b.y += btn_h + gap;
    if (wl_ui_button(ui, b, "Options")) {
        /* open the existing settings menu on top of the pause state; the
         * world session is kept and Done/Cancel returns here */
        g->cfg_entry = g->cfg;
        g->settings_tab = 0;
        g->rebinding_action = -1;
        g->settings_return = WL_STATE_PAUSED;
        g->state = WL_STATE_SETTINGS;
        EnableCursor();
    }
    b.y += btn_h + gap;
    if (wl_ui_button(ui, b, "Save and Quit to Title")) {
        /* wl_game_exit_world() saves first; if the save fails it keeps
         * the session open and last_save_failed reports why below */
        if (!wl_game_exit_world(g)) {
            WL_LOGE("pause: 'Save and Quit to Title' aborted — save failed");
            return; /* stay in the pause menu with the error visible */
        }
    }
    b.y += btn_h + gap;
    if (wl_ui_button(ui, b, "Quit to Desktop")) {
        /* saves the session, then exits through the normal shutdown path;
         * on save failure the game stays open and the error shows below */
        if (!wl_game_quit_to_desktop(g)) {
            WL_LOGE("pause: 'Quit to Desktop' aborted — save failed");
            return;
        }
    }

    /* save failures are reported here, never silently discarded */
    if (g->last_save_failed) {
        Font f = wl_ui_font(ui);
        const char *err = "Saving failed! Progress NOT written — "
                          "check disk space, then try again.";
        float efs = 20.0f * ui->scale;
        Vector2 es = MeasureTextEx(f, err, efs, 1.0f);
        DrawTextEx(f, err, (Vector2){ (sw - es.x) / 2, panel.y + ph + 14 * ui->scale },
                   efs, 1.0f, (Color){ 255, 110, 110, 255 });
    }

    /* ESC toggles back into the game */
    if (wl_game_key_pressed(g, g->keys.key[WL_ACT_PAUSE])) {
        g->state = WL_STATE_PLAYING;
        DisableCursor();
    }
}
