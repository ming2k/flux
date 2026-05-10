/*
 * Geometry layer: path construction, bezier flattening, tessellation, stroke expansion.
 *
 * All geometry is expressed in device-independent user coordinates.
 * These modules have no dependency on any rendering backend.
 */
#ifndef FX_GEOMETRY_H
#define FX_GEOMETRY_H

#include "flux/flux.h"
#include "math/arena.h"
#include <stdbool.h>
#include <stddef.h>

/* ---- path introspection (from path.c) ---- */

bool   fx_path_has_multiple_subpaths(const fx_path *path);
size_t fx_path_subpath_count(const fx_path *path);
bool   fx_path_is_axis_aligned_rect(const fx_path *path, fx_rect *out_rect);
bool   fx_path_get_line_loop(const fx_path *path,
                             const fx_point **out_points,
                             size_t *out_count);

/* Flatten a subpath to a polyline (beziers → line segments). */
bool   fx_path_flatten_subpath(const fx_path *path, size_t subpath_index,
                               float tolerance, fx_arena *arena,
                               fx_point **out_points, size_t *out_count,
                               bool *out_closed);
bool   fx_path_flatten_polyline(const fx_path *path, float tolerance,
                                fx_arena *arena,
                                fx_point **out_points, size_t *out_count,
                                bool *out_closed);
bool   fx_path_flatten_line_loop(const fx_path *path, float tolerance,
                                 fx_arena *arena,
                                 fx_point **out_points, size_t *out_count);

/* ---- tessellation (from tess.c) ---- */

/* Ear-clipping triangulation of a simple (non-self-intersecting) polygon. */
bool   fx_tessellate_simple_polygon(const fx_point *points, size_t count,
                                    fx_arena *arena,
                                    fx_point **out_tris, size_t *out_count);

/* ---- stroke (from stroker.c) ---- */

/* Expand a polyline into a triangle strip with caps and joins. */
bool   fx_stroke_polyline(const fx_point *points, size_t count, bool closed,
                          const fx_paint *paint, fx_arena *arena,
                          fx_point **out_tris, size_t *out_count);

#endif /* FX_GEOMETRY_H */
