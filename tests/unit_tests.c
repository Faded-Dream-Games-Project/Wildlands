/* unit_tests.c — deterministic tests for the engine's core systems.
 * Run with: make test */
#include "wl/wl_common.h"
#include "wl/util.h"
#include "wl/noise.h"
#include "wl/worldgen.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/inventory.h"
#include "wl/crafting.h"
#include "wl/config.h"
#include "wl/keybinds.h"
#include "wl/world.h"
#include "wl/world_store.h"
#include "wl/chunk.h"
#include "wl/tiles.h"
#include "wl/mesher.h"
#include "wl/frustum.h"
#include "wl/ui.h"
#include "wl/game.h" /* wl_game_key_pressed input seam */
#include "raylib.h" /* key codes for keybind roundtrip */
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        printf("FAIL %s:%d: %s\r\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

/* ------------------------------------------------------------------ */

static void test_util(void)
{
    char buf[8];
    wl_scpy(buf, "0123456789", sizeof(buf));
    CHECK(strlen(buf) == 7); /* truncated, always terminated */

    CHECK(wl_clampf(5.0f, 0.0f, 1.0f) == 1.0f);
    CHECK(wl_clampi(-3, 0, 10) == 0);

    long v = 0;
    CHECK(wl_parse_long("42", &v) && v == 42);
    CHECK(!wl_parse_long("4x2", &v));
    float f = 0;
    CHECK(wl_parse_float("1.5", &f) && f == 1.5f);
    bool b = false;
    CHECK(wl_parse_bool("yes", &b) && b);
    CHECK(wl_parse_bool("0", &b) && !b);

    char key[16], val[64];
    CHECK(wl_kv_line("  name = My World  # not a comment", key, sizeof(key), val, sizeof(val)));
    CHECK(wl_str_eq(key, "name") && wl_str_eq(val, "My World  # not a comment"));
    CHECK(!wl_kv_line("# comment", key, sizeof(key), val, sizeof(val)));
}

static void test_rle(void)
{
    /* roundtrip on smooth and worst-case data */
    uint8_t in[WL_CHUNK_VOL], out[WL_CHUNK_VOL], enc[WL_CHUNK_VOL * 2];
    memset(in, 7, sizeof(in));
    size_t n = wl_rle_encode(in, sizeof(in), enc, sizeof(enc));
    CHECK(n == 2 * ((sizeof(in) + 254) / 255)); /* 32768 -> 129 runs */
    CHECK(wl_rle_decode(enc, n, out, sizeof(out)) == sizeof(in));
    CHECK(memcmp(in, out, sizeof(in)) == 0);

    /* worst case: alternating bytes */
    for (size_t i = 0; i < sizeof(in); i++) in[i] = (uint8_t)(i & 1);
    n = wl_rle_encode(in, sizeof(in), enc, sizeof(enc));
    CHECK(n == 2 * sizeof(in));
    CHECK(wl_rle_decode(enc, n, out, sizeof(out)) == sizeof(in));
    CHECK(memcmp(in, out, sizeof(in)) == 0);

    /* malformed input must not overflow the output buffer */
    CHECK(wl_rle_decode(enc, n, out, sizeof(out) - 1) == 0);
}

static void test_noise(void)
{
    WlRng a, b;
    wl_rng_seed(&a, 12345);
    wl_rng_seed(&b, 12345);
    bool same = true;
    for (int i = 0; i < 100; i++)
        if (wl_rng_u32(&a) != wl_rng_u32(&b)) same = false;
    CHECK(same);

    /* same input, same output; different seeds differ */
    CHECK(wl_noise_fbm2(777, 1.25f, -3.5f, 4) == wl_noise_fbm2(777, 1.25f, -3.5f, 4));
    CHECK(wl_noise_fbm2(777, 1.25f, -3.5f, 4) != wl_noise_fbm2(778, 1.25f, -3.5f, 4));

    /* value noise stays in range */
    bool in_range = true;
    for (int i = 0; i < 200; i++) {
        float v = wl_noise_value3(42, i * 0.37f, -i * 0.11f, i * 0.03f);
        if (v < -1.001f || v > 1.001f) in_range = false;
    }
    CHECK(in_range);

    /* rng range */
    wl_rng_seed(&a, 1);
    bool range_ok = true;
    for (int i = 0; i < 500; i++) {
        int r = wl_rng_range(&a, 3, 7);
        if (r < 3 || r > 7) range_ok = false;
    }
    CHECK(range_ok);
}

static void test_worldgen_determinism(void)
{
    WlWorldGen wg;
    wl_worldgen_init(&wg, 987654321, WL_WORLDTYPE_STANDARD);

    static uint8_t a[WL_CHUNK_VOL], b[WL_CHUNK_VOL];
    static uint8_t ba[WL_CHUNK_AREA], bb[WL_CHUNK_AREA];

    wl_worldgen_chunk(&wg, -3, 7, a, ba);
    wl_worldgen_chunk(&wg, -3, 7, b, bb);
    CHECK(memcmp(a, b, sizeof(a)) == 0);
    CHECK(memcmp(ba, bb, sizeof(ba)) == 0);

    wl_worldgen_init(&wg, 111222333, WL_WORLDTYPE_STANDARD);
    wl_worldgen_chunk(&wg, -3, 7, b, bb);
    CHECK(memcmp(a, b, sizeof(a)) != 0); /* different seed, different world */

    /* superflat is solid ground with no air pockets below the surface */
    wl_worldgen_init(&wg, 5, WL_WORLDTYPE_SUPERFLAT);
    wl_worldgen_chunk(&wg, 0, 0, a, ba);
    bool solid = true;
    for (int y = 0; y <= 4; y++)
        for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++)
                if (a[wl_chunk_index(x, y, z)] == WL_BLOCK_AIR) solid = false;
    CHECK(solid);
    CHECK(a[wl_chunk_index(8, 20, 8)] == WL_BLOCK_AIR); /* and air above */

    /* spawn search finds land */
    wl_worldgen_init(&wg, 42, WL_WORLDTYPE_STANDARD);
    int sx = 0, sz = 0;
    CHECK(wl_worldgen_find_spawn(&wg, &sx, &sz));
    CHECK(wl_worldgen_height_at(&wg, sx, sz) > WL_SEA_LEVEL);
}

static void test_registries(void)
{
    wl_blocks_init();
    wl_items_init();
    wl_crafting_init();

    CHECK(wl_blocks_count() >= WL_BLOCK_COUNT_BASE);
    const WlBlock *stone = wl_block_get(WL_BLOCK_STONE);
    CHECK(stone && wl_str_eq(stone->name, "stone"));
    CHECK(stone->drop_item == WL_BLOCK_COBBLE);

    /* item ids mirror block ids for the base game */
    for (int b = 1; b < WL_BLOCK_COUNT_BASE; b++) {
        const WlItem *it = wl_item_get((WlItemId)b);
        CHECK(it != NULL);
        if (b != WL_BLOCK_WATER) CHECK(it->is_block && it->block_id == b);
        else CHECK(!it->is_block); /* water shadow item is not placeable */
    }

    /* tool chain sanity */
    const WlItem *pick = wl_item_get(WL_ITEM_PICK_DIAMOND);
    CHECK(pick && pick->tool == WL_TOOL_PICKAXE && pick->tool_tier == 4);
    CHECK(pick->durability == 1562);
}

static void test_inventory(void)
{
    WlInventory inv;
    wl_inv_init(&inv);
    CHECK(inv.slots[0].item == WL_ITEM_NONE);

    CHECK(wl_inv_add(&inv, WL_BLOCK_DIRT, 100, 0));
    CHECK(wl_inv_count(&inv, WL_BLOCK_DIRT) == 100); /* 64 + 36 across stacks */
    CHECK(wl_inv_take(&inv, WL_BLOCK_DIRT, 70));
    CHECK(wl_inv_count(&inv, WL_BLOCK_DIRT) == 30);
    CHECK(!wl_inv_take(&inv, WL_BLOCK_DIRT, 31));
    CHECK(wl_inv_count(&inv, WL_BLOCK_DIRT) == 30);

    /* tools stack to 1 */
    CHECK(wl_inv_add(&inv, WL_ITEM_PICK_WOOD, 2, 60));
    CHECK(wl_inv_count(&inv, WL_ITEM_PICK_WOOD) == 2);
    int occupied = 0;
    for (int i = 0; i < WL_INV_SIZE; i++)
        if (inv.slots[i].item == WL_ITEM_PICK_WOOD) occupied++;
    CHECK(occupied == 2);

    /* consume selected slot */
    inv.slots[0] = (WlSlot){ WL_BLOCK_SAND, 2, 0 };
    wl_inv_consume_slot(&inv, 0);
    CHECK(inv.slots[0].count == 1 && inv.slots[0].item == WL_BLOCK_SAND);
    wl_inv_consume_slot(&inv, 0);
    CHECK(inv.slots[0].item == WL_ITEM_NONE);
}

static void test_crafting(void)
{
    WlSlot grid[9];
    /* canonical empty grid: item == WL_ITEM_NONE everywhere */
    for (int i = 0; i < 9; i++) grid[i] = (WlSlot){ WL_ITEM_NONE, 0, 0 };

    /* any log -> 4 planks (shapeless, group) */
    grid[0] = (WlSlot){ WL_BLOCK_LOG, 3, 0 };
    const WlRecipe *r = wl_crafting_match(grid, 2);
    CHECK(r && r->out_item == WL_BLOCK_PLANKS && r->out_count == 4);
    grid[0] = (WlSlot){ WL_BLOCK_BIRCH_LOG, 1, 0 };
    r = wl_crafting_match(grid, 3);
    CHECK(r && r->out_item == WL_BLOCK_PLANKS);

    /* 2 planks vertical -> 4 sticks, position independent */
    for (int i = 0; i < 9; i++) grid[i] = (WlSlot){ WL_ITEM_NONE, 0, 0 };
    grid[4] = (WlSlot){ WL_BLOCK_PLANKS, 1, 0 };
    grid[7] = (WlSlot){ WL_BLOCK_PLANKS, 1, 0 };
    r = wl_crafting_match(grid, 3);
    CHECK(r && r->out_item == WL_ITEM_STICK && r->out_count == 4);

    /* stone pickaxe: material row + stick handle */
    for (int i = 0; i < 9; i++) grid[i] = (WlSlot){ WL_ITEM_NONE, 0, 0 };
    grid[0] = (WlSlot){ WL_BLOCK_COBBLE, 1, 0 };
    grid[1] = (WlSlot){ WL_BLOCK_COBBLE, 1, 0 };
    grid[2] = (WlSlot){ WL_BLOCK_COBBLE, 1, 0 };
    grid[4] = (WlSlot){ WL_ITEM_STICK, 1, 0 };
    grid[7] = (WlSlot){ WL_ITEM_STICK, 1, 0 };
    r = wl_crafting_match(grid, 3);
    CHECK(r && r->out_item == WL_ITEM_PICK_STONE);

    /* axe pattern must match mirrored (flipped left-right) */
    for (int i = 0; i < 9; i++) grid[i] = (WlSlot){ WL_ITEM_NONE, 0, 0 };
    /* flipped axe placed at columns 1-2:
       .  M  M
       .  S  M
       .  S  .  */
    grid[1] = (WlSlot){ WL_ITEM_RAW_IRON, 1, 0 };
    grid[2] = (WlSlot){ WL_ITEM_RAW_IRON, 1, 0 };
    grid[4] = (WlSlot){ WL_ITEM_STICK, 1, 0 };
    grid[5] = (WlSlot){ WL_ITEM_RAW_IRON, 1, 0 };
    grid[7] = (WlSlot){ WL_ITEM_STICK, 1, 0 };
    r = wl_crafting_match(grid, 3);
    CHECK(r && r->out_item == WL_ITEM_AXE_IRON);

    /* glass: coal + 4 sand anywhere (shapeless) */
    for (int i = 0; i < 9; i++) grid[i] = (WlSlot){ WL_ITEM_NONE, 0, 0 };
    grid[0] = (WlSlot){ WL_ITEM_COAL, 1, 0 };
    grid[2] = (WlSlot){ WL_BLOCK_SAND, 1, 0 };
    grid[4] = (WlSlot){ WL_BLOCK_SAND, 1, 0 };
    grid[6] = (WlSlot){ WL_BLOCK_SAND, 1, 0 };
    grid[8] = (WlSlot){ WL_BLOCK_SAND, 1, 0 };
    r = wl_crafting_match(grid, 3);
    CHECK(r && r->out_item == WL_BLOCK_GLASS && r->out_count == 4);

    /* 2x2 grid cannot make 3-wide recipes */
    for (int i = 0; i < 9; i++) grid[i] = (WlSlot){ WL_ITEM_NONE, 0, 0 };
    grid[0] = grid[1] = (WlSlot){ WL_BLOCK_COBBLE, 1, 0 };
    CHECK(wl_crafting_match(grid, 2) == NULL);

    /* zero-initialized grids (item id 0) also count as empty */
    memset(grid, 0, sizeof(grid));
    grid[0] = (WlSlot){ WL_BLOCK_LOG, 1, 0 };
    r = wl_crafting_match(grid, 2);
    CHECK(r && r->out_item == WL_BLOCK_PLANKS);
}

static void test_config(void)
{
    WlConfig cfg;
    wl_config_defaults(&cfg);
    CHECK(cfg.render_distance == 8 && cfg.vsync && !cfg.msaa);

    const char *path = "tests/.tmp-options.txt";
    wl_delete_tree(path);

    /* missing file keeps defaults */
    CHECK(wl_config_load(&cfg, path));
    CHECK(cfg.render_distance == 8);

    /* bad values get clamped, unknown keys ignored */
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fprintf(f, "render_distance = 999\r\n");
        fprintf(f, "brightness = -5\r\n");
        fprintf(f, "nonsense_key = 1\r\n");
        fprintf(f, "vsync = false\r\n");
        fclose(f);
    }
    CHECK(wl_config_load(&cfg, path));
    CHECK(cfg.render_distance == 16); /* clamped to max */
    CHECK(cfg.brightness == 0.5f);    /* clamped to min */
    CHECK(!cfg.vsync);

    /* save + reload roundtrip */
    cfg.brightness = 1.25f;
    CHECK(wl_config_save(&cfg, path));
    WlConfig cfg2;
    wl_config_defaults(&cfg2);
    CHECK(wl_config_load(&cfg2, path));
    CHECK(cfg2.brightness == 1.25f && !cfg2.vsync);

    wl_delete_tree(path);
}

static void test_keybinds(void)
{
    WlKeybinds kb;
    wl_keybinds_defaults(&kb);
    CHECK(kb.key[WL_ACT_FORWARD] == KEY_W);
    CHECK(kb.key[WL_ACT_JUMP] == KEY_SPACE);

    /* key name roundtrip */
    bool ok = true;
    for (int k = KEY_A; k <= KEY_Z; k++) {
        const char *name = wl_key_name(k);
        if (!name[0] || wl_key_from_name(name) != k) ok = false;
    }
    CHECK(ok);
    CHECK(wl_key_from_name("LEFT SHIFT") == KEY_LEFT_SHIFT);
    CHECK(wl_key_from_name("left_shift") == KEY_LEFT_SHIFT);
    CHECK(wl_key_from_name("NO_SUCH_KEY") < 0);

    CHECK(wl_action_from_id("forward") == WL_ACT_FORWARD);
    CHECK(wl_action_from_id("bogus") == WL_ACT_COUNT);
}

static void test_world_store(void)
{
    const char *dir = "tests/.tmp-world";
    wl_delete_tree(dir);

    WlWorldMeta meta;
    memset(&meta, 0, sizeof(meta));
    wl_scpy(meta.name, "Test World", sizeof(meta.name));
    meta.seed = 4242;
    meta.type = WL_WORLDTYPE_STANDARD;
    meta.gamemode = WL_WORLD_GAMEMODE_CREATIVE;
    meta.time_ticks = 1234;
    meta.spawn_x = 1.5f;
    meta.spawn_y = 70.0f;
    meta.spawn_z = -2.5f;
    meta.created_unix = 1000;
    meta.last_played_unix = 2000;
    meta.play_seconds = 12.5;
    meta.format_version = WL_WORLD_FORMAT_VERSION;

    CHECK(wl_store_create_world(dir, &meta));

    /* chunk roundtrip */
    static uint8_t blocks[WL_CHUNK_VOL], loaded[WL_CHUNK_VOL];
    for (int i = 0; i < WL_CHUNK_VOL; i++) blocks[i] = (uint8_t)(i % 30);
    CHECK(wl_store_save_chunk(dir, -5, 9, blocks));
    memset(loaded, 0, sizeof(loaded));
    CHECK(wl_store_load_chunk(dir, -5, 9, loaded));
    CHECK(memcmp(blocks, loaded, sizeof(blocks)) == 0);
    CHECK(!wl_store_load_chunk(dir, 100, 100, loaded)); /* never saved */

    /* level roundtrip */
    WlWorldMeta back;
    CHECK(wl_store_load_level(dir, &back));
    CHECK(back.seed == 4242 && back.gamemode == WL_WORLD_GAMEMODE_CREATIVE);
    CHECK(back.time_ticks == 1234);
    CHECK(back.spawn_z == -2.5f);
    CHECK(wl_str_eq(back.name, "Test World"));

    wl_delete_tree(dir);
}

static void test_tiles_contract(void)
{
    /* the atlas layout is a save-format contract: never change existing
     * ids, only append */
    CHECK(TILE_STONE == 0);
    CHECK(TILE_DIRT == 1);
    CHECK(TILE_ICON_APPLE == 39);
    CHECK(TILE_ICON_SWORD_DIAMOND == 55);
    CHECK(TILE_BASE_COUNT == 56);
}

static void test_tile_uv(void)
{
    /* atlas tile UVs: half-texel inset (no bleeding from neighbouring
     * tiles), each tile strictly inside its atlas cell */
    float e = 0.5f / ((float)WL_ATLAS_GRID * (float)WL_TILE_PX);
    float u0, v0, u1, v1;

    wl_tile_uv(0, &u0, &v0, &u1, &v1);
    CHECK(u0 == e && v0 == e);
    CHECK(u1 == 1.0f / WL_ATLAS_GRID - e);
    CHECK(v1 == 1.0f / WL_ATLAS_GRID - e);

    wl_tile_uv(1, &u0, &v0, &u1, &v1);
    CHECK(u0 == 1.0f / WL_ATLAS_GRID + e);
    CHECK(u1 == 2.0f / WL_ATLAS_GRID - e);

    bool ok = true;
    for (int t = 0; t < 256; t++) {
        wl_tile_uv(t, &u0, &v0, &u1, &v1);
        int col = t % WL_ATLAS_GRID, row = t / WL_ATLAS_GRID;
        if (!(u1 > u0 && v1 > v0)) ok = false;
        if (!(u0 >= (float)col / WL_ATLAS_GRID &&
              u1 <= (float)(col + 1) / WL_ATLAS_GRID)) ok = false;
        if (!(v0 >= (float)row / WL_ATLAS_GRID &&
              v1 <= (float)(row + 1) / WL_ATLAS_GRID)) ok = false;
    }
    CHECK(ok);
}

static void test_atlas_mip_alignment(void)
{
    /* The atlas is minification-filtered (asset_manager.c generates
     * mipmaps and samples NEAREST inside each level, LINEAR between
     * levels). That is only bleed-free while every mip box of a tile
     * stays inside the tile: level k averages 2^k-texel boxes, so the
     * tile size must be a power of two, and levels 0..log2(tile) are
     * tile-pure with level log2(tile) collapsing a tile to one texel.
     * The half-texel UV inset must stay below one mip texel at every
     * used level so NEAREST always resolves to the tile's own edge
     * texel, never the neighbour's. */
    int tile = WL_TILE_PX;
    CHECK(tile > 0 && (tile & (tile - 1)) == 0);      /* power of two */
    int atlas = WL_ATLAS_GRID * WL_TILE_PX;
    CHECK(atlas > 0 && (atlas & (atlas - 1)) == 0);   /* power of two */

    int levels = 0;
    while ((1 << levels) < tile) levels++;            /* tile -> 1 texel */
    CHECK(levels >= 4);                               /* 16px -> 4 levels */

    float e = 0.5f / (float)atlas;                    /* UV inset */
    bool ok = true;
    for (int k = 0; k <= levels && ok; k++) {
        float mip_px = (float)atlas / (float)(1 << k); /* atlas px @ level k */
        if (!(e * mip_px < 1.0f)) ok = false;          /* inset < 1 mip texel */
        if (!((float)tile / (float)(1 << k) >= 1.0f)) ok = false;
    }
    CHECK(ok);
}

static void test_frustum(void)
{
    /* identity-ish view: camera at the origin looking down -Z, up +Y.
     * Symmetric 90-degree fov (horizontal and vertical), near 1, far 100. */
    Matrix view = MatrixLookAt((Vector3){ 0, 0, 0 }, (Vector3){ 0, 0, -1 },
                               (Vector3){ 0, 1, 0 });
    Matrix proj = MatrixFrustum(-1, 1, -1, 1, 1, 100);
    Vector4 f[6];
    wl_frustum_extract(&view, &proj, f);

    CHECK(wl_frustum_aabb_visible(f, 0, 0, -50, 16, 128, -34));        /* ahead */
    CHECK(!wl_frustum_aabb_visible(f, 0, 0, 34, 16, 128, 50));         /* behind */
    CHECK(!wl_frustum_aabb_visible(f, 300, 0, -200, 316, 128, -184));  /* right of fov */
    CHECK(!wl_frustum_aabb_visible(f, 0, 300, -50, 16, 328, -34));     /* above fov */
    CHECK(!wl_frustum_aabb_visible(f, 0, 0, -150, 16, 128, -134));     /* beyond far */
    CHECK(!wl_frustum_aabb_visible(f, 0, 0, -0.9f, 16, 128, -0.5f));   /* before near */
    CHECK(wl_frustum_aabb_visible(f, 16, 16, -50, 32, 32, -34));       /* diagonal */
    CHECK(wl_frustum_aabb_visible(f, 0, 0, -50, 116, 128, -34));       /* straddling */

    /* a game-like camera (fov 75, 16:9, raylib near/far) above ground */
    Vector3 eye = { 8, 80, 8 };
    view = MatrixLookAt(eye, (Vector3){ eye.x, eye.y, eye.z - 1 },
                        (Vector3){ 0, 1, 0 });
    proj = MatrixFrustum(-0.01366f, 0.01366f, -0.00767f, 0.00767f, 0.01f, 1000.0f);
    wl_frustum_extract(&view, &proj, f);
    CHECK(wl_frustum_aabb_visible(f, 0, 0, -16, 16, 128, 0));            /* chunk ahead */
    CHECK(wl_frustum_aabb_visible(f, 16, 0, -80, 32, 128, -64));         /* ahead-right */
    CHECK(!wl_frustum_aabb_visible(f, 0, 0, 16, 16, 128, 32));           /* chunk behind */
    CHECK(!wl_frustum_aabb_visible(f, -800, 0, 800, -784, 128, 816));    /* behind-left */

    /* turning 90 degrees to +X must flip which chunks are visible */
    view = MatrixLookAt(eye, (Vector3){ eye.x + 1, eye.y, eye.z },
                        (Vector3){ 0, 1, 0 });
    wl_frustum_extract(&view, &proj, f);
    CHECK(wl_frustum_aabb_visible(f, 80, 0, -16, 96, 128, 0));           /* now ahead */
    CHECK(!wl_frustum_aabb_visible(f, -96, 0, -80, -80, 128, -64));      /* now behind-left */
}

static void test_textfield_edit(void)
{
    char buf[16];
    int caret;
    bool changed;

    /* typing appends at the caret */
    wl_scpy(buf, "hello", sizeof(buf)); caret = 5;
    changed = wl_ui_textfield_edit(buf, sizeof(buf), &caret, "!", 0, 0, 0, 0, 0, 0);
    CHECK(changed && wl_str_eq(buf, "hello!") && caret == 6);

    /* backspace deletes AT the caret, not only at the end */
    wl_scpy(buf, "hello", sizeof(buf)); caret = 2;
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "", 1, 0, 0, 0, 0, 0);
    CHECK(wl_str_eq(buf, "hllo") && caret == 1);

    /* delete removes the character right of the caret */
    wl_scpy(buf, "hello", sizeof(buf)); caret = 1;
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "", 0, 1, 0, 0, 0, 0);
    CHECK(wl_str_eq(buf, "hllo") && caret == 1);

    /* insertion in the middle */
    wl_scpy(buf, "hllo", sizeof(buf)); caret = 1;
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "e", 0, 0, 0, 0, 0, 0);
    CHECK(wl_str_eq(buf, "hello") && caret == 2);

    /* caret movement: right at the end stays, left moves, home/end jump */
    wl_scpy(buf, "hello", sizeof(buf)); caret = 5;
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "", 0, 0, 0, 1, 0, 0);
    CHECK(caret == 5);
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "", 0, 0, 1, 0, 0, 0);
    CHECK(caret == 4);
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "", 0, 0, 0, 0, 1, 0);
    CHECK(caret == 0);
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "X", 0, 0, 0, 0, 0, 0);
    CHECK(wl_str_eq(buf, "Xhello") && caret == 1);
    wl_ui_textfield_edit(buf, sizeof(buf), &caret, "", 0, 0, 0, 0, 0, 1);
    CHECK(caret == 6);

    /* nothing to do: reports no change */
    wl_scpy(buf, "hello", sizeof(buf)); caret = 3;
    changed = wl_ui_textfield_edit(buf, sizeof(buf), &caret, "", 0, 0, 0, 0, 0, 0);
    CHECK(!changed && wl_str_eq(buf, "hello") && caret == 3);

    /* capacity is respected: the buffer never overflows its NUL */
    wl_scpy(buf, "012345678901234", sizeof(buf)); caret = 15;
    changed = wl_ui_textfield_edit(buf, sizeof(buf), &caret, "xyz", 0, 0, 0, 0, 0, 0);
    CHECK(!changed && strlen(buf) == 15);
}

/* pause/input seams: injected key presses must behave exactly like real
 * ones for the handlers that consume them (one shot, right key only) */
static void test_input_seams(void)
{
    WlGame g;
    memset(&g, 0, sizeof(g));

    g.inject_key = KEY_ENTER;
    CHECK(wl_game_key_pressed(&g, KEY_ENTER));       /* injected press */
    CHECK(!wl_game_key_pressed(&g, KEY_ENTER));      /* consumed */
    CHECK(!wl_game_key_pressed(&g, KEY_SPACE));      /* nothing pending */

    g.inject_key = KEY_ENTER;
    CHECK(!wl_game_key_pressed(&g, KEY_SPACE));      /* other key: untouched */
    CHECK(g.inject_key == KEY_ENTER);                /* still pending */
    CHECK(wl_game_key_pressed(&g, KEY_ENTER));

    g.inject_key = 0;
    CHECK(!wl_game_key_pressed(&g, KEY_ESCAPE));     /* no physical keys in tests */

    /* held-key seam used by --walk */
    WlKeybinds kb;
    wl_keybinds_defaults(&kb);
    CHECK(!wl_keybinds_down(&kb, WL_ACT_FORWARD));
    wl_keybinds_debug_hold(WL_ACT_FORWARD, true);
    CHECK(wl_keybinds_down(&kb, WL_ACT_FORWARD));
    CHECK(!wl_keybinds_down(&kb, WL_ACT_BACK));
    wl_keybinds_debug_hold(WL_ACT_FORWARD, false);
    CHECK(!wl_keybinds_down(&kb, WL_ACT_FORWARD));
}

/* drop entities + partial inventory add (death-drop machinery) */
static void test_drops_and_partial_add(void)
{
    WlInventory inv;
    wl_inv_init(&inv);

    /* partial add: stacks onto the existing pile, then opens new slots;
     * reports how many were actually added */
    wl_inv_add(&inv, WL_BLOCK_LOG, 60, 0);           /* stack of 60 */
    int taken = wl_inv_add_partial(&inv, WL_BLOCK_LOG, 30, 0);
    CHECK(taken == 30);                              /* 4 stack + 26 new slot */
    CHECK(inv.slots[0].count == 64);
    CHECK(wl_inv_count(&inv, WL_BLOCK_LOG) == 90);

    /* a nearly-full inventory accepts only what fits: one open log
     * stack with room for 63, and no empty slots anywhere else */
    wl_inv_init(&inv);
    inv.slots[0].item = WL_BLOCK_LOG;
    inv.slots[0].count = 1;
    for (int i = 1; i < WL_INV_SIZE; i++) {
        inv.slots[i].item = WL_BLOCK_COBBLE;
        inv.slots[i].count = 64;
    }
    taken = wl_inv_add_partial(&inv, WL_BLOCK_LOG, 100, 0);
    CHECK(taken == 63);
    CHECK(wl_inv_count(&inv, WL_BLOCK_LOG) == 64);

    /* tools stack to 1: partial add across empty slots */
    wl_inv_init(&inv);
    taken = wl_inv_add_partial(&inv, WL_ITEM_PICK_IRON, 3, 120);
    CHECK(taken == 3);
    CHECK(wl_inv_count(&inv, WL_ITEM_PICK_IRON) == 3);
    taken = wl_inv_add_partial(&inv, WL_ITEM_PICK_IRON, 40, 0);
    CHECK(taken == 33);                              /* 36 slots - 3 used */
    CHECK(wl_inv_add_partial(&inv, WL_ITEM_APPLE, 5, 0) == 0); /* full */

    /* drop lifecycle: add, physics-only update, save + reload */
    const char *dir = "tests/.tmp-drops";
    wl_delete_tree(dir);
    CHECK(wl_mkdir_p(dir));
    WlWorldParams params;
    memset(&params, 0, sizeof(params));
    wl_scpy(params.name, "DropTest", sizeof(params.name));
    params.seed = 99;
    params.type = WL_WORLDTYPE_STANDARD;
    params.gamemode = WL_WORLD_GAMEMODE_SURVIVAL;
    WlWorld *w = wl_world_create(&params, dir);
    CHECK(w != NULL);
    if (w) {
        Vector3 pos = { 0.5f, 80.0f, 0.5f };
        CHECK(wl_world_add_drop(w, WL_BLOCK_LOG, 12, 0, pos));
        CHECK(wl_world_add_drop(w, WL_ITEM_APPLE, 5, 0, pos));
        int drops = 0;
        wl_world_drops(w, &drops);
        CHECK(drops == 2);

        /* cap: the list refuses extra stacks when full is not tested
         * here (256 spawns); save/load round-trip is what matters */
        CHECK(wl_world_save_drops(w));

        WlWorld *w2 = wl_world_create(&params, dir);
        CHECK(w2 != NULL);
        if (w2) {
            int drops2 = 0;
            const WlDrop *d2 = wl_world_drops(w2, &drops2);
            CHECK(drops2 == 2);
            if (drops2 == 2) {
                CHECK(d2[0].item == WL_BLOCK_LOG && d2[0].count == 12);
                CHECK(d2[1].item == WL_ITEM_APPLE && d2[1].count == 5);
            }
            wl_world_destroy(w2);
        }
        wl_world_destroy(w);
    }
    wl_delete_tree(dir);
}

int main(void)
{
    test_util();
    test_rle();
    test_noise();
    test_worldgen_determinism();
    test_registries();
    test_inventory();
    test_crafting();
    test_config();
    test_keybinds();
    test_world_store();
    test_tiles_contract();
    test_tile_uv();
    test_atlas_mip_alignment();
    test_frustum();
    test_textfield_edit();
    test_input_seams();
    test_drops_and_partial_add();

    printf("unit tests: %d run, %d failed\r\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
