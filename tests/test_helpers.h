/* Tiny test harness shared by all test_*.c files. */
#ifndef FLUX_TEST_HELPERS_H
#define FLUX_TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n",                   \
                    #cond, __FILE__, __LINE__);                             \
            return 1;                                                       \
        }                                                                   \
    } while (0)

#define EPS 1e-5f

static inline bool approx_eq(float a, float b) { return fabsf(a - b) < EPS; }

#endif
