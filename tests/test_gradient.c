#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    flux_color colors[3] = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu };
    float      stops [3] = { 0.0f, 0.5f, 1.0f };

    flux_linear_gradient_desc lg = {
        .size       = sizeof(lg),
        .start      = { 0, 0 },
        .end        = { 100, 0 },
        .colors     = colors,
        .stops      = stops,
        .stop_count = 3,
        .extend     = FLUX_EXTEND_PAD,
    };
    flux_gradient *g = NULL;
    CHECK(flux_gradient_create_linear(ctx, &lg, &g) == FLUX_OK);
    flux_gradient_release(g);

    /* Too few stops: rejected. */
    lg.stop_count = 1;
    CHECK(flux_gradient_create_linear(ctx, &lg, &g) == FLUX_ERROR_OUT_OF_RANGE);

    /* Non-monotonic stops: rejected. */
    float bad[3] = { 0.0f, 0.5f, 0.4f };
    lg.stop_count = 3;
    lg.stops      = bad;
    CHECK(flux_gradient_create_linear(ctx, &lg, &g) == FLUX_ERROR_INVALID_ARGUMENT);

    /* Stops out of [0,1]: rejected. */
    float oob[2] = { 0.0f, 1.5f };
    lg.stop_count = 2;
    lg.stops      = oob;
    CHECK(flux_gradient_create_linear(ctx, &lg, &g) == FLUX_ERROR_OUT_OF_RANGE);

    /* Radial: valid path. */
    flux_radial_gradient_desc rg = {
        .size       = sizeof(rg),
        .center     = { 50, 50 },
        .radius     = 25,
        .colors     = colors,
        .stops      = stops,
        .stop_count = 3,
        .extend     = FLUX_EXTEND_REPEAT,
    };
    g = NULL;
    CHECK(flux_gradient_create_radial(ctx, &rg, &g) == FLUX_OK);
    flux_gradient_release(g);

    rg.radius = 0.0f;
    CHECK(flux_gradient_create_radial(ctx, &rg, &g) == FLUX_ERROR_INVALID_ARGUMENT);

    flux_context_release(ctx);
    printf("gradient OK\n");
    return 0;
}
