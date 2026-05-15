# Pixel Formats

flux surfaces, images, and colors are all expressed in terms of a small set of pixel formats. This document explains what each format means, where it appears, how the two backends interpret them, and the premultiplied-alpha convention that runs through the entire library.

## The three formats

```c
typedef enum flux_pixel_format {
    FLUX_FMT_BGRA8_UNORM = 0,
    FLUX_FMT_RGBA8_UNORM = 1,
    FLUX_FMT_A8_UNORM    = 2,
} flux_pixel_format;
```

| Format | Bytes / pixel | Channels | Typical use |
|---|---|---|---|
| **BGRA8_UNORM** | 4 | B, G, R, A | Vulkan swapchain default; Windows DIB compatibility. |
| **RGBA8_UNORM** | 4 | R, G, B, A | Cross-platform default; software framebuffer internal layout. |
| **A8_UNORM** | 1 | A only | Glyph atlas, masks, alpha-only textures. |

All three are **UNORM** — unsigned normalized — meaning each 8-bit channel is interpreted as `value / 255.0f` in shader or blending arithmetic.

## Premultiplied alpha

Every `flux_color` value is **premultiplied alpha** in `0xAARRGGBB` layout:

```
MSB                             LSB
+--------+--------+--------+--------+
|  Alpha |   Red  |  Green |  Blue  |
| 8 bits | 8 bits | 8 bits | 8 bits |
+--------+--------+--------+--------+
```

The helpers in `<flux/flux.h>` enforce this convention:

- `flux_color_rgba(r, g, b, a)` — accepts **non-premultiplied** components and premultiplies them for you.
- `flux_color_rgba_premul(r, g, b, a)` — accepts components that are **already** premultiplied (caller guarantees `r,g,b ≤ a`).
- `flux_color_unpack(c, &r, &g, &b, &a)` — returns **non-premultiplied** components by undoing the multiplication.

### Why premultiplied?

Premultiplied alpha makes compositing mathematically correct and cheaper:

- **SRC_OVER blending** becomes a simple lerp: `dst = src + dst × (1 − src_alpha)`.
- **Texture filtering** does not darken edges. Bilinear interpolation of premultiplied pixels preserves color correctness; doing the same with straight-alpha creates dark halos.
- **Additive modes** like `FLUX_BLEND_PLUS` only make sense when the source color is already scaled by alpha.

If you construct a `flux_color` by hand with bit-shifts, make sure the RGB channels are premultiplied. `flux_color_rgba(255, 0, 0, 128)` gives you a 50 % transparent red with premultiplied channels `(128, 0, 0, 128)`, not `(255, 0, 0, 128)`.

## Surfaces and formats

### Offscreen surfaces

`flux_surface_create_offscreen` lets you choose the format:

```c
flux_surface_create_offscreen(ctx, 800, 600,
                               FLUX_FMT_RGBA8_UNORM,
                               FLUX_CS_SRGB, &surface);
```

The format is stored on the surface handle and reported by `flux_surface_get_format`. However, the **software backend always renders into an internal RGBA8 framebuffer**, regardless of the format you requested. The format tag is preserved for API consistency but does not change the memory layout of `read_pixels` output.

### Vulkan surfaces

`flux_surface_create_vulkan` ignores the caller's format preference and always uses `FLUX_FMT_BGRA8_UNORM`. The Vulkan swapchain code queries the surface for supported formats and prefers `VK_FORMAT_B8G8R8A8_UNORM`; if unavailable it falls back to the first reported format.

## Images and stride

`flux_image_desc` carries both a `format` and a `stride`:

```c
flux_image_desc desc = {
    .size   = sizeof(desc),
    .width  = 256,
    .height = 256,
    .format = FLUX_FMT_BGRA8_UNORM,
    .stride = 256 * 4,   /* bytes per row; 0 means packed */
    .data   = pixel_buf,
};
```

Rules:

- **Stride** is in bytes, not pixels. `stride == 0` tells flux to compute `width × bpp` automatically.
- **Padding rows** are allowed. Stride can be larger than `width × bpp` (e.g. for row-aligned DIBs).
- **CPU-side copy.** `flux_image_create` always deep-copies `data` through the context allocator. The pointer you pass remains yours.
- **Sub-region updates.** `flux_image_update_region` copies `(w × h)` pixels from the source buffer at the given stride into the image at `(x, y)`. The source stride and the image's internal stride may differ.

## Backend mapping

### Vulkan

`vk_pixel_format_to_vk` maps flux formats to Vulkan formats:

| flux | Vulkan | Notes |
|---|---|---|
| `FLUX_FMT_BGRA8_UNORM` | `VK_FORMAT_B8G8R8A8_UNORM` | Swapchain default. |
| `FLUX_FMT_RGBA8_UNORM` | `VK_FORMAT_R8G8B8A8_UNORM` | |
| `FLUX_FMT_A8_UNORM` | `VK_FORMAT_R8_UNORM` | Single-channel red; shader samples `.r` as alpha. |

All color pipelines use the same blend state: `ONE / ONE_MINUS_SRC_ALPHA` with premultiplied source. This is the hardware-efficient expression of SRC_OVER.

### Software renderer

The software renderer keeps everything in a CPU-side RGBA8 framebuffer (`uint8_t pixels[]` row-major). When it samples textures it swizzles on the fly:

| Texture format | Sampling behaviour |
|---|---|
| `FLUX_FMT_RGBA8_UNORM` | Read `pixels[(y×stride) + x×4 + 0..3]` directly as R, G, B, A. |
| `FLUX_FMT_BGRA8_UNORM` | Read the same layout, then swizzle: `B←p[0], G←p[1], R←p[2], A←p[3]`. |
| `FLUX_FMT_A8_UNORM` | Read `pixels[(y×stride) + x]` as a single byte; replicate to R=G=B=A. |

The bilinear filter operates on the unswizzled `uint8_t` values, then applies the tint color (from `flux_color`) in premultiplied space.

Because the software framebuffer is always RGBA8, `flux_surface_read_pixels` on an offscreen surface returns RGBA8 data with a stride of `width × 4`, even if the surface was created with `FLUX_FMT_BGRA8_UNORM`.

## Reading pixels back

```c
uint8_t *buf = malloc(width * height * 4);
flux_surface_read_pixels(surface, buf, width * 4);
```

- **Software backend** — copies directly from the internal framebuffer. Fast, synchronous.
- **Vulkan backend** — performs a GPU→CPU transfer through a staging buffer, then copies into your pointer. Slower; intended for screenshots and test verification, not per-frame use.

If you pass `stride == 0` to `flux_surface_read_pixels`, the backend uses its natural stride (`width × 4`). Passing an explicit stride lets you read into a larger buffer with row padding.

## Format size helper

`flux_pixel_format_bytes` returns the bytes-per-pixel:

```c
uint32_t bpp = flux_pixel_format_bytes(FLUX_FMT_A8_UNORM);     /* 1 */
uint32_t bpp = flux_pixel_format_bytes(FLUX_FMT_RGBA8_UNORM);  /* 4 */
```

This is the authoritative way to compute buffer sizes and strides. Do not hard-code `4`; future formats may change the size.

## Common pitfalls

| Mistake | Why it hurts | Fix |
|---|---|---|
| **Hand-packing straight-alpha into `flux_color`** | `0x80FF0000` is not 50 % red; it is fully red with 50 % alpha, but flux treats it as premultiplied and will darken it further. | Use `flux_color_rgba(255, 0, 0, 128)`. |
| **Assuming BGRA8 surface reads back as BGRA8** | Software renderer always returns RGBA8 from `read_pixels`. | Check `flux_surface_get_format` only for metadata; inspect actual bytes when swapping channels. |
| **Zero stride on non-packed image data** | `flux_image_create` will compute `width × bpp`, which may skip your row padding. | Set `stride` to the real bytes-per-row. |
| **A8 texture with RGB tint expectations** | A8 textures replicate alpha to all channels. Tinting with a red paint gives red-tinted alpha, not red pixels. | This is correct for glyph rendering; use RGBA8 images for colored content. |

## See also

- [Software renderer](software-renderer.md) — CPU rasterisation and texture sampling details.
- [Vulkan backend](vulkan-backend.md) — swapchain format selection and pipeline blend state.
- [Glyph atlas](glyph-atlas.md) — where `A8_UNORM` is used in practice.
