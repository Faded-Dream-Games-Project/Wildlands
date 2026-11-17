/* renderer.c — the frame's 3D pass: sky, sun/moon, chunks, clouds, water. */
#include "wl/renderer.h"
#include "wl/frustum.h"
#include "wl/world.h"
#include "wl/player.h"
#include "wl/config.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/tiles.h"
#include "wl/mesher.h" /* wl_tile_uv */
#include "wl/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
/* direct GL access for the opt-in diagnostics above (vendor log + error
 * trace); glad declarations are provided by the vendored raylib build */
#include "external/glad.h"
#include "wl/util.h"

/* ------------------------------------------------------------------ */
/* sky keyframes                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t tick;
    Color top, horizon;
    float light;
} SkyKey;

/* one full day = 24000 ticks: dawn ~23000..1000, day, dusk ~11000..13000, night */
static const SkyKey k_sky[] = {
    { 0,     (Color){ 90, 110, 160, 255 },  (Color){ 230, 160, 100, 255 }, 0.75f }, /* sunrise */
    { 2500,  (Color){ 110, 160, 255, 255 }, (Color){ 175, 210, 255, 255 }, 1.00f }, /* day */
    { 10500, (Color){ 110, 160, 255, 255 }, (Color){ 175, 210, 255, 255 }, 1.00f },
    { 12000, (Color){ 120, 100, 150, 255 }, (Color){ 245, 140, 90, 255 },  0.70f }, /* sunset */
    { 13500, (Color){ 8, 10, 24, 255 },     (Color){ 16, 20, 40, 255 },    0.28f }, /* night */
    { 22500, (Color){ 8, 10, 24, 255 },     (Color){ 16, 20, 40, 255 },    0.28f },
    { 23500, (Color){ 60, 80, 130, 255 },   (Color){ 200, 150, 110, 255 }, 0.55f }, /* pre-dawn */
    { 24000, (Color){ 90, 110, 160, 255 },  (Color){ 230, 160, 100, 255 }, 0.75f },
};

static Color lerp_color(Color a, Color b, float t)
{
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255
    };
}

static void sky_at(uint32_t ticks, Color *top, Color *horizon, float *light)
{
    size_t n = WL_ARRAY_COUNT(k_sky);
    for (size_t i = 0; i < n - 1; i++) {
        if (ticks >= k_sky[i].tick && ticks < k_sky[i + 1].tick) {
            float t = (float)(ticks - k_sky[i].tick) /
                      (float)(k_sky[i + 1].tick - k_sky[i].tick);
            *top = lerp_color(k_sky[i].top, k_sky[i + 1].top, t);
            *horizon = lerp_color(k_sky[i].horizon, k_sky[i + 1].horizon, t);
            *light = k_sky[i].light + (k_sky[i + 1].light - k_sky[i].light) * t;
            return;
        }
    }
    *top = k_sky[0].top;
    *horizon = k_sky[0].horizon;
    *light = k_sky[0].light;
}

/* ------------------------------------------------------------------ */
/* frustum                                                             */
/* ------------------------------------------------------------------ */

static void extract_frustum(WlRenderer *r)
{
    /* BeginMode3D sets modelview = camera view and projection = frustum;
     * the combined clip matrix must be proj * view. (raymath's
     * MatrixMultiply(A, B) computes B*A, so wl_frustum_extract builds the
     * product explicitly.) */
    Matrix view = rlGetMatrixModelview();
    Matrix proj = rlGetMatrixProjection();
    wl_frustum_extract(&view, &proj, r->frustum);
}

static bool chunk_in_frustum(const WlRenderer *r, int cx, int cz)
{
    float x0 = (float)(cx * WL_CHUNK_W);
    float z0 = (float)(cz * WL_CHUNK_W);
    return wl_frustum_aabb_visible(r->frustum, x0, 0.0f, z0,
                                   x0 + WL_CHUNK_W, (float)WL_CHUNK_H,
                                   z0 + WL_CHUNK_W);
}

/* Chunks entirely beyond the fog range are drawn as pure fog color —
 * skipping them is visually identical and saves the fill cost. */
static bool chunk_beyond_fog(const WlRenderer *r, int cx, int cz)
{
    float x0 = (float)(cx * WL_CHUNK_W);
    float z0 = (float)(cz * WL_CHUNK_W);
    float x1 = x0 + WL_CHUNK_W;
    float z1 = z0 + WL_CHUNK_W;
    Vector3 c = r->cam.position;
    float nx = c.x < x0 ? x0 : (c.x > x1 ? x1 : c.x);
    float ny = c.y < 0.0f ? 0.0f : (c.y > (float)WL_CHUNK_H ? (float)WL_CHUNK_H : c.y);
    float nz = c.z < z0 ? z0 : (c.z > z1 ? z1 : c.z);
    float dx = c.x - nx, dy = c.y - ny, dz = c.z - nz;
    return dx * dx + dy * dy + dz * dz > r->fog_far * r->fog_far;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

/* ---- opt-in GL diagnostics (WL_GL_TRACE=1) ------------------------ */

static bool gl_trace_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *env = getenv("WL_GL_TRACE");
        enabled = (env && env[0] == '1') ? 1 : 0;
        if (enabled) WL_LOGI("GL trace enabled (WL_GL_TRACE)");
    }
    return enabled == 1;
}

void wl_renderer_gl_check(const char *site)
{
    if (!gl_trace_enabled()) return;
    GLenum e = glGetError();
    if (e == GL_NO_ERROR) return;
    /* log each (site, error) pair only once to avoid per-frame spam */
    static char seen[32][96];
    static int seen_count;
    char key[96];
    snprintf(key, sizeof(key), "%s:%04x", site, (unsigned)e);
    for (int i = 0; i < seen_count; i++)
        if (strcmp(seen[i], key) == 0) return;
    if (seen_count < 32) wl_scpy(seen[seen_count++], key, sizeof(seen[0]));
    WL_LOGW("GL error %04x at %s", (unsigned)e, site);
}

const char *wl_renderer_gpu_info(void)
{
    static char info[160];
    if (info[0] == '\0') {
        const char *vendor = (const char *)glGetString(GL_VENDOR);
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        const char *version = (const char *)glGetString(GL_VERSION);
        snprintf(info, sizeof(info), "%s / %s / GL %s",
                 vendor ? vendor : "?", renderer ? renderer : "?",
                 version ? version : "?");
        WL_LOGI("GPU: %s", info);
    }
    return info;
}

void wl_renderer_init(WlRenderer *r, WlAssets *assets)
{
    memset(r, 0, sizeof(*r));
    r->assets = assets;
    r->atlas = wl_assets_tex(assets, WL_TEX_ATLAS);
    r->light_scale = 1.0f;
    r->fog_near = 60.0f;
    r->fog_far = 120.0f;
    r->sky_top = (Color){ 110, 160, 255, 255 };
    r->sky_horizon = (Color){ 175, 210, 255, 255 };
    r->fog_color = r->sky_horizon;

    /* Materials are plain structs sharing the assets-owned shader and
     * atlas; the maps arrays are embedded in WlRenderer so nothing is
     * heap-allocated (UnloadMaterial is never called on these — it would
     * free the shared shader and atlas the asset manager owns). */
    memset(r->mat_maps, 0, sizeof(r->mat_maps));

    r->mat_opaque = (Material){ 0 };
    r->mat_opaque.shader = wl_assets_shader(assets, WL_SHADER_CHUNK);
    r->mat_opaque.maps = r->mat_maps[0];
    r->mat_maps[0][MATERIAL_MAP_DIFFUSE].texture = r->atlas;
    r->mat_maps[0][MATERIAL_MAP_DIFFUSE].color = WHITE;

    r->mat_water = r->mat_opaque; /* same shader, separate maps array */
    r->mat_water.maps = r->mat_maps[1];
    r->mat_maps[1][MATERIAL_MAP_DIFFUSE].texture = r->atlas;
    r->mat_maps[1][MATERIAL_MAP_DIFFUSE].color = WHITE;

    r->mat_cloud = (Material){ 0 };
    r->mat_cloud.shader = r->mat_opaque.shader;
    r->mat_cloud.maps = r->mat_maps[2];
    r->mat_maps[2][MATERIAL_MAP_DIFFUSE].texture =
        wl_assets_tex(assets, WL_TEX_ENV_CLOUDS);
    r->mat_maps[2][MATERIAL_MAP_DIFFUSE].color = WHITE;

    r->cam.up = (Vector3){ 0, 1, 0 };
    r->cam.fovy = 75.0f;
    r->cam.projection = CAMERA_PERSPECTIVE;
    r->cam.position = (Vector3){ 0, 80, 0 };
    r->cam.target = (Vector3){ 0, 79, -1 };

    /* cloud plane: 1024x1024 quad, UV repeat 4x.
     * Arrays must be heap-allocated: UnloadMesh() frees them. */
    const float S = 1024.0f;
    const float U = 4.0f;
    float *pos = malloc(12 * sizeof(float));
    float *uv = malloc(8 * sizeof(float));
    unsigned char *col = malloc(16);
    unsigned short *idx = malloc(6 * sizeof(unsigned short));
    if (pos && uv && col && idx) {
        const float qpos[12] = {
            -S / 2, 0, -S / 2,   S / 2, 0, -S / 2,   S / 2, 0, S / 2,   -S / 2, 0, S / 2
        };
        const float quv[8] = { 0, 0,  U, 0,  U, U,  0, U };
        const unsigned short qidx[6] = { 0, 1, 2, 0, 2, 3 };
        memcpy(pos, qpos, sizeof(qpos));
        memcpy(uv, quv, sizeof(quv));
        memcpy(idx, qidx, sizeof(qidx));
        for (int i = 0; i < 4; i++) {
            col[i * 4 + 0] = 255; col[i * 4 + 1] = 255;
            col[i * 4 + 2] = 255; col[i * 4 + 3] = 235;
        }

        Mesh m;
        memset(&m, 0, sizeof(m));
        m.vertexCount = 4;
        m.triangleCount = 2;
        m.vertices = pos;
        m.texcoords = uv;
        m.colors = col;
        m.indices = idx;
        UploadMesh(&m, false);
        r->cloud_mesh = m;
        r->cloud_ok = true;
    } else {
        WL_LOGW("cloud mesh could not be allocated; clouds disabled");
        free(pos);
        free(uv);
        free(col);
        free(idx);
        r->cloud_ok = false;
    }
}

void wl_renderer_shutdown(WlRenderer *r)
{
    if (r->cloud_ok) {
        UnloadMesh(r->cloud_mesh);
        r->cloud_ok = false;
    }
    for (int i = 0; i < 256; i++) {
        if (r->item_mesh_ok[i]) {
            UnloadMesh(r->item_mesh[i]);
            r->item_mesh_ok[i] = false;
        }
    }
    /* The materials are plain structs sharing the assets-owned shader and
     * atlas texture — wl_assets_destroy() owns their lifetime, so nothing
     * else is unloaded here. */
    memset(&r->mat_opaque, 0, sizeof(r->mat_opaque));
    memset(&r->mat_water, 0, sizeof(r->mat_water));
    memset(&r->mat_cloud, 0, sizeof(r->mat_cloud));
}

/* ------------------------------------------------------------------ */
/* per-frame setup                                                     */
/* ------------------------------------------------------------------ */

void wl_renderer_begin(WlRenderer *r, const WlPlayer *p, const WlConfig *cfg,
                       uint32_t ticks, bool cam_underwater, int render_distance)
{
    Color top, horizon;
    float light;
    sky_at(ticks, &top, &horizon, &light);

    r->sky_top = top;
    r->sky_horizon = horizon;
    r->light_scale = light * cfg->brightness;
    if (r->light_scale < 0.22f) r->light_scale = 0.22f;

    float rd = (float)render_distance;
    r->fog_color = lerp_color(horizon, BLACK, 0.06f);
    r->fog_near = rd * 16.0f * 0.55f;
    r->fog_far = rd * 16.0f * 0.95f;
    if (cam_underwater) {
        r->fog_color = (Color){ 20, 50, 110, 255 };
        r->fog_near = 3.0f;
        r->fog_far = 16.0f;
    }

    /* camera with view bobbing */
    Vector3 eye = wl_player_eye(p);
    if (cfg->view_bobbing && p->bob_amount > 0.01f) {
        float sway = cosf(p->bob_phase * 0.5f) * 0.045f * p->bob_amount;
        float lift = fabsf(sinf(p->bob_phase)) * 0.07f * p->bob_amount;
        float rx = cosf(p->yaw), rz = -sinf(p->yaw); /* right vector */
        eye.x += rx * sway;
        eye.z += rz * sway;
        eye.y += lift;
    }
    r->cam.position = eye;
    Vector3 dir = wl_player_look_dir(p);
    r->cam.target = (Vector3){ eye.x + dir.x, eye.y + dir.y, eye.z + dir.z };
    r->cam.fovy = (float)cfg->fov;
}

Vector3 wl_renderer_cam_pos(const WlRenderer *r) { return r->cam.position; }
float wl_renderer_light_scale(const WlRenderer *r) { return r->light_scale; }

/* ------------------------------------------------------------------ */
/* world drawing                                                       */
/* ------------------------------------------------------------------ */

static void set_shader_uniforms(WlRenderer *r, float uv_ox, float uv_oy)
{
    const WlChunkShaderLocs *loc = wl_assets_chunk_locs(r->assets);
    Shader sh = wl_assets_shader(r->assets, WL_SHADER_CHUNK);
    if (loc->fog_color >= 0) {
        float c[3] = { r->fog_color.r / 255.0f, r->fog_color.g / 255.0f,
                       r->fog_color.b / 255.0f };
        SetShaderValue(sh, loc->fog_color, c, SHADER_UNIFORM_VEC3);
    }
    if (loc->fog_range >= 0) {
        float fr[2] = { r->fog_near, r->fog_far };
        SetShaderValue(sh, loc->fog_range, fr, SHADER_UNIFORM_VEC2);
    }
    if (loc->light_scale >= 0)
        SetShaderValue(sh, loc->light_scale, &r->light_scale, SHADER_UNIFORM_FLOAT);
    if (loc->uv_offset >= 0) {
        float uv[2] = { uv_ox, uv_oy };
        SetShaderValue(sh, loc->uv_offset, uv, SHADER_UNIFORM_VEC2);
    }
}

void wl_renderer_draw_world(WlRenderer *r, WlWorld *w, const struct WlConfig *cfg,
                            uint32_t ticks, bool cam_underwater)
{
    int width = GetScreenWidth();
    int height = GetScreenHeight();

    /* Clear color + DEPTH before the 3D pass. The depth clear is the
     * load-bearing part: the sky "clear" below is a 2D gradient quad that
     * writes no depth, so without this glClear the depth buffer keeps the
     * previous frame's terrain depths. With GL_LEQUAL any fragment that
     * becomes farther than its stale depth (the eye moved: mouse look,
     * view bobbing, gravity settle) is discarded, and terrain vanishes in
     * patches that show the pale horizon sky through the holes — the
     * "blocks turn white / untextured" symptom. A perfectly still camera
     * reproduces identical depths every frame and looks fine, which is
     * why static screenshots never caught it. (WL_NO_DEPTHCLEAR=1 keeps
     * the old behaviour for regression testing.) */
    static bool no_clear = false;
    static bool no_clear_init = false;
    if (!no_clear_init) {
        const char *env = getenv("WL_NO_DEPTHCLEAR");
        no_clear = (env && env[0] == '1');
        no_clear_init = true;
    }
    if (!no_clear) ClearBackground(r->sky_top);
    wl_renderer_gl_check("world-clear");

    /* sky gradient */
    Color h = r->sky_horizon;
    Color t2 = r->sky_top;
    if (cam_underwater) {
        h = (Color){ 20, 50, 110, 255 };
        t2 = (Color){ 12, 32, 78, 255 };
    }
    DrawRectangleGradientV(0, 0, width, height, t2, h);

    BeginMode3D(r->cam);
    extract_frustum(r);
    set_shader_uniforms(r, 0.0f, 0.0f);
    wl_renderer_gl_check("mode3d-begin");

    /* sun and moon (behind everything, no depth) */
    if (cfg->graphics_quality >= 1 && !cam_underwater) {
        float ang = ((float)ticks / (float)WL_DAY_TICKS) * 2.0f * PI - PI * 0.5f;
        Vector3 c = r->cam.position;
        Vector3 sun = { c.x + cosf(ang) * 420.0f, c.y + sinf(ang) * 420.0f, c.z - 90.0f };
        Vector3 moon = { c.x - cosf(ang) * 420.0f, c.y - sinf(ang) * 420.0f, c.z - 90.0f };
        rlDisableDepthTest();
        Texture sun_tex = wl_assets_tex(r->assets, WL_TEX_ENV_SUN);
        Texture moon_tex = wl_assets_tex(r->assets, WL_TEX_ENV_MOON);
        DrawBillboard(r->cam, sun_tex, sun, 48.0f, WHITE);
        DrawBillboard(r->cam, moon_tex, moon, 34.0f, WHITE);
        rlEnableDepthTest();
    }

    /* opaque + cutout chunk pass */
    r->drawn_chunks = 0;
    r->visible_chunks = 0;
    r->drawn_tris = 0;
    int count = 0;
    WlChunk *const *list = wl_world_chunk_list(w, &count);
    for (int i = 0; i < count; i++) {
        WlChunk *c = (WlChunk *)list[i];
        if (!c->generated || !c->mesh_opaque.valid) continue;
        if (!chunk_in_frustum(r, c->cx, c->cz)) continue;
        r->visible_chunks++;
        if (chunk_beyond_fog(r, c->cx, c->cz)) continue;
        Matrix tf = MatrixTranslate((float)(c->cx * WL_CHUNK_W), 0.0f,
                                    (float)(c->cz * WL_CHUNK_W));
        DrawMesh(c->mesh_opaque.mesh, r->mat_opaque, tf);
        r->drawn_chunks++;
        r->drawn_tris += c->mesh_opaque.tri_count;
    }
    wl_renderer_gl_check("opaque-pass");

    /* clouds: scrolling alpha plane, no depth write */
    if (cfg->graphics_quality >= 1 && r->cloud_ok && !cam_underwater) {
        static float scroll = 0.0f;
        scroll += 0.00002f; /* gentle drift */
        if (scroll > 1.0f) scroll -= 1.0f;
        set_shader_uniforms(r, scroll, 0.0f);
        rlDisableDepthMask();
        Vector3 c = r->cam.position;
        Matrix tf = MatrixTranslate(c.x, WL_CLOUD_HEIGHT, c.z);
        DrawMesh(r->cloud_mesh, r->mat_cloud, tf);
        rlEnableDepthMask();
        set_shader_uniforms(r, 0.0f, 0.0f);
    }
    wl_renderer_gl_check("cloud-pass");

    /* water pass: alpha blend, no depth writes */
    rlDisableDepthMask();
    for (int i = 0; i < count; i++) {
        WlChunk *c = (WlChunk *)list[i];
        if (!c->generated || !c->mesh_transparent.valid) continue;
        if (!chunk_in_frustum(r, c->cx, c->cz)) continue;
        if (chunk_beyond_fog(r, c->cx, c->cz)) continue;
        Matrix tf = MatrixTranslate((float)(c->cx * WL_CHUNK_W), 0.0f,
                                    (float)(c->cz * WL_CHUNK_W));
        DrawMesh(c->mesh_transparent.mesh, r->mat_water, tf);
        r->drawn_tris += c->mesh_transparent.tri_count;
    }
    rlEnableDepthMask();
    wl_renderer_gl_check("water-pass");

    EndMode3D();
}

/* ------------------------------------------------------------------ */
/* item entity meshes (held item + world drops)                        */
/* ------------------------------------------------------------------ */

/* quad facing +axis: emits two triangles wound front-facing (raylib 6.2
 * enables backface culling and the render batch never disables it) */
static void item_face(float *pos, float *uv, unsigned char *col, unsigned short *idx,
                      int *vc, int *ic,
                      const float p[4][3], int tile, unsigned char shade)
{
    float u0, v0, u1, v1;
    wl_tile_uv(tile, &u0, &v0, &u1, &v1);
    for (int i = 0; i < 4; i++) {
        pos[3 * (*vc + i) + 0] = p[i][0];
        pos[3 * (*vc + i) + 1] = p[i][1];
        pos[3 * (*vc + i) + 2] = p[i][2];
        uv[2 * (*vc + i) + 0] = (i == 0 || i == 3) ? u0 : u1;
        uv[2 * (*vc + i) + 1] = (i == 0 || i == 1) ? v1 : v0;
        col[4 * (*vc + i) + 0] = shade;
        col[4 * (*vc + i) + 1] = shade;
        col[4 * (*vc + i) + 2] = shade;
        col[4 * (*vc + i) + 3] = 255;
    }
    idx[(*ic)++] = (unsigned short)(*vc + 0);
    idx[(*ic)++] = (unsigned short)(*vc + 1);
    idx[(*ic)++] = (unsigned short)(*vc + 2);
    idx[(*ic)++] = (unsigned short)(*vc + 0);
    idx[(*ic)++] = (unsigned short)(*vc + 2);
    idx[(*ic)++] = (unsigned short)(*vc + 3);
    *vc += 4;
}

static const Mesh *item_mesh(WlRenderer *r, WlItemId item)
{
    if (item == WL_ITEM_NONE || item >= 256 || !wl_item_valid(item)) return NULL;
    if (r->item_mesh_ok[item]) return &r->item_mesh[item];

    const WlItem *def = wl_item_get(item);
    int tile = -1;
    const WlBlock *blk = NULL;
    if (def->is_block) {
        blk = wl_block_get(def->block_id);
        if (!blk) return NULL;
        if (blk->shape != WL_RENDER_CROSS) tile = -2; /* full cube */
    }
    if (tile >= 0 || tile == -2) {
        /* build */
        Mesh m;
        memset(&m, 0, sizeof(m));
        int cap_v = (tile == -2) ? 24 : 8;  /* cube: 6 faces; sprite: 2 sides */
        int cap_i = (tile == -2) ? 36 : 12;
        float *pos = malloc((size_t)cap_v * 3 * sizeof(float));
        float *uv = malloc((size_t)cap_v * 2 * sizeof(float));
        unsigned char *col = malloc((size_t)cap_v * 4);
        unsigned short *ix = malloc((size_t)cap_i * sizeof(unsigned short));
        if (!pos || !uv || !col || !ix) {
            free(pos); free(uv); free(col); free(ix);
            return NULL;
        }
        int vc = 0, ic = 0;

        if (tile == -2) {
            /* unit cube around the origin, one atlas tile per face */
            static const float faces[6][4][3] = {
                { { .5f,-.5f,-.5f}, { .5f, .5f,-.5f}, { .5f, .5f, .5f}, { .5f,-.5f, .5f} }, /* +X */
                { {-.5f,-.5f, .5f}, {-.5f, .5f, .5f}, {-.5f, .5f,-.5f}, {-.5f,-.5f,-.5f} }, /* -X */
                { {-.5f, .5f, .5f}, { .5f, .5f, .5f}, { .5f, .5f,-.5f}, {-.5f, .5f,-.5f} }, /* +Y */
                { {-.5f,-.5f,-.5f}, { .5f,-.5f,-.5f}, { .5f,-.5f, .5f}, {-.5f,-.5f, .5f} }, /* -Y */
                { {-.5f,-.5f, .5f}, { .5f,-.5f, .5f}, { .5f, .5f, .5f}, {-.5f, .5f, .5f} }, /* +Z */
                { { .5f,-.5f,-.5f}, {-.5f,-.5f,-.5f}, {-.5f, .5f,-.5f}, { .5f, .5f,-.5f} }, /* -Z */
            };
            static const unsigned char shade[6] = { 190, 150, 255, 120, 210, 170 };
            for (int f = 0; f < 6; f++)
                item_face(pos, uv, col, ix, &vc, &ic, faces[f], blk->tex[f], shade[f]);
        } else {
            /* flat sprite, both windings so it is visible from two sides */
            float s = 0.5f;
            float front[4][3] = { {-s,-s,0}, { s,-s,0}, { s, s,0}, {-s, s,0} };
            float back[4][3]  = { { s,-s,0}, {-s,-s,0}, {-s, s,0}, { s, s,0} };
            int t = tile >= 0 ? tile : blk->tex[0];
            item_face(pos, uv, col, ix, &vc, &ic, front, t, 255);
            item_face(pos, uv, col, ix, &vc, &ic, back, t, 255);
        }

        m.vertexCount = vc;
        m.triangleCount = ic / 3;
        m.vertices = pos;
        m.texcoords = uv;
        m.colors = col;
        m.indices = ix;
        UploadMesh(&m, false);
        r->item_mesh[item] = m;
        r->item_mesh_ok[item] = true;
        return &r->item_mesh[item];
    }
    return NULL;
}

void wl_renderer_draw_drops(WlRenderer *r, struct WlWorld *w, uint32_t ticks)
{
    int count = 0;
    const WlDrop *drops = wl_world_drops(w, &count);
    if (!drops || count == 0) return;

    float t = (float)ticks * 0.05f;
    Vector3 cp = r->cam.position;
    BeginMode3D(r->cam);
    set_shader_uniforms(r, 0.0f, 0.0f);
    for (int i = 0; i < count; i++) {
        const WlDrop *d = &drops[i];
        float dx = d->pos.x - cp.x, dz = d->pos.z - cp.z;
        if (dx * dx + dz * dz > r->fog_far * r->fog_far) continue; /* fogged out */
        const Mesh *m = item_mesh(r, d->item);
        if (!m) continue;
        float spin = t * 1.4f + (float)(d->seed & 63) * 0.1f;
        float bob = sinf(t * 2.0f + (float)(d->seed & 31) * 0.2f) * 0.06f;
        Vector3 pos = { d->pos.x, d->pos.y + 0.14f + bob, d->pos.z };
        /* MatrixMultiply(A,B) = B*A, so build T * Ry * S from the right */
        Matrix tf = MatrixMultiply(MatrixScale(0.26f, 0.26f, 0.26f),
                                   MatrixRotateY(spin));
        tf = MatrixMultiply(tf, MatrixTranslate(pos.x, pos.y, pos.z));
        DrawMesh(*m, r->mat_opaque, tf);
    }
    EndMode3D();
}

void wl_renderer_draw_held_item(WlRenderer *r, const struct WlPlayer *p)
{
    if (!p || p->dead) return;
    WlSlot slot = p->inv.slots[p->selected_slot];
    if (slot.item == WL_ITEM_NONE) return; /* empty hand draws nothing */
    const Mesh *m = item_mesh(r, slot.item);
    if (!m) return;

    /* camera-space placement: forward + right + down, tilted like a hand */
    Vector3 f = Vector3Subtract(r->cam.target, r->cam.position);
    f = Vector3Normalize(f);
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    Vector3 right = Vector3CrossProduct(f, up);
    right = Vector3Normalize(right);
    Vector3 pos = Vector3Add(r->cam.position, f);
    pos = Vector3Add(pos, Vector3Scale(right, 0.46f));
    pos.y -= 0.42f;

    float yaw = atan2f(f.x, f.z) + 0.7f;
    Matrix tf = MatrixMultiply(MatrixScale(0.30f, 0.30f, 0.30f),
                               MatrixRotateX(-0.32f));
    tf = MatrixMultiply(tf, MatrixRotateY(yaw));
    tf = MatrixMultiply(tf, MatrixTranslate(pos.x, pos.y, pos.z));

    /* never buried in terrain: flush, clear depth, then draw on top of
     * everything while still lighting/fogging it like the world */
    rlDrawRenderBatchActive();
    glClear(GL_DEPTH_BUFFER_BIT);
    BeginMode3D(r->cam);
    set_shader_uniforms(r, 0.0f, 0.0f);
    DrawMesh(*m, r->mat_opaque, tf);
    EndMode3D();
}
