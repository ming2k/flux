/*
 * Display-list op definitions consumed by engine.c.
 *
 * Recorded at flux_canvas_* call time and replayed by the execution
 * engine during flux_surface_present.
 */
#ifndef FLUX_STATE_H
#define FLUX_STATE_H

#include "flux/flux.h"
#include <stdbool.h>
#include <stddef.h>

/* Forward decl: the actual struct lives in internal.h. */
struct flux_paint_state;
typedef struct flux_paint_state flux_paint_state;

typedef enum flux_op_kind {
    FLUX_OP_FILL_RECT   = 0,
    FLUX_OP_FILL_PATH   = 1,
    FLUX_OP_STROKE_PATH = 2,
    FLUX_OP_DRAW_IMAGE  = 3,
    FLUX_OP_DRAW_GLYPHS = 4,
    FLUX_OP_CLIP_RECT   = 5,
    FLUX_OP_CLIP_PATH   = 6,
    FLUX_OP_RESET_CLIP  = 7,
    FLUX_OP_APPLY_BLUR  = 8,
} flux_op_kind;

typedef struct {
    flux_rect  rect;
    flux_color color;
} flux_fill_rect_op;

/* Paths in path-bearing ops are *borrowed* from the caller unless
 * `owns_path` is true, in which case the canvas was forced to construct
 * a transformed copy and owns it (release on canvas reset). */

typedef struct {
    const flux_path        *path;
    struct flux_paint_state paint;
    bool                    owns_path;
} flux_fill_path_op;

typedef struct {
    const flux_path        *path;
    struct flux_paint_state paint;
    bool                    owns_path;
} flux_stroke_path_op;

typedef struct {
    const flux_path *path;
    bool             owns_path;
} flux_clip_path_op;

typedef struct {
    flux_rect rect;
} flux_clip_rect_op;

typedef struct {
    const flux_image *image;        /* borrowed */
    flux_rect         src;
    flux_rect         dst;
} flux_draw_image_op;

typedef struct {
    const flux_glyph_run   *run;    /* borrowed */
    float                   x, y;
    struct flux_paint_state paint;
} flux_draw_glyphs_op;

typedef struct {
    float sigma;
} flux_apply_blur_op;

typedef struct flux_op {
    flux_op_kind kind;
    union {
        flux_fill_rect_op   fill_rect;
        flux_fill_path_op   fill_path;
        flux_stroke_path_op stroke_path;
        flux_draw_image_op  draw_image;
        flux_draw_glyphs_op draw_glyphs;
        flux_clip_rect_op   clip_rect;
        flux_clip_path_op   clip_path;
        flux_apply_blur_op  apply_blur;
    } u;
} flux_op;

#endif /* FLUX_STATE_H */
