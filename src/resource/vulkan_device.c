/* Optional convenience helpers for creating a Vulkan device.
 * Advanced applications can skip these and fill flux_vulkan_device directly. */
#include "internal.h"
#include "flux/flux_vulkan.h"
#include "rhi/vulkan/vk_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static bool has_layer(const char *name,
                      const VkLayerProperties *layers, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (strcmp(layers[i].layerName, name) == 0) return true;
    return false;
}

static bool has_ext(const char *name,
                    const VkExtensionProperties *exts, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (strcmp(exts[i].extensionName, name) == 0) return true;
    return false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
         [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT types,
         const VkDebugUtilsMessengerCallbackDataEXT *data,
         void *user)
{
    flux_context *ctx = user;
    flux_log_level lvl = FLUX_LOG_INFO;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        lvl = FLUX_LOG_ERROR;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        lvl = FLUX_LOG_WARN;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        lvl = FLUX_LOG_INFO;
    else
        lvl = FLUX_LOG_DEBUG;
    flux_log_impl(ctx, lvl, __FILE__, __LINE__, "[vk] %s", data->pMessage);
    return VK_FALSE;
}

static bool create_instance(VkInstance *out_inst,
                            VkDebugUtilsMessengerEXT *out_debug,
                            bool want_validation,
                            const char *const *user_exts, uint32_t n_user_exts)
{
    uint32_t n_layers = 0;
    vkEnumerateInstanceLayerProperties(&n_layers, nullptr);
    VkLayerProperties *layers = calloc(n_layers, sizeof(*layers));
    if (n_layers) vkEnumerateInstanceLayerProperties(&n_layers, layers);

    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    bool have_validation = want_validation
        && has_layer(validation_layer, layers, n_layers);
    free(layers);

    uint32_t n_exts = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n_exts, nullptr);
    VkExtensionProperties *exts = calloc(n_exts, sizeof(*exts));
    if (n_exts) vkEnumerateInstanceExtensionProperties(nullptr, &n_exts, exts);

    const char *enabled[16];
    uint32_t n_enabled = 0;

    const char *wanted[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };
    for (size_t i = 0; i < sizeof(wanted)/sizeof(wanted[0]) && n_enabled < 16; ++i) {
        if (has_ext(wanted[i], exts, n_exts))
            enabled[n_enabled++] = wanted[i];
    }

    for (uint32_t i = 0; i < n_user_exts && n_enabled < 16; ++i) {
        if (has_ext(user_exts[i], exts, n_exts))
            enabled[n_enabled++] = user_exts[i];
    }

    bool have_debug_utils = has_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, exts, n_exts);
    if (have_validation && have_debug_utils && n_enabled < 16)
        enabled[n_enabled++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    free(exts);

    VkApplicationInfo app = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "flux",
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName        = "flux",
        .engineVersion      = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion         = VK_API_VERSION_1_2,
    };
    VkInstanceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app,
        .enabledExtensionCount   = n_enabled,
        .ppEnabledExtensionNames = enabled,
    };
    const char *layer_names[1] = { validation_layer };
    if (have_validation) {
        ci.enabledLayerCount   = 1;
        ci.ppEnabledLayerNames = layer_names;
    }

    VkResult r = vkCreateInstance(&ci, nullptr, out_inst);
    if (r != VK_SUCCESS) return false;

    if (have_validation && have_debug_utils) {
        PFN_vkCreateDebugUtilsMessengerEXT create_fn =
            (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(*out_inst, "vkCreateDebugUtilsMessengerEXT");
        if (create_fn) {
            VkDebugUtilsMessengerCreateInfoEXT mi = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                .messageType =
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = debug_cb,
                .pUserData       = nullptr,
            };
            create_fn(*out_inst, &mi, nullptr, out_debug);
        }
    }
    return true;
}

static bool pick_physical_device(VkInstance inst, VkPhysicalDevice *out)
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    if (n == 0) return false;
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    vkEnumeratePhysicalDevices(inst, &n, devs);

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devs[i], &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            chosen = devs[i];
            break;
        }
    }
    if (!chosen) {
        for (uint32_t i = 0; i < n; ++i) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(devs[i], &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                chosen = devs[i];
                break;
            }
        }
    }
    if (!chosen) chosen = devs[0];

    *out = chosen;
    free(devs);
    return true;
}

static bool find_queues(VkPhysicalDevice phys, VkSurfaceKHR surface,
                        uint32_t *graphics_family, uint32_t *present_family)
{
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
    VkQueueFamilyProperties *qps = calloc(n, sizeof(*qps));
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, qps);

    *graphics_family = UINT32_MAX;
    *present_family  = UINT32_MAX;

    for (uint32_t i = 0; i < n; ++i) {
        if (qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            *graphics_family = i;
        if (surface != VK_NULL_HANDLE) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &present);
            if (present) *present_family = i;
        }
    }
    free(qps);

    if (*graphics_family == UINT32_MAX) return false;
    if (surface != VK_NULL_HANDLE && *present_family == UINT32_MAX) return false;
    return true;
}

static bool create_device(VkPhysicalDevice phys, VkSurfaceKHR surface,
                          VkDevice *out_dev,
                          uint32_t *out_gfx_family, uint32_t *out_present_family,
                          VkQueue *out_gfx_queue, VkQueue *out_present_queue)
{
    uint32_t graphics_family, present_family;
    if (!find_queues(phys, surface, &graphics_family, &present_family))
        return false;
    if (present_family == UINT32_MAX) present_family = graphics_family;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci[2] = {0};
    uint32_t n_qci = 1;
    qci[0].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[0].queueFamilyIndex = graphics_family;
    qci[0].queueCount       = 1;
    qci[0].pQueuePriorities = &priority;

    if (present_family != graphics_family) {
        qci[1].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci[1].queueFamilyIndex = present_family;
        qci[1].queueCount       = 1;
        qci[1].pQueuePriorities = &priority;
        n_qci = 2;
    }

    /* Query device extensions */
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &ext_count, nullptr);
    VkExtensionProperties *avail_exts = calloc(ext_count, sizeof(*avail_exts));
    if (ext_count) vkEnumerateDeviceExtensionProperties(phys, nullptr, &ext_count, avail_exts);

    const char *exts[4];
    uint32_t n_exts = 0;
    exts[n_exts++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    bool has_eds3 = has_ext(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME, avail_exts, ext_count);
    bool has_blend_advanced = has_ext(VK_EXT_BLEND_OPERATION_ADVANCED_EXTENSION_NAME, avail_exts, ext_count);
    free(avail_exts);

    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3_feats = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
    };
    VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT blend_advanced_feats = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT,
    };

    void *p_next = nullptr;
    if (has_eds3) {
        exts[n_exts++] = VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME;
        eds3_feats.extendedDynamicState3ColorBlendEquation = VK_TRUE;
        p_next = &eds3_feats;
    }
    if (has_blend_advanced) {
        exts[n_exts++] = VK_EXT_BLEND_OPERATION_ADVANCED_EXTENSION_NAME;
        blend_advanced_feats.advancedBlendCoherentOperations = VK_TRUE;
        if (has_eds3) {
            eds3_feats.pNext = &blend_advanced_feats;
        } else {
            p_next = &blend_advanced_feats;
        }
    }

    VkPhysicalDeviceFeatures feats = {0};

    VkDeviceCreateInfo dci = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = p_next,
        .queueCreateInfoCount    = n_qci,
        .pQueueCreateInfos       = qci,
        .enabledExtensionCount   = n_exts,
        .ppEnabledExtensionNames = exts,
        .pEnabledFeatures        = &feats,
    };

    VkResult r = vkCreateDevice(phys, &dci, nullptr, out_dev);
    if (r != VK_SUCCESS) return false;

    vkGetDeviceQueue(*out_dev, graphics_family, 0, out_gfx_queue);
    if (present_family == graphics_family)
        *out_present_queue = *out_gfx_queue;
    else
        vkGetDeviceQueue(*out_dev, present_family, 0, out_present_queue);

    *out_gfx_family    = graphics_family;
    *out_present_family = present_family;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Public convenience API                                            */
/* ------------------------------------------------------------------ */

flux_result flux_vulkan_device_create(const flux_context_desc *desc,
                                      const char *const *instance_extensions,
                                      uint32_t instance_extension_count,
                                      flux_vulkan_device *out)
{
    if (!out) return FLUX_ERROR_INVALID_ARGUMENT;
    if (desc && desc->size < offsetof(flux_context_desc, min_log_level))
        return FLUX_ERROR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    VkInstance inst = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
    if (!create_instance(&inst, &debug,
                         desc ? false : false, /* validation disabled in helpers for KISS */
                         instance_extensions, instance_extension_count)) {
        return FLUX_ERROR_BACKEND_FAILURE;
    }

    VkPhysicalDevice phys = VK_NULL_HANDLE;
    if (!pick_physical_device(inst, &phys)) {
        vkDestroyInstance(inst, nullptr);
        return FLUX_ERROR_BACKEND_FAILURE;
    }

    VkDevice dev = VK_NULL_HANDLE;
    VkQueue gfx_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    uint32_t gfx_family = 0, present_family = 0;
    if (!create_device(phys, VK_NULL_HANDLE, &dev,
                       &gfx_family, &present_family,
                       &gfx_queue, &present_queue)) {
        vkDestroyInstance(inst, nullptr);
        return FLUX_ERROR_BACKEND_FAILURE;
    }

    out->instance       = inst;
    out->physical_device = phys;
    out->device         = dev;
    out->graphics_queue = gfx_queue;
    out->present_queue  = present_queue;
    out->graphics_family = gfx_family;
    out->present_family  = present_family;
    (void)debug; /* kept alive with instance */
    return FLUX_OK;
}

void flux_vulkan_device_destroy(flux_vulkan_device *device)
{
    if (!device) return;
    if (device->device)   vkDestroyDevice(device->device, nullptr);
    if (device->instance) vkDestroyInstance(device->instance, nullptr);
    memset(device, 0, sizeof(*device));
}
