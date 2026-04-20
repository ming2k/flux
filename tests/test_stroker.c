#include "internal.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #cond, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

static bool test_open_polyline_stroke(void)
{
    fx_point pts[3] = {
        { 0.0f, 0.0f },
        { 10.0f, 0.0f },
        { 10.0f, 10.0f },
    };
    fx_point *tris = NULL;
    size_t count = 0;
    fx_paint paint = { .color = 0, .stroke_width = 2.0f, .miter_limit = 4.0f, .line_cap = FX_CAP_BUTT, .line_join = FX_JOIN_MITER };

    CHECK(fx_stroke_polyline(pts, 3, false, &paint, &tris, &count));
    CHECK(tris != NULL);
    CHECK(count >= 12);
    free(tris);
    return true;
}

static bool test_closed_polyline_stroke(void)
{
    fx_point pts[4] = {
        { 0.0f, 0.0f },
        { 10.0f, 0.0f },
        { 10.0f, 10.0f },
        { 0.0f, 10.0f },
    };
    fx_point *tris = NULL;
    size_t count = 0;
    fx_paint paint = { .color = 0, .stroke_width = 3.0f, .miter_limit = 4.0f, .line_cap = FX_CAP_BUTT, .line_join = FX_JOIN_MITER };

    CHECK(fx_stroke_polyline(pts, 4, true, &paint, &tris, &count));
    CHECK(tris != NULL);
    CHECK(count >= 24);
    free(tris);
    return true;
}

static bool test_reject_bad_input(void)
{
    fx_point pts[1] = { { 0.0f, 0.0f } };
    fx_point *tris = NULL;
    size_t count = 0;
    fx_paint paint = { .color = 0, .stroke_width = 2.0f, .miter_limit = 4.0f, .line_cap = FX_CAP_BUTT, .line_join = FX_JOIN_MITER };

    CHECK(!fx_stroke_polyline(pts, 1, false, &paint, &tris, &count));
    CHECK(!fx_stroke_polyline(NULL, 0, false, &paint, &tris, &count));
    
    paint.stroke_width = 0.0f;
    CHECK(!fx_stroke_polyline(pts, 1, false, &paint, &tris, &count));
    return true;
}

int main(void)
{
    if (!test_open_polyline_stroke()) return 1;
    if (!test_closed_polyline_stroke()) return 1;
    if (!test_reject_bad_input()) return 1;
    return 0;
}
