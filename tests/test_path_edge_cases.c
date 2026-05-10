#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__,     \
                    __LINE__);                                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define EPS 1.0f

static bool approx_eq(float a, float b) { return fabsf(a - b) < EPS; }

/* ---------- Arc large-arc / sweep combinations ---------- */

static bool test_arc_all_combinations(void)
{
    /* SVG F.6: 4 combinations of (large_arc, sweep) for quarter circle */
    for (int la = 0; la <= 1; ++la) {
        for (int sw = 0; sw <= 1; ++sw) {
            fx_path *path = fx_path_create();
            CHECK(path != nullptr);
            CHECK(fx_path_move_to(path, 100.0f, 0.0f));
            CHECK(fx_path_arc_to(path, 100.0f, 100.0f, 0.0f,
                                 (bool)la, (bool)sw, 0.0f, 100.0f));
            CHECK(fx_path_verb_count(path) >= 2);

            size_t pts = fx_path_point_count(path);
            CHECK(pts >= 1);

            /* Last point should be the target */
            fx_rect bounds;
            CHECK(fx_path_get_bounds(path, &bounds));
            /* Target is (0,100), so bounds should include it */
            CHECK(bounds.x <= 0.0f);
            CHECK(bounds.y <= 0.0f);
            CHECK(bounds.x + bounds.w >= 100.0f);
            CHECK(bounds.y + bounds.h >= 100.0f);

            fx_path_destroy(path);
        }
    }
    return true;
}

/* ---------- Arc with rotation (phi != 0) ---------- */

static bool test_arc_with_rotation(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 100.0f, 0.0f));
    /* 45-degree rotated ellipse */
    CHECK(fx_path_arc_to(path, 100.0f, 50.0f, 3.14159265f / 4.0f,
                         false, false, 0.0f, 100.0f));
    CHECK(fx_path_verb_count(path) >= 2);
    fx_path_destroy(path);
    return true;
}

/* ---------- Arc degenerate cases ---------- */

static bool test_arc_rx_ry_zero(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    /* Both radii zero => line to target */
    CHECK(fx_path_arc_to(path, 0.0f, 0.0f, 0.0f, false, false, 50.0f, 50.0f));
    CHECK(fx_path_verb_count(path) == 2); /* move + line */
    fx_path_destroy(path);
    return true;
}

static bool test_arc_negative_radii(void)
{
    /* Negative radii should be treated as absolute values */
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 100.0f, 0.0f));
    CHECK(fx_path_arc_to(path, -100.0f, -100.0f, 0.0f,
                         false, false, 0.0f, 100.0f));
    CHECK(fx_path_verb_count(path) >= 2);
    fx_path_destroy(path);
    return true;
}

/* ---------- Path with no current point ---------- */

static bool test_arc_no_current_point(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    /* Arc with no current point should act as move_to */
    CHECK(fx_path_arc_to(path, 50.0f, 50.0f, 0.0f,
                         false, false, 10.0f, 10.0f));
    CHECK(fx_path_verb_count(path) == 1); /* move */
    CHECK(fx_path_point_count(path) == 1);
    fx_path_destroy(path);
    return true;
}

/* ---------- Path with single point (open) ---------- */

static bool test_path_flatten_open_line(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 0.0f));
    /* Not closed => flatten_line_loop should reject */
    fx_arena arena;
    fx_arena_init(&arena, 0);
    fx_point *pts = nullptr;
    size_t count = 0;
    CHECK(!fx_path_flatten_line_loop(path, 0.25f, &arena, &pts, &count));
    fx_arena_destroy(&arena);
    fx_path_destroy(path);
    return true;
}

/* ---------- Path bounds with single point ---------- */

static bool test_path_bounds_single_point(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 5.0f, 10.0f));
    fx_rect bounds;
    /* move_to sets bounds even for a single point */
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 5.0f);
    CHECK(bounds.y == 10.0f);
    CHECK(bounds.w == 0.0f);
    CHECK(bounds.h == 0.0f);
    fx_path_destroy(path);
    return true;
}

/* ---------- Path reset clears bounds ---------- */

static bool test_path_reset_clears_bounds(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 10.0f));
    fx_rect bounds;
    CHECK(fx_path_get_bounds(path, &bounds));
    fx_path_reset(path);
    CHECK(!fx_path_get_bounds(path, &bounds));
    fx_path_destroy(path);
    return true;
}

/* ---------- Path with duplicate points ---------- */

static bool test_path_duplicate_points(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_line_to(path, 0.0f, 0.0f)); /* same as start */
    CHECK(fx_path_line_to(path, 10.0f, 0.0f));
    CHECK(fx_path_close(path));
    CHECK(fx_path_point_count(path) == 3);
    fx_path_destroy(path);
    return true;
}

/* ---------- Path subpath count ---------- */

static bool test_path_subpath_count(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_subpath_count(path) == 0);

    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_subpath_count(path) == 1);

    CHECK(fx_path_line_to(path, 10.0f, 0.0f));
    CHECK(fx_path_subpath_count(path) == 1);

    CHECK(fx_path_move_to(path, 20.0f, 20.0f));
    CHECK(fx_path_subpath_count(path) == 2);

    fx_path_destroy(path);
    return true;
}

/* ---------- Path verb/point count after reset ---------- */

static bool test_path_counts_after_reset(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 0.0f));
    CHECK(fx_path_close(path));
    CHECK(fx_path_verb_count(path) == 3);
    CHECK(fx_path_point_count(path) == 2);

    fx_path_reset(path);
    CHECK(fx_path_verb_count(path) == 0);
    CHECK(fx_path_point_count(path) == 0);
    fx_path_destroy(path);
    return true;
}

/* ---------- Path with quad and cubic ---------- */

static bool test_path_quad_bounds(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_quad_to(path, 5.0f, 10.0f, 10.0f, 0.0f));
    fx_rect bounds;
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 0.0f);
    CHECK(bounds.y == 0.0f);
    CHECK(bounds.w == 10.0f);
    CHECK(bounds.h >= 0.0f);
    fx_path_destroy(path);
    return true;
}

static bool test_path_cubic_bounds(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_cubic_to(path, 2.0f, 10.0f, 8.0f, 10.0f, 10.0f, 0.0f));
    fx_rect bounds;
    CHECK(fx_path_get_bounds(path, &bounds));
    CHECK(bounds.x == 0.0f);
    CHECK(bounds.y == 0.0f);
    CHECK(bounds.w == 10.0f);
    fx_path_destroy(path);
    return true;
}

/* ---------- Path axis-aligned rect detection ---------- */

static bool test_path_axis_aligned_rect_true(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 5.0f));
    CHECK(fx_path_line_to(path, 0.0f, 5.0f));
    CHECK(fx_path_close(path));
    fx_rect r;
    CHECK(fx_path_is_axis_aligned_rect(path, &r));
    CHECK(r.x == 0.0f);
    CHECK(r.y == 0.0f);
    CHECK(r.w == 10.0f);
    CHECK(r.h == 5.0f);
    fx_path_destroy(path);
    return true;
}

static bool test_path_axis_aligned_rect_false(void)
{
    /* Diamond shape is not axis-aligned rect */
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 5.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 5.0f));
    CHECK(fx_path_line_to(path, 5.0f, 10.0f));
    CHECK(fx_path_line_to(path, 0.0f, 5.0f));
    CHECK(fx_path_close(path));
    fx_rect r;
    CHECK(!fx_path_is_axis_aligned_rect(path, &r));
    fx_path_destroy(path);
    return true;
}

/* ---------- Path line loop detection ---------- */

static bool test_path_get_line_loop_true(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 0.0f));
    CHECK(fx_path_line_to(path, 10.0f, 10.0f));
    CHECK(fx_path_close(path));
    const fx_point *pts = nullptr;
    size_t count = 0;
    CHECK(fx_path_get_line_loop(path, &pts, &count));
    CHECK(count == 3);
    CHECK(pts != nullptr);
    fx_path_destroy(path);
    return true;
}

static bool test_path_get_line_loop_false_quad(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    CHECK(fx_path_move_to(path, 0.0f, 0.0f));
    CHECK(fx_path_quad_to(path, 5.0f, 10.0f, 10.0f, 0.0f));
    CHECK(fx_path_close(path));
    const fx_point *pts = nullptr;
    size_t count = 0;
    CHECK(!fx_path_get_line_loop(path, &pts, &count));
    fx_path_destroy(path);
    return true;
}

int main(void)
{
    if (!test_arc_all_combinations()) return 1;
    if (!test_arc_with_rotation()) return 1;
    if (!test_arc_rx_ry_zero()) return 1;
    if (!test_arc_negative_radii()) return 1;
    if (!test_arc_no_current_point()) return 1;
    if (!test_path_flatten_open_line()) return 1;
    if (!test_path_bounds_single_point()) return 1;
    if (!test_path_reset_clears_bounds()) return 1;
    if (!test_path_duplicate_points()) return 1;
    if (!test_path_subpath_count()) return 1;
    if (!test_path_counts_after_reset()) return 1;
    if (!test_path_quad_bounds()) return 1;
    if (!test_path_cubic_bounds()) return 1;
    if (!test_path_axis_aligned_rect_true()) return 1;
    if (!test_path_axis_aligned_rect_false()) return 1;
    if (!test_path_get_line_loop_true()) return 1;
    if (!test_path_get_line_loop_false_quad()) return 1;
    printf("path_edge_cases OK\n");
    return 0;
}
