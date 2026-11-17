/* inventory.c — slots, stacking, pocket crafting grid. */
#include "wl/inventory.h"
#include "wl/log.h"

#include <string.h>

void wl_inv_init(WlInventory *inv)
{
    memset(inv, 0, sizeof(*inv));
    for (int i = 0; i < WL_INV_SIZE; i++)
        inv->slots[i].item = WL_ITEM_NONE;
    for (int i = 0; i < WL_CRAFT_GRID_MAX; i++)
        inv->craft[i].item = WL_ITEM_NONE;
    inv->craft_w = 0;
}

int wl_inv_add_partial(WlInventory *inv, WlItemId item, uint8_t count, uint16_t dur)
{
    const WlItem *def = wl_item_get(item);
    if (!def || count == 0) return 0;

    int remaining = count;
    int max = def->max_stack > 0 ? def->max_stack : 64;

    if (max > 1) {
        for (int pass = 0; pass < 2 && remaining > 0; pass++) {
            for (int i = 0; i < WL_INV_SIZE && remaining > 0; i++) {
                WlSlot *s = &inv->slots[i];
                if (pass == 0) {
                    if (s->item != item || s->count >= max) continue;
                    int room = max - s->count;
                    int add = room < remaining ? room : remaining;
                    s->count += (uint8_t)add;
                    remaining -= add;
                } else {
                    if (s->item != WL_ITEM_NONE) continue;
                    int add = max < remaining ? max : remaining;
                    s->item = item;
                    s->count = (uint8_t)add;
                    s->dur = dur;
                    remaining -= add;
                }
            }
        }
    } else {
        for (int i = 0; i < WL_INV_SIZE && remaining > 0; i++) {
            if (inv->slots[i].item != WL_ITEM_NONE) continue;
            inv->slots[i].item = item;
            inv->slots[i].count = 1;
            inv->slots[i].dur = dur;
            remaining--;
        }
    }
    return count - remaining;
}

bool wl_inv_add(WlInventory *inv, WlItemId item, uint8_t count, uint16_t dur)
{
    return wl_inv_add_partial(inv, item, count, dur) == (int)count;
}

int wl_inv_count(const WlInventory *inv, WlItemId item)
{
    int n = 0;
    for (int i = 0; i < WL_INV_SIZE; i++)
        if (inv->slots[i].item == item)
            n += inv->slots[i].count;
    return n;
}

bool wl_inv_take(WlInventory *inv, WlItemId item, uint8_t count)
{
    if (wl_inv_count(inv, item) < (int)count) return false;
    int remaining = count;
    for (int i = 0; i < WL_INV_SIZE && remaining > 0; i++) {
        WlSlot *s = &inv->slots[i];
        if (s->item != item) continue;
        int take = s->count < remaining ? s->count : remaining;
        s->count = (uint8_t)(s->count - take);
        remaining -= take;
        if (s->count == 0) {
            s->item = WL_ITEM_NONE;
            s->dur = 0;
        }
    }
    return true;
}

void wl_inv_consume_slot(WlInventory *inv, int slot)
{
    if (slot < 0 || slot >= WL_INV_SIZE) return;
    WlSlot *s = &inv->slots[slot];
    if (s->item == WL_ITEM_NONE) return;
    s->count--;
    if (s->count == 0) {
        s->item = WL_ITEM_NONE;
        s->dur = 0;
    }
}

void wl_inv_open_craft(WlInventory *inv, int w)
{
    if (w != 2 && w != 3) return;
    inv->craft_w = w; /* leftover grid contents from before are preserved */
}

void wl_inv_close_craft(WlInventory *inv)
{
    for (int i = 0; i < WL_CRAFT_GRID_MAX; i++) {
        WlSlot *s = &inv->craft[i];
        if (s->item == WL_ITEM_NONE) continue;
        if (wl_inv_add(inv, s->item, s->count, s->dur)) {
            s->item = WL_ITEM_NONE;
            s->count = 0;
            s->dur = 0;
        }
        /* else: keep in the grid for next time; nothing is lost */
    }
    inv->craft_w = 0;
}

WlSlot *wl_inv_craft_slot(WlInventory *inv, int x, int y)
{
    if (inv->craft_w == 0 || x < 0 || y < 0 || x >= inv->craft_w || y >= inv->craft_w)
        return NULL;
    return &inv->craft[y * inv->craft_w + x];
}
