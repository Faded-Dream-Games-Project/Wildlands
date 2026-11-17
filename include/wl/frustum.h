/* frustum.h — view-frustum plane extraction and AABB tests (pure math). */
#ifndef WL_FRUSTUM_H
#define WL_FRUSTUM_H

#include "wl/wl_common.h"
#include "raylib.h"

/* Extracts the six world-space frustum planes from a view and projection
 * matrix. planes[0..2] = left/bottom/near, planes[3..5] = right/top/far;
 * each plane is (a, b, c, d) with a*x + b*y + c*z + d >= 0 inside. */
void wl_frustum_extract(const Matrix *view, const Matrix *proj, Vector4 planes[6]);

/* Conservative AABB-vs-frustum test (true = possibly visible). */
bool wl_frustum_aabb_visible(const Vector4 planes[6],
                             float x0, float y0, float z0,
                             float x1, float y1, float z1);

#endif /* WL_FRUSTUM_H */
