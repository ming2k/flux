/*
 * State layer: canvas recording, transform stack, paint management.
 *
 * These modules are the "recording" half of the two-phase architecture.
 * They build display lists and manage graphics state but do not render
 * anything — that is the renderer's job at present time.
 */
#ifndef FX_STATE_H
#define FX_STATE_H

#include "flux/flux.h"
#include <stdbool.h>
#include <stddef.h>

/* ---- op kinds recorded in the display list ---- */

typedef enum {
    FX_OP_FILL_RECT = 0,
    FX_OP_FILL_PATH = 1,
    FX_OP_STROKE_PATH = 2,
    FX_OP_DRAW_IMAGE = 3,
    FX_OP_DRAW_GLYPHS = 4,
    FX_OP_CLIP_RECT = 5,
    FX_OP_RESET_CLIP = 6,
    FX_OP_CLIP_PATH = 7,
    FX_OP_MASK_BLUR = 8,
} fx_op_kind;

typedef struct {
    const fx_path   *path;
    fx_paint         paint;
    bool             owns_path;
} fx_fill_path_op;

typedef struct {
    const fx_path   *path;
    fx_paint         paint;
    bool             owns_path;
} fx_stroke_path_op;

typedef struct {
    const fx_image  *image;
    fx_rect          src;
    fx_rect          dst;
} fx_draw_image_op;

typedef struct {
    const fx_glyph_run *run;
    float               x;
    float               y;
    fx_paint            paint;
} fx_draw_glyphs_op;

typedef struct {
    fx_rect  rect;
    fx_color color;
} fx_fill_rect_op;

typedef struct {
    fx_rect rect;
} fx_clip_rect_op;

typedef struct {
    float sigma;
} fx_mask_blur_op;

typedef struct {
    fx_op_kind kind;
    union {
        fx_fill_rect_op    fill_rect;
        fx_fill_path_op    fill_path;
        fx_stroke_path_op  stroke_path;
        fx_draw_image_op   draw_image;
        fx_draw_glyphs_op  draw_glyphs;
        fx_clip_rect_op    clip_rect;
        fx_fill_path_op    clip_path;
        fx_mask_blur_op    mask_blur;
    } u;
} fx_op;

/* ---- canvas (display list + transform stack) ---- */

/* struct fx_canvas is defined in internal.h (public opaque type).
 * These helpers operate on it. */

/* Append an op to the display list. */
bool  fx_canvas_push_op(fx_canvas *c, const fx_op *op);

/* Reset the canvas for a new frame (frees owned paths, clears ops). */
void  fx_canvas_reset(fx_canvas *c);
void  fx_canvas_dispose(fx_canvas *c);

#endif /* FX_STATE_H */
