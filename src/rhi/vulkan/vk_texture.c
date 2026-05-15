/*
 * Vulkan texture allocation, atlas management, upload helper.
 */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

VkFormat vk_pixel_format_to_vk(flux_pixel_format fmt)
{
    switch (fmt) {
    case FLUX_FMT_BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
    case FLUX_FMT_RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case FLUX_FMT_A8_UNORM:    return VK_FORMAT_R8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

bool ensure_atlas(vk_renderer *vk)
{
    if (vk->atlas_image) return true;
    VkDevice dev = vk->device.device;
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = { 2048, 2048, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult res = vkCreateImage(dev, &ici, nullptr, &vk->atlas_image);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, vk->atlas_image, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(vk->device.physical_device, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    res = vkAllocateMemory(dev, &ai, nullptr, &vk->atlas_mem);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkDestroyImage(dev, vk->atlas_image, nullptr);
        vk->atlas_image = VK_NULL_HANDLE;
        return false;
    }
    FLUX_VK_CHECK(vkBindImageMemory(dev, vk->atlas_image, vk->atlas_mem, 0));

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk->atlas_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    res = vkCreateImageView(dev, &vci, nullptr, &vk->atlas_view);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkFreeMemory(dev, vk->atlas_mem, nullptr);
        vkDestroyImage(dev, vk->atlas_image, nullptr);
        vk->atlas_image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  Staging buffer pool                                               */
/* ------------------------------------------------------------------ */

void staging_pool_destroy(vk_renderer *vk)
{
    VkDevice dev = vk->device.device;
    for (uint32_t i = 0; i < vk->staging_pool.count; i++) {
        vk_staging_buffer *sb = &vk->staging_pool.buffers[i];
        if (sb->mem) vkUnmapMemory(dev, sb->mem);
        if (sb->buf) vkDestroyBuffer(dev, sb->buf, nullptr);
        if (sb->mem) vkFreeMemory(dev, sb->mem, nullptr);
    }
    vk->staging_pool.count = 0;
}

void staging_pool_gc(vk_renderer *vk)
{
    if (!vk->transfer_pending) return;
    VkDevice dev = vk->device.device;
    VkResult res = vkGetFenceStatus(dev, vk->transfer_fence);
    if (res == VK_SUCCESS) {
        vkResetFences(dev, 1, &vk->transfer_fence);
        vkResetCommandBuffer(vk->transfer_cmd, 0);
        vk->transfer_pending = false;
        for (uint32_t i = 0; i < vk->staging_pool.count; i++)
            vk->staging_pool.buffers[i].in_use = false;
    }
}

static vk_staging_buffer *staging_pool_acquire(vk_renderer *vk, size_t need)
{
    staging_pool_gc(vk);

    for (uint32_t i = 0; i < vk->staging_pool.count; i++) {
        vk_staging_buffer *sb = &vk->staging_pool.buffers[i];
        if (!sb->in_use && sb->size >= need)
            return sb;
    }

    if (vk->staging_pool.count < FLUX_MAX_STAGING_BUFFERS) {
        vk_staging_buffer *sb = &vk->staging_pool.buffers[vk->staging_pool.count];
        VkDevice dev = vk->device.device;
        VkBufferCreateInfo bi = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = need,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VkResult res = vkCreateBuffer(dev, &bi, nullptr, &sb->buf);
        FLUX_VK_CHECK(res);
        if (res != VK_SUCCESS) return nullptr;

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, sb->buf, &req);
        VkMemoryAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size,
            .memoryTypeIndex = find_memory_type(vk->device.physical_device, req.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        res = vkAllocateMemory(dev, &ai, nullptr, &sb->mem);
        FLUX_VK_CHECK(res);
        if (res != VK_SUCCESS) {
            vkDestroyBuffer(dev, sb->buf, nullptr);
            sb->buf = VK_NULL_HANDLE;
            return nullptr;
        }
        FLUX_VK_CHECK(vkBindBufferMemory(dev, sb->buf, sb->mem, 0));
        FLUX_VK_CHECK(vkMapMemory(dev, sb->mem, 0, need, 0, &sb->map));
        sb->size = need;
        sb->in_use = false;
        vk->staging_pool.count++;
        return sb;
    }

    if (vk->transfer_pending) {
        FLUX_VK_CHECK(vkWaitForFences(vk->device.device, 1, &vk->transfer_fence, VK_TRUE, UINT64_MAX));
        staging_pool_gc(vk);
        for (uint32_t i = 0; i < vk->staging_pool.count; i++) {
            vk_staging_buffer *sb = &vk->staging_pool.buffers[i];
            if (sb->size >= need)
                return sb;
        }
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/*  Async texture upload                                              */
/* ------------------------------------------------------------------ */

bool upload_texture_data(vk_renderer *vk, vk_texture *t,
                         const void *data,
                         uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h,
                         size_t stride)
{
    VkDevice dev = vk->device.device;
    uint32_t bpp = (t->format == VK_FORMAT_R8_UNORM) ? 1 : 4;
    size_t row = stride > 0 ? stride : (size_t)w * bpp;
    size_t total = row * h;

    vk_staging_buffer *sb = staging_pool_acquire(vk, total);
    if (!sb) return false;

    const uint8_t *src = data;
    size_t dense = (size_t)w * bpp;
    if (row == dense) {
        memcpy(sb->map, src, total);
    } else {
        for (uint32_t i = 0; i < h; i++)
            memcpy((uint8_t *)sb->map + i * dense, src + i * row, dense);
    }

    if (vk->transfer_pending) {
        VkResult res = vkGetFenceStatus(dev, vk->transfer_fence);
        if (res != VK_SUCCESS) {
            FLUX_VK_CHECK(vkWaitForFences(dev, 1, &vk->transfer_fence, VK_TRUE, UINT64_MAX));
        }
        staging_pool_gc(vk);
    }

    VkCommandBufferBeginInfo bbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    FLUX_VK_CHECK(vkBeginCommandBuffer(vk->transfer_cmd, &bbi));

    VkImageMemoryBarrier to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = t->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(vk->transfer_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_dst);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { (int32_t)x, (int32_t)y, 0 },
        .imageExtent = { w, h, 1 },
    };
    vkCmdCopyBufferToImage(vk->transfer_cmd, sb->buf, t->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier to_shader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = t->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(vk->transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_shader);

    FLUX_VK_CHECK(vkEndCommandBuffer(vk->transfer_cmd));

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vk->transfer_cmd,
    };
    FLUX_VK_CHECK(vkQueueSubmit(vk->device.graphics_queue, 1, &si, vk->transfer_fence));
    sb->in_use = true;
    vk->transfer_pending = true;
    return true;
}

flux_r_texture *vk_texture_alloc(flux_rhi_device *r, uint32_t w, uint32_t h,
                                flux_pixel_format fmt, const void *data, size_t stride)
{
    vk_renderer *vk = VKR(r);
    VkDevice dev = vk->device.device;
    VkFormat vkfmt = vk_pixel_format_to_vk(fmt);
    if (vkfmt == VK_FORMAT_UNDEFINED) return nullptr;

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vkfmt,
        .extent = { w, h, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image;
    VkResult res = vkCreateImage(dev, &ici, nullptr, &image);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS)
        return nullptr;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, image, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(vk->device.physical_device, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VkDeviceMemory mem;
    res = vkAllocateMemory(dev, &ai, nullptr, &mem);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkDestroyImage(dev, image, nullptr);
        return nullptr;
    }
    FLUX_VK_CHECK(vkBindImageMemory(dev, image, mem, 0));

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vkfmt,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkImageView view;
    res = vkCreateImageView(dev, &vci, nullptr, &view);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkFreeMemory(dev, mem, nullptr);
        vkDestroyImage(dev, image, nullptr);
        return nullptr;
    }

    vk_texture *t = calloc(1, sizeof(*t));
    if (!t) {
        vkDestroyImageView(dev, view, nullptr);
        vkFreeMemory(dev, mem, nullptr);
        vkDestroyImage(dev, image, nullptr);
        return nullptr;
    }
    t->image = image;
    t->mem = mem;
    t->view = view;
    t->w = w; t->h = h;
    t->format = vkfmt;
    t->next = vk->textures;
    vk->textures = t;

    if (data) {
        upload_texture_data(vk, t, data, 0, 0, w, h, stride);
    }
    return (flux_r_texture *)t;
}

void vk_texture_free(flux_rhi_device *r, flux_r_texture *tex)
{
    vk_renderer *vk = VKR(r);
    vk_texture *t = (vk_texture *)tex;
    if (!t) return;

    if (vk->transfer_pending) {
        VkResult res = vkGetFenceStatus(vk->device.device, vk->transfer_fence);
        if (res != VK_SUCCESS)
            FLUX_VK_CHECK(vkWaitForFences(vk->device.device, 1, &vk->transfer_fence, VK_TRUE, UINT64_MAX));
        staging_pool_gc(vk);
    }

    vk_texture **prev = &vk->textures;
    while (*prev && *prev != t) prev = &(*prev)->next;
    if (*prev) *prev = t->next;

    VkDevice dev = vk->device.device;
    vkDestroyImageView(dev, t->view, nullptr);
    vkDestroyImage(dev, t->image, nullptr);
    vkFreeMemory(dev, t->mem, nullptr);
    free(t);
}

void vk_texture_update(flux_rhi_device *r, flux_r_texture *tex,
                       const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!r || !tex || !data || w == 0 || h == 0) return;
    upload_texture_data(VKR(r), (vk_texture *)tex, data, x, y, w, h, 0);
}

flux_r_texture *vk_surface_texture(flux_rhi_device *r)
{
    (void)r;
    return nullptr;
}
