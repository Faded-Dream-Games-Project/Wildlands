/* inventory.h — player inventory and the pocket crafting grid. */
#ifndef WL_INVENTORY_H
#define WL_INVENTORY_H

#include "wl/wl_common.h"
#include "wl/item.h"

#define WL_INV_SIZE 36      /* 0..8 = hotbar, 9..35 = main */
#define WL_HOTBAR_SIZE 9
#define WL_CRAFT_GRID_MAX 9

typedef struct {
    uint16_t item;  /* WL_ITEM_NONE when empty */
    uint8_t count;
    uint16_t dur;   /* remaining durability for tools */
} WlSlot;

typedef struct {
    WlSlot slots[WL_INV_SIZE];
    WlSlot craft[WL_CRAFT_GRID_MAX]; /* active grid: 2x2 or 3x3 */
    int craft_w;                    /* 0 = closed, 2 or 3 */
} WlInventory;

void wl_inv_init(WlInventory *inv);

/* Adds items (stacking first, hotbar first). Returns true when everything
 * fit; on false some may have been added. */
bool wl_inv_add(WlInventory *inv, WlItemId item, uint8_t count, uint16_t dur);
/* Same, but returns how many actually fit (0 = nothing added) — used by
 * drop pickup, which must keep the remainder in the world. */
int  wl_inv_add_partial(WlInventory *inv, WlItemId item, uint8_t count, uint16_t dur);
int  wl_inv_count(const WlInventory *inv, WlItemId item);
/* Removes `count` of item if available; returns false (nothing removed) if
 * not enough. */
bool wl_inv_take(WlInventory *inv, WlItemId item, uint8_t count);
/* Decrements the slot (used after placing a block). */
void wl_inv_consume_slot(WlInventory *inv, int slot);

/* Crafting grid lifecycle. Closing returns grid contents to the inventory;
 * anything that does not fit stays in the grid (nothing is lost). */
void wl_inv_open_craft(WlInventory *inv, int w);
void wl_inv_close_craft(WlInventory *inv);
WlSlot *wl_inv_craft_slot(WlInventory *inv, int x, int y);

#endif /* WL_INVENTORY_H */
