/* frustum.c — view-frustum extraction and AABB visibility (pure math).
 *
 * Matrix conventions: raylib stores element (row, col) at m[4*col + row]
 * (MatrixLookAt writes its basis rows that way, and OpenGL uploads read
 * the same layout). raymath's MatrixMultiply(A, B) computes B*A, so the
 * combined clip matrix (projection * view, column-vector math) is built
 * here explicitly instead of going through it. */
#include "wl/frustum.h"

#include <math.h>

/* raylib's Matrix struct groups its fields as (m0,m4,m8,m12), (m1,m5,m9,m13),
 * ... in memory — NOT in numeric order. Casting to float* therefore gives
 * flat[4*row + col] == math element (row, col) (e.g. flat[14] == m11).
 * MatrixLookAt writes its basis rows under exactly this convention. */
static float me(const Matrix *m, int row, int col)
{
    return ((const float *)m)[4 * row + col];
}

void wl_frustum_extract(const Matrix *view, const Matrix *proj, Vector4 planes[6])
{
    /* combined = proj * view */
    float c[16];
    for (int i = 0; i < 4; i++) {          /* row */
        for (int j = 0; j < 4; j++) {      /* col */
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += me(proj, i, k) * me(view, k, j);
            c[4 * j + i] = s;
        }
    }

    /* Gribb-Hartmann plane extraction from the combined rows:
     * left = row3+row0, bottom = row3+row1, near = row3+row2,
     * right = row3-row0, top = row3-row1, far = row3-row2 */
    for (int p = 0; p < 6; p++) {
        int o = p % 3;
        float sign = (p < 3) ? 1.0f : -1.0f;
        float a = c[4 * 0 + 3] + sign * c[4 * 0 + o];
        float b = c[4 * 1 + 3] + sign * c[4 * 1 + o];
        float n = c[4 * 2 + 3] + sign * c[4 * 2 + o];
        float d = c[4 * 3 + 3] + sign * c[4 * 3 + o];
        float len = sqrtf(a * a + b * b + n * n);
        if (len > 1e-8f) { a /= len; b /= len; n /= len; d /= len; }
        planes[p] = (Vector4){ a, b, n, d };
    }
}

bool wl_frustum_aabb_visible(const Vector4 planes[6],
                             float x0, float y0, float z0,
                             float x1, float y1, float z1)
{
    for (int p = 0; p < 6; p++) {
        Vector4 pl = planes[p];
        /* positive vertex: the AABB corner farthest along the plane normal */
        float px = pl.x >= 0.0f ? x1 : x0;
        float py = pl.y >= 0.0f ? y1 : y0;
        float pz = pl.z >= 0.0f ? z1 : z0;
        if (pl.x * px + pl.y * py + pl.z * pz + pl.w < 0.0f)
            return false;
    }
    return true;
}
