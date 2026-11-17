/* assets.c — asset manager: load, cache, fall back, unload. */
#include "wl/assets.h"
#include "wl/util.h"
#include "wl/paths.h"
#include "wl/log.h"
#include "wl/tiles.h"

#include <stdlib.h>
#include <string.h>
#include "rlgl.h" /* rlGetShaderDefault() */

typedef struct {
    char path[WL_PATH_MAX];
    Texture2D tex;
} DynTex;

typedef struct {
    char path[WL_PATH_MAX];
    Sound snd;
    bool ok;
} DynSound;

typedef struct {
    Image img;
    int tile;
} PendingTile;

struct WlAssets {
    char root[WL_PATH_MAX];

    Texture2D tex[WL_TEX_COUNT];
    bool tex_ok[WL_TEX_COUNT];
    Font fonts[WL_FONT_COUNT];
    bool fonts_ok[WL_FONT_COUNT];
    Shader shaders[WL_SHADER_COUNT];
    WlChunkShaderLocs chunk_locs;
    WlEntityShaderLocs entity_locs;
    Sound snds[WL_SND_COUNT];
    bool snds_ok[WL_SND_COUNT];
    Music music[WL_MUS_COUNT];
    bool music_ok[WL_MUS_COUNT];

    Texture2D fallback; /* magenta/black checker */
    Font fallback_font;

    Image atlas_img;
    bool atlas_img_ok;
    PendingTile *pending;
    int pending_count, pending_cap;
    int next_tile;

    DynTex *dyn_tex;
    int dyn_tex_count, dyn_tex_cap;
    DynSound *dyn_snd;
    int dyn_snd_count, dyn_snd_cap;
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static bool build_path(char *out, size_t cap, const WlAssets *a, const char *rel)
{
    char joined[WL_PATH_MAX];
    if (!wl_path_join(joined, sizeof(joined), a->root, rel)) return false;
    wl_scpy(out, joined, cap);
    return true;
}

static Texture2D load_texture_rel(WlAssets *a, const char *rel, bool *ok)
{
    char path[WL_PATH_MAX];
    if (!build_path(path, sizeof(path), a, rel)) { *ok = false; return (Texture2D){0}; }
    Texture2D t = LoadTexture(path);
    *ok = (t.id != 0);
    if (!*ok) {
        WL_LOGW("missing texture: %s (using placeholder)", path);
        return a->fallback;
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* create / destroy                                                    */
/* ------------------------------------------------------------------ */

WlAssets *wl_assets_create(const char *root)
{
    WlAssets *a = calloc(1, sizeof(WlAssets));
    if (!a) {
        WL_LOGE("out of memory creating asset manager");
        return NULL;
    }
    wl_scpy(a->root, root, sizeof(a->root));

    /* placeholder texture + font */
    Image img = GenImageChecked(16, 16, 2, 2, MAGENTA, BLACK);
    a->fallback = LoadTextureFromImage(img);
    UnloadImage(img);
    a->fallback_font = GetFontDefault();

    /* base image of the atlas is loaded now; mods may append tiles */
    char atlas_path[WL_PATH_MAX];
    if (build_path(atlas_path, sizeof(atlas_path), a, "textures/blocks.png")) {
        a->atlas_img = LoadImage(atlas_path);
        a->atlas_img_ok = (a->atlas_img.data != NULL);
        if (!a->atlas_img_ok) {
            WL_LOGW("missing atlas %s; generating placeholder atlas", atlas_path);
            a->atlas_img = GenImageColor(WL_ATLAS_GRID * WL_TILE_PX,
                                         WL_ATLAS_GRID * WL_TILE_PX, GRAY);
            a->atlas_img_ok = true;
        }
    }
    a->next_tile = TILE_BASE_COUNT;

    static const char *tex_files[WL_TEX_COUNT] = {
        NULL, /* atlas handled separately */
        "gui/button.png",
        "gui/panel.png",
        "gui/slot.png",
        "gui/slot_selected.png",
        "gui/logo.png",
        "gui/heart.png",
        "gui/heart_empty.png",
        "gui/hunger.png",
        "gui/hunger_empty.png",
        "gui/crosshair.png",
        "environment/sun.png",
        "environment/moon.png",
        "environment/clouds.png",
        "textures/entities.png",
    };
    for (int i = 1; i < (int)WL_TEX_COUNT; i++)
        a->tex[i] = load_texture_rel(a, tex_files[i], &a->tex_ok[i]);
    a->tex_ok[WL_TEX_ATLAS] = false; /* until finalize */

    struct { WlFontId id; const char *rel; int size; } font_files[WL_FONT_COUNT] = {
        { WL_FONT_UI,    "fonts/VT323-Regular.ttf",        48 },
        { WL_FONT_TITLE, "fonts/PressStart2P-Regular.ttf", 24 },
    };
    for (int i = 0; i < (int)WL_FONT_COUNT; i++) {
        char path[WL_PATH_MAX];
        if (!build_path(path, sizeof(path), a, font_files[i].rel)) continue;
        a->fonts[i] = LoadFontEx(path, font_files[i].size, NULL, 0);
        a->fonts_ok[i] = (a->fonts[i].texture.id != 0);
        if (!a->fonts_ok[i]) {
            WL_LOGW("missing font: %s (using default)", path);
            a->fonts[i] = a->fallback_font;
        }
    }

    /* chunk shader: try GLSL 330, fall back to the compat variant */
    char vs330[WL_PATH_MAX], fs330[WL_PATH_MAX], vsc[WL_PATH_MAX], fsc[WL_PATH_MAX];
    bool p1 = build_path(vs330, sizeof(vs330), a, "shaders/chunk_330.vs");
    bool p2 = build_path(fs330, sizeof(fs330), a, "shaders/chunk_330.fs");
    bool p3 = build_path(vsc, sizeof(vsc), a, "shaders/chunk_compat.vs");
    bool p4 = build_path(fsc, sizeof(fsc), a, "shaders/chunk_compat.fs");
    if (p1 && p2) a->shaders[WL_SHADER_CHUNK] = LoadShader(vs330, fs330);
    if (a->shaders[WL_SHADER_CHUNK].id == 0 && p3 && p4) {
        WL_LOGW("GLSL 330 shader failed; falling back to compat shaders");
        a->shaders[WL_SHADER_CHUNK] = LoadShader(vsc, fsc);
    }
    if (a->shaders[WL_SHADER_CHUNK].id == 0) {
        WL_LOGE("chunk shaders failed to load; using raylib default (visuals degraded)");
        a->shaders[WL_SHADER_CHUNK] = LoadShader(NULL, NULL);
    } else {
        Shader s = a->shaders[WL_SHADER_CHUNK];
        a->chunk_locs.fog_color = GetShaderLocation(s, "fogColor");
        a->chunk_locs.fog_range = GetShaderLocation(s, "fogRange");
        a->chunk_locs.light_scale = GetShaderLocation(s, "lightScale");
        a->chunk_locs.uv_offset = GetShaderLocation(s, "uvOffset");
    }

    /* entity shader: mobs, dropped items, held items */
    bool e1 = build_path(vs330, sizeof(vs330), a, "shaders/entity_330.vs");
    bool e2 = build_path(fs330, sizeof(fs330), a, "shaders/entity_330.fs");
    bool e3 = build_path(vsc, sizeof(vsc), a, "shaders/entity_compat.vs");
    bool e4 = build_path(fsc, sizeof(fsc), a, "shaders/entity_compat.fs");
    if (e1 && e2) a->shaders[WL_SHADER_ENTITY] = LoadShader(vs330, fs330);
    if (a->shaders[WL_SHADER_ENTITY].id == 0 && e3 && e4) {
        WL_LOGW("GLSL 330 entity shader failed; falling back to compat");
        a->shaders[WL_SHADER_ENTITY] = LoadShader(vsc, fsc);
    }
    if (a->shaders[WL_SHADER_ENTITY].id == 0) {
        WL_LOGE("entity shaders failed to load; entities use the default shader");
        a->shaders[WL_SHADER_ENTITY] = LoadShader(NULL, NULL);
    } else {
        Shader s = a->shaders[WL_SHADER_ENTITY];
        a->entity_locs.entity_light = GetShaderLocation(s, "entityLight");
        a->entity_locs.hurt_flash = GetShaderLocation(s, "hurtFlash");
        a->entity_locs.fog_color = GetShaderLocation(s, "fogColor");
        a->entity_locs.fog_range = GetShaderLocation(s, "fogRange");
    }

    static const char *snd_files[WL_SND_COUNT] = {
        "audio/click.wav", "audio/place.wav", "audio/break_stone.wav",
        "audio/break_dirt.wav", "audio/break_wood.wav", "audio/step_grass.wav",
        "audio/step_stone.wav", "audio/hurt.wav", "audio/eat.wav",
        "audio/splash.wav",
        "audio/mob_hurt.wav",
        "audio/bow.wav",
        "audio/pickup.wav",
    };
    for (int i = 0; i < (int)WL_SND_COUNT; i++) {
        char path[WL_PATH_MAX];
        if (!build_path(path, sizeof(path), a, snd_files[i])) continue;
        a->snds[i] = LoadSound(path);
        a->snds_ok[i] = (a->snds[i].frameCount > 0);
        if (!a->snds_ok[i]) WL_LOGW("missing sound: %s (silent)", path);
    }

    static const char *mus_files[WL_MUS_COUNT] = {
        "music/menu.wav", "music/day.wav", "music/night.wav",
    };
    for (int i = 0; i < (int)WL_MUS_COUNT; i++) {
        char path[WL_PATH_MAX];
        if (!build_path(path, sizeof(path), a, mus_files[i])) continue;
        a->music[i] = LoadMusicStream(path);
        a->music_ok[i] = (a->music[i].frameCount > 0);
        if (!a->music_ok[i]) WL_LOGW("missing music: %s (silent)", path);
    }

    WL_LOGI("asset manager ready (root %s)", a->root);
    return a;
}

void wl_assets_destroy(WlAssets *a)
{
    if (!a) return;
    for (int i = 0; i < (int)WL_TEX_COUNT; i++)
        if (i != (int)WL_TEX_ATLAS && a->tex_ok[i]) UnloadTexture(a->tex[i]);
    if (a->tex_ok[WL_TEX_ATLAS]) UnloadTexture(a->tex[WL_TEX_ATLAS]);
    if (a->fallback.id != 0) UnloadTexture(a->fallback);

    for (int i = 0; i < (int)WL_FONT_COUNT; i++)
        if (a->fonts_ok[i]) UnloadFont(a->fonts[i]);

    if (a->shaders[WL_SHADER_CHUNK].id != 0 &&
        a->shaders[WL_SHADER_CHUNK].id != rlGetShaderIdDefault())
        UnloadShader(a->shaders[WL_SHADER_CHUNK]);

    for (int i = 0; i < (int)WL_SND_COUNT; i++)
        if (a->snds_ok[i]) UnloadSound(a->snds[i]);
    for (int i = 0; i < (int)WL_MUS_COUNT; i++)
        if (a->music_ok[i]) UnloadMusicStream(a->music[i]);

    if (a->atlas_img_ok) UnloadImage(a->atlas_img);
    for (int i = 0; i < a->pending_count; i++) UnloadImage(a->pending[i].img);
    free(a->pending);

    for (int i = 0; i < a->dyn_tex_count; i++)
        if (a->dyn_tex[i].tex.id != 0) UnloadTexture(a->dyn_tex[i].tex);
    free(a->dyn_tex);
    for (int i = 0; i < a->dyn_snd_count; i++)
        if (a->dyn_snd[i].ok) UnloadSound(a->dyn_snd[i].snd);
    free(a->dyn_snd);

    free(a);
    WL_LOGI("asset manager shut down");
}

/* ------------------------------------------------------------------ */
/* atlas composition (base + mod tiles)                                */
/* ------------------------------------------------------------------ */

int wl_assets_add_tile(WlAssets *a, const char *png_path, const char *rel_base)
{
    if (!a || !png_path) return -1;

    char path[WL_PATH_MAX];
    wl_scpy(path, png_path, sizeof(path));
    if (!wl_file_exists(path) && rel_base && rel_base[0] != '\0') {
        char joined[WL_PATH_MAX];
        if (wl_path_join(joined, sizeof(joined), rel_base, png_path))
            wl_scpy(path, joined, sizeof(path));
    }
    if (!wl_file_exists(path)) {
        WL_LOGW("mod texture not found: %s", png_path);
        return -1;
    }
    if (a->next_tile >= WL_ATLAS_GRID * WL_ATLAS_GRID) {
        WL_LOGE("atlas full; cannot add mod tile %s", png_path);
        return -1;
    }

    Image img = LoadImage(path);
    if (img.data == NULL) {
        WL_LOGW("mod texture failed to load: %s", path);
        return -1;
    }

    if (a->pending_count == a->pending_cap) {
        int cap = a->pending_cap ? a->pending_cap * 2 : 16;
        PendingTile *p = realloc(a->pending, (size_t)cap * sizeof(PendingTile));
        if (!p) {
            UnloadImage(img);
            WL_LOGE("out of memory queueing mod tile");
            return -1;
        }
        a->pending = p;
        a->pending_cap = cap;
    }
    int tile = a->next_tile++;
    a->pending[a->pending_count].img = img;
    a->pending[a->pending_count].tile = tile;
    a->pending_count++;
    WL_LOGD("queued atlas tile #%d from %s", tile, path);
    return tile;
}

int wl_assets_next_free_tile(const WlAssets *a)
{
    return a ? a->next_tile : -1;
}

bool wl_assets_finalize_atlas(WlAssets *a)
{
    if (!a || !a->atlas_img_ok) return false;

    if (a->tex_ok[WL_TEX_ATLAS]) return true; /* already finalized */

    int px = WL_ATLAS_GRID * WL_TILE_PX;
    if (a->atlas_img.width != px || a->atlas_img.height != px) {
        WL_LOGW("atlas has unexpected size %dx%d (expected %dx%d)",
                a->atlas_img.width, a->atlas_img.height, px, px);
    }

    int mod_tiles = a->pending_count;
    for (int i = 0; i < a->pending_count; i++) {
        int col = a->pending[i].tile % WL_ATLAS_GRID;
        int row = a->pending[i].tile / WL_ATLAS_GRID;
        Image tile = a->pending[i].img;
        if (tile.width != WL_TILE_PX || tile.height != WL_TILE_PX)
            ImageResize(&tile, WL_TILE_PX, WL_TILE_PX);
        ImageDrawImage(&a->atlas_img, tile, col * WL_TILE_PX, row * WL_TILE_PX, WHITE);
        UnloadImage(a->pending[i].img);
        if (tile.data != a->pending[i].img.data) UnloadImage(tile);
    }
    a->pending_count = 0;

    if (a->tex[WL_TEX_ATLAS].id != 0) UnloadTexture(a->tex[WL_TEX_ATLAS]);
    a->tex[WL_TEX_ATLAS] = LoadTextureFromImage(a->atlas_img);
    a->tex_ok[WL_TEX_ATLAS] = (a->tex[WL_TEX_ATLAS].id != 0);
    SetTextureFilter(a->tex[WL_TEX_ATLAS], TEXTURE_FILTER_POINT);

    /* Minification filtering. POINT sampling without mipmaps aliases
     * badly once a 16-texel face shrinks below ~16 pixels on screen
     * (about 30 blocks out at default FOV): every sub-pixel camera move
     * flips which texels get sampled and distant terrain shimmers and
     * crawls — the "glitchy textures" symptom. The atlas is a
     * power-of-two 256px grid of 16px tiles, so mip levels 1..4 average
     * texels strictly within one tile (16 = 2^4, tile-aligned boxes),
     * and with NEAREST sampling inside a level the mesher's half-texel
     * UV inset keeps every sample on the correct tile: no cross-tile
     * bleeding at any distance. LINEAR blending between levels removes
     * the shimmer; MAG stays NEAREST so the crisp pixel-art look up
     * close is unchanged. (faces beyond render distance never exceed
     * mip 4 in practice, and fog covers that range anyway.) */
    GenTextureMipmaps(&a->tex[WL_TEX_ATLAS]);
    if (a->tex[WL_TEX_ATLAS].mipmaps > 1) {
        rlTextureParameters(a->tex[WL_TEX_ATLAS].id, RL_TEXTURE_MIN_FILTER,
                            RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR);
        rlTextureParameters(a->tex[WL_TEX_ATLAS].id, RL_TEXTURE_MAG_FILTER,
                            RL_TEXTURE_FILTER_NEAREST);
        WL_LOGD("atlas sampling: %d mip levels, NEAREST mag / "
                "NEAREST_MIPMAP_LINEAR min", a->tex[WL_TEX_ATLAS].mipmaps);
    } else {
        WL_LOGW("atlas mipmaps could not be generated; distant textures "
                "may shimmer");
    }

    UnloadImage(a->atlas_img);
    a->atlas_img_ok = false;

    WL_LOGI("atlas finalized (%d tiles reserved, %d from mods)",
            a->next_tile, mod_tiles);
    return a->tex_ok[WL_TEX_ATLAS];
}

/* ------------------------------------------------------------------ */
/* accessors                                                           */
/* ------------------------------------------------------------------ */

Texture2D wl_assets_tex(WlAssets *a, WlTexId id)
{
    if (!a || id < 0 || id >= WL_TEX_COUNT || !a->tex_ok[id]) return a ? a->fallback : (Texture2D){0};
    return a->tex[id];
}

Font wl_assets_font(WlAssets *a, WlFontId id)
{
    if (!a || id < 0 || id >= WL_FONT_COUNT || !a->fonts_ok[id])
        return a ? a->fallback_font : GetFontDefault();
    return a->fonts[id];
}

Shader wl_assets_shader(WlAssets *a, WlShaderId id)
{
    if (!a || id < 0 || id >= WL_SHADER_COUNT) return LoadShader(NULL, NULL);
    return a->shaders[id];
}

const WlChunkShaderLocs *wl_assets_chunk_locs(WlAssets *a)
{
    return a ? &a->chunk_locs : NULL;
}

const WlEntityShaderLocs *wl_assets_entity_locs(WlAssets *a)
{
    return a ? &a->entity_locs : NULL;
}

Sound wl_assets_snd(WlAssets *a, WlSndId id)
{
    if (!a || id < 0 || id >= WL_SND_COUNT || !a->snds_ok[id]) return (Sound){0};
    return a->snds[id];
}

bool wl_assets_snd_ok(WlAssets *a, WlSndId id)
{
    return a && id >= 0 && id < WL_SND_COUNT && a->snds_ok[id];
}

Music wl_assets_music(WlAssets *a, WlMusId id)
{
    if (!a || id < 0 || id >= WL_MUS_COUNT || !a->music_ok[id]) return (Music){0};
    return a->music[id];
}

bool wl_assets_music_ok(WlAssets *a, WlMusId id)
{
    return a && id >= 0 && id < WL_MUS_COUNT && a->music_ok[id];
}

/* ------------------------------------------------------------------ */
/* dynamic cached loads                                                */
/* ------------------------------------------------------------------ */

Texture2D wl_assets_load_texture(WlAssets *a, const char *path)
{
    if (!a || !path) return (Texture2D){0};
    for (int i = 0; i < a->dyn_tex_count; i++)
        if (strcmp(a->dyn_tex[i].path, path) == 0) return a->dyn_tex[i].tex;

    Texture2D t = LoadTexture(path);
    if (t.id == 0) {
        WL_LOGW("texture not found: %s (placeholder)", path);
        return a->fallback;
    }
    if (a->dyn_tex_count == a->dyn_tex_cap) {
        int cap = a->dyn_tex_cap ? a->dyn_tex_cap * 2 : 16;
        DynTex *p = realloc(a->dyn_tex, (size_t)cap * sizeof(DynTex));
        if (!p) return t;
        a->dyn_tex = p;
        a->dyn_tex_cap = cap;
    }
    wl_scpy(a->dyn_tex[a->dyn_tex_count].path, path, WL_PATH_MAX);
    a->dyn_tex[a->dyn_tex_count].tex = t;
    a->dyn_tex_count++;
    return t;
}

Sound wl_assets_load_sound(WlAssets *a, const char *path)
{
    if (!a || !path) return (Sound){0};
    for (int i = 0; i < a->dyn_snd_count; i++)
        if (strcmp(a->dyn_snd[i].path, path) == 0) return a->dyn_snd[i].snd;

    Sound s = LoadSound(path);
    if (s.frameCount == 0) {
        WL_LOGW("sound not found: %s (silent)", path);
        return (Sound){0};
    }
    if (a->dyn_snd_count == a->dyn_snd_cap) {
        int cap = a->dyn_snd_cap ? a->dyn_snd_cap * 2 : 16;
        DynSound *p = realloc(a->dyn_snd, (size_t)cap * sizeof(DynSound));
        if (!p) return s;
        a->dyn_snd = p;
        a->dyn_snd_cap = cap;
    }
    wl_scpy(a->dyn_snd[a->dyn_snd_count].path, path, WL_PATH_MAX);
    a->dyn_snd[a->dyn_snd_count].snd = s;
    a->dyn_snd[a->dyn_snd_count].ok = true;
    a->dyn_snd_count++;
    return s;
}
