/* ui.h — immediate-mode widget toolkit shared by all menus and the HUD. */
#ifndef WL_UI_H
#define WL_UI_H

#include "wl/wl_common.h"
#include "wl/assets.h"
#include "wl/inventory.h"
#include "raylib.h"

struct WlConfig;

#define WL_UI_TEXTFIELD_CHARS 96

typedef struct WlUi {
    WlAssets *assets;
    float scale;
    Vector2 mouse;
    bool mouse_down, mouse_pressed, mouse_released;
    bool mouse_right_pressed;
    int wheel;
    int hot, active;
    int next_id;
    char chars[16];
    int char_count;
    int focus_textfield; /* widget id of focused text field */
    bool any_hover;      /* any widget hovered this frame (block game input) */
} WlUi;

/* Per-frame begin/end; reads mouse + keyboard state. */
void wl_ui_begin(WlUi *ui, WlAssets *assets, const struct WlConfig *cfg);
void wl_ui_end(WlUi *ui);

Font wl_ui_font(const WlUi *ui);
Font wl_ui_title_font(const WlUi *ui);
/* Rectangle scaled by the UI scale factor. */
Rectangle wl_ui_rect(const WlUi *ui, float x, float y, float w, float h);
Rectangle wl_ui_centered(const WlUi *ui, float w, float h, float y_frac);
Vector2 wl_ui_text_size(const WlUi *ui, const char *text, float size, bool title);

/* --- widgets (true = activated this frame) --- */
bool wl_ui_button(WlUi *ui, Rectangle r, const char *label);
bool wl_ui_slider(WlUi *ui, Rectangle r, float *value, float vmin, float vmax,
                  float step);
/* integer variant for config fields stored as int */
bool wl_ui_slider_i(WlUi *ui, Rectangle r, int *value, int vmin, int vmax, int step);
bool wl_ui_toggle(WlUi *ui, Rectangle r, bool *value);
/* dropdown: returns true when a (possibly new) option was chosen. */
bool wl_ui_dropdown(WlUi *ui, Rectangle r, int *index, const char **options,
                    int count);
/* id must be unique and stable per field (caller-assigned). */
bool wl_ui_textfield(WlUi *ui, int id, Rectangle r, char *buf, size_t cap);

void wl_ui_label(WlUi *ui, Rectangle r, const char *text, Color color, bool title);
void wl_ui_label_centered(WlUi *ui, Rectangle r, const char *text, Color color,
                          bool title);
void wl_ui_panel(WlUi *ui, Rectangle r);
void wl_ui_dim(float alpha);
/* returns the newly active tab index (true when changed) */
bool wl_ui_tabbar(WlUi *ui, Rectangle r, const char **tabs, int count, int *active);
void wl_ui_tooltip(WlUi *ui, const char *text);

/* --- item rendering --- */
/* Isometric mini-cube for blocks, flat sprite for items/cross blocks. */
void wl_ui_draw_item_icon(WlAssets *assets, WlItemId item, Rectangle r);
/* Slot background + item + count + durability bar. */
void wl_ui_draw_slot_item(WlUi *ui, Rectangle r, const WlSlot *slot);

#endif /* WL_UI_H */
