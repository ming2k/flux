#ifndef FLUX_VK_MEMORY_H
#define FLUX_VK_MEMORY_H

#include <vulkan/vulkan.h>
#include <stddef.h>

VK_DEFINE_HANDLE(VmaAllocation)

typedef struct flux_context flux_context;

typedef struct flux_vbuf_chunk {
    VkBuffer        buffer;
    VmaAllocation   alloc;
    void           *map;
    size_t          size;
    struct flux_vbuf_chunk *next;
} flux_vbuf_chunk;

typedef struct {
    flux_context    *ctx;
    flux_vbuf_chunk *head;
    size_t         cursor;
    size_t         next_size;
} flux_vbuf_pool;

void flux_vbuf_pool_init(flux_vbuf_pool *pool, flux_context *ctx);
void flux_vbuf_pool_destroy(flux_vbuf_pool *pool);
void flux_vbuf_pool_reset(flux_vbuf_pool *pool);

/*
 * Allocates 'size' bytes from the pool.
 * Returns the mapped pointer, and fills 'out_buffer' and 'out_offset'.
 * Returns nullptr on failure.
 */
[[nodiscard]] void *flux_vbuf_pool_alloc(flux_vbuf_pool *pool, size_t size,
                         VkBuffer *out_buffer, VkDeviceSize *out_offset);

#endif /* FLUX_VK_MEMORY_H */
