#include "internal.h"

struct vg_path {
    uint8_t  *verbs;
    size_t    verb_count;
    size_t    verb_cap;
    vg_point *points;
    size_t    point_count;
    size_t    point_cap;
    vg_rect   bounds;
    bool      has_bounds;
};

enum {
    VG_PATH_MOVE = 0,
    VG_PATH_LINE = 1,
    VG_PATH_QUAD = 2,
    VG_PATH_CUBIC = 3,
    VG_PATH_CLOSE = 4,
};

#define VG_FLATTEN_MAX_DEPTH 16

static bool ensure_verb_capacity(vg_path *path, size_t extra)
{
    size_t need = path->verb_count + extra;
    if (need <= path->verb_cap) return true;

    size_t new_cap = path->verb_cap ? path->verb_cap : 16;
    while (new_cap < need) new_cap *= 2;

    uint8_t *verbs = realloc(path->verbs, new_cap * sizeof(*verbs));
    if (!verbs) return false;

    path->verbs = verbs;
    path->verb_cap = new_cap;
    return true;
}

static bool ensure_point_capacity(vg_path *path, size_t extra)
{
    size_t need = path->point_count + extra;
    if (need <= path->point_cap) return true;

    size_t new_cap = path->point_cap ? path->point_cap : 32;
    while (new_cap < need) new_cap *= 2;

    vg_point *points = realloc(path->points, new_cap * sizeof(*points));
    if (!points) return false;

    path->points = points;
    path->point_cap = new_cap;
    return true;
}

static bool ensure_temp_point_capacity(vg_point **points,
                                       size_t *count,
                                       size_t *cap,
                                       size_t extra)
{
    size_t need = *count + extra;
    if (need <= *cap) return true;

    size_t new_cap = *cap ? *cap : 32;
    while (new_cap < need) new_cap *= 2;

    vg_point *grown = realloc(*points, new_cap * sizeof(*grown));
    if (!grown) return false;

    *points = grown;
    *cap = new_cap;
    return true;
}

static bool points_equal(vg_point a, vg_point b)
{
    return a.x == b.x && a.y == b.y;
}

static bool append_flat_point(vg_point **points,
                              size_t *count,
                              size_t *cap,
                              vg_point pt)
{
    if (*count && points_equal((*points)[*count - 1], pt)) return true;
    if (!ensure_temp_point_capacity(points, count, cap, 1)) return false;
    (*points)[(*count)++] = pt;
    return true;
}

static float point_line_distance_sq(vg_point p, vg_point a, vg_point b)
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

static bool flatten_quad_recursive(vg_point p0, vg_point p1, vg_point p2,
                                   float tol_sq, unsigned depth,
                                   vg_point **points,
                                   size_t *count, size_t *cap)
{
    vg_point p01;
    vg_point p12;
    vg_point p012;

    if (depth >= VG_FLATTEN_MAX_DEPTH ||
        point_line_distance_sq(p1, p0, p2) <= tol_sq) {
        return append_flat_point(points, count, cap, p2);
    }

    p01 = (vg_point){ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    p12 = (vg_point){ (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    p012 = (vg_point){ (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };

    return flatten_quad_recursive(p0, p01, p012, tol_sq, depth + 1,
                                  points, count, cap) &&
           flatten_quad_recursive(p012, p12, p2, tol_sq, depth + 1,
                                  points, count, cap);
}

static bool flatten_cubic_recursive(vg_point p0, vg_point p1,
                                    vg_point p2, vg_point p3,
                                    float tol_sq, unsigned depth,
                                    vg_point **points,
                                    size_t *count, size_t *cap)
{
    vg_point p01;
    vg_point p12;
    vg_point p23;
    vg_point p012;
    vg_point p123;
    vg_point p0123;

    if (depth >= VG_FLATTEN_MAX_DEPTH ||
        (point_line_distance_sq(p1, p0, p3) <= tol_sq &&
         point_line_distance_sq(p2, p0, p3) <= tol_sq)) {
        return append_flat_point(points, count, cap, p3);
    }

    p01 = (vg_point){ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    p12 = (vg_point){ (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    p23 = (vg_point){ (p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f };
    p012 = (vg_point){ (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };
    p123 = (vg_point){ (p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f };
    p0123 = (vg_point){ (p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f };

    return flatten_cubic_recursive(p0, p01, p012, p0123, tol_sq, depth + 1,
                                   points, count, cap) &&
           flatten_cubic_recursive(p0123, p123, p23, p3, tol_sq, depth + 1,
                                   points, count, cap);
}

static void update_bounds(vg_path *path, float x, float y)
{
    float min_x;
    float min_y;
    float max_x;
    float max_y;

    if (!path->has_bounds) {
        path->bounds = (vg_rect){ .x = x, .y = y, .w = 0.0f, .h = 0.0f };
        path->has_bounds = true;
        return;
    }

    min_x = path->bounds.x;
    min_y = path->bounds.y;
    max_x = path->bounds.x + path->bounds.w;
    max_y = path->bounds.y + path->bounds.h;

    if (x < min_x) min_x = x;
    if (y < min_y) min_y = y;
    if (x > max_x) max_x = x;
    if (y > max_y) max_y = y;

    path->bounds.x = min_x;
    path->bounds.y = min_y;
    path->bounds.w = max_x - min_x;
    path->bounds.h = max_y - min_y;
}

static bool push_verb_and_points(vg_path *path, uint8_t verb,
                                 const vg_point *points, size_t point_count)
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

vg_path *vg_path_create(void)
{
    return calloc(1, sizeof(vg_path));
}

void vg_path_destroy(vg_path *path)
{
    if (!path) return;
    free(path->verbs);
    free(path->points);
    free(path);
}

void vg_path_reset(vg_path *path)
{
    if (!path) return;
    path->verb_count = 0;
    path->point_count = 0;
    path->bounds = (vg_rect){ 0 };
    path->has_bounds = false;
}

bool vg_path_move_to(vg_path *path, float x, float y)
{
    vg_point pt = { .x = x, .y = y };
    return push_verb_and_points(path, VG_PATH_MOVE, &pt, 1);
}

bool vg_path_line_to(vg_path *path, float x, float y)
{
    vg_point pt = { .x = x, .y = y };
    return push_verb_and_points(path, VG_PATH_LINE, &pt, 1);
}

bool vg_path_quad_to(vg_path *path, float cx, float cy, float x, float y)
{
    vg_point pts[2] = {
        { .x = cx, .y = cy },
        { .x = x,  .y = y  },
    };
    return push_verb_and_points(path, VG_PATH_QUAD, pts, 2);
}

bool vg_path_cubic_to(vg_path *path,
                      float cx0, float cy0,
                      float cx1, float cy1,
                      float x, float y)
{
    vg_point pts[3] = {
        { .x = cx0, .y = cy0 },
        { .x = cx1, .y = cy1 },
        { .x = x,   .y = y   },
    };
    return push_verb_and_points(path, VG_PATH_CUBIC, pts, 3);
}

bool vg_path_close(vg_path *path)
{
    return push_verb_and_points(path, VG_PATH_CLOSE, NULL, 0);
}

bool vg_path_add_rect(vg_path *path, const vg_rect *rect)
{
    float x0;
    float y0;
    float x1;
    float y1;

    if (!path || !rect) return false;
    x0 = rect->x;
    y0 = rect->y;
    x1 = rect->x + rect->w;
    y1 = rect->y + rect->h;

    return vg_path_move_to(path, x0, y0) &&
           vg_path_line_to(path, x1, y0) &&
           vg_path_line_to(path, x1, y1) &&
           vg_path_line_to(path, x0, y1) &&
           vg_path_close(path);
}

bool vg_path_get_bounds(const vg_path *path, vg_rect *out_bounds)
{
    if (!path || !path->has_bounds) return false;
    if (out_bounds) *out_bounds = path->bounds;
    return true;
}

size_t vg_path_verb_count(const vg_path *path)
{
    return path ? path->verb_count : 0;
}

size_t vg_path_point_count(const vg_path *path)
{
    return path ? path->point_count : 0;
}

bool vg_path_is_axis_aligned_rect(const vg_path *path, vg_rect *out_rect)
{
    vg_rect bounds;
    float min_x;
    float min_y;
    float max_x;
    float max_y;
    bool seen[4] = { false, false, false, false };

    if (!path) return false;
    if (path->verb_count != 5 || path->point_count != 4) return false;
    if (path->verbs[0] != VG_PATH_MOVE ||
        path->verbs[1] != VG_PATH_LINE ||
        path->verbs[2] != VG_PATH_LINE ||
        path->verbs[3] != VG_PATH_LINE ||
        path->verbs[4] != VG_PATH_CLOSE) {
        return false;
    }
    if (!vg_path_get_bounds(path, &bounds)) return false;

    min_x = bounds.x;
    min_y = bounds.y;
    max_x = bounds.x + bounds.w;
    max_y = bounds.y + bounds.h;
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return false;

    for (size_t i = 0; i < 4; ++i) {
        const vg_point *pt = &path->points[i];
        if (pt->x == min_x && pt->y == min_y) seen[0] = true;
        else if (pt->x == max_x && pt->y == min_y) seen[1] = true;
        else if (pt->x == max_x && pt->y == max_y) seen[2] = true;
        else if (pt->x == min_x && pt->y == max_y) seen[3] = true;
        else return false;
    }

    if (!(seen[0] && seen[1] && seen[2] && seen[3])) return false;
    if (out_rect) *out_rect = bounds;
    return true;
}

bool vg_path_get_line_loop(const vg_path *path,
                           const vg_point **out_points,
                           size_t *out_count)
{
    if (!path) return false;
    if (path->verb_count < 4 || path->point_count < 3) return false;
    if (path->verbs[0] != VG_PATH_MOVE) return false;
    if (path->verbs[path->verb_count - 1] != VG_PATH_CLOSE) return false;

    for (size_t i = 1; i + 1 < path->verb_count; ++i)
        if (path->verbs[i] != VG_PATH_LINE) return false;

    if (out_points) *out_points = path->points;
    if (out_count) *out_count = path->point_count;
    return true;
}

bool vg_path_flatten_polyline(const vg_path *path, float tolerance,
                              vg_point **out_points, size_t *out_count,
                              bool *out_closed)
{
    vg_point *flat = NULL;
    size_t flat_count = 0;
    size_t flat_cap = 0;
    vg_point current = { 0 };
    vg_point start = { 0 };
    size_t point_i = 0;
    bool have_current = false;
    float tol_sq;

    if (out_points) *out_points = NULL;
    if (out_count) *out_count = 0;
    if (out_closed) *out_closed = false;
    if (!path || !out_points || !out_count) return false;
    if (path->verb_count < 2 || path->point_count < 2) return false;
    if (tolerance <= 0.0f) tolerance = 0.25f;
    tol_sq = tolerance * tolerance;

    for (size_t i = 0; i < path->verb_count; ++i) {
        uint8_t verb = path->verbs[i];

        if (verb == VG_PATH_MOVE) {
            if (have_current || point_i >= path->point_count) goto fail;
            current = path->points[point_i++];
            start = current;
            have_current = true;
            if (!append_flat_point(&flat, &flat_count, &flat_cap, current))
                goto fail;
        } else if (verb == VG_PATH_LINE) {
            if (!have_current || point_i >= path->point_count) goto fail;
            current = path->points[point_i++];
            if (!append_flat_point(&flat, &flat_count, &flat_cap, current))
                goto fail;
        } else if (verb == VG_PATH_QUAD) {
            vg_point c;
            vg_point p;
            if (!have_current || point_i + 1 >= path->point_count) goto fail;
            c = path->points[point_i++];
            p = path->points[point_i++];
            if (!flatten_quad_recursive(current, c, p, tol_sq, 0,
                                        &flat, &flat_count, &flat_cap)) {
                goto fail;
            }
            current = p;
        } else if (verb == VG_PATH_CUBIC) {
            vg_point c0;
            vg_point c1;
            vg_point p;
            if (!have_current || point_i + 2 >= path->point_count) goto fail;
            c0 = path->points[point_i++];
            c1 = path->points[point_i++];
            p = path->points[point_i++];
            if (!flatten_cubic_recursive(current, c0, c1, p, tol_sq, 0,
                                         &flat, &flat_count, &flat_cap)) {
                goto fail;
            }
            current = p;
        } else if (verb == VG_PATH_CLOSE) {
            if (!have_current || i != path->verb_count - 1) goto fail;
            if (!points_equal(current, start) && flat_count >= 2 &&
                points_equal(flat[flat_count - 1], start)) {
                flat_count--;
            }
            if (out_closed) *out_closed = true;
        } else {
            goto fail;
        }
    }

    if (point_i != path->point_count || flat_count < 2) goto fail;
    if (flat_count >= 2 && points_equal(flat[flat_count - 1], flat[0]))
        flat_count--;
    if ((out_closed && *out_closed && flat_count < 3) || flat_count < 2)
        goto fail;

    *out_points = flat;
    *out_count = flat_count;
    return true;

fail:
    free(flat);
    return false;
}

bool vg_path_flatten_line_loop(const vg_path *path, float tolerance,
                               vg_point **out_points, size_t *out_count)
{
    bool closed = false;

    if (!vg_path_flatten_polyline(path, tolerance, out_points, out_count, &closed))
        return false;
    if (!closed) {
        free(*out_points);
        *out_points = NULL;
        *out_count = 0;
        return false;
    }
    return true;
}

/* ---------- matrix & path transform ---------- */

void vg_matrix_multiply(vg_matrix *out, const vg_matrix *a, const vg_matrix *b)
{
    float a0 = a->m[0], a1 = a->m[1], a2 = a->m[2], a3 = a->m[3], a4 = a->m[4], a5 = a->m[5];
    float b0 = b->m[0], b1 = b->m[1], b2 = b->m[2], b3 = b->m[3], b4 = b->m[4], b5 = b->m[5];

    /*
     * [a0 a2 a4]   [b0 b2 b4]
     * [a1 a3 a5] * [b1 b3 b5]
     * [0  0  1 ]   [0  0  1 ]
     */
    out->m[0] = a0 * b0 + a2 * b1;
    out->m[1] = a1 * b0 + a3 * b1;
    out->m[2] = a0 * b2 + a2 * b3;
    out->m[3] = a1 * b2 + a3 * b3;
    out->m[4] = a0 * b4 + a2 * b5 + a4;
    out->m[5] = a1 * b4 + a3 * b5 + a5;
}

void vg_matrix_transform_point(const vg_matrix *m, float *x, float *y)
{
    float px = *x;
    float py = *y;
    *x = m->m[0] * px + m->m[2] * py + m->m[4];
    *y = m->m[1] * px + m->m[3] * py + m->m[5];
}

vg_path *vg_path_transform(const vg_path *src, const vg_matrix *m)
{
    if (!src || !m) return NULL;
    if (vg_matrix_is_identity(m)) {
        /* Optimization: if identity, we could just copy, but transform
         * is more robust if the caller expects a new path. */
    }

    vg_path *dst = vg_path_create();
    if (!dst) return NULL;

    if (!ensure_verb_capacity(dst, src->verb_count) ||
        !ensure_point_capacity(dst, src->point_count)) {
        vg_path_destroy(dst);
        return NULL;
    }

    memcpy(dst->verbs, src->verbs, src->verb_count);
    dst->verb_count = src->verb_count;

    for (size_t i = 0; i < src->point_count; ++i) {
        float x = src->points[i].x;
        float y = src->points[i].y;
        vg_matrix_transform_point(m, &x, &y);
        dst->points[i] = (vg_point){ x, y };
        update_bounds(dst, x, y);
    }
    dst->point_count = src->point_count;

    return dst;
}
