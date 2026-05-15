/*
 * flux — Optional console log helper with ANSI styling.
 *
 * This header provides a ready-made `flux_log_fn` callback that prints
 * colourised, aligned messages to stderr.  It is completely optional; the
 * core library never includes or depends on it.
 *
 * Usage:
 *   flux_context_desc desc = {
 *       .size = sizeof(desc),
 *       .log  = flux_default_console_logger,
 *   };
 *   flux_context_create(&desc, &ctx);
 */

#ifndef FLUX_LOG_HELPER_H
#define FLUX_LOG_HELPER_H

#include "flux/flux.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A drop-in `flux_log_fn` implementation that writes to stderr.
 *
 * Format:
 *   [HH:MM:SS] [LEVEL] file:line: message
 *
 * Styling:
 *   - Timestamp and location are dim.
 *   - LEVEL is colour-coded (green INFO, yellow WARN, red ERROR, etc.).
 *   - The LEVEL tag is padded to exactly 5 characters for alignment.
 *
 * On Windows this enables ENABLE_VIRTUAL_TERMINAL_PROCESSING once so that
 * ANSI escape codes render correctly in modern consoles.
 *
 * Thread-safety: this function itself is thread-safe (it locks stderr).
 * If you share the same callback across multiple contexts you do not need
 * additional synchronization for the output stream.
 */
FLUX_API void flux_default_console_logger(flux_log_level level,
                                          const char *file, int line,
                                          const char *fmt, const char *msg,
                                          void *user);

#ifdef __cplusplus
}
#endif

#endif /* FLUX_LOG_HELPER_H */
