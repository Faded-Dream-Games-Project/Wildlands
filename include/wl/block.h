/* block.h — the block registry (base game + mods). */
#ifndef WL_BLOCK_H
#define WL_BLOCK_H

#include "wl/wl_common.h"

#define WL_BLOCK_MAX 256
typedef uint8_t WlBlockId;

/* Base-game block ids. Stable: worldgen, saved chunks and mods reference them. */
enum {
    WL_BLOCK_AIR = 0,
    WL_BLOCK_STONE,
    WL_BLOCK_GRASS,
    WL_BLOCK_DIRT,
    WL_BLOCK_COBBLE,
    WL_BLOCK_PLANKS,
    WL_BLOCK_SAND,
    WL_BLOCK_SANDSTONE,
    WL_BLOCK_WATER,
    WL_BLOCK_GLASS,
    WL_BLOCK_LOG,
    WL_BLOCK_LEAVES,
    WL_BLOCK_BIRCH_LOG,
    WL_BLOCK_BIRCH_LEAVES,
    WL_BLOCK_GRAVEL,
    WL_BLOCK_COAL_ORE,
    WL_BLOCK_IRON_ORE,
    WL_BLOCK_GOLD_ORE,
    WL_BLOCK_DIAMOND_ORE,
    WL_BLOCK_BEDROCK,
    WL_BLOCK_SNOW_GRASS,
    WL_BLOCK_SNOW,
    WL_BLOCK_ICE,
    WL_BLOCK_CACTUS,
    WL_BLOCK_CRAFTING_TABLE,
    WL_BLOCK_TORCH,
    WL_BLOCK_FLOWER_RED,
    WL_BLOCK_FLOWER_YELLOW,
    WL_BLOCK_TALLGRASS,
    WL_BLOCK_COUNT_BASE
};

typedef enum {
    WL_RENDER_SOLID = 0, /* full cube */
    WL_RENDER_CROSS,     /* X-shaped plant sprite */
    WL_RENDER_LIQUID,    /* translucent cube */
} WlRenderShape;

typedef enum {
    WL_TOOL_NONE = 0,
    WL_TOOL_PICKAXE,
    WL_TOOL_AXE,
    WL_TOOL_SHOVEL,
    WL_TOOL_SWORD
} WlToolKind;

typedef struct {
    const char *name;    /* registry id, e.g. "stone" */
    const char *display; /* shown in UI, e.g. "Stone" */
    uint8_t tex[6];      /* atlas tiles: +X -X +Y(top) -Y(bottom) +Z -Z */
    WlRenderShape shape;
    bool solid;          /* collides with entities */
    bool opaque;         /* culls neighbor faces, blocks skylight */
    bool unbreakable;    /* cannot be broken in survival */
    uint8_t hardness;    /* 10 units ~ 1.0s barehand; 0 = instant */
    WlToolKind tool;     /* preferred tool */
    uint8_t tool_tier;   /* minimum tool tier required for drops */
    bool needs_tool;     /* no drops without a matching tool of tier */
    uint16_t drop_item;  /* item id dropped (WL_ITEM_NONE = nothing) */
    uint8_t drop_count;
    bool gravity;        /* falls when the block below is removed */
} WlBlock;

/* Registers base blocks. Call once at startup, before loading mods. */
void        wl_blocks_init(void);
/* Registers a block (used by base init and mods). Returns 0 on failure. */
WlBlockId  wl_blocks_register(const WlBlock *def);
/* Post-registration fixup used by the mod loader (drop item may reference
 * the block's own item id, which is only known after registration). */
void        wl_block_set_drop(WlBlockId id, uint16_t drop_item);
const WlBlock *wl_block_get(WlBlockId id);
int         wl_blocks_count(void);

/* Fast lookups for hot paths (meshing, physics). */
bool wl_block_is_opaque(WlBlockId id);
bool wl_block_is_solid(WlBlockId id);

#endif /* WL_BLOCK_H */
