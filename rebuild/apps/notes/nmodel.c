#define _POSIX_C_SOURCE 200809L
#include "nmodel.h"
#include "bdstore.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct nm_store {
    char *root;
    char *notes_dir;
    char *deleted_dir;
    char *attach_dir;
    uint64_t trash_budget;
    uint64_t next_id;
    uint64_t seq;
};

/* ---------------------------------------------------------------- */
/* small helpers                                                     */
/* ---------------------------------------------------------------- */

static int join2(char *out, size_t cap, const char *a, const char *b)
{
    return bd_path_join(out, cap, a, b);
}

static int note_paths(nm_store *s, const char *folder, uint64_t id,
                      bool deleted, char *body, size_t bcap,
                      char *meta, size_t mcap)
{
    char name[64];
    if (deleted) {
        snprintf(name, sizeof(name), "%llu.txt",
                 (unsigned long long)id);
        if (body && join2(body, bcap, s->deleted_dir, name) != 0)
            return -1;
        snprintf(name, sizeof(name), "%llu.meta",
                 (unsigned long long)id);
        if (meta && join2(meta, mcap, s->deleted_dir, name) != 0)
            return -1;
        return 0;
    }
    char dir[NM_PATH_MAX];
    if (join2(dir, sizeof(dir), s->notes_dir, folder) != 0) return -1;
    snprintf(name, sizeof(name), "%llu.txt", (unsigned long long)id);
    if (body && join2(body, bcap, dir, name) != 0) return -1;
    snprintf(name, sizeof(name), "%llu.meta", (unsigned long long)id);
    if (meta && join2(meta, mcap, dir, name) != 0) return -1;
    return 0;
}

typedef struct {
    char folder[256];
    bool pinned;
    uint64_t cseq, mseq, dseq;
} meta_rec;

static int meta_write(nm_store *s, const char *folder, uint64_t id,
                      bool deleted, const meta_rec *m)
{
    char mp[NM_PATH_MAX];
    if (note_paths(s, folder, id, deleted, NULL, 0, mp, sizeof(mp)) != 0)
        return -1;
    char esc[512];
    bd_escape(m->folder, esc, sizeof(esc));
    char body[1024];
    snprintf(body, sizeof(body),
             "folder=%s\npinned=%d\ncseq=%llu\nmseq=%llu\ndseq=%llu\n",
             esc, m->pinned ? 1 : 0,
             (unsigned long long)m->cseq,
             (unsigned long long)m->mseq,
             (unsigned long long)m->dseq);
    return bd_write_atomic_str(mp, body);
}

static bool meta_read_file(const char *mp, meta_rec *m)
{
    size_t len = 0;
    char *text = bd_read_all(mp, &len);
    if (!text) return false;
    memset(m, 0, sizeof(*m));
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        if (strncmp(line, "folder=", 7) == 0) {
            char un[256];
            bd_unescape(line + 7, un, sizeof(un));
            snprintf(m->folder, sizeof(m->folder), "%s", un);
        } else if (strncmp(line, "pinned=", 7) == 0) {
            m->pinned = line[7] == '1';
        } else if (strncmp(line, "cseq=", 5) == 0) {
            m->cseq = strtoull(line + 5, NULL, 10);
        } else if (strncmp(line, "mseq=", 5) == 0) {
            m->mseq = strtoull(line + 5, NULL, 10);
        } else if (strncmp(line, "dseq=", 5) == 0) {
            m->dseq = strtoull(line + 5, NULL, 10);
        }
    }
    free(text);
    return true;
}

/* Locate a note: searches live folders first, then deleted.
 * Fills folder_out (live) and *deleted_out. */
static bool find_note(nm_store *s, uint64_t id, char *folder_out,
                      size_t fcap, bool *deleted_out)
{
    /* deleted? */
    char bp[NM_PATH_MAX];
    if (note_paths(s, NULL, id, true, bp, sizeof(bp), NULL, 0) == 0) {
        struct stat st;
        if (stat(bp, &st) == 0) {
            if (deleted_out) *deleted_out = true;
            if (folder_out && fcap) folder_out[0] = '\0';
            return true;
        }
    }
    DIR *d = opendir(s->notes_dir);
    if (!d) return false;
    bool found = false;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char bp2[NM_PATH_MAX];
        if (note_paths(s, de->d_name, id, false, bp2, sizeof(bp2),
                       NULL, 0) != 0)
            continue;
        struct stat st;
        if (stat(bp2, &st) == 0) {
            if (folder_out)
                snprintf(folder_out, fcap, "%s", de->d_name);
            if (deleted_out) *deleted_out = false;
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static void counter_save(nm_store *s, const char *name, uint64_t v)
{
    char p[NM_PATH_MAX];
    if (join2(p, sizeof(p), s->root, name) != 0) return;
    char body[32];
    snprintf(body, sizeof(body), "%llu\n", (unsigned long long)v);
    (void)bd_write_atomic_str(p, body);
}

static uint64_t counter_load(nm_store *s, const char *name)
{
    char p[NM_PATH_MAX];
    if (join2(p, sizeof(p), s->root, name) != 0) return 0;
    size_t len = 0;
    char *t = bd_read_all(p, &len);
    if (!t) return 0;
    uint64_t v = strtoull(t, NULL, 10);
    free(t);
    return v;
}

static uint64_t next_seq(nm_store *s)
{
    uint64_t v = ++s->seq;
    counter_save(s, "seq", v);
    return v;
}

/* Back a string's tail off to a UTF-8 boundary in place: the
 * byte-capped derivations below can tear a multi-byte
 * sequence, which the renderer would show as U+FFFD. */
static void utf8_safe_tail(char *s)
{
    size_t w = strlen(s);
    if (!w) return;
    size_t i = w;
    while (i > 0 && (s[i - 1] & 0xC0) == 0x80) i--;
    if (i == 0) { s[0] = '\0'; return; }
    unsigned char c = (unsigned char)s[i - 1];
    size_t need = c < 0x80 ? 1
                : (c & 0xE0) == 0xC0 ? 2
                : (c & 0xF0) == 0xE0 ? 3
                : (c & 0xF8) == 0xF0 ? 4 : 1;
    if (w - (i - 1) < need) s[i - 1] = '\0';
}

/* Derive title/preview from the first bytes of a body file. */
static void derive_title_preview(const char *body_path,
                                 char **title_out, char **preview_out,
                                 uint64_t *bytes_out)
{
    *title_out = strdup("");
    *preview_out = strdup("");
    *bytes_out = 0;
    FILE *f = fopen(body_path, "rb");
    if (!f) return;
    struct stat st;
    if (fstat(fileno(f), &st) == 0) *bytes_out = (uint64_t)st.st_size;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* title: first non-empty line */
    const char *p = buf;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        size_t a = 0;
        while (a < len && (p[a] == ' ' || p[a] == '\t' || p[a] == '\r'))
            a++;
        if (a < len) {
            free(*title_out);
            *title_out = strndup(p + a, len - a);
            utf8_safe_tail(*title_out);
            /* preview: text after this line, whitespace-collapsed
             * to one line, capped for display */
            const char *rest = eol ? eol + 1 : p + len;
            char prev[160];
            size_t w = 0;
            bool last_space = true;
            for (const char *q = rest; *q && w + 1 < sizeof(prev); q++) {
                if (*q == '\n' || *q == '\r' || *q == '\t' ||
                    *q == ' ') {
                    if (!last_space) prev[w++] = ' ';
                    last_space = true;
                } else {
                    prev[w++] = *q;
                    last_space = false;
                }
            }
            prev[w] = '\0';
            utf8_safe_tail(prev);
            free(*preview_out);
            *preview_out = strdup(prev);
            return;
        }
        if (!eol) break;
        p = eol + 1;
    }
}

/* ---------------------------------------------------------------- */
/* open / scan / heal                                                */
/* ---------------------------------------------------------------- */

typedef struct {
    uint64_t id;
    char folder[256];
    bool deleted;
} scan_item;

static size_t scan_bodies(nm_store *s, bool deleted, scan_item **out)
{
    *out = NULL;
    size_t n = 0, cap = 0;
    if (deleted) {
        DIR *d = opendir(s->deleted_dir);
        if (!d) return 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strstr(de->d_name, ".txt")) continue;
            uint64_t id = strtoull(de->d_name, NULL, 10);
            if (id == 0) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                *out = realloc(*out, cap * sizeof(scan_item));
            }
            (*out)[n].id = id;
            (*out)[n].folder[0] = '\0';
            (*out)[n].deleted = true;
            n++;
        }
        closedir(d);
        return n;
    }
    DIR *d = opendir(s->notes_dir);
    if (!d) return 0;
    struct dirent *fe;
    while ((fe = readdir(d)) != NULL) {
        if (fe->d_name[0] == '.') continue;
        char fdir[NM_PATH_MAX];
        if (join2(fdir, sizeof(fdir), s->notes_dir, fe->d_name) != 0)
            continue;
        struct stat st;
        if (stat(fdir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        DIR *nd = opendir(fdir);
        if (!nd) continue;
        struct dirent *de;
        while ((de = readdir(nd)) != NULL) {
            const char *nm = de->d_name;
            size_t len = strlen(nm);
            if (len < 5 || strcmp(nm + len - 4, ".txt") != 0) continue;
            uint64_t id = strtoull(nm, NULL, 10);
            if (id == 0) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                *out = realloc(*out, cap * sizeof(scan_item));
            }
            (*out)[n].id = id;
            snprintf((*out)[n].folder, sizeof((*out)[n].folder),
                     "%s", fe->d_name);
            (*out)[n].deleted = false;
            n++;
        }
        closedir(nd);
    }
    closedir(d);
    return n;
}

nm_store *nm_open(const char *root, uint64_t trash_budget_bytes)
{
    nm_store *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->root = strdup(root);
    char p[NM_PATH_MAX];
    if (join2(p, sizeof(p), root, "notes") != 0) goto fail;
    s->notes_dir = strdup(p);
    if (join2(p, sizeof(p), root, "deleted") != 0) goto fail;
    s->deleted_dir = strdup(p);
    if (join2(p, sizeof(p), root, "attachments") != 0) goto fail;
    s->attach_dir = strdup(p);
    s->trash_budget = trash_budget_bytes;
    if (bd_mkdir_p(s->notes_dir) != 0 ||
        bd_mkdir_p(s->deleted_dir) != 0 ||
        bd_mkdir_p(s->attach_dir) != 0)
        goto fail;
    {
        char def[NM_PATH_MAX];
        if (join2(def, sizeof(def), s->notes_dir, NM_DEFAULT_FOLDER) == 0)
            (void)bd_mkdir_p(def);
    }
    (void)bd_sweep_tmp(s->root);
    (void)bd_sweep_tmp(s->deleted_dir);
    /* sweep tmp inside each folder */
    {
        DIR *d = opendir(s->notes_dir);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char fdir[NM_PATH_MAX];
                if (join2(fdir, sizeof(fdir), s->notes_dir,
                          de->d_name) == 0)
                    (void)bd_sweep_tmp(fdir);
            }
            closedir(d);
        }
    }
    /* Heal: synthesize missing metas, drop orphan metas, and
     * rebuild the counters from what actually exists. */
    s->next_id = counter_load(s, "nextid");
    s->seq = counter_load(s, "seq");
    for (int pass = 0; pass < 2; pass++) {
        bool deleted = pass == 1;
        scan_item *items = NULL;
        size_t n = scan_bodies(s, deleted, &items);
        for (size_t i = 0; i < n; i++) {
            if (items[i].id >= s->next_id) s->next_id = items[i].id + 1;
            char mp[NM_PATH_MAX];
            if (note_paths(s, items[i].folder, items[i].id, deleted,
                           NULL, 0, mp, sizeof(mp)) != 0)
                continue;
            meta_rec m;
            if (!meta_read_file(mp, &m)) {
                memset(&m, 0, sizeof(m));
                snprintf(m.folder, sizeof(m.folder), "%s",
                         deleted ? "" : items[i].folder);
                m.cseq = m.mseq = ++s->seq;
                if (deleted) m.dseq = ++s->seq;
                (void)meta_write(s, items[i].folder, items[i].id,
                                 deleted, &m);
            }
            if (m.cseq > s->seq) s->seq = m.cseq;
            if (m.mseq > s->seq) s->seq = m.mseq;
            if (m.dseq > s->seq) s->seq = m.dseq;
        }
        free(items);
    }
    if (s->next_id == 0) s->next_id = 1;
    counter_save(s, "nextid", s->next_id);
    counter_save(s, "seq", s->seq);
    return s;
fail:
    nm_close(s);
    return NULL;
}

void nm_close(nm_store *s)
{
    if (!s) return;
    free(s->root);
    free(s->notes_dir);
    free(s->deleted_dir);
    free(s->attach_dir);
    free(s);
}

const char *nm_root(const nm_store *s) { return s->root; }

/* ---------------------------------------------------------------- */
/* folders                                                           */
/* ---------------------------------------------------------------- */

bool nm_folder_name_valid(const char *name)
{
    if (!name || !*name) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    if (strchr(name, '/')) return false;
    if (strlen(name) > 200) return false;
    return true;
}

int nm_folder_create(nm_store *s, const char *name)
{
    if (!nm_folder_name_valid(name)) { errno = EINVAL; return -1; }
    char p[NM_PATH_MAX];
    if (join2(p, sizeof(p), s->notes_dir, name) != 0) return -1;
    if (mkdir(p, 0755) != 0) return -1;
    return 0;
}

int nm_folder_rename(nm_store *s, const char *old_name,
                     const char *new_name)
{
    if (!nm_folder_name_valid(old_name) ||
        !nm_folder_name_valid(new_name)) {
        errno = EINVAL;
        return -1;
    }
    if (strcmp(old_name, NM_DEFAULT_FOLDER) == 0) {
        errno = EPERM; /* the default folder keeps its name */
        return -1;
    }
    char op[NM_PATH_MAX], np[NM_PATH_MAX];
    if (join2(op, sizeof(op), s->notes_dir, old_name) != 0 ||
        join2(np, sizeof(np), s->notes_dir, new_name) != 0)
        return -1;
    if (rename(op, np) != 0) return -1;
    /* fix the folder field in every meta inside */
    scan_item *items = NULL;
    size_t n = scan_bodies(s, false, &items);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(items[i].folder, old_name) != 0) continue;
        char mp[NM_PATH_MAX];
        if (note_paths(s, new_name, items[i].id, false, NULL, 0,
                       mp, sizeof(mp)) != 0)
            continue;
        meta_rec m;
        if (meta_read_file(mp, &m)) {
            snprintf(m.folder, sizeof(m.folder), "%s", new_name);
            (void)meta_write(s, new_name, items[i].id, false, &m);
        }
    }
    free(items);
    return 0;
}

int nm_folder_delete(nm_store *s, const char *name)
{
    if (strcmp(name, NM_DEFAULT_FOLDER) == 0) {
        errno = EPERM;
        return -1;
    }
    char p[NM_PATH_MAX];
    if (join2(p, sizeof(p), s->notes_dir, name) != 0) return -1;
    struct stat st;
    if (stat(p, &st) != 0) return -1;
    /* move every note to the default folder first */
    scan_item *items = NULL;
    size_t n = scan_bodies(s, false, &items);
    int rc = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(items[i].folder, name) != 0) continue;
        if (nm_move_folder(s, items[i].id, NM_DEFAULT_FOLDER) != 0)
            rc = -1;
    }
    free(items);
    if (rc != 0) return -1;
    return rmdir(p);
}

static int str_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

size_t nm_folders(nm_store *s, char ***out)
{
    *out = NULL;
    size_t n = 0, cap = 0;
    DIR *d = opendir(s->notes_dir);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char p[NM_PATH_MAX];
        if (join2(p, sizeof(p), s->notes_dir, de->d_name) != 0) continue;
        struct stat st;
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            *out = realloc(*out, cap * sizeof(char *));
        }
        (*out)[n++] = strdup(de->d_name);
    }
    closedir(d);
    if (n > 1) qsort(*out, n, sizeof(char *), str_cmp);
    /* default folder first */
    for (size_t i = 0; i < n; i++) {
        if (strcmp((*out)[i], NM_DEFAULT_FOLDER) == 0 && i != 0) {
            char *tmp = (*out)[i];
            memmove(&(*out)[1], &(*out)[0], i * sizeof(char *));
            (*out)[0] = tmp;
            break;
        }
    }
    return n;
}

void nm_strv_free(char **v, size_t n)
{
    if (!v) return;
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
}

/* ---------------------------------------------------------------- */
/* notes                                                             */
/* ---------------------------------------------------------------- */

uint64_t nm_create(nm_store *s, const char *folder, const char *body)
{
    if (!folder || !*folder) folder = NM_DEFAULT_FOLDER;
    if (!nm_folder_name_valid(folder)) return 0;
    if (nm_folder_create(s, folder) != 0 && errno != EEXIST) return 0;
    uint64_t id = s->next_id++;
    counter_save(s, "nextid", s->next_id);
    char bp[NM_PATH_MAX];
    if (note_paths(s, folder, id, false, bp, sizeof(bp), NULL, 0) != 0)
        return 0;
    if (bd_write_atomic_str(bp, body ? body : "") != 0) return 0;
    meta_rec m;
    memset(&m, 0, sizeof(m));
    snprintf(m.folder, sizeof(m.folder), "%s", folder);
    m.cseq = m.mseq = next_seq(s);
    if (meta_write(s, folder, id, false, &m) != 0) return 0;
    return id;
}

static bool load_meta_for(nm_store *s, uint64_t id, meta_rec *m,
                          char *folder_out, size_t fcap,
                          bool *deleted_out)
{
    char folder[256];
    bool deleted = false;
    if (!find_note(s, id, folder, sizeof(folder), &deleted)) return false;
    char mp[NM_PATH_MAX];
    if (note_paths(s, folder, id, deleted, NULL, 0, mp, sizeof(mp)) != 0)
        return false;
    if (!meta_read_file(mp, m)) return false;
    if (folder_out) snprintf(folder_out, fcap, "%s", folder);
    if (deleted_out) *deleted_out = deleted;
    return true;
}

char *nm_load_body(nm_store *s, uint64_t id)
{
    char folder[256];
    bool deleted = false;
    if (!find_note(s, id, folder, sizeof(folder), &deleted)) return NULL;
    char bp[NM_PATH_MAX];
    if (note_paths(s, folder, id, deleted, bp, sizeof(bp), NULL, 0) != 0)
        return NULL;
    size_t len = 0;
    return bd_read_all(bp, &len);
}

int nm_save_body(nm_store *s, uint64_t id, const char *body)
{
    meta_rec m;
    char folder[256];
    bool deleted = false;
    if (!load_meta_for(s, id, &m, folder, sizeof(folder), &deleted))
        return -1;
    if (deleted) { errno = EPERM; return -1; }
    char bp[NM_PATH_MAX];
    if (note_paths(s, folder, id, false, bp, sizeof(bp), NULL, 0) != 0)
        return -1;
    if (bd_write_atomic_str(bp, body ? body : "") != 0) return -1;
    m.mseq = next_seq(s);
    return meta_write(s, folder, id, false, &m);
}

int nm_set_pinned(nm_store *s, uint64_t id, bool pinned)
{
    meta_rec m;
    char folder[256];
    bool deleted = false;
    if (!load_meta_for(s, id, &m, folder, sizeof(folder), &deleted))
        return -1;
    m.pinned = pinned;
    return meta_write(s, folder, id, deleted, &m);
}

int nm_move_folder(nm_store *s, uint64_t id, const char *folder)
{
    if (!nm_folder_name_valid(folder)) { errno = EINVAL; return -1; }
    if (nm_folder_create(s, folder) != 0 && errno != EEXIST) return -1;
    meta_rec m;
    char cur[256];
    bool deleted = false;
    if (!load_meta_for(s, id, &m, cur, sizeof(cur), &deleted)) return -1;
    if (deleted) { errno = EPERM; return -1; }
    if (strcmp(cur, folder) == 0) return 0;
    char ob[NM_PATH_MAX], om[NM_PATH_MAX], nb[NM_PATH_MAX], nm2[NM_PATH_MAX];
    if (note_paths(s, cur, id, false, ob, sizeof(ob), om, sizeof(om)) != 0 ||
        note_paths(s, folder, id, false, nb, sizeof(nb), nm2, sizeof(nm2)) != 0)
        return -1;
    if (rename(ob, nb) != 0) return -1;
    if (rename(om, nm2) != 0) {
        (void)rename(nb, ob); /* roll back */
        return -1;
    }
    snprintf(m.folder, sizeof(m.folder), "%s", folder);
    return meta_write(s, folder, id, false, &m);
}

uint64_t nm_deleted_bytes(nm_store *s)
{
    uint64_t total = 0;
    scan_item *items = NULL;
    size_t n = scan_bodies(s, true, &items);
    for (size_t i = 0; i < n; i++) {
        char bp[NM_PATH_MAX];
        if (note_paths(s, NULL, items[i].id, true, bp, sizeof(bp),
                       NULL, 0) != 0)
            continue;
        struct stat st;
        if (stat(bp, &st) == 0) total += (uint64_t)st.st_size;
    }
    free(items);
    return total;
}

static int enforce_deleted_budget(nm_store *s)
{
    int evicted = 0;
    for (;;) {
        if (nm_deleted_bytes(s) <= s->trash_budget) break;
        /* find the deleted note with the lowest deletion seq */
        scan_item *items = NULL;
        size_t n = scan_bodies(s, true, &items);
        if (n == 0) { free(items); break; }
        uint64_t victim = 0, victim_dseq = UINT64_MAX;
        for (size_t i = 0; i < n; i++) {
            char mp[NM_PATH_MAX];
            if (note_paths(s, NULL, items[i].id, true, NULL, 0,
                           mp, sizeof(mp)) != 0)
                continue;
            meta_rec m;
            if (!meta_read_file(mp, &m)) continue;
            if (m.dseq < victim_dseq) {
                victim_dseq = m.dseq;
                victim = items[i].id;
            }
        }
        free(items);
        if (victim == 0) break;
        if (nm_delete_permanent(s, victim) != 0) break;
        evicted++;
    }
    return evicted;
}

int nm_delete(nm_store *s, uint64_t id, int *evicted_out)
{
    meta_rec m;
    char folder[256];
    bool deleted = false;
    if (!load_meta_for(s, id, &m, folder, sizeof(folder), &deleted))
        return -1;
    if (deleted) { errno = EINVAL; return -1; }
    char ob[NM_PATH_MAX], om[NM_PATH_MAX], nb[NM_PATH_MAX], nm2[NM_PATH_MAX];
    if (note_paths(s, folder, id, false, ob, sizeof(ob), om, sizeof(om)) != 0 ||
        note_paths(s, NULL, id, true, nb, sizeof(nb), nm2, sizeof(nm2)) != 0)
        return -1;
    if (rename(ob, nb) != 0) return -1;
    m.dseq = next_seq(s);
    if (meta_write(s, NULL, id, true, &m) != 0) {
        (void)rename(nb, ob);
        return -1;
    }
    (void)unlink(om);
    int evicted = enforce_deleted_budget(s);
    if (evicted_out) *evicted_out = evicted;
    return 0;
}

int nm_restore(nm_store *s, uint64_t id)
{
    meta_rec m;
    char folder[256];
    bool deleted = false;
    if (!load_meta_for(s, id, &m, folder, sizeof(folder), &deleted))
        return -1;
    if (!deleted) { errno = EINVAL; return -1; }
    /* copy the target folder name out of m.folder first: target
     * must not alias m.folder, which is rewritten below (an
     * overlapping snprintf(m.folder, "%s", target) is UB and in
     * practice emptied the string) */
    char target_buf[256];
    snprintf(target_buf, sizeof(target_buf), "%s",
             m.folder[0] ? m.folder : NM_DEFAULT_FOLDER);
    const char *target = target_buf;
    if (nm_folder_create(s, target) != 0 && errno != EEXIST) return -1;
    char ob[NM_PATH_MAX], om[NM_PATH_MAX], nb[NM_PATH_MAX], nm2[NM_PATH_MAX];
    if (note_paths(s, NULL, id, true, ob, sizeof(ob), om, sizeof(om)) != 0 ||
        note_paths(s, target, id, false, nb, sizeof(nb), nm2, sizeof(nm2)) != 0)
        return -1;
    if (rename(ob, nb) != 0) return -1;
    m.dseq = 0;
    snprintf(m.folder, sizeof(m.folder), "%s", target);
    if (meta_write(s, target, id, false, &m) != 0) {
        (void)rename(nb, ob);
        return -1;
    }
    (void)unlink(om);
    return 0;
}

int nm_delete_permanent(nm_store *s, uint64_t id)
{
    char folder[256];
    bool deleted = false;
    if (!find_note(s, id, folder, sizeof(folder), &deleted)) return -1;
    char bp[NM_PATH_MAX], mp[NM_PATH_MAX];
    if (note_paths(s, folder, id, deleted, bp, sizeof(bp), mp, sizeof(mp)) != 0)
        return -1;
    int rc = 0;
    if (unlink(bp) != 0) rc = -1;
    if (unlink(mp) != 0 && errno != ENOENT) rc = -1;
    /* attachments go with the note */
    char adir[NM_PATH_MAX];
    char idname[32];
    snprintf(idname, sizeof(idname), "%llu", (unsigned long long)id);
    if (join2(adir, sizeof(adir), s->attach_dir, idname) == 0)
        (void)bd_remove_recursive(adir);
    return rc;
}

int nm_empty_deleted(nm_store *s)
{
    scan_item *items = NULL;
    size_t n = scan_bodies(s, true, &items);
    int rc = 0;
    for (size_t i = 0; i < n; i++)
        if (nm_delete_permanent(s, items[i].id) != 0) rc = -1;
    free(items);
    return rc;
}

/* ---------------------------------------------------------------- */
/* listing                                                           */
/* ---------------------------------------------------------------- */

static nm_sort g_sort;

static int note_cmp(const void *a, const void *b)
{
    const nm_note *x = a, *y = b;
    if (x->pinned != y->pinned) return x->pinned ? -1 : 1;
    int c = 0;
    switch (g_sort) {
    case NM_SORT_MODIFIED:
        c = (x->modified_seq < y->modified_seq) ? 1 :
            (x->modified_seq > y->modified_seq) ? -1 : 0;
        break;
    case NM_SORT_CREATED:
        c = (x->created_seq < y->created_seq) ? 1 :
            (x->created_seq > y->created_seq) ? -1 : 0;
        break;
    case NM_SORT_TITLE:
        c = strcmp(x->title, y->title);
        break;
    }
    if (c == 0) c = (x->id < y->id) ? -1 : (x->id > y->id) ? 1 : 0;
    return c;
}

static bool fill_note(nm_store *s, const scan_item *it, nm_note *out)
{
    memset(out, 0, sizeof(*out));
    out->id = it->id;
    out->deleted = it->deleted;
    char bp[NM_PATH_MAX], mp[NM_PATH_MAX];
    if (note_paths(s, it->folder, it->id, it->deleted, bp, sizeof(bp),
                   mp, sizeof(mp)) != 0)
        return false;
    meta_rec m;
    if (!meta_read_file(mp, &m)) return false;
    out->folder = strdup(it->deleted
                             ? (m.folder[0] ? m.folder : NM_DEFAULT_FOLDER)
                             : it->folder);
    out->pinned = m.pinned;
    out->created_seq = m.cseq;
    out->modified_seq = m.mseq;
    out->deleted_seq = m.dseq;
    derive_title_preview(bp, &out->title, &out->preview, &out->body_bytes);
    return true;
}

int nm_list(nm_store *s, const char *folder, bool include_deleted,
            nm_sort sort, nm_note **out, size_t *n_out)
{
    *out = NULL;
    *n_out = 0;
    scan_item *items = NULL;
    size_t n = scan_bodies(s, include_deleted, &items);
    size_t cap = 0;
    for (size_t i = 0; i < n; i++) {
        if (!include_deleted && folder &&
            strcmp(items[i].folder, folder) != 0)
            continue;
        nm_note note;
        if (!fill_note(s, &items[i], &note)) continue;
        if (*n_out == cap) {
            cap = cap ? cap * 2 : 16;
            *out = realloc(*out, cap * sizeof(nm_note));
        }
        (*out)[(*n_out)++] = note;
    }
    free(items);
    g_sort = sort;
    if (*n_out > 1) qsort(*out, *n_out, sizeof(nm_note), note_cmp);
    return 0;
}

void nm_list_free(nm_note *v, size_t n)
{
    if (!v) return;
    for (size_t i = 0; i < n; i++) {
        free(v[i].folder);
        free(v[i].title);
        free(v[i].preview);
    }
    free(v);
}

bool nm_get(nm_store *s, uint64_t id, nm_note *out)
{
    char folder[256];
    bool deleted = false;
    if (!find_note(s, id, folder, sizeof(folder), &deleted)) return false;
    scan_item it;
    memset(&it, 0, sizeof(it));
    it.id = id;
    it.deleted = deleted;
    snprintf(it.folder, sizeof(it.folder), "%s", folder);
    return fill_note(s, &it, out);
}

/* ---------------------------------------------------------------- */
/* search                                                            */
/* ---------------------------------------------------------------- */

int nm_search(nm_store *s, const char *query, nm_hit **out, size_t *n_out)
{
    *out = NULL;
    *n_out = 0;
    nm_note *notes = NULL;
    size_t nn = 0;
    if (nm_list(s, NULL, false, NM_SORT_MODIFIED, &notes, &nn) != 0)
        return -1;
    size_t cap = 0;
    for (size_t i = 0; i < nn; i++) {
        bool in_title = bd_contains_ci(notes[i].title, query);
        char *body = NULL;
        bool in_body = false;
        const char *hit_pos = NULL;
        {
            body = nm_load_body(s, notes[i].id);
            if (body) {
                /* case-insensitive body search via folded copy of
                 * the query against each position is O(n*m); the
                 * notes on this device are small - but cap the scan
                 * so a pasted book cannot stall search. */
                size_t bl = strlen(body);
                size_t ql = strlen(query);
                if (ql > 0 && bl >= ql) {
                    for (size_t k = 0; k + ql <= bl; k++) {
                        size_t j = 0;
                        while (j < ql) {
                            char a = body[k + j], b = query[j];
                            if (a >= 'A' && a <= 'Z') a += 32;
                            if (b >= 'A' && b <= 'Z') b += 32;
                            if (a != b) break;
                            j++;
                        }
                        if (j == ql) { in_body = true; hit_pos = body + k; break; }
                    }
                }
            }
        }
        /* attachments: filename match counts as a hit too */
        bool in_attach = false;
        if (!in_title && !in_body) {
            char **names = NULL;
            size_t an = nm_attach_list(s, notes[i].id, &names);
            for (size_t k = 0; k < an; k++)
                if (bd_contains_ci(names[k], query)) in_attach = true;
            nm_strv_free(names, an);
        }
        if (!in_title && !in_body && !in_attach) {
            free(body);
            continue;
        }
        if (*n_out == cap) {
            cap = cap ? cap * 2 : 8;
            *out = realloc(*out, cap * sizeof(nm_hit));
        }
        nm_hit *h = &(*out)[(*n_out)++];
        h->id = notes[i].id;
        h->title = strdup(notes[i].title);
        h->in_title = in_title;
        if (in_body && hit_pos && body) {
            /* snippet: up to 40 bytes before / 60 after the hit,
             * snapped forward/back to UTF-8 boundaries */
            const char *start = hit_pos;
            int back = 0;
            while (start > body && back < 40) {
                start--;
                back++;
                if ((*start & 0xC0) != 0x80) break;
            }
            size_t len = strlen(hit_pos);
            if (len > 100) len = 100;
            while (len > 0 && ((hit_pos[len] & 0xC0) == 0x80)) len--;
            h->snippet = strndup(start, (size_t)(hit_pos - start) + len);
        } else {
            h->snippet = strdup(notes[i].title);
        }
        free(body);
    }
    nm_list_free(notes, nn);
    return 0;
}

void nm_search_free(nm_hit *v, size_t n)
{
    if (!v) return;
    for (size_t i = 0; i < n; i++) {
        free(v[i].title);
        free(v[i].snippet);
    }
    free(v);
}

/* ---------------------------------------------------------------- */
/* attachments                                                       */
/* ---------------------------------------------------------------- */

static int attach_dir_for(nm_store *s, uint64_t id, char *out, size_t cap)
{
    char idname[32];
    snprintf(idname, sizeof(idname), "%llu", (unsigned long long)id);
    if (join2(out, cap, s->attach_dir, idname) != 0) return -1;
    return bd_mkdir_p(out);
}

int nm_attach_add(nm_store *s, uint64_t id, const char *src_path,
                  char *out_name, size_t cap)
{
    char dir[NM_PATH_MAX];
    if (attach_dir_for(s, id, dir, sizeof(dir)) != 0) return -1;
    char dst[NM_PATH_MAX];
    if (join2(dst, sizeof(dst), dir, bd_basename(src_path)) != 0)
        return -1;
    struct stat st;
    if (stat(dst, &st) == 0) {
        char uniq[NM_PATH_MAX];
        if (bd_unique_path(dst, uniq, sizeof(uniq)) != 0) return -1;
        snprintf(dst, sizeof(dst), "%s", uniq);
    }
    if (bd_copy_atomic(src_path, dst) != 0) return -1;
    if (out_name) {
        const char *bn = bd_basename(dst);
        if (strlen(bn) + 1 > cap) return -1;
        strcpy(out_name, bn);
    }
    return 0;
}

size_t nm_attach_list(nm_store *s, uint64_t id, char ***out)
{
    *out = NULL;
    char dir[NM_PATH_MAX];
    char idname[32];
    snprintf(idname, sizeof(idname), "%llu", (unsigned long long)id);
    if (join2(dir, sizeof(dir), s->attach_dir, idname) != 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (strstr(de->d_name, ".tmp.")) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            *out = realloc(*out, cap * sizeof(char *));
        }
        (*out)[n++] = strdup(de->d_name);
    }
    closedir(d);
    if (n > 1) qsort(*out, n, sizeof(char *), str_cmp);
    return n;
}

int nm_attach_remove(nm_store *s, uint64_t id, const char *name)
{
    char p[NM_PATH_MAX];
    if (nm_attach_path(s, id, name, p, sizeof(p)) != 0) return -1;
    return unlink(p);
}

int nm_attach_path(nm_store *s, uint64_t id, const char *name,
                   char *out, size_t cap)
{
    if (strchr(name, '/')) { errno = EINVAL; return -1; }
    char dir[NM_PATH_MAX];
    char idname[32];
    snprintf(idname, sizeof(idname), "%llu", (unsigned long long)id);
    if (join2(dir, sizeof(dir), s->attach_dir, idname) != 0) return -1;
    return join2(out, cap, dir, name);
}

/* ---------------------------------------------------------------- */
/* checklist helpers                                                 */
/* ---------------------------------------------------------------- */

char *nm_checklist_toggle(const char *body, size_t line_idx,
                          bool *now_checked)
{
    /* locate the line */
    const char *p = body;
    for (size_t i = 0; i < line_idx; i++) {
        const char *eol = strchr(p, '\n');
        if (!eol) return NULL;
        p = eol + 1;
    }
    bool checked;
    size_t mark_off;
    if (strncmp(p, "[ ] ", 4) == 0) { checked = false; mark_off = 1; }
    else if (strncmp(p, "[x] ", 4) == 0 || strncmp(p, "[X] ", 4) == 0) {
        checked = true;
        mark_off = 1;
    } else {
        return NULL;
    }
    size_t total = strlen(body);
    size_t pos = (size_t)(p - body) + mark_off;
    char *out = malloc(total + 1);
    if (!out) return NULL;
    memcpy(out, body, total + 1);
    out[pos] = checked ? ' ' : 'x';
    if (now_checked) *now_checked = !checked;
    return out;
}

void nm_checklist_count(const char *body, size_t *done, size_t *total)
{
    size_t d = 0, t = 0;
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, "[ ] ", 4) == 0) t++;
        else if (strncmp(p, "[x] ", 4) == 0 || strncmp(p, "[X] ", 4) == 0) {
            t++;
            d++;
        }
        const char *eol = strchr(p, '\n');
        p = eol ? eol + 1 : NULL;
    }
    if (done) *done = d;
    if (total) *total = t;
}
