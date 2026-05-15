#include "arena.h"
#include <stdlib.h>

void flux_arena_init(flux_arena *arena, size_t block_size)
{
    arena->head = nullptr;
    arena->block_size = block_size > 0 ? block_size : 65536;
}

void flux_arena_destroy(flux_arena *arena)
{
    flux_arena_block *curr = arena->head;
    while (curr) {
        flux_arena_block *next = curr->next;
        free(curr);
        curr = next;
    }
    arena->head = nullptr;
}

void *flux_arena_alloc(flux_arena *arena, size_t size)
{
    size = (size + 7) & ~7;

    if (arena->head && (arena->head->used + size <= arena->head->size)) {
        void *ptr = arena->head->data + arena->head->used;
        arena->head->used += size;
        return ptr;
    }

    size_t alloc_size = size > arena->block_size ? size : arena->block_size;
    flux_arena_block *block = malloc(sizeof(flux_arena_block) + alloc_size);
    if (!block) return nullptr;

    block->size = alloc_size;
    block->used = size;
    block->next = arena->head;
    arena->head = block;

    return block->data;
}

void flux_arena_reset(flux_arena *arena)
{
    flux_arena_block *curr = arena->head;
    if (!curr) return;

    while (curr->next) {
        flux_arena_block *next = curr->next->next;
        free(curr->next);
        curr->next = next;
    }

    arena->head->used = 0;
}
