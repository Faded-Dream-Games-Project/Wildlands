/* chunk.c — chunk lifecycle and block access. */
#include "wl/chunk.h"
#include "wl/block.h"
#include "wl/log.h"

#include <stdlib.h>
#include <string.h>

WlChunk *wl_chunk_alloc(int cx, int cz)
{
    WlChunk *c = calloc(1, sizeof(WlChunk));
    if (!c) {
        WL_LOGE("out of memory allocating chunk %d,%d", cx, cz);
        return NULL;
    }
    c->cx = cx;
    c->cz = cz;
    return c;
}

void wl_chunk_free(WlChunk *c)
{
    if (!c) return;
    if (c->mesh_opaque.valid) {
        UnloadMesh(c->mesh_opaque.mesh);
        c->mesh_opaque.valid = false;
    }
    if (c->mesh_transparent.valid) {
        UnloadMesh(c->mesh_transparent.mesh);
        c->mesh_transparent.valid = false;
    }
    free(c);
}

uint8_t wl_chunk_get(const WlChunk *c, int x, int y, int z)
{
    if (!c || x < 0 || x >= WL_CHUNK_W || z < 0 || z >= WL_CHUNK_W ||
        y < 0 || y >= WL_CHUNK_H)
        return WL_BLOCK_AIR;
    return c->blocks[wl_chunk_index(x, y, z)];
}

bool wl_chunk_set(WlChunk *c, int x, int y, int z, uint8_t id)
{
    if (!c || x < 0 || x >= WL_CHUNK_W || z < 0 || z >= WL_CHUNK_W ||
        y < 0 || y >= WL_CHUNK_H)
        return false;

    c->blocks[wl_chunk_index(x, y, z)] = id;

    /* Keep the per-column opaque-top cache current (used for sky shading). */
    int col = z * WL_CHUNK_W + x;
    if (wl_block_is_opaque(id)) {
        if (y + 1 > c->top[col]) c->top[col] = (uint8_t)(y + 1);
    } else if (y + 1 == c->top[col]) {
        int yy = y;
        while (yy >= 0 && !wl_block_is_opaque(c->blocks[wl_chunk_index(x, yy, z)]))
            yy--;
        c->top[col] = (uint8_t)(yy + 1);
    }
    return true;
}

void wl_chunk_compute_top(WlChunk *c)
{
    if (!c) return;
    for (int z = 0; z < WL_CHUNK_W; z++) {
        for (int x = 0; x < WL_CHUNK_W; x++) {
            int y = WL_CHUNK_H - 1;
            while (y >= 0 && !wl_block_is_opaque(c->blocks[wl_chunk_index(x, y, z)]))
                y--;
            c->top[z * WL_CHUNK_W + x] = (uint8_t)(y + 1);
        }
    }
}
