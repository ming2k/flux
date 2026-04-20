#include "flux/flux_wayland.h"
#include "internal.h"

#include <math.h>

#include <vulkan/vulkan_wayland.h>

/* Converts 0xAARRGGBB premultiplied to a VkClearColorValue.
 * When targetting a SRGB swapchain format, Vulkan expects linear
 * floats — the presentation hardware applies the sRGB EOTF. We treat
 * the input color as sRGB 8-bit and convert to linear here. */
static VkClearColorValue to_clear_color(fx_color c, VkFormat fmt)
{
    float a = ((c >> 24) & 0xFF) / 255.0f;
    float r = ((c >> 16) & 0xFF) / 255.0f;
    float g = ((c >>  8) & 0xFF) / 255.0f;
    float b = ((c      ) & 0xFF) / 255.0f;

    bool srgb = (fmt == VK_FORMAT_B8G8R8A8_SRGB ||
                 fmt == VK_FORMAT_R8G8B8A8_SRGB);
    if (srgb) {
#       define EOTF(v) ((v) <= 0.04045f \
                        ? (v) / 12.92f \
                        : powf(((v) + 0.055f) / 1.055f, 2.4f))
        r = EOTF(r);
        g = EOTF(g);
        b = EOTF(b);
#       undef EOTF
    }
    VkClearColorValue out;
    out.float32[0] = r;
    out.float32[1] = g;
    out.float32[2] = b;
    out.float32[3] = a;
    return out;
}

static bool rect_has_area(const fx_rect *rect)
{
    return rect && rect->w > 0.0f && rect->h > 0.0f;
}

static void bind_solid_pipeline(fx_surface *s, VkCommandBuffer cmd)
{
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)s->extent.width,
        .height = (float)s->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = s->extent,
    };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      s->solid_rect_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

static void push_solid_color(fx_surface *s, VkCommandBuffer cmd, fx_color color)
{
    VkClearColorValue linear = to_clear_color(color, s->surface_format.format);
    fx_solid_color_pc pc = {
        .surface_size = { (float)s->extent.width, (float)s->extent.height },
        .pad = { 0.0f, 0.0f },
        .color = {
            linear.float32[0],
            linear.float32[1],
            linear.float32[2],
            linear.float32[3],
        },
    };

    vkCmdPushConstants(cmd, s->solid_rect_layout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
}

typedef struct {
    fx_color color;
    VkBuffer vbuf;
    uint32_t first_vertex;
    uint32_t vertex_count;
    bool active;
} fx_batch;

static void flush_batch(fx_surface *s, VkCommandBuffer cmd, fx_batch *batch)
{
    if (!batch->active || batch->vertex_count == 0) return;

    push_solid_color(s, cmd, batch->color);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &batch->vbuf, &offset);
    vkCmdDraw(cmd, batch->vertex_count, 1, batch->first_vertex, 0);

    batch->vertex_count = 0;
    batch->active = false;
}

static bool add_to_batch(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                         fx_batch *batch, fx_color color,
                         const fx_solid_vertex *verts, size_t count)
{
    if (batch->active && (batch->color != color)) {
        flush_batch(s, cmd, batch);
    }

    VkBuffer vbuf;
    VkDeviceSize offset;
    fx_solid_vertex *map = fx_vbuf_pool_alloc(&fr->vbuf, count * sizeof(fx_solid_vertex), &vbuf, &offset);
    if (!map) return false;

    uint32_t first = (uint32_t)(offset / sizeof(fx_solid_vertex));

    if (batch->active && batch->vbuf == vbuf && (batch->first_vertex + batch->vertex_count == first)) {
        /* Continue existing batch */
        memcpy(map, verts, count * sizeof(fx_solid_vertex));
        batch->vertex_count += (uint32_t)count;
    } else {
        /* Start new batch */
        flush_batch(s, cmd, batch);
        memcpy(map, verts, count * sizeof(fx_solid_vertex));
        batch->active = true;
        batch->color = color;
        batch->vbuf = vbuf;
        batch->first_vertex = first;
        batch->vertex_count = (uint32_t)count;
    }
    return true;
}

static bool draw_solid_rect(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                            fx_batch *batch, const fx_rect *rect, fx_color color)
{
    fx_solid_vertex verts[6];

    if (!rect_has_area(rect)) return false;

    verts[0] = (fx_solid_vertex){ .pos = { rect->x, rect->y } };
    verts[1] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y } };
    verts[2] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y + rect->h } };
    verts[3] = (fx_solid_vertex){ .pos = { rect->x, rect->y } };
    verts[4] = (fx_solid_vertex){ .pos = { rect->x + rect->w, rect->y + rect->h } };
    verts[5] = (fx_solid_vertex){ .pos = { rect->x, rect->y + rect->h } };

    return add_to_batch(s, fr, cmd, batch, color, verts, 6);
}

static bool draw_polygon_fill(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                              fx_batch *batch, const fx_point *points, size_t count,
                              fx_color color)
{
    fx_point *tris = NULL;
    size_t tri_points = 0;
    fx_solid_vertex *verts = NULL;
    bool ok = false;

    if (!points || count < 3) return false;

    if (!fx_tessellate_simple_polygon(points, count, &tris, &tri_points))
        return false;

    verts = malloc(tri_points * sizeof(*verts));
    if (!verts) goto done;

    for (size_t i = 0; i < tri_points; ++i)
        verts[i] = (fx_solid_vertex){ .pos = { tris[i].x, tris[i].y } };

    ok = add_to_batch(s, fr, cmd, batch, color, verts, tri_points);

done:
    free(verts);
    free(tris);
    return ok;
}

static bool draw_rect_stroke(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                             fx_batch *batch, const fx_rect *rect, float width,
                             fx_color color)
{
    float half_w;
    fx_rect outer;
    fx_rect inner;
    bool emitted = false;

    if (!rect || width <= 0.0f) return false;

    half_w = width * 0.5f;
    outer = (fx_rect){
        .x = rect->x - half_w,
        .y = rect->y - half_w,
        .w = rect->w + width,
        .h = rect->h + width,
    };
    inner = (fx_rect){
        .x = rect->x + half_w,
        .y = rect->y + half_w,
        .w = rect->w - width,
        .h = rect->h - width,
    };

    if (inner.w <= 0.0f || inner.h <= 0.0f)
        return draw_solid_rect(s, fr, cmd, batch, &outer, color);

    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = outer.x,
        .y = outer.y,
        .w = outer.w,
        .h = inner.y - outer.y,
    }, color);
    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = outer.x,
        .y = inner.y + inner.h,
        .w = outer.w,
        .h = (outer.y + outer.h) - (inner.y + inner.h),
    }, color);
    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = outer.x,
        .y = inner.y,
        .w = inner.x - outer.x,
        .h = inner.h,
    }, color);
    emitted |= draw_solid_rect(s, fr, cmd, batch, &(fx_rect){
        .x = inner.x + inner.w,
        .y = inner.y,
        .w = (outer.x + outer.w) - (inner.x + inner.w),
        .h = inner.h,
    }, color);

    return emitted;
}

static bool draw_image_quad(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                            fx_batch *batch, const fx_image *image,
                            const fx_rect *src, const fx_rect *dst)
{
    flush_batch(s, cmd, batch);

    fx_image_vertex verts[6];
    float sw = (float)image->desc.width;
    float sh = (float)image->desc.height;
    float u0 = src->x / sw, v0 = src->y / sh;
    float u1 = (src->x + src->w) / sw, v1 = (src->y + src->h) / sh;

    verts[0] = (fx_image_vertex){ { dst->x, dst->y }, { u0, v0 } };
    verts[1] = (fx_image_vertex){ { dst->x + dst->w, dst->y }, { u1, v0 } };
    verts[2] = (fx_image_vertex){ { dst->x + dst->w, dst->y + dst->h }, { u1, v1 } };
    verts[3] = (fx_image_vertex){ { dst->x, dst->y }, { u0, v0 } };
    verts[4] = (fx_image_vertex){ { dst->x + dst->w, dst->y + dst->h }, { u1, v1 } };
    verts[5] = (fx_image_vertex){ { dst->x, dst->y + dst->h }, { u0, v1 } };

    VkBuffer vbuf;
    VkDeviceSize offset;
    void *map = fx_vbuf_pool_alloc(&fr->vbuf, sizeof(verts), &vbuf, &offset);
    if (!map) return false;
    memcpy(map, verts, sizeof(verts));

    /* Descriptor Set */
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = fr->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &s->image_dsl,
    };
    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(s->ctx->device, &ai, &ds) != VK_SUCCESS) return false;

    VkDescriptorImageInfo dii = {
        .sampler = s->sampler,
        .imageView = image->vk_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = ds,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &dii,
    };
    vkUpdateDescriptorSets(s->ctx->device, 1, &write, 0, NULL);

    /* Draw */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->image_pipeline);
    
    fx_image_pc pc = { .surface_size = { (float)s->extent.width, (float)s->extent.height } };
    vkCmdPushConstants(cmd, s->image_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->image_layout, 0, 1, &ds, 0, NULL);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
    vkCmdDraw(cmd, 6, 1, 0, 0);

    /* Re-bind solid pipeline for subsequent ops */
    bind_solid_pipeline(s, cmd);
    return true;
}

static bool draw_glyph_run(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd,
                           fx_batch *batch, const fx_draw_glyphs_op *op)
{
    flush_batch(s, cmd, batch);

    size_t count = fx_glyph_run_count(op->run);
    const fx_glyph *glyphs = fx_glyph_run_data(op->run);
    fx_image_vertex *verts = malloc(count * 6 * sizeof(fx_image_vertex));
    if (!verts) return false;

    size_t active_count = 0;
    for (size_t i = 0; i < count; ++i) {
        fx_atlas_entry ent;
        if (!fx_atlas_ensure_glyph(s->ctx, (fx_font *)op->font, glyphs[i].glyph_id, &ent)) continue;

        float x = op->x + glyphs[i].x + (float)ent.bearing_x;
        float y = op->y + glyphs[i].y - (float)ent.bearing_y;
        float w = (float)ent.w;
        float h = (float)ent.h;

        fx_image_vertex *v = &verts[active_count * 6];
        v[0] = (fx_image_vertex){ { x,     y     }, { ent.u0, ent.v0 } };
        v[1] = (fx_image_vertex){ { x + w, y     }, { ent.u1, ent.v0 } };
        v[2] = (fx_image_vertex){ { x + w, y + h }, { ent.u1, ent.v1 } };
        v[3] = (fx_image_vertex){ { x,     y     }, { ent.u0, ent.v0 } };
        v[4] = (fx_image_vertex){ { x + w, y + h }, { ent.u1, ent.v1 } };
        v[5] = (fx_image_vertex){ { x,     y + h }, { ent.u0, ent.v1 } };
        active_count++;
    }

    if (active_count == 0) {
        free(verts);
        return true;
    }

    VkBuffer vbuf;
    VkDeviceSize offset;
    void *map = fx_vbuf_pool_alloc(&fr->vbuf, active_count * 6 * sizeof(fx_image_vertex), &vbuf, &offset);
    if (!map) { free(verts); return false; }
    memcpy(map, verts, active_count * 6 * sizeof(fx_image_vertex));
    free(verts);

    /* Descriptor Set for Atlas */
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = fr->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &s->image_dsl,
    };
    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(s->ctx->device, &ai, &ds) != VK_SUCCESS) return false;

    VkDescriptorImageInfo dii = {
        .sampler = s->sampler,
        .imageView = s->ctx->atlas.image->vk_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = ds,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &dii,
    };
    vkUpdateDescriptorSets(s->ctx->device, 1, &write, 0, NULL);

    /* Draw */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->text_pipeline);
    
    VkClearColorValue linear = to_clear_color(op->paint.color, s->surface_format.format);
    fx_text_pc pc = {
        .surface_size = { (float)s->extent.width, (float)s->extent.height },
        .color = { linear.float32[0], linear.float32[1], linear.float32[2], linear.float32[3] },
    };
    vkCmdPushConstants(cmd, s->text_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->text_layout, 0, 1, &ds, 0, NULL);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
    vkCmdDraw(cmd, (uint32_t)active_count * 6, 1, 0, 0);

    /* Re-bind solid pipeline */
    bind_solid_pipeline(s, cmd);
    return true;
}

static size_t record_bootstrap_ops(fx_surface *s, fx_frame *fr, VkCommandBuffer cmd)
{
    size_t executed = 0;
    fx_batch batch = { .active = false };

    bind_solid_pipeline(s, cmd);

    for (size_t i = 0; i < s->canvas.op_count; ++i) {
        const fx_op *op = &s->canvas.ops[i];
        fx_rect rect;
        const fx_point *points = NULL;
        size_t point_count = 0;
        fx_point *flattened = NULL;
        bool closed = false;
        fx_point *stroke_tris = NULL;
        size_t stroke_count = 0;

        if (op->kind == FX_OP_FILL_PATH) {
            if (fx_path_is_axis_aligned_rect(op->u.fill_path.path, &rect)) {
                if (draw_solid_rect(s, fr, cmd, &batch, &rect, op->u.fill_path.paint.color))
                    executed++;
                continue;
            }
            if (fx_path_get_line_loop(op->u.fill_path.path,
                                      &points, &point_count) &&
                draw_polygon_fill(s, fr, cmd, &batch, points, point_count,
                                  op->u.fill_path.paint.color)) {
                executed++;
                continue;
            }
            if (fx_path_flatten_line_loop(op->u.fill_path.path, 0.25f,
                                          &flattened, &point_count) &&
                draw_polygon_fill(s, fr, cmd, &batch, flattened, point_count,
                                  op->u.fill_path.paint.color)) {
                executed++;
            }
            free(flattened);
        } else if (op->kind == FX_OP_STROKE_PATH) {
            if (op->u.stroke_path.paint.line_join == FX_JOIN_MITER &&
                fx_path_is_axis_aligned_rect(op->u.stroke_path.path, &rect)) {
                if (draw_rect_stroke(s, fr, cmd, &batch, &rect,
                                     op->u.stroke_path.paint.stroke_width,
                                     op->u.stroke_path.paint.color))
                    executed++;
                continue;
            }
            if (!fx_path_flatten_polyline(op->u.stroke_path.path, 0.25f,
                                          &flattened, &point_count, &closed))
                continue;
            if (fx_stroke_polyline(flattened, point_count, closed,
                                   &op->u.stroke_path.paint,
                                   &stroke_tris, &stroke_count)) {
                fx_solid_vertex *verts = (fx_solid_vertex *)stroke_tris;
                if (add_to_batch(s, fr, cmd, &batch, op->u.stroke_path.paint.color,
                                 verts, stroke_count)) {
                    executed++;
                }
            }
            free(stroke_tris);
            free(flattened);
        } else if (op->kind == FX_OP_DRAW_IMAGE) {
            if (draw_image_quad(s, fr, cmd, &batch, op->u.draw_image.image,
                                &op->u.draw_image.src, &op->u.draw_image.dst))
                executed++;
        } else if (op->kind == FX_OP_DRAW_GLYPHS) {
            if (draw_glyph_run(s, fr, cmd, &batch, &op->u.draw_glyphs))
                executed++;
        }
    }

    flush_batch(s, cmd, &batch);
    return executed;
}

fx_surface *fx_surface_create_wayland(fx_context *ctx,
                                      struct wl_display *display,
                                      struct wl_surface *wl_surface,
                                      int32_t width, int32_t height,
                                      fx_color_space cs)
{
    if (!ctx || !display || !wl_surface) return NULL;

    fx_surface *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx          = ctx;
    s->requested_w  = width;
    s->requested_h  = height;
    s->color_space  = cs;
    s->canvas.owner = s;

    VkWaylandSurfaceCreateInfoKHR wsci = {
        .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = display,
        .surface = wl_surface,
    };
    PFN_vkCreateWaylandSurfaceKHR create_fn =
        (PFN_vkCreateWaylandSurfaceKHR)
        vkGetInstanceProcAddr(ctx->instance, "vkCreateWaylandSurfaceKHR");
    if (!create_fn) {
        FX_LOGE(ctx, "vkCreateWaylandSurfaceKHR not exposed");
        free(s);
        return NULL;
    }
    if (create_fn(ctx->instance, &wsci, NULL, &s->vk_surface) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateWaylandSurfaceKHR failed");
        free(s);
        return NULL;
    }

    if (!ctx->device) {
        if (!fx_device_init(ctx, s->vk_surface)) {
            vkDestroySurfaceKHR(ctx->instance, s->vk_surface, NULL);
            free(s);
            return NULL;
        }
    } else {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(ctx->phys, ctx->graphics_family,
                                             s->vk_surface, &supported);
        if (!supported) {
            FX_LOGE(ctx, "device cannot present to this wl_surface");
            vkDestroySurfaceKHR(ctx->instance, s->vk_surface, NULL);
            free(s);
            return NULL;
        }
    }

    if (!fx_swapchain_build(s)) {
        vkDestroySurfaceKHR(ctx->instance, s->vk_surface, NULL);
        free(s);
        return NULL;
    }
    return s;
}

void fx_surface_resize(fx_surface *s, int32_t w, int32_t h)
{
    if (!s) return;
    s->requested_w   = w;
    s->requested_h   = h;
    s->needs_recreate = true;
}

static bool recreate_swapchain(fx_surface *s)
{
    fx_surface_wait_idle(s);
    vkDeviceWaitIdle(s->ctx->device);
    fx_swapchain_destroy(s);
    return fx_swapchain_build(s);
}

static void destroy_surface(fx_surface *s)
{
    fx_swapchain_destroy(s);
    fx_canvas_dispose(&s->canvas);
    if (s->vk_surface) {
        vkDestroySurfaceKHR(s->ctx->instance, s->vk_surface, NULL);
        s->vk_surface = VK_NULL_HANDLE;
    }
    free(s);
}

void fx_surface_destroy(fx_surface *s)
{
    if (!s) return;
    fx_surface_wait_idle(s);
    vkDeviceWaitIdle(s->ctx->device);
    destroy_surface(s);
}

fx_canvas *fx_surface_acquire(fx_surface *s)
{
    if (!s) return NULL;

    if (s->needs_recreate) {
        if (!recreate_swapchain(s)) return NULL;
        if (s->needs_recreate) return NULL;  /* still zero extent */
    }

    fx_frame *fr = &s->frames[s->frame_index];
    vkWaitForFences(s->ctx->device, 1, &fr->in_flight, VK_TRUE, UINT64_MAX);

    VkResult ar = vkAcquireNextImageKHR(s->ctx->device, s->swapchain,
                                        UINT64_MAX,
                                        fr->image_available,
                                        VK_NULL_HANDLE,
                                        &s->acquired_image);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        s->needs_recreate = true;
        if (!recreate_swapchain(s)) return NULL;
        return fx_surface_acquire(s);
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        FX_LOGE(s->ctx, "vkAcquireNextImageKHR: %d", (int)ar);
        return NULL;
    }

    vkResetFences(s->ctx->device, 1, &fr->in_flight);
    fx_vbuf_pool_reset(&fr->vbuf);
    vkResetDescriptorPool(s->ctx->device, fr->desc_pool, 0);

    /* Reset display list for the new frame. */
    fx_canvas_reset(&s->canvas);
    return &s->canvas;
}

void fx_surface_present(fx_surface *s)
{
    if (!s) return;
    fx_frame *fr = &s->frames[s->frame_index];

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkResetCommandBuffer(fr->cmd, 0);
    vkBeginCommandBuffer(fr->cmd, &bi);

    VkClearValue cv = { 0 };
    if (s->canvas.has_clear)
        cv.color = to_clear_color(s->canvas.clear_color,
                                  s->surface_format.format);

    VkRenderPassBeginInfo rpbi = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = s->render_pass,
        .framebuffer = s->images[s->acquired_image].framebuffer,
        .renderArea  = { .offset = {0,0}, .extent = s->extent },
        .clearValueCount = 1,
        .pClearValues    = &cv,
    };
    vkCmdBeginRenderPass(fr->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    size_t executed_ops = record_bootstrap_ops(s, fr, fr->cmd);
    if (s->canvas.op_count > executed_ops && !s->reported_unimplemented_ops) {
        FX_LOGI(s->ctx,
                "executed %zu/%zu recorded canvas ops; remaining ops still "
                "wait for the Vulkan raster backend",
                executed_ops, s->canvas.op_count);
        s->reported_unimplemented_ops = true;
    }
    vkCmdEndRenderPass(fr->cmd);
    vkEndCommandBuffer(fr->cmd);

    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &fr->image_available,
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &fr->cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &fr->render_finished,
    };
    FX_CHECK_VK(s->ctx, vkQueueSubmit(s->ctx->graphics_queue, 1, &si,
                                      fr->in_flight));

    VkPresentInfoKHR pi = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &fr->render_finished,
        .swapchainCount     = 1,
        .pSwapchains        = &s->swapchain,
        .pImageIndices      = &s->acquired_image,
    };
    VkResult pr = vkQueuePresentKHR(s->ctx->graphics_queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        s->needs_recreate = true;
    } else if (pr != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkQueuePresentKHR: %d", (int)pr);
    }

    s->frame_index = (s->frame_index + 1) % FX_MAX_FRAMES_IN_FLIGHT;
}
