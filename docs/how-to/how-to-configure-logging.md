# How to configure logging

By default flux prints diagnostic messages to `stderr` with a timestamp and level tag.  This guide shows how to replace that behaviour with a custom callback, suppress logs entirely, or use the optional styled console helper.

## Use the default stderr sink

Do nothing.  If `flux_context_desc.log` is `NULL`, flux installs a minimal sink automatically.

```c
flux_context_desc desc = {
    .size = sizeof(desc),
    /* .log left NULL */
};
flux_context_create(&desc, &ctx);
```

## Suppress all logs

Set `min_log_level` higher than the highest level you want to drop.  Because `FLUX_LOG_ERROR` is the highest level (`4`), setting the minimum to `5` silences everything including errors.

```c
flux_context_desc desc = {
    .size         = sizeof(desc),
    .log          = NULL,          /* sink is unused when everything is filtered */
    .min_log_level = 5,            /* drop TRACE/DEBUG/INFO/WARN/ERROR */
};
flux_context_create(&desc, &ctx);
```

> **Note:** Errors are still returned as `flux_result` codes from the public API.  Silencing logs only removes human-readable text; it does not change return values.

## Write a custom callback

Implement `flux_log_fn` and attach it at context creation.

```c
#include <flux/flux.h>
#include <stdio.h>

static void my_logger(flux_log_level level,
                      const char *file, int line,
                      const char *fmt, const char *msg,
                      void *user)
{
    (void)fmt;
    FILE *out = (FILE *)user;

    const char *tag = "?????";
    switch (level) {
    case FLUX_LOG_TRACE: tag = "TRACE"; break;
    case FLUX_LOG_DEBUG: tag = "DEBUG"; break;
    case FLUX_LOG_INFO:  tag = "INFO "; break;
    case FLUX_LOG_WARN:  tag = "WARN "; break;
    case FLUX_LOG_ERROR: tag = "ERROR"; break;
    }

    fprintf(out, "[%s] %s:%d %s\n", tag, file, line, msg);
    fflush(out);
}

/* Usage */
flux_context_desc desc = {
    .size         = sizeof(desc),
    .log          = my_logger,
    .log_user     = stdout,
    .min_log_level = FLUX_LOG_WARN, /* only WARN and ERROR */
};
flux_context_create(&desc, &ctx);
```

### Thread-safe file logging

If the same callback is shared by multiple contexts (or the same context is used from multiple threads), protect the shared file with a mutex:

```c
#include <threads.h>

typedef struct {
    FILE *fp;
    mtx_t lock;
} log_state;

static void thread_safe_logger(flux_log_level level,
                               const char *file, int line,
                               const char *fmt, const char *msg,
                               void *user)
{
    log_state *st = (log_state *)user;
    const char *tag = (level == FLUX_LOG_ERROR) ? "ERROR" :
                      (level == FLUX_LOG_WARN)  ? "WARN " : "OTHER";

    mtx_lock(&st->lock);
    fprintf(st->fp, "[%s] %s:%d %s\n", tag, file, line, msg);
    fflush(st->fp);
    mtx_unlock(&st->lock);
}
```

## Use the styled console helper

For coloured terminal output without writing a callback yourself, include the optional helper header:

```c
#include <flux/flux_log_helper.h>

flux_context_desc desc = {
    .size = sizeof(desc),
    .log  = flux_default_console_logger,
};
flux_context_create(&desc, &ctx);
```

The helper prints messages like this (colours visible in a terminal):

```
[14:32:01] [ERROR] context.c:214: out of memory
[14:32:01] [WARN ] vulkan_rhi.c:89: swapchain sub-optimal, recreating
[14:32:02] [INFO ] surface.c:45: surface resized to 1920x1080
```

On Windows the helper automatically enables virtual-terminal processing so that ANSI escape codes render correctly in `cmd.exe` and PowerShell.

## Change the log level at runtime

flux does not provide a runtime setter for `min_log_level`; the value is fixed at context creation.  To change verbosity dynamically, store the threshold inside your `user` pointer and check it inside the callback:

```c
typedef struct {
    atomic_int threshold;
    FILE *fp;
} dynamic_log_cfg;

static void dynamic_logger(flux_log_level level,
                           const char *file, int line,
                           const char *fmt, const char *msg,
                           void *user)
{
    dynamic_log_cfg *cfg = (dynamic_log_cfg *)user;
    if (level < atomic_load(&cfg->threshold)) return;
    fprintf(cfg->fp, "%s\n", msg);
}
```

Because flux filters internally before calling your callback, you must set `min_log_level` to the lowest level you ever plan to emit (e.g. `FLUX_LOG_TRACE`) and perform the dynamic filter yourself.
