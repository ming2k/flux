/*
 * Render performance benchmark.
 *
 * Renders a stress-test scene for a fixed number of frames and
 * reports average frame time and throughput.  Used by Meson
 * benchmark() to detect performance regressions.
 */
#include <flux/flux.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 512
#define H 512
#define FRAME_COUNT 120

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int die(const char *what, flux_result r)
{
    fprintf(stderr, "%s: %s\n", what, flux_result_string(r));
    return 1;
}

int main(void)
{
    flux_context *ctx = NULL;
    flux_result r = flux_context_create(NULL, &ctx);
    if (r != FLUX_OK) return die("context_create", r);

    flux_surface *s = NULL;
    r = flux_surface_create_offscreen(ctx, W, H, FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s);
    if (r != FLUX_OK) { flux_context_release(ctx); return die("surface_create", r); }

    /* Pre-upload a glyph atlas tile */
    uint8_t glyph_bm[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) glyph_bm[i] = (uint8_t)(i * 4);
    if (flux_glyph_upload(ctx, 1, glyph_bm, 8, 8, 0, 8, 8) != FLUX_OK) {
        fprintf(stderr, "glyph upload failed\n");
    }

    /* Pre-build paths / paints */
    flux_path *path = NULL;
    if (flux_path_create(ctx, &path) != FLUX_OK) {
        flux_surface_release(s); flux_context_release(ctx); return die("path_create", r);
    }
    (void)flux_path_add_round_rect(path, &(flux_rect){ 50, 50, W - 100, H - 100 }, 20.0f);

    flux_color colors[2] = { 0xFFFF0000u, 0xFF0000FFu };
    float      stops [2] = { 0.0f, 1.0f };
    flux_linear_gradient_desc gdesc = {
        .size       = sizeof(gdesc),
        .start      = { 0.0f, 0.0f },
        .end        = { (float)W, (float)H },
        .colors     = colors,
        .stops      = stops,
        .stop_count = 2,
    };
    flux_gradient *grad = NULL;
    if (flux_gradient_create_linear(ctx, &gdesc, &grad) != FLUX_OK) {
        flux_path_release(path); flux_surface_release(s); flux_context_release(ctx);
        return die("gradient_create", r);
    }

    flux_paint *paint = NULL;
    if (flux_paint_create(ctx, &paint) != FLUX_OK) {
        flux_gradient_release(grad); flux_path_release(path);
        flux_surface_release(s); flux_context_release(ctx);
        return die("paint_create", r);
    }
    (void)flux_paint_set_gradient(paint, grad);

    flux_glyph_run *run = NULL;
    if (flux_glyph_run_create(ctx, 64, &run) != FLUX_OK) {
        flux_paint_release(paint); flux_gradient_release(grad); flux_path_release(path);
        flux_surface_release(s); flux_context_release(ctx);
        return die("glyph_run_create", r);
    }
    for (int i = 0; i < 64; ++i)
        (void)flux_glyph_run_append(run, 1, (float)(i % 8) * 60.0f, (float)(i / 8) * 60.0f);

    /* Benchmark loop */
    double start = now_seconds();
    for (int f = 0; f < FRAME_COUNT; ++f) {
        flux_canvas *c = flux_surface_acquire(s);
        (void)flux_canvas_clear(c, flux_color_rgba(240, 240, 240, 255));

        /* Stress: many primitives */
        for (int i = 0; i < 20; ++i) {
            flux_rect rc = { (float)(i * 20), (float)(i * 20), 80.0f, 80.0f };
            (void)flux_canvas_fill_rect(c, &rc, flux_color_rgba((uint8_t)(i * 12), 128, 255, 200));
        }
        (void)flux_canvas_fill_path(c, path, paint);
        (void)flux_canvas_draw_glyph_run(c, run, 10.0f, 10.0f, paint);
        (void)flux_surface_present(s);
    }
    double elapsed = now_seconds() - start;

    double ms_per_frame = (elapsed / FRAME_COUNT) * 1000.0;
    double fps = FRAME_COUNT / elapsed;
    double mpix_per_sec = ((double)W * H * FRAME_COUNT / elapsed) / 1e6;

    printf("frames:      %d\n", FRAME_COUNT);
    printf("elapsed_ms:  %.2f\n", elapsed * 1000.0);
    printf("ms_per_frame:%.3f\n", ms_per_frame);
    printf("fps:         %.1f\n", fps);
    printf("mpix/sec:    %.2f\n", mpix_per_sec);

    flux_glyph_run_release(run);
    flux_paint_release(paint);
    flux_gradient_release(grad);
    flux_path_release(path);
    flux_surface_release(s);
    flux_context_release(ctx);
    return 0;
}
