/* furnace_ui.h — the furnace overlay screen. */
#ifndef WL_FURNACE_UI_H
#define WL_FURNACE_UI_H

struct WlGame;

/* Opens the furnace screen bound to the block at (x,y,z). */
void wl_furnace_ui_open(struct WlGame *g, int x, int y, int z);
/* Draws the overlay when WL_OVERLAY_FURNACE is active. */
void wl_furnace_ui_draw(struct WlGame *g);
/* Closes the overlay, returning the cursor stack to the inventory.
 * The caller re-locks the gameplay cursor afterwards. */
void wl_furnace_ui_close(struct WlGame *g);

#endif /* WL_FURNACE_UI_H */
