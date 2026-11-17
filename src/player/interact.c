/* interact.c — raycasting, mining, placing, eating, pick-block. */
#include "wl/interact.h"
#include "wl/entities.h"
#include "wl/world.h"
#include "wl/player.h"
#include "wl/world.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/config.h"
#include "wl/audio.h"
#include "wl/log.h"

#include <math.h>
#include <string.h>

#define WL_REACH 5.0f
#define WL_EAT_TIME 1.2f

/* ------------------------------------------------------------------ */
/* voxel raycast                                                       */
/* ------------------------------------------------------------------ */

bool wl_raycast(WlWorld *w, Vector3 o, Vector3 d, float max_dist, WlTarget *out)
{
    out->valid = false;
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-6f) return false;
    d.x /= len;
    d.y /= len;
    d.z /= len;

    int x = (int)floorf(o.x), y = (int)floorf(o.y), z = (int)floorf(o.z);
    int step_x = d.x > 0 ? 1 : -1;
    int step_y = d.y > 0 ? 1 : -1;
    int step_z = d.z > 0 ? 1 : -1;

    float tdx = d.x != 0 ? fabsf(1.0f / d.x) : 1e30f;
    float tdy = d.y != 0 ? fabsf(1.0f / d.y) : 1e30f;
    float tdz = d.z != 0 ? fabsf(1.0f / d.z) : 1e30f;

    float tmx = d.x != 0 ? (d.x > 0 ? (float)(x + 1) - o.x : o.x - (float)x) * tdx : 1e30f;
    float tmy = d.y != 0 ? (d.y > 0 ? (float)(y + 1) - o.y : o.y - (float)y) * tdy : 1e30f;
    float tmz = d.z != 0 ? (d.z > 0 ? (float)(z + 1) - o.z : o.z - (float)z) * tdz : 1e30f;

    int last_axis = -1; /* 0=x 1=y 2=z */
    float t = 0.0f;

    for (int i = 0; i < 256; i++) {
        uint8_t id = wl_world_get_block(w, x, y, z);
        const WlBlock *def = wl_block_get(id);
        if (def && def->shape != WL_RENDER_LIQUID) {
            out->valid = true;
            out->x = x;
            out->y = y;
            out->z = z;
            out->id = id;
            out->px = x;
            out->py = y;
            out->pz = z;
            if (last_axis == 0) out->px = x - step_x;
            else if (last_axis == 1) out->py = y - step_y;
            else if (last_axis == 2) out->pz = z - step_z;
            return true;
        }

        if (tmx < tmy && tmx < tmz) {
            x += step_x;
            t = tmx;
            tmx += tdx;
            last_axis = 0;
        } else if (tmy < tmz) {
            y += step_y;
            t = tmy;
            tmy += tdy;
            last_axis = 1;
        } else {
            z += step_z;
            t = tmz;
            tmz += tdz;
            last_axis = 2;
        }
        if (t > max_dist) break;
    }
    return false;
}

WlTarget wl_interact_query(WlWorld *w, const WlPlayer *p)
{
    WlTarget t;
    memset(&t, 0, sizeof(t));
    if (!p || p->dead) return t;
    Vector3 eye = wl_player_eye(p);
    Vector3 dir = wl_player_look_dir(p);
    wl_raycast(w, eye, dir, WL_REACH, &t);
    return t;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static WlSndId break_sound_for(const WlBlock *def)
{
    if (def->tool == WL_TOOL_AXE) return WL_SND_BREAK_WOOD;
    if (def->tool == WL_TOOL_PICKAXE) return WL_SND_BREAK_STONE;
    return WL_SND_BREAK_DIRT;
}

static float mining_speed(const WlSlot *slot, const WlBlock *def)
{
    static const float tier_mult[5] = { 1.0f, 2.0f, 4.0f, 6.0f, 8.0f };
    const WlItem *item = slot ? wl_item_get(slot->item) : NULL;
    if (item && item->tool != WL_TOOL_NONE && item->tool == def->tool &&
        item->tool_tier <= 4)
        return tier_mult[item->tool_tier];
    return 1.0f;
}

static bool drops_allowed(const WlSlot *slot, const WlBlock *def)
{
    if (!def->needs_tool) return true;
    const WlItem *item = slot ? wl_item_get(slot->item) : NULL;
    return item && item->tool == def->tool && item->tool_tier >= def->tool_tier;
}

static void damage_tool(WlPlayer *p, WlSlot *slot, WlAudio *audio)
{
    (void)p; /* kept for API symmetry */
    if (!slot || slot->item == WL_ITEM_NONE) return;
    const WlItem *item = wl_item_get(slot->item);
    if (!item || item->durability == 0) return;

    if (slot->dur == 0) slot->dur = item->durability; /* fresh tool */
    if (slot->dur > 0) slot->dur--;
    if (slot->dur == 0) {
        WL_LOGD("tool '%s' broke", item->name);
        slot->item = WL_ITEM_NONE;
        slot->count = 0;
        slot->dur = 0;
        wl_audio_play(audio, WL_SND_BREAK_WOOD);
    }
}

static bool block_intersects_player(const WlPlayer *p, int bx, int by, int bz)
{
    float hw, h;
    wl_player_extents(p, &hw, &h);
    return (bx + 1 > p->pos.x - hw && bx < p->pos.x + hw) &&
           (by + 1 > p->pos.y && by < p->pos.y + h) &&
           (bz + 1 > p->pos.z - hw && bz < p->pos.z + hw);
}

static bool cell_replaceable(WlWorld *w, int x, int y, int z)
{
    uint8_t id = wl_world_get_block(w, x, y, z);
    return id == WL_BLOCK_AIR || id == WL_BLOCK_WATER || id == WL_BLOCK_TALLGRASS;
}

/* ------------------------------------------------------------------ */
/* mining / building                                                   */
/* ------------------------------------------------------------------ */

static void break_block(WlPlayer *p, WlWorld *w, const WlTarget *t,
                        WlAudio *audio, int gamemode)
{
    const WlBlock *def = wl_block_get(t->id);
    if (!def) return;

    /* furnace contents pop out before the block goes away */
    if (t->id == WL_BLOCK_FURNACE || t->id == WL_BLOCK_FURNACE_LIT) {
        WlSlot stacks[3];
        if (wl_world_remove_furnace(w, t->x, t->y, t->z, stacks)) {
            for (int i = 0; i < 3; i++)
                if (stacks[i].item != WL_ITEM_NONE)
                    wl_world_add_drop(w, stacks[i].item, stacks[i].count,
                                      stacks[i].dur,
                                      (Vector3){ t->x + 0.5f, t->y + 0.5f,
                                                 t->z + 0.5f });
        }
    }

    WlSlot *slot = &p->inv.slots[p->selected_slot];
    wl_audio_play(audio, break_sound_for(def));

    if (gamemode == WL_WORLD_GAMEMODE_SURVIVAL) {
        if (drops_allowed(slot, def) && def->drop_item != WL_ITEM_NONE)
            wl_inv_add(&p->inv, def->drop_item, def->drop_count, 0);

        /* occasional apple from leaves */
        if ((t->id == WL_BLOCK_LEAVES || t->id == WL_BLOCK_BIRCH_LEAVES)) {
            uint64_t r = (uint64_t)(t->x * 73856093) ^ (uint64_t)(t->y * 19349663) ^
                         (uint64_t)(t->z * 83492791);
            if (r % 100 < 5) wl_inv_add(&p->inv, WL_ITEM_APPLE, 1, 0);
        }
        damage_tool(p, slot, audio);
        p->exhaustion += 0.005f;
    }

    wl_world_set_block(w, t->x, t->y, t->z, WL_BLOCK_AIR);
}

static bool try_place(WlPlayer *p, WlWorld *w, const WlTarget *t,
                      WlAudio *audio, int gamemode)
{
    WlSlot *slot = &p->inv.slots[p->selected_slot];
    if (slot->item == WL_ITEM_NONE) return false;
    const WlItem *item = wl_item_get(slot->item);
    if (!item || !item->is_block) return false;

    /* when the crosshair is on a replaceable plant (tall grass), place
     * INTO its cell instead of the adjacent one — the adjacent cell sits
     * on top of the plant and has no solid floor, which used to eat the
     * placement on meadows */
    int px = t->px, py = t->py, pz = t->pz;
    if (cell_replaceable(w, t->x, t->y, t->z)) {
        px = t->x; py = t->y; pz = t->z;
    }
    if (!cell_replaceable(w, px, py, pz)) return false;

    const WlBlock *def = wl_block_get(item->block_id);
    if (!def) return false;

    if (def->shape == WL_RENDER_CROSS) {
        /* plants need a solid block below (flowers only on grass/dirt) */
        uint8_t below = wl_world_get_block(w, px, py - 1, pz);
        if (!wl_block_is_solid(below)) return false;
        if (item->block_id == WL_BLOCK_FLOWER_RED ||
            item->block_id == WL_BLOCK_FLOWER_YELLOW) {
            if (below != WL_BLOCK_GRASS && below != WL_BLOCK_DIRT &&
                below != WL_BLOCK_SNOW_GRASS)
                return false;
        }
    } else if (def->solid && block_intersects_player(p, px, py, pz)) {
        return false;
    }

    if (!wl_world_set_block(w, px, py, pz, item->block_id)) return false;

    if (gamemode == WL_WORLD_GAMEMODE_SURVIVAL)
        wl_inv_consume_slot(&p->inv, p->selected_slot);

    wl_audio_play(audio, WL_SND_PLACE);
    return true;
}

/* ------------------------------------------------------------------ */
/* per-frame update                                                    */
/* ------------------------------------------------------------------ */

/* test seam: when enabled, replaces the physical mouse buttons (--selftest) */
static bool s_dbg_active = false;
static bool s_dbg_left = false;
static bool s_dbg_right = false;

void wl_interact_debug_mouse(bool left, bool right, bool enable)
{
    s_dbg_left = left;
    s_dbg_right = right;
    s_dbg_active = enable;
}

int wl_interact_update(WlPlayer *p, WlWorld *w, const WlConfig *cfg,
                       WlAudio *audio, float dt, bool input_active)
{
    if (!p || p->dead) return WL_INTERACT_NONE;
    int gamemode = wl_world_gamemode(w);
    (void)cfg;

    WlTarget t = wl_interact_query(w, p);
    WlSlot *slot = &p->inv.slots[p->selected_slot];
    const WlItem *held = wl_item_get(slot->item);

    bool mouse_left = input_active &&
        (s_dbg_active ? s_dbg_left : IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    bool mouse_right = input_active &&
        (s_dbg_active ? s_dbg_right : IsMouseButtonDown(MOUSE_BUTTON_RIGHT));

    /* ---- eating (hold right mouse with food) ---- */
    const WlItem *food = held;
    if (mouse_right && food && food->food > 0 && p->hunger < 20.0f) {
        p->eat_timer += dt;
        if (p->eat_timer >= WL_EAT_TIME) {
            p->hunger = p->hunger + (float)food->food > 20.0f ? 20.0f : p->hunger + (float)food->food;
            wl_inv_consume_slot(&p->inv, p->selected_slot);
            wl_audio_play(audio, WL_SND_EAT);
            p->eat_timer = 0.0;
        }
        return WL_INTERACT_NONE; /* eating blocks other use actions */
    }
    p->eat_timer = 0.0;

    /* ---- melee: an entity under the crosshair takes priority over
     * mining; retriggered on a fresh press or every cooldown cycle ---- */
    if (mouse_left && p->break_cooldown <= 0.0) {
        Vector3 eye = wl_player_eye(p);
        Vector3 look = wl_player_look_dir(p);
        float hit_d = 0.0f;
        WlEntity *target = wl_entity_ray_hit(w, eye, look, 3.6f, &hit_d);
        if (target) {
            float dmg = 1.0f; /* bare hands */
            if (held && held->tool == WL_TOOL_SWORD)
                dmg = 3.0f + (float)held->tool_tier; /* wood 4 .. diamond 7 */
            else if (held && held->tool != WL_TOOL_NONE)
                dmg = 2.0f + (float)held->tool_tier * 0.5f;
            wl_entity_hurt(w, target, dmg, look, audio);
            if (held && held->durability > 0) damage_tool(p, slot, audio);
            p->break_cooldown = 0.45;
            p->break_progress = 0.0;
            p->exhaustion += 0.1f;
            return WL_INTERACT_NONE;
        }
    }

    /* ---- breaking ---- */
    if (mouse_left && t.valid) {
        const WlBlock *def = wl_block_get(t.id);
        if (def && !def->unbreakable) {
            /* reset progress when the target changes */
            if (t.x != p->break_x || t.y != p->break_y || t.z != p->break_z ||
                t.id != p->break_id) {
                p->break_x = t.x;
                p->break_y = t.y;
                p->break_z = t.z;
                p->break_id = t.id;
                p->break_progress = 0.0;
            }

            float speed = mining_speed(slot, def);
            float time_total = (float)def->hardness / (10.0f * speed);

            if (gamemode == WL_WORLD_GAMEMODE_CREATIVE) {
                if (p->break_cooldown <= 0.0) {
                    break_block(p, w, &t, audio, gamemode);
                    p->break_cooldown = 0.18;
                }
            } else if (time_total <= 0.001f) {
                break_block(p, w, &t, audio, gamemode);
            } else {
                p->break_progress += dt / time_total;
                if (p->break_progress >= 1.0) {
                    break_block(p, w, &t, audio, gamemode);
                    p->break_progress = 0.0;
                }
            }
        } else {
            p->break_progress = 0.0;
        }
    } else {
        p->break_progress = 0.0;
    }

    /* ---- placing / using ---- */
    if (mouse_right && t.valid) {
        /* crafting table opens the 3x3 grid unless sneaking */
        if (t.id == WL_BLOCK_CRAFTING_TABLE && !p->sneaking)
            return WL_INTERACT_OPEN_CRAFT;

        /* furnaces open their own screen (sneaking still places) */
        if ((t.id == WL_BLOCK_FURNACE || t.id == WL_BLOCK_FURNACE_LIT) &&
            !p->sneaking)
            return WL_INTERACT_OPEN_FURNACE;

        if (p->place_cooldown <= 0.0) {
            if (try_place(p, w, &t, audio, gamemode))
                p->place_cooldown = 0.22;
        }
    }

    /* ---- pick block (middle mouse) ---- */
    if (input_active && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && t.valid) {
        if (gamemode == WL_WORLD_GAMEMODE_CREATIVE) {
            int target = -1;
            for (int i = 0; i < WL_INV_SIZE; i++)
                if (p->inv.slots[i].item == (uint16_t)t.id) { target = i; break; }
            if (target < 0) target = p->selected_slot;

            p->inv.slots[target].item = t.id;
            p->inv.slots[target].count = 1;
            p->inv.slots[target].dur = 0;
            if (target >= WL_HOTBAR_SIZE) {
                /* swap into the selected hotbar slot for convenience */
                WlSlot tmp = p->inv.slots[p->selected_slot];
                p->inv.slots[p->selected_slot] = p->inv.slots[target];
                p->inv.slots[target] = tmp;
            }
        } else {
            /* survival: select the matching hotbar item if present */
            for (int i = 0; i < WL_HOTBAR_SIZE; i++)
                if (p->inv.slots[i].item == (uint16_t)t.id) {
                    p->selected_slot = i;
                    break;
                }
        }
    }

    return WL_INTERACT_NONE;
}
