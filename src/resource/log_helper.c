/*
 * Optional console log helper — colourised stderr output.
 *
 * This is a standalone translation unit.  The core library does not link
 * it automatically; consumers who want the styled sink can set
 * `desc.log = flux_default_console_logger` at context creation time.
 */

#include "flux/flux_log_helper.h"

#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
#endif

/* ------------------------------------------------------------------ */
/*  ANSI helpers                                                      */
/* ------------------------------------------------------------------ */

#define ANSI_RESET   "\x1b[0m"
#define ANSI_DIM     "\x1b[2m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_GRAY    "\x1b[90m"

static const char *level_tag(flux_log_level lvl)
{
    switch (lvl) {
    case FLUX_LOG_TRACE: return "TRACE";
    case FLUX_LOG_DEBUG: return "DEBUG";
    case FLUX_LOG_INFO:  return "INFO ";   /* pad to 5 chars */
    case FLUX_LOG_WARN:  return "WARN ";   /* pad to 5 chars */
    case FLUX_LOG_ERROR: return "ERROR";
    }
    return "?????";
}

static const char *level_color(flux_log_level lvl)
{
    switch (lvl) {
    case FLUX_LOG_TRACE: return ANSI_GRAY;
    case FLUX_LOG_DEBUG: return ANSI_CYAN;
    case FLUX_LOG_INFO:  return ANSI_GREEN;
    case FLUX_LOG_WARN:  return ANSI_YELLOW;
    case FLUX_LOG_ERROR: return ANSI_RED;
    }
    return ANSI_RESET;
}

/* ------------------------------------------------------------------ */
/*  Windows VT support                                                */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)
static void enable_vt_mode(void)
{
    static bool done = false;
    if (done) return;
    done = true;

    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(h, mode);
}
#else
#  define enable_vt_mode() ((void)0)
#endif

/* ------------------------------------------------------------------ */
/*  Public callback                                                   */
/* ------------------------------------------------------------------ */

void flux_default_console_logger(flux_log_level level,
                                 const char *file, int line,
                                 const char *fmt, const char *msg,
                                 void *user)
{
    (void)fmt;
    (void)user;

    enable_vt_mode();

    char ts[32] = "";
    time_t now = time(NULL);
    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

    /* Strip directory prefix from file for brevity. */
    const char *file_name = file ? file : "?";
    for (const char *p = file_name; *p; ++p) {
#if defined(_WIN32)
        if (*p == '\\' || *p == '/')
#else
        if (*p == '/')
#endif
            file_name = p + 1;
    }

    flockfile(stderr);
    fprintf(stderr,
            ANSI_DIM "[%s]" ANSI_RESET " %s[%s]" ANSI_RESET " "
            ANSI_DIM "%s:%d:" ANSI_RESET " %s\n",
            ts,
            level_color(level), level_tag(level),
            file_name, line,
            msg ? msg : "");
    funlockfile(stderr);
}
