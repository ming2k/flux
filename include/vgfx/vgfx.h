/*
 * vgfx — low-level 2D graphics foundation on Vulkan.
 *
 * The public API intentionally stays close to the rendering substrate:
 * surfaces, images, paths, explicit glyph runs, and a frame-local
 * canvas recorder. Layout and shaping stay above vgfx; Vulkan objects
 * and queue management stay below it.
 */
#ifndef VGFX_H
#define VGFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(VGFX_STATIC)
#  ifdef VGFX_BUILDING
#    define VGFX_API __declspec(dllexport)
#  else
#    define VGFX_API __declspec(dllimport)
#  endif
#else
#  define VGFX_API __attribute__((visibility("default")))
#endif

typedef struct vg_context  vg_context;
typedef struct vg_surface  vg_surface;
typedef struct vg_canvas   vg_canvas;
typedef struct vg_image    vg_image;
typedef struct vg_path     vg_path;
typedef struct vg_font     vg_font;
typedef struct vg_glyph_run vg_glyph_run;

typedef uint32_t vg_color;   /* 0xAARRGGBB, premultiplied */

typedef struct {
    float x;
    float y;
} vg_point;

typedef struct {
    float x;
    float y;
    float w;
    float h;
} vg_rect;

typedef struct {
    float m[6];  /* [a c e; b d f] -> x' = ax + cy + e, y' = bx + dy + f */
} vg_matrix;

typedef enum {
    VG_CAP_BUTT = 0,
    VG_CAP_ROUND = 1,
    VG_CAP_SQUARE = 2,
} vg_line_cap;

typedef enum {
    VG_JOIN_MITER = 0,
    VG_JOIN_ROUND = 1,
    VG_JOIN_BEVEL = 2,
} vg_line_join;

typedef struct {
    vg_color     color;
    float        stroke_width;
    float        miter_limit;
    vg_line_cap  line_cap;
    vg_line_join line_join;
} vg_paint;

typedef enum {
    VG_CS_SRGB = 0,
    VG_CS_SRGB_LINEAR = 1,
} vg_color_space;

typedef enum {
    VG_FMT_BGRA8_UNORM = 0,
    VG_FMT_RGBA8_UNORM = 1,
    VG_FMT_A8_UNORM    = 2,
} vg_pixel_format;

typedef enum {
    VG_IMAGE_USAGE_SAMPLED          = 1u << 0,
    VG_IMAGE_USAGE_STORAGE          = 1u << 1,
    VG_IMAGE_USAGE_COLOR_ATTACHMENT = 1u << 2,
    VG_IMAGE_USAGE_TRANSFER_SRC     = 1u << 3,
    VG_IMAGE_USAGE_TRANSFER_DST     = 1u << 4,
} vg_image_usage_bits;

typedef enum {
    VG_LOG_ERROR = 0,
    VG_LOG_WARN  = 1,
    VG_LOG_INFO  = 2,
    VG_LOG_DEBUG = 3,
} vg_log_level;

typedef void (*vg_log_fn)(vg_log_level level, const char *msg, void *user);

typedef struct {
    vg_log_fn log;
    void     *log_user;
    bool      enable_validation;  /* honored only if compiled in */
    const char *app_name;
} vg_context_desc;

typedef struct {
    bool      validation_enabled;
    bool      graphics_queue;
    bool      present_queue;
    bool      sampled_images;
    bool      storage_images;
    uint32_t  api_version;
    uint32_t  max_image_dimension_2d;
    uint32_t  max_color_attachments;
    uint32_t  max_compute_workgroup_invocations;
} vg_device_caps;

typedef struct {
    uint32_t        width;
    uint32_t        height;
    vg_pixel_format format;
    uint32_t        usage;      /* vg_image_usage_bits */
    const void     *data;       /* optional initial pixels */
    size_t          stride;     /* bytes per row; 0 = tightly packed */
} vg_image_desc;

typedef struct {
    const char *family;       /* optional family/display name */
    const char *source_name;  /* optional file or source identifier */
    float       size;
    int32_t     weight;
    bool        italic;
} vg_font_desc;

typedef struct {
    uint32_t glyph_id;
    float    x;
    float    y;
} vg_glyph;

VGFX_API vg_context *vg_context_create(const vg_context_desc *desc);
VGFX_API void        vg_context_destroy(vg_context *ctx);
VGFX_API bool        vg_context_get_device_caps(const vg_context *ctx,
                                                vg_device_caps  *out_caps);

VGFX_API void vg_surface_destroy(vg_surface *s);
VGFX_API void vg_surface_resize(vg_surface *s, int32_t w, int32_t h);

/*
 * Begin a frame. Returns a canvas valid until the matching
 * vg_surface_present. Do not keep the pointer across frames.
 */
VGFX_API vg_canvas *vg_surface_acquire(vg_surface *s);

/* End a frame: submit recorded commands and present. */
VGFX_API void       vg_surface_present(vg_surface *s);

/* Clear the entire canvas to a premultiplied color. */
VGFX_API void vg_clear(vg_canvas *c, vg_color color);
VGFX_API size_t vg_canvas_op_count(const vg_canvas *c);

/*
 * Canvas state and transformation. Transformations are applied to
 * paths and glyph positions immediately upon recording.
 */
VGFX_API void vg_save(vg_canvas *c);
VGFX_API void vg_restore(vg_canvas *c);

VGFX_API void vg_translate(vg_canvas *c, float dx, float dy);
VGFX_API void vg_scale(vg_canvas *c, float sx, float sy);
VGFX_API void vg_rotate(vg_canvas *c, float radians);
VGFX_API void vg_concat(vg_canvas *c, const vg_matrix *m);
VGFX_API void vg_set_matrix(vg_canvas *c, const vg_matrix *m);
VGFX_API void vg_get_matrix(const vg_canvas *c, vg_matrix *out_m);

VGFX_API void vg_paint_init(vg_paint *paint, vg_color color);

/*
 * Images are context-owned resource objects. In the current runtime
 * they retain a validated descriptor and an optional CPU-side pixel
 * copy; the Vulkan upload path is the next layer built on top of this
 * object.
 */
VGFX_API vg_image *vg_image_create(vg_context *ctx, const vg_image_desc *desc);
VGFX_API void      vg_image_destroy(vg_image *image);
VGFX_API bool      vg_image_get_desc(const vg_image *image,
                                     vg_image_desc  *out_desc);
VGFX_API const void *vg_image_data(const vg_image *image,
                                   size_t         *out_size,
                                   size_t         *out_stride);

/*
 * Paths are explicit verb/point streams. No hidden tessellation or
 * stroke expansion happens at construction time.
 */
VGFX_API vg_path *vg_path_create(void);
VGFX_API void     vg_path_destroy(vg_path *path);
VGFX_API void     vg_path_reset(vg_path *path);
VGFX_API bool     vg_path_move_to(vg_path *path, float x, float y);
VGFX_API bool     vg_path_line_to(vg_path *path, float x, float y);
VGFX_API bool     vg_path_quad_to(vg_path *path,
                                  float cx, float cy,
                                  float x,  float y);
VGFX_API bool     vg_path_cubic_to(vg_path *path,
                                   float cx0, float cy0,
                                   float cx1, float cy1,
                                   float x,   float y);
VGFX_API bool     vg_path_close(vg_path *path);
VGFX_API bool     vg_path_add_rect(vg_path *path, const vg_rect *rect);
VGFX_API bool     vg_path_get_bounds(const vg_path *path, vg_rect *out_bounds);
VGFX_API size_t   vg_path_verb_count(const vg_path *path);
VGFX_API size_t   vg_path_point_count(const vg_path *path);

/*
 * Fonts are metadata handles. Text shaping and font fallback stay in
 * upstream components such as HarfBuzz/Pango; vgfx consumes positioned
 * glyph runs.
 */
VGFX_API vg_font *vg_font_create(vg_context *ctx, const vg_font_desc *desc);
VGFX_API void     vg_font_destroy(vg_font *font);
VGFX_API bool     vg_font_get_desc(const vg_font *font, vg_font_desc *out_desc);

VGFX_API vg_glyph_run *vg_glyph_run_create(size_t reserve_glyphs);
VGFX_API void          vg_glyph_run_destroy(vg_glyph_run *run);
VGFX_API void          vg_glyph_run_reset(vg_glyph_run *run);
VGFX_API bool          vg_glyph_run_append(vg_glyph_run *run,
                                           uint32_t      glyph_id,
                                           float         x,
                                           float         y);
VGFX_API size_t        vg_glyph_run_count(const vg_glyph_run *run);
VGFX_API const vg_glyph *vg_glyph_run_data(const vg_glyph_run *run);

/*
 * Low-level recording entry points. These append work to the frame's
 * canvas; the raster backend consumes the recorded ops at present time.
 */
VGFX_API bool vg_fill_path(vg_canvas *c, const vg_path *path, const vg_paint *paint);
VGFX_API bool vg_stroke_path(vg_canvas *c, const vg_path *path, const vg_paint *paint);
VGFX_API bool vg_draw_image(vg_canvas *c, const vg_image *image,
                            const vg_rect *src, const vg_rect *dst);
VGFX_API bool vg_draw_glyph_run(vg_canvas           *c,
                                const vg_font       *font,
                                const vg_glyph_run  *run,
                                float                x,
                                float                y,
                                const vg_paint      *paint);

/* Color helper: builds 0xAARRGGBB with premultiplied alpha from
 * non-premultiplied rgba in [0,255]. */
static inline vg_color vg_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    unsigned int ar = (r * a + 127) / 255;
    unsigned int ag = (g * a + 127) / 255;
    unsigned int ab = (b * a + 127) / 255;
    return ((vg_color)a  << 24) |
           ((vg_color)ar << 16) |
           ((vg_color)ag << 8 ) |
           ((vg_color)ab      );
}

#ifdef __cplusplus
}
#endif

#endif /* VGFX_H */
