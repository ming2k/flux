#include "internal.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static bool append_triangle(flux_point *tris, size_t *count,
                            flux_point a, flux_point b, flux_point c)
{
    tris[(*count)++] = a;
    tris[(*count)++] = b;
    tris[(*count)++] = c;
    return true;
}

static flux_point normalize(flux_point v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len > 0.0f) return (flux_point){ v.x / len, v.y / len };
    return (flux_point){ 0, 0 };
}

static bool get_normal(flux_point a, flux_point b, flux_point *out_n)
{
    flux_point d = { b.x - a.x, b.y - a.y };
    flux_point n = { -d.y, d.x };
    flux_point unit_n = normalize(n);
    if (unit_n.x == 0.0f && unit_n.y == 0.0f) return false;
    *out_n = unit_n;
    return true;
}

static bool append_join(flux_point *tris, size_t *count,
                        flux_point p, flux_point n1, flux_point n2,
                        const flux_stroke_style *style)
{
    float cross = n1.x * n2.y - n1.y * n2.x;
    if (fabsf(cross) < 1e-4f) return true; /* Collinear or nearly so */

    bool left = cross > 0.0f;
    float half_w = style->stroke_width * 0.5f;

    if (style->line_join == FLUX_JOIN_MITER) {
        float dot = n1.x * n2.x + n1.y * n2.y;
        flux_point bisect = normalize((flux_point){ n1.x + n2.x, n1.y + n2.y });
        float cos_half_theta = sqrtf((1.0f + dot) * 0.5f);
        if (cos_half_theta > 1e-4f) {
            float miter_dist = half_w / cos_half_theta;
            if (miter_dist <= style->miter_limit * half_w) {
                flux_point miter_pt = { p.x + bisect.x * miter_dist, p.y + bisect.y * miter_dist };
                if (left) {
                    miter_pt.x = p.x - bisect.x * miter_dist;
                    miter_pt.y = p.y - bisect.y * miter_dist;
                    append_triangle(tris, count, p, 
                                    (flux_point){ p.x - n1.x * half_w, p.y - n1.y * half_w },
                                    miter_pt);
                    append_triangle(tris, count, p,
                                    miter_pt,
                                    (flux_point){ p.x - n2.x * half_w, p.y - n2.y * half_w });
                } else {
                    append_triangle(tris, count, p, 
                                    (flux_point){ p.x + n1.x * half_w, p.y + n1.y * half_w },
                                    miter_pt);
                    append_triangle(tris, count, p,
                                    miter_pt,
                                    (flux_point){ p.x + n2.x * half_w, p.y + n2.y * half_w });
                }
                return true;
            }
        }
    }

    if (style->line_join == FLUX_JOIN_ROUND) {
        int steps = 8;
        flux_point v1 = { (left ? -n1.x : n1.x) * half_w, (left ? -n1.y : n1.y) * half_w };
        flux_point v2 = { (left ? -n2.x : n2.x) * half_w, (left ? -n2.y : n2.y) * half_w };
        float a1 = atan2f(v1.y, v1.x);
        float a2 = atan2f(v2.y, v2.x);
        
        float diff = a2 - a1;
        if (left) {
            while (diff > 0) diff -= 2.0f * (float)M_PI;
            while (diff < -2.0f * (float)M_PI) diff += 2.0f * (float)M_PI;
        } else {
            while (diff < 0) diff += 2.0f * (float)M_PI;
            while (diff > 2.0f * (float)M_PI) diff -= 2.0f * (float)M_PI;
        }

        flux_point prev = { p.x + v1.x, p.y + v1.y };
        for (int i = 1; i <= steps; ++i) {
            float a = a1 + diff * (float)i / (float)steps;
            flux_point next = { p.x + cosf(a) * half_w, p.y + sinf(a) * half_w };
            append_triangle(tris, count, p, prev, next);
            prev = next;
        }
        return true;
    }

    /* Bevel join */
    if (left) {
        append_triangle(tris, count, p,
                        (flux_point){ p.x - n1.x * half_w, p.y - n1.y * half_w },
                        (flux_point){ p.x - n2.x * half_w, p.y - n2.y * half_w });
    } else {
        append_triangle(tris, count, p,
                        (flux_point){ p.x + n1.x * half_w, p.y + n1.y * half_w },
                        (flux_point){ p.x + n2.x * half_w, p.y + n2.y * half_w });
    }
    return true;
}

flux_result flux_stroke_polyline(const flux_point *points, size_t count, bool closed,
                                 const flux_stroke_style *style, flux_arena *arena,
                                 flux_point **out_tris, size_t *out_count)
{
    if (!points || !out_tris || !out_count || count < 2 || !style || style->stroke_width <= 0.0f)
        return FLUX_ERROR_INVALID_ARGUMENT;

    float half_w = style->stroke_width * 0.5f;
    int steps = 8;
    
    /* Estimate max triangle count */
    size_t max_tris = (count + 1) * 2; /* segments */
    max_tris += (size_t)count * (size_t)steps; /* joins */
    max_tris += 2u * (size_t)steps; /* caps */
    
    flux_point *tris = flux_arena_alloc(arena, max_tris * 3 * sizeof(flux_point));
    if (!tris) return FLUX_ERROR_OUT_OF_MEMORY;
    size_t tri_count = 0;

    flux_point *normals = flux_arena_alloc(arena, count * sizeof(flux_point));
    if (!normals) return FLUX_ERROR_OUT_OF_MEMORY;

    size_t seg_count = closed ? count : count - 1;
    for (size_t i = 0; i < seg_count; ++i) {
        if (!get_normal(points[i], points[(i + 1) % count], &normals[i])) {
            normals[i] = (i > 0) ? normals[i-1] : (flux_point){0, 0};
        }
    }

    for (size_t i = 0; i < seg_count; ++i) {
        size_t j = (i + 1) % count;
        flux_point n = normals[i];
        flux_point p1 = points[i];
        flux_point p2 = points[j];

        flux_point v1l = { p1.x + n.x * half_w, p1.y + n.y * half_w };
        flux_point v1r = { p1.x - n.x * half_w, p1.y - n.y * half_w };
        flux_point v2l = { p2.x + n.x * half_w, p2.y + n.y * half_w };
        flux_point v2r = { p2.x - n.x * half_w, p2.y - n.y * half_w };

        append_triangle(tris, &tri_count, v1l, v1r, v2r);
        append_triangle(tris, &tri_count, v1l, v2r, v2l);
    }

    for (size_t i = 0; i < count; ++i) {
        if (!closed && (i == 0 || i == count - 1)) continue;
        size_t prev_i = (i + count - 1) % count;
        size_t next_i = i;
        append_join(tris, &tri_count, points[i], normals[prev_i], normals[next_i], style);
    }

    if (!closed) {
        /* Start cap */
        flux_point p0 = points[0];
        flux_point n0 = normals[0];
        flux_point d0 = normalize((flux_point){ points[1].x - p0.x, points[1].y - p0.y });

        if (style->line_cap == FLUX_CAP_SQUARE) {
            flux_point ext = { -d0.x * half_w, -d0.y * half_w };
            flux_point v0l = { p0.x + n0.x * half_w, p0.y + n0.y * half_w };
            flux_point v0r = { p0.x - n0.x * half_w, p0.y - n0.y * half_w };
            flux_point e0l = { v0l.x + ext.x, v0l.y + ext.y };
            flux_point e0r = { v0r.x + ext.x, v0r.y + ext.y };
            append_triangle(tris, &tri_count, v0l, v0r, e0r);
            append_triangle(tris, &tri_count, v0l, e0r, e0l);
        } else if (style->line_cap == FLUX_CAP_ROUND) {
            flux_point v_start = { n0.x * half_w, n0.y * half_w };
            float angle_start = atan2f(v_start.y, v_start.x);
            flux_point prev = { p0.x + v_start.x, p0.y + v_start.y };
            for (int k = 1; k <= steps; ++k) {
                float a = angle_start + (float)k / (float)steps * M_PI;
                flux_point next = { p0.x + cosf(a) * half_w, p0.y + sinf(a) * half_w };
                append_triangle(tris, &tri_count, p0, prev, next);
                prev = next;
            }
        }

        /* End cap */
        size_t last = count - 1;
        flux_point pl = points[last];
        flux_point nl = normals[last - 1];
        flux_point dl = normalize((flux_point){ pl.x - points[last-1].x, pl.y - points[last-1].y });

        if (style->line_cap == FLUX_CAP_SQUARE) {
            flux_point ext = { dl.x * half_w, dl.y * half_w };
            flux_point vll = { pl.x + nl.x * half_w, pl.y + nl.y * half_w };
            flux_point vlr = { pl.x - nl.x * half_w, pl.y - nl.y * half_w };
            flux_point ell = { vll.x + ext.x, vll.y + ext.y };
            flux_point elr = { vlr.x + ext.x, vlr.y + ext.y };
            append_triangle(tris, &tri_count, vll, vlr, elr);
            append_triangle(tris, &tri_count, vll, elr, ell);
        } else if (style->line_cap == FLUX_CAP_ROUND) {
            flux_point v_start = { -nl.x * half_w, -nl.y * half_w };
            float angle_start = atan2f(v_start.y, v_start.x);
            flux_point prev = { pl.x + v_start.x, pl.y + v_start.y };
            for (int k = 1; k <= steps; ++k) {
                float a = angle_start + (float)k / (float)steps * M_PI;
                flux_point next = { pl.x + cosf(a) * half_w, pl.y + sinf(a) * half_w };
                append_triangle(tris, &tri_count, pl, prev, next);
                prev = next;
            }
        }
    }

    *out_tris = tris;
    *out_count = tri_count;
    return FLUX_OK;
}
