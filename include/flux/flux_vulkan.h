/*
 * flux — Vulkan interop entry points.
 *
 * Include this header only at the seam where the application hands an
 * externally-created Vulkan device or VkSurfaceKHR to flux. All core
 * drawing code includes <flux/flux.h> and never touches Vulkan types.
 *
 * flux does NOT create or own:
 *   - the VkInstance,
 *   - the VkPhysicalDevice,
 *   - the VkDevice,
 *   - graphics / present queues,
 *   - the VkSurfaceKHR (window surface).
 *
 * The application creates these (via GLFW, SDL, raw platform APIs) and
 * lends them to flux for the lifetime of the corresponding flux_surface.
 * Releasing the flux_surface does NOT destroy any of these external
 * objects; that is the caller's responsibility.
 */

#ifndef FLUX_VULKAN_H
#define FLUX_VULKAN_H

#include "flux.h"

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Application-owned Vulkan device                                   */
/* ------------------------------------------------------------------ */

typedef struct flux_vulkan_device {
    uint32_t         size;            /* sizeof(flux_vulkan_device) */
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          graphics_queue;
    VkQueue          present_queue;   /* May equal graphics_queue. */
    uint32_t         graphics_family;
    uint32_t         present_family;
} flux_vulkan_device;

/*
 * Convenience: create a Vulkan instance + device + queues sufficient for
 * flux. Advanced applications can skip this and fill flux_vulkan_device
 * directly from their own Vulkan setup.
 *
 * `instance_extensions` is the platform surface extensions list returned
 * by the windowing library (e.g., glfwGetRequiredInstanceExtensions).
 *
 * The caller owns and must destroy the returned device with
 * flux_vulkan_device_destroy AFTER releasing every flux_surface that
 * uses it.
 */
FLUX_NODISCARD FLUX_API flux_result flux_vulkan_device_create(
    const flux_context_desc *desc,
    const char *const *instance_extensions,
    uint32_t instance_extension_count,
    flux_vulkan_device *out_device);

FLUX_API void flux_vulkan_device_destroy(flux_vulkan_device *device);

/* ------------------------------------------------------------------ */
/*  Vulkan-backed surface                                             */
/* ------------------------------------------------------------------ */

/*
 * Create a windowed surface backed by the Vulkan GPU renderer.
 *
 * Lifetime contract:
 *   - flux borrows `device` and `surface` for the lifetime of the
 *     returned flux_surface. Both must outlive flux_surface_release.
 *   - On failure this returns FLUX_ERROR_BACKEND_FAILURE; flux does
 *     NOT silently fall back to software rasterisation.
 */
FLUX_NODISCARD FLUX_API flux_result flux_surface_create_vulkan(
    flux_context *ctx,
    const flux_vulkan_device *device,
    VkSurfaceKHR surface,
    int32_t width, int32_t height,
    flux_color_space cs,
    flux_surface **out_surface);

#ifdef __cplusplus
}
#endif

#endif /* FLUX_VULKAN_H */
