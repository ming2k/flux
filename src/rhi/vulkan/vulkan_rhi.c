/*
 * Vulkan RHI backend — entry point, frame lifecycle, vertex allocation, vtable.
 */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PIPELINE_CACHE_DIR  ".cache/flux"
#define PIPELINE_CACHE_FILE "pipeline_cache.bin"

static void get_pipeline_cache_path(char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(out, out_size, "%s/%s/%s", home, PIPELINE_CACHE_DIR, PIPELINE_CACHE_FILE);
}

static bool load_pipeline_cache(vk_renderer *vk)
{
    VkDevice dev = vk->device.device;
    char path[512];
    get_pipeline_cache_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) goto create_empty;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); goto create_empty; }

    void *data = malloc(size);
    if (!data) { fclose(f); goto create_empty; }

    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        goto create_empty;
    }
    fclose(f);

    VkPipelineCacheCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = size,
        .pInitialData = data,
    };
    VkResult res = vkCreatePipelineCache(dev, &ci, nullptr, &vk->pipeline_cache);
    free(data);
    FLUX_VK_CHECK(res);
    return res == VK_SUCCESS;

create_empty:
    {
        VkPipelineCacheCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        };
        VkResult res = vkCreatePipelineCache(dev, &ci, nullptr, &vk->pipeline_cache);
        FLUX_VK_CHECK(res);
        return res == VK_SUCCESS;
    }
}

static void save_pipeline_cache(vk_renderer *vk)
{
    VkDevice dev = vk->device.device;
    if (vk->pipeline_cache == VK_NULL_HANDLE) return;

    size_t size = 0;
    VkResult res = vkGetPipelineCacheData(dev, vk->pipeline_cache, &size, nullptr);
    if (res != VK_SUCCESS || size == 0) return;

    void *data = malloc(size);
    if (!data) return;

    res = vkGetPipelineCacheData(dev, vk->pipeline_cache, &size, data);
    if (res != VK_SUCCESS) {
        free(data);
        return;
    }

    char path[512];
    get_pipeline_cache_path(path, sizeof(path));

    const char *home = getenv("HOME");
    if (home) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/.cache", home);
        mkdir(dir, 0755);
        snprintf(dir, sizeof(dir), "%s/%s", home, PIPELINE_CACHE_DIR);
        mkdir(dir, 0755);
    }

    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
    free(data);
}

void vk_destroy(flux_rhi_device *r)
{
    vk_renderer *vk = VKR(r);
    if (!vk) return;
    VkDevice dev = vk->device.device;
    if (dev == VK_NULL_HANDLE) { free(vk); return; }

    FLUX_VK_CHECK(vkDeviceWaitIdle(dev));

    save_pipeline_cache(vk);
    if (vk->pipeline_cache) {
        vkDestroyPipelineCache(dev, vk->pipeline_cache, nullptr);
        vk->pipeline_cache = VK_NULL_HANDLE;
    }

    destroy_pipelines(vk);

    if (vk->sampler) vkDestroySampler(dev, vk->sampler, nullptr);

    vk_texture *tex = vk->textures;
    while (tex) {
        vk_texture *n = tex->next;
        vkDestroyImageView(dev, tex->view, nullptr);
        vkDestroyImage(dev, tex->image, nullptr);
        vkFreeMemory(dev, tex->mem, nullptr);
        free(tex);
        tex = n;
    }

    destroy_swapchain_resources(vk);

    for (uint32_t i = 0; i < FLUX_MAX_FRAMES_IN_FLIGHT; i++) {
        vk_frame *f = &vk->frames[i];
        if (f->vbuf) {
            vkUnmapMemory(dev, f->vbuf_mem);
            vkDestroyBuffer(dev, f->vbuf, nullptr);
            vkFreeMemory(dev, f->vbuf_mem, nullptr);
        }
        if (f->desc_pool) vkDestroyDescriptorPool(dev, f->desc_pool, nullptr);
        if (f->sem_image_avail) vkDestroySemaphore(dev, f->sem_image_avail, nullptr);
        if (f->sem_render_done) vkDestroySemaphore(dev, f->sem_render_done, nullptr);
        if (f->fence) vkDestroyFence(dev, f->fence, nullptr);
    }

    staging_pool_destroy(vk);
    if (vk->transfer_sem) vkDestroySemaphore(dev, vk->transfer_sem, nullptr);
    if (vk->transfer_fence) vkDestroyFence(dev, vk->transfer_fence, nullptr);
    if (vk->transfer_cmd) vkFreeCommandBuffers(dev, vk->transfer_cmd_pool, 1, &vk->transfer_cmd);
    if (vk->transfer_cmd_pool) vkDestroyCommandPool(dev, vk->transfer_cmd_pool, nullptr);

    if (vk->cmd_pool) vkDestroyCommandPool(dev, vk->cmd_pool, nullptr);
    free(vk);
}

void vk_surface_extent(flux_rhi_device *r, uint32_t *w, uint32_t *h)
{
    *w = VKR(r)->sc_extent.width;
    *h = VKR(r)->sc_extent.height;
}

void vk_begin_frame(flux_rhi_device *r)
{
    vk_renderer *vk = VKR(r);
    if (vk->needs_recreate) {
        vk->needs_recreate = false;
        FLUX_VK_CHECK(vkDeviceWaitIdle(vk->device.device));
        destroy_swapchain_resources(vk);
        destroy_pipelines(vk);
        if (!create_swapchain(vk) || !create_render_pass(vk) ||
            !create_stencil_and_framebuffers(vk)) {
            vk->needs_recreate = true;
            return;
        }
        vk->w = vk->sc_extent.width;
        vk->h = vk->sc_extent.height;
    }

    if (vk->transfer_pending) {
        VkResult fence_res = vkGetFenceStatus(vk->device.device, vk->transfer_fence);
        if (fence_res == VK_SUCCESS)
            staging_pool_gc(vk);
    }

    vk_frame *f = &vk->frames[vk->frame_idx];
    FLUX_VK_CHECK(vkWaitForFences(vk->device.device, 1, &f->fence, VK_TRUE, UINT64_MAX));
    FLUX_VK_CHECK(vkResetFences(vk->device.device, 1, &f->fence));

    FLUX_VK_CHECK(vkResetCommandBuffer(f->cmd, 0));
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    FLUX_VK_CHECK(vkBeginCommandBuffer(f->cmd, &bi));

    f->vbuf_cursor = 0;
    f->desc_count = 0;
    if (f->desc_pool) {
        vkDestroyDescriptorPool(vk->device.device, f->desc_pool, nullptr);
        f->desc_pool = VK_NULL_HANDLE;
    }
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = FLUX_MAX_DESC_CACHE,
    };
    VkDescriptorPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FLUX_MAX_DESC_CACHE,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    FLUX_VK_CHECK(vkCreateDescriptorPool(vk->device.device, &pci, nullptr, &f->desc_pool));

    VkResult acq = vkAcquireNextImageKHR(vk->device.device, vk->swapchain, UINT64_MAX,
                                          f->sem_image_avail, VK_NULL_HANDLE,
                                          &vk->acquired_img_idx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) {
        vk->needs_recreate = true;
        FLUX_VK_CHECK(vkEndCommandBuffer(f->cmd));
        return;
    }
    FLUX_VK_CHECK(acq);

    vk->frame_began = true;
    vk->pass_began = false;
}

void vk_begin_pass(flux_rhi_device *r, flux_color clear)
{
    vk_renderer *vk = VKR(r);
    if (!vk->frame_began || vk->pass_began) return;

    if (!ensure_pipelines(vk)) return;

    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    VkClearValue clear_vals[2];
    clear_vals[0].color.float32[0] = ((clear >> 16) & 0xFF) / 255.0f;
    clear_vals[0].color.float32[1] = ((clear >> 8)  & 0xFF) / 255.0f;
    clear_vals[0].color.float32[2] = (clear & 0xFF) / 255.0f;
    clear_vals[0].color.float32[3] = ((clear >> 24) & 0xFF) / 255.0f;
    clear_vals[1].depthStencil = (VkClearDepthStencilValue){ 0, 0 };

    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk->render_pass,
        .framebuffer = vk->sc_images[vk->acquired_img_idx].framebuffer,
        .renderArea = { {0, 0}, vk->sc_extent },
        .clearValueCount = 2,
        .pClearValues = clear_vals,
    };
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {
        .x = 0, .y = 0,
        .width = (float)vk->sc_extent.width,
        .height = (float)vk->sc_extent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D sc = { {0, 0}, vk->sc_extent };
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vk->pass_began = true;
}

void vk_end_pass(flux_rhi_device *r)
{
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    vkCmdEndRenderPass(vk->frames[vk->frame_idx].cmd);
    vk->pass_began = false;
}

void vk_submit(flux_rhi_device *r)
{
    vk_renderer *vk = VKR(r);
    if (!vk->frame_began) return;

    if (vk->pass_began) {
        vkCmdEndRenderPass(vk->frames[vk->frame_idx].cmd);
        vk->pass_began = false;
    }

    FLUX_VK_CHECK(vkEndCommandBuffer(vk->frames[vk->frame_idx].cmd));

    VkSemaphore wait_sems[2];
    VkPipelineStageFlags wait_stages[2];
    uint32_t wait_count = 0;

    wait_sems[wait_count]   = vk->frames[vk->frame_idx].sem_image_avail;
    wait_stages[wait_count] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    wait_count++;

    if (vk->transfer_sem_signaled) {
        wait_sems[wait_count]   = vk->transfer_sem;
        wait_stages[wait_count] = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        wait_count++;
        vk->transfer_sem_signaled = false;
    }

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = wait_count,
        .pWaitSemaphores      = wait_sems,
        .pWaitDstStageMask    = wait_stages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &vk->frames[vk->frame_idx].cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &vk->frames[vk->frame_idx].sem_render_done,
    };
    FLUX_VK_CHECK(vkQueueSubmit(vk->device.graphics_queue, 1, &si, vk->frames[vk->frame_idx].fence));

    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &vk->frames[vk->frame_idx].sem_render_done,
        .swapchainCount     = 1,
        .pSwapchains        = &vk->swapchain,
        .pImageIndices      = &vk->acquired_img_idx,
    };
    VkResult pr = vkQueuePresentKHR(vk->device.present_queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
        vk->needs_recreate = true;
    else
        FLUX_VK_CHECK(pr);

    vk->frame_began = false;
    vk->frame_idx = (vk->frame_idx + 1) % FLUX_MAX_FRAMES_IN_FLIGHT;

    if (vk->transfer_pending) {
        VkResult fence_res = vkGetFenceStatus(vk->device.device, vk->transfer_fence);
        if (fence_res == VK_SUCCESS)
            staging_pool_gc(vk);
    }
}

bool vk_read_pixels(flux_rhi_device *r, void *data, size_t stride)
{
    vk_renderer *vk = VKR(r);
    if (!vk->frame_began || !data) return false;

    VkDevice dev = vk->device.device;
    uint32_t w = vk->sc_extent.width;
    uint32_t h = vk->sc_extent.height;
    size_t dst_stride = stride > 0 ? stride : (size_t)w * 4;
    size_t staging_size = (size_t)w * 4 * h;

    /* Staging buffer */
    VkBuffer staging;
    VkDeviceMemory staging_mem;
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = staging_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult res = vkCreateBuffer(dev, &bi, nullptr, &staging);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, staging, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(vk->device.physical_device, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    res = vkAllocateMemory(dev, &ai, nullptr, &staging_mem);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkDestroyBuffer(dev, staging, nullptr);
        return false;
    }
    FLUX_VK_CHECK(vkBindBufferMemory(dev, staging, staging_mem, 0));

    FLUX_VK_CHECK(vkDeviceWaitIdle(dev));

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    res = vkAllocateCommandBuffers(dev, &cai, &cmd);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkDestroyBuffer(dev, staging, nullptr);
        vkFreeMemory(dev, staging_mem, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo bbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    FLUX_VK_CHECK(vkBeginCommandBuffer(cmd, &bbi));

    VkImage img = vk->sc_images[vk->acquired_img_idx].image;

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { w, h, 1 },
    };
    vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    FLUX_VK_CHECK(vkEndCommandBuffer(cmd));

    VkFence fence;
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    res = vkCreateFence(dev, &fci, nullptr, &fence);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vkFreeCommandBuffers(dev, vk->cmd_pool, 1, &cmd);
        vkDestroyBuffer(dev, staging, nullptr);
        vkFreeMemory(dev, staging_mem, nullptr);
        return false;
    }

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    FLUX_VK_CHECK(vkQueueSubmit(vk->device.graphics_queue, 1, &si, fence));
    FLUX_VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));

    void *map;
    FLUX_VK_CHECK(vkMapMemory(dev, staging_mem, 0, VK_WHOLE_SIZE, 0, &map));
    if (dst_stride == (size_t)w * 4) {
        memcpy(data, map, staging_size);
    } else {
        for (uint32_t y = 0; y < h; y++)
            memcpy((uint8_t *)data + y * dst_stride,
                   (const uint8_t *)map + y * w * 4,
                   (size_t)w * 4);
    }
    vkUnmapMemory(dev, staging_mem);

    vkDestroyFence(dev, fence, nullptr);
    vkFreeCommandBuffers(dev, vk->cmd_pool, 1, &cmd);
    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, staging_mem, nullptr);
    return true;
}

bool vk_resize(flux_rhi_device *r, uint32_t w, uint32_t h)
{
    vk_renderer *vk = VKR(r);
    if (w == 0 || h == 0) return false;
    vk->w = w;
    vk->h = h;
    vk->needs_recreate = true;
    return true;
}

flux_solid_vertex *vk_alloc_solid(flux_rhi_device *r, size_t n,
                                flux_r_buffer **buf, uint32_t *first)
{
    vk_renderer *vk = VKR(r);
    vk_frame *f = &vk->frames[vk->frame_idx];
    size_t aligned = align_up(f->vbuf_cursor, 16);
    size_t need = aligned + n * sizeof(flux_solid_vertex);
    if (!ensure_vbuf(vk, f, need)) {
        *buf = nullptr; *first = 0; return nullptr;
    }
    flux_solid_vertex *ptr = (flux_solid_vertex *)((uint8_t *)f->vbuf_map + aligned);
    *first = (uint32_t)(aligned / sizeof(flux_solid_vertex));
    *buf = (flux_r_buffer *)(uintptr_t)1;
    f->vbuf_cursor = aligned + n * sizeof(flux_solid_vertex);
    return ptr;
}

flux_image_vertex *vk_alloc_image(flux_rhi_device *r, size_t n,
                                flux_r_buffer **buf, uint32_t *first)
{
    vk_renderer *vk = VKR(r);
    vk_frame *f = &vk->frames[vk->frame_idx];
    size_t aligned = align_up(f->vbuf_cursor, 16);
    size_t need = aligned + n * sizeof(flux_image_vertex);
    if (!ensure_vbuf(vk, f, need)) {
        *buf = nullptr; *first = 0; return nullptr;
    }
    flux_image_vertex *ptr = (flux_image_vertex *)((uint8_t *)f->vbuf_map + aligned);
    *first = (uint32_t)(aligned / sizeof(flux_image_vertex));
    *buf = (flux_r_buffer *)(uintptr_t)1;
    f->vbuf_cursor = aligned + n * sizeof(flux_image_vertex);
    return ptr;
}

flux_fringe_vertex *vk_alloc_fringe(flux_rhi_device *r, size_t n,
                                    flux_r_buffer **buf, uint32_t *first)
{
    vk_renderer *vk = VKR(r);
    vk_frame *f = &vk->frames[vk->frame_idx];
    size_t aligned = align_up(f->vbuf_cursor, 16);
    size_t need = aligned + n * sizeof(flux_fringe_vertex);
    if (!ensure_vbuf(vk, f, need)) {
        *buf = nullptr; *first = 0; return nullptr;
    }
    flux_fringe_vertex *ptr = (flux_fringe_vertex *)((uint8_t *)f->vbuf_map + aligned);
    *first = (uint32_t)(aligned / sizeof(flux_fringe_vertex));
    *buf = (flux_r_buffer *)(uintptr_t)1;
    f->vbuf_cursor = aligned + n * sizeof(flux_fringe_vertex);
    return ptr;
}

static void vk_blend_mode(flux_rhi_device *r, flux_blend_mode mode)
{
    vk_renderer *vk = VKR(r);
    vk->current_blend_mode = mode;
}

static const flux_rhi_vtbl vk_vtbl = {
    .destroy         = vk_destroy,
    .surface_extent  = vk_surface_extent,
    .begin_frame     = vk_begin_frame,
    .begin_pass      = vk_begin_pass,
    .end_pass        = vk_end_pass,
    .submit          = vk_submit,
    .read_pixels     = vk_read_pixels,
    .resize          = vk_resize,
    .alloc_solid     = vk_alloc_solid,
    .alloc_image     = vk_alloc_image,
    .alloc_fringe    = vk_alloc_fringe,
    .draw_solid      = vk_draw_solid,
    .flush_solid     = vk_flush_solid,
    .draw_image      = vk_draw_image,
    .draw_text       = vk_draw_text,
    .draw_gradient   = vk_draw_gradient,
    .scissor         = vk_scissor,
    .stencil_clear   = vk_stencil_clear,
    .stencil_fill    = vk_stencil_fill,
    .stencil_ref     = vk_stencil_ref,
    .cover_solid     = vk_cover_solid,
    .cover_gradient  = vk_cover_gradient,
    .draw_fringe     = vk_draw_fringe,
    .blur            = vk_blur,
    .set_blend_mode  = vk_blend_mode,
    .texture_alloc   = vk_texture_alloc,
    .texture_free    = vk_texture_free,
    .texture_update  = vk_texture_update,
    .surface_texture = vk_surface_texture,
};

flux_rhi_device *flux_rhi_create_vulkan(const flux_vulkan_device *device,
                                    VkSurfaceKHR surface, int32_t w, int32_t h)
{
    if (!device || !device->device || !surface || w <= 0 || h <= 0)
        return nullptr;

    vk_renderer *vk = calloc(1, sizeof(*vk));
    if (!vk) return nullptr;

    vk->device = *device;
    vk->surface = surface;
    vk->w = (uint32_t)w;
    vk->h = (uint32_t)h;
    vk->vtbl = &vk_vtbl;

    VkDevice dev = vk->device.device;

    /* Detect blend-related extensions */
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(vk->device.physical_device, nullptr, &ext_count, nullptr);
    VkExtensionProperties *exts = calloc(ext_count, sizeof(VkExtensionProperties));
    if (exts) {
        vkEnumerateDeviceExtensionProperties(vk->device.physical_device, nullptr, &ext_count, exts);
        for (uint32_t i = 0; i < ext_count; i++) {
            if (strcmp(exts[i].extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME) == 0) {
                vk->has_dynamic_blend = true;
            }
            if (strcmp(exts[i].extensionName, VK_EXT_BLEND_OPERATION_ADVANCED_EXTENSION_NAME) == 0) {
                vk->has_blend_op_advanced = true;
            }
        }
        free(exts);
    }
    if (vk->has_dynamic_blend) {
        vk->pfnCmdSetColorBlendEquationEXT = (PFN_vkCmdSetColorBlendEquationEXT)
            vkGetDeviceProcAddr(dev, "vkCmdSetColorBlendEquationEXT");
        if (!vk->pfnCmdSetColorBlendEquationEXT)
            vk->has_dynamic_blend = false;
    }

    if (!create_swapchain(vk) || !create_render_pass(vk) ||
        !create_stencil_and_framebuffers(vk) || !create_blur_src(vk)) {
        vk_destroy((flux_rhi_device *)vk);
        return nullptr;
    }

    VkCommandPoolCreateInfo cpi = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk->device.graphics_family,
    };
    VkResult res = vkCreateCommandPool(dev, &cpi, nullptr, &vk->cmd_pool);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vk_destroy((flux_rhi_device *)vk);
        return nullptr;
    }

    VkCommandBufferAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = vk->cmd_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    for (uint32_t i = 0; i < FLUX_MAX_FRAMES_IN_FLIGHT; i++) {
        res = vkAllocateCommandBuffers(dev, &ai, &vk->frames[i].cmd);
        FLUX_VK_CHECK(res);
    }

    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                   .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (uint32_t i = 0; i < FLUX_MAX_FRAMES_IN_FLIGHT; i++) {
        res = vkCreateSemaphore(dev, &sci, nullptr, &vk->frames[i].sem_image_avail);
        FLUX_VK_CHECK(res);
        res = vkCreateSemaphore(dev, &sci, nullptr, &vk->frames[i].sem_render_done);
        FLUX_VK_CHECK(res);
        res = vkCreateFence(dev, &fci, nullptr, &vk->frames[i].fence);
        FLUX_VK_CHECK(res);
    }

    VkSamplerCreateInfo sci_sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    FLUX_VK_CHECK(vkCreateSampler(dev, &sci_sampler, nullptr, &vk->sampler));

    VkCommandPoolCreateInfo transfer_cpi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk->device.graphics_family,
    };
    res = vkCreateCommandPool(dev, &transfer_cpi, nullptr, &vk->transfer_cmd_pool);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vk_destroy((flux_rhi_device *)vk);
        return nullptr;
    }

    VkCommandBufferAllocateInfo transfer_ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->transfer_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    res = vkAllocateCommandBuffers(dev, &transfer_ai, &vk->transfer_cmd);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vk_destroy((flux_rhi_device *)vk);
        return nullptr;
    }

    VkFenceCreateInfo transfer_fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    res = vkCreateFence(dev, &transfer_fci, nullptr, &vk->transfer_fence);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vk_destroy((flux_rhi_device *)vk);
        return nullptr;
    }

    VkSemaphoreCreateInfo transfer_sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    res = vkCreateSemaphore(dev, &transfer_sci, nullptr, &vk->transfer_sem);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS) {
        vk_destroy((flux_rhi_device *)vk);
        return nullptr;
    }

    if (!load_pipeline_cache(vk)) {
        vk_destroy((flux_rhi_device *)vk);
        return nullptr;
    }

    return (flux_rhi_device *)vk;
}
