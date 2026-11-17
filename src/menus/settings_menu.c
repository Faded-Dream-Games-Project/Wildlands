/* settings_menu.c — video / audio / controls / gameplay, live-applied. */
#include "wl/menus.h"
#include "wl/game.h"
#include "wl/keybinds.h"

#include <stdio.h>
#include <string.h>

static const char *k_tabs[] = { "Video", "Audio", "Controls", "Gameplay" };

static const char *k_res_options[] = {
    "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440"
};
static const int k_res_w[] = { 1280, 1600, 1920, 2560 };
static const int k_res_h[] = { 720, 900, 1080, 1440 };

static const char *k_quality[] = { "Low", "Medium", "High" };
static const char *k_difficulty[] = { "Peaceful", "Easy", "Normal", "Hard" };

typedef struct {
    Rectangle label;
    Rectangle control;
} Row;

static void next_row(Row *row, float x, float w, float *y)
{
    row->label = (Rectangle){ x, *y, w * 0.52f, 40 };
    row->control = (Rectangle){ x + w * 0.55f, *y, w * 0.45f, 40 };
    *y += 52;
}

void wl_menu_settings(WlGame *g)
{
    int sw = GetScreenWidth();
    wl_ui_dim(0.5f);

    /* ESC must not navigate back while it is being captured as the
     * "cancel rebinding" key below */
    bool rebinding_at_entry = (g->rebinding_action >= 0);

    WlConfig *c = &g->cfg;

    Font tf = wl_ui_title_font(&g->ui);
    const char *title = "Settings";
    float fs = 26.0f * g->ui.scale;
    Vector2 ts = MeasureTextEx(tf, title, fs, 2.0f);
    DrawTextEx(tf, title, (Vector2){ (sw - ts.x) / 2, 26 * g->ui.scale }, fs, 2.0f, WHITE);

    float pw = 640 * g->ui.scale, ph = 520 * g->ui.scale;
    /* keep the panel (and its footer buttons) inside the window; smaller
     * windows scroll the content instead */
    float max_ph = (float)GetScreenHeight() - 76 * g->ui.scale - 8;
    if (ph > max_ph) ph = max_ph;
    Rectangle panel = { (sw - pw) / 2, 76 * g->ui.scale, pw, ph };
    wl_ui_panel(&g->ui, panel);

    Rectangle tabs = { panel.x + 12, panel.y + 12, panel.width - 24, 40 * g->ui.scale };
    if (wl_ui_tabbar(&g->ui, tabs, k_tabs, 4, &g->settings_tab))
        g->settings_scroll = 0; /* jump to the top when switching tabs */

    Rectangle area = { panel.x + 24, tabs.y + 56 * g->ui.scale,
                       panel.width - 48, panel.height - 190 * g->ui.scale };

    /* content height per tab (rows are 52 px, binding rows 42 px); the
     * list scrolls whenever the content outgrows the panel */
    float content_h;
    if (g->settings_tab == 0)      content_h = 12 * 52.0f + 8;
    else if (g->settings_tab == 1) content_h = 4 * 52.0f + 8;
    else if (g->settings_tab == 2)
        content_h = 3 * 52.0f + 36.0f + (float)WL_ACT_COUNT * 42.0f + 8;
    else                           content_h = 5 * 52.0f + 8;

    wl_ui_scroll_begin(&g->ui, area, content_h, &g->settings_scroll);

    float x = area.x, w = area.width;
    float y = area.y - g->settings_scroll;
    Row r;

    if (g->settings_tab == 0) {
        /* --- video --- */
        if (c->window_width == 1280 && c->window_height == 720) g->res_index = 0;
        else if (c->window_width == 1600 && c->window_height == 900) g->res_index = 1;
        else if (c->window_width == 1920 && c->window_height == 1080) g->res_index = 2;
        else g->res_index = 3;

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Resolution", RAYWHITE, false);
        if (wl_ui_dropdown(&g->ui, r.control, &g->res_index, k_res_options, 4)) {
            c->window_width = k_res_w[g->res_index];
            c->window_height = k_res_h[g->res_index];
            wl_game_apply_settings(g);
        }
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Fullscreen", RAYWHITE, false);
        if (wl_ui_toggle(&g->ui, r.control, &c->fullscreen)) wl_game_apply_settings(g);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Borderless Window", RAYWHITE, false);
        if (wl_ui_toggle(&g->ui, r.control, &c->borderless)) wl_game_apply_settings(g);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "VSync", RAYWHITE, false);
        if (wl_ui_toggle(&g->ui, r.control, &c->vsync)) wl_game_apply_settings(g);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Anti-Aliasing 4x (restart)", RAYWHITE, false);
        if (wl_ui_toggle(&g->ui, r.control, &c->msaa)) wl_game_apply_settings(g);

        char fps_lbl[64];
        snprintf(fps_lbl, sizeof(fps_lbl), "FPS Limit: %s",
                 c->fps_limit > 0 ? TextFormat("%d", c->fps_limit)
                                  : "Auto (60)");
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, fps_lbl, RAYWHITE, false);
        if (wl_ui_slider_i(&g->ui, r.control, &c->fps_limit, 0, 240, 30))
            wl_game_apply_settings(g);

        char rd_lbl[48];
        snprintf(rd_lbl, sizeof(rd_lbl), "Render Distance: %d chunks", c->render_distance);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, rd_lbl, RAYWHITE, false);
        if (wl_ui_slider_i(&g->ui, r.control, &c->render_distance, 2, 16, 1))
            wl_game_apply_settings(g);

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Graphics Quality", RAYWHITE, false);
        if (wl_ui_dropdown(&g->ui, r.control, &c->graphics_quality, k_quality, 3)) {}

        char fov_lbl[48];
        snprintf(fov_lbl, sizeof(fov_lbl), "Field of View: %d", c->fov);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, fov_lbl, RAYWHITE, false);
        wl_ui_slider_i(&g->ui, r.control, &c->fov, 50, 110, 1);

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "View Bobbing", RAYWHITE, false);
        wl_ui_toggle(&g->ui, r.control, &c->view_bobbing);

        char br_lbl[48];
        snprintf(br_lbl, sizeof(br_lbl), "Brightness: %.2f", c->brightness);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, br_lbl, RAYWHITE, false);
        wl_ui_slider(&g->ui, r.control, &c->brightness, 0.5f, 1.5f, 0.05f);

        char ui_lbl[48];
        snprintf(ui_lbl, sizeof(ui_lbl), "UI Scale: %.2fx", c->ui_scale);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, ui_lbl, RAYWHITE, false);
        wl_ui_slider(&g->ui, r.control, &c->ui_scale, 0.75f, 2.0f, 0.05f);
    } else if (g->settings_tab == 1) {
        /* --- audio --- */
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "Master: %d%%", (int)(c->vol_master * 100));
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, lbl, RAYWHITE, false);
        if (wl_ui_slider(&g->ui, r.control, &c->vol_master, 0, 1, 0.05f))
            wl_game_apply_settings(g);
        snprintf(lbl, sizeof(lbl), "Music: %d%%", (int)(c->vol_music * 100));
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, lbl, RAYWHITE, false);
        if (wl_ui_slider(&g->ui, r.control, &c->vol_music, 0, 1, 0.05f))
            wl_game_apply_settings(g);
        snprintf(lbl, sizeof(lbl), "Sound Effects: %d%%", (int)(c->vol_sfx * 100));
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, lbl, RAYWHITE, false);
        if (wl_ui_slider(&g->ui, r.control, &c->vol_sfx, 0, 1, 0.05f))
            wl_game_apply_settings(g);
        snprintf(lbl, sizeof(lbl), "Ambient: %d%%", (int)(c->vol_ambient * 100));
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, lbl, RAYWHITE, false);
        if (wl_ui_slider(&g->ui, r.control, &c->vol_ambient, 0, 1, 0.05f))
            wl_game_apply_settings(g);
    } else if (g->settings_tab == 2) {
        /* --- controls --- */
        char sens_lbl[48];
        snprintf(sens_lbl, sizeof(sens_lbl), "Mouse Sensitivity: %.2f", c->mouse_sensitivity);
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, sens_lbl, RAYWHITE, false);
        wl_ui_slider(&g->ui, r.control, &c->mouse_sensitivity, 0.2f, 3.0f, 0.05f);

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Invert Mouse Y", RAYWHITE, false);
        wl_ui_toggle(&g->ui, r.control, &c->invert_mouse);

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Mouse Smoothing", RAYWHITE, false);
        wl_ui_toggle(&g->ui, r.control, &c->mouse_smoothing);

        y += 10;
        Font f = wl_ui_font(&g->ui);
        DrawTextEx(f, "Click a binding, then press the new key (ESC cancels)",
                   (Vector2){ x, y }, 18 * g->ui.scale, 1.0f,
                   (Color){ 165, 172, 182, 255 });
        y += 26;

        float vy0 = area.y - 4, vy1 = area.y + area.height;
        for (int a = 0; a < (int)WL_ACT_COUNT; a++) {
            /* rows clipped out of the viewport are skipped; scrolling
             * reaches them */
            if (y + 36 < vy0 || y > vy1) { y += 42; continue; }
            Rectangle lr = { x, y, w * 0.52f, 36 };
            Rectangle br = { x + w * 0.55f, y, w * 0.45f, 36 };
            wl_ui_label(&g->ui, lr, wl_action_label((WlAction)a), RAYWHITE, false);

            const char *key = wl_key_name(g->keys.key[a]);
            char blabel[48];
            if (g->rebinding_action == a)
                snprintf(blabel, sizeof(blabel), "> press key <");
            else
                snprintf(blabel, sizeof(blabel), "%s", key[0] ? key : "?");

            if (wl_ui_button(&g->ui, br, blabel) && g->rebinding_action < 0)
                g->rebinding_action = a;
            y += 42;
        }

        /* modifier hints that do not need their own binding */
        y += 6;
        wl_ui_label(&g->ui, (Rectangle){ x, y, w, 30 },
                    "Hold Shift while dropping to throw the whole stack",
                    (Color){ 165, 172, 182, 255 }, false);
        y += 34;

        /* capture the next key press for the binding being edited */
        if (g->rebinding_action >= 0) {
            int newkey = GetKeyPressed();
            if (newkey > 0) {
                if (newkey != KEY_ESCAPE && wl_key_is_bindable(newkey))
                    g->keys.key[g->rebinding_action] = newkey;
                g->rebinding_action = -1;
            }
        }
    } else {
        /* --- gameplay --- */
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Difficulty", RAYWHITE, false);
        wl_ui_dropdown(&g->ui, r.control, &c->difficulty, k_difficulty, 4);

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Smooth Camera", RAYWHITE, false);
        wl_ui_toggle(&g->ui, r.control, &c->smooth_camera);

        char as_lbl[64];
        snprintf(as_lbl, sizeof(as_lbl), "Autosave: %s",
                 c->autosave_interval > 0 ? TextFormat("every %d s", c->autosave_interval)
                                          : "Off");
        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, as_lbl, RAYWHITE, false);
        wl_ui_slider_i(&g->ui, r.control, &c->autosave_interval, 0, 600, 30);

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Debug Overlay (F3)", RAYWHITE, false);
        wl_ui_toggle(&g->ui, r.control, &c->show_debug_info);

        next_row(&r, x, w, &y);
        wl_ui_label(&g->ui, r.label, "Pause on Focus Loss", RAYWHITE, false);
        wl_ui_toggle(&g->ui, r.control, &c->pause_on_focus_loss);
    }

    wl_ui_scroll_end(&g->ui);

    /* footer buttons */
    Rectangle b = { panel.x + 16, panel.y + ph - 60 * g->ui.scale,
                    (pw - 64 * g->ui.scale) / 4, 46 * g->ui.scale };
    /* ESC behaves like Cancel: revert pending changes and go back to
     * wherever the settings were opened from (menu or pause screen) */
    if (wl_game_key_pressed(g, KEY_ESCAPE) && !rebinding_at_entry) {
        g->cfg = g->cfg_entry;
        wl_game_apply_settings(g);
        g->state = g->settings_return;
        if (g->state == WL_STATE_PLAYING) DisableCursor();
        return;
    }
    if (wl_ui_button(&g->ui, b, "Done")) {
        wl_config_validate(&g->cfg);
        wl_game_save_config(g);
        g->state = g->settings_return;
        if (g->state == WL_STATE_PLAYING) DisableCursor();
    }
    b.x += (pw - 48 * g->ui.scale) / 4 + 8;
    if (wl_ui_button(&g->ui, b, "Cancel")) {
        g->cfg = g->cfg_entry;
        wl_game_apply_settings(g);
        g->state = g->settings_return;
        if (g->state == WL_STATE_PLAYING) DisableCursor();
    }
    b.x += (pw - 48 * g->ui.scale) / 4 + 8;
    if (wl_ui_button(&g->ui, b, "Defaults")) {
        wl_config_defaults(&g->cfg);
        wl_game_apply_settings(g);
    }
    b.x += (pw - 48 * g->ui.scale) / 4 + 8;
    if (wl_ui_button(&g->ui, b, "Reset Keys")) {
        wl_keybinds_defaults(&g->keys);
    }
}
