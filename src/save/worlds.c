/* worlds.c — world directory management and session save/load. */
#include "wl/worlds.h"
#include "wl/world.h"
#include "wl/entities.h"
#include "wl/player.h"
#include "wl/paths.h"
#include "wl/util.h"
#include "wl/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void sanitize_name(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    for (const char *p = src; *p && o + 1 < cap; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-')
            dst[o++] = c;
        else
            dst[o++] = '_';
    }
    dst[o] = '\0';
    /* trim */
    size_t len = strlen(dst);
    while (len > 0 && dst[len - 1] == ' ') { dst[--len] = '\0'; }
    if (len == 0) wl_scpy(dst, "World", cap);
    if (len > 48) dst[48] = '\0'; /* keep directory names short-ish */
}

static uint64_t random_seed(void)
{
    uint64_t s = (uint64_t)time(NULL) ^ ((uint64_t)clock() << 20);
    s ^= (uint64_t)(uintptr_t)&s; /* ASLR entropy */
    s *= 0x9E3779B97F4A7C15ull;
    s ^= s >> 29;
    return s | 1ull;
}

static int entry_cmp(const void *a, const void *b)
{
    const WlWorldEntry *ea = a, *eb = b;
    if (eb->meta.last_played_unix != ea->meta.last_played_unix)
        return (eb->meta.last_played_unix > ea->meta.last_played_unix) ? 1 : -1;
    return strcmp(ea->dir_name, eb->dir_name);
}

/* ------------------------------------------------------------------ */
/* listing / paths                                                     */
/* ------------------------------------------------------------------ */

bool wl_worlds_dir_path(char *out, size_t cap, const char *dir_name)
{
    char worlds[WL_PATH_MAX];
    if (!wl_path_worlds(worlds, sizeof(worlds), "")) return false;
    return wl_path_join(out, cap, worlds, dir_name);
}

int wl_worlds_list(WlWorldEntry *out, int cap)
{
    char worlds[WL_PATH_MAX];
    if (!wl_path_worlds(worlds, sizeof(worlds), "")) return 0;

    DIR *d = opendir(worlds);
    if (!d) return 0;

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < cap) {
        if (e->d_name[0] == '.') continue;
        char dir[WL_PATH_MAX];
        if (!wl_path_join(dir, sizeof(dir), worlds, e->d_name)) continue;
        if (!wl_is_dir(dir)) continue;

        WlWorldMeta meta;
        if (!wl_store_load_level(dir, &meta)) continue; /* not a Wildlands world */

        wl_scpy(out[count].dir_name, e->d_name, sizeof(out[count].dir_name));
        out[count].meta = meta;
        count++;
    }
    closedir(d);

    qsort(out, (size_t)count, sizeof(out[0]), entry_cmp);
    return count;
}

bool wl_worlds_create(const char *display_name, uint64_t seed, int type,
                      int gamemode, WlWorldEntry *out)
{
    char clean[WL_NAME_MAX];
    sanitize_name(clean, sizeof(clean), display_name);

    char worlds[WL_PATH_MAX];
    if (!wl_path_worlds(worlds, sizeof(worlds), "")) return false;

    /* find a free directory name */
    char dir_name[WL_NAME_MAX];
    char candidate[WL_NAME_MAX * 2];
    wl_scpy(dir_name, clean, sizeof(dir_name));
    for (int attempt = 2; attempt < 100; attempt++) {
        char probe[WL_PATH_MAX];
        if (!wl_path_join(probe, sizeof(probe), worlds, dir_name)) return false;
        if (!wl_is_dir(probe)) break;
        snprintf(candidate, sizeof(candidate), "%s %d", clean, attempt);
        wl_scpy(dir_name, candidate, sizeof(dir_name));
    }

    char dir[WL_PATH_MAX];
    if (!wl_path_join(dir, sizeof(dir), worlds, dir_name)) return false;

    WlWorldMeta meta;
    memset(&meta, 0, sizeof(meta));
    wl_scpy(meta.name, clean, sizeof(meta.name));
    meta.seed = seed ? seed : random_seed();
    meta.type = type;
    meta.gamemode = gamemode;
    meta.time_ticks = 1000; /* morning */
    meta.created_unix = wl_time_unix();
    meta.last_played_unix = meta.created_unix;
    meta.play_seconds = 0.0;
    meta.format_version = WL_WORLD_FORMAT_VERSION;

    if (!wl_store_create_world(dir, &meta)) return false;

    wl_scpy(out->dir_name, dir_name, sizeof(out->dir_name));
    out->meta = meta;
    WL_LOGI("created world '%s' (seed %llu)", meta.name, (unsigned long long)meta.seed);
    return true;
}

bool wl_worlds_delete(const char *dir_name)
{
    char dir[WL_PATH_MAX];
    if (!wl_worlds_dir_path(dir, sizeof(dir), dir_name)) return false;
    WL_LOGI("deleting world directory %s", dir);
    return wl_delete_tree(dir);
}

/* ------------------------------------------------------------------ */
/* session persistence                                                 */
/* ------------------------------------------------------------------ */

bool wl_worlds_save_session(WlWorld *w, const WlPlayer *p, Vector3 spawn,
                            double session_seconds)
{
    if (!w) return false;
    const char *dir = wl_world_save_dir(w);
    if (dir[0] == '\0') return false;

    WlWorldMeta meta;
    memset(&meta, 0, sizeof(meta));
    if (!wl_store_load_level(dir, &meta)) {
        /* first save of a fresh world */
        wl_scpy(meta.name, wl_world_name(w), sizeof(meta.name));
        meta.seed = wl_world_gen(w)->seed;
        meta.type = wl_world_gen(w)->type;
        meta.gamemode = wl_world_gamemode(w);
        meta.created_unix = wl_time_unix();
        meta.format_version = WL_WORLD_FORMAT_VERSION;
    }

    meta.seed = wl_world_gen(w)->seed;
    meta.type = wl_world_gen(w)->type;
    meta.gamemode = wl_world_gamemode(w);
    meta.time_ticks = wl_world_time(w);
    meta.spawn_x = spawn.x;
    meta.spawn_y = spawn.y;
    meta.spawn_z = spawn.z;
    meta.last_played_unix = wl_time_unix();
    meta.play_seconds += session_seconds;
    meta.format_version = WL_WORLD_FORMAT_VERSION;

    bool ok = wl_store_save_level(dir, &meta);
    ok = wl_world_save_modified(w) && ok;
    ok = wl_world_save_drops(w) && ok; /* death drops persist with the session */
    ok = wl_world_save_entities(w) && ok; /* mobs + animals persist too */
    ok = wl_world_save_furnaces(w) && ok; /* furnace contents + progress */

    /* player.dat */
    if (p) {
        char path[WL_PATH_MAX];
        if (!wl_path_join(path, sizeof(path), dir, "player.dat")) return false;
        WlStrBuf sb;
        if (!wl_sb_init(&sb, 2048)) return false;
        bool pok = wl_sb_printf(&sb, "# Wildlands player data\n");
        pok = pok && wl_sb_printf(&sb, "x = %.3f\n", p->pos.x);
        pok = pok && wl_sb_printf(&sb, "y = %.3f\n", p->pos.y);
        pok = pok && wl_sb_printf(&sb, "z = %.3f\n", p->pos.z);
        pok = pok && wl_sb_printf(&sb, "yaw = %.4f\n", p->yaw);
        pok = pok && wl_sb_printf(&sb, "pitch = %.4f\n", p->pitch);
        pok = pok && wl_sb_printf(&sb, "health = %.1f\n", p->health);
        pok = pok && wl_sb_printf(&sb, "hunger = %.1f\n", p->hunger);
        pok = pok && wl_sb_printf(&sb, "air = %.1f\n", p->air);
        pok = pok && wl_sb_printf(&sb, "selected = %d\n", p->selected_slot);
        for (int i = 0; i < WL_INV_SIZE; i++) {
            const WlSlot *s = &p->inv.slots[i];
            if (s->item == WL_ITEM_NONE) continue;
            pok = pok && wl_sb_printf(&sb, "inv.%d = %d:%d:%d\n", i,
                                      (int)s->item, (int)s->count, (int)s->dur);
        }
        /* worn armor */
        for (int i = 0; i < 4; i++) {
            const WlSlot *s = &p->armor[i];
            if (s->item == WL_ITEM_NONE) continue;
            pok = pok && wl_sb_printf(&sb, "armor.%d = %d:%d:%d\n", i,
                                      (int)s->item, (int)s->count, (int)s->dur);
        }
        /* crafting grid leftovers */
        for (int i = 0; i < WL_CRAFT_GRID_MAX; i++) {
            const WlSlot *s = &p->inv.craft[i];
            if (s->item == WL_ITEM_NONE) continue;
            pok = pok && wl_sb_printf(&sb, "craft.%d = %d:%d:%d\n", i,
                                      (int)s->item, (int)s->count, (int)s->dur);
        }
        if (pok) pok = wl_write_file_atomic(path, sb.buf, sb.len);
        wl_sb_free(&sb);
        ok = pok && ok;
    }

    WL_LOGI("session saved (%s)", meta.name);
    return ok;
}

bool wl_worlds_load_player(const char *dir_name, WlPlayer *p, bool *out_found)
{
    *out_found = false;
    char dir[WL_PATH_MAX];
    if (!wl_worlds_dir_path(dir, sizeof(dir), dir_name)) return false;

    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "player.dat")) return false;
    if (!wl_file_exists(path)) return true; /* no save: keep fresh spawn */

    char *data = NULL;
    size_t len = 0;
    if (!wl_read_file(path, &data, &len)) return false;

    char key[64], val[128];
    char *save = NULL;
    float fv = 0;
    long lv = 0;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!wl_kv_line(line, key, sizeof(key), val, sizeof(val))) continue;
        if (wl_str_eq(key, "x") && wl_parse_float(val, &fv)) p->pos.x = fv;
        else if (wl_str_eq(key, "y") && wl_parse_float(val, &fv)) p->pos.y = fv;
        else if (wl_str_eq(key, "z") && wl_parse_float(val, &fv)) p->pos.z = fv;
        else if (wl_str_eq(key, "yaw") && wl_parse_float(val, &fv)) p->yaw = fv;
        else if (wl_str_eq(key, "pitch") && wl_parse_float(val, &fv)) p->pitch = fv;
        else if (wl_str_eq(key, "health") && wl_parse_float(val, &fv)) p->health = fv;
        else if (wl_str_eq(key, "hunger") && wl_parse_float(val, &fv)) p->hunger = fv;
        else if (wl_str_eq(key, "air") && wl_parse_float(val, &fv)) p->air = fv;
        else if (wl_str_eq(key, "selected") && wl_parse_long(val, &lv))
            p->selected_slot = wl_clampi((int)lv, 0, WL_HOTBAR_SIZE - 1);
        else if (wl_str_starts_with(key, "armor.")) {
            int idx = -1;
            if (wl_parse_long(key + 6, &lv)) idx = (int)lv;
            if (idx >= 0 && idx < 4) {
                int item = 0, count = 0, dur = 0;
                if (sscanf(val, "%d:%d:%d", &item, &count, &dur) == 3 &&
                    item > 0 && count > 0 && wl_item_valid((WlItemId)item)) {
                    const WlItem *def = wl_item_get((WlItemId)item);
                    if (def && def->armor_slot == idx + 1) {
                        p->armor[idx].item = (uint16_t)item;
                        p->armor[idx].count = (uint8_t)(count > 1 ? 1 : count);
                        p->armor[idx].dur = (uint16_t)dur;
                    }
                }
            }
        }
        else if (wl_str_starts_with(key, "inv.") || wl_str_starts_with(key, "craft.")) {
            int idx = -1;
            if (wl_parse_long(key + (key[0] == 'i' ? 4 : 6), &lv)) idx = (int)lv;
            bool craft = (key[0] == 'c');
            if (idx >= 0 && idx < (craft ? WL_CRAFT_GRID_MAX : WL_INV_SIZE)) {
                int item = 0, count = 0, dur = 0;
                if (sscanf(val, "%d:%d:%d", &item, &count, &dur) == 3 &&
                    item > 0 && count > 0 && wl_item_valid((WlItemId)item)) {
                    WlSlot *s = craft ? &p->inv.craft[idx] : &p->inv.slots[idx];
                    s->item = (uint16_t)item;
                    s->count = (uint8_t)(count > 64 ? 64 : count);
                    s->dur = (uint16_t)dur;
                }
            }
        }
    }
    free(data);
    *out_found = true;
    WL_LOGI("loaded player data from %s", path);
    return true;
}
