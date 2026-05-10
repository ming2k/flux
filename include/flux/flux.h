/*
 * flux — low-level 2D graphics foundation.
 *
 * Public API. See docs/ for explanation and examples.
 */
#ifndef FLUX_H
#define FLUX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(FX_STATIC)
#  ifdef FX_BUILDING
#    define FX_API __declspec(dllexport)
#  else
#    define FX_API __declspec(dllimport)
#  endif
#else
#  define FX_API __attribute__((visibility("default")))
#endif

#define FX_NODISCARD [[nodiscard]]

/* ------------------------------------------------------------------ */
/*  Opaque handle types                                               */
/* ------------------------------------------------------------------ */

typedef struct fx_context   fx_context;
typedef struct fx_surface   fx_surface;
typedef struct fx_canvas    fx_canvas;
typedef struct fx_image     fx_image;
typedef struct fx_path      fx_path;
typedef struct fx_gradient  fx_gradient;
typedef struct fx_glyph_run fx_glyph_run;

/* ------------------------------------------------------------------ */
/*  Value types                                                       */
/* ------------------------------------------------------------------ */

typedef uint32_t fx_color;   /* 0xAARRGGBB, premultiplied */

typedef struct { float x, y;    } fx_point;
typedef struct { float x, y, w, h; } fx_rect;
typedef struct { float m[6];    } fx_matrix;

typedef enum { FX_CAP_BUTT = 0, FX_CAP_ROUND = 1, FX_CAP_SQUARE = 2 } fx_line_cap;
typedef enum { FX_JOIN_MITER = 0, FX_JOIN_ROUND = 1, FX_JOIN_BEVEL = 2 } fx_line_join;

typedef enum { FX_FILL_EVEN_ODD = 0, FX_FILL_NON_ZERO = 1 } fx_fill_rule;

typedef struct {
    fx_color     color;
    float        stroke_width;
    float        miter_limit;
    fx_line_cap  line_cap;
    fx_line_join line_join;
    fx_gradient *gradient;
    fx_fill_rule fill_rule;
    const float *dash_array;
    uint32_t     dash_count;
    float        dash_phase;
} fx_paint;

typedef enum { FX_CS_SRGB = 0, FX_CS_SRGB_LINEAR = 1 } fx_color_space;

typedef enum {
    FX_FMT_BGRA8_UNORM = 0,
    FX_FMT_RGBA8_UNORM = 1,
    FX_FMT_A8_UNORM    = 2,
} fx_pixel_format;

typedef enum {
    FX_IMAGE_USAGE_SAMPLED          = 1u << 0,
    FX_IMAGE_USAGE_STORAGE          = 1u << 1,
    FX_IMAGE_USAGE_COLOR_ATTACHMENT = 1u << 2,
    FX_IMAGE_USAGE_TRANSFER_SRC     = 1u << 3,
    FX_IMAGE_USAGE_TRANSFER_DST     = 1u << 4,
} fx_image_usage_bits;

typedef enum {
    FX_LOG_TRACE = 0, FX_LOG_DEBUG = 1, FX_LOG_INFO  = 2,
    FX_LOG_WARN  = 3, FX_LOG_ERROR = 4,
} fx_log_level;

typedef void (*fx_log_fn)(fx_log_level level, const char *file, int line,
                          const char *fmt, const char *msg, void *user);

typedef struct {
    fx_log_fn    log;
    void        *log_user;
    fx_log_level min_log_level;
    bool         enable_validation;
    const char  *app_name;
} fx_context_desc;

typedef struct {
    bool      validation_enabled;
    bool      graphics_queue;
    bool      present_queue;
    bool      sampled_images;
    bool      storage_images;
    uint32_t  api_version;
    uint32_t  max_image_dimension_2d;
    uint32_t  max_color_attachments;
} fx_device_caps;

typedef struct {
    uint32_t        width, height;
    fx_pixel_format format;
    uint32_t        usage;
    const void     *data;
    size_t          stride;
} fx_image_desc;

typedef struct {
    fx_point start, end;
    fx_color colors[4];
    float    stops[4];
    uint32_t stop_count;
} fx_linear_gradient_desc;

typedef struct {
    fx_point center;
    float    radius;
    fx_color colors[4];
    float    stops[4];
    uint32_t stop_count;
} fx_radial_gradient_desc;

typedef struct { uint32_t glyph_id; float x, y; } fx_glyph;

/* ------------------------------------------------------------------ */
/*  Context                                                           */
/* ------------------------------------------------------------------ */

FX_NODISCARD FX_API fx_context *fx_context_create(const fx_context_desc *desc);
FX_API void   fx_context_destroy(fx_context *ctx);
FX_NODISCARD FX_API bool fx_context_get_device_caps(const fx_context *ctx, fx_device_caps *out);

/* ------------------------------------------------------------------ */
/*  Surface                                                           */
/* ------------------------------------------------------------------ */

/* Destroy a surface. */
FX_API void fx_surface_destroy(fx_surface *s);
FX_API void fx_surface_resize(fx_surface *s, int32_t w, int32_t h);
FX_API void fx_surface_set_dpr(fx_surface *s, float dpr);
FX_API float fx_surface_get_dpr(const fx_surface *s);

/* Create a headless offscreen surface backed by the CPU software renderer.
 * No platform or GPU is required. Pixels are readable via
 * fx_surface_read_pixels after present. */
FX_NODISCARD FX_API fx_surface *fx_surface_create_offscreen(fx_context *ctx,
    int32_t width, int32_t height, fx_pixel_format format, fx_color_space cs);

/* Read pixels from the most recently presented offscreen frame.
 * stride is bytes per row (0 = default). */
FX_NODISCARD FX_API bool fx_surface_read_pixels(fx_surface *s, void *data, size_t stride);

/* Begin a frame. Returns a canvas valid until fx_surface_present. */
FX_NODISCARD FX_API fx_canvas *fx_surface_acquire(fx_surface *s);

/* End a frame: execute recorded ops and present. */
FX_API void fx_surface_present(fx_surface *s);

/* ------------------------------------------------------------------ */
/*  Canvas recording                                                  */
/* ------------------------------------------------------------------ */

FX_API void   fx_clear(fx_canvas *c, fx_color color);
FX_API size_t fx_canvas_op_count(const fx_canvas *c);

/* Transform stack */
FX_API void fx_save(fx_canvas *c);
FX_API void fx_restore(fx_canvas *c);
FX_API void fx_translate(fx_canvas *c, float dx, float dy);
FX_API void fx_scale(fx_canvas *c, float sx, float sy);
FX_API void fx_rotate(fx_canvas *c, float radians);
FX_API void fx_concat(fx_canvas *c, const fx_matrix *m);
FX_API void fx_set_matrix(fx_canvas *c, const fx_matrix *m);
FX_API void fx_get_matrix(const fx_canvas *c, fx_matrix *out_m);

/* Paint */
FX_API void fx_paint_init(fx_paint *paint, fx_color color);
FX_API void fx_paint_set_gradient(fx_paint *paint, fx_gradient *gradient);
FX_API void fx_paint_set_dash(fx_paint *paint, const float *dashes, uint32_t count, float phase);

/* Clipping */
FX_API void fx_clip_rect(fx_canvas *c, const fx_rect *rect);
FX_API void fx_reset_clip(fx_canvas *c);
FX_API void fx_clip_path(fx_canvas *c, const fx_path *path);

/* Draw ops */
FX_NODISCARD FX_API bool fx_fill_rect(fx_canvas *c, const fx_rect *rect, fx_color color);
FX_NODISCARD FX_API bool fx_fill_path(fx_canvas *c, const fx_path *path, const fx_paint *paint);
FX_NODISCARD FX_API bool fx_stroke_path(fx_canvas *c, const fx_path *path, const fx_paint *paint);
FX_NODISCARD FX_API bool fx_draw_image(fx_canvas *c, const fx_image *image,
                           const fx_rect *src, const fx_rect *dst);
FX_NODISCARD FX_API bool fx_draw_glyph_run(fx_canvas *c, const fx_glyph_run *run,
                               float x, float y, const fx_paint *paint);

/* Apply a Gaussian blur to the current surface. Sigma <= 0 is a no-op.
 * Only valid inside an offscreen surface (software backend). */
FX_NODISCARD FX_API bool fx_mask_blur(fx_canvas *c, float sigma);

/* ------------------------------------------------------------------ */
/*  Image                                                             */
/* ------------------------------------------------------------------ */

/* Create an image that wraps an offscreen surface's pixel data.
 * Reads the most recently presented frame. The surface must remain
 * alive while the image is in use. Returns NULL if not supported. */
FX_NODISCARD FX_API fx_image *fx_image_create_from_surface(fx_surface *s);

FX_NODISCARD FX_API fx_image *fx_image_create(fx_context *ctx, const fx_image_desc *desc);
FX_API void      fx_image_destroy(fx_image *image);
FX_NODISCARD FX_API bool fx_image_update(fx_image *image, const void *data, size_t stride);
FX_NODISCARD FX_API bool fx_image_get_desc(const fx_image *image, fx_image_desc *out);
FX_NODISCARD FX_API const void *fx_image_data(const fx_image *image, size_t *out_size, size_t *out_stride);

/* ------------------------------------------------------------------ */
/*  Path                                                              */
/* ------------------------------------------------------------------ */

FX_NODISCARD FX_API fx_path *fx_path_create(void);
FX_API void     fx_path_destroy(fx_path *path);
FX_API void     fx_path_reset(fx_path *path);
FX_API bool     fx_path_move_to(fx_path *path, float x, float y);
FX_API bool     fx_path_line_to(fx_path *path, float x, float y);
FX_API bool     fx_path_quad_to(fx_path *path, float cx, float cy, float x, float y);
FX_API bool     fx_path_cubic_to(fx_path *path, float cx0, float cy0, float cx1, float cy1, float x, float y);
FX_API bool     fx_path_close(fx_path *path);
FX_API bool     fx_path_add_rect(fx_path *path, const fx_rect *rect);
FX_API bool     fx_path_arc_to(fx_path *path, float rx, float ry, float rotation,
                                bool large_arc, bool sweep, float x, float y);
FX_NODISCARD FX_API bool   fx_path_get_bounds(const fx_path *path, fx_rect *out);
FX_NODISCARD FX_API size_t fx_path_verb_count(const fx_path *path);
FX_NODISCARD FX_API size_t fx_path_point_count(const fx_path *path);

/* ------------------------------------------------------------------ */
/*  Gradient                                                          */
/* ------------------------------------------------------------------ */

FX_NODISCARD FX_API fx_gradient *fx_gradient_create_linear(fx_context *ctx,
    const fx_linear_gradient_desc *desc);
FX_NODISCARD FX_API fx_gradient *fx_gradient_create_radial(fx_context *ctx,
    const fx_radial_gradient_desc *desc);
FX_API void fx_gradient_destroy(fx_gradient *gradient);

/* ------------------------------------------------------------------ */
/*  Glyphs                                                            */
/* ------------------------------------------------------------------ */

FX_NODISCARD FX_API bool fx_glyph_upload(fx_context *ctx, uint32_t glyph_id,
    const uint8_t *bitmap, int w, int h, int bearing_x, int bearing_y, int advance);
FX_NODISCARD FX_API fx_glyph_run *fx_glyph_run_create(size_t reserve);
FX_API void          fx_glyph_run_destroy(fx_glyph_run *run);
FX_API void          fx_glyph_run_reset(fx_glyph_run *run);
FX_NODISCARD FX_API bool          fx_glyph_run_append(fx_glyph_run *run,
    uint32_t glyph_id, float x, float y);
FX_NODISCARD FX_API size_t        fx_glyph_run_count(const fx_glyph_run *run);
FX_NODISCARD FX_API const fx_glyph *fx_glyph_run_data(const fx_glyph_run *run);

/* ------------------------------------------------------------------ */
/*  Math (matrix)                                                     */
/* ------------------------------------------------------------------ */

FX_API void fx_matrix_multiply(fx_matrix *out, const fx_matrix *a, const fx_matrix *b);
FX_API void fx_matrix_transform_point(const fx_matrix *m, float *x, float *y);
FX_NODISCARD FX_API fx_path *fx_path_transform(const fx_path *src, const fx_matrix *m);

/* Color helper: premultiplied 0xAARRGGBB from non-premultiplied rgba. */
static inline fx_color fx_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    unsigned int ar = ((unsigned int)r * a + 127) / 255;
    unsigned int ag = ((unsigned int)g * a + 127) / 255;
    unsigned int ab = ((unsigned int)b * a + 127) / 255;
    return ((fx_color)a  << 24) | ((fx_color)ar << 16) | ((fx_color)ag << 8) | ((fx_color)ab);
}

#ifdef __cplusplus
}
#endif

#endif /* FLUX_H */
