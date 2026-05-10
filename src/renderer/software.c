/*
 * CPU software renderer backend.
 *
 * Renders directly to a pixel buffer in system memory. Implements
 * triangle rasterization (scanline with edge walking), alpha
 * blending, bilinear texture sampling, gradient evaluation, and a
 * stencil buffer — enough to prove the renderer vtable abstraction.
 */
#include "renderer/renderer.h"
#include "internal.h"
#include "math/arena.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- internal pixel buffer ---- */

typedef struct {
    uint8_t *pixels;     /* RGBA8888 row-major */
    uint8_t *stencil;    /* 1 byte per pixel */
    uint32_t w, h;
    size_t   stride;     /* bytes per row */
} sw_fb;

/* ---- sw implementation types ---- */

typedef struct sw_buffer {
    fx_solid_vertex  *solid;
    fx_image_vertex  *image;
    size_t            count;
} sw_buffer;

typedef struct sw_texture {
    uint8_t       *pixels;
    uint32_t       w, h;
    size_t         stride;
    fx_pixel_format fmt;
    struct sw_texture *next; /* linked list for cleanup */
} sw_texture;

typedef struct sw_batch {
    fx_color  color;
    uint32_t  first;
    uint32_t  count;
    bool      active;
} sw_batch;

typedef struct sw_renderer {
    const fx_renderer_vtbl *vtbl;  /* must be first — vt() reads this */

    sw_fb       fb;
    sw_fb       target;    /* pointer to fb or external buffer */
    bool        offscreen;

    sw_buffer  *buffers;
    uint32_t    buf_count;
    uint32_t    buf_cap;

    sw_texture *textures;
    sw_texture *atlas;     /* glyph atlas texture */

    sw_batch    batch;

    /* clip state */
    int32_t     scissor_x, scissor_y;
    uint32_t    scissor_w, scissor_h;
    uint32_t    stencil_ref;
    int         stencil_fill_rule;

    fx_arena    arena;
} sw_renderer;

/* vtable forward decls */
static void  sw_destroy(fx_renderer *r);
static void  sw_surface_extent(fx_renderer *r, uint32_t *w, uint32_t *h);
static void  sw_begin_frame(fx_renderer *r);
static void  sw_begin_pass(fx_renderer *r, fx_color clear);
static void  sw_end_pass(fx_renderer *r);
static void  sw_submit(fx_renderer *r);
static bool  sw_read_pixels(fx_renderer *r, void *data, size_t stride);
static fx_solid_vertex *sw_alloc_solid(fx_renderer *r, size_t count, fx_r_buffer **buf, uint32_t *first);
static fx_image_vertex *sw_alloc_image(fx_renderer *r, size_t count, fx_r_buffer **buf, uint32_t *first);
static void  sw_draw_solid(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color);
static void  sw_flush_solid(fx_renderer *r);
static void  sw_draw_image(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_r_texture *tex);
static void  sw_draw_text(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color);
static void  sw_draw_gradient(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, const fx_gradient *grad);
static void  sw_scissor(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
static void  sw_stencil_clear(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
static void  sw_stencil_fill(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, int fill_rule);
static void  sw_blur(fx_renderer *r, float sigma);
static fx_r_texture *sw_surface_texture(fx_renderer *r);
static void  sw_stencil_ref(fx_renderer *r, uint32_t ref);
static void  sw_cover_solid(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color);
static void  sw_cover_gradient(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, const fx_gradient *grad);
static fx_r_texture *sw_texture_alloc(fx_renderer *r, uint32_t w, uint32_t h, fx_pixel_format fmt, const void *data, size_t stride);
static void  sw_texture_free(fx_renderer *r, fx_r_texture *tex);
static void  sw_texture_update(fx_renderer *r, fx_r_texture *tex, const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* ---- helpers ---- */

static sw_renderer *self(fx_renderer *r) { return (sw_renderer *)r; }

static inline uint8_t to_u8(float v) {
    int iv = (int)(v * 255.0f + 0.5f);
    return (uint8_t)(iv < 0 ? 0 : iv > 255 ? 255 : iv);
}

static void blend_pixel(uint8_t *dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
    if (sa == 0) return;
    if (sa == 255) {
        dst[0] = sb; dst[1] = sg; dst[2] = sr; dst[3] = 255;
        return;
    }
    uint32_t da = dst[3];
    uint32_t inv_a = 255 - sa;
    dst[0] = (uint8_t)(((uint32_t)sb * 255 + (uint32_t)dst[0] * inv_a) / 255);
    dst[1] = (uint8_t)(((uint32_t)sg * 255 + (uint32_t)dst[1] * inv_a) / 255);
    dst[2] = (uint8_t)(((uint32_t)sr * 255 + (uint32_t)dst[2] * inv_a) / 255);
    dst[3] = (uint8_t)(sa + (da * inv_a) / 255);
}

static inline int32_t min_i(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t max_i(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- triangle rasterization ---- */

static void raster_solid(sw_renderer *sw,
                         const fx_solid_vertex *v0,
                         const fx_solid_vertex *v1,
                         const fx_solid_vertex *v2,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                         bool stencil_mode, bool cover_solid, uint8_t cover_r, uint8_t cover_g, uint8_t cover_b, uint8_t cover_a)
{
    int32_t x0 = (int32_t)v0->pos[0], y0 = (int32_t)v0->pos[1];
    int32_t x1 = (int32_t)v1->pos[0], y1 = (int32_t)v1->pos[1];
    int32_t x2 = (int32_t)v2->pos[0], y2 = (int32_t)v2->pos[1];

    int32_t minx = max_i(sw->scissor_x, min_i(min_i(x0, x1), x2));
    int32_t maxx = min_i(sw->scissor_x + (int32_t)sw->scissor_w - 1,
                         max_i(max_i(x0, x1), x2));
    int32_t miny = max_i(sw->scissor_y, min_i(min_i(y0, y1), y2));
    int32_t maxy = min_i(sw->scissor_y + (int32_t)sw->scissor_h - 1,
                         max_i(max_i(y0, y1), y2));

    if (minx > maxx || miny > maxy) return;

    /* Edge functions: f_ab(p) = (p - a) × (b - a) */
    int32_t area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0) return;

    int32_t A01 = y0 - y1, B01 = x1 - x0;
    int32_t A12 = y1 - y2, B12 = x2 - x1;
    int32_t A20 = y2 - y0, B20 = x0 - x2;

    /* Top-left fill convention bias */
    int32_t bias0 = (A01 < 0 || (A01 == 0 && B01 < 0)) ? 0 : -1;
    int32_t bias1 = (A12 < 0 || (A12 == 0 && B12 < 0)) ? 0 : -1;
    int32_t bias2 = (A20 < 0 || (A20 == 0 && B20 < 0)) ? 0 : -1;

    /* Starting edge values at (minx, miny) */
    int32_t w0_row = A01 * minx + B01 * miny + (x1 * y0 - y1 * x0) + bias0;
    int32_t w1_row = A12 * minx + B12 * miny + (x2 * y1 - y2 * x1) + bias1;
    int32_t w2_row = A20 * minx + B20 * miny + (x0 * y2 - y0 * x2) + bias2;

    uint8_t *row = sw->target.pixels + (size_t)miny * sw->target.stride + (size_t)minx * 4;
    uint8_t *srow = sw->target.stencil + (size_t)miny * sw->target.w + minx;

    for (int32_t y = miny; y <= maxy; y++) {
        int32_t w0 = w0_row, w1 = w1_row, w2 = w2_row;
        uint8_t *p = row;
        uint8_t *sp = srow;

        for (int32_t x = minx; x <= maxx; x++) {
            if ((w0 | w1 | w2) >= 0) {
                if (stencil_mode) {
                    if (a >= 128) {
                        uint32_t sv = *sp + 1;
                        *sp = sv > 255 ? 255 : (uint8_t)sv;
                    }
                } else if (cover_solid) {
                    bool pass = (sw->stencil_fill_rule == 0) ? (*sp == sw->stencil_ref) : (*sp != 0);
                    if (pass)
                        blend_pixel(p, cover_r, cover_g, cover_b, cover_a);
                } else {
                    bool pass = (sw->stencil_fill_rule == 0) ? (*sp == sw->stencil_ref) : (*sp != 0);
                    if (pass)
                        blend_pixel(p, r, g, b, a);
                }
            }

            w0 += A01; w1 += A12; w2 += A20;
            p += 4; sp++;
        }

        w0_row += B01; w1_row += B12; w2_row += B20;
        row += sw->target.stride;
        srow += sw->target.w;
    }
}

/* ---- gradient evaluation ---- */

static void eval_gradient(const fx_gradient *grad, float t, uint8_t col[4])
{
    float cf[4] = {0};

    if (t <= grad->stops[0]) {
        cf[0] = grad->colors[0][0]; cf[1] = grad->colors[0][1];
        cf[2] = grad->colors[0][2]; cf[3] = grad->colors[0][3];
    } else if (t >= grad->stops[grad->stop_count - 1]) {
        uint32_t i = grad->stop_count - 1;
        cf[0] = grad->colors[i][0]; cf[1] = grad->colors[i][1];
        cf[2] = grad->colors[i][2]; cf[3] = grad->colors[i][3];
    } else {
        for (uint32_t i = 0; i < grad->stop_count - 1; i++) {
            if (t >= grad->stops[i] && t <= grad->stops[i + 1]) {
                float range = grad->stops[i + 1] - grad->stops[i];
                float f = range > 0 ? (t - grad->stops[i]) / range : 0;
                for (int j = 0; j < 4; j++)
                    cf[j] = grad->colors[i][j] * (1.0f - f) + grad->colors[i + 1][j] * f;
                break;
            }
        }
    }

    col[0] = to_u8(cf[0]); col[1] = to_u8(cf[1]);
    col[2] = to_u8(cf[2]); col[3] = to_u8(cf[3]);
}

static float grad_t_linear(const fx_gradient *grad, float x, float y)
{
    float dx = grad->end[0] - grad->start[0];
    float dy = grad->end[1] - grad->start[1];
    float denom = dx * dx + dy * dy;
    if (denom == 0.0f) return 0.0f;
    return ((x - grad->start[0]) * dx + (y - grad->start[1]) * dy) / denom;
}

static float grad_t_radial(const fx_gradient *grad, float x, float y)
{
    float dx = x - grad->start[0];
    float dy = y - grad->start[1];
    float dist = sqrtf(dx * dx + dy * dy);
    float r = grad->end[0];  /* radius stored in end[0] */
    if (r == 0.0f) return 0.0f;
    return dist / r;
}

static void raster_gradient(sw_renderer *sw,
                            const fx_solid_vertex *v0,
                            const fx_solid_vertex *v1,
                            const fx_solid_vertex *v2,
                            const fx_gradient *grad,
                            bool cover)
{
    int32_t x0 = (int32_t)v0->pos[0], y0 = (int32_t)v0->pos[1];
    int32_t x1 = (int32_t)v1->pos[0], y1 = (int32_t)v1->pos[1];
    int32_t x2 = (int32_t)v2->pos[0], y2 = (int32_t)v2->pos[1];

    int32_t minx = max_i(sw->scissor_x, min_i(min_i(x0, x1), x2));
    int32_t maxx = min_i(sw->scissor_x + (int32_t)sw->scissor_w - 1, max_i(max_i(x0, x1), x2));
    int32_t miny = max_i(sw->scissor_y, min_i(min_i(y0, y1), y2));
    int32_t maxy = min_i(sw->scissor_y + (int32_t)sw->scissor_h - 1, max_i(max_i(y0, y1), y2));

    if (minx > maxx || miny > maxy) return;

    int32_t A01 = y0 - y1, B01 = x1 - x0;
    int32_t A12 = y1 - y2, B12 = x2 - x1;
    int32_t A20 = y2 - y0, B20 = x0 - x2;
    int32_t bias0 = (A01 < 0 || (A01 == 0 && B01 < 0)) ? 0 : -1;
    int32_t bias1 = (A12 < 0 || (A12 == 0 && B12 < 0)) ? 0 : -1;
    int32_t bias2 = (A20 < 0 || (A20 == 0 && B20 < 0)) ? 0 : -1;

    int32_t w0_row = A01 * minx + B01 * miny + (x1 * y0 - y1 * x0) + bias0;
    int32_t w1_row = A12 * minx + B12 * miny + (x2 * y1 - y2 * x1) + bias1;
    int32_t w2_row = A20 * minx + B20 * miny + (x0 * y2 - y0 * x2) + bias2;

    float (*t_fn)(const fx_gradient *, float, float) =
        grad->mode == 0 ? grad_t_linear : grad_t_radial;

    uint8_t *row = sw->target.pixels + (size_t)miny * sw->target.stride + (size_t)minx * 4;
    uint8_t *srow = sw->target.stencil + (size_t)miny * sw->target.w + minx;

    for (int32_t y = miny; y <= maxy; y++) {
        int32_t w0 = w0_row, w1 = w1_row, w2 = w2_row;
        uint8_t *p = row;
        uint8_t *sp = srow;

        for (int32_t x = minx; x <= maxx; x++) {
            if ((w0 | w1 | w2) >= 0) {
                bool pass = !cover || (sw->stencil_fill_rule == 0 ? (*sp == sw->stencil_ref) : (*sp != 0));
                if (pass) {
                    float t = t_fn(grad, (float)x + 0.5f, (float)y + 0.5f);
                    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
                    uint8_t col[4];
                    eval_gradient(grad, t, col);
                    blend_pixel(p, col[0], col[1], col[2], col[3]);
                }
            }
            w0 += A01; w1 += A12; w2 += A20;
            p += 4; sp++;
        }
        w0_row += B01; w1_row += B12; w2_row += B20;
        row += sw->target.stride;
        srow += sw->target.w;
    }
}

/* ---- image rasterization ---- */

static void raster_image(sw_renderer *sw,
                         const fx_image_vertex *v0,
                         const fx_image_vertex *v1,
                         const fx_image_vertex *v2,
                         const sw_texture *tex,
                         uint8_t tint_r, uint8_t tint_g, uint8_t tint_b, uint8_t tint_a)
{
    float x0 = v0->pos[0], y0 = v0->pos[1], u0 = v0->uv[0], vv0 = v0->uv[1];
    float x1 = v1->pos[0], y1 = v1->pos[1], u1 = v1->uv[0], vv1 = v1->uv[1];
    float x2 = v2->pos[0], y2 = v2->pos[1], u2 = v2->uv[0], vv2 = v2->uv[1];

    int32_t minx = max_i(sw->scissor_x, (int32_t)floorf(min_i(min_i(x0, x1), x2)));
    int32_t maxx = min_i(sw->scissor_x + (int32_t)sw->scissor_w - 1, (int32_t)ceilf(max_i(max_i(x0, x1), x2)));
    int32_t miny = max_i(sw->scissor_y, (int32_t)floorf(min_i(min_i(y0, y1), y2)));
    int32_t maxy = min_i(sw->scissor_y + (int32_t)sw->scissor_h - 1, (int32_t)ceilf(max_i(max_i(y0, y1), y2)));

    if (minx > maxx || miny > maxy) return;

    float denom = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(denom) < 0.0001f) return;
    float inv_denom = 1.0f / denom;

    float tw = (float)tex->w, th = (float)tex->h;
    uint8_t *row = sw->target.pixels + (size_t)miny * sw->target.stride + (size_t)minx * 4;

    for (int32_t y = miny; y <= maxy; y++) {
        uint8_t *p = row;
        for (int32_t x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((x1 - x2) * (py - y2) - (y1 - y2) * (px - x2)) * inv_denom;
            float w1 = ((x2 - x0) * (py - y0) - (y2 - y0) * (px - x0)) * inv_denom;
            float w2 = 1.0f - w0 - w1;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                float su = u0 * w0 + u1 * w1 + u2 * w2;
                float sv = vv0 * w0 + vv1 * w1 + vv2 * w2;

                /* bilinear sample */
                float tu = su * tw - 0.5f;
                float tv = sv * th - 0.5f;
                int32_t iu = (int32_t)tu, iv = (int32_t)tv;
                float fu = tu - (float)iu, fv = tv - (float)iv;

                int32_t cu0 = clampi(iu, 0, (int32_t)tex->w - 1);
                int32_t cv0 = clampi(iv, 0, (int32_t)tex->h - 1);
                int32_t cu1 = clampi(iu + 1, 0, (int32_t)tex->w - 1);
                int32_t cv1 = clampi(iv + 1, 0, (int32_t)tex->h - 1);

                uint8_t *t00 = tex->pixels + (size_t)cv0 * tex->stride + (size_t)cu0 * 4;
                uint8_t *t10 = tex->pixels + (size_t)cv0 * tex->stride + (size_t)cu1 * 4;
                uint8_t *t01 = tex->pixels + (size_t)cv1 * tex->stride + (size_t)cu0 * 4;
                uint8_t *t11 = tex->pixels + (size_t)cv1 * tex->stride + (size_t)cu1 * 4;

                float ir = (float)t00[2] * (1-fu)*(1-fv) + (float)t10[2] * fu*(1-fv) +
                          (float)t01[2] * (1-fu)*fv + (float)t11[2] * fu*fv;
                float ig = (float)t00[1] * (1-fu)*(1-fv) + (float)t10[1] * fu*(1-fv) +
                          (float)t01[1] * (1-fu)*fv + (float)t11[1] * fu*fv;
                float ib = (float)t00[0] * (1-fu)*(1-fv) + (float)t10[0] * fu*(1-fv) +
                          (float)t01[0] * (1-fu)*fv + (float)t11[0] * fu*fv;
                float ia = (float)t00[3] * (1-fu)*(1-fv) + (float)t10[3] * fu*(1-fv) +
                          (float)t01[3] * (1-fu)*fv + (float)t11[3] * fu*fv;

                uint8_t r = (uint8_t)((ir / 255.0f) * tint_r + 0.5f);
                uint8_t g = (uint8_t)((ig / 255.0f) * tint_g + 0.5f);
                uint8_t b = (uint8_t)((ib / 255.0f) * tint_b + 0.5f);
                uint8_t a = (uint8_t)((ia / 255.0f) * tint_a + 0.5f);

                blend_pixel(p, r, g, b, a);
            }
            p += 4;
        }
        row += sw->target.stride;
    }
}

/* ---- vtable implementation ---- */

static const fx_renderer_vtbl *sw_vtable_init(sw_renderer *sw)
{
    static const fx_renderer_vtbl v = {
        .destroy          = sw_destroy,
        .surface_extent   = sw_surface_extent,
        .begin_frame      = sw_begin_frame,
        .begin_pass       = sw_begin_pass,
        .end_pass         = sw_end_pass,
        .submit           = sw_submit,
        .read_pixels      = sw_read_pixels,
        .alloc_solid      = sw_alloc_solid,
        .alloc_image      = sw_alloc_image,
        .draw_solid       = sw_draw_solid,
        .flush_solid      = sw_flush_solid,
        .draw_image       = sw_draw_image,
        .draw_text        = sw_draw_text,
        .draw_gradient    = sw_draw_gradient,
        .scissor          = sw_scissor,
        .stencil_clear    = sw_stencil_clear,
        .stencil_fill     = sw_stencil_fill,
        .stencil_ref      = sw_stencil_ref,
        .cover_solid      = sw_cover_solid,
        .cover_gradient   = sw_cover_gradient,
        .blur             = sw_blur,
        .texture_alloc    = sw_texture_alloc,
        .texture_free     = sw_texture_free,
        .texture_update   = sw_texture_update,
        .surface_texture  = sw_surface_texture,
    };
    sw->vtbl = &v;
    return sw->vtbl;
}

fx_renderer *fx_renderer_create_software(uint32_t w, uint32_t h)
{
    sw_renderer *sw = calloc(1, sizeof(*sw));
    if (!sw) return nullptr;

    size_t stride = (size_t)w * 4;
    sw->fb.pixels  = calloc(1, (size_t)h * stride);
    sw->fb.stencil = calloc(1, (size_t)w * h);
    sw->fb.w = w;
    sw->fb.h = h;
    sw->fb.stride = stride;

    if (!sw->fb.pixels || !sw->fb.stencil) {
        free(sw->fb.pixels);
        free(sw->fb.stencil);
        free(sw);
        return nullptr;
    }

    sw->target = sw->fb;
    sw->scissor_x = 0;
    sw->scissor_y = 0;
    sw->scissor_w = w;
    sw->scissor_h = h;
    sw->offscreen = true;

    fx_arena_init(&sw->arena, 65536);
    sw_vtable_init(sw);
    return (fx_renderer *)sw;
}

static void sw_destroy(fx_renderer *r)
{
    sw_renderer *sw = self(r);
    free(sw->fb.pixels);
    free(sw->fb.stencil);
    for (uint32_t i = 0; i < sw->buf_count; i++) {
        free(sw->buffers[i].solid);
        free(sw->buffers[i].image);
    }
    free(sw->buffers);
    sw_texture *t = sw->textures;
    while (t) {
        sw_texture *nx = t->next;
        free(t->pixels);
        free(t);
        t = nx;
    }
    fx_arena_destroy(&sw->arena);
    free(sw);
}

static void sw_surface_extent(fx_renderer *r, uint32_t *w, uint32_t *h)
{
    sw_renderer *sw = self(r);
    *w = sw->fb.w;
    *h = sw->fb.h;
}

static void sw_begin_frame(fx_renderer *r)
{
    sw_renderer *sw = self(r);
    fx_arena_reset(&sw->arena);
    sw->buf_count = 0;
}

static void sw_begin_pass(fx_renderer *r, fx_color clear)
{
    sw_renderer *sw = self(r);
    if (clear) {
        uint8_t cr = (clear >> 16) & 0xFF, cg = (clear >> 8) & 0xFF;
        uint8_t cb = clear & 0xFF, ca = (clear >> 24) & 0xFF;
        uint8_t *p = sw->fb.pixels;
        for (uint32_t y = 0; y < sw->fb.h; y++) {
            for (uint32_t x = 0; x < sw->fb.w; x++) {
                p[x * 4 + 0] = cb;
                p[x * 4 + 1] = cg;
                p[x * 4 + 2] = cr;
                p[x * 4 + 3] = ca;
            }
            p += sw->fb.stride;
        }
    }
    memset(sw->fb.stencil, 0, (size_t)sw->fb.w * sw->fb.h);
    sw->scissor_x = 0;
    sw->scissor_y = 0;
    sw->scissor_w = sw->fb.w;
    sw->scissor_h = sw->fb.h;
    sw->stencil_ref = 0;
    sw->batch.active = false;
}

static void sw_end_pass(fx_renderer *r)
{
    sw_flush_solid(r);
}

static void sw_submit(fx_renderer *r)
{
    (void)r;
}

static bool sw_read_pixels(fx_renderer *r, void *data, size_t stride)
{
    sw_renderer *sw = self(r);
    if (!data) return false;
    if (stride == 0) stride = sw->fb.stride;
    for (uint32_t y = 0; y < sw->fb.h; y++) {
        memcpy((uint8_t *)data + y * stride,
               sw->fb.pixels + y * sw->fb.stride,
               sw->fb.w * 4);
    }
    return true;
}

static void ensure_buf_cap(sw_renderer *sw)
{
    if (sw->buf_count >= sw->buf_cap) {
        uint32_t nc = sw->buf_cap ? sw->buf_cap * 2 : 16;
        sw_buffer *nb = realloc(sw->buffers, nc * sizeof(*nb));
        if (nb) { sw->buffers = nb; sw->buf_cap = nc; }
    }
}

static fx_solid_vertex *sw_alloc_solid(fx_renderer *r, size_t count, fx_r_buffer **buf, uint32_t *first)
{
    sw_renderer *sw = self(r);
    ensure_buf_cap(sw);
    uint32_t idx = sw->buf_count++;
    sw->buffers[idx] = (sw_buffer){0};
    sw->buffers[idx].solid = malloc(count * sizeof(fx_solid_vertex));
    sw->buffers[idx].count = count;
    *buf = (fx_r_buffer *)(uintptr_t)(idx + 1);
    *first = 0;
    return sw->buffers[idx].solid;
}

static fx_image_vertex *sw_alloc_image(fx_renderer *r, size_t count, fx_r_buffer **buf, uint32_t *first)
{
    sw_renderer *sw = self(r);
    ensure_buf_cap(sw);
    uint32_t idx = sw->buf_count++;
    sw->buffers[idx] = (sw_buffer){0};
    sw->buffers[idx].image = malloc(count * sizeof(fx_image_vertex));
    sw->buffers[idx].count = count;
    *buf = (fx_r_buffer *)(uintptr_t)(idx + 1);
    *first = 0;
    return sw->buffers[idx].image;
}

static sw_buffer *resolve_buf(sw_renderer *sw, fx_r_buffer *buf)
{
    uint32_t idx = (uint32_t)(uintptr_t)buf - 1;
    if (idx >= sw->buf_count) return nullptr;
    return &sw->buffers[idx];
}

static void sw_draw_solid(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color)
{
    sw_renderer *sw = self(r);
    if (sw->batch.active && sw->batch.color != color) {
        sw_flush_solid(r);
    }
    if (!sw->batch.active) {
        sw->batch.color = color;
        sw->batch.first = first;
        sw->batch.count = count;
        sw->batch.active = true;
    } else {
        sw->batch.count = first + count;
    }
    (void)buf;
}

static void sw_flush_solid(fx_renderer *r)
{
    sw_renderer *sw = self(r);
    if (!sw->batch.active) return;

    uint8_t cr = (sw->batch.color >> 16) & 0xFF;
    uint8_t cg = (sw->batch.color >> 8) & 0xFF;
    uint8_t cb = sw->batch.color & 0xFF;
    uint8_t ca = (sw->batch.color >> 24) & 0xFF;

    /* Walk all buffers in range; batch draws all triangles sequentially */
    uint32_t remaining = sw->batch.count;
    for (uint32_t bi = 0; bi < sw->buf_count && remaining > 0; bi++) {
        sw_buffer *b = &sw->buffers[bi];
        if (!b->solid || b->count == 0) continue;
        uint32_t tri_count = b->count / 3;
        if (tri_count == 0) continue;
        for (uint32_t t = 0; t < tri_count; t++) {
            raster_solid(sw,
                         &b->solid[t * 3 + 0],
                         &b->solid[t * 3 + 1],
                         &b->solid[t * 3 + 2],
                         cr, cg, cb, ca,
                         false, false, 0, 0, 0, 0);
        }
        uint32_t drawn = tri_count * 3;
        remaining = drawn >= remaining ? 0 : remaining - drawn;
    }

    sw->batch.active = false;
}

static void sw_draw_image(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_r_texture *tex)
{
    sw_renderer *sw = self(r);
    sw_flush_solid(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b || !b->image) return;
    sw_texture *t = (sw_texture *)tex;
    if (!t) return;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_image(sw, &b->image[first + i], &b->image[first + i + 1], &b->image[first + i + 2],
                     t, 255, 255, 255, 255);
    }
}

static void sw_draw_text(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color)
{
    sw_renderer *sw = self(r);
    sw_flush_solid(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b || !b->image) return;
    if (!sw->atlas) return;

    uint8_t cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF;
    uint8_t cb = color & 0xFF, ca = (color >> 24) & 0xFF;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_image(sw, &b->image[first + i], &b->image[first + i + 1], &b->image[first + i + 2],
                     sw->atlas, cr, cg, cb, ca);
    }
}

static void sw_draw_gradient(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, const fx_gradient *grad)
{
    sw_renderer *sw = self(r);
    sw_flush_solid(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b || !b->solid) return;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_gradient(sw, &b->solid[first + i], &b->solid[first + i + 1], &b->solid[first + i + 2],
                        grad, false);
    }
}

static void sw_scissor(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    sw_renderer *sw = self(r);
    sw->scissor_x = x;
    sw->scissor_y = y;
    sw->scissor_w = w;
    sw->scissor_h = h;
}

static void sw_stencil_clear(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    sw_renderer *sw = self(r);
    int32_t ex = min_i(x + (int32_t)w, (int32_t)sw->fb.w);
    int32_t ey = min_i(y + (int32_t)h, (int32_t)sw->fb.h);
    for (int32_t yy = y; yy < ey; yy++) {
        memset(sw->fb.stencil + (size_t)yy * sw->fb.w + (size_t)x, 0, (size_t)(ex - x));
    }
}

static void sw_stencil_fill(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, int fill_rule)
{
    sw_renderer *sw = self(r);
    sw->stencil_fill_rule = fill_rule;
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b || !b->solid) return;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_solid(sw, &b->solid[first + i], &b->solid[first + i + 1], &b->solid[first + i + 2],
                     0, 0, 0, 255, true, false, 0, 0, 0, 0);
    }
}

static void sw_blur(fx_renderer *r, float sigma)
{
    sw_renderer *sw = self(r);
    if (sigma <= 0.0f) return;

    /* Separable Gaussian blur in-place.
     * Uses a 7-tap kernel at ±3 sigma range. */
    int radius = (int)(sigma * 3.0f + 0.5f);
    if (radius < 1) radius = 1;
    if (radius > 64) radius = 64;

    uint32_t w = sw->fb.w, h = sw->fb.h;
    size_t stride = sw->fb.stride;
    uint8_t *src = sw->fb.pixels;

    /* Allocate temp buffer */
    uint8_t *tmp = malloc((size_t)h * stride);
    if (!tmp) return;

    /* Build 1D kernel */
    float kernel[129];
    float sum = 0.0f;
    float s2 = 2.0f * sigma * sigma;
    for (int i = -radius; i <= radius; i++) {
        kernel[i + radius] = expf(-(float)(i * i) / s2);
        sum += kernel[i + radius];
    }
    for (int i = -radius; i <= radius; i++)
        kernel[i + radius] /= sum;

    /* Horizontal pass: src → tmp */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = src + (size_t)y * stride;
        uint8_t *dst_row = tmp + (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int k = -radius; k <= radius; k++) {
                int sx = (int)x + k;
                if (sx < 0) sx = 0;
                if (sx >= (int)w) sx = (int)w - 1;
                uint8_t *sp = row + (size_t)sx * 4;
                float wt = kernel[k + radius];
                r += (float)sp[2] * wt;
                g += (float)sp[1] * wt;
                b += (float)sp[0] * wt;
                a += (float)sp[3] * wt;
            }
            dst_row[x * 4 + 0] = (uint8_t)(b + 0.5f);
            dst_row[x * 4 + 1] = (uint8_t)(g + 0.5f);
            dst_row[x * 4 + 2] = (uint8_t)(r + 0.5f);
            dst_row[x * 4 + 3] = (uint8_t)(a + 0.5f);
        }
    }

    /* Vertical pass: tmp → src */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *dst_row = src + (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int k = -radius; k <= radius; k++) {
                int sy = (int)y + k;
                if (sy < 0) sy = 0;
                if (sy >= (int)h) sy = (int)h - 1;
                uint8_t *sp = tmp + (size_t)sy * stride + (size_t)x * 4;
                float wt = kernel[k + radius];
                r += (float)sp[2] * wt;
                g += (float)sp[1] * wt;
                b += (float)sp[0] * wt;
                a += (float)sp[3] * wt;
            }
            dst_row[x * 4 + 0] = (uint8_t)(b + 0.5f);
            dst_row[x * 4 + 1] = (uint8_t)(g + 0.5f);
            dst_row[x * 4 + 2] = (uint8_t)(r + 0.5f);
            dst_row[x * 4 + 3] = (uint8_t)(a + 0.5f);
        }
    }

    free(tmp);
}

static fx_r_texture *sw_surface_texture(fx_renderer *r)
{
    /* The software framebuffer IS the texture — wrap it directly. */
    sw_renderer *sw = self(r);
    sw_texture *t = calloc(1, sizeof(*t));
    if (!t) return nullptr;
    t->w = sw->fb.w;
    t->h = sw->fb.h;
    t->stride = sw->fb.stride;
    t->fmt = FX_FMT_BGRA8_UNORM;
    t->pixels = malloc((size_t)t->h * t->stride);
    if (!t->pixels) { free(t); return nullptr; }
    memcpy(t->pixels, sw->fb.pixels, (size_t)t->h * t->stride);
    t->next = sw->textures;
    sw->textures = t;
    return (fx_r_texture *)t;
}

static void sw_stencil_ref(fx_renderer *r, uint32_t ref)
{
    sw_renderer *sw = self(r);
    sw->stencil_ref = (uint8_t)ref;
}

static void sw_cover_solid(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color)
{
    sw_renderer *sw = self(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b || !b->solid) return;

    uint8_t cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF;
    uint8_t cb = color & 0xFF, ca = (color >> 24) & 0xFF;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_solid(sw, &b->solid[first + i], &b->solid[first + i + 1], &b->solid[first + i + 2],
                     0, 0, 0, 0, false, true, cr, cg, cb, ca);
    }
}

static void sw_cover_gradient(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, const fx_gradient *grad)
{
    sw_renderer *sw = self(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b || !b->solid) return;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_gradient(sw, &b->solid[first + i], &b->solid[first + i + 1], &b->solid[first + i + 2],
                        grad, true);
    }
}

static fx_r_texture *sw_texture_alloc(fx_renderer *r, uint32_t w, uint32_t h, fx_pixel_format fmt, const void *data, size_t stride)
{
    sw_renderer *sw = self(r);
    sw_texture *t = calloc(1, sizeof(*t));
    if (!t) return nullptr;

    size_t real_stride = stride > 0 ? stride : (size_t)w * 4;
    t->pixels = malloc((size_t)h * real_stride);
    if (!t->pixels) { free(t); return nullptr; }
    t->w = w; t->h = h; t->stride = real_stride; t->fmt = fmt;

    if (data) {
        memcpy(t->pixels, data, (size_t)h * real_stride);
    } else {
        memset(t->pixels, 0, (size_t)h * real_stride);
    }

    t->next = sw->textures;
    sw->textures = t;
    return (fx_r_texture *)t;
}

static void sw_texture_free(fx_renderer *r, fx_r_texture *tex)
{
    (void)r;
    sw_texture *t = (sw_texture *)tex;
    if (!t) return;
    free(t->pixels);
    free(t);
}

static void sw_texture_update(fx_renderer *r, fx_r_texture *tex, const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    (void)r;
    sw_texture *t = (sw_texture *)tex;
    if (!t || !data) return;
    for (uint32_t row = 0; row < h; row++) {
        const uint8_t *src = (const uint8_t *)data + (size_t)row * w * 4;
        uint8_t *dst = t->pixels + (size_t)(y + row) * t->stride + (size_t)x * 4;
        memcpy(dst, src, (size_t)w * 4);
    }
}
