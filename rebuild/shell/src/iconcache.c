/* iconcache.c - see iconcache.h. */
#include "iconcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Exported by the toolkit's tk_image.c (stb_image backend); not in
 * the public tk.h, declared here for the default decoder. */
extern bool tk_image_load_rgba(const char *path, uint32_t **out_px,
                               int *w, int *h);

static bool default_decode(void *ud, const char *path,
                           uint32_t **out_px, int *out_w, int *out_h)
{
    (void)ud;
    return tk_image_load_rgba(path, out_px, out_w, out_h);
}

typedef struct ic_entry {
    char *path;
    int tw, th;
    uint32_t *px;
    size_t bytes;
    unsigned long last_use;
    struct ic_entry *next;
} ic_entry;

struct ic_cache {
    size_t budget;
    size_t bytes;
    ic_entry *head; /* most-recently-used first */
    ic_decode_fn decode;
    void *decode_ud;
    unsigned long tick;
    size_t hits, misses, evictions;
    uint32_t *transient; /* oversized items live here, uncached */
};

ic_cache *ic_cache_new(size_t budget_bytes)
{
    ic_cache *c = calloc(1, sizeof(*c));
    c->budget = budget_bytes;
    c->decode = default_decode;
    return c;
}

static void entry_free(ic_entry *e)
{
    free(e->path);
    free(e->px);
    free(e);
}

void ic_cache_free(ic_cache *c)
{
    if (!c) return;
    ic_entry *e = c->head;
    while (e) { ic_entry *n = e->next; entry_free(e); e = n; }
    free(c->transient);
    free(c);
}

void ic_cache_set_decoder(ic_cache *c, ic_decode_fn fn, void *userdata)
{
    c->decode = fn ? fn : default_decode;
    c->decode_ud = userdata;
}

static ic_entry *find_entry(ic_cache *c, const char *path, int tw, int th)
{
    ic_entry *prev = NULL, *e = c->head;
    while (e) {
        if (e->tw == tw && e->th == th && strcmp(e->path, path) == 0) {
            if (prev) { /* move to front (MRU) */
                prev->next = e->next;
                e->next = c->head;
                c->head = e;
            }
            e->last_use = ++c->tick;
            return e;
        }
        prev = e;
        e = e->next;
    }
    return NULL;
}

static void evict_lru(ic_cache *c)
{
    /* list is MRU-first, so the tail is the LRU entry */
    ic_entry *prev = NULL, *e = c->head;
    if (!e) return;
    while (e->next) { prev = e; e = e->next; }
    if (prev) prev->next = NULL;
    else c->head = NULL;
    c->bytes -= e->bytes;
    c->evictions++;
    entry_free(e);
}

const uint32_t *ic_get(ic_cache *c, const char *path,
                       int target_w, int target_h,
                       int *out_w, int *out_h)
{
    if (!c || !path || target_w <= 0 || target_h <= 0) return NULL;
    ic_entry *hit = find_entry(c, path, target_w, target_h);
    if (hit) {
        c->hits++;
        if (out_w) *out_w = hit->tw;
        if (out_h) *out_h = hit->th;
        return hit->px;
    }
    c->misses++;

    uint32_t *src = NULL;
    int sw = 0, sh = 0;
    if (!c->decode(c->decode_ud, path, &src, &sw, &sh) || !src)
        return NULL;

    size_t bytes = (size_t)target_w * (size_t)target_h * sizeof(uint32_t);
    uint32_t *scaled = malloc(bytes);
    if (!scaled) { free(src); return NULL; }
    /* nearest-neighbour, identical mapping to ImageView painting */
    for (int dy = 0; dy < target_h; dy++) {
        int sy = dy * sh / target_h;
        for (int dx = 0; dx < target_w; dx++) {
            int sx = dx * sw / target_w;
            scaled[(size_t)dy * target_w + dx] = src[(size_t)sy * sw + sx];
        }
    }
    free(src);

    if (bytes > c->budget) {
        /* transient slot: returned but not cached */
        free(c->transient);
        c->transient = scaled;
        if (out_w) *out_w = target_w;
        if (out_h) *out_h = target_h;
        return c->transient;
    }
    while (c->bytes + bytes > c->budget && c->head)
        evict_lru(c);

    ic_entry *e = calloc(1, sizeof(*e));
    e->path = strdup(path);
    e->tw = target_w;
    e->th = target_h;
    e->px = scaled;
    e->bytes = bytes;
    e->last_use = ++c->tick;
    e->next = c->head;
    c->head = e;
    c->bytes += bytes;
    if (out_w) *out_w = target_w;
    if (out_h) *out_h = target_h;
    return e->px;
}

void ic_cache_invalidate(ic_cache *c, const char *path)
{
    ic_entry *prev = NULL, *e = c->head;
    while (e) {
        if (strcmp(e->path, path) == 0) {
            ic_entry *dead = e;
            e = e->next;
            if (prev) prev->next = e;
            else c->head = e;
            c->bytes -= dead->bytes;
            entry_free(dead);
            continue;
        }
        prev = e;
        e = e->next;
    }
}

/* ---------------- icon name resolution (M4) ---------------- */

/* freedesktop icon name -> stem in the bundled Papirus-derived
 * set (assets/app/, see its NOTICE). Only names the rebuild's own
 * apps and common desktop entries use; unknown names still resolve
 * when a real theme provides them under their own name. */
static const struct { const char *name, *stem; } ic_aliases[] = {
    { "accessories-calculator", "calc" },
    { "bd-calc", "calc" },
    { "multimedia-player", "player" },
    { "bd-player", "player" },
    { "media-record", "recorder" },
    { "bd-recorder", "recorder" },
    { "preferences-system", "settings" },
    { "preferences", "settings" },
    { "bd-settings", "settings" },
    { "system-software-install", "pkg" },
    { "bd-pkg", "pkg" },
    { "utilities-system-monitor", "devices" },
    { "bd-admin", "devices" },
    { "bd-files", "files" },
    { "system-file-manager", "files" },
    { "pcmanfm", "files" },
    { "bd-notes", "notes" },
    { "text-editor", "notes" },
    { "writer", "notes" },
    { "bd-tasks", "tasks" },
    { "bd-clock", "clock" },
    { "utilities-terminal", "terminal" },
    { "lxterminal", "terminal" },
    { "web-browser", "web" },
    { "netsurf", "web" },
    { "user-trash", "trash" },
    { "folder", "files" },
    { "puzzle", "pkg" },
    { "link", "web" },
    { "myeditor", "notes" },
    { NULL, NULL }
};

static bool ic_try(char *out, size_t out_size, const char *dir,
                   const char *stem)
{
    if (!dir || !*dir) return false;
    int n = snprintf(out, out_size, "%s/%s.png", dir, stem);
    if (n < 0 || (size_t)n >= out_size) return false;
    return access(out, F_OK) == 0;
}

bool ic_resolve_icon_path(const char *icon, char *out, size_t out_size)
{
    if (!icon || !*icon || !out || out_size == 0) return false;
    if (strchr(icon, '/')) {
        if (access(icon, F_OK) == 0) {
            snprintf(out, out_size, "%s", icon);
            return true;
        }
        return false;
    }
    /* stem: the name without a trailing image extension */
    char stem[128];
    snprintf(stem, sizeof(stem), "%s", icon);
    size_t len = strlen(stem);
    if (len > 4) {
        const char *ext = stem + len - 4;
        if (strcmp(ext, ".png") == 0 || strcmp(ext, ".xpm") == 0 ||
            strcmp(ext, ".svg") == 0)
            stem[len - 4] = 0;
    }
    const char *env = getenv("BD_ICON_DIR");
    char home_dir[512] = "";
    const char *home = getenv("HOME");
    if (home && *home)
        snprintf(home_dir, sizeof(home_dir), "%s/.local/share/icons/brainux/app",
                 home);
    /* NOTE: no NULL terminator logic - the first slot is the
     * BD_ICON_DIR env value, which is NULL when unset, so the
     * loop below iterates by count and skips empty slots. */
    const char *dirs[] = {
        env,
        "assets/app",
        "../assets/app",
        "/usr/share/icons/brainux/app",
        home_dir,
        "/usr/share/icons/hicolor/48x48/apps",
        "/usr/share/icons/Papirus/48x48/apps",
        "/usr/share/icons/Papirus/32x32/apps",
        "/usr/share/pixmaps",
    };
    for (int pass = 0; pass < 2; pass++) {
        const char *want = stem;
        if (pass == 1) {
            want = NULL;
            for (int i = 0; ic_aliases[i].name; i++)
                if (strcmp(ic_aliases[i].name, stem) == 0) {
                    want = ic_aliases[i].stem;
                    break;
                }
            if (!want) break;
        }
        for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++)
            if (ic_try(out, out_size, dirs[d], want)) return true;
    }
    return false;
}

size_t ic_cache_bytes(const ic_cache *c) { return c->bytes; }

void ic_cache_stats(const ic_cache *c, size_t *hits, size_t *misses,
                    size_t *evictions)
{
    if (hits) *hits = c->hits;
    if (misses) *misses = c->misses;
    if (evictions) *evictions = c->evictions;
}
