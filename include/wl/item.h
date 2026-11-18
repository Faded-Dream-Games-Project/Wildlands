/* item.h — the item registry (block items + pure items + mods). */
#ifndef WL_ITEM_H
#define WL_ITEM_H

#include "wl/wl_common.h"
#include "wl/block.h" /* WlToolKind, WlBlockId */

#define WL_ITEM_MAX 512
#define WL_ITEM_NONE 0xFFFF
typedef uint16_t WlItemId;

/* Items 1..255 mirror placeable blocks (item id == block id).
 * Pure base items start at 256. */
enum {
    WL_ITEM_STICK = 256,
    WL_ITEM_COAL,
    WL_ITEM_RAW_IRON,
    WL_ITEM_RAW_GOLD,
    WL_ITEM_DIAMOND,
    WL_ITEM_APPLE,
    WL_ITEM_PICK_WOOD,
    WL_ITEM_PICK_STONE,
    WL_ITEM_PICK_IRON,
    WL_ITEM_PICK_DIAMOND,
    WL_ITEM_AXE_WOOD,
    WL_ITEM_AXE_STONE,
    WL_ITEM_AXE_IRON,
    WL_ITEM_AXE_DIAMOND,
    WL_ITEM_SHOVEL_WOOD,
    WL_ITEM_SHOVEL_STONE,
    WL_ITEM_SHOVEL_IRON,
    WL_ITEM_SHOVEL_DIAMOND,
    WL_ITEM_SWORD_WOOD,
    WL_ITEM_SWORD_STONE,
    WL_ITEM_SWORD_IRON,
    WL_ITEM_SWORD_DIAMOND,
    WL_ITEM_LEATHER,
    WL_ITEM_FEATHER,
    WL_ITEM_WOOL,
    WL_ITEM_IRON_INGOT,
    WL_ITEM_BEEF_RAW,
    WL_ITEM_BEEF_COOKED,
    WL_ITEM_PORK_RAW,
    WL_ITEM_PORK_COOKED,
    WL_ITEM_CHICKEN_RAW,
    WL_ITEM_CHICKEN_COOKED,
    WL_ITEM_MUTTON_RAW,
    WL_ITEM_MUTTON_COOKED,
    WL_ITEM_ARROW,
    WL_ITEM_ROTTEN_FLESH,
    WL_ITEM_STRING,
    WL_ITEM_BONE,
    /* armor: leather / iron / diamond */
    WL_ITEM_LEATHER_HELMET,
    WL_ITEM_LEATHER_CHESTPLATE,
    WL_ITEM_LEATHER_LEGGINGS,
    WL_ITEM_LEATHER_BOOTS,
    WL_ITEM_IRON_HELMET,
    WL_ITEM_IRON_CHESTPLATE,
    WL_ITEM_IRON_LEGGINGS,
    WL_ITEM_IRON_BOOTS,
    WL_ITEM_DIAMOND_HELMET,
    WL_ITEM_DIAMOND_CHESTPLATE,
    WL_ITEM_DIAMOND_LEGGINGS,
    WL_ITEM_DIAMOND_BOOTS,
    /* hoes + farming */
    WL_ITEM_HOE_WOOD,
    WL_ITEM_HOE_STONE,
    WL_ITEM_HOE_IRON,
    WL_ITEM_HOE_DIAMOND,
    WL_ITEM_WHEAT,
    WL_ITEM_SEEDS,
    WL_ITEM_BREAD,
    WL_ITEM_COUNT_BASE
};

typedef struct {
    const char *name;    /* registry id, e.g. "iron_pickaxe" */
    const char *display; /* shown in UI, e.g. "Iron Pickaxe" */
    uint16_t tile;       /* atlas tile used for the icon */
    bool is_block;       /* places a block when used */
    uint8_t block_id;    /* which block (when is_block) */
    uint8_t max_stack;   /* 64 for materials, 1 for tools */
    WlToolKind tool;     /* WL_TOOL_NONE for non-tools */
    uint8_t tool_tier;   /* 0 none; 1 wood 2 stone 3 iron 4 diamond */
    uint16_t durability; /* 0 = not a tool */
    uint8_t food;        /* hunger restored; 0 = not edible */
    uint8_t armor_slot;  /* 0 = not armor; 1 helmet 2 chest 3 legs 4 boots */
    uint8_t armor_points;/* protection points when worn (MC-style 4%/pt) */
} WlItem;

/* Ingredient groups used by the crafting matcher (stable ids for mods). */
typedef enum {
    WL_GROUP_NONE = 0,
    WL_GROUP_ANY_LOG,
    WL_GROUP_ANY_PLANKS,
    WL_GROUP_COUNT
} WlItemGroup;

/* Registers base items (must run after wl_blocks_init). */
void        wl_items_init(void);
WlItemId   wl_items_register(const WlItem *def);
/* Overwrites a reserved mirror slot (ids 1..255) — used by the mod loader
 * so modded blocks get an item with the same id. */
bool        wl_items_register_at(WlItemId id, const WlItem *def);
const WlItem *wl_item_get(WlItemId id);
int         wl_items_count(void);

/* True when the id is a valid, registered item. */
bool wl_item_valid(WlItemId id);

/* Group membership (single-place check; logs nothing on false). */
bool wl_item_in_group(WlItemId id, WlItemGroup group);

#endif /* WL_ITEM_H */
