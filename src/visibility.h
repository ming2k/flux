#ifndef FLUX_VISIBILITY_H
#define FLUX_VISIBILITY_H

#if (defined(__GNUC__) || defined(__clang__)) && !defined(_WIN32)
#  define FLUX_INTERNAL __attribute__((visibility("default")))
#else
#  define FLUX_INTERNAL
#endif

#endif
