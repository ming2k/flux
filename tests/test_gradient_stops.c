#include "internal.h"
#include <math.h>
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

#define EPS 1e-4f

static bool approx_eq(float a, float b) { return fabsf(a - b) < EPS; }

/* ---------- Linear gradient basic ---------- */

static bool test_gradient_linear_two_stops(void)
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
    CHECK(g != nullptr);
    CHECK(g->mode == 0);
    CHECK(approx_eq(g->start[0], 0.0f));
    CHECK(approx_eq(g->start[1], 0.0f));
    CHECK(approx_eq(g->end[0], 10.0f));
    CHECK(approx_eq(g->end[1], 0.0f));
    CHECK(g->stop_count == 2);
    fx_gradient_destroy(g);
    return true;
}

static bool test_gradient_linear_four_stops(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 0.0f, 0.0f },
            .end = { 100.0f, 100.0f },
            .colors = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu },
            .stops = { 0.0f, 0.33f, 0.67f, 1.0f },
            .stop_count = 4,
        });
    CHECK(g != nullptr);
    CHECK(g->stop_count == 4);
    fx_gradient_destroy(g);
    return true;
}

/* ---------- Radial gradient basic ---------- */

static bool test_gradient_radial_two_stops(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_radial(
        &fake_ctx,
        &(fx_radial_gradient_desc){
            .center = { 50.0f, 50.0f },
            .radius = 25.0f,
            .colors = { 0xFFFF0000u, 0xFF0000FFu },
            .stops = { 0.0f, 1.0f },
            .stop_count = 2,
        });
    CHECK(g != nullptr);
    CHECK(g->mode == 1);
    CHECK(approx_eq(g->start[0], 50.0f));
    CHECK(approx_eq(g->start[1], 50.0f));
    CHECK(approx_eq(g->end[0], 25.0f));
    CHECK(approx_eq(g->end[1], 0.0f));
    fx_gradient_destroy(g);
    return true;
}

/* ---------- Gradient stop boundary values ---------- */

static bool test_gradient_min_stops(void)
{
    fx_context fake_ctx = { 0 };
    /* stop_count < 2 should fail */
    CHECK(fx_gradient_create_linear(
              &fake_ctx,
              &(fx_linear_gradient_desc){ .stop_count = 1,
                                           .stops = { 0.0f } })
          == nullptr);
    CHECK(fx_gradient_create_radial(
              &fake_ctx,
              &(fx_radial_gradient_desc){ .stop_count = 1,
                                            .stops = { 0.0f } })
          == nullptr);
    return true;
}

static bool test_gradient_max_stops(void)
{
    fx_context fake_ctx = { 0 };
    /* stop_count > 4 should fail */
    CHECK(fx_gradient_create_linear(
              &fake_ctx,
              &(fx_linear_gradient_desc){ .stop_count = 5,
                                           .stops = { 0.0f, 0.25f, 0.5f,
                                                     0.75f, 1.0f } })
          == nullptr);
    return true;
}

static bool test_gradient_exactly_two_stops(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 0.0f, 0.0f },
            .end = { 1.0f, 0.0f },
            .colors = { 0xFF000000u, 0xFFFFFFFFu },
            .stops = { 0.0f, 1.0f },
            .stop_count = 2,
        });
    CHECK(g != nullptr);
    CHECK(g->stop_count == 2);
    fx_gradient_destroy(g);
    return true;
}

static bool test_gradient_exactly_four_stops(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_radial(
        &fake_ctx,
        &(fx_radial_gradient_desc){
            .center = { 0.0f, 0.0f },
            .radius = 1.0f,
            .colors = { 0xFF000000u, 0xFF444444u, 0xFF888888u, 0xFFFFFFFFu },
            .stops = { 0.0f, 0.33f, 0.67f, 1.0f },
            .stop_count = 4,
        });
    CHECK(g != nullptr);
    CHECK(g->stop_count == 4);
    fx_gradient_destroy(g);
    return true;
}

/* ---------- Gradient color conversion ---------- */

static bool test_gradient_colors_converted(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 0.0f, 0.0f },
            .end = { 1.0f, 0.0f },
            .colors = { 0xFFFF0000u, 0xFF00FF00u },
            .stops = { 0.0f, 1.0f },
            .stop_count = 2,
        });
    CHECK(g != nullptr);
    /* Red should be (1, 0, 0, 1) in float */
    CHECK(approx_eq(g->colors[0][0], 1.0f));
    CHECK(approx_eq(g->colors[0][1], 0.0f));
    CHECK(approx_eq(g->colors[0][2], 0.0f));
    CHECK(approx_eq(g->colors[0][3], 1.0f));
    /* Green should be (0, 1, 0, 1) */
    CHECK(approx_eq(g->colors[1][0], 0.0f));
    CHECK(approx_eq(g->colors[1][1], 1.0f));
    CHECK(approx_eq(g->colors[1][2], 0.0f));
    CHECK(approx_eq(g->colors[1][3], 1.0f));
    fx_gradient_destroy(g);
    return true;
}

static bool test_gradient_premultiplied_colors(void)
{
    fx_context fake_ctx = { 0 };
    /* 50% red */
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 0.0f, 0.0f },
            .end = { 1.0f, 0.0f },
            .colors = { 0x80FF0000u, 0x8000FF00u },
            .stops = { 0.0f, 1.0f },
            .stop_count = 2,
        });
    CHECK(g != nullptr);
    /* Alpha should be ~0.5 */
    CHECK(fabsf(g->colors[0][3] - 0.50196f) < 0.01f);
    fx_gradient_destroy(g);
    return true;
}

/* ---------- Gradient null safety ---------- */

static bool test_gradient_null_context(void)
{
    CHECK(fx_gradient_create_linear(nullptr, &(fx_linear_gradient_desc){ 0 })
          == nullptr);
    CHECK(fx_gradient_create_radial(nullptr, &(fx_radial_gradient_desc){ 0 })
          == nullptr);
    return true;
}

static bool test_gradient_null_desc(void)
{
    fx_context fake_ctx = { 0 };
    CHECK(fx_gradient_create_linear(&fake_ctx, nullptr)
          == nullptr);
    CHECK(fx_gradient_create_radial(&fake_ctx, nullptr)
          == nullptr);
    return true;
}

static bool test_gradient_destroy_null(void)
{
    fx_gradient_destroy(nullptr);
    return true;
}

/* ---------- Gradient stops ordering ---------- */

static bool test_gradient_stops_monotonic(void)
{
    fx_context fake_ctx = { 0 };
    /* Non-monotonic stops should still be accepted (library does not validate) */
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 0.0f, 0.0f },
            .end = { 1.0f, 0.0f },
            .colors = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu },
            .stops = { 0.0f, 0.5f, 1.0f },
            .stop_count = 3,
        });
    CHECK(g != nullptr);
    fx_gradient_destroy(g);
    return true;
}

static bool test_gradient_stops_out_of_range(void)
{
    fx_context fake_ctx = { 0 };
    /* Stops outside [0,1] should still be accepted */
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 0.0f, 0.0f },
            .end = { 1.0f, 0.0f },
            .colors = { 0xFFFF0000u, 0xFF00FF00u },
            .stops = { -0.5f, 1.5f },
            .stop_count = 2,
        });
    CHECK(g != nullptr);
    fx_gradient_destroy(g);
    return true;
}

/* ---------- Gradient with zero vector ---------- */

static bool test_gradient_linear_zero_vector(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_linear(
        &fake_ctx,
        &(fx_linear_gradient_desc){
            .start = { 5.0f, 5.0f },
            .end = { 5.0f, 5.0f }, /* zero-length direction */
            .colors = { 0xFFFF0000u, 0xFF0000FFu },
            .stops = { 0.0f, 1.0f },
            .stop_count = 2,
        });
    CHECK(g != nullptr);
    fx_gradient_destroy(g);
    return true;
}

static bool test_gradient_radial_zero_radius(void)
{
    fx_context fake_ctx = { 0 };
    fx_gradient *g = fx_gradient_create_radial(
        &fake_ctx,
        &(fx_radial_gradient_desc){
            .center = { 50.0f, 50.0f },
            .radius = 0.0f,
            .colors = { 0xFFFF0000u, 0xFF0000FFu },
            .stops = { 0.0f, 1.0f },
            .stop_count = 2,
        });
    CHECK(g != nullptr);
    CHECK(approx_eq(g->end[0], 0.0f));
    fx_gradient_destroy(g);
    return true;
}

int main(void)
{
    if (!test_gradient_linear_two_stops()) return 1;
    if (!test_gradient_linear_four_stops()) return 1;
    if (!test_gradient_radial_two_stops()) return 1;
    if (!test_gradient_min_stops()) return 1;
    if (!test_gradient_max_stops()) return 1;
    if (!test_gradient_exactly_two_stops()) return 1;
    if (!test_gradient_exactly_four_stops()) return 1;
    if (!test_gradient_colors_converted()) return 1;
    if (!test_gradient_premultiplied_colors()) return 1;
    if (!test_gradient_null_context()) return 1;
    if (!test_gradient_null_desc()) return 1;
    if (!test_gradient_destroy_null()) return 1;
    if (!test_gradient_stops_monotonic()) return 1;
    if (!test_gradient_stops_out_of_range()) return 1;
    if (!test_gradient_linear_zero_vector()) return 1;
    if (!test_gradient_radial_zero_radius()) return 1;
    printf("gradient_stops OK\n");
    return 0;
}
