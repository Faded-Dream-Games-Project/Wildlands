/* util.h — small shared helpers: math, strings, files, RLE, string builder. */
#ifndef WL_UTIL_H
#define WL_UTIL_H

#include "wl/wl_common.h"

/* --- math --- */
float wl_clampf(float v, float lo, float hi);
int   wl_clampi(int v, int lo, int hi);
float wl_lerpf(float a, float b, float t);

/* --- strings --- */
/* Safe copy: always NUL-terminates, never overflows. */
void  wl_scpy(char *dst, const char *src, size_t cap);
bool  wl_str_eq(const char *a, const char *b);
bool  wl_str_starts_with(const char *s, const char *prefix);
bool  wl_parse_long(const char *s, long *out);
bool  wl_parse_float(const char *s, float *out);
bool  wl_parse_bool(const char *s, bool *out); /* true/false/1/0/yes/no/on/off */

/* --- files / directories --- */
bool  wl_file_exists(const char *path);
bool  wl_is_dir(const char *path);
bool  wl_mkdir_p(const char *path);
/* Reads whole file; *out_data is malloc'd and NUL-terminated (binary-safe for
 * len bytes). Caller frees. Returns false if the file cannot be read. */
bool  wl_read_file(const char *path, char **out_data, size_t *out_len);
/* Writes via "<path>.tmp" + rename so a crash mid-write never corrupts an
 * existing file. */
bool  wl_write_file_atomic(const char *path, const void *data, size_t len);
/* Recursive delete; returns true if the path is gone (or never existed). */
bool  wl_delete_tree(const char *path);

/* --- time --- */
int64_t wl_time_unix(void);
/* "YYYY-MM-DD HH:MM" in local time. */
void  wl_format_datetime(char *buf, size_t cap, int64_t t);

/* --- key=value line parsing (shared by options.txt, manifests, level.dat) ---
 * Skips blank lines and '#' comments, trims whitespace.
 * Returns false for lines that are not key=value. */
bool  wl_kv_line(const char *line, char *key, size_t key_cap,
                 char *val, size_t val_cap);

/* --- run-length codec for chunk block storage ---
 * encode: returns bytes written, or 0 if out_cap is too small.
 * decode: returns bytes produced, or 0 on malformed input / overflow. */
size_t wl_rle_encode(const uint8_t *in, size_t n, uint8_t *out, size_t out_cap);
size_t wl_rle_decode(const uint8_t *in, size_t n, uint8_t *out, size_t out_cap);

/* --- growable string buffer (for building text files in memory) --- */
typedef struct {
    char *buf;    /* NUL-terminated */
    size_t len;
    size_t cap;
} WlStrBuf;

bool  wl_sb_init(WlStrBuf *sb, size_t initial_cap);
void  wl_sb_free(WlStrBuf *sb);
bool  wl_sb_printf(WlStrBuf *sb, const char *fmt, ...);

#endif /* WL_UTIL_H */
