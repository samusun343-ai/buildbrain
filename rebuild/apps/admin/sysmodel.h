/* sysmodel - bd-admin system information model.
 *
 * Everything reads through an injectable proc root ("/proc"
 * in production) so tests run against fixture trees. Log
 * reading is bounded: only the tail of a file is loaded
 * (constant memory regardless of log size), with an optional
 * substring filter applied line by line.
 */
#ifndef SYSMODEL_H
#define SYSMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pid;
    char name[64];
    char state;              /* R, S, D, Z, T... */
    uint64_t rss_kb;
    uint64_t cpu_ticks;      /* utime + stime */
} sy_proc;

enum { SY_SORT_PID, SY_SORT_NAME, SY_SORT_RSS, SY_SORT_CPU };

typedef struct {
    char device[128];
    char mountpoint[256];
    char fstype[32];
    uint64_t total_bytes;    /* 0 when statvfs fails */
    uint64_t avail_bytes;
} sy_mount;

typedef struct sy_sys sy_sys;

/* proc_root: "/proc" in production. log_dir: "/var/log". */
sy_sys *sy_open(const char *proc_root, const char *log_dir);
void sy_close(sy_sys *s);

size_t sy_processes(sy_sys *s, sy_proc *out, size_t max);
void sy_procs_sort(sy_proc *procs, size_t n, int field, bool desc);
/* kill(2) wrapper (signal 0 = existence probe). */
int sy_kill(int pid, int sig);

int sy_meminfo(sy_sys *s, uint64_t *total_kb, uint64_t *avail_kb,
               uint64_t *swap_total_kb, uint64_t *swap_free_kb);

size_t sy_mounts(sy_sys *s, sy_mount *out, size_t max);

/* System identity: uname fields + rootfs version string
 * (contents of the version file, first line). version_file
 * may be NULL ("/etc/brainux-version"). */
int sy_system_info(sy_sys *s, const char *version_file,
                   char *sysname, size_t scap,
                   char *release, size_t rcap,
                   char *machine, size_t mcap,
                   char *version, size_t vcap);

/* Log files in the log dir (regular files, sorted by name). */
size_t sy_log_files(sy_sys *s, char (*names)[128], size_t max);
/* Read the last tail_bytes of a log file into out (NUL-
 * terminated, truncated to cap-1). When needle is non-empty,
 * only lines containing it are kept. Returns bytes written. */
size_t sy_log_read(sy_sys *s, const char *name, const char *needle,
                   size_t tail_bytes, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SYSMODEL_H */
