# Graphics Foundations

Concepts you must internalize before writing a performant graphics library — from the memory bus to the display panel.

## Memory and cache

Graphics rendering is, at its core, high-frequency reads and writes across massive memory regions. If you ignore CPU cache behaviour, your rasteriser may run an order of magnitude slower.

### Linear memory and 2D-to-1D mapping

The screen is 2D but main memory is 1D. Every pixel coordinate `(x, y)` must be mapped into a flat buffer via:

```
offset = y * pitch + x * bytes_per_pixel
```

**Pitch (stride)** is the byte distance between consecutive rows. It is rarely equal to `width * bytes_per_pixel` because operating systems and drivers align scanlines to satisfy hardware requirements (e.g. 16- or 64-byte boundaries). Code that assumes `pitch == width * bpp` will break on many real framebuffers.

Always store `pitch` independently of `width` and use it for row addressing.

### Cache lines

Modern CPUs read memory in 64-byte chunks called **cache lines**. When you touch one byte, the CPU brings the entire surrounding 64-byte block into L1 cache. Subsequent accesses inside that block are nearly free.

The core discipline is **data locality**:

| Traversal pattern | Cache behaviour |
|---|---|
| Row-major (horizontal scan) | Near-perfect cache utilisation; every fetched line is used before eviction |
| Column-major (vertical scan) | Cache line is fetched, one pixel is read, the line is evicted — repeat N times |

A vertical pixel scan touches a new cache line on every row. On a 4096-pixel-tall image, that is 4096 cache misses for what row-major traversal accomplishes in one miss per 16 pixels (assuming 32-bit RGBA). The performance gap can exceed 10×.

**Rule:** structure rendering loops so the innermost dimension advances in the contiguous (stride) direction.

### Memory alignment

OS allocators (`malloc`) return byte-aligned pointers. Graphics workloads need stricter alignment:

- **16-byte alignment** — required by SSE (`_mm_load_ps` / `_mm_store_ps`) and common Vulkan uniform buffer layouts.
- **32-byte alignment** — required by AVX-256 (`_mm256_load_ps` / `_mm256_store_ps`).
- **64-byte alignment** — matches cache line boundaries, preventing false sharing when DMA or multiple cores touch adjacent buffers.

Use `posix_memalign` or `aligned_alloc` to allocate pixel buffers and vertex data. Misaligned loads cause either segfaults (if you use the aligned intrinsics) or multi-instruction emulation (if the compiler silently fixes it).

## CPU architecture and SIMD

Scalar C/C++ is insufficient for competitive software rasterisation. You must exploit the data-level parallelism exposed by the CPU.

### SIMD (Single Instruction, Multiple Data)

SIMD is the baseline skill for low-level graphics authors. The relevant instruction sets:

| ISA | Register width | Pixels packed (RGBA `uint32_t`) |
|---|---|---|
| SSE2 / SSE4 | 128 bit | 4 pixels per operation |
| AVX2 | 256 bit | 8 pixels per operation |
| AVX-512 | 512 bit | 16 pixels per operation |
| ARM NEON | 128 bit | 4 pixels per operation |

**Shift your thinking from scalar to vector:** instead of blending one pixel at a time —

```c
dst[i] = src[i] * alpha + dst[i] * (1 - alpha);
```

— pack four or eight pixels into a 128/256-bit register and complete the blend for all of them with a single instruction:

```c
__m128i src = _mm_loadu_si128((__m128i *)&src_buf[i]);
__m128i dst = _mm_loadu_si128((__m128i *)&dst_buf[i]);
__m128i blended = _mm_add_epi8(
    _mm_mullo_epi16(src, alpha_vec),
    _mm_mullo_epi16(dst, inv_alpha_vec)
);
_mm_storeu_si128((__m128i *)&dst_buf[i], blended);
```

Nearly all compositing primitives — blending, premultiplication, format conversion, bilinear filtering — benefit from SIMD.

### Endianness

When you treat a pixel as a `uint32_t` (e.g. `0xAARRGGBB`), the byte layout in memory depends on endianness:

| Format | Little-endian (x86/ARM) | Big-endian |
|---|---|---|
| `0xAABBGGRR` | `[R, G, B, A]` | `[A, B, G, R]` |
| `0xRRGGBBAA` | `[A, B, G, R]` | `[R, G, B, A]` |

This matters when your pixel data crosses system boundaries — a framebuffer provided by the window system, a texture uploaded to a GPU, or a PNG saved to disk. Know which component order your platform expects and write explicit conversion routines rather than relying on pointer-casting across formats.

## Data representation and numeric computation

Mathematical formulas are exact; computer numbers are not.

### IEEE 754 floating point

Floating-point arithmetic introduces:

- **Limited precision** — `float` carries roughly 7 decimal digits; `double` about 15. Each addition can lose low-order bits.
- **Subnormals** — values near zero that trigger microcode and cost 20–100× more cycles than normal operations. Flush them to zero when they are not meaningful (e.g. in geometry computed far off-screen).
- **NaN and infinity** — results of `0.0/0.0` or `1.0/0.0`. A NaN contamination will propagate through an entire computation silently, turning all downstream results to NaN.

**Rule for geometric predicates:** never check exact equality. Use an epsilon:

```c
bool approx_equal(float a, float b) {
    return fabsf(a - b) <= 1e-6f * fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
}
```

Intersection tests (ray–triangle, segment–segment, point-in-polygon) that omit epsilon tolerances will produce "leaks" — pixels the rasteriser skips because a computed coordinate was `5.999999e-01` instead of `6.000000e-01`.

### Fixed-point math

Modern CPU FPUs are fast, but fixed-point representation has advantages in specific inner loops:

- **Deterministic rounding** — no IEEE 754 rounding-mode variations.
- **Integer throughput** — integer ALU ports are often less contended than FPU ports on superscalar CPUs.
- **Predictable error** — error is a constant ±½ LSB, unlike floating point where it grows with magnitude.

The **16.16 format** (16 integer bits + 16 fractional bits) is common in scanline rasterisers:

```c
int32_t fixed = (int32_t)(value * 65536.0f);
// Addition and subtraction are free (just integer ops)
// Multiplication requires a 64-bit intermediate and a right-shift by 16
int32_t product = (int32_t)(((int64_t)a * (int64_t)b) >> 16);
```

Classic FreeType and early Skia relied heavily on fixed-point for edge-tracking and anti-aliasing. If you are writing a software triangle rasteriser, evaluate whether your innermost subpixel stepping loop is faster in float or 16.16 — the answer depends on the specific CPU microarchitecture.

## Operating system and display

Your library must ultimately place pixels where a human can see them.

### Framebuffer

The framebuffer is the lowest-level abstraction the OS graphics stack exposes: a region of memory the display controller scans out to the physical screen. How you acquire one:

| Platform | Mechanism |
|---|---|
| Linux/X11 | `XImage` (shared memory via MIT-SHM extension) or `DRM/KMS` (direct rendering manager) |
| Linux/Wayland | `wl_shm` buffer pool or DMA-BUF |
| Windows | `CreateDIBSection` (GDI device-independent bitmap) |
| macOS | `CGContext` backed by `CGBitmapContextCreate` |

After drawing, you **commit** the buffer — a blit to the front buffer or a page-flip that swaps scanout memory.

### Double buffering and vertical synchronisation

Drawing directly into the front buffer (the memory currently scanned to the monitor) causes visible **tearing**: the scanout controller reaches a region you are still writing. The fix is **double buffering**:

| Buffer | Role |
|---|---|
| Front buffer | Currently displayed by the display controller |
| Back buffer | Being drawn into, invisible to the user |

When drawing is complete you **swap** the front and back buffers. This swap is synchronised to the display's **vertical blanking interval** (VSync) — the moment between frames when the scanout controller resets to the top-left corner. Swapping during vblank eliminates tearing.

Modern compositing window systems (Windows DWM, macOS Quartz Compositor, Wayland compositors) manage their own buffers. Your library typically renders into a shared memory region and notifies the compositor; the compositor handles the swap.

### Window system handles

Each platform provides an opaque handle to embed rendering into a window:

| Platform | Handle type |
|---|---|
| Windows | `HWND` (window handle) |
| macOS | `NSView*` |
| X11 | `Window` (32-bit XID) |
| Wayland | `wl_surface*` |

A portable graphics library abstracts these into an internal surface type and provides platform-specific constructors. The handle is needed to query pixel dimensions, pixel format, and DPI scaling factor.

## GPU hardware acceleration

When software rasterisation is too slow, you move work to the GPU through one of the hardware graphics APIs.

### The graphics pipeline

Every GPU API abstracts the same logical pipeline:

```
Vertex Fetch → Vertex Shader → Rasteriser → Fragment Shader → Blender → Framebuffer
```

| Stage | What happens |
|---|---|
| **Vertex Fetch** | Pulls vertex attributes (position, colour, UV) from bound buffers |
| **Vertex Shader** | Transforms vertices (model → world → clip space). Runs once per vertex |
| **Rasteriser** | Converts triangle geometry into pixel-sized fragments. Fixed-function hardware |
| **Fragment Shader** | Computes colour and opacity at each fragment. Runs once per fragment (roughly per pixel) |
| **Blender** | Combines fragment output with the existing framebuffer pixel (alpha blending, depth testing, stencil ops) |

Your 2D fill and blending logic must be expressed as vertex and fragment shader programs that run on thousands of GPU threads in parallel.

### Shader languages

| API | Shader language | Notes |
|---|---|---|
| OpenGL / GLES | GLSL | `#version 450`, compiled to SPIR-V or consumed directly |
| Vulkan | GLSL → SPIR-V, or HLSL | SPIR-V is the binary intermediate format |
| Direct3D 11/12 | HLSL | `float4`, `float4x4`, compiled to DXBC or DXIL |
| Metal | MSL (Metal Shading Language) | C++14-based, compiled to Apple GPU binary |

Shaders are small, stateless programs. They cannot persist data across invocations (except via atomics into buffers). All inputs come from buffers and push constants; all outputs go to framebuffer attachments or buffer storage.

### Command buffer model

In modern APIs (Vulkan, Metal, D3D12), the CPU does not draw directly. Instead it **records** a command buffer — a serialised list of draw instructions — and submits it to the GPU work queue:

```
CPU: Record "bind pipeline, bind vertex buffer, draw 6 vertices" → Command Buffer
GPU: Dequeue command buffer → execute → signal fence when done
```

This decouples CPU and GPU timelines. The CPU can record the next frame while the GPU executes the current one (pipelined parallelism). The trade-off is explicit synchronisation: you must fence between a CPU write to a buffer and a GPU read from it, or you will read half-written data.

## Compiler and C++ reality

When an inner loop executes tens of millions of times per frame, language-level abstractions that look cheap can be fatal.

### Pointer aliasing

Two pointers of the same type may legally point to the same memory in C/C++. The compiler therefore must reload values through a pointer after any write through a potentially-aliasing pointer, preventing vectorisation and enregistration.

When source and destination buffers in a blend or copy kernel are provably disjoint, annotate them with `__restrict` (or `restrict`):

```c
void blend(uint32_t * __restrict dst,
           const uint32_t * __restrict src,
           int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = blend_pixel(dst[i], src[i]); // compiler can keep dst[i] in a register
    }
}
```

Without `__restrict`, the compiler must re-read `dst[i]` after the blend because `src` could alias `dst + offset`. The generated code will be scalar and much slower.

### Inlining and call overhead

A function call (stack frame setup, register save/restore, branch predictor state) costs tens of cycles. Inside an inner loop that runs millions of times per frame, cumulative overhead is substantial.

| Abuse pattern | Fix |
|---|---|
| Virtual function per pixel | Template the pixel format and resolve at compile time |
| Function-pointer dispatch per span | Use a `switch` on a small enum; the compiler can build a jump table |
| Separate function for 3-line arithmetic | Mark it `static inline` or define it as a macro |

**Check your generated assembly** for inner loops. If you see `call` instructions inside your hottest pixel-processing loop, restructure until they disappear.

### Volatile and memory ordering

The `volatile` keyword prevents the compiler from reordering or eliding memory accesses. It is correct for MMIO (memory-mapped device registers) and signal handlers. It is **incorrect** for multithreaded synchronisation — `volatile` does not insert memory barriers and does not guarantee atomicity. Use `std::atomic` with the appropriate `std::memory_order` for lock-free ring buffers and inter-thread signalling.

## Further reading

- [Rendering pipeline](rendering-pipeline.md) — how flux applies these concepts end-to-end.
- [Why GPUs use triangles](why-triangles.md) — why everything becomes a triangle mesh.
- Agner Fog's optimisation manuals — the definitive reference for instruction timing and microarchitecture details.
- Intel Intrinsics Guide — searchable reference for SSE/AVX compiler intrinsics.
