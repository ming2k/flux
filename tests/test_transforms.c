#include "vgfx/vgfx.h"
#include "../src/internal.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>

#define EPSILON 1e-6f

static bool approx(float a, float b) {
    return fabsf(a - b) < EPSILON;
}

int main(void)
{
    printf("Running matrix math tests...\n");

    vg_matrix m1, m2, res;
    vg_matrix_identity(&m1);
    assert(vg_matrix_is_identity(&m1));

    /* Test translation */
    vg_matrix_identity(&m1);
    m1.m[4] = 10.0f;
    m1.m[5] = 20.0f;
    float x = 5.0f, y = 5.0f;
    vg_matrix_transform_point(&m1, &x, &y);
    assert(approx(x, 15.0f));
    assert(approx(y, 25.0f));

    /* Test scale */
    vg_matrix_identity(&m1);
    m1.m[0] = 2.0f;
    m1.m[3] = 3.0f;
    x = 1.0f; y = 1.0f;
    vg_matrix_transform_point(&m1, &x, &y);
    assert(approx(x, 2.0f));
    assert(approx(y, 3.0f));

    /* Test multiplication (concat) */
    /* T(10, 20) * S(2, 2) */
    vg_matrix_identity(&m1);
    m1.m[4] = 10.0f; m1.m[5] = 20.0f;
    vg_matrix_identity(&m2);
    m2.m[0] = 2.0f; m2.m[3] = 2.0f;
    
    vg_matrix_multiply(&res, &m1, &m2);
    /* Point (1, 1) -> S(2, 2) -> (2, 2) -> T(10, 20) -> (12, 22) */
    x = 1.0f; y = 1.0f;
    vg_matrix_transform_point(&res, &x, &y);
    assert(approx(x, 12.0f));
    assert(approx(y, 22.0f));

    printf("Running path transform tests...\n");
    vg_path *p = vg_path_create();
    vg_path_add_rect(p, &(vg_rect){ 0, 0, 10, 10 });
    
    vg_matrix_identity(&m1);
    m1.m[4] = 5.0f; m1.m[5] = 5.0f;
    
    vg_path *tp = vg_path_transform(p, &m1);
    assert(tp);
    vg_rect bounds;
    vg_path_get_bounds(tp, &bounds);
    assert(approx(bounds.x, 5.0f));
    assert(approx(bounds.y, 5.0f));
    assert(approx(bounds.w, 10.0f));
    assert(approx(bounds.h, 10.0f));

    vg_path_destroy(p);
    vg_path_destroy(tp);

    printf("All transformation tests passed.\n");
    return 0;
}
