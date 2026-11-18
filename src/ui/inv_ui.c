/* inv_ui.c — inventory screen with drag & drop, crafting grid and result. */
#include "wl/inv_ui.h"
#include "wl/game.h"
#include "wl/world.h"
#include <stdio.h>
#include "wl/crafting.h"
#include "wl/item.h"
#include "wl/audio.h"

#include <string.h>

/* logical slot references */
typedef enum {
    SLOT_NONE = 0,
    SLOT_INV,     /* index 0..35 */
    SLOT_CRAFT,   /* index 0..8  */
    SLOT_RESULT,
    SLOT_ARMOR,   /* index 0..3: helmet, chest, legs, boots */
} SlotKind;

typedef struct {
    SlotKind kind;
    int index;
} SlotRef;

static WlSlot *ref_slot(WlGame *g, SlotRef s)
{
    if (s.kind == SLOT_INV) return &g->player.inv.slots[s.index];
    if (s.kind == SLOT_CRAFT) return &g->player.inv.craft[s.index];
    if (s.kind == SLOT_ARMOR) return &g->player.armor[s.index];
    return NULL;
}

/* first_index: slot index of the top-left cell (hotbar grid = 0, main = 9) */
static void draw_grid(WlGame *g, Rectangle pos, SlotKind kind, int count, int cols,
                      int first_index)
{
    float cell = pos.width / cols;
    for (int i = 0; i < count; i++) {
        Rectangle r = { pos.x + (i % cols) * cell, pos.y + (i / cols) * cell,
                        cell - 3, cell - 3 };
        SlotRef ref = { kind, first_index + i };
        WlSlot *slot = ref_slot(g, ref);
        if (slot) wl_ui_draw_slot_item(&g->ui, r, slot);

        if (CheckCollisionPointRec(g->ui.mouse, r) && slot) {
            DrawRectangleLinesEx((Rectangle){ r.x - 1, r.y - 1, r.width + 2, r.height + 2 },
                                 2.0f, (Color){ 240, 236, 190, 220 });
            if (slot->item != WL_ITEM_NONE && !g->ui.mouse_down) {
                const WlItem *item = wl_item_get(slot->item);
                if (item) wl_ui_tooltip(&g->ui, item->display);
            }
        }
    }
}

static SlotRef slot_at(WlGame *g, Rectangle inv_area, Rectangle craft_area,
                       Rectangle result_area, Rectangle armor_area)
{
    if (CheckCollisionPointRec(g->ui.mouse, result_area)) return (SlotRef){ SLOT_RESULT, 0 };

    if (armor_area.width > 0 && CheckCollisionPointRec(g->ui.mouse, armor_area)) {
        float cell = armor_area.width;
        int idx = (int)((g->ui.mouse.y - armor_area.y) / cell);
        if (idx >= 0 && idx < 4)
            return (SlotRef){ SLOT_ARMOR, idx };
    }

    if (CheckCollisionPointRec(g->ui.mouse, craft_area)) {
        int cols = g->player.inv.craft_w;
        float cell = craft_area.width / cols;
        int ix = (int)((g->ui.mouse.x - craft_area.x) / cell);
        int iy = (int)((g->ui.mouse.y - craft_area.y) / cell);
        if (ix >= 0 && ix < cols && iy >= 0 && iy < cols)
            return (SlotRef){ SLOT_CRAFT, iy * cols + ix };
    }

    if (CheckCollisionPointRec(g->ui.mouse, inv_area)) {
        int cols = 9;
        float cell = inv_area.width / cols;
        int ix = (int)((g->ui.mouse.x - inv_area.x) / cell);
        int iy = (int)((g->ui.mouse.y - inv_area.y) / cell);
        if (ix >= 0 && ix < cols && iy >= 0 && iy < 4) {
            /* rows 0-2: main inventory (slots 9..35), row 3: hotbar (0..8) */
            int idx = (iy < 3) ? (9 + iy * cols + ix) : (iy - 3) * cols + ix;
            if (idx >= 0 && idx < WL_INV_SIZE)
                return (SlotRef){ SLOT_INV, idx };
        }
    }
    return (SlotRef){ SLOT_NONE, 0 };
}

/* consume one ingredient per occupied grid cell after crafting */
static void consume_grid(WlInventory *inv)
{
    for (int i = 0; i < WL_CRAFT_GRID_MAX; i++) {
        WlSlot *s = &inv->craft[i];
        if (s->item == WL_ITEM_NONE) continue;
        s->count--;
        if (s->count == 0) {
            s->item = WL_ITEM_NONE;
            s->dur = 0;
        }
    }
}

static void craft_result(WlGame *g, bool craft_all)
{
    WlInventory *inv = &g->player.inv;
    const WlRecipe *r = wl_crafting_match(inv->craft, inv->craft_w);
    if (!r) return;

    int guard = 0;
    do {
        if (craft_all) {
            if (!wl_inv_add(inv, r->out_item, r->out_count, 0)) break;
        } else {
            WlSlot *held = &g->ui_held;
            if (held->item != WL_ITEM_NONE) {
                if (held->item != r->out_item) break;
                const WlItem *def = wl_item_get(held->item);
                int max = def ? def->max_stack : 64;
                if (held->count + r->out_count > max) break;
                held->count = (uint8_t)(held->count + r->out_count);
            } else {
                held->item = r->out_item;
                held->count = r->out_count;
                held->dur = 0;
            }
        }
        consume_grid(inv);
        wl_audio_play(&g->audio, WL_SND_CLICK);
        r = wl_crafting_match(inv->craft, inv->craft_w);
    } while (r && craft_all && ++guard < 64);
}

static void slot_click(WlGame *g, SlotRef ref, bool right, bool shift)
{
    WlSlot *held = &g->ui_held;

    if (ref.kind == SLOT_RESULT) {
        craft_result(g, shift);
        return;
    }
    if (ref.kind == SLOT_NONE) return;
    WlSlot *s = ref_slot(g, ref);
    if (!s) return;

    /* armor slots only accept the matching piece; taking is always ok */
    if (ref.kind == SLOT_ARMOR && held->item != WL_ITEM_NONE) {
        const WlItem *hdef = wl_item_get(held->item);
        if (!hdef || hdef->armor_slot != ref.index + 1) return;
    }

    if (shift && held->item == WL_ITEM_NONE && s->item != WL_ITEM_NONE) {
        /* quick move: armor pieces auto-equip, worn armor goes back to
         * the inventory, everything else hotbar <-> main */
        if (ref.kind == SLOT_ARMOR) {
            if (wl_inv_add(&g->player.inv, s->item, s->count, s->dur)) {
                s->item = WL_ITEM_NONE;
                s->count = 0;
                s->dur = 0;
            }
            return;
        }
        if (ref.kind == SLOT_INV) {
            const WlItem *idef = wl_item_get(s->item);
            if (idef && idef->armor_slot > 0 &&
                g->player.armor[idef->armor_slot - 1].item == WL_ITEM_NONE) {
                g->player.armor[idef->armor_slot - 1] = *s;
                s->item = WL_ITEM_NONE;
                s->count = 0;
                s->dur = 0;
                return;
            }
        }
        if (ref.kind == SLOT_CRAFT) {
            if (wl_inv_add(&g->player.inv, s->item, s->count, s->dur)) {
                s->item = WL_ITEM_NONE;
                s->count = 0;
                s->dur = 0;
            }
        } else {
            WlInventory *inv = &g->player.inv;
            bool from_hotbar = ref.index < WL_HOTBAR_SIZE;
            int lo = from_hotbar ? WL_HOTBAR_SIZE : 0;
            int hi = from_hotbar ? WL_INV_SIZE : WL_HOTBAR_SIZE;
            /* try stacking then empty slots in the target range */
            for (int pass = 0; pass < 2 && s->count > 0; pass++) {
                for (int i = lo; i < hi && s->count > 0; i++) {
                    WlSlot *d = &inv->slots[i];
                    const WlItem *def = wl_item_get(s->item);
                    int max = def ? def->max_stack : 64;
                    if (pass == 0 && d->item == s->item && d->count < max) {
                        int move = max - d->count;
                        if (move > s->count) move = s->count;
                        d->count = (uint8_t)(d->count + move);
                        s->count = (uint8_t)(s->count - move);
                    } else if (pass == 1 && d->item == WL_ITEM_NONE) {
                        d->item = s->item;
                        d->count = s->count;
                        d->dur = s->dur;
                        s->item = WL_ITEM_NONE;
                        s->count = 0;
                        s->dur = 0;
                    }
                }
            }
        }
        return;
    }

    if (!right) {
        if (held->item == WL_ITEM_NONE) {
            /* pick up the whole stack */
            *held = *s;
            s->item = WL_ITEM_NONE;
            s->count = 0;
            s->dur = 0;
        } else if (s->item == WL_ITEM_NONE) {
            *s = *held;
            held->item = WL_ITEM_NONE;
            held->count = 0;
            held->dur = 0;
        } else if (s->item == held->item) {
            const WlItem *def = wl_item_get(s->item);
            int max = def ? def->max_stack : 64;
            int move = max - s->count;
            if (move > held->count) move = held->count;
            s->count = (uint8_t)(s->count + move);
            held->count = (uint8_t)(held->count - move);
            if (held->count == 0) held->item = WL_ITEM_NONE;
        } else {
            WlSlot tmp = *s;
            *s = *held;
            *held = tmp;
        }
    } else {
        if (held->item == WL_ITEM_NONE && s->item != WL_ITEM_NONE) {
            /* pick up half */
            int half = (s->count + 1) / 2;
            held->item = s->item;
            held->count = (uint8_t)half;
            held->dur = s->dur;
            s->count = (uint8_t)(s->count - half);
            if (s->count == 0) s->item = WL_ITEM_NONE;
        } else if (held->item != WL_ITEM_NONE) {
            if (s->item == WL_ITEM_NONE) {
                s->item = held->item;
                s->count = 1;
                s->dur = held->dur;
                held->count--;
                if (held->count == 0) held->item = WL_ITEM_NONE;
            } else if (s->item == held->item) {
                const WlItem *def = wl_item_get(s->item);
                int max = def ? def->max_stack : 64;
                if (s->count < max) {
                    s->count++;
                    held->count--;
                    if (held->count == 0) held->item = WL_ITEM_NONE;
                }
            }
        }
    }
    wl_audio_play(&g->audio, WL_SND_CLICK);
}

/* ------------------------------------------------------------------ */
/* creative item catalogue                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    CAT_BLOCKS = 0,
    CAT_NATURE,
    CAT_TOOLS,
    CAT_EQUIPMENT,
    CAT_FOOD,
    CAT_MATERIALS,
    CAT_COUNT
} CatId;

static const char *k_cat_names[CAT_COUNT] = {
    "Building", "Nature", "Tools", "Gear", "Food", "Materials"
};

static int item_category(WlItemId id)
{
    const WlItem *d = wl_item_get(id);
    if (!d) return -1;
    if (d->armor_slot > 0) return CAT_EQUIPMENT;
    if (d->tool != WL_TOOL_NONE) return CAT_TOOLS;
    if (d->food > 0) return CAT_FOOD;
    if (!d->is_block) return CAT_MATERIALS;
    const char *n = d->name;
    if (strstr(n, "flower") || strstr(n, "grass") || strstr(n, "log") ||
        strstr(n, "leaves") || strstr(n, "cactus") || strstr(n, "sand") ||
        strstr(n, "snow") || strstr(n, "ice") || strstr(n, "gravel") ||
        strstr(n, "dirt") || strstr(n, "wheat") || strstr(n, "seeds"))
        return CAT_NATURE;
    return CAT_BLOCKS;
}

/* obtainable in creative: everything registered except the unobtainable
 * water shadow item and the lit furnace (a running state, not a item) */
static bool catalogue_visible(WlItemId id)
{
    const WlItem *d = wl_item_get(id);
    if (!d) return false;
    if (strcmp(d->name, "water") == 0) return false;
    if (strcmp(d->name, "furnace_lit") == 0) return false;
    return true;
}

static int s_cat_tab = CAT_BLOCKS;
static float s_cat_scroll = 0.0f;
static char s_cat_search[24] = "";

static void str_lower(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = (src[i] >= 'A' && src[i] <= 'Z') ? (char)(src[i] + 32) : src[i];
    dst[i] = '\0';
}

/* draws the catalogue: category tabs, a search field and a scrollable
 * grid of every obtainable item, on top of the player inventory rows */
static void draw_catalogue(WlGame *g, Rectangle area)
{
    WlUi *ui = &g->ui;
    float cell = area.width / 9.0f;

    /* tabs */
    Font f = wl_ui_font(ui);
    float tab_fs = 15.0f * ui->scale;
    float tx = area.x;
    for (int c = 0; c < CAT_COUNT; c++) {
        const char *name = k_cat_names[c];
        Vector2 ts = MeasureTextEx(f, name, tab_fs, 1.0f);
        Rectangle tr = { tx, area.y, ts.x + 18.0f * ui->scale,
                         24.0f * ui->scale };
        bool active = (s_cat_tab == c);
        DrawRectangleRec(tr, active ? (Color){ 74, 78, 88, 255 }
                                    : (Color){ 44, 46, 52, 255 });
        DrawTextEx(f, name, (Vector2){ tr.x + 9 * ui->scale, tr.y + 4 },
                   tab_fs, 1.0f, active ? (Color){ 240, 236, 190, 255 }
                                        : (Color){ 165, 172, 182, 255 });
        if (CheckCollisionPointRec(ui->mouse, tr) && ui->mouse_pressed) {
            s_cat_tab = c;
            s_cat_scroll = 0.0f;
        }
        tx += tr.width + 6.0f * ui->scale;
    }

    /* search field (top right) */
    Rectangle search_r = { area.x + area.width - 170.0f * ui->scale,
                           area.y + 28.0f * ui->scale,
                           170.0f * ui->scale, 26.0f * ui->scale };
    DrawTextEx(f, "search:", (Vector2){ search_r.x - 52 * ui->scale,
                                        search_r.y + 5 }, tab_fs, 1.0f,
               (Color){ 165, 172, 182, 255 });
    wl_ui_textfield(ui, 77, search_r, s_cat_search, sizeof(s_cat_search));

    /* collect the visible items for the active tab + search filter */
    WlItemId list[WL_ITEM_MAX];
    int n = 0;
    char needle[24];
    str_lower(needle, s_cat_search, sizeof(needle));
    int total = wl_items_count();
    for (WlItemId id = 1; id < (WlItemId)total && n < WL_ITEM_MAX; id++) {
        if (!catalogue_visible(id)) continue;
        if (item_category(id) != s_cat_tab) continue;
        if (needle[0]) {
            char hay[64];
            str_lower(hay, wl_item_get(id)->display, sizeof(hay));
            if (!strstr(hay, needle)) continue;
        }
        list[n++] = id;
    }

    /* scrollable grid */
    int cols = 9;
    int rows = (n + cols - 1) / cols;
    Rectangle grid_r = { area.x, area.y + 60.0f * ui->scale,
                         area.width, cell * 3 };
    if (CheckCollisionPointRec(ui->mouse, grid_r))
        s_cat_scroll -= ui->wheel * cell;
    float max_scroll = (rows - 3) * cell;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (s_cat_scroll < 0.0f) s_cat_scroll = 0.0f;
    if (s_cat_scroll > max_scroll) s_cat_scroll = max_scroll;

    /* border + clip region */
    DrawRectangleRec(grid_r, (Color){ 30, 32, 36, 255 });
    DrawRectangleLinesEx(grid_r, 2.0f, (Color){ 70, 74, 82, 255 });
    int first_row = (int)(s_cat_scroll / cell);
    int y0 = (int)(grid_r.y - (s_cat_scroll - first_row * cell));
    for (int row = first_row; row <= first_row + 3 && row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int idx = row * cols + col;
            if (idx >= n) break;
            WlItemId id = list[idx];
            Rectangle r = { grid_r.x + col * cell + 2,
                            y0 + (row - first_row) * cell + 2,
                            cell - 4, cell - 4 };
            if (r.y + r.height <= grid_r.y || r.y >= grid_r.y + grid_r.height)
                continue; /* clipped */
            WlSlot display = { id, 1, 0 };
            wl_ui_draw_slot_item(ui, r, &display);
            if (CheckCollisionPointRec(ui->mouse, r)) {
                DrawRectangleLinesEx((Rectangle){ r.x - 1, r.y - 1,
                                                 r.width + 2, r.height + 2 },
                                     2.0f, (Color){ 240, 236, 190, 220 });
                if (!ui->mouse_down) {
                    const WlItem *item = wl_item_get(id);
                    if (item) wl_ui_tooltip(ui, item->display);
                }
                /* left: take a full stack (or trash the held one);
                 * right: take a single item */
                if (ui->mouse_pressed) {
                    if (g->ui_held.item != WL_ITEM_NONE) {
                        g->ui_held.item = WL_ITEM_NONE; /* creative trash */
                        g->ui_held.count = 0;
                        g->ui_held.dur = 0;
                    } else {
                        const WlItem *def = wl_item_get(id);
                        g->ui_held = (WlSlot){ id,
                                (uint8_t)(def ? def->max_stack : 64), 0 };
                    }
                    wl_audio_play(&g->audio, WL_SND_CLICK);
                } else if (ui->mouse_right_pressed) {
                    if (g->ui_held.item == WL_ITEM_NONE)
                        g->ui_held = (WlSlot){ id, 1, 0 };
                    wl_audio_play(&g->audio, WL_SND_CLICK);
                }
            }
        }
    }
    if (n == 0)
        DrawTextEx(f, "nothing matches", (Vector2){ grid_r.x + 12, grid_r.y + 12 },
                   tab_fs, 1.0f, (Color){ 140, 144, 152, 255 });
}

void wl_inv_ui_close(WlGame *g)
{
    /* return crafting grid + held stack to the inventory (never lost) */
    for (int i = 0; i < WL_CRAFT_GRID_MAX; i++) {
        WlSlot *s = &g->player.inv.craft[i];
        if (s->item == WL_ITEM_NONE) continue;
        if (wl_inv_add(&g->player.inv, s->item, s->count, s->dur)) {
            s->item = WL_ITEM_NONE;
            s->count = 0;
            s->dur = 0;
        }
    }
    if (g->ui_held.item != WL_ITEM_NONE) {
        if (wl_inv_add(&g->player.inv, g->ui_held.item, g->ui_held.count,
                       g->ui_held.dur)) {
            g->ui_held.item = WL_ITEM_NONE;
            g->ui_held.count = 0;
            g->ui_held.dur = 0;
        }
    }
    g->player.inv.craft_w = 0;
    g->overlay = WL_OVERLAY_NONE;
}

void wl_inv_ui_draw(WlGame *g)
{
    if (g->overlay != WL_OVERLAY_INVENTORY && g->overlay != WL_OVERLAY_CRAFTING)
        return;

    wl_ui_dim(0.5f);

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    WlInventory *inv = &g->player.inv;
    bool creative = (g->overlay == WL_OVERLAY_INVENTORY) &&
                    wl_world_gamemode(g->world) == WL_WORLD_GAMEMODE_CREATIVE;
    int cw = creative ? 0 : inv->craft_w;

    float cell = 46 * g->ui.scale;
    /* character mode (survival inventory, not the crafting table and
     * not creative) adds a preview + armor column on the left */
    bool character = (g->overlay == WL_OVERLAY_INVENTORY) && !creative;
    float col = character ? 170.0f * g->ui.scale : 0.0f;
    float pw = cell * 9 + 48 * g->ui.scale + col;
    float grid_h = creative ? cell * 3 + 96.0f * g->ui.scale : cell * cw;
    float ph = grid_h + cell * 5 + 96 * g->ui.scale;
    Rectangle panel = { (sw - pw) / 2, (sh - ph) / 2, pw, ph };
    wl_ui_panel(&g->ui, panel);

    /* left column: rotating character preview + the four armor slots */
    Rectangle armor_area = { 0, 0, 0, 0 };
    static float s_prev_yaw = 0.55f;
    if (character) {
        float px0 = panel.x + 18 * g->ui.scale;
        float py0 = panel.y + 50 * g->ui.scale;
        float pw2 = 118 * g->ui.scale, ph2 = 168 * g->ui.scale;
        Rectangle prev_r = { px0, py0, pw2, ph2 };

        /* drag to rotate */
        static bool s_prev_drag = false;
        static Vector2 s_last_mouse = { 0, 0 };
        if (CheckCollisionPointRec(g->ui.mouse, prev_r) && g->ui.mouse_pressed)
            s_prev_drag = true;
        if (!g->ui.mouse_down) s_prev_drag = false;
        if (s_prev_drag) {
            s_prev_yaw += (g->ui.mouse.x - s_last_mouse.x) * 0.02f;
        }
        s_last_mouse = g->ui.mouse;

        Texture2D pt = wl_renderer_player_preview(
            &g->rend, g->player.armor, s_prev_yaw);
        if (pt.id != 0) {
            Rectangle src = { 0, 0, (float)pt.width, -(float)pt.height };
            DrawTexturePro(pt, src, prev_r, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
        DrawRectangleLinesEx(prev_r, 2.0f, (Color){ 70, 74, 82, 255 });
        DrawTextEx(wl_ui_font(&g->ui), "drag to turn",
                   (Vector2){ px0 + 18 * g->ui.scale, py0 + ph2 + 4 * g->ui.scale },
                   13 * g->ui.scale, 1.0f, (Color){ 140, 146, 154, 255 });

        /* armor slots: helmet, chest, legs, boots (top to bottom) */
        float ax = px0 + pw2 + 12 * g->ui.scale;
        float ay = py0 + 8 * g->ui.scale;
        armor_area = (Rectangle){ ax, ay, cell, cell * 4 + 24.0f * g->ui.scale };
        static const char *k_slot_names[4] = { "helmet", "chest", "legs", "boots" };
        for (int i = 0; i < 4; i++) {
            Rectangle r = { ax, ay + i * (cell + 8.0f * g->ui.scale),
                            cell - 3.0f * g->ui.scale, cell - 3.0f * g->ui.scale };
            WlSlot *s = &g->player.armor[i];
            wl_ui_draw_slot_item(&g->ui, r, s);
            if (s->item == WL_ITEM_NONE) {
                /* empty-slot hint label */
                DrawTextEx(wl_ui_font(&g->ui), k_slot_names[i],
                           (Vector2){ r.x + 3, r.y + r.height - 13 * g->ui.scale },
                           11 * g->ui.scale, 1.0f, (Color){ 110, 116, 124, 255 });
            }
            if (CheckCollisionPointRec(g->ui.mouse, r)) {
                DrawRectangleLinesEx((Rectangle){ r.x - 1, r.y - 1,
                                                 r.width + 2, r.height + 2 },
                                     2.0f, (Color){ 240, 236, 190, 220 });
                if (s->item != WL_ITEM_NONE && !g->ui.mouse_down) {
                    const WlItem *item = wl_item_get(s->item);
                    if (item) {
                        char tip[96];
                        snprintf(tip, sizeof(tip), "%s (%d armor)",
                                 item->display, (int)item->armor_points);
                        wl_ui_tooltip(&g->ui, tip);
                    }
                }
            }
        }
    }

    /* title */
    Font tf = wl_ui_title_font(&g->ui);
    const char *title = creative ? "Creative"
                     : (g->overlay == WL_OVERLAY_CRAFTING) ? "Crafting" : "Inventory";
    float fs = 16 * g->ui.scale;
    DrawTextEx(tf, title, (Vector2){ panel.x + 20, panel.y + 14 }, fs, 2.0f,
               (Color){ 235, 235, 225, 255 });

    float gx = panel.x + 24 * g->ui.scale + col;
    float gy = panel.y + 56 * g->ui.scale;

    if (creative) {
        /* catalogue replaces the crafting area; the player inventory
         * below stays interactive for arranging what you take */
        Rectangle cat_area = { gx, gy, cell * 9, cell * 3 + 60.0f * g->ui.scale };
        draw_catalogue(g, cat_area);
    }

    /* crafting grid -> arrow -> result */
    Rectangle craft_area = { gx, gy, cell * cw, cell * cw };
    if (!creative)
        draw_grid(g, craft_area, SLOT_CRAFT, cw * cw, cw, 0);

    const WlRecipe *recipe = creative ? NULL : wl_crafting_match(inv->craft, cw);
    Rectangle result_area = creative
        ? (Rectangle){ 0, 0, 0, 0 }
        : (Rectangle){ gx + cell * cw + 28 * g->ui.scale,
                       gy + (cell * cw) / 2 - cell / 2, cell, cell };
    Font arrowf = wl_ui_font(&g->ui);
    DrawTextEx(arrowf, "->", (Vector2){ gx + cell * cw + 4, gy + (cell * cw) / 2 - 14 },
               28 * g->ui.scale, 1.0f, (Color){ 200, 206, 214, 255 });
    if (recipe) {
        WlSlot out = { recipe->out_item, recipe->out_count, 0 };
        /* (creative never reaches here: recipe is NULL) */
        wl_ui_draw_slot_item(&g->ui, result_area, &out);
        const WlItem *out_item = wl_item_get(recipe->out_item);
        if (out_item && CheckCollisionPointRec(g->ui.mouse, result_area) &&
            !g->ui.mouse_down)
            wl_ui_tooltip(&g->ui, out_item->display);
    } else {
        wl_ui_draw_slot_item(&g->ui, result_area, &(WlSlot){ 0 });
    }
    DrawRectangleLinesEx(result_area, 2.0f,
                         recipe ? (Color){ 230, 226, 170, 200 } : (Color){ 70, 74, 82, 200 });

    /* main inventory 3x9 (slots 9..35) + hotbar 1x9 (slots 0..8) */
    float iy = gy + grid_h + 24 * g->ui.scale;
    Rectangle inv_area = { gx, iy, cell * 9, cell * 4 + 14 * g->ui.scale };
    draw_grid(g, (Rectangle){ inv_area.x, inv_area.y, cell * 9, cell * 3 },
              SLOT_INV, 27, 9, 9);

    float hy = iy + cell * 3 + 14 * g->ui.scale;
    draw_grid(g, (Rectangle){ gx, hy, cell * 9, cell }, SLOT_INV, 9, 9, 0);

    /* interactions */
    SlotRef ref = slot_at(g, inv_area, craft_area, result_area, armor_area);
    if (g->ui.mouse_pressed && ref.kind != SLOT_NONE)
        slot_click(g, ref, false, IsKeyDown(KEY_LEFT_SHIFT));
    else if (g->ui.mouse_right_pressed && ref.kind != SLOT_NONE)
        slot_click(g, ref, true, false);

    /* held stack at the cursor */
    if (g->ui_held.item != WL_ITEM_NONE) {
        Rectangle held_r = { g->ui.mouse.x - cell / 2, g->ui.mouse.y - cell / 2,
                             cell, cell };
        wl_ui_draw_slot_item(&g->ui, held_r, &g->ui_held);
    }

    /* NOTE: close handling (inventory key / Esc) lives in play_frame's
     * global-keys block, NOT here. This screen draws while the overlay is
     * open on the very frame the key opened it, and IsKeyPressed() stays
     * true for the whole frame — a close check here would immediately
     * close the inventory again ("inventory instantly closes" bug). */
}
