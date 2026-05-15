/*
 * Internal types shared across all flux translation units.
 *
 * Public handles (flux_context, flux_paint, ...) are forward-declared in
 * <flux/flux.h>; this header defines their concrete layout.
 *
 * Backend-specific types (Vulkan handles, software pixel buffers) live
 * inside the corresponding RHI implementation files.
 */
#ifndef FLUX_INTERNAL_H
#define FLUX_INTERNAL_H

#include "flux/flux.h"
#include "rhi/rhi.h"

#ifndef FLUX_NO_VULKAN
#include "flux/flux_vulkan.h"
#endif
#include "geometry/geometry.h"
#include "math/matrix.h"
#include "math/rect.h"
#include "math/arena.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __STDC_NO_ATOMICS__
#  error "C11 atomics required"
#endif
#include <stdatomic.h>

/* ------------------------------------------------------------------ */
/*  Thread-affinity assertion (debug only)                            */
/* ------------------------------------------------------------------ */

#ifndef NDEBUG
#  include <threads.h>
#  define FLUX_ASSERT_THREAD(obj) \
     do { if ((obj)) assert(thrd_current() == (obj)->owner_thread); } while (0)
#else
#  define FLUX_ASSERT_THREAD(obj) ((void)0)
#endif

/* ------------------------------------------------------------------ */
/*  Logging                                                           */
/* ------------------------------------------------------------------ */

struct flux_context;

void flux_log_impl(const struct flux_context *ctx, flux_log_level lvl,
                   const char *file, int line,
                   const char *fmt, ...) __attribute__((format(printf, 5, 6)));

#define FLUX_LOGE(ctx, ...) flux_log_impl((ctx), FLUX_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define FLUX_LOGW(ctx, ...) flux_log_impl((ctx), FLUX_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define FLUX_LOGI(ctx, ...) flux_log_impl((ctx), FLUX_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#ifdef NDEBUG
#  define FLUX_LOGD(ctx, ...) ((void)0)
#  define FLUX_LOGT(ctx, ...) ((void)0)
#else
#  define FLUX_LOGD(ctx, ...) flux_log_impl((ctx), FLUX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#  define FLUX_LOGT(ctx, ...) flux_log_impl((ctx), FLUX_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#endif

/* ------------------------------------------------------------------ */
/*  Refcount helpers                                                  */
/* ------------------------------------------------------------------ */

static inline void flux_ref_init(atomic_int *ref) { atomic_init(ref, 1); }

static inline int flux_ref_retain(atomic_int *ref) {
    return atomic_fetch_add_explicit(ref, 1, memory_order_relaxed) + 1;
}

static inline int flux_ref_release(atomic_int *ref) {
    int prev = atomic_fetch_sub_explicit(ref, 1, memory_order_release);
    if (prev == 1) atomic_thread_fence(memory_order_acquire);
    return prev - 1;
}

/* ------------------------------------------------------------------ */
/*  Allocator: context-routed                                         */
/* ------------------------------------------------------------------ */

void *flux_alloc  (flux_context *ctx, size_t bytes);
void *flux_realloc(flux_context *ctx, void *ptr, size_t old_bytes, size_t new_bytes);
void  flux_free   (flux_context *ctx, void *ptr);

/* Convenience: alloc-and-zero. */
void *flux_calloc(flux_context *ctx, size_t count, size_t element_size);

/* Stack-allocated context for early bring-up where no flux_context exists.
 * Falls through to libc malloc. */
extern flux_context *const FLUX_DEFAULT_CTX;

/* ------------------------------------------------------------------ */
/*  flux_context                                                      */
/* ------------------------------------------------------------------ */

struct flux_context {
    atomic_int      ref_count;
#ifndef NDEBUG
    thrd_t          owner_thread;
#endif
    flux_allocator  allocator;        /* Resolved (default-filled). */
    flux_log_fn     log;
    void           *log_user;
    flux_log_level  min_log_level;
};

/* ------------------------------------------------------------------ */
/*  flux_paint_state — POD snapshot consumed by the engine            */
/* ------------------------------------------------------------------ */

/*
 * The geometry and engine layers read paint state through this POD,
 * not through the public flux_paint handle. This keeps geometry
 * uncoupled from the public API's lifetime model.
 *
 * The dash array and gradient pointers in the snapshot are *owned* by
 * the recorded op (deep-copied / retained at record time) and freed by
 * canvas_reset.
 */
typedef struct flux_paint_state {
    flux_color      color;
    flux_blend_mode blend_mode;
    flux_fill_rule  fill_rule;
    flux_line_cap   line_cap;
    flux_line_join  line_join;
    float           stroke_width;
    float           miter_limit;
    float          *dash_array;       /* owned by op (allocator-allocated) */
    uint32_t        dash_count;
    float           dash_phase;
    flux_gradient  *gradient;         /* retained by op */
} flux_paint_state;

struct flux_paint {
    atomic_int       ref_count;
    flux_context    *ctx;             /* retained */
    flux_paint_state state;           /* dash_array allocated via ctx allocator */
};

/* Snapshot a public paint into a POD (allocates dash array, retains gradient). */
flux_result flux_paint_snapshot(const flux_paint *src, flux_paint_state *out);
/* Free state owned by a snapshot (releases gradient, frees dash array). */
void        flux_paint_state_dispose(flux_paint_state *st, flux_context *ctx);

/* ------------------------------------------------------------------ */
/*  flux_canvas                                                       */
/* ------------------------------------------------------------------ */

#include "state/state.h"   /* flux_op definitions */

struct flux_canvas {
    flux_surface  *owner;
    flux_color     clear_color;
    bool           has_clear;

    flux_op       *ops;
    size_t         op_count;
    size_t         op_cap;

    flux_matrix   *state_stack;
    size_t         state_count;
    size_t         state_cap;
    flux_matrix    current_matrix;

    float          dpr;
};

/* ------------------------------------------------------------------ */
/*  flux_surface                                                      */
/* ------------------------------------------------------------------ */

struct flux_surface {
    atomic_int        ref_count;
    flux_context     *ctx;
    flux_rhi_device  *rhi;
    flux_canvas       canvas;
    bool              is_offscreen;
    bool              needs_recreate;
    int32_t           width, height;
    flux_pixel_format format;
};

/* ------------------------------------------------------------------ */
/*  flux_path                                                         */
/* ------------------------------------------------------------------ */

struct flux_path {
    atomic_int     ref_count;
    flux_context  *ctx;             /* retained */
    uint8_t       *verbs;
    size_t         verb_count, verb_cap;
    flux_point    *points;
    size_t         point_count, point_cap;
    flux_rect      bounds;
    bool           has_bounds;
};

/* ------------------------------------------------------------------ */
/*  flux_glyph_run                                                    */
/* ------------------------------------------------------------------ */

struct flux_glyph_run {
    atomic_int     ref_count;
    flux_context  *ctx;             /* retained */
    flux_glyph    *glyphs;
    size_t         count, cap;
};

/* ------------------------------------------------------------------ */
/*  flux_gradient                                                     */
/* ------------------------------------------------------------------ */

#define FLUX_MAX_GRADIENT_STOPS 16u

struct flux_gradient {
    atomic_int            ref_count;
    flux_context         *ctx;       /* retained */
    uint32_t              mode;      /* 0 = linear, 1 = radial */
    flux_gradient_extend  extend;
    float                 start[2];  /* radial: center.xy */
    float                 end[2];    /* radial: { radius, 0 } */
    float                 colors[FLUX_MAX_GRADIENT_STOPS][4];  /* normalised RGBA */
    float                 stops[FLUX_MAX_GRADIENT_STOPS];
    uint32_t              stop_count;
};

/* ------------------------------------------------------------------ */
/*  flux_image                                                        */
/* ------------------------------------------------------------------ */

struct flux_image {
    atomic_int       ref_count;
    flux_context    *ctx;            /* retained */
    flux_surface    *surface;        /* retained if non-NULL */
    flux_image_desc  desc;
    void            *data;           /* allocator-allocated copy */
    size_t           data_size;
    flux_r_texture  *rtex;           /* backend-owned */
};

/* ------------------------------------------------------------------ */
/*  RHI constructors                                                  */
/* ------------------------------------------------------------------ */

#ifndef FLUX_NO_VULKAN
flux_rhi_device *flux_rhi_create_vulkan(const flux_vulkan_device *device,
                                        VkSurfaceKHR surface,
                                        int32_t w, int32_t h);
#endif

flux_rhi_device *flux_rhi_create_software(uint32_t w, uint32_t h);

/* ------------------------------------------------------------------ */
/*  Engine                                                            */
/* ------------------------------------------------------------------ */

flux_result flux_engine_execute(flux_canvas *canvas, flux_rhi_device *r);

/* ------------------------------------------------------------------ */
/*  Canvas helpers                                                    */
/* ------------------------------------------------------------------ */

void flux_canvas_init    (flux_canvas *c, flux_surface *owner);
void flux_canvas_reset   (flux_canvas *c);
void flux_canvas_dispose (flux_canvas *c);
bool flux_canvas_push_op (flux_canvas *c, const flux_op *op);

/* ------------------------------------------------------------------ */
/*  Misc                                                              */
/* ------------------------------------------------------------------ */

uint32_t flux_pixel_format_bytes(flux_pixel_format fmt);

/* Stroke style snapshot consumed by the geometry stroker. */
typedef struct flux_stroke_style {
    flux_line_cap  line_cap;
    flux_line_join line_join;
    float          stroke_width;
    float          miter_limit;
} flux_stroke_style;

#endif /* FLUX_INTERNAL_H */
