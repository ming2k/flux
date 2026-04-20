/* Internal declarations shared across vgfx translation units. */
#ifndef VGFX_INTERNAL_H
#define VGFX_INTERNAL_H

#include "vgfx/vgfx.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>

#include "vk/memory.h"

#define VG_MAX_FRAMES_IN_FLIGHT 2
#define VG_MAX_SWAPCHAIN_IMAGES 8

/* ---------- logging ---------- */

void vg_log(const vg_context *ctx, vg_log_level lvl,
            const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#define VG_LOGE(ctx, ...) vg_log((ctx), VG_LOG_ERROR, __VA_ARGS__)
#define VG_LOGW(ctx, ...) vg_log((ctx), VG_LOG_WARN,  __VA_ARGS__)
#define VG_LOGI(ctx, ...) vg_log((ctx), VG_LOG_INFO,  __VA_ARGS__)
#define VG_LOGD(ctx, ...) vg_log((ctx), VG_LOG_DEBUG, __VA_ARGS__)

#define VG_CHECK_VK(ctx, expr) \
    do { \
        VkResult _r = (expr); \
        if (_r != VK_SUCCESS) { \
            VG_LOGE((ctx), "%s:%d: %s => VkResult %d", \
                    __FILE__, __LINE__, #expr, (int)_r); \
        } \
    } while (0)

/* ---------- glyph atlas ---------- */

typedef struct {
    uint32_t glyph_id;
    void    *font_id;   /* vg_font pointer */
    float    u0, v0, u1, v1;
    int      w, h;
    int      bearing_x, bearing_y;
    int      advance;
} vg_atlas_entry;

struct vg_atlas {
    struct vg_image *image;
    vg_atlas_entry  *entries;
    size_t           entry_count;
    size_t           entry_cap;

    int              shelf_y;
    int              shelf_h;
    int              shelf_x;
};

/* ---------- vg_context ---------- */

struct vg_context {
    vg_log_fn log;
    void     *log_user;

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
    FT_Library    ft_lib;

    struct vg_atlas atlas;
};

/* Picks a physical device and creates the logical device. Graphics
 * family is chosen to also support presentation to `probe_surface`.
 * `probe_surface` may be VK_NULL_HANDLE; in that case any graphics
 * queue suffices (useful for offscreen contexts; phase-0 always has a
 * wayland surface, but this keeps the seam clean). */
bool vg_instance_create(vg_context *ctx, const char *app_name,
                        bool want_validation,
                        const char *const *exts_wanted,
                        uint32_t exts_wanted_n);
bool vg_device_init(vg_context *ctx, VkSurfaceKHR probe_surface);
void vg_device_shutdown(vg_context *ctx);
void vg_instance_destroy(vg_context *ctx);

/* ---------- vg_font ---------- */

struct vg_glyph_run {
    vg_glyph *glyphs;
    size_t    count;
    size_t    cap;
};

struct vg_font {
    vg_context *ctx;
    char   *family;
    char   *source_name;
    float   size;
    int32_t weight;
    bool    italic;

    FT_Face      ft_face;
    hb_font_t   *hb_font;
};

/* ---------- vg_image ---------- */

struct vg_image {
    vg_context    *ctx;
    vg_image_desc  desc;
    void          *data;
    size_t         data_size;

    VkImage        vk_image;
    VkImageView    vk_view;
    VkDeviceMemory vk_mem;
};

/* ---------- vg_surface ---------- */

typedef struct {
    VkImage       image;
    VkImageView   view;
    VkFramebuffer framebuffer;
} vg_sc_image;

typedef struct {
    float surface_size[2];
    float pad[2];
    float color[4];
} vg_solid_color_pc;

typedef struct {
    float surface_size[2];
    float pad[2];
    float color[4];
} vg_text_pc;

typedef struct {
    float surface_size[2];
    float pad[2];
} vg_image_pc;

typedef struct {
    float pos[2];
    float uv[2];
} vg_image_vertex;

typedef struct {
    float pos[2];
} vg_solid_vertex;

typedef struct {
    VkSemaphore     image_available;
    VkSemaphore     render_finished;
    VkFence         in_flight;
    VkCommandBuffer cmd;
    vg_vbuf_pool    vbuf;
    VkDescriptorPool desc_pool;
} vg_frame;

typedef enum {
    VG_OP_FILL_PATH = 0,
    VG_OP_STROKE_PATH = 1,
    VG_OP_DRAW_IMAGE = 2,
    VG_OP_DRAW_GLYPHS = 3,
} vg_op_kind;

typedef struct {
    const vg_path *path;
    vg_paint       paint;
    bool           owns_path;
} vg_fill_path_op;

typedef struct {
    const vg_path *path;
    vg_paint       paint;
    bool           owns_path;
} vg_stroke_path_op;

typedef struct {
    const vg_image *image;
    vg_rect         src;
    vg_rect         dst;
} vg_draw_image_op;

typedef struct {
    const vg_font      *font;
    const vg_glyph_run *run;
    float               x;
    float               y;
    vg_paint            paint;
} vg_draw_glyphs_op;

typedef struct {
    vg_op_kind kind;
    union {
        vg_fill_path_op    fill_path;
        vg_stroke_path_op  stroke_path;
        vg_draw_image_op   draw_image;
        vg_draw_glyphs_op  draw_glyphs;
    } u;
} vg_op;

struct vg_surface {
    vg_context *ctx;

    VkSurfaceKHR      vk_surface;
    VkSwapchainKHR    swapchain;

    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR   present_mode;
    VkExtent2D         extent;

    VkRenderPass      render_pass;
    VkPipelineLayout  solid_rect_layout;
    VkPipeline        solid_rect_pipeline;

    VkDescriptorSetLayout image_dsl;
    VkPipelineLayout  image_layout;
    VkPipeline        image_pipeline;

    VkPipelineLayout  text_layout;
    VkPipeline        text_pipeline;

    VkSampler         sampler;


    vg_sc_image       images[VG_MAX_SWAPCHAIN_IMAGES];
    uint32_t          image_count;

    vg_frame          frames[VG_MAX_FRAMES_IN_FLIGHT];
    uint32_t          frame_index;      /* cycles 0..MAX_FRAMES-1 */
    uint32_t          acquired_image;   /* set by acquire, read by present */

    bool              needs_recreate;
    int32_t           requested_w, requested_h;
    bool              reported_unimplemented_ops;

    vg_color_space    color_space;

    /* Canvas recording state: commands are appended CPU-side. */
    struct vg_canvas {
        vg_surface *owner;
        vg_color    clear_color;
        bool        has_clear;
        vg_op      *ops;
        size_t      op_count;
        size_t      op_cap;

        vg_matrix  *state_stack;
        size_t      state_count;
        size_t      state_cap;
        vg_matrix   current_matrix;
    } canvas;
};

bool vg_swapchain_build(vg_surface *s);
void vg_swapchain_destroy(vg_surface *s);
/* Waits on all in-flight frame fences and destroys per-frame objects. */
void vg_surface_wait_idle(vg_surface *s);
void vg_canvas_reset(vg_canvas *c);
void vg_canvas_dispose(vg_canvas *c);
bool vg_path_is_axis_aligned_rect(const vg_path *path, vg_rect *out_rect);
bool vg_path_get_line_loop(const vg_path *path,
                           const vg_point **out_points,
                           size_t *out_count);
bool vg_path_flatten_polyline(const vg_path *path, float tolerance,
                              vg_point **out_points, size_t *out_count,
                              bool *out_closed);
bool vg_path_flatten_line_loop(const vg_path *path, float tolerance,
                               vg_point **out_points, size_t *out_count);
bool vg_tessellate_simple_polygon(const vg_point *points, size_t count,
                                  vg_point **out_tris, size_t *out_count);
bool vg_stroke_polyline(const vg_point *points, size_t count, bool closed,
                        const vg_paint *paint, vg_point **out_tris, size_t *out_count);

bool vg_atlas_ensure_glyph(vg_context *ctx, vg_font *font, uint32_t glyph_id, vg_atlas_entry *out_entry);

/* ---------- matrix & path transform ---------- */

static inline void vg_matrix_identity(vg_matrix *m)
{
    m->m[0] = 1.0f; m->m[1] = 0.0f;
    m->m[2] = 0.0f; m->m[3] = 1.0f;
    m->m[4] = 0.0f; m->m[5] = 0.0f;
}

static inline bool vg_matrix_is_identity(const vg_matrix *m)
{
    return m->m[0] == 1.0f && m->m[1] == 0.0f &&
           m->m[2] == 0.0f && m->m[3] == 1.0f &&
           m->m[4] == 0.0f && m->m[5] == 0.0f;
}

void vg_matrix_multiply(vg_matrix *out, const vg_matrix *a, const vg_matrix *b);
void vg_matrix_transform_point(const vg_matrix *m, float *x, float *y);

/* Returns a newly allocated path with coordinates transformed by m. */
vg_path *vg_path_transform(const vg_path *src, const vg_matrix *m);

#endif /* VGFX_INTERNAL_H */
