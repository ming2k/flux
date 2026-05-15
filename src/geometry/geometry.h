/*
 * Geometry layer: path construction (in path.c), bezier flattening,
 * tessellation, stroke expansion. Backend-independent: no Vulkan, no
 * pixel buffers — pure CPU geometry on user coordinates.
 *
 * The stroker takes a flux_stroke_style POD (defined in internal.h) so
 * that it never depends on the public flux_paint handle layout.
 */
#ifndef FLUX_GEOMETRY_H
#define FLUX_GEOMETRY_H

#include "flux/flux.h"
#include "math/arena.h"
#include "visibility.h"
#include <stdbool.h>
#include <stddef.h>

/* Forward decl for flux_stroke_style (defined in internal.h). */
struct flux_stroke_style;

/* ---- path introspection (path.c) ---- */

FLUX_INTERNAL bool   flux_path_has_multiple_subpaths(const flux_path *path);
FLUX_INTERNAL size_t flux_path_subpath_count(const flux_path *path);
FLUX_INTERNAL bool   flux_path_is_axis_aligned_rect(const flux_path *path,
                                                    flux_rect *out_rect);
FLUX_INTERNAL bool   flux_path_get_line_loop(const flux_path *path,
                                             const flux_point **out_points,
                                             size_t *out_count);

FLUX_INTERNAL flux_result flux_path_flatten_subpath(const flux_path *path,
                                                    size_t subpath_index,
                                                    float tolerance, flux_arena *arena,
                                                    flux_point **out_points,
                                                    size_t *out_count,
                                                    bool *out_closed);
FLUX_INTERNAL flux_result flux_path_flatten_polyline(const flux_path *path, float tolerance,
                                                     flux_arena *arena,
                                                     flux_point **out_points, size_t *out_count,
                                                     bool *out_closed);
FLUX_INTERNAL flux_result flux_path_flatten_line_loop(const flux_path *path, float tolerance,
                                                      flux_arena *arena,
                                                      flux_point **out_points, size_t *out_count);

/* ---- tessellation (tess.c) ---- */

FLUX_INTERNAL flux_result flux_tessellate_simple_polygon(const flux_point *points, size_t count,
                                                         flux_arena *arena,
                                                         flux_point **out_tris,
                                                         size_t *out_count);

/* ---- stroke (stroker.c) ---- */

FLUX_INTERNAL flux_result flux_stroke_polyline(const flux_point *points, size_t count, bool closed,
                                               const struct flux_stroke_style *style,
                                               flux_arena *arena,
                                               flux_point **out_tris, size_t *out_count);

#endif /* FLUX_GEOMETRY_H */
