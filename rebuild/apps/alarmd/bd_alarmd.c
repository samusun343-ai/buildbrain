/* bd_alarmd - resident alarm/timer daemon. See alarmd.h for the
 * design and alarmd_wake.h for the (unverified) RTC wakealarm
 * interface.
 *
 * Main loop shape: event-driven, no periodic polling.
 *   - timerfd armed for alarmd_next_sleep_ms() (next alarm /
 *     timer / snooze, capped at 30 s);
 *   - inotify on the clock data directory wakes the loop when
 *     bd-clock (or anything) rewrites alarms.txt / timers.txt /
 *     counters; an mtime comparison against the snapshot taken
 *     after the daemon's own evaluations tells external edits
 *     apart from the daemon's own saves, and the store is then
 *     reloaded (see alarmd_reload for the trade-off);
 *   - SIGTERM/SIGINT stop the loop cleanly.
 *
 * Single instance: an flock on <root>/alarmd.lock; a second
 * instance exits immediately (two evaluators would double every
 * notification).
 *
 * The ring hook only logs. The rebuild stack has no alarm audio
 * path today (bd-clock itself is banner-only); the seam is
 * alarmd_sink.ring - see alarmd.h.
 */
#include "alarmd.h"
#include "alarmd_wake.h"
#include "bdstore.h"
#include "notify.h"
#include "reg.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int64_t wall_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec;
}

static int64_t mono_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void log_line(const char *what, int kind, uint64_t id,
                     bool catch_up)
{
    char buf[64];
    time_t t = (time_t)wall_now();
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(stderr, "bd-alarmd[%s]: %s kind=%d id=%llu catch_up=%d\n",
            buf, what, kind, (unsigned long long)id, catch_up ? 1 : 0);
}

/* --- sink: notification IPC + log-only ring --- */

static int daemon_notify(void *ud, const char *app, const char *title,
                         const char *body, int urgency)
{
    (void)ud;
    /* NULL path = bd_nt_default_path(); -1 when the shell is
     * not up. The core counts failures; the daemon keeps
     * evaluating regardless. */
    return bd_notify_send(NULL, app, title, body, urgency);
}

static void daemon_ring(void *ud, int kind, uint64_t id, bool catch_up)
{
    (void)ud;
    log_line("ring (no audio path in the stack yet; log only)",
             kind, id, catch_up);
}

/* --- store file mtimes (external-edit detection) --- */

typedef struct {
    struct timespec alarms, timers, counters;
    bool valid;
} mtree;

static struct timespec file_mtime(const char *root, const char *name)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    struct stat st;
    if (stat(path, &st) != 0) return (struct timespec){ 0, 0 };
    return st.st_mtim;
}

static void mtree_take(mtree *m, const char *root)
{
    m->alarms = file_mtime(root, "alarms.txt");
    m->timers = file_mtime(root, "timers.txt");
    m->counters = file_mtime(root, "counters");
    m->valid = true;
}

static bool mtree_changed(const mtree *m, const char *root)
{
    if (!m->valid) return false;
    struct timespec a = file_mtime(root, "alarms.txt");
    struct timespec t = file_mtime(root, "timers.txt");
    struct timespec c = file_mtime(root, "counters");
    return a.tv_sec != m->alarms.tv_sec || a.tv_nsec != m->alarms.tv_nsec ||
           t.tv_sec != m->timers.tv_sec || t.tv_nsec != m->timers.tv_nsec ||
           c.tv_sec != m->counters.tv_sec ||
           c.tv_nsec != m->counters.tv_nsec;
}

int main(void)
{
    /* Data root: identical resolution to bd-clock's main. */
    char root[1024];
    const char *env_root = getenv("BRAINUX_CLOCK_DIR");
    const char *home = getenv("HOME");
    if (env_root) snprintf(root, sizeof(root), "%s", env_root);
    else if (home)
        snprintf(root, sizeof(root), "%s/.local/share/brainux/clock",
                 home);
    else snprintf(root, sizeof(root), ".local/share/brainux/clock");
    if (bd_mkdir_p(root) != 0) {
        fprintf(stderr, "bd-alarmd: cannot create %s\n", root);
        return 1;
    }

    /* Single instance. */
    char lockpath[1200];
    snprintf(lockpath, sizeof(lockpath), "%s/alarmd.lock", root);
    int lockfd = open(lockpath, O_CREAT | O_RDWR, 0644);
    if (lockfd < 0 || flock(lockfd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "bd-alarmd: another instance is running\n");
        return 1;
    }

    int offset = reg_get_int("datetime", "local_offset_min", 540);
    alarmd_sink sink = { daemon_notify, daemon_ring };
    alarmd *d = alarmd_new(root, offset, &sink, NULL);
    if (!d) {
        fprintf(stderr, "bd-alarmd: cannot open store at %s\n", root);
        return 1;
    }

    const bool use_wake = getenv("BD_ALARMD_WAKEALARM") &&
                          getenv("BD_ALARMD_WAKEALARM")[0] == '1';
    if (use_wake)
        fprintf(stderr, "bd-alarmd: RTC wakealarm programming "
                        "enabled (interface unverified on hardware)\n");

    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (ifd >= 0)
        inotify_add_watch(ifd, root,
                          IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        fprintf(stderr, "bd-alarmd: timerfd_create failed\n");
        return 1;
    }

    struct sigaction sa = { 0 };
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    mtree snap = { 0 };
    bool first = true;
    int64_t last_wake_epoch = -2;   /* last programmed wakealarm */
    bool wake_failed = false;

    while (!g_stop) {
        /* External edit since our own last save? Reload (and
         * pick up a changed local offset from the settings). */
        if (!first && mtree_changed(&snap, root)) {
            int off2 = reg_get_int("datetime", "local_offset_min", 540);
            if (off2 != offset) {
                alarmd_free(d);
                offset = off2;
                d = alarmd_new(root, offset, &sink, NULL);
                if (!d) break;
            } else if (alarmd_reload(d) != 0) {
                fprintf(stderr, "bd-alarmd: reload failed\n");
            }
        }
        first = false;

        int64_t wall = wall_now();
        int64_t mono = mono_now_ms();
        (void)alarmd_poll(d, wall, mono);
        mtree_take(&snap, root);

        if (use_wake) {
            int64_t na = ck_next_alarm_epoch(alarmd_store(d), wall);
            if (na != last_wake_epoch) {
                last_wake_epoch = na;
                if (bd_wakealarm_program_default(na) != 0) {
                    if (!wake_failed)
                        fprintf(stderr, "bd-alarmd: wakealarm "
                                        "programming failed\n");
                    wake_failed = true;
                } else {
                    wake_failed = false;
                }
            }
        }

        int64_t sleep_ms = alarmd_next_sleep_ms(d, wall, mono);
        struct itimerspec its = { 0 };
        its.it_value.tv_sec = sleep_ms / 1000;
        its.it_value.tv_nsec = (sleep_ms % 1000) * 1000000L;
        if (sleep_ms <= 0) {
            its.it_value.tv_nsec = 1000000L; /* 1 ms floor */
        }
        timerfd_settime(tfd, 0, &its, NULL);

        struct pollfd pf[2] = {
            { .fd = tfd, .events = POLLIN },
            { .fd = ifd, .events = POLLIN },
        };
        int npf = ifd >= 0 ? 2 : 1;
        int rc = poll(pf, npf, -1);
        if (rc < 0 && errno != EINTR)
            break;
        if (pf[0].revents & POLLIN) {
            uint64_t exp;
            (void)!read(tfd, &exp, sizeof(exp));
        }
        if (ifd >= 0 && (pf[1].revents & POLLIN)) {
            char buf[4096];
            (void)!read(ifd, buf, sizeof(buf)); /* drained; the
                mtime check at the top of the loop decides */
        }
    }

    if (use_wake) (void)bd_wakealarm_clear_default();
    close(tfd);
    if (ifd >= 0) close(ifd);
    alarmd_free(d);
    close(lockfd);
    return 0;
}
