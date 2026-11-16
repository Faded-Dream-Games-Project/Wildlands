/* inv_ui.h — inventory + crafting overlay. */
#ifndef WL_INV_UI_H
#define WL_INV_UI_H

struct WlGame;

/* Draws and handles the inventory/crafting overlay for the current
 * g->overlay (WL_OVERLAY_INVENTORY = 2x2, WL_OVERLAY_CRAFTING = 3x3).
 * Call between BeginDrawing and EndDrawing after the HUD. */
void wl_inv_ui_draw(struct WlGame *g);

/* Closes any open overlay, returning grid + held items to the inventory. */
void wl_inv_ui_close(struct WlGame *g);

#endif /* WL_INV_UI_H */
