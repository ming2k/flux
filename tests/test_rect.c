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

/* ---------- fx_rect operations ---------- */

static bool test_rect_contains_center(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(fx_rect_contains(&r, 5.0f, 5.0f));
    return true;
}

static bool test_rect_contains_corners(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(fx_rect_contains(&r, 0.0f, 0.0f));
    CHECK(fx_rect_contains(&r, 10.0f, 0.0f));
    CHECK(fx_rect_contains(&r, 10.0f, 10.0f));
    CHECK(fx_rect_contains(&r, 0.0f, 10.0f));
    return true;
}

static bool test_rect_contains_outside_left(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(!fx_rect_contains(&r, -0.1f, 5.0f));
    return true;
}

static bool test_rect_contains_outside_right(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(!fx_rect_contains(&r, 10.1f, 5.0f));
    return true;
}

static bool test_rect_contains_outside_top(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(!fx_rect_contains(&r, 5.0f, -0.1f));
    return true;
}

static bool test_rect_contains_outside_bottom(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(!fx_rect_contains(&r, 5.0f, 10.1f));
    return true;
}

static bool test_rect_contains_null_rect(void)
{
    CHECK(!fx_rect_contains(nullptr, 0.0f, 0.0f));
    return true;
}

static bool test_rect_contains_zero_width(void)
{
    fx_rect r = { .x = 5.0f, .y = 0.0f, .w = 0.0f, .h = 10.0f };
    CHECK(fx_rect_contains(&r, 5.0f, 5.0f));
    CHECK(!fx_rect_contains(&r, 5.1f, 5.0f));
    return true;
}

static bool test_rect_contains_zero_height(void)
{
    fx_rect r = { .x = 0.0f, .y = 5.0f, .w = 10.0f, .h = 0.0f };
    CHECK(fx_rect_contains(&r, 5.0f, 5.0f));
    CHECK(!fx_rect_contains(&r, 5.0f, 5.1f));
    return true;
}

static bool test_rect_contains_negative_width(void)
{
    /* Negative width: x >= 10 AND x <= 5 is impossible */
    fx_rect r = { .x = 10.0f, .y = 0.0f, .w = -5.0f, .h = 10.0f };
    CHECK(!fx_rect_contains(&r, 8.0f, 5.0f));
    CHECK(!fx_rect_contains(&r, 12.0f, 5.0f));
    return true;
}

static bool test_rect_contains_negative_height(void)
{
    /* Negative height: y >= 10 AND y <= 5 is impossible */
    fx_rect r = { .x = 0.0f, .y = 10.0f, .w = 10.0f, .h = -5.0f };
    CHECK(!fx_rect_contains(&r, 5.0f, 8.0f));
    CHECK(!fx_rect_contains(&r, 5.0f, 12.0f));
    return true;
}

static bool test_rect_contains_translated(void)
{
    fx_rect r = { .x = -10.0f, .y = -20.0f, .w = 5.0f, .h = 5.0f };
    CHECK(fx_rect_contains(&r, -8.0f, -18.0f));
    CHECK(!fx_rect_contains(&r, 0.0f, 0.0f));
    return true;
}

static bool test_rect_contains_large_values(void)
{
    fx_rect r = { .x = 1e6f, .y = 1e6f, .w = 100.0f, .h = 100.0f };
    CHECK(fx_rect_contains(&r, 1e6f + 50.0f, 1e6f + 50.0f));
    CHECK(!fx_rect_contains(&r, 1e6f - 1.0f, 1e6f + 50.0f));
    return true;
}

/* ---------- fx_rect initialization patterns ---------- */

static bool test_rect_zero_init(void)
{
    fx_rect r = { 0 };
    CHECK(r.x == 0.0f);
    CHECK(r.y == 0.0f);
    CHECK(r.w == 0.0f);
    CHECK(r.h == 0.0f);
    CHECK(fx_rect_contains(&r, 0.0f, 0.0f));
    return true;
}

static bool test_rect_designated_init(void)
{
    fx_rect r = { .x = 1.0f, .y = 2.0f, .w = 3.0f, .h = 4.0f };
    CHECK(r.x == 1.0f);
    CHECK(r.y == 2.0f);
    CHECK(r.w == 3.0f);
    CHECK(r.h == 4.0f);
    return true;
}

int main(void)
{
    if (!test_rect_contains_center()) return 1;
    if (!test_rect_contains_corners()) return 1;
    if (!test_rect_contains_outside_left()) return 1;
    if (!test_rect_contains_outside_right()) return 1;
    if (!test_rect_contains_outside_top()) return 1;
    if (!test_rect_contains_outside_bottom()) return 1;
    if (!test_rect_contains_null_rect()) return 1;
    if (!test_rect_contains_zero_width()) return 1;
    if (!test_rect_contains_zero_height()) return 1;
    if (!test_rect_contains_negative_width()) return 1;
    if (!test_rect_contains_negative_height()) return 1;
    if (!test_rect_contains_translated()) return 1;
    if (!test_rect_contains_large_values()) return 1;
    if (!test_rect_zero_init()) return 1;
    if (!test_rect_designated_init()) return 1;
    printf("rect OK\n");
    return 0;
}
