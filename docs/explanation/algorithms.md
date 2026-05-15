# Algorithms

This document explains the core algorithms flux uses to turn 2D drawing commands into pixels. Each section links to the source file that implements it.

## Bézier flattening

**Source:** `src/geometry/path.c`

Curves are flattened to line segments before tessellation because GPUs understand straight edges, not curves.

- **Quadratic Bézier:** Recursively split at the midpoint until every segment deviates from the true curve by less than `0.25 px` in device space.
- **Cubic Bézier:** Same recursive subdivision, but the deviation test checks both control points against the chord.

The flattening is **scale-aware:** the tolerance is measured in screen pixels after the current transform matrix is applied. A curve zoomed to 10× gets ten times more segments than the same curve at 1×, ensuring smoothness at any scale.

```c
/* Pseudocode of the quadratic flattener */
void flatten_quad(flux_point p0, flux_point p1, flux_point p2, float tol)
{
    flux_point mid = { (p0.x + p1.x) / 2, (p0.y + p1.y) / 2 };
    flux_point q = { (mid.x + (p1.x + p2.x) / 2) / 2,
                     (mid.y + (p1.y + p2.y) / 2) / 2 };
    float dev = distance_from_line(q, p0, p2);
    if (dev <= tol) {
        emit_line(p0, p2);
    } else {
        flatten_quad(p0, mid, q, tol);
        flatten_quad(q, (flux_point){(p1.x+p2.x)/2, (p1.y+p2.y)/2}, p2, tol);
    }
}
```

## Polygon tessellation

**Source:** `src/geometry/tess.c`

After flattening, paths are filled using ear clipping. The algorithm works on simple polygons (no self-intersections); the stroker and flattener guarantee this invariant.

1. Walk the polygon vertices.
2. At each vertex `i`, test whether the triangle `(i-1, i, i+1)` is an "ear" — i.e., it lies entirely inside the polygon and contains no other vertices.
3. If it is an ear, emit the triangle and remove vertex `i` from the ring.
4. Repeat until only three vertices remain.

Ear clipping is `O(n²)` in the worst case, but for typical UI paths (tens to hundreds of vertices) it is fast enough and produces correct results without complex data structures.

## Stencil-based path fill

**Source:** `src/rhi/software/software_rhi.c` (`sw_stencil_fill`)

For paths with holes or multiple subpaths (donut shapes), flux uses a **stencil buffer**:

1. Clear the stencil region to `0`.
2. Rasterise every triangle of the path with `GL_INCR` (or `+1` in software), but **do not write color**.
3. After all triangles are submitted, cover the path's bounding box with a full-screen quad that reads the stencil value and writes color only where the stencil is odd.

This implements the **even-odd fill rule:** every time a ray crosses a boundary, the stencil toggles. Regions inside an odd number of boundaries are filled; regions inside an even number (holes) are not.

The software backend keeps an `uint8_t` stencil buffer parallel to the color buffer. Values wrap at `255` (clamp), which is safe because no real path produces that many overlapping boundaries.

## Barycentric triangle rasterization

**Source:** `src/rhi/software/software_rhi.c` (`raster_image` and `raster_solid`)

The software renderer rasterises triangles using **edge functions** (a form of barycentric coordinates):

```
denom = cross(v1 - v0, v2 - v0)
w0    = cross(v1 - v2, p - v2) / denom
w1    = cross(v2 - v0, p - v0) / denom
w2    = 1 - w0 - w1
```

A pixel centre `p` is inside the triangle when `w0 >= 0`, `w1 >= 0`, and `w2 >= 0`.

**Why this works:** `w0`, `w1`, `w2` are the signed areas of the three sub-triangles formed by `p` and the edges. When all three are non-negative, `p` lies on the same side of every edge as the opposite vertex.

**Scanline optimization:** Instead of testing every pixel on the screen, the renderer computes the axis-aligned bounding box of the triangle and only tests pixels inside that box. This is cheap because the bounding box is just `min/max` of the three vertex coordinates.

## Image sampling (bilinear)

**Source:** `src/rhi/software/software_rhi.c` (`raster_image`)

When `draw_image` samples a texture, it uses bilinear filtering:

1. Convert the interpolated UV `(su, sv)` to texel coordinates:
   ```
   tu = su * texture_width  - 0.5f
   tv = sv * texture_height - 0.5f
   ```
2. Take the four neighbouring texels `(iu, iv)`, `(iu+1, iv)`, `(iu, iv+1)`, `(iu+1, iv+1)`.
3. Interpolate horizontally with fractional part `fu`, then vertically with `fv`.

The `-0.5f` offset shifts the sample grid so that UV `(0, 0)` maps to the centre of the top-left texel, not its corner. This prevents a half-texel drift when blitting images pixel-perfectly.

For `A8_UNORM` textures (the glyph atlas), the single alpha channel is replicated to RGB, then multiplied by the tint colour supplied to `draw_image`.

## Shelf packing (glyph atlas)

**Source:** `src/resource/text.c`

The glyph atlas packs small rectangles into a fixed 2048×2048 texture using a **shelf allocator**:

1. Maintain a current shelf with origin `(row_y)` and height `(row_h)`.
2. Place glyphs left-to-right along the shelf (`row_x`).
3. If a glyph does not fit in the remaining width, commit the shelf (`row_y += row_h + padding`) and start a new one.
4. Each glyph is surrounded by a 2-pixel padding to prevent bilinear filtering bleed.

```
Shelf 0: y=2,  h=16   [glyphA][glyphB][glyphC]
Shelf 1: y=20, h=24   [glyphD][glyphE]
Shelf 2: y=46, h=12   [glyphF]
```

The shelf allocator is `O(1)` per insertion and needs no auxiliary data structures. The trade-off is that a very tall glyph can waste the rest of its shelf. In practice, UI text glyphs have similar heights, so fragmentation is low.

When the atlas fills up, `flux_glyph_upload` returns `FLUX_ERROR_OUT_OF_MEMORY`. The caller decides whether to skip the glyph, rasterize smaller, or start fresh.

## Blend modes

**Source:** `src/rhi/software/software_rhi.c` (`blend_pixel`)

The software backend implements the full Porter-Duff set plus photographic modes. The core operation is:

```
out = src * f(src, dst) + dst * g(src, dst)
```

Where `f` and `g` are coefficients defined by the blend mode. For example:

| Mode | `f` (src coeff) | `g` (dst coeff) |
|---|---|---|
| `SRC_OVER` | `1` | `1 - src_alpha` |
| `MULTIPLY` | `dst` | `0` |
| `SCREEN` | `1 - dst` | `1` |
| `PLUS` | `1` | `1` |

All blending is done in 8-bit-per-channel unorm space with integer arithmetic to avoid rounding drift. The Vulkan backend currently hard-codes `SRC_OVER` for all pipelines; full blend mode support on GPU is future work.

## See also

- [Bézier curves](bezier-curves.md) — focused on curve mathematics.
- [Glyph atlas](glyph-atlas.md) — atlas design and GPU synchronization.
- [Software renderer](software-renderer.md) — how the CPU backend fits together.
- [Rendering](rendering.md) — high-level rendering architecture.
