#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__,     \
                    __LINE__);                                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

/* ---------- fx_color_rgba basic correctness ---------- */

static bool test_color_rgba_opaque(void)
{
    /* Pure red, full alpha */
    fx_color c = fx_color_rgba(255, 0, 0, 255);
    CHECK(c == 0xFFFF0000u);

    /* Pure green */
    c = fx_color_rgba(0, 255, 0, 255);
    CHECK(c == 0xFF00FF00u);

    /* Pure blue */
    c = fx_color_rgba(0, 0, 255, 255);
    CHECK(c == 0xFF0000FFu);

    /* White */
    c = fx_color_rgba(255, 255, 255, 255);
    CHECK(c == 0xFFFFFFFFu);

    /* Black */
    c = fx_color_rgba(0, 0, 0, 255);
    CHECK(c == 0xFF000000u);
    return true;
}

static bool test_color_rgba_transparent(void)
{
    /* Fully transparent should yield 0 in all channels */
    fx_color c = fx_color_rgba(255, 255, 255, 0);
    CHECK(c == 0x00000000u);

    c = fx_color_rgba(128, 64, 32, 0);
    CHECK(c == 0x00000000u);
    return true;
}

static bool test_color_rgba_premultiplication(void)
{
    /* 50% opacity: components should be approximately halved */
    fx_color c = fx_color_rgba(255, 255, 255, 128);
    uint8_t a = (c >> 24) & 0xFF;
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b = (c >> 0)  & 0xFF;

    CHECK(a == 128);
    /* (255 * 128 + 127) / 255 = 128 (rounded) */
    CHECK(r == 128);
    CHECK(g == 128);
    CHECK(b == 128);
    return true;
}

static bool test_color_rgba_premult_exact(void)
{
    /* Verify the +127 rounding bias */
    fx_color c = fx_color_rgba(1, 1, 1, 1);
    uint8_t a = (c >> 24) & 0xFF;
    uint8_t r = (c >> 16) & 0xFF;
    /* (1 * 1 + 127) / 255 = 0 */
    CHECK(a == 1);
    CHECK(r == 0);
    return true;
}

static bool test_color_rgba_all_values(void)
{
    /* Spot-check premultiplication across the range */
    for (int v = 0; v <= 255; ++v) {
        fx_color c = fx_color_rgba((uint8_t)v, 0, 0, 255);
        uint8_t r = (c >> 16) & 0xFF;
        CHECK(r == v);
    }
    return true;
}

static bool test_color_rgba_gradual_alpha(void)
{
    /* Red at various alphas */
    for (int a = 0; a <= 255; ++a) {
        fx_color c = fx_color_rgba(255, 0, 0, (uint8_t)a);
        uint8_t ca = (c >> 24) & 0xFF;
        uint8_t cr = (c >> 16) & 0xFF;
        CHECK(ca == a);
        unsigned expected = ((unsigned)255 * a + 127) / 255;
        CHECK(cr == (uint8_t)expected);
    }
    return true;
}

/* ---------- fx_rect_contains ---------- */

static bool test_rect_contains_inside(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(fx_rect_contains(&r, 5.0f, 5.0f));
    CHECK(fx_rect_contains(&r, 0.0f, 0.0f));
    CHECK(fx_rect_contains(&r, 10.0f, 10.0f));
    return true;
}

static bool test_rect_contains_outside(void)
{
    fx_rect r = { .x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f };
    CHECK(!fx_rect_contains(&r, -0.1f, 5.0f));
    CHECK(!fx_rect_contains(&r, 5.0f, -0.1f));
    CHECK(!fx_rect_contains(&r, 10.1f, 5.0f));
    CHECK(!fx_rect_contains(&r, 5.0f, 10.1f));
    return true;
}

static bool test_rect_contains_null(void)
{
    CHECK(!fx_rect_contains(nullptr, 0.0f, 0.0f));
    return true;
}

static bool test_rect_contains_zero_size(void)
{
    fx_rect r = { .x = 5.0f, .y = 5.0f, .w = 0.0f, .h = 0.0f };
    CHECK(fx_rect_contains(&r, 5.0f, 5.0f));
    CHECK(!fx_rect_contains(&r, 5.1f, 5.0f));
    return true;
}

static bool test_rect_contains_negative_size(void)
{
    /* Negative width/height: containment requires x >= x AND x <= x+w, impossible */
    fx_rect r = { .x = 10.0f, .y = 10.0f, .w = -5.0f, .h = -5.0f };
    CHECK(!fx_rect_contains(&r, 8.0f, 8.0f));
    CHECK(!fx_rect_contains(&r, 12.0f, 12.0f));
    return true;
}

int main(void)
{
    if (!test_color_rgba_opaque()) return 1;
    if (!test_color_rgba_transparent()) return 1;
    if (!test_color_rgba_premultiplication()) return 1;
    if (!test_color_rgba_premult_exact()) return 1;
    if (!test_color_rgba_all_values()) return 1;
    if (!test_color_rgba_gradual_alpha()) return 1;
    if (!test_rect_contains_inside()) return 1;
    if (!test_rect_contains_outside()) return 1;
    if (!test_rect_contains_null()) return 1;
    if (!test_rect_contains_zero_size()) return 1;
    if (!test_rect_contains_negative_size()) return 1;
    printf("color OK\n");
    return 0;
}
