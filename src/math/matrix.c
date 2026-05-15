/*
 * 2D affine matrix (column-major, six floats).
 *
 *   [ m[0]  m[2]  m[4] ]
 *   [ m[1]  m[3]  m[5] ]
 *   [  0     0     1   ]
 *
 *   x' = m[0]*x + m[2]*y + m[4]
 *   y' = m[1]*x + m[3]*y + m[5]
 */

#include "matrix.h"
#include <math.h>

void flux_matrix_rotation(flux_matrix *m, float radians)
{
    float s = sinf(radians);
    float c = cosf(radians);
    m->m[0] = c;    m->m[1] = s;
    m->m[2] = -s;   m->m[3] = c;
    m->m[4] = 0.0f; m->m[5] = 0.0f;
}

void flux_matrix_multiply(flux_matrix *out, const flux_matrix *a, const flux_matrix *b)
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

bool flux_matrix_invert(const flux_matrix *m, flux_matrix *out)
{
    float a = m->m[0], b = m->m[1], c = m->m[2];
    float d = m->m[3], e = m->m[4], f = m->m[5];

    float det = a * d - b * c;
    if (det == 0.0f) return false;
    float inv = 1.0f / det;

    out->m[0] =  d * inv;
    out->m[1] = -b * inv;
    out->m[2] = -c * inv;
    out->m[3] =  a * inv;
    out->m[4] = (c * f - d * e) * inv;
    out->m[5] = (b * e - a * f) * inv;
    return true;
}

bool flux_matrix_is_identity(const flux_matrix *m)
{
    return m->m[0] == 1.0f && m->m[1] == 0.0f &&
           m->m[2] == 0.0f && m->m[3] == 1.0f &&
           m->m[4] == 0.0f && m->m[5] == 0.0f;
}

void flux_matrix_transform_point(const flux_matrix *m, float *x, float *y)
{
    float nx = m->m[0] * (*x) + m->m[2] * (*y) + m->m[4];
    float ny = m->m[1] * (*x) + m->m[3] * (*y) + m->m[5];
    *x = nx;
    *y = ny;
}

void flux_matrix_transform_rect(const flux_matrix *m,
                                const flux_rect *in, flux_rect *out)
{
    float x0 = in->x,           y0 = in->y;
    float x1 = in->x + in->w,   y1 = in->y;
    float x2 = in->x + in->w,   y2 = in->y + in->h;
    float x3 = in->x,           y3 = in->y + in->h;
    flux_matrix_transform_point(m, &x0, &y0);
    flux_matrix_transform_point(m, &x1, &y1);
    flux_matrix_transform_point(m, &x2, &y2);
    flux_matrix_transform_point(m, &x3, &y3);

    float minx = x0, maxx = x0, miny = y0, maxy = y0;
    if (x1 < minx) minx = x1; else if (x1 > maxx) maxx = x1;
    if (x2 < minx) minx = x2; else if (x2 > maxx) maxx = x2;
    if (x3 < minx) minx = x3; else if (x3 > maxx) maxx = x3;
    if (y1 < miny) miny = y1; else if (y1 > maxy) maxy = y1;
    if (y2 < miny) miny = y2; else if (y2 > maxy) maxy = y2;
    if (y3 < miny) miny = y3; else if (y3 > maxy) maxy = y3;

    out->x = minx;
    out->y = miny;
    out->w = maxx - minx;
    out->h = maxy - miny;
}
