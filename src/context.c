#include "internal.h"

static void default_log(vg_log_level lvl, const char *msg, void *user)
{
    (void)user;
    const char *tag = "?";
    switch (lvl) {
        case VG_LOG_ERROR: tag = "E"; break;
        case VG_LOG_WARN:  tag = "W"; break;
        case VG_LOG_INFO:  tag = "I"; break;
        case VG_LOG_DEBUG: tag = "D"; break;
    }
    fprintf(stderr, "[vgfx %s] %s\n", tag, msg);
}

void vg_log(const vg_context *ctx, vg_log_level lvl, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (ctx && ctx->log) ctx->log(lvl, buf, ctx->log_user);
    else                 default_log(lvl, buf, NULL);
}

static bool env_flag(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && v[0] != '0';
}

vg_context *vg_context_create(const vg_context_desc *desc)
{
    vg_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    if (desc) {
        ctx->log      = desc->log;
        ctx->log_user = desc->log_user;
    }

    bool want_validation = desc && desc->enable_validation;
    if (!want_validation) want_validation = env_flag("VG_ENABLE_VALIDATION");

    /* Phase-0 always wants Wayland surface support. When we add
     * offscreen-only contexts we'll make this conditional. */
    const char *inst_exts[] = {
        "VK_KHR_surface",
        "VK_KHR_wayland_surface",
    };
    if (!vg_instance_create(ctx,
                            desc ? desc->app_name : NULL,
                            want_validation,
                            inst_exts,
                            (uint32_t)(sizeof(inst_exts)/sizeof(*inst_exts)))) {
        free(ctx);
        return NULL;
    }

    /* Defer device creation until the first surface: surface support is
     * a queue-family selection input. */
    if (FT_Init_FreeType(&ctx->ft_lib) != 0) {
        VG_LOGE(ctx, "failed to initialize FreeType");
        vg_device_shutdown(ctx);
        free(ctx);
        return NULL;
    }
    return ctx;
}

bool vg_context_get_device_caps(const vg_context *ctx, vg_device_caps *out_caps)
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
    out_caps->max_compute_workgroup_invocations =
        ctx->phys_props.limits.maxComputeWorkGroupInvocations;

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

void vg_context_destroy(vg_context *ctx)
{
    if (!ctx) return;
    if (ctx->ft_lib) FT_Done_FreeType(ctx->ft_lib);
    vg_device_shutdown(ctx);
    free(ctx);
}
