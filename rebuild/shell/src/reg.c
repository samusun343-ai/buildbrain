/* reg.c - brainreg-compatible store. See reg.h for the contract.
 * Files are tiny; parsing is a plain line scan with no cache, the
 * same trade-off the old implementation made (settings reads are
 * rare, RAM is not). */
#include "reg.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void sys_dir(char *out, size_t n)
{
    const char *e = getenv("BRAINREG_SYS_DIR");
    snprintf(out, n, "%s", e ? e : "/etc/brainreg.d");
}

static void user_dir(char *out, size_t n)
{
    const char *e = getenv("BRAINREG_USER_DIR");
    if (e) { snprintf(out, n, "%s", e); return; }
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/.config/brainreg.d", home ? home : "/home/user");
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

typedef void (*reg_cb)(const char *sec, const char *key,
                       const char *val, void *ctx);

/* Scan one file. If cb != NULL every key is reported; otherwise the
 * (want_sec, want_key) value is collected into *found (later wins). */
static void scan_file(const char *path, const char *want_sec,
                      const char *want_key, char **found,
                      reg_cb cb, void *ctx)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[512], section[64] = "";
    while (fgets(line, sizeof(line), fp)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) { *end = 0; snprintf(section, sizeof(section), "%s", trim(s + 1)); }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        if (cb) {
            cb(section, k, v, ctx);
        } else if (found && strcmp(section, want_sec) == 0 &&
                   strcmp(k, want_key) == 0) {
            free(*found);
            *found = strdup(v);
        }
    }
    fclose(fp);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* Scan a directory's *.conf files in name order. */
static void scan_dir(const char *dir, const char *sec, const char *key,
                     char **found, reg_cb cb, void *ctx)
{
    DIR *d = opendir(dir);
    if (!d) return;
    char names[64][256];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 64) {
        size_t len = strlen(de->d_name);
        if (len > 5 && strcmp(de->d_name + len - 5, ".conf") == 0)
            snprintf(names[n++], 256, "%s", de->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, 256, cmp_str);
    for (int i = 0; i < n; i++) {
        char path[768];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        scan_file(path, sec, key, found, cb, ctx);
    }
}

static void env_name(const char *sec, const char *key, char *out, size_t n)
{
    snprintf(out, n, "BRAINREG_");
    size_t len = strlen(out);
    for (const char *p = sec; *p && len + 2 < n; p++)
        out[len++] = isalnum((unsigned char)*p) ? toupper((unsigned char)*p) : '_';
    out[len++] = '_';
    for (const char *p = key; *p && len + 2 < n; p++)
        out[len++] = isalnum((unsigned char)*p) ? toupper((unsigned char)*p) : '_';
    out[len] = 0;
}

char *reg_get(const char *section, const char *key)
{
    char name[160];
    env_name(section, key, name, sizeof(name));
    const char *e = getenv(name);
    if (e) return strdup(e);
    char dir[512], *found = NULL;
    sys_dir(dir, sizeof(dir));
    scan_dir(dir, section, key, &found, NULL, NULL);
    char *sysval = found;
    found = NULL;
    user_dir(dir, sizeof(dir));
    scan_dir(dir, section, key, &found, NULL, NULL);
    if (found) { free(sysval); return found; }
    return sysval;
}

int reg_get_int(const char *section, const char *key, int def)
{
    char *v = reg_get(section, key);
    if (!v) return def;
    int r = (int)strtol(v, NULL, 10);
    free(v);
    return r;
}

bool reg_get_bool(const char *section, const char *key, bool def)
{
    char *v = reg_get(section, key);
    if (!v) return def;
    bool r = def;
    if (!strcmp(v, "true") || !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "1"))
        r = true;
    else if (!strcmp(v, "false") || !strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "0"))
        r = false;
    free(v);
    return r;
}

/* Rewrite the user-layer file for one section with key set to value
 * (value NULL = remove). The file holds exactly one [section]. */
static int rewrite_user_section(const char *section, const char *key,
                                const char *value)
{
    char dir[512], path[768], tmp[800];
    user_dir(dir, sizeof(dir));
    /* mkdir -p without shelling out */
    for (char *p = dir + 1; ; p++) {
        if (*p == '/' || *p == 0) {
            char save = *p; *p = 0;
            mkdir(dir, 0755);
            *p = save;
            if (!save) break;
        }
    }
    snprintf(path, sizeof(path), "%s/%s.conf", dir, section);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *in = fopen(path, "r");
    FILE *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return -1; }
    fprintf(out, "[%s]\n", section);
    bool written = false;
    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char *s = trim(line);
            if (!*s || *s == '#' || *s == ';') {
                if (*s) fprintf(out, "%s\n", s);
                continue;
            }
            if (*s == '[') continue; /* we already wrote the header */
            char *eq = strchr(s, '=');
            if (!eq) continue;
            *eq = 0;
            char *k = trim(s);
            if (strcmp(k, key) == 0) {
                if (value && !written) {
                    fprintf(out, "%s = %s\n", key, value);
                    written = true;
                }
                continue;
            }
            fprintf(out, "%s = %s\n", k, trim(eq + 1));
        }
        fclose(in);
    }
    if (value && !written) fprintf(out, "%s = %s\n", key, value);
    /* Atomicity against power loss / reset at any moment (the
     * device resets often, DESIGN-shell-v2.md section 12):
     * flush + fsync the tmp file, rename it over the target
     * (atomic on POSIX), then fsync the directory so the rename
     * itself is durable. A crash leaves either the old file or
     * the new file, never a torn one; a leftover .tmp is ignored
     * by readers (scan only picks up *.conf). */
    fflush(out);
    if (fsync(fileno(out)) != 0) { /* best effort on exotic fs */ }
    fclose(out);
    if (rename(tmp, path) != 0) return -1;
    int dfd = open(dir, O_RDONLY);
    if (dfd >= 0) {
        if (fsync(dfd) != 0) { /* best effort */ }
        close(dfd);
    }
    return 0;
}

int reg_set(const char *section, const char *key, const char *value)
{
    return rewrite_user_section(section, key, value);
}

int reg_unset(const char *section, const char *key)
{
    return rewrite_user_section(section, key, NULL);
}

struct list_ctx { reg_entry *out; int max, n, source; };

static void list_cb(const char *sec, const char *key, const char *val, void *vctx)
{
    struct list_ctx *c = vctx;
    for (int i = 0; i < c->n; i++) {
        if (!strcmp(c->out[i].section, sec) && !strcmp(c->out[i].key, key)) {
            snprintf(c->out[i].value, 256, "%s", val);
            c->out[i].source = c->source;
            return;
        }
    }
    if (c->n < c->max) {
        reg_entry *e = &c->out[c->n++];
        snprintf(e->section, 64, "%s", sec);
        snprintf(e->key, 64, "%s", key);
        snprintf(e->value, 256, "%s", val);
        e->source = c->source;
    }
}

int reg_list(reg_entry *out, int max)
{
    struct list_ctx c = { out, max, 0, REG_SRC_SYSTEM };
    char dir[512];
    sys_dir(dir, sizeof(dir));
    scan_dir(dir, NULL, NULL, NULL, list_cb, &c);
    c.source = REG_SRC_USER;
    user_dir(dir, sizeof(dir));
    scan_dir(dir, NULL, NULL, NULL, list_cb, &c);
    for (int i = 0; i < c.n; i++) {
        char name[160];
        env_name(out[i].section, out[i].key, name, sizeof(name));
        const char *e = getenv(name);
        if (e) {
            snprintf(out[i].value, 256, "%s", e);
            out[i].source = REG_SRC_ENV;
        }
    }
    return c.n;
}

void reg_export(FILE *outf)
{
    reg_entry entries[256];
    int n = reg_list(entries, 256);
    for (int i = 0; i < n; i++) {
        char name[160];
        env_name(entries[i].section, entries[i].key, name, sizeof(name));
        fprintf(outf, "%s='", name);
        for (const char *p = entries[i].value; *p; p++) {
            if (*p == '\'') fprintf(outf, "'\\''");
            else fputc(*p, outf);
        }
        fprintf(outf, "'\n");
    }
}
