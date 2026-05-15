/*
 * Path: explicit verb/point stream with bezier flattening, simple-shape
 * convenience constructors, and SVG-style elliptical arcs.
 *
 * Memory comes from the owning context's allocator. Bounds are tracked
 * incrementally so flux_path_get_bounds is O(1).
 */
#include "internal.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

enum {
    FLUX_PATH_MOVE  = 0,
    FLUX_PATH_LINE  = 1,
    FLUX_PATH_QUAD  = 2,
    FLUX_PATH_CUBIC = 3,
    FLUX_PATH_CLOSE = 4,
};

static constexpr int FLUX_FLATTEN_MAX_DEPTH = 16;

/* ------------------------------------------------------------------ */
/*  Storage helpers (allocator-routed)                                */
/* ------------------------------------------------------------------ */

static bool ensure_verb_capacity(flux_path *path, size_t extra)
{
    size_t need = path->verb_count + extra;
    if (need <= path->verb_cap) return true;

    size_t new_cap = path->verb_cap ? path->verb_cap : 16;
    while (new_cap < need) new_cap *= 2;

    uint8_t *verbs = flux_realloc(path->ctx, path->verbs,
                                  path->verb_cap * sizeof(*verbs),
                                  new_cap * sizeof(*verbs));
    if (!verbs) return false;

    path->verbs = verbs;
    path->verb_cap = new_cap;
    return true;
}

static bool ensure_point_capacity(flux_path *path, size_t extra)
{
    size_t need = path->point_count + extra;
    if (need <= path->point_cap) return true;

    size_t new_cap = path->point_cap ? path->point_cap : 32;
    while (new_cap < need) new_cap *= 2;

    flux_point *points = flux_realloc(path->ctx, path->points,
                                      path->point_cap * sizeof(*points),
                                      new_cap * sizeof(*points));
    if (!points) return false;

    path->points = points;
    path->point_cap = new_cap;
    return true;
}

static bool ensure_temp_point_capacity(flux_point **points, size_t *count,
                                       size_t *cap, size_t extra)
{
    size_t need = *count + extra;
    if (need <= *cap) return true;

    size_t new_cap = *cap ? *cap : 32;
    while (new_cap < need) new_cap *= 2;

    flux_point *grown = realloc(*points, new_cap * sizeof(*grown));
    if (!grown) return false;

    *points = grown;
    *cap = new_cap;
    return true;
}

static bool points_equal(flux_point a, flux_point b)
{
    return a.x == b.x && a.y == b.y;
}

static flux_result append_flat_point(flux_point **points, size_t *count,
                                     size_t *cap, flux_point pt)
{
    if (*count && points_equal((*points)[*count - 1], pt)) return FLUX_OK;
    if (!ensure_temp_point_capacity(points, count, cap, 1))
        return FLUX_ERROR_OUT_OF_MEMORY;
    (*points)[(*count)++] = pt;
    return FLUX_OK;
}

/* ------------------------------------------------------------------ */
/*  Bezier flattening                                                 */
/* ------------------------------------------------------------------ */

static float point_line_distance_sq(flux_point p, flux_point a, flux_point b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float denom = dx * dx + dy * dy;

    if (denom == 0.0f) {
        float px = p.x - a.x;
        float py = p.y - a.y;
        return px * px + py * py;
    }
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / denom;
    float qx = a.x + t * dx;
    float qy = a.y + t * dy;
    float ex = p.x - qx;
    float ey = p.y - qy;
    return ex * ex + ey * ey;
}

static flux_result flatten_quad_recursive(flux_point p0, flux_point p1, flux_point p2,
                                          float tol_sq, unsigned depth,
                                          flux_point **points, size_t *count, size_t *cap)
{
    if (depth >= FLUX_FLATTEN_MAX_DEPTH ||
        point_line_distance_sq(p1, p0, p2) <= tol_sq)
        return append_flat_point(points, count, cap, p2);

    flux_point p01  = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    flux_point p12  = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    flux_point p012 = { (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };

    flux_result r = flatten_quad_recursive(p0, p01, p012, tol_sq, depth + 1,
                                           points, count, cap);
    if (r != FLUX_OK) return r;
    return flatten_quad_recursive(p012, p12, p2, tol_sq, depth + 1,
                                  points, count, cap);
}

static flux_result flatten_cubic_recursive(flux_point p0, flux_point p1,
                                           flux_point p2, flux_point p3,
                                           float tol_sq, unsigned depth,
                                           flux_point **points, size_t *count, size_t *cap)
{
    if (depth >= FLUX_FLATTEN_MAX_DEPTH ||
        (point_line_distance_sq(p1, p0, p3) <= tol_sq &&
         point_line_distance_sq(p2, p0, p3) <= tol_sq))
        return append_flat_point(points, count, cap, p3);

    flux_point p01   = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    flux_point p12   = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    flux_point p23   = { (p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f };
    flux_point p012  = { (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };
    flux_point p123  = { (p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f };
    flux_point p0123 = { (p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f };

    flux_result r = flatten_cubic_recursive(p0, p01, p012, p0123, tol_sq, depth + 1,
                                            points, count, cap);
    if (r != FLUX_OK) return r;
    return flatten_cubic_recursive(p0123, p123, p23, p3, tol_sq, depth + 1,
                                   points, count, cap);
}

/* ------------------------------------------------------------------ */
/*  Bounds tracking                                                   */
/* ------------------------------------------------------------------ */

static void update_bounds(flux_path *path, float x, float y)
{
    if (!path->has_bounds) {
        path->bounds = (flux_rect){ x, y, 0.0f, 0.0f };
        path->has_bounds = true;
        return;
    }
    float min_x = path->bounds.x;
    float min_y = path->bounds.y;
    float max_x = path->bounds.x + path->bounds.w;
    float max_y = path->bounds.y + path->bounds.h;

    if (x < min_x) min_x = x;
    if (y < min_y) min_y = y;
    if (x > max_x) max_x = x;
    if (y > max_y) max_y = y;

    path->bounds.x = min_x;
    path->bounds.y = min_y;
    path->bounds.w = max_x - min_x;
    path->bounds.h = max_y - min_y;
}

static bool push_verb_and_points(flux_path *path, uint8_t verb,
                                 const flux_point *points, size_t point_count)
{
    if (!path) return false;
    if (!ensure_verb_capacity(path, 1)) return false;
    if (point_count && !ensure_point_capacity(path, point_count)) return false;

    path->verbs[path->verb_count++] = verb;
    for (size_t i = 0; i < point_count; ++i) {
        path->points[path->point_count++] = points[i];
        update_bounds(path, points[i].x, points[i].y);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */

flux_result flux_path_create(flux_context *ctx, flux_path **out_path)
{
    if (!ctx || !out_path) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_path *path = flux_calloc(ctx, 1, sizeof(*path));
    if (!path) return FLUX_ERROR_OUT_OF_MEMORY;
    flux_ref_init(&path->ref_count);
    path->ctx = flux_context_retain(ctx);
    *out_path = path;
    return FLUX_OK;
}

flux_path *flux_path_retain(flux_path *path)
{
    if (path) flux_ref_retain(&path->ref_count);
    return path;
}

void flux_path_release(flux_path *path)
{
    if (!path) return;
    if (flux_ref_release(&path->ref_count) == 0) {
        flux_context *ctx = path->ctx;
        flux_free(ctx, path->verbs);
        flux_free(ctx, path->points);
        flux_free(ctx, path);
        flux_context_release(ctx);
    }
}

void flux_path_clear(flux_path *path)
{
    if (!path) return;
    path->verb_count = 0;
    path->point_count = 0;
    path->bounds = (flux_rect){ 0 };
    path->has_bounds = false;
}

/* ------------------------------------------------------------------ */
/*  Verb construction                                                 */
/* ------------------------------------------------------------------ */

flux_result flux_path_move_to(flux_path *path, float x, float y)
{
    flux_point pt = { x, y };
    return push_verb_and_points(path, FLUX_PATH_MOVE, &pt, 1)
        ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

flux_result flux_path_line_to(flux_path *path, float x, float y)
{
    flux_point pt = { x, y };
    return push_verb_and_points(path, FLUX_PATH_LINE, &pt, 1)
        ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

flux_result flux_path_quad_to(flux_path *path, float cx, float cy, float x, float y)
{
    flux_point pts[2] = { { cx, cy }, { x, y } };
    return push_verb_and_points(path, FLUX_PATH_QUAD, pts, 2)
        ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

flux_result flux_path_cubic_to(flux_path *path, float cx0, float cy0,
                               float cx1, float cy1, float x, float y)
{
    flux_point pts[3] = { { cx0, cy0 }, { cx1, cy1 }, { x, y } };
    return push_verb_and_points(path, FLUX_PATH_CUBIC, pts, 3)
        ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

flux_result flux_path_close(flux_path *path)
{
    return push_verb_and_points(path, FLUX_PATH_CLOSE, nullptr, 0)
        ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

/* ------------------------------------------------------------------ */
/*  Convenience shapes                                                */
/* ------------------------------------------------------------------ */

flux_result flux_path_add_rect(flux_path *path, const flux_rect *rect)
{
    if (!path || !rect) return FLUX_ERROR_INVALID_ARGUMENT;
    float x0 = rect->x,           y0 = rect->y;
    float x1 = rect->x + rect->w, y1 = rect->y + rect->h;
    flux_result r;
    if ((r = flux_path_move_to(path, x0, y0)) != FLUX_OK) return r;
    if ((r = flux_path_line_to(path, x1, y0)) != FLUX_OK) return r;
    if ((r = flux_path_line_to(path, x1, y1)) != FLUX_OK) return r;
    if ((r = flux_path_line_to(path, x0, y1)) != FLUX_OK) return r;
    return flux_path_close(path);
}

/* Cubic-bezier circle constant: 4*(sqrt(2)-1)/3.  Used by add_circle /
 * add_ellipse / add_round_rect to approximate a 90 degree arc. */
static constexpr float FLUX_KAPPA = 0.5522847498307933f;

flux_result flux_path_add_round_rect(flux_path *path, const flux_rect *rect, float r)
{
    if (!path || !rect) return FLUX_ERROR_INVALID_ARGUMENT;
    if (r <= 0.0f) return flux_path_add_rect(path, rect);

    float w = rect->w, h = rect->h;
    float maxr = (w < h ? w : h) * 0.5f;
    if (r > maxr) r = maxr;

    float x0 = rect->x,     y0 = rect->y;
    float x1 = rect->x + w, y1 = rect->y + h;
    float k = r * FLUX_KAPPA;

    flux_result rr;
    if ((rr = flux_path_move_to (path, x0 + r, y0))                                          != FLUX_OK) return rr;
    if ((rr = flux_path_line_to (path, x1 - r, y0))                                          != FLUX_OK) return rr;
    if ((rr = flux_path_cubic_to(path, x1 - r + k, y0,     x1, y0 + r - k, x1, y0 + r))      != FLUX_OK) return rr;
    if ((rr = flux_path_line_to (path, x1, y1 - r))                                          != FLUX_OK) return rr;
    if ((rr = flux_path_cubic_to(path, x1, y1 - r + k,     x1 - r + k, y1, x1 - r, y1))      != FLUX_OK) return rr;
    if ((rr = flux_path_line_to (path, x0 + r, y1))                                          != FLUX_OK) return rr;
    if ((rr = flux_path_cubic_to(path, x0 + r - k, y1,     x0, y1 - r + k, x0, y1 - r))      != FLUX_OK) return rr;
    if ((rr = flux_path_line_to (path, x0, y0 + r))                                          != FLUX_OK) return rr;
    if ((rr = flux_path_cubic_to(path, x0, y0 + r - k,     x0 + r - k, y0, x0 + r, y0))      != FLUX_OK) return rr;
    return flux_path_close(path);
}

flux_result flux_path_add_ellipse(flux_path *path, float cx, float cy, float rx, float ry)
{
    if (!path || rx <= 0.0f || ry <= 0.0f) return FLUX_ERROR_INVALID_ARGUMENT;

    float kx = rx * FLUX_KAPPA;
    float ky = ry * FLUX_KAPPA;

    flux_result r;
    if ((r = flux_path_move_to (path, cx + rx, cy))                                                       != FLUX_OK) return r;
    if ((r = flux_path_cubic_to(path, cx + rx, cy + ky, cx + kx, cy + ry, cx,      cy + ry))              != FLUX_OK) return r;
    if ((r = flux_path_cubic_to(path, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy))                   != FLUX_OK) return r;
    if ((r = flux_path_cubic_to(path, cx - rx, cy - ky, cx - kx, cy - ry, cx,      cy - ry))              != FLUX_OK) return r;
    if ((r = flux_path_cubic_to(path, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy))                   != FLUX_OK) return r;
    return flux_path_close(path);
}

flux_result flux_path_add_circle(flux_path *path, float cx, float cy, float radius)
{
    return flux_path_add_ellipse(path, cx, cy, radius, radius);
}

/* ------------------------------------------------------------------ */
/*  SVG elliptical arc                                                */
/* ------------------------------------------------------------------ */

flux_result flux_path_arc_to(flux_path *path, float rx, float ry, float phi,
                             bool large_arc, bool sweep, float x, float y)
{
    if (!path) return FLUX_ERROR_INVALID_ARGUMENT;

    if (path->point_count == 0) return flux_path_move_to(path, x, y);

    float x0 = path->points[path->point_count - 1].x;
    float y0 = path->points[path->point_count - 1].y;
    if (x0 == x && y0 == y) return FLUX_OK;

    rx = fabsf(rx);
    ry = fabsf(ry);
    if (rx == 0.0f || ry == 0.0f) return flux_path_line_to(path, x, y);

    float cos_phi = cosf(phi), sin_phi = sinf(phi);
    float dx2 = (x0 - x) * 0.5f;
    float dy2 = (y0 - y) * 0.5f;

    float x1p =  cos_phi * dx2 + sin_phi * dy2;
    float y1p = -sin_phi * dx2 + cos_phi * dy2;

    float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) {
        float scale = sqrtf(lambda);
        rx *= scale;
        ry *= scale;
    }

    float s = sqrtf(fmaxf(0.0f,
        ((rx * rx * ry * ry) - (rx * rx * y1p * y1p) - (ry * ry * x1p * x1p)) /
        ((rx * rx * y1p * y1p) + (ry * ry * x1p * x1p))));
    if (large_arc == sweep) s = -s;

    float cxp =  s * rx * y1p / ry;
    float cyp = -s * ry * x1p / rx;
    float cx  = cos_phi * cxp - sin_phi * cyp + (x0 + x) * 0.5f;
    float cy  = sin_phi * cxp + cos_phi * cyp + (y0 + y) * 0.5f;

    float ux = ( x1p - cxp) / rx;
    float uy = ( y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;
    float theta1 = atan2f(uy, ux);
    float delta_theta = atan2f(vy, vx) - atan2f(uy, ux);

    if (!sweep && delta_theta > 0.0f) delta_theta -= 2.0f * M_PI;
    if ( sweep && delta_theta < 0.0f) delta_theta += 2.0f * M_PI;

    int n_segments = (int)ceilf(fabsf(delta_theta) / (M_PI * 0.5f));
    if (n_segments < 1) n_segments = 1;
    float seg_delta = delta_theta / (float)n_segments;

    for (int i = 0; i < n_segments; ++i) {
        float a0 = theta1 + seg_delta * (float)i;
        float a1 = theta1 + seg_delta * (float)(i + 1);
        float d  = (4.0f / 3.0f) * tanf((a1 - a0) * 0.25f);

        float ca0 = cosf(a0), sa0 = sinf(a0);
        float ca1 = cosf(a1), sa1 = sinf(a1);

        float p1x = ca0 - d * sa0, p1y = sa0 + d * ca0;
        float p2x = ca1 + d * sa1, p2y = sa1 - d * ca1;

        float ex0 = rx * p1x,  ey0 = ry * p1y;
        float ex1 = rx * p2x,  ey1 = ry * p2y;
        float ex2 = rx * ca1,  ey2 = ry * sa1;

        float c0x = cos_phi * ex0 - sin_phi * ey0 + cx;
        float c0y = sin_phi * ex0 + cos_phi * ey0 + cy;
        float c1x = cos_phi * ex1 - sin_phi * ey1 + cx;
        float c1y = sin_phi * ex1 + cos_phi * ey1 + cy;
        float px  = cos_phi * ex2 - sin_phi * ey2 + cx;
        float py  = sin_phi * ex2 + cos_phi * ey2 + cy;

        flux_result r = flux_path_cubic_to(path, c0x, c0y, c1x, c1y, px, py);
        if (r != FLUX_OK) return r;
    }
    return FLUX_OK;
}

/* ------------------------------------------------------------------ */
/*  Queries                                                           */
/* ------------------------------------------------------------------ */

flux_result flux_path_get_bounds(const flux_path *path, flux_rect *out_bounds)
{
    if (!path || !out_bounds) return FLUX_ERROR_INVALID_ARGUMENT;
    if (!path->has_bounds) return FLUX_ERROR_INVALID_STATE;
    *out_bounds = path->bounds;
    return FLUX_OK;
}

size_t flux_path_verb_count(const flux_path *path)  { return path ? path->verb_count : 0; }
size_t flux_path_point_count(const flux_path *path) { return path ? path->point_count : 0; }

bool flux_path_is_axis_aligned_rect(const flux_path *path, flux_rect *out_rect)
{
    if (!path) return false;
    if (path->verb_count != 5 || path->point_count != 4) return false;
    if (path->verbs[0] != FLUX_PATH_MOVE  ||
        path->verbs[1] != FLUX_PATH_LINE  ||
        path->verbs[2] != FLUX_PATH_LINE  ||
        path->verbs[3] != FLUX_PATH_LINE  ||
        path->verbs[4] != FLUX_PATH_CLOSE) return false;
    if (!path->has_bounds) return false;

    flux_rect bounds = path->bounds;
    float min_x = bounds.x, min_y = bounds.y;
    float max_x = bounds.x + bounds.w;
    float max_y = bounds.y + bounds.h;
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return false;

    bool seen[4] = { false, false, false, false };
    for (size_t i = 0; i < 4; ++i) {
        const flux_point *pt = &path->points[i];
        if      (pt->x == min_x && pt->y == min_y) seen[0] = true;
        else if (pt->x == max_x && pt->y == min_y) seen[1] = true;
        else if (pt->x == max_x && pt->y == max_y) seen[2] = true;
        else if (pt->x == min_x && pt->y == max_y) seen[3] = true;
        else return false;
    }
    if (!(seen[0] && seen[1] && seen[2] && seen[3])) return false;
    if (out_rect) *out_rect = bounds;
    return true;
}

bool flux_path_get_line_loop(const flux_path *path,
                             const flux_point **out_points, size_t *out_count)
{
    if (!path) return false;
    if (path->verb_count < 4 || path->point_count < 3) return false;
    if (path->verbs[0] != FLUX_PATH_MOVE) return false;
    if (path->verbs[path->verb_count - 1] != FLUX_PATH_CLOSE) return false;

    for (size_t i = 1; i + 1 < path->verb_count; ++i)
        if (path->verbs[i] != FLUX_PATH_LINE) return false;

    if (out_points) *out_points = path->points;
    if (out_count)  *out_count  = path->point_count;
    return true;
}

bool flux_path_has_multiple_subpaths(const flux_path *path)
{
    if (!path) return false;
    size_t move_count = 0;
    for (size_t i = 0; i < path->verb_count; ++i)
        if (path->verbs[i] == FLUX_PATH_MOVE && ++move_count > 1) return true;
    return false;
}

size_t flux_path_subpath_count(const flux_path *path)
{
    if (!path) return 0;
    size_t count = 0;
    for (size_t i = 0; i < path->verb_count; ++i)
        if (path->verbs[i] == FLUX_PATH_MOVE) count++;
    return count;
}

/* ------------------------------------------------------------------ */
/*  Transform                                                         */
/* ------------------------------------------------------------------ */

flux_result flux_path_transform(const flux_path *src, const flux_matrix *m,
                                flux_path **out_path)
{
    if (!src || !m || !out_path) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_path *dst = NULL;
    flux_result r = flux_path_create(src->ctx, &dst);
    if (r != FLUX_OK) return r;

    if (!ensure_verb_capacity(dst, src->verb_count) ||
        !ensure_point_capacity(dst, src->point_count)) {
        flux_path_release(dst);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }

    memcpy(dst->verbs, src->verbs, src->verb_count);
    dst->verb_count = src->verb_count;

    for (size_t i = 0; i < src->point_count; ++i) {
        float x = src->points[i].x;
        float y = src->points[i].y;
        flux_matrix_transform_point(m, &x, &y);
        dst->points[i] = (flux_point){ x, y };
        update_bounds(dst, x, y);
    }
    dst->point_count = src->point_count;

    *out_path = dst;
    return FLUX_OK;
}

/* ------------------------------------------------------------------ */
/*  Subpath flattening (internal, consumed by engine)                 */
/* ------------------------------------------------------------------ */

static flux_result flatten_subpath_at(const flux_path *path,
                                      size_t start_verb, size_t start_pt,
                                      float tol_sq,
                                      flux_point **flat, size_t *flat_count, size_t *flat_cap,
                                      size_t *out_end_verb, size_t *out_end_pt,
                                      bool *out_closed)
{
    flux_point current = { 0 };
    flux_point start = { 0 };
    size_t point_i = start_pt;
    bool have_current = false;
    bool closed = false;
    size_t i;

    for (i = start_verb; i < path->verb_count; ++i) {
        uint8_t verb = path->verbs[i];

        if (verb == FLUX_PATH_MOVE) {
            if (have_current) break;
            if (point_i >= path->point_count) return FLUX_ERROR_INVALID_ARGUMENT;
            current = path->points[point_i++];
            start = current;
            have_current = true;
            flux_result r = append_flat_point(flat, flat_count, flat_cap, current);
            if (r != FLUX_OK) return r;
        } else if (verb == FLUX_PATH_LINE) {
            if (!have_current || point_i >= path->point_count) return FLUX_ERROR_INVALID_ARGUMENT;
            current = path->points[point_i++];
            flux_result r = append_flat_point(flat, flat_count, flat_cap, current);
            if (r != FLUX_OK) return r;
        } else if (verb == FLUX_PATH_QUAD) {
            if (!have_current || point_i + 1 >= path->point_count) return FLUX_ERROR_INVALID_ARGUMENT;
            flux_point c = path->points[point_i++];
            flux_point p = path->points[point_i++];
            flux_result r = flatten_quad_recursive(current, c, p, tol_sq, 0,
                                                   flat, flat_count, flat_cap);
            if (r != FLUX_OK) return r;
            current = p;
        } else if (verb == FLUX_PATH_CUBIC) {
            if (!have_current || point_i + 2 >= path->point_count) return FLUX_ERROR_INVALID_ARGUMENT;
            flux_point c0 = path->points[point_i++];
            flux_point c1 = path->points[point_i++];
            flux_point p  = path->points[point_i++];
            flux_result r = flatten_cubic_recursive(current, c0, c1, p, tol_sq, 0,
                                                    flat, flat_count, flat_cap);
            if (r != FLUX_OK) return r;
            current = p;
        } else if (verb == FLUX_PATH_CLOSE) {
            if (!have_current) return FLUX_ERROR_INVALID_ARGUMENT;
            if (!points_equal(current, start) && *flat_count >= 2 &&
                points_equal((*flat)[*flat_count - 1], start))
                (*flat_count)--;
            closed = true;
        } else {
            return FLUX_ERROR_INVALID_ARGUMENT;
        }
    }

    if (!have_current || *flat_count < 2) return FLUX_ERROR_INVALID_ARGUMENT;
    if (*flat_count >= 2 && points_equal((*flat)[*flat_count - 1], (*flat)[0]))
        (*flat_count)--;
    if ((closed && *flat_count < 3) || *flat_count < 2)
        return FLUX_ERROR_INVALID_ARGUMENT;

    *out_end_verb = i;
    *out_end_pt   = point_i;
    *out_closed   = closed;
    return FLUX_OK;
}

flux_result flux_path_flatten_subpath(const flux_path *path, size_t subpath_index,
                                      float tolerance, flux_arena *arena,
                                      flux_point **out_points, size_t *out_count,
                                      bool *out_closed)
{
    if (out_points) *out_points = nullptr;
    if (out_count)  *out_count  = 0;
    if (out_closed) *out_closed = false;
    if (!path || !out_points || !out_count) return FLUX_ERROR_INVALID_ARGUMENT;
    if (path->verb_count < 2 || path->point_count < 2) return FLUX_ERROR_INVALID_ARGUMENT;
    if (tolerance <= 0.0f) tolerance = 0.25f;
    float tol_sq = tolerance * tolerance;

    size_t verb_i = 0, pt_i = 0, move_seen = 0;
    for (size_t i = 0; i < path->verb_count; ++i) {
        uint8_t verb = path->verbs[i];
        if (verb == FLUX_PATH_MOVE) {
            if (move_seen == subpath_index) { verb_i = i; break; }
            move_seen++; pt_i++;
        }
        else if (verb == FLUX_PATH_LINE)  pt_i++;
        else if (verb == FLUX_PATH_QUAD)  pt_i += 2;
        else if (verb == FLUX_PATH_CUBIC) pt_i += 3;
    }
    if (verb_i >= path->verb_count) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_point *flat = nullptr;
    size_t flat_count = 0, flat_cap = 0;
    size_t end_verb, end_pt;
    bool closed = false;
    flux_result r = flatten_subpath_at(path, verb_i, pt_i, tol_sq,
                                       &flat, &flat_count, &flat_cap,
                                       &end_verb, &end_pt, &closed);
    if (r != FLUX_OK) { free(flat); return r; }

    *out_points = flux_arena_alloc(arena, flat_count * sizeof(flux_point));
    if (!*out_points) { free(flat); return FLUX_ERROR_OUT_OF_MEMORY; }
    memcpy(*out_points, flat, flat_count * sizeof(flux_point));
    *out_count = flat_count;
    if (out_closed) *out_closed = closed;
    free(flat);
    return FLUX_OK;
}

flux_result flux_path_flatten_polyline(const flux_path *path, float tolerance,
                                       flux_arena *arena,
                                       flux_point **out_points, size_t *out_count,
                                       bool *out_closed)
{
    if (out_points) *out_points = nullptr;
    if (out_count)  *out_count  = 0;
    if (out_closed) *out_closed = false;
    if (!path || !out_points || !out_count) return FLUX_ERROR_INVALID_ARGUMENT;
    if (path->verb_count < 2 || path->point_count < 2) return FLUX_ERROR_INVALID_ARGUMENT;
    if (tolerance <= 0.0f) tolerance = 0.25f;
    float tol_sq = tolerance * tolerance;

    flux_point *flat = nullptr;
    size_t flat_count = 0, flat_cap = 0;
    size_t end_verb, end_pt;
    bool closed = false;
    flux_result r = flatten_subpath_at(path, 0, 0, tol_sq,
                                       &flat, &flat_count, &flat_cap,
                                       &end_verb, &end_pt, &closed);
    if (r != FLUX_OK) { free(flat); return r; }
    if (end_verb != path->verb_count || end_pt != path->point_count) {
        free(flat);
        return FLUX_ERROR_INVALID_ARGUMENT;
    }

    *out_points = flux_arena_alloc(arena, flat_count * sizeof(flux_point));
    if (!*out_points) { free(flat); return FLUX_ERROR_OUT_OF_MEMORY; }
    memcpy(*out_points, flat, flat_count * sizeof(flux_point));
    *out_count = flat_count;
    if (out_closed) *out_closed = closed;
    free(flat);
    return FLUX_OK;
}

flux_result flux_path_flatten_line_loop(const flux_path *path, float tolerance,
                                        flux_arena *arena,
                                        flux_point **out_points, size_t *out_count)
{
    bool closed = false;
    flux_result r = flux_path_flatten_polyline(path, tolerance, arena,
                                               out_points, out_count, &closed);
    if (r != FLUX_OK) return r;
    if (!closed) {
        *out_points = nullptr;
        *out_count = 0;
        return FLUX_ERROR_INVALID_ARGUMENT;
    }
    return FLUX_OK;
}
