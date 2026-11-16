/* paths.c — root resolution, runtime directories, log pruning. */
#include "wl/paths.h"
#include "wl/util.h"
#include "wl/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static char g_root[WL_PATH_MAX] = {0};
static bool g_initialized = false;

static bool dir_has_assets(const char *dir)
{
    char p[WL_PATH_MAX];
    if (!wl_path_join(p, sizeof(p), dir, "assets")) return false;
    return wl_is_dir(p);
}

/* Walk up from `start` (a directory) up to `levels` parents looking for assets/. */
static bool find_root_upwards(const char *start, int levels)
{
    char dir[WL_PATH_MAX];
    wl_scpy(dir, start, sizeof(dir));
    for (int i = 0; i <= levels; i++) {
        if (dir_has_assets(dir)) {
            wl_scpy(g_root, dir, sizeof(g_root));
            return true;
        }
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) break;
        *slash = '\0';
    }
    return false;
}

bool wl_paths_init(const char *argv0)
{
    g_initialized = false;
    g_root[0] = '\0';

    const char *env = getenv("WILDLANDS_ROOT");
    if (env && *env && dir_has_assets(env)) {
        wl_scpy(g_root, env, sizeof(g_root));
        g_initialized = true;
        return true;
    }

    char cwd[WL_PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) && dir_has_assets(cwd)) {
        wl_scpy(g_root, cwd, sizeof(g_root));
        g_initialized = true;
        return true;
    }

    char exe[WL_PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0 && (size_t)n < sizeof(exe) - 1) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0'; /* strip binary name -> its directory */
            if (find_root_upwards(exe, 4)) {
                g_initialized = true;
                return true;
            }
        }
    }

    if (argv0 && *argv0 && strchr(argv0, '/')) {
        char dir[WL_PATH_MAX];
        wl_scpy(dir, argv0, sizeof(dir));
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            if (dir[0] != '/') {
                /* relative argv[0]: resolve against cwd */
                char abs[WL_PATH_MAX];
                if (getcwd(abs, sizeof(abs))) {
                    char joined[WL_PATH_MAX];
                    if (snprintf(joined, sizeof(joined), "%s/%s", abs, dir) < (int)sizeof(joined) &&
                        find_root_upwards(joined, 4)) {
                        g_initialized = true;
                        return true;
                    }
                }
            } else if (find_root_upwards(dir, 4)) {
                g_initialized = true;
                return true;
            }
        }
    }

    return false;
}

bool wl_paths_ensure_dirs(void)
{
    static const char *dirs[] = { "worlds", "mods", "config", "logs", "screenshots" };
    bool ok = true;
    for (size_t i = 0; i < WL_ARRAY_COUNT(dirs); i++) {
        char p[WL_PATH_MAX];
        if (!wl_path_join(p, sizeof(p), g_root, dirs[i])) { ok = false; continue; }
        if (!wl_mkdir_p(p)) {
            WL_LOGE("cannot create runtime directory: %s", p);
            ok = false;
        }
    }
    return ok;
}

typedef struct {
    char name[256];
    int64_t mtime;
} WlLogFile;

static int log_file_cmp(const void *a, const void *b)
{
    const WlLogFile *la = a, *lb = b;
    if (lb->mtime != la->mtime) return (lb->mtime > la->mtime) ? 1 : -1;
    return strcmp(la->name, lb->name);
}

void wl_paths_prune_logs(int keep)
{
    char dir[WL_PATH_MAX];
    if (!wl_path_logs(dir, sizeof(dir), "")) return;

    DIR *d = opendir(dir);
    if (!d) return;

    WlLogFile files[128];
    size_t count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < WL_ARRAY_COUNT(files)) {
        if (!wl_str_starts_with(e->d_name, "wildlands-")) continue;
        wl_scpy(files[count].name, e->d_name, sizeof(files[count].name));
        files[count].mtime = 0;
        char full[WL_PATH_MAX];
        if (snprintf(full, sizeof(full), "%s/%s", dir, e->d_name) < (int)sizeof(full)) {
            char *data = NULL;
            size_t len = 0;
            /* mtime via a tiny stat instead of reading content */
            struct stat st;
            if (stat(full, &st) == 0) files[count].mtime = (int64_t)st.st_mtime;
            (void)data; (void)len;
        }
        count++;
    }
    closedir(d);

    if (count <= (size_t)keep) return;

    qsort(files, count, sizeof(files[0]), log_file_cmp);
    for (size_t i = (size_t)keep; i < count; i++) {
        char full[WL_PATH_MAX];
        if (snprintf(full, sizeof(full), "%s/%s", dir, files[i].name) < (int)sizeof(full)) {
            if (remove(full) == 0)
                WL_LOGD("pruned old log %s", files[i].name);
        }
    }
}

const char *wl_paths_root(void)
{
    return g_root;
}

bool wl_path_join(char *out, size_t cap, const char *a, const char *b)
{
    if (!out || cap == 0 || !a || !b) return false;
    int n;
    if (*b == '\0' || a[0] == '\0')
        n = snprintf(out, cap, "%s%s", a, b);
    else if (a[strlen(a) - 1] == '/')
        n = snprintf(out, cap, "%s%s", a, (*b == '/') ? b + 1 : b);
    else
        n = snprintf(out, cap, "%s/%s", a, b);
    return n >= 0 && (size_t)n < cap;
}

bool wl_path_assets(char *out, size_t cap, const char *rel)
{
    char a[WL_PATH_MAX];
    return wl_path_join(a, sizeof(a), g_root, "assets") && wl_path_join(out, cap, a, rel);
}

bool wl_path_config(char *out, size_t cap, const char *rel)
{
    char a[WL_PATH_MAX];
    return wl_path_join(a, sizeof(a), g_root, "config") && wl_path_join(out, cap, a, rel);
}

bool wl_path_logs(char *out, size_t cap, const char *rel)
{
    char a[WL_PATH_MAX];
    return wl_path_join(a, sizeof(a), g_root, "logs") && wl_path_join(out, cap, a, rel);
}

bool wl_path_worlds(char *out, size_t cap, const char *rel)
{
    char a[WL_PATH_MAX];
    return wl_path_join(a, sizeof(a), g_root, "worlds") && wl_path_join(out, cap, a, rel);
}

bool wl_path_mods(char *out, size_t cap, const char *rel)
{
    char a[WL_PATH_MAX];
    return wl_path_join(a, sizeof(a), g_root, "mods") && wl_path_join(out, cap, a, rel);
}

bool wl_path_screenshots(char *out, size_t cap, const char *rel)
{
    char a[WL_PATH_MAX];
    return wl_path_join(a, sizeof(a), g_root, "screenshots") && wl_path_join(out, cap, a, rel);
}
