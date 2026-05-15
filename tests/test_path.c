#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    /* basic build + bounds */
    flux_path *p = NULL;
    CHECK(flux_path_create(ctx, &p) == FLUX_OK);
    CHECK(flux_path_move_to(p, 10, 10) == FLUX_OK);
    CHECK(flux_path_line_to(p, 50, 10) == FLUX_OK);
    CHECK(flux_path_line_to(p, 50, 30) == FLUX_OK);
    CHECK(flux_path_close  (p)         == FLUX_OK);
    CHECK(flux_path_verb_count(p)  == 4);
    CHECK(flux_path_point_count(p) == 3);

    flux_rect b;
    CHECK(flux_path_get_bounds(p, &b) == FLUX_OK);
    CHECK(approx_eq(b.x, 10.0f) && approx_eq(b.y, 10.0f));
    CHECK(approx_eq(b.w, 40.0f) && approx_eq(b.h, 20.0f));

    /* clear + reuse */
    flux_path_clear(p);
    CHECK(flux_path_verb_count(p) == 0);
    CHECK(flux_path_get_bounds(p, &b) == FLUX_ERROR_INVALID_STATE);

    /* convenience: rect */
    CHECK(flux_path_add_rect(p, &(flux_rect){ 0, 0, 100, 80 }) == FLUX_OK);
    CHECK(flux_path_verb_count(p) == 5);  /* MOVE,LINE,LINE,LINE,CLOSE */

    /* convenience: round_rect */
    flux_path *rr = NULL;
    CHECK(flux_path_create(ctx, &rr) == FLUX_OK);
    CHECK(flux_path_add_round_rect(rr, &(flux_rect){ 0, 0, 100, 50 }, 10.0f) == FLUX_OK);
    flux_rect rb;
    CHECK(flux_path_get_bounds(rr, &rb) == FLUX_OK);
    CHECK(approx_eq(rb.w, 100.0f) && approx_eq(rb.h, 50.0f));

    /* convenience: circle */
    flux_path *circle = NULL;
    CHECK(flux_path_create(ctx, &circle) == FLUX_OK);
    CHECK(flux_path_add_circle(circle, 50.0f, 50.0f, 25.0f) == FLUX_OK);
    flux_rect cb;
    CHECK(flux_path_get_bounds(circle, &cb) == FLUX_OK);
    CHECK(approx_eq(cb.w, 50.0f) && approx_eq(cb.h, 50.0f));

    /* transform: returns new path, original untouched */
    flux_matrix t;
    flux_matrix_translation(&t, 5.0f, -3.0f);
    flux_path *moved = NULL;
    CHECK(flux_path_transform(p, &t, &moved) == FLUX_OK);
    flux_rect mb;
    CHECK(flux_path_get_bounds(moved, &mb) == FLUX_OK);
    CHECK(approx_eq(mb.x, 5.0f) && approx_eq(mb.y, -3.0f));
    CHECK(approx_eq(mb.w, 100.0f) && approx_eq(mb.h, 80.0f));

    flux_path_release(moved);
    flux_path_release(circle);
    flux_path_release(rr);
    flux_path_release(p);
    flux_context_release(ctx);
    printf("path OK\n");
    return 0;
}
