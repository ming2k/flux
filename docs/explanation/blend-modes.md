# Blend Modes

Blend modes control how a source pixel (the thing being drawn) composites with the destination pixel (what is already in the framebuffer). flux provides the full Porter-Duff set plus a handful of separable photographic modes. This document explains what each mode does, how to select one, and the important differences between backends.

## The thirteen modes

```c
typedef enum flux_blend_mode {
    FLUX_BLEND_SRC_OVER  = 0,   /* default */
    FLUX_BLEND_DST_OVER  = 1,
    FLUX_BLEND_SRC_IN    = 2,
    FLUX_BLEND_DST_IN    = 3,
    FLUX_BLEND_SRC_OUT   = 4,
    FLUX_BLEND_DST_OUT   = 5,
    FLUX_BLEND_SRC_ATOP  = 6,
    FLUX_BLEND_DST_ATOP  = 7,
    FLUX_BLEND_XOR       = 8,
    FLUX_BLEND_PLUS      = 9,
    FLUX_BLEND_MULTIPLY  = 10,
    FLUX_BLEND_SCREEN    = 11,
    FLUX_BLEND_OVERLAY   = 12,
} flux_blend_mode;
```

The default for every `flux_paint` is `FLUX_BLEND_SRC_OVER`. If you never call `flux_paint_set_blend_mode`, every draw uses SRC_OVER.

## Setting a blend mode

Blend mode is paint state, recorded at canvas op time:

```c
flux_paint *p = NULL;
flux_paint_create(ctx, &p);
flux_paint_set_blend_mode(p, FLUX_BLEND_MULTIPLY);
flux_canvas_fill_path(c, path, p);
```

`FLUX_OP_FILL_RECT` is an exception: it always uses `FLUX_BLEND_SRC_OVER` regardless of the paint's blend mode. Every other operation—`fill_path`, `stroke_path`, and `draw_glyph_run`—respects the paint.

## Porter-Duff compositing (modes 0–8)

Porter-Duff modes are defined by alpha algebra. Let `sa` be source alpha and `da` be destination alpha (both in 0..255). The result alpha `ra` and result color `rc` are:

| Mode | `Fa` (source coefficient) | `Fb` (dest coefficient) | Intuition |
|---|---|---|---|
| **SRC_OVER** | 1 | 1 − sa | Source paints over destination; the default. |
| **DST_OVER** | 1 − da | 1 | Destination paints over source. |
| **SRC_IN** | da | 0 | Keep source only where destination is opaque. |
| **DST_IN** | 0 | sa | Keep destination only where source is opaque. |
| **SRC_OUT** | 1 − da | 0 | Keep source only where destination is transparent. |
| **DST_OUT** | 0 | 1 − sa | Keep destination only where source is transparent. |
| **SRC_ATOP** | da | 1 − sa | Source inside destination, keep rest of destination. |
| **DST_ATOP** | 1 − da | sa | Destination inside source, keep rest of source. |
| **XOR** | 1 − da | 1 − sa | Source outside destination plus destination outside source. |

The software backend computes these with integer arithmetic (`/255`) per channel. Color channels are **not** premultiplied for the Porter-Duff set; the alpha terms act directly as weighting coefficients.

## Separable modes (modes 9–12)

These modes operate on the color channels with a blend function `B(s, d)` and then compose the result with the standard SRC_OVER alpha formula:

```
rc = sc × (1 − da) + dc × (1 − sa) + B(sc, dc) × sa × da
ra = sa + da × (1 − sa)
```

Where `sc` and `dc` are the *unpremultiplied* source and destination colors (0..1). The software renderer first unpremultiplies the stored pixel values, applies `B`, then repremultiplies before writing back.

| Mode | `B(sc, dc)` | Use case |
|---|---|---|
| **PLUS** | `sc + dc` (clamped) | Additive light effects, glows. |
| **MULTIPLY** | `sc × dc` | Darkening, shadows, ink simulation. |
| **SCREEN** | `sc + dc − sc × dc` | Lightening, highlights. |
| **OVERLAY** | `2×sc×dc` if `dc < 0.5`, else `1 − 2×(1−sc)×(1−dc)` | Contrast boost, photographic effects. |

Because these modes require floating-point unpremultiplication, they are slower than the pure Porter-Duff modes in the software backend.

## Backend coverage

Not every backend implements every mode.

| Backend | Porter-Duff (0–8) | Separable (9–12) | Notes |
|---|---|---|---|
| **Software** | ✅ All | ✅ All | `SRC_OVER` has a fast path for `sa==0` and `sa==255`. |
| **Vulkan** | ⚠️ SRC_OVER only | ❌ None | All pipelines are hard-coded to `ONE / ONE_MINUS_SRC_ALPHA`. `vk_blend_mode` is currently a no-op. |

### What happens on Vulkan when you request another mode?

The engine still calls `vt(r)->set_blend_mode(r, mode)`, but the Vulkan backend ignores the argument. The draw proceeds with `SRC_OVER`. This is a known gap: adding dynamic blend-mode support to Vulkan requires either per-mode pipeline variants or subpass blending state that is not yet implemented.

If your application needs `MULTIPLY` or `XOR` on the GPU, use the software backend for offscreen rendering and read the pixels back, or pre-compose the layers in software before uploading as an image.

## How blending happens in the software renderer

The software renderer batches sequential draws that share the same color, buffer, and blend mode. When `sw_blend_mode` changes, the current batch is flushed and rasterised with the old mode; the next batch picks up the new mode.

Per-pixel blending lives in `blend_pixel()` (`src/rhi/software/software_rhi.c`):

1. **SRC_OVER fast path** — If source alpha is `0`, the function returns immediately. If source alpha is `255`, it overwrites the destination RGB and sets A to `255`. No multiplies, no divides.
2. **Porter-Duff general path** — Integer math with `/255` scaling. The formulas match the table above exactly.
3. **Separable path** — Convert to float, unpremultiply, apply `B()`, repremultiply, convert back to `uint8_t`.

The batch system means that switching blend modes frequently can reduce batch size and increase CPU rasterisation overhead. For best performance, group draws by blend mode:

```c
/* all SRC_OVER draws first */
flux_paint_set_blend_mode(p, FLUX_BLEND_SRC_OVER);
flux_canvas_fill_path(c, bg_path, p);
flux_canvas_fill_path(c, fg_path, p);

/* then the multiply layer */
flux_paint_set_blend_mode(p, FLUX_BLEND_MULTIPLY);
flux_canvas_fill_path(c, shadow_path, p);
```

## Blend mode and the stencil pipeline

Stencil operations (path fill setup, clip paths) do **not** use the paint blend mode. The stencil increment and cover passes use fixed-function blending configured for the task at hand:

- **Stencil increment** — blending is disabled entirely (`blendEnable = VK_FALSE` on Vulkan, equivalent write-through in software).
- **Cover pass** — always uses `SRC_OVER` so the final color composites correctly over the background.

This is why `flux_paint_set_blend_mode` has no effect during the hidden stencil phase of a path fill.

## Common recipes

| Effect | Blend mode | Notes |
|---|---|---|
| Standard UI rendering | `SRC_OVER` | Default. Correct for text, rectangles, and images on top of a background. |
| Drop shadow | `SRC_OVER` with translucent black paint | No special mode needed; alpha does the work. |
| Photo multiply / darken | `MULTIPLY` | Software backend only. Darkens wherever both layers are dark. |
| Photo screen / lighten | `SCREEN` | Software backend only. Lightens wherever either layer is light. |
| Additive glow | `PLUS` | Software backend only. Clamped at white. Good for particle effects. |
| Masking / clipping | `SRC_IN` or `DST_IN` | Use `SRC_IN` when the mask is the destination and the content is the source. |
| Knock-out | `DST_OUT` | Erase the destination wherever the source is opaque. |

## See also

- [Software renderer](software-renderer.md) — CPU rasterisation details.
- [Vulkan backend](vulkan-backend.md) — pipeline and blending state.
- [Rendering](rendering.md) — how the engine issues draw calls.
