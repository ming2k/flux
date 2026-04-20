#include "internal.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static bool ensure_tri_capacity(vg_point **tris, size_t *count, size_t *cap,
                                size_t extra)
{
    size_t need = *count + extra;
    if (need <= *cap) return true;

    size_t new_cap = *cap ? *cap : 96;
    while (new_cap < need) new_cap *= 2;

    vg_point *grown = realloc(*tris, new_cap * sizeof(*grown));
    if (!grown) return false;

    *tris = grown;
    *cap = new_cap;
    return true;
}

static bool append_triangle(vg_point **tris, size_t *count, size_t *cap,
                            vg_point a, vg_point b, vg_point c)
{
    if (!ensure_tri_capacity(tris, count, cap, 3)) return false;
    (*tris)[(*count)++] = a;
    (*tris)[(*count)++] = b;
    (*tris)[(*count)++] = c;
    return true;
}

static vg_point normalize(vg_point v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len > 0.0f) return (vg_point){ v.x / len, v.y / len };
    return (vg_point){ 0, 0 };
}

static bool get_normal(vg_point a, vg_point b, vg_point *out_n)
{
    vg_point d = { b.x - a.x, b.y - a.y };
    vg_point n = { -d.y, d.x };
    vg_point unit_n = normalize(n);
    if (unit_n.x == 0.0f && unit_n.y == 0.0f) return false;
    *out_n = unit_n;
    return true;
}

static bool append_join(vg_point **tris, size_t *count, size_t *cap,
                        vg_point p, vg_point n1, vg_point n2,
                        const vg_paint *paint)
{
    float cross = n1.x * n2.y - n1.y * n2.x;
    if (fabsf(cross) < 1e-4f) return true; /* Collinear or nearly so */

    bool left = cross > 0.0f;
    float half_w = paint->stroke_width * 0.5f;

    if (paint->line_join == VG_JOIN_MITER) {
        /* Intersection of (p+out1*half_w + t*dir1) and (p+out2*half_w + u*dir2) */
        float dot = n1.x * n2.x + n1.y * n2.y;
        
        /* Bisector direction is normalize(n1 + n2) */
        vg_point bisect = normalize((vg_point){ n1.x + n2.x, n1.y + n2.y });
        float cos_half_theta = sqrtf((1.0f + dot) * 0.5f);
        if (cos_half_theta > 1e-4f) {
            float miter_dist = half_w / cos_half_theta;
            if (miter_dist <= paint->miter_limit * half_w) {
                vg_point miter_pt = { p.x + bisect.x * miter_dist, p.y + bisect.y * miter_dist };
                if (left) {
                    /* On the left turn, the outside is the negative side of normals */
                    miter_pt.x = p.x - bisect.x * miter_dist;
                    miter_pt.y = p.y - bisect.y * miter_dist;
                    return append_triangle(tris, count, cap, p, 
                                           (vg_point){ p.x - n1.x * half_w, p.y - n1.y * half_w },
                                           miter_pt) &&
                           append_triangle(tris, count, cap, p,
                                           miter_pt,
                                           (vg_point){ p.x - n2.x * half_w, p.y - n2.y * half_w });
                } else {
                    return append_triangle(tris, count, cap, p, 
                                           (vg_point){ p.x + n1.x * half_w, p.y + n1.y * half_w },
                                           miter_pt) &&
                           append_triangle(tris, count, cap, p,
                                           miter_pt,
                                           (vg_point){ p.x + n2.x * half_w, p.y + n2.y * half_w });
                }
            }
        }
        /* Fallback to bevel */
    }

    if (paint->line_join == VG_JOIN_ROUND) {
        int steps = 8;
        vg_point v1 = { (left ? -n1.x : n1.x) * half_w, (left ? -n1.y : n1.y) * half_w };
        vg_point v2 = { (left ? -n2.x : n2.x) * half_w, (left ? -n2.y : n2.y) * half_w };
        float a1 = atan2f(v1.y, v1.x);
        float a2 = atan2f(v2.y, v2.x);
        
        float diff = a2 - a1;
        if (left) {
            while (diff > 0) diff -= 2.0f * M_PI;
            while (diff < -2.0f * M_PI) diff += 2.0f * M_PI;
        } else {
            while (diff < 0) diff += 2.0f * M_PI;
            while (diff > 2.0f * M_PI) diff -= 2.0f * M_PI;
        }

        vg_point prev = { p.x + v1.x, p.y + v1.y };
        for (int i = 1; i <= steps; ++i) {
            float a = a1 + diff * (float)i / (float)steps;
            vg_point next = { p.x + cosf(a) * half_w, p.y + sinf(a) * half_w };
            if (!append_triangle(tris, count, cap, p, prev, next)) return false;
            prev = next;
        }
        return true;
    }

    /* Bevel join */
    if (left) {
        return append_triangle(tris, count, cap, p,
                               (vg_point){ p.x - n1.x * half_w, p.y - n1.y * half_w },
                               (vg_point){ p.x - n2.x * half_w, p.y - n2.y * half_w });
    } else {
        return append_triangle(tris, count, cap, p,
                               (vg_point){ p.x + n1.x * half_w, p.y + n1.y * half_w },
                               (vg_point){ p.x + n2.x * half_w, p.y + n2.y * half_w });
    }
}

bool vg_stroke_polyline(const vg_point *points, size_t count, bool closed,
                        const vg_paint *paint, vg_point **out_tris, size_t *out_count)
{
    if (!points || !out_tris || !out_count || count < 2 || !paint || paint->stroke_width <= 0.0f)
        return false;

    vg_point *tris = NULL;
    size_t tri_count = 0;
    size_t tri_cap = 0;
    float half_w = paint->stroke_width * 0.5f;

    vg_point *normals = malloc(count * sizeof(vg_point));
    if (!normals) return false;

    /* 1. Calculate segment normals */
    size_t seg_count = closed ? count : count - 1;
    for (size_t i = 0; i < seg_count; ++i) {
        if (!get_normal(points[i], points[(i + 1) % count], &normals[i])) {
            /* degenerate segment, reuse previous or use zero */
            normals[i] = (i > 0) ? normals[i-1] : (vg_point){0, 0};
        }
    }

    /* 2. Generate quads for segments */
    for (size_t i = 0; i < seg_count; ++i) {
        size_t j = (i + 1) % count;
        vg_point n = normals[i];
        vg_point p1 = points[i];
        vg_point p2 = points[j];

        vg_point v1l = { p1.x + n.x * half_w, p1.y + n.y * half_w };
        vg_point v1r = { p1.x - n.x * half_w, p1.y - n.y * half_w };
        vg_point v2l = { p2.x + n.x * half_w, p2.y + n.y * half_w };
        vg_point v2r = { p2.x - n.x * half_w, p2.y - n.y * half_w };

        if (!append_triangle(&tris, &tri_count, &tri_cap, v1l, v1r, v2r) ||
            !append_triangle(&tris, &tri_count, &tri_cap, v1l, v2r, v2l)) {
            goto fail;
        }
    }

    /* 3. Generate joins */
    for (size_t i = 0; i < count; ++i) {
        if (!closed && (i == 0 || i == count - 1)) continue;

        size_t prev_i = (i + count - 1) % count;
        size_t next_i = i;
        if (!append_join(&tris, &tri_count, &tri_cap, points[i], normals[prev_i], normals[next_i], paint))
            goto fail;
    }

    /* 4. Generate caps for open paths */
    if (!closed) {
        /* Start cap */
        vg_point p0 = points[0];
        vg_point n0 = normals[0];
        vg_point d0 = normalize((vg_point){ points[1].x - p0.x, points[1].y - p0.y });

        if (paint->line_cap == VG_CAP_SQUARE) {
            vg_point ext = { -d0.x * half_w, -d0.y * half_w };
            vg_point v0l = { p0.x + n0.x * half_w, p0.y + n0.y * half_w };
            vg_point v0r = { p0.x - n0.x * half_w, p0.y - n0.y * half_w };
            vg_point e0l = { v0l.x + ext.x, v0l.y + ext.y };
            vg_point e0r = { v0r.x + ext.x, v0r.y + ext.y };
            if (!append_triangle(&tris, &tri_count, &tri_cap, v0l, v0r, e0r) ||
                !append_triangle(&tris, &tri_count, &tri_cap, v0l, e0r, e0l)) goto fail;
        } else if (paint->line_cap == VG_CAP_ROUND) {
            /* Round cap at start: semi-circle from n to -n in direction -d0 */
            /* Using a simplified round cap logic */
            int steps = 8;
            vg_point v_start = { n0.x * half_w, n0.y * half_w };
            float angle_start = atan2f(v_start.y, v_start.x);
            vg_point prev = { p0.x + v_start.x, p0.y + v_start.y };
            for (int k = 1; k <= steps; ++k) {
                float a = angle_start + (float)k / (float)steps * M_PI;
                vg_point next = { p0.x + cosf(a) * half_w, p0.y + sinf(a) * half_w };
                if (!append_triangle(&tris, &tri_count, &tri_cap, p0, prev, next)) goto fail;
                prev = next;
            }
        }

        /* End cap */
        size_t last = count - 1;
        vg_point pl = points[last];
        vg_point nl = normals[last - 1];
        vg_point dl = normalize((vg_point){ pl.x - points[last-1].x, pl.y - points[last-1].y });

        if (paint->line_cap == VG_CAP_SQUARE) {
            vg_point ext = { dl.x * half_w, dl.y * half_w };
            vg_point vll = { pl.x + nl.x * half_w, pl.y + nl.y * half_w };
            vg_point vlr = { pl.x - nl.x * half_w, pl.y - nl.y * half_w };
            vg_point ell = { vll.x + ext.x, vll.y + ext.y };
            vg_point elr = { vlr.x + ext.x, vlr.y + ext.y };
            if (!append_triangle(&tris, &tri_count, &tri_cap, vll, vlr, elr) ||
                !append_triangle(&tris, &tri_count, &tri_cap, vll, elr, ell)) goto fail;
        } else if (paint->line_cap == VG_CAP_ROUND) {
            int steps = 8;
            vg_point v_start = { -nl.x * half_w, -nl.y * half_w };
            float angle_start = atan2f(v_start.y, v_start.x);
            vg_point prev = { pl.x + v_start.x, pl.y + v_start.y };
            for (int k = 1; k <= steps; ++k) {
                float a = angle_start + (float)k / (float)steps * M_PI;
                vg_point next = { pl.x + cosf(a) * half_w, pl.y + sinf(a) * half_w };
                if (!append_triangle(&tris, &tri_count, &tri_cap, pl, prev, next)) goto fail;
                prev = next;
            }
        }
    }

    free(normals);
    *out_tris = tris;
    *out_count = tri_count;
    return true;

fail:
    free(normals);
    free(tris);
    return false;
}
