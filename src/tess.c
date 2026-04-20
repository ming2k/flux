#include "internal.h"

#include <math.h>

static float signed_area(const vg_point *points, size_t count)
{
    float area = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        const vg_point *a = &points[i];
        const vg_point *b = &points[(i + 1) % count];
        area += a->x * b->y - b->x * a->y;
    }

    return area * 0.5f;
}

static float orient2d(vg_point a, vg_point b, vg_point c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool is_convex(vg_point a, vg_point b, vg_point c, bool ccw)
{
    float cross = orient2d(a, b, c);
    return ccw ? cross > 0.0f : cross < 0.0f;
}

static bool point_in_triangle(vg_point p, vg_point a, vg_point b, vg_point c)
{
    float ab = orient2d(a, b, p);
    float bc = orient2d(b, c, p);
    float ca = orient2d(c, a, p);
    bool has_neg = (ab < 0.0f) || (bc < 0.0f) || (ca < 0.0f);
    bool has_pos = (ab > 0.0f) || (bc > 0.0f) || (ca > 0.0f);
    return !(has_neg && has_pos);
}

bool vg_tessellate_simple_polygon(const vg_point *points, size_t count,
                                  vg_point **out_tris, size_t *out_count)
{
    int *indices = NULL;
    vg_point *tris = NULL;
    size_t tri_cap;
    size_t tri_count = 0;
    size_t rem = count;
    bool ccw;
    size_t guard = 0;

    if (out_tris) *out_tris = NULL;
    if (out_count) *out_count = 0;
    if (!points || !out_tris || !out_count || count < 3) return false;

    tri_cap = (count - 2) * 3;
    tris = malloc(tri_cap * sizeof(*tris));
    indices = malloc(count * sizeof(*indices));
    if (!tris || !indices) goto fail;

    for (size_t i = 0; i < count; ++i)
        indices[i] = (int)i;

    {
        float area = signed_area(points, count);
        if (area == 0.0f) goto fail;
        ccw = area > 0.0f;
    }

    while (rem > 3) {
        bool clipped = false;

        if (++guard > count * count) goto fail;

        for (size_t i = 0; i < rem; ++i) {
            size_t ip = (i + rem - 1) % rem;
            size_t in = (i + 1) % rem;
            vg_point a = points[indices[ip]];
            vg_point b = points[indices[i]];
            vg_point c = points[indices[in]];
            bool ear = true;

            if (!is_convex(a, b, c, ccw)) continue;
            if (orient2d(a, b, c) == 0.0f) continue;

            for (size_t j = 0; j < rem; ++j) {
                vg_point p;
                if (j == ip || j == i || j == in) continue;
                p = points[indices[j]];
                if (point_in_triangle(p, a, b, c)) {
                    ear = false;
                    break;
                }
            }
            if (!ear) continue;

            if (ccw) {
                tris[tri_count++] = a;
                tris[tri_count++] = b;
                tris[tri_count++] = c;
            } else {
                tris[tri_count++] = a;
                tris[tri_count++] = c;
                tris[tri_count++] = b;
            }

            memmove(&indices[i], &indices[i + 1],
                    (rem - i - 1) * sizeof(*indices));
            rem--;
            clipped = true;
            break;
        }

        if (!clipped) goto fail;
    }

    if (rem == 3) {
        vg_point a = points[indices[0]];
        vg_point b = points[indices[1]];
        vg_point c = points[indices[2]];
        if (ccw) {
            tris[tri_count++] = a;
            tris[tri_count++] = b;
            tris[tri_count++] = c;
        } else {
            tris[tri_count++] = a;
            tris[tri_count++] = c;
            tris[tri_count++] = b;
        }
    }

    free(indices);
    *out_tris = tris;
    *out_count = tri_count;
    return true;

fail:
    free(indices);
    free(tris);
    return false;
}
