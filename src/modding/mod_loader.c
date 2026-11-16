/* mod_loader.c — manifest parsing, dependency resolution, dlopen loading.
 *
 * Robustness: a broken mod never takes the game down. Every failure path
 * (missing manifest, unknown dependency, wrong api version, failing entry
 * function) is logged, the mod is marked with a status + reason and skipped.
 *
 * Strings a mod passes to the registry are copied into a loader-owned arena
 * so the registries never point into unloaded library memory. */
#include "wl/mods.h"
#include "wl/assets.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/crafting.h"
#include "wl/tiles.h"
#include "wl/paths.h"
#include "wl/util.h"
#include "wl/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>

/* ------------------------------------------------------------------ */
/* string arena (registry-safe copies of mod-provided strings)         */
/* ------------------------------------------------------------------ */

typedef struct StrBlock {
    struct StrBlock *next;
    size_t used;
    char data[8192];
} StrBlock;

static StrBlock *g_strings = NULL;

static const char *dup_str(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    if (n > sizeof(((StrBlock *)0)->data)) {
        WL_LOGW("mod string too long (%d bytes), truncated", (int)n);
        return NULL;
    }
    for (StrBlock *b = g_strings; b; b = b->next) {
        if (b->used + n <= sizeof(b->data)) {
            const char *out = b->data + b->used;
            memcpy(b->data + b->used, s, n);
            b->used += n;
            return out;
        }
    }
    StrBlock *b = malloc(sizeof(StrBlock));
    if (!b) return NULL;
    b->next = g_strings;
    b->used = 0;
    g_strings = b;
    memcpy(b->data, s, n);
    b->used = n;
    return b->data;
}

static void strings_free(void)
{
    while (g_strings) {
        StrBlock *n = g_strings->next;
        free(g_strings);
        g_strings = n;
    }
}

/* ------------------------------------------------------------------ */
/* api vtable implementation                                           */
/* ------------------------------------------------------------------ */

static WlAssets *g_assets = NULL;
static char g_cur_mod_dir[WL_PATH_MAX] = "";

static void api_log(WlLogLevel lvl, const char *fmt, va_list args)
{
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    wl_log_write(lvl, "mod", 0, "%s", buf);
}

static void api_log_info(const char *fmt, ...)
{
    va_list a; va_start(a, fmt); api_log(WL_LOG_INFO, fmt, a); va_end(a);
}
static void api_log_warn(const char *fmt, ...)
{
    va_list a; va_start(a, fmt); api_log(WL_LOG_WARN, fmt, a); va_end(a);
}
static void api_log_error(const char *fmt, ...)
{
    va_list a; va_start(a, fmt); api_log(WL_LOG_ERROR, fmt, a); va_end(a);
}

static int api_register_texture(const char *png_path)
{
    if (!g_assets) return -1;
    return wl_assets_add_tile(g_assets, png_path, g_cur_mod_dir);
}

static int api_register_block(const WlModBlockDef *def)
{
    if (!def || !def->name || !def->display || !def->texture) {
        api_log_error("register_block: name/display/texture are required");
        return 0;
    }
    int tile = api_register_texture(def->texture);
    if (tile < 0) {
        api_log_error("register_block('%s'): texture '%s' failed to load",
                      def->name, def->texture);
        return 0;
    }

    WlBlock b;
    memset(&b, 0, sizeof(b));
    b.name = dup_str(def->name);
    b.display = dup_str(def->display);
    if (!b.name || !b.display) return 0;
    for (int i = 0; i < 6; i++) b.tex[i] = (uint8_t)tile;
    b.shape = WL_RENDER_SOLID;
    b.solid = def->solid;
    b.opaque = def->opaque;
    b.unbreakable = def->unbreakable;
    b.hardness = (uint8_t)(def->hardness < 0 ? 0 : (def->hardness > 255 ? 255 : def->hardness));
    b.tool = (WlToolKind)def->tool_kind;
    b.tool_tier = (uint8_t)def->tool_tier;
    b.needs_tool = def->needs_tool;
    b.drop_count = (uint8_t)(def->drop_count > 0 ? def->drop_count : 1);
    b.gravity = def->gravity;

    WlBlockId id = wl_blocks_register(&b);
    if (id == 0) return 0;

    /* drop item: explicit id, or the block's own mirrored item */
    uint16_t drop = def->drop_item_id > 0 ? (WlItemId)def->drop_item_id : (WlItemId)id;
    wl_block_set_drop(id, drop);

    /* every modded block gets a placeable item with the same id */
    WlItem it;
    memset(&it, 0, sizeof(it));
    it.name = b.name;
    it.display = b.display;
    it.is_block = true;
    it.block_id = id;
    it.max_stack = 64;
    if (!wl_items_register_at(id, &it))
        WL_LOGW("mod block '%s': item mirror slot %d unavailable", def->name, (int)id);
    return id;
}

static int api_register_item(const WlModItemDef *def)
{
    if (!def || !def->name || !def->display || !def->texture) {
        api_log_error("register_item: name/display/texture are required");
        return -1;
    }
    int tile = api_register_texture(def->texture);
    if (tile < 0) return -1;

    WlItem it;
    memset(&it, 0, sizeof(it));
    it.name = dup_str(def->name);
    it.display = dup_str(def->display);
    if (!it.name || !it.display) return -1;
    it.tile = (uint16_t)tile;
    it.max_stack = def->max_stack > 0 ? (uint8_t)def->max_stack : 64;
    it.tool = (WlToolKind)def->tool_kind;
    it.tool_tier = (uint8_t)def->tool_tier;
    it.durability = (uint16_t)def->durability;
    it.food = (uint8_t)(def->food > 0 ? def->food : 0);

    WlItemId id = wl_items_register(&it);
    return id == WL_ITEM_NONE ? -1 : (int)id;
}

static bool api_register_recipe(const WlModRecipeDef *def)
{
    if (!def) return false;
    WlRecipe r;
    memset(&r, 0, sizeof(r));
    r.out_item = (uint16_t)def->out_item_id;
    r.out_count = (uint8_t)(def->out_count > 0 ? def->out_count : 1);
    r.shapeless = def->shapeless;
    if (!r.shapeless) {
        r.w = def->w;
        r.h = def->h;
        if (r.w < 1 || r.w > 3 || r.h < 1 || r.h > 3) {
            api_log_error("register_recipe: invalid size %dx%d", def->w, def->h);
            return false;
        }
        for (int i = 0; i < 9; i++) r.pattern[i] = (uint16_t)def->pattern[i];
    } else {
        r.n_ing = def->n_ing;
        if (r.n_ing < 1 || r.n_ing > 9) {
            api_log_error("register_recipe: invalid ingredient count %d", def->n_ing);
            return false;
        }
        for (int i = 0; i < 9; i++) r.ingredients[i] = (uint16_t)def->ingredients[i];
    }
    return wl_crafting_register(&r) >= 0;
}

/* ------------------------------------------------------------------ */
/* manifest                                                            */
/* ------------------------------------------------------------------ */

static bool parse_manifest(const char *dir, WlMod *mod)
{
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "mod.wl")) return false;

    char *data = NULL;
    size_t len = 0;
    if (!wl_read_file(path, &data, &len)) return false;

    char key[64], val[256];
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!wl_kv_line(line, key, sizeof(key), val, sizeof(val))) continue;
        if (wl_str_eq(key, "id")) wl_scpy(mod->id, val, sizeof(mod->id));
        else if (wl_str_eq(key, "name")) wl_scpy(mod->name, val, sizeof(mod->name));
        else if (wl_str_eq(key, "version")) wl_scpy(mod->version, val, sizeof(mod->version));
        else if (wl_str_eq(key, "author")) wl_scpy(mod->author, val, sizeof(mod->author));
        else if (wl_str_eq(key, "description")) wl_scpy(mod->description, val, sizeof(mod->description));
        else if (wl_str_eq(key, "library")) wl_scpy(mod->library, val, sizeof(mod->library));
        else if (wl_str_eq(key, "depends")) wl_scpy(mod->depends, val, sizeof(mod->depends));
        else if (wl_str_eq(key, "api")) {
            long v;
            if (wl_parse_long(val, &v)) mod->api_version_seen = (int)v;
        } else if (wl_str_eq(key, "enabled")) {
            wl_parse_bool(val, &mod->enabled);
        }
    }
    free(data);

    if (mod->id[0] == '\0' || mod->name[0] == '\0' || mod->library[0] == '\0') {
        wl_scpy(mod->error, "manifest requires id, name and library", sizeof(mod->error));
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* loading                                                             */
/* ------------------------------------------------------------------ */

static bool load_mod(WlMods *m, WlMod *mod)
{
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), m->root, mod->dir)) return false;
    char lib[WL_PATH_MAX];
    if (!wl_path_join(lib, sizeof(lib), path, mod->library)) return false;

    if (mod->api_version_seen != WL_API_VERSION) {
        snprintf(mod->error, sizeof(mod->error),
                 "api version mismatch (mod built for %d, game has %d)",
                 mod->api_version_seen, WL_API_VERSION);
        return false;
    }
    if (!wl_file_exists(lib)) {
        snprintf(mod->error, sizeof(mod->error), "library not found: %s", mod->library);
        return false;
    }

    void *handle = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char *err = dlerror();
        snprintf(mod->error, sizeof(mod->error), "dlopen failed: %s",
                 err ? err : "?");
        return false;
    }

    WlModEntryFn entry = (WlModEntryFn)dlsym(handle, "wl_mod_main");
    if (!entry) {
        snprintf(mod->error, sizeof(mod->error), "missing wl_mod_main export");
        dlclose(handle);
        return false;
    }

    wl_scpy(g_cur_mod_dir, path, sizeof(g_cur_mod_dir));
    WlModInfo info;
    memset(&info, 0, sizeof(info));
    int rc = entry(&m->api, &info);
    g_cur_mod_dir[0] = '\0';

    if (rc != 0) {
        snprintf(mod->error, sizeof(mod->error), "wl_mod_main returned %d", rc);
        dlclose(handle);
        return false;
    }
    if (info.api_version != WL_API_VERSION) {
        snprintf(mod->error, sizeof(mod->error),
                 "mod reported api version %d", (int)info.api_version);
        dlclose(handle);
        return false;
    }

    mod->handle = handle;
    WL_LOGI("mod loaded: %s %s by %s", mod->name, mod->version, mod->author);
    return true;
}

/* does the mod list contain id? */
static bool mod_exists(const WlMods *m, const char *id)
{
    for (int i = 0; i < m->count; i++)
        if (wl_str_eq(m->mods[i].id, id)) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

const char *wl_mod_status_name(WlModStatus s)
{
    switch (s) {
    case WL_MOD_LOADED: return "Loaded";
    case WL_MOD_DISABLED: return "Disabled";
    case WL_MOD_ERROR: return "Failed";
    case WL_MOD_MISSING_DEP: return "Missing dependency";
    case WL_MOD_BAD_MANIFEST: return "Bad manifest";
    case WL_MOD_DUPLICATE: return "Duplicate id";
    default: return "Unknown";
    }
}

int wl_mods_loaded_count(const WlMods *m)
{
    int n = 0;
    for (int i = 0; i < m->count; i++)
        if (m->mods[i].status == WL_MOD_LOADED) n++;
    return n;
}

bool wl_mods_init(WlMods *m, WlAssets *assets, const char *root_dir)
{
    memset(m, 0, sizeof(*m));
    m->assets = assets;
    g_assets = assets;
    wl_scpy(m->root, root_dir, sizeof(m->root));

    m->api.api_version = WL_API_VERSION;
    m->api.log_info = api_log_info;
    m->api.log_warn = api_log_warn;
    m->api.log_error = api_log_error;
    m->api.register_block = api_register_block;
    m->api.register_item = api_register_item;
    m->api.register_texture = api_register_texture;
    m->api.register_recipe = api_register_recipe;

    /* scan mods/ */
    DIR *d = opendir(m->root);
    if (!d) {
        WL_LOGW("mods directory missing: %s", m->root);
        return false;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && m->count < WL_MODS_MAX) {
        if (e->d_name[0] == '.') continue;
        char dir[WL_PATH_MAX];
        if (!wl_path_join(dir, sizeof(dir), m->root, e->d_name)) continue;
        if (!wl_is_dir(dir)) continue;
        char manifest[WL_PATH_MAX];
        if (!wl_path_join(manifest, sizeof(manifest), dir, "mod.wl")) continue;
        if (!wl_file_exists(manifest)) continue;

        WlMod *mod = &m->mods[m->count];
        memset(mod, 0, sizeof(*mod));
        wl_scpy(mod->dir, e->d_name, sizeof(mod->dir));
        mod->enabled = true;
        mod->status = WL_MOD_BAD_MANIFEST;
        mod->api_version_seen = -1;
        wl_scpy(mod->error, "manifest unreadable", sizeof(mod->error));

        if (parse_manifest(dir, mod)) {
            /* duplicate id? */
            bool dup = false;
            for (int i = 0; i < m->count; i++)
                if (wl_str_eq(m->mods[i].id, mod->id)) dup = true;
            if (dup) {
                char dup_id[64];
                wl_scpy(dup_id, mod->id, sizeof(dup_id));
                mod->status = WL_MOD_DUPLICATE;
                snprintf(mod->error, sizeof(mod->error),
                         "another mod already uses id '%s'", dup_id);
            } else {
                mod->status = WL_MOD_ERROR;
                wl_scpy(mod->error, "not loaded yet", sizeof(mod->error));
            }
        }
        m->count++;
    }
    closedir(d);

    /* dependency resolution + loading (repeat until no progress) */
    bool done[WL_MODS_MAX] = { false };
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < m->count; i++) {
            if (done[i]) continue;
            WlMod *mod = &m->mods[i];

            if (!mod->enabled) {
                mod->status = WL_MOD_DISABLED;
                done[i] = true;
                progress = true;
                continue;
            }
            if (mod->status == WL_MOD_BAD_MANIFEST || mod->status == WL_MOD_DUPLICATE) {
                done[i] = true; /* permanent failure */
                progress = true;
                continue;
            }

            /* check dependencies */
            char deps[192];
            wl_scpy(deps, mod->depends, sizeof(deps));
            char *save = NULL;
            bool blocked = false, missing = false;
            for (char *tok = strtok_r(deps, ",", &save); tok;
                 tok = strtok_r(NULL, ",", &save)) {
                while (*tok == ' ') tok++;
                if (*tok == '\0') continue;
                char dep_id[64];
                wl_scpy(dep_id, tok, sizeof(dep_id));
                if (!mod_exists(m, dep_id)) {
                    missing = true;
                    snprintf(mod->error, sizeof(mod->error),
                             "depends on '%s' which is not installed", dep_id);
                    break;
                }
                /* installed: is it loaded yet? */
                bool loaded = false;
                for (int j = 0; j < m->count; j++) {
                    if (wl_str_eq(m->mods[j].id, tok) &&
                        m->mods[j].status == WL_MOD_LOADED) {
                        loaded = true;
                        break;
                    }
                }
                if (!loaded) blocked = true;
            }

            if (missing) {
                mod->status = WL_MOD_MISSING_DEP;
                done[i] = true;
                progress = true;
                continue;
            }
            if (blocked) continue; /* try again next pass */

            if (load_mod(m, mod)) {
                mod->status = WL_MOD_LOADED;
                mod->error[0] = '\0';
            } else {
                mod->status = WL_MOD_ERROR;
                WL_LOGW("mod '%s' failed to load: %s", mod->name, mod->error);
            }
            done[i] = true;
            progress = true;
        }
    }

    /* anything left is a dependency cycle */
    for (int i = 0; i < m->count; i++) {
        if (!done[i]) {
            m->mods[i].status = WL_MOD_MISSING_DEP;
            wl_scpy(m->mods[i].error, "dependency cycle detected",
                    sizeof(m->mods[i].error));
        }
    }

    int loaded = wl_mods_loaded_count(m);
    WL_LOGI("mods: %d found, %d loaded, %d skipped", m->count, loaded,
            m->count - loaded);
    return true;
}

bool wl_mods_set_enabled(WlMods *m, int index, bool enabled)
{
    if (index < 0 || index >= m->count) return false;
    WlMod *mod = &m->mods[index];

    char dir[WL_PATH_MAX];
    if (!wl_path_join(dir, sizeof(dir), m->root, mod->dir)) return false;
    char path[WL_PATH_MAX];
    if (!wl_path_join(path, sizeof(path), dir, "mod.wl")) return false;

    char *data = NULL;
    size_t len = 0;
    if (!wl_read_file(path, &data, &len)) return false;

    /* rewrite the enabled line, keep everything else */
    WlStrBuf sb;
    if (!wl_sb_init(&sb, 1024)) { free(data); return false; }
    char *save = NULL;
    bool found = false;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char key[64], val[256];
        if (wl_kv_line(line, key, sizeof(key), val, sizeof(val)) &&
            wl_str_eq(key, "enabled")) {
            wl_sb_printf(&sb, "enabled = %s\r\n", enabled ? "true" : "false");
            found = true;
        } else {
            wl_sb_printf(&sb, "%s\r\n", line);
        }
    }
    if (!found) wl_sb_printf(&sb, "enabled = %s\r\n", enabled ? "true" : "false");
    free(data);

    bool ok = wl_write_file_atomic(path, sb.buf, sb.len);
    wl_sb_free(&sb);
    if (ok) mod->enabled = enabled;
    return ok;
}

void wl_mods_shutdown(WlMods *m)
{
    if (!m) return;
    for (int i = 0; i < m->count; i++) {
        if (m->mods[i].handle) {
            dlclose(m->mods[i].handle);
            m->mods[i].handle = NULL;
        }
    }
    m->count = 0;
    strings_free();
}
