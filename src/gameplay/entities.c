/* entities.c — mob/animal AI, physics, combat, spawning, persistence.
 *
 * Movement uses the same axis-separated AABB collision as the player
 * (simplified): hostile mobs auto-jump when blocked. The natural spawner
 * relies on the voxel lighting engine — hostile mobs need light < 8
 * (night surface or dark caves), animals need daylight and grass. */
#include "wl/entities.h"
#include "wl/world.h"
#include "wl/light.h"
#include "wl/player.h"
#include "wl/config.h"
#include "wl/audio.h"
#include "wl/assets.h"

#define WL_DEATH_TIME 0.9f /* seconds of fall-over death animation */
#include "wl/block.h"
#include "wl/item.h"
#include "wl/worldgen.h"
#include "wl/paths.h"
#include "wl/util.h"
#include "wl/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WL_ENT_GRAVITY 24.0f
#define WL_ENT_PICKUP_NONE 0.0f

/* ------------------------------------------------------------------ */
/* type table                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    float half_w, height;
    float health;
    float speed;
    bool hostile;
} WlEntInfo;

static const WlEntInfo k_info[WL_ENT_TYPE_COUNT] = {
    [WL_ENT_NONE]     = { 0.0f, 0.0f, 0.0f, 0.0f, false },
    [WL_ENT_ZOMBIE]   = { 0.3f, 1.9f, 20.0f, 2.2f, true },
    [WL_ENT_SKELETON] = { 0.3f, 1.9f, 16.0f, 2.0f, true },
    [WL_ENT_SPIDER]   = { 0.55f, 0.85f, 14.0f, 2.9f, true },
    [WL_ENT_ARROW]    = { 0.05f, 0.1f, 1.0f, 0.0f, false },
    [WL_ENT_COW]      = { 0.45f, 1.3f, 10.0f, 1.1f, false },
    [WL_ENT_PIG]      = { 0.4f, 1.0f, 10.0f, 1.1f, false },
    [WL_ENT_CHICKEN]  = { 0.25f, 0.7f, 4.0f, 0.9f, false },
    [WL_ENT_SHEEP]    = { 0.45f, 1.25f, 8.0f, 1.0f, false },
};

void wl_entity_extents(WlEntityType t, float *half_w, float *height)
{
    if (t <= WL_ENT_NONE || t >= WL_ENT_TYPE_COUNT) t = WL_ENT_ZOMBIE;
    *half_w = k_info[t].half_w;
    *height = k_info[t].height;
}

bool wl_entity_type_hostile(WlEntityType t)
{
    return t > WL_ENT_NONE && t < WL_ENT_TYPE_COUNT && k_info[t].hostile;
}

void wl_entity_defaults(WlEntity *e)
{
    if (!e || e->type <= WL_ENT_NONE || e->type >= WL_ENT_TYPE_COUNT) return;
    e->health = k_info[e->type].health;
    e->yaw = (float)(e->id % 628) / 100.0f;
}

/* ------------------------------------------------------------------ */
/* world storage (accessed through the opaque WlWorld)                 */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* physics                                                             */
/* ------------------------------------------------------------------ */

/* axis-separated AABB move; returns whether horizontal movement was blocked */
static bool ent_move(WlWorld *w, WlEntity *e, float dt)
{
    float hw, h;
    wl_entity_extents(e->type, &hw, &h);
    bool blocked = false;

    /* X axis */
    float nx = e->pos.x + e->vel.x * dt;
    {
        float dir = nx > e->pos.x ? 1.0f : -1.0f;
        int bx = (int)floorf(nx + dir * hw);
        for (int y = (int)floorf(e->pos.y + 0.05f);
             y <= (int)floorf(e->pos.y + h - 0.05f); y++)
            for (int z = (int)floorf(e->pos.z - hw);
                 z <= (int)floorf(e->pos.z + hw); z++) {
                if (!wl_world_solid_at(w, bx, y, z)) continue;
                nx = dir > 0 ? (float)bx - hw - 0.001f : (float)(bx + 1) + hw + 0.001f;
                e->vel.x = 0.0f;
                blocked = true;
            }
    }
    e->pos.x = nx;

    /* Z axis */
    float nz = e->pos.z + e->vel.z * dt;
    {
        float dir = nz > e->pos.z ? 1.0f : -1.0f;
        int bz = (int)floorf(nz + dir * hw);
        for (int y = (int)floorf(e->pos.y + 0.05f);
             y <= (int)floorf(e->pos.y + h - 0.05f); y++)
            for (int x = (int)floorf(e->pos.x - hw);
                 x <= (int)floorf(e->pos.x + hw); x++) {
                if (!wl_world_solid_at(w, x, y, bz)) continue;
                nz = dir > 0 ? (float)bz - hw - 0.001f : (float)(bz + 1) + hw + 0.001f;
                e->vel.z = 0.0f;
                blocked = true;
            }
    }
    e->pos.z = nz;

    /* Y axis */
    float ny = e->pos.y + e->vel.y * dt;
    e->on_ground = false;
    if (e->vel.y <= 0.0f) {
        int by = (int)floorf(ny);
        for (int x = (int)floorf(e->pos.x - hw);
             x <= (int)floorf(e->pos.x + hw); x++)
            for (int z = (int)floorf(e->pos.z - hw);
                 z <= (int)floorf(e->pos.z + hw); z++) {
                if (!wl_world_solid_at(w, x, by, z)) continue;
                ny = (float)(by + 1);
                e->vel.y = 0.0f;
                e->on_ground = true;
            }
    } else {
        int by = (int)floorf(ny + h);
        for (int x = (int)floorf(e->pos.x - hw);
             x <= (int)floorf(e->pos.x + hw); x++)
            for (int z = (int)floorf(e->pos.z - hw);
                 z <= (int)floorf(e->pos.z + hw); z++) {
                if (!wl_world_solid_at(w, x, by, z)) continue;
                ny = (float)by - h - 0.001f;
                e->vel.y = 0.0f;
            }
    }
    e->pos.y = ny;
    return blocked;
}

/* ------------------------------------------------------------------ */
/* drops + death                                                       */
/* ------------------------------------------------------------------ */

static void drop_loot(WlWorld *w, WlEntity *e)
{
    /* counts chosen with the world time so repeated runs are stable */
    uint32_t r = (uint32_t)(e->id * 2654435761u) ^ (uint32_t)(e->pos.x * 8191.0f);
    switch (e->type) {
    case WL_ENT_ZOMBIE:
        if ((r & 3) == 0) /* 25% */ wl_world_add_drop(w, WL_ITEM_ROTTEN_FLESH, 1, 0, e->pos);
        break;
    case WL_ENT_SKELETON:
        wl_world_add_drop(w, WL_ITEM_ARROW, 1 + (r & 1), 0, e->pos);
        if ((r & 3) == 0) wl_world_add_drop(w, WL_ITEM_BONE, 1, 0, e->pos);
        break;
    case WL_ENT_SPIDER:
        if ((r & 3) != 0) /* 75% */ wl_world_add_drop(w, WL_ITEM_STRING, 1 + (r & 1), 0, e->pos);
        break;
    case WL_ENT_COW:
        wl_world_add_drop(w, WL_ITEM_BEEF_RAW, 1 + (r % 3), 0, e->pos);
        if ((r & 3) != 0) wl_world_add_drop(w, WL_ITEM_LEATHER, 1 + (r & 1), 0, e->pos);
        break;
    case WL_ENT_PIG:
        wl_world_add_drop(w, WL_ITEM_PORK_RAW, 1 + (r % 2), 0, e->pos);
        break;
    case WL_ENT_CHICKEN:
        if ((r & 1) == 0) wl_world_add_drop(w, WL_ITEM_CHICKEN_RAW, 1, 0, e->pos);
        wl_world_add_drop(w, WL_ITEM_FEATHER, 1 + (r & 1), 0, e->pos);
        break;
    case WL_ENT_SHEEP:
        wl_world_add_drop(w, WL_ITEM_MUTTON_RAW, 1 + (r % 2), 0, e->pos);
        wl_world_add_drop(w, WL_ITEM_WOOL, 1, 0, e->pos);
        break;
    default:
        break;
    }
}

void wl_entity_hurt(WlWorld *w, WlEntity *e, float amount, Vector3 knock_dir,
                    struct WlAudio *audio)
{
    if (!e || !e->active || e->type == WL_ENT_ARROW) return;
    e->health -= amount;
    e->hurt_flash = 0.35f;
    /* the voice answers the hit (pitch-varied below) */
    e->provoked = true;
    /* knockback + upward pop */
    e->vel.x += knock_dir.x * 5.5f;
    e->vel.z += knock_dir.z * 5.5f;
    if (e->on_ground) e->vel.y = 4.5f;
    /* animals panic */
    if (!wl_entity_type_hostile(e->type)) e->wander_timer = -6.0f; /* flee mode */
    wl_audio_play_v(audio, WL_SND_MOB_HURT, 0.15f);
    if (e->health <= 0.0f && e->death_timer <= 0.0f) {
        drop_loot(w, e);
        e->death_timer = WL_DEATH_TIME; /* fall-over animation */
    }
}

/* ------------------------------------------------------------------ */
/* per-type AI                                                         */
/* ------------------------------------------------------------------ */

static void face_towards(WlEntity *e, Vector3 target)
{
    float dx = target.x - e->pos.x;
    float dz = target.z - e->pos.z;
    e->yaw = atan2f(-dx, -dz); /* yaw 0 looks toward -Z, like the player */
}

static float dist2_xz(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

static void walk_forward(WlEntity *e, float speed, float dt)
{
    float s = sinf(e->yaw), c = cosf(e->yaw);
    /* player yaw convention: forward = (-sin(yaw), 0, -cos(yaw)) */
    e->vel.x = -s * speed;
    e->vel.z = -c * speed;
    e->anim_phase += dt * speed * 2.4f;
}

/* deterministic pseudo random from the entity's stable id + tick */
static uint32_t ent_rand(WlEntity *e, uint32_t salt)
{
    uint32_t x = e->id * 747796405u + salt * 2891336453u + 1;
    x = (x ^ (x >> 15)) * 2246822519u;
    x = (x ^ (x >> 13)) * 3266489917u;
    return x ^ (x >> 16);
}

static void ai_hostile_melee(WlWorld *w, WlEntity *e, struct WlPlayer *p,
                             struct WlAudio *audio, int difficulty, float dt,
                             float attack_range, float damage)
{
    float d2 = dist2_xz(e->pos, p->pos);
    if (d2 > 24.0f * 24.0f) return; /* out of sight */

    face_towards(e, p->pos);
    walk_forward(e, k_info[e->type].speed, dt);

    float d = sqrtf(d2);
    if (d < attack_range && e->attack_cd <= 0.0f) {
        e->attack_cd = 1.0f;
        /* vertical reach check so ceiling campers are safe */
        float dy = p->pos.y - e->pos.y;
        if (dy > -2.2f && dy < 2.2f)
            wl_player_damage(p, damage, audio, difficulty);
    }
    (void)w;
}

static void ai_skeleton(WlWorld *w, WlEntity *e, struct WlPlayer *p,
                      struct WlAudio *audio, float dt)
{
    float d2 = dist2_xz(e->pos, p->pos);
    if (d2 > 20.0f * 20.0f) return;
    float d = sqrtf(d2);

    face_towards(e, p->pos);
    if (d > 13.0f) {
        walk_forward(e, k_info[e->type].speed, dt);
    } else if (d < 6.0f) {
        /* back away while facing the player */
        walk_forward(e, -k_info[e->type].speed * 0.8f, dt);
    } else {
        /* strafe slowly so it is not a sitting duck */
        e->vel.x *= 0.4f;
        e->vel.z *= 0.4f;
        e->anim_phase += dt * 1.2f;
    }

    e->shoot_cd -= dt;
    if (e->shoot_cd <= 0.0f && d > 3.0f && d < 16.0f) {
        e->shoot_cd = 2.4f;
        /* line of sight: raycast from the skeleton's eye to the player's
         * chest; only fire when no block is in between */
        Vector3 o = { e->pos.x, e->pos.y + 1.5f, e->pos.z };
        Vector3 t = { p->pos.x - o.x, p->pos.y + 1.0f - o.y, p->pos.z - o.z };
        float len = sqrtf(t.x * t.x + t.y * t.y + t.z * t.z);
        if (len > 0.01f) {
            t.x /= len; t.y /= len; t.z /= len;
            /* reuse the block raycast; a hit closer than the player = cover */
            bool has_cover = false;
            for (float s = 0.5f; s < len - 0.5f; s += 0.5f) {
                if (wl_world_solid_at(w, (int)floorf(o.x + t.x * s),
                                      (int)floorf(o.y + t.y * s),
                                      (int)floorf(o.z + t.z * s))) {
                    has_cover = true;
                    break;
                }
            }
            if (!has_cover) {
                WlEntity *arrow = wl_world_spawn_entity(w, WL_ENT_ARROW, o);
                if (arrow) {
                    uint32_t r = ent_rand(e, (uint32_t)(e->live_ticks * 4.0f));
                    float spread = 0.06f;
                    arrow->vel.x = t.x * 16.0f + ((float)(r & 15) - 7.5f) * spread;
                    arrow->vel.y = t.y * 16.0f + 1.2f + ((float)((r >> 4) & 15) - 7.5f) * spread;
                    arrow->vel.z = t.z * 16.0f + ((float)((r >> 8) & 15) - 7.5f) * spread;
                    arrow->yaw = e->yaw;
                    wl_audio_play(audio, WL_SND_BOW);
                }
            }
        }
    }
}

static void ai_spider(WlWorld *w, WlEntity *e, struct WlPlayer *p,
                      struct WlAudio *audio, int difficulty, float dt)
{
    /* spiders are only hostile in the dark or once provoked */
    uint8_t light = wl_light_level(w, (int)e->pos.x, (int)(e->pos.y + 0.5f),
                                   (int)e->pos.z);
    if (light >= 8 && !e->provoked) return; /* docile in daylight */

    float d2 = dist2_xz(e->pos, p->pos);
    if (d2 > 20.0f * 20.0f) return;

    face_towards(e, p->pos);
    walk_forward(e, k_info[e->type].speed, dt);
    /* erratic weave */
    float weave = sinf(e->anim_phase * 1.7f) * 0.55f;
    float s = sinf(e->yaw), c = cosf(e->yaw);
    e->vel.x += -c * weave * k_info[e->type].speed;
    e->vel.z += s * weave * k_info[e->type].speed;

    float d = sqrtf(d2);
    if (d < 1.6f && e->attack_cd <= 0.0f) {
        e->attack_cd = 1.2f;
        wl_player_damage(p, 2.0f, audio, difficulty);
    }
}

static void ai_animal(WlWorld *w, WlEntity *e, struct WlPlayer *p, float dt)
{
    (void)w; /* animals only need the player for fleeing */
    if (e->wander_timer < 0.0f) {
        /* fleeing after being hurt: run away from the player */
        if (e->wander_timer > -5.0f && p) {
            face_towards(e, (Vector3){ 2 * e->pos.x - p->pos.x, e->pos.y,
                                       2 * e->pos.z - p->pos.z });
            walk_forward(e, k_info[e->type].speed * 2.4f, dt);
        } else {
            e->wander_timer = 0.0f;
        }
    } else {
        e->wander_timer -= dt;
        if (e->wander_timer <= 0.0f) {
            e->wander_timer = 2.0f + (float)(ent_rand(e, 7) % 40) * 0.1f;
            if (ent_rand(e, 9) & 1) e->yaw = (float)(ent_rand(e, 11) % 628) / 100.0f;
            else e->vel.x = e->vel.z = 0.0f; /* idle a while */
        }
        walk_forward(e, k_info[e->type].speed * 0.6f, dt);
    }
}

static void ai_arrow(WlWorld *w, WlEntity *e, struct WlPlayer *p,
                     struct WlAudio *audio, int difficulty, float dt)
{
    e->age += dt;
    e->vel.y -= 11.0f * dt;
    e->pos.x += e->vel.x * dt;
    e->pos.y += e->vel.y * dt;
    e->pos.z += e->vel.z * dt;
    e->yaw = atan2f(-e->vel.x, -e->vel.z);

    if (e->age > 8.0f) { e->active = false; return; }
    if (wl_world_solid_at(w, (int)floorf(e->pos.x), (int)floorf(e->pos.y),
                          (int)floorf(e->pos.z))) {
        e->active = false; /* stuck in a block */
        if (audio) wl_audio_play_v(audio, WL_SND_ARROW_HIT, 0.12f);
        return;
    }
    if (p && !p->dead) {
        float hw, h;
        wl_entity_extents(e->type, &hw, &h);
        float dy = p->pos.y + 0.9f - e->pos.y;
        float dx = p->pos.x - e->pos.x, dz = p->pos.z - e->pos.z;
        if (dx * dx + dy * dy * 0.5f + dz * dz < 0.7f * 0.7f) {
            e->active = false;
            wl_audio_play_v(audio, WL_SND_ARROW_HIT, 0.15f);
            wl_player_damage(p, 3.0f, audio, difficulty);
        }
    }
}

/* ------------------------------------------------------------------ */
/* update + spawner                                                    */
/* ------------------------------------------------------------------ */

static float s_spawn_rate = 1.4f; /* attempts per second (test seam) */
void wl_entities_debug_spawn_rate(float r) { s_spawn_rate = r; }

static int count_kind(WlWorld *w, bool hostile)
{
    int n = 0, count = 0;
    WlEntity *ents = wl_world_entities(w, &count);
    for (int i = 0; i < count; i++)
        if (wl_entity_type_hostile(ents[i].type) == hostile) n++;
    return n;
}

static bool spot_free(WlWorld *w, int x, int y, int z, float hw, float h)
{
    for (int yy = y; yy < y + (int)ceilf(h); yy++)
        for (int xx = (int)floorf((float)x + 0.5f - hw);
             xx <= (int)floorf((float)x + 0.5f + hw); xx++)
            for (int zz = (int)floorf((float)z + 0.5f - hw);
                 zz <= (int)floorf((float)z + 0.5f + hw); zz++)
                if (wl_world_solid_at(w, xx, yy, zz)) return false;
    return true;
}

/* is it dark enough for hostile mobs? sky light counts only while the
 * sun is up (~13000..23000 is night); caves are dark all day. */
static bool spot_is_dark(WlWorld *w, int x, int y, int z)
{
    if (wl_light_block(w, x, y, z) >= 8) return false; /* torch-lit */
    if (wl_light_sky(w, x, y, z) < 8) return true;     /* cave / roofed */
    uint32_t t = wl_world_time(w) % WL_DAY_TICKS;
    return t >= 13200 && t < 22800;                    /* night surface */
}

static void try_spawn(WlWorld *w, struct WlPlayer *p, const WlConfig *cfg)
{
    static uint32_t s_salt = 0;
    uint32_t r = (uint32_t)(p->pos.x * 97.0f) ^ (uint32_t)(p->pos.z * 89.0f) ^
                 (uint32_t)(wl_world_time(w) * 31.0f) ^ (s_salt++ * 747796405u);
    r = (r ^ (r >> 13)) * 3266489917u;
    r ^= r >> 16;

    bool hostile = (r & 1) == 0 || count_kind(w, false) >= 12;
    if (hostile) {
        if (cfg->difficulty == 0) return; /* peaceful */
        int cap = cfg->difficulty == 1 ? 10 : (cfg->difficulty == 2 ? 16 : 24);
        if (count_kind(w, true) >= cap) return;
    } else {
        if (count_kind(w, false) >= 12) return;
    }

    /* random position in a ring around the player */
    float ang = (float)(r % 628) / 100.0f;
    float dist = hostile ? 16.0f + (float)((r >> 5) % 24) : 22.0f + (float)((r >> 5) % 26);
    int x = (int)floorf(p->pos.x + cosf(ang) * dist);
    int z = (int)floorf(p->pos.z + sinf(ang) * dist);
    int y = wl_world_top_at(w, x, z);
    if (y <= 1 || y >= WL_CHUNK_H - 2) return;
    if (!wl_world_chunk_at(w, x >> 4, z >> 4)) return; /* unloaded */

    if (hostile) {
        /* hostiles need darkness: night surface or caves */
        if (!spot_is_dark(w, x, y, z)) return;
        WlEntityType t;
        switch ((r >> 10) % 3) {
        case 0: t = WL_ENT_ZOMBIE; break;
        case 1: t = WL_ENT_SKELETON; break;
        default: t = WL_ENT_SPIDER; break;
        }
        float hw, h;
        wl_entity_extents(t, &hw, &h);
        if (!spot_free(w, x, y, z, hw, h)) return;
        wl_world_spawn_entity(w, t, (Vector3){ (float)x + 0.5f, (float)y,
                                               (float)z + 0.5f });
    } else {
        /* animals: grass + daylight + temperate biome */
        if (wl_light_level(w, x, y, z) < 9) return;
        if (wl_world_get_block(w, x, y - 1, z) != WL_BLOCK_GRASS) return;
        WlBiome biome = wl_worldgen_biome_at(wl_world_gen(w), x, z);
        if (biome != WL_BIOME_PLAINS && biome != WL_BIOME_FOREST) return;
        WlEntityType t;
        switch ((r >> 10) % 4) {
        case 0: t = WL_ENT_COW; break;
        case 1: t = WL_ENT_PIG; break;
        case 2: t = WL_ENT_SHEEP; break;
        default: t = WL_ENT_CHICKEN; break;
        }
        float hw, h;
        wl_entity_extents(t, &hw, &h);
        if (!spot_free(w, x, y, z, hw, h)) return;
        wl_world_spawn_entity(w, t, (Vector3){ (float)x + 0.5f, (float)y,
                                               (float)z + 0.5f });
    }
}

/* ambient voices: one per type on the air at a time, attenuated by the
 * distance to the player */
static WlSndId voice_for(WlEntityType t)
{
    switch (t) {
    case WL_ENT_ZOMBIE: return WL_SND_ZOMBIE_AMBIENT;
    case WL_ENT_SKELETON: return WL_SND_SKELETON_AMBIENT;
    case WL_ENT_SPIDER: return WL_SND_SPIDER_AMBIENT;
    case WL_ENT_COW: return WL_SND_COW;
    case WL_ENT_PIG: return WL_SND_PIG;
    case WL_ENT_CHICKEN: return WL_SND_CHICKEN;
    case WL_ENT_SHEEP: return WL_SND_SHEEP;
    default: return WL_SND_COUNT; /* none */
    }
}

static float s_voice_cooldown[WL_ENT_TYPE_COUNT];

void wl_world_update_entities(WlWorld *w, struct WlPlayer *p,
                              const WlConfig *cfg, struct WlAudio *audio,
                              float dt)
{
    if (!w) return;
    int count = 0;
    WlEntity *ents = wl_world_entities(w, &count);
    if (!ents) return;

    for (int t = 0; t < WL_ENT_TYPE_COUNT; t++)
        if (s_voice_cooldown[t] > 0.0f) s_voice_cooldown[t] -= dt;

    /* pass 1: dying entities count down and vanish; nothing else runs */
    for (int i = 0; i < count; i++) {
        WlEntity *e = &ents[i];
        if (!e->active || e->death_timer <= 0.0f) continue;
        e->death_timer -= dt;
        if (e->death_timer <= 0.0f) e->active = false;
    }

    static float spawn_acc = 0.0f;
    spawn_acc += dt * s_spawn_rate;
    while (spawn_acc >= 1.0f) {
        spawn_acc -= 1.0f;
        if (p) try_spawn(w, p, cfg);
    }

    for (int i = 0; i < count; i++) {
        WlEntity *e = &ents[i];
        if (!e->active) continue;
        if (e->death_timer > 0.0f) continue; /* dying: no AI, no physics */
        e->age += dt;
        e->live_ticks += dt;
        if (e->attack_cd > 0.0f) e->attack_cd -= dt;
        if (e->hurt_flash > 0.0f) e->hurt_flash -= dt;

        /* occasional ambient voice when close enough to be heard */
        if (p && audio && e->type != WL_ENT_ARROW) {
            WlSndId voice = voice_for(e->type);
            if (voice != WL_SND_COUNT && s_voice_cooldown[e->type] <= 0.0f) {
                float d2v = dist2_xz(e->pos, p->pos);
                if (d2v < 20.0f * 20.0f &&
                    (ent_rand(e, (uint32_t)(e->live_ticks * 3.0f) + 99) % 97) < 1) {
                    Vector3 ear = wl_player_eye(p);
                    wl_audio_play_at(audio, voice, e->pos, ear, 26.0f);
                    s_voice_cooldown[e->type] = 4.0f;
                }
            }
        }

        if (e->type == WL_ENT_ARROW) {
            ai_arrow(w, e, p, audio, cfg->difficulty, dt);
            continue;
        }

        /* gravity */
        if (!e->on_ground) e->vel.y -= WL_ENT_GRAVITY * dt;
        if (e->vel.y < -22.0f) e->vel.y = -22.0f;
        if (e->type == WL_ENT_CHICKEN && e->vel.y < -2.2f) e->vel.y = -2.2f;

        bool blocked = ent_move(w, e, dt);
        if (blocked && e->on_ground &&
            (wl_entity_type_hostile(e->type) || e->wander_timer < 0.0f))
            e->vel.y = 7.5f; /* auto-jump */

        if (e->on_ground) { e->vel.x *= 0.7f; e->vel.z *= 0.7f; }

        /* despawn when far away (hostiles faster than animals) */
        if (p) {
            float d2 = dist2_xz(e->pos, p->pos);
            float far = wl_entity_type_hostile(e->type) ? 52.0f : 76.0f;
            if (d2 > far * far) {
                e->despawn_timer += dt;
                if (e->despawn_timer > 4.0f) { e->active = false; continue; }
            } else {
                e->despawn_timer = 0.0f;
            }
        }

        /* fell out of the world */
        if (e->pos.y < -8.0f) { e->active = false; continue; }

        if (p && !p->dead && cfg->difficulty > 0) {
            switch (e->type) {
            case WL_ENT_ZOMBIE:
                ai_hostile_melee(w, e, p, audio, cfg->difficulty, dt, 1.7f, 3.0f);
                break;
            case WL_ENT_SKELETON:
                ai_skeleton(w, e, p, audio, dt);
                break;
            case WL_ENT_SPIDER:
                ai_spider(w, e, p, audio, cfg->difficulty, dt);
                break;
            default:
                ai_animal(w, e, p, dt);
                break;
            }
        } else if (e->type != WL_ENT_ARROW) {
            ai_animal(w, e, NULL, dt); /* peaceful idle wander */
        }
    }
}

/* ------------------------------------------------------------------ */
/* ray hit for melee                                                   */
/* ------------------------------------------------------------------ */

WlEntity *wl_entity_ray_hit(WlWorld *w, Vector3 origin, Vector3 dir,
                            float max_dist, float *hit_dist)
{
    int count = 0;
    WlEntity *ents = wl_world_entities(w, &count);
    WlEntity *best = NULL;
    float best_d = max_dist;
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-6f) return NULL;
    dir.x /= len; dir.y /= len; dir.z /= len;

    for (int i = 0; i < count; i++) {
        WlEntity *e = &ents[i];
        if (!e->active || e->type == WL_ENT_ARROW) continue;
        float hw, h;
        wl_entity_extents(e->type, &hw, &h);
        /* slab test against the entity AABB */
        float tmin = 0.0f, tmax = best_d;
        float lo[3] = { e->pos.x - hw, e->pos.y, e->pos.z - hw };
        float hi[3] = { e->pos.x + hw, e->pos.y + h, e->pos.z + hw };
        float o[3] = { origin.x, origin.y, origin.z };
        float d[3] = { dir.x, dir.y, dir.z };
        bool ok = true;
        for (int a = 0; a < 3 && ok; a++) {
            if (fabsf(d[a]) < 1e-6f) {
                if (o[a] < lo[a] || o[a] > hi[a]) ok = false;
            } else {
                float t1 = (lo[a] - o[a]) / d[a];
                float t2 = (hi[a] - o[a]) / d[a];
                if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) ok = false;
            }
        }
        if (ok && tmin < best_d) {
            best_d = tmin;
            best = e;
        }
    }
    if (best && hit_dist) *hit_dist = best_d;
    return best;
}

/* ------------------------------------------------------------------ */
/* persistence (entities.dat)                                          */
/* ------------------------------------------------------------------ */

static const char *type_name(WlEntityType t)
{
    switch (t) {
    case WL_ENT_ZOMBIE: return "zombie";
    case WL_ENT_SKELETON: return "skeleton";
    case WL_ENT_SPIDER: return "spider";
    case WL_ENT_ARROW: return "arrow";
    case WL_ENT_COW: return "cow";
    case WL_ENT_PIG: return "pig";
    case WL_ENT_CHICKEN: return "chicken";
    case WL_ENT_SHEEP: return "sheep";
    default: return "?";
    }
}

static WlEntityType type_from_name(const char *s)
{
    if (wl_str_eq(s, "zombie")) return WL_ENT_ZOMBIE;
    if (wl_str_eq(s, "skeleton")) return WL_ENT_SKELETON;
    if (wl_str_eq(s, "spider")) return WL_ENT_SPIDER;
    if (wl_str_eq(s, "arrow")) return WL_ENT_ARROW;
    if (wl_str_eq(s, "cow")) return WL_ENT_COW;
    if (wl_str_eq(s, "pig")) return WL_ENT_PIG;
    if (wl_str_eq(s, "chicken")) return WL_ENT_CHICKEN;
    if (wl_str_eq(s, "sheep")) return WL_ENT_SHEEP;
    return WL_ENT_NONE;
}

bool wl_world_save_entities(const WlWorld *w)
{
    const char *dir = w ? wl_world_save_dir(w) : NULL;
    if (!dir || dir[0] == '\0') return false;
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "entities.dat"))
        return false;

    int count = 0;
    WlEntity *ents = wl_world_entities((WlWorld *)w, &count);
    WlStrBuf sb;
    if (!wl_sb_init(&sb, 512)) return false;
    bool ok = wl_sb_printf(&sb, "# Wildlands entities\n");
    for (int i = 0; i < count && ok; i++) {
        const WlEntity *e = &ents[i];
        if (!e->active || e->type == WL_ENT_ARROW) continue; /* arrows are transient */
        ok = wl_sb_printf(&sb, "ent = %s %.2f %.2f %.2f %.1f %.2f %u\n",
                          type_name(e->type), e->pos.x, e->pos.y, e->pos.z,
                          e->health, e->yaw, (unsigned)e->id);
    }
    if (ok) ok = wl_write_file_atomic(path, sb.buf, sb.len);
    wl_sb_free(&sb);
    if (!ok) WL_LOGE("failed to write %s", path);
    return ok;
}

void wl_world_load_entities(WlWorld *w)
{
    const char *dir = w ? wl_world_save_dir(w) : NULL;
    if (!dir || dir[0] == '\0') return;
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "entities.dat")) return;
    if (!wl_file_exists(path)) return;

    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char tname[24];
        float x, y, z, health, yaw;
        unsigned id;
        if (sscanf(line, "ent = %23s %f %f %f %f %f %u",
                   tname, &x, &y, &z, &health, &yaw, &id) != 7)
            continue;
        WlEntityType t = type_from_name(tname);
        if (t == WL_ENT_NONE) continue;
        WlEntity *e = wl_world_spawn_entity(w, t, (Vector3){ x, y, z });
        if (e) {
            e->health = health;
            e->yaw = yaw;
            e->id = id;
            wl_world_note_entity_id(w, id);
        }
    }
    fclose(f);
    int n = 0;
    wl_world_entities(w, &n);
    WL_LOGI("loaded %d entities", n);
}
