/* alarmd.c - resident alarm/timer evaluation core. See alarmd.h.
 *
 * This file is deliberately free of sockets, files beyond the
 * clockmodel store, and clocks: time is injected through
 * alarmd_poll(), outputs leave through the sink. That is what
 * makes the daemon's behaviour testable without a running shell.
 */
#include "alarmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Notification urgency values mirror bd_nt_urgency (notify.h);
 * duplicated here as literals so the core does not depend on
 * the shell library - the embedder's notify callback owns the
 * mapping to the wire. */
#define URG_NORMAL 1
#define URG_CRITICAL 2

struct alarmd {
    char *root;
    int local_offset_min;
    alarmd_sink sink;
    void *ud;
    ck_store *store;
    unsigned notify_failures;
};

alarmd *alarmd_new(const char *root, int local_offset_min,
                   const alarmd_sink *sink, void *ud)
{
    if (!root || !sink || !sink->notify) return NULL;
    alarmd *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->root = strdup(root);
    d->local_offset_min = local_offset_min;
    d->sink = *sink;
    d->ud = ud;
    d->store = ck_open(root);
    if (!d->store) {
        free(d->root);
        free(d);
        return NULL;
    }
    ck_set_local_offset(d->store, local_offset_min);
    return d;
}

void alarmd_free(alarmd *d)
{
    if (!d) return;
    ck_close(d->store);
    free(d->root);
    free(d);
}

ck_store *alarmd_store(alarmd *d) { return d->store; }

unsigned alarmd_notify_failures(const alarmd *d)
{
    return d->notify_failures;
}

int alarmd_reload(alarmd *d)
{
    ck_store *fresh = ck_open(d->root);
    if (!fresh) return -1;
    ck_set_local_offset(fresh, d->local_offset_min);
    ck_close(d->store);
    d->store = fresh;
    return 0;
}

static void fire_one(alarmd *d, const ck_event *ev)
{
    char title[64];
    const char *body = "";
    int urgency;
    if (ev->kind == CK_EV_ALARM) {
        const ck_alarm *a = ck_alarm_find(d->store, ev->id);
        char hm[8] = "??:??";
        if (a) {
            ck_format_hm(a->hour, a->min, hm, sizeof(hm));
            body = a->label ? a->label : "";
        }
        snprintf(title, sizeof(title), "アラーム %s", hm);
        urgency = URG_CRITICAL;
    } else {
        const ck_timer *t = ck_timer_find(d->store, ev->id);
        snprintf(title, sizeof(title), "タイマー終了");
        body = (t && t->label) ? t->label : "";
        urgency = URG_NORMAL;
    }
    int id = d->sink.notify(d->ud, "bd-alarmd", title, body, urgency);
    if (id < 0) d->notify_failures++;
    if (d->sink.ring)
        d->sink.ring(d->ud, ev->kind, ev->id, ev->catch_up);
}

int alarmd_poll(alarmd *d, int64_t wall, int64_t mono_ms)
{
    if (!d) return -1;
    ck_event evs[16];
    int total = 0;
    /* ck_poll caps at `max` per call with the rest pending;
     * drain until it reports no more. */
    for (;;) {
        int n = ck_poll(d->store, wall, mono_ms, evs, 16);
        for (int i = 0; i < n; i++) fire_one(d, &evs[i]);
        total += n;
        if (n < 16) break;
    }
    return total;
}

int64_t alarmd_next_sleep_ms(alarmd *d, int64_t wall, int64_t mono_ms)
{
    int64_t best = ALARMD_MAX_SLEEP_MS;
    int64_t na = ck_next_alarm_epoch(d->store, wall);
    if (na >= 0) {
        int64_t ms = (na - wall) * 1000;
        if (ms < best) best = ms;
    }
    for (size_t i = 0; i < ck_timer_count(d->store); i++) {
        const ck_timer *t = ck_timer_at(d->store, i);
        if (t && t->state == CK_TMR_RUNNING && t->remain_ms < best)
            best = t->remain_ms;
    }
    int64_t snz = ck_next_snooze_until(d->store);
    if (snz >= 0) {
        int64_t ms = snz - mono_ms;
        if (ms < best) best = ms;
    }
    if (best < 0) best = 0;
    return best;
}
