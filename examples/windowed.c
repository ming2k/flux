/*
 * windowed.c — GLFW windowed rendering with the Vulkan backend.
 *
 * Build: meson compile -C build
 * Run:   ./build/examples/windowed
 *
 * Demonstrates the application-owned-Vulkan-device pattern: the
 * caller (or, for convenience, flux_vulkan_device_create) builds the
 * VkInstance / VkDevice / queues, then hands them to flux for
 * rendering.
 */
#include <flux/flux.h>
#include <flux/flux_vulkan.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void glfw_error_cb(int code, const char *desc)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

static int die(const char *what, flux_result r)
{
    fprintf(stderr, "%s: %s\n", what, flux_result_string(r));
    return 1;
}

int main(void)
{
    glfwSetErrorCallback(glfw_error_cb);
    if (!glfwInit() || !glfwVulkanSupported()) {
        fprintf(stderr, "Vulkan not available\n");
        glfwTerminate();
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *win = glfwCreateWindow(800, 600, "flux windowed example", NULL, NULL);
    if (!win) { glfwTerminate(); return 1; }

    uint32_t glfw_ext_count = 0;
    const char **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);

    flux_vulkan_device vk_dev = { .size = sizeof(vk_dev) };
    flux_result r = flux_vulkan_device_create(
        &(flux_context_desc){ .size = sizeof(flux_context_desc) },
        glfw_exts, glfw_ext_count, &vk_dev);
    if (r != FLUX_OK) { glfwDestroyWindow(win); glfwTerminate(); return die("vk_device", r); }

    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(vk_dev.instance, win, NULL, &vk_surface) != VK_SUCCESS) {
        flux_vulkan_device_destroy(&vk_dev);
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }

    flux_context *ctx = NULL;
    if ((r = flux_context_create(&(flux_context_desc){ .size = sizeof(flux_context_desc) },
                                 &ctx)) != FLUX_OK) {
        vkDestroySurfaceKHR(vk_dev.instance, vk_surface, NULL);
        flux_vulkan_device_destroy(&vk_dev);
        glfwDestroyWindow(win);
        glfwTerminate();
        return die("context_create", r);
    }

    flux_surface *surface = NULL;
    if ((r = flux_surface_create_vulkan(ctx, &vk_dev, vk_surface,
                                        800, 600, FLUX_CS_SRGB,
                                        &surface)) != FLUX_OK) {
        flux_context_release(ctx);
        vkDestroySurfaceKHR(vk_dev.instance, vk_surface, NULL);
        flux_vulkan_device_destroy(&vk_dev);
        glfwDestroyWindow(win);
        glfwTerminate();
        return die("surface_create_vulkan", r);
    }

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        if (w > 0 && h > 0) flux_surface_resize(surface, w, h);

        flux_canvas *c = flux_surface_acquire(surface);
        flux_canvas_clear(c, flux_color_rgba(30, 30, 30, 255));

        float t = (float)glfwGetTime();
        float cx = 400.0f + sinf(t)        * 200.0f;
        float cy = 300.0f + cosf(t * 0.7f) * 150.0f;

        flux_paint *paint = NULL;
        (void)flux_paint_create(ctx, &paint);
        flux_paint_set_color(paint, flux_color_rgba(0, 200, 255, 255));

        flux_path *box = NULL;
        (void)flux_path_create(ctx, &box);
        flux_path_add_round_rect(box, &(flux_rect){ cx - 50, cy - 50, 100, 100 }, 15.0f);
        (void)flux_canvas_fill_path(c, box, paint);

        flux_paint *circle_paint = NULL;
        (void)flux_paint_create(ctx, &circle_paint);
        flux_paint_set_color(circle_paint, flux_color_rgba(255, 100, 100, 180));
        flux_path *circle = NULL;
        (void)flux_path_create(ctx, &circle);
        flux_path_add_circle(circle, cx + 100.0f, cy, 40.0f);
        (void)flux_canvas_fill_path(c, circle, circle_paint);

        flux_surface_present(surface);

        flux_path_release(box);
        flux_path_release(circle);
        flux_paint_release(paint);
        flux_paint_release(circle_paint);
    }

    flux_surface_release(surface);
    flux_context_release(ctx);
    vkDestroySurfaceKHR(vk_dev.instance, vk_surface, NULL);
    flux_vulkan_device_destroy(&vk_dev);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
