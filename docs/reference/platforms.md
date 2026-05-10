# Platform Support

This document describes where flux runs and known limitations.

## Supported

### Linux

flux is developed and tested on Linux. It requires a Vulkan loader and a compatible ICD.

### Headless / Offscreen

Offscreen surfaces work on any system with a Vulkan loader and a compatible ICD, even without a display server.

```c
fx_surface *s = fx_surface_create_offscreen(ctx, 800, 600,
                                            FX_FMT_RGBA8_UNORM,
                                            FX_CS_SRGB);
```

All automated tests run headless using Mesa lavapipe (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`).

## Windowed rendering

flux does not create platform windows. The caller creates a window using their preferred toolkit (GLFW, SDL, raw platform APIs, etc.) and creates a `VkSurfaceKHR` from it. flux then manages the swapchain and presentation via `fx_surface_create_vulkan`.

```c
// Caller creates VkSurfaceKHR from their window
fx_surface *s = fx_surface_create_vulkan(ctx, vk_surface, width, height, FX_CS_SRGB);
```

## GPU vendors

| Vendor              | Status                               |
|---------------------|--------------------------------------|
| Intel               | Primary development target. Tested.  |
| AMD                 | Should work (RADV).                  |
| NVIDIA              | Should work (proprietary driver).    |
| Software (lavapipe) | CI target. Slower but complete.      |

## Vulkan version

Minimum required Vulkan version: **1.2**

flux does not use Vulkan 1.3 core features or ray tracing extensions.

## Feature matrix

| Feature                 | Windowed | Offscreen |
|-------------------------|----------|-----------|
| Swapchain presentation  | Yes      | No        |
| Offscreen rendering     | Yes      | Yes       |
| Text rendering          | Yes      | Yes       |
| Image upload & draw     | Yes      | Yes       |
| Gradients               | Yes      | Yes       |
| Clipping (scissor)      | Yes      | Yes       |
| Clipping (stencil path) | Yes      | Yes       |
| Resize                  | Yes      | No*       |
| HiDPI / DPR             | Yes      | Manual    |

\* Offscreen surfaces can be recreated at a new size by destroying and re-creating the surface.

## Reporting platform issues

Please open an issue with:
- GPU and driver version
- Distribution / OS version
- Vulkan loader and ICD details (`vulkaninfo --summary`)
- Minimal reproduction steps

## See also

- [Developer setup](../dev/setup.md)
