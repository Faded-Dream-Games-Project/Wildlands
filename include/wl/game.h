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
    WL_OVERLAY_FURNACE,  /* furnace input/fuel/output screen */
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
    bool last_save_failed;    /* most recent session save reported failure */
    char gpu_info[160];       /* vendor / renderer / GL version (debug) */

    /* death handling: the inventory is dropped exactly once per death;
     * the flag resets on respawn (partial drop failures retry, so items
     * are never silently lost) */
    bool death_dropped;

    /* camera stability: raylib warps the cursor to the window center in
     * EnableCursor()/DisableCursor(); the first mouse deltas after such a
     * warp are the warp itself and must not rotate the camera */
    int look_guard;
    bool cursor_hidden_last;

    /* HUD */
    float item_toast;       /* seconds remaining to show the held item name */
    char item_toast_name[64];

    /* inventory UI: stack held by the mouse */
    WlSlot ui_held;

    /* settings scratch */
    WlConfig cfg_entry;     /* snapshot for Cancel */
    int settings_tab;
    float settings_scroll;  /* settings list scroll position (px) */
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
        float create_scroll;    /* create screen scroll position (px) */
    } menus;

    /* demo mode (--frames): exits after N frames */
    int demo_frames;
    char demo_screenshot[WL_PATH_MAX];
    char demo_world[WL_NAME_MAX];
    uint64_t demo_seed;
    int demo_gamemode;
    int demo_type;
    int demo_width, demo_height;
    /* scripted camera motion (--lookspin/--looknod): synthetic mouse-look
     * deltas each frame; used to reproduce movement-triggered rendering
     * artifacts in automated runs */
    float demo_lookspin;      /* yaw degrees per frame */
    float demo_looknod;       /* pitch oscillation amplitude, degrees */
    bool demo_walk;           /* hold Walk Forward (--walk) */
    /* test automation: run menus without a world, open an inventory overlay */
    bool demo_menu;
    char demo_start_menu[16];   /* "" | world select | create | settings */
    int demo_overlay;           /* 0 none, 2 inventory, 3 crafting table */
    int demo_time;              /* >= 0: jump world time at entry (--time) */
    /* demo camera direction (--yaw/--pitch degrees, applied at world entry) */
    bool demo_dir_set;
    float demo_yaw, demo_pitch;

    /* runtime self test (--selftest / make selftest) */
    bool selftest;
    bool selftest_failed;

    /* UI runtime test (--uitest settings|create): scripted pointer driver
     * over the real menus, with logged assertions */
    int uitest;                 /* 0 none, 1 settings, 2 create, 3 pause */
    int uitest_phase;
    int uitest_wait;
    bool uitest_failed;
    WlConfig uitest_cfg;        /* snapshot, restored when the test ends */
    char uitest_shot[WL_PATH_MAX]; /* screenshot taken mid-frame when set */

    /* test seam: one synthetic key press, consumed by wl_game_key_pressed
     * (lets scripted tests press ESC etc. through the real handlers) */
    int inject_key;

    /* startup diagnostics */
    double init_started_ms;     /* CLOCK_MONOTONIC at init begin */
    double init_done_ms;        /* total init time */
    double first_frame_ms;      /* init begin -> first presented frame */
    bool first_frame_done;
    bool audio_pending;         /* audio device init deferred to frame 1 */

    /* frame-time statistics (debug overlay + perf logs) */
    float ft_ema_ms;            /* exponential average frame time */
    float ft_worst_ms;          /* worst frame time in the last ~128 */
    float ft_hist[128];
    int ft_hist_count, ft_hist_next;

    /* cached main-menu background (rendered once per window size) */
    RenderTexture menu_bg;
    int menu_bg_w, menu_bg_h;
} WlGame;

/* Boot: paths, logging, config, window, registries, assets, audio. */
bool wl_game_init(WlGame *g, int argc, char **argv);
void wl_game_shutdown(WlGame *g);

/* Runs one frame (dispatches by state). */
void wl_game_run_frame(WlGame *g);

/* World session lifecycle. */
bool wl_game_enter_world(WlGame *g, const WlWorldEntry *entry);
/* Saves the session, then unloads the world and returns to the title.
 * Returns false (and KEEPS the world session) when saving failed, so
 * progress is never silently discarded. */
bool wl_game_exit_world(WlGame *g);
/* Saves the session; returns false when the save failed. */
bool wl_game_save_session(WlGame *g);
/* Save-then-quit via the normal shutdown path; returns false (and keeps
 * the game open) when the save failed. */
bool wl_game_quit_to_desktop(WlGame *g);
/* Drops the player's inventory (slots, crafting grid, cursor stack) into
 * the world as collectible item entities. Returns true when everything
 * was dropped; partially dropped stacks are retried on the next call so
 * nothing is silently lost or duplicated. */
bool wl_game_drop_inventory(WlGame *g);

/* Applies window/audio/world-affecting settings right now. */
void wl_game_apply_settings(WlGame *g);

/* Saves config + keybindings to disk. */
void wl_game_save_config(WlGame *g);

/* True when this key was physically pressed this frame or injected by a
 * test (--uitest). Consumes a pending injection. */
bool wl_game_key_pressed(WlGame *g, int key);

#endif /* WL_GAME_H */
