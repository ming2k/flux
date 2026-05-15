/*
 * flux — 2D graphics foundation library.
 *
 * Public API v0.3.0.
 *
 * This is a clean break from prior 0.x versions. Every symbol uses the
 * `flux_*` prefix; every fallible call returns `flux_result`; every
 * lifetime-managed resource uses retain/release reference counting; every
 * descriptor struct begins with a `size` field for forward-compatible field
 * additions.
 *
 * Design contract (read this once):
 *
 *   - Opaque handles. Resource layout is private. Never dereference.
 *
 *   - Refcounted lifetime. Every `_create*` returns a refcount of 1.
 *     `_retain` increments; `_release` decrements and frees on zero.
 *     Both are NULL-safe.
 *
 *   - Sized descriptors. Set `desc.size = sizeof(*desc)` before passing.
 *     New fields are added to the end of descs in future versions; older
 *     callers using the smaller `size` continue to work.
 *
 *   - Result codes. Fallible calls return `flux_result`. Compare with
 *     `FLUX_OK`. `flux_result_string(r)` for diagnostics.
 *
 *   - Borrowed canvases. `flux_surface_acquire(s)` returns a borrowed
 *     `flux_canvas*` valid until the matching `flux_surface_present(s)`.
 *     Do not retain it; do not pass it to another thread.
 *
 *   - Borrowed resources in ops. Recorded ops borrow their `flux_path`,
 *     `flux_image`, `flux_glyph_run`. Keep these alive until present.
 *     `flux_paint`, `flux_gradient`, dash arrays are deep-copied at
 *     record time.
 *
 *   - Thread model. One context per thread. Resources may not be shared
 *     between contexts. Refcount operations are atomic, so cross-thread
 *     `release` of an idle resource is safe.
 */

#ifndef FLUX_H
#define FLUX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Symbol visibility                                                 */
/* ------------------------------------------------------------------ */

#if defined(_WIN32) && !defined(FLUX_STATIC)
#  ifdef FLUX_BUILDING
#    define FLUX_API __declspec(dllexport)
#  else
#    define FLUX_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define FLUX_API __attribute__((visibility("default")))
#else
#  define FLUX_API
#endif

/* `[[nodiscard]]` is C23. Fall back gracefully on older toolchains. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#  define FLUX_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#  define FLUX_NODISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#  define FLUX_NODISCARD _Check_return_
#else
#  define FLUX_NODISCARD
#endif

/* ------------------------------------------------------------------ */
/*  Versioning                                                        */
/* ------------------------------------------------------------------ */

#define FLUX_VERSION_MAJOR 0
#define FLUX_VERSION_MINOR 2
#define FLUX_VERSION_PATCH 0

/* Encoded as 0xMMmmpp00 (24-bit semantic version, low 8 bits reserved). */
#define FLUX_VERSION_NUMBER \
    ((FLUX_VERSION_MAJOR << 24) | (FLUX_VERSION_MINOR << 16) | (FLUX_VERSION_PATCH << 8))

FLUX_API void     flux_version(int *major, int *minor, int *patch);
FLUX_API uint32_t flux_version_number(void);

/* True if the linked library is at least the requested version.
 * Use this in `main()` if you ship against multiple flux versions. */
FLUX_API bool flux_version_check(int major, int minor, int patch);

/* ------------------------------------------------------------------ */
/*  Result codes                                                      */
/* ------------------------------------------------------------------ */

typedef enum flux_result {
    FLUX_OK                       = 0,
    FLUX_ERROR_INVALID_ARGUMENT   = 1,  /* NULL pointer, bad enum, etc. */
    FLUX_ERROR_OUT_OF_MEMORY      = 2,
    FLUX_ERROR_OUT_OF_RANGE       = 3,  /* Index or value past valid bound. */
    FLUX_ERROR_INVALID_STATE      = 4,  /* e.g. restore() with empty stack. */
    FLUX_ERROR_UNSUPPORTED        = 5,  /* Feature not built / not on this backend. */
    FLUX_ERROR_BACKEND_FAILURE    = 6,  /* Backend (Vulkan, etc.) refused. */
    FLUX_ERROR_DEVICE_LOST        = 7,
    FLUX_ERROR_SURFACE_LOST       = 8,
} flux_result;

FLUX_API const char *flux_result_string(flux_result r);

/* ------------------------------------------------------------------ */
/*  Allocator                                                         */
/* ------------------------------------------------------------------ */

/*
 * Pluggable memory allocator. All optional; passing NULL or leaving the
 * `flux_allocator` zero-initialized in `flux_context_desc` selects the
 * default (libc `malloc`/`realloc`/`free`).
 *
 * If supplied, ALL three callbacks must be non-NULL and must be valid
 * for the entire lifetime of the context (including its descendant
 * surfaces and resources).
 *
 *   - `alloc(bytes, user)`           returns a pointer to `bytes` bytes
 *                                    aligned for any standard scalar, or
 *                                    NULL on failure.
 *
 *   - `realloc(p, old, new, user)`   resizes a previous allocation. `p`
 *                                    may be NULL (equivalent to alloc).
 *                                    `old` may be 0 if not tracked.
 *
 *   - `free(p, user)`                p may be NULL.
 */
typedef struct flux_allocator {
    void *(*alloc)  (size_t bytes, void *user);
    void *(*realloc)(void *ptr, size_t old_bytes, size_t new_bytes, void *user);
    void  (*free)   (void *ptr, void *user);
    void  *user;
} flux_allocator;

/* ------------------------------------------------------------------ */
/*  Logger                                                            */
/* ------------------------------------------------------------------ */

typedef enum flux_log_level {
    FLUX_LOG_TRACE = 0,
    FLUX_LOG_DEBUG = 1,
    FLUX_LOG_INFO  = 2,
    FLUX_LOG_WARN  = 3,
    FLUX_LOG_ERROR = 4,
} flux_log_level;

/*
 * Logger callback. Called from inside flux for diagnostics. The format
 * string `fmt` is provided so structured loggers can re-parse arguments;
 * `msg` is the fully-formatted text and is safe to forward as-is.
 *
 * `file` and `line` describe the call site inside flux. They are stable
 * within a single library version but may change across versions.
 */
typedef void (*flux_log_fn)(flux_log_level level,
                            const char *file, int line,
                            const char *fmt, const char *msg,
                            void *user);

/* ------------------------------------------------------------------ */
/*  Capabilities                                                      */
/* ------------------------------------------------------------------ */

typedef struct flux_capabilities {
    uint32_t size;                  /* Set to sizeof(flux_capabilities). */
    bool     has_vulkan;
    bool     has_software;
    bool     has_stencil;
    bool     has_msaa;
    uint32_t max_gradient_stops;
    uint32_t max_image_size;
    uint32_t max_surface_size;
} flux_capabilities;

FLUX_API void flux_get_capabilities(flux_capabilities *out_caps);

/* ------------------------------------------------------------------ */
/*  Opaque handles                                                    */
/* ------------------------------------------------------------------ */

typedef struct flux_context   flux_context;
typedef struct flux_surface   flux_surface;
typedef struct flux_canvas    flux_canvas;
typedef struct flux_path      flux_path;
typedef struct flux_paint     flux_paint;
typedef struct flux_gradient  flux_gradient;
typedef struct flux_image     flux_image;
typedef struct flux_glyph_run flux_glyph_run;

/* ------------------------------------------------------------------ */
/*  Value types                                                       */
/* ------------------------------------------------------------------ */

/* 0xAARRGGBB premultiplied. Build with `flux_color_rgba` or `_rgba_premul`. */
typedef uint32_t flux_color;

typedef struct flux_point  { float x, y;       } flux_point;
typedef struct flux_size   { float w, h;       } flux_size;
typedef struct flux_rect   { float x, y, w, h; } flux_rect;

/* 2D affine transform stored column-major:
 *   [ m[0]  m[2]  m[4] ]
 *   [ m[1]  m[3]  m[5] ]
 *   [  0     0     1   ]
 *
 *   x' = m[0]*x + m[2]*y + m[4]
 *   y' = m[1]*x + m[3]*y + m[5]
 *
 * Matches Cairo, SVG, and HTML Canvas conventions. */
typedef struct flux_matrix { float m[6]; } flux_matrix;

/* ------------------------------------------------------------------ */
/*  Enums                                                             */
/* ------------------------------------------------------------------ */

typedef enum flux_line_cap {
    FLUX_CAP_BUTT   = 0,
    FLUX_CAP_ROUND  = 1,
    FLUX_CAP_SQUARE = 2,
} flux_line_cap;

typedef enum flux_line_join {
    FLUX_JOIN_MITER = 0,
    FLUX_JOIN_ROUND = 1,
    FLUX_JOIN_BEVEL = 2,
} flux_line_join;

typedef enum flux_fill_rule {
    FLUX_FILL_EVEN_ODD = 0,
    FLUX_FILL_NON_ZERO = 1,
} flux_fill_rule;

/* Porter-Duff and selected separable blend modes. Backends that do not
 * support a mode return FLUX_ERROR_UNSUPPORTED at record time. */
typedef enum flux_blend_mode {
    FLUX_BLEND_SRC_OVER = 0,
    FLUX_BLEND_DST_OVER = 1,
    FLUX_BLEND_SRC_IN   = 2,
    FLUX_BLEND_DST_IN   = 3,
    FLUX_BLEND_SRC_OUT  = 4,
    FLUX_BLEND_DST_OUT  = 5,
    FLUX_BLEND_SRC_ATOP = 6,
    FLUX_BLEND_DST_ATOP = 7,
    FLUX_BLEND_XOR      = 8,
    FLUX_BLEND_PLUS     = 9,
    FLUX_BLEND_MULTIPLY = 10,
    FLUX_BLEND_SCREEN   = 11,
    FLUX_BLEND_OVERLAY  = 12,
} flux_blend_mode;

typedef enum flux_color_space {
    FLUX_CS_SRGB        = 0,
    FLUX_CS_SRGB_LINEAR = 1,
} flux_color_space;

typedef enum flux_pixel_format {
    FLUX_FMT_BGRA8_UNORM = 0,
    FLUX_FMT_RGBA8_UNORM = 1,
    FLUX_FMT_A8_UNORM    = 2,
} flux_pixel_format;

typedef enum flux_image_usage_bits {
    FLUX_IMAGE_USAGE_SAMPLED          = 1u << 0,
    FLUX_IMAGE_USAGE_STORAGE          = 1u << 1,
    FLUX_IMAGE_USAGE_COLOR_ATTACHMENT = 1u << 2,
    FLUX_IMAGE_USAGE_TRANSFER_SRC     = 1u << 3,
    FLUX_IMAGE_USAGE_TRANSFER_DST     = 1u << 4,
} flux_image_usage_bits;

typedef enum flux_gradient_extend {
    FLUX_EXTEND_PAD     = 0,   /* Hold edge color past the last stop. */
    FLUX_EXTEND_REPEAT  = 1,
    FLUX_EXTEND_REFLECT = 2,
} flux_gradient_extend;

/* ------------------------------------------------------------------ */
/*  Descriptor structs                                                */
/* ------------------------------------------------------------------ */

typedef struct flux_context_desc {
    uint32_t        size;          /* sizeof(flux_context_desc) */
    flux_allocator  allocator;     /* Zero-init = libc malloc. */
    flux_log_fn     log;           /* NULL = silent. */
    void           *log_user;
    flux_log_level  min_log_level; /* Messages below this are dropped. */
} flux_context_desc;

typedef struct flux_image_desc {
    uint32_t          size;        /* sizeof(flux_image_desc) */
    uint32_t          width, height;
    flux_pixel_format format;
    uint32_t          usage;       /* Bitfield of flux_image_usage_bits. */
    const void       *data;        /* Optional initial pixel data. */
    size_t            stride;      /* Bytes per row of `data`; 0 = packed. */
} flux_image_desc;

typedef struct flux_linear_gradient_desc {
    uint32_t              size;    /* sizeof(flux_linear_gradient_desc) */
    flux_point            start, end;
    const flux_color     *colors;  /* Length = stop_count. */
    const float          *stops;   /* Length = stop_count, monotonic in [0,1]. */
    uint32_t              stop_count;
    flux_gradient_extend  extend;
} flux_linear_gradient_desc;

typedef struct flux_radial_gradient_desc {
    uint32_t              size;    /* sizeof(flux_radial_gradient_desc) */
    flux_point            center;
    float                 radius;
    const flux_color     *colors;
    const float          *stops;
    uint32_t              stop_count;
    flux_gradient_extend  extend;
} flux_radial_gradient_desc;

typedef struct flux_glyph {
    uint32_t glyph_id;
    float    x, y;
} flux_glyph;

/* ------------------------------------------------------------------ */
/*  Context                                                           */
/* ------------------------------------------------------------------ */

FLUX_NODISCARD FLUX_API flux_result flux_context_create(
    const flux_context_desc *desc, flux_context **out_ctx);

FLUX_NODISCARD FLUX_API flux_context *flux_context_retain(flux_context *ctx);
FLUX_API               void           flux_context_release(flux_context *ctx);

/* Reflective getters — useful for FFI bindings and tests. */
FLUX_API flux_log_level         flux_context_get_log_level(const flux_context *ctx);
FLUX_API const flux_allocator  *flux_context_get_allocator(const flux_context *ctx);

/* ------------------------------------------------------------------ */
/*  Math: matrix                                                      */
/* ------------------------------------------------------------------ */

static inline void flux_matrix_identity(flux_matrix *m)
{
    m->m[0] = 1.0f; m->m[1] = 0.0f;
    m->m[2] = 0.0f; m->m[3] = 1.0f;
    m->m[4] = 0.0f; m->m[5] = 0.0f;
}

static inline void flux_matrix_translation(flux_matrix *m, float dx, float dy)
{
    m->m[0] = 1.0f; m->m[1] = 0.0f;
    m->m[2] = 0.0f; m->m[3] = 1.0f;
    m->m[4] = dx;   m->m[5] = dy;
}

static inline void flux_matrix_scaling(flux_matrix *m, float sx, float sy)
{
    m->m[0] = sx;   m->m[1] = 0.0f;
    m->m[2] = 0.0f; m->m[3] = sy;
    m->m[4] = 0.0f; m->m[5] = 0.0f;
}

FLUX_API void flux_matrix_rotation(flux_matrix *m, float radians);

FLUX_API void flux_matrix_multiply(flux_matrix *out,
                                   const flux_matrix *a, const flux_matrix *b);

/* Returns false if `m` is singular (determinant == 0); `out` unchanged. */
FLUX_NODISCARD FLUX_API bool flux_matrix_invert(const flux_matrix *m, flux_matrix *out);

FLUX_API bool flux_matrix_is_identity(const flux_matrix *m);

FLUX_API void flux_matrix_transform_point(const flux_matrix *m, float *x, float *y);

/* Compute the axis-aligned bounding rect of `in` after transformation. */
FLUX_API void flux_matrix_transform_rect(const flux_matrix *m,
                                         const flux_rect *in, flux_rect *out);

/* ------------------------------------------------------------------ */
/*  Math: color                                                       */
/* ------------------------------------------------------------------ */

/* Pack non-premultiplied RGBA components, premultiplying alpha. */
static inline flux_color flux_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    unsigned int ar = ((unsigned int)r * a + 127) / 255;
    unsigned int ag = ((unsigned int)g * a + 127) / 255;
    unsigned int ab = ((unsigned int)b * a + 127) / 255;
    return ((flux_color)a  << 24) | ((flux_color)ar << 16)
         | ((flux_color)ag << 8)  | ((flux_color)ab);
}

/* Pack already-premultiplied RGBA. Caller is responsible for r,g,b <= a. */
static inline flux_color flux_color_rgba_premul(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((flux_color)a << 24) | ((flux_color)r << 16)
         | ((flux_color)g << 8)  | ((flux_color)b);
}

/* Unpack to non-premultiplied components. */
FLUX_API void flux_color_unpack(flux_color c,
                                uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

/* ------------------------------------------------------------------ */
/*  Surface                                                           */
/* ------------------------------------------------------------------ */

FLUX_NODISCARD FLUX_API flux_result flux_surface_create_offscreen(
    flux_context *ctx,
    int32_t width, int32_t height,
    flux_pixel_format format, flux_color_space cs,
    flux_surface **out_surface);

FLUX_NODISCARD FLUX_API flux_surface *flux_surface_retain(flux_surface *s);
FLUX_API               void           flux_surface_release(flux_surface *s);

FLUX_API flux_result flux_surface_resize(flux_surface *s, int32_t w, int32_t h);

FLUX_API flux_result flux_surface_get_size(const flux_surface *s,
                                           int32_t *out_w, int32_t *out_h);
FLUX_API flux_pixel_format flux_surface_get_format(const flux_surface *s);

FLUX_API flux_result flux_surface_set_dpr(flux_surface *s, float dpr);
FLUX_API float       flux_surface_get_dpr(const flux_surface *s);

/* Begin a frame. The returned canvas is borrowed and valid only until
 * the matching flux_surface_present(s). Returns NULL on failure. */
FLUX_NODISCARD FLUX_API flux_canvas *flux_surface_acquire(flux_surface *s);

/* Execute the recorded display list and present. */
FLUX_API flux_result flux_surface_present(flux_surface *s);

/* Read back the most recently presented frame. Stride 0 = packed.
 * Only available for offscreen / software surfaces. */
FLUX_NODISCARD FLUX_API flux_result flux_surface_read_pixels(
    flux_surface *s, void *out_data, size_t stride);

/* ------------------------------------------------------------------ */
/*  Canvas (recording)                                                */
/* ------------------------------------------------------------------ */

FLUX_API flux_result flux_canvas_clear(flux_canvas *c, flux_color color);
FLUX_API size_t      flux_canvas_op_count(const flux_canvas *c);

/* Transform stack. */
FLUX_API flux_result flux_canvas_save(flux_canvas *c);
FLUX_API flux_result flux_canvas_restore(flux_canvas *c);
FLUX_API flux_result flux_canvas_translate(flux_canvas *c, float dx, float dy);
FLUX_API flux_result flux_canvas_scale(flux_canvas *c, float sx, float sy);
FLUX_API flux_result flux_canvas_rotate(flux_canvas *c, float radians);
FLUX_API flux_result flux_canvas_concat(flux_canvas *c, const flux_matrix *m);
FLUX_API flux_result flux_canvas_set_matrix(flux_canvas *c, const flux_matrix *m);
FLUX_API flux_result flux_canvas_get_matrix(const flux_canvas *c, flux_matrix *out_m);

/* Clipping. */
FLUX_API flux_result flux_canvas_clip_rect(flux_canvas *c, const flux_rect *rect);
FLUX_API flux_result flux_canvas_clip_path(flux_canvas *c, const flux_path *path);
FLUX_API flux_result flux_canvas_reset_clip(flux_canvas *c);

/* Drawing. */
FLUX_NODISCARD FLUX_API flux_result flux_canvas_fill_rect(
    flux_canvas *c, const flux_rect *rect, flux_color color);

FLUX_NODISCARD FLUX_API flux_result flux_canvas_fill_path(
    flux_canvas *c, const flux_path *path, const flux_paint *paint);

FLUX_NODISCARD FLUX_API flux_result flux_canvas_stroke_path(
    flux_canvas *c, const flux_path *path, const flux_paint *paint);

FLUX_NODISCARD FLUX_API flux_result flux_canvas_draw_image(
    flux_canvas *c, const flux_image *image,
    const flux_rect *src /* NULL = full image */, const flux_rect *dst);

FLUX_NODISCARD FLUX_API flux_result flux_canvas_draw_glyph_run(
    flux_canvas *c, const flux_glyph_run *run,
    float x, float y, const flux_paint *paint);

/* Apply a Gaussian blur. Sigma <= 0 is a no-op. Offscreen surfaces only. */
FLUX_NODISCARD FLUX_API flux_result flux_canvas_apply_blur(flux_canvas *c, float sigma);

/* ------------------------------------------------------------------ */
/*  Paint                                                             */
/* ------------------------------------------------------------------ */

FLUX_NODISCARD FLUX_API flux_result flux_paint_create(
    flux_context *ctx, flux_paint **out_paint);

FLUX_NODISCARD FLUX_API flux_paint *flux_paint_retain(flux_paint *paint);
FLUX_API               void         flux_paint_release(flux_paint *paint);

/* Setters. */
FLUX_API flux_result flux_paint_set_color       (flux_paint *p, flux_color c);
FLUX_API flux_result flux_paint_set_stroke_width(flux_paint *p, float w);
FLUX_API flux_result flux_paint_set_miter_limit (flux_paint *p, float limit);
FLUX_API flux_result flux_paint_set_line_cap    (flux_paint *p, flux_line_cap cap);
FLUX_API flux_result flux_paint_set_line_join   (flux_paint *p, flux_line_join join);
FLUX_API flux_result flux_paint_set_fill_rule   (flux_paint *p, flux_fill_rule rule);
FLUX_API flux_result flux_paint_set_blend_mode  (flux_paint *p, flux_blend_mode mode);
FLUX_API flux_result flux_paint_set_gradient    (flux_paint *p, flux_gradient *g /* may be NULL */);
FLUX_API flux_result flux_paint_set_dash        (flux_paint *p, const float *dashes,
                                                 uint32_t count, float phase);

/* Getters. */
FLUX_API flux_color      flux_paint_get_color       (const flux_paint *p);
FLUX_API float           flux_paint_get_stroke_width(const flux_paint *p);
FLUX_API float           flux_paint_get_miter_limit (const flux_paint *p);
FLUX_API flux_line_cap   flux_paint_get_line_cap    (const flux_paint *p);
FLUX_API flux_line_join  flux_paint_get_line_join   (const flux_paint *p);
FLUX_API flux_fill_rule  flux_paint_get_fill_rule   (const flux_paint *p);
FLUX_API flux_blend_mode flux_paint_get_blend_mode  (const flux_paint *p);
FLUX_API flux_gradient  *flux_paint_get_gradient    (const flux_paint *p);  /* borrowed */
FLUX_API uint32_t        flux_paint_get_dash_count  (const flux_paint *p);
FLUX_API float           flux_paint_get_dash_phase  (const flux_paint *p);
/* Copy at most `cap` dash lengths into `out` and return the actual count. */
FLUX_API uint32_t        flux_paint_copy_dash       (const flux_paint *p,
                                                     float *out, uint32_t cap);

/* ------------------------------------------------------------------ */
/*  Path                                                              */
/* ------------------------------------------------------------------ */

FLUX_NODISCARD FLUX_API flux_result flux_path_create(
    flux_context *ctx, flux_path **out_path);

FLUX_NODISCARD FLUX_API flux_path *flux_path_retain(flux_path *path);
FLUX_API               void        flux_path_release(flux_path *path);

/* Clear all verbs and points without freeing the path object. */
FLUX_API void flux_path_clear(flux_path *path);

/* Verb construction. */
FLUX_API flux_result flux_path_move_to (flux_path *p, float x, float y);
FLUX_API flux_result flux_path_line_to (flux_path *p, float x, float y);
FLUX_API flux_result flux_path_quad_to (flux_path *p, float cx, float cy,
                                        float x, float y);
FLUX_API flux_result flux_path_cubic_to(flux_path *p, float cx0, float cy0,
                                        float cx1, float cy1, float x, float y);
FLUX_API flux_result flux_path_close   (flux_path *p);

/* Convenience shapes. Open a new subpath at the appropriate start. */
FLUX_API flux_result flux_path_add_rect      (flux_path *p, const flux_rect *r);
FLUX_API flux_result flux_path_add_round_rect(flux_path *p, const flux_rect *r,
                                              float radius);
FLUX_API flux_result flux_path_add_circle    (flux_path *p, float cx, float cy, float r);
FLUX_API flux_result flux_path_add_ellipse   (flux_path *p, float cx, float cy,
                                              float rx, float ry);

/* SVG-style elliptical arc from the current point to (x,y). */
FLUX_API flux_result flux_path_arc_to(flux_path *p,
                                      float rx, float ry, float rotation,
                                      bool large_arc, bool sweep,
                                      float x, float y);

/* Queries. */
FLUX_NODISCARD FLUX_API flux_result flux_path_get_bounds(const flux_path *p, flux_rect *out);
FLUX_API size_t flux_path_verb_count (const flux_path *p);
FLUX_API size_t flux_path_point_count(const flux_path *p);

/* Return a new path that is `src` transformed by `m`. The original is
 * untouched. Caller releases the returned path. */
FLUX_NODISCARD FLUX_API flux_result flux_path_transform(
    const flux_path *src, const flux_matrix *m, flux_path **out_path);

/* ------------------------------------------------------------------ */
/*  Gradient                                                          */
/* ------------------------------------------------------------------ */

FLUX_NODISCARD FLUX_API flux_result flux_gradient_create_linear(
    flux_context *ctx, const flux_linear_gradient_desc *desc,
    flux_gradient **out_gradient);

FLUX_NODISCARD FLUX_API flux_result flux_gradient_create_radial(
    flux_context *ctx, const flux_radial_gradient_desc *desc,
    flux_gradient **out_gradient);

FLUX_NODISCARD FLUX_API flux_gradient *flux_gradient_retain(flux_gradient *g);
FLUX_API               void            flux_gradient_release(flux_gradient *g);

/* ------------------------------------------------------------------ */
/*  Image                                                             */
/* ------------------------------------------------------------------ */

FLUX_NODISCARD FLUX_API flux_result flux_image_create(
    flux_context *ctx, const flux_image_desc *desc, flux_image **out_image);

/* Wrap the most recently presented frame of an offscreen surface as an
 * image. The surface must remain alive while the image is in use. The
 * image holds a reference on the surface. */
FLUX_NODISCARD FLUX_API flux_result flux_image_create_from_surface(
    flux_surface *s, flux_image **out_image);

FLUX_NODISCARD FLUX_API flux_image *flux_image_retain(flux_image *image);
FLUX_API               void         flux_image_release(flux_image *image);

/* Replace pixel data. `stride == 0` means packed (width * bpp). */
FLUX_NODISCARD FLUX_API flux_result flux_image_update(
    flux_image *image, const void *data, size_t stride);

/* Update a sub-region. (x, y) is the top-left in image space; the source
 * data is `w` * `h` pixels at the given stride. */
FLUX_NODISCARD FLUX_API flux_result flux_image_update_region(
    flux_image *image, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
    const void *data, size_t stride);

FLUX_API flux_result flux_image_get_size(const flux_image *image,
                                         uint32_t *out_w, uint32_t *out_h);
FLUX_API flux_pixel_format flux_image_get_format(const flux_image *image);

/* CPU-side pixel pointer if the image keeps one (for offscreen / readback).
 * Returns NULL otherwise. */
FLUX_API const void *flux_image_data(const flux_image *image,
                                     size_t *out_size, size_t *out_stride);

/* ------------------------------------------------------------------ */
/*  Glyph run                                                         */
/* ------------------------------------------------------------------ */

FLUX_NODISCARD FLUX_API flux_result flux_glyph_upload(
    flux_context *ctx, uint32_t glyph_id,
    const uint8_t *bitmap, int w, int h,
    int bearing_x, int bearing_y, int advance);

FLUX_NODISCARD FLUX_API flux_result flux_glyph_run_create(
    flux_context *ctx, size_t reserve, flux_glyph_run **out_run);

FLUX_NODISCARD FLUX_API flux_glyph_run *flux_glyph_run_retain(flux_glyph_run *run);
FLUX_API               void             flux_glyph_run_release(flux_glyph_run *run);

FLUX_API void flux_glyph_run_clear(flux_glyph_run *run);

FLUX_API flux_result flux_glyph_run_append(
    flux_glyph_run *run, uint32_t glyph_id, float x, float y);

FLUX_API size_t            flux_glyph_run_count(const flux_glyph_run *run);
FLUX_API const flux_glyph *flux_glyph_run_data (const flux_glyph_run *run);

#ifdef __cplusplus
}
#endif

#endif /* FLUX_H */
