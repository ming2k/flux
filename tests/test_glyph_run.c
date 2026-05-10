#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__,     \
                    __LINE__);                                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

/* ---------- Basic glyph run lifecycle ---------- */

static bool test_glyph_run_create_destroy(void)
{
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    CHECK(fx_glyph_run_count(run) == 0);
    fx_glyph_run_destroy(run);
    return true;
}

static bool test_glyph_run_create_with_reserve(void)
{
    fx_glyph_run *run = fx_glyph_run_create(100);
    CHECK(run != nullptr);
    CHECK(fx_glyph_run_count(run) == 0);
    /* Append should succeed without reallocation */
    for (int i = 0; i < 100; ++i) {
        CHECK(fx_glyph_run_append(run, (uint32_t)i, (float)i, (float)(i * 2)));
    }
    CHECK(fx_glyph_run_count(run) == 100);
    fx_glyph_run_destroy(run);
    return true;
}

static bool test_glyph_run_append_grow(void)
{
    fx_glyph_run *run = fx_glyph_run_create(1);
    CHECK(run != nullptr);
    /* Force reallocation by appending more than reserved */
    for (int i = 0; i < 20; ++i) {
        CHECK(fx_glyph_run_append(run, (uint32_t)i, (float)i, 0.0f));
    }
    CHECK(fx_glyph_run_count(run) == 20);
    fx_glyph_run_destroy(run);
    return true;
}

/* ---------- Glyph data integrity ---------- */

static bool test_glyph_run_data_integrity(void)
{
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    CHECK(fx_glyph_run_append(run, 42, 1.5f, 2.5f));
    CHECK(fx_glyph_run_append(run, 99, -3.0f, 0.0f));

    const fx_glyph *data = fx_glyph_run_data(run);
    CHECK(data != nullptr);
    CHECK(data[0].glyph_id == 42);
    CHECK(data[0].x == 1.5f);
    CHECK(data[0].y == 2.5f);
    CHECK(data[1].glyph_id == 99);
    CHECK(data[1].x == -3.0f);
    CHECK(data[1].y == 0.0f);

    fx_glyph_run_destroy(run);
    return true;
}

/* ---------- Reset clears count but keeps capacity ---------- */

static bool test_glyph_run_reset(void)
{
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    CHECK(fx_glyph_run_append(run, 1, 0.0f, 0.0f));
    CHECK(fx_glyph_run_append(run, 2, 0.0f, 0.0f));
    CHECK(fx_glyph_run_count(run) == 2);

    fx_glyph_run_reset(run);
    CHECK(fx_glyph_run_count(run) == 0);

    /* Should be able to append again without reallocation (capacity kept) */
    CHECK(fx_glyph_run_append(run, 3, 0.0f, 0.0f));
    CHECK(fx_glyph_run_count(run) == 1);

    fx_glyph_run_destroy(run);
    return true;
}

/* ---------- Multiple reset cycles ---------- */

static bool test_glyph_run_reset_cycles(void)
{
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    for (int cycle = 0; cycle < 5; ++cycle) {
        for (int i = 0; i < 10; ++i) {
            CHECK(fx_glyph_run_append(run, (uint32_t)i, 0.0f, 0.0f));
        }
        CHECK(fx_glyph_run_count(run) == 10);
        fx_glyph_run_reset(run);
    }
    fx_glyph_run_destroy(run);
    return true;
}

/* ---------- Empty run data ---------- */

static bool test_glyph_run_empty_data(void)
{
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    CHECK(fx_glyph_run_data(run) == nullptr);
    fx_glyph_run_destroy(run);
    return true;
}

/* ---------- Large glyph ID ---------- */

static bool test_glyph_run_large_glyph_id(void)
{
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    CHECK(fx_glyph_run_append(run, 0xFFFFFFFFu, 0.0f, 0.0f));
    const fx_glyph *data = fx_glyph_run_data(run);
    CHECK(data[0].glyph_id == 0xFFFFFFFFu);
    fx_glyph_run_destroy(run);
    return true;
}

/* ---------- Null safety ---------- */

static bool test_glyph_run_null_create(void)
{
    /* fx_glyph_run_create(0) should not return nullptr */
    fx_glyph_run *run = fx_glyph_run_create(0);
    CHECK(run != nullptr);
    fx_glyph_run_destroy(run);
    return true;
}

static bool test_glyph_run_null_append(void)
{
    CHECK(!fx_glyph_run_append(nullptr, 0, 0.0f, 0.0f));
    return true;
}

static bool test_glyph_run_null_count(void)
{
    CHECK(fx_glyph_run_count(nullptr) == 0);
    return true;
}

static bool test_glyph_run_null_data(void)
{
    CHECK(fx_glyph_run_data(nullptr) == nullptr);
    return true;
}

static bool test_glyph_run_null_reset(void)
{
    /* Must not crash */
    fx_glyph_run_reset(nullptr);
    return true;
}

static bool test_glyph_run_null_destroy(void)
{
    /* Must not crash */
    fx_glyph_run_destroy(nullptr);
    return true;
}

int main(void)
{
    if (!test_glyph_run_create_destroy()) return 1;
    if (!test_glyph_run_create_with_reserve()) return 1;
    if (!test_glyph_run_append_grow()) return 1;
    if (!test_glyph_run_data_integrity()) return 1;
    if (!test_glyph_run_reset()) return 1;
    if (!test_glyph_run_reset_cycles()) return 1;
    if (!test_glyph_run_empty_data()) return 1;
    if (!test_glyph_run_large_glyph_id()) return 1;
    if (!test_glyph_run_null_create()) return 1;
    if (!test_glyph_run_null_append()) return 1;
    if (!test_glyph_run_null_count()) return 1;
    if (!test_glyph_run_null_data()) return 1;
    if (!test_glyph_run_null_reset()) return 1;
    if (!test_glyph_run_null_destroy()) return 1;
    printf("glyph_run OK\n");
    return 0;
}
