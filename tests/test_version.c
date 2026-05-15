#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    int M = -1, m = -1, p = -1;
    flux_version(&M, &m, &p);
    CHECK(M == FLUX_VERSION_MAJOR);
    CHECK(m == FLUX_VERSION_MINOR);
    CHECK(p == FLUX_VERSION_PATCH);

    CHECK(flux_version_number() == FLUX_VERSION_NUMBER);

    CHECK(flux_version_check(0, 0, 0));
    CHECK(flux_version_check(FLUX_VERSION_MAJOR, FLUX_VERSION_MINOR, FLUX_VERSION_PATCH));
    CHECK(!flux_version_check(FLUX_VERSION_MAJOR, FLUX_VERSION_MINOR + 1, 0));
    CHECK(!flux_version_check(FLUX_VERSION_MAJOR + 1, 0, 0));
    return 0;
}
