/* debug: print extracted planes and per-plane AABB dots for failing cases */
#include <stdio.h>
#include "wl/frustum.h"
#include "raymath.h"

int main(void)
{
    Matrix view = MatrixLookAt((Vector3){0,0,0}, (Vector3){0,0,-1}, (Vector3){0,1,0});
    Matrix proj = MatrixFrustum(-1, 1, -1, 1, 1, 100);
    Vector4 f[6];
    wl_frustum_extract(&view, &proj, f);

    printf("proj: m0=%.3f m5=%.3f m8=%.3f m9=%.3f m10=%.3f m11=%.3f m14=%.3f m15=%.3f\n",
           proj.m0, proj.m5, proj.m8, proj.m9, proj.m10, proj.m11, proj.m14, proj.m15);
    for (int p = 0; p < 6; p++)
        printf("plane %d: (%.4f, %.4f, %.4f, %.4f)\n", p,
               f[p].x, f[p].y, f[p].z, f[p].w);

    struct { const char *name; float b[6]; } cases[] = {
        { "ahead      (0,0,-50,16,128,-34)",   { 0, 0, -50, 16, 128, -34 } },
        { "right-out  (300,0,-200,316,128,-184)", { 300, 0, -200, 316, 128, -184 } },
        { "beyond-far (0,0,-150,16,128,-134)", { 0, 0, -150, 16, 128, -134 } },
        { "before-near(0,0,-0.9,16,128,-0.5)", { 0, 0, -0.9f, 16, 128, -0.5f } },
    };
    for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
        float *b = cases[c].b;
        bool vis = wl_frustum_aabb_visible(f, b[0],b[1],b[2],b[3],b[4],b[5]);
        printf("%s -> %s  [", cases[c].name, vis ? "VISIBLE" : "culled ");
        for (int p = 0; p < 6; p++) {
            Vector4 pl = f[p];
            float px = pl.x >= 0 ? b[3] : b[0];
            float py = pl.y >= 0 ? b[4] : b[1];
            float pz = pl.z >= 0 ? b[5] : b[2];
            printf(" %d:%.2f", p, pl.x*px + pl.y*py + pl.z*pz + pl.w);
        }
        printf(" ]\n");
    }
    return 0;
}
