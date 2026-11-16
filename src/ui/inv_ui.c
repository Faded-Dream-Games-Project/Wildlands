/* inv_ui.c — inventory screen with drag & drop, crafting grid and result. */
#include "wl/inv_ui.h"
#include "wl/game.h"
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
} SlotKind;

typedef struct {
    SlotKind kind;
    int index;
} SlotRef;

static WlSlot *ref_slot(WlGame *g, SlotRef s)
{
    if (s.kind == SLOT_INV) return &g->player.inv.slots[s.index];
    if (s.kind == SLOT_CRAFT) return &g->player.inv.craft[s.index];
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
                       Rectangle result_area)
{
    if (CheckCollisionPointRec(g->ui.mouse, result_area)) return (SlotRef){ SLOT_RESULT, 0 };

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

    if (shift && held->item == WL_ITEM_NONE && s->item != WL_ITEM_NONE) {
        /* quick move: hotbar <-> main inventory, or grid -> inventory */
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
    int cw = inv->craft_w;

    float cell = 46 * g->ui.scale;
    float pw = cell * 9 + 48 * g->ui.scale;
    float grid_h = cell * cw;
    float ph = grid_h + cell * 5 + 96 * g->ui.scale;
    Rectangle panel = { (sw - pw) / 2, (sh - ph) / 2, pw, ph };
    wl_ui_panel(&g->ui, panel);

    /* title */
    Font tf = wl_ui_title_font(&g->ui);
    const char *title = (g->overlay == WL_OVERLAY_CRAFTING) ? "Crafting" : "Inventory";
    float fs = 16 * g->ui.scale;
    DrawTextEx(tf, title, (Vector2){ panel.x + 20, panel.y + 14 }, fs, 2.0f,
               (Color){ 235, 235, 225, 255 });

    /* crafting grid -> arrow -> result */
    float gx = panel.x + 24 * g->ui.scale;
    float gy = panel.y + 56 * g->ui.scale;
    Rectangle craft_area = { gx, gy, cell * cw, cell * cw };
    draw_grid(g, craft_area, SLOT_CRAFT, cw * cw, cw, 0);

    const WlRecipe *recipe = wl_crafting_match(inv->craft, cw);
    Rectangle result_area = { gx + cell * cw + 28 * g->ui.scale,
                              gy + (cell * cw) / 2 - cell / 2, cell, cell };
    Font arrowf = wl_ui_font(&g->ui);
    DrawTextEx(arrowf, "->", (Vector2){ gx + cell * cw + 4, gy + (cell * cw) / 2 - 14 },
               28 * g->ui.scale, 1.0f, (Color){ 200, 206, 214, 255 });
    if (recipe) {
        WlSlot out = { recipe->out_item, recipe->out_count, 0 };
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
    SlotRef ref = slot_at(g, inv_area, craft_area, result_area);
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

    /* close: E / ESC */
    if (IsKeyPressed(g->keys.key[WL_ACT_INVENTORY]) ||
        IsKeyPressed(g->keys.key[WL_ACT_PAUSE])) {
        wl_inv_ui_close(g);
        DisableCursor();
    }
}
