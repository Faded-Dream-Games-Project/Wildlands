/* menus.h — one draw+handle function per menu screen. */
#ifndef WL_MENUS_H
#define WL_MENUS_H

struct WlGame;

void wl_menu_main(struct WlGame *g);
void wl_menu_world_select(struct WlGame *g);
void wl_menu_world_create(struct WlGame *g);
void wl_menu_settings(struct WlGame *g);
void wl_menu_pause(struct WlGame *g);
void wl_menu_mods(struct WlGame *g);

#endif /* WL_MENUS_H */
