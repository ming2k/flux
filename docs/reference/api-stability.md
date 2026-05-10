# API Stability and Deprecation Policy

flux is pre-1.0 software. This document explains what stability guarantees exist today and how breaking changes are managed.

## Current status

**Version:** 0.1.0  
**Stability level:** Pre-1.0 development

Public APIs may change on minor version bumps (0.1 → 0.2). Patch releases (0.1.0 → 0.1.1) fix bugs without breaking ABI.

## What "breaking change" means

A breaking change is any modification that requires source-level updates in calling code:

- Removing a public function or type.
- Changing the signature of a public function.
- Changing the meaning of a struct field.
- Changing enum values or their order.
- Changing the default behavior of an existing feature.

The following are NOT considered breaking:

- Adding new functions or types.
- Adding new enum variants at the end of an enum.
- Adding new fields to the end of a struct (callers using designated initializers or zeroing the struct are unaffected).
- Changing internal implementation details.
- Fixing rendering bugs that produce incorrect output.

## Deprecation process

When a breaking change is necessary, flux follows this timeline:

1. **Announcement** — The upcoming change is documented in [CHANGELOG.md](../../CHANGELOG.md) under `[Unreleased]` with a "Deprecation" section.
2. **Transition period** — The old API remains functional for at least one minor release with a compile-time deprecation warning where possible.
3. **Removal** — The old API is removed in the subsequent minor release.

Example from the 0.1.0 release:

```
## [0.1.0] — 2026-04-25

### Removed
- `fx_draw_image_ex` — was a trivial wrapper. Use `fx_draw_image` directly.

### Changed
- `FX_CHECK_VK` replaced by `FX_TRY_VK` and `FX_LOG_VK`. Update macros:
  - `FX_TRY_VK(ctx, expr)` for `bool` functions.
  - `FX_LOG_VK(ctx, expr)` for `void` functions.
```

## ABI compatibility

flux does not guarantee ABI compatibility across minor releases. Recompile your application when updating flux.

If you need ABI stability, pin to an exact version and vendor the library.

## Header stability

The three public headers are committed to structural stability after 1.0:

- `<flux/flux.h>` — Core drawing API.
- `<flux/flux_vulkan.h>` — Vulkan interop.

New functionality will be added via:
- New functions in existing headers.
- New optional headers (e.g., `<flux/flux_x11.h>` if X11 support is added).

## Pre-1.0 checklist

The following items are blockers for a 1.0 release:

- [ ] Golden-image test suite (`tests/test_render_golden.c`) passing on reference hardware.
- [ ] Performance baselines recorded and automated.
- [ ] SDF text rendering for large glyphs.
- [ ] Multi-driver CI (Intel, AMD, NVIDIA, lavapipe).
- [ ] Multi-distro CI (at least 2 distributions).

Until these are complete, flux remains pre-1.0 and APIs may shift.

## Versioning

flux follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html) with the pre-1.0 exception: minor bumps may introduce breaking changes.

| Version pattern | Meaning |
|---|---|
| `0.x.y` | Pre-1.0 development. Breaking changes on `x` bumps. |
| `1.x.y` | Post-1.0 stable. Breaking changes only on major bumps. |

## Reporting breakage

If an upgrade breaks your build and the change is not documented in CHANGELOG, please open an issue with:
- Your previous and new flux versions.
- The compiler error or behavioral change.
- A minimal code snippet that compiled before but fails now.

## See also

- [CHANGELOG.md](../../CHANGELOG.md)
- [Release process](../dev/release-process.md)
- [Roadmap](../explanation/roadmap.md)
