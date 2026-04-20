/*
 * vgfx — Wayland surface constructor.
 *
 * The caller owns the wl_display and wl_surface. The library owns the
 * VkSurfaceKHR and the swapchain attached to it.
 */
#ifndef VGFX_WAYLAND_H
#define VGFX_WAYLAND_H

#include "vgfx.h"

struct wl_display;
struct wl_surface;

#ifdef __cplusplus
extern "C" {
#endif

VGFX_API vg_surface *vg_surface_create_wayland(vg_context      *ctx,
                                               struct wl_display *display,
                                               struct wl_surface *surface,
                                               int32_t           width,
                                               int32_t           height,
                                               vg_color_space    cs);

#ifdef __cplusplus
}
#endif

#endif /* VGFX_WAYLAND_H */
