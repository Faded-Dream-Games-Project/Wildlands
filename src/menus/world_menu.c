/* world_menu.c — world selection, creation and deletion. */
#include "wl/menus.h"
#include "wl/game.h"
#include "wl/util.h"

#include <stdio.h>
#include <string.h>

static void format_last_played(int64_t t, char *buf, size_t cap)
{
    if (t <= 0) {
        wl_scpy(buf, "never", cap);
        return;
    }
    int64_t now = wl_time_unix();
    int64_t mins = (now - t) / 60;
    if (mins < 1) wl_scpy(buf, "just now", cap);
    else if (mins < 60) snprintf(buf, cap, "%lld min ago", (long long)mins);
    else if (mins < 60 * 24) snprintf(buf, cap, "%lld h ago", (long long)(mins / 60));
    else snprintf(buf, cap, "%lld d ago", (long long)(mins / (60 * 24)));
}

void wl_menu_world_select(WlGame *g)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    wl_ui_dim(0.35f);

    Font tf = wl_ui_title_font(&g->ui);
    const char *title = "Select World";
    float fs = 26.0f * g->ui.scale;
    Vector2 ts = MeasureTextEx(tf, title, fs, 2.0f);
    DrawTextEx(tf, title, (Vector2){ (sw - ts.x) / 2, 34 * g->ui.scale }, fs, 2.0f, WHITE);

    static int scroll = 0;

    /* world list */
    float panel_w = 560 * g->ui.scale;
    float panel_h = 380 * g->ui.scale;
    Rectangle panel = { (sw - panel_w) / 2, 90 * g->ui.scale, panel_w, panel_h };
    wl_ui_panel(&g->ui, panel);

    Rectangle list = { panel.x + 10, panel.y + 10,
                       panel.width - 20, panel.height - 20 };
    if (CheckCollisionPointRec(GetMousePosition(), panel) && g->ui.wheel != 0)
        scroll -= g->ui.wheel;
    if (scroll < 0) scroll = 0;

    float row_h = 64 * g->ui.scale;
    int max_scroll = g->menus.world_count * row_h - list.height;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;

    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    for (int i = 0; i < g->menus.world_count; i++) {
        const WlWorldEntry *e = &g->menus.worlds[i];
        float y = list.y + i * row_h - scroll;
        if (y + row_h < list.y || y > list.y + list.height) continue;

        Rectangle row = { list.x + 4, y + 2, list.width - 8, row_h - 6 };
        bool hover = CheckCollisionPointRec(GetMousePosition(), row);
        bool sel = (i == g->menus.selected);

        if (sel) DrawRectangleRec(row, (Color){ 255, 255, 255, 30 });
        else if (hover) DrawRectangleRec(row, (Color){ 255, 255, 255, 14 });
        DrawRectangleLinesEx(row, 1.0f, sel ? (Color){ 230, 226, 170, 220 }
                                            : (Color){ 60, 64, 72, 200 });

        Font f = wl_ui_font(&g->ui);
        float nfs = 24 * g->ui.scale;
        DrawTextEx(f, e->meta.name, (Vector2){ row.x + 12, row.y + 8 }, nfs, 1.0f,
                   sel ? (Color){ 245, 242, 215, 255 } : RAYWHITE);

        char when[48];
        format_last_played(e->meta.last_played_unix, when, sizeof(when));
        char sub[160];
        snprintf(sub, sizeof(sub), "%s  |  seed %llu  |  %s  |  played %.0f min",
                 e->meta.gamemode == WL_WORLD_GAMEMODE_CREATIVE ? "Creative" : "Survival",
                 (unsigned long long)e->meta.seed,
                 e->meta.type == WL_WORLDTYPE_SUPERFLAT ? "Superflat" : "Standard",
                 e->meta.play_seconds / 60.0);
        float sfs = 18 * g->ui.scale;
        DrawTextEx(f, sub, (Vector2){ row.x + 12, row.y + 8 + nfs + 4 }, sfs, 1.0f,
                   (Color){ 158, 165, 175, 255 });
        DrawTextEx(f, when, (Vector2){ row.x + row.width - 130 * g->ui.scale,
                                       row.y + 8 + nfs + 4 }, sfs, 1.0f,
                   (Color){ 158, 165, 175, 255 });

        if (hover && g->ui.mouse_pressed) g->menus.selected = i;
    }
    EndScissorMode();

    if (g->menus.world_count == 0) {
        Font f = wl_ui_font(&g->ui);
        const char *msg = "No worlds yet - create one!";
        float mfs = 26 * g->ui.scale;
        Vector2 ms = MeasureTextEx(f, msg, mfs, 1.0f);
        DrawTextEx(f, msg, (Vector2){ panel.x + (panel.width - ms.x) / 2,
                                      panel.y + panel.height / 2 }, mfs, 1.0f,
                   (Color){ 170, 176, 186, 255 });
    }

    /* action buttons */
    Rectangle b = { panel.x, panel.y + panel_h + 12 * g->ui.scale,
                    panel_w / 2 - 8, 44 * g->ui.scale };
    bool have = g->menus.selected >= 0 && g->menus.selected < g->menus.world_count;
    if (wl_ui_button(&g->ui, b, have ? "Play Selected" : "Play (nothing selected)")) {
        if (have) wl_game_enter_world(g, &g->menus.worlds[g->menus.selected]);
    }
    b.x = panel.x + panel_w / 2 + 8;
    if (wl_ui_button(&g->ui, b, "Create New World")) {
        g->menus.new_name[0] = '\0';
        g->menus.new_seed[0] = '\0';
        g->menus.new_gamemode = WL_WORLD_GAMEMODE_SURVIVAL;
        g->menus.new_type = WL_WORLDTYPE_STANDARD;
        g->state = WL_STATE_WORLD_CREATE;
    }

    b = (Rectangle){ panel.x, b.y + 56 * g->ui.scale, panel_w / 2 - 8, 44 * g->ui.scale };
    if (wl_ui_button(&g->ui, b, have ? "Delete World" : "Delete (nothing selected)")) {
        if (have) g->menus.confirm_delete = true;
    }
    b.x = panel.x + panel_w / 2 + 8;
    if (wl_ui_button(&g->ui, b, "Back"))
        g->state = WL_STATE_MAIN_MENU;

    /* delete confirmation modal */
    if (g->menus.confirm_delete && have) {
        wl_ui_dim(0.6f);
        const WlWorldEntry *e = &g->menus.worlds[g->menus.selected];
        float mw = 420 * g->ui.scale, mh = 170 * g->ui.scale;
        Rectangle m = { (sw - mw) / 2, (sh - mh) / 2, mw, mh };
        wl_ui_panel(&g->ui, m);
        char msg[160];
        snprintf(msg, sizeof(msg), "Delete '%s' permanently?", e->meta.name);
        Rectangle tr = { m.x + 16, m.y + 14, m.width - 32, 40 * g->ui.scale };
        wl_ui_label_centered(&g->ui, tr, msg, RAYWHITE, false);
        Rectangle warn = { m.x + 16, m.y + 58 * g->ui.scale, m.width - 32, 30 * g->ui.scale };
        wl_ui_label_centered(&g->ui, warn, "This cannot be undone.",
                             (Color){ 220, 120, 110, 255 }, false);
        Rectangle yes = { m.x + 24, m.y + mh - 58 * g->ui.scale,
                          mw / 2 - 40, 44 * g->ui.scale };
        if (wl_ui_button(&g->ui, yes, "Delete")) {
            if (wl_worlds_delete(e->dir_name)) {
                g->menus.world_count = wl_worlds_list(g->menus.worlds, WL_MENUS_MAX_WORLDS);
                g->menus.selected = -1;
            }
            g->menus.confirm_delete = false;
        }
        Rectangle no = { m.x + mw / 2 + 16, yes.y, mw / 2 - 40, 44 * g->ui.scale };
        if (wl_ui_button(&g->ui, no, "Cancel"))
            g->menus.confirm_delete = false;
    }

    if (IsKeyPressed(g->keys.key[WL_ACT_PAUSE]))
        g->state = WL_STATE_MAIN_MENU;
}

void wl_menu_world_create(WlGame *g)
{
    int sw = GetScreenWidth();
    wl_ui_dim(0.35f);

    Font tf = wl_ui_title_font(&g->ui);
    const char *title = "Create New World";
    float fs = 26.0f * g->ui.scale;
    Vector2 ts = MeasureTextEx(tf, title, fs, 2.0f);
    DrawTextEx(tf, title, (Vector2){ (sw - ts.x) / 2, 34 * g->ui.scale }, fs, 2.0f, WHITE);

    float s = g->ui.scale;
    float pw = 460 * s;
    /* four label(34) + control(44) groups with 16 px gaps; the action
     * buttons are pinned to the panel bottom so they can never overlap
     * the last control */
    float content_h = (4.0f * (34.0f + 44.0f) + 3.0f * 16.0f) * s;
    float btn_h = 46 * s, pad = 24 * s;
    float ph = pad + content_h + 16 * s + btn_h + pad;
    float max_ph = GetScreenHeight() - 108 * s - 8; /* keep the title visible */
    if (ph > max_ph) ph = max_ph;
    Rectangle panel = { (sw - pw) / 2, 100 * s, pw, ph };
    wl_ui_panel(&g->ui, panel);

    /* scrolling content: at large UI scales / small windows the groups
     * scroll instead of running off the screen */
    Rectangle viewport = { panel.x + 24 * s, panel.y + pad,
                           pw - 48 * s, ph - pad - 16 * s - btn_h - pad };
    if (viewport.height < 60) viewport.height = 60;
    wl_ui_scroll_begin(&g->ui, viewport, content_h, &g->menus.create_scroll);

    float x = panel.x + 24 * s;
    float y = viewport.y - g->menus.create_scroll;
    float w = pw - 48 * s;
    float h = 44 * s;

    wl_ui_label(&g->ui, (Rectangle){ x, y, w, 28 * s },
                "World Name", (Color){ 200, 206, 214, 255 }, false);
    y += 34 * s;
    wl_ui_textfield(&g->ui, 1001, (Rectangle){ x, y, w, h },
                    g->menus.new_name, sizeof(g->menus.new_name));
    y += h + 16 * s;

    wl_ui_label(&g->ui, (Rectangle){ x, y, w, 28 * s },
                "Seed (blank = random)", (Color){ 200, 206, 214, 255 }, false);
    y += 34 * s;
    wl_ui_textfield(&g->ui, 1002, (Rectangle){ x, y, w, h },
                    g->menus.new_seed, sizeof(g->menus.new_seed));
    y += h + 16 * s;

    static const char *gmodes[] = { "Survival", "Creative" };
    static const char *wtypes[] = { "Standard", "Superflat" };

    wl_ui_label(&g->ui, (Rectangle){ x, y, w, 28 * s },
                "Game Mode", (Color){ 200, 206, 214, 255 }, false);
    y += 34 * s;
    wl_ui_dropdown(&g->ui, (Rectangle){ x, y, w, h }, &g->menus.new_gamemode,
                   gmodes, 2);
    y += h + 16 * s;

    wl_ui_label(&g->ui, (Rectangle){ x, y, w, 28 * s },
                "World Type", (Color){ 200, 206, 214, 255 }, false);
    y += 34 * s;
    wl_ui_dropdown(&g->ui, (Rectangle){ x, y, w, h }, &g->menus.new_type,
                   wtypes, 2);
    wl_ui_scroll_end(&g->ui);

    /* buttons pinned below the scroll area */
    Rectangle b = { panel.x + 24 * s, panel.y + ph - pad - btn_h,
                    (pw - 72 * s) / 2, btn_h };
    bool ok = g->menus.new_name[0] != '\0';
    if (wl_ui_button(&g->ui, b, ok ? "Create World" : "Name required")) {
        if (ok) {
            uint64_t seed = 0;
            if (g->menus.new_seed[0] != '\0') {
                long parsed;
                if (wl_parse_long(g->menus.new_seed, &parsed)) seed = (uint64_t)parsed;
            }
            WlWorldEntry entry;
            if (wl_worlds_create(g->menus.new_name, seed, g->menus.new_type,
                                 g->menus.new_gamemode, &entry)) {
                g->menus.world_count = wl_worlds_list(g->menus.worlds, WL_MENUS_MAX_WORLDS);
                wl_game_enter_world(g, &entry);
            }
        }
    }
    b.x = panel.x + pw / 2 + 12 * g->ui.scale;
    if (wl_ui_button(&g->ui, b, "Back"))
        g->state = WL_STATE_WORLD_SELECT;

    if (IsKeyPressed(g->keys.key[WL_ACT_PAUSE]))
        g->state = WL_STATE_WORLD_SELECT;
}
