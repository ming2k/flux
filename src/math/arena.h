/*
 * Bump / arena allocator for per-frame temporary geometry.
 * Allocation is fast (just bump a pointer); reset is O(1)
 * per frame. The first block is retained across resets.
 */
#ifndef FX_MATH_ARENA_H
#define FX_MATH_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct fx_arena_block {
    struct fx_arena_block *next;
    size_t size;
    size_t used;
    uint8_t data[];
} fx_arena_block;

typedef struct {
    fx_arena_block *head;
    size_t block_size;
} fx_arena;

void  fx_arena_init(fx_arena *arena, size_t block_size);
void  fx_arena_destroy(fx_arena *arena);
void *fx_arena_alloc(fx_arena *arena, size_t size);
void  fx_arena_reset(fx_arena *arena);

#endif /* FX_MATH_ARENA_H */
