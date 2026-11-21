/* pkgmodel - see pkgmodel.h. */
#define _POSIX_C_SOURCE 200809L
#include "pkgmodel.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ================= Debian version compare ================= */

static int order_char(char c)
{
    if (c == '~') return -1;
    if (c >= '0' && c <= '9') return 0;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return (unsigned char)c;
    return (unsigned char)c + 256;
}

/* the verrevcmp algorithm from Debian policy */
static int verrevcmp(const char *a, const char *b)
{
    while (*a || *b) {
        int first_diff = 0;
        while ((*a && !(*a >= '0' && *a <= '9')) ||
               (*b && !(*b >= '0' && *b <= '9'))) {
            int oa = order_char(*a), ob = order_char(*b);
            if (oa != ob) return oa - ob;
            a++;
            b++;
        }
        while (*a == '0') a++;
        while (*b == '0') b++;
        while ((*a >= '0' && *a <= '9') && (*b >= '0' && *b <= '9')) {
            if (!first_diff) first_diff = *a - *b;
            a++;
            b++;
        }
        if (*a >= '0' && *a <= '9') return 1;
        if (*b >= '0' && *b <= '9') return -1;
        if (first_diff) return first_diff;
    }
    return 0;
}

static void split_ver(const char *v, long *epoch,
                      const char **upstream, const char **revision)
{
    *epoch = 0;
    const char *colon = strchr(v, ':');
    if (colon) {
        *epoch = strtol(v, NULL, 10);
        v = colon + 1;
    }
    const char *dash = strrchr(v, '-');
    *upstream = v;
    *revision = dash ? dash + 1 : "0";
}

int pkg_debver_cmp(const char *a, const char *b)
{
    long ea, eb;
    const char *ua, *ra, *ub, *rb;
    split_ver(a, &ea, &ua, &ra);
    split_ver(b, &eb, &ub, &rb);
    if (ea != eb) return ea < eb ? -1 : 1;
    /* upstream spans differ in length when a revision exists:
     * compare with explicit lengths */
    const char *da = strrchr(ua, '-');
    const char *db = strrchr(ub, '-');
    size_t la = da ? (size_t)(da - ua) : strlen(ua);
    size_t lb = db ? (size_t)(db - ub) : strlen(ub);
    char bufa[128], bufb[128];
    if (la >= sizeof(bufa)) la = sizeof(bufa) - 1;
    if (lb >= sizeof(bufb)) lb = sizeof(bufb) - 1;
    memcpy(bufa, ua, la);
    bufa[la] = 0;
    memcpy(bufb, ub, lb);
    bufb[lb] = 0;
    int c = verrevcmp(bufa, bufb);
    if (c) return c;
    return verrevcmp(ra, rb);
}

/* ================= runner ================= */

int pkg_runner_exec(void *ud, const char *const *argv,
                    char **out, size_t *out_len)
{
    (void)ud;
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(fds[1]);
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    ssize_t n;
    char chunk[4096];
    while ((n = read(fds[0], chunk, sizeof(chunk))) > 0) {
        if (out && buf) {
            if (len + (size_t)n + 1 > cap) {
                cap = (len + (size_t)n + 1) * 2;
                buf = realloc(buf, cap);
            }
            memcpy(buf + len, chunk, (size_t)n);
            len += (size_t)n;
        }
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (out && buf) {
        buf[len] = 0;
        *out = buf;
        if (out_len) *out_len = len;
    } else {
        free(buf);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* ================= system ================= */

struct pkg_sys {
    pkg_runner runner;
    void *ud;
    char *root;             /* NULL = live system */
    char *admin;
    pkg_entry *inst;
    size_t ni, capi;
    pkg_entry *cat;
    size_t nc, capc;
};

pkg_sys *pkg_open(pkg_runner runner, void *ud,
                  const char *dpkg_root, const char *dpkg_admin)
{
    pkg_sys *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->runner = runner ? runner : pkg_runner_exec;
    s->ud = ud;
    s->root = dpkg_root ? strdup(dpkg_root) : NULL;
    s->admin = dpkg_admin ? strdup(dpkg_admin) : NULL;
    return s;
}

void pkg_close(pkg_sys *s)
{
    if (!s) return;
    free(s->root);
    free(s->admin);
    free(s->inst);
    free(s->cat);
    free(s);
}

static void push_entry(pkg_entry **arr, size_t *n, size_t *cap,
                       const pkg_entry *e)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 32;
        *arr = realloc(*arr, *cap * sizeof(pkg_entry));
    }
    (*arr)[(*n)++] = *e;
}

/* Build the common option prefix; returns argc. */
static size_t opt_prefix(const pkg_sys *s, const char **argv, size_t max)
{
    size_t n = 0;
    if (s->root && n + 2 < max) {
        argv[n++] = "--root";
        argv[n++] = s->root;
    }
    if (s->admin && n + 2 < max) {
        argv[n++] = "--admindir";
        argv[n++] = s->admin;
    }
    if (n < max) argv[n] = NULL;
    return n;
}

int pkg_refresh_installed(pkg_sys *s)
{
    /* dpkg-query takes --admindir (not --root in older
     * versions); derive the admin dir from the root when only
     * a root was given. */
    const char *qargv[16];
    size_t qn = 0;
    static char adminbuf[1100];
    qargv[qn++] = "dpkg-query";
    if (s->admin) {
        qargv[qn++] = "--admindir";
        qargv[qn++] = s->admin;
    } else if (s->root) {
        snprintf(adminbuf, sizeof(adminbuf), "%s/var/lib/dpkg",
                 s->root);
        qargv[qn++] = "--admindir";
        qargv[qn++] = adminbuf;
    }
    qargv[qn++] = "-W";
    qargv[qn++] = "-f=${Package}\\t${Version}\\t${Architecture}\\t"
                  "${Section}\\t${Installed-Size}\\t${Status}\\n";
    qargv[qn] = NULL;
    char *out = NULL;
    size_t out_len = 0;
    int rc = s->runner(s->ud, qargv, &out, &out_len);
    s->ni = 0;
    if (rc != 0 || !out) {
        free(out);
        return rc ? rc : -1;
    }
    char *save = NULL;
    for (char *line = strtok_r(out, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        /* fields: name ver arch section size status... */
        char *f[6];
        int nf = 0;
        char *p = line;
        while (nf < 6) {
            f[nf++] = p;
            char *tab = strchr(p, '\t');
            if (!tab) break;
            *tab = 0;
            p = tab + 1;
        }
        if (nf < 5) continue;
        /* only fully installed packages */
        if (nf >= 6 && !strstr(f[5], "installed")) continue;
        pkg_entry e;
        memset(&e, 0, sizeof(e));
        snprintf(e.name, sizeof(e.name), "%s", f[0]);
        snprintf(e.version, sizeof(e.version), "%s", f[1]);
        snprintf(e.arch, sizeof(e.arch), "%s", f[2]);
        snprintf(e.section, sizeof(e.section), "%s", f[3]);
        e.installed_size_kb = strtoull(f[4], NULL, 10);
        push_entry(&s->inst, &s->ni, &s->capi, &e);
    }
    free(out);
    return 0;
}

size_t pkg_installed_count(const pkg_sys *s) { return s->ni; }
const pkg_entry *pkg_installed_at(const pkg_sys *s, size_t i)
{
    return i < s->ni ? &s->inst[i] : NULL;
}
const pkg_entry *pkg_installed_find(const pkg_sys *s, const char *name)
{
    for (size_t i = 0; i < s->ni; i++)
        if (strcmp(s->inst[i].name, name) == 0) return &s->inst[i];
    return NULL;
}

/* ---- catalog ---- */

static void parse_control(const char *text, pkg_entry *e)
{
    /* "Key: value" lines; continuation lines start with a
     * space (only the first Description line is kept) */
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        char line[600];
        if (ll >= sizeof(line)) ll = sizeof(line) - 1;
        memcpy(line, p, ll);
        line[ll] = 0;
        if (line[0] != ' ' && line[0] != '\t') {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = 0;
                const char *val = colon + 1;
                if (*val == ' ') val++;
                if (strcmp(line, "Package") == 0)
                    snprintf(e->name, sizeof(e->name), "%s", val);
                else if (strcmp(line, "Version") == 0)
                    snprintf(e->version, sizeof(e->version), "%s", val);
                else if (strcmp(line, "Architecture") == 0)
                    snprintf(e->arch, sizeof(e->arch), "%s", val);
                else if (strcmp(line, "Section") == 0)
                    snprintf(e->section, sizeof(e->section), "%s", val);
                else if (strcmp(line, "Maintainer") == 0)
                    snprintf(e->maintainer, sizeof(e->maintainer),
                             "%s", val);
                else if (strcmp(line, "Installed-Size") == 0)
                    e->installed_size_kb = strtoull(val, NULL, 10);
                else if (strcmp(line, "Description") == 0)
                    snprintf(e->description, sizeof(e->description),
                             "%s", val);
            }
        }
        p = eol ? eol + 1 : p + ll;
    }
}

int pkg_scan_catalog(pkg_sys *s, const char *dir)
{
    s->nc = 0;
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t ln = strlen(nm);
        if (ln < 5 || strcasecmp(nm + ln - 4, ".deb") != 0) continue;
        char path[1100];
        snprintf(path, sizeof(path), "%s/%s", dir, nm);
        const char *argv[5];
        argv[0] = "dpkg-deb";
        argv[1] = "-f";
        argv[2] = path;
        argv[3] = NULL;
        char *out = NULL;
        size_t out_len = 0;
        if (s->runner(s->ud, argv, &out, &out_len) != 0 || !out) {
            free(out);
            continue;
        }
        pkg_entry e;
        memset(&e, 0, sizeof(e));
        parse_control(out, &e);
        free(out);
        if (!e.name[0]) continue;
        snprintf(e.deb_path, sizeof(e.deb_path), "%s", path);
        push_entry(&s->cat, &s->nc, &s->capc, &e);
    }
    closedir(d);
    return 0;
}

size_t pkg_catalog_count(const pkg_sys *s) { return s->nc; }
const pkg_entry *pkg_catalog_at(const pkg_sys *s, size_t i)
{
    return i < s->nc ? &s->cat[i] : NULL;
}
const pkg_entry *pkg_catalog_find(const pkg_sys *s, const char *name)
{
    const pkg_entry *best = NULL;
    for (size_t i = 0; i < s->nc; i++) {
        if (strcmp(s->cat[i].name, name) != 0) continue;
        if (!best || pkg_debver_cmp(s->cat[i].version,
                                    best->version) > 0)
            best = &s->cat[i];
    }
    return best;
}

size_t pkg_updates_count(const pkg_sys *s)
{
    size_t n = 0;
    for (size_t i = 0; i < s->ni; i++) {
        const pkg_entry *c = pkg_catalog_find(s, s->inst[i].name);
        if (c && pkg_debver_cmp(c->version, s->inst[i].version) > 0)
            n++;
    }
    return n;
}

const pkg_entry *pkg_update_at(const pkg_sys *s, size_t i,
                               const pkg_entry **catalog_entry)
{
    size_t seen = 0;
    for (size_t j = 0; j < s->ni; j++) {
        const pkg_entry *c = pkg_catalog_find(s, s->inst[j].name);
        if (c && pkg_debver_cmp(c->version, s->inst[j].version) > 0) {
            if (seen == i) {
                if (catalog_entry) *catalog_entry = c;
                return &s->inst[j];
            }
            seen++;
        }
    }
    return NULL;
}

/* ---- actions ---- */

size_t pkg_install_argv(const pkg_sys *s, const char *deb_path,
                        const char **argv_out, size_t max)
{
    size_t n = 0;
    if (n < max) argv_out[n++] = "dpkg";
    n += opt_prefix(s, argv_out + n, max - n);
    if (n < max) argv_out[n++] = "-i";
    if (n < max) argv_out[n++] = deb_path;
    if (n < max) argv_out[n] = NULL;
    return n;
}

size_t pkg_remove_argv(const pkg_sys *s, const char *name,
                       const char **argv_out, size_t max)
{
    size_t n = 0;
    if (n < max) argv_out[n++] = "dpkg";
    n += opt_prefix(s, argv_out + n, max - n);
    if (n < max) argv_out[n++] = "-r";
    if (n < max) argv_out[n++] = name;
    if (n < max) argv_out[n] = NULL;
    return n;
}

static void argv_to_log(const char *const *argv, char *log, size_t cap)
{
    size_t w = 0;
    if (cap) log[0] = 0;
    for (size_t i = 0; argv[i] && w + 1 < cap; i++)
        w += (size_t)snprintf(log + w, cap - w, "%s%s", i ? " " : "",
                              argv[i]);
}

int pkg_install(pkg_sys *s, const char *deb_path, bool dry_run,
                char *log, size_t cap)
{
    const char *argv[12];
    pkg_install_argv(s, deb_path, argv, 12);
    if (dry_run) {
        if (log) {
            char tmp[1200];
            argv_to_log(argv, tmp, sizeof(tmp));
            snprintf(log, cap, "[dry-run] %s", tmp);
        }
        return 0;
    }
    char *out = NULL;
    size_t out_len = 0;
    int rc = s->runner(s->ud, argv, &out, &out_len);
    if (log) {
        if (rc == 0)
            snprintf(log, cap, "インストール完了: %s", deb_path);
        else
            snprintf(log, cap, "dpkg エラー (rc=%d): %.900s", rc,
                     out ? out : "");
    }
    free(out);
    return rc;
}

int pkg_remove(pkg_sys *s, const char *name, bool dry_run,
               char *log, size_t cap)
{
    const char *argv[12];
    pkg_remove_argv(s, name, argv, 12);
    if (dry_run) {
        if (log) {
            char tmp[1200];
            argv_to_log(argv, tmp, sizeof(tmp));
            snprintf(log, cap, "[dry-run] %s", tmp);
        }
        return 0;
    }
    char *out = NULL;
    size_t out_len = 0;
    int rc = s->runner(s->ud, argv, &out, &out_len);
    if (log) {
        if (rc == 0)
            snprintf(log, cap, "削除完了: %s", name);
        else
            snprintf(log, cap, "dpkg エラー (rc=%d): %.900s", rc,
                     out ? out : "");
    }
    free(out);
    return rc;
}
