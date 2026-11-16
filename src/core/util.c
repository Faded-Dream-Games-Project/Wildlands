/* util.c — implementations for util.h. */
#include "wl/util.h"
#include "wl/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

float wl_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int wl_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float wl_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

void wl_scpy(char *dst, const char *src, size_t cap)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, cap, "%s", src);
}

bool wl_str_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

bool wl_str_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return false;
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

bool wl_parse_long(const char *s, long *out)
{
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || !end || *end != '\0') return false;
    *out = v;
    return true;
}

bool wl_parse_float(const char *s, float *out)
{
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    float v = strtof(s, &end);
    if (errno != 0 || !end || *end != '\0') return false;
    *out = v;
    return true;
}

bool wl_parse_bool(const char *s, bool *out)
{
    if (!s) return false;
    if (wl_str_eq(s, "true") || wl_str_eq(s, "1") ||
        wl_str_eq(s, "yes") || wl_str_eq(s, "on")) { *out = true; return true; }
    if (wl_str_eq(s, "false") || wl_str_eq(s, "0") ||
        wl_str_eq(s, "no") || wl_str_eq(s, "off")) { *out = false; return true; }
    return false;
}

bool wl_file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool wl_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool wl_mkdir_p(const char *path)
{
    if (!path || !*path) return false;
    char tmp[WL_PATH_MAX];
    wl_scpy(tmp, path, sizeof(tmp));
    size_t len = strlen(tmp);
    if (len == 0) return false;
    if (len >= sizeof(tmp) - 1) return false;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

bool wl_read_file(const char *path, char **out_data, size_t *out_len)
{
    *out_data = NULL;
    *out_len = 0;
    if (!path) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return false; }
    rewind(f);

    char *data = malloc((size_t)size + 1);
    if (!data) { fclose(f); return false; }
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);
    data[size] = '\0';

    *out_data = data;
    *out_len = (size_t)size;
    return true;
}

bool wl_write_file_atomic(const char *path, const void *data, size_t len)
{
    if (!path || !data) return false;

    char tmp[WL_PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;

    FILE *f = fopen(tmp, "wb");
    if (!f) return false;

    bool ok = true;
    if (len > 0 && fwrite(data, 1, len, f) != len) ok = false;
    if (ok && fflush(f) != 0) ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }

    if (rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

bool wl_delete_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT; /* already gone counts as success */

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return false;
        struct dirent *e;
        bool ok = true;
        char child[WL_PATH_MAX];
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) {
                ok = false;
                continue;
            }
            if (!wl_delete_tree(child)) ok = false;
        }
        closedir(d);
        if (rmdir(path) != 0) ok = false;
        return ok;
    }

    return unlink(path) == 0;
}

int64_t wl_time_unix(void)
{
    return (int64_t)time(NULL);
}

void wl_format_datetime(char *buf, size_t cap, int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm_buf;
    struct tm *tm = localtime_r(&tt, &tm_buf);
    if (tm)
        strftime(buf, cap, "%Y-%m-%d %H:%M", tm);
    else
        wl_scpy(buf, "unknown", cap);
}

int wl_util_rss_mb(void)
{
#ifdef __linux__
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return -1;
    long total = 0, resident = 0;
    int got = fscanf(f, "%ld %ld", &total, &resident);
    fclose(f);
    if (got != 2) return -1;
    long page = sysconf(_SC_PAGESIZE);
    return (int)((resident * page) / (1024 * 1024));
#else
    return -1;
#endif
}

bool wl_kv_line(const char *line, char *key, size_t key_cap,
                char *val, size_t val_cap)
{
    if (!line || !key || !val || key_cap == 0 || val_cap == 0) return false;

    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#' || *line == '\n' || *line == '\r') return false;

    const char *eq = strchr(line, '=');
    if (!eq) return false;

    size_t klen = (size_t)(eq - line);
    while (klen > 0 && (line[klen - 1] == ' ' || line[klen - 1] == '\t')) klen--;
    if (klen == 0 || klen >= key_cap) return false;
    memcpy(key, line, klen);
    key[klen] = '\0';

    const char *v = eq + 1;
    while (*v == ' ' || *v == '\t') v++;

    const char *end = v + strlen(v);
    while (end > v && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\n' || end[-1] == '\r'))
        end--;
    size_t vlen = (size_t)(end - v);
    if (vlen >= val_cap) return false;
    memcpy(val, v, vlen);
    val[vlen] = '\0';

    return true;
}

size_t wl_rle_encode(const uint8_t *in, size_t n, uint8_t *out, size_t out_cap)
{
    size_t o = 0;
    size_t i = 0;
    while (i < n) {
        uint8_t v = in[i];
        size_t run = 1;
        while (i + run < n && in[i + run] == v && run < 255) run++;
        if (o + 2 > out_cap) return 0;
        out[o++] = (uint8_t)run;
        out[o++] = v;
        i += run;
    }
    return o;
}

size_t wl_rle_decode(const uint8_t *in, size_t n, uint8_t *out, size_t out_cap)
{
    size_t o = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        size_t run = in[i];
        if (o + run > out_cap) return 0;
        memset(out + o, in[i + 1], run);
        o += run;
    }
    return o;
}

bool wl_sb_init(WlStrBuf *sb, size_t initial_cap)
{
    if (initial_cap < 64) initial_cap = 64;
    sb->buf = malloc(initial_cap);
    if (!sb->buf) return false;
    sb->buf[0] = '\0';
    sb->len = 0;
    sb->cap = initial_cap;
    return true;
}

void wl_sb_free(WlStrBuf *sb)
{
    free(sb->buf);
    sb->buf = NULL;
    sb->len = sb->cap = 0;
}

bool wl_sb_printf(WlStrBuf *sb, const char *fmt, ...)
{
    if (!sb || !sb->buf || !fmt) return false;

    for (int attempt = 0; attempt < 8; attempt++) {
        va_list args;
        va_start(args, fmt);
        int need = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, args);
        va_end(args);
        if (need < 0) return false;
        if ((size_t)need < sb->cap - sb->len) {
            sb->len += (size_t)need;
            return true;
        }
        /* grow: make room for the pending text */
        size_t new_cap = sb->cap * 2;
        while (new_cap < sb->len + (size_t)need + 1) new_cap *= 2;
        char *nb = realloc(sb->buf, new_cap);
        if (!nb) return false;
        sb->buf = nb;
        sb->cap = new_cap;
    }
    return false;
}
