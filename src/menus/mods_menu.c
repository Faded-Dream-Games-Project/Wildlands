/* mods_menu.c — installed mod management. */
#include "wl/menus.h"
#include "wl/game.h"
#include "wl/mods.h"

#include <stdio.h>

void wl_menu_mods(WlGame *g)
{
    int sw = GetScreenWidth();
    wl_ui_dim(0.35f);

    Font tf = wl_ui_title_font(&g->ui);
    const char *title = "Mods";
    float fs = 26.0f * g->ui.scale;
    Vector2 ts = MeasureTextEx(tf, title, fs, 2.0f);
    DrawTextEx(tf, title, (Vector2){ (sw - ts.x) / 2, 34 * g->ui.scale }, fs, 2.0f, WHITE);

    static int selected = 0;
    if (selected >= g->mods.count) selected = g->mods.count - 1;
    if (selected < 0) selected = 0;

    float pw = 620 * g->ui.scale, ph = 430 * g->ui.scale;
    Rectangle panel = { (sw - pw) / 2, 90 * g->ui.scale, pw, ph };
    wl_ui_panel(&g->ui, panel);

    if (g->mods.count == 0) {
        const char *msg = "No mods installed.\nPut mod folders into mods/ and restart.";
        wl_ui_label_centered(&g->ui, panel, msg, (Color){ 170, 176, 186, 255 }, false);
    } else {
        Rectangle list = { panel.x + 10, panel.y + 10,
                           panel.width - 20, panel.height - 20 };
        BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
        float row_h = 72 * g->ui.scale;
        for (int i = 0; i < g->mods.count; i++) {
            const WlMod *mod = &g->mods.mods[i];
            float y = list.y + i * row_h;
            if (y + row_h < list.y || y > list.y + list.height) continue;

            Rectangle row = { list.x + 4, y + 2, list.width - 8, row_h - 6 };
            bool hover = CheckCollisionPointRec(g->ui.mouse, row);
            bool sel = (i == selected);
            if (sel) DrawRectangleRec(row, (Color){ 255, 255, 255, 30 });
            else if (hover) DrawRectangleRec(row, (Color){ 255, 255, 255, 14 });
            DrawRectangleLinesEx(row, 1.0f, sel ? (Color){ 230, 226, 170, 220 }
                                                : (Color){ 60, 64, 72, 200 });
            if (hover && g->ui.mouse_pressed) selected = i;

            Font f = wl_ui_font(&g->ui);
            float nfs = 22 * g->ui.scale;
            char line1[192];
            snprintf(line1, sizeof(line1), "%s %s  -  %s",
                     mod->name, mod->version, mod->author);
            DrawTextEx(f, line1, (Vector2){ row.x + 12, row.y + 8 }, nfs, 1.0f,
                       mod->status == WL_MOD_LOADED ? RAYWHITE
                                                    : (Color){ 200, 190, 160, 255 });

            Color stc = (mod->status == WL_MOD_LOADED) ? (Color){ 120, 210, 120, 255 }
                        : (mod->status == WL_MOD_DISABLED) ? (Color){ 160, 166, 176, 255 }
                        : (Color){ 230, 120, 110, 255 };
            const char *st = wl_mod_status_name(mod->status);
            Vector2 ss = MeasureTextEx(f, st, nfs, 1.0f);
            DrawTextEx(f, st, (Vector2){ row.x + row.width - ss.x - 12, row.y + 8 },
                       nfs, 1.0f, stc);

            float sfs = 17 * g->ui.scale;
            char line2[256];
            if (mod->error[0] != '\0')
                snprintf(line2, sizeof(line2), "%s", mod->error);
            else
                snprintf(line2, sizeof(line2), "%s", mod->description);
            DrawTextEx(f, line2, (Vector2){ row.x + 12, row.y + 8 + nfs + 6 },
                       sfs, 1.0f, (Color){ 158, 165, 175, 255 });
        }
        EndScissorMode();
    }

    Font note_f = wl_ui_font(&g->ui);
    const char *note = "Enabling/disabling takes effect after a restart.";
    DrawTextEx(note_f, note,
               (Vector2){ panel.x, panel.y + panel.height + 10 * g->ui.scale },
               18 * g->ui.scale, 1.0f, (Color){ 150, 156, 166, 255 });

    Rectangle b = { panel.x, panel.y + panel.height + 34 * g->ui.scale,
                    pw / 2 - 8, 44 * g->ui.scale };
    if (g->mods.count > 0) {
        const WlMod *mod = &g->mods.mods[selected];
        char label[64];
        snprintf(label, sizeof(label), mod->enabled ? "Disable '%s'" : "Enable '%s'",
                 mod->name);
        if (wl_ui_button(&g->ui, b, label))
            wl_mods_set_enabled(&g->mods, selected, !mod->enabled);
    } else {
        wl_ui_button(&g->ui, b, "No mods");
    }
    b.x = panel.x + pw / 2 + 8;
    if (wl_ui_button(&g->ui, b, "Back"))
        g->state = WL_STATE_MAIN_MENU;

    if (IsKeyPressed(g->keys.key[WL_ACT_PAUSE]))
        g->state = WL_STATE_MAIN_MENU;
}
