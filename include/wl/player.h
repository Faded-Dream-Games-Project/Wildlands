/* player.h — first-person player: physics, survival stats, look control. */
#ifndef WL_PLAYER_H
#define WL_PLAYER_H

#include "wl/wl_common.h"
#include "wl/inventory.h"
#include "raylib.h"

struct WlWorld;
struct WlKeybinds;
struct WlConfig;
struct WlAudio;

typedef struct WlPlayer {
    Vector3 pos;        /* feet center */
    float yaw, pitch;   /* radians; yaw 0 looks toward -Z */
    Vector3 vel;

    bool on_ground;
    bool in_water, head_in_water;
    bool sprinting, sneaking, flying;
    bool was_sneak_forced; /* ceiling too low to stand up */

    float eye_height;    /* eased current */
    float bob_phase;
    float bob_amount;

    /* survival */
    float health, hunger;
    float exhaustion;
    float air;               /* seconds of breath */
    float regen_timer, starve_timer, drown_timer;
    float fall_start_y;      /* -1 = not falling */
    double damage_flash;
    double eat_timer;        /* >0 while eating */
    bool dead;

    /* interaction */
    double break_progress;
    int break_x, break_y, break_z;
    uint8_t break_id;
    double break_cooldown, place_cooldown;
    double last_jump_tap;    /* double-tap space = fly toggle (creative) */

    /* look input smoothing accumulators */
    float pend_x, pend_y;
    float step_acc;          /* distance walked since last footstep */

    WlInventory inv;
    int selected_slot;
} WlPlayer;

void wl_player_init(WlPlayer *p, Vector3 spawn);

/* input_active=false while a menu is open (no movement, still physics). */
void wl_player_update(WlPlayer *p, struct WlWorld *w, const struct WlKeybinds *kb,
                      const struct WlConfig *cfg, struct WlAudio *audio,
                      float dt, bool input_active, int gamemode);

/* Raw mouse deltas; sensitivity/invert/smoothing applied by the player. */
void wl_player_apply_look(WlPlayer *p, float dx, float dy);

void wl_player_damage(WlPlayer *p, float amount, struct WlAudio *audio, int difficulty);
void wl_player_respawn(WlPlayer *p, Vector3 spawn);

Vector3 wl_player_eye(const WlPlayer *p);
Vector3 wl_player_look_dir(const WlPlayer *p);
/* AABB extents for collisions: half-width and current height. */
void wl_player_extents(const WlPlayer *p, float *half_w, float *height);

#endif /* WL_PLAYER_H */
