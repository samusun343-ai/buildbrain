/* desktop.c - Desktop Entry parser + catalog. See desktop.h. */
#include "desktop.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* One claimed desktop-file id: which file won it, from which loaded
 * directory, and that file's mtime when last parsed/checked. Kept
 * also for ids whose winner is Hidden or invalid (they mask lower
 * priority directories). */
typedef struct {
    char *id;
    char *path;
    int dir_idx;
    long mtime_sec, mtime_nsec;
} seen_rec;

/* ---------------- generation arena (M3) ----------------
 * One load/refresh batch = one generation = a chain of chunks
 * holding every string and string-list of the batch's entries,
 * bump-allocated. Entries are relocated into the generation one
 * file at a time as they are parsed, so the transient peak is a
 * single file's temp strings, never the whole batch. An entry
 * references its generation; the chunks are freed whole when its
 * last entry goes away. This replaces ~14 mallocs per entry
 * (each with header + size rounding and fragmentation cost)
 * with a handful of chunk allocations per batch. */
typedef struct de_chunk {
    struct de_chunk *next;
    size_t used, cap;
    char data[];
} de_chunk;

typedef struct de_arena {
    struct de_arena *next;  /* catalog's live generations */
    int refs;               /* live entries pointing into this gen */
    size_t bytes;           /* total chunk bytes incl. headers */
    de_chunk *head, *tail;
} de_arena;

#define DE_ARENA_ALIGN 8u
#define DE_ARENA_CHUNK 8192u

static de_arena *arena_new(void)
{
    de_arena *a = calloc(1, sizeof(*a));
    return a;
}

static void arena_free(de_arena *a)
{
    if (!a) return;
    de_chunk *ch = a->head;
    while (ch) {
        de_chunk *nx = ch->next;
        free(ch);
        ch = nx;
    }
    free(a);
}

static void *arena_alloc(de_arena *a, size_t size)
{
    if (!a) return NULL; /* OOM batch: relocation keeps malloc'd strings */
    de_chunk *ch = a->tail;
    size_t off = ch ? (ch->used + (DE_ARENA_ALIGN - 1)) &
                      ~(size_t)(DE_ARENA_ALIGN - 1) : 0;
    if (!ch || off + size > ch->cap) {
        size_t cap = size > DE_ARENA_CHUNK ? size : DE_ARENA_CHUNK;
        ch = malloc(sizeof(*ch) + cap);
        if (!ch) return NULL;
        ch->next = NULL;
        ch->used = 0;
        ch->cap = cap;
        if (a->tail) a->tail->next = ch;
        else a->head = ch;
        a->tail = ch;
        a->bytes += sizeof(*ch) + cap;
        off = 0;
    }
    void *p = ch->data + off;
    ch->used = off + size;
    return p;
}

static char *arena_strdup(de_arena *a, const char *s)
{
    if (!a || !s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = arena_alloc(a, len);
    if (p) memcpy(p, s, len);
    return p;
}

struct de_catalog {
    char *locale;      /* e.g. "ja_JP" */
    char *lang;        /* e.g. "ja" */
    de_entry *entries;
    size_t n, cap;
    seen_rec *seen;
    size_t n_seen, cap_seen;
    char **dirs;       /* loaded directories, in priority order */
    size_t n_dirs, cap_dirs;
    size_t budget;     /* resident-memory budget, 0 = unlimited */
    bool trimmed;      /* non-essential strings were dropped */
    de_arena *arenas;  /* live generation blocks (M3) */
};

/* Drop one entry's reference to its generation; frees the chunks
 * when the last referencing entry goes away. */
static void arena_unref(de_catalog *c, de_arena *a)
{
    if (!a) return;
    if (--a->refs > 0) return;
    de_arena **pp = &c->arenas;
    while (*pp && *pp != a) pp = &(*pp)->next;
    if (*pp) *pp = a->next;
    arena_free(a);
}

/* Start a batch generation and link it into the catalog. */
static de_arena *arena_begin(de_catalog *c)
{
    de_arena *a = arena_new();
    if (a) {
        a->next = c->arenas;
        c->arenas = a;
    }
    return a;
}

/* End a batch: a generation no entry referenced (all files of
 * the batch were Hidden/invalid) is freed immediately instead of
 * lingering empty. */
static void arena_end_batch(de_catalog *c, de_arena *a)
{
    if (a && a->refs == 0) arena_unref(c, a);
}

/* ---------------- small helpers ---------------- */

static char *xstrdup(const char *s) { return s ? strdup(s) : NULL; }

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

/* Decode Desktop Entry string escapes in place: \s \n \t \r \\.
 * Unknown escapes keep the escaped character (lenient). */
static void unescape(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '\\' && r[1]) {
            r++;
            switch (*r) {
            case 's': *w++ = ' '; break;
            case 'n': *w++ = '\n'; break;
            case 't': *w++ = '\t'; break;
            case 'r': *w++ = '\r'; break;
            default:  *w++ = *r; break;
            }
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static char **split_list(const char *v, size_t *n_out)
{
    *n_out = 0;
    if (!v || !*v) return NULL;
    char *copy = strdup(v);
    size_t cap = 8, n = 0;
    char **arr = malloc(cap * sizeof(char *));
    char *p = copy;
    while (p) {
        char *semi = strchr(p, ';');
        if (semi) *semi = 0;
        char *item = trim(p);
        if (*item) {
            if (n == cap) { cap *= 2; arr = realloc(arr, cap * sizeof(char *)); }
            arr[n++] = strdup(item);
        }
        p = semi ? semi + 1 : NULL;
    }
    free(copy);
    *n_out = n;
    return arr;
}

static bool parse_bool(const char *v)
{
    return v && strcmp(v, "true") == 0;
}

/* ---------------- category mapping ---------------- */

const char *de_category_name(de_category cat)
{
    switch (cat) {
    case DE_CAT_ACCESSORIES: return "Accessories";
    case DE_CAT_DEVELOPMENT: return "Development";
    case DE_CAT_EDUCATION:   return "Education";
    case DE_CAT_GAME:        return "Game";
    case DE_CAT_GRAPHICS:    return "Graphics";
    case DE_CAT_NETWORK:     return "Network";
    case DE_CAT_OFFICE:      return "Office";
    case DE_CAT_AUDIOVIDEO:  return "AudioVideo";
    case DE_CAT_SETTINGS:    return "Settings";
    case DE_CAT_SYSTEM:      return "System";
    case DE_CAT_OTHER:       return "Other";
    default:                 return "All";
    }
}

/* Main categories, checked in the entry's own order first (below). */
static const struct { const char *name; de_category cat; } main_cats[] = {
    { "Utility", DE_CAT_ACCESSORIES },
    { "Development", DE_CAT_DEVELOPMENT },
    { "Education", DE_CAT_EDUCATION },
    { "Game", DE_CAT_GAME },
    { "Graphics", DE_CAT_GRAPHICS },
    { "Network", DE_CAT_NETWORK },
    { "Office", DE_CAT_OFFICE },
    { "AudioVideo", DE_CAT_AUDIOVIDEO },
    { "Settings", DE_CAT_SETTINGS },
    { "System", DE_CAT_SYSTEM },
};

/* Additional categories -> documented related main category
 * (freedesktop "Additional Category" table, subset relevant here). */
static const struct { const char *name; de_category cat; } add_cats[] = {
    { "WebBrowser", DE_CAT_NETWORK },
    { "Email", DE_CAT_NETWORK },
    { "Chat", DE_CAT_NETWORK },
    { "FileManager", DE_CAT_SYSTEM },
    { "TerminalEmulator", DE_CAT_SYSTEM },
    { "SystemSettings", DE_CAT_SETTINGS },
    { "DesktopSettings", DE_CAT_SETTINGS },
    { "TextEditor", DE_CAT_ACCESSORIES },
    { "Calculator", DE_CAT_ACCESSORIES },
    { "Archiving", DE_CAT_ACCESSORIES },
    { "WordProcessor", DE_CAT_OFFICE },
    { "Spreadsheet", DE_CAT_OFFICE },
    { "Presentation", DE_CAT_OFFICE },
    { "Audio", DE_CAT_AUDIOVIDEO },
    { "Video", DE_CAT_AUDIOVIDEO },
    { "Player", DE_CAT_AUDIOVIDEO },
    { "Photography", DE_CAT_GRAPHICS },
    { "Viewer", DE_CAT_GRAPHICS },
    { "IDE", DE_CAT_DEVELOPMENT },
    { "Debugger", DE_CAT_DEVELOPMENT },
    { "Science", DE_CAT_EDUCATION },
    { "PuzzleGame", DE_CAT_GAME },
    { "ActionGame", DE_CAT_GAME },
};

static de_category map_category(char **cats, size_t n)
{
    /* Pass 1: a main category, in the order the file lists them. */
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < sizeof(main_cats) / sizeof(main_cats[0]); j++)
            if (strcmp(cats[i], main_cats[j].name) == 0)
                return main_cats[j].cat;
    /* Pass 2: additional categories. */
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < sizeof(add_cats) / sizeof(add_cats[0]); j++)
            if (strcmp(cats[i], add_cats[j].name) == 0)
                return add_cats[j].cat;
    return DE_CAT_OTHER;
}

/* ---------------- parsing ---------------- */

/* Raw key/value store for one file's [Desktop Entry] group. */
typedef struct { char *key, *val; } kv;
typedef struct { kv *items; size_t n, cap; } kvlist;

static const char *kv_get(kvlist *l, const char *key)
{
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->items[i].key, key) == 0)
            return l->items[i].val;
    return NULL;
}

static void kv_put(kvlist *l, const char *key, const char *val)
{
    if (kv_get(l, key)) return; /* first occurrence wins */
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->items = realloc(l->items, l->cap * sizeof(kv));
    }
    l->items[l->n].key = strdup(key);
    l->items[l->n].val = strdup(val);
    l->n++;
}

static void kv_free(kvlist *l)
{
    for (size_t i = 0; i < l->n; i++) { free(l->items[i].key); free(l->items[i].val); }
    free(l->items);
}

/* Localized lookup: key[locale], then key[lang], then key. */
static const char *kv_get_localized(kvlist *l, const char *key,
                                    const char *locale, const char *lang)
{
    char buf[128];
    if (locale) {
        snprintf(buf, sizeof(buf), "%s[%s]", key, locale);
        const char *v = kv_get(l, buf);
        if (v) return v;
    }
    if (lang && (!locale || strcmp(lang, locale) != 0)) {
        snprintf(buf, sizeof(buf), "%s[%s]", key, lang);
        const char *v = kv_get(l, buf);
        if (v) return v;
    }
    return kv_get(l, key);
}

/* Parse one file. Returns 1 and fills *out when it is a valid
 * Application entry, 0 when the file should not enter the catalog
 * (invalid, non-Application, or Hidden - *hidden set in that case). */
static int parse_file(const char *path, const char *locale,
                      const char *lang, de_entry *out, bool *hidden)
{
    *hidden = false;
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    kvlist l = {0};
    char line[1024];
    int group_state = 0; /* 0 = before first group, 1 = in Desktop Entry, 2 = in other group */
    bool first_group_seen = false;
    while (fgets(line, sizeof(line), fp)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) { if (!first_group_seen) { fclose(fp); kv_free(&l); return 0; } continue; }
            *end = 0;
            char *gname = trim(s + 1);
            if (!first_group_seen) {
                first_group_seen = true;
                if (strcmp(gname, "Desktop Entry") != 0) {
                    fclose(fp); kv_free(&l); return 0; /* spec: first group must be Desktop Entry */
                }
                group_state = 1;
            } else {
                group_state = (strcmp(gname, "Desktop Entry") == 0) ? 1 : 2;
            }
            continue;
        }
        if (group_state != 1) continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        kv_put(&l, trim(s), trim(eq + 1));
    }
    fclose(fp);
    if (!first_group_seen) { kv_free(&l); return 0; }

    const char *type = kv_get(&l, "Type");
    if (parse_bool(kv_get(&l, "Hidden"))) { *hidden = true; kv_free(&l); return 0; }
    if (!type || strcmp(type, "Application") != 0) { kv_free(&l); return 0; }
    const char *name = kv_get_localized(&l, "Name", locale, lang);
    if (!name || !*name) { kv_free(&l); return 0; }

    memset(out, 0, sizeof(*out));
    out->name = strdup(name); unescape(out->name);
    const char *gn = kv_get_localized(&l, "GenericName", locale, lang);
    out->generic_name = xstrdup(gn); if (out->generic_name) unescape(out->generic_name);
    const char *cm = kv_get_localized(&l, "Comment", locale, lang);
    out->comment = xstrdup(cm); if (out->comment) unescape(out->comment);
    out->icon = xstrdup(kv_get(&l, "Icon"));
    out->exec_raw = xstrdup(kv_get(&l, "Exec"));
    out->try_exec = xstrdup(kv_get(&l, "TryExec"));
    out->no_display = parse_bool(kv_get(&l, "NoDisplay"));
    out->terminal = parse_bool(kv_get(&l, "Terminal"));
    out->categories = split_list(kv_get(&l, "Categories"), &out->n_categories);
    out->keywords = split_list(kv_get(&l, "Keywords"), &out->n_keywords);
    out->main_category = map_category(out->categories, out->n_categories);
    out->launchable = out->exec_raw && *out->exec_raw;
    kv_free(&l);
    return 1;
}

/* Release a committed entry's strings: drop its reference on the
 * generation arena (the block is freed whole when its last entry
 * goes). id/path are borrowed from the seen record and are only
 * unlinked here, never freed. */
static void entry_clear(de_catalog *c, de_entry *e)
{
    arena_unref(c, e->arena);
    memset(e, 0, sizeof(*e));
}

static char **list_relocate(de_arena *a, char **arr, size_t n)
{
    if (!arr) return NULL;
    char **na = arena_alloc(a, n * sizeof(char *));
    if (!na) return arr; /* cannot happen (pre-measured); stay safe */
    for (size_t i = 0; i < n; i++) {
        char *cp = arena_strdup(a, arr[i]);
        na[i] = cp ? cp : arr[i]; /* leak-safe fallback, never dangles */
        if (cp) free(arr[i]);
    }
    free(arr);
    return na;
}

/* Move a temp entry's strings into generation `a`, freeing the
 * malloc'd originals, and take the entry's reference on `a`. */
static void entry_relocate(de_entry *e, de_arena *a)
{
    char *cp;
    cp = arena_strdup(a, e->name);         if (cp) { free(e->name); e->name = cp; }
    cp = arena_strdup(a, e->generic_name); if (cp) { free(e->generic_name); e->generic_name = cp; }
    cp = arena_strdup(a, e->comment);      if (cp) { free(e->comment); e->comment = cp; }
    cp = arena_strdup(a, e->icon);         if (cp) { free(e->icon); e->icon = cp; }
    cp = arena_strdup(a, e->exec_raw);     if (cp) { free(e->exec_raw); e->exec_raw = cp; }
    cp = arena_strdup(a, e->try_exec);     if (cp) { free(e->try_exec); e->try_exec = cp; }
    e->categories = list_relocate(a, e->categories, e->n_categories);
    e->keywords = list_relocate(a, e->keywords, e->n_keywords);
    e->arena = a;
    if (a) a->refs++;
}

/* ---------------- catalog ---------------- */

static void catalog_apply_budget(de_catalog *c);

de_catalog *de_catalog_new(const char *locale)
{
    de_catalog *c = calloc(1, sizeof(*c));
    c->locale = xstrdup(locale);
    if (locale) {
        c->lang = strdup(locale);
        char *u = strchr(c->lang, '_');
        if (u) *u = 0;
        char *dot = strchr(c->lang, '.');
        if (dot) *dot = 0;
    }
    c->budget = 1024 * 1024; /* 1 MiB default resident budget */
    return c;
}

void de_catalog_free(de_catalog *c)
{
    if (!c) return;
    for (size_t i = 0; i < c->n; i++) entry_clear(c, &c->entries[i]);
    free(c->entries);
    /* generations are freed by the unrefs above; sweep defensively */
    while (c->arenas) {
        de_arena *a = c->arenas;
        c->arenas = a->next;
        arena_free(a);
    }
    for (size_t i = 0; i < c->n_seen; i++) {
        free(c->seen[i].id);
        free(c->seen[i].path);
    }
    free(c->seen);
    for (size_t i = 0; i < c->n_dirs; i++) free(c->dirs[i]);
    free(c->dirs);
    free(c->locale); free(c->lang);
    free(c);
}

static seen_rec *seen_find(de_catalog *c, const char *id)
{
    for (size_t i = 0; i < c->n_seen; i++)
        if (strcmp(c->seen[i].id, id) == 0) return &c->seen[i];
    return NULL;
}

static void seen_set(de_catalog *c, const char *id, const char *path,
                     int dir_idx, long msec, long mnsec)
{
    seen_rec *r = seen_find(c, id);
    if (!r) {
        if (c->n_seen == c->cap_seen) {
            c->cap_seen = c->cap_seen ? c->cap_seen * 2 : 32;
            c->seen = realloc(c->seen, c->cap_seen * sizeof(seen_rec));
        }
        r = &c->seen[c->n_seen++];
        r->id = strdup(id);
        r->path = NULL;
    }
    free(r->path);
    r->path = strdup(path);
    r->dir_idx = dir_idx;
    r->mtime_sec = msec;
    r->mtime_nsec = mnsec;
}

static void seen_remove(de_catalog *c, const char *id)
{
    for (size_t i = 0; i < c->n_seen; i++) {
        if (strcmp(c->seen[i].id, id) == 0) {
            free(c->seen[i].id);
            free(c->seen[i].path);
            memmove(&c->seen[i], &c->seen[i + 1],
                    (c->n_seen - i - 1) * sizeof(seen_rec));
            c->n_seen--;
            return;
        }
    }
}

static int cmp_names(const void *a, const void *b)
{
    /* elements are char[256] file names, not pointers */
    return strcasecmp((const char *)a, (const char *)b);
}

static bool file_mtime(const char *path, long *sec, long *nsec)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *sec = (long)st.st_mtim.tv_sec;
    *nsec = (long)st.st_mtim.tv_nsec;
    return true;
}

static int dir_register(de_catalog *c, const char *dir)
{
    for (size_t i = 0; i < c->n_dirs; i++)
        if (strcmp(c->dirs[i], dir) == 0) return (int)i;
    if (c->n_dirs == c->cap_dirs) {
        c->cap_dirs = c->cap_dirs ? c->cap_dirs * 2 : 8;
        c->dirs = realloc(c->dirs, c->cap_dirs * sizeof(char *));
    }
    c->dirs[c->n_dirs] = strdup(dir);
    return (int)c->n_dirs++;
}

static void entry_append(de_catalog *c, de_entry *e)
{
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 32;
        c->entries = realloc(c->entries, c->cap * sizeof(de_entry));
    }
    c->entries[c->n++] = *e;
}

static void entry_remove(de_catalog *c, const char *id)
{
    for (size_t i = 0; i < c->n; i++) {
        if (strcmp(c->entries[i].id, id) == 0) {
            entry_clear(c, &c->entries[i]);
            memmove(&c->entries[i], &c->entries[i + 1],
                    (c->n - i - 1) * sizeof(de_entry));
            c->n--;
            return;
        }
    }
}

/* List the *.desktop file names of a directory, sorted. Caller
 * frees the returned array (names are inline char[256]). */
static char (*list_desktop_files(const char *dir, size_t *n_out))[256]
{
    *n_out = 0;
    DIR *d = opendir(dir);
    if (!d) return NULL;
    char (*names)[256] = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        size_t len = strlen(de->d_name);
        if (len > 8 && strcmp(de->d_name + len - 8, ".desktop") == 0) {
            if (n == cap) { cap = cap ? cap * 2 : 32; names = realloc(names, cap * sizeof(*names)); }
            snprintf(names[n++], 256, "%s", de->d_name);
        }
    }
    closedir(d);
    qsort(names, n, sizeof(*names), cmp_names);
    *n_out = n;
    return names;
}

/* Commit one parsed temp entry for `id` (parsed == NULL drops
 * any existing entry: the file became Hidden/invalid, but the id
 * stays claimed in the seen table so it keeps masking lower
 * priority directories). The temp's strings are relocated into
 * the batch generation `batch`; the entry's id/path are borrowed
 * from the seen record (no second copy). Returns 1 when the
 * catalog changed. */
static int catalog_commit(de_catalog *c, const char *id,
                          const char *path, int dir_idx,
                          long msec, long mnsec,
                          de_entry *parsed, de_arena *batch)
{
    de_entry *existing = NULL;
    size_t existing_idx = 0;
    for (size_t i = 0; i < c->n; i++)
        if (strcmp(c->entries[i].id, id) == 0) {
            existing = &c->entries[i];
            existing_idx = i;
            break;
        }
    if (existing) entry_clear(c, existing); /* drop old generation ref */
    seen_set(c, id, path, dir_idx, msec, mnsec);
    seen_rec *r = seen_find(c, id);
    if (!parsed) {
        if (existing) {
            memmove(&c->entries[existing_idx], &c->entries[existing_idx + 1],
                    (c->n - existing_idx - 1) * sizeof(de_entry));
            c->n--;
            return 1;
        }
        return 0;
    }
    entry_relocate(parsed, batch);
    parsed->id = r->id;     /* borrowed: owned by the seen record */
    parsed->path = r->path; /* borrowed */
    parsed->src_mtime_sec = msec;
    parsed->src_mtime_nsec = mnsec;
    if (existing) {
        *existing = *parsed;
    } else {
        entry_append(c, parsed);
    }
    return 1;
}

int de_catalog_load_dir(de_catalog *c, const char *dir)
{
    int dir_idx = dir_register(c, dir);
    size_t n = 0;
    char (*names)[256] = list_desktop_files(dir, &n);
    de_arena *batch = NULL; /* this load's generation, lazily begun */
    int added = 0;
    for (size_t i = 0; i < n; i++) {
        const char *id = names[i];
        if (seen_find(c, id)) continue; /* an earlier dir claimed it */
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, id);
        long msec = 0, mnsec = 0;
        file_mtime(path, &msec, &mnsec);
        de_entry e;
        bool hidden = false;
        int ok = parse_file(path, c->locale, c->lang, &e, &hidden);
        if (!batch) batch = arena_begin(c);
        size_t before = c->n;
        catalog_commit(c, id, path, dir_idx, msec, mnsec,
                       ok ? &e : NULL, batch);
        if (c->n > before) added++;
    }
    free(names);
    arena_end_batch(c, batch);
    catalog_apply_budget(c);
    return added;
}

int de_catalog_refresh(de_catalog *c)
{
    int changes = 0;
    /* Determine the current winner of every id: the first loaded
     * directory (priority order) that contains the file. */
    typedef struct { char *id; char *path; int dir_idx; long sec, nsec; } win;
    win *wins = NULL;
    size_t n_wins = 0, cap_wins = 0;
    for (size_t di = 0; di < c->n_dirs; di++) {
        size_t n = 0;
        char (*names)[256] = list_desktop_files(c->dirs[di], &n);
        for (size_t i = 0; i < n; i++) {
            const char *id = names[i];
            bool already = false;
            for (size_t k = 0; k < n_wins; k++)
                if (strcmp(wins[k].id, id) == 0) { already = true; break; }
            if (already) continue;
            if (n_wins == cap_wins) {
                cap_wins = cap_wins ? cap_wins * 2 : 32;
                wins = realloc(wins, cap_wins * sizeof(win));
            }
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", c->dirs[di], id);
            wins[n_wins].id = strdup(id);
            wins[n_wins].path = strdup(path);
            wins[n_wins].dir_idx = (int)di;
            wins[n_wins].sec = 0;
            wins[n_wins].nsec = 0;
            file_mtime(path, &wins[n_wins].sec, &wins[n_wins].nsec);
            n_wins++;
        }
        free(names);
    }
    /* Apply winners: unchanged (same path + mtime) files are kept
     * without re-reading; changed/new files are re-parsed into ONE
     * new generation arena for this refresh batch. */
    {
        de_arena *batch = NULL;
        for (size_t k = 0; k < n_wins; k++) {
            seen_rec *r = seen_find(c, wins[k].id);
            if (r && strcmp(r->path, wins[k].path) == 0 &&
                r->mtime_sec == wins[k].sec && r->mtime_nsec == wins[k].nsec)
                continue;
            de_entry e;
            bool hidden = false;
            int ok = parse_file(wins[k].path, c->locale, c->lang,
                                &e, &hidden);
            if (!batch) batch = arena_begin(c);
            /* a re-parse that yields the same visible state still
             * counts as applied work only when the entry set
             * changed; catalog_commit returns 1 for replace as
             * well, which is what callers want (the entry content
             * may differ). */
            changes += catalog_commit(c, wins[k].id, wins[k].path,
                                      wins[k].dir_idx, wins[k].sec,
                                      wins[k].nsec,
                                      ok ? &e : NULL, batch);
        }
        arena_end_batch(c, batch);
    }
    /* Drop ids that no longer have a winner. */
    for (size_t i = 0; i < c->n_seen;) {
        const char *id = c->seen[i].id;
        bool alive = false;
        for (size_t k = 0; k < n_wins; k++)
            if (strcmp(wins[k].id, id) == 0) { alive = true; break; }
        if (alive) { i++; continue; }
        entry_remove(c, id);
        seen_remove(c, id);
        changes++;
    }
    for (size_t k = 0; k < n_wins; k++) { free(wins[k].id); free(wins[k].path); }
    free(wins);
    catalog_apply_budget(c);
    return changes;
}

/* ---------------- memory budget ---------------- */

static size_t str_bytes(const char *s) { return s ? strlen(s) + 1 : 0; }

static size_t entry_bytes(const de_entry *e)
{
    size_t b = sizeof(de_entry);
    b += str_bytes(e->id) + str_bytes(e->path) + str_bytes(e->name) +
         str_bytes(e->generic_name) + str_bytes(e->comment) +
         str_bytes(e->icon) + str_bytes(e->exec_raw) + str_bytes(e->try_exec);
    for (size_t i = 0; i < e->n_categories; i++) b += str_bytes(e->categories[i]) + sizeof(char *);
    for (size_t i = 0; i < e->n_keywords; i++) b += str_bytes(e->keywords[i]) + sizeof(char *);
    return b;
}

size_t de_catalog_memory(const de_catalog *c)
{
    size_t b = sizeof(*c);
    /* entry_bytes counts id/path once: they are the seen records'
     * strings, borrowed by the entries (M3), so the seen loop
     * below adds only the record structs. */
    for (size_t i = 0; i < c->n; i++) b += entry_bytes(&c->entries[i]);
    for (size_t i = 0; i < c->n_seen; i++)
        b += sizeof(seen_rec);
    for (size_t i = 0; i < c->n_dirs; i++) b += str_bytes(c->dirs[i]) + sizeof(char *);
    return b;
}

size_t de_catalog_arena_bytes(const de_catalog *c)
{
    size_t b = 0;
    for (const de_arena *a = c->arenas; a; a = a->next)
        b += sizeof(*a) + a->bytes;
    return b;
}

size_t de_catalog_generations(const de_catalog *c)
{
    size_t n = 0;
    for (const de_arena *a = c->arenas; a; a = a->next) n++;
    return n;
}

void de_catalog_set_budget(de_catalog *c, size_t bytes)
{
    c->budget = bytes;
    catalog_apply_budget(c);
}

bool de_catalog_trimmed(const de_catalog *c) { return c->trimmed; }

/* Trim order: Comment strings first, then Keywords lists. Essential
 * fields (Name/GenericName/Icon/Exec/Categories) are never cut, so
 * trimming degrades search richness, never launchability. With
 * generation-arena storage (M3) trimming is LOGICAL: the pointers
 * are dropped (the memory estimate and all searches see them as
 * gone); the physical bytes return when the entry's generation is
 * replaced or removed. */
static void catalog_apply_budget(de_catalog *c)
{
    if (!c->budget) return;
    if (de_catalog_memory(c) <= c->budget) return;
    for (size_t i = 0; i < c->n; i++) {
        if (c->entries[i].comment) {
            c->entries[i].comment = NULL;
            c->trimmed = true;
        }
    }
    if (de_catalog_memory(c) <= c->budget) return;
    for (size_t i = 0; i < c->n; i++) {
        if (c->entries[i].keywords) {
            c->entries[i].keywords = NULL;
            c->entries[i].n_keywords = 0;
            c->trimmed = true;
        }
    }
}

size_t de_catalog_count(const de_catalog *c) { return c->n; }

size_t de_catalog_visible_count(const de_catalog *c)
{
    size_t n = 0;
    for (size_t i = 0; i < c->n; i++)
        if (!c->entries[i].no_display) n++;
    return n;
}

const de_entry *de_catalog_at(const de_catalog *c, size_t i)
{
    return i < c->n ? &c->entries[i] : NULL;
}

const de_entry *de_catalog_find_id(const de_catalog *c, const char *id)
{
    for (size_t i = 0; i < c->n; i++)
        if (strcmp(c->entries[i].id, id) == 0) return &c->entries[i];
    return NULL;
}

static bool contains_ci(const char *hay, const char *needle)
{
    if (!hay) return false;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++)
        if (strncasecmp(p, needle, nl) == 0) return true;
    return false;
}

static bool entry_matches(const de_entry *e, const char *q)
{
    if (!q || !*q) return true;
    if (contains_ci(e->name, q) || contains_ci(e->generic_name, q) ||
        contains_ci(e->comment, q))
        return true;
    for (size_t i = 0; i < e->n_keywords; i++)
        if (contains_ci(e->keywords[i], q)) return true;
    for (size_t i = 0; i < e->n_categories; i++)
        if (contains_ci(e->categories[i], q)) return true;
    return false;
}

static int cmp_entry_ptrs(const void *a, const void *b)
{
    const de_entry *ea = *(const de_entry * const *)a;
    const de_entry *eb = *(const de_entry * const *)b;
    return strcasecmp(ea->name, eb->name);
}

size_t de_catalog_query(const de_catalog *c, de_category cat,
                        const char *query, const de_entry **out, size_t max)
{
    const de_entry **all = malloc((c->n ? c->n : 1) * sizeof(de_entry *));
    size_t m = 0;
    for (size_t i = 0; i < c->n; i++) {
        const de_entry *e = &c->entries[i];
        if (e->no_display) continue;
        if (cat != DE_CAT_ALL && e->main_category != cat) continue;
        if (!entry_matches(e, query)) continue;
        all[m++] = e;
    }
    qsort(all, m, sizeof(de_entry *), cmp_entry_ptrs);
    size_t fill = m < max ? m : max;
    for (size_t i = 0; i < fill; i++) out[i] = all[i];
    free(all);
    return m;
}

/* ---------------- Exec tokenization ---------------- */

typedef struct { char **v; size_t n, cap; } argvec;

static void arg_push(argvec *a, const char *s, size_t len)
{
    if (a->n + 2 > a->cap) {
        a->cap = a->cap ? a->cap * 2 : 8;
        a->v = realloc(a->v, a->cap * sizeof(char *));
    }
    a->v[a->n] = strndup(s, len);
    a->n++;
    a->v[a->n] = NULL;
}

/* Append s to the token under construction (bounded like the
 * existing %c/%k expansions). */
static void tok_append(char *tok, size_t *tl, const char *s)
{
    if (!s) return;
    for (; *s && *tl < 1023; s++) tok[(*tl)++] = *s;
}

static char **exec_argv_impl(const de_entry *e,
                             char *const *files, size_t n_files,
                             char *const *urls, size_t n_urls)
{
    if (!e || !e->launchable) return NULL;
    argvec a = {0};
    const char *p = e->exec_raw;
    char tok[1024];
    size_t tl = 0;
    bool in_tok = false, in_quote = false;
    for (; *p; p++) {
        char ch = *p;
        if (in_quote) {
            if (ch == '"') { in_quote = false; continue; }
            if (ch == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '`' || p[1] == '$')) {
                tok[tl++] = *++p; continue;
            }
            tok[tl++] = ch; continue;
        }
        if (ch == '"') { in_quote = true; in_tok = true; continue; }
        if (ch == '\\' && p[1]) { tok[tl++] = *++p; in_tok = true; continue; }
        if (isspace((unsigned char)ch)) {
            if (in_tok) { arg_push(&a, tok, tl); tl = 0; in_tok = false; }
            continue;
        }
        if (ch == '%' && p[1]) {
            char code = *++p;
            switch (code) {
            case '%': tok[tl++] = '%'; in_tok = true; break;
            case 'c':
                tok_append(tok, &tl, e->name);
                in_tok = true; break;
            case 'k':
                tok_append(tok, &tl, e->path);
                in_tok = true; break;
            case 'f':   /* single file: substitutes into the token */
                if (n_files > 0) { tok_append(tok, &tl, files[0]); in_tok = tl > 0; }
                break;
            case 'u':   /* single URL: substitutes into the token */
                if (n_urls > 0) { tok_append(tok, &tl, urls[0]); in_tok = tl > 0; }
                break;
            case 'F':   /* file list: standalone arguments */
                if (in_tok) { arg_push(&a, tok, tl); tl = 0; in_tok = false; }
                for (size_t i = 0; i < n_files; i++)
                    arg_push(&a, files[i], strlen(files[i]));
                break;
            case 'U':   /* URL list: standalone arguments */
                if (in_tok) { arg_push(&a, tok, tl); tl = 0; in_tok = false; }
                for (size_t i = 0; i < n_urls; i++)
                    arg_push(&a, urls[i], strlen(urls[i]));
                break;
            case 'i':
                /* %i is a standalone argument pair; flush current token */
                if (in_tok) { arg_push(&a, tok, tl); tl = 0; in_tok = false; }
                if (e->icon) { arg_push(&a, "--icon", 6); arg_push(&a, e->icon, strlen(e->icon)); }
                break;
            default:
                /* deprecated codes (%d %D %n %N %v %m) and unknown
                 * codes: removed, per spec */
                break;
            }
            continue;
        }
        tok[tl++] = ch; in_tok = true;
    }
    if (in_tok) arg_push(&a, tok, tl);
    if (a.n == 0) { free(a.v); return NULL; }
    return a.v;
}

char **de_exec_argv(const de_entry *e)
{
    return exec_argv_impl(e, NULL, 0, NULL, 0);
}

char **de_exec_argv_files(const de_entry *e,
                          char *const *files, size_t n_files,
                          char *const *urls, size_t n_urls)
{
    return exec_argv_impl(e, files, n_files, urls, n_urls);
}

void de_argv_free(char **argv)
{
    if (!argv) return;
    for (size_t i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}
