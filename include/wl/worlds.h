/* worlds.h — world list management and session persistence. */
#ifndef WL_WORLDS_H
#define WL_WORLDS_H

#include "wl/wl_common.h"
#include "wl/world_store.h"

struct WlWorld;
struct WlPlayer;

typedef struct {
    char dir_name[WL_NAME_MAX]; /* directory name under worlds/ */
    WlWorldMeta meta;
} WlWorldEntry;

/* Scans worlds/ and returns entries sorted by last-played (newest first).
 * Returns the number of valid worlds found. */
int  wl_worlds_list(WlWorldEntry *out, int cap);
bool wl_worlds_dir_path(char *out, size_t cap, const char *dir_name);

/* Creates a new world directory with a unique name derived from the display
 * name. seed == 0 generates a random seed. */
bool wl_worlds_create(const char *display_name, uint64_t seed, int type,
                      int gamemode, WlWorldEntry *out);
bool wl_worlds_delete(const char *dir_name);

/* Writes level.dat, player.dat and all modified chunks. */
bool wl_worlds_save_session(struct WlWorld *w, const struct WlPlayer *p,
                            Vector3 spawn, double session_seconds);
/* Loads player.dat into p; *out_found is false when no save exists. */
bool wl_worlds_load_player(const char *dir_name, struct WlPlayer *p, bool *out_found);

#endif /* WL_WORLDS_H */
