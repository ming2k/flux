# How to Use flux from Zig

This guide shows how to call flux's C API from Zig.

## When to use this

Use this page when you want to build a Zig application or library that uses flux for 2D rendering.

## Prerequisites

- Zig 0.13+ or master
- flux built and installed (see [Getting Started](../tutorials/01-getting-started.md))
- Vulkan headers and loader
- FreeType 2 and HarfBuzz

## Overview

Zig has first-class C interoperability, so you can use flux directly without writing any C code:

1. Import flux headers with `@cImport`
2. Link `libflux` and its dependencies in `build.zig`
3. Use the C API with Zig syntax

## Step 1: Build and install flux

First, build flux as a shared or static library:

```bash
# Shared library (default)
meson setup build
meson compile -C build
meson install -C build

# Or static library
meson setup build-static -Ddefault_library=static
meson compile -C build-static
meson install -C build-static
```

Make sure `pkg-config` can find flux:

```bash
pkg-config --cflags --libs flux
```

## Step 2: Create a Zig project

```bash
mkdir my-flux-app
cd my-flux-app
zig init-exe
```

## Step 3: Configure build.zig

Edit `build.zig` to link flux and its dependencies:

```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "my-flux-app",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Add flux include path
    exe.addIncludePath(.{ .cwd_relative = "/usr/local/include" });
    // Or wherever flux was installed (e.g., /usr/include, /opt/homebrew/include)

    // Link flux library
    exe.linkSystemLibrary("flux");
    
    // Link flux dependencies
    exe.linkSystemLibrary("vulkan");
    exe.linkSystemLibrary("freetype2");
    exe.linkSystemLibrary("harfbuzz");
    exe.linkLibC();

    // If using static linking, you may also need:
    // exe.linkSystemLibrary("pthread");
    // exe.linkSystemLibrary("m");

    b.installArtifact(exe);
}
```

### Finding the correct include path

If you don't know where flux was installed:

```bash
# Get the include path
pkg-config --cflags-only-I flux
# Example output: -I/usr/local/include

# Get library flags
pkg-config --libs flux
# Example output: -L/usr/local/lib -lflux
```

Use the path after `-I` for `addIncludePath`.

## Step 4: Import and use flux

Create `src/main.zig`:

```zig
const std = @import("std");

// Import flux C API
const flux = @cImport({
    @cInclude("flux/flux.h");
});

pub fn main() !void {
    // Initialize flux context
    const desc = flux.flux_context_desc{
        .min_log_level = flux.FLUX_LOG_INFO,
        .log = null,
        .log_user = null,
    };

    const ctx = flux.flux_context_create(&desc);
    if (ctx == null) {
        std.log.err("Failed to create flux context", .{});
        return error.FluxContextCreationFailed;
    }
    defer flux.flux_context_destroy(ctx);

    // Query device capabilities
    var caps: flux.flux_device_caps = undefined;
    _ = flux.flux_context_get_device_caps(ctx, &caps);
    std.log.info("Vulkan API version: {d}", .{caps.api_version});

    // Create offscreen surface
    const surface = flux.flux_surface_create_offscreen(
        ctx,
        800,
        600,
        flux.FLUX_FMT_BGRA8_UNORM,
        flux.FLUX_CS_SRGB,
    );
    if (surface == null) {
        std.log.err("Failed to create surface", .{});
        return error.FluxSurfaceCreationFailed;
    }
    defer flux.flux_surface_destroy(surface);

    // Acquire canvas and draw
    const canvas = flux.flux_surface_acquire(surface);
    
    // Clear background
    flux.flux_clear(canvas, flux.flux_color_rgba(40, 40, 40, 255));

    // Draw a rectangle
    const rect = flux.flux_rect{
        .x = 100.0,
        .y = 100.0,
        .w = 200.0,
        .h = 150.0,
    };
    flux.flux_fill_rect(canvas, &rect, flux.flux_color_rgba(90, 180, 255, 255));

    // Draw a circle using path
    const path = flux.flux_path_create();
    defer flux.flux_path_destroy(path);
    
    const cx: f32 = 400.0;
    const cy: f32 = 300.0;
    const r: f32 = 50.0;
    
    _ = flux.flux_path_move_to(path, cx + r, cy);
    _ = flux.flux_path_arc_to(path, r, r, 0.0, false, true, cx - r, cy);
    _ = flux.flux_path_arc_to(path, r, r, 0.0, false, true, cx + r, cy);
    _ = flux.flux_path_close(path);

    var paint: flux.flux_paint = undefined;
    flux.flux_paint_init(&paint, flux.flux_color_rgba(255, 120, 80, 255));
    _ = flux.flux_fill_path(canvas, path, &paint);

    // Present frame
    flux.flux_surface_present(surface);

    // Read pixels (for offscreen surface)
    const width: usize = 800;
    const height: usize = 600;
    const stride: usize = width * 4;
    const buffer_size = height * stride;
    
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();
    
    const pixels = try allocator.alloc(u8, buffer_size);
    defer allocator.free(pixels);
    
    if (flux.flux_surface_read_pixels(surface, pixels.ptr, stride)) {
        std.log.info("Read {d} bytes of pixel data", .{buffer_size});
        // Save to file or process further...
    } else {
        std.log.err("Failed to read pixels", .{});
    }
}
```

## Build and run

```bash
zig build
./zig-out/bin/my-flux-app
```

## Important Notes

### Memory Management

- flux uses raw pointers (`*flux_context`, `*flux_surface`, etc.). In Zig, these translate to optional pointers (`?*flux.flux_context`).
- Always check for null after creation functions.
- Use `defer` to ensure cleanup happens even if errors occur.

### Error Handling

flux C functions that return `bool` indicate success/failure. Always check the return value:

```zig
if (!flux.flux_path_move_to(path, x, y)) {
    std.log.err("Failed to move path", .{});
}
```

### Enums and Constants

Zig imports C enums as regular enums. Access them with the enum name:

```zig
flux.FLUX_CAP_ROUND
flux.FLUX_JOIN_MITER
flux.FLUX_FMT_BGRA8_UNORM
```

### Color Helper

The `flux_color_rgba` inline function is available as a regular function in Zig:

```zig
const color = flux.flux_color_rgba(255, 128, 0, 255);
```

### Vulkan and Vulkan Surfaces

For windowed applications, use the appropriate surface creation function:

```zig
// Vulkan surface (you create VkSurfaceKHR externally)
const surface = flux.flux_surface_create_vulkan(ctx, vk_surface, width, height, flux.FLUX_CS_SRGB);

// Vulkan surface (Linux)
const surface = flux.flux_surface_create_Vulkan(ctx, display, wl_surface, width, height, flux.FLUX_CS_SRGB);
```

Note: For `flux_surface_create_vulkan`, include `flux/flux_vulkan.h` and link against the Vulkan loader.

## Advanced: Wrapping the C API

For a more idiomatic Zig API, you can create a wrapper:

```zig
pub const Context = struct {
    ptr: *flux.flux_context,

    pub fn init(desc: *const flux.flux_context_desc) !Context {
        const ptr = flux.flux_context_create(desc) orelse return error.ContextCreationFailed;
        return .{ .ptr = ptr };
    }

    pub fn deinit(self: *Context) void {
        flux.flux_context_destroy(self.ptr);
    }

    pub fn createOffscreenSurface(self: *Context, width: i32, height: i32) !Surface {
        const ptr = flux.flux_surface_create_offscreen(
            self.ptr, width, height, flux.FLUX_FMT_BGRA8_UNORM, flux.FLUX_CS_SRGB,
        ) orelse return error.SurfaceCreationFailed;
        return .{ .ptr = ptr };
    }
};

pub const Surface = struct {
    ptr: *flux.flux_surface,

    pub fn deinit(self: *Surface) void {
        flux.flux_surface_destroy(self.ptr);
    }

    pub fn acquire(self: *Surface) *flux.flux_canvas {
        return flux.flux_surface_acquire(self.ptr).?;
    }

    pub fn present(self: *Surface) void {
        flux.flux_surface_present(self.ptr);
    }
};
```

## Troubleshooting

### "cannot find flux/flux.h"

The include path in `build.zig` is wrong. Find the correct path:

```bash
# On Linux
find /usr -name "flux.h" 2>/dev/null

# On macOS with Homebrew
find /opt/homebrew -name "flux.h" 2>/dev/null
```

### "cannot find -lflux"

The library path is missing. Add it in `build.zig`:

```zig
exe.addLibraryPath(.{ .cwd_relative = "/usr/local/lib" });
```

### Link errors for Vulkan, FreeType, or HarfBuzz

Make sure all dependencies are installed and linked:

```zig
exe.linkSystemLibrary("vulkan");
exe.linkSystemLibrary("freetype2");
exe.linkSystemLibrary("harfbuzz");
```

On some systems, the pkg-config names may differ (e.g., `freetype2` vs `freetype`).

### Validation errors

If you see Vulkan validation errors, make sure the library was compiled with validation support (`-Dvalidation=enabled`).

## Verification

A successful build produces an executable that:
1. Creates a flux context without errors
2. Renders to an offscreen surface
3. Reads pixel data successfully

For windowed applications, you should see the rendered content in the window.

## See Also

- [How to link flux](link-flux.md) - General linking instructions
- [How to draw basic shapes](draw-basic-shapes.md) - Drawing examples in C (translate to Zig)
- [How to record and present a frame](record-and-present-a-frame.md) - Frame lifecycle
- [Zig Documentation: C](https://ziglang.org/documentation/master/#C) - Official Zig C interop docs
