# API Stability and Deprecation Policy

flux is pre-1.0 software. This document describes the stability
contract today and the process by which it tightens toward 1.0.

## Current status

**Library version:** 0.2.0
**Stability level:** Pre-1.0 development. The 0.2 series is the first
public surface intended to remain shape-stable. Field additions and
new functions are non-breaking; signature changes will only happen on
a minor bump and only with prior `[Unreleased]` notice.

## What "breaking change" means

Any modification that requires source-level updates in calling code:

- Removing a public function, type, enum value, or struct field.
- Changing the signature of a public function.
- Changing the meaning of a struct field.
- Renumbering enum values.
- Changing the default behaviour of an existing feature.

The following are **not** considered breaking:

- Adding new functions, types, or enum variants at the end of an enum.
- Adding new fields to the **end** of a sized descriptor (`*_desc`).
  Callers using designated initialisers and `desc.size = sizeof(*desc)`
  are unaffected; older callers using the smaller `size` continue to
  work because flux only reads up to the size they declared.
- Changing internal implementation details (RHI vtable, geometry
  algorithms, software rasteriser).
- Fixing rendering bugs that produce incorrect output.

## Sized descriptors

Every public `flux_*_desc` struct begins with `uint32_t size`. The
caller sets it to `sizeof(*desc)` before passing the struct in. flux
reads only up to that size; new fields appended in future versions are
inferred as zero for older callers. This is the mechanism by which the
API can grow without breaking ABI.

## Refcounting

Every owned resource (`flux_context`, `flux_surface`, `flux_path`,
`flux_paint`, `flux_gradient`, `flux_image`, `flux_glyph_run`) is
returned with refcount 1, retained by `_retain`, released by
`_release`. Both are NULL-safe and atomic, so cross-thread `release`
of an idle resource is safe.

## Deprecation process

When a breaking change is necessary:

1. **Announcement.** The upcoming change is documented in
   [CHANGELOG.md](../../CHANGELOG.md) under `[Unreleased]`, with a
   "Deprecation" section.
2. **Transition period.** The old API remains functional for at least
   one minor release. Where possible, the deprecated symbols carry a
   compile-time deprecation attribute so callers see warnings.
3. **Removal.** The old API is removed in the subsequent minor
   release.

## ABI compatibility

flux does not guarantee binary compatibility across minor releases
(0.3 → 0.4). Recompile your application when updating flux. Within a
patch series (0.3.0 → 0.3.1) the ABI is preserved.

If you need durable ABI, pin to an exact version and vendor the
library. Use `flux_version_check(major, minor, patch)` at startup to
detect a mismatched runtime.

## Header stability

The two public headers are committed to structural stability after
1.0:

- `<flux/flux.h>` — core drawing API.
- `<flux/flux_vulkan.h>` — Vulkan interop.

New functionality is added via:

- New functions in existing headers.
- New optional headers (e.g., `<flux/flux_x11.h>` if X11 support is
  added).

No header file will ever be silently merged or split.

## Pre-1.0 checklist

Blockers for a 1.0 freeze:

- [ ] Golden-image regression suite on reference hardware.
- [ ] Performance baselines recorded and automated.
- [ ] SDF text path for large glyphs.
- [ ] Multi-driver CI (Intel, AMD, NVIDIA, lavapipe).
- [ ] Multi-distro CI (at least two distributions).
- [ ] Public deprecation cycle exercised at least once successfully.

Until these land, flux remains pre-1.0 and shape changes may continue
to occur on minor bumps — though we will work hard to make them rare.

## Versioning

flux follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
with the pre-1.0 exception: minor bumps may introduce breaking
changes.

| Version pattern | Meaning |
|---|---|
| `0.x.y` | Pre-1.0 development. Breaking changes possible on `x` bumps until 0.2. |
| `1.x.y` | Post-1.0 stable. Breaking changes only on major bumps. |

## Reporting breakage

If an upgrade breaks your build and the change is not documented in
CHANGELOG, please open an issue with:

- Your previous and new flux versions.
- The compiler error or behavioural change.
- A minimal code snippet that compiled before but fails now.

## See also

- [CHANGELOG.md](../../CHANGELOG.md)
- [API reference](api.md)
- [Thread safety](thread-safety.md)
