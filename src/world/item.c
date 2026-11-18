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

    reg(WL_ITEM_LEATHER, (WlItem){
        .name = "leather", .display = "Leather", .tile = TILE_ICON_LEATHER, .max_stack = 64 });
    reg(WL_ITEM_FEATHER, (WlItem){
        .name = "feather", .display = "Feather", .tile = TILE_ICON_FEATHER, .max_stack = 64 });
    reg(WL_ITEM_WOOL, (WlItem){
        .name = "wool", .display = "Wool", .tile = TILE_ICON_WOOL, .max_stack = 64 });
    reg(WL_ITEM_IRON_INGOT, (WlItem){
        .name = "iron_ingot", .display = "Iron Ingot", .tile = TILE_ICON_IRON_INGOT, .max_stack = 64 });
    reg(WL_ITEM_BEEF_RAW, (WlItem){
        .name = "beef_raw", .display = "Raw Beef", .tile = TILE_ICON_BEEF_RAW,
        .food = 3, .max_stack = 64 });
    reg(WL_ITEM_BEEF_COOKED, (WlItem){
        .name = "beef_cooked", .display = "Steak", .tile = TILE_ICON_BEEF_COOKED,
        .food = 8, .max_stack = 64 });
    reg(WL_ITEM_PORK_RAW, (WlItem){
        .name = "pork_raw", .display = "Raw Porkchop", .tile = TILE_ICON_PORK_RAW,
        .food = 3, .max_stack = 64 });
    reg(WL_ITEM_PORK_COOKED, (WlItem){
        .name = "pork_cooked", .display = "Cooked Porkchop", .tile = TILE_ICON_PORK_COOKED,
        .food = 8, .max_stack = 64 });
    reg(WL_ITEM_CHICKEN_RAW, (WlItem){
        .name = "chicken_raw", .display = "Raw Chicken", .tile = TILE_ICON_CHICKEN_RAW,
        .food = 2, .max_stack = 64 });
    reg(WL_ITEM_CHICKEN_COOKED, (WlItem){
        .name = "chicken_cooked", .display = "Cooked Chicken", .tile = TILE_ICON_CHICKEN_COOKED,
        .food = 6, .max_stack = 64 });
    reg(WL_ITEM_MUTTON_RAW, (WlItem){
        .name = "mutton_raw", .display = "Raw Mutton", .tile = TILE_ICON_MUTTON_RAW,
        .food = 2, .max_stack = 64 });
    reg(WL_ITEM_MUTTON_COOKED, (WlItem){
        .name = "mutton_cooked", .display = "Cooked Mutton", .tile = TILE_ICON_MUTTON_COOKED,
        .food = 6, .max_stack = 64 });
    reg(WL_ITEM_ARROW, (WlItem){
        .name = "arrow", .display = "Arrow", .tile = TILE_ICON_ARROW, .max_stack = 64 });
    reg(WL_ITEM_ROTTEN_FLESH, (WlItem){
        .name = "rotten_flesh", .display = "Rotten Flesh", .tile = TILE_ICON_ROTTEN_FLESH,
        .food = 2, .max_stack = 64 });
    reg(WL_ITEM_STRING, (WlItem){
        .name = "string", .display = "String", .tile = TILE_ICON_STRING, .max_stack = 64 });
    reg(WL_ITEM_BONE, (WlItem){
        .name = "bone", .display = "Bone", .tile = TILE_ICON_BONE, .max_stack = 64 });

    /* armor: protection points per piece follow the classic formula
     * (total * 4% damage reduction, capped at 80%); durability matches
     * the standard survival balance so pieces last a night of fighting,
     * not forever */
    {
        struct { WlItemId id; const char *name, *display; int tile;
                 int pts, dur; } arm[] = {
            { WL_ITEM_LEATHER_HELMET, "leather_helmet", "Leather Helmet",
              TILE_ICON_ARM_LEATHER_HELM, 1, 55 },
            { WL_ITEM_LEATHER_CHESTPLATE, "leather_chestplate", "Leather Chestplate",
              TILE_ICON_ARM_LEATHER_CHEST, 3, 80 },
            { WL_ITEM_LEATHER_LEGGINGS, "leather_leggings", "Leather Leggings",
              TILE_ICON_ARM_LEATHER_LEGS, 2, 75 },
            { WL_ITEM_LEATHER_BOOTS, "leather_boots", "Leather Boots",
              TILE_ICON_ARM_LEATHER_BOOTS, 1, 65 },
            { WL_ITEM_IRON_HELMET, "iron_helmet", "Iron Helmet",
              TILE_ICON_ARM_IRON_HELM, 2, 165 },
            { WL_ITEM_IRON_CHESTPLATE, "iron_chestplate", "Iron Chestplate",
              TILE_ICON_ARM_IRON_CHEST, 6, 240 },
            { WL_ITEM_IRON_LEGGINGS, "iron_leggings", "Iron Leggings",
              TILE_ICON_ARM_IRON_LEGS, 5, 225 },
            { WL_ITEM_IRON_BOOTS, "iron_boots", "Iron Boots",
              TILE_ICON_ARM_IRON_BOOTS, 2, 195 },
            { WL_ITEM_DIAMOND_HELMET, "diamond_helmet", "Diamond Helmet",
              TILE_ICON_ARM_DIAMOND_HELM, 3, 363 },
            { WL_ITEM_DIAMOND_CHESTPLATE, "diamond_chestplate", "Diamond Chestplate",
              TILE_ICON_ARM_DIAMOND_CHEST, 8, 528 },
            { WL_ITEM_DIAMOND_LEGGINGS, "diamond_leggings", "Diamond Leggings",
              TILE_ICON_ARM_DIAMOND_LEGS, 6, 495 },
            { WL_ITEM_DIAMOND_BOOTS, "diamond_boots", "Diamond Boots",
              TILE_ICON_ARM_DIAMOND_BOOTS, 3, 429 },
        };
        for (size_t i = 0; i < sizeof(arm) / sizeof(arm[0]); i++) {
            WlItem d = {
                .name = arm[i].name, .display = arm[i].display,
                .tile = (uint16_t)arm[i].tile, .max_stack = 1,
                .durability = (uint16_t)arm[i].dur,
                .armor_slot = (uint8_t)(i % 4 + 1),
                .armor_points = (uint8_t)arm[i].pts,
            };
            reg(arm[i].id, d);
        }
    }
    {
        struct { WlItemId id; const char *name, *display; int tile, dur; } hoes[] = {
            { WL_ITEM_HOE_WOOD, "wooden_hoe", "Wooden Hoe",
              TILE_ICON_HOE_WOOD, 60 },
            { WL_ITEM_HOE_STONE, "stone_hoe", "Stone Hoe",
              TILE_ICON_HOE_STONE, 132 },
            { WL_ITEM_HOE_IRON, "iron_hoe", "Iron Hoe",
              TILE_ICON_HOE_IRON, 251 },
            { WL_ITEM_HOE_DIAMOND, "diamond_hoe", "Diamond Hoe",
              TILE_ICON_HOE_DIAMOND, 1562 },
        };
        for (size_t i = 0; i < sizeof(hoes) / sizeof(hoes[0]); i++) {
            WlItem d = {
                .name = hoes[i].name, .display = hoes[i].display,
                .tile = (uint16_t)hoes[i].tile, .max_stack = 1,
                .tool = WL_TOOL_HOE, .tool_tier = (uint8_t)(i + 1),
                .durability = (uint16_t)hoes[i].dur,
            };
            reg(hoes[i].id, d);
        }
    }
    reg(WL_ITEM_WHEAT, (WlItem){
        .name = "wheat", .display = "Wheat", .tile = TILE_ICON_WHEAT,
        .max_stack = 64 });
    reg(WL_ITEM_SEEDS, (WlItem){
        .name = "seeds", .display = "Seeds", .tile = TILE_ICON_SEEDS,
        .max_stack = 64 });
    reg(WL_ITEM_BREAD, (WlItem){
        .name = "bread", .display = "Bread", .tile = TILE_ICON_BREAD,
        .max_stack = 64, .food = 5 });

    g_items_initialized = true;
    WL_LOGI("item registry: %d entries", g_item_count);
}
