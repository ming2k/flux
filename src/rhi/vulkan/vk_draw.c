/*
 * Vulkan draw commands.
 */
#include "vk_internal.h"
#include <string.h>

static void apply_blend_mode(vk_renderer *vk, VkCommandBuffer cmd)
{
    if (!vk->has_dynamic_blend || !vk->pfnCmdSetColorBlendEquationEXT)
        return;

    VkBlendFactor src_color = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendFactor src_alpha = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp color_op = VK_BLEND_OP_ADD;
    VkBlendOp alpha_op = VK_BLEND_OP_ADD;

    switch (vk->current_blend_mode) {
    case FLUX_BLEND_SRC_OVER:
        src_color = VK_BLEND_FACTOR_ONE;
        dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        src_alpha = VK_BLEND_FACTOR_ONE;
        dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case FLUX_BLEND_DST_OVER:
        src_color = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_color = VK_BLEND_FACTOR_ONE;
        src_alpha = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_alpha = VK_BLEND_FACTOR_ONE;
        break;
    case FLUX_BLEND_SRC_IN:
        src_color = VK_BLEND_FACTOR_DST_ALPHA;
        dst_color = VK_BLEND_FACTOR_ZERO;
        src_alpha = VK_BLEND_FACTOR_DST_ALPHA;
        dst_alpha = VK_BLEND_FACTOR_ZERO;
        break;
    case FLUX_BLEND_DST_IN:
        src_color = VK_BLEND_FACTOR_ZERO;
        dst_color = VK_BLEND_FACTOR_SRC_ALPHA;
        src_alpha = VK_BLEND_FACTOR_ZERO;
        dst_alpha = VK_BLEND_FACTOR_SRC_ALPHA;
        break;
    case FLUX_BLEND_SRC_OUT:
        src_color = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_color = VK_BLEND_FACTOR_ZERO;
        src_alpha = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_alpha = VK_BLEND_FACTOR_ZERO;
        break;
    case FLUX_BLEND_DST_OUT:
        src_color = VK_BLEND_FACTOR_ZERO;
        dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        src_alpha = VK_BLEND_FACTOR_ZERO;
        dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case FLUX_BLEND_SRC_ATOP:
        src_color = VK_BLEND_FACTOR_DST_ALPHA;
        dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        src_alpha = VK_BLEND_FACTOR_DST_ALPHA;
        dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case FLUX_BLEND_DST_ATOP:
        src_color = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_color = VK_BLEND_FACTOR_SRC_ALPHA;
        src_alpha = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_alpha = VK_BLEND_FACTOR_SRC_ALPHA;
        break;
    case FLUX_BLEND_XOR:
        src_color = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        src_alpha = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case FLUX_BLEND_PLUS:
        src_color = VK_BLEND_FACTOR_ONE;
        dst_color = VK_BLEND_FACTOR_ONE;
        src_alpha = VK_BLEND_FACTOR_ONE;
        dst_alpha = VK_BLEND_FACTOR_ONE;
        break;
    case FLUX_BLEND_MULTIPLY:
        if (vk->has_blend_op_advanced) {
            color_op = VK_BLEND_OP_MULTIPLY_EXT;
            alpha_op = VK_BLEND_OP_MULTIPLY_EXT;
            src_color = VK_BLEND_FACTOR_ONE;
            dst_color = VK_BLEND_FACTOR_ONE;
            src_alpha = VK_BLEND_FACTOR_ONE;
            dst_alpha = VK_BLEND_FACTOR_ONE;
        }
        /* else falls back to SRC_OVER */
        break;
    case FLUX_BLEND_SCREEN:
        if (vk->has_blend_op_advanced) {
            color_op = VK_BLEND_OP_SCREEN_EXT;
            alpha_op = VK_BLEND_OP_SCREEN_EXT;
            src_color = VK_BLEND_FACTOR_ONE;
            dst_color = VK_BLEND_FACTOR_ONE;
            src_alpha = VK_BLEND_FACTOR_ONE;
            dst_alpha = VK_BLEND_FACTOR_ONE;
        }
        /* else falls back to SRC_OVER */
        break;
    case FLUX_BLEND_OVERLAY:
        if (vk->has_blend_op_advanced) {
            color_op = VK_BLEND_OP_OVERLAY_EXT;
            alpha_op = VK_BLEND_OP_OVERLAY_EXT;
            src_color = VK_BLEND_FACTOR_ONE;
            dst_color = VK_BLEND_FACTOR_ONE;
            src_alpha = VK_BLEND_FACTOR_ONE;
            dst_alpha = VK_BLEND_FACTOR_ONE;
        }
        /* else falls back to SRC_OVER */
        break;
    default:
        break;
    }

    VkColorBlendEquationEXT eq = {
        .srcColorBlendFactor = src_color,
        .dstColorBlendFactor = dst_color,
        .colorBlendOp = color_op,
        .srcAlphaBlendFactor = src_alpha,
        .dstAlphaBlendFactor = dst_alpha,
        .alphaBlendOp = alpha_op,
    };
    vk->pfnCmdSetColorBlendEquationEXT(cmd, 0, 1, &eq);
}

bool ensure_vbuf(vk_renderer *vk, vk_frame *f, size_t need)
{
    if (f->vbuf_size >= need) return true;
    VkDevice dev = vk->device.device;
    if (f->vbuf) {
        vkUnmapMemory(dev, f->vbuf_mem);
        vkDestroyBuffer(dev, f->vbuf, nullptr);
        vkFreeMemory(dev, f->vbuf_mem, nullptr);
    }
    VkDeviceSize new_size = f->vbuf_size ? f->vbuf_size * 2 : VERTEX_BUF_INITIAL;
    while ((size_t)new_size < need) new_size *= 2;

    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = new_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult res = vkCreateBuffer(dev, &bi, nullptr, &f->vbuf);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, f->vbuf, &req);

    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(vk->device.physical_device, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    res = vkAllocateMemory(dev, &ai, nullptr, &f->vbuf_mem);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS)
        return false;

    FLUX_VK_CHECK(vkBindBufferMemory(dev, f->vbuf, f->vbuf_mem, 0));
    FLUX_VK_CHECK(vkMapMemory(dev, f->vbuf_mem, 0, new_size, 0, &f->vbuf_map));
    f->vbuf_size = (size_t)new_size;
    return true;
}

VkDescriptorSet get_descriptor_set(vk_renderer *vk, vk_frame *f,
                                   VkImageView view, VkSampler sampler)
{
    for (uint32_t i = 0; i < f->desc_count; i++) {
        if (f->desc_views[i] == view && f->desc_samplers[i] == sampler)
            return f->desc_sets[i];
    }
    if (f->desc_count >= FLUX_MAX_DESC_CACHE) return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = f->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk->image_dsl,
    };
    VkDescriptorSet set;
    VkResult res = vkAllocateDescriptorSets(vk->device.device, &ai, &set);
    FLUX_VK_CHECK(res);
    if (res != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkDescriptorImageInfo ii = {
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &ii,
    };
    vkUpdateDescriptorSets(vk->device.device, 1, &w, 0, nullptr);

    f->desc_sets[f->desc_count] = set;
    f->desc_views[f->desc_count] = view;
    f->desc_samplers[f->desc_count] = sampler;
    f->desc_count++;
    return set;
}

void vk_draw_solid(flux_rhi_device *r, flux_r_buffer *buf,
                   uint32_t first, uint32_t n, flux_color c)
{
    (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->solid_pipeline);
    apply_blend_mode(vk, cmd);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    flux_solid_color_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .pad = {0,0},
        .color = {
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 8)  & 0xFF) / 255.0f,
            (c & 0xFF) / 255.0f,
            ((c >> 24) & 0xFF) / 255.0f,
        },
    };
    vkCmdPushConstants(cmd, vk->solid_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_draw_fringe(flux_rhi_device *r, flux_r_buffer *buf,
                    uint32_t first, uint32_t n, flux_color c)
{
    (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->fringe_pipeline);
    apply_blend_mode(vk, cmd);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    flux_solid_color_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .pad = {0,0},
        .color = {
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 8)  & 0xFF) / 255.0f,
            (c & 0xFF) / 255.0f,
            ((c >> 24) & 0xFF) / 255.0f,
        },
    };
    vkCmdPushConstants(cmd, vk->solid_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_flush_solid(flux_rhi_device *r)
{
    (void)r;
}

void vk_draw_image(flux_rhi_device *r, flux_r_buffer *buf,
                   uint32_t first, uint32_t n, flux_r_texture *tex, flux_color tint)
{
    (void)tint;
    (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began || !tex) return;
    vk_texture *t = (vk_texture *)tex;
#ifndef NDEBUG
    t->last_use_fence = vk->frames[vk->frame_idx].fence;
#endif
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->image_pipeline);
    apply_blend_mode(vk, cmd);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    VkDescriptorSet set = get_descriptor_set(vk, &vk->frames[vk->frame_idx], t->view, vk->sampler);
    if (set) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->image_layout,
                                0, 1, &set, 0, nullptr);
    }

    flux_image_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .pad = {0,0},
    };
    vkCmdPushConstants(cmd, vk->image_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_draw_text(flux_rhi_device *r, flux_r_buffer *buf,
                  uint32_t first, uint32_t n, flux_color c)
{
    (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->text_pipeline);
    apply_blend_mode(vk, cmd);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    VkDescriptorSet set = get_descriptor_set(vk, &vk->frames[vk->frame_idx], vk->atlas_view, vk->sampler);
    if (set) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->text_layout,
                                0, 1, &set, 0, nullptr);
    }

    flux_text_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .pad = {0,0},
        .color = {
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 8)  & 0xFF) / 255.0f,
            (c & 0xFF) / 255.0f,
            ((c >> 24) & 0xFF) / 255.0f,
        },
    };
    vkCmdPushConstants(cmd, vk->text_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_draw_gradient(flux_rhi_device *r, flux_r_buffer *buf,
                      uint32_t first, uint32_t n, const flux_gradient *g)
{
    (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began || !g) return;
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->gradient_pipeline);
    apply_blend_mode(vk, cmd);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    flux_gradient_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .mode = g->mode,
        .stop_count = g->stop_count,
        .start = { g->start[0], g->start[1] },
        .end   = { g->end[0],   g->end[1]   },
    };
    memcpy(pc.colors, g->colors, sizeof(pc.colors));
    memcpy(pc.stops, g->stops, sizeof(pc.stops));

    vkCmdPushConstants(cmd, vk->gradient_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_scissor(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    VkRect2D rc = { { x, y }, { w, h } };
    vkCmdSetScissor(vk->frames[vk->frame_idx].cmd, 0, 1, &rc);
}

void vk_stencil_clear(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    VkClearAttachment att = {
        .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
        .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0 } },
    };
    VkClearRect rect = {
        .rect = { { x, y }, { w, h } },
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vkCmdClearAttachments(vk->frames[vk->frame_idx].cmd, 1, &att, 1, &rect);
}

void vk_stencil_fill(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t n, int fill_rule)
{
    (void)fill_rule; (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->fill_stencil_pipeline);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    flux_solid_color_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .pad = {0,0},
        .color = {0,0,0,0},
    };
    vkCmdPushConstants(cmd, vk->stencil_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_stencil_ref(flux_rhi_device *r, uint32_t ref)
{
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    vkCmdSetStencilReference(vk->frames[vk->frame_idx].cmd,
                             VK_STENCIL_FACE_FRONT_AND_BACK, ref);
}

void vk_cover_solid(flux_rhi_device *r, flux_r_buffer *buf,
                    uint32_t first, uint32_t n, flux_color c)
{
    (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began) return;
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->solid_cover_pipeline);
    apply_blend_mode(vk, cmd);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    flux_solid_color_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .pad = {0,0},
        .color = {
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 8)  & 0xFF) / 255.0f,
            (c & 0xFF) / 255.0f,
            ((c >> 24) & 0xFF) / 255.0f,
        },
    };
    vkCmdPushConstants(cmd, vk->solid_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_cover_gradient(flux_rhi_device *r, flux_r_buffer *buf,
                       uint32_t first, uint32_t n, const flux_gradient *g)
{
    (void)buf;
    vk_renderer *vk = VKR(r);
    if (!vk->pass_began || !g) return;
    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->gradient_cover_pipeline);
    apply_blend_mode(vk, cmd);

    VkBuffer vbuf = vk->frames[vk->frame_idx].vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

    flux_gradient_pc pc = {
        .surface_size = { (float)vk->sc_extent.width, (float)vk->sc_extent.height },
        .mode = g->mode,
        .stop_count = g->stop_count,
        .start = { g->start[0], g->start[1] },
        .end   = { g->end[0],   g->end[1]   },
    };
    memcpy(pc.colors, g->colors, sizeof(pc.colors));
    memcpy(pc.stops, g->stops, sizeof(pc.stops));

    vkCmdPushConstants(cmd, vk->gradient_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, n, 1, first, 0);
}

void vk_blur(flux_rhi_device *r, float sigma)
{
    vk_renderer *vk = VKR(r);
    if (!vk->frame_began || sigma <= 0.0f) return;
    if (!ensure_pipelines(vk)) return;
    if (!create_blur_src(vk)) return;

    VkCommandBuffer cmd = vk->frames[vk->frame_idx].cmd;
    uint32_t w = vk->sc_extent.width;
    uint32_t h = vk->sc_extent.height;

    /* End current render pass so we can copy the framebuffer. */
    if (vk->pass_began) {
        vkCmdEndRenderPass(cmd);
        vk->pass_began = false;
    }

    VkImage src_img = vk->sc_images[vk->acquired_img_idx].image;
    VkImage dst_img = vk->blur_src_image;

    /* Transition swapchain image to TRANSFER_SRC. */
    VkImageMemoryBarrier barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src_img,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst_img,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);

    VkImageCopy copy = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffset = { 0, 0, 0 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffset = { 0, 0, 0 },
        .extent = { w, h, 1 },
    };
    vkCmdCopyImage(cmd, src_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    /* Transition swapchain image back to PRESENT_SRC and blur_src to SHADER_READ. */
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);

    /* Re-begin render pass with LOAD (preserve blurred source on framebuffer). */
    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk->blur_render_pass,
        .framebuffer = vk->sc_images[vk->acquired_img_idx].framebuffer,
        .renderArea = { {0, 0}, {w, h} },
    };
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vk->pass_began = true;

    /* Fullscreen quad vertices. */
    vk_frame *f = &vk->frames[vk->frame_idx];
    flux_r_buffer *buf = nullptr;
    uint32_t first = 0;
    flux_image_vertex *v = vk_alloc_image(r, 6, &buf, &first);
    if (!v) return;
    v[0] = (flux_image_vertex){ { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    v[1] = (flux_image_vertex){ { (float)w, 0.0f }, { 1.0f, 0.0f } };
    v[2] = (flux_image_vertex){ { (float)w, (float)h }, { 1.0f, 1.0f } };
    v[3] = (flux_image_vertex){ { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    v[4] = (flux_image_vertex){ { (float)w, (float)h }, { 1.0f, 1.0f } };
    v[5] = (flux_image_vertex){ { 0.0f, (float)h }, { 0.0f, 1.0f } };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->blur_pipeline);
    apply_blend_mode(vk, cmd);
    VkBuffer vb = f->vbuf;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);

    VkDescriptorSet set = get_descriptor_set(vk, f, vk->blur_src_view, vk->sampler);
    if (set) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->blur_layout,
                                0, 1, &set, 0, nullptr);
    }

    typedef struct { float surface_size[2], pad[2], texel_size[2]; } flux_blur_pc;
    flux_blur_pc pc = {
        .surface_size = { (float)w, (float)h },
        .pad = {0, 0},
        .texel_size = { sigma / (float)w, sigma / (float)h },
    };
    vkCmdPushConstants(cmd, vk->blur_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 6, 1, first, 0);
}
