/* furnace_ui.c — the furnace overlay: input, fuel and output slots with
 * smelting progress and burn indicators. Slot interaction follows the
 * inventory's cursor-stack model (click to pick up / put down). */
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
    float pw = cell * 4 + 64.0f * ui->scale;
    float ph = cell * 3 + 96.0f * ui->scale;
    Rectangle panel = { (sw - pw) / 2.0f, (sh - ph) / 2.0f, pw, ph };

    wl_ui_dim(0.5f);
    wl_ui_panel(ui, panel);

    Font tf = wl_ui_title_font(ui);
    float title_fs = 20.0f * ui->scale;
    DrawTextEx(tf, "Furnace", (Vector2){ panel.x + 20 * ui->scale,
                                         panel.y + 14 * ui->scale },
               title_fs, 2.0f, (Color){ 235, 230, 210, 255 });

    float slot_px = cell - 3.0f * ui->scale;
    Rectangle in_r = { panel.x + 22 * ui->scale,
                       panel.y + 52 * ui->scale, slot_px, slot_px };
    Rectangle fuel_r = { in_r.x, in_r.y + cell + 8 * ui->scale, slot_px, slot_px };
    Rectangle out_r = { panel.x + pw - 22 * ui->scale - slot_px,
                        fuel_r.y, slot_px * 1.15f, slot_px * 1.15f };
    Rectangle arrow_r = { fuel_r.x + cell + 6 * ui->scale,
                          in_r.y + slot_px * 0.25f,
                          out_r.x - fuel_r.x - cell - 12 * ui->scale,
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
    Rectangle flame_r = { arrow_r.x, arrow_r.y + arrow_r.height + 8 * ui->scale,
                          arrow_r.width, 8.0f * ui->scale };
    DrawRectangleRec(flame_r, (Color){ 24, 26, 30, 255 });
    if (f->burn_total > 0.0f && f->burn_left > 0.0f) {
        Rectangle burn = flame_r;
        burn.width *= f->burn_left / f->burn_total;
        DrawRectangleRec(burn, (Color){ 240, 130, 40, 255 });
    }
    DrawRectangleLinesEx(flame_r, 1.5f, (Color){ 90, 92, 96, 255 });

    /* --- slots --- */
    struct { Rectangle r; WlSlot *slot; bool output; } slots[3] = {
        { in_r, &f->input, false },
        { fuel_r, &f->fuel, false },
        { out_r, &f->output, true },
    };

    for (int i = 0; i < 3; i++) {
        wl_ui_draw_slot_item(ui, slots[i].r, slots[i].slot);
        bool hover = CheckCollisionPointRec(ui->mouse, slots[i].r);
        if (hover) {
            DrawRectangleLinesEx((Rectangle){ slots[i].r.x - 1, slots[i].r.y - 1,
                                             slots[i].r.width + 2,
                                             slots[i].r.height + 2 },
                                 2.0f, (Color){ 240, 236, 190, 220 });
        }
        if (hover && ui->mouse_pressed) {
            WlSlot *s = slots[i].slot;
            WlSlot *held = &g->ui_held;
            if (slots[i].output) {
                /* take the result only */
                if (s->item != WL_ITEM_NONE && held->item == WL_ITEM_NONE) {
                    *held = *s;
                    s->item = WL_ITEM_NONE;
                    s->count = 0;
                    s->dur = 0;
                    wl_audio_play(&g->audio, WL_SND_CLICK);
                }
            } else if (held->item == WL_ITEM_NONE) {
                if (s->item != WL_ITEM_NONE) {
                    *held = *s;
                    s->item = WL_ITEM_NONE;
                    s->count = 0;
                    s->dur = 0;
                    wl_audio_play(&g->audio, WL_SND_CLICK);
                }
            } else if (s->item == WL_ITEM_NONE) {
                *s = *held;
                held->item = WL_ITEM_NONE;
                held->count = 0;
                held->dur = 0;
                wl_audio_play(&g->audio, WL_SND_CLICK);
            } else if (s->item == held->item) {
                const WlItem *def = wl_item_get(s->item);
                int max = def && def->max_stack > 0 ? def->max_stack : 64;
                int move = held->count;
                if (move > max - s->count) move = max - s->count;
                if (move > 0) {
                    s->count = (uint8_t)(s->count + move);
                    held->count = (uint8_t)(held->count - move);
                    if (held->count == 0) {
                        held->item = WL_ITEM_NONE;
                        held->dur = 0;
                    }
                    wl_audio_play(&g->audio, WL_SND_CLICK);
                }
            }
        }
    }

    /* tooltip on hover */
    for (int i = 0; i < 3; i++) {
        if (CheckCollisionPointRec(ui->mouse, slots[i].r) &&
            slots[i].slot->item != WL_ITEM_NONE && !ui->mouse_down) {
            const WlItem *item = wl_item_get(slots[i].slot->item);
            if (item) wl_ui_tooltip(ui, item->display);
        }
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
