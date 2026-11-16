/* item.c — item registry: mirrored block items plus base pure items. */
#include "wl/item.h"
#include "wl/tiles.h"
#include "wl/util.h"
#include "wl/log.h"

#include <string.h>

static WlItem g_items[WL_ITEM_MAX];
static int g_item_count = 0; /* next free id */
static bool g_items_initialized = false;

bool wl_item_valid(WlItemId id)
{
    return id != WL_ITEM_NONE && id < g_item_count && g_items[id].name != NULL;
}

const WlItem *wl_item_get(WlItemId id)
{
    if (!wl_item_valid(id)) return NULL;
    return &g_items[id];
}

int wl_items_count(void)
{
    return g_item_count;
}

WlItemId wl_items_register(const WlItem *def)
{
    if (!def || !def->name || def->name[0] == '\0') {
        WL_LOGE("item registration rejected: missing name");
        return WL_ITEM_NONE;
    }
    if (g_item_count >= WL_ITEM_MAX) {
        WL_LOGE("item registry full, cannot register '%s'", def->name);
        return WL_ITEM_NONE;
    }
    WlItemId id = (WlItemId)g_item_count;
    g_items[id] = *def;
    g_item_count++;
    WL_LOGD("registered item #%d '%s'", (int)id, def->name);
    return id;
}

bool wl_items_register_at(WlItemId id, const WlItem *def)
{
    if (!def || !def->name || id == 0 || id >= 256) return false;
    /* only unused mirror slots may be claimed */
    if (g_items[id].name != NULL && !wl_str_eq(g_items[id].name, "reserved"))
        return false;
    g_items[id] = *def;
    return true;
}

bool wl_item_in_group(WlItemId id, WlItemGroup group)
{
    if (group == WL_GROUP_NONE) return false;
    if (group == WL_GROUP_ANY_LOG)
        return id == WL_BLOCK_LOG || id == WL_BLOCK_BIRCH_LOG;
    if (group == WL_GROUP_ANY_PLANKS)
        return id == WL_BLOCK_PLANKS;
    return false;
}

static void reg(WlItemId expect, WlItem def)
{
    WlItemId got = wl_items_register(&def);
    if (got != expect)
        WL_LOGE("item id mismatch: expected %d, got %d ('%s')",
                (int)expect, (int)got, def.name);
}

static void reg_tool(WlItemId expect, const char *name, const char *display,
                     uint16_t tile, WlToolKind kind, uint16_t durability)
{
    /* tiers: wood 1, stone 2, iron 3, diamond 4 — derived from slot order */
    static const uint8_t tier_by_slot[4] = { 1, 2, 3, 4 };
    uint8_t tier = 0;
    if (expect == WL_ITEM_PICK_WOOD || expect == WL_ITEM_AXE_WOOD ||
        expect == WL_ITEM_SHOVEL_WOOD || expect == WL_ITEM_SWORD_WOOD)
        tier = tier_by_slot[0];
    else if (expect == WL_ITEM_PICK_STONE || expect == WL_ITEM_AXE_STONE ||
             expect == WL_ITEM_SHOVEL_STONE || expect == WL_ITEM_SWORD_STONE)
        tier = tier_by_slot[1];
    else if (expect == WL_ITEM_PICK_IRON || expect == WL_ITEM_AXE_IRON ||
             expect == WL_ITEM_SHOVEL_IRON || expect == WL_ITEM_SWORD_IRON)
        tier = tier_by_slot[2];
    else if (expect == WL_ITEM_PICK_DIAMOND || expect == WL_ITEM_AXE_DIAMOND ||
             expect == WL_ITEM_SHOVEL_DIAMOND || expect == WL_ITEM_SWORD_DIAMOND)
        tier = tier_by_slot[3];

    reg(expect, (WlItem){
        .name = name, .display = display, .tile = tile,
        .max_stack = 1, .tool = kind, .tool_tier = tier,
        .durability = durability,
    });
}

void wl_items_init(void)
{
    if (g_items_initialized) {
        WL_LOGW("wl_items_init called twice; ignored");
        return;
    }
    memset(g_items, 0, sizeof(g_items));
    g_item_count = 0;

    /* Slot 0 must stay invalid (item id 0 is never used). */
    reg(0, (WlItem){ .name = "invalid", .display = "?", .max_stack = 1 });

    /* Mirror every base block so item ids stay aligned with block ids.
     * Water gets a shadow item (not placeable, never obtainable) to keep
     * the id == block-id contract intact. */
    for (int b = 1; b < WL_BLOCK_COUNT_BASE; b++) {
        const WlBlock *blk = wl_block_get((WlBlockId)b);
        if (!blk) continue;
        WlItem def = {
            .name = blk->name,
            .display = blk->display,
            .tile = 0, /* block icons are composited from face tiles */
            .is_block = (b != WL_BLOCK_WATER),
            .block_id = (uint8_t)b,
            .max_stack = 64,
        };
        if (wl_items_register(&def) != (WlItemId)b)
            WL_LOGE("block item mirror failed for block %d", b);
    }

    /* Pad to 256 so pure items get the ids declared in item.h. */
    while (g_item_count < 256) {
        WlItem filler = { .name = "reserved", .display = "?", .max_stack = 1 };
        wl_items_register(&filler);
    }

    reg(WL_ITEM_STICK, (WlItem){
        .name = "stick", .display = "Stick", .tile = TILE_ICON_STICK, .max_stack = 64 });
    reg(WL_ITEM_COAL, (WlItem){
        .name = "coal", .display = "Coal", .tile = TILE_ICON_COAL, .max_stack = 64 });
    reg(WL_ITEM_RAW_IRON, (WlItem){
        .name = "iron_chunk", .display = "Iron Chunk", .tile = TILE_ICON_RAW_IRON, .max_stack = 64 });
    reg(WL_ITEM_RAW_GOLD, (WlItem){
        .name = "gold_chunk", .display = "Gold Chunk", .tile = TILE_ICON_RAW_GOLD, .max_stack = 64 });
    reg(WL_ITEM_DIAMOND, (WlItem){
        .name = "diamond", .display = "Diamond", .tile = TILE_ICON_DIAMOND, .max_stack = 64 });
    reg(WL_ITEM_APPLE, (WlItem){
        .name = "apple", .display = "Apple", .tile = TILE_ICON_APPLE,
        .max_stack = 64, .food = 4 });

    reg_tool(WL_ITEM_PICK_WOOD,    "wooden_pickaxe",  "Wooden Pickaxe",
             TILE_ICON_PICK_WOOD,    WL_TOOL_PICKAXE,  60);
    reg_tool(WL_ITEM_PICK_STONE,   "stone_pickaxe",   "Stone Pickaxe",
             TILE_ICON_PICK_STONE,   WL_TOOL_PICKAXE, 132);
    reg_tool(WL_ITEM_PICK_IRON,    "iron_pickaxe",    "Iron Pickaxe",
             TILE_ICON_PICK_IRON,    WL_TOOL_PICKAXE, 251);
    reg_tool(WL_ITEM_PICK_DIAMOND, "diamond_pickaxe", "Diamond Pickaxe",
             TILE_ICON_PICK_DIAMOND, WL_TOOL_PICKAXE, 1562);

    reg_tool(WL_ITEM_AXE_WOOD,     "wooden_axe",      "Wooden Axe",
             TILE_ICON_AXE_WOOD,     WL_TOOL_AXE,      60);
    reg_tool(WL_ITEM_AXE_STONE,    "stone_axe",       "Stone Axe",
             TILE_ICON_AXE_STONE,    WL_TOOL_AXE,     132);
    reg_tool(WL_ITEM_AXE_IRON,     "iron_axe",        "Iron Axe",
             TILE_ICON_AXE_IRON,     WL_TOOL_AXE,     251);
    reg_tool(WL_ITEM_AXE_DIAMOND,  "diamond_axe",     "Diamond Axe",
             TILE_ICON_AXE_DIAMOND,  WL_TOOL_AXE,    1562);

    reg_tool(WL_ITEM_SHOVEL_WOOD,     "wooden_shovel",  "Wooden Shovel",
             TILE_ICON_SHOVEL_WOOD,     WL_TOOL_SHOVEL,   60);
    reg_tool(WL_ITEM_SHOVEL_STONE,    "stone_shovel",   "Stone Shovel",
             TILE_ICON_SHOVEL_STONE,    WL_TOOL_SHOVEL,  132);
    reg_tool(WL_ITEM_SHOVEL_IRON,     "iron_shovel",    "Iron Shovel",
             TILE_ICON_SHOVEL_IRON,     WL_TOOL_SHOVEL,  251);
    reg_tool(WL_ITEM_SHOVEL_DIAMOND,  "diamond_shovel", "Diamond Shovel",
             TILE_ICON_SHOVEL_DIAMOND,  WL_TOOL_SHOVEL, 1562);

    reg_tool(WL_ITEM_SWORD_WOOD,     "wooden_sword",  "Wooden Sword",
             TILE_ICON_SWORD_WOOD,     WL_TOOL_SWORD,   60);
    reg_tool(WL_ITEM_SWORD_STONE,    "stone_sword",   "Stone Sword",
             TILE_ICON_SWORD_STONE,    WL_TOOL_SWORD,  132);
    reg_tool(WL_ITEM_SWORD_IRON,     "iron_sword",    "Iron Sword",
             TILE_ICON_SWORD_IRON,     WL_TOOL_SWORD,  251);
    reg_tool(WL_ITEM_SWORD_DIAMOND,  "diamond_sword", "Diamond Sword",
             TILE_ICON_SWORD_DIAMOND,  WL_TOOL_SWORD, 1562);

    g_items_initialized = true;
    WL_LOGI("item registry: %d entries", g_item_count);
}
