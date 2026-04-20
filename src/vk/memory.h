#ifndef VGFX_VK_MEMORY_H
#define VGFX_VK_MEMORY_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct vg_context vg_context;

typedef struct vg_vbuf_chunk {
    VkBuffer        buffer;
    VkDeviceMemory  memory;
    void           *map;
    size_t          size;
    struct vg_vbuf_chunk *next;
} vg_vbuf_chunk;

typedef struct {
    vg_context    *ctx;
    vg_vbuf_chunk *head;
    size_t         cursor;
    size_t         next_size;
} vg_vbuf_pool;

void vg_vbuf_pool_init(vg_vbuf_pool *pool, vg_context *ctx);
void vg_vbuf_pool_destroy(vg_vbuf_pool *pool);
void vg_vbuf_pool_reset(vg_vbuf_pool *pool);

/*
 * Allocates 'size' bytes from the pool.
 * Returns the mapped pointer, and fills 'out_buffer' and 'out_offset'.
 * Returns NULL on failure.
 */
void *vg_vbuf_pool_alloc(vg_vbuf_pool *pool, size_t size,
                         VkBuffer *out_buffer, VkDeviceSize *out_offset);

#endif /* VGFX_VK_MEMORY_H */
