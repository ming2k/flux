/*
 * Color helpers. The pack and premul-pack helpers are inline in the
 * public header (zero-cost). This file holds the unpack helper that
 * undoes alpha premultiplication for callers that need to inspect.
 */

#include "flux/flux.h"

void flux_color_unpack(flux_color c,
                       uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    uint8_t pa = (uint8_t)((c >> 24) & 0xFF);
    uint8_t pr = (uint8_t)((c >> 16) & 0xFF);
    uint8_t pg = (uint8_t)((c >>  8) & 0xFF);
    uint8_t pb = (uint8_t)(c         & 0xFF);

    if (a) *a = pa;
    if (pa == 0) {
        if (r) *r = 0;
        if (g) *g = 0;
        if (b) *b = 0;
        return;
    }
    if (r) *r = (uint8_t)((pr * 255 + (pa >> 1)) / pa);
    if (g) *g = (uint8_t)((pg * 255 + (pa >> 1)) / pa);
    if (b) *b = (uint8_t)((pb * 255 + (pa >> 1)) / pa);
}
