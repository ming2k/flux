# Bézier Curves

This document explains quadratic and cubic Bézier curves, why flux uses them, and how they are converted into line segments for GPU rendering.

## What Bézier curves are

A Bézier curve is a parametric curve defined by control points. The curve interpolates the first and last points, while the interior points "pull" the curve without necessarily lying on it.

### Quadratic Bézier (one control point)

Three points: start **P0**, control **P1**, end **P2**.

```
P0
  \
   \
    P1  (control point, off the curve)
     \
      \
       P2
```

The curve at parameter `t` (0 ≤ t ≤ 1):

```
B(t) = (1 - t)² P0 + 2(1 - t)t P1 + t² P2
```

At `t = 0`: the curve is at P0.
At `t = 1`: the curve is at P2.
At `t = 0.5`: the curve is closest to P1 but does not pass through it.

### Cubic Bézier (two control points)

Four points: start **P0**, control **P1**, control **P2**, end **P3**.

```
P0
  \
   P1
    \
     \
      P2
       \
        P3
```

The curve at parameter `t`:

```
B(t) = (1 - t)³ P0 + 3(1 - t)²t P1 + 3(1 - t)t² P2 + t³ P3
```

Two control points allow an inflection point: the curve can change direction without the start and end tangents being parallel. This is essential for matching the expressive range of PostScript and TrueType outlines.

## Why flux uses Bézier curves

1. **Industry standard** — PostScript, PDF, SVG, TrueType, and OpenType all represent outlines as Bézier curves. Using anything else would force callers to convert upstream.

2. **Compact representation** — A smooth arc needs many line segments but only one cubic Bézier. A circle approximated with cubics needs four curves; with lines it needs dozens.

3. **Affine invariant** — Rotating, scaling, or translating the control points produces the same result as transforming the curve. This matches flux's 2D matrix transform model.

## How flux represents curves

Paths in flux are verb/point streams:

| Verb | Points | Meaning |
|---|---|---|
| `MOVE` | 1 | Move pen to (x, y) |
| `LINE` | 1 | Draw line to (x, y) |
| `QUAD` | 2 | Quadratic Bézier to (x, y) with control (cx, cy) |
| `CUBIC` | 3 | Cubic Bézier to (x, y) with controls (cx0, cy0) and (cx1, cy1) |
| `CLOSE` | 0 | Close current subpath |

```c
fx_path *path = fx_path_create();
fx_path_move_to(path, 10.0f, 10.0f);
fx_path_cubic_to(path, 20.0f, 50.0f,  // P1
                        80.0f, 50.0f,  // P2
                        90.0f, 10.0f); // P3
```

## Flattening: curves to lines

GPUs draw triangles, not curves. Before a path reaches the GPU, every curve is converted into a sequence of line segments. This process is called **flattening**.

### The algorithm

flux uses recursive midpoint subdivision (de Casteljau's algorithm):

1. Compute the midpoint of each control-point pair.
2. Use those midpoints to compute a new midpoint — the point on the curve at `t = 0.5`.
3. This splits one curve into two curves, each with half the parameter range.
4. Recurse until the control points are "flat enough" — meaning the control point is very close to the line connecting the endpoints.

```
Original curve:          P0 ---- P1 ---- P2 ---- P3
                                    |
                                    v
Split at midpoint:       P0 -- P01 -- P012 -- P0123
                                  /              \
                         P0123 -- P123 -- P23 -- P3
```

### Stopping condition

The recursion stops when either:

1. **Max depth** — 16 levels of subdivision (65,536 segments worst case, never reached in practice).
2. **Flatness** — The perpendicular distance from the control point(s) to the line connecting the endpoints is below a tolerance.

For a cubic curve, both inner control points must be within tolerance of the endpoint line. For a quadratic, only the single control point is checked.

```c
// In src/geometry/path.c
if (depth >= FX_FLATTEN_MAX_DEPTH ||
    (point_line_distance_sq(p1, p0, p3) <= tol_sq &&
     point_line_distance_sq(p2, p0, p3) <= tol_sq)) {
    return append_flat_point(points, count, cap, p3);
}
```

### Tolerance

The tolerance is fixed at **0.25 pixels** in device space. This means:

- The flattened polyline never deviates more than 0.25 pixels from the true curve.
- At high DPR (Retina displays), the tolerance scales with the transform, so curves stay smooth.
- The number of segments depends on the curve's curvature: a nearly-straight curve gets 2 segments; a tight loop gets 32+.

### Performance

- **CPU cost** — Flattening happens during `fx_surface_present`, not during recording. The canvas stores only the control points.
- **Memory** — Flattened points are written into a per-frame arena allocator. They are discarded after the frame is rendered.
- **Quality vs speed trade-off** — 0.25px is a conservative default. A smaller tolerance would produce more segments (slower, smoother); a larger tolerance would produce fewer (faster, blockier).

## Arc approximation

SVG elliptical arcs are not native Bézier curves. flux approximates them as a sequence of cubic Bézier segments using the parameterization from the SVG 1.1 specification. Each arc segment is then flattened like any other cubic.

## See also

- [Rendering](rendering.md) — how flattened paths become GPU triangles.
- [How to draw basic shapes](../how-to/draw-basic-shapes.md) — API usage examples.
- `src/geometry/path.c` — flattening implementation (`flatten_quad_recursive`, `flatten_cubic_recursive`).
