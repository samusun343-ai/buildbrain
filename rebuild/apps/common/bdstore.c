#define _POSIX_C_SOURCE 200809L
#include "bdstore.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int bd_fsync_dir_of(const char *file_path)
{
    char dir[4096];
    if (bd_dirname(file_path, dir, sizeof(dir)) != 0) return -1;
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    int rc = fsync(fd);
    close(fd);
    return rc;
}

int bd_write_atomic(const char *path, const void *data, size_t len)
{
    char tmp[4096];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    const char *p = (const char *)data;
    size_t left = len;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return -1;
        }
        p += w;
        left -= (size_t)w;
    }
    if (fsync(fd) != 0) { close(fd); unlink(tmp); return -1; }
    if (close(fd) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    (void)bd_fsync_dir_of(path);
    return 0;
}

int bd_write_atomic_str(const char *path, const char *text)
{
    return bd_write_atomic(path, text, strlen(text));
}

char *bd_read_all(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { fclose(f); return NULL; }
    for (;;) {
        if (len == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap + 1);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
        }
        size_t r = fread(buf + len, 1, cap - len, f);
        if (r == 0) break;
        len += r;
    }
    fclose(f);
    buf[len] = '\0';
    if (len_out) *len_out = len;
    return buf;
}

int bd_mkdir_p(const char *path)
{
    if (!path || !*path) { errno = EINVAL; return -1; }
    char tmp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    memcpy(tmp, path, len + 1);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char save = tmp[i];
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            tmp[i] = save;
        }
    }
    struct stat st;
    if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

int bd_copy_atomic(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    struct stat st;
    if (fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(in);
        errno = EINVAL;
        return -1;
    }
    char tmp[4096];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", dst, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        close(in);
        errno = ENAMETOOLONG;
        return -1;
    }
    int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return -1; }
    char buf[16384];
    int rc = 0;
    for (;;) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            rc = -1;
            break;
        }
        if (r == 0) break;
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                rc = -1;
                break;
            }
            off += w;
        }
        if (rc != 0) break;
    }
    if (rc == 0 && fsync(out) != 0) rc = -1;
    if (close(out) != 0) rc = -1;
    close(in);
    if (rc != 0) { unlink(tmp); return -1; }
    if (rename(tmp, dst) != 0) { unlink(tmp); return -1; }
    (void)bd_fsync_dir_of(dst);
    return 0;
}

int bd_move_path(const char *src, const char *dst)
{
    if (rename(src, dst) == 0) {
        (void)bd_fsync_dir_of(dst);
        return 0;
    }
    if (errno != EXDEV) return -1;
    struct stat st;
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        errno = EXDEV;
        return -1;
    }
    if (bd_copy_atomic(src, dst) != 0) return -1;
    if (unlink(src) != 0) return -1;
    return 0;
}

int bd_remove_recursive(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0 ? 1 : -1;
    }
    DIR *d = opendir(path);
    if (!d) return -1;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char child[4096];
        if (bd_path_join(child, sizeof(child), path, de->d_name) != 0) {
            closedir(d);
            errno = ENAMETOOLONG;
            return -1;
        }
        int r = bd_remove_recursive(child);
        if (r < 0) { closedir(d); return -1; }
        count += r;
    }
    closedir(d);
    if (rmdir(path) != 0) return -1;
    return count + 1;
}

int bd_sweep_tmp(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int removed = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strstr(de->d_name, ".tmp.") == NULL) continue;
        char p[4096];
        if (bd_path_join(p, sizeof(p), dir, de->d_name) != 0) continue;
        struct stat st;
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode) && unlink(p) == 0)
            removed++;
    }
    closedir(d);
    return removed;
}

int bd_unique_path(const char *path, char *out, size_t cap)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        if (strlen(path) + 1 > cap) return -1;
        strcpy(out, path);
        return 0;
    }
    /* split dir / stem / ext */
    const char *base = bd_basename(path);
    const char *dot = NULL;
    if (base[0] != '.') { /* a leading dot is not an extension */
        const char *p = strrchr(base, '.');
        if (p && p != base) dot = p;
    }
    for (int i = 2; i < 10000; i++) {
        int n;
        if (dot) {
            n = snprintf(out, cap, "%.*s %d%s",
                         (int)(dot - path), path, i, dot);
        } else {
            n = snprintf(out, cap, "%s %d", path, i);
        }
        if (n < 0 || (size_t)n >= cap) return -1;
        if (stat(out, &st) != 0) return 0;
    }
    return -1;
}

int bd_path_join(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    while (la > 1 && a[la - 1] == '/') la--;
    int n;
    if (la == 1 && a[0] == '/')
        n = snprintf(out, cap, "/%s", b);
    else if (la == 0)
        n = snprintf(out, cap, "%s", b);
    else
        n = snprintf(out, cap, "%.*s/%s", (int)la, a, b);
    if (n < 0 || (size_t)n >= cap) { errno = ENAMETOOLONG; return -1; }
    return 0;
}

const char *bd_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int bd_dirname(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        if (cap < 2) return -1;
        strcpy(out, ".");
        return 0;
    }
    if (slash == path) {
        if (cap < 2) return -1;
        strcpy(out, "/");
        return 0;
    }
    size_t len = (size_t)(slash - path);
    if (len + 1 > cap) return -1;
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

void bd_format_size(uint64_t bytes, char *out, size_t cap)
{
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    if (u == 0)
        snprintf(out, cap, "%llu B", (unsigned long long)bytes);
    else
        snprintf(out, cap, "%.1f %s", v, units[u]);
}

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

bool bd_contains_ci(const char *haystack, const char *needle)
{
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    size_t nl = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               ascii_lower((unsigned char)p[i]) ==
                   ascii_lower((unsigned char)needle[i]))
            i++;
        if (i == nl) return true;
    }
    return false;
}

size_t bd_escape(const char *in, char *out, size_t cap)
{
    size_t need = 0;
    for (const char *p = in; *p; p++) {
        const char *rep = NULL;
        char two[2] = { *p, '\0' };
        switch (*p) {
        case '\\': rep = "\\\\"; break;
        case '\t': rep = "\\t"; break;
        case '\n': rep = "\\n"; break;
        case '\r': rep = "\\r"; break;
        default: rep = two; break;
        }
        size_t rl = strlen(rep);
        if (out && need + rl < cap) memcpy(out + need, rep, rl);
        need += rl;
    }
    if (out && cap > 0) out[need < cap ? need : cap - 1] = '\0';
    return need;
}

size_t bd_unescape(const char *in, char *out, size_t cap)
{
    size_t w = 0;
    for (const char *p = in; *p; p++) {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 't': c = '\t'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case '\\': c = '\\'; break;
            default: c = *p; break;
            }
        }
        if (out && w + 1 < cap) out[w] = c;
        w++;
    }
    if (out && cap > 0) out[w < cap ? w : cap - 1] = '\0';
    return w;
}
