/* crafting.h — recipe registry and grid matching. */
#ifndef WL_CRAFTING_H
#define WL_CRAFTING_H

#include "wl/wl_common.h"
#include "wl/inventory.h"
#include "wl/item.h"

#define WL_RECIPES_MAX 128
#define WL_ING_GROUP 0x8000u /* flag: ingredient is a group id */

/* Shaped recipes may be placed anywhere in the grid and are matched
 * mirrored. 'pattern' is w*h ingredient codes, 0 = must stay empty.
 * Shapeless recipes match any arrangement of 'ingredients'. */
typedef struct {
    uint16_t out_item;
    uint8_t out_count;
    bool shapeless;
    int w, h;
    uint16_t pattern[9];
    int n_ing;
    uint16_t ingredients[9];
} WlRecipe;

/* Registers the base recipes; call once after wl_items_init. */
void wl_crafting_init(void);
/* Mods may add recipes; returns the recipe index or -1. */
int  wl_crafting_register(const WlRecipe *r);

/* Finds the recipe matching the w*w grid (2 or 3). NULL when none. */
const WlRecipe *wl_crafting_match(const WlSlot *grid, int w);
const WlRecipe *wl_crafting_get(int index);
int  wl_crafting_count(void);

#endif /* WL_CRAFTING_H */
