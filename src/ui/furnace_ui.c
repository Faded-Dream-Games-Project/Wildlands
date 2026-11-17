/* furnace_ui.c — the furnace overlay: input, fuel and output slots with
 * smelting progress and burn indicators, combined with the player's
 * inventory and hotbar so ingredients and fuel can be moved without
 * closing the screen. Slot interaction follows the inventory's
 * cursor-stack model (click to pick up / put down, right-click to split,
 * shift-click to quick-move). */
#include "wl/inv_ui.h"
#include "wl/game.h"
#include "wl/world.h"
#include "wl/furnace_ui.h"
#include "wl/audio.h"
#include "wl/log.h"

#include <string.h>

/* position of the furnace this screen is bound to (set when opening) */
static int s_fx, s_fy, s_fz;

void wl_furnace_ui_open(WlGame *g, int x, int y, int z)
{
    /* the block-entity record is created lazily on first open; without
     * it the draw below would instantly self-close the overlay — after
     * the cursor had already been released, leaving the game with a
     * free cursor and no UI (the "furnace eats my mouse" bug) */
    if (!wl_world_get_furnace(g->world, x, y, z, true)) {
        WL_LOGW("furnace store full; not opening the UI at %d,%d,%d",
                x, y, z);
        return;
    }
    s_fx = x;
    s_fy = y;
    s_fz = z;
    g->overlay = WL_OVERLAY_FURNACE;
    EnableCursor();
    WL_LOGI("furnace ui opened at %d,%d,%d", x, y, z);
}

void wl_furnace_ui_close(WlGame *g)
{
    if (g->overlay != WL_OVERLAY_FURNACE) return;
    /* the cursor stack returns to the inventory (never lost) */
    if (g->ui_held.item != WL_ITEM_NONE) {
        if (wl_inv_add(&g->player.inv, g->ui_held.item, g->ui_held.count,
                       g->ui_held.dur)) {
            g->ui_held.item = WL_ITEM_NONE;
            g->ui_held.count = 0;
            g->ui_held.dur = 0;
        }
    }
    g->overlay = WL_OVERLAY_NONE;
}

/* ------------------------------------------------------------------ */
/* slot interaction                                                    */
/* ------------------------------------------------------------------ */

/* where the click landed */
typedef enum {
    FSEL_NONE = 0,
    FSEL_IN,
    FSEL_FUEL,
    FSEL_OUT,
    FSEL_INV, /* index 0..35 */
} FselKind;

typedef struct {
    FselKind kind;
    int index;
} Fsel;

static WlSlot *fsel_slot(WlGame *g, Fsel s, WlFurnace *f)
{
    if (s.kind == FSEL_IN) return &f->input;
    if (s.kind == FSEL_FUEL) return &f->fuel;
    if (s.kind == FSEL_OUT) return &f->output;
    if (s.kind == FSEL_INV) return &g->player.inv.slots[s.index];
    return NULL;
}

/* items the slot accepts (output is take-only) */
static bool fsel_accepts(FselKind k, WlItemId item)
{
    const WlItem *def = wl_item_get(item);
    if (!def) return false;
    if (k == FSEL_IN) return wl_furnace_smelt_result(item) != WL_ITEM_NONE;
    if (k == FSEL_FUEL) return wl_furnace_fuel_time(item) > 0.0f;
    return true;
}

/* shift-click quick move out of an inventory slot: smeltables go to the
 * input, fuel to the fuel slot, everything else hotbar <-> main */
static void quick_move_from_inv(WlGame *g, WlFurnace *f, int idx)
{
    WlSlot *s = &g->player.inv.slots[idx];
    if (s->item == WL_ITEM_NONE) return;

    WlSlot *dst = NULL;
    if (wl_furnace_smelt_result(s->item) != WL_ITEM_NONE) dst = &f->input;
    else if (wl_furnace_fuel_time(s->item) > 0.0f) dst = &f->fuel;
    if (dst) {
        const WlItem *def = wl_item_get(s->item);
        int max = def ? def->max_stack : 64;
        if (dst->item == WL_ITEM_NONE) {
            dst->item = s->item;
            dst->count = s->count;
            dst->dur = s->dur;
            s->item = WL_ITEM_NONE;
            s->count = 0;
            s->dur = 0;
            return;
        }
        if (dst->item == s->item && dst->count < max) {
            int move = max - dst->count;
            if (move > s->count) move = s->count;
            dst->count = (uint8_t)(dst->count + move);
            s->count = (uint8_t)(s->count - move);
            if (s->count == 0) {
                s->item = WL_ITEM_NONE;
                s->dur = 0;
            }
            return;
        }
        return; /* target full: no fallback, keep it simple and honest */
    }

    /* hotbar <-> main inventory, same as the inventory screen */
    WlInventory *inv = &g->player.inv;
    bool from_hotbar = idx < WL_HOTBAR_SIZE;
    int lo = from_hotbar ? WL_HOTBAR_SIZE : 0;
    int hi = from_hotbar ? WL_INV_SIZE : WL_HOTBAR_SIZE;
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

/* move a furnace slot's contents back into the inventory (shift-click) */
static void quick_move_to_inv(WlGame *g, WlSlot *s)
{
    if (s->item == WL_ITEM_NONE) return;
    if (wl_inv_add(&g->player.inv, s->item, s->count, s->dur)) {
        s->item = WL_ITEM_NONE;
        s->count = 0;
        s->dur = 0;
    }
}

static void fsel_click(WlGame *g, WlFurnace *f, Fsel sel, bool right, bool shift)
{
    WlSlot *held = &g->ui_held;
    WlSlot *s = fsel_slot(g, sel, f);
    if (!s) return;

    /* --- shift quick moves --- */
    if (shift && held->item == WL_ITEM_NONE && s->item != WL_ITEM_NONE) {
        if (sel.kind == FSEL_INV) quick_move_from_inv(g, f, sel.index);
        else quick_move_to_inv(g, s); /* furnace slots -> inventory */
        wl_audio_play(&g->audio, WL_SND_CLICK);
        return;
    }

    /* --- output: take only --- */
    if (sel.kind == FSEL_OUT) {
        if (s->item == WL_ITEM_NONE) return;
        if (!right) {
            if (held->item == WL_ITEM_NONE) {
                *held = *s;
            } else if (held->item == s->item) {
                const WlItem *def = wl_item_get(held->item);
                int max = def ? def->max_stack : 64;
                int move = max - held->count;
                if (move > s->count) move = s->count;
                held->count = (uint8_t)(held->count + move);
                s->count = (uint8_t)(s->count - move);
                if (s->count == 0) {
                    s->item = WL_ITEM_NONE;
                    s->dur = 0;
                }
            } else {
                return;
            }
        } else {
            return; /* no splitting on take-only */
        }
        if (held->count > 0 && held->item != WL_ITEM_NONE)
            wl_audio_play(&g->audio, WL_SND_CLICK);
        return;
    }

    /* --- normal cursor-stack clicks (with furnace validation) --- */
    if (!right) {
        if (held->item == WL_ITEM_NONE) {
            if (s->item == WL_ITEM_NONE) return;
            *held = *s;
            s->item = WL_ITEM_NONE;
            s->count = 0;
            s->dur = 0;
        } else if (s->item == WL_ITEM_NONE) {
            if (!fsel_accepts(sel.kind, held->item)) return;
            *s = *held;
            held->item = WL_ITEM_NONE;
            held->count = 0;
            held->dur = 0;
        } else if (s->item == held->item) {
            const WlItem *def = wl_item_get(s->item);
            int max = def ? def->max_stack : 64;
            int move = max - s->count;
            if (move > held->count) move = held->count;
            if (move > 0) {
                s->count = (uint8_t)(s->count + move);
                held->count = (uint8_t)(held->count - move);
                if (held->count == 0) {
                    held->item = WL_ITEM_NONE;
                    held->dur = 0;
                }
            }
        } else {
            /* swap: the incoming stack must be valid for the slot */
            if (!fsel_accepts(sel.kind, held->item)) return;
            WlSlot tmp = *s;
            *s = *held;
            *held = tmp;
        }
    } else {
        /* right-click: split / place one */
        if (held->item == WL_ITEM_NONE && s->item != WL_ITEM_NONE) {
            int half = (s->count + 1) / 2;
            held->item = s->item;
            held->count = (uint8_t)half;
            held->dur = s->dur;
            s->count = (uint8_t)(s->count - half);
            if (s->count == 0) s->item = WL_ITEM_NONE;
        } else if (held->item != WL_ITEM_NONE) {
            if (s->item == WL_ITEM_NONE) {
                if (!fsel_accepts(sel.kind, held->item)) return;
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
/* layout + draw                                                        */
/* ------------------------------------------------------------------ */

void wl_furnace_ui_draw(WlGame *g)
{
    if (g->overlay != WL_OVERLAY_FURNACE) return;
    WlFurnace *f = wl_world_get_furnace(g->world, s_fx, s_fy, s_fz, false);
    if (!f) {
        /* the furnace was broken underneath us: close properly so the
         * cursor state stays consistent (the caller re-locks it) */
        wl_furnace_ui_close(g);
        DisableCursor();
        return;
    }

    WlUi *ui = &g->ui;
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    float cell = 46.0f * ui->scale;
    float pw = cell * 9 + 48.0f * ui->scale;
    /* furnace row + gap + inventory 4 rows + title/footer */
    float ph = 52.0f * ui->scale + cell * 2 + 10.0f * ui->scale +
               cell * 4 + 14.0f * ui->scale + 70.0f * ui->scale;
    Rectangle panel = { (sw - pw) / 2.0f, (sh - ph) / 2.0f, pw, ph };

    wl_ui_dim(0.5f);
    wl_ui_panel(ui, panel);

    Font tf = wl_ui_title_font(ui);
    float title_fs = 20.0f * ui->scale;
    DrawTextEx(tf, "Furnace", (Vector2){ panel.x + 20 * ui->scale,
                                         panel.y + 14 * ui->scale },
               title_fs, 2.0f, (Color){ 235, 230, 210, 255 });

    float slot_px = cell - 3.0f * ui->scale;
    float fx0 = panel.x + 24.0f * ui->scale;
    float fy0 = panel.y + 52.0f * ui->scale;
    Rectangle in_r = { fx0, fy0, slot_px, slot_px };
    Rectangle fuel_r = { fx0, fy0 + cell + 8.0f * ui->scale, slot_px, slot_px };
    Rectangle out_r = { panel.x + pw / 2 + cell * 1.1f,
                        fy0 + cell * 0.5f, slot_px * 1.15f, slot_px * 1.15f };
    Rectangle arrow_r = { fuel_r.x + cell + 6.0f * ui->scale,
                          in_r.y + slot_px * 0.25f,
                          out_r.x - fuel_r.x - cell - 12.0f * ui->scale,
                          slot_px * 0.5f };

    /* --- smelting progress arrow (outline + fill) --- */
    DrawRectangleRec(arrow_r, (Color){ 24, 26, 30, 255 });
    if (f->progress > 0.0f) {
        Rectangle fill = arrow_r;
        fill.width *= f->progress < 1.0f ? f->progress : 1.0f;
        DrawRectangleRec(fill, (Color){ 235, 190, 90, 255 });
    }
    DrawRectangleLinesEx(arrow_r, 2.0f, (Color){ 90, 92, 96, 255 });

    /* --- fuel burn indicator (flame bar under the arrow) --- */
    Rectangle flame_r = { arrow_r.x, arrow_r.y + arrow_r.height + 8.0f * ui->scale,
                          arrow_r.width, 8.0f * ui->scale };
    DrawRectangleRec(flame_r, (Color){ 24, 26, 30, 255 });
    if (f->burn_total > 0.0f && f->burn_left > 0.0f) {
        Rectangle burn = flame_r;
        burn.width *= f->burn_left / f->burn_total;
        DrawRectangleRec(burn, (Color){ 240, 130, 40, 255 });
    }
    DrawRectangleLinesEx(flame_r, 1.5f, (Color){ 90, 92, 96, 255 });

    /* --- furnace slots --- */
    struct { Rectangle r; WlSlot *slot; FselKind kind; } fslots[3] = {
        { in_r, &f->input, FSEL_IN },
        { fuel_r, &f->fuel, FSEL_FUEL },
        { out_r, &f->output, FSEL_OUT },
    };
    for (int i = 0; i < 3; i++) {
        wl_ui_draw_slot_item(ui, fslots[i].r, fslots[i].slot);
        bool hover = CheckCollisionPointRec(ui->mouse, fslots[i].r);
        if (hover)
            DrawRectangleLinesEx((Rectangle){ fslots[i].r.x - 1, fslots[i].r.y - 1,
                                             fslots[i].r.width + 2,
                                             fslots[i].r.height + 2 },
                                 2.0f, (Color){ 240, 236, 190, 220 });
        if (hover && fslots[i].slot->item != WL_ITEM_NONE && !ui->mouse_down) {
            const WlItem *item = wl_item_get(fslots[i].slot->item);
            if (item) wl_ui_tooltip(ui, item->display);
        }
    }

    /* --- player inventory: 3x9 main + 1x9 hotbar (same geometry as
     * the inventory screen, so muscle memory carries over) --- */
    float iy = fy0 + cell * 2 + 10.0f * ui->scale;
    Rectangle inv_area = { fx0, iy, cell * 9, cell * 3 };
    Rectangle hot_area = { fx0, iy + cell * 3 + 14.0f * ui->scale,
                           cell * 9, cell };
    for (int row = 0; row < 4; row++) {
        /* rows 0-2 stack downward inside inv_area; row 3 is the hotbar */
        float row_y = row < 3 ? inv_area.y + row * cell : hot_area.y;
        int first = row < 3 ? 9 + row * 9 : 0;
        for (int i = 0; i < 9; i++) {
            Rectangle r = { inv_area.x + i * cell, row_y,
                            cell - 3.0f * ui->scale, cell - 3.0f * ui->scale };
            WlSlot *slot = &g->player.inv.slots[first + i];
            wl_ui_draw_slot_item(ui, r, slot);
            if (CheckCollisionPointRec(ui->mouse, r)) {
                DrawRectangleLinesEx((Rectangle){ r.x - 1, r.y - 1,
                                                 r.width + 2, r.height + 2 },
                                     2.0f, (Color){ 240, 236, 190, 220 });
                if (slot->item != WL_ITEM_NONE && !ui->mouse_down) {
                    const WlItem *item = wl_item_get(slot->item);
                    if (item) wl_ui_tooltip(ui, item->display);
                }
            }
        }
    }

    /* --- hit test + clicks --- */
    Fsel sel = { FSEL_NONE, 0 };
    for (int i = 0; i < 3; i++)
        if (CheckCollisionPointRec(ui->mouse, fslots[i].r))
            sel = (Fsel){ fslots[i].kind, 0 };
    if (sel.kind == FSEL_NONE && CheckCollisionPointRec(ui->mouse, inv_area)) {
        int ix = (int)((ui->mouse.x - inv_area.x) / cell);
        int iy2 = (int)((ui->mouse.y - inv_area.y) / cell);
        sel = (Fsel){ FSEL_INV, 9 + iy2 * 9 + ix };
    }
    if (sel.kind == FSEL_NONE && CheckCollisionPointRec(ui->mouse, hot_area)) {
        int ix = (int)((ui->mouse.x - hot_area.x) / cell);
        sel = (Fsel){ FSEL_INV, ix };
    }
    if (sel.kind != FSEL_NONE) {
        if (ui->mouse_pressed)
            fsel_click(g, f, sel, false, IsKeyDown(KEY_LEFT_SHIFT) ||
                                              IsKeyDown(KEY_RIGHT_SHIFT));
        else if (ui->mouse_right_pressed)
            fsel_click(g, f, sel, true, false);
    }

    /* cursor stack */
    if (g->ui_held.item != WL_ITEM_NONE) {
        Rectangle held_r = { ui->mouse.x - cell / 2, ui->mouse.y - cell / 2,
                             cell, cell };
        wl_ui_draw_slot_item(ui, held_r, &g->ui_held);
    }

    /* closing is handled by play_frame's global keys (E/Esc), matching
     * the inventory overlay; the furnace keeps its contents. */
}
