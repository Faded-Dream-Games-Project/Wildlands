/* furnace.c — smelting: recipes, fuel, per-furnace tick, persistence. */
#include "wl/world.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/inventory.h"
#include "wl/paths.h"
#include "wl/util.h"
#include "wl/log.h"

#include <stdio.h>
#include <string.h>

#define WL_SMELT_TIME 5.0f /* seconds per item */

/* ------------------------------------------------------------------ */
/* recipes + fuel                                                      */
/* ------------------------------------------------------------------ */

WlItemId wl_furnace_smelt_result(WlItemId input)
{
    switch (input) {
    case WL_BLOCK_IRON_ORE: return WL_ITEM_IRON_INGOT;
    case WL_ITEM_RAW_IRON:  return WL_ITEM_IRON_INGOT;
    case WL_ITEM_BEEF_RAW:      return WL_ITEM_BEEF_COOKED;
    case WL_ITEM_PORK_RAW:      return WL_ITEM_PORK_COOKED;
    case WL_ITEM_CHICKEN_RAW:   return WL_ITEM_CHICKEN_COOKED;
    case WL_ITEM_MUTTON_RAW:    return WL_ITEM_MUTTON_COOKED;
    case WL_BLOCK_SAND:         return WL_BLOCK_GLASS;
    default: return WL_ITEM_NONE;
    }
}

float wl_furnace_fuel_time(WlItemId fuel)
{
    switch (fuel) {
    case WL_ITEM_COAL:      return 40.0f;  /* 8 smelts */
    case WL_BLOCK_PLANKS:   return 7.5f;   /* 1.5 */
    case WL_BLOCK_LOG:
    case WL_BLOCK_BIRCH_LOG: return 15.0f; /* 3 */
    case WL_ITEM_STICK:     return 2.5f;   /* 0.5 */
    case WL_BLOCK_CRAFTING_TABLE: return 15.0f;
    default: return 0.0f;
    }
}

/* ------------------------------------------------------------------ */
/* tick                                                                */
/* ------------------------------------------------------------------ */

static bool can_output(const WlFurnace *f, WlItemId result)
{
    if (result == WL_ITEM_NONE) return false;
    if (f->output.item == WL_ITEM_NONE) return true;
    if (f->output.item != result) return false;
    const WlItem *def = wl_item_get(result);
    int max = def && def->max_stack > 0 ? def->max_stack : 64;
    return f->output.count < max;
}

void wl_furnaces_update(WlWorld *w, float dt)
{
    if (!w) return;
    int count = 0;
    /* the array is const from outside; mutation happens through the same
     * storage in world.c via an internal accessor */
    WlFurnace *list = wl_world_furnaces_mutable(w, &count);
    if (!list) return;

    for (int i = 0; i < count; i++) {
        WlFurnace *f = &list[i];
        if (!f->active) continue;

        WlItemId result = wl_furnace_smelt_result(f->input.item);
        bool smeltable = result != WL_ITEM_NONE && f->input.count > 0 &&
                         can_output(f, result);

        /* ignite fresh fuel when the fire is out */
        if (f->burn_left <= 0.0f && smeltable &&
            wl_furnace_fuel_time(f->fuel.item) > 0.0f && f->fuel.count > 0) {
            f->burn_total = wl_furnace_fuel_time(f->fuel.item);
            f->burn_left = f->burn_total;
            f->fuel.count--;
            if (f->fuel.count == 0) {
                f->fuel.item = WL_ITEM_NONE;
                f->fuel.dur = 0;
            }
        }

        bool burning = f->burn_left > 0.0f;
        if (burning) {
            f->burn_left -= dt;
            if (f->burn_left <= 0.0f) f->burn_left = 0.0f;
        }

        if (burning && smeltable) {
            f->progress += dt / WL_SMELT_TIME;
            if (f->progress >= 1.0f) {
                f->progress = 0.0f;
                if (can_output(f, result)) {
                    if (f->output.item == WL_ITEM_NONE) {
                        f->output.item = result;
                        f->output.count = 1;
                        f->output.dur = 0;
                    } else {
                        f->output.count++;
                    }
                    f->input.count--;
                    if (f->input.count == 0) {
                        f->input.item = WL_ITEM_NONE;
                        f->input.dur = 0;
                    }
                }
            }
        } else if (!smeltable) {
            f->progress = 0.0f; /* input removed or output blocked */
        }

        /* swap the world block between lit/unlit so the face and the
         * light emission follow the fire */
        uint8_t id = wl_world_get_block(w, f->x, f->y, f->z);
        if (f->burn_left > 0.0f && id == WL_BLOCK_FURNACE)
            wl_world_set_block(w, f->x, f->y, f->z, WL_BLOCK_FURNACE_LIT);
        else if (f->burn_left <= 0.0f && id == WL_BLOCK_FURNACE_LIT)
            wl_world_set_block(w, f->x, f->y, f->z, WL_BLOCK_FURNACE);
    }
}

/* ------------------------------------------------------------------ */
/* persistence                                                         */
/* ------------------------------------------------------------------ */

static void write_slot(WlStrBuf *sb, const char *key, const WlSlot *s)
{
    wl_sb_printf(sb, "%s = %u %u %u\\n", key, (unsigned)s->item,
                 (unsigned)s->count, (unsigned)s->dur);
}

bool wl_world_save_furnaces(const WlWorld *w)
{
    const char *dir = w ? wl_world_save_dir(w) : NULL;
    if (!dir || dir[0] == '\0') return false;
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "furnaces.dat"))
        return false;

    int count = 0;
    const WlFurnace *list = wl_world_furnaces(w, &count);
    WlStrBuf sb;
    if (!wl_sb_init(&sb, 512)) return false;
    bool ok = wl_sb_printf(&sb, "# Wildlands furnaces\\n");
    for (int i = 0; i < count && ok; i++) {
        const WlFurnace *f = &list[i];
        if (!f->active) continue;
        ok = wl_sb_printf(&sb, "furnace = %d %d %d %.2f %.2f %.3f\\n",
                          f->x, f->y, f->z, f->burn_left, f->burn_total,
                          f->progress);
        if (ok) write_slot(&sb, "  in", &f->input);
        if (ok) write_slot(&sb, "  fuel", &f->fuel);
        if (ok) write_slot(&sb, "  out", &f->output);
    }
    if (ok) ok = wl_write_file_atomic(path, sb.buf, sb.len);
    wl_sb_free(&sb);
    if (!ok) WL_LOGE("failed to write %s", path);
    return ok;
}

void wl_world_load_furnaces(WlWorld *w)
{
    const char *dir = w ? wl_world_save_dir(w) : NULL;
    if (!dir || dir[0] == '\0') return;
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "furnaces.dat")) return;
    if (!wl_file_exists(path)) return;

    FILE *fp = fopen(path, "rb");
    if (!fp) return;

    WlFurnace *f = NULL;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        int x, y, z;
        float bl, bt, pr;
        unsigned item, count, dur;
        if (sscanf(line, "furnace = %d %d %d %f %f %f",
                   &x, &y, &z, &bl, &bt, &pr) == 6) {
            f = wl_world_get_furnace(w, x, y, z, true);
            if (f) {
                f->burn_left = bl;
                f->burn_total = bt;
                f->progress = pr;
            }
        } else if (f && sscanf(line, " in = %u %u %u", &item, &count, &dur) == 3) {
            f->input = (WlSlot){ (WlItemId)item, (uint8_t)count, (uint16_t)dur };
        } else if (f && sscanf(line, " fuel = %u %u %u", &item, &count, &dur) == 3) {
            f->fuel = (WlSlot){ (WlItemId)item, (uint8_t)count, (uint16_t)dur };
        } else if (f && sscanf(line, " out = %u %u %u", &item, &count, &dur) == 3) {
            f->output = (WlSlot){ (WlItemId)item, (uint8_t)count, (uint16_t)dur };
        }
    }
    fclose(fp);
    WL_LOGI("furnaces loaded");
}
