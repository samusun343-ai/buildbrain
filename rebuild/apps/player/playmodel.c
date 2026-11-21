/* playmodel - see playmodel.h. */
#define _POSIX_C_SOURCE 200809L
#include "playmodel.h"
#include "bdstore.h"
#include "pengine.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ================= library ================= */

typedef struct {
    pl_track t;
    uint64_t last_scan;
} cache_ent;

typedef struct {
    char *name;
    char **paths;
    size_t n, cap;
} playlist;

struct pl_lib {
    char root[1024];
    cache_ent *ents;
    size_t n, cap;
    uint64_t scan_seq;
    size_t probes;
    uint64_t budget;
    playlist *lists;
    size_t nl, capl;
};

static bool is_audio(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".ogg") == 0 ||
           strcasecmp(dot, ".oga") == 0 || strcasecmp(dot, ".wav") == 0;
}

static const char *field_str(const pl_track *t, int field)
{
    switch (field) {
    case PL_F_TITLE: return t->title;
    case PL_F_ARTIST: return t->artist;
    case PL_F_ALBUM: return t->album;
    case PL_F_GENRE: return t->genre;
    case PL_F_PATH: return t->path;
    default: return t->title;
    }
}

/* ---- cache persistence ---- */

static void cache_save(pl_lib *lib)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/cache.tsv", lib->root);
    size_t cap = 256 + lib->n * 1600;
    char *buf = malloc(cap);
    if (!buf) return;
    size_t w = 0;
    for (size_t i = 0; i < lib->n; i++) {
        const pl_track *t = &lib->ents[i].t;
        char ep[1200], et[256], ea[256], el[256], eg[160];
        bd_escape(t->path, ep, sizeof(ep));
        bd_escape(t->title, et, sizeof(et));
        bd_escape(t->artist, ea, sizeof(ea));
        bd_escape(t->album, el, sizeof(el));
        bd_escape(t->genre, eg, sizeof(eg));
        w += (size_t)snprintf(buf + w, cap - w,
                              "%s\t%lld\t%llu\t%u\t%s\t%s\t%s\t%s\t%llu\n",
                              ep, (long long)t->mtime,
                              (unsigned long long)t->size, t->duration_ms,
                              et, ea, el, eg,
                              (unsigned long long)lib->ents[i].last_scan);
    }
    bd_write_atomic(path, buf, w);
    free(buf);
}

static void cache_load(pl_lib *lib)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/cache.tsv", lib->root);
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        /* 9 TSV fields */
        char *f[9];
        int nf = 0;
        char *s = line;
        while (nf < 9) {
            f[nf++] = s;
            char *tab = strchr(s, '\t');
            if (!tab) break;
            *tab = 0;
            s = tab + 1;
        }
        if (nf < 9) continue;
        if (lib->n == lib->cap) {
            lib->cap = lib->cap ? lib->cap * 2 : 64;
            lib->ents = realloc(lib->ents, lib->cap * sizeof(cache_ent));
        }
        cache_ent *e = &lib->ents[lib->n];
        memset(e, 0, sizeof(*e));
        bd_unescape(f[0], e->t.path, sizeof(e->t.path));
        e->t.mtime = strtoll(f[1], NULL, 10);
        e->t.size = strtoull(f[2], NULL, 10);
        e->t.duration_ms = (uint32_t)strtoul(f[3], NULL, 10);
        bd_unescape(f[4], e->t.title, sizeof(e->t.title));
        bd_unescape(f[5], e->t.artist, sizeof(e->t.artist));
        bd_unescape(f[6], e->t.album, sizeof(e->t.album));
        bd_unescape(f[7], e->t.genre, sizeof(e->t.genre));
        e->last_scan = strtoull(f[8], NULL, 10);
        if (e->last_scan > lib->scan_seq) lib->scan_seq = e->last_scan;
        lib->n++;
    }
    free(data);
}

static cache_ent *cache_find(pl_lib *lib, const char *path)
{
    for (size_t i = 0; i < lib->n; i++)
        if (strcmp(lib->ents[i].t.path, path) == 0) return &lib->ents[i];
    return NULL;
}

uint64_t pl_lib_cache_bytes(const pl_lib *lib)
{
    /* serialized size estimate: sum of field lengths + overhead */
    uint64_t total = 0;
    for (size_t i = 0; i < lib->n; i++) {
        const pl_track *t = &lib->ents[i].t;
        total += strlen(t->path) + strlen(t->title) + strlen(t->artist) +
                 strlen(t->album) + strlen(t->genre) + 48;
    }
    return total;
}

static void cache_evict(pl_lib *lib)
{
    while (lib->n > 0 && pl_lib_cache_bytes(lib) > lib->budget) {
        size_t victim = 0;
        for (size_t i = 1; i < lib->n; i++)
            if (lib->ents[i].last_scan < lib->ents[victim].last_scan)
                victim = i;
        memmove(&lib->ents[victim], &lib->ents[victim + 1],
                (lib->n - victim - 1) * sizeof(cache_ent));
        lib->n--;
    }
}

/* ---- scanning ---- */

static void scan_dir(pl_lib *lib, const char *dir, int depth,
                     uint64_t seq)
{
    if (depth > 8) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[1100];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        struct stat sb;
        if (stat(path, &sb) != 0) continue;
        if (S_ISDIR(sb.st_mode)) {
            scan_dir(lib, path, depth + 1, seq);
            continue;
        }
        if (!S_ISREG(sb.st_mode) || !is_audio(de->d_name)) continue;
        cache_ent *e = cache_find(lib, path);
        if (e && e->t.mtime == (int64_t)sb.st_mtime &&
            e->t.size == (uint64_t)sb.st_size) {
            e->last_scan = seq;
            continue;
        }
        pe_meta meta;
        lib->probes++;
        if (pe_probe(path, &meta) != 0) continue;
        if (!e) {
            if (lib->n == lib->cap) {
                lib->cap = lib->cap ? lib->cap * 2 : 64;
                lib->ents = realloc(lib->ents, lib->cap * sizeof(cache_ent));
            }
            e = &lib->ents[lib->n++];
            memset(e, 0, sizeof(*e));
            snprintf(e->t.path, sizeof(e->t.path), "%s", path);
        }
        snprintf(e->t.title, sizeof(e->t.title), "%s", meta.title);
        snprintf(e->t.artist, sizeof(e->t.artist), "%s", meta.artist);
        snprintf(e->t.album, sizeof(e->t.album), "%s", meta.album);
        snprintf(e->t.genre, sizeof(e->t.genre), "%s", meta.genre);
        e->t.duration_ms = meta.duration_ms;
        e->t.mtime = (int64_t)sb.st_mtime;
        e->t.size = (uint64_t)sb.st_size;
        e->last_scan = seq;
    }
    closedir(d);
}

int pl_lib_scan(pl_lib *lib, const char *dir)
{
    lib->scan_seq++;
    scan_dir(lib, dir, 0, lib->scan_seq);
    /* entries not seen in ANY recent scan of a now-missing dir
     * stay until budget eviction; entries whose file vanished
     * under a scanned dir were not refreshed but we cannot tell
     * which dir they belonged to cheaply - prune by existence. */
    size_t w = 0;
    for (size_t i = 0; i < lib->n; i++) {
        struct stat sb;
        if (stat(lib->ents[i].t.path, &sb) == 0) lib->ents[w++] = lib->ents[i];
    }
    lib->n = w;
    cache_evict(lib);
    cache_save(lib);
    return 0;
}

pl_lib *pl_lib_open(const char *root)
{
    pl_lib *lib = calloc(1, sizeof(*lib));
    if (!lib) return NULL;
    snprintf(lib->root, sizeof(lib->root), "%s", root);
    bd_mkdir_p(root);
    lib->budget = 4ull * 1024 * 1024;
    cache_load(lib);
    /* playlists load */
    char path[1100];
    snprintf(path, sizeof(path), "%s/playlists.tsv", root);
    size_t len;
    char *data = bd_read_all(path, &len);
    if (data) {
        char *save = NULL;
        for (char *line = strtok_r(data, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = 0;
            char name[128];
            bd_unescape(line, name, sizeof(name));
            playlist *pl = NULL;
            for (size_t i = 0; i < lib->nl; i++)
                if (strcmp(lib->lists[i].name, name) == 0) pl = &lib->lists[i];
            if (!pl) {
                if (lib->nl == lib->capl) {
                    lib->capl = lib->capl ? lib->capl * 2 : 8;
                    lib->lists = realloc(lib->lists,
                                         lib->capl * sizeof(playlist));
                }
                pl = &lib->lists[lib->nl++];
                memset(pl, 0, sizeof(*pl));
                pl->name = strdup(name);
            }
            char pbuf[1024];
            bd_unescape(tab + 1, pbuf, sizeof(pbuf));
            if (!pbuf[0]) continue;   /* empty-list marker line */
            if (pl->n == pl->cap) {
                pl->cap = pl->cap ? pl->cap * 2 : 8;
                pl->paths = realloc(pl->paths, pl->cap * sizeof(char *));
            }
            pl->paths[pl->n++] = strdup(pbuf);
        }
        free(data);
    }
    return lib;
}

void pl_lib_close(pl_lib *lib)
{
    if (!lib) return;
    free(lib->ents);
    for (size_t i = 0; i < lib->nl; i++) {
        free(lib->lists[i].name);
        for (size_t j = 0; j < lib->lists[i].n; j++)
            free(lib->lists[i].paths[j]);
        free(lib->lists[i].paths);
    }
    free(lib->lists);
    free(lib);
}

size_t pl_lib_count(const pl_lib *lib) { return lib->n; }
const pl_track *pl_lib_at(const pl_lib *lib, size_t i)
{
    return i < lib->n ? &lib->ents[i].t : NULL;
}
const pl_track *pl_lib_find(const pl_lib *lib, const char *path)
{
    for (size_t i = 0; i < lib->n; i++)
        if (strcmp(lib->ents[i].t.path, path) == 0) return &lib->ents[i].t;
    return NULL;
}
size_t pl_lib_probes(const pl_lib *lib) { return lib->probes; }
void pl_lib_set_cache_budget(pl_lib *lib, uint64_t bytes)
{
    lib->budget = bytes;
}

static int sort_field;
static bool sort_desc;
static int track_cmp(const void *a, const void *b)
{
    const cache_ent *x = a, *y = b;
    int c;
    if (sort_field == PL_F_DURATION)
        c = (x->t.duration_ms > y->t.duration_ms) -
            (x->t.duration_ms < y->t.duration_ms);
    else
        c = strcmp(field_str(&x->t, sort_field),
                   field_str(&y->t, sort_field));
    if (c == 0) c = strcmp(x->t.path, y->t.path);
    return sort_desc ? -c : c;
}
void pl_lib_sort(pl_lib *lib, int field, bool descending)
{
    sort_field = field;
    sort_desc = descending;
    if (lib->n) qsort(lib->ents, lib->n, sizeof(cache_ent), track_cmp);
}

static int str_cmp_q(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

size_t pl_lib_values(const pl_lib *lib, int field,
                     const char **out, size_t max)
{
    /* collect distinct non-empty values; caller buffer holds
     * pointers into a scratch array we keep in the lib? To keep
     * the API allocation-free we use a static thread-unsafe
     * scratch - the app is single-threaded by design. */
    static const char **scratch;
    static size_t scratch_cap;
    size_t n = 0;
    for (size_t i = 0; i < lib->n; i++) {
        const char *v = field_str(&lib->ents[i].t, field);
        if (!*v) continue;
        bool dup = false;
        for (size_t j = 0; j < n; j++)
            if (strcmp(scratch[j], v) == 0) { dup = true; break; }
        if (dup) continue;
        if (n == scratch_cap) {
            scratch_cap = scratch_cap ? scratch_cap * 2 : 64;
            scratch = realloc(scratch, scratch_cap * sizeof(char *));
        }
        scratch[n++] = v;
    }
    if (n) qsort(scratch, n, sizeof(char *), str_cmp_q);
    size_t w = n < max ? n : max;
    for (size_t i = 0; i < w; i++) out[i] = scratch[i];
    return w;
}

size_t pl_lib_filter(const pl_lib *lib, int field, const char *value,
                     size_t *idx_out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < lib->n && n < max; i++)
        if (strcmp(field_str(&lib->ents[i].t, field), value) == 0)
            idx_out[n++] = i;
    return n;
}

size_t pl_lib_search(const pl_lib *lib, const char *needle,
                     size_t *idx_out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < lib->n && n < max; i++) {
        const pl_track *t = &lib->ents[i].t;
        if (bd_contains_ci(t->title, needle) ||
            bd_contains_ci(t->artist, needle) ||
            bd_contains_ci(t->album, needle))
            idx_out[n++] = i;
    }
    return n;
}

/* ================= playlists ================= */

static playlist *list_find(pl_lib *lib, const char *name)
{
    for (size_t i = 0; i < lib->nl; i++)
        if (strcmp(lib->lists[i].name, name) == 0) return &lib->lists[i];
    return NULL;
}

static void lists_save(pl_lib *lib)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/playlists.tsv", lib->root);
    size_t total = 0, cap = 256;
    for (size_t i = 0; i < lib->nl; i++) total += lib->lists[i].n;
    cap += total * 1200 + lib->nl * 200;
    char *buf = malloc(cap);
    if (!buf) return;
    size_t w = 0;
    for (size_t i = 0; i < lib->nl; i++) {
        char en[256];
        bd_escape(lib->lists[i].name, en, sizeof(en));
        if (lib->lists[i].n == 0) {
            /* keep empty lists: marker line with empty path */
            w += (size_t)snprintf(buf + w, cap - w, "%s\t\n", en);
        }
        for (size_t j = 0; j < lib->lists[i].n; j++) {
            char ep[1200];
            bd_escape(lib->lists[i].paths[j], ep, sizeof(ep));
            w += (size_t)snprintf(buf + w, cap - w, "%s\t%s\n", en, ep);
        }
    }
    bd_write_atomic(path, buf, w);
    free(buf);
}

size_t pl_lists_count(const pl_lib *lib) { return lib->nl; }
const char *pl_lists_name(const pl_lib *lib, size_t i)
{
    return i < lib->nl ? lib->lists[i].name : NULL;
}

int pl_lists_create(pl_lib *lib, const char *name)
{
    if (!name || !*name || list_find(lib, name)) return -1;
    if (lib->nl == lib->capl) {
        lib->capl = lib->capl ? lib->capl * 2 : 8;
        lib->lists = realloc(lib->lists, lib->capl * sizeof(playlist));
    }
    playlist *pl = &lib->lists[lib->nl++];
    memset(pl, 0, sizeof(*pl));
    pl->name = strdup(name);
    lists_save(lib);
    return 0;
}

int pl_lists_delete(pl_lib *lib, const char *name)
{
    for (size_t i = 0; i < lib->nl; i++) {
        if (strcmp(lib->lists[i].name, name) != 0) continue;
        free(lib->lists[i].name);
        for (size_t j = 0; j < lib->lists[i].n; j++)
            free(lib->lists[i].paths[j]);
        free(lib->lists[i].paths);
        memmove(&lib->lists[i], &lib->lists[i + 1],
                (lib->nl - i - 1) * sizeof(playlist));
        lib->nl--;
        lists_save(lib);
        return 0;
    }
    return -1;
}

int pl_lists_rename(pl_lib *lib, const char *old_name,
                    const char *new_name)
{
    playlist *pl = list_find(lib, old_name);
    if (!pl || !new_name || !*new_name || list_find(lib, new_name))
        return -1;
    free(pl->name);
    pl->name = strdup(new_name);
    lists_save(lib);
    return 0;
}

size_t pl_lists_items(const pl_lib *lib, const char *name)
{
    playlist *pl = list_find((pl_lib *)lib, name);
    return pl ? pl->n : 0;
}
const char *pl_lists_item(const pl_lib *lib, const char *name, size_t i)
{
    playlist *pl = list_find((pl_lib *)lib, name);
    return (pl && i < pl->n) ? pl->paths[i] : NULL;
}

int pl_lists_add(pl_lib *lib, const char *name, const char *path)
{
    playlist *pl = list_find(lib, name);
    if (!pl) return -1;
    if (pl->n == pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 8;
        pl->paths = realloc(pl->paths, pl->cap * sizeof(char *));
    }
    pl->paths[pl->n++] = strdup(path);
    lists_save(lib);
    return 0;
}

int pl_lists_remove_at(pl_lib *lib, const char *name, size_t i)
{
    playlist *pl = list_find(lib, name);
    if (!pl || i >= pl->n) return -1;
    free(pl->paths[i]);
    memmove(&pl->paths[i], &pl->paths[i + 1],
            (pl->n - i - 1) * sizeof(char *));
    pl->n--;
    lists_save(lib);
    return 0;
}

int pl_lists_move(pl_lib *lib, const char *name, size_t from, size_t to)
{
    playlist *pl = list_find(lib, name);
    if (!pl || from >= pl->n || to >= pl->n) return -1;
    char *tmp = pl->paths[from];
    if (from < to)
        memmove(&pl->paths[from], &pl->paths[from + 1],
                (to - from) * sizeof(char *));
    else
        memmove(&pl->paths[to + 1], &pl->paths[to],
                (from - to) * sizeof(char *));
    pl->paths[to] = tmp;
    lists_save(lib);
    return 0;
}

/* ================= play order ================= */

struct pl_order {
    char **ctx;             /* context paths */
    size_t n, cap;
    size_t *perm;           /* order -> ctx index */
    size_t pos;             /* position within perm */
    bool shuffle;
    pl_repeat repeat;
    char *current;          /* owned copy (ctx or queue item) */
    char **queue;
    size_t qn, qcap;
    bool finished;
};

pl_order *pl_order_new(void)
{
    pl_order *o = calloc(1, sizeof(*o));
    return o;
}

static void order_clear_ctx(pl_order *o)
{
    for (size_t i = 0; i < o->n; i++) free(o->ctx[i]);
    free(o->ctx);
    free(o->perm);
    o->ctx = NULL;
    o->perm = NULL;
    o->n = o->cap = 0;
}

void pl_order_free(pl_order *o)
{
    if (!o) return;
    order_clear_ctx(o);
    free(o->current);
    for (size_t i = 0; i < o->qn; i++) free(o->queue[i]);
    free(o->queue);
    free(o);
}

static void order_build_perm(pl_order *o, uint32_t seed)
{
    free(o->perm);
    o->perm = malloc((o->n ? o->n : 1) * sizeof(size_t));
    for (size_t i = 0; i < o->n; i++) o->perm[i] = i;
    if (o->shuffle && o->n > 1) {
        /* Fisher-Yates with a small LCG (deterministic by seed) */
        uint32_t s = seed ? seed : 0x9E3779B9u;
        for (size_t i = o->n - 1; i > 0; i--) {
            s = s * 1664525u + 1013904223u;
            size_t j = (s >> 16) % (i + 1);
            size_t t = o->perm[i];
            o->perm[i] = o->perm[j];
            o->perm[j] = t;
        }
    }
}

void pl_order_set_context(pl_order *o, const char *const *paths,
                          size_t n, size_t start)
{
    order_clear_ctx(o);
    o->n = n;
    o->cap = n;
    o->ctx = malloc((n ? n : 1) * sizeof(char *));
    for (size_t i = 0; i < n; i++) o->ctx[i] = strdup(paths[i]);
    order_build_perm(o, 0xC0FFEEu);
    /* anchor: rotate the permutation so the start track is
     * first - forward playback then visits every track */
    o->pos = 0;
    for (size_t i = 0; i < n; i++)
        if (o->perm[i] == start) o->pos = i;
    if (o->pos > 0) {
        size_t *tmp = malloc(o->pos * sizeof(size_t));
        memcpy(tmp, o->perm, o->pos * sizeof(size_t));
        memmove(o->perm, o->perm + o->pos,
                (n - o->pos) * sizeof(size_t));
        memcpy(o->perm + (n - o->pos), tmp, o->pos * sizeof(size_t));
        free(tmp);
        o->pos = 0;
    }
    free(o->current);
    o->current = n ? strdup(o->ctx[o->perm[o->pos]]) : NULL;
    o->finished = false;
}

void pl_order_set_shuffle(pl_order *o, bool on, uint32_t seed)
{
    size_t cur_ctx = o->n ? o->perm[o->pos] : 0;
    o->shuffle = on;
    order_build_perm(o, seed);
    /* anchor the current track at the head of the new order */
    size_t at = 0;
    for (size_t i = 0; i < o->n; i++)
        if (o->perm[i] == cur_ctx) at = i;
    if (at > 0 && o->n) {
        size_t *tmp = malloc(at * sizeof(size_t));
        memcpy(tmp, o->perm, at * sizeof(size_t));
        memmove(o->perm, o->perm + at, (o->n - at) * sizeof(size_t));
        memcpy(o->perm + (o->n - at), tmp, at * sizeof(size_t));
        free(tmp);
    }
    o->pos = 0;
}
bool pl_order_shuffle(const pl_order *o) { return o->shuffle; }
void pl_order_set_repeat(pl_order *o, pl_repeat r) { o->repeat = r; }
pl_repeat pl_order_repeat(const pl_order *o) { return o->repeat; }
const char *pl_order_current(const pl_order *o) { return o->current; }

static const char *order_take_queue(pl_order *o)
{
    if (!o->qn) return NULL;
    free(o->current);
    o->current = o->queue[0];
    memmove(&o->queue[0], &o->queue[1], (o->qn - 1) * sizeof(char *));
    o->qn--;
    o->finished = false;
    return o->current;
}

const char *pl_order_next(pl_order *o, bool auto_eos)
{
    const char *q = order_take_queue(o);
    if (q) return q;
    if (!o->n) return NULL;
    if (auto_eos && o->repeat == PL_REPEAT_ONE && o->current)
        return o->current;
    if (o->finished) return NULL;
    if (o->pos + 1 < o->n) {
        o->pos++;
    } else if (o->repeat == PL_REPEAT_ALL) {
        o->pos = 0;
    } else {
        o->finished = true;
        return NULL;
    }
    free(o->current);
    o->current = strdup(o->ctx[o->perm[o->pos]]);
    return o->current;
}

const char *pl_order_prev(pl_order *o)
{
    if (!o->n) return o->current;
    if (o->pos > 0) o->pos--;
    else if (o->repeat == PL_REPEAT_ALL) o->pos = o->n - 1;
    o->finished = false;
    free(o->current);
    o->current = strdup(o->ctx[o->perm[o->pos]]);
    return o->current;
}

static void queue_insert(pl_order *o, size_t at, const char *path)
{
    if (o->qn == o->qcap) {
        o->qcap = o->qcap ? o->qcap * 2 : 8;
        o->queue = realloc(o->queue, o->qcap * sizeof(char *));
    }
    memmove(&o->queue[at + 1], &o->queue[at],
            (o->qn - at) * sizeof(char *));
    o->queue[at] = strdup(path);
    o->qn++;
}
void pl_order_queue_next(pl_order *o, const char *path)
{
    queue_insert(o, 0, path);
}
void pl_order_queue_later(pl_order *o, const char *path)
{
    queue_insert(o, o->qn, path);
}
size_t pl_order_queue_count(const pl_order *o) { return o->qn; }
const char *pl_order_queue_at(const pl_order *o, size_t i)
{
    return i < o->qn ? o->queue[i] : NULL;
}
int pl_order_queue_remove(pl_order *o, size_t i)
{
    if (i >= o->qn) return -1;
    free(o->queue[i]);
    memmove(&o->queue[i], &o->queue[i + 1],
            (o->qn - i - 1) * sizeof(char *));
    o->qn--;
    return 0;
}
int pl_order_queue_move(pl_order *o, size_t from, size_t to)
{
    if (from >= o->qn || to >= o->qn) return -1;
    char *tmp = o->queue[from];
    if (from < to)
        memmove(&o->queue[from], &o->queue[from + 1],
                (to - from) * sizeof(char *));
    else
        memmove(&o->queue[to + 1], &o->queue[to],
                (from - to) * sizeof(char *));
    o->queue[to] = tmp;
    return 0;
}
void pl_order_queue_clear(pl_order *o)
{
    for (size_t i = 0; i < o->qn; i++) free(o->queue[i]);
    o->qn = 0;
}

/* ================= persisted state ================= */

int pl_state_save(const char *root, const pl_state *st)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/state.txt", root);
    char ep[1200];
    bd_escape(st->path, ep, sizeof(ep));
    char buf[1400];
    int n = snprintf(buf, sizeof(buf), "%s\t%llu\t%.3f\t%d\t%d\n",
                     ep, (unsigned long long)st->position_ms,
                     st->volume, st->shuffle ? 1 : 0, (int)st->repeat);
    if (n < 0) return -1;
    return bd_write_atomic(path, buf, (size_t)n);
}

int pl_state_load(const char *root, pl_state *st)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/state.txt", root);
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return -1;
    memset(st, 0, sizeof(*st));
    st->volume = 1.0;
    char *f[5];
    int nf = 0;
    char *s = data;
    while (nf < 5) {
        f[nf++] = s;
        char *tab = strchr(s, '\t');
        if (!tab) break;
        *tab = 0;
        s = tab + 1;
    }
    if (nf >= 1) bd_unescape(f[0], st->path, sizeof(st->path));
    if (nf >= 2) st->position_ms = strtoull(f[1], NULL, 10);
    if (nf >= 3) st->volume = strtod(f[2], NULL);
    if (nf >= 4) st->shuffle = strcmp(f[3], "1") == 0;
    if (nf >= 5) {
        /* last field may carry the newline */
        st->repeat = (pl_repeat)atoi(f[4]);
        if (st->repeat < PL_REPEAT_OFF || st->repeat > PL_REPEAT_ONE)
            st->repeat = PL_REPEAT_OFF;
    }
    if (st->volume < 0 || st->volume > 1) st->volume = 1.0;
    free(data);
    return 0;
}
