/* mod_api.h — PUBLIC, versioned C API for Wildlands mods.
 *
 * A mod is a directory under mods/ containing:
 *   mod.wl     manifest (key = value text file)
 *   <lib>      native shared library (see "library" in the manifest)
 *   assets/    mod-specific assets (textures, sounds)
 *
 * The library must export:
 *   int wl_mod_main(const WlApi *api, WlModInfo *out_info);
 * which fills out_info and registers content through the api vtable.
 * Return 0 on success; any other value aborts loading the mod.
 *
 * The API is forward compatible: new functions are appended to WlApi and
 * api_version is bumped. Mods compiled against an older minor version keep
 * working; a mismatched major version refuses to load.
 *
 * This header is intentionally independent of raylib so mods do not need
 * raylib headers to build. */
#ifndef WL_MOD_API_H
#define WL_MOD_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WL_API_VERSION 1

/* tool kinds (matches the engine's WlToolKind) */
enum {
    WL_MOD_TOOL_NONE = 0,
    WL_MOD_TOOL_PICKAXE = 1,
    WL_MOD_TOOL_AXE = 2,
    WL_MOD_TOOL_SHOVEL = 3,
    WL_MOD_TOOL_SWORD = 4,
};

typedef struct WlModBlockDef {
    const char *name;        /* registry id, e.g. "frost_stone" */
    const char *display;     /* UI name, e.g. "Frost Stone" */
    const char *texture;     /* png path relative to the mod directory */
    bool solid;              /* collides with entities */
    bool opaque;             /* culls neighbor faces */
    bool gravity;            /* falls when unsupported */
    bool unbreakable;        /* cannot be mined in survival */
    int hardness;            /* 10 units ~ 1.0s barehand; 0 = instant */
    int tool_kind;           /* WL_MOD_TOOL_* */
    int tool_tier;           /* minimum tool tier required for drops */
    bool needs_tool;         /* no drops without a matching tool */
    int drop_item_id;        /* 0 = drop self */
    int drop_count;
} WlModBlockDef;

typedef struct WlModItemDef {
    const char *name;
    const char *display;
    const char *texture;     /* png path relative to the mod directory */
    int max_stack;           /* 0 = default 64 */
    int food;                /* hunger restored; 0 = not edible */
    int tool_kind;           /* WL_MOD_TOOL_* */
    int tool_tier;           /* 1 wood .. 4 diamond */
    int durability;          /* 0 = not a tool */
} WlModItemDef;

typedef struct WlModRecipeDef {
    int out_item_id;
    int out_count;
    bool shapeless;
    int w, h;                /* shaped: 1..3 */
    int pattern[9];          /* shaped: item ids, 0 = must stay empty */
    int n_ing;               /* shapeless: ingredient count */
    int ingredients[9];      /* shapeless: item ids */
} WlModRecipeDef;

/* function table handed to wl_mod_main */
typedef struct WlApi {
    uint32_t api_version;

    /* logging (thread-safe) */
    void (*log_info)(const char *fmt, ...);
    void (*log_warn)(const char *fmt, ...);
    void (*log_error)(const char *fmt, ...);

    /* registration; returns the new id (block id doubles as its item id),
     * or 0/-1 on failure */
    int (*register_block)(const WlModBlockDef *def);
    int (*register_item)(const WlModItemDef *def);
    int (*register_texture)(const char *png_path); /* returns atlas tile */
    bool (*register_recipe)(const WlModRecipeDef *def);
} WlApi;

typedef struct WlModInfo {
    const char *id;
    const char *name;
    const char *version;
    const char *author;
    const char *description;
    uint32_t api_version;
} WlModInfo;

typedef int (*WlModEntryFn)(const WlApi *api, WlModInfo *out_info);

#ifdef __cplusplus
}
#endif

#endif /* WL_MOD_API_H */
