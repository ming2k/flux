/*
 * 2D affine matrix (3x3, stored as 6 floats).
 * [ a  c  e ]     x' = a·x + c·y + e
 * [ b  d  f ]     y' = b·x + d·y + f
 * [ 0  0  1 ]
 */
#ifndef FX_MATH_MATRIX_H
#define FX_MATH_MATRIX_H

#include "flux/flux.h"

static inline void fx_matrix_identity(fx_matrix *m)
{
    m->m[0] = 1.0f; m->m[1] = 0.0f;
    m->m[2] = 0.0f; m->m[3] = 1.0f;
    m->m[4] = 0.0f; m->m[5] = 0.0f;
}

static inline bool fx_matrix_is_identity(const fx_matrix *m)
{
    return m->m[0] == 1.0f && m->m[1] == 0.0f &&
           m->m[2] == 0.0f && m->m[3] == 1.0f &&
           m->m[4] == 0.0f && m->m[5] == 0.0f;
}

#endif /* FX_MATH_MATRIX_H */
