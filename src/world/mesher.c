/* mesher.c — builds per-chunk meshes.
 *
 * Vertex format: position (3f) + texcoord (2f) + color (4ub, baked shading).
 * Shading = face direction * ambient occlusion * skylight depth, baked per
 * vertex so the fragment shader stays trivial. Water gets an alpha-blended
 * second mesh with a lowered surface. Cutout blocks (plants, leaves, glass)
 * live in the opaque pass with alpha-discard in the shader. */
#include "wl/mesher.h"
#include "wl/world.h"
#include "wl/block.h"
#include "wl/tiles.h"
#include "wl/log.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* atlas UVs                                                           */
/* ------------------------------------------------------------------ */

void wl_tile_uv(int tile, float *u0, float *v0, float *u1, float *v1)
{
    int t = tile & 255; /* clamp into the 256-slot atlas */
    int col = t % WL_ATLAS_GRID;
    int row = t / WL_ATLAS_GRID;
    float s = 1.0f / (float)WL_ATLAS_GRID;
    if (u0) *u0 = (float)col * s;
    if (u1) *u1 = (float)(col + 1) * s;
    if (v0) *v0 = (float)row * s;
    if (v1) *v1 = (float)(row + 1) * s;
}

/* ------------------------------------------------------------------ */
/* growable mesh buffers (u16 indices: cap 65532 vertices per chunk)   */
/* ------------------------------------------------------------------ */

#define WL_MESHER_VERT_CAP 65532

typedef struct {
    float *pos;              /* 3f per vertex */
    float *uv;               /* 2f per vertex */
    unsigned char *col;      /* 4ub per vertex */
    unsigned short *idx;
    int vcount, icount;
    int vcap, icap;
    bool overflow;
} MeshBuf;

static void mb_free(MeshBuf *b)
{
    free(b->pos);
    free(b->uv);
    free(b->col);
    free(b->idx);
    b->pos = NULL;
    b->uv = NULL;
    b->col = NULL;
    b->idx = NULL;
}

static bool mb_init(MeshBuf *b)
{
    memset(b, 0, sizeof(*b));
    b->vcap = 2048;
    b->icap = 4096;
    b->pos = malloc((size_t)b->vcap * 3 * sizeof(float));
    b->uv = malloc((size_t)b->vcap * 2 * sizeof(float));
    b->col = malloc((size_t)b->vcap * 4);
    b->idx = malloc((size_t)b->icap * sizeof(unsigned short));
    return b->pos && b->uv && b->col && b->idx;
}

static bool mb_reserve_v(MeshBuf *b, int add)
{
    if (b->vcount + add <= b->vcap) return true;
    if (b->vcount + add > WL_MESHER_VERT_CAP) {
        b->overflow = true;
        return false;
    }
    int cap = b->vcap;
    while (cap < b->vcount + add) cap *= 2;
    float *p = realloc(b->pos, (size_t)cap * 3 * sizeof(float));
    if (!p) { b->overflow = true; return false; }
    b->pos = p;
    float *u = realloc(b->uv, (size_t)cap * 2 * sizeof(float));
    if (!u) { b->overflow = true; return false; }
    b->uv = u;
    unsigned char *c = realloc(b->col, (size_t)cap * 4);
    if (!c) { b->overflow = true; return false; }
    b->col = c;
    b->vcap = cap;
    return true;
}

static bool mb_reserve_i(MeshBuf *b, int add)
{
    if (b->icount + add <= b->icap) return true;
    int cap = b->icap;
    while (cap < b->icount + add) cap *= 2;
    unsigned short *p = realloc(b->idx, (size_t)cap * sizeof(unsigned short));
    if (!p) { b->overflow = true; return false; }
    b->idx = p;
    b->icap = cap;
    return true;
}

static void emit_quad(MeshBuf *b, const float pos[4][3], float u0, float v0,
                      float u1, float v1, const float bright[4], float alpha)
{
    if (!mb_reserve_v(b, 4) || !mb_reserve_i(b, 6)) return;

    int base = b->vcount;
    for (int k = 0; k < 4; k++) {
        b->pos[(base + k) * 3 + 0] = pos[k][0];
        b->pos[(base + k) * 3 + 1] = pos[k][1];
        b->pos[(base + k) * 3 + 2] = pos[k][2];
    }
    /* corner order is BL, BR, TR, TL */
    const float uvs[4][2] = {
        { u0, v1 }, { u1, v1 }, { u1, v0 }, { u0, v0 }
    };
    for (int k = 0; k < 4; k++) {
        b->uv[(base + k) * 2 + 0] = uvs[k][0];
        b->uv[(base + k) * 2 + 1] = uvs[k][1];
        unsigned char c = (unsigned char)(bright[k] * 255.0f + 0.5f);
        b->col[(base + k) * 4 + 0] = c;
        b->col[(base + k) * 4 + 1] = c;
        b->col[(base + k) * 4 + 2] = c;
        b->col[(base + k) * 4 + 3] = (unsigned char)(alpha * 255.0f + 0.5f);
    }

    /* flip the split diagonal when AO makes it look bent */
    if (bright[0] + bright[2] >= bright[1] + bright[3]) {
        b->idx[b->icount++] = (unsigned short)(base + 0);
        b->idx[b->icount++] = (unsigned short)(base + 1);
        b->idx[b->icount++] = (unsigned short)(base + 2);
        b->idx[b->icount++] = (unsigned short)(base + 0);
        b->idx[b->icount++] = (unsigned short)(base + 2);
        b->idx[b->icount++] = (unsigned short)(base + 3);
    } else {
        b->idx[b->icount++] = (unsigned short)(base + 1);
        b->idx[b->icount++] = (unsigned short)(base + 2);
        b->idx[b->icount++] = (unsigned short)(base + 3);
        b->idx[b->icount++] = (unsigned short)(base + 1);
        b->idx[b->icount++] = (unsigned short)(base + 3);
        b->idx[b->icount++] = (unsigned short)(base + 0);
    }
    b->vcount += 4;
}

/* ------------------------------------------------------------------ */
/* face geometry tables                                                */
/* ------------------------------------------------------------------ */

/* order matches block def tex[6]: +X -X +Y -Y +Z -Z */
static const int8_t FACE_NORMAL[6][3] = {
    { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
    { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
};
/* corners in CCW order seen from outside: BL, BR, TR, TL (unit cube) */
static const int8_t FACE_CORNERS[6][4][3] = {
    /* +X */ { { 1, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 } },
    /* -X */ { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 } },
    /* +Y */ { { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 0 } },
    /* -Y */ { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } },
    /* +Z */ { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } },
    /* -Z */ { { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } },
};
static const float FACE_SHADE[6] = { 0.72f, 0.72f, 1.0f, 0.55f, 0.85f, 0.85f };

/* ------------------------------------------------------------------ */
/* 3x3 chunk neighborhood (fast neighbor lookups)                      */
/* ------------------------------------------------------------------ */

typedef struct {
    WlChunk *ch[3][3]; /* [dx+1][dz+1] */
} Hood;

static void hood_build(Hood *h, WlWorld *w, int cx, int cz)
{
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++)
            h->ch[dx + 1][dz + 1] = wl_world_chunk_at(w, cx + dx, cz + dz);
}

/* lx/lz in [-16, 31] relative to the chunk being meshed */
static uint8_t hood_get(const Hood *h, int lx, int y, int lz)
{
    if (y < 0) return WL_BLOCK_BEDROCK;
    if (y >= WL_CHUNK_H) return WL_BLOCK_AIR;
    int dx = (lx + 16) >> 4;
    int dz = (lz + 16) >> 4;
    const WlChunk *c = h->ch[dx][dz];
    if (!c || !c->generated) return WL_BLOCK_AIR;
    return c->blocks[wl_chunk_index(lx & 15, y, lz & 15)];
}

static int hood_top(const Hood *h, int lx, int lz)
{
    int dx = (lx + 16) >> 4;
    int dz = (lz + 16) >> 4;
    const WlChunk *c = h->ch[dx][dz];
    if (!c || !c->generated) return 0;
    return c->top[(lz & 15) * WL_CHUNK_W + (lx & 15)];
}

static bool hood_opaque(const Hood *h, int lx, int y, int lz)
{
    return wl_block_is_opaque(hood_get(h, lx, y, lz));
}

/* skylight approximation: darker the deeper below the column's top */
static float depth_light(const Hood *h, int lx, int y, int lz)
{
    int top = hood_top(h, lx, lz);
    if (y >= top) return 1.0f;
    float d = 1.0f - (float)(top - y) * 0.09f;
    return d < 0.28f ? 0.28f : d;
}

static float corner_ao(bool s1, bool s2, bool c)
{
    if (s1 && s2) return 0.45f;
    int n = (int)s1 + (int)s2 + (int)c;
    return (n == 0) ? 1.0f : (n == 1 ? 0.80f : 0.62f);
}

/* ------------------------------------------------------------------ */
/* face emission                                                       */
/* ------------------------------------------------------------------ */

static void emit_face(MeshBuf *buf, const Hood *h, int lx, int y, int lz,
                      int dir, const WlBlock *def, bool liquid, float top_off)
{
    const int8_t *n = FACE_NORMAL[dir];
    int nx = lx + n[0], ny = y + n[1], nz = lz + n[2];

    float u0, v0, u1, v1;
    wl_tile_uv(def->tex[dir], &u0, &v0, &u1, &v1);

    float dlight = depth_light(h, nx, ny, nz);
    float shade = FACE_SHADE[dir];

    /* normal axis + the two tangent axes */
    int A = (n[0] != 0) ? 0 : ((n[1] != 0) ? 1 : 2);
    int B = (A + 1) % 3;
    int C = (A + 2) % 3;

    float bright[4];
    float pos[4][3];
    for (int k = 0; k < 4; k++) {
        const int8_t *cor = FACE_CORNERS[dir][k];
        float ao = 1.0f;
        if (!liquid) {
            int sb = cor[B] ? 1 : -1;
            int sc = cor[C] ? 1 : -1;
            int p1[3] = { nx, ny, nz };
            int p2[3] = { nx, ny, nz };
            int p3[3] = { nx, ny, nz };
            p1[B] += sb;
            p2[C] += sc;
            p3[B] += sb;
            p3[C] += sc;
            ao = corner_ao(hood_opaque(h, p1[0], p1[1], p1[2]),
                           hood_opaque(h, p2[0], p2[1], p2[2]),
                           hood_opaque(h, p3[0], p3[1], p3[2]));
        }
        bright[k] = shade * dlight * ao;
        pos[k][0] = (float)(lx + cor[0]);
        pos[k][1] = (float)y + (cor[1] ? top_off : 0.0f);
        pos[k][2] = (float)(lz + cor[2]);
    }

    emit_quad(buf, pos, u0, v0, u1, v1, bright, liquid ? 0.65f : 1.0f);
}

static void emit_cross(MeshBuf *buf, const Hood *h, int lx, int y, int lz,
                       const WlBlock *def)
{
    float u0, v0, u1, v1;
    wl_tile_uv(def->tex[0], &u0, &v0, &u1, &v1);

    float light = depth_light(h, lx, y, lz);
    float bright[4] = { light, light, light, light };

    float pa[4][3] = {
        { (float)lx, (float)y, (float)lz },
        { (float)lx + 1, (float)y, (float)lz + 1 },
        { (float)lx + 1, (float)y + 1, (float)lz + 1 },
        { (float)lx, (float)y + 1, (float)lz },
    };
    float pb[4][3] = {
        { (float)lx + 1, (float)y, (float)lz },
        { (float)lx, (float)y, (float)lz + 1 },
        { (float)lx, (float)y + 1, (float)lz + 1 },
        { (float)lx + 1, (float)y + 1, (float)lz },
    };

    /* both windings so plants are visible from either side */
    float pr[4][3];
    emit_quad(buf, pa, u0, v0, u1, v1, bright, 1.0f);
    pr[0][0] = pa[3][0]; pr[0][1] = pa[3][1]; pr[0][2] = pa[3][2];
    pr[1][0] = pa[2][0]; pr[1][1] = pa[2][1]; pr[1][2] = pa[2][2];
    pr[2][0] = pa[1][0]; pr[2][1] = pa[1][1]; pr[2][2] = pa[1][2];
    pr[3][0] = pa[0][0]; pr[3][1] = pa[0][1]; pr[3][2] = pa[0][2];
    emit_quad(buf, pr, u0, v0, u1, v1, bright, 1.0f);

    emit_quad(buf, pb, u0, v0, u1, v1, bright, 1.0f);
    pr[0][0] = pb[3][0]; pr[0][1] = pb[3][1]; pr[0][2] = pb[3][2];
    pr[1][0] = pb[2][0]; pr[1][1] = pb[2][1]; pr[1][2] = pb[2][2];
    pr[2][0] = pb[1][0]; pr[2][1] = pb[1][1]; pr[2][2] = pb[1][2];
    pr[3][0] = pb[0][0]; pr[3][1] = pb[0][1]; pr[3][2] = pb[0][2];
    emit_quad(buf, pr, u0, v0, u1, v1, bright, 1.0f);
}

/* ------------------------------------------------------------------ */
/* upload to raylib                                                    */
/* ------------------------------------------------------------------ */

static void upload_mesh(WlChunkMesh *m, MeshBuf *b)
{
    if (m->valid) {
        UnloadMesh(m->mesh);
        m->valid = false;
    }
    m->tri_count = b->icount / 3;
    if (b->icount == 0) return; /* nothing to upload; buffers freed by caller */

    Mesh mesh;
    memset(&mesh, 0, sizeof(mesh));
    mesh.vertexCount = b->vcount;
    mesh.triangleCount = b->icount / 3;
    mesh.vertices = b->pos;
    mesh.texcoords = b->uv;
    mesh.colors = b->col;
    mesh.indices = b->idx;
    UploadMesh(&mesh, false);

    m->mesh = mesh;
    m->valid = true;
    /* ownership transferred to the Mesh */
    b->pos = NULL;
    b->uv = NULL;
    b->col = NULL;
    b->idx = NULL;
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */
/* ------------------------------------------------------------------ */

void wl_mesher_build(WlWorld *w, WlChunk *c)
{
    MeshBuf opq, tra;
    if (!mb_init(&opq) || !mb_init(&tra)) {
        WL_LOGE("mesher: out of memory for chunk %d,%d", c->cx, c->cz);
        mb_free(&opq);
        mb_free(&tra);
        return;
    }

    Hood hood;
    hood_build(&hood, w, c->cx, c->cz);

    for (int y = 0; y < WL_CHUNK_H; y++) {
        for (int lz = 0; lz < WL_CHUNK_W; lz++) {
            for (int lx = 0; lx < WL_CHUNK_W; lx++) {
                uint8_t id = c->blocks[wl_chunk_index(lx, y, lz)];
                if (id == WL_BLOCK_AIR) continue;
                const WlBlock *def = wl_block_get(id);
                if (!def) continue;

                if (def->shape == WL_RENDER_CROSS) {
                    emit_cross(&opq, &hood, lx, y, lz, def);
                    continue;
                }

                bool liquid = (def->shape == WL_RENDER_LIQUID);
                MeshBuf *buf = liquid ? &tra : &opq;

                /* water surface sits 2/16 below the block top */
                float top_off = 1.0f;
                if (liquid && hood_get(&hood, lx, y + 1, lz) != id)
                    top_off = 14.0f / 16.0f;

                for (int dir = 0; dir < 6; dir++) {
                    const int8_t *n = FACE_NORMAL[dir];
                    uint8_t nb = hood_get(&hood, lx + n[0], y + n[1], lz + n[2]);

                    if (wl_block_is_opaque(nb)) continue;
                    if (liquid && nb == id) continue;
                    /* same-type see-through neighbors share no inner faces */
                    if (!liquid && nb == id && !def->opaque) continue;

                    emit_face(buf, &hood, lx, y, lz, dir, def, liquid, top_off);
                }
            }
        }
    }

    upload_mesh(&c->mesh_opaque, &opq);
    upload_mesh(&c->mesh_transparent, &tra);
    mb_free(&opq);
    mb_free(&tra);

    c->dirty_mesh = false;
    if (opq.overflow || tra.overflow)
        WL_LOGW("chunk %d,%d mesh hit the vertex cap; visuals may clip", c->cx, c->cz);
}
