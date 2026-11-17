/* crafting.c — recipe registry, shaped/shapeless matching, base recipes. */
#include "wl/crafting.h"
#include "wl/log.h"

#include <string.h>

static WlRecipe g_recipes[WL_RECIPES_MAX];
static int g_recipe_count = 0;
static bool g_initialized = false;

int wl_crafting_register(const WlRecipe *r)
{
    if (!r) return -1;
    if (g_recipe_count >= WL_RECIPES_MAX) {
        WL_LOGE("recipe registry full");
        return -1;
    }
    if (!r->shapeless && (r->w < 1 || r->w > 3 || r->h < 1 || r->h > 3)) {
        WL_LOGE("invalid shaped recipe size %dx%d", r->w, r->h);
        return -1;
    }
    if (!wl_item_valid(r->out_item)) {
        WL_LOGE("recipe outputs invalid item %d", (int)r->out_item);
        return -1;
    }
    g_recipes[g_recipe_count] = *r;
    return g_recipe_count++;
}

const WlRecipe *wl_crafting_get(int index)
{
    if (index < 0 || index >= g_recipe_count) return NULL;
    return &g_recipes[index];
}

int wl_crafting_count(void) { return g_recipe_count; }

/* ------------------------------------------------------------------ */
/* matching                                                            */
/* ------------------------------------------------------------------ */

static bool ingredient_matches(uint16_t want, uint16_t have)
{
    /* 0 and WL_ITEM_NONE both mean "empty slot" (defensive for zeroed data) */
    bool have_empty = (have == 0 || have == WL_ITEM_NONE);
    if (want == 0) return have_empty;
    if (have_empty) return false;
    if (want & WL_ING_GROUP)
        return wl_item_in_group(have, (WlItemGroup)(want & 0x7FFFu));
    return want == have;
}

static bool match_at(const WlRecipe *r, const WlSlot *grid, int gw, int ox, int oy,
                     bool mirror)
{
    for (int gy = 0; gy < gw; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            /* map grid cell to pattern cell */
            int px = gx - ox, py = gy - oy;
            uint16_t want = 0;
            if (px >= 0 && px < r->w && py >= 0 && py < r->h) {
                int pxx = mirror ? (r->w - 1 - px) : px;
                want = r->pattern[py * r->w + pxx];
            }
            if (!ingredient_matches(want, grid[gy * gw + gx].item))
                return false;
        }
    }
    return true;
}

static bool match_shaped(const WlRecipe *r, const WlSlot *grid, int gw)
{
    for (int oy = 0; oy + r->h <= gw; oy++)
        for (int ox = 0; ox + r->w <= gw; ox++) {
            if (match_at(r, grid, gw, ox, oy, false)) return true;
            if (match_at(r, grid, gw, ox, oy, true)) return true;
        }
    return false;
}

static bool match_shapeless(const WlRecipe *r, const WlSlot *grid, int gw)
{
    uint16_t have[9];
    int n = 0;
    for (int i = 0; i < gw * gw; i++)
        if (grid[i].item != 0 && grid[i].item != WL_ITEM_NONE)
            have[n++] = grid[i].item;
    if (n != r->n_ing) return false;

    uint16_t want[9];
    memcpy(want, r->ingredients, sizeof(want));
    for (int i = 0; i < n; i++) {
        bool found = false;
        for (int j = 0; j < n; j++) {
            if (want[j] == 0) continue;
            if (ingredient_matches(want[j], have[i])) {
                want[j] = 0;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

const WlRecipe *wl_crafting_match(const WlSlot *grid, int w)
{
    if (w != 2 && w != 3) return NULL;
    for (int i = 0; i < g_recipe_count; i++) {
        const WlRecipe *r = &g_recipes[i];
        if (r->shapeless ? match_shapeless(r, grid, w)
                         : match_shaped(r, grid, w))
            return r;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* base recipes                                                        */
/* ------------------------------------------------------------------ */

static void add_shaped(uint16_t out, uint8_t count, int w, int h,
                       const uint16_t *pattern)
{
    WlRecipe r;
    memset(&r, 0, sizeof(r));
    r.out_item = out;
    r.out_count = count;
    r.w = w;
    r.h = h;
    memcpy(r.pattern, pattern, sizeof(uint16_t) * (size_t)(w * h));
    wl_crafting_register(&r);
}

static void add_shapeless(uint16_t out, uint8_t count, const uint16_t *ing, int n)
{
    WlRecipe r;
    memset(&r, 0, sizeof(r));
    r.out_item = out;
    r.out_count = count;
    r.shapeless = true;
    r.n_ing = n;
    memcpy(r.ingredients, ing, sizeof(uint16_t) * (size_t)n);
    wl_crafting_register(&r);
}

#define G_LOGS (WL_ING_GROUP | WL_GROUP_ANY_LOG)

void wl_crafting_init(void)
{
    if (g_initialized) {
        WL_LOGW("wl_crafting_init called twice; ignored");
        return;
    }

    /* basics */
    {
        uint16_t ing[1] = { G_LOGS };
        add_shapeless(WL_BLOCK_PLANKS, 4, ing, 1);
    }
    {
        uint16_t pat[2] = { WL_BLOCK_PLANKS, WL_BLOCK_PLANKS };
        add_shaped(WL_ITEM_STICK, 4, 1, 2, pat);
    }
    {
        uint16_t pat[4] = { WL_BLOCK_PLANKS, WL_BLOCK_PLANKS,
                            WL_BLOCK_PLANKS, WL_BLOCK_PLANKS };
        add_shaped(WL_BLOCK_CRAFTING_TABLE, 1, 2, 2, pat);
    }
    {
        uint16_t pat[2] = { WL_ITEM_COAL, WL_ITEM_STICK };
        add_shaped(WL_BLOCK_TORCH, 4, 1, 2, pat);
    }
    {
        uint16_t ing[5] = { WL_ITEM_COAL, WL_BLOCK_SAND, WL_BLOCK_SAND,
                            WL_BLOCK_SAND, WL_BLOCK_SAND };
        add_shapeless(WL_BLOCK_GLASS, 4, ing, 5); /* no furnace yet: see docs */
    }
    {
        uint16_t pat[4] = { WL_BLOCK_SAND, WL_BLOCK_SAND,
                            WL_BLOCK_SAND, WL_BLOCK_SAND };
        add_shaped(WL_BLOCK_SANDSTONE, 1, 2, 2, pat);
    }

    /* furnace: 8 cobblestone in a ring (standard pattern) */
    {
        uint16_t C = WL_BLOCK_COBBLE;
        uint16_t pat[9] = { C, C, C,  C, 0, C,  C, C, C };
        add_shaped(WL_BLOCK_FURNACE, 1, 3, 3, pat);
    }

    /* tools: material row + stick handle */
    struct { uint16_t mat; uint16_t pick, axe, shovel, sword; } tiers[] = {
        { WL_BLOCK_PLANKS,     WL_ITEM_PICK_WOOD,    WL_ITEM_AXE_WOOD,
          WL_ITEM_SHOVEL_WOOD, WL_ITEM_SWORD_WOOD },
        { WL_BLOCK_COBBLE,     WL_ITEM_PICK_STONE,   WL_ITEM_AXE_STONE,
          WL_ITEM_SHOVEL_STONE, WL_ITEM_SWORD_STONE },
        { WL_ITEM_IRON_INGOT,   WL_ITEM_PICK_IRON,    WL_ITEM_AXE_IRON,
          WL_ITEM_SHOVEL_IRON, WL_ITEM_SWORD_IRON },
        { WL_ITEM_DIAMOND,     WL_ITEM_PICK_DIAMOND, WL_ITEM_AXE_DIAMOND,
          WL_ITEM_SHOVEL_DIAMOND, WL_ITEM_SWORD_DIAMOND },
    };
    for (size_t t = 0; t < sizeof(tiers) / sizeof(tiers[0]); t++) {
        uint16_t M = tiers[t].mat, S = WL_ITEM_STICK;
        uint16_t pick[9] = { M, M, M,  0, S, 0,  0, S, 0 };
        add_shaped(tiers[t].pick, 1, 3, 3, pick);

        uint16_t axe[6] = { M, M,  M, S,  0, S };
        add_shaped(tiers[t].axe, 1, 2, 3, axe);

        uint16_t shovel[3] = { M, S, S };
        add_shaped(tiers[t].shovel, 1, 1, 3, shovel);

        uint16_t sword[3] = { M, M, S };
        add_shaped(tiers[t].sword, 1, 1, 3, sword);
    }

    g_initialized = true;
    WL_LOGI("crafting registry: %d recipes", g_recipe_count);
}
