/*
 * CPU software renderer backend.
 *
 * Renders directly to a pixel buffer in system memory. Implements
 * triangle rasterization (scanline with edge walking), alpha
 * blending, bilinear texture sampling, gradient evaluation, and a
 * stencil buffer — enough to prove the renderer vtable abstraction.
 */
#include "rhi/rhi.h"
#include "../../internal.h"
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

/* ---- ring buffer for vertex data ---- */

typedef struct {
    uint8_t *base;
    size_t   cap;
    size_t   used;
} sw_buffer_pool;

/* ---- sw implementation types ---- */

typedef struct sw_buffer {
    size_t offset;  /* byte offset into pool */
    size_t count;
} sw_buffer;

typedef struct sw_texture {
    uint8_t       *pixels;
    uint32_t       w, h;
    size_t         stride;
    flux_pixel_format fmt;
    struct sw_texture *next; /* linked list for cleanup */
} sw_texture;

typedef struct sw_batch {
    flux_color  color;
    uint32_t  buf_index;  /* which buffer the batch starts in */
    uint32_t  first;
    uint32_t  count;
    bool      active;
    flux_blend_mode blend_mode;
} sw_batch;

typedef struct sw_renderer {
    const flux_rhi_vtbl *vtbl;  /* must be first — vt() reads this */

    sw_fb       fb;
    sw_fb       target;    /* pointer to fb or external buffer */
    bool        offscreen;

    sw_buffer  *buffers;
    uint32_t    buf_count;
    uint32_t    buf_cap;

    sw_buffer_pool pool;

    sw_texture *textures;

    sw_batch    batch;

    /* clip state */
    int32_t     scissor_x, scissor_y;
    uint32_t    scissor_w, scissor_h;
    uint32_t    stencil_ref;
    int         stencil_fill_rule;
    flux_blend_mode blend_mode;
} sw_renderer;

/* vtable forward decls */
static void  sw_destroy(flux_rhi_device *r);
static void  sw_surface_extent(flux_rhi_device *r, uint32_t *w, uint32_t *h);
static void  sw_begin_frame(flux_rhi_device *r);
static void  sw_begin_pass(flux_rhi_device *r, flux_color clear);
static void  sw_end_pass(flux_rhi_device *r);
static void  sw_submit(flux_rhi_device *r);
static bool  sw_read_pixels(flux_rhi_device *r, void *data, size_t stride);
static bool  sw_resize(flux_rhi_device *r, uint32_t w, uint32_t h);
static flux_solid_vertex *sw_alloc_solid(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first);
static flux_image_vertex *sw_alloc_image(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first);
static flux_fringe_vertex *sw_alloc_fringe(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first);
static void  sw_draw_solid(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color);
static void  sw_draw_fringe(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color);
static void  sw_flush_solid(flux_rhi_device *r);
static void  sw_draw_image(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_r_texture *tex, flux_color tint);
static void  sw_draw_text(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_r_texture *tex, flux_color color);
static void  sw_draw_gradient(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, const flux_gradient *grad);
static void  sw_scissor(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
static void  sw_stencil_clear(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
static void  sw_stencil_fill(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, int fill_rule);
static void  sw_blur(flux_rhi_device *r, float sigma);
static void  sw_blend_mode(flux_rhi_device *r, flux_blend_mode mode);
static flux_r_texture *sw_surface_texture(flux_rhi_device *r);
static void  sw_stencil_ref(flux_rhi_device *r, uint32_t ref);
static void  sw_cover_solid(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color);
static void  sw_cover_gradient(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, const flux_gradient *grad);
static flux_r_texture *sw_texture_alloc(flux_rhi_device *r, uint32_t w, uint32_t h, flux_pixel_format fmt, const void *data, size_t stride);
static void  sw_texture_free(flux_rhi_device *r, flux_r_texture *tex);
static void  sw_texture_update(flux_rhi_device *r, flux_r_texture *tex, const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* ---- helpers ---- */

static sw_renderer *self(flux_rhi_device *r) { return (sw_renderer *)r; }

static inline uint8_t to_u8(float v) {
    int iv = (int)(v * 255.0f + 0.5f);
    return (uint8_t)(iv < 0 ? 0 : iv > 255 ? 255 : iv);
}

static void blend_pixel(uint8_t *dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa, flux_blend_mode mode)
{
    if (mode == FLUX_BLEND_SRC_OVER) {
        if (sa == 0) return;
        if (sa == 255) {
            dst[0] = sr; dst[1] = sg; dst[2] = sb; dst[3] = 255;
            return;
        }
        uint32_t da = dst[3];
        uint32_t inv_a = 255 - sa;
        dst[0] = (uint8_t)(((uint32_t)sr * 255 + (uint32_t)dst[0] * inv_a) / 255);
        dst[1] = (uint8_t)(((uint32_t)sg * 255 + (uint32_t)dst[1] * inv_a) / 255);
        dst[2] = (uint8_t)(((uint32_t)sb * 255 + (uint32_t)dst[2] * inv_a) / 255);
        dst[3] = (uint8_t)(sa + (da * inv_a) / 255);
        return;
    }

    uint8_t dr = dst[0], dg = dst[1], db = dst[2], da = dst[3];

    switch (mode) {
    case FLUX_BLEND_DST_OVER: {
        uint32_t fa = 255 - da;
        dst[0] = (uint8_t)(((uint32_t)sr * fa + (uint32_t)dr * 255) / 255);
        dst[1] = (uint8_t)(((uint32_t)sg * fa + (uint32_t)dg * 255) / 255);
        dst[2] = (uint8_t)(((uint32_t)sb * fa + (uint32_t)db * 255) / 255);
        dst[3] = (uint8_t)(((uint32_t)sa * fa + (uint32_t)da * 255) / 255);
        break;
    }
    case FLUX_BLEND_SRC_IN: {
        dst[0] = (uint8_t)(((uint32_t)sr * da) / 255);
        dst[1] = (uint8_t)(((uint32_t)sg * da) / 255);
        dst[2] = (uint8_t)(((uint32_t)sb * da) / 255);
        dst[3] = (uint8_t)(((uint32_t)sa * da) / 255);
        break;
    }
    case FLUX_BLEND_DST_IN: {
        dst[0] = (uint8_t)(((uint32_t)dr * sa) / 255);
        dst[1] = (uint8_t)(((uint32_t)dg * sa) / 255);
        dst[2] = (uint8_t)(((uint32_t)db * sa) / 255);
        dst[3] = (uint8_t)(((uint32_t)da * sa) / 255);
        break;
    }
    case FLUX_BLEND_SRC_OUT: {
        uint32_t fa = 255 - da;
        dst[0] = (uint8_t)(((uint32_t)sr * fa) / 255);
        dst[1] = (uint8_t)(((uint32_t)sg * fa) / 255);
        dst[2] = (uint8_t)(((uint32_t)sb * fa) / 255);
        dst[3] = (uint8_t)(((uint32_t)sa * fa) / 255);
        break;
    }
    case FLUX_BLEND_DST_OUT: {
        uint32_t fb = 255 - sa;
        dst[0] = (uint8_t)(((uint32_t)dr * fb) / 255);
        dst[1] = (uint8_t)(((uint32_t)dg * fb) / 255);
        dst[2] = (uint8_t)(((uint32_t)db * fb) / 255);
        dst[3] = (uint8_t)(((uint32_t)da * fb) / 255);
        break;
    }
    case FLUX_BLEND_SRC_ATOP: {
        dst[0] = (uint8_t)(((uint32_t)sr * da + (uint32_t)dr * (255 - sa)) / 255);
        dst[1] = (uint8_t)(((uint32_t)sg * da + (uint32_t)dg * (255 - sa)) / 255);
        dst[2] = (uint8_t)(((uint32_t)sb * da + (uint32_t)db * (255 - sa)) / 255);
        dst[3] = (uint8_t)(((uint32_t)sa * da + (uint32_t)da * (255 - sa)) / 255);
        break;
    }
    case FLUX_BLEND_DST_ATOP: {
        dst[0] = (uint8_t)(((uint32_t)sr * (255 - da) + (uint32_t)dr * sa) / 255);
        dst[1] = (uint8_t)(((uint32_t)sg * (255 - da) + (uint32_t)dg * sa) / 255);
        dst[2] = (uint8_t)(((uint32_t)sb * (255 - da) + (uint32_t)db * sa) / 255);
        dst[3] = (uint8_t)(((uint32_t)sa * (255 - da) + (uint32_t)da * sa) / 255);
        break;
    }
    case FLUX_BLEND_XOR: {
        uint32_t fa = 255 - da;
        uint32_t fb = 255 - sa;
        dst[0] = (uint8_t)(((uint32_t)sr * fa + (uint32_t)dr * fb) / 255);
        dst[1] = (uint8_t)(((uint32_t)sg * fa + (uint32_t)dg * fb) / 255);
        dst[2] = (uint8_t)(((uint32_t)sb * fa + (uint32_t)db * fb) / 255);
        dst[3] = (uint8_t)(((uint32_t)sa * fa + (uint32_t)da * fb) / 255);
        break;
    }
    case FLUX_BLEND_PLUS: {
        uint16_t r = (uint16_t)sr + dr;
        uint16_t g = (uint16_t)sg + dg;
        uint16_t b = (uint16_t)sb + db;
        uint16_t a = (uint16_t)sa + da;
        dst[0] = r > 255 ? 255 : (uint8_t)r;
        dst[1] = g > 255 ? 255 : (uint8_t)g;
        dst[2] = b > 255 ? 255 : (uint8_t)b;
        dst[3] = a > 255 ? 255 : (uint8_t)a;
        break;
    }
    case FLUX_BLEND_MULTIPLY:
    case FLUX_BLEND_SCREEN:
    case FLUX_BLEND_OVERLAY: {
        float sa_f = sa / 255.0f;
        float da_f = da / 255.0f;
        float s[3], d[3];
        for (int i = 0; i < 3; i++) {
            uint8_t sv = i == 0 ? sr : i == 1 ? sg : sb;
            uint8_t dv = i == 0 ? dr : i == 1 ? dg : db;
            s[i] = sa_f > 0.0f ? (sv / 255.0f) / sa_f : 0.0f;
            d[i] = da_f > 0.0f ? (dv / 255.0f) / da_f : 0.0f;
        }
        float r[3];
        for (int i = 0; i < 3; i++) {
            float bval;
            if (mode == FLUX_BLEND_MULTIPLY) {
                bval = s[i] * d[i];
            } else if (mode == FLUX_BLEND_SCREEN) {
                bval = s[i] + d[i] - s[i] * d[i];
            } else { /* OVERLAY */
                if (d[i] < 0.5f)
                    bval = 2.0f * s[i] * d[i];
                else
                    bval = 1.0f - 2.0f * (1.0f - s[i]) * (1.0f - d[i]);
            }
            r[i] = s[i] * (1.0f - da_f) + d[i] * (1.0f - sa_f) + bval * sa_f * da_f;
        }
        float ra = sa_f + da_f * (1.0f - sa_f);
        if (ra > 0.0f) {
            for (int i = 0; i < 3; i++)
                dst[i] = (uint8_t)(r[i] * ra * 255.0f + 0.5f);
        } else {
            dst[0] = dst[1] = dst[2] = 0;
        }
        dst[3] = (uint8_t)(ra * 255.0f + 0.5f);
        break;
    }
    default:
        break;
    }
}

static inline int32_t min_i(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t max_i(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void *sw_pool_alloc(sw_renderer *sw, size_t size)
{
    size_t aligned = (size + 15) & ~(size_t)15;  /* 16-byte align */
    if (sw->pool.used + aligned > sw->pool.cap)
        return nullptr;
    void *ptr = sw->pool.base + sw->pool.used;
    sw->pool.used += aligned;
    return ptr;
}

static flux_solid_vertex *buf_solid(const sw_renderer *sw, const sw_buffer *b)
{
    return (flux_solid_vertex *)(sw->pool.base + b->offset);
}

static flux_image_vertex *buf_image(const sw_renderer *sw, const sw_buffer *b)
{
    return (flux_image_vertex *)(sw->pool.base + b->offset);
}

/* ---- triangle rasterization ---- */

static void raster_solid(sw_renderer *sw,
                         const flux_solid_vertex *v0,
                         const flux_solid_vertex *v1,
                         const flux_solid_vertex *v2,
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
    int32_t bias0 = (A01 > 0 || (A01 == 0 && B01 > 0)) ? 0 : -1;
    int32_t bias1 = (A12 > 0 || (A12 == 0 && B12 > 0)) ? 0 : -1;
    int32_t bias2 = (A20 > 0 || (A20 == 0 && B20 > 0)) ? 0 : -1;

    /* Starting edge values at (minx, miny) */
    int32_t w0_row = A01 * minx + B01 * miny + (x0 * y1 - y0 * x1) + bias0;
    int32_t w1_row = A12 * minx + B12 * miny + (x1 * y2 - y1 * x2) + bias1;
    int32_t w2_row = A20 * minx + B20 * miny + (x2 * y0 - y2 * x0) + bias2;

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
                        *sp = (*sp + 1) & 0xFF;  /* modulo 256 wrap */
                    }
                } else if (cover_solid) {
                    bool pass = (sw->stencil_fill_rule == 0)
                        ? ((*sp & 1) != 0)      /* even-odd */
                        : (*sp != 0);            /* non-zero */
                    if (pass)
                        blend_pixel(p, cover_r, cover_g, cover_b, cover_a, sw->blend_mode);
                } else {
                    /* Normal drawing: stencil acts as a mask against stencil_ref. */
                    if (*sp == sw->stencil_ref)
                        blend_pixel(p, r, g, b, a, sw->blend_mode);
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

static void raster_fringe(sw_renderer *sw,
                          const flux_fringe_vertex *v0,
                          const flux_fringe_vertex *v1,
                          const flux_fringe_vertex *v2,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    float fx0 = v0->pos[0], fy0 = v0->pos[1];
    float fx1 = v1->pos[0], fy1 = v1->pos[1];
    float fx2 = v2->pos[0], fy2 = v2->pos[1];

    int32_t x0 = (int32_t)fx0, y0 = (int32_t)fy0;
    int32_t x1 = (int32_t)fx1, y1 = (int32_t)fy1;
    int32_t x2 = (int32_t)fx2, y2 = (int32_t)fy2;

    int32_t minx = max_i(sw->scissor_x, min_i(min_i(x0, x1), x2));
    int32_t maxx = min_i(sw->scissor_x + (int32_t)sw->scissor_w - 1,
                         max_i(max_i(x0, x1), x2));
    int32_t miny = max_i(sw->scissor_y, min_i(min_i(y0, y1), y2));
    int32_t maxy = min_i(sw->scissor_y + (int32_t)sw->scissor_h - 1,
                         max_i(max_i(y0, y1), y2));

    if (minx > maxx || miny > maxy) return;

    int32_t area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0) return;

    int32_t A01 = y0 - y1, B01 = x1 - x0;
    int32_t A12 = y1 - y2, B12 = x2 - x1;
    int32_t A20 = y2 - y0, B20 = x0 - x2;

    int32_t bias0 = (A01 > 0 || (A01 == 0 && B01 > 0)) ? 0 : -1;
    int32_t bias1 = (A12 > 0 || (A12 == 0 && B12 > 0)) ? 0 : -1;
    int32_t bias2 = (A20 > 0 || (A20 == 0 && B20 > 0)) ? 0 : -1;

    int32_t w0_row = A01 * minx + B01 * miny + (x0 * y1 - y0 * x1) + bias0;
    int32_t w1_row = A12 * minx + B12 * miny + (x1 * y2 - y1 * x2) + bias1;
    int32_t w2_row = A20 * minx + B20 * miny + (x2 * y0 - y2 * x0) + bias2;

    float c0 = v0->coverage, c1 = v1->coverage, c2 = v2->coverage;
    float inv_area = 1.0f / (float)area;

    uint8_t *row = sw->target.pixels + (size_t)miny * sw->target.stride + (size_t)minx * 4;

    for (int32_t y = miny; y <= maxy; y++) {
        int32_t w0 = w0_row, w1 = w1_row, w2 = w2_row;
        uint8_t *p = row;

        for (int32_t x = minx; x <= maxx; x++) {
            if ((w0 | w1 | w2) >= 0) {
                float lambda0 = (float)w0 * inv_area;
                float lambda1 = (float)w1 * inv_area;
                float lambda2 = (float)w2 * inv_area;
                float cov = lambda0 * c0 + lambda1 * c1 + lambda2 * c2;
                if (cov > 0.0f) {
                    uint8_t alpha = (uint8_t)(a * cov + 0.5f);
                    if (alpha > 0)
                        blend_pixel(p, r, g, b, alpha, sw->blend_mode);
                }
            }
            w0 += A01; w1 += A12; w2 += A20;
            p += 4;
        }

        w0_row += B01; w1_row += B12; w2_row += B20;
        row += sw->target.stride;
    }
}

/* ---- gradient evaluation ---- */

static void eval_gradient(const flux_gradient *grad, float t, uint8_t col[4])
{
    float cf[4] = {0};

    switch (grad->extend) {
    case FLUX_EXTEND_PAD:
        break;
    case FLUX_EXTEND_REPEAT:
        t = t - floorf(t);
        break;
    case FLUX_EXTEND_REFLECT:
        t = fabsf(t - 2.0f * floorf(t * 0.5f + 0.5f));
        break;
    }

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

static float grad_t_linear(const flux_gradient *grad, float x, float y)
{
    float dx = grad->end[0] - grad->start[0];
    float dy = grad->end[1] - grad->start[1];
    float denom = dx * dx + dy * dy;
    if (denom == 0.0f) return 0.0f;
    return ((x - grad->start[0]) * dx + (y - grad->start[1]) * dy) / denom;
}

static float grad_t_radial(const flux_gradient *grad, float x, float y)
{
    float dx = x - grad->start[0];
    float dy = y - grad->start[1];
    float dist = sqrtf(dx * dx + dy * dy);
    float r = grad->end[0];  /* radius stored in end[0] */
    if (r == 0.0f) return 0.0f;
    return dist / r;
}

static void raster_gradient(sw_renderer *sw,
                            const flux_solid_vertex *v0,
                            const flux_solid_vertex *v1,
                            const flux_solid_vertex *v2,
                            const flux_gradient *grad,
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
    int32_t bias0 = (A01 > 0 || (A01 == 0 && B01 > 0)) ? 0 : -1;
    int32_t bias1 = (A12 > 0 || (A12 == 0 && B12 > 0)) ? 0 : -1;
    int32_t bias2 = (A20 > 0 || (A20 == 0 && B20 > 0)) ? 0 : -1;

    int32_t w0_row = A01 * minx + B01 * miny + (x0 * y1 - y0 * x1) + bias0;
    int32_t w1_row = A12 * minx + B12 * miny + (x1 * y2 - y1 * x2) + bias1;
    int32_t w2_row = A20 * minx + B20 * miny + (x2 * y0 - y2 * x0) + bias2;

    float (*t_fn)(const flux_gradient *, float, float) =
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
                    blend_pixel(p, col[0], col[1], col[2], col[3], sw->blend_mode);
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
                         const flux_image_vertex *v0,
                         const flux_image_vertex *v1,
                         const flux_image_vertex *v2,
                         const sw_texture *tex,
                         uint8_t tint_r, uint8_t tint_g, uint8_t tint_b, uint8_t tint_a)
{
    float x0 = v0->pos[0], y0 = v0->pos[1], u0 = v0->uv[0], vv0 = v0->uv[1];
    float x1 = v1->pos[0], y1 = v1->pos[1], u1 = v1->uv[0], vv1 = v1->uv[1];
    float x2 = v2->pos[0], y2 = v2->pos[1], u2 = v2->uv[0], vv2 = v2->uv[1];

    int32_t minx = max_i(sw->scissor_x, (int32_t)floorf(fminf(fminf(x0, x1), x2)));
    int32_t maxx = min_i(sw->scissor_x + (int32_t)sw->scissor_w - 1, (int32_t)ceilf(fmaxf(fmaxf(x0, x1), x2)));
    int32_t miny = max_i(sw->scissor_y, (int32_t)floorf(fminf(fminf(y0, y1), y2)));
    int32_t maxy = min_i(sw->scissor_y + (int32_t)sw->scissor_h - 1, (int32_t)ceilf(fmaxf(fmaxf(y0, y1), y2)));

    if (minx > maxx || miny > maxy) return;

    float denom = (x2 - x0) * (y1 - y0) - (x1 - x0) * (y2 - y0);
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

                uint8_t r00, g00, b00, a00;
                uint8_t r10, g10, b10, a10;
                uint8_t r01, g01, b01, a01;
                uint8_t r11, g11, b11, a11;

                if (tex->fmt == FLUX_FMT_A8_UNORM) {
                    uint8_t *p00 = tex->pixels + (size_t)cv0 * tex->stride + (size_t)cu0;
                    uint8_t *p10 = tex->pixels + (size_t)cv0 * tex->stride + (size_t)cu1;
                    uint8_t *p01 = tex->pixels + (size_t)cv1 * tex->stride + (size_t)cu0;
                    uint8_t *p11 = tex->pixels + (size_t)cv1 * tex->stride + (size_t)cu1;
                    r00 = g00 = b00 = a00 = p00[0];
                    r10 = g10 = b10 = a10 = p10[0];
                    r01 = g01 = b01 = a01 = p01[0];
                    r11 = g11 = b11 = a11 = p11[0];
                } else {
                    uint8_t *p00 = tex->pixels + (size_t)cv0 * tex->stride + (size_t)cu0 * 4;
                    uint8_t *p10 = tex->pixels + (size_t)cv0 * tex->stride + (size_t)cu1 * 4;
                    uint8_t *p01 = tex->pixels + (size_t)cv1 * tex->stride + (size_t)cu0 * 4;
                    uint8_t *p11 = tex->pixels + (size_t)cv1 * tex->stride + (size_t)cu1 * 4;
                    if (tex->fmt == FLUX_FMT_BGRA8_UNORM) {
                        r00 = p00[2]; g00 = p00[1]; b00 = p00[0]; a00 = p00[3];
                        r10 = p10[2]; g10 = p10[1]; b10 = p10[0]; a10 = p10[3];
                        r01 = p01[2]; g01 = p01[1]; b01 = p01[0]; a01 = p01[3];
                        r11 = p11[2]; g11 = p11[1]; b11 = p11[0]; a11 = p11[3];
                    } else {
                        r00 = p00[0]; g00 = p00[1]; b00 = p00[2]; a00 = p00[3];
                        r10 = p10[0]; g10 = p10[1]; b10 = p10[2]; a10 = p10[3];
                        r01 = p01[0]; g01 = p01[1]; b01 = p01[2]; a01 = p01[3];
                        r11 = p11[0]; g11 = p11[1]; b11 = p11[2]; a11 = p11[3];
                    }
                }

                float ir = (float)r00 * (1-fu)*(1-fv) + (float)r10 * fu*(1-fv) +
                          (float)r01 * (1-fu)*fv + (float)r11 * fu*fv;
                float ig = (float)g00 * (1-fu)*(1-fv) + (float)g10 * fu*(1-fv) +
                          (float)g01 * (1-fu)*fv + (float)g11 * fu*fv;
                float ib = (float)b00 * (1-fu)*(1-fv) + (float)b10 * fu*(1-fv) +
                          (float)b01 * (1-fu)*fv + (float)b11 * fu*fv;
                float ia = (float)a00 * (1-fu)*(1-fv) + (float)a10 * fu*(1-fv) +
                          (float)a01 * (1-fu)*fv + (float)a11 * fu*fv;

                uint8_t r = (uint8_t)((ir / 255.0f) * tint_r + 0.5f);
                uint8_t g = (uint8_t)((ig / 255.0f) * tint_g + 0.5f);
                uint8_t b = (uint8_t)((ib / 255.0f) * tint_b + 0.5f);
                uint8_t a = (uint8_t)((ia / 255.0f) * tint_a + 0.5f);

                blend_pixel(p, r, g, b, a, sw->blend_mode);
            }
            p += 4;
        }
        row += sw->target.stride;
    }
}

/* ---- vtable implementation ---- */

static const flux_rhi_vtbl *sw_vtable_init(sw_renderer *sw)
{
    static const flux_rhi_vtbl v = {
        .destroy          = sw_destroy,
        .surface_extent   = sw_surface_extent,
        .begin_frame      = sw_begin_frame,
        .begin_pass       = sw_begin_pass,
        .end_pass         = sw_end_pass,
        .submit           = sw_submit,
        .read_pixels      = sw_read_pixels,
        .resize           = sw_resize,
        .alloc_solid      = sw_alloc_solid,
        .alloc_image      = sw_alloc_image,
        .alloc_fringe     = sw_alloc_fringe,
        .draw_solid       = sw_draw_solid,
        .draw_fringe      = sw_draw_fringe,
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
        .set_blend_mode   = sw_blend_mode,
        .texture_alloc    = sw_texture_alloc,
        .texture_free     = sw_texture_free,
        .texture_update   = sw_texture_update,
        .surface_texture  = sw_surface_texture,
    };
    sw->vtbl = &v;
    return sw->vtbl;
}

flux_rhi_device *flux_rhi_create_software(uint32_t w, uint32_t h)
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

    sw->pool.base = malloc(1024 * 1024);
    if (!sw->pool.base) {
        free(sw->fb.pixels);
        free(sw->fb.stencil);
        free(sw);
        return nullptr;
    }
    sw->pool.cap = 1024 * 1024;
    sw->pool.used = 0;

    sw_vtable_init(sw);
    return (flux_rhi_device *)sw;
}

static void sw_destroy(flux_rhi_device *r)
{
    sw_renderer *sw = self(r);
    free(sw->fb.pixels);
    free(sw->fb.stencil);
    free(sw->buffers);
    free(sw->pool.base);
    sw_texture *t = sw->textures;
    while (t) {
        sw_texture *nx = t->next;
        free(t->pixels);
        free(t);
        t = nx;
    }
    free(sw);
}

static void sw_surface_extent(flux_rhi_device *r, uint32_t *w, uint32_t *h)
{
    sw_renderer *sw = self(r);
    *w = sw->fb.w;
    *h = sw->fb.h;
}

static void sw_begin_frame(flux_rhi_device *r)
{
    sw_renderer *sw = self(r);
    sw->pool.used = 0;
    sw->buf_count = 0;
}

static void sw_blend_mode(flux_rhi_device *r, flux_blend_mode mode)
{
    sw_renderer *sw = self(r);
    sw->blend_mode = mode;
}

static void sw_begin_pass(flux_rhi_device *r, flux_color clear)
{
    sw_renderer *sw = self(r);
    sw->blend_mode = FLUX_BLEND_SRC_OVER;
    if (clear) {
        uint8_t cr = (uint8_t)((clear >> 16) & 0xFF), cg = (uint8_t)((clear >> 8) & 0xFF);
        uint8_t cb = (uint8_t)(clear & 0xFF), ca = (uint8_t)((clear >> 24) & 0xFF);
        uint8_t *p = sw->fb.pixels;
        for (uint32_t y = 0; y < sw->fb.h; y++) {
            for (uint32_t x = 0; x < sw->fb.w; x++) {
                p[x * 4 + 0] = cr;
                p[x * 4 + 1] = cg;
                p[x * 4 + 2] = cb;
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

static void sw_end_pass(flux_rhi_device *r)
{
    sw_flush_solid(r);
}

static void sw_submit(flux_rhi_device *r)
{
    (void)r;
}

static bool sw_read_pixels(flux_rhi_device *r, void *data, size_t stride)
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

static bool sw_resize(flux_rhi_device *r, uint32_t w, uint32_t h)
{
    sw_renderer *sw = self(r);
    if (w == 0 || h == 0) return false;

    size_t pixel_size = (size_t)w * h * 4;
    size_t stencil_size = (size_t)w * h;

    uint8_t *new_pixels = realloc(sw->fb.pixels, pixel_size);
    if (!new_pixels) return false;
    uint8_t *new_stencil = realloc(sw->fb.stencil, stencil_size);
    if (!new_stencil) {
        /* Do not update dimensions if stencil resize failed.
         * Caller must treat this as a fatal failure. */
        return false;
    }
    sw->fb.pixels = new_pixels;
    sw->fb.stencil = new_stencil;
    sw->fb.w = w;
    sw->fb.h = h;
    sw->fb.stride = (size_t)w * 4;
    if (sw->offscreen) sw->target = sw->fb;
    return true;
}

static bool ensure_buf_cap(sw_renderer *sw)
{
    if (sw->buf_count >= sw->buf_cap) {
        uint32_t nc = sw->buf_cap ? sw->buf_cap * 2 : 16;
        sw_buffer *nb = realloc(sw->buffers, nc * sizeof(*nb));
        if (!nb) return false;
        sw->buffers = nb;
        sw->buf_cap = nc;
    }
    return true;
}

static flux_solid_vertex *sw_alloc_solid(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first)
{
    sw_renderer *sw = self(r);
    if (!ensure_buf_cap(sw)) return nullptr;
    uint32_t idx = sw->buf_count++;
    size_t size = count * sizeof(flux_solid_vertex);
    void *ptr = sw_pool_alloc(sw, size);
    if (!ptr) { sw->buf_count--; return nullptr; }
    sw->buffers[idx] = (sw_buffer){ .offset = (size_t)((uint8_t *)ptr - sw->pool.base), .count = count };
    *buf = (flux_r_buffer *)(uintptr_t)(idx + 1);
    *first = 0;
    return ptr;
}

static flux_image_vertex *sw_alloc_image(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first)
{
    sw_renderer *sw = self(r);
    if (!ensure_buf_cap(sw)) return nullptr;
    uint32_t idx = sw->buf_count++;
    size_t size = count * sizeof(flux_image_vertex);
    void *ptr = sw_pool_alloc(sw, size);
    if (!ptr) { sw->buf_count--; return nullptr; }
    sw->buffers[idx] = (sw_buffer){ .offset = (size_t)((uint8_t *)ptr - sw->pool.base), .count = count };
    *buf = (flux_r_buffer *)(uintptr_t)(idx + 1);
    *first = 0;
    return ptr;
}

static flux_fringe_vertex *sw_alloc_fringe(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first)
{
    sw_renderer *sw = self(r);
    if (!ensure_buf_cap(sw)) return nullptr;
    uint32_t idx = sw->buf_count++;
    size_t size = count * sizeof(flux_fringe_vertex);
    void *ptr = sw_pool_alloc(sw, size);
    if (!ptr) { sw->buf_count--; return nullptr; }
    sw->buffers[idx] = (sw_buffer){ .offset = (size_t)((uint8_t *)ptr - sw->pool.base), .count = count };
    *buf = (flux_r_buffer *)(uintptr_t)(idx + 1);
    *first = 0;
    return ptr;
}

static sw_buffer *resolve_buf(sw_renderer *sw, flux_r_buffer *buf)
{
    uint32_t idx = (uint32_t)(uintptr_t)buf - 1;
    if (idx >= sw->buf_count) return nullptr;
    return &sw->buffers[idx];
}

static uint32_t buffer_index(sw_renderer *sw, flux_r_buffer *buf)
{
    uintptr_t tag = (uintptr_t)buf;
    if (tag > 0 && tag <= sw->buf_count) return (uint32_t)(tag - 1);
    return 0;
}

static void sw_draw_solid(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color)
{
    sw_renderer *sw = self(r);
    uint32_t bi = buffer_index(sw, buf);
    if (sw->batch.active && (sw->batch.color != color || sw->batch.buf_index != bi || sw->batch.blend_mode != sw->blend_mode)) {
        sw_flush_solid(r);
    }
    if (!sw->batch.active) {
        sw->batch.color = color;
        sw->batch.buf_index = bi;
        sw->batch.blend_mode = sw->blend_mode;
        sw->batch.first = first;
        sw->batch.count = count;
        sw->batch.active = true;
    } else {
        sw->batch.count = first + count;
    }
}

static void sw_flush_solid(flux_rhi_device *r)
{
    sw_renderer *sw = self(r);
    if (!sw->batch.active) return;

    uint8_t cr = (uint8_t)((sw->batch.color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((sw->batch.color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(sw->batch.color & 0xFF);
    uint8_t ca = (uint8_t)((sw->batch.color >> 24) & 0xFF);

    /* Draw triangles from the batch buffer, starting at batch.first */
    sw_buffer *b = &sw->buffers[sw->batch.buf_index];
    flux_solid_vertex *solid = buf_solid(sw, b);
    if (solid && b->count > 0) {
        uint32_t end = sw->batch.first + sw->batch.count;
        if (end > b->count) end = (uint32_t)b->count;
        for (uint32_t t = sw->batch.first / 3; t < end / 3; t++) {
            raster_solid(sw,
                         &solid[t * 3 + 0],
                         &solid[t * 3 + 1],
                         &solid[t * 3 + 2],
                         cr, cg, cb, ca,
                         false, false, 0, 0, 0, 0);
        }
    }

    sw->batch.active = false;
}

static void sw_draw_fringe(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color)
{
    sw_renderer *sw = self(r);
    sw_flush_solid(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b) return;
    flux_fringe_vertex *fringe = (flux_fringe_vertex *)(sw->pool.base + b->offset);
    if (!fringe) return;

    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);
    uint8_t ca = (uint8_t)((color >> 24) & 0xFF);

    for (uint32_t i = first; i + 2 < first + count; i += 3) {
        raster_fringe(sw, &fringe[i], &fringe[i + 1], &fringe[i + 2], cr, cg, cb, ca);
    }
}

static void sw_draw_image(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_r_texture *tex, flux_color tint)
{
    sw_renderer *sw = self(r);
    sw_flush_solid(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b) return;
    flux_image_vertex *img = buf_image(sw, b);
    if (!img) return;
    sw_texture *t = (sw_texture *)tex;
    if (!t) return;

    uint8_t tr = (tint >> 16) & 0xFF, tg = (tint >> 8) & 0xFF;
    uint8_t tb = (uint8_t)(tint & 0xFF), ta = (uint8_t)((tint >> 24) & 0xFF);
    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_image(sw, &img[first + i], &img[first + i + 1], &img[first + i + 2],
                     t, tr, tg, tb, ta);
    }
}

static void sw_draw_text(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_r_texture *tex, flux_color color)
{
    sw_renderer *sw = self(r);
    sw_flush_solid(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b) return;
    flux_image_vertex *img = buf_image(sw, b);
    if (!img) return;
    sw_texture *t = (sw_texture *)tex;
    if (!t) return;

    uint8_t cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF;
    uint8_t cb = (uint8_t)(color & 0xFF), ca = (uint8_t)((color >> 24) & 0xFF);

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_image(sw, &img[first + i], &img[first + i + 1], &img[first + i + 2],
                     t, cr, cg, cb, ca);
    }
}

static void sw_draw_gradient(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, const flux_gradient *grad)
{
    sw_renderer *sw = self(r);
    sw_flush_solid(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b) return;
    flux_solid_vertex *solid = buf_solid(sw, b);
    if (!solid) return;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_gradient(sw, &solid[first + i], &solid[first + i + 1], &solid[first + i + 2],
                        grad, false);
    }
}

static void sw_scissor(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    sw_renderer *sw = self(r);
    sw->scissor_x = x;
    sw->scissor_y = y;
    sw->scissor_w = w;
    sw->scissor_h = h;
}

static void sw_stencil_clear(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    sw_renderer *sw = self(r);
    int32_t ex = min_i(x + (int32_t)w, (int32_t)sw->fb.w);
    int32_t ey = min_i(y + (int32_t)h, (int32_t)sw->fb.h);
    for (int32_t yy = y; yy < ey; yy++) {
        memset(sw->fb.stencil + (size_t)yy * sw->fb.w + (size_t)x, 0, (size_t)(ex - x));
    }
}

static void sw_stencil_fill(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, int fill_rule)
{
    sw_renderer *sw = self(r);
    sw->stencil_fill_rule = fill_rule;
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b) return;
    flux_solid_vertex *solid = buf_solid(sw, b);
    if (!solid) return;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_solid(sw, &solid[first + i], &solid[first + i + 1], &solid[first + i + 2],
                     0, 0, 0, 255, true, false, 0, 0, 0, 0);
    }
}

static void sw_blur(flux_rhi_device *r, float sigma)
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
            float acc_r = 0, acc_g = 0, acc_b = 0, acc_a = 0;
            for (int k = -radius; k <= radius; k++) {
                int sx = (int)x + k;
                if (sx < 0) sx = 0;
                if (sx >= (int)w) sx = (int)w - 1;
                uint8_t *sp = row + (size_t)sx * 4;
                float wt = kernel[k + radius];
                acc_r += (float)sp[0] * wt;
                acc_g += (float)sp[1] * wt;
                acc_b += (float)sp[2] * wt;
                acc_a += (float)sp[3] * wt;
            }
            dst_row[x * 4 + 0] = (uint8_t)(acc_r + 0.5f);
            dst_row[x * 4 + 1] = (uint8_t)(acc_g + 0.5f);
            dst_row[x * 4 + 2] = (uint8_t)(acc_b + 0.5f);
            dst_row[x * 4 + 3] = (uint8_t)(acc_a + 0.5f);
        }
    }

    /* Vertical pass: tmp → src */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *dst_row = src + (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            float acc_r = 0, acc_g = 0, acc_b = 0, acc_a = 0;
            for (int k = -radius; k <= radius; k++) {
                int sy = (int)y + k;
                if (sy < 0) sy = 0;
                if (sy >= (int)h) sy = (int)h - 1;
                uint8_t *sp = tmp + (size_t)sy * stride + (size_t)x * 4;
                float wt = kernel[k + radius];
                acc_r += (float)sp[0] * wt;
                acc_g += (float)sp[1] * wt;
                acc_b += (float)sp[2] * wt;
                acc_a += (float)sp[3] * wt;
            }
            dst_row[x * 4 + 0] = (uint8_t)(acc_r + 0.5f);
            dst_row[x * 4 + 1] = (uint8_t)(acc_g + 0.5f);
            dst_row[x * 4 + 2] = (uint8_t)(acc_b + 0.5f);
            dst_row[x * 4 + 3] = (uint8_t)(acc_a + 0.5f);
        }
    }

    free(tmp);
}

static flux_r_texture *sw_surface_texture(flux_rhi_device *r)
{
    /* The software framebuffer IS the texture — wrap it directly. */
    sw_renderer *sw = self(r);
    sw_texture *t = calloc(1, sizeof(*t));
    if (!t) return nullptr;
    t->w = sw->fb.w;
    t->h = sw->fb.h;
    t->stride = sw->fb.stride;
    t->fmt = FLUX_FMT_RGBA8_UNORM;
    t->pixels = malloc((size_t)t->h * t->stride);
    if (!t->pixels) { free(t); return nullptr; }
    memcpy(t->pixels, sw->fb.pixels, (size_t)t->h * t->stride);
    t->next = sw->textures;
    sw->textures = t;
    return (flux_r_texture *)t;
}

static void sw_stencil_ref(flux_rhi_device *r, uint32_t ref)
{
    sw_renderer *sw = self(r);
    sw->stencil_ref = (uint8_t)ref;
}

static void sw_cover_solid(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color)
{
    sw_renderer *sw = self(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b) return;
    flux_solid_vertex *solid = buf_solid(sw, b);
    if (!solid) return;

    uint8_t cr = (uint8_t)((color >> 16) & 0xFF), cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF), ca = (uint8_t)((color >> 24) & 0xFF);

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_solid(sw, &solid[first + i], &solid[first + i + 1], &solid[first + i + 2],
                     0, 0, 0, 0, false, true, cr, cg, cb, ca);
    }
}

static void sw_cover_gradient(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, const flux_gradient *grad)
{
    sw_renderer *sw = self(r);
    sw_buffer *b = resolve_buf(sw, buf);
    if (!b) return;
    flux_solid_vertex *solid = buf_solid(sw, b);
    if (!solid) return;

    for (uint32_t i = 0; i + 2 < count; i += 3) {
        raster_gradient(sw, &solid[first + i], &solid[first + i + 1], &solid[first + i + 2],
                        grad, true);
    }
}

static flux_r_texture *sw_texture_alloc(flux_rhi_device *r, uint32_t w, uint32_t h, flux_pixel_format fmt, const void *data, size_t stride)
{
    sw_renderer *sw = self(r);
    sw_texture *t = calloc(1, sizeof(*t));
    if (!t) return nullptr;

    uint32_t bpp = flux_pixel_format_bytes(fmt);
    if (bpp == 0) { free(t); return nullptr; }
    size_t real_stride = stride > 0 ? stride : (size_t)w * bpp;
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
    return (flux_r_texture *)t;
}

static void sw_texture_free(flux_rhi_device *r, flux_r_texture *tex)
{
    sw_renderer *sw = self(r);
    sw_texture *t = (sw_texture *)tex;
    if (!t) return;

    /* Unlink from sw->textures list */
    sw_texture **pp = &sw->textures;
    while (*pp) {
        if (*pp == t) {
            *pp = t->next;
            break;
        }
        pp = &(*pp)->next;
    }

    free(t->pixels);
    free(t);
}

static void sw_texture_update(flux_rhi_device *r, flux_r_texture *tex, const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    (void)r;
    sw_texture *t = (sw_texture *)tex;
    if (!t || !data) return;
    uint32_t bpp = flux_pixel_format_bytes(t->fmt);
    for (uint32_t row = 0; row < h; row++) {
        const uint8_t *src = (const uint8_t *)data + (size_t)row * w * bpp;
        uint8_t *dst = t->pixels + (size_t)(y + row) * t->stride + (size_t)x * bpp;
        memcpy(dst, src, (size_t)w * bpp);
    }
}
