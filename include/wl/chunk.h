/* chunk.h — chunk storage and GPU mesh handles. */
#ifndef WL_CHUNK_H_
#define WL_CHUNK_H_

#include "wl/wl_common.h"
#include "raylib.h" /* Mesh */

#define WL_CHUNK_W 16
#define WL_CHUNK_H 128
#define WL_CHUNK_AREA (WL_CHUNK_W * WL_CHUNK_W)
#define WL_CHUNK_VOL (WL_CHUNK_AREA * WL_CHUNK_H)

/* Local block index (x,z in 0..15, y in 0..127); y-major. */
static inline int wl_chunk_index(int x, int y, int z)
{
    return (y * WL_CHUNK_W + z) * WL_CHUNK_W + x;
}

typedef struct {
    Mesh mesh;      /* raylib mesh, uploaded to the GPU */
    bool valid;     /* mesh holds GPU data */
    int tri_count;
} WlChunkMesh;

typedef struct WlChunk {
    int cx, cz;                       /* chunk-grid coordinates */
    uint8_t blocks[WL_CHUNK_VOL];
    uint8_t biome[WL_CHUNK_AREA];
    uint8_t top[WL_CHUNK_AREA];       /* first free y above highest opaque block */
    bool generated;                   /* block data is valid */
    bool modified;                    /* has player edits, must be saved */
    bool dirty_mesh;                  /* needs remeshing */
    bool queued_gen;                  /* queued for the generation thread */
    WlChunkMesh mesh_opaque;          /* solid + cutout pass */
    WlChunkMesh mesh_transparent;     /* water pass */
    struct WlChunk *hash_next;        /* world hash-table chain */
} WlChunk;

WlChunk *wl_chunk_alloc(int cx, int cz);
/* Frees the chunk and its GPU meshes (main thread only). */
void wl_chunk_free(WlChunk *c);

uint8_t wl_chunk_get(const WlChunk *c, int x, int y, int z); /* AIR outside bounds */
bool wl_chunk_set(WlChunk *c, int x, int y, int z, uint8_t id);
void wl_chunk_compute_top(WlChunk *c);

#endif /* WL_CHUNK_H_ */
