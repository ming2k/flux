#include "flux/flux_vulkan.h"
#include "internal.h"

#include <time.h>

static const char *log_basename(const char *path)
{
    const char *p = path;
    const char *base = path;
    while (*p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
        p++;
    }
    return base;
}

static void default_log(fx_log_level lvl, const char *file, int line,
                        [[maybe_unused]] const char *fmt, const char *msg,
                        [[maybe_unused]] void *user)
{
    const char *tag = "?";
    switch (lvl) {
        case FX_LOG_TRACE: tag = "T"; break;
        case FX_LOG_DEBUG: tag = "D"; break;
        case FX_LOG_INFO:  tag = "I"; break;
        case FX_LOG_WARN:  tag = "W"; break;
        case FX_LOG_ERROR: tag = "E"; break;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = 0;
        ts.tv_nsec = 0;
    }
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);

    char timebuf[64];
    snprintf(timebuf, sizeof(timebuf),
             "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000L);

    flockfile(stderr);
    fprintf(stderr, "[%s][flux %s][%s:%d] %s\n",
            timebuf, tag, log_basename(file), line, msg);
    funlockfile(stderr);
}

void fx_log_impl(const fx_context *ctx, fx_log_level lvl,
                 const char *file, int line,
                 const char *fmt, ...)
{
    if (!file || !fmt) return;

    fx_log_level min_lvl = FX_LOG_INFO;
    fx_log_fn cb = nullptr;
    void *user = nullptr;

    if (ctx) {
        min_lvl = ctx->min_log_level;
        cb = ctx->log;
        user = ctx->log_user;
    }

    if (lvl < min_lvl) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) {
        buf[0] = '\0';
    } else if ((size_t)n >= sizeof(buf)) {
        /* Truncated: overwrite tail with "..." */
        if (sizeof(buf) > 4) {
            buf[sizeof(buf) - 4] = '.';
            buf[sizeof(buf) - 3] = '.';
            buf[sizeof(buf) - 2] = '.';
            buf[sizeof(buf) - 1] = '\0';
        }
    }

    if (cb) {
        cb(lvl, file, line, fmt, buf, user);
    } else {
        default_log(lvl, file, line, fmt, buf, user);
    }
}

static bool env_flag(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && v[0] != '0';
}

fx_context *fx_context_create(const fx_context_desc *desc)
{
    fx_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return nullptr;
    if (desc) {
        ctx->log           = desc->log;
        ctx->log_user      = desc->log_user;
        ctx->min_log_level = desc->min_log_level;
    } else {
        ctx->min_log_level = FX_LOG_INFO;
    }

    bool want_validation = desc && desc->enable_validation;
    if (!want_validation) want_validation = env_flag("FX_ENABLE_VALIDATION");

    const char *inst_exts[] = {
        "VK_KHR_surface",
    };
    if (!fx_instance_create(ctx,
                            desc ? desc->app_name : nullptr,
                            want_validation,
                            inst_exts,
                            (uint32_t)(sizeof(inst_exts)/sizeof(*inst_exts)))) {
        free(ctx);
        return nullptr;
    }

    [[maybe_unused]] VkPipelineCacheCreateInfo pcci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    /* Device may not exist yet; pipeline_cache is created on first surface. */

    /* Defer device creation until the first surface: surface support is
     * a queue-family selection input. */
    return ctx;
}

VkInstance fx_context_get_instance(fx_context *ctx)
{
    return ctx ? ctx->instance : VK_NULL_HANDLE;
}

bool fx_context_get_device_caps(const fx_context *ctx, fx_device_caps *out_caps)
{
    VkFormatProperties fmt_props;

    if (!ctx || !ctx->phys || !out_caps) return false;

    memset(out_caps, 0, sizeof(*out_caps));
    out_caps->validation_enabled = ctx->validation_enabled;
    out_caps->graphics_queue = ctx->device != VK_NULL_HANDLE;
    out_caps->present_queue = ctx->queue_supports_present;
    out_caps->api_version = ctx->phys_props.apiVersion;
    out_caps->max_image_dimension_2d =
        ctx->phys_props.limits.maxImageDimension2D;
    out_caps->max_color_attachments =
        ctx->phys_props.limits.maxColorAttachments;

    vkGetPhysicalDeviceFormatProperties(ctx->phys, VK_FORMAT_R8G8B8A8_UNORM,
                                        &fmt_props);
    out_caps->sampled_images =
        (fmt_props.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    out_caps->storage_images =
        (fmt_props.optimalTilingFeatures &
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
    return true;
}

void fx_context_destroy(fx_context *ctx)
{
    if (!ctx) return;
    if (ctx->atlas.image) fx_image_destroy(ctx->atlas.image);
    free(ctx->atlas.entries);
    fx_device_shutdown(ctx);
    free(ctx);
}
