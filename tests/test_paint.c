#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__,     \
                    __LINE__);                                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

/* ---------- fx_paint_init ---------- */

static bool test_paint_init_defaults(void)
{
    fx_paint p;
    fx_paint_init(&p, 0xFFFF0000u);
    CHECK(p.color == 0xFFFF0000u);
    CHECK(p.stroke_width == 1.0f);
    CHECK(p.miter_limit == 4.0f);
    CHECK(p.line_cap == FX_CAP_BUTT);
    CHECK(p.line_join == FX_JOIN_MITER);
    CHECK(p.gradient == nullptr);
    return true;
}

static bool test_paint_init_different_colors(void)
{
    fx_paint p;
    fx_paint_init(&p, 0xFF00FF00u);
    CHECK(p.color == 0xFF00FF00u);

    fx_paint_init(&p, 0x00000000u);
    CHECK(p.color == 0x00000000u);
    return true;
}

static bool test_paint_init_null(void)
{
    /* Must not crash */
    fx_paint_init(nullptr, 0xFFFFFFFFu);
    return true;
}

/* ---------- fx_paint_set_gradient ---------- */

static bool test_paint_set_gradient(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 0.0f, 0.0f },
            .end = { 10.0f, 0.0f },
            .colors = { 0xFFFF0000u, 0xFF0000FFu },
            .stops = { 0.0f, 1.0f },
            .stop_count = 2,
        });
    /* gradient creation with fake ctx may fail; that's ok for this test */
    fx_paint p;
    fx_paint_init(&p, 0xFFFFFFFFu);
    fx_paint_set_gradient(&p, g);
    CHECK(p.gradient == g);
    return true;
}

static bool test_paint_set_gradient_null(void)
{
    fx_paint p;
    fx_paint_init(&p, 0xFFFFFFFFu);
    fx_paint_set_gradient(&p, nullptr);
    CHECK(p.gradient == nullptr);
    return true;
}

static bool test_paint_set_gradient_to_null_paint(void)
{
    /* Must not crash */
    fx_paint_set_gradient(nullptr, nullptr);
    return true;
}

/* ---------- fx_paint isolation ---------- */

static bool test_paint_isolation(void)
{
    fx_paint p1, p2;
    fx_paint_init(&p1, 0xFFFF0000u);
    fx_paint_init(&p2, 0xFF00FF00u);

    p1.stroke_width = 5.0f;
    p1.miter_limit = 10.0f;
    p1.line_cap = FX_CAP_ROUND;
    p1.line_join = FX_JOIN_BEVEL;

    CHECK(p2.stroke_width == 1.0f);
    CHECK(p2.miter_limit == 4.0f);
    CHECK(p2.line_cap == FX_CAP_BUTT);
    CHECK(p2.line_join == FX_JOIN_MITER);
    return true;
}

int main(void)
{
    if (!test_paint_init_defaults()) return 1;
    if (!test_paint_init_different_colors()) return 1;
    if (!test_paint_init_null()) return 1;
    if (!test_paint_set_gradient()) return 1;
    if (!test_paint_set_gradient_null()) return 1;
    if (!test_paint_set_gradient_to_null_paint()) return 1;
    if (!test_paint_isolation()) return 1;
    printf("paint OK\n");
    return 0;
}
