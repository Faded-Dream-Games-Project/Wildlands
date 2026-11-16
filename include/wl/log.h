/* log.h — thread-safe logging to console and a per-session file. */
#ifndef WL_LOG_H
#define WL_LOG_H

typedef enum {
    WL_LOG_DEBUG = 0,
    WL_LOG_INFO  = 1,
    WL_LOG_WARN  = 2,
    WL_LOG_ERROR = 3
} WlLogLevel;

/* Opens the log file (truncating a previous session). Pass NULL to log to
 * console only. The caller is responsible for creating the directory. */
void wl_log_init(const char *file_path);
void wl_log_close(void);

void wl_log_write(WlLogLevel level, const char *file, int line, const char *fmt, ...);

const char *wl_log_level_name(WlLogLevel level);

#define WL_LOGD(...) wl_log_write(WL_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define WL_LOGI(...) wl_log_write(WL_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define WL_LOGW(...) wl_log_write(WL_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define WL_LOGE(...) wl_log_write(WL_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* WL_LOG_H */
