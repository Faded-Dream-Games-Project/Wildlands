/* world_store.h — on-disk persistence for a single world.
 *
 * Layout of worlds/<name>/:
 *   level.dat          world metadata (text key=value)
 *   player.dat         player state (written by the player save module)
 *   chunks/c.<cx>.<cz>.bin   one file per modified chunk (RLE-compressed)
 *
 * Only chunks the player modified are stored; everything else regenerates
 * deterministically from the seed. All writes are atomic (tmp + rename). */
#ifndef WL_WORLD_STORE_H
#define WL_WORLD_STORE_H

#include "wl/wl_common.h"
#include "wl/chunk.h"

typedef struct {
    char name[WL_NAME_MAX];   /* display name */
    uint64_t seed;
    int type;                 /* WlWorldType */
    int gamemode;             /* WL_WORLD_GAMEMODE_SURVIVAL / _CREATIVE */
    uint32_t time_ticks;      /* 0..24000, 20 ticks per second */
    float spawn_x, spawn_y, spawn_z;
    int64_t created_unix;
    int64_t last_played_unix;
    double play_seconds;
    int format_version;
} WlWorldMeta;

/* Creates the directory structure for a new world. */
bool wl_store_create_world(const char *dir, const WlWorldMeta *meta);
bool wl_store_save_level(const char *dir, const WlWorldMeta *meta);
/* Returns false when level.dat is missing or unreadable. */
bool wl_store_load_level(const char *dir, WlWorldMeta *meta);

/* Chunk persistence. load returns false when no file exists (regenerate) or
 * the file is corrupt (treated as missing, logged). */
bool wl_store_save_chunk(const char *dir, int cx, int cz,
                         const uint8_t blocks[WL_CHUNK_VOL]);
bool wl_store_load_chunk(const char *dir, int cx, int cz,
                         uint8_t blocks[WL_CHUNK_VOL]);

#endif /* WL_WORLD_STORE_H */
