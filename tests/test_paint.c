#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    flux_paint *p = NULL;
    CHECK(flux_paint_create(ctx, &p) == FLUX_OK);

    /* Defaults */
    CHECK(flux_paint_get_color(p)        == 0xFF000000u);
    CHECK(flux_paint_get_stroke_width(p) == 1.0f);
    CHECK(flux_paint_get_miter_limit(p)  == 4.0f);
    CHECK(flux_paint_get_line_cap(p)     == FLUX_CAP_BUTT);
    CHECK(flux_paint_get_line_join(p)    == FLUX_JOIN_MITER);
    CHECK(flux_paint_get_fill_rule(p)    == FLUX_FILL_EVEN_ODD);
    CHECK(flux_paint_get_blend_mode(p)   == FLUX_BLEND_SRC_OVER);
    CHECK(flux_paint_get_gradient(p)     == NULL);
    CHECK(flux_paint_get_dash_count(p)   == 0u);

    /* Setters round-trip via getters */
    flux_paint_set_color       (p, 0xFFAABBCCu);
    flux_paint_set_stroke_width(p, 3.5f);
    flux_paint_set_miter_limit (p, 8.0f);
    flux_paint_set_line_cap    (p, FLUX_CAP_ROUND);
    flux_paint_set_line_join   (p, FLUX_JOIN_BEVEL);
    flux_paint_set_fill_rule   (p, FLUX_FILL_NON_ZERO);
    flux_paint_set_blend_mode  (p, FLUX_BLEND_MULTIPLY);

    CHECK(flux_paint_get_color(p)        == 0xFFAABBCCu);
    CHECK(approx_eq(flux_paint_get_stroke_width(p), 3.5f));
    CHECK(approx_eq(flux_paint_get_miter_limit(p),  8.0f));
    CHECK(flux_paint_get_line_cap(p)     == FLUX_CAP_ROUND);
    CHECK(flux_paint_get_line_join(p)    == FLUX_JOIN_BEVEL);
    CHECK(flux_paint_get_fill_rule(p)    == FLUX_FILL_NON_ZERO);
    CHECK(flux_paint_get_blend_mode(p)   == FLUX_BLEND_MULTIPLY);

    /* Negative width / sub-1 miter rejected */
    CHECK(flux_paint_set_stroke_width(p, -1.0f) == FLUX_ERROR_INVALID_ARGUMENT);
    CHECK(flux_paint_set_miter_limit (p,  0.5f) == FLUX_ERROR_INVALID_ARGUMENT);

    /* Dash round-trip */
    float pattern[3] = { 5.0f, 2.0f, 8.0f };
    CHECK(flux_paint_set_dash(p, pattern, 3, 1.5f) == FLUX_OK);
    CHECK(flux_paint_get_dash_count(p) == 3u);
    CHECK(approx_eq(flux_paint_get_dash_phase(p), 1.5f));

    float copy[4] = { 0 };
    CHECK(flux_paint_copy_dash(p, copy, 4) == 3u);
    CHECK(approx_eq(copy[0], 5.0f) && approx_eq(copy[1], 2.0f) && approx_eq(copy[2], 8.0f));

    /* Clearing dash via NULL+0 */
    CHECK(flux_paint_set_dash(p, NULL, 0, 0.0f) == FLUX_OK);
    CHECK(flux_paint_get_dash_count(p) == 0u);

    /* NULL safety on getters returns sentinel defaults */
    CHECK(flux_paint_get_color(NULL)      == 0u);
    CHECK(flux_paint_get_line_cap(NULL)   == FLUX_CAP_BUTT);

    flux_paint_release(p);
    flux_context_release(ctx);
    printf("paint OK\n");
    return 0;
}
