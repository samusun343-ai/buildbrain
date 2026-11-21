/* test_alarmd - resident alarm evaluation core (alarmd.c):
 * the daemon fires alarms/timers without bd-clock running,
 * reusing clockmodel's ck_poll unchanged.
 *
 * Time is injected, so every case is deterministic: wall is an
 * epoch second, mono a millisecond counter, offset is UTC.
 */
#include "alarmd.h"
#include "alarmd_wake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* 2026-11-21 (a Saturday) at hh:mm UTC. */
static int64_t epoch_at(int day_off, int hh, int mm)
{
    int64_t days = ck_days_from_civil(2026, 11, 21) + day_off;
    return days * 86400 + hh * 3600 + mm * 60;
}

static char *make_root(void)
{
    char tmpl[] = "/tmp/bdalarmdXXXXXX";
    char *r = mkdtemp(tmpl);
    return r ? strdup(r) : NULL;
}

/* --- recording sink --- */
typedef struct {
    int n_notify;
    char app[8][40];
    char title[8][64];
    char body[8][128];
    int urgency[8];
    int n_ring;
    int ring_kind[8];
    bool ring_catchup[8];
    int notify_ret;      /* value the notify callback returns */
} rec;

static int rec_notify(void *ud, const char *app, const char *title,
                      const char *body, int urgency)
{
    rec *r = ud;
    int i = r->n_notify++;
    if (i < 8) {
        snprintf(r->app[i], sizeof(r->app[i]), "%s", app);
        snprintf(r->title[i], sizeof(r->title[i]), "%s", title);
        snprintf(r->body[i], sizeof(r->body[i]), "%s", body);
        r->urgency[i] = urgency;
    }
    return r->notify_ret;
}

static void rec_ring(void *ud, int kind, uint64_t id, bool catch_up)
{
    rec *r = ud;
    (void)id;
    int i = r->n_ring++;
    if (i < 8) {
        r->ring_kind[i] = kind;
        r->ring_catchup[i] = catch_up;
    }
}

static alarmd *new_daemon(const char *root, rec *r)
{
    alarmd_sink sink = { rec_notify, rec_ring };
    return alarmd_new(root, 0, &sink, r);
}

/* --- recording sysfs writer for the wakealarm sequence --- */
static char wlog[8][40];
static int wn;
static int wfail_at = -1;    /* write index to fail; -1 = never */

static int rec_write(void *ud, const char *path, const char *value)
{
    (void)ud;
    if (strcmp(path, "/fake/wakealarm") != 0) return -1;
    if (wn == wfail_at) return -1;
    if (wn < 8) snprintf(wlog[wn], sizeof(wlog[wn]), "%s", value);
    wn++;
    return 0;
}

static uint64_t add_alarm(ck_store *s, int hh, int mm, int repeat,
                          int snooze, const char *label)
{
    ck_alarm a;
    memset(&a, 0, sizeof(a));
    a.hour = hh;
    a.min = mm;
    a.repeat_mask = repeat;
    a.enabled = true;
    a.snooze_min = snooze;
    a.sound = (char *)"";
    a.label = (char *)label;
    return ck_alarm_add(s, &a);
}

int main(void)
{
    alarmd_sink sink0 = { rec_notify, rec_ring };

    /* misuse guards */
    rec r0;
    memset(&r0, 0, sizeof(r0));
    r0.notify_ret = 1;
    CHECK(alarmd_new(NULL, 0, &sink0, &r0) == NULL);
    CHECK(alarmd_poll(NULL, 0, 0) == -1);

    /* ---------- 1. alarm fires with bd-clock nowhere in sight --- */
    char *root1 = make_root();
    rec r1;
    memset(&r1, 0, sizeof(r1));
    r1.notify_ret = 42;
    alarmd *d1 = new_daemon(root1, &r1);
    CHECK(d1 != NULL);
    uint64_t aid = add_alarm(alarmd_store(d1), 7, 0, 0, 0, "起きる");
    CHECK(aid != 0);
    int64_t w0 = epoch_at(0, 6, 59), m0 = 100000;
    CHECK(alarmd_poll(d1, w0, m0) == 0);          /* baseline */
    CHECK(r1.n_notify == 0);
    CHECK(alarmd_poll(d1, w0 + 61, m0 + 61000) == 1);
    CHECK(r1.n_notify == 1);
    CHECK(strcmp(r1.app[0], "bd-alarmd") == 0);
    CHECK(strcmp(r1.title[0], "アラーム 07:00") == 0);
    CHECK(strcmp(r1.body[0], "起きる") == 0);
    CHECK(r1.urgency[0] == 2);                    /* CRITICAL */
    CHECK(r1.n_ring == 1 && r1.ring_kind[0] == CK_EV_ALARM);
    CHECK(r1.ring_catchup[0] == false);
    /* no refire while it stays ringing in the daemon's store */
    CHECK(alarmd_poll(d1, w0 + 120, m0 + 120000) == 0);
    CHECK(r1.n_notify == 1);

    /* ---------- 3. once-alarm stays fired across a restart ----- */
    const ck_alarm *after = ck_alarm_find(alarmd_store(d1), aid);
    CHECK(after && !after->enabled);              /* auto-disabled */
    alarmd_free(d1);
    rec r1b;
    memset(&r1b, 0, sizeof(r1b));
    r1b.notify_ret = 1;
    alarmd *d1b = new_daemon(root1, &r1b);
    CHECK(d1b != NULL);
    CHECK(alarmd_poll(d1b, epoch_at(1, 7, 0), 200000) == 0);
    CHECK(alarmd_poll(d1b, epoch_at(1, 7, 1), 260000) == 0);
    CHECK(r1b.n_notify == 0);
    alarmd_free(d1b);

    /* ---------- 2. forward wall jump: catch-up fires once ------ */
    char *root2 = make_root();
    rec r2;
    memset(&r2, 0, sizeof(r2));
    r2.notify_ret = 1;
    alarmd *d2 = new_daemon(root2, &r2);
    CHECK(d2 != NULL);
    add_alarm(alarmd_store(d2), 7, 0, 0x7F, 0, "毎日");
    int64_t w6 = epoch_at(0, 6, 0), mm6 = 500000;
    CHECK(alarmd_poll(d2, w6, mm6) == 0);
    /* wall leaps 06:00 -> 07:30 while only 60 s of mono passes */
    CHECK(alarmd_poll(d2, epoch_at(0, 7, 30), mm6 + 60000) == 1);
    CHECK(r2.n_notify == 1);
    CHECK(r2.n_ring == 1 && r2.ring_catchup[0] == true);
    alarmd_free(d2);

    /* ---------- 4. timer that expired while the daemon was down  */
    char *root4 = make_root();
    int64_t tw = epoch_at(0, 12, 0), tm = 700000;
    ck_store *raw = ck_open(root4);
    CHECK(raw != NULL);
    uint64_t tid = ck_timer_add(raw, "ゆで卵", 60);
    CHECK(tid != 0);
    CHECK(ck_timer_start(raw, tid, tw, tm) == 0);
    ck_close(raw);                                 /* daemon "down" */
    rec r4;
    memset(&r4, 0, sizeof(r4));
    r4.notify_ret = 1;
    alarmd *d4 = new_daemon(root4, &r4);
    CHECK(d4 != NULL);
    /* first poll re-arms from the persisted wall end: long past */
    CHECK(alarmd_poll(d4, tw + 3600, tm + 3600000) == 1);
    CHECK(r4.n_notify == 1);
    CHECK(strcmp(r4.title[0], "タイマー終了") == 0);
    CHECK(strcmp(r4.body[0], "ゆで卵") == 0);
    CHECK(r4.urgency[0] == 1);                     /* NORMAL */
    CHECK(r4.n_ring == 1 && r4.ring_kind[0] == CK_EV_TIMER);
    alarmd_free(d4);

    /* ---------- 5. torn store: reset mid-write ----------------- */
    char *root5 = make_root();
    ck_store *raw5 = ck_open(root5);
    CHECK(raw5 != NULL);
    add_alarm(raw5, 7, 0, 0, 0, "無事な方");
    add_alarm(raw5, 8, 0, 0, 0, "壊れる方");
    ck_close(raw5);
    char apath[1200];
    snprintf(apath, sizeof(apath), "%s/alarms.txt", root5);
    /* truncate the file in the middle of the second line */
    FILE *f = fopen(apath, "r");
    CHECK(f != NULL);
    char content[4096];
    size_t clen = fread(content, 1, sizeof(content) - 1, f);
    fclose(f);
    char *nl = memchr(content, '\n', clen);
    CHECK(nl != NULL);
    size_t cut = (size_t)(nl - content) + 4;       /* mid-line 2 */
    CHECK(truncate(apath, (off_t)cut) == 0);
    /* a leftover temp file from an interrupted atomic write */
    char tmppath[1200];
    snprintf(tmppath, sizeof(tmppath), "%s/alarms.txt.tmp.4242",
             root5);
    f = fopen(tmppath, "w");
    CHECK(f != NULL);
    fputs("garbage\n", f);
    fclose(f);
    rec r5;
    memset(&r5, 0, sizeof(r5));
    r5.notify_ret = 1;
    alarmd *d5 = new_daemon(root5, &r5);
    CHECK(d5 != NULL);
    CHECK(ck_load_warnings(alarmd_store(d5)) >= 1);
    CHECK(access(tmppath, F_OK) != 0);             /* tmp swept */
    CHECK(ck_alarm_count(alarmd_store(d5)) == 1);  /* intact line */
    int64_t w5 = epoch_at(0, 6, 59), m5 = 300000;
    CHECK(alarmd_poll(d5, w5, m5) == 0);
    CHECK(alarmd_poll(d5, w5 + 61, m5 + 61000) == 1);
    CHECK(r5.n_notify == 1);
    CHECK(strcmp(r5.body[0], "無事な方") == 0);
    alarmd_free(d5);

    /* ---------- 6. snooze refires through the daemon ----------- */
    char *root6 = make_root();
    rec r6;
    memset(&r6, 0, sizeof(r6));
    r6.notify_ret = 1;
    alarmd *d6 = new_daemon(root6, &r6);
    CHECK(d6 != NULL);
    add_alarm(alarmd_store(d6), 7, 0, 0x7F, 5, "スヌーズ");
    int64_t w7 = epoch_at(0, 7, 0), m7 = 900000;
    CHECK(alarmd_poll(d6, w7 - 60, m7 - 60000) == 0);
    CHECK(alarmd_poll(d6, w7, m7) == 1);
    CHECK(r6.n_notify == 1);
    CHECK(ck_alarm_snooze(alarmd_store(d6), m7) == 0);
    /* 1 s before the snooze deadline the sleep is ~1 s */
    CHECK(alarmd_next_sleep_ms(d6, w7 + 299, m7 + 299000) == 1000);
    CHECK(alarmd_poll(d6, w7 + 300, m7 + 300000) == 1);
    CHECK(r6.n_notify == 2);
    alarmd_free(d6);

    /* ---------- 7. sleep sizing: cap, timer, empty ------------- */
    char *root7 = make_root();
    rec r7;
    memset(&r7, 0, sizeof(r7));
    r7.notify_ret = 1;
    alarmd *d7 = new_daemon(root7, &r7);
    CHECK(d7 != NULL);
    int64_t w8 = epoch_at(0, 6, 50), m8 = 1000;
    CHECK(alarmd_next_sleep_ms(d7, w8, m8) == ALARMD_MAX_SLEEP_MS);
    add_alarm(alarmd_store(d7), 7, 0, 0x7F, 0, "毎日");
    /* next alarm is 600 s out: the 30 s cap wins */
    CHECK(alarmd_next_sleep_ms(d7, w8, m8) == ALARMD_MAX_SLEEP_MS);
    uint64_t t7 = ck_timer_add(alarmd_store(d7), "5秒", 5);
    CHECK(ck_timer_start(alarmd_store(d7), t7, w8, m8) == 0);
    CHECK(alarmd_poll(d7, w8, m8) == 0);           /* baseline */
    CHECK(alarmd_next_sleep_ms(d7, w8, m8) == 5000);
    alarmd_free(d7);

    /* ---------- 8. external edit picked up by reload ----------- */
    char *root8 = make_root();
    rec r8;
    memset(&r8, 0, sizeof(r8));
    r8.notify_ret = 1;
    alarmd *d8 = new_daemon(root8, &r8);
    CHECK(d8 != NULL);
    ck_store *ext = ck_open(root8);                /* "bd-clock" */
    add_alarm(ext, 7, 0, 0, 0, "追加された");
    ck_close(ext);
    CHECK(alarmd_reload(d8) == 0);
    int64_t w9 = epoch_at(0, 6, 59), m9 = 5000;
    CHECK(alarmd_poll(d8, w9, m9) == 0);
    CHECK(alarmd_poll(d8, w9 + 61, m9 + 61000) == 1);
    CHECK(r8.n_notify == 1);
    alarmd_free(d8);

    /* ---------- 9. notify channel down: counted, not fatal ----- */
    char *root9 = make_root();
    rec r9;
    memset(&r9, 0, sizeof(r9));
    r9.notify_ret = -1;
    alarmd *d9 = new_daemon(root9, &r9);
    CHECK(d9 != NULL);
    add_alarm(alarmd_store(d9), 7, 0, 0, 0, "届かない");
    int64_t wA = epoch_at(0, 6, 59), mA = 1000;
    CHECK(alarmd_poll(d9, wA, mA) == 0);
    CHECK(alarmd_poll(d9, wA + 61, mA + 61000) == 1);
    CHECK(alarmd_notify_failures(d9) == 1);
    alarmd_free(d9);

    /* ---------- 10. ck_next_alarm_epoch / snooze queries ------- */
    char *rootA = make_root();
    ck_store *sa = ck_open(rootA);
    CHECK(sa != NULL);
    ck_set_local_offset(sa, 0);
    CHECK(ck_next_alarm_epoch(sa, epoch_at(0, 6, 59)) == -1);
    uint64_t daily = add_alarm(sa, 7, 0, 0x7F, 0, "毎日");
    CHECK(ck_next_alarm_epoch(sa, epoch_at(0, 6, 59)) ==
          epoch_at(0, 7, 0));
    /* strictly after: at the fire time itself, next is tomorrow */
    CHECK(ck_next_alarm_epoch(sa, epoch_at(0, 7, 0)) ==
          epoch_at(1, 7, 0));
    CHECK(ck_alarm_set_enabled(sa, daily, false) == 0);
    CHECK(ck_next_alarm_epoch(sa, epoch_at(0, 6, 59)) == -1);
    /* Mondays only (bit 0 = Mon): 2026-11-21 is a Saturday, so
     * the next fire is Monday 2026-11-23 */
    add_alarm(sa, 7, 0, 1 << CK_WD_MON, 0, "月曜");
    CHECK(ck_weekday_from_days(ck_days_from_civil(2026, 11, 21)) == 6);
    CHECK(ck_next_alarm_epoch(sa, epoch_at(0, 6, 59)) ==
          epoch_at(2, 7, 0));
    CHECK(ck_next_snooze_until(sa) == -1);
    ck_close(sa);

    /* ---------- 11. wakealarm write sequence (fake writer) ----- */
    /* The sysfs target itself is unverified on hardware; what is
     * tested here is the documented clear-then-set sequence. */
    wn = 0;
    wfail_at = -1;
    CHECK(bd_wakealarm_program(1700000000, rec_write, NULL,
                               "/fake/wakealarm") == 0);
    CHECK(wn == 2);
    CHECK(strcmp(wlog[0], "0") == 0);
    CHECK(strcmp(wlog[1], "1700000000") == 0);
    wn = 0;
    CHECK(bd_wakealarm_program(0, rec_write, NULL,
                               "/fake/wakealarm") == 0);
    CHECK(wn == 1 && strcmp(wlog[0], "0") == 0);
    wn = 0;
    CHECK(bd_wakealarm_clear(rec_write, NULL, "/fake/wakealarm") == 0);
    CHECK(wn == 1 && strcmp(wlog[0], "0") == 0);
    /* a failing clear aborts before the set is attempted */
    wn = 0;
    wfail_at = 0;
    CHECK(bd_wakealarm_program(1700000000, rec_write, NULL,
                               "/fake/wakealarm") == -1);
    CHECK(wn == 0);
    /* a failing set propagates */
    wn = 0;
    wfail_at = 1;
    CHECK(bd_wakealarm_program(1700000000, rec_write, NULL,
                               "/fake/wakealarm") == -1);
    CHECK(wn == 1);

    printf("test_alarmd: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
