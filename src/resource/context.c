/*
 * Context: version, capabilities, allocator, logger, and lifecycle.
 *
 * The allocator and logger are the two pluggable hooks every other
 * module routes through. Setting them at context creation time means
 * the entire object graph for that context inherits them.
 */
#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Version                                                           */
/* ------------------------------------------------------------------ */

void flux_version(int *major, int *minor, int *patch)
{
    if (major) *major = FLUX_VERSION_MAJOR;
    if (minor) *minor = FLUX_VERSION_MINOR;
    if (patch) *patch = FLUX_VERSION_PATCH;
}

uint32_t flux_version_number(void)
{
    return FLUX_VERSION_NUMBER;
}

bool flux_version_check(int major, int minor, int patch)
{
    if (major != FLUX_VERSION_MAJOR) return false;
    if (minor > FLUX_VERSION_MINOR)  return false;
    if (minor == FLUX_VERSION_MINOR && patch > FLUX_VERSION_PATCH) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Result strings                                                    */
/* ------------------------------------------------------------------ */

const char *flux_result_string(flux_result r)
{
    switch (r) {
    case FLUX_OK:                       return "OK";
    case FLUX_ERROR_INVALID_ARGUMENT:   return "INVALID_ARGUMENT";
    case FLUX_ERROR_OUT_OF_MEMORY:      return "OUT_OF_MEMORY";
    case FLUX_ERROR_OUT_OF_RANGE:       return "OUT_OF_RANGE";
    case FLUX_ERROR_INVALID_STATE:      return "INVALID_STATE";
    case FLUX_ERROR_UNSUPPORTED:        return "UNSUPPORTED";
    case FLUX_ERROR_BACKEND_FAILURE:    return "BACKEND_FAILURE";
    case FLUX_ERROR_DEVICE_LOST:        return "DEVICE_LOST";
    case FLUX_ERROR_SURFACE_LOST:       return "SURFACE_LOST";
    }
    return "UNKNOWN";
}

/* ------------------------------------------------------------------ */
/*  Capabilities                                                      */
/* ------------------------------------------------------------------ */

void flux_get_capabilities(flux_capabilities *caps)
{
    if (!caps || caps->size < sizeof(uint32_t)) return;

    flux_capabilities filled = {
        .size                = caps->size,
        .has_vulkan          = true,
        .has_software        = true,
        .has_stencil         = true,
        .has_msaa            = false,
        .max_gradient_stops  = FLUX_MAX_GRADIENT_STOPS,
        .max_image_size      = 16384,
        .max_surface_size    = 16384,
    };
    size_t copy = caps->size < sizeof(filled) ? caps->size : sizeof(filled);
    memcpy(caps, &filled, copy);
    caps->size = copy < sizeof(uint32_t) ? caps->size : (uint32_t)copy;
}

/* ------------------------------------------------------------------ */
/*  Default allocator                                                 */
/* ------------------------------------------------------------------ */

static void *default_alloc(size_t bytes, void *user)
{
    (void)user;
    return malloc(bytes);
}

static void *default_realloc(void *ptr, size_t old_bytes, size_t new_bytes, void *user)
{
    (void)old_bytes; (void)user;
    return realloc(ptr, new_bytes);
}

static void default_free(void *ptr, void *user)
{
    (void)user;
    free(ptr);
}

static const flux_allocator k_default_allocator = {
    .alloc   = default_alloc,
    .realloc = default_realloc,
    .free    = default_free,
    .user    = NULL,
};

/* ------------------------------------------------------------------ */
/*  Allocator routing                                                 */
/* ------------------------------------------------------------------ */

void *flux_alloc(flux_context *ctx, size_t bytes)
{
    const flux_allocator *a = ctx ? &ctx->allocator : &k_default_allocator;
    return a->alloc(bytes, a->user);
}

void *flux_realloc(flux_context *ctx, void *ptr, size_t old_bytes, size_t new_bytes)
{
    const flux_allocator *a = ctx ? &ctx->allocator : &k_default_allocator;
    return a->realloc(ptr, old_bytes, new_bytes, a->user);
}

void flux_free(flux_context *ctx, void *ptr)
{
    if (!ptr) return;
    const flux_allocator *a = ctx ? &ctx->allocator : &k_default_allocator;
    a->free(ptr, a->user);
}

void *flux_calloc(flux_context *ctx, size_t count, size_t element_size)
{
    size_t total = count * element_size;
    void *p = flux_alloc(ctx, total);
    if (p) memset(p, 0, total);
    return p;
}

flux_context *const FLUX_DEFAULT_CTX = NULL;

/* ------------------------------------------------------------------ */
/*  Default stderr log sink                                           */
/* ------------------------------------------------------------------ */

static const char *level_tag(flux_log_level lvl)
{
    switch (lvl) {
    case FLUX_LOG_TRACE: return "TRACE";
    case FLUX_LOG_DEBUG: return "DEBUG";
    case FLUX_LOG_INFO:  return "INFO ";
    case FLUX_LOG_WARN:  return "WARN ";
    case FLUX_LOG_ERROR: return "ERROR";
    }
    return "?????";
}

static void default_stderr_sink(flux_log_level lvl,
                                const char *file, int line,
                                const char *fmt, const char *msg, void *user)
{
    (void)fmt; (void)user;
    char ts[32] = "";
    time_t now = time(NULL);
    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);

    flockfile(stderr);
    fprintf(stderr, "[%s] %s %s:%d: %s\n",
            ts, level_tag(lvl), file ? file : "?", line, msg);
    funlockfile(stderr);
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */

flux_result flux_context_create(const flux_context_desc *desc, flux_context **out_ctx)
{
    if (!out_ctx) return FLUX_ERROR_INVALID_ARGUMENT;

    /* desc is optional (NULL = all defaults). */
    flux_context_desc resolved = {0};
    if (desc) {
        if (desc->size < sizeof(uint32_t)) return FLUX_ERROR_INVALID_ARGUMENT;
        size_t copy = desc->size < sizeof(resolved) ? desc->size : sizeof(resolved);
        memcpy(&resolved, desc, copy);
    }

    flux_allocator alloc = resolved.allocator;
    bool any_alloc_set = alloc.alloc || alloc.realloc || alloc.free;
    if (any_alloc_set) {
        if (!alloc.alloc || !alloc.realloc || !alloc.free)
            return FLUX_ERROR_INVALID_ARGUMENT;
    } else {
        alloc = k_default_allocator;
    }

    flux_context *ctx = alloc.alloc(sizeof(*ctx), alloc.user);
    if (!ctx) return FLUX_ERROR_OUT_OF_MEMORY;
    memset(ctx, 0, sizeof(*ctx));

    flux_ref_init(&ctx->ref_count);
#ifndef NDEBUG
    ctx->owner_thread = thrd_current();
#endif
    ctx->allocator     = alloc;
    ctx->log           = resolved.log ? resolved.log : default_stderr_sink;
    ctx->log_user      = resolved.log_user;
    ctx->min_log_level = resolved.min_log_level ? resolved.min_log_level : FLUX_LOG_INFO;

    *out_ctx = ctx;
    return FLUX_OK;
}

flux_context *flux_context_retain(flux_context *ctx)
{
    if (!ctx) return NULL;
    flux_ref_retain(&ctx->ref_count);
    return ctx;
}

void flux_context_release(flux_context *ctx)
{
    if (!ctx) return;
    if (flux_ref_release(&ctx->ref_count) == 0) {
        flux_allocator a = ctx->allocator;
        a.free(ctx, a.user);
    }
}

flux_log_level flux_context_get_log_level(const flux_context *ctx)
{
    return ctx ? ctx->min_log_level : FLUX_LOG_INFO;
}

const flux_allocator *flux_context_get_allocator(const flux_context *ctx)
{
    return ctx ? &ctx->allocator : &k_default_allocator;
}

/* ------------------------------------------------------------------ */
/*  Logging implementation                                            */
/* ------------------------------------------------------------------ */

void flux_log_impl(const flux_context *ctx, flux_log_level lvl,
                   const char *file, int line,
                   const char *fmt, ...)
{
    if (!ctx || !ctx->log) return;
    if (lvl < ctx->min_log_level) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ctx->log(lvl, file, line, fmt, buf, ctx->log_user);
}

/* ------------------------------------------------------------------ */
/*  Pixel format size                                                 */
/* ------------------------------------------------------------------ */

uint32_t flux_pixel_format_bytes(flux_pixel_format fmt)
{
    switch (fmt) {
    case FLUX_FMT_BGRA8_UNORM:
    case FLUX_FMT_RGBA8_UNORM:  return 4;
    case FLUX_FMT_A8_UNORM:     return 1;
    }
    return 0;
}
