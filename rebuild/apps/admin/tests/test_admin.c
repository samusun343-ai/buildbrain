/* test_admin - sysmodel tests against fixture /proc trees.
 *
 * Process parsing is checked against hand-built stat files
 * (including a comm containing spaces and parentheses - the
 * classic /proc parsing trap), meminfo/mounts against
 * fixtures, log tail reading is byte-bounded and starts on a
 * line boundary, the filter keeps only matching lines, and
 * sy_kill really terminates a child process.
 */
#define _POSIX_C_SOURCE 200809L
#include "sysmodel.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("PASS "); } \
    else { fails++; printf("FAIL "); } \
    printf(__VA_ARGS__); putchar('\n'); } while (0)

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

/* Build a stat line: fields after comm are 3..52; we set
 * state(3), utime(14), stime(15), rss(24) and zero elsewhere. */
static void make_stat(const char *dir, int pid, const char *comm,
                      char state, long utime, long stime, long rss)
{
    char path[1100], sub[1100], cmd[2400];
    snprintf(sub, sizeof(sub), "%s/%d", dir, pid);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", sub);
    if (system(cmd) != 0) exit(2);
    snprintf(path, sizeof(path), "%s/stat", sub);
    FILE *f = fopen(path, "w");
    fprintf(f, "%d (%s) %c", pid, comm, state);
    /* fields 4..52: index 4=ppid ... 14=utime 15=stime 24=rss */
    for (int field = 4; field <= 52; field++) {
        long v = 0;
        if (field == 14) v = utime;
        if (field == 15) v = stime;
        if (field == 24) v = rss;
        fprintf(f, " %ld", v);
    }
    fprintf(f, "\n");
    fclose(f);
}

int main(void)
{
    char tmpl[] = "/tmp/bdadmin-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    char proc[1100], logs[1100], cmd[2400];
    snprintf(proc, sizeof(proc), "%s/proc", tmpl);
    snprintf(logs, sizeof(logs), "%s/logs", tmpl);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s %s", proc, logs);
    if (system(cmd) != 0) return 2;

    long page_kb = sysconf(_SC_PAGESIZE) / 1024;
    make_stat(proc, 1, "init", 'S', 100, 50, 250);
    make_stat(proc, 42, "my proc (x)", 'R', 1000, 500, 1000);
    make_stat(proc, 123, "zombie", 'Z', 0, 0, 0);
    /* a non-process entry that must be ignored */
    char p[1200];
    snprintf(p, sizeof(p), "%s/meminfo", proc);
    write_file(p, "MemTotal:       131072 kB\n"
                  "MemAvailable:    65536 kB\n"
                  "SwapTotal:       32768 kB\n"
                  "SwapFree:        16384 kB\n");
    snprintf(p, sizeof(p), "%s/mounts", proc);
    char mp[1300];
    snprintf(mp, sizeof(mp),
             "/dev/mmcblk0p2 %s ext4 rw,relatime 0 0\n"
             "tmpfs /run tmpfs rw 0 0\n", tmpl);
    write_file(p, mp);

    sy_sys *sys = sy_open(proc, logs);
    sy_proc procs[16];
    size_t n = sy_processes(sys, procs, 16);
    CHECK(n == 3, "3 processes, got %zu", n);
    const sy_proc *by_pid[200] = {0};
    for (size_t i = 0; i < n; i++) by_pid[procs[i].pid] = &procs[i];
    CHECK(by_pid[1] && strcmp(by_pid[1]->name, "init") == 0, "init name");
    CHECK(by_pid[42] && strcmp(by_pid[42]->name, "my proc (x)") == 0,
          "comm with spaces/parens parsed: '%s'",
          by_pid[42] ? by_pid[42]->name : "?");
    CHECK(by_pid[42] && by_pid[42]->state == 'R', "state R");
    CHECK(by_pid[42] && by_pid[42]->cpu_ticks == 1500, "cpu ticks 1500");
    CHECK(by_pid[42] &&
          by_pid[42]->rss_kb == (uint64_t)(1000 * page_kb),
          "rss pages->KB %llu", (unsigned long long)by_pid[42]->rss_kb);
    CHECK(by_pid[123] && by_pid[123]->state == 'Z', "zombie state");

    /* sorting: RSS descending puts pid 42 first */
    sy_procs_sort(procs, n, SY_SORT_RSS, true);
    CHECK(procs[0].pid == 42, "rss desc first is 42");
    sy_procs_sort(procs, n, SY_SORT_NAME, false);
    CHECK(strcmp(procs[0].name, "init") == 0, "name sort");

    uint64_t total, avail, st, sf;
    CHECK(sy_meminfo(sys, &total, &avail, &st, &sf) == 0, "meminfo");
    CHECK(total == 131072 && avail == 65536 && st == 32768 &&
          sf == 16384, "meminfo values");

    sy_mount mounts[8];
    size_t nm = sy_mounts(sys, mounts, 8);
    CHECK(nm == 2, "2 mounts");
    CHECK(nm >= 1 && mounts[0].total_bytes > 0,
          "statvfs filled for the fixture mount");
    CHECK(nm >= 1 && strcmp(mounts[0].fstype, "ext4") == 0,
          "fstype parsed");

    /* logs: bounded tail + filter */
    snprintf(p, sizeof(p), "%s/system.log", logs);
    FILE *lf = fopen(p, "w");
    for (int i = 0; i < 5000; i++)
        fprintf(lf, "line %04d %s\n", i,
                i % 100 == 0 ? "ERROR happened" : "ok");
    fclose(lf);
    snprintf(p, sizeof(p), "%s/other.log", logs);
    write_file(p, "hello\n");
    char names[8][128];
    size_t nl = sy_log_files(sys, names, 8);
    CHECK(nl == 2, "2 log files");
    CHECK(nl == 2 && strcmp(names[0], "other.log") == 0,
          "log files sorted");
    char buf[8192];
    size_t got = sy_log_read(sys, "system.log", NULL, 1000,
                             buf, sizeof(buf));
    CHECK(got <= 1000 && got > 900, "tail bounded: %zu bytes", got);
    CHECK(strstr(buf, "line 4999") != NULL, "tail reaches EOF");
    CHECK(buf[0] == 'l', "tail starts at a line boundary");
    got = sy_log_read(sys, "system.log", "ERROR", 100000,
                      buf, sizeof(buf));
    int err_lines = 0;
    for (char *s = buf; *s; s++) if (*s == '\n') err_lines++;
    CHECK(err_lines == 50, "filter kept 50 ERROR lines, got %d",
          err_lines);
    CHECK(strstr(buf, "ok") == NULL, "filter dropped ok lines");

    /* kill: a real child process */
    pid_t child = fork();
    if (child == 0) { pause(); _exit(0); }
    CHECK(sy_kill(child, 0) == 0, "child exists");
    CHECK(sy_kill(child, SIGTERM) == 0, "SIGTERM sent");
    int status = 0;
    waitpid(child, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM,
          "child died by SIGTERM");
    CHECK(sy_kill(child, 0) != 0, "child gone");

    sy_close(sys);
    printf("test_admin: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
