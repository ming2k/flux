#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__,     \
                    __LINE__);                                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define EPS 1e-5f

static bool approx_eq(float a, float b) { return fabsf(a - b) < EPS; }

/* ---------- fx_matrix_identity ---------- */

static bool test_matrix_identity(void)
{
    fx_matrix m;
    fx_matrix_identity(&m);
    CHECK(approx_eq(m.m[0], 1.0f));
    CHECK(approx_eq(m.m[1], 0.0f));
    CHECK(approx_eq(m.m[2], 0.0f));
    CHECK(approx_eq(m.m[3], 1.0f));
    CHECK(approx_eq(m.m[4], 0.0f));
    CHECK(approx_eq(m.m[5], 0.0f));
    CHECK(fx_matrix_is_identity(&m));
    return true;
}

static bool test_matrix_identity_is_idempotent(void)
{
    fx_matrix m = { .m = { 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f } };
    fx_matrix_identity(&m);
    CHECK(fx_matrix_is_identity(&m));
    fx_matrix_identity(&m);
    CHECK(fx_matrix_is_identity(&m));
    return true;
}

/* ---------- fx_matrix_multiply ---------- */

static bool test_matrix_multiply_identity(void)
{
    fx_matrix a = { .m = { 1.0f, 0.0f, 0.0f, 1.0f, 5.0f, 10.0f } };
    fx_matrix i;
    fx_matrix_identity(&i);
    fx_matrix out;

    /* A * I == A */
    fx_matrix_multiply(&out, &a, &i);
    CHECK(approx_eq(out.m[0], a.m[0]));
    CHECK(approx_eq(out.m[1], a.m[1]));
    CHECK(approx_eq(out.m[2], a.m[2]));
    CHECK(approx_eq(out.m[3], a.m[3]));
    CHECK(approx_eq(out.m[4], a.m[4]));
    CHECK(approx_eq(out.m[5], a.m[5]));

    /* I * A == A */
    fx_matrix_multiply(&out, &i, &a);
    CHECK(approx_eq(out.m[0], a.m[0]));
    CHECK(approx_eq(out.m[1], a.m[1]));
    CHECK(approx_eq(out.m[2], a.m[2]));
    CHECK(approx_eq(out.m[3], a.m[3]));
    CHECK(approx_eq(out.m[4], a.m[4]));
    CHECK(approx_eq(out.m[5], a.m[5]));

    return true;
}

static bool test_matrix_multiply_translation(void)
{
    fx_matrix t1 = { .m = { 1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f } };
    fx_matrix t2 = { .m = { 1.0f, 0.0f, 0.0f, 1.0f, 5.0f,  -5.0f } };
    fx_matrix out;

    /* t1 * t2 => translate by (15, 15) */
    fx_matrix_multiply(&out, &t1, &t2);
    CHECK(approx_eq(out.m[4], 15.0f));
    CHECK(approx_eq(out.m[5], 15.0f));
    return true;
}

static bool test_matrix_multiply_scale(void)
{
    fx_matrix s = { .m = { 2.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f } };
    fx_matrix t = { .m = { 1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f } };
    fx_matrix out;

    /* scale * translate */
    fx_matrix_multiply(&out, &s, &t);
    CHECK(approx_eq(out.m[0], 2.0f));
    CHECK(approx_eq(out.m[3], 3.0f));
    CHECK(approx_eq(out.m[4], 20.0f));
    CHECK(approx_eq(out.m[5], 60.0f));
    return true;
}

static bool test_matrix_multiply_rotation(void)
{
    fx_matrix r90;
    float s = sinf(3.14159265f / 2.0f);
    float c = cosf(3.14159265f / 2.0f);
    r90.m[0] = c;
    r90.m[1] = s;
    r90.m[2] = -s;
    r90.m[3] = c;
    r90.m[4] = 0.0f;
    r90.m[5] = 0.0f;

    fx_matrix out;
    /* R(90) * R(90) = R(180) */
    fx_matrix_multiply(&out, &r90, &r90);
    CHECK(approx_eq(out.m[0], -1.0f));
    CHECK(approx_eq(out.m[1], 0.0f));
    CHECK(approx_eq(out.m[2], 0.0f));
    CHECK(approx_eq(out.m[3], -1.0f));
    return true;
}

static bool test_matrix_multiply_associativity(void)
{
    fx_matrix a = { .m = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f } };
    fx_matrix b = { .m = { 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f } };
    fx_matrix c = { .m = { 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f } };
    fx_matrix ab, bc, ab_c, a_bc;

    fx_matrix_multiply(&ab, &a, &b);
    fx_matrix_multiply(&bc, &b, &c);
    fx_matrix_multiply(&ab_c, &ab, &c);
    fx_matrix_multiply(&a_bc, &a, &bc);

    for (int i = 0; i < 6; ++i)
        CHECK(approx_eq(ab_c.m[i], a_bc.m[i]));
    return true;
}

/* ---------- fx_matrix_transform_point ---------- */

static bool test_transform_point_identity(void)
{
    fx_matrix i;
    fx_matrix_identity(&i);
    float x = 42.0f, y = -17.0f;
    fx_matrix_transform_point(&i, &x, &y);
    CHECK(approx_eq(x, 42.0f));
    CHECK(approx_eq(y, -17.0f));
    return true;
}

static bool test_transform_point_translation(void)
{
    fx_matrix t = { .m = { 1.0f, 0.0f, 0.0f, 1.0f, 10.0f, -5.0f } };
    float x = 1.0f, y = 2.0f;
    fx_matrix_transform_point(&t, &x, &y);
    CHECK(approx_eq(x, 11.0f));
    CHECK(approx_eq(y, -3.0f));
    return true;
}

static bool test_transform_point_scale(void)
{
    fx_matrix s = { .m = { 3.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f } };
    float x = 4.0f, y = 5.0f;
    fx_matrix_transform_point(&s, &x, &y);
    CHECK(approx_eq(x, 12.0f));
    CHECK(approx_eq(y, 10.0f));
    return true;
}

static bool test_transform_point_rotation_90(void)
{
    fx_matrix r90;
    float s = sinf(3.14159265f / 2.0f);
    float c = cosf(3.14159265f / 2.0f);
    r90.m[0] = c;
    r90.m[1] = s;
    r90.m[2] = -s;
    r90.m[3] = c;
    r90.m[4] = 0.0f;
    r90.m[5] = 0.0f;

    float x = 1.0f, y = 0.0f;
    fx_matrix_transform_point(&r90, &x, &y);
    CHECK(approx_eq(x, 0.0f));
    CHECK(approx_eq(y, 1.0f));
    return true;
}

static bool test_transform_point_zero(void)
{
    fx_matrix m = { .m = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f } };
    float x = 0.0f, y = 0.0f;
    fx_matrix_transform_point(&m, &x, &y);
    CHECK(approx_eq(x, 5.0f));
    CHECK(approx_eq(y, 6.0f));
    return true;
}

/* ---------- fx_matrix_is_identity ---------- */

static bool test_matrix_is_identity_false(void)
{
    fx_matrix m;
    fx_matrix_identity(&m);
    CHECK(fx_matrix_is_identity(&m));

    m.m[0] = 1.0001f;
    CHECK(!fx_matrix_is_identity(&m));

    fx_matrix_identity(&m);
    m.m[4] = 0.0001f;
    CHECK(!fx_matrix_is_identity(&m));
    return true;
}

/* ---------- fx_path_transform ---------- */

static bool test_path_transform_identity(void)
{
    fx_path *src = fx_path_create();
    CHECK(src != nullptr);
    CHECK(fx_path_move_to(src, 1.0f, 2.0f));
    CHECK(fx_path_line_to(src, 3.0f, 4.0f));

    fx_matrix i;
    fx_matrix_identity(&i);
    fx_path *dst = fx_path_transform(src, &i);
    CHECK(dst != nullptr);
    CHECK(dst != src);
    CHECK(fx_path_point_count(dst) == fx_path_point_count(src));

    fx_path_destroy(src);
    fx_path_destroy(dst);
    return true;
}

static bool test_path_transform_translation(void)
{
    fx_path *src = fx_path_create();
    CHECK(src != nullptr);
    CHECK(fx_path_move_to(src, 0.0f, 0.0f));
    CHECK(fx_path_line_to(src, 10.0f, 0.0f));
    CHECK(fx_path_line_to(src, 10.0f, 10.0f));

    fx_matrix t = { .m = { 1.0f, 0.0f, 0.0f, 1.0f, 5.0f, -3.0f } };
    fx_path *dst = fx_path_transform(src, &t);
    CHECK(dst != nullptr);

    fx_rect bounds;
    CHECK(fx_path_get_bounds(dst, &bounds));
    CHECK(approx_eq(bounds.x, 5.0f));
    CHECK(approx_eq(bounds.y, -3.0f));
    CHECK(approx_eq(bounds.w, 10.0f));
    CHECK(approx_eq(bounds.h, 10.0f));

    fx_path_destroy(src);
    fx_path_destroy(dst);
    return true;
}

static bool test_path_transform_scale(void)
{
    fx_path *src = fx_path_create();
    CHECK(src != nullptr);
    CHECK(fx_path_move_to(src, 0.0f, 0.0f));
    CHECK(fx_path_line_to(src, 10.0f, 0.0f));
    CHECK(fx_path_line_to(src, 10.0f, 10.0f));

    fx_matrix s = { .m = { 2.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f } };
    fx_path *dst = fx_path_transform(src, &s);
    CHECK(dst != nullptr);

    fx_rect bounds;
    CHECK(fx_path_get_bounds(dst, &bounds));
    CHECK(approx_eq(bounds.x, 0.0f));
    CHECK(approx_eq(bounds.y, 0.0f));
    CHECK(approx_eq(bounds.w, 20.0f));
    CHECK(approx_eq(bounds.h, 30.0f));

    fx_path_destroy(src);
    fx_path_destroy(dst);
    return true;
}

static bool test_path_transform_null(void)
{
    fx_matrix i;
    fx_matrix_identity(&i);
    CHECK(fx_path_transform(nullptr, &i) == nullptr);

    fx_path *src = fx_path_create();
    CHECK(src != nullptr);
    CHECK(fx_path_transform(src, nullptr) == nullptr);
    fx_path_destroy(src);
    return true;
}

static bool test_path_transform_empty(void)
{
    fx_path *src = fx_path_create();
    CHECK(src != nullptr);
    fx_matrix i;
    fx_matrix_identity(&i);
    fx_path *dst = fx_path_transform(src, &i);
    CHECK(dst != nullptr);
    CHECK(fx_path_point_count(dst) == 0);
    fx_path_destroy(src);
    fx_path_destroy(dst);
    return true;
}

int main(void)
{
    if (!test_matrix_identity()) return 1;
    if (!test_matrix_identity_is_idempotent()) return 1;
    if (!test_matrix_multiply_identity()) return 1;
    if (!test_matrix_multiply_translation()) return 1;
    if (!test_matrix_multiply_scale()) return 1;
    if (!test_matrix_multiply_rotation()) return 1;
    if (!test_matrix_multiply_associativity()) return 1;
    if (!test_transform_point_identity()) return 1;
    if (!test_transform_point_translation()) return 1;
    if (!test_transform_point_scale()) return 1;
    if (!test_transform_point_rotation_90()) return 1;
    if (!test_transform_point_zero()) return 1;
    if (!test_matrix_is_identity_false()) return 1;
    if (!test_path_transform_identity()) return 1;
    if (!test_path_transform_translation()) return 1;
    if (!test_path_transform_scale()) return 1;
    if (!test_path_transform_null()) return 1;
    if (!test_path_transform_empty()) return 1;
    printf("matrix OK\n");
    return 0;
}
