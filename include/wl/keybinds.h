/* keybinds.h — rebindable keyboard actions, persisted to keybindings.txt. */
#ifndef WL_KEYBINDS_H
#define WL_KEYBINDS_H

#include "wl/wl_common.h"

typedef enum {
    WL_ACT_FORWARD = 0,
    WL_ACT_BACK,
    WL_ACT_LEFT,
    WL_ACT_RIGHT,
    WL_ACT_JUMP,
    WL_ACT_SNEAK,
    WL_ACT_SPRINT,
    WL_ACT_INVENTORY,
    WL_ACT_DROP,
    WL_ACT_DEBUG,
    WL_ACT_SCREENSHOT,
    WL_ACT_PAUSE,
    WL_ACT_COUNT
} WlAction;

typedef struct WlKeybinds {
    int key[WL_ACT_COUNT]; /* raylib KeyboardKey codes */
} WlKeybinds;

void        wl_keybinds_defaults(WlKeybinds *kb);
/* Missing file => defaults (returns true, fresh install). Unknown actions or
 * key names are logged and skipped. */
bool        wl_keybinds_load(WlKeybinds *kb, const char *path);
bool        wl_keybinds_save(const WlKeybinds *kb, const char *path);

const char *wl_action_label(WlAction a);  /* "Walk Forward" */
const char *wl_action_id(WlAction a);     /* "forward" */
int         wl_action_from_id(const char *id); /* WL_ACT_COUNT when unknown */

const char *wl_key_name(int key);         /* "" when unknown */
int         wl_key_from_name(const char *name); /* -1 when unknown */
bool        wl_key_is_bindable(int key);

/* Is the action's key currently held? (physical state or a debug hold) */
bool        wl_keybinds_down(const WlKeybinds *kb, WlAction action);
/* Test seam: force an action's held state (used by --walk automation). */
void        wl_keybinds_debug_hold(WlAction action, bool held);

#endif /* WL_KEYBINDS_H */
