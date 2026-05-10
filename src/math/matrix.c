#include "matrix.h"

void fx_matrix_multiply(fx_matrix *out, const fx_matrix *a, const fx_matrix *b)
{
    float a0 = a->m[0], a1 = a->m[1], a2 = a->m[2];
    float a3 = a->m[3], a4 = a->m[4], a5 = a->m[5];

    float b0 = b->m[0], b1 = b->m[1], b2 = b->m[2];
    float b3 = b->m[3], b4 = b->m[4], b5 = b->m[5];

    out->m[0] = a0 * b0 + a2 * b1;
    out->m[1] = a1 * b0 + a3 * b1;
    out->m[2] = a0 * b2 + a2 * b3;
    out->m[3] = a1 * b2 + a3 * b3;
    out->m[4] = a0 * b4 + a2 * b5 + a4;
    out->m[5] = a1 * b4 + a3 * b5 + a5;
}

void fx_matrix_transform_point(const fx_matrix *m, float *x, float *y)
{
    float nx = m->m[0] * (*x) + m->m[2] * (*y) + m->m[4];
    float ny = m->m[1] * (*x) + m->m[3] * (*y) + m->m[5];
    *x = nx;
    *y = ny;
}
