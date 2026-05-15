/*
 * Golden-image regression test.
 *
 * Renders a set of reference scenes with the software backend and
 * compares the output pixel-by-pixel against checked-in PPM files
 * in tests/golden/.
 *
 * To regenerate references after an intentional visual change:
 *   FLUX_GOLDEN_UPDATE=1 meson test -C build --suite golden
 */
#include <flux/flux.h>
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 128
#define H 128

static const char *golden_dir = NULL;
static int g_fail = 0;

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static uint8_t *render_solid_rect(flux_context *ctx)
{
    flux_surface *s = NULL;
    if (flux_surface_create_offscreen(ctx, W, H, FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) != FLUX_OK)
        return NULL;
    flux_canvas *c = flux_surface_acquire(s);
    (void)flux_canvas_clear(c, flux_color_rgba(0, 0, 0, 255));
    (void)flux_canvas_fill_rect(c, &(flux_rect){ 32, 32, 64, 64 }, flux_color_rgba(255, 128, 64, 255));
    if (flux_surface_present(s) != FLUX_OK) { flux_surface_release(s); return NULL; }

    uint8_t *pixels = xmalloc((size_t)W * H * 4);
    if (flux_surface_read_pixels(s, pixels, 0) != FLUX_OK) { free(pixels); flux_surface_release(s); return NULL; }
    flux_surface_release(s);
    return pixels;
}

static uint8_t *render_gradient(flux_context *ctx)
{
    flux_surface *s = NULL;
    if (flux_surface_create_offscreen(ctx, W, H, FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) != FLUX_OK)
        return NULL;
    flux_canvas *c = flux_surface_acquire(s);
    (void)flux_canvas_clear(c, flux_color_rgba(255, 255, 255, 255));

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
    if (flux_gradient_create_linear(ctx, &gdesc, &grad) != FLUX_OK) { flux_surface_release(s); return NULL; }

    flux_paint *paint = NULL;
    if (flux_paint_create(ctx, &paint) != FLUX_OK) { flux_gradient_release(grad); flux_surface_release(s); return NULL; }
    flux_paint_set_gradient(paint, grad);
    (void)flux_canvas_fill_rect(c, &(flux_rect){ 16, 16, 96, 96 }, 0u);
    if (flux_surface_present(s) != FLUX_OK) { flux_paint_release(paint); flux_gradient_release(grad); flux_surface_release(s); return NULL; }

    uint8_t *pixels = xmalloc((size_t)W * H * 4);
    if (flux_surface_read_pixels(s, pixels, 0) != FLUX_OK) { free(pixels); flux_paint_release(paint); flux_gradient_release(grad); flux_surface_release(s); return NULL; }

    flux_paint_release(paint);
    flux_gradient_release(grad);
    flux_surface_release(s);
    return pixels;
}

static uint8_t *render_clip(flux_context *ctx)
{
    flux_surface *s = NULL;
    if (flux_surface_create_offscreen(ctx, W, H, FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) != FLUX_OK)
        return NULL;
    flux_canvas *c = flux_surface_acquire(s);
    (void)flux_canvas_clear(c, flux_color_rgba(50, 50, 50, 255));
    (void)flux_canvas_clip_rect(c, &(flux_rect){ 32, 32, 64, 64 });
    (void)flux_canvas_fill_rect(c, &(flux_rect){ 0, 0, 128, 128 }, flux_color_rgba(0, 255, 0, 255));
    if (flux_surface_present(s) != FLUX_OK) { flux_surface_release(s); return NULL; }

    uint8_t *pixels = xmalloc((size_t)W * H * 4);
    if (flux_surface_read_pixels(s, pixels, 0) != FLUX_OK) { free(pixels); flux_surface_release(s); return NULL; }
    flux_surface_release(s);
    return pixels;
}

static uint8_t *render_glyph(flux_context *ctx)
{
    uint8_t bitmap[4*4] = {0, 255, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255};
    if (flux_glyph_upload(ctx, 65, bitmap, 4, 4, 0, 4, 4) != FLUX_OK) return NULL;

    flux_surface *s = NULL;
    if (flux_surface_create_offscreen(ctx, W, H, FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) != FLUX_OK)
        return NULL;
    flux_canvas *c = flux_surface_acquire(s);
    (void)flux_canvas_clear(c, flux_color_rgba(0, 0, 0, 255));

    flux_paint *paint = NULL;
    if (flux_paint_create(ctx, &paint) != FLUX_OK) { flux_surface_release(s); return NULL; }
    (void)flux_paint_set_color(paint, flux_color_rgba(255, 255, 255, 255));

    flux_glyph_run *run = NULL;
    if (flux_glyph_run_create(ctx, 4, &run) != FLUX_OK) { flux_paint_release(paint); flux_surface_release(s); return NULL; }
    if (flux_glyph_run_append(run, 65, 8.0f, 8.0f) != FLUX_OK) { flux_glyph_run_release(run); flux_paint_release(paint); flux_surface_release(s); return NULL; }
    if (flux_canvas_draw_glyph_run(c, run, 0, 0, paint) != FLUX_OK) { flux_glyph_run_release(run); flux_paint_release(paint); flux_surface_release(s); return NULL; }
    if (flux_surface_present(s) != FLUX_OK) { flux_glyph_run_release(run); flux_paint_release(paint); flux_surface_release(s); return NULL; }
    flux_glyph_run_release(run);
    flux_paint_release(paint);

    uint8_t *pixels = xmalloc((size_t)W * H * 4);
    if (flux_surface_read_pixels(s, pixels, 0) != FLUX_OK) { free(pixels); flux_surface_release(s); return NULL; }
    flux_surface_release(s);
    return pixels;
}

static uint8_t *render_transform(flux_context *ctx)
{
    flux_surface *s = NULL;
    if (flux_surface_create_offscreen(ctx, W, H, FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) != FLUX_OK)
        return NULL;
    flux_canvas *c = flux_surface_acquire(s);
    (void)flux_canvas_clear(c, flux_color_rgba(0, 0, 0, 255));
    (void)flux_canvas_translate(c, 64.0f, 64.0f);
    (void)flux_canvas_rotate(c, 45.0f);
    (void)flux_canvas_fill_rect(c, &(flux_rect){ -20, -20, 40, 40 }, flux_color_rgba(255, 0, 0, 255));
    if (flux_surface_present(s) != FLUX_OK) { flux_surface_release(s); return NULL; }

    uint8_t *pixels = xmalloc((size_t)W * H * 4);
    if (flux_surface_read_pixels(s, pixels, 0) != FLUX_OK) { free(pixels); flux_surface_release(s); return NULL; }
    flux_surface_release(s);
    return pixels;
}

static uint8_t *render_path_aa(flux_context *ctx)
{
    flux_surface *s = NULL;
    if (flux_surface_create_offscreen(ctx, W, H, FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) != FLUX_OK)
        return NULL;
    flux_canvas *c = flux_surface_acquire(s);
    (void)flux_canvas_clear(c, flux_color_rgba(0, 0, 0, 255));

    flux_path *p = NULL;
    if (flux_path_create(ctx, &p) != FLUX_OK) { flux_surface_release(s); return NULL; }
    flux_path_move_to(p, 20.0f, 20.0f);
    flux_path_line_to(p, 108.0f, 40.0f);
    flux_path_line_to(p, 88.0f, 108.0f);
    flux_path_line_to(p, 40.0f, 88.0f);
    flux_path_close(p);

    flux_paint *paint = NULL;
    if (flux_paint_create(ctx, &paint) != FLUX_OK) { flux_path_release(p); flux_surface_release(s); return NULL; }
    (void)flux_paint_set_color(paint, flux_color_rgba(0, 255, 0, 255));
    (void)flux_canvas_fill_path(c, p, paint);
    (void)flux_surface_present(s);

    flux_paint_release(paint);
    flux_path_release(p);

    uint8_t *pixels = xmalloc((size_t)W * H * 4);
    if (flux_surface_read_pixels(s, pixels, 0) != FLUX_OK) { free(pixels); flux_surface_release(s); return NULL; }
    flux_surface_release(s);
    return pixels;
}

static void write_ppm(const char *path, const uint8_t *pixels)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "failed to open %s\n", path); g_fail++; return; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            fwrite(&pixels[(y * W + x) * 4], 3, 1, f);
    fclose(f);
}

static uint8_t *read_ppm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int pw = 0, ph = 0, maxval = 0;
    if (fscanf(f, "P6 %d %d %d", &pw, &ph, &maxval) != 3 || pw != W || ph != H || maxval != 255) {
        fclose(f);
        return NULL;
    }
    fgetc(f); /* consume newline after maxval */
    uint8_t *rgb = malloc((size_t)W * H * 3);
    if (!rgb || fread(rgb, (size_t)W * H * 3, 1, f) != 1) {
        free(rgb);
        fclose(f);
        return NULL;
    }
    fclose(f);

    /* Convert RGB PPM to RGBA buffer (alpha = 255) */
    uint8_t *rgba = xmalloc((size_t)W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
    }
    free(rgb);
    return rgba;
}

static bool compare_pixels(const uint8_t *a, const uint8_t *b, const char *name, int *out_diff_x, int *out_diff_y)
{
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = (y * W + x) * 4;
            int dr = abs((int)a[i + 0] - (int)b[i + 0]);
            int dg = abs((int)a[i + 1] - (int)b[i + 1]);
            int db = abs((int)a[i + 2] - (int)b[i + 2]);
            if (dr > 1 || dg > 1 || db > 1) {
                *out_diff_x = x;
                *out_diff_y = y;
                fprintf(stderr, "golden mismatch in '%s' at (%d,%d): "
                        "expected (%d,%d,%d) got (%d,%d,%d)\n",
                        name, x, y,
                        b[i + 0], b[i + 1], b[i + 2],
                        a[i + 0], a[i + 1], a[i + 2]);
                return false;
            }
        }
    }
    return true;
}

typedef uint8_t *(*render_fn)(flux_context *);

typedef struct {
    const char *name;
    render_fn   render;
} scene;

int main(void)
{
    golden_dir = getenv("FLUX_GOLDEN_DIR");
    if (!golden_dir) golden_dir = "tests/golden";
    bool update = getenv("FLUX_GOLDEN_UPDATE") != NULL;

    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    scene scenes[] = {
        { "solid_rect", render_solid_rect },
        { "gradient",   render_gradient   },
        { "clip",       render_clip       },
        { "glyph",      render_glyph      },
        { "transform",  render_transform  },
        { "path_aa",    render_path_aa    },
    };
    size_t n = sizeof(scenes) / sizeof(scenes[0]);

    int failures = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t *rendered = scenes[i].render(ctx);
        if (!rendered) {
            fprintf(stderr, "failed to render '%s'\n", scenes[i].name);
            failures++;
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.ppm", golden_dir, scenes[i].name);

        if (update) {
            write_ppm(path, rendered);
            printf("updated %s\n", scenes[i].name);
        } else {
            uint8_t *ref = read_ppm(path);
            if (!ref) {
                fprintf(stderr, "missing reference: %s\n", path);
                failures++;
            } else {
                int dx = -1, dy = -1;
                if (!compare_pixels(rendered, ref, scenes[i].name, &dx, &dy))
                    failures++;
                else
                    printf("%s OK\n", scenes[i].name);
                free(ref);
            }
        }
        free(rendered);
    }

    flux_context_release(ctx);
    return failures > 0 ? 1 : 0;
}
