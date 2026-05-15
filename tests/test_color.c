#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    /* Opaque red round-trips. */
    flux_color c = flux_color_rgba(255, 0, 0, 255);
    uint8_t r, g, b, a;
    flux_color_unpack(c, &r, &g, &b, &a);
    CHECK(r == 255 && g == 0 && b == 0 && a == 255);

    /* Premultiply: rgba(200,100,50,128) -> roughly (100,50,25,128) */
    flux_color t = flux_color_rgba(200, 100, 50, 128);
    flux_color_unpack(t, &r, &g, &b, &a);
    CHECK(a == 128);
    /* Round-tripping through premul/unpremul is lossy near 0; allow ±2. */
    CHECK(r > 197 && r < 203);
    CHECK(g > 97  && g < 103);
    CHECK(b > 47  && b < 53);

    /* Fully transparent unpacks to zero RGB. */
    flux_color z = flux_color_rgba(255, 255, 255, 0);
    flux_color_unpack(z, &r, &g, &b, &a);
    CHECK(a == 0 && r == 0 && g == 0 && b == 0);

    /* Premul helper accepts already-premultiplied input as-is. */
    flux_color p = flux_color_rgba_premul(64, 32, 16, 128);
    CHECK(((p >> 24) & 0xFF) == 128);
    CHECK(((p >> 16) & 0xFF) == 64);

    printf("color OK\n");
    return 0;
}
