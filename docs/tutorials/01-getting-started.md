# Getting Started: Your First flux Build

By the end of this tutorial you will have:

- Configured a local flux build
- Built the library
- Run the unit smoke tests
- Seen where to go next for drawing, linking, and development

**Estimated time:** 10 minutes
**Difficulty:** Beginner

## Prerequisites

- GCC 12 or Clang 15+
- Meson 1.3+
- Ninja 1.10+
- Vulkan 1.2 headers and loader
- FreeType 2 and HarfBuzz
- `glslangValidator`

For full package names, see [Developer setup](../dev/setup.md).

## Step 1: Configure the build

```bash
meson setup build -Dexamples=false
```

You should see Meson finish configuration and print a build directory summary. Disabling Vulkan keeps this first run focused on the library and tests rather than a compositor session.

> **If you see "Directory already configured":** use the existing build directory and continue to the next step.

## Step 2: Build flux

```bash
meson compile -C build
```

You should see Ninja compile the C sources and link `src/libflux.so`.

## Step 3: Run the smoke tests

```bash
meson test -C build --suite unit
```

All unit tests should pass. This verifies that the CPU-side path, canvas, transform, stroker, and error-logging code works in your environment.

## Step 4: See the test output

Meson prints one line per test and ends with a summary similar to:

```text
Ok:                 10
Fail:               0
```

The exact test count may grow as the project adds coverage, but failures should be zero.

## What's next?

- Want to link flux into an application? See [How to link flux](../how-to/link-flux.md)
- Want to draw with the API? See [How to draw basic shapes](../how-to/draw-basic-shapes.md)
- Want to understand the architecture? See [Architecture overview](../explanation/architecture-overview.md)
- Want to contribute code? See [Developer setup](../dev/setup.md)

## Troubleshooting

- **Meson cannot find Vulkan, FreeType, HarfBuzz, or glslangValidator**: install the packages listed in [Developer setup](../dev/setup.md).
- **Examples do not build**: keep `-Dexamples=false` for this tutorial, or install the Vulkan dependencies from [Developer setup](../dev/setup.md).
- See [How to troubleshoot common issues](../how-to/troubleshooting.md) for more.
