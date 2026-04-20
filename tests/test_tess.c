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

static float tri_area(vg_point a, vg_point b, vg_point c)
{
    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return area < 0.0f ? -area * 0.5f : area * 0.5f;
}

static bool test_convex_polygon(void)
{
    vg_point poly[4] = {
        { 0.0f, 0.0f },
        { 4.0f, 0.0f },
        { 5.0f, 3.0f },
        { 1.0f, 4.0f },
    };
    vg_point *tris = NULL;
    size_t count = 0;
    float area = 0.0f;

    CHECK(vg_tessellate_simple_polygon(poly, 4, &tris, &count));
    CHECK(tris != NULL);
    CHECK(count == 6);
    for (size_t i = 0; i < count; i += 3)
        area += tri_area(tris[i], tris[i + 1], tris[i + 2]);
    CHECK(area > 0.0f);
    free(tris);
    return true;
}

static bool test_concave_polygon(void)
{
    vg_point poly[5] = {
        { 0.0f, 0.0f },
        { 5.0f, 0.0f },
        { 5.0f, 2.0f },
        { 2.5f, 1.0f },
        { 0.0f, 3.0f },
    };
    vg_point *tris = NULL;
    size_t count = 0;
    float area = 0.0f;

    CHECK(vg_tessellate_simple_polygon(poly, 5, &tris, &count));
    CHECK(tris != NULL);
    CHECK(count == 9);
    for (size_t i = 0; i < count; i += 3)
        area += tri_area(tris[i], tris[i + 1], tris[i + 2]);
    CHECK(area > 0.0f);
    free(tris);
    return true;
}

static bool test_reject_degenerate(void)
{
    vg_point poly[3] = {
        { 0.0f, 0.0f },
        { 1.0f, 1.0f },
        { 2.0f, 2.0f },
    };
    vg_point *tris = NULL;
    size_t count = 0;

    CHECK(!vg_tessellate_simple_polygon(poly, 3, &tris, &count));
    CHECK(tris == NULL);
    CHECK(count == 0);
    return true;
}

int main(void)
{
    if (!test_convex_polygon()) return 1;
    if (!test_concave_polygon()) return 1;
    if (!test_reject_degenerate()) return 1;
    return 0;
}
