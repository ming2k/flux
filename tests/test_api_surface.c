/* Verify the full v0.3 public API symbol set is linkable.
 *
 * If the library forgets to export anything, the linker fails here
 * before any test executes. New symbols added to <flux/flux.h> should
 * be appended below. */
#include <flux/flux.h>
#include <flux/flux_vulkan.h>
#include <stdio.h>

int main(void)
{
    /* version & error & capabilities */
    (void)&flux_version;
    (void)&flux_version_number;
    (void)&flux_version_check;
    (void)&flux_result_string;
    (void)&flux_get_capabilities;

    /* context */
    (void)&flux_context_create;
    (void)&flux_context_retain;
    (void)&flux_context_release;
    (void)&flux_context_get_log_level;
    (void)&flux_context_get_allocator;

    /* math: matrix */
    (void)&flux_matrix_rotation;
    (void)&flux_matrix_multiply;
    (void)&flux_matrix_invert;
    (void)&flux_matrix_is_identity;
    (void)&flux_matrix_transform_point;
    (void)&flux_matrix_transform_rect;

    /* math: color */
    (void)&flux_color_unpack;

    /* surface */
    (void)&flux_surface_create_offscreen;
    (void)&flux_surface_create_vulkan;
    (void)&flux_surface_retain;
    (void)&flux_surface_release;
    (void)&flux_surface_resize;
    (void)&flux_surface_get_size;
    (void)&flux_surface_get_format;
    (void)&flux_surface_set_dpr;
    (void)&flux_surface_get_dpr;
    (void)&flux_surface_acquire;
    (void)&flux_surface_present;
    (void)&flux_surface_read_pixels;

    /* canvas */
    (void)&flux_canvas_clear;
    (void)&flux_canvas_op_count;
    (void)&flux_canvas_save;
    (void)&flux_canvas_restore;
    (void)&flux_canvas_translate;
    (void)&flux_canvas_scale;
    (void)&flux_canvas_rotate;
    (void)&flux_canvas_concat;
    (void)&flux_canvas_set_matrix;
    (void)&flux_canvas_get_matrix;
    (void)&flux_canvas_clip_rect;
    (void)&flux_canvas_clip_path;
    (void)&flux_canvas_reset_clip;
    (void)&flux_canvas_fill_rect;
    (void)&flux_canvas_fill_path;
    (void)&flux_canvas_stroke_path;
    (void)&flux_canvas_draw_image;
    (void)&flux_canvas_draw_glyph_run;
    (void)&flux_canvas_apply_blur;

    /* paint setters + getters */
    (void)&flux_paint_create;
    (void)&flux_paint_retain;
    (void)&flux_paint_release;
    (void)&flux_paint_set_color;
    (void)&flux_paint_set_stroke_width;
    (void)&flux_paint_set_miter_limit;
    (void)&flux_paint_set_line_cap;
    (void)&flux_paint_set_line_join;
    (void)&flux_paint_set_fill_rule;
    (void)&flux_paint_set_blend_mode;
    (void)&flux_paint_set_gradient;
    (void)&flux_paint_set_dash;
    (void)&flux_paint_get_color;
    (void)&flux_paint_get_stroke_width;
    (void)&flux_paint_get_miter_limit;
    (void)&flux_paint_get_line_cap;
    (void)&flux_paint_get_line_join;
    (void)&flux_paint_get_fill_rule;
    (void)&flux_paint_get_blend_mode;
    (void)&flux_paint_get_gradient;
    (void)&flux_paint_get_dash_count;
    (void)&flux_paint_get_dash_phase;
    (void)&flux_paint_copy_dash;

    /* path */
    (void)&flux_path_create;
    (void)&flux_path_retain;
    (void)&flux_path_release;
    (void)&flux_path_clear;
    (void)&flux_path_move_to;
    (void)&flux_path_line_to;
    (void)&flux_path_quad_to;
    (void)&flux_path_cubic_to;
    (void)&flux_path_close;
    (void)&flux_path_add_rect;
    (void)&flux_path_add_round_rect;
    (void)&flux_path_add_circle;
    (void)&flux_path_add_ellipse;
    (void)&flux_path_arc_to;
    (void)&flux_path_get_bounds;
    (void)&flux_path_verb_count;
    (void)&flux_path_point_count;
    (void)&flux_path_transform;

    /* gradient */
    (void)&flux_gradient_create_linear;
    (void)&flux_gradient_create_radial;
    (void)&flux_gradient_retain;
    (void)&flux_gradient_release;

    /* image */
    (void)&flux_image_create;
    (void)&flux_image_create_from_surface;
    (void)&flux_image_retain;
    (void)&flux_image_release;
    (void)&flux_image_update;
    (void)&flux_image_update_region;
    (void)&flux_image_get_size;
    (void)&flux_image_get_format;
    (void)&flux_image_data;

    /* glyph_run */
    (void)&flux_glyph_upload;
    (void)&flux_glyph_run_create;
    (void)&flux_glyph_run_retain;
    (void)&flux_glyph_run_release;
    (void)&flux_glyph_run_clear;
    (void)&flux_glyph_run_append;
    (void)&flux_glyph_run_count;
    (void)&flux_glyph_run_data;

    /* vulkan interop */
    (void)&flux_vulkan_device_create;
    (void)&flux_vulkan_device_destroy;

    printf("api_surface OK\n");
    return 0;
}
