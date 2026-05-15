#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    flux_glyph_run *run = NULL;
    CHECK(flux_glyph_run_create(ctx, 4, &run) == FLUX_OK);
    CHECK(flux_glyph_run_count(run) == 0);

    /* Append past the reserve forces grow. */
    for (uint32_t i = 0; i < 10; ++i)
        CHECK(flux_glyph_run_append(run, i, (float)i, (float)(i * 2)) == FLUX_OK);
    CHECK(flux_glyph_run_count(run) == 10);

    const flux_glyph *data = flux_glyph_run_data(run);
    CHECK(data != NULL);
    CHECK(data[3].glyph_id == 3);
    CHECK(approx_eq(data[3].x, 3.0f));
    CHECK(approx_eq(data[3].y, 6.0f));

    flux_glyph_run_clear(run);
    CHECK(flux_glyph_run_count(run) == 0);
    CHECK(flux_glyph_run_data(run)  == NULL);

    flux_glyph_run_release(run);
    flux_context_release(ctx);
    printf("glyph_run OK\n");
    return 0;
}
