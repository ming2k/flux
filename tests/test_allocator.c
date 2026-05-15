#include <flux/flux.h>
#include "test_helpers.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    atomic_size_t bytes_allocated;
    atomic_int    allocs;
    atomic_int    frees;
} alloc_stats;

static void *track_alloc(size_t bytes, void *user)
{
    alloc_stats *s = user;
    void *p = malloc(bytes);
    if (p) {
        atomic_fetch_add(&s->bytes_allocated, bytes);
        atomic_fetch_add(&s->allocs, 1);
    }
    return p;
}

static void *track_realloc(void *ptr, size_t old_bytes, size_t new_bytes, void *user)
{
    alloc_stats *s = user;
    void *np = realloc(ptr, new_bytes);
    if (!ptr) atomic_fetch_add(&s->allocs, 1);
    atomic_fetch_add(&s->bytes_allocated, new_bytes - old_bytes);
    return np;
}

static void track_free(void *ptr, void *user)
{
    alloc_stats *s = user;
    if (ptr) atomic_fetch_add(&s->frees, 1);
    free(ptr);
}

int main(void)
{
    alloc_stats stats = { 0 };
    flux_context_desc desc = {
        .size = sizeof(desc),
        .allocator = {
            .alloc   = track_alloc,
            .realloc = track_realloc,
            .free    = track_free,
            .user    = &stats,
        },
    };

    flux_context *ctx = NULL;
    CHECK(flux_context_create(&desc, &ctx) == FLUX_OK);
    CHECK(atomic_load(&stats.allocs) > 0);   /* context body. */

    /* Create + destroy enough things to exercise the allocator. */
    for (int i = 0; i < 8; ++i) {
        flux_paint *p = NULL;
        CHECK(flux_paint_create(ctx, &p) == FLUX_OK);
        flux_paint_release(p);

        flux_path *pth = NULL;
        CHECK(flux_path_create(ctx, &pth) == FLUX_OK);
        flux_path_add_circle(pth, 50, 50, 25);
        flux_path_release(pth);
    }

    flux_context_release(ctx);

    /* Every alloc had a matching free. */
    CHECK(atomic_load(&stats.allocs) == atomic_load(&stats.frees));
    printf("allocator OK (%d allocs, %zu bytes peak)\n",
           atomic_load(&stats.allocs),
           (size_t)atomic_load(&stats.bytes_allocated));
    return 0;
}
