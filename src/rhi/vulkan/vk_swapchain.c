/*
 * Vulkan swapchain, render pass, framebuffer, stencil image, blur source image.
 */
#include "vk_internal.h"
#include <alloca.h>

uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t type_filter,
                          VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return 0;
}

void destroy_swapchain_resources(vk_renderer *vk)
{
    VkDevice dev = vk->device.device;
    for (uint32_t i = 0; i < FLUX_MAX_SWAPCHAIN_IMAGES; i++) {
        if (vk->sc_images[i].framebuffer) {
            vkDestroyFramebuffer(dev, vk->sc_images[i].framebuffer, nullptr);
            vk->sc_images[i].framebuffer = VK_NULL_HANDLE;
        }
        if (vk->sc_images[i].stencil_view) {
            vkDestroyImageView(dev, vk->sc_images[i].stencil_view, nullptr);
            vk->sc_images[i].stencil_view = VK_NULL_HANDLE;
        }
        if (vk->sc_images[i].stencil_image) {
            vkDestroyImage(dev, vk->sc_images[i].stencil_image, nullptr);
            vk->sc_images[i].stencil_image = VK_NULL_HANDLE;
        }
        if (vk->sc_images[i].stencil_mem) {
            vkFreeMemory(dev, vk->sc_images[i].stencil_mem, nullptr);
            vk->sc_images[i].stencil_mem = VK_NULL_HANDLE;
        }
        if (vk->sc_images[i].view) {
            vkDestroyImageView(dev, vk->sc_images[i].view, nullptr);
            vk->sc_images[i].view = VK_NULL_HANDLE;
        }
        vk->sc_images[i].image = VK_NULL_HANDLE;
    }
    vk->image_count = 0;

    if (vk->swapchain) {
        vkDestroySwapchainKHR(dev, vk->swapchain, nullptr);
        vk->swapchain = VK_NULL_HANDLE;
    }
    if (vk->render_pass) {
        vkDestroyRenderPass(dev, vk->render_pass, nullptr);
        vk->render_pass = VK_NULL_HANDLE;
    }
    if (vk->blur_render_pass) {
        vkDestroyRenderPass(dev, vk->blur_render_pass, nullptr);
        vk->blur_render_pass = VK_NULL_HANDLE;
    }
    if (vk->blur_src_view) {
        vkDestroyImageView(dev, vk->blur_src_view, nullptr);
        vk->blur_src_view = VK_NULL_HANDLE;
    }
    if (vk->blur_src_image) {
        vkDestroyImage(dev, vk->blur_src_image, nullptr);
        vk->blur_src_image = VK_NULL_HANDLE;
    }
    if (vk->blur_src_mem) {
        vkFreeMemory(dev, vk->blur_src_mem, nullptr);
        vk->blur_src_mem = VK_NULL_HANDLE;
    }
}

bool create_swapchain(vk_renderer *vk)
{
    VkDevice dev = vk->device.device;

    VkSurfaceCapabilitiesKHR caps;
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->device.physical_device, vk->surface, &caps);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width  = vk->w;
        extent.height = vk->h;
        if (extent.width < caps.minImageExtent.width)
            extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width)
            extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height)
            extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height)
            extent.height = caps.maxImageExtent.height;
    }
    vk->sc_extent = extent;

    if (extent.width == 0 || extent.height == 0) {
        vk->needs_recreate = true;
        return true;
    }

    uint32_t nfmt = 0;
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(vk->device.physical_device, vk->surface, &nfmt, nullptr);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;
    VkSurfaceFormatKHR *fmts = alloca(nfmt * sizeof(*fmts));
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(vk->device.physical_device, vk->surface, &nfmt, fmts);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;

    VkFormat want = VK_FORMAT_B8G8R8A8_UNORM;
    vk->sc_format = fmts[0].format;
    for (uint32_t i = 0; i < nfmt; i++) {
        if (fmts[i].format == want) {
            vk->sc_format = fmts[i].format;
            break;
        }
    }

    uint32_t npm = 0;
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(vk->device.physical_device, vk->surface, &npm, nullptr);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;
    VkPresentModeKHR *pms = alloca(npm * sizeof(*pms));
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(vk->device.physical_device, vk->surface, &npm, pms);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < npm; i++) {
        if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = pms[i];
            break;
        }
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = vk->surface,
        .minImageCount    = image_count,
        .imageFormat      = vk->sc_format,
        .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = present_mode,
        .clipped          = VK_TRUE,
    };

    uint32_t qf[] = { vk->device.graphics_family, vk->device.present_family };
    if (vk->device.graphics_family != vk->device.present_family) {
        sci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices   = qf;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkSwapchainKHR old = vk->swapchain;
    sci.oldSwapchain = old;

    res = vkCreateSwapchainKHR(dev, &sci, nullptr, &vk->swapchain);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS)
        return false;
    if (old) vkDestroySwapchainKHR(dev, old, nullptr);

    res = vkGetSwapchainImagesKHR(dev, vk->swapchain, &vk->image_count, nullptr);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;
    VkImage imgs[FLUX_MAX_SWAPCHAIN_IMAGES];
    res = vkGetSwapchainImagesKHR(dev, vk->swapchain, &vk->image_count, imgs);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;

    for (uint32_t i = 0; i < vk->image_count; i++) {
        vk->sc_images[i].image = imgs[i];
        if (vk->sc_images[i].view)
            vkDestroyImageView(dev, vk->sc_images[i].view, nullptr);

        VkImageViewCreateInfo vci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = imgs[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = vk->sc_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        res = vkCreateImageView(dev, &vci, nullptr, &vk->sc_images[i].view);
        FLUX_VK_CHECK(res);
        if (res != VK_SUCCESS)
            return false;
    }
    return true;
}

bool create_render_pass(vk_renderer *vk)
{
    VkAttachmentDescription attachments[2] = {0};
    attachments[0] = (VkAttachmentDescription){
        .format         = vk->sc_format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    attachments[1] = (VkAttachmentDescription){
        .format         = VK_FORMAT_S8_UINT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference stencil_ref = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &color_ref,
        .pDepthStencilAttachment = &stencil_ref,
    };
    VkSubpassDependency deps[1] = {
        {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        },
    };
    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments    = attachments,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = deps,
    };
    VkResult res = vkCreateRenderPass(vk->device.device, &ci, nullptr, &vk->render_pass);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS)
        return false;

    /* Create a compatible render pass with LOAD ops for mid-frame blur. */
    VkAttachmentDescription blur_atts[2];
    blur_atts[0] = attachments[0];
    blur_atts[0].loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
    blur_atts[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    blur_atts[1] = attachments[1];
    blur_atts[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
    blur_atts[1].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkRenderPassCreateInfo bci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments    = blur_atts,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = deps,
    };
    res = vkCreateRenderPass(vk->device.device, &bci, nullptr, &vk->blur_render_pass);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkDestroyRenderPass(vk->device.device, vk->render_pass, nullptr);
        vk->render_pass = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool create_stencil_and_framebuffers(vk_renderer *vk)
{
    VkDevice dev = vk->device.device;
    for (uint32_t i = 0; i < vk->image_count; i++) {
        VkImageCreateInfo sici = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = VK_FORMAT_S8_UINT,
            .extent        = { vk->sc_extent.width, vk->sc_extent.height, 1 },
            .mipLevels     = 1,
            .arrayLayers   = 1,
            .samples       = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VkImage stencil_img;
        VkDeviceMemory stencil_mem;
        VkResult res = vkCreateImage(dev, &sici, nullptr, &stencil_img);
        FLUX_VK_CHECK(res);
        if (res != VK_SUCCESS)
            return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(dev, stencil_img, &req);
        VkMemoryAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size,
            .memoryTypeIndex = find_memory_type(vk->device.physical_device, req.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        res = vkAllocateMemory(dev, &ai, nullptr, &stencil_mem);
        FLUX_VK_CHECK(res);
        if (res != VK_SUCCESS) {
            vkDestroyImage(dev, stencil_img, nullptr);
            return false;
        }
        FLUX_VK_CHECK(vkBindImageMemory(dev, stencil_img, stencil_mem, 0));

        VkImageViewCreateInfo svci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = stencil_img,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = VK_FORMAT_S8_UINT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        VkImageView stencil_view;
        res = vkCreateImageView(dev, &svci, nullptr, &stencil_view);
        FLUX_VK_CHECK(res);
        if (res != VK_SUCCESS) {
            vkFreeMemory(dev, stencil_mem, nullptr);
            vkDestroyImage(dev, stencil_img, nullptr);
            return false;
        }

        VkImageView atts[2] = { vk->sc_images[i].view, stencil_view };
        VkFramebufferCreateInfo fci = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = vk->render_pass,
            .attachmentCount = 2,
            .pAttachments    = atts,
            .width           = vk->sc_extent.width,
            .height          = vk->sc_extent.height,
            .layers          = 1,
        };
        VkFramebuffer fb;
        res = vkCreateFramebuffer(dev, &fci, nullptr, &fb);
        FLUX_VK_CHECK(res);
        if (res != VK_SUCCESS) {
            vkDestroyImageView(dev, stencil_view, nullptr);
            vkFreeMemory(dev, stencil_mem, nullptr);
            vkDestroyImage(dev, stencil_img, nullptr);
            return false;
        }
        vk->sc_images[i].framebuffer   = fb;
        vk->sc_images[i].stencil_image = stencil_img;
        vk->sc_images[i].stencil_mem   = stencil_mem;
        vk->sc_images[i].stencil_view  = stencil_view;
    }
    return true;
}

bool create_blur_src(vk_renderer *vk)
{
    if (vk->blur_src_image && vk->w == vk->sc_extent.width && vk->h == vk->sc_extent.height)
        return true;

    VkDevice dev = vk->device.device;
    if (vk->blur_src_view)  vkDestroyImageView(dev, vk->blur_src_view, nullptr);
    if (vk->blur_src_image) vkDestroyImage(dev, vk->blur_src_image, nullptr);
    if (vk->blur_src_mem)   vkFreeMemory(dev, vk->blur_src_mem, nullptr);
    vk->blur_src_view = VK_NULL_HANDLE;
    vk->blur_src_image = VK_NULL_HANDLE;
    vk->blur_src_mem = VK_NULL_HANDLE;

    uint32_t w = vk->sc_extent.width;
    uint32_t h = vk->sc_extent.height;
    if (w == 0 || h == 0) return false;

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk->sc_format,
        .extent = { w, h, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult res = vkCreateImage(dev, &ici, nullptr, &vk->blur_src_image);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, vk->blur_src_image, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(vk->device.physical_device, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    res = vkAllocateMemory(dev, &ai, nullptr, &vk->blur_src_mem);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkDestroyImage(dev, vk->blur_src_image, nullptr);
        vk->blur_src_image = VK_NULL_HANDLE;
        return false;
    }
    FLUX_VK_CHECK(vkBindImageMemory(dev, vk->blur_src_image, vk->blur_src_mem, 0));

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk->blur_src_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk->sc_format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    res = vkCreateImageView(dev, &vci, nullptr, &vk->blur_src_view);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkFreeMemory(dev, vk->blur_src_mem, nullptr);
        vkDestroyImage(dev, vk->blur_src_image, nullptr);
        vk->blur_src_image = VK_NULL_HANDLE;
        vk->blur_src_mem = VK_NULL_HANDLE;
        return false;
    }
    return true;
}
