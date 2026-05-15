# ADR-0004: Context-Driven Callback Logging

- **Status**: Accepted
- **Date**: 2026-05-15
- **Deciders**: flux maintainers

## Context

flux needs a diagnostic logging mechanism that works across multiple backends (Vulkan, software), multiple threads, and diverse host applications (games, GUI toolkits, headless servers).  A traditional global `fprintf(stderr, ...)` approach violates two core design goals:

1. **No hidden global state.**  A library embedded inside a larger engine must not clobber the host's own logging system.
2. **Host-controlled output.**  Some consumers want structured JSON logs; others need Android `__android_log_print`; others want logs routed to an in-game console.

We also observed that formatting strings inside the library can trigger allocations (`vasprintf`, `malloc` in `glibc`), which conflicts with the goal of predictable, real-time-safe behaviour.

## Decision

Adopt a **context-driven, callback-based** logging architecture with the following properties:

1. **Per-context logger.**  `flux_context_desc` carries `log`, `log_user`, and `min_log_level`.  Every log emitted by a context (or its derived surfaces, paths, images, etc.) routes through that context's callback.
2. **Stack-only formatting.**  `flux_log_impl` formats into `char buf[1024]` on the stack using `vsnprintf`.  No heap allocation occurs on the logging path.
3. **External synchronisation contract.**  The library guarantees that calling the user's `flux_log_fn` is itself thread-safe and lock-free (it merely loads a function pointer and invokes it).  If the callback writes to a shared resource such as `stdout` or a file, the user must provide any required mutex inside their own callback.
4. **Silent by default if no callback is supplied.**  When `desc.log == NULL`, the library uses a minimal built-in sink that writes to `stderr` with a timestamp and level tag.  This gives new users immediate feedback without forcing every tutorial to implement a custom logger.
5. **Debug / trace elision.**  `FLUX_LOGD` and `FLUX_LOGT` compile to no-ops in `NDEBUG` builds, eliminating both the format call and the callback overhead in release binaries.

## Alternatives considered

- **Global logger handle (spdlog-style).**  Rejected because it introduces global state and makes it impossible for two unrelated flux contexts in the same process to log to different destinations.
- **Return error strings from every function.**  Rejected because it complicates the public API (`flux_result` would need to carry an allocated string or a fixed-size buffer) and does not help with non-fatal warnings or debug traces.
- **Structured `G2dLogMessage` struct passed to the callback.**  Considered during design review.  Rejected in favour of the current flat `(level, file, line, fmt, msg, user)` signature because the flat form is easier to bind to foreign languages (Zig, Python ctypes, etc.) and avoids an extra allocation or lifetime question for the struct pointer.  The helper header `flux_log_helper.h` provides a ready-made styled sink for users who do not wish to write their own callback.

## Consequences

- **Positive:** Host applications have total control over log routing, filtering, and formatting.  The library remains embeddable in sandboxed or multi-context environments.
- **Positive:** The 1024-byte stack buffer bounds worst-case stack usage and eliminates allocator failure modes on the logging path.
- **Trade-off:** Users who need `printf`-style formatting in the callback must re-parse the `fmt` string themselves.  Most users simply forward the pre-formatted `msg` argument.
- **Trade-off:** The flat callback signature does not carry an explicit `error_code` field.  Error codes are instead conveyed through the `flux_result` return value of the failing API call; the log message provides human-readable context.
