# flux

Low-level 2D graphics foundation on Vulkan. C11, meson, Wayland.

Status: **v0.0.2 production-preview** — Full 2D vector primitives,
gradients, clipping, batched Vulkan execution, alpha-blended text via
dynamic GPU atlases, and a unified upload path. Not yet a stable 1.0:
see [docs/release-readiness.md](docs/release-readiness.md) for the
tracked production gaps and [CHANGELOG.md](CHANGELOG.md) for what
changed in this release.

## Features

- **Vector Primitives:** Concave path filling and stroking with SVG-grade caps and joins. (flux renders paths; it does not parse SVG documents.)
- **Transformation:** Full 3×3 affine matrix stack with CPU-side resolution-independent flattening.
- **Text Rendering:** FreeType glyph rasterization and HarfBuzz-ready glyph runs.
- **Dynamic Atlas:** 2048×2048 single-channel alpha glyph cache with shelf-packing and eviction-on-overflow.
- **Vulkan Backend:** Automatic draw call batching, per-frame dynamic ring buffers, and a unified GPU upload path (persistent staging, reusable command buffer and fence).
- **Images and Gradients:** GPU-resident image uploads; linear and radial gradients with up to 4 stops.
- **Clipping:** Rectangular clips via scissor; path clips via a scissor bound to the path's bounding box.
- **Offscreen Rendering:** Headless render targets with persistent readback staging for tests and thumbnails.

For a practical capability model and application-level examples, see
`docs/usage/`.

## Dependencies

- Vulkan 1.2+
- FreeType 2
- HarfBuzz
- Wayland (optional)

## Build

    meson setup build
    ninja -C build

Options:
- `-Dwayland=auto|enabled|disabled`
- `-Dexamples=true|false`
- `-Dvalidation=auto|enabled|disabled`

## Run the Phase-0 smoke test

    WAYLAND_DISPLAY=$WAYLAND_DISPLAY ./build/examples/hello_rect

Opens an xdg-toplevel and clears it to a slowly sweeping color.
The demo records a concave filled and stroked polygon plus a filled and
stroked rectangular path so the current solid-color geometry execution
paths are exercised. Close the window or Ctrl-C to exit. Set
`FX_ENABLE_VALIDATION=1` to turn on the Vulkan validation layer.

## Layout

    include/flux/*.h   — public API headers
    src/               — implementation
    src/vk/            — Vulkan backend
    examples/          — runnable demos
    docs/              — design, API, roadmap, responsibility boundaries
