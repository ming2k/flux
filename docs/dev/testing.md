# Testing

## Test structure

flux uses Meson for both tests and benchmarks. Everything lives in `tests/`:

| File | Suite | Purpose |
|---|---|---|
| `test_*.c` (13 files) | `unit` | Component tests: math, colour, path, paint, gradient, image, glyph runs, canvas transforms, allocator, offscreen, null safety, API surface, version. |
| `test_golden.c` | `golden` | Pixel-level regression tests against checked-in reference images. |
| `test_vulkan_smoke.c` | `integration` | Verifies Vulkan device creation on the current driver. |
| `bench_render.c` | `benchmark` | Performance regression test; reports fps and mpix/sec. |

## Running tests

```sh
meson setup build
ninja -C build

# All tests
meson test -C build

# Individual suites
meson test -C build --suite unit
meson test -C build --suite golden
meson test -C build --suite integration

# Serial execution (recommended for software Vulkan drivers)
meson test -C build --suite unit --suite golden --suite integration --num-processes 1
```

## Unit tests

Unit tests exercise one component at a time and do not require a GPU. They run in milliseconds.

| Test | Coverage |
|---|---|
| `test_version` | Version string and number macros |
| `test_matrix` | Affine transforms, inversion, identity |
| `test_color` | RGBA packing and unpacking |
| `test_path` | Verb recording, bounds, transforms |
| `test_paint` | Property getters and setters |
| `test_gradient` | Linear and radial gradient creation, validation |
| `test_image` | CPU image creation, update, sub-region upload |
| `test_glyph_run` | Run creation, append, retain/release |
| `test_canvas_transform` | Save/restore, matrix stack |
| `test_offscreen` | Software surface creation, clear, fill_rect, read_pixels |
| `test_allocator` | Context allocator hooks and peak tracking |
| `test_null_safety` | NULL input handling on every public entry point |
| `test_api_surface` | Link-time verification of all exported symbols |

## Golden-image regression tests

`test_golden.c` renders five reference scenes to 128×128 offscreen surfaces and compares every pixel against checked-in PPM files in `tests/golden/`.

| Scene | What it exercises |
|---|---|
| `solid_rect` | Solid colour fill_rect |
| `gradient` | Linear gradient fill |
| `clip` | Rectangular clip + fill |
| `glyph` | Glyph upload, atlas binding, draw_image tinting |
| `transform` | Rotate + translate matrix |

Pass criterion: every RGB channel must match within `±1` of the reference. The software renderer is deterministic, so exact match is expected; the small tolerance accounts for build-to-build floating-point rounding differences.

**Updating references after an intentional visual change:**

```sh
FLUX_GOLDEN_UPDATE=1 meson test -C build --suite golden
```

Review the regenerated PPM files in `tests/golden/` before committing.

## Performance benchmark

`bench_render.c` renders a stress-test scene for 120 frames and reports throughput:

```
frames:       120
elapsed_ms:   925
ms_per_frame: 7.71
fps:          129.7
mpix/sec:     34.0
```

The scene contains 20 fill rects, a rounded-rect path with a gradient, and 64 textured glyphs.

Run benchmarks explicitly (they are not part of `meson test`):

```sh
meson test --benchmark -C build --verbose
```

Benchmarks are used for regression detection in CI. A significant slowdown (e.g. >10% fps drop) should block a PR until it is understood.

## Integration tests

`test_vulkan_smoke.c` attempts to create a Vulkan device through flux's convenience helper. It may **skip** (exit code 77) if:

- flux was compiled without Vulkan support (`FLUX_NO_VULKAN=1`)
- No Vulkan driver is installed on the machine

This makes the test safe to run on diverse CI runners without hard-failing on missing GPU hardware.

## Writing a new unit test

1. Create `tests/test_my_feature.c`.
2. Include `<flux/flux.h>` and `"test_helpers.h"`.
3. Use the `CHECK(cond)` macro for assertions; it prints file/line and returns 1 on failure.
4. Add the file to `tests/meson.build`:
   ```meson
   test('my_feature', executable('test_my_feature', 'test_my_feature.c',
        dependencies : [flux_dep]), suite : 'unit')
   ```
5. Run `meson test -C build --suite unit` to verify.

Tests should not require a display server, physical GPU, or user interaction. If a test needs Vulkan, place it in the `integration` suite and make it skip gracefully when Vulkan is unavailable.

## See also

- [How to run from source](../how-to/run-from-source.md) — local build instructions.
