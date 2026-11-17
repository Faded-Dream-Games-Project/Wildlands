/* empirically determine what raymath's MatrixMultiply(A, B) computes
 * in standard column-vector math (M[i][j] = m[4j + i]) */
#include <stdio.h>
#include "raymath.h"

/* column-vector transform with the column-major convention */
static Vector3 xfvm(Matrix m, Vector3 p)
{
    Vector3 r;
    r.x = m.m0 * p.x + m.m4 * p.y + m.m8 * p.z + m.m12;
    r.y = m.m1 * p.x + m.m5 * p.y + m.m9 * p.z + m.m13;
    r.z = m.m2 * p.x + m.m6 * p.y + m.m10 * p.z + m.m14;
    return r;
}

int main(void)
{
    Matrix A = MatrixTranslate(5, 0, 0);      /* M[i][j]=m[4j+i] -> m12 = 5 */
    Matrix B = MatrixRotateY(90.0f * DEG2RAD);
    Matrix M = MatrixMultiply(A, B);

    Vector3 p = { 1, 0, 0 };
    Vector3 q = xfvm(M, p);
    printf("MatrixMultiply(Translate(5,0,0), RotY(90)) * (1,0,0) = (%.2f, %.2f, %.2f)\n",
           q.x, q.y, q.z);
    printf("  A.B (translate after rotate) would be (5.00, 0.00, -1.00)\n");
    printf("  B.A (rotate after translate)  would be (0.00, 0.00, -6.00)\n");

    /* also verify MatrixTranslate storage convention */
    Matrix T = MatrixTranslate(7, 8, 9);
    printf("Translate(7,8,9): m12=%.0f m13=%.0f m14=%.0f (expect 7 8 9)\n",
           T.m12, T.m13, T.m14);
    return 0;
}
