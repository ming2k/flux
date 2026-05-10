/* Minimal context implementation. */
#include "internal.h"
#include <stdlib.h>

fx_context *fx_context_create(const fx_context_desc *desc)
{
    if (!desc) return nullptr;

    fx_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return nullptr;

    ctx->log = desc->log;
    ctx->log_user = desc->log_user;
    ctx->min_log_level = desc->min_log_level;

    return ctx;
}

void fx_context_destroy(fx_context *ctx)
{
    if (!ctx) return;
    free(ctx);
}

bool fx_context_get_device_caps(const fx_context *ctx, fx_device_caps *out)
{
    if (!ctx || !out) return false;
    memset(out, 0, sizeof(*out));
    /* Software backend always reports these basics. */
    out->graphics_queue = true;
    out->present_queue = true;
    out->sampled_images = true;
    out->max_image_dimension_2d = 16384;
    return true;
}

/* Log stub */
void fx_log_impl(const fx_context *ctx, fx_log_level lvl,
                 const char *file, int line,
                 const char *fmt, ...)
{
    if (!ctx || !ctx->log) return;
    if (lvl < ctx->min_log_level) return;

    va_list ap;
    va_start(ap, fmt);
    /* Format the message */
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ctx->log(lvl, file, line, fmt, buf, ctx->log_user);
}
