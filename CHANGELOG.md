# Changelog

All notable changes to flux are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [semantic versioning](https://semver.org/spec/v2.0.0.html).

Dates are ISO 8601. Pre-1.0 minor releases may introduce breaking
changes; the breaking commit is called out explicitly.

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

[0.2.0]: https://github.com/anthropics/flux/releases/tag/v0.2.0
[0.1.0]: https://github.com/anthropics/flux/compare/v0.0.2...v0.1.0
[0.0.2]: https://github.com/anthropics/flux/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/anthropics/flux/releases/tag/v0.0.1
