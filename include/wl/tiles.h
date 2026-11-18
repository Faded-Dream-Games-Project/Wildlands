/* tiles.h — texture atlas tile layout.
 *
 * The base atlas (assets/textures/blocks.png) is a 16x16 grid of 16px tiles.
 * Base tile indices are stable: save files and mods may reference them.
 * tools/gen_assets.py MUST generate tiles in exactly this order; the unit
 * tests verify TILE_BASE_COUNT matches the generated atlas layout file. */
#ifndef WL_TILES_H
#define WL_TILES_H

#include "wl/wl_common.h"

#define WL_ATLAS_GRID 16 /* tiles per row/column -> 256 tile slots */
#define WL_TILE_PX    16 /* tile size in pixels   -> 256x256 atlas */

enum WlTile {
    TILE_STONE = 0,
    TILE_DIRT,
    TILE_GRASS_TOP,
    TILE_GRASS_SIDE,
    TILE_COBBLE,
    TILE_PLANKS,
    TILE_SAND,
    TILE_SANDSTONE,
    TILE_WATER,
    TILE_GLASS,
    TILE_LOG_SIDE,
    TILE_LOG_TOP,
    TILE_LEAVES,
    TILE_BIRCH_LOG_SIDE,
    TILE_BIRCH_LOG_TOP,
    TILE_BIRCH_LEAVES,
    TILE_GRAVEL,
    TILE_COAL_ORE,
    TILE_IRON_ORE,
    TILE_GOLD_ORE,
    TILE_DIAMOND_ORE,
    TILE_BEDROCK,
    TILE_SNOW,
    TILE_GRASS_SNOW_SIDE,
    TILE_ICE,
    TILE_CACTUS_SIDE,
    TILE_CACTUS_TOP,
    TILE_CRAFTING_TOP,
    TILE_CRAFTING_SIDE,
    TILE_CRAFTING_FRONT,
    TILE_TORCH,
    TILE_FLOWER_RED,
    TILE_FLOWER_YELLOW,
    TILE_TALLGRASS,
    TILE_ICON_STICK,
    TILE_ICON_COAL,
    TILE_ICON_RAW_IRON,
    TILE_ICON_RAW_GOLD,
    TILE_ICON_DIAMOND,
    TILE_ICON_APPLE,
    TILE_ICON_PICK_WOOD,
    TILE_ICON_PICK_STONE,
    TILE_ICON_PICK_IRON,
    TILE_ICON_PICK_DIAMOND,
    TILE_ICON_AXE_WOOD,
    TILE_ICON_AXE_STONE,
    TILE_ICON_AXE_IRON,
    TILE_ICON_AXE_DIAMOND,
    TILE_ICON_SHOVEL_WOOD,
    TILE_ICON_SHOVEL_STONE,
    TILE_ICON_SHOVEL_IRON,
    TILE_ICON_SHOVEL_DIAMOND,
    TILE_ICON_SWORD_WOOD,
    TILE_ICON_SWORD_STONE,
    TILE_ICON_SWORD_IRON,
    TILE_ICON_SWORD_DIAMOND,
    /* furnace blocks (side / front / lit front / top) */
    TILE_FURNACE_SIDE,
    TILE_FURNACE_FRONT,
    TILE_FURNACE_FRONT_LIT,
    TILE_FURNACE_TOP,
    /* material + food icons */
    TILE_ICON_LEATHER,
    TILE_ICON_FEATHER,
    TILE_ICON_WOOL,
    TILE_ICON_IRON_INGOT,
    TILE_ICON_BEEF_RAW,
    TILE_ICON_BEEF_COOKED,
    TILE_ICON_PORK_RAW,
    TILE_ICON_PORK_COOKED,
    TILE_ICON_CHICKEN_RAW,
    TILE_ICON_CHICKEN_COOKED,
    TILE_ICON_MUTTON_RAW,
    TILE_ICON_MUTTON_COOKED,
    TILE_ICON_ARROW,
    TILE_ICON_ROTTEN_FLESH,
    TILE_ICON_STRING,
    TILE_ICON_BONE,
    /* progressive block-breaking cracks (stage 0..9) */
    TILE_CRACK_0,
    TILE_CRACK_1,
    TILE_CRACK_2,
    TILE_CRACK_3,
    TILE_CRACK_4,
    TILE_CRACK_5,
    TILE_CRACK_6,
    TILE_CRACK_7,
    TILE_CRACK_8,
    TILE_CRACK_9,
    TILE_BASE_COUNT,
    /* fixed high slot used to tint solid-colour box models (held
     * tools): far above every base and mod-registered tile */
    TILE_SOLID_WHITE = 250,
    /* armor / hoe / farming icon tiles and block textures, also at
     * fixed high slots so mod atlas tiles never shift (see
     * tools/gen_assets.py, painted after the base loop) */
    TILE_ICON_ARM_LEATHER_HELM = 220,
    TILE_ICON_ARM_LEATHER_CHEST = 221,
    TILE_ICON_ARM_LEATHER_LEGS = 222,
    TILE_ICON_ARM_LEATHER_BOOTS = 223,
    TILE_ICON_ARM_IRON_HELM = 224,
    TILE_ICON_ARM_IRON_CHEST = 225,
    TILE_ICON_ARM_IRON_LEGS = 226,
    TILE_ICON_ARM_IRON_BOOTS = 227,
    TILE_ICON_ARM_DIAMOND_HELM = 228,
    TILE_ICON_ARM_DIAMOND_CHEST = 229,
    TILE_ICON_ARM_DIAMOND_LEGS = 230,
    TILE_ICON_ARM_DIAMOND_BOOTS = 231,
    TILE_ICON_HOE_WOOD = 232,
    TILE_ICON_HOE_STONE = 233,
    TILE_ICON_HOE_IRON = 234,
    TILE_ICON_HOE_DIAMOND = 235,
    TILE_ICON_WHEAT = 236,
    TILE_ICON_SEEDS = 237,
    TILE_ICON_BREAD = 238,
    TILE_FARMLAND = 239,
    TILE_WHEAT_STAGE1 = 240,
    TILE_WHEAT_STAGE2 = 241,
    TILE_WHEAT_STAGE3 = 242
};

#endif /* WL_TILES_H */
