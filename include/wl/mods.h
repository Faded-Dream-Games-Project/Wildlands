/* mods.h — mod discovery, loading and lifecycle. */
#ifndef WL_MODS_H
#define WL_MODS_H

#include "wl/wl_common.h"
#include "wl/mod_api.h"

struct WlAssets;

#define WL_MODS_MAX 32

typedef enum {
    WL_MOD_LOADED = 0,
    WL_MOD_DISABLED,
    WL_MOD_ERROR,
    WL_MOD_MISSING_DEP,
    WL_MOD_BAD_MANIFEST,
    WL_MOD_DUPLICATE,
} WlModStatus;

typedef struct {
    char dir[WL_NAME_MAX];
    char id[64], name[64], version[32], author[64], description[192];
    char library[128];
    char depends[192];        /* comma-separated mod ids */
    bool enabled;
    int api_version_seen;     /* from the manifest */
    WlModStatus status;
    char error[160];          /* human-readable failure reason */
    void *handle;             /* dlopen handle */
} WlMod;

typedef struct {
    WlMod mods[WL_MODS_MAX];
    int count;
    WlApi api;                /* vtable handed to mods */
    char root[WL_PATH_MAX];   /* mods/ directory */
    struct WlAssets *assets;
} WlMods;

/* Scans mods/, reads manifests, resolves dependencies and loads libraries.
 * Must run after wl_items_init() and before wl_assets_finalize_atlas(). */
bool wl_mods_init(WlMods *m, struct WlAssets *assets, const char *root_dir);
void wl_mods_shutdown(WlMods *m);

/* Flips the enabled flag and rewrites the manifest (applies next start). */
bool wl_mods_set_enabled(WlMods *m, int index, bool enabled);

const char *wl_mod_status_name(WlModStatus s);
int wl_mods_loaded_count(const WlMods *m);

#endif /* WL_MODS_H */
