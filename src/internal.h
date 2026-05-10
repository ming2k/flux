/*
 * Internal header — shared across all flux translation units.
 *
 * This header declares the types shared between the state layer
 * (recording), geometry layer (tessellation), and the execution
 * engine. Backend-specific types (Vulkan objects, etc.) live in
 * their respective renderer implementation files.
 */
#ifndef FX_INTERNAL_H
#define FX_INTERNAL_H

#include "flux/flux.h"
#include "renderer/renderer.h"
#include "state/state.h"
#include "geometry/geometry.h"
#include "math/matrix.h"
#include "math/rect.h"
#include "math/arena.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Logging                                                           */
/* ------------------------------------------------------------------ */

struct fx_context;

void fx_log_impl(const struct fx_context *ctx, fx_log_level lvl,
                 const char *file, int line,
                 const char *fmt, ...) __attribute__((format(printf, 5, 6)));

#define FX_LOGE(ctx, ...) fx_log_impl((ctx), FX_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define FX_LOGW(ctx, ...) fx_log_impl((ctx), FX_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define FX_LOGI(ctx, ...) fx_log_impl((ctx), FX_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#ifdef NDEBUG
#  define FX_LOGD(ctx, ...) ((void)0)
#  define FX_LOGT(ctx, ...) ((void)0)
#else
#  define FX_LOGD(ctx, ...) fx_log_impl((ctx), FX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#  define FX_LOGT(ctx, ...) fx_log_impl((ctx), FX_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#endif

/* ------------------------------------------------------------------ */
/*  fx_canvas — recording state (the public opaque type)             */
/* ------------------------------------------------------------------ */

struct fx_canvas {
    fx_surface *owner;
    fx_color    clear_color;
    bool        has_clear;
    fx_op      *ops;
    size_t      op_count;
    size_t      op_cap;

    fx_matrix  *state_stack;
    size_t      state_count;
    size_t      state_cap;
    fx_matrix   current_matrix;
    float       dpr;
};

/* ------------------------------------------------------------------ */
/*  fx_context — rendering context (global GPU state)                 */
/* ------------------------------------------------------------------ */

struct fx_context {
    fx_log_fn    log;
    void        *log_user;
    fx_log_level min_log_level;
    void        *backend_data;
};

/* ------------------------------------------------------------------ */
/*  fx_surface — render target + canvas                              */
/* ------------------------------------------------------------------ */

struct fx_surface {
    fx_context      *ctx;
    fx_renderer     *renderer;
    fx_canvas        canvas;
    uint32_t         frame_index;
    bool             is_offscreen;
    bool             needs_recreate;
};

/* ------------------------------------------------------------------ */
/*  fx_path / fx_image / fx_glyph_run / fx_gradient                  */
/* ------------------------------------------------------------------ */

struct fx_path {
    uint8_t  *verbs;
    size_t    verb_count;
    size_t    verb_cap;
    fx_point *points;
    size_t    point_count;
    size_t    point_cap;
    fx_rect   bounds;
    bool      has_bounds;
};

struct fx_glyph_run {
    fx_glyph *glyphs;
    size_t    count;
    size_t    cap;
};

struct fx_gradient {
    fx_context *ctx;
    uint32_t    mode;
    float       start[2];
    float       end[2];
    float       colors[4][4];
    float       stops[4];
    uint32_t    stop_count;
};

struct fx_image {
    fx_context    *ctx;
    fx_image_desc  desc;
    void          *data;
    size_t         data_size;
    /* Backend texture handle (set by renderer). */
    fx_r_texture  *rtex;
};

/* ------------------------------------------------------------------ */
/*  Renderer constructors (implemented in renderer/ subdirs)          */
/* ------------------------------------------------------------------ */

/* Create a Vulkan hardware-accelerated renderer. */
fx_renderer *fx_renderer_create_vulkan(fx_context *ctx,
                                       void *vk_surface, int32_t w, int32_t h);

/* Create a CPU software renderer. */
fx_renderer *fx_renderer_create_software(uint32_t w, uint32_t h);

/* ------------------------------------------------------------------ */
/*  Execution engine (engine.c)                                       */
/* ------------------------------------------------------------------ */

/* Execute all recorded ops on the canvas through the renderer. */
size_t fx_engine_execute(fx_canvas *canvas, fx_renderer *r);

/* ------------------------------------------------------------------ */
/*  Canvas helpers (state/canvas.c)                                   */
/* ------------------------------------------------------------------ */

void  fx_canvas_reset(fx_canvas *c);
void  fx_canvas_dispose(fx_canvas *c);
bool  fx_canvas_push_op(fx_canvas *c, const fx_op *op);

/* ------------------------------------------------------------------ */
/*  Image / text internals                                            */
/* ------------------------------------------------------------------ */

/* Backend-independent pixel format conversion (for Vulkan). */
uint32_t fx_pixel_format_bytes(fx_pixel_format fmt);

#endif /* FX_INTERNAL_H */
