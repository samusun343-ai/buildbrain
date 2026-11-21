#define _POSIX_C_SOURCE 200809L
#include "fmodel.h"
#include "bdstore.h"
#include "bdmime.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------------------------------------------------------- */
/* listing                                                           */
/* ---------------------------------------------------------------- */

static void entry_fill(fm_entry *e, const char *dir, const char *name,
                       const struct stat *st, bool is_link)
{
    e->name = strdup(name);
    char p[FM_PATH_MAX];
    if (bd_path_join(p, sizeof(p), dir, name) == 0) e->path = strdup(p);
    else e->path = strdup(name);
    e->is_dir = S_ISDIR(st->st_mode);
    e->is_link = is_link;
    e->hidden = name[0] == '.';
    e->size = (uint64_t)st->st_size;
    e->mtime = (int64_t)st->st_mtime;
    e->mode = st->st_mode;
    const char *kind = bd_mime_kind(e->path, e->is_dir);
    snprintf(e->kind, sizeof(e->kind), "%s", kind);
}

int fm_listing_load(fm_listing *l, const char *path, bool show_hidden)
{
    memset(l, 0, sizeof(*l));
    DIR *d = opendir(path);
    if (!d) return -1;
    size_t cap = 0;
    struct dirent *de;
    int saved_errno = 0;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!show_hidden && name[0] == '.') continue;
        char full[FM_PATH_MAX];
        if (bd_path_join(full, sizeof(full), path, name) != 0) continue;
        struct stat lst, st;
        if (lstat(full, &lst) != 0) continue;
        bool is_link = S_ISLNK(lst.st_mode);
        st = lst;
        if (is_link) {
            struct stat tgt;
            if (stat(full, &tgt) == 0) st = tgt; /* follow for kind/size */
        }
        if (l->n == cap) {
            cap = cap ? cap * 2 : 32;
            fm_entry *nv = realloc(l->v, cap * sizeof(fm_entry));
            if (!nv) { saved_errno = ENOMEM; break; }
            l->v = nv;
        }
        entry_fill(&l->v[l->n], path, name, &st, is_link);
        l->n++;
    }
    closedir(d);
    if (saved_errno) {
        fm_listing_free(l);
        errno = saved_errno;
        return -1;
    }
    l->path = strdup(path);
    return 0;
}

void fm_listing_free(fm_listing *l)
{
    if (!l) return;
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].name);
        free(l->v[i].path);
    }
    free(l->v);
    free(l->path);
    memset(l, 0, sizeof(*l));
}

static fm_sort_key g_key;
static bool g_desc, g_dirs_first;

static int entry_cmp(const void *a, const void *b)
{
    const fm_entry *x = a, *y = b;
    if (g_dirs_first && x->is_dir != y->is_dir)
        return x->is_dir ? -1 : 1;
    int c = 0;
    switch (g_key) {
    case FM_SORT_NAME: {
        /* case-insensitive, then exact for stability */
        const char *p = x->name, *q = y->name;
        while (*p && *q) {
            int lp = (*p >= 'A' && *p <= 'Z') ? *p + 32 : (unsigned char)*p;
            int lq = (*q >= 'A' && *q <= 'Z') ? *q + 32 : (unsigned char)*q;
            if (lp != lq) { c = lp - lq; break; }
            p++; q++;
        }
        if (c == 0) c = strcmp(x->name, y->name);
        break;
    }
    case FM_SORT_MTIME:
        c = (x->mtime < y->mtime) ? -1 : (x->mtime > y->mtime) ? 1 : 0;
        break;
    case FM_SORT_SIZE:
        c = (x->size < y->size) ? -1 : (x->size > y->size) ? 1 : 0;
        break;
    case FM_SORT_TYPE:
        c = strcmp(x->kind, y->kind);
        if (c == 0) c = strcmp(x->name, y->name);
        break;
    }
    if (c == 0 && g_key != FM_SORT_NAME && g_key != FM_SORT_TYPE)
        c = strcmp(x->name, y->name);
    return g_desc ? -c : c;
}

void fm_listing_sort(fm_listing *l, fm_sort_key key, bool desc,
                     bool dirs_first)
{
    g_key = key;
    g_desc = desc;
    g_dirs_first = dirs_first;
    if (l->n > 1) qsort(l->v, l->n, sizeof(fm_entry), entry_cmp);
}

int fm_entry_group(const fm_entry *e, fm_group g,
                   int64_t today_start, int64_t yesterday_start,
                   char *label, size_t cap)
{
    switch (g) {
    case FM_GROUP_NONE:
        if (cap) label[0] = '\0';
        return 0;
    case FM_GROUP_TYPE:
        snprintf(label, cap, "%s", e->kind);
        return 1;
    case FM_GROUP_DATE:
        if (e->mtime >= today_start) {
            snprintf(label, cap, "Today");
            return 2;
        }
        if (e->mtime >= yesterday_start) {
            snprintf(label, cap, "Yesterday");
            return 3;
        }
        snprintf(label, cap, "Earlier");
        return 4;
    }
    if (cap) label[0] = '\0';
    return 0;
}

/* ---------------------------------------------------------------- */
/* search                                                            */
/* ---------------------------------------------------------------- */

typedef struct {
    const char *query;
    bool show_hidden;
    fm_listing *out;
    size_t cap;
    bool truncated;
} search_ctx;

static void search_walk(search_ctx *ctx, const char *dir, int depth)
{
    if (ctx->out->n >= ctx->cap) { ctx->truncated = true; return; }
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!ctx->show_hidden && name[0] == '.') continue;
        char full[FM_PATH_MAX];
        if (bd_path_join(full, sizeof(full), dir, name) != 0) continue;
        struct stat lst, st;
        if (lstat(full, &lst) != 0) continue;
        bool is_link = S_ISLNK(lst.st_mode);
        st = lst;
        if (is_link && stat(full, &st) != 0) st = lst;
        if (bd_contains_ci(name, ctx->query)) {
            if (ctx->out->n >= ctx->cap) { ctx->truncated = true; break; }
            fm_entry *e = &ctx->out->v[ctx->out->n];
            memset(e, 0, sizeof(*e));
            entry_fill(e, dir, name, &st, is_link);
            ctx->out->n++;
        }
        if (S_ISDIR(st.st_mode) && !is_link) {
            if (depth + 1 > FM_SEARCH_MAX_DEPTH) ctx->truncated = true;
            else search_walk(ctx, full, depth + 1);
        }
    }
    closedir(d);
}

int fm_search(const char *root, const char *query, bool show_hidden,
              fm_listing *out, size_t cap, bool *truncated)
{
    memset(out, 0, sizeof(*out));
    out->v = calloc(cap ? cap : 1, sizeof(fm_entry));
    if (!out->v) { errno = ENOMEM; return -1; }
    out->path = strdup(root);
    search_ctx ctx = { query, show_hidden, out, cap, false };
    search_walk(&ctx, root, 0);
    if (truncated) *truncated = ctx.truncated;
    return 0;
}

/* ---------------------------------------------------------------- */
/* file operations                                                   */
/* ---------------------------------------------------------------- */

static int join_new(const char *dir, const char *name,
                    char *out, size_t cap)
{
    if (!name || !*name || strchr(name, '/')) {
        errno = EINVAL;
        return -1;
    }
    return bd_path_join(out, cap, dir, name);
}

int fm_mkdir(const char *dir, const char *name, char *out, size_t cap)
{
    char p[FM_PATH_MAX];
    if (join_new(dir, name, p, sizeof(p)) != 0) return -1;
    if (mkdir(p, 0755) != 0) return -1;
    (void)bd_fsync_dir_of(p);
    if (out) {
        if (strlen(p) + 1 > cap) return -1;
        strcpy(out, p);
    }
    return 0;
}

int fm_create_file(const char *dir, const char *name, char *out, size_t cap)
{
    char p[FM_PATH_MAX];
    if (join_new(dir, name, p, sizeof(p)) != 0) return -1;
    struct stat st;
    if (lstat(p, &st) == 0) { errno = EEXIST; return -1; }
    if (bd_write_atomic(p, "", 0) != 0) return -1;
    if (out) {
        if (strlen(p) + 1 > cap) return -1;
        strcpy(out, p);
    }
    return 0;
}

int fm_rename(const char *path, const char *new_name,
              char *out, size_t cap)
{
    char dir[FM_PATH_MAX];
    if (bd_dirname(path, dir, sizeof(dir)) != 0) return -1;
    char p[FM_PATH_MAX];
    if (join_new(dir, new_name, p, sizeof(p)) != 0) return -1;
    struct stat st;
    if (lstat(p, &st) == 0) { errno = EEXIST; return -1; }
    if (rename(path, p) != 0) return -1;
    (void)bd_fsync_dir_of(p);
    if (out) {
        if (strlen(p) + 1 > cap) return -1;
        strcpy(out, p);
    }
    return 0;
}

static int copy_recursive(const char *src, const char *dst,
                          bool overwrite, int *copied)
{
    struct stat st;
    if (lstat(src, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        struct stat dst_st;
        if (stat(dst, &dst_st) == 0) {
            if (!S_ISDIR(dst_st.st_mode)) { errno = ENOTDIR; return -1; }
            if (!overwrite) { errno = EEXIST; return -1; }
        } else if (mkdir(dst, 0755) != 0) {
            return -1;
        }
        DIR *d = opendir(src);
        if (!d) return -1;
        int rc = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 ||
                strcmp(de->d_name, "..") == 0) continue;
            char s2[FM_PATH_MAX], d2[FM_PATH_MAX];
            if (bd_path_join(s2, sizeof(s2), src, de->d_name) != 0 ||
                bd_path_join(d2, sizeof(d2), dst, de->d_name) != 0) {
                rc = -1;
                break;
            }
            if (copy_recursive(s2, d2, overwrite, copied) != 0) {
                rc = -1;
                break;
            }
        }
        closedir(d);
        return rc;
    }
    if (S_ISLNK(st.st_mode)) {
        char target[FM_PATH_MAX];
        ssize_t n = readlink(src, target, sizeof(target) - 1);
        if (n < 0) return -1;
        target[n] = '\0';
        struct stat dst_st;
        if (lstat(dst, &dst_st) == 0) {
            if (!overwrite) { errno = EEXIST; return -1; }
            if (bd_remove_recursive(dst) < 0) return -1;
        }
        if (symlink(target, dst) != 0) return -1;
        if (copied) (*copied)++;
        return 0;
    }
    /* regular file: atomic copy; without overwrite, refuse to
     * replace (bd_copy_atomic would rename over it). */
    struct stat dst_st;
    if (!overwrite && lstat(dst, &dst_st) == 0) {
        errno = EEXIST;
        return -1;
    }
    if (bd_copy_atomic(src, dst) != 0) return -1;
    /* preserve the permission bits (content atomicity first) */
    (void)chmod(dst, st.st_mode & 0777);
    if (copied) (*copied)++;
    return 0;
}

int fm_copy(const char *src, const char *dst_dir, bool overwrite,
            char *out, size_t cap, int *copied_out)
{
    char dst[FM_PATH_MAX];
    if (bd_path_join(dst, sizeof(dst), dst_dir, bd_basename(src)) != 0)
        return -1;
    /* refuse copying a directory into itself or its own subtree */
    size_t sl = strlen(src);
    if (strncmp(dst, src, sl) == 0 && dst[sl] == '/') {
        errno = EINVAL;
        return -1;
    }
    int copied = 0;
    if (copy_recursive(src, dst, overwrite, &copied) != 0) {
        if (copied_out) *copied_out = copied;
        return -1;
    }
    if (copied_out) *copied_out = copied;
    if (out) {
        if (strlen(dst) + 1 > cap) return -1;
        strcpy(out, dst);
    }
    return 0;
}

int fm_move(const char *src, const char *dst_dir, bool overwrite,
            char *out, size_t cap)
{
    char dst[FM_PATH_MAX];
    if (bd_path_join(dst, sizeof(dst), dst_dir, bd_basename(src)) != 0)
        return -1;
    if (strcmp(src, dst) == 0) { errno = EINVAL; return -1; }
    struct stat st;
    if (lstat(dst, &st) == 0) {
        if (!overwrite) { errno = EEXIST; return -1; }
        if (bd_remove_recursive(dst) < 0) return -1;
    }
    if (rename(src, dst) == 0) {
        (void)bd_fsync_dir_of(dst);
        if (out) {
            if (strlen(dst) + 1 > cap) return -1;
            strcpy(out, dst);
        }
        return 0;
    }
    if (errno != EXDEV) return -1;
    int copied = 0;
    if (copy_recursive(src, dst, true, &copied) != 0) return -1;
    if (bd_remove_recursive(src) < 0) return -1;
    if (out) {
        if (strlen(dst) + 1 > cap) return -1;
        strcpy(out, dst);
    }
    return 0;
}

int fm_delete_permanent(const char *path)
{
    return bd_remove_recursive(path) >= 0 ? 0 : -1;
}

/* ---------------------------------------------------------------- */
/* image dimensions (header sniff, no decode)                        */
/* ---------------------------------------------------------------- */

bool fm_image_dims(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char buf[64 * 1024];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 8) return false;
    /* PNG: 8-byte signature, IHDR at 16, w/h big-endian at 16/20 */
    static const unsigned char png_sig[8] =
        { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n >= 24 && memcmp(buf, png_sig, 8) == 0) {
        *w = (buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19];
        *h = (buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23];
        return *w > 0 && *h > 0;
    }
    /* GIF: "GIF8", w/h little-endian at 6/8 */
    if (n >= 10 && memcmp(buf, "GIF8", 4) == 0) {
        *w = buf[6] | (buf[7] << 8);
        *h = buf[8] | (buf[9] << 8);
        return *w > 0 && *h > 0;
    }
    /* JPEG: scan markers for a Start-Of-Frame */
    if (n >= 4 && buf[0] == 0xFF && buf[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 <= n) {
            if (buf[i] != 0xFF) { i++; continue; }
            unsigned marker = buf[i + 1];
            if (marker == 0xD8 || marker == 0x01 ||
                (marker >= 0xD0 && marker <= 0xD9)) { i += 2; continue; }
            unsigned seglen = (buf[i + 2] << 8) | buf[i + 3];
            if (seglen < 2) return false;
            bool sof = (marker >= 0xC0 && marker <= 0xCF &&
                        marker != 0xC4 && marker != 0xC8 && marker != 0xCC);
            if (sof) {
                *h = (buf[i + 5] << 8) | buf[i + 6];
                *w = (buf[i + 7] << 8) | buf[i + 8];
                return *w > 0 && *h > 0;
            }
            i += 2 + seglen;
        }
    }
    return false;
}

/* ---------------------------------------------------------------- */
/* properties                                                        */
/* ---------------------------------------------------------------- */

static void perms_str(mode_t m, bool is_dir, bool is_link, char out[11])
{
    out[0] = is_link ? 'l' : is_dir ? 'd' : '-';
    static const char rwx[] = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        out[1 + i] = (m & (1u << (8 - i))) ? rwx[i] : '-';
    out[10] = '\0';
}

typedef struct {
    fm_props *p;
    int depth;
} props_ctx;

static void props_walk(props_ctx *ctx, const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return;
    ctx->p->size_on_disk += (uint64_t)st.st_blocks * 512u;
    if (S_ISDIR(st.st_mode)) {
        ctx->p->n_dirs++;
        if (ctx->depth >= 32) { ctx->p->walk_truncated = true; return; }
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 ||
                strcmp(de->d_name, "..") == 0) continue;
            char child[FM_PATH_MAX];
            if (bd_path_join(child, sizeof(child), path, de->d_name) != 0) {
                ctx->p->walk_truncated = true;
                continue;
            }
            ctx->depth++;
            props_walk(ctx, child);
            ctx->depth--;
        }
        closedir(d);
    } else {
        ctx->p->n_files++;
        ctx->p->size += (uint64_t)st.st_size;
    }
}

int fm_props_load(const char *path, fm_props *out)
{
    memset(out, 0, sizeof(*out));
    struct stat lst;
    if (lstat(path, &lst) != 0) return -1;
    bool is_link = S_ISLNK(lst.st_mode);
    struct stat st = lst;
    if (is_link) (void)stat(path, &st);
    snprintf(out->name, sizeof(out->name), "%s", bd_basename(path));
    snprintf(out->path, sizeof(out->path), "%s", path);
    out->is_dir = S_ISDIR(st.st_mode);
    out->is_link = is_link;
    const char *kind = bd_mime_kind(path, out->is_dir);
    snprintf(out->kind, sizeof(out->kind), "%s", kind);
    out->ctime = (int64_t)st.st_ctime;
    out->mtime = (int64_t)st.st_mtime;
    out->atime = (int64_t)st.st_atime;
    out->mode = (unsigned)st.st_mode;
    out->uid = (int)st.st_uid;
    out->gid = (int)st.st_gid;
    out->readonly_flag = (st.st_mode & S_IWUSR) == 0;
    out->hidden_flag = out->name[0] == '.';
    perms_str(st.st_mode, out->is_dir, is_link, out->perms);
    if (out->is_dir) {
        props_ctx ctx = { out, 0 };
        /* count the directory itself via the walk's file branch
         * convention: seed size with 0 and let the walk add up
         * children; the dir's own blocks count as disk usage. */
        out->size_on_disk = (uint64_t)lst.st_blocks * 512u;
        DIR *d = opendir(path);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 ||
                    strcmp(de->d_name, "..") == 0) continue;
                char child[FM_PATH_MAX];
                if (bd_path_join(child, sizeof(child), path, de->d_name) != 0)
                    continue;
                props_walk(&ctx, child);
            }
            closedir(d);
        }
    } else {
        out->size = (uint64_t)st.st_size;
        out->size_on_disk = (uint64_t)st.st_blocks * 512u;
        out->n_files = 1;
    }
    int w = 0, h = 0;
    if (!out->is_dir && fm_image_dims(path, &w, &h)) {
        out->img_w = w;
        out->img_h = h;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* trash                                                             */
/* ---------------------------------------------------------------- */

struct fm_trash {
    char *root;
    char *files_dir;
    char *info_dir;
    uint64_t budget;
    uint64_t next_seq;
    uint64_t total;
};

static void trash_paths(const fm_trash *t, uint64_t id, const char *name,
                        char *fp, size_t fcap, char *ip, size_t icap)
{
    char fn[300];
    snprintf(fn, sizeof(fn), "%llu_%s", (unsigned long long)id,
             name ? name : "item");
    /* '/' in a name would break the layout; items are moved with
     * their basename only, so this cannot happen in practice. */
    for (char *p = fn; *p; p++) if (*p == '/') *p = '_';
    if (fp) (void)bd_path_join(fp, fcap, t->files_dir, fn);
    if (ip) {
        char in[64];
        snprintf(in, sizeof(in), "%llu.info", (unsigned long long)id);
        (void)bd_path_join(ip, icap, t->info_dir, in);
    }
}

static int trash_scan(fm_trash *t, fm_trash_list *out)
{
    memset(out, 0, sizeof(*out));
    DIR *d = opendir(t->info_dir);
    if (!d) return -1;
    size_t cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t len = strlen(nm);
        if (len < 6 || strcmp(nm + len - 5, ".info") != 0) continue;
        char ip[FM_PATH_MAX];
        if (bd_path_join(ip, sizeof(ip), t->info_dir, nm) != 0) continue;
        size_t flen = 0;
        char *text = bd_read_all(ip, &flen);
        if (!text) continue;
        fm_trash_entry e;
        memset(&e, 0, sizeof(e));
        e.id = strtoull(nm, NULL, 10);
        char orig[FM_PATH_MAX] = "";
        /* info format: orig=<escaped>\nsize=<n>\ndir=<0|1>\n */
        for (char *line = strtok(text, "\n"); line;
             line = strtok(NULL, "\n")) {
            if (strncmp(line, "orig=", 5) == 0) {
                char un[FM_PATH_MAX];
                bd_unescape(line + 5, un, sizeof(un));
                snprintf(orig, sizeof(orig), "%s", un);
            } else if (strncmp(line, "size=", 5) == 0) {
                e.size = strtoull(line + 5, NULL, 10);
            } else if (strncmp(line, "dir=", 4) == 0) {
                e.is_dir = line[4] == '1';
            }
        }
        free(text);
        e.orig_path = strdup(orig);
        e.name = strdup(orig[0] ? bd_basename(orig) : "item");
        if (out->n == cap) {
            cap = cap ? cap * 2 : 16;
            fm_trash_entry *nv = realloc(out->v, cap * sizeof(*nv));
            if (!nv) { free(e.orig_path); free(e.name); break; }
            out->v = nv;
        }
        out->v[out->n++] = e;
    }
    closedir(d);
    /* sort by id ascending (insertion order) */
    for (size_t i = 1; i < out->n; i++) {
        fm_trash_entry tmp = out->v[i];
        size_t j = i;
        while (j > 0 && out->v[j - 1].id > tmp.id) {
            out->v[j] = out->v[j - 1];
            j--;
        }
        out->v[j] = tmp;
    }
    return 0;
}

void fm_trash_list_free(fm_trash_list *l)
{
    if (!l) return;
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].name);
        free(l->v[i].orig_path);
    }
    free(l->v);
    memset(l, 0, sizeof(*l));
}

static uint64_t tree_size(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return (uint64_t)st.st_size;
    uint64_t total = 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char child[FM_PATH_MAX];
        if (bd_path_join(child, sizeof(child), path, de->d_name) == 0)
            total += tree_size(child);
    }
    closedir(d);
    return total;
}

/* Find the stored files/ path for an id by scanning for "<id>_". */
static bool trash_file_for(const fm_trash *t, uint64_t id,
                           char *out, size_t cap)
{
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%llu_", (unsigned long long)id);
    size_t pl = strlen(prefix);
    DIR *d = opendir(t->files_dir);
    if (!d) return false;
    bool found = false;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, prefix, pl) == 0) {
            if (bd_path_join(out, cap, t->files_dir, de->d_name) == 0)
                found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static int trash_write_info(fm_trash *t, uint64_t id, const char *orig,
                            uint64_t size, bool is_dir)
{
    char esc[FM_PATH_MAX * 2];
    bd_escape(orig, esc, sizeof(esc));
    char body[FM_PATH_MAX * 2 + 128];
    snprintf(body, sizeof(body), "orig=%s\nsize=%llu\ndir=%d\n",
             esc, (unsigned long long)size, is_dir ? 1 : 0);
    char ip[FM_PATH_MAX];
    trash_paths(t, id, NULL, NULL, 0, ip, sizeof(ip));
    return bd_write_atomic_str(ip, body);
}

static void trash_save_seq(fm_trash *t)
{
    char p[FM_PATH_MAX];
    if (bd_path_join(p, sizeof(p), t->root, "seq") != 0) return;
    char body[32];
    snprintf(body, sizeof(body), "%llu\n", (unsigned long long)t->next_seq);
    (void)bd_write_atomic_str(p, body);
}

static int trash_enforce_budget(fm_trash *t)
{
    int evicted = 0;
    while (t->total > t->budget) {
        fm_trash_list l;
        if (trash_scan(t, &l) != 0) break;
        if (l.n == 0) { fm_trash_list_free(&l); break; }
        uint64_t victim = l.v[0].id; /* lowest sequence = oldest in */
        fm_trash_list_free(&l);
        if (fm_trash_delete(t, victim) != 0) break; /* adjusts total */
        evicted++;
    }
    return evicted;
}

/* Move a file or directory tree to an exact destination path,
 * across devices when needed (copy + remove fallback). Used by
 * the trash, whose home (rootfs) may be a different filesystem
 * than the file being trashed (e.g. the FAT boot partition). */
static int move_any(const char *src, const char *dst)
{
    if (rename(src, dst) == 0) {
        (void)bd_fsync_dir_of(dst);
        return 0;
    }
    if (errno != EXDEV) return -1;
    int copied = 0;
    if (copy_recursive(src, dst, true, &copied) != 0) return -1;
    if (bd_remove_recursive(src) < 0) return -1;
    return 0;
}

fm_trash *fm_trash_open(const char *root, uint64_t budget_bytes)
{
    fm_trash *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->root = strdup(root);
    char p[FM_PATH_MAX];
    if (bd_path_join(p, sizeof(p), root, "files") != 0) goto fail;
    t->files_dir = strdup(p);
    if (bd_path_join(p, sizeof(p), root, "info") != 0) goto fail;
    t->info_dir = strdup(p);
    t->budget = budget_bytes;
    if (bd_mkdir_p(t->files_dir) != 0 || bd_mkdir_p(t->info_dir) != 0)
        goto fail;
    (void)bd_sweep_tmp(t->info_dir);
    /* next sequence: persisted counter, rebuilt from infos when
     * the counter file is missing/corrupt (self-healing). */
    t->next_seq = 1;
    if (bd_path_join(p, sizeof(p), root, "seq") == 0) {
        size_t len = 0;
        char *s = bd_read_all(p, &len);
        if (s) {
            t->next_seq = strtoull(s, NULL, 10);
            free(s);
        }
    }
    {
        fm_trash_list l;
        t->total = 0;
        if (trash_scan(t, &l) == 0) {
            for (size_t i = 0; i < l.n; i++) {
                t->total += l.v[i].size;
                if (l.v[i].id >= t->next_seq) t->next_seq = l.v[i].id + 1;
            }
            fm_trash_list_free(&l);
        }
    }
    /* Adopt orphans: files/ entries without an info file. They get
     * an info with an empty origin (restore uses the fallback). */
    {
        DIR *d = opendir(t->files_dir);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char *under = strchr(de->d_name, '_');
                if (!under) continue;
                uint64_t id = strtoull(de->d_name, NULL, 10);
                if (id == 0) continue;
                char ip[FM_PATH_MAX];
                trash_paths(t, id, NULL, NULL, 0, ip, sizeof(ip));
                struct stat st;
                if (stat(ip, &st) == 0) continue; /* has info */
                char fp[FM_PATH_MAX];
                if (bd_path_join(fp, sizeof(fp), t->files_dir,
                                 de->d_name) != 0) continue;
                struct stat fst;
                if (stat(fp, &fst) != 0) continue;
                uint64_t sz = tree_size(fp);
                if (trash_write_info(t, id, "", sz,
                                     S_ISDIR(fst.st_mode)) == 0) {
                    t->total += sz;
                    if (id >= t->next_seq) t->next_seq = id + 1;
                }
            }
            closedir(d);
        }
    }
    /* Drop infos whose files/ entry vanished (interrupted delete). */
    {
        fm_trash_list l;
        if (trash_scan(t, &l) == 0) {
            for (size_t i = 0; i < l.n; i++) {
                char fp[FM_PATH_MAX];
                if (!trash_file_for(t, l.v[i].id, fp, sizeof(fp))) {
                    char ip[FM_PATH_MAX];
                    trash_paths(t, l.v[i].id, NULL, NULL, 0, ip, sizeof(ip));
                    (void)unlink(ip);
                    t->total -= l.v[i].size;
                }
            }
            fm_trash_list_free(&l);
        }
    }
    return t;
fail:
    fm_trash_close(t);
    return NULL;
}

void fm_trash_close(fm_trash *t)
{
    if (!t) return;
    free(t->root);
    free(t->files_dir);
    free(t->info_dir);
    free(t);
}

uint64_t fm_trash_budget(const fm_trash *t) { return t->budget; }
uint64_t fm_trash_total_bytes(const fm_trash *t) { return t->total; }

int fm_trash_add(fm_trash *t, const char *path, int *evicted_out)
{
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    uint64_t size = tree_size(path);
    bool is_dir = S_ISDIR(st.st_mode);
    uint64_t id = t->next_seq++;
    trash_save_seq(t);
    char fp[FM_PATH_MAX], ip[FM_PATH_MAX];
    trash_paths(t, id, bd_basename(path), fp, sizeof(fp), ip, sizeof(ip));
    /* Order matters for crash safety: move the payload first, then
     * publish the info atomically. A crash between the two leaves
     * an orphan payload, which fm_trash_open adopts. */
    if (move_any(path, fp) != 0) return -1;
    if (trash_write_info(t, id, path, size, is_dir) != 0) {
        /* Try to put it back; if that fails the orphan adoption
         * still keeps the data reachable from the trash. */
        (void)move_any(fp, path);
        return -1;
    }
    t->total += size;
    int evicted = trash_enforce_budget(t);
    if (evicted_out) *evicted_out = evicted;
    return 0;
}

int fm_trash_list_all(fm_trash *t, fm_trash_list *out)
{
    return trash_scan(t, out);
}

int fm_trash_restore(fm_trash *t, uint64_t id, const char *fallback_dir,
                     char *out, size_t cap)
{
    fm_trash_list l;
    if (trash_scan(t, &l) != 0) return -1;
    const fm_trash_entry *e = NULL;
    for (size_t i = 0; i < l.n; i++)
        if (l.v[i].id == id) { e = &l.v[i]; break; }
    if (!e) { fm_trash_list_free(&l); errno = ENOENT; return -1; }
    char fp[FM_PATH_MAX];
    if (!trash_file_for(t, id, fp, sizeof(fp))) {
        fm_trash_list_free(&l);
        errno = ENOENT;
        return -1;
    }
    char dst[FM_PATH_MAX];
    if (e->orig_path[0]) {
        snprintf(dst, sizeof(dst), "%s", e->orig_path);
        /* ensure the original parent still exists */
        char parent[FM_PATH_MAX];
        if (bd_dirname(dst, parent, sizeof(parent)) == 0)
            (void)bd_mkdir_p(parent);
    } else {
        if (bd_path_join(dst, sizeof(dst), fallback_dir, e->name) != 0) {
            fm_trash_list_free(&l);
            return -1;
        }
        (void)bd_mkdir_p(fallback_dir);
    }
    struct stat st;
    if (lstat(dst, &st) == 0) {
        char uniq[FM_PATH_MAX];
        if (bd_unique_path(dst, uniq, sizeof(uniq)) != 0) {
            fm_trash_list_free(&l);
            return -1;
        }
        snprintf(dst, sizeof(dst), "%s", uniq);
    }
    uint64_t size = e->size;
    if (move_any(fp, dst) != 0) {
        fm_trash_list_free(&l);
        return -1;
    }
    char ip[FM_PATH_MAX];
    trash_paths(t, id, NULL, NULL, 0, ip, sizeof(ip));
    (void)unlink(ip);
    (void)bd_fsync_dir_of(ip);
    t->total -= size;
    if (out) {
        if (strlen(dst) + 1 > cap) { fm_trash_list_free(&l); return -1; }
        strcpy(out, dst);
    }
    fm_trash_list_free(&l);
    return 0;
}

int fm_trash_delete(fm_trash *t, uint64_t id)
{
    fm_trash_list l;
    if (trash_scan(t, &l) != 0) return -1;
    uint64_t size = 0;
    bool found = false;
    for (size_t i = 0; i < l.n; i++)
        if (l.v[i].id == id) { size = l.v[i].size; found = true; break; }
    fm_trash_list_free(&l);
    if (!found) { errno = ENOENT; return -1; }
    char fp[FM_PATH_MAX];
    if (trash_file_for(t, id, fp, sizeof(fp)))
        (void)bd_remove_recursive(fp);
    char ip[FM_PATH_MAX];
    trash_paths(t, id, NULL, NULL, 0, ip, sizeof(ip));
    (void)unlink(ip);
    t->total -= size;
    return 0;
}

int fm_trash_empty(fm_trash *t)
{
    fm_trash_list l;
    if (trash_scan(t, &l) != 0) return -1;
    int rc = 0;
    for (size_t i = 0; i < l.n; i++)
        if (fm_trash_delete(t, l.v[i].id) != 0) rc = -1;
    fm_trash_list_free(&l);
    return rc;
}

/* ---------------------------------------------------------------- */
/* thumbnail/dimension cache                                         */
/* ---------------------------------------------------------------- */

typedef struct thumb_node {
    char *path;
    int64_t mtime;
    uint64_t size;
    int w, h;
    bool ok;
    struct thumb_node *prev, *next;
} thumb_node;

struct fm_thumbcache {
    size_t budget;
    size_t bytes;
    size_t count;
    unsigned long hits, misses;
    thumb_node *head, *tail; /* head = most recently used */
};

#define THUMB_ENTRY_OVERHEAD 96

static size_t thumb_cost(const thumb_node *n)
{
    return THUMB_ENTRY_OVERHEAD + strlen(n->path) + 1;
}

static void thumb_unlink(fm_thumbcache *c, thumb_node *n)
{
    if (n->prev) n->prev->next = n->next; else c->head = n->next;
    if (n->next) n->next->prev = n->prev; else c->tail = n->prev;
    n->prev = n->next = NULL;
}

static void thumb_push_front(fm_thumbcache *c, thumb_node *n)
{
    n->prev = NULL;
    n->next = c->head;
    if (c->head) c->head->prev = n;
    c->head = n;
    if (!c->tail) c->tail = n;
}

static void thumb_free_node(thumb_node *n)
{
    free(n->path);
    free(n);
}

fm_thumbcache *fm_thumbcache_new(size_t budget_bytes)
{
    fm_thumbcache *c = calloc(1, sizeof(*c));
    if (c) c->budget = budget_bytes;
    return c;
}

void fm_thumbcache_free(fm_thumbcache *c)
{
    if (!c) return;
    thumb_node *n = c->head;
    while (n) {
        thumb_node *next = n->next;
        thumb_free_node(n);
        n = next;
    }
    free(c);
}

bool fm_thumbcache_dims(fm_thumbcache *c, const char *path, int *w, int *h)
{
    struct stat st;
    if (stat(path, &st) != 0) { c->misses++; return false; }
    for (thumb_node *n = c->head; n; n = n->next) {
        if (strcmp(n->path, path) == 0 &&
            n->mtime == (int64_t)st.st_mtime &&
            n->size == (uint64_t)st.st_size) {
            thumb_unlink(c, n);
            thumb_push_front(c, n);
            c->hits++;
            if (n->ok) { *w = n->w; *h = n->h; }
            return n->ok;
        }
    }
    c->misses++;
    int dw = 0, dh = 0;
    bool ok = fm_image_dims(path, &dw, &dh);
    thumb_node *n = calloc(1, sizeof(*n));
    if (!n) return ok;
    n->path = strdup(path);
    n->mtime = (int64_t)st.st_mtime;
    n->size = (uint64_t)st.st_size;
    n->w = dw;
    n->h = dh;
    n->ok = ok;
    thumb_push_front(c, n);
    c->bytes += thumb_cost(n);
    c->count++;
    while (c->bytes > c->budget && c->tail && c->tail != n) {
        thumb_node *victim = c->tail;
        thumb_unlink(c, victim);
        c->bytes -= thumb_cost(victim);
        c->count--;
        thumb_free_node(victim);
    }
    if (ok) { *w = dw; *h = dh; }
    return ok;
}

size_t fm_thumbcache_count(const fm_thumbcache *c) { return c->count; }
size_t fm_thumbcache_bytes(const fm_thumbcache *c) { return c->bytes; }
unsigned long fm_thumbcache_hits(const fm_thumbcache *c) { return c->hits; }
unsigned long fm_thumbcache_misses(const fm_thumbcache *c) { return c->misses; }

/* ---------------------------------------------------------------- */
/* history + breadcrumbs                                             */
/* ---------------------------------------------------------------- */

void fm_history_init(fm_history *h) { memset(h, 0, sizeof(*h)); }

void fm_history_free(fm_history *h)
{
    for (size_t i = 0; i < h->n; i++) free(h->v[i]);
    free(h->v);
    memset(h, 0, sizeof(*h));
}

void fm_history_push(fm_history *h, const char *path)
{
    if (h->n > 0) {
        if (strcmp(h->v[h->pos], path) == 0) return;
        /* drop forward entries */
        for (size_t i = h->pos + 1; i < h->n; i++) free(h->v[i]);
        h->n = h->pos + 1;
    }
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 8;
        h->v = realloc(h->v, h->cap * sizeof(char *));
    }
    h->v[h->n] = strdup(path);
    h->pos = h->n;
    h->n++;
}

const char *fm_history_current(const fm_history *h)
{
    return h->n ? h->v[h->pos] : NULL;
}

const char *fm_history_back(fm_history *h)
{
    if (!fm_history_can_back(h)) return NULL;
    h->pos--;
    return h->v[h->pos];
}

const char *fm_history_forward(fm_history *h)
{
    if (!fm_history_can_forward(h)) return NULL;
    h->pos++;
    return h->v[h->pos];
}

bool fm_history_can_back(const fm_history *h)
{
    return h->n > 0 && h->pos > 0;
}

bool fm_history_can_forward(const fm_history *h)
{
    return h->n > 0 && h->pos + 1 < h->n;
}

int fm_crumb_count(const char *path)
{
    int n = 1; /* root */
    for (const char *p = path; *p; p++)
        if (*p == '/' && p[1] != '\0' && p[1] != '/') n++;
    /* "/a/b" -> slashes with following content: 2 -> crumbs 3 total
     * (root + a + b); the loop above counts '/' at pos0? For "/a/b":
     * p='/' next 'a' -> 2, p='/' next 'b' -> 3. Good. For "/": 1. */
    return n;
}

bool fm_crumb_at(const char *path, int idx,
                 char *label, size_t lcap, char *cpath, size_t pcap)
{
    if (idx < 0 || idx >= fm_crumb_count(path)) return false;
    if (idx == 0) {
        snprintf(label, lcap, "/");
        snprintf(cpath, pcap, "/");
        return true;
    }
    /* walk to the idx-th component */
    const char *p = path;
    int cur = 0;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != '/') p++;
        cur++;
        if (cur == idx) {
            size_t len = (size_t)(p - start);
            if (len + 1 > lcap) return false;
            memcpy(label, start, len);
            label[len] = '\0';
            size_t plen = (size_t)(p - path);
            if (plen + 1 > pcap) return false;
            memcpy(cpath, path, plen);
            cpath[plen] = '\0';
            return true;
        }
    }
    return false;
}
