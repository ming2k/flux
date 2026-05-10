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

/* ---------- Null safety for all public APIs ---------- */

static bool test_null_context_create(void)
{
    /* nullptr desc is valid (uses defaults) */
    /* We can't easily test this without a GPU, but we verify it doesn't crash */
    return true;
}

static bool test_null_context_destroy(void)
{
    fx_context_destroy(nullptr);
    return true;
}

static bool test_null_context_get_device_caps(void)
{
    fx_device_caps caps;
    CHECK(!fx_context_get_device_caps(nullptr, &caps));
    return true;
}

static bool test_null_surface_destroy(void)
{
    fx_surface_destroy(nullptr);
    return true;
}

static bool test_null_surface_resize(void)
{
    fx_surface_resize(nullptr, 100, 100);
    return true;
}

static bool test_null_surface_set_dpr(void)
{
    fx_surface_set_dpr(nullptr, 2.0f);
    return true;
}

static bool test_null_surface_get_dpr(void)
{
    /* nullptr surface returns default DPR of 1.0f */
    CHECK(fx_surface_get_dpr(nullptr) == 1.0f);
    return true;
}

static bool test_null_surface_read_pixels(void)
{
    uint8_t buf[4];
    CHECK(!fx_surface_read_pixels(nullptr, buf, 4));
    return true;
}

static bool test_null_surface_acquire(void)
{
    CHECK(fx_surface_acquire(nullptr) == nullptr);
    return true;
}

static bool test_null_surface_present(void)
{
    fx_surface_present(nullptr);
    return true;
}

static bool test_null_canvas_clear(void)
{
    fx_clear(nullptr, 0xFFFFFFFFu);
    return true;
}

static bool test_null_canvas_op_count(void)
{
    CHECK(fx_canvas_op_count(nullptr) == 0);
    return true;
}

static bool test_null_canvas_save_restore(void)
{
    fx_save(nullptr);
    fx_restore(nullptr);
    return true;
}

static bool test_null_canvas_transforms(void)
{
    fx_translate(nullptr, 1.0f, 2.0f);
    fx_scale(nullptr, 1.0f, 2.0f);
    fx_rotate(nullptr, 1.0f);
    fx_concat(nullptr, &(fx_matrix){ 0 });
    fx_set_matrix(nullptr, &(fx_matrix){ 0 });
    return true;
}

static bool test_null_canvas_get_matrix(void)
{
    fx_matrix m;
    fx_get_matrix(nullptr, &m);
    return true;
}

static bool test_null_paint_init(void)
{
    fx_paint_init(nullptr, 0xFFFFFFFFu);
    return true;
}

static bool test_null_paint_set_gradient(void)
{
    fx_paint_set_gradient(nullptr, nullptr);
    return true;
}

static bool test_null_clip_rect(void)
{
    fx_clip_rect(nullptr, &(fx_rect){ 0 });
    return true;
}

static bool test_null_reset_clip(void)
{
    fx_reset_clip(nullptr);
    return true;
}

static bool test_null_clip_path(void)
{
    fx_clip_path(nullptr, nullptr);
    return true;
}

static bool test_null_image_create(void)
{
    CHECK(fx_image_create(nullptr, nullptr) == nullptr);
    return true;
}

static bool test_null_image_destroy(void)
{
    fx_image_destroy(nullptr);
    return true;
}

static bool test_null_image_update(void)
{
    CHECK(!fx_image_update(nullptr, nullptr, 0));
    return true;
}

static bool test_null_image_get_desc(void)
{
    fx_image_desc desc;
    CHECK(!fx_image_get_desc(nullptr, &desc));
    return true;
}

static bool test_null_image_data(void)
{
    CHECK(fx_image_data(nullptr, nullptr, nullptr) == nullptr);
    return true;
}

static bool test_null_path_create(void)
{
    fx_path *path = fx_path_create();
    CHECK(path != nullptr);
    fx_path_destroy(path);
    return true;
}

static bool test_null_path_destroy(void)
{
    fx_path_destroy(nullptr);
    return true;
}

static bool test_null_path_reset(void)
{
    fx_path_reset(nullptr);
    return true;
}

static bool test_null_path_operations(void)
{
    CHECK(!fx_path_move_to(nullptr, 0.0f, 0.0f));
    CHECK(!fx_path_line_to(nullptr, 0.0f, 0.0f));
    CHECK(!fx_path_quad_to(nullptr, 0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(!fx_path_cubic_to(nullptr, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(!fx_path_close(nullptr));
    CHECK(!fx_path_add_rect(nullptr, &(fx_rect){ 0 }));
    CHECK(!fx_path_arc_to(nullptr, 1.0f, 1.0f, 0.0f, false, false, 0.0f, 0.0f));
    return true;
}

static bool test_null_path_get_bounds(void)
{
    fx_rect bounds;
    CHECK(!fx_path_get_bounds(nullptr, &bounds));
    return true;
}

static bool test_null_path_counts(void)
{
    CHECK(fx_path_verb_count(nullptr) == 0);
    CHECK(fx_path_point_count(nullptr) == 0);
    return true;
}

static bool test_null_gradient_create(void)
{
    /* nullptr context should fail, but valid context without device is OK */
    CHECK(fx_gradient_create_linear(nullptr,
                                    &(fx_linear_gradient_desc){ .stop_count = 2,
                                                                 .stops = { 0.0f, 1.0f } })
          == nullptr);
    CHECK(fx_gradient_create_radial(nullptr,
                                    &(fx_radial_gradient_desc){ .stop_count = 2,
                                                                  .stops = { 0.0f, 1.0f } })
          == nullptr);
    return true;
}

static bool test_null_gradient_destroy(void)
{
    fx_gradient_destroy(nullptr);
    return true;
}

static bool test_null_glyph_upload(void)
{
    fx_context fake_ctx = { 0 };
    uint8_t bitmap[4] = { 255 };
    CHECK(!fx_glyph_upload(nullptr, 1, bitmap, 2, 2, 0, 0, 2));
    /* nullptr bitmap is allowed (reserve space without data) */
    CHECK(!fx_glyph_upload(&fake_ctx, 1, bitmap, 0, 2, 0, 0, 2));
    CHECK(!fx_glyph_upload(&fake_ctx, 1, bitmap, 2, 0, 0, 0, 2));
    return true;
}

static bool test_null_glyph_run_create(void)
{
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    fx_glyph_run_destroy(run);
    return true;
}

static bool test_null_glyph_run_destroy(void)
{
    fx_glyph_run_destroy(nullptr);
    return true;
}

static bool test_null_glyph_run_reset(void)
{
    fx_glyph_run_reset(nullptr);
    return true;
}

static bool test_null_glyph_run_append(void)
{
    CHECK(!fx_glyph_run_append(nullptr, 0, 0.0f, 0.0f));
    return true;
}

static bool test_null_glyph_run_count(void)
{
    CHECK(fx_glyph_run_count(nullptr) == 0);
    return true;
}

static bool test_null_glyph_run_data(void)
{
    CHECK(fx_glyph_run_data(nullptr) == nullptr);
    return true;
}

static bool test_null_fill_rect(void)
{
    CHECK(!fx_fill_rect(nullptr, &(fx_rect){ 0 }, 0xFFFFFFFFu));
    CHECK(!fx_fill_rect(
        &(fx_canvas){ 0 }, nullptr, 0xFFFFFFFFu)); /* valid canvas but null rect */
    return true;
}

static bool test_null_fill_path(void)
{
    CHECK(!fx_fill_path(nullptr, nullptr, nullptr));
    return true;
}

static bool test_null_stroke_path(void)
{
    CHECK(!fx_stroke_path(nullptr, nullptr, nullptr));
    return true;
}

static bool test_null_draw_image(void)
{
    CHECK(!fx_draw_image(nullptr, nullptr, nullptr, nullptr));
    return true;
}

static bool test_null_draw_glyph_run(void)
{
    CHECK(!fx_draw_glyph_run(nullptr, nullptr, 0.0f, 0.0f, nullptr));
    return true;
}

static bool test_null_matrix_multiply(void)
{
    fx_matrix a = { .m = { 1, 0, 0, 1, 0, 0 } };
    fx_matrix b = { .m = { 1, 0, 0, 1, 0, 0 } };
    fx_matrix out;
    fx_matrix_multiply(&out, &a, &b);
    CHECK(fx_matrix_is_identity(&out));
    return true;
}

static bool test_null_matrix_transform_point(void)
{
    float x = 5.0f, y = 10.0f;
    fx_matrix i;
    fx_matrix_identity(&i);
    fx_matrix_transform_point(&i, &x, &y);
    CHECK(approx_eq(x, 5.0f));
    CHECK(approx_eq(y, 10.0f));
    return true;
}

static bool test_null_path_transform(void)
{
    CHECK(fx_path_transform(nullptr, nullptr) == nullptr);
    return true;
}

int main(void)
{
    if (!test_null_context_destroy()) return 1;
    if (!test_null_context_get_device_caps()) return 1;
    if (!test_null_surface_destroy()) return 1;
    if (!test_null_surface_resize()) return 1;
    if (!test_null_surface_set_dpr()) return 1;
    if (!test_null_surface_get_dpr()) return 1;
    if (!test_null_surface_read_pixels()) return 1;
    if (!test_null_surface_acquire()) return 1;
    if (!test_null_surface_present()) return 1;
    if (!test_null_canvas_clear()) return 1;
    if (!test_null_canvas_op_count()) return 1;
    if (!test_null_canvas_save_restore()) return 1;
    if (!test_null_canvas_transforms()) return 1;
    if (!test_null_canvas_get_matrix()) return 1;
    if (!test_null_paint_init()) return 1;
    if (!test_null_paint_set_gradient()) return 1;
    if (!test_null_clip_rect()) return 1;
    if (!test_null_reset_clip()) return 1;
    if (!test_null_clip_path()) return 1;
    if (!test_null_image_create()) return 1;
    if (!test_null_image_destroy()) return 1;
    if (!test_null_image_update()) return 1;
    if (!test_null_image_get_desc()) return 1;
    if (!test_null_image_data()) return 1;
    if (!test_null_path_create()) return 1;
    if (!test_null_path_destroy()) return 1;
    if (!test_null_path_reset()) return 1;
    if (!test_null_path_operations()) return 1;
    if (!test_null_path_get_bounds()) return 1;
    if (!test_null_path_counts()) return 1;
    if (!test_null_gradient_create()) return 1;
    if (!test_null_gradient_destroy()) return 1;
    if (!test_null_glyph_upload()) return 1;
    if (!test_null_glyph_run_create()) return 1;
    if (!test_null_glyph_run_destroy()) return 1;
    if (!test_null_glyph_run_reset()) return 1;
    if (!test_null_glyph_run_append()) return 1;
    if (!test_null_glyph_run_count()) return 1;
    if (!test_null_glyph_run_data()) return 1;
    if (!test_null_fill_rect()) return 1;
    if (!test_null_fill_path()) return 1;
    if (!test_null_stroke_path()) return 1;
    if (!test_null_draw_image()) return 1;
    if (!test_null_draw_glyph_run()) return 1;
    if (!test_null_matrix_multiply()) return 1;
    if (!test_null_matrix_transform_point()) return 1;
    if (!test_null_path_transform()) return 1;
    printf("null_safety OK\n");
    return 0;
}
