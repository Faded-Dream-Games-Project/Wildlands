/* world_store.c — reliable world persistence (atomic writes, RLE chunks). */
#include "wl/world_store.h"
#include "wl/paths.h"
#include "wl/util.h"
#include "wl/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WL_CHUNK_MAGIC "WLCH"
#define WL_CHUNK_HEADER_SIZE 16 /* magic(4) + version(4) + cx(4) + cz(4) */

bool wl_store_create_world(const char *dir, const WlWorldMeta *meta)
{
    char chunks[WL_PATH_MAX];
    if (!wl_path_join(chunks, sizeof(chunks), dir, "chunks"))
        return false;
    if (!wl_mkdir_p(chunks)) {
        WL_LOGE("cannot create world directory %s", chunks);
        return false;
    }
    return wl_store_save_level(dir, meta);
}

bool wl_store_save_level(const char *dir, const WlWorldMeta *meta)
{
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "level.dat"))
        return false;

    WlStrBuf sb;
    if (!wl_sb_init(&sb, 1024)) return false;

    bool ok = wl_sb_printf(&sb, "# Wildlands world metadata\n");
    ok = ok && wl_sb_printf(&sb, "format_version = %d\n", meta->format_version);
    ok = ok && wl_sb_printf(&sb, "name = %s\n", meta->name);
    ok = ok && wl_sb_printf(&sb, "seed = %llu\n", (unsigned long long)meta->seed);
    ok = ok && wl_sb_printf(&sb, "type = %d\n", meta->type);
    ok = ok && wl_sb_printf(&sb, "gamemode = %d\n", meta->gamemode);
    ok = ok && wl_sb_printf(&sb, "time_ticks = %u\n", (unsigned)meta->time_ticks);
    ok = ok && wl_sb_printf(&sb, "spawn_x = %.2f\n", meta->spawn_x);
    ok = ok && wl_sb_printf(&sb, "spawn_y = %.2f\n", meta->spawn_y);
    ok = ok && wl_sb_printf(&sb, "spawn_z = %.2f\n", meta->spawn_z);
    ok = ok && wl_sb_printf(&sb, "created = %lld\n", (long long)meta->created_unix);
    ok = ok && wl_sb_printf(&sb, "last_played = %lld\n", (long long)meta->last_played_unix);
    ok = ok && wl_sb_printf(&sb, "play_seconds = %.1f\n", meta->play_seconds);

    if (ok) ok = wl_write_file_atomic(path, sb.buf, sb.len);
    wl_sb_free(&sb);

    if (!ok) WL_LOGE("failed to write %s", path);
    return ok;
}

bool wl_store_load_level(const char *dir, WlWorldMeta *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->format_version = -1;

    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "level.dat"))
        return false;

    char *data = NULL;
    size_t len = 0;
    if (!wl_read_file(path, &data, &len)) return false;

    bool ok = true;
    long lv = 0;
    float fv = 0.0f;
    char key[64], val[256];
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!wl_kv_line(line, key, sizeof(key), val, sizeof(val))) continue;
        if (wl_str_eq(key, "format_version")) {
            if (wl_parse_long(val, &lv)) meta->format_version = (int)lv;
        } else if (wl_str_eq(key, "name")) {
            wl_scpy(meta->name, val, sizeof(meta->name));
        } else if (wl_str_eq(key, "seed")) {
            if (wl_parse_long(val, &lv)) meta->seed = (uint64_t)lv;
        } else if (wl_str_eq(key, "type")) {
            if (wl_parse_long(val, &lv)) meta->type = (int)lv;
        } else if (wl_str_eq(key, "gamemode")) {
            if (wl_parse_long(val, &lv)) meta->gamemode = (int)lv;
        } else if (wl_str_eq(key, "time_ticks")) {
            if (wl_parse_long(val, &lv)) meta->time_ticks = (uint32_t)lv;
        } else if (wl_str_eq(key, "spawn_x")) {
            if (wl_parse_float(val, &fv)) meta->spawn_x = fv;
        } else if (wl_str_eq(key, "spawn_y")) {
            if (wl_parse_float(val, &fv)) meta->spawn_y = fv;
        } else if (wl_str_eq(key, "spawn_z")) {
            if (wl_parse_float(val, &fv)) meta->spawn_z = fv;
        } else if (wl_str_eq(key, "created")) {
            if (wl_parse_long(val, &lv)) meta->created_unix = (int64_t)lv;
        } else if (wl_str_eq(key, "last_played")) {
            if (wl_parse_long(val, &lv)) meta->last_played_unix = (int64_t)lv;
        } else if (wl_str_eq(key, "play_seconds")) {
            if (wl_parse_float(val, &fv)) meta->play_seconds = fv;
        }
    }
    free(data);

    if (meta->format_version < 0 || meta->format_version > WL_WORLD_FORMAT_VERSION) {
        WL_LOGE("unsupported world format version %d in %s",
                meta->format_version, path);
        ok = false;
    } else if (meta->name[0] == '\0') {
        WL_LOGE("world at %s has no name; refusing to load", path);
        ok = false;
    }
    return ok;
}

bool wl_store_save_chunk(const char *dir, int cx, int cz,
                         const uint8_t blocks[WL_CHUNK_VOL])
{
    char path[WL_PATH_MAX], chunks[WL_PATH_MAX];
    if (!wl_path_join(chunks, sizeof(chunks), dir, "chunks")) return false;
    if (!wl_is_dir(chunks) && !wl_mkdir_p(chunks)) return false;
    if (snprintf(path, sizeof(path), "%s/c.%d.%d.bin", chunks, cx, cz) >= (int)sizeof(path))
        return false;

    /* worst case: 2 bytes per block */
    size_t cap = WL_CHUNK_VOL * 2 + WL_CHUNK_HEADER_SIZE;
    uint8_t *buf = malloc(cap);
    if (!buf) return false;

    memcpy(buf, WL_CHUNK_MAGIC, 4);
    uint32_t version = WL_WORLD_FORMAT_VERSION;
    uint32_t rle_len = 0;
    int32_t scx = cx, scz = cz;
    memcpy(buf + 4, &version, 4);
    memcpy(buf + 8, &scx, 4);
    memcpy(buf + 12, &scz, 4);

    size_t rle = wl_rle_encode(blocks, WL_CHUNK_VOL,
                               buf + WL_CHUNK_HEADER_SIZE, cap - WL_CHUNK_HEADER_SIZE);
    if (rle == 0) {
        WL_LOGE("RLE encode overflow for chunk %d,%d", cx, cz);
        free(buf);
        return false;
    }
    rle_len = (uint32_t)rle;
    memcpy(buf + 4, &version, 4); /* keep fields together for clarity */
    (void)rle_len;

    bool ok = wl_write_file_atomic(path, buf, WL_CHUNK_HEADER_SIZE + rle);
    free(buf);
    if (!ok) WL_LOGE("failed to save chunk %d,%d to %s", cx, cz, path);
    return ok;
}

bool wl_store_load_chunk(const char *dir, int cx, int cz,
                         uint8_t blocks[WL_CHUNK_VOL])
{
    char path[WL_PATH_MAX], chunks[WL_PATH_MAX];
    if (!wl_path_join(chunks, sizeof(chunks), dir, "chunks")) return false;
    if (snprintf(path, sizeof(path), "%s/c.%d.%d.bin", chunks, cx, cz) >= (int)sizeof(path))
        return false;
    if (!wl_file_exists(path)) return false; /* not modified: regenerate */

    char *data = NULL;
    size_t len = 0;
    if (!wl_read_file(path, &data, &len) || len < WL_CHUNK_HEADER_SIZE) {
        WL_LOGW("corrupt chunk file %s; regenerating", path);
        free(data);
        return false;
    }

    bool ok = false;
    do {
        if (memcmp(data, WL_CHUNK_MAGIC, 4) != 0) break;
        uint32_t version = 0;
        int32_t fcx = 0, fcz = 0;
        memcpy(&version, data + 4, 4);
        memcpy(&fcx, data + 8, 4);
        memcpy(&fcz, data + 12, 4);
        if ((int)version != WL_WORLD_FORMAT_VERSION || fcx != cx || fcz != cz) break;

        size_t got = wl_rle_decode((const uint8_t *)data + WL_CHUNK_HEADER_SIZE,
                                   len - WL_CHUNK_HEADER_SIZE, blocks, WL_CHUNK_VOL);
        if (got != WL_CHUNK_VOL) break;
        ok = true;
    } while (0);

    if (!ok) WL_LOGW("chunk file %s failed validation; regenerating", path);
    free(data);
    return ok;
}
