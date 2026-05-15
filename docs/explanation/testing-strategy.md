# Testing Strategy

This document explains why flux's test suite is structured the way it is, what each layer protects against, and the trade-offs baked into the design. For command-line recipes, see [Testing](../dev/testing.md).

## The problem: testing a 2D graphics library is hard

Most software bugs are logic errors: wrong branch taken, null dereference, off-by-one. These are easy to catch with unit tests. A 2D graphics library has logic errors too, but its primary output is **pixels**—and pixel bugs have unique properties:

- **Silent.** A blend-mode implementation that is slightly wrong still produces a plausible image. No crash, no exception, no log line.
- **Hardware-dependent.** The same Vulkan command stream can produce visually different results across vendors, driver versions, and OS compositors.
- **Float-sensitive.** A rasteriser that changes `0.25f` to `0.25000001f` can shift triangle coverage by one pixel, failing a pixel-perfect comparison.
- **Cross-backend.** flux supports multiple renderers (software, Vulkan, and potentially more). They must produce the same pixels for the same input, or the abstraction leaks.

These constraints shape the four-layer test pyramid.

## The four layers

```
          ┌─────────────┐
          │  benchmark  │  < 1 s, one datapoint, CI trend
          ├─────────────┤
          │ integration │  < 1 s, skips if no GPU
          ├─────────────┤
          │   golden    │  ~100 ms, pixel-perfect, deterministic
          ├─────────────┤
          │    unit     │  < 10 ms each, 13 files, no GPU
          └─────────────┘
```

### Unit tests: protect invariants, not pixels

Unit tests live at the bottom because they are fast, deterministic, and require no GPU. They verify that data structures behave correctly:

- Matrix inversion round-trips to identity.
- Path bounds are conservative.
- Gradient stop validation rejects non-monotonic inputs.
- Null inputs on every public API return the documented error code.

These tests do **not** verify that a path fill looks correct. They verify that the path object records the right verbs, that the flattening tolerance is respected, and that the paint snapshot deep-copies its dash array. Unit tests catch refactoring bugs; they do not catch rendering bugs.

### Golden-image tests: the pixel contract

Golden tests are flux's substitute for the "visual regression test" that human QA would perform. They render reference scenes with the **software backend** and compare every pixel against checked-in PPM files.

**Why pixel-perfect?** Because the software renderer is fully deterministic: same code, same compiler, same CPU, same pixels every time. There is no GPU scheduler, no driver heuristic, no tile-rendering order variation. A tolerance of `±1` per channel is enough to absorb build-to-build rounding differences; beyond that, any mismatch is a real bug.

**Why PPM?** PPM is uncompressed, text-readable, and diff-friendly in `git`. A one-pixel change produces a one-line diff. PNG would be smaller but opaque to code review.

**Why the software backend as reference?** The software renderer is the simplest complete implementation of the RHI vtable. It contains no async queues, no descriptor sets, no pipeline caches—just scanline rasterisation and per-pixel blending. When a future Metal or WebGPU backend produces different pixels, the software renderer is considered correct until proven otherwise.

Golden tests are **not** run against Vulkan. Vulkan output is intentionally excluded from the golden suite because:

1. Driver differences make pixel-perfect comparison flaky.
2. The software backend already proves the engine's op stream is correct.
3. A Vulkan-specific golden suite would multiply reference image count by driver count.

### Integration tests: "does the GPU exist?"

`test_vulkan_smoke.c` does one thing: attempt to create a Vulkan device. It may **skip** if Vulkan is unavailable. This test exists because:

- Vulkan initialization touches OS windowing APIs, physical device enumeration, and queue family selection—code paths that unit tests cannot exercise.
- CI runners vary widely in GPU availability. A hard failure on every headless runner would make the test useless.
- A skip is informative: it tells the CI matrix "this configuration cannot be validated here."

The integration suite is deliberately thin. Heavy GPU validation (memory barriers, descriptor leaks, pipeline cache correctness) is handled by the Khronos validation layers during local development, not by custom tests.

### Benchmarks: performance as a test

`bench_render.c` is not a correctness test; it is a **regression detector**. It renders a fixed stress scene for 120 frames and reports throughput. The benchmark is run in CI and its output is trended, not gated. A >10 % drop triggers human investigation.

Benchmarks use the software backend by default so they run on every CI runner. Vulkan benchmarks are run manually on reference hardware during release preparation.

## Design trade-offs

| Decision | Rationale | Cost |
|---|---|---|
| **Software renderer as reference** | Simplest, most deterministic backend. | Golden tests do not catch Vulkan-specific bugs. |
| **Pixel-perfect comparison** | Deterministic software renderer makes it cheap and authoritative. | Any intentional visual change requires updating reference images. |
| **No GPU golden suite** | Avoids driver-flakiness. | Relies on manual validation and validation layers for GPU correctness. |
| **PPM instead of PNG** | Diff-friendly, human-reviewable in PRs. | Larger repository size (~50 KB per 128×128 image). |
| **Skip-on-missing-GPU integration** | CI matrix safety. | No automated signal when a GPU configuration breaks. |
| **Benchmarks in CI** | Catches accidental algorithmic regressions early. | Noisy on shared CI runners; used for trend detection, not hard gates. |

## When tests must change

| Change | Required test updates |
|---|---|
| Refactor internal data structure | Unit tests only. |
| Fix a rasterisation bug | Update golden reference images. Attach before/after diffs in PR. |
| Add a new backend | Add golden-image scenes that exercise every vtable entry. |
| Optimise a hot path | Run benchmark before and after; verify no golden regression. |
| Public API change | Update `test_api_surface` link check and `test_null_safety`. |

## Relationship to the rest of the project

- The **software renderer** is the reference implementation. See [Software renderer](software-renderer.md).
- The **RHI vtable** makes golden tests backend-portable. See [ADR-0003: Renderer vtable abstraction](../adr/0003-renderer-vtable.md).
- **Release gates** require golden suite green. See [Release process](../dev/release-process.md).
- **Contributors** are asked to update golden images when rendering changes. See [Contributing](../dev/contributing.md).
