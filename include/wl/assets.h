/* assets.h — centralized asset loading, caching and fallbacks.
 *
 * Everything graphical/audible is an external file under assets/.
 * Missing assets log a warning and fall back to a placeholder texture or
 * silence — the game keeps running. */
#ifndef WL_ASSETS_H
#define WL_ASSETS_H

#include "wl/wl_common.h"
#include "raylib.h"

typedef enum {
    WL_TEX_ATLAS = 0,   /* block/item atlas (see tiles.h) */
    WL_TEX_GUI_BUTTON,
    WL_TEX_GUI_PANEL,
    WL_TEX_GUI_SLOT,
    WL_TEX_GUI_SLOT_SEL,
    WL_TEX_GUI_LOGO,
    WL_TEX_GUI_HEART,
    WL_TEX_GUI_HEART_EMPTY,
    WL_TEX_GUI_HUNGER,
    WL_TEX_GUI_HUNGER_EMPTY,
    WL_TEX_GUI_CROSSHAIR,
    WL_TEX_ENV_SUN,
    WL_TEX_ENV_MOON,
    WL_TEX_ENV_CLOUDS,
    WL_TEX_ENTITIES,    /* mob/animal model skins (see tools/gen_assets.py) */
    WL_TEX_COUNT
} WlTexId;

typedef enum {
    WL_FONT_UI = 0,   /* VT323 — body/menus/HUD */
    WL_FONT_TITLE,    /* Press Start 2P — logo/headers */
    WL_FONT_COUNT
} WlFontId;

typedef enum {
    WL_SND_CLICK = 0,
    WL_SND_PLACE,
    WL_SND_BREAK_STONE,
    WL_SND_BREAK_DIRT,
    WL_SND_BREAK_WOOD,
    WL_SND_STEP_GRASS,
    WL_SND_STEP_STONE,
    WL_SND_HURT,
    WL_SND_EAT,
    WL_SND_SPLASH,
    WL_SND_MOB_HURT,
    WL_SND_BOW,
    WL_SND_PICKUP,
    WL_SND_THROW,
    WL_SND_STEP_DIRT,
    WL_SND_STEP_WOOD,
    WL_SND_STEP_SAND,
    WL_SND_STEP_SNOW,
    WL_SND_JUMP,
    WL_SND_LAND,
    WL_SND_DEATH,
    WL_SND_SWORD_SWING,
    WL_SND_ARROW_HIT,
    WL_SND_FURNACE_IGNITE,
    WL_SND_FURNACE_DONE,
    WL_SND_ZOMBIE_AMBIENT,
    WL_SND_SKELETON_AMBIENT,
    WL_SND_SPIDER_AMBIENT,
    WL_SND_COW,
    WL_SND_PIG,
    WL_SND_CHICKEN,
    WL_SND_SHEEP,
    WL_SND_AMB_DAY,
    WL_SND_AMB_NIGHT,
    WL_SND_AMB_CAVE,
    WL_SND_COUNT
} WlSndId;

typedef enum {
    WL_MUS_MENU = 0,
    WL_MUS_DAY,
    WL_MUS_NIGHT,
    WL_MUS_COUNT
} WlMusId;

typedef enum {
    WL_SHADER_CHUNK = 0, /* used for chunks, water, clouds */
    WL_SHADER_ENTITY,    /* mobs, drops, held items (local light) */
    WL_SHADER_COUNT
} WlShaderId;

/* custom uniform locations of the chunk shader */
typedef struct {
    int fog_color;
    int fog_range;
    int light_scale;
    int uv_offset;
} WlChunkShaderLocs;

/* custom uniform locations of the entity shader */
typedef struct {
    int entity_light;
    int hurt_flash;
    int fog_color;
    int fog_range;
} WlEntityShaderLocs;

typedef struct WlAssets WlAssets;

/* root = the assets/ directory. Loads the base set immediately. */
WlAssets *wl_assets_create(const char *root);
void wl_assets_destroy(WlAssets *a);

/* Mods append tiles before finalize; returns the tile index or -1. */
int wl_assets_add_tile(WlAssets *a, const char *png_path, const char *rel_base);
/* Composes and uploads the final atlas texture. Call once after all
 * wl_assets_add_tile calls (before first frame). */
bool wl_assets_finalize_atlas(WlAssets *a);
int wl_assets_next_free_tile(const WlAssets *a);

Texture2D wl_assets_tex(WlAssets *a, WlTexId id);
Font wl_assets_font(WlAssets *a, WlFontId id);
Shader wl_assets_shader(WlAssets *a, WlShaderId id);
const WlChunkShaderLocs *wl_assets_chunk_locs(WlAssets *a);
const WlEntityShaderLocs *wl_assets_entity_locs(WlAssets *a);
Sound wl_assets_snd(WlAssets *a, WlSndId id);
bool wl_assets_snd_ok(WlAssets *a, WlSndId id);
Music wl_assets_music(WlAssets *a, WlMusId id);
bool wl_assets_music_ok(WlAssets *a, WlMusId id);

/* Generic cached loads (mods, misc): return placeholder/silence on error. */
Texture2D wl_assets_load_texture(WlAssets *a, const char *path);
Sound wl_assets_load_sound(WlAssets *a, const char *path);

#endif /* WL_ASSETS_H */
