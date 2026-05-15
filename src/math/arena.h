/*
 * Bump / arena allocator for per-frame temporary geometry.
 * Allocation is fast (just bump a pointer); reset is O(1)
 * per frame. The first block is retained across resets.
 */
#ifndef FLUX_MATH_ARENA_H
#define FLUX_MATH_ARENA_H

#include "visibility.h"
#include <stddef.h>
#include <stdint.h>

typedef struct flux_arena_block {
    struct flux_arena_block *next;
    size_t size;
    size_t used;
    uint8_t data[];
} flux_arena_block;

typedef struct {
    flux_arena_block *head;
    size_t block_size;
} flux_arena;

FLUX_INTERNAL void  flux_arena_init(flux_arena *arena, size_t block_size);
FLUX_INTERNAL void  flux_arena_destroy(flux_arena *arena);
FLUX_INTERNAL void *flux_arena_alloc(flux_arena *arena, size_t size);
FLUX_INTERNAL void  flux_arena_reset(flux_arena *arena);

#endif /* FLUX_MATH_ARENA_H */
