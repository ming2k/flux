#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    flux_matrix m;

    /* identity / is_identity */
    flux_matrix_identity(&m);
    CHECK(flux_matrix_is_identity(&m));
    CHECK(approx_eq(m.m[0], 1.0f) && approx_eq(m.m[3], 1.0f));
    CHECK(approx_eq(m.m[1], 0.0f) && approx_eq(m.m[2], 0.0f));
    CHECK(approx_eq(m.m[4], 0.0f) && approx_eq(m.m[5], 0.0f));

    /* translation */
    flux_matrix t;
    flux_matrix_translation(&t, 10.0f, -5.0f);
    float x = 1.0f, y = 2.0f;
    flux_matrix_transform_point(&t, &x, &y);
    CHECK(approx_eq(x, 11.0f) && approx_eq(y, -3.0f));

    /* scaling */
    flux_matrix s;
    flux_matrix_scaling(&s, 2.0f, 3.0f);
    x = 4.0f; y = 5.0f;
    flux_matrix_transform_point(&s, &x, &y);
    CHECK(approx_eq(x, 8.0f) && approx_eq(y, 15.0f));

    /* rotation by pi/2: (1, 0) -> (0, 1) */
    flux_matrix r;
    flux_matrix_rotation(&r, 1.5707963f);
    x = 1.0f; y = 0.0f;
    flux_matrix_transform_point(&r, &x, &y);
    CHECK(fabsf(x) < 1e-3f);
    CHECK(approx_eq(y, 1.0f));

    /* multiply: T * S applied to point should scale then translate */
    flux_matrix ts;
    flux_matrix_multiply(&ts, &t, &s);
    x = 4.0f; y = 5.0f;
    flux_matrix_transform_point(&ts, &x, &y);
    CHECK(approx_eq(x, 8.0f + 10.0f));
    CHECK(approx_eq(y, 15.0f - 5.0f));

    /* invert */
    flux_matrix inv;
    CHECK(flux_matrix_invert(&t, &inv));
    x = 11.0f; y = -3.0f;
    flux_matrix_transform_point(&inv, &x, &y);
    CHECK(approx_eq(x, 1.0f) && approx_eq(y, 2.0f));

    /* singular matrix invert returns false */
    flux_matrix sing = { .m = { 0, 0, 0, 0, 0, 0 } };
    CHECK(!flux_matrix_invert(&sing, &inv));

    /* transform_rect bounds an axis-aligned rect under rotation */
    flux_rect rect_in  = { 0, 0, 10, 10 };
    flux_rect rect_out;
    flux_matrix_rotation(&r, 0.7853981f);  /* 45 deg */
    flux_matrix_transform_rect(&r, &rect_in, &rect_out);
    CHECK(rect_out.w > 14.0f && rect_out.w < 14.2f);
    CHECK(rect_out.h > 14.0f && rect_out.h < 14.2f);

    printf("matrix OK\n");
    return 0;
}
