/* test_clock - bd-clock model tests.
 *
 * Civil date math, world-clock store, alarm scheduling
 * (weekday masks, once auto-disable, wall-jump catch-up,
 * mono-based snooze), stopwatch laps, timers (mono deadlines,
 * persistence re-arm, expired-while-off), corrupt-store healing.
 * All time is injected: the tests are fully deterministic.
 */
#define _POSIX_C_SOURCE 200809L
#include "clockmodel.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_int(const char *what, long got, long want)
{
    if (got == want) { passes++; printf("PASS %s -> %ld\n", what, got); }
    else { fails++; printf("FAIL %s: got %ld, want %ld\n", what, got, want); }
}

#define OFF 540   /* local offset used throughout: JST */
/* Epoch of a local civil time at offset OFF. */
static int64_t local_epoch(int y, int mo, int d, int h, int mi, int s)
{
    return ck_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s
           - (int64_t)OFF * 60;
}

static uint64_t add_alarm(ck_store *s, int h, int mi, int mask,
                          int snooze, const char *label)
{
    ck_alarm f;
    memset(&f, 0, sizeof(f));
    f.hour = h;
    f.min = mi;
    f.repeat_mask = mask;
    f.enabled = true;
    f.snooze_min = snooze;
    f.sound = (char *)"";
    f.label = (char *)label;
    return ck_alarm_add(s, &f);
}

int main(void)
{
    /* ---- civil math ---- */
    check_int("C epoch day", (long)ck_days_from_civil(1970, 1, 1), 0);
    check_int("C y2k day", (long)ck_days_from_civil(2000, 1, 1), 10957);
    check_int("C weekday epoch (Thu)",
              ck_weekday_from_days(0), 4);
    check_int("C weekday 2026-11-20 (Fri)",
              ck_weekday_from_days(ck_days_from_civil(2026, 11, 20)), 5);
    {
        int y, m, d;
        ck_civil_from_days(ck_days_from_civil(2024, 2, 29), &y, &m, &d);
        check_true("C leap round trip", y == 2024 && m == 2 && d == 29);
        ck_broken b;
        ck_break_down(0, OFF, &b);
        check_true("C breakdown JST epoch",
                   b.year == 1970 && b.mon == 1 && b.mday == 1 &&
                   b.hour == 9 && b.min == 0 && b.wday == 4);
    }

    char tmpl[] = "/tmp/bdclock-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    ck_store *s = ck_open(tmpl);
    check_true("C open", s != NULL);
    ck_set_local_offset(s, OFF);

    /* ---- cities ---- */
    check_int("C seed cities", (long)ck_city_count(s), 5);
    check_int("C add Paris", ck_city_add(s, "パリ", 60), 1);
    check_int("C dup rejected", ck_city_add(s, "パリ", 60), 0);
    check_int("C dup seed rejected", ck_city_add(s, "ロンドン", 0), 0);
    check_int("C cities now", (long)ck_city_count(s), 6);
    check_int("C remove", ck_city_remove(s, 5), 0);
    check_int("C cities after remove", (long)ck_city_count(s), 5);

    /* ---- daily alarm fires, then again next day ---- */
    uint64_t daily = add_alarm(s, 7, 0, 0x7F, 10, "朝");
    check_true("A daily added", daily != 0);
    int64_t w0 = local_epoch(2026, 11, 20, 6, 59, 0);   /* Fri */
    ck_event ev[4];
    check_int("A baseline no fire", ck_poll(s, w0, 0, ev, 4), 0);
    check_int("A fires at 07:00",
              ck_poll(s, w0 + 61, 61000, ev, 4), 1);
    check_true("A event kind/id",
               ev[0].kind == CK_EV_ALARM && ev[0].id == daily &&
               !ev[0].catch_up);
    check_int("A ringing", (long)ck_ringing_alarm(s), (long)daily);
    /* snooze runs on mono: +10 min of mono, wall follows */
    check_int("A snooze", ck_alarm_snooze(s, 61000), 0);
    check_int("A snoozed quiet",
              ck_poll(s, w0 + 61 + 599, 61000 + 599000, ev, 4), 0);
    check_int("A snooze refires",
              ck_poll(s, w0 + 61 + 600, 61000 + 600000, ev, 4), 1);
    check_int("A dismiss", ck_alarm_dismiss(s), 0);
    /* no double fire later the same day */
    check_int("A no refire same day",
              ck_poll(s, w0 + 3600, 61000 + 600000 + 3600000, ev, 4), 0);
    int64_t mono_now = 61000 + 600000 + 3600000;
    int64_t wall_now = w0 + 3600;
    /* next day */
    int64_t w_next = local_epoch(2026, 11, 21, 7, 0, 1);
    mono_now += (w_next - wall_now) * 1000;
    check_int("A fires next day", ck_poll(s, w_next, mono_now, ev, 4), 1);
    ck_alarm_dismiss(s);
    wall_now = w_next;
    ck_alarm_set_enabled(s, daily, false);  /* keep later windows clean */

    /* ---- once alarm auto-disables ---- */
    uint64_t once = add_alarm(s, 8, 30, 0, 0, "一度");
    int64_t w1 = local_epoch(2026, 11, 21, 8, 29, 0);
    mono_now += (w1 - wall_now) * 1000;
    check_int("A once baseline", ck_poll(s, w1, mono_now, ev, 4), 0);
    mono_now += 61000;
    check_int("A once fires", ck_poll(s, w1 + 61, mono_now, ev, 4), 1);
    check_true("A once is the event", ev[0].id == once);
    ck_alarm_dismiss(s);
    check_true("A once auto-disabled",
               !ck_alarm_find(s, once)->enabled);
    wall_now = w1 + 61;

    /* ---- weekday mask: Monday-only alarm ---- */
    /* 2026-11-21 is Saturday; 2026-11-23 is Monday. */
    uint64_t mon = add_alarm(s, 9, 0, 1 << CK_WD_MON, 0, "月曜");
    int64_t w_sat = local_epoch(2026, 11, 21, 9, 0, 1);
    mono_now += (w_sat - wall_now) * 1000;
    check_int("A mon-only silent Sat", ck_poll(s, w_sat, mono_now, ev, 4),
              0);
    int64_t w_mon = local_epoch(2026, 11, 23, 9, 0, 1);
    mono_now += (w_mon - w_sat) * 1000;
    check_int("A mon-only fires Mon", ck_poll(s, w_mon, mono_now, ev, 4),
              1);
    check_true("A mon event", ev[0].id == mon);
    ck_alarm_dismiss(s);
    wall_now = w_mon;
    ck_alarm_set_enabled(s, mon, false);

    /* ---- forward wall jump: catch-up fire ---- */
    uint64_t jump = add_alarm(s, 12, 0, 0x7F, 0, "正午");
    int64_t w_pre = local_epoch(2026, 11, 23, 11, 0, 0);
    mono_now += (w_pre - wall_now) * 1000;
    check_int("A jump baseline", ck_poll(s, w_pre, mono_now, ev, 4), 0);
    /* wall leaps 2h while mono advances only 60 s */
    int64_t w_post = local_epoch(2026, 11, 23, 13, 0, 0);
    check_int("A jump catch-up",
              ck_poll(s, w_post, mono_now + 60000, ev, 4), 1);
    check_true("A catch-up flag",
               ev[0].id == jump && ev[0].catch_up);
    ck_alarm_dismiss(s);
    mono_now += 60000;
    wall_now = w_post;
    ck_alarm_set_enabled(s, jump, false);

    /* ---- backward jump: no fire, then normal fire ---- */
    uint64_t back = add_alarm(s, 14, 0, 0x7F, 0, "午後");
    int64_t w_b1 = local_epoch(2026, 11, 23, 13, 59, 0);
    mono_now += (w_b1 - wall_now) * 1000;
    check_int("A back baseline", ck_poll(s, w_b1, mono_now, ev, 4), 0);
    int64_t w_b0 = local_epoch(2026, 11, 23, 13, 0, 0);
    check_int("A backward jump quiet",
              ck_poll(s, w_b0, mono_now + 60000, ev, 4), 0);
    mono_now += 60000;
    int64_t w_b2 = local_epoch(2026, 11, 23, 14, 0, 1);
    mono_now += (w_b2 - w_b0) * 1000;
    check_int("A fires after back-jump",
              ck_poll(s, w_b2, mono_now, ev, 4), 1);
    check_true("A back event not catch-up",
               ev[0].id == back && !ev[0].catch_up);
    ck_alarm_dismiss(s);
    wall_now = w_b2;

    /* ---- toggle off silences ---- */
    ck_alarm_set_enabled(s, back, false);
    int64_t w_off = local_epoch(2026, 11, 24, 14, 0, 1);
    mono_now += (w_off - wall_now) * 1000;
    check_int("A disabled silent", ck_poll(s, w_off, mono_now, ev, 4), 0);
    wall_now = w_off;

    /* ---- timers: mono deadline, wall-independent ---- */
    uint64_t t1 = ck_timer_add(s, "テスト", 60);
    check_true("T added", t1 != 0);
    int64_t tw = local_epoch(2026, 11, 24, 15, 0, 0);
    mono_now += (tw - wall_now) * 1000;
    check_int("T baseline", ck_poll(s, tw, mono_now, ev, 4), 0);
    check_int("T start", ck_timer_start(s, t1, tw, mono_now), 0);
    check_int("T quiet at 59s",
              ck_poll(s, tw + 59, mono_now + 59000, ev, 4), 0);
    check_int("T fires at 60s",
              ck_poll(s, tw + 60, mono_now + 60000, ev, 4), 1);
    check_true("T event", ev[0].kind == CK_EV_TIMER && ev[0].id == t1);
    check_int("T state ringing",
              ck_timer_find(s, t1)->state, CK_TMR_RINGING);
    check_int("T dismiss", ck_timer_dismiss(s, t1), 0);
    check_int("T state idle", ck_timer_find(s, t1)->state, CK_TMR_IDLE);
    mono_now += 60000;

    /* pause / resume */
    check_int("T restart", ck_timer_start(s, t1, tw + 60, mono_now), 0);
    check_int("T pause", ck_timer_pause(s, t1, mono_now + 20000), 0);
    check_int("T paused remain",
              (long)(ck_timer_find(s, t1)->remain_ms / 1000), 40);
    check_int("T resume",
              ck_timer_resume(s, t1, tw + 80, mono_now + 20000), 0);
    check_int("T fires after resume",
              ck_poll(s, tw + 120, mono_now + 60000, ev, 4), 1);
    ck_timer_dismiss(s, t1);
    mono_now += 60000;
    wall_now = tw + 120;

    /* ---- timer persistence: re-arm across reopen ---- */
    uint64_t t2 = ck_timer_add(s, "永続", 300);
    int64_t pw = local_epoch(2026, 11, 24, 16, 0, 0);
    mono_now += (pw - wall_now) * 1000;
    ck_poll(s, pw, mono_now, ev, 4);
    check_int("T2 start", ck_timer_start(s, t2, pw, mono_now), 0);
    ck_close(s);
    s = ck_open(tmpl);
    ck_set_local_offset(s, OFF);
    check_true("T2 persisted running",
               ck_timer_find(s, t2)->state == CK_TMR_RUNNING);
    /* first poll 100 s of wall later: re-armed, not fired */
    check_int("T2 rearmed quiet", ck_poll(s, pw + 100, 5000, ev, 4), 0);
    check_int("T2 remain ~200",
              (long)(ck_timer_find(s, t2)->remain_ms / 1000), 200);
    check_int("T2 fires at end",
              ck_poll(s, pw + 300, 5000 + 200000, ev, 4), 1);
    check_true("T2 event", ev[0].kind == CK_EV_TIMER && ev[0].id == t2);
    ck_timer_dismiss(s, t2);

    /* expired while powered off: fires on first poll */
    uint64_t t3 = ck_timer_add(s, "切替", 120);
    int64_t ew = local_epoch(2026, 11, 24, 17, 0, 0);
    ck_poll(s, ew, 300000, ev, 4);
    ck_timer_start(s, t3, ew, 300000);
    ck_close(s);
    s = ck_open(tmpl);
    ck_set_local_offset(s, OFF);
    check_int("T3 expired fires on first poll",
              ck_poll(s, ew + 999, 0, ev, 4), 1);
    check_true("T3 event", ev[0].kind == CK_EV_TIMER && ev[0].id == t3);
    ck_timer_dismiss(s, t3);
    ck_close(s);

    /* ---- stopwatch ---- */
    ck_stopwatch sw;
    ck_sw_init(&sw);
    ck_sw_start(&sw, 1000);
    check_int("S lap1", ck_sw_lap(&sw, 2000), 0);   /* 1000 ms */
    check_int("S lap2", ck_sw_lap(&sw, 4000), 1);   /* 2000 ms */
    check_int("S lap3", ck_sw_lap(&sw, 4500), 2);   /*  500 ms */
    check_int("S fastest", ck_sw_fastest(&sw), 2);
    check_int("S slowest", ck_sw_slowest(&sw), 1);
    ck_sw_stop(&sw, 5000);
    check_int("S elapsed frozen", (long)ck_sw_elapsed(&sw, 99999), 4000);
    ck_sw_reset(&sw);
    check_int("S reset laps", sw.n_laps, 0);

    /* ---- corrupt store healing ---- */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/alarms.txt", tmpl);
        FILE *fp = fopen(path, "a");
        fputs("garbage line without tabs\n", fp);
        fputs("999\t25\t99\t0\t1\t0\t\tbad\n", fp);
        fclose(fp);
    }
    s = ck_open(tmpl);
    check_true("H warnings counted", ck_load_warnings(s) >= 2);
    check_true("H valid alarms survive", ck_alarm_count(s) >= 4);
    ck_close(s);

    printf("test_clock: %d PASS, %d FAIL\n", passes, fails);
    return fails ? 1 : 0;
}
