/* Rectangle helpers. */
#ifndef FLUX_MATH_RECT_H
#define FLUX_MATH_RECT_H

#include "flux/flux.h"
#include <stdbool.h>

static inline bool flux_rect_has_area(const flux_rect *r)
{
    return r && r->w > 0.0f && r->h > 0.0f;
}

static inline bool flux_rect_contains(const flux_rect *r, float x, float y)
{
    return r && x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h;
}

#endif /* FLUX_MATH_RECT_H */
