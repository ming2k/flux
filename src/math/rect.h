/* Rectangle helpers. */
#ifndef FX_MATH_RECT_H
#define FX_MATH_RECT_H

#include "flux/flux.h"
#include <stdbool.h>

static inline bool fx_rect_has_area(const fx_rect *r)
{
    return r && r->w > 0.0f && r->h > 0.0f;
}

static inline bool fx_rect_contains(const fx_rect *r, float x, float y)
{
    return r && x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h;
}

#endif /* FX_MATH_RECT_H */
