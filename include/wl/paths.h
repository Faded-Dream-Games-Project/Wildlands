/* paths.h — project-root resolution and path building for all runtime data. */
#ifndef WL_PATHS_H
#define WL_PATHS_H

#include "wl/wl_common.h"

/* Resolves the project root once:
 *  1. $WILDLANDS_ROOT if it contains an assets/ directory,
 *  2. the current working directory if it contains assets/,
 *  3. walking up from the executable location (bin/, bin/../, up to 4 levels).
 * Returns false if no root with assets/ could be found. */
bool  wl_paths_init(const char *argv0);

/* Creates every runtime directory the game needs (worlds, mods, config, logs,
 * screenshots). Safe to call repeatedly. */
bool  wl_paths_ensure_dirs(void);

/* Deletes old log files, keeping the newest `keep` wildlands-*.log files. */
void  wl_paths_prune_logs(int keep);

const char *wl_paths_root(void);

/* Path builders; return false on truncation. */
bool wl_path_join(char *out, size_t cap, const char *a, const char *b);
bool wl_path_assets(char *out, size_t cap, const char *rel);
bool wl_path_config(char *out, size_t cap, const char *rel);
bool wl_path_logs(char *out, size_t cap, const char *rel);
bool wl_path_worlds(char *out, size_t cap, const char *rel);
bool wl_path_mods(char *out, size_t cap, const char *rel);
bool wl_path_screenshots(char *out, size_t cap, const char *rel);

#endif /* WL_PATHS_H */
