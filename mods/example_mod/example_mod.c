/* example_mod.c — "Frostbite": a complete, working Wildlands mod.
 *
 * Adds two blocks (frost stone, frost brick) with their own textures and a
 * crafting recipe (2x2 frost stone -> 4 frost bricks).
 * Build: make mods   (see mods/example_mod/README.md) */
#include "wl/mod_api.h"

static int g_frost_stone = 0;
static int g_frost_brick = 0;

int wl_mod_main(const WlApi *api, WlModInfo *out_info)
{
    if (!api || !out_info) return 1;

    out_info->id = "frostbite";
    out_info->name = "Frostbite";
    out_info->version = "1.0.0";
    out_info->author = "Wildlands Team";
    out_info->description = "Frozen rocks from the deep: frost stone and frost bricks.";
    out_info->api_version = WL_API_VERSION;

    g_frost_stone = api->register_block(&(WlModBlockDef){
        .name = "frost_stone",
        .display = "Frost Stone",
        .texture = "assets/textures/frost_stone.png",
        .solid = true,
        .opaque = true,
        .hardness = 18,
        .tool_kind = WL_MOD_TOOL_PICKAXE,
        .tool_tier = 2,      /* needs at least a stone pickaxe */
        .needs_tool = true,
        .drop_item_id = 0,   /* drops itself */
        .drop_count = 1,
    });
    if (g_frost_stone <= 0) {
        api->log_error("frost_stone registration failed");
        return 1;
    }

    g_frost_brick = api->register_block(&(WlModBlockDef){
        .name = "frost_brick",
        .display = "Frost Brick",
        .texture = "assets/textures/frost_brick.png",
        .solid = true,
        .opaque = true,
        .hardness = 14,
        .tool_kind = WL_MOD_TOOL_PICKAXE,
        .tool_tier = 1,
        .needs_tool = true,
    });
    if (g_frost_brick <= 0) {
        api->log_error("frost_brick registration failed");
        return 1;
    }

    /* 2x2 frost stone -> 4 frost bricks */
    WlModRecipeDef recipe = {
        .out_item_id = g_frost_brick,
        .out_count = 4,
        .w = 2, .h = 2,
        .pattern = {
            g_frost_stone, g_frost_stone,
            g_frost_stone, g_frost_stone,
        },
    };
    if (!api->register_recipe(&recipe)) {
        api->log_error("recipe registration failed");
        return 1;
    }

    api->log_info("Frostbite ready: mine frost stone with a stone pickaxe!");
    return 0;
}
