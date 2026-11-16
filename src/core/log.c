/* log.c — console + file logging, safe to call from any thread. */
#include "wl/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define WL_LOG_MSG_MAX 1024

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *wl_log_level_name(WlLogLevel level)
{
    switch (level) {
    case WL_LOG_DEBUG: return "DEBUG";
    case WL_LOG_INFO:  return "INFO";
    case WL_LOG_WARN:  return "WARN";
    case WL_LOG_ERROR: return "ERROR";
    }
    return "?";
}

void wl_log_init(const char *file_path)
{
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    if (file_path) {
        g_log_file = fopen(file_path, "w");
        if (!g_log_file)
            fprintf(stderr, "[Wildlands] could not open log file: %s\n", file_path);
    }
}

void wl_log_close(void)
{
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void wl_log_write(WlLogLevel level, const char *file, int line, const char *fmt, ...)
{
    char msg[WL_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;

    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm = localtime_r(&now, &tm_buf);
    char stamp[16];
    if (tm)
        strftime(stamp, sizeof(stamp), "%H:%M:%S", tm);
    else
        snprintf(stamp, sizeof(stamp), "??:??:??");

    pthread_mutex_lock(&g_log_mutex);

    FILE *con = (level >= WL_LOG_WARN) ? stderr : stdout;
    fprintf(con, "[%s] [%-5s] %s\n", stamp, wl_log_level_name(level), msg);

    if (g_log_file) {
        fprintf(g_log_file, "[%s] [%-5s] [%s:%d] %s\n",
                stamp, wl_log_level_name(level), base, line, msg);
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}
