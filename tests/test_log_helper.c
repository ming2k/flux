/*
 * Unit tests for flux_default_console_logger.
 *
 * We redirect stderr to a temporary file, emit one message per level,
 * and verify that each line contains the expected tag and text.
 */

#include "flux/flux.h"
#include "flux/flux_log_helper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <io.h>
#  define dup2 _dup2
#  define dup  _dup
#else
#  include <unistd.h>
#endif

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (buf) {
        fread(buf, 1, (size_t)len, f);
        buf[len] = '\0';
    }
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static bool line_contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

int main(void)
{
    const char *tmp_path = "test_log_helper_stderr.tmp";

    /* Redirect stderr to a file. */
    FILE *tmp = fopen(tmp_path, "w");
    assert(tmp);
    int orig_stderr = dup(fileno(stderr));
    fflush(stderr);
    dup2(fileno(tmp), fileno(stderr));
    fclose(tmp);

    /* Emit messages at every level. */
    flux_default_console_logger(FLUX_LOG_TRACE, "test_log_helper.c", __LINE__,
                                "%s", "trace-message", NULL);
    flux_default_console_logger(FLUX_LOG_DEBUG, "test_log_helper.c", __LINE__,
                                "%s", "debug-message", NULL);
    flux_default_console_logger(FLUX_LOG_INFO,  "test_log_helper.c", __LINE__,
                                "%s", "info-message", NULL);
    flux_default_console_logger(FLUX_LOG_WARN,  "test_log_helper.c", __LINE__,
                                "%s", "warn-message", NULL);
    flux_default_console_logger(FLUX_LOG_ERROR, "test_log_helper.c", __LINE__,
                                "%s", "error-message", NULL);

    /* Also test with a path in the file string. */
    flux_default_console_logger(FLUX_LOG_INFO, "src/resource/context.c", 42,
                                "%s", "path-strip-message", NULL);

    /* Restore stderr. */
    fflush(stderr);
    dup2(orig_stderr, fileno(stderr));
    close(orig_stderr);

    /* Read back and validate. */
    size_t len = 0;
    char *buf = read_file(tmp_path, &len);
    assert(buf);

    /* Each line should contain the expected 5-char tag. */
    assert(line_contains(buf, "[TRACE]"));
    assert(line_contains(buf, "trace-message"));
    assert(line_contains(buf, "[DEBUG]"));
    assert(line_contains(buf, "debug-message"));
    assert(line_contains(buf, "[INFO ]"));   /* padded to 5 chars */
    assert(line_contains(buf, "info-message"));
    assert(line_contains(buf, "[WARN ]"));   /* padded to 5 chars */
    assert(line_contains(buf, "warn-message"));
    assert(line_contains(buf, "[ERROR]"));
    assert(line_contains(buf, "error-message"));

    /* Path should be stripped to basename. */
    assert(line_contains(buf, "context.c:42:"));
    assert(!line_contains(buf, "src/resource/context.c:42:"));

    free(buf);
    remove(tmp_path);

    return 0;
}
