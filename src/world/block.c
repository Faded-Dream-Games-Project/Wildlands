/* block.c — base block registry. Mods add entries via wl_blocks_register. */
#include "wl/block.h"
#include "wl/item.h"
#include "wl/tiles.h"
#include "wl/log.h"

#include <string.h>

static WlBlock g_blocks[WL_BLOCK_MAX];
static int g_block_count = 0;
static bool g_opaque[WL_BLOCK_MAX];
static bool g_solid[WL_BLOCK_MAX];
static bool g_blocks_initialized = false;

/* WlBlockId is uint8_t, so ids are always < WL_ARRAY_COUNT(g_opaque). */
bool wl_block_is_opaque(WlBlockId id) { return g_opaque[id]; }
bool wl_block_is_solid(WlBlockId id)  { return g_solid[id]; }

WlBlockId wl_blocks_register(const WlBlock *def)
{
    if (!def || !def->name || def->name[0] == '\0') {
        WL_LOGE("block registration rejected: missing name");
        return 0;
    }
    if (g_block_count >= WL_BLOCK_MAX) {
        WL_LOGE("block registry full, cannot register '%s'", def->name);
        return 0;
    }
    WlBlockId id = (WlBlockId)g_block_count;
    g_blocks[id] = *def;
    g_opaque[id] = def->opaque;
    g_solid[id] = def->solid;
    g_block_count++;
    WL_LOGD("registered block #%d '%s'", (int)id, def->name);
    return id;
}

void wl_block_set_drop(WlBlockId id, uint16_t drop_item)
{
    if (id == 0 || id >= (WlBlockId)g_block_count) return;
    g_blocks[id].drop_item = drop_item;
}

const WlBlock *wl_block_get(WlBlockId id)
{
    if (id == 0 || id >= g_block_count) return NULL;
    return &g_blocks[id];
}

int wl_blocks_count(void)
{
    return g_block_count;
}

static void reg(WlBlockId expect, WlBlock def)
{
    WlBlockId got = wl_blocks_register(&def);
    if (got != expect)
        WL_LOGE("block id mismatch: expected %d, got %d ('%s')",
                (int)expect, (int)got, def.name);
}

#define T6(t) { (t), (t), (t), (t), (t), (t) }
/* +X -X top bottom +Z -Z for blocks with distinct top/side/bottom */
#define T6T(side, top, bottom) { (side), (side), (top), (bottom), (side), (side) }
#define T6T4(front, side, top) { (front), (front), (top), (top), (front), (front) }

void wl_blocks_init(void)
{
    if (g_blocks_initialized) {
        WL_LOGW("wl_blocks_init called twice; ignored");
        return;
    }
    memset(g_blocks, 0, sizeof(g_blocks));
    memset(g_opaque, 0, sizeof(g_opaque));
    memset(g_solid, 0, sizeof(g_solid));
    g_block_count = 0;

    reg(WL_BLOCK_AIR, (WlBlock){
        .name = "air", .display = "Air", .tex = T6(0),
        .shape = WL_RENDER_SOLID, .solid = false, .opaque = false,
    });

    reg(WL_BLOCK_STONE, (WlBlock){
        .name = "stone", .display = "Stone", .tex = T6(TILE_STONE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 15, .tool = WL_TOOL_PICKAXE, .tool_tier = 1, .needs_tool = true,
        .drop_item = WL_BLOCK_COBBLE, .drop_count = 1,
    });

    reg(WL_BLOCK_GRASS, (WlBlock){
        .name = "grass_block", .display = "Grass Block",
        .tex = T6T(TILE_GRASS_SIDE, TILE_GRASS_TOP, TILE_DIRT),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 6, .tool = WL_TOOL_SHOVEL,
        .drop_item = WL_BLOCK_DIRT, .drop_count = 1,
    });

    reg(WL_BLOCK_DIRT, (WlBlock){
        .name = "dirt", .display = "Dirt", .tex = T6(TILE_DIRT),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 5, .tool = WL_TOOL_SHOVEL,
        .drop_item = WL_BLOCK_DIRT, .drop_count = 1,
    });

    reg(WL_BLOCK_COBBLE, (WlBlock){
        .name = "cobblestone", .display = "Cobblestone", .tex = T6(TILE_COBBLE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 16, .tool = WL_TOOL_PICKAXE, .tool_tier = 1, .needs_tool = true,
        .drop_item = WL_BLOCK_COBBLE, .drop_count = 1,
    });

    reg(WL_BLOCK_PLANKS, (WlBlock){
        .name = "planks", .display = "Wood Planks", .tex = T6(TILE_PLANKS),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 10, .tool = WL_TOOL_AXE,
        .drop_item = WL_BLOCK_PLANKS, .drop_count = 1,
    });

    reg(WL_BLOCK_SAND, (WlBlock){
        .name = "sand", .display = "Sand", .tex = T6(TILE_SAND),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 5, .tool = WL_TOOL_SHOVEL, .gravity = true,
        .drop_item = WL_BLOCK_SAND, .drop_count = 1,
    });

    reg(WL_BLOCK_SANDSTONE, (WlBlock){
        .name = "sandstone", .display = "Sandstone",
        .tex = T6T(TILE_SANDSTONE, TILE_SANDSTONE, TILE_SANDSTONE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 14, .tool = WL_TOOL_PICKAXE, .tool_tier = 1, .needs_tool = true,
        .drop_item = WL_BLOCK_SANDSTONE, .drop_count = 1,
    });

    reg(WL_BLOCK_WATER, (WlBlock){
        .name = "water", .display = "Water", .tex = T6(TILE_WATER),
        .shape = WL_RENDER_LIQUID, .solid = false, .opaque = false,
        .unbreakable = true,
    });

    reg(WL_BLOCK_GLASS, (WlBlock){
        .name = "glass", .display = "Glass", .tex = T6(TILE_GLASS),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = false,
        .hardness = 3, .drop_item = WL_ITEM_NONE,
    });

    reg(WL_BLOCK_LOG, (WlBlock){
        .name = "log", .display = "Oak Log",
        .tex = T6T(TILE_LOG_SIDE, TILE_LOG_TOP, TILE_LOG_TOP),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 12, .tool = WL_TOOL_AXE,
        .drop_item = WL_BLOCK_LOG, .drop_count = 1,
    });

    reg(WL_BLOCK_LEAVES, (WlBlock){
        .name = "leaves", .display = "Oak Leaves", .tex = T6(TILE_LEAVES),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = false,
        .hardness = 2,
        .drop_item = WL_ITEM_NONE,
    });

    reg(WL_BLOCK_BIRCH_LOG, (WlBlock){
        .name = "birch_log", .display = "Birch Log",
        .tex = T6T(TILE_BIRCH_LOG_SIDE, TILE_BIRCH_LOG_TOP, TILE_BIRCH_LOG_TOP),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 12, .tool = WL_TOOL_AXE,
        .drop_item = WL_BLOCK_BIRCH_LOG, .drop_count = 1,
    });

    reg(WL_BLOCK_BIRCH_LEAVES, (WlBlock){
        .name = "birch_leaves", .display = "Birch Leaves", .tex = T6(TILE_BIRCH_LEAVES),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = false,
        .hardness = 2, .drop_item = WL_ITEM_NONE,
    });

    reg(WL_BLOCK_GRAVEL, (WlBlock){
        .name = "gravel", .display = "Gravel", .tex = T6(TILE_GRAVEL),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 6, .tool = WL_TOOL_SHOVEL, .gravity = true,
        .drop_item = WL_BLOCK_GRAVEL, .drop_count = 1,
    });

    reg(WL_BLOCK_COAL_ORE, (WlBlock){
        .name = "coal_ore", .display = "Coal Ore", .tex = T6(TILE_COAL_ORE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 18, .tool = WL_TOOL_PICKAXE, .tool_tier = 1, .needs_tool = true,
        .drop_item = WL_ITEM_COAL, .drop_count = 1,
    });

    reg(WL_BLOCK_IRON_ORE, (WlBlock){
        .name = "iron_ore", .display = "Iron Ore", .tex = T6(TILE_IRON_ORE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 20, .tool = WL_TOOL_PICKAXE, .tool_tier = 2, .needs_tool = true,
        .drop_item = WL_ITEM_RAW_IRON, .drop_count = 1,
    });

    reg(WL_BLOCK_GOLD_ORE, (WlBlock){
        .name = "gold_ore", .display = "Gold Ore", .tex = T6(TILE_GOLD_ORE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 20, .tool = WL_TOOL_PICKAXE, .tool_tier = 3, .needs_tool = true,
        .drop_item = WL_ITEM_RAW_GOLD, .drop_count = 1,
    });

    reg(WL_BLOCK_DIAMOND_ORE, (WlBlock){
        .name = "diamond_ore", .display = "Diamond Ore", .tex = T6(TILE_DIAMOND_ORE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 22, .tool = WL_TOOL_PICKAXE, .tool_tier = 3, .needs_tool = true,
        .drop_item = WL_ITEM_DIAMOND, .drop_count = 1,
    });

    reg(WL_BLOCK_BEDROCK, (WlBlock){
        .name = "bedrock", .display = "Bedrock", .tex = T6(TILE_BEDROCK),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .unbreakable = true, .hardness = 255,
    });

    reg(WL_BLOCK_SNOW_GRASS, (WlBlock){
        .name = "snowy_grass", .display = "Snowy Grass",
        .tex = T6T(TILE_GRASS_SNOW_SIDE, TILE_SNOW, TILE_DIRT),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 6, .tool = WL_TOOL_SHOVEL,
        .drop_item = WL_BLOCK_DIRT, .drop_count = 1,
    });

    reg(WL_BLOCK_SNOW, (WlBlock){
        .name = "snow_block", .display = "Snow Block", .tex = T6(TILE_SNOW),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 3, .tool = WL_TOOL_SHOVEL,
        .drop_item = WL_BLOCK_SNOW, .drop_count = 1,
    });

    reg(WL_BLOCK_ICE, (WlBlock){
        .name = "ice", .display = "Ice", .tex = T6(TILE_ICE),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = false,
        .hardness = 5, .tool = WL_TOOL_PICKAXE, .drop_item = WL_ITEM_NONE,
    });

    reg(WL_BLOCK_CACTUS, (WlBlock){
        .name = "cactus", .display = "Cactus",
        .tex = T6T(TILE_CACTUS_SIDE, TILE_CACTUS_TOP, TILE_CACTUS_TOP),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 4,
        .drop_item = WL_BLOCK_CACTUS, .drop_count = 1,
    });

    reg(WL_BLOCK_CRAFTING_TABLE, (WlBlock){
        .name = "crafting_table", .display = "Crafting Table",
        .tex = { TILE_CRAFTING_SIDE, TILE_CRAFTING_SIDE, TILE_CRAFTING_TOP,
                 TILE_PLANKS, TILE_CRAFTING_FRONT, TILE_CRAFTING_SIDE },
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 10, .tool = WL_TOOL_AXE,
        .drop_item = WL_BLOCK_CRAFTING_TABLE, .drop_count = 1,
    });

    reg(WL_BLOCK_TORCH, (WlBlock){
        .name = "torch", .display = "Torch", .tex = T6(TILE_TORCH),
        .shape = WL_RENDER_CROSS, .solid = false, .opaque = false,
        .hardness = 0,
        .drop_item = WL_BLOCK_TORCH, .drop_count = 1,
            .light = 14,
    });

    reg(WL_BLOCK_FLOWER_RED, (WlBlock){
        .name = "rose", .display = "Wild Rose", .tex = T6(TILE_FLOWER_RED),
        .shape = WL_RENDER_CROSS, .solid = false, .opaque = false,
        .hardness = 0,
        .drop_item = WL_BLOCK_FLOWER_RED, .drop_count = 1,
    });

    reg(WL_BLOCK_FLOWER_YELLOW, (WlBlock){
        .name = "sunbloom", .display = "Sunbloom", .tex = T6(TILE_FLOWER_YELLOW),
        .shape = WL_RENDER_CROSS, .solid = false, .opaque = false,
        .hardness = 0,
        .drop_item = WL_BLOCK_FLOWER_YELLOW, .drop_count = 1,
    });

    reg(WL_BLOCK_TALLGRASS, (WlBlock){
        .name = "tall_grass", .display = "Tall Grass", .tex = T6(TILE_TALLGRASS),
        .shape = WL_RENDER_CROSS, .solid = false, .opaque = false,
        .hardness = 0, .drop_item = WL_ITEM_NONE,
    });

    reg(WL_BLOCK_FURNACE, (WlBlock){
        .name = "furnace", .display = "Furnace",
        .tex = T6T4(TILE_FURNACE_FRONT, TILE_FURNACE_SIDE, TILE_FURNACE_TOP),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 16, .tool = WL_TOOL_PICKAXE,
        .drop_item = WL_BLOCK_FURNACE, .drop_count = 1,
    });

    reg(WL_BLOCK_FURNACE_LIT, (WlBlock){
        .name = "furnace_lit", .display = "Furnace",
        .tex = T6T4(TILE_FURNACE_FRONT_LIT, TILE_FURNACE_SIDE, TILE_FURNACE_TOP),
        .shape = WL_RENDER_SOLID, .solid = true, .opaque = true,
        .hardness = 16, .tool = WL_TOOL_PICKAXE,
        .drop_item = WL_BLOCK_FURNACE, .drop_count = 1,
        .light = 13,
    });

    g_blocks_initialized = true;
    WL_LOGI("block registry: %d base blocks", g_block_count);
}
