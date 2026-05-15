# Changelog

All notable changes to flux are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [semantic versioning](https://semver.org/spec/v2.0.0.html).

Dates are ISO 8601. Pre-1.0 minor releases may introduce breaking
changes; the breaking commit is called out explicitly.

## [0.2.1] — 2026-05-15

### Added

- **Software backend: all 13 blend modes.** `blend_pixel()` now implements
  the full Porter-Duff set (`SRC_OVER`, `DST_OVER`, `SRC_IN`, `DST_IN`,
  `SRC_OUT`, `DST_OUT`, `SRC_ATOP`, `DST_ATOP`, `XOR`) plus separable
  modes (`PLUS`, `MULTIPLY`, `SCREEN`, `OVERLAY`). `SRC_OVER` retains its
  fast path.
- **Software backend: BGRA8 and A8 texture formats.** `sw_texture_alloc`
  now computes correct stride per format; `raster_image` performs channel
  swizzling (BGRA8) or alpha-only sampling (A8) as appropriate.
- **Vulkan: asynchronous texture upload via staging buffer pool.**
  `upload_texture_data()` no longer creates temporary buffers, command
  buffers, and fences for every upload. A ring of reusable staging buffers
  feeds a dedicated transfer command buffer; the CPU returns immediately
  and buffers are reclaimed when the GPU fence signals.
- **Vulkan: persistent pipeline cache.** `VkPipelineCache` is loaded from
  `~/.cache/flux/pipeline_cache.bin` at startup and saved at shutdown.
  Swapchain resize no longer recompiles SPIR-V from scratch.

### Fixed

- `FLUX_OP_FILL_RECT` now respects the current blend mode set by
  `flux_paint_set_blend_mode` via the RHI vtable.

## [0.2.0] — 2026-05-15

**Stabilisation release.** This release fixes all known crash and
corruption bugs identified in the 0.1.x series. The public API surface
is intentionally kept identical to the last 0.1.x snapshot; no breaking
changes are introduced.

### Fixed

- **Double submit in `flux_surface_present`.** Removed the redundant
  `submit()` call that caused illegal Vulkan command buffer usage.
- **Dash subdivision buffer overflow.** `subdivide_dash` now computes
  a safe upper bound based on total path length and minimum dash period.
- **Self-intersecting path fill.** Removed the tessellation fast-path
  for single-subpath polygons; all non-rect paths now use stencil-based
  fill which correctly handles self-intersection.
- **Clip path ordering in `fill_stencil_path`.** Clip bounds are now
  applied as a scissor intersection before cover quads.
- **Software renderer stencil wrap.** Stencil values now wrap modulo 256
  instead of saturating at 255, fixing even-odd fill for complex paths.
- **Software renderer resize safety.** `sw_resize` no longer updates
  dimensions if the stencil buffer realloc fails.
- **Software renderer texture format rejection.** Non-RGBA8 formats
  (BGRA8, A8) are now rejected cleanly instead of causing memory errors.
- **Gradient extend modes in software backend.** `REPEAT` and `REFLECT`
  are now supported in `eval_gradient`.
- **Vulkan error handling.** `FLUX_VK_CHECK` now aborts on failure
  instead of silently continuing with undefined behaviour.
- **Build system: Vulkan is optional.** The library now compiles and
  runs with only the software backend when Vulkan is unavailable.

## [0.1.0] — 2026-04-25

Tagged for completeness. Stencil-based path fill, native `FILL_RECT`
op, pipeline boilerplate consolidation, and `FX_TRY_VK` macro family.

## [0.0.2] — 2026-04-24

Descriptor set cache, context-shared `VkPipelineCache`, exact path
clipping, fence-tracked image updates, VMA integration. Superseded.

## [0.0.1] — 2026-04-23

First tagged development preview.

[0.2.1]: https://github.com/anthropics/flux/releases/tag/v0.2.1
[0.2.0]: https://github.com/anthropics/flux/releases/tag/v0.2.0
[0.1.0]: https://github.com/anthropics/flux/compare/v0.0.2...v0.1.0
[0.0.2]: https://github.com/anthropics/flux/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/anthropics/flux/releases/tag/v0.0.1
