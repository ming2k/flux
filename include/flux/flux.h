/*
 * flux — low-level 2D graphics foundation on Vulkan.
 *
 * The public API intentionally stays close to the rendering substrate:
 * surfaces, images, paths, explicit glyph runs, and a frame-local
 * canvas recorder. Layout and shaping stay above flux; Vulkan objects
 * and queue management stay below it.
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

#include <vulkan/vulkan.h>
typedef struct fx_context  fx_context;

typedef struct fx_surface  fx_surface;
typedef struct fx_canvas   fx_canvas;
typedef struct fx_image    fx_image;
typedef struct fx_path     fx_path;
typedef struct fx_font     fx_font;
typedef struct fx_glyph_run fx_glyph_run;

typedef uint32_t fx_color;   /* 0xAARRGGBB, premultiplied */

typedef struct {
    float x;
    float y;
} fx_point;

typedef struct {
    float x;
    float y;
    float w;
    float h;
} fx_rect;

typedef struct {
    float m[6];  /* [a c e; b d f] -> x' = ax + cy + e, y' = bx + dy + f */
} fx_matrix;

typedef enum {
    FX_CAP_BUTT = 0,
    FX_CAP_ROUND = 1,
    FX_CAP_SQUARE = 2,
} fx_line_cap;

typedef enum {
    FX_JOIN_MITER = 0,
    FX_JOIN_ROUND = 1,
    FX_JOIN_BEVEL = 2,
} fx_line_join;

typedef struct {
    fx_color     color;
    float        stroke_width;
    float        miter_limit;
    fx_line_cap  line_cap;
    fx_line_join line_join;
} fx_paint;

typedef enum {
    FX_CS_SRGB = 0,
    FX_CS_SRGB_LINEAR = 1,
} fx_color_space;

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
    FX_LOG_ERROR = 0,
    FX_LOG_WARN  = 1,
    FX_LOG_INFO  = 2,
    FX_LOG_DEBUG = 3,
} fx_log_level;

typedef void (*fx_log_fn)(fx_log_level level, const char *msg, void *user);

typedef struct {
    fx_log_fn log;
    void     *log_user;
    bool      enable_validation;  /* honored only if compiled in */
    const char *app_name;
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
    uint32_t        width;
    uint32_t        height;
    fx_pixel_format format;
    uint32_t        usage;      /* fx_image_usage_bits */
    const void     *data;       /* optional initial pixels */
    size_t          stride;     /* bytes per row; 0 = tightly packed */
} fx_image_desc;

typedef struct {
    const char *family;       /* optional family/display name */
    const char *source_name;  /* optional file or source identifier */
    float       size;
    int32_t     weight;
    bool        italic;
} fx_font_desc;

typedef struct {
    uint32_t glyph_id;
    float    x;
    float    y;
} fx_glyph;

FX_API fx_context *fx_context_create(const fx_context_desc *desc);
FX_API void        fx_context_destroy(fx_context *ctx);
FX_API VkInstance  fx_context_get_instance(fx_context *ctx);
FX_API bool        fx_context_get_device_caps(const fx_context *ctx,
                                                fx_device_caps  *out_caps);

FX_API void fx_surface_destroy(fx_surface *s);
FX_API void fx_surface_resize(fx_surface *s, int32_t w, int32_t h);
FX_API fx_surface *fx_surface_create_vulkan(fx_context *ctx, VkSurfaceKHR vk_surface, int32_t width, int32_t height, fx_color_space cs);

/*
 * Create a headless offscreen surface for rendering to CPU-readable memory.
 * No Wayland or Vulkan surface (VkSurfaceKHR) is created.
 * After fx_surface_present, use fx_surface_read_pixels to read the result.
 */
FX_API fx_surface *fx_surface_create_offscreen(fx_context *ctx, int32_t width, int32_t height, fx_pixel_format format, fx_color_space cs);

/* Read pixels from the most recently presented offscreen frame.
 * `data` must point to at least height * stride bytes.
 * `stride` is bytes per row. Returns false if the surface is not offscreen
 * or if the read fails. */
FX_API bool fx_surface_read_pixels(fx_surface *s, void *data, size_t stride);

/*
 * Begin a frame. Returns a canvas valid until the matching
 * fx_surface_present. Do not keep the pointer across frames.
 */
FX_API fx_canvas *fx_surface_acquire(fx_surface *s);

/* End a frame: submit recorded commands and present. */
FX_API void       fx_surface_present(fx_surface *s);

/* Clear the entire canvas to a premultiplied color. */
FX_API void fx_clear(fx_canvas *c, fx_color color);
FX_API size_t fx_canvas_op_count(const fx_canvas *c);

/*
 * Canvas state and transformation. Transformations are applied to
 * paths and glyph positions immediately upon recording.
 */
FX_API void fx_save(fx_canvas *c);
FX_API void fx_restore(fx_canvas *c);

FX_API void fx_translate(fx_canvas *c, float dx, float dy);
FX_API void fx_scale(fx_canvas *c, float sx, float sy);
FX_API void fx_rotate(fx_canvas *c, float radians);
FX_API void fx_concat(fx_canvas *c, const fx_matrix *m);
FX_API void fx_set_matrix(fx_canvas *c, const fx_matrix *m);
FX_API void fx_get_matrix(const fx_canvas *c, fx_matrix *out_m);

FX_API void fx_paint_init(fx_paint *paint, fx_color color);

/*
 * Images are context-owned resource objects. In the current runtime
 * they retain a validated descriptor and an optional CPU-side pixel
 * copy; the Vulkan upload path is the next layer built on top of this
 * object.
 */
FX_API fx_image *fx_image_create(fx_context *ctx, const fx_image_desc *desc);
FX_API void      fx_image_destroy(fx_image *image);
FX_API bool      fx_image_update(fx_image *image, const void *data, size_t stride);
FX_API bool      fx_image_get_desc(const fx_image *image,
                                     fx_image_desc  *out_desc);
FX_API const void *fx_image_data(const fx_image *image,
                                   size_t         *out_size,
                                   size_t         *out_stride);

/*
 * Paths are explicit verb/point streams. No hidden tessellation or
 * stroke expansion happens at construction time.
 */
FX_API fx_path *fx_path_create(void);
FX_API void     fx_path_destroy(fx_path *path);
FX_API void     fx_path_reset(fx_path *path);
FX_API bool     fx_path_move_to(fx_path *path, float x, float y);
FX_API bool     fx_path_line_to(fx_path *path, float x, float y);
FX_API bool     fx_path_quad_to(fx_path *path,
                                  float cx, float cy,
                                  float x,  float y);
FX_API bool     fx_path_cubic_to(fx_path *path,
                                   float cx0, float cy0,
                                   float cx1, float cy1,
                                   float x,   float y);
FX_API bool     fx_path_close(fx_path *path);
FX_API bool     fx_path_add_rect(fx_path *path, const fx_rect *rect);
FX_API bool     fx_path_arc_to(fx_path *path,
                                float rx, float ry,
                                float x_axis_rotation,
                                bool large_arc, bool sweep,
                                float x, float y);
FX_API bool     fx_path_get_bounds(const fx_path *path, fx_rect *out_bounds);
FX_API size_t   fx_path_verb_count(const fx_path *path);
FX_API size_t   fx_path_point_count(const fx_path *path);

/*
 * Fonts are metadata handles. Text shaping and font fallback stay in
 * upstream components such as HarfBuzz/Pango; flux consumes positioned
 * glyph runs.
 */
FX_API fx_font *fx_font_create(fx_context *ctx, const fx_font_desc *desc);
FX_API void     fx_font_destroy(fx_font *font);
FX_API bool     fx_font_get_desc(const fx_font *font, fx_font_desc *out_desc);

/* Expose the underlying HarfBuzz font for caller-side text shaping.
 * The returned pointer is borrowed and valid for the lifetime of `font`. */
struct hb_font_t;
FX_API struct hb_font_t *fx_font_get_hb_font(fx_font *font);

FX_API fx_glyph_run *fx_glyph_run_create(size_t reserve_glyphs);
FX_API void          fx_glyph_run_destroy(fx_glyph_run *run);
FX_API void          fx_glyph_run_reset(fx_glyph_run *run);
FX_API bool          fx_glyph_run_append(fx_glyph_run *run,
                                           uint32_t      glyph_id,
                                           float         x,
                                           float         y);
FX_API size_t        fx_glyph_run_count(const fx_glyph_run *run);
FX_API const fx_glyph *fx_glyph_run_data(const fx_glyph_run *run);

/*
 * Low-level recording entry points. These append work to the frame's
 * canvas; the raster backend consumes the recorded ops at present time.
 */
static inline bool fx_rect_contains(const fx_rect *r, float x, float y) {
    return r && x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h;
}
FX_API bool fx_fill_rect(fx_canvas *c, const fx_rect *rect, fx_color color);
/* Fill a path.  The current tessellator handles concave simple polygons;
 * self-intersecting paths and the even-odd fill rule are not yet supported. */
FX_API bool fx_fill_path(fx_canvas *c, const fx_path *path, const fx_paint *paint);
FX_API bool fx_stroke_path(fx_canvas *c, const fx_path *path, const fx_paint *paint);
FX_API bool fx_draw_image(fx_canvas *c, const fx_image *image,
                            const fx_rect *src, const fx_rect *dst);
FX_API bool fx_draw_image_ex(fx_canvas *c, const fx_image *image, const fx_rect *src, const fx_rect *dst);
FX_API bool fx_draw_glyph_run(fx_canvas           *c,
                                const fx_font       *font,
                                const fx_glyph_run  *run,
                                float                x,
                                float                y,
                                const fx_paint      *paint);

FX_API void fx_matrix_multiply(fx_matrix *out, const fx_matrix *a, const fx_matrix *b);
FX_API void fx_matrix_transform_point(const fx_matrix *m, float *x, float *y);
FX_API fx_path *fx_path_transform(const fx_path *src, const fx_matrix *m);

/* Color helper: builds 0xAARRGGBB with premultiplied alpha from
 * non-premultiplied rgba in [0,255]. */
static inline fx_color fx_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    unsigned int ar = (r * a + 127) / 255;
    unsigned int ag = (g * a + 127) / 255;
    unsigned int ab = (b * a + 127) / 255;
    return ((fx_color)a  << 24) |
           ((fx_color)ar << 16) |
           ((fx_color)ag << 8 ) |
           ((fx_color)ab      );
}

#ifdef __cplusplus
}
#endif

#endif /* FX_H */
