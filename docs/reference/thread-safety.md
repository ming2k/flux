# Thread Safety

flux does not use internal locking. All objects are designed for single-threaded access per instance. Violating these rules results in data races, GPU synchronization errors, or undefined behavior.

## The Golden Rule

**One context, one thread.** A single OS thread must own all API calls targeting a given `flux_context` and its derived surfaces, images, paths, and fonts.

## Object-level guarantees

| Object | Thread safe? | Rule |
|---|---|---|
| `flux_context` | **No** | Create, configure, and destroy from one thread only. |
| `flux_surface` | **No** | Acquire, record, and present from the same thread that owns the context. |
| `flux_canvas` | **No** | Valid only between `flux_surface_acquire` and `flux_surface_present` on the owning thread. |
| `flux_image` | **No** | Create, update, and destroy from the context thread. Do not sample from one surface while updating from another. |
| `flux_path` | **No** | Build and destroy from the context thread. |
| `flux_glyph_run` | **No** | Append glyphs and draw from the context thread. |
| `flux_gradient` | **No** | Create and destroy from the context thread. |

## What you CAN do across threads

1. **Multiple independent contexts.** Each `flux_context` lives on its own thread with its own Vulkan instance and device. There is no shared global mutable state.

2. **CPU-side preparation.** You may construct pixel buffers, path data, or glyph runs on worker threads, but you must synchronize and hand them off to the context thread before calling any flux API that consumes them.

3. **Read-only inspection.** Calling `flux_path_get_bounds` or `flux_canvas_op_count` from a different thread is safe only if no mutating API call is in flight on the owning thread.

## Common pitfalls

### Recording from multiple threads

```c
/* WRONG: two threads record into the same canvas */
flux_canvas *c = flux_surface_acquire(s);
thread_A_draw(c);   /* thread A */
thread_B_draw(c);   /* thread B -- race on op_count and op array */
flux_surface_present(s);
```

**Fix:** Record all ops from one thread, or partition the scene into per-thread command lists and merge on the context thread.

### Updating an image while it is sampled

```c
/* WRONG: updating an image while a previous frame is still reading it */
flux_draw_image(c, icon, &src, &dst);   /* frame N references icon */
flux_surface_present(s);
flux_image_update(icon, new_pixels, stride); /* may race with GPU sampling */
```

**Fix:** Wait for the frame to complete, or double-buffer the image resource. `flux_image_update` internally waits on the image's `last_use_fence`, but this only protects against the same context's in-flight frames. External synchronization is still required if you are calling `flux_image_update` from a thread that is not the context thread.

### Resizing from an event thread

```c
/* WRONG: compositor callback resizes surface while the render loop is acquiring */
void on_configure(int w, int h) {
    flux_surface_resize(s, w, h);  /* event thread */
}

/* render thread */
flux_canvas *c = flux_surface_acquire(s); /* may race with resize */
```

**Fix:** Queue resize events and process them on the render thread before `flux_surface_acquire`.

## Suggested architecture

```text
┌─────────────────────────────────────────┐
│         Main / Render Thread            │
│  ┌─────────┐  acquire / present cycle  │
│  │ context │◄─────────────────────────►│
│  │ surface │  canvas recording         │
│  │ images  │                           │
│  └─────────┘                           │
└─────────────────────────────────────────┘
                    ▲
                    │ hand off prepared data
┌─────────────────────────────────────────┐
│           Worker Thread(s)              │
│  CPU-side path building                 │
│  Asset decoding                         │
│  Text shaping (HarfBuzz)                │
└─────────────────────────────────────────┘
```

## Vulkan queue concurrency

flux uses a single graphics queue. Even if your application creates additional Vulkan queues on the same device, do not submit work that accesses flux-owned images or buffers from those queues without explicit queue-family ownership transfers.

## See also

- [Capability model](../explanation/capability-model.md) — what flux owns versus what the application owns.
- [Performance tuning](../how-to/optimize-performance.md) — batching and frame pacing recommendations.
