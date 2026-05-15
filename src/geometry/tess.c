#include "internal.h"

#include <math.h>

static float signed_area(const flux_point *points, size_t count)
{
    float area = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        const flux_point *a = &points[i];
        const flux_point *b = &points[(i + 1) % count];
        area += a->x * b->y - b->x * a->y;
    }

    return area * 0.5f;
}

static float orient2d(flux_point a, flux_point b, flux_point c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool is_convex(flux_point a, flux_point b, flux_point c, bool ccw)
{
    float cross = orient2d(a, b, c);
    return ccw ? cross > 0.0f : cross < 0.0f;
}

static bool point_in_triangle(flux_point p, flux_point a, flux_point b, flux_point c)
{
    float ab = orient2d(a, b, p);
    float bc = orient2d(b, c, p);
    float ca = orient2d(c, a, p);
    bool has_neg = (ab < 0.0f) || (bc < 0.0f) || (ca < 0.0f);
    bool has_pos = (ab > 0.0f) || (bc > 0.0f) || (ca > 0.0f);
    return !(has_neg && has_pos);
}

flux_result flux_tessellate_simple_polygon(const flux_point *points, size_t count,
                                           flux_arena *arena,
                                           flux_point **out_tris, size_t *out_count)
{
    int *indices = nullptr;
    flux_point *tris = nullptr;
    size_t tri_cap;
    size_t tri_count = 0;
    size_t rem = count;
    bool ccw;
    size_t guard = 0;

    if (out_tris) *out_tris = nullptr;
    if (out_count) *out_count = 0;
    if (!points || !out_tris || !out_count || count < 3) return FLUX_ERROR_INVALID_ARGUMENT;

    tri_cap = (count - 2) * 3;
    tris = flux_arena_alloc(arena, tri_cap * sizeof(*tris));
    indices = flux_arena_alloc(arena, count * sizeof(*indices));
    if (!tris || !indices) return FLUX_ERROR_OUT_OF_MEMORY;

    for (size_t i = 0; i < count; ++i)
        indices[i] = (int)i;

    {
        float area = signed_area(points, count);
        if (area == 0.0f) return FLUX_ERROR_INVALID_ARGUMENT;
        ccw = area > 0.0f;
    }

    while (rem > 3) {
        bool clipped = false;

        if (++guard > count * count) return FLUX_ERROR_UNSUPPORTED;

        for (size_t i = 0; i < rem; ++i) {
            size_t ip = (i + rem - 1) % rem;
            size_t in = (i + 1) % rem;
            flux_point a = points[indices[ip]];
            flux_point b = points[indices[i]];
            flux_point c = points[indices[in]];
            bool ear = true;

            if (!is_convex(a, b, c, ccw)) continue;
            if (orient2d(a, b, c) == 0.0f) continue;

            for (size_t j = 0; j < rem; ++j) {
                flux_point p;
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

        if (!clipped) return FLUX_ERROR_UNSUPPORTED;
    }

    if (rem == 3) {
        flux_point a = points[indices[0]];
        flux_point b = points[indices[1]];
        flux_point c = points[indices[2]];
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

    *out_tris = tris;
    *out_count = tri_count;
    return FLUX_OK;
}
