/* game.h — the application context and top-level state machine. */
#ifndef WL_GAME_H
#define WL_GAME_H

#include "wl/wl_common.h"
#include "wl/config.h"
#include "wl/keybinds.h"
#include "wl/assets.h"
#include "wl/audio.h"
#include "wl/world.h"
#include "wl/worlds.h"
#include "wl/player.h"
#include "wl/interact.h"
#include "wl/renderer.h"
#include "wl/ui.h"
#include "wl/mods.h"

typedef enum {
    WL_STATE_MAIN_MENU = 0,
    WL_STATE_WORLD_SELECT,
    WL_STATE_WORLD_CREATE,
    WL_STATE_SETTINGS,
    WL_STATE_MODS,
    WL_STATE_LOADING,
    WL_STATE_PLAYING,
    WL_STATE_PAUSED,
} WlState;

typedef enum {
    WL_OVERLAY_NONE = 0,
    WL_OVERLAY_INVENTORY, /* 2x2 pocket crafting */
    WL_OVERLAY_CRAFTING,  /* 3x3 crafting table  */
    WL_OVERLAY_DEATH,
} WlOverlay;

#define WL_MENUS_MAX_WORLDS 64

typedef struct WlGame {
    /* systems */
    WlConfig cfg;
    WlKeybinds keys;
    WlAssets *assets;
    WlAudio audio;
    WlRenderer rend;
    WlUi ui;
    WlMods mods;

    /* state machine */
    WlState state;
    WlState settings_return;
    WlOverlay overlay;
    bool quit;

    /* world session */
    WlWorld *world;
    WlPlayer player;
    Vector3 spawn;
    WlWorldEntry entry;
    double session_start;
    double autosave_timer;
    int load_initial_pending;

    /* HUD */
    float item_toast;       /* seconds remaining to show the held item name */
    char item_toast_name[64];

    /* inventory UI: stack held by the mouse */
    WlSlot ui_held;

    /* settings scratch */
    WlConfig cfg_entry;     /* snapshot for Cancel */
    int settings_tab;
    int rebinding_action;   /* -1 none, otherwise WlAction */
    int res_index;          /* resolution dropdown index */

    /* world menus */
    struct {
        WlWorldEntry worlds[WL_MENUS_MAX_WORLDS];
        int world_count;
        int selected;
        bool confirm_delete;
        char new_name[WL_NAME_MAX];
        char new_seed[32];
        int new_gamemode;
        int new_type;
    } menus;

    /* demo mode (--frames): exits after N frames */
    int demo_frames;
    char demo_screenshot[WL_PATH_MAX];
    char demo_world[WL_NAME_MAX];
    uint64_t demo_seed;
    int demo_gamemode;
    int demo_type;
    int demo_width, demo_height;
    /* test automation: run menus without a world, open an inventory overlay */
    bool demo_menu;
    char demo_start_menu[16];   /* "" | world select | create | settings */
    int demo_overlay;           /* 0 none, 2 inventory, 3 crafting table */
} WlGame;

/* Boot: paths, logging, config, window, registries, assets, audio. */
bool wl_game_init(WlGame *g, int argc, char **argv);
void wl_game_shutdown(WlGame *g);

/* Runs one frame (dispatches by state). */
void wl_game_run_frame(WlGame *g);

/* World session lifecycle. */
bool wl_game_enter_world(WlGame *g, const WlWorldEntry *entry);
void wl_game_exit_world(WlGame *g);
void wl_game_save_session(WlGame *g);

/* Applies window/audio/world-affecting settings right now. */
void wl_game_apply_settings(WlGame *g);

/* Saves config + keybindings to disk. */
void wl_game_save_config(WlGame *g);

#endif /* WL_GAME_H */
