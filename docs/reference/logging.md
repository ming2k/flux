# Logging Reference

flux uses a context-driven callback model: every diagnostic message is routed through the `flux_log_fn` registered on the `flux_context`.  The library itself performs no I/O; the caller decides where logs go.

## Log levels

| Enum | Value | Typical use |
|---|---|---|
| `FLUX_LOG_TRACE` | 0 | Verbose execution tracing (e.g. every draw call). Compiled out in `NDEBUG` builds. |
| `FLUX_LOG_DEBUG` | 1 | Internal state dumps, buffer sizes, timing. Compiled out in `NDEBUG` builds. |
| `FLUX_LOG_INFO`  | 2 | Lifecycle events (context created, surface resized, atlas grown). |
| `FLUX_LOG_WARN`  | 3 | Recoverable anomalies (fallback path taken, feature unsupported). |
| `FLUX_LOG_ERROR` | 4 | Runtime failures (out of memory, device lost, invalid argument). |

Messages with a level **strictly lower** than `min_log_level` are dropped before formatting.

## Callback signature

```c
typedef void (*flux_log_fn)(flux_log_level level,
                            const char *file, int line,
                            const char *fmt, const char *msg,
                            void *user);
```

| Parameter | Meaning |
|---|---|
| `level` | Severity of the message. |
| `file`  | Source file inside flux where the log was emitted. Stable within a release; may shift across versions. |
| `line`  | Line number in `file`. |
| `fmt`   | Original `printf` format string.  Useful for structured loggers that want to re-parse arguments (advanced). |
| `msg`   | Fully-formatted message text. Safe to print or forward as-is. Always nul-terminated. |
| `user`  | Opaque pointer copied from `flux_context_desc.log_user`. |

## Context descriptor fields

```c
typedef struct flux_context_desc {
    uint32_t        size;          /* sizeof(flux_context_desc) */
    flux_allocator  allocator;
    flux_log_fn     log;           /* NULL = use built-in stderr sink */
    void           *log_user;      /* Passed back to log callback */
    flux_log_level  min_log_level; /* Default = FLUX_LOG_INFO if 0 */
} flux_context_desc;
```

## Internal macros

These macros are used inside the flux implementation.  They are **not** part of the public API, but they illustrate the calling convention:

| Macro | Level | Stripped in `NDEBUG`? |
|---|---|---|
| `FLUX_LOGE(ctx, ...)` | `FLUX_LOG_ERROR` | No |
| `FLUX_LOGW(ctx, ...)` | `FLUX_LOG_WARN`  | No |
| `FLUX_LOGI(ctx, ...)` | `FLUX_LOG_INFO`  | No |
| `FLUX_LOGD(ctx, ...)` | `FLUX_LOG_DEBUG` | Yes |
| `FLUX_LOGT(ctx, ...)` | `FLUX_LOG_TRACE` | Yes |

All macros pass `__FILE__` and `__LINE__` automatically and forward to `flux_log_impl`, which performs the stack-formatting and level filtering.

## Built-in sinks

### Default stderr sink

If `desc.log` is `NULL` at context creation, flux installs an internal sink that prints:

```
[2026-05-15T14:32:01] ERROR src/resource/context.c:214: out of memory
```

This sink uses `flockfile` / `funlockfile` for thread-safe stderr access and performs no heap allocation.

### Styled console helper

`<flux/flux_log_helper.h>` provides an optional replacement sink:

```c
void flux_default_console_logger(flux_log_level level,
                                 const char *file, int line,
                                 const char *fmt, const char *msg,
                                 void *user);
```

Usage:

```c
flux_context_desc desc = {
    .size = sizeof(desc),
    .log  = flux_default_console_logger,
};
flux_context_create(&desc, &ctx);
```

Output characteristics:

- Format: `[HH:MM:SS] [LEVEL] file:line: message`
- ANSI colours: green for INFO, yellow for WARN, red for ERROR, cyan for DEBUG, gray for TRACE.
- LEVEL tag is padded to exactly 5 characters for vertical alignment (`INFO `, `WARN `).
- On Windows, `ENABLE_VIRTUAL_TERMINAL_PROCESSING` is enabled automatically so that colours render in modern terminals.

## Thread-safety contract

- **Library side:** Emitting a log message (formatting into the stack buffer and invoking the callback pointer) is lock-free and safe to call from any thread that owns a valid `flux_context*`.
- **User side:** If your callback writes to a shared resource (e.g. a global file descriptor or a circular buffer), you must serialise access inside the callback yourself.  flux does not impose a mutex because many callbacks are naturally thread-local (e.g. Android logcat, `per-thread` file handles).
