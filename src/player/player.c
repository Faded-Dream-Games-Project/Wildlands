/* player.c — movement, swept-AABB collision, survival simulation. */
#include "wl/player.h"
#include "wl/world.h"
#include "wl/block.h"
#include "wl/keybinds.h"
#include "wl/config.h"
#include "wl/audio.h"
#include "wl/util.h"
#include "wl/log.h"

#include <math.h>
#include <string.h>

#define WL_P_HALF_W      0.30f
#define WL_P_H_STAND     1.80f
#define WL_P_H_SNEAK     1.50f
#define WL_P_EYE_STAND   1.62f
#define WL_P_EYE_SNEAK   1.27f
#define WL_P_GRAVITY     28.0f
#define WL_P_TERMINAL    60.0f
#define WL_P_JUMP        8.6f
#define WL_P_WALK        4.3f
#define WL_P_SPRINT      5.6f
#define WL_P_SNEAK_SPD   1.4f
#define WL_P_SWIM        2.6f
#define WL_P_FLY         10.5f
#define WL_P_FLY_SPRINT  20.0f
#define WL_P_EPS         0.001f

void wl_player_init(WlPlayer *p, Vector3 spawn)
{
    memset(p, 0, sizeof(*p));
    p->pos = spawn;
    p->yaw = 0.0f;
    p->pitch = 0.0f;
    p->eye_height = WL_P_EYE_STAND;
    p->health = 20.0f;
    p->hunger = 20.0f;
    p->air = 10.0f;
    p->fall_start_y = -1.0f;
    wl_inv_init(&p->inv);
}

void wl_player_extents(const WlPlayer *p, float *half_w, float *height)
{
    *half_w = WL_P_HALF_W;
    *height = p->sneaking ? WL_P_H_SNEAK : WL_P_H_STAND;
}

Vector3 wl_player_eye(const WlPlayer *p)
{
    return (Vector3){ p->pos.x, p->pos.y + p->eye_height, p->pos.z };
}

Vector3 wl_player_look_dir(const WlPlayer *p)
{
    float cp = cosf(p->pitch);
    return (Vector3){ -sinf(p->yaw) * cp, sinf(p->pitch), -cosf(p->yaw) * cp };
}

int wl_player_armor_points(const WlPlayer *p)
{
    if (!p) return 0;
    int pts = 0;
    for (int i = 0; i < 4; i++) {
        const WlItem *def = wl_item_get(p->armor[i].item);
        if (def && def->armor_slot == i + 1) pts += def->armor_points;
    }
    return pts;
}

void wl_player_cancel_actions(WlPlayer *p)
{
    if (!p) return;
    p->break_progress = 0.0;
    p->break_id = 0; /* invalidate the target so mining restarts cleanly */
    p->action_swing = 0.0f;
    p->eat_timer = 0.0f;
    p->break_cooldown = 0.0f;
}

void wl_player_apply_look(WlPlayer *p, float dx, float dy)
{
    p->pend_x += dx;
    p->pend_y += dy;
}

static void apply_pending_look(WlPlayer *p, const WlConfig *cfg)
{
    float use = cfg->mouse_smoothing ? 0.35f : 1.0f;
    float ax = p->pend_x * use;
    float ay = p->pend_y * use;
    p->pend_x -= ax;
    p->pend_y -= ay;

    float sens = 0.0022f * cfg->mouse_sensitivity;
    p->yaw -= ax * sens;
    p->pitch -= ay * sens * (cfg->invert_mouse ? -1.0f : 1.0f);

    const float lim = 1.5533f; /* ~89deg */
    if (p->pitch > lim) p->pitch = lim;
    if (p->pitch < -lim) p->pitch = -lim;
    /* keep yaw in a sane range */
    if (p->yaw > PI) p->yaw -= 2.0f * PI;
    if (p->yaw < -PI) p->yaw += 2.0f * PI;
}

/* ------------------------------------------------------------------ */
/* collision                                                           */
/* ------------------------------------------------------------------ */

static bool player_collides(WlWorld *w, const WlPlayer *p, int *out_x, int *out_y, int *out_z)
{
    float hw, h;
    wl_player_extents(p, &hw, &h);
    int x0 = (int)floorf(p->pos.x - hw), x1 = (int)floorf(p->pos.x + hw);
    int y0 = (int)floorf(p->pos.y), y1 = (int)floorf(p->pos.y + h - WL_P_EPS);
    int z0 = (int)floorf(p->pos.z - hw), z1 = (int)floorf(p->pos.z + hw);

    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
                if (wl_world_solid_at(w, x, y, z)) {
                    if (out_x) *out_x = x;
                    if (out_y) *out_y = y;
                    if (out_z) *out_z = z;
                    return true;
                }
    return false;
}

static bool has_ground_below(WlWorld *w, const WlPlayer *p)
{
    float hw, h;
    wl_player_extents(p, &hw, &h);
    int x0 = (int)floorf(p->pos.x - hw), x1 = (int)floorf(p->pos.x + hw);
    int z0 = (int)floorf(p->pos.z - hw), z1 = (int)floorf(p->pos.z + hw);
    int y = (int)floorf(p->pos.y - 0.06f);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            if (wl_world_solid_at(w, x, y, z)) return true;
    return false;
}

/* Moves one axis in sub-steps, snapping to block faces on collision. */
static bool move_axis(WlWorld *w, WlPlayer *p, int axis, float delta)
{
    if (delta == 0.0f) return false;
    float hw, h;
    wl_player_extents(p, &hw, &h);

    int steps = (int)(fabsf(delta) / 0.2f) + 1;
    float step = delta / (float)steps;
    bool hit = false;

    for (int i = 0; i < steps; i++) {
        if (axis == 0) p->pos.x += step;
        else if (axis == 1) p->pos.y += step;
        else p->pos.z += step;

        int bx, by, bz;
        if (player_collides(w, p, &bx, &by, &bz)) {
            if (axis == 0)
                p->pos.x = step > 0 ? (float)bx - hw - WL_P_EPS
                                    : (float)bx + 1.0f + hw + WL_P_EPS;
            else if (axis == 1)
                p->pos.y = step > 0 ? (float)by - h - WL_P_EPS
                                    : (float)by + 1.0f + WL_P_EPS;
            else
                p->pos.z = step > 0 ? (float)bz - hw - WL_P_EPS
                                    : (float)bz + 1.0f + hw + WL_P_EPS;
            hit = true;
            break;
        }
    }
    return hit;
}

/* ------------------------------------------------------------------ */
/* survival                                                            */
/* ------------------------------------------------------------------ */

void wl_player_damage(WlPlayer *p, float amount, struct WlAudio *audio, int difficulty)
{
    if (p->dead || amount <= 0.0f) return;
    if (difficulty == 0) return; /* peaceful: no damage */

    /* armor: classic point formula (4% per point, capped at 80%); every
     * worn piece loses one durability per hit and shatters at zero */
    {
        int pts = wl_player_armor_points(p);
        if (pts > 0) {
            float red = 0.04f * (float)pts;
            if (red > 0.8f) red = 0.8f;
            amount *= 1.0f - red;
            for (int i = 0; i < 4; i++) {
                WlSlot *s = &p->armor[i];
                const WlItem *def = wl_item_get(s->item);
                if (!def || def->armor_slot != i + 1) continue;
                if (s->dur > 0) s->dur--;
                if (s->dur == 0 && def->durability > 0) {
                    WL_LOGD("armor piece broke: %s", def->name);
                    s->item = WL_ITEM_NONE;
                    s->count = 0;
                    s->dur = 0;
                }
            }
        }
    }

    p->health -= amount;
    p->damage_flash = 0.45;
    wl_audio_play(audio, WL_SND_HURT);
    if (p->health <= 0.0f) {
        p->health = 0.0f;
        p->dead = true;
        wl_audio_play(audio, WL_SND_DEATH);
        WL_LOGI("player died");
    }
}

static WlSndId step_sound_for(uint8_t block)
{
    switch (block) {
    case WL_BLOCK_STONE: case WL_BLOCK_COBBLE: case WL_BLOCK_SANDSTONE:
    case WL_BLOCK_COAL_ORE: case WL_BLOCK_IRON_ORE: case WL_BLOCK_GOLD_ORE:
    case WL_BLOCK_DIAMOND_ORE: case WL_BLOCK_BEDROCK: case WL_BLOCK_FURNACE:
    case WL_BLOCK_FURNACE_LIT:
        return WL_SND_STEP_STONE;
    case WL_BLOCK_DIRT: case WL_BLOCK_GRAVEL:
        return WL_SND_STEP_DIRT;
    case WL_BLOCK_PLANKS: case WL_BLOCK_LOG: case WL_BLOCK_BIRCH_LOG:
    case WL_BLOCK_CRAFTING_TABLE:
        return WL_SND_STEP_WOOD;
    case WL_BLOCK_SAND:
        return WL_SND_STEP_SAND;
    case WL_BLOCK_SNOW: case WL_BLOCK_SNOW_GRASS:
        return WL_SND_STEP_SNOW;
    case WL_BLOCK_ICE:
        return WL_SND_STEP_STONE;
    default:
        return WL_SND_STEP_GRASS;
    }
}

static void survival_update(WlPlayer *p, const WlConfig *cfg, struct WlAudio *audio,
                            float dt, bool moving, int gamemode)
{
    if (gamemode != WL_WORLD_GAMEMODE_SURVIVAL) {
        p->hunger = 20.0f;
        return;
    }

    /* exhaustion -> hunger */
    p->exhaustion += (p->sprinting ? 0.55f : (moving ? 0.06f : 0.012f)) * dt;
    if (p->exhaustion >= 4.0f) {
        p->exhaustion -= 4.0f;
        if (p->hunger > 0.0f) p->hunger -= 1.0f;
    }

    /* drowning */
    if (p->head_in_water) {
        p->air -= dt;
        if (p->air <= 0.0f) {
            p->drown_timer += dt;
            if (p->drown_timer >= 1.0f) {
                p->drown_timer = 0.0f;
                wl_player_damage(p, 2.0f, audio, cfg->difficulty);
            }
        }
    } else {
        p->air = 10.0f;
        p->drown_timer = 0.0f;
    }

    /* regeneration / starvation */
    if (cfg->difficulty == 0) {
        p->hunger = 20.0f;
        p->regen_timer += dt;
        if (p->regen_timer >= 2.0f && p->health < 20.0f) {
            p->regen_timer = 0.0f;
            p->health = p->health + 1.0f > 20.0f ? 20.0f : p->health + 1.0f;
        }
        return;
    }

    if (p->hunger >= 18.0f && p->health < 20.0f) {
        p->regen_timer += dt;
        if (p->regen_timer >= 4.0f) {
            p->regen_timer = 0.0f;
            p->health = p->health + 1.0f > 20.0f ? 20.0f : p->health + 1.0f;
            p->exhaustion += 1.5f;
        }
    } else {
        p->regen_timer = 0.0f;
    }

    if (p->hunger <= 0.0f) {
        p->starve_timer += dt;
        if (p->starve_timer >= 4.0f) {
            p->starve_timer = 0.0f;
            bool easy_floor = cfg->difficulty == 1 && p->health <= 1.0f;
            if (!easy_floor) wl_player_damage(p, 1.0f, audio, cfg->difficulty);
        }
    } else {
        p->starve_timer = 0.0f;
    }
}

void wl_player_respawn(WlPlayer *p, Vector3 spawn)
{
    p->pos = spawn;
    p->vel = (Vector3){ 0 };
    p->health = 20.0f;
    p->hunger = 20.0f;
    p->air = 10.0f;
    p->exhaustion = 0.0f;
    p->fall_start_y = -1.0f;
    p->dead = false;
    p->damage_flash = 0.0f;
    p->break_progress = 0.0f;
}

/* ------------------------------------------------------------------ */
/* main update                                                         */
/* ------------------------------------------------------------------ */

void wl_player_update(WlPlayer *p, WlWorld *w, const WlKeybinds *kb,
                      const WlConfig *cfg, WlAudio *audio,
                      float dt, bool input_active, int gamemode)
{
    apply_pending_look(p, cfg);
    if (p->dead) {
        p->vel = (Vector3){ 0 };
        return;
    }
    if (dt > 0.1f) dt = 0.1f;

    /* --- state: water --- */
    Vector3 eye = wl_player_eye(p);
    uint8_t b_feet = wl_world_get_block(w, (int)floorf(p->pos.x),
                                        (int)floorf(p->pos.y + 0.3f), (int)floorf(p->pos.z));
    uint8_t b_head = wl_world_get_block(w, (int)floorf(eye.x),
                                        (int)floorf(eye.y), (int)floorf(eye.z));
    bool now_water = wl_blocks_liquid(b_feet);
    if (now_water && !p->in_water) wl_audio_play(audio, WL_SND_SPLASH);
    p->in_water = now_water;
    p->head_in_water = wl_blocks_liquid(b_head);

    /* --- input --- */
    bool want_forward = false, want_back = false, want_left = false, want_right = false;
    bool want_jump = false, want_sneak = false, want_sprint = false;
    if (input_active) {
        want_forward = wl_keybinds_down(kb, WL_ACT_FORWARD);
        want_back = wl_keybinds_down(kb, WL_ACT_BACK);
        want_left = wl_keybinds_down(kb, WL_ACT_LEFT);
        want_right = wl_keybinds_down(kb, WL_ACT_RIGHT);
        want_jump = wl_keybinds_down(kb, WL_ACT_JUMP);
        want_sneak = wl_keybinds_down(kb, WL_ACT_SNEAK);
        want_sprint = wl_keybinds_down(kb, WL_ACT_SPRINT);

        /* double-tap jump toggles flying (creative only) */
        if (gamemode == WL_WORLD_GAMEMODE_CREATIVE && IsKeyPressed(kb->key[WL_ACT_JUMP])) {
            if (p->last_jump_tap < 0.0) p->last_jump_tap = GetTime();
            else {
                double now = GetTime();
                if (now - p->last_jump_tap < 0.3) {
                    p->flying = !p->flying;
                    p->vel.y = 0.0f;
                    p->last_jump_tap = -1.0;
                } else {
                    p->last_jump_tap = now;
                }
            }
        }
    }

    /* sneaking (forced while under a low ceiling) */
    bool want_sneak_state = want_sneak && !p->flying;
    if (!want_sneak_state && p->sneaking) {
        /* check headroom before standing up */
        WlPlayer probe = *p;
        probe.sneaking = false;
        int cx = 0, cy = 0, cz = 0;
        if (player_collides(w, &probe, &cx, &cy, &cz))
            want_sneak_state = true; /* ceiling too low: stay sneaking */
    }
    p->sneaking = want_sneak_state;
    if (p->flying) p->sneaking = false;

    p->sprinting = want_sprint && want_forward && !p->sneaking &&
                   (p->hunger > 6.0f || gamemode == WL_WORLD_GAMEMODE_CREATIVE);

    /* --- wish velocity --- */
    float fwd_x = -sinf(p->yaw), fwd_z = -cosf(p->yaw);
    float rgt_x = cosf(p->yaw), rgt_z = -sinf(p->yaw);
    float ix = 0.0f, iz = 0.0f;
    if (want_forward) { ix += fwd_x; iz += fwd_z; }
    if (want_back) { ix -= fwd_x; iz -= fwd_z; }
    if (want_right) { ix += rgt_x; iz += rgt_z; }
    if (want_left) { ix -= rgt_x; iz -= rgt_z; }
    float ilen = sqrtf(ix * ix + iz * iz);
    if (ilen > 1.0f) { ix /= ilen; iz /= ilen; }

    float speed;
    if (p->flying) speed = p->sprinting ? WL_P_FLY_SPRINT : WL_P_FLY;
    else if (p->in_water) speed = WL_P_SWIM;
    else if (p->sneaking) speed = WL_P_SNEAK_SPD;
    else if (p->sprinting) speed = WL_P_SPRINT;
    else speed = WL_P_WALK;

    float wish_x = ix * speed, wish_z = iz * speed;

    float accel = p->flying ? 30.0f : (p->on_ground ? 40.0f : (p->in_water ? 20.0f : 8.0f));
    float k = accel * dt;
    if (k > 1.0f) k = 1.0f;
    p->vel.x += (wish_x - p->vel.x) * k;
    p->vel.z += (wish_z - p->vel.z) * k;

    /* --- vertical --- */
    if (p->flying) {
        float vy = 0.0f;
        if (want_jump) vy = speed * 0.8f;
        else if (want_sneak) vy = -speed * 0.8f;
        float fk = 10.0f * dt;
        if (fk > 1.0f) fk = 1.0f;
        p->vel.y += (vy - p->vel.y) * fk;
    } else if (p->in_water) {
        float vy = want_jump ? 4.4f : -1.7f;
        float kw = 6.0f * dt;
        if (kw > 1.0f) kw = 1.0f;
        p->vel.y += (vy - p->vel.y) * kw;
    } else {
        if (want_jump && p->on_ground) {
            p->vel.y = WL_P_JUMP;
            p->on_ground = false;
            p->exhaustion += 0.1f;
            wl_audio_play_v(audio, WL_SND_JUMP, 0.10f);
            p->fall_start_y = p->pos.y; /* jump starts a fall measurement */
        }
        p->vel.y -= WL_P_GRAVITY * dt;
        if (p->vel.y < -WL_P_TERMINAL) p->vel.y = -WL_P_TERMINAL;
    }

    /* --- move with collision --- */
    bool grounded_before = p->on_ground;
    p->on_ground = false;

    float dx = p->vel.x * dt, dz = p->vel.z * dt, dy = p->vel.y * dt;

    /* sneak edge guard: do not walk off blocks while sneaking on ground */
    if (p->sneaking && grounded_before && !p->flying) {
        WlPlayer probe = *p;
        probe.pos.x += dx;
        if (!has_ground_below(w, &probe)) { dx = 0.0f; p->vel.x = 0.0f; }
        probe = *p;
        probe.pos.z += dz;
        if (!has_ground_below(w, &probe)) { dz = 0.0f; p->vel.z = 0.0f; }
    }

    bool hit_x = move_axis(w, p, 0, dx);
    bool hit_z = move_axis(w, p, 2, dz);
    bool hit_y = move_axis(w, p, 1, dy);

    if (hit_x) p->vel.x = 0.0f;
    if (hit_z) p->vel.z = 0.0f;
    if (hit_y) {
        if (dy < 0.0f) p->on_ground = true;
        p->vel.y = 0.0f;
    }
    /* ground check when standing still (velocity clamped to 0 by snap) */
    if (!p->on_ground && !p->flying) {
        WlPlayer probe = *p;
        probe.pos.y -= 0.04f;
        if (player_collides(w, &probe, NULL, NULL, NULL)) p->on_ground = true;
    }

    /* --- fall damage --- */
    if (p->flying || p->in_water) {
        p->fall_start_y = -1.0f;
    } else if (!p->on_ground && p->vel.y < 0.0f) {
        if (p->fall_start_y < 0.0f) p->fall_start_y = p->pos.y;
    } else if (p->on_ground && p->fall_start_y >= 0.0f) {
        float dist = p->fall_start_y - p->pos.y;
        p->fall_start_y = -1.0f;
        if (dist > 3.2f && gamemode == WL_WORLD_GAMEMODE_SURVIVAL)
            wl_player_damage(p, floorf(dist - 3.0f), audio, cfg->difficulty);
        else if (dist > 1.2f)
            wl_audio_play_v(audio, WL_SND_LAND, 0.10f); /* soft landing */
    }

    /* --- eye height + bobbing --- */
    float target_eye = p->sneaking ? WL_P_EYE_SNEAK : WL_P_EYE_STAND;
    float ek = (cfg->smooth_camera ? 12.0f : 60.0f) * dt;
    if (ek > 1.0f) ek = 1.0f;
    p->eye_height += (target_eye - p->eye_height) * ek;

    float hspeed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    bool moving = hspeed > 0.4f;
    if (p->on_ground && moving) {
        p->bob_phase += hspeed * dt * 1.9f;
        p->bob_amount += (1.0f - p->bob_amount) * wl_clampf(6.0f * dt, 0.0f, 1.0f);
    } else {
        p->bob_amount += (0.0f - p->bob_amount) * wl_clampf(8.0f * dt, 0.0f, 1.0f);
    }

    /* --- footsteps --- */
    if (p->on_ground && moving) {
        p->step_acc += hspeed * dt;
        if (p->step_acc > 2.3f) {
            p->step_acc = 0.0f;
            uint8_t below = wl_world_get_block(w, (int)floorf(p->pos.x),
                                               (int)floorf(p->pos.y - 0.4f),
                                               (int)floorf(p->pos.z));
            wl_audio_play_v(audio, step_sound_for(below), 0.12f);
        }
    }

    /* --- timers --- */
    if (p->damage_flash > 0.0f) p->damage_flash -= dt;
    if (p->break_cooldown > 0.0) p->break_cooldown -= dt;
    if (p->place_cooldown > 0.0) p->place_cooldown -= dt;

    survival_update(p, cfg, audio, dt, moving, gamemode);
}
