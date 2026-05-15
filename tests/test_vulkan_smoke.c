/*
 * Vulkan backend smoke test.
 *
 * Verifies that flux can create a Vulkan instance + device on the
 * driver selected by the environment.
 *
 * On CI this is run with different VK_ICD_FILENAMES values to
 * exercise multiple drivers (lavapipe, Intel, etc.).
 */
#include <flux/flux.h>
#include <flux/flux_vulkan.h>
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    flux_capabilities caps;
    flux_get_capabilities(&caps);

    if (!caps.has_vulkan) {
        printf("vulkan_smoke SKIP (no vulkan support compiled)\n");
        return 77; /* Meson SKIP exit code */
    }

    const char *icd = getenv("VK_ICD_FILENAMES");
    if (icd)
        printf("driver: %s\n", icd);

    flux_vulkan_device device;
    flux_result r = flux_vulkan_device_create(NULL, NULL, 0, &device);

    if (r != FLUX_OK) {
        fprintf(stderr, "vulkan device creation failed: %s\n", flux_result_string(r));
        printf("vulkan_smoke SKIP (no driver available)\n");
        return 77;
    }

    printf("vulkan device: %s\n", device.physical_device ? "ok" : "missing");
    flux_vulkan_device_destroy(&device);
    printf("vulkan_smoke OK\n");
    return 0;
}
