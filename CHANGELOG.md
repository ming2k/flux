# Changelog

All notable changes to flux are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [semantic versioning](https://semver.org/spec/v2.0.0.html).

Dates are ISO 8601. Pre-1.0 releases may introduce breaking changes on
minor bumps; the commit that breaks ABI is called out explicitly below.

## [Unreleased]

## [0.1.0] — 2026-04-25

### Added

- **Stencil-based path fill.** `fx_fill_path` now uses a two-pass stencil
  algorithm (increment + cover) for all multi-subpath and non-line-loop
  paths. This correctly implements the even-odd fill rule, so donut
  shapes with holes are rendered accurately.
- **`FX_OP_FILL_RECT` native backend.** `fx_fill_rect` no longer allocates
  a temporary `fx_path` on every call. It records a dedicated
  `FX_OP_FILL_RECT` command and the backend routes it straight to the
  existing solid-rect batch path. Transformed rects still fall back to
  path fill, but the common axis-aligned case is now allocation-free.
- **`make_pipeline_core` helper.** All 9 graphics-pipeline creation
  functions in `src/vk/swapchain.c` delegate to a single helper that
  builds the shared `VkGraphicsPipelineCreateInfo` boilerplate. Cuts
  ~797 lines of mechanical duplication while preserving every
  pipeline's exact Vulkan state.

### Removed

- **`fx_draw_image_ex`** — was a trivial wrapper with no additional
  parameters. `fx_draw_image` now contains the full implementation.
- **`FX_CHECK_VK`** — the old macro logged but never propagated failure.
  Replaced by:
  - `FX_TRY_VK(ctx, expr)` — logs and `return false` on error (for `bool`
    functions).
  - `FX_LOG_VK(ctx, expr)` — logs only (for `void` functions).

### Changed

- `pixel_format_to_vk` and `to_vk_format` deduplicated into a single
  `fx_pixel_format_to_vk()` exported from `src/image.c`.
- Phase-marker comments (`Phase 0`, `phase 1+`, etc.) removed from
  source and documentation.

### Fixed

- `fx_device_init` now returns `false` on `vkCreateCommandPool`,
  `vkCreatePipelineCache`, and `vmaCreateAllocator` failure instead of
  continuing in a broken state.

## [0.0.2] — 2026-04-24

### Added

- **Descriptor set cache.** Each frame caches `(image_view, sampler) → VkDescriptorSet`
  lookups in a small per-frame array, eliminating per-draw descriptor allocation
  for repeated images and glyph draws.
- **Context-shared `VkPipelineCache`.** Shader compilation results are cached at
  the context level and reused across all surfaces, eliminating recompile on
  surface creation.
- **Context-shared pipeline sets.** Pipelines are keyed by `(format, samples)`
  and stored in `fx_context.pipeline_sets[]`. A pipeline set is created on first
  use and survives resizes.
- **Exact path clipping.** `FX_OP_CLIP_PATH` triangulates and rasterizes to the
  stencil buffer for pixel-accurate clipping, replacing the scissor-bound
  fallback used in v0.0.1.
- **Per-image last-use fence tracking.** `fx_image_update` waits on the image's
  `last_use_fence` before writing, making mid-frame image updates safe under
  load.
- **VMA integration.** All images and buffers now use Vulkan Memory Allocator
  (`vmaCreateImage`, `vmaCreateBuffer`), replacing dedicated `VkDeviceMemory`
  allocations and removing the `maxMemoryAllocationCount` bottleneck on
  image-heavy workloads.

### Changed (internals; no public API change)

- **Unified GPU upload path** (`src/vk/upload.c`). A single
  context-owned subsystem — persistent growable staging buffer, one
  reusable command buffer, one reusable fence — now handles every
  image and glyph upload. Replaces three copy-paste blocks in
  `fx_image_create`, `fx_image_update`, and the glyph-atlas loader
  that each allocated a fresh `VkBuffer` + `VkDeviceMemory` +
  `VkCommandBuffer` and ran `vkQueueWaitIdle` per call.
- **Swapchain lifetime split.** `fx_swapchain_destroy` is now
  swapchain-scoped only (swapchain, images, framebuffers, stencil,
  per-frame sync). Render pass and sampler are surface-scoped and
  live through resizes via `fx_surface_destroy_pipelines`.
  A window resize no longer recompiles any pipeline.
- **Persistent readback staging.** `fx_surface_read_pixels` keeps its
  staging buffer and memory on the surface and grows it on extent
  change, instead of allocating and freeing them per call.
- **Offscreen present no longer blocks.** `fx_surface_present` skips
  the forced `vkWaitForFences` on the offscreen path. `read_pixels`
  waits on `fx_surface_wait_idle` before submitting its copy, and
  `acquire` already waits on the frame fence.
- **Renames.** `record_bootstrap_ops` → `record_ops`;
  `fx_make_bootstrap_pipeline` → `fx_make_solid_pipeline`. Dropped
  the misleading "remaining ops still wait for the Vulkan raster
  backend" log line — every op kind dispatches.

### Fixed

- **Glyph atlas overflow.** Previously failed silently, dropping
  glyphs without warning. Now logs a warning, evicts all cached
  entries, waits for in-flight frames to release the atlas, and
  reuses the texture. Also rejects glyphs larger than the atlas
  up-front with a specific error.
- **Images created without initial data** are now transitioned to
  `SHADER_READ_ONLY_OPTIMAL` at creation time. Previously they were
  left in `UNDEFINED`, which would cause a validation error the
  first time the image was sampled.
- **`fx_image_create` error paths.** All `vkCreateBuffer`,
  `vkAllocateMemory`, `vkBindImageMemory`, and `vkMapMemory` return
  codes are now checked, logged with `FX_LOGE`, and wind down the
  partially-constructed image cleanly.

## [0.0.1] — 2026-04-23

First tagged development preview. Full vector primitives (filled and
stroked paths, images, gradients, rectangular and path clipping),
dynamic GPU glyph atlas with FreeType + HarfBuzz, HiDPI DPR state,
offscreen surfaces with readback, Wayland swapchain integration, and
a green unit + integration test suite.

[Unreleased]: https://github.com/anthropics/flux/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/anthropics/flux/compare/v0.0.2...v0.1.0
[0.0.2]: https://github.com/anthropics/flux/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/anthropics/flux/releases/tag/v0.0.1
