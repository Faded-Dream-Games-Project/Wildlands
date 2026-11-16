# Wildlands Modding Guide

Wildlands is designed around modding. Mods are **native C shared libraries**
that register content through a **versioned C API** — no scripting language,
no bytecode, full native performance, full engine access.

A working example lives in [`mods/example_mod/`](../mods/example_mod/) and is
built by `make mods`.

---

## 1. Anatomy of a mod

```
mods/
  my-mod/
    mod.wl                    manifest (required)
    mod.so                    shared library (name from the manifest)
    assets/
      textures/my_block.png   16x16 textures (RGBA, transparent supported)
```

### The manifest (`mod.wl`)

A plain `key = value` file:

```ini
id = my-mod            # unique id (required)
name = My Mod          # display name (required)
version = 1.0.0
author = You
description = Adds a thing.
api = 1                # WL_API_VERSION the mod was built against (required)
library = mod.so       # library file inside the mod directory (required)
depends = other-mod    # comma-separated mod ids (optional)
enabled = true         # flipped by the in-game Mods menu
```

## 2. The mod entry point

Every mod exports exactly one function:

```c
#include "wl/mod_api.h"

int wl_mod_main(const WlApi *api, WlModInfo *out_info)
{
    out_info->id = "my-mod";
    out_info->name = "My Mod";
    out_info->version = "1.0.0";
    out_info->author = "You";
    out_info->description = "Adds a thing.";
    out_info->api_version = WL_API_VERSION;

    /* register content through api->... (see below) */
    return 0; /* 0 = success; anything else aborts loading this mod */
}
```

Build it as a shared library:

```sh
cc -std=c11 -fPIC -shared -Iinclude -o mods/my-mod/mod.so my_mod.c
```

## 3. The API (version 1)

`wl/mod_api.h` is the complete public API. It deliberately does not include
raylib, so mods build without raylib headers.

### 3.1 Registering blocks

```c
int id = api->register_block(&(WlModBlockDef){
    .name       = "frost_stone",          /* registry id (required) */
    .display    = "Frost Stone",          /* UI name (required) */
    .texture    = "assets/textures/frost_stone.png", /* 16x16 png (required) */
    .solid      = true,                   /* collides with players */
    .opaque     = true,                   /* culls neighbor faces */
    .gravity    = false,                  /* falls when unsupported */
    .unbreakable = false,
    .hardness   = 18,                     /* 10 units ~ 1.0s barehand */
    .tool_kind  = WL_MOD_TOOL_PICKAXE,    /* preferred tool */
    .tool_tier  = 2,                      /* min tier for drops (1 wood..4 diamond) */
    .needs_tool = true,                   /* no drops without the right tool */
    .drop_item_id = 0,                    /* 0 = drops itself */
    .drop_count = 1,
});
```

Every registered block automatically gets a **placeable item with the same
id**, so it can be held, crafted with and placed.

### 3.2 Registering items

```c
int gem = api->register_item(&(WlModItemDef){
    .name = "glacial_shard", .display = "Glacial Shard",
    .texture = "assets/textures/shard.png",
    .max_stack = 16,
    .food = 0,                 /* hunger restored, 0 = not edible */
    .tool_kind = WL_MOD_TOOL_NONE,
    .tool_tier = 0, .durability = 0,
});
```

### 3.3 Registering recipes

```c
/* shaped: a 2x2 pattern that may sit anywhere in the grid and matches
   mirrored; 0 = the cell must be empty */
api->register_recipe(&(WlModRecipeDef){
    .out_item_id = brick_id, .out_count = 4,
    .w = 2, .h = 2,
    .pattern = { stone_id, stone_id, stone_id, stone_id },
});

/* shapeless: ingredient multiset in any arrangement */
api->register_recipe(&(WlModRecipeDef){
    .shapeless = true,
    .out_item_id = gem_id, .out_count = 1,
    .n_ing = 3,
    .ingredients = { stone_id, coal_id, coal_id },
});
```

### 3.4 Textures

`api->register_texture(path)` loads a png (relative to your mod directory)
into the global block atlas and returns its tile index. `register_block` and
`register_item` call this for you; use it directly only if you need the raw
tile index. The atlas has room for 200 mod tiles.

### 3.5 Logging

```c
api->log_info("hello from %s", "my mod");
api->log_warn(...);
api->log_error(...);
```

Messages appear on the console and in `logs/wildlands-*.log` tagged `[mod]`.

## 4. Dependencies

`depends = base-mod, extra-mod` in the manifest. The loader resolves the
dependency graph at startup:

- Dependencies load before their dependents.
- A missing dependency disables your mod with status **Missing dependency**.
- Circular dependencies are detected and reported.

## 5. Failure handling

The engine is defensive: a mod that crashes its `wl_mod_main`, registers
invalid data, or targets a different API version is **skipped with a logged
reason** — the base game keeps running. Strings you pass to the registry are
copied, so nothing in the engine points into your library's memory.

## 6. Versioning

- `WL_API_VERSION` is checked against the `api =` manifest key *and* the
  `WlModInfo::api_version` your entry reports.
- A mismatch refuses to load (no undefined behavior from ABI drift).
- The API grows by appending functions to `WlApi`; the version is bumped on
  every breaking change. Mods compiled against an older header that is still
  compatible keep working because the vtable is passed **to** you — the
  engine fills it.

## 7. Limits (current)

| Limit | Value |
| --- | --- |
| Blocks (base + mods) | 256 total (29 base blocks) |
| Items | 512 total |
| Recipes | 128 total |
| Mods installed | 32 |
| Atlas tiles for mods | 200 |

## 8. Roadmap for the API

Planned for future versions (append-only, versioned):
world generation hooks (biomes, structures, ores), custom entities,
sound/music registration, keybind-adjacent mod settings, commands,
dimension registration. See [ROADMAP.md](ROADMAP.md).
