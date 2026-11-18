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
    WL_BLOCK_FURNACE,
    WL_BLOCK_FURNACE_LIT,
    WL_BLOCK_COUNT_BASE
};

/* Flowing water levels live at FIXED high ids (registered like base
 * blocks but not mirrored into the item registry) so that mod block
 * ids -- which start at WL_BLOCK_COUNT_BASE -- never shift. Level 7
 * (strongest) is id 246 down to level 1 (last spread step) at 240. */
#define WL_BLOCK_WATER_FLOW_FIRST 240
static inline uint8_t wl_water_flow_id(int level)
{
    return (uint8_t)(WL_BLOCK_WATER_FLOW_FIRST - 1 + level); /* 1..7 */
}
static inline int wl_water_flow_level(uint8_t id)
{
    return (id >= WL_BLOCK_WATER_FLOW_FIRST && id < WL_BLOCK_WATER_FLOW_FIRST + 7)
               ? (int)id - (WL_BLOCK_WATER_FLOW_FIRST - 1)
               : 0;
}

/* true for source and flowing water alike */
bool wl_blocks_liquid(uint8_t id);

/* Farming blocks live at FIXED high ids (like the water flows) so mod
 * block ids and old saves never shift. Wheat stages count DOWN: the
 * mature stage is 231, sprout is 233, so growing is id - 1. */
#define WL_BLOCK_FARMLAND     234
#define WL_BLOCK_WHEAT_0      233 /* sprout */
#define WL_BLOCK_WHEAT_1      232 /* mid */
#define WL_BLOCK_WHEAT_2      231 /* mature: drops wheat + seeds */
static inline int wl_wheat_stage(uint8_t id)
{
    return (id >= WL_BLOCK_WHEAT_2 && id <= WL_BLOCK_WHEAT_0)
               ? WL_BLOCK_WHEAT_0 - (int)id + 1 : 0; /* 1..3, 0 = none */
}
static inline uint8_t wl_wheat_id(int stage) /* stage 1..3 */
{
    return (uint8_t)(WL_BLOCK_WHEAT_0 - (stage - 1));
}
bool wl_blocks_wheat(uint8_t id);

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
    WL_TOOL_HOE,
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
    uint8_t light;       /* light emission 0..15 (torch 14, lit furnace 13) */
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
