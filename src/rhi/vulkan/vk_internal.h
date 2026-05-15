/*
 * Vulkan RHI backend private header.
 *
 * Shared across all vulkan/ implementation files.
 * Not visible outside the RHI backend.
 */
#ifndef FLUX_VK_INTERNAL_H
#define FLUX_VK_INTERNAL_H

#include "internal.h"
#include "flux/flux.h"
#include "flux/flux_vulkan.h"
#include "rhi/rhi.h"
#include <vulkan/vulkan.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

#define FLUX_MAX_SWAPCHAIN_IMAGES 8
#define FLUX_MAX_FRAMES_IN_FLIGHT 2
#define FLUX_MAX_DESC_CACHE       64
#define FLUX_MAX_STAGING_BUFFERS  8
#define VERTEX_BUF_INITIAL      (1 << 20) /* 1 MiB */

/* ------------------------------------------------------------------ */
/*  Push constants (must match shaders exactly)                       */
/* ------------------------------------------------------------------ */

typedef struct {
    float surface_size[2];
    float pad[2];
    float color[4];
} flux_solid_color_pc;

typedef struct {
    float surface_size[2];
    float pad[2];
} flux_image_pc;

typedef struct {
    float surface_size[2];
    float pad[2];
    float color[4];
} flux_text_pc;

typedef struct {
    float surface_size[2];
    uint32_t mode;
    uint32_t stop_count;
    float start[2];
    float end[2];
    float colors[4][4];
    float stops[4];
} flux_gradient_pc;

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    VkImage       image;
    VkImageView   view;
    VkFramebuffer framebuffer;
    VkImage       stencil_image;
    VkDeviceMemory stencil_mem;
    VkImageView    stencil_view;
} vk_sc_image;

typedef struct vk_texture {
    VkImage          image;
    VkDeviceMemory   mem;
    VkImageView      view;
    uint32_t         w, h;
    VkFormat         format;
    struct vk_texture *next;
#ifndef NDEBUG
    void            *last_use_fence;
#endif
} vk_texture;

typedef struct {
    VkCommandBuffer cmd;
    VkSemaphore     sem_image_avail;
    VkSemaphore     sem_render_done;
    VkFence         fence;

    VkBuffer        vbuf;
    VkDeviceMemory  vbuf_mem;
    size_t          vbuf_size;
    size_t          vbuf_cursor;
    void           *vbuf_map;

    VkDescriptorPool desc_pool;
    VkDescriptorSet  desc_sets[FLUX_MAX_DESC_CACHE];
    VkImageView      desc_views[FLUX_MAX_DESC_CACHE];
    VkSampler        desc_samplers[FLUX_MAX_DESC_CACHE];
    uint32_t         desc_count;
} vk_frame;

typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    size_t         size;
    void          *map;
    bool           in_use;
} vk_staging_buffer;

typedef struct {
    vk_staging_buffer buffers[FLUX_MAX_STAGING_BUFFERS];
    uint32_t          count;
} vk_staging_pool;

typedef struct {
    const flux_rhi_vtbl *vtbl;
    flux_vulkan_device   device;

    VkSurfaceKHR   surface;
    VkSwapchainKHR swapchain;
    VkFormat       sc_format;
    VkExtent2D     sc_extent;
    uint32_t       image_count;
    vk_sc_image    sc_images[FLUX_MAX_SWAPCHAIN_IMAGES];

    vk_frame    frames[FLUX_MAX_FRAMES_IN_FLIGHT];
    uint32_t    frame_idx;
    uint32_t    acquired_img_idx;
    bool        frame_began;
    bool        pass_began;

    VkCommandPool cmd_pool;

    VkRenderPass   render_pass;
    VkRenderPass   blur_render_pass;

    VkImage        blur_src_image;
    VkDeviceMemory blur_src_mem;
    VkImageView    blur_src_view;

    VkPipelineLayout solid_layout;
    VkPipeline       solid_pipeline;
    VkPipelineLayout image_layout;
    VkPipeline       image_pipeline;
    VkPipelineLayout text_layout;
    VkPipeline       text_pipeline;
    VkPipelineLayout gradient_layout;
    VkPipeline       gradient_pipeline;
    VkPipelineLayout stencil_layout;
    VkPipeline       stencil_pipeline;
    VkPipeline       fill_stencil_pipeline;
    VkPipeline       solid_cover_pipeline;
    VkPipeline       gradient_cover_pipeline;
    VkPipelineLayout blur_layout;
    VkPipeline       blur_pipeline;
    VkPipeline       fringe_pipeline;
    VkDescriptorSetLayout image_dsl;
    bool pipelines_ready;

    VkSampler sampler;

    VkImage        atlas_image;
    VkDeviceMemory atlas_mem;
    VkImageView    atlas_view;

    vk_texture *textures;

    uint32_t w, h;
    bool needs_recreate;

    vk_staging_pool  staging_pool;
    VkCommandPool    transfer_cmd_pool;
    VkCommandBuffer  transfer_cmd;
    VkFence          transfer_fence;
    bool             transfer_pending;

    VkPipelineCache  pipeline_cache;

    flux_blend_mode current_blend_mode;
    bool            has_dynamic_blend;
    bool            has_blend_op_advanced;
    PFN_vkCmdSetColorBlendEquationEXT pfnCmdSetColorBlendEquationEXT;
} vk_renderer;

#define VKR(r) ((vk_renderer *)(r))

/* ------------------------------------------------------------------ */
/*  Inline helpers                                                    */
/* ------------------------------------------------------------------ */

static inline size_t align_up(size_t v, size_t a)
{
    return (v + a - 1) & ~(a - 1);
}

/* ------------------------------------------------------------------ */
/*  Logging                                                           */
/* ------------------------------------------------------------------ */

struct flux_context;
void flux_log_impl(const struct flux_context *ctx, flux_log_level lvl,
                 const char *file, int line,
                 const char *fmt, ...) __attribute__((format(printf, 5, 6)));

#define FLUX_LOGE(ctx, ...) flux_log_impl((ctx), FLUX_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define FLUX_LOGW(ctx, ...) flux_log_impl((ctx), FLUX_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define FLUX_LOGI(ctx, ...) flux_log_impl((ctx), FLUX_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#ifdef NDEBUG
#  define FLUX_LOGD(ctx, ...) ((void)0)
#  define FLUX_LOGT(ctx, ...) ((void)0)
#else
#  define FLUX_LOGD(ctx, ...) flux_log_impl((ctx), FLUX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#  define FLUX_LOGT(ctx, ...) flux_log_impl((ctx), FLUX_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#endif

/* ------------------------------------------------------------------ */
/*  Result checking macros                                            */
/* ------------------------------------------------------------------ */

#define FLUX_VK_CHECK(expr) \
    do { \
        VkResult _vkres = (expr); \
        if (_vkres != VK_SUCCESS) { \
            FLUX_LOGE(nullptr, "%s returned VkResult %d", #expr, (int)_vkres); \
            abort(); \
        } \
    } while(0)

#define FLUX_TRY_VK(ctx, expr) \
    do { \
        VkResult _r = (expr); \
        if (_r != VK_SUCCESS) { FLUX_LOGE((ctx), "%s => VkResult %d", #expr, (int)_r); return false; } \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Lifecycle & frame management (vulkan_rhi.c)                       */
/* ------------------------------------------------------------------ */

extern void vk_destroy(flux_rhi_device *r);
extern void vk_surface_extent(flux_rhi_device *r, uint32_t *w, uint32_t *h);
extern void vk_begin_frame(flux_rhi_device *r);
extern void vk_begin_pass(flux_rhi_device *r, flux_color clear);
extern void vk_end_pass(flux_rhi_device *r);
extern void vk_submit(flux_rhi_device *r);
extern bool vk_read_pixels(flux_rhi_device *r, void *data, size_t stride);
extern bool vk_resize(flux_rhi_device *r, uint32_t w, uint32_t h);
extern flux_solid_vertex *vk_alloc_solid(flux_rhi_device *r, size_t n,
                                        flux_r_buffer **buf, uint32_t *first);
extern flux_image_vertex *vk_alloc_image(flux_rhi_device *r, size_t n,
                                        flux_r_buffer **buf, uint32_t *first);

/* ------------------------------------------------------------------ */
/*  Swapchain & render pass (vk_swapchain.c)                          */
/* ------------------------------------------------------------------ */

extern uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t type_filter,
                                  VkMemoryPropertyFlags props);
extern void     destroy_swapchain_resources(vk_renderer *vk);
extern bool     create_swapchain(vk_renderer *vk);
extern bool     create_render_pass(vk_renderer *vk);
extern bool     create_stencil_and_framebuffers(vk_renderer *vk);
extern bool     create_blur_src(vk_renderer *vk);

/* ------------------------------------------------------------------ */
/*  Pipelines (vk_pipeline.c)                                         */
/* ------------------------------------------------------------------ */

extern void destroy_pipelines(vk_renderer *vk);
extern bool ensure_pipelines(vk_renderer *vk);

/* ------------------------------------------------------------------ */
/*  Textures & upload (vk_texture.c)                                  */
/* ------------------------------------------------------------------ */

extern VkFormat vk_pixel_format_to_vk(flux_pixel_format fmt);
extern bool     ensure_atlas(vk_renderer *vk);
extern bool     upload_texture_data(vk_renderer *vk, vk_texture *t,
                                     const void *data,
                                     uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h,
                                     size_t stride);
extern flux_r_texture *vk_texture_alloc(flux_rhi_device *r, uint32_t w, uint32_t h,
                                       flux_pixel_format fmt, const void *data, size_t stride);
extern void vk_texture_free(flux_rhi_device *r, flux_r_texture *tex);
extern void vk_texture_update(flux_rhi_device *r, flux_r_texture *tex,
                              const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
extern flux_r_texture *vk_surface_texture(flux_rhi_device *r);
extern void staging_pool_gc(vk_renderer *vk);
extern void staging_pool_destroy(vk_renderer *vk);

/* ------------------------------------------------------------------ */
/*  Draw commands (vk_draw.c)                                         */
/* ------------------------------------------------------------------ */

extern bool ensure_vbuf(vk_renderer *vk, vk_frame *f, size_t need);
extern VkDescriptorSet get_descriptor_set(vk_renderer *vk, vk_frame *f,
                                           VkImageView view, VkSampler sampler);
extern void vk_draw_solid(flux_rhi_device *r, flux_r_buffer *buf,
                          uint32_t first, uint32_t n, flux_color c);
extern void vk_flush_solid(flux_rhi_device *r);
extern flux_fringe_vertex *vk_alloc_fringe(flux_rhi_device *r, size_t n,
                                           flux_r_buffer **buf, uint32_t *first);
extern void vk_draw_fringe(flux_rhi_device *r, flux_r_buffer *buf,
                           uint32_t first, uint32_t n, flux_color c);
extern void vk_draw_image(flux_rhi_device *r, flux_r_buffer *buf,
                          uint32_t first, uint32_t n, flux_r_texture *tex, flux_color tint);
extern void vk_draw_text(flux_rhi_device *r, flux_r_buffer *buf,
                         uint32_t first, uint32_t n, flux_color c);
extern void vk_draw_gradient(flux_rhi_device *r, flux_r_buffer *buf,
                             uint32_t first, uint32_t n, const flux_gradient *g);
extern void vk_scissor(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
extern void vk_stencil_clear(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
extern void vk_stencil_fill(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t n, int fill_rule);
extern void vk_stencil_ref(flux_rhi_device *r, uint32_t ref);
extern void vk_cover_solid(flux_rhi_device *r, flux_r_buffer *buf,
                           uint32_t first, uint32_t n, flux_color c);
extern void vk_cover_gradient(flux_rhi_device *r, flux_r_buffer *buf,
                              uint32_t first, uint32_t n, const flux_gradient *g);
extern void vk_blur(flux_rhi_device *r, float sigma);

#endif /* FLUX_VK_INTERNAL_H */
