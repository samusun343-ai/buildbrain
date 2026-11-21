/* sysmodel - see sysmodel.h. */
#define _POSIX_C_SOURCE 200809L
#include "sysmodel.h"
#include "bdstore.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

struct sy_sys {
    char proc_root[512];
    char log_dir[512];
};

sy_sys *sy_open(const char *proc_root, const char *log_dir)
{
    sy_sys *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    snprintf(s->proc_root, sizeof(s->proc_root), "%s",
             proc_root ? proc_root : "/proc");
    snprintf(s->log_dir, sizeof(s->log_dir), "%s",
             log_dir ? log_dir : "/var/log");
    return s;
}
void sy_close(sy_sys *s) { free(s); }

size_t sy_processes(sy_sys *s, sy_proc *out, size_t max)
{
    DIR *d = opendir(s->proc_root);
    if (!d) return 0;
    long page_kb = sysconf(_SC_PAGESIZE) / 1024;
    if (page_kb <= 0) page_kb = 4;
    size_t n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && n < max) {
        const char *nm = de->d_name;
        if (nm[0] < '0' || nm[0] > '9') continue;
        int pid = atoi(nm);
        if (pid <= 0) continue;
        char path[700];
        snprintf(path, sizeof(path), "%s/%s/stat", s->proc_root, nm);
        size_t len;
        char *data = bd_read_all(path, &len);
        if (!data) continue;
        /* comm is in parens and may itself contain spaces or
         * parens: parse from the LAST ')' */
        char *rp = strrchr(data, ')');
        char *lp = strchr(data, '(');
        if (!rp || !lp || rp < lp) { free(data); continue; }
        sy_proc *pr = &out[n];
        memset(pr, 0, sizeof(*pr));
        pr->pid = pid;
        size_t cl = (size_t)(rp - lp - 1);
        if (cl >= sizeof(pr->name)) cl = sizeof(pr->name) - 1;
        memcpy(pr->name, lp + 1, cl);
        pr->name[cl] = 0;
        /* fields after ')': state is field 3 overall; the
         * remainder tokenizes as fields 3..52 */
        char *save = NULL;
        int field = 3;
        uint64_t utime = 0, stime = 0;
        long rss_pages = 0;
        for (char *tok = strtok_r(rp + 1, " ", &save); tok;
             tok = strtok_r(NULL, " ", &save), field++) {
            switch (field) {
            case 3: pr->state = tok[0]; break;
            case 14: utime = strtoull(tok, NULL, 10); break;
            case 15: stime = strtoull(tok, NULL, 10); break;
            case 24: rss_pages = strtol(tok, NULL, 10); break;
            default: break;
            }
        }
        pr->cpu_ticks = utime + stime;
        pr->rss_kb = rss_pages > 0 ? (uint64_t)rss_pages * (uint64_t)page_kb
                                   : 0;
        free(data);
        n++;
    }
    closedir(d);
    return n;
}

static int cmp_field;
static bool cmp_desc;
static int proc_cmp(const void *a, const void *b)
{
    const sy_proc *x = a, *y = b;
    int c = 0;
    switch (cmp_field) {
    case SY_SORT_NAME: c = strcmp(x->name, y->name); break;
    case SY_SORT_RSS:
        c = (x->rss_kb > y->rss_kb) - (x->rss_kb < y->rss_kb);
        break;
    case SY_SORT_CPU:
        c = (x->cpu_ticks > y->cpu_ticks) -
            (x->cpu_ticks < y->cpu_ticks);
        break;
    default:
        c = (x->pid > y->pid) - (x->pid < y->pid);
        break;
    }
    if (c == 0) c = (x->pid > y->pid) - (x->pid < y->pid);
    return cmp_desc ? -c : c;
}
void sy_procs_sort(sy_proc *procs, size_t n, int field, bool desc)
{
    cmp_field = field;
    cmp_desc = desc;
    if (n) qsort(procs, n, sizeof(sy_proc), proc_cmp);
}

int sy_kill(int pid, int sig) { return kill(pid, sig); }

int sy_meminfo(sy_sys *s, uint64_t *total_kb, uint64_t *avail_kb,
               uint64_t *swap_total_kb, uint64_t *swap_free_kb)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/meminfo", s->proc_root);
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return -1;
    *total_kb = *avail_kb = *swap_total_kb = *swap_free_kb = 0;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        unsigned long long v;
        if (sscanf(line, "MemTotal: %llu", &v) == 1)
            *total_kb = v;
        else if (sscanf(line, "MemAvailable: %llu", &v) == 1)
            *avail_kb = v;
        else if (sscanf(line, "SwapTotal: %llu", &v) == 1)
            *swap_total_kb = v;
        else if (sscanf(line, "SwapFree: %llu", &v) == 1)
            *swap_free_kb = v;
    }
    free(data);
    return 0;
}

size_t sy_mounts(sy_sys *s, sy_mount *out, size_t max)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/mounts", s->proc_root);
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return 0;
    size_t n = 0;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line && n < max;
         line = strtok_r(NULL, "\n", &save)) {
        sy_mount *m = &out[n];
        memset(m, 0, sizeof(*m));
        char extra[256];
        if (sscanf(line, "%127s %255s %31s %255s",
                   m->device, m->mountpoint, m->fstype, extra) < 3)
            continue;
        /* mounts escapes spaces as \040 - undo the common ones */
        for (char *p = m->mountpoint; (p = strstr(p, "\\040"));)
            { *p = ' '; memmove(p + 1, p + 4, strlen(p + 4) + 1); }
        struct statvfs vfs;
        if (statvfs(m->mountpoint, &vfs) == 0) {
            m->total_bytes = (uint64_t)vfs.f_blocks * vfs.f_frsize;
            m->avail_bytes = (uint64_t)vfs.f_bavail * vfs.f_frsize;
        }
        n++;
    }
    free(data);
    return n;
}

int sy_system_info(sy_sys *s, const char *version_file,
                   char *sysname, size_t scap,
                   char *release, size_t rcap,
                   char *machine, size_t mcap,
                   char *version, size_t vcap)
{
    (void)s;
    struct utsname un;
    if (uname(&un) != 0) return -1;
    snprintf(sysname, scap, "%s", un.sysname);
    snprintf(release, rcap, "%s", un.release);
    snprintf(machine, mcap, "%s", un.machine);
    if (vcap) version[0] = 0;
    const char *vf = version_file ? version_file
                                  : "/etc/brainux-version";
    size_t len;
    char *data = bd_read_all(vf, &len);
    if (data) {
        char *nl = strchr(data, '\n');
        if (nl) *nl = 0;
        snprintf(version, vcap, "%s", data);
        free(data);
    }
    return 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

size_t sy_log_files(sy_sys *s, char (*names)[128], size_t max)
{
    DIR *d = opendir(s->log_dir);
    if (!d) return 0;
    size_t n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && n < max) {
        if (de->d_name[0] == '.') continue;
        char path[700];
        snprintf(path, sizeof(path), "%s/%s", s->log_dir, de->d_name);
        struct stat sb;
        if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode)) continue;
        snprintf(names[n], 128, "%s", de->d_name);
        n++;
    }
    closedir(d);
    qsort(names, n, 128, name_cmp);
    return n;
}

size_t sy_log_read(sy_sys *s, const char *name, const char *needle,
                   size_t tail_bytes, char *out, size_t cap)
{
    if (cap == 0) return 0;
    out[0] = 0;
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", s->log_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) { fclose(f); return 0; }
    size_t want = (size_t)size < tail_bytes ? (size_t)size : tail_bytes;
    fseek(f, size - (long)want, SEEK_SET);
    char *buf = malloc(want + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, want, f);
    fclose(f);
    buf[got] = 0;
    /* skip the partial first line when we cut into the file */
    char *start = buf;
    if ((size_t)size > want) {
        char *nl = strchr(buf, '\n');
        if (nl) start = nl + 1;
    }
    size_t w = 0;
    if (!needle || !*needle) {
        size_t l = strlen(start);
        if (l > cap - 1) {
            /* keep the END of the tail within the buffer */
            start += l - (cap - 1);
            l = cap - 1;
        }
        memcpy(out, start, l);
        out[l] = 0;
        w = l;
    } else {
        char *save = NULL;
        for (char *line = strtok_r(start, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            if (!strstr(line, needle)) continue;
            size_t l = strlen(line);
            if (w + l + 2 > cap) break;
            memcpy(out + w, line, l);
            w += l;
            out[w++] = '\n';
        }
        out[w] = 0;
    }
    free(buf);
    return w;
}
