/*
 * Vulkan backend private header.
 *
 * Declares Vulkan-specific types that the backend implementation
 * needs. Not visible to the rest of the codebase.
 */
#ifndef FX_VK_INTERNAL_H
#define FX_VK_INTERNAL_H

#include "flux/flux.h"
#include "flux/flux_vulkan.h"
#include "renderer/renderer.h"
#include "renderer/vulkan/memory.h"
#include "state/state.h"
#include "geometry/geometry.h"
#include "math/matrix.h"
#include "math/rect.h"
#include "math/arena.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

/* ---- VMA opaque handles (must come before usage) ---- */

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

/* ---- constants ---- */

#define FX_MAX_PIPELINE_SETS   4
#define FX_MAX_FRAMES_IN_FLIGHT 2
#define FX_MAX_SWAPCHAIN_IMAGES 8

/* ---- logging ---- */

struct fx_context;

void fx_log_impl(const struct fx_context *ctx, fx_log_level lvl,
                 const char *file, int line,
                 const char *fmt, ...) __attribute__((format(printf, 5, 6)));

#define FX_LOGE(ctx, ...) fx_log_impl((ctx), FX_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define FX_LOGW(ctx, ...) fx_log_impl((ctx), FX_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define FX_LOGI(ctx, ...) fx_log_impl((ctx), FX_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#ifdef NDEBUG
#  define FX_LOGD(ctx, ...) ((void)0)
#  define FX_LOGT(ctx, ...) ((void)0)
#else
#  define FX_LOGD(ctx, ...) fx_log_impl((ctx), FX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#  define FX_LOGT(ctx, ...) fx_log_impl((ctx), FX_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#endif

#define FX_LOG_VK(ctx, expr) \
    do { \
        VkResult _r = (expr); \
        if (_r != VK_SUCCESS) { FX_LOGE((ctx), "%s => VkResult %d", #expr, (int)_r); } \
    } while (0)

#define FX_TRY_VK(ctx, expr) \
    do { \
        VkResult _r = (expr); \
        if (_r != VK_SUCCESS) { FX_LOGE((ctx), "%s => VkResult %d", #expr, (int)_r); return false; } \
    } while (0)

/* ---- pipeline set (must be before fx_context) ---- */

typedef struct {
    VkFormat              format;
    VkSampleCountFlagBits samples;
    VkRenderPass          template_render_pass;
    VkDescriptorSetLayout image_dsl;
    VkPipelineLayout      solid_rect_layout;
    VkPipeline            solid_rect_pipeline;
    VkPipelineLayout      image_layout;
    VkPipeline            image_pipeline;
    VkPipelineLayout      text_layout;
    VkPipeline            text_pipeline;
    VkPipelineLayout      gradient_layout;
    VkPipeline            gradient_pipeline;
    VkPipelineLayout      stencil_layout;
    VkPipeline            stencil_pipeline;
    VkPipeline            fill_stencil_pipeline;
    VkPipeline            solid_cover_pipeline;
    VkPipeline            gradient_cover_pipeline;
    VkPipelineLayout      blur_layout;
    VkPipeline            blur_pipeline;
} fx_pipeline_set;

/* ---- fx_context (Vulkan-specific members) ---- */

struct fx_context {
    fx_log_fn    log;
    void        *log_user;
    fx_log_level min_log_level;

    VkInstance                 instance;
    VkDebugUtilsMessengerEXT   debug_messenger;
    bool                       validation_enabled;

    VkPhysicalDevice           phys;
    VkPhysicalDeviceProperties phys_props;
    VkPhysicalDeviceMemoryProperties mem_props;
    bool                       queue_supports_present;

    VkDevice      device;
    uint32_t      graphics_family;
    VkQueue       graphics_queue;
    VkCommandPool frame_cmd_pool;

    VkPipelineCache   pipeline_cache;
    fx_pipeline_set   pipeline_sets[FX_MAX_PIPELINE_SETS];
    uint32_t          pipeline_set_count;

    VmaAllocator      vma_allocator;

    struct {
        VkBuffer        staging_buffer;
        VmaAllocation   staging_alloc;
        void           *staging_mapped;
        VkDeviceSize    staging_size;
        VkCommandBuffer cmd;
        VkFence         fence;
    } upload;

    /* glyph atlas */
    struct fx_image *atlas_image;
    void            *atlas_entries;
    size_t           atlas_entry_count;
    size_t           atlas_entry_cap;
    int              atlas_shelf_y, atlas_shelf_h, atlas_shelf_x;
};

/* ---- struct definitions (shared with internal.h) ---- */

struct fx_path {
    uint8_t  *verbs;
    size_t    verb_count, verb_cap;
    fx_point *points;
    size_t    point_count, point_cap;
    fx_rect   bounds;
    bool      has_bounds;
};

struct fx_glyph_run {
    fx_glyph *glyphs;
    size_t    count, cap;
};

struct fx_gradient {
    struct fx_context *ctx;
    uint32_t    mode;
    float       start[2], end[2];
    float       colors[4][4];
    float       stops[4];
    uint32_t    stop_count;
};

struct fx_image {
    struct fx_context *ctx;
    fx_image_desc  desc;
    void          *data;
    size_t         data_size;
    fx_r_texture  *rtex;
    VkImage        vk_image;
    VkImageView    vk_view;
    VmaAllocation  vma_alloc;
    VkFence        last_use_fence;
};

struct fx_surface;

struct fx_canvas {
    struct fx_surface *owner;
    fx_color    clear_color;
    bool        has_clear;
    fx_op      *ops;
    size_t      op_count, op_cap;
    fx_matrix  *state_stack;
    size_t      state_count, state_cap;
    fx_matrix   current_matrix;
    float       dpr;
};

/* ---- prototypes ---- */

VkFormat fx_pixel_format_to_vk(fx_pixel_format fmt);

fx_pipeline_set *fx_pipeline_set_get(struct fx_context *ctx,
                                      VkFormat color_format,
                                      VkSampleCountFlagBits samples);
void fx_pipeline_set_destroy_all(struct fx_context *ctx);

bool fx_upload_init(struct fx_context *ctx);
void fx_upload_shutdown(struct fx_context *ctx);
bool fx_upload_image(struct fx_context *ctx, VkImage image,
                     VkImageLayout old_layout, VkImageLayout new_layout,
                     int32_t dst_x, int32_t dst_y,
                     uint32_t w, uint32_t h,
                     const void *data, size_t row_bytes, size_t bpp);

bool fx_instance_create(struct fx_context *ctx, const char *app_name,
                        bool want_validation,
                        const char *const *exts_wanted,
                        uint32_t exts_wanted_n);
bool fx_device_init(struct fx_context *ctx, VkSurfaceKHR probe);
void fx_device_shutdown(struct fx_context *ctx);
void fx_instance_destroy(struct fx_context *ctx);

/* fx_surface_vk is the Vulkan-specific surface impl */
typedef struct { int dummy; } fx_surface_vk;

bool fx_swapchain_build(fx_surface_vk *s);
void fx_swapchain_destroy(fx_surface_vk *s);
void fx_surface_wait_idle(fx_surface_vk *s);
bool fx_make_render_pass(fx_surface_vk *s, VkImageLayout final_layout);
bool fx_make_image_dsl(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_image_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_text_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_gradient_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_blur_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_stencil_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_fill_stencil_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_solid_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_solid_cover_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_gradient_cover_pipeline(fx_pipeline_set *ps, struct fx_context *ctx);
bool fx_make_images(fx_surface_vk *s);
bool fx_make_frames(fx_surface_vk *s);

#endif /* FX_VK_INTERNAL_H */
