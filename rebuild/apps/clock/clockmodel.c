#define _POSIX_C_SOURCE 200809L
#include "clockmodel.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= civil time ================= */
/* Howard Hinnant's days-from-civil algorithms (public domain
 * mathematics), the standard branch-free civil date conversion. */

int64_t ck_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5
                         + (unsigned)d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

void ck_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + (mp < 10 ? 3 : -9));
    *y = (int)(yy + (*m <= 2));
}

int ck_weekday_from_days(int64_t z)
{
    /* 1970-01-01 was a Thursday (4). */
    int64_t w = (z + 4) % 7;
    if (w < 0) w += 7;
    return (int)w;
}

void ck_break_down(int64_t epoch, int offset_min, ck_broken *out)
{
    int64_t t = epoch + (int64_t)offset_min * 60;
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    ck_civil_from_days(days, &out->year, &out->mon, &out->mday);
    out->hour = (int)(rem / 3600);
    out->min = (int)((rem % 3600) / 60);
    out->sec = (int)(rem % 60);
    out->wday = ck_weekday_from_days(days);
}

void ck_format_hm(int hour, int min, char *out, size_t cap)
{
    snprintf(out, cap, "%02d:%02d", hour, min);
}

void ck_format_hms(int64_t total_sec, char *out, size_t cap)
{
    if (total_sec < 0) total_sec = 0;
    snprintf(out, cap, "%02d:%02d:%02d",
             (int)(total_sec / 3600), (int)((total_sec / 60) % 60),
             (int)(total_sec % 60));
}

/* ================= built-in cities ================= */

static const ck_city builtin_cities[] = {
    { "東京", 540 }, { "ソウル", 540 }, { "北京", 480 },
    { "シンガポール", 480 }, { "バンコク", 420 }, { "デリー", 330 },
    { "ドバイ", 240 }, { "モスクワ", 180 }, { "パリ", 60 },
    { "ロンドン", 0 }, { "UTC", 0 }, { "ニューヨーク", -300 },
    { "シカゴ", -360 }, { "デンバー", -420 },
    { "ロサンゼルス", -480 }, { "アンカレッジ", -540 },
    { "ホノルル", -600 }, { "シドニー", 600 },
    { "オークランド", 720 },
};

size_t ck_builtin_count(void)
{
    return sizeof(builtin_cities) / sizeof(builtin_cities[0]);
}
const ck_city *ck_builtin_at(size_t i)
{
    return i < ck_builtin_count() ? &builtin_cities[i] : NULL;
}
const ck_city *ck_builtin_find(const char *name)
{
    for (size_t i = 0; i < ck_builtin_count(); i++)
        if (strcmp(builtin_cities[i].name, name) == 0)
            return &builtin_cities[i];
    return NULL;
}

/* ================= store ================= */

struct ck_store {
    char *root;
    ck_city *cities;
    size_t n_cities, cap_cities;
    ck_alarm *alarms;
    size_t n_alarms, cap_alarms;
    ck_timer *timers;
    size_t n_timers, cap_timers;
    uint64_t next_alarm_id, next_timer_id;
    int warnings;
    /* poll runtime */
    bool polled;
    int64_t last_wall, last_mono;
    int local_offset_min;    /* device local offset for alarms */
    /* per-alarm runtime keyed by alarm id: last local day-count
     * on which it fired (double-fire guard) and a pending
     * snooze deadline in mono ms (0 = none) */
    struct { uint64_t id; int64_t fired_day; int64_t snooze_until; } *rt;
    size_t n_rt, cap_rt;
    /* monotonic deadlines, index-parallel to s->timers */
    int64_t *tmr_dl;
    /* ringing alarm queue (FIFO of alarm ids) */
    uint64_t ringq[16];
    int n_ringq;
    bool timers_rearmed;     /* persisted timers re-armed on 1st poll */
};

static char *xstrdup(const char *s)
{
    return strdup(s ? s : "");
}

static void path_of(const ck_store *s, const char *name,
                    char *out, size_t cap)
{
    bd_path_join(out, cap, s->root, name);
}

static int save_cities(ck_store *s)
{
    char path[1024], tmp[2048];
    path_of(s, "cities.txt", path, sizeof(path));
    size_t off = 0;
    tmp[0] = 0;
    for (size_t i = 0; i < s->n_cities; i++) {
        char e1[512];
        bd_escape(s->cities[i].name, e1, sizeof(e1));
        off += (size_t)snprintf(tmp + off, sizeof(tmp) - off, "%s\t%d\n",
                                e1, s->cities[i].offset_min);
        if (off >= sizeof(tmp)) return -1;
    }
    return bd_write_atomic(path, tmp, off);
}

static int save_alarms(ck_store *s)
{
    char path[1024];
    path_of(s, "alarms.txt", path, sizeof(path));
    size_t cap = s->n_alarms * 640 + 64;
    char *buf = malloc(cap);
    if (!buf) return -1;
    size_t off = 0;
    buf[0] = 0;
    for (size_t i = 0; i < s->n_alarms; i++) {
        const ck_alarm *a = &s->alarms[i];
        char e1[512], e2[512];
        bd_escape(a->sound, e1, sizeof(e1));
        bd_escape(a->label, e2, sizeof(e2));
        off += (size_t)snprintf(buf + off, cap - off,
                                "%llu\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
                                (unsigned long long)a->id, a->hour,
                                a->min, a->repeat_mask,
                                a->enabled ? 1 : 0, a->snooze_min,
                                e1, e2);
    }
    int rc = bd_write_atomic(path, buf, off);
    free(buf);
    return rc;
}

static int save_timers(ck_store *s)
{
    char path[1024];
    path_of(s, "timers.txt", path, sizeof(path));
    size_t cap = s->n_timers * 640 + 64;
    char *buf = malloc(cap);
    if (!buf) return -1;
    size_t off = 0;
    buf[0] = 0;
    for (size_t i = 0; i < s->n_timers; i++) {
        const ck_timer *t = &s->timers[i];
        char e1[512];
        bd_escape(t->label, e1, sizeof(e1));
        int64_t remain_sec = (t->remain_ms + 999) / 1000;
        off += (size_t)snprintf(buf + off, cap - off,
                                "%llu\t%lld\t%d\t%lld\t%lld\t%s\n",
                                (unsigned long long)t->id,
                                (long long)t->duration_sec, t->state,
                                (long long)remain_sec,
                                (long long)t->end_wall, e1);
    }
    int rc = bd_write_atomic(path, buf, off);
    free(buf);
    return rc;
}

static int save_counters(ck_store *s)
{
    char path[1024], buf[128];
    path_of(s, "counters", path, sizeof(path));
    snprintf(buf, sizeof(buf), "next_alarm=%llu\nnext_timer=%llu\n",
             (unsigned long long)s->next_alarm_id,
             (unsigned long long)s->next_timer_id);
    return bd_write_atomic_str(path, buf);
}

/* Split a line into at most `max` tab-separated fields in place.
 * Returns the field count. */
static int split_tabs(char *line, char **fields, int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = 0;
        p = tab + 1;
    }
    return n;
}

static void load_cities(ck_store *s)
{
    char path[1024];
    path_of(s, "cities.txt", path, sizeof(path));
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) {
        /* First open: seed with a sensible default list. */
        static const char *seed[] = {
            "東京", "ロンドン", "ニューヨーク", "ロサンゼルス",
            "シドニー"
        };
        for (size_t i = 0; i < sizeof(seed) / sizeof(seed[0]); i++) {
            const ck_city *b = ck_builtin_find(seed[i]);
            if (b) ck_city_add(s, b->name, b->offset_min);
        }
        return;
    }
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *f[2];
        if (split_tabs(line, f, 2) != 2) { s->warnings++; continue; }
        char name[512];
        bd_unescape(f[0], name, sizeof(name));
        if (!name[0]) { s->warnings++; continue; }
        if (s->n_cities == s->cap_cities) {
            s->cap_cities = s->cap_cities ? s->cap_cities * 2 : 8;
            s->cities = realloc(s->cities,
                                s->cap_cities * sizeof(ck_city));
        }
        s->cities[s->n_cities].name = xstrdup(name);
        s->cities[s->n_cities].offset_min = atoi(f[1]);
        s->n_cities++;
    }
    free(data);
}

static void load_alarms(ck_store *s)
{
    char path[1024];
    path_of(s, "alarms.txt", path, sizeof(path));
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *f[8];
        if (split_tabs(line, f, 8) != 8) { s->warnings++; continue; }
        ck_alarm a;
        memset(&a, 0, sizeof(a));
        a.id = strtoull(f[0], NULL, 10);
        a.hour = atoi(f[1]);
        a.min = atoi(f[2]);
        a.repeat_mask = atoi(f[3]);
        a.enabled = atoi(f[4]) != 0;
        a.snooze_min = atoi(f[5]);
        char snd[512], lab[512];
        bd_unescape(f[6], snd, sizeof(snd));
        bd_unescape(f[7], lab, sizeof(lab));
        a.sound = xstrdup(snd);
        a.label = xstrdup(lab);
        if (!a.id || a.hour < 0 || a.hour > 23 || a.min < 0 ||
            a.min > 59) {
            free(a.sound);
            free(a.label);
            s->warnings++;
            continue;
        }
        if (s->n_alarms == s->cap_alarms) {
            s->cap_alarms = s->cap_alarms ? s->cap_alarms * 2 : 8;
            s->alarms = realloc(s->alarms,
                                s->cap_alarms * sizeof(ck_alarm));
        }
        s->alarms[s->n_alarms++] = a;
        if (a.id >= s->next_alarm_id) s->next_alarm_id = a.id + 1;
    }
    free(data);
}

static void load_timers(ck_store *s)
{
    char path[1024];
    path_of(s, "timers.txt", path, sizeof(path));
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *f[6];
        if (split_tabs(line, f, 6) != 6) { s->warnings++; continue; }
        ck_timer t;
        memset(&t, 0, sizeof(t));
        t.id = strtoull(f[0], NULL, 10);
        t.duration_sec = strtoll(f[1], NULL, 10);
        t.state = atoi(f[2]);
        t.remain_ms = strtoll(f[3], NULL, 10) * 1000;
        t.end_wall = strtoll(f[4], NULL, 10);
        char lab[512];
        bd_unescape(f[5], lab, sizeof(lab));
        t.label = xstrdup(lab);
        if (!t.id || t.duration_sec <= 0) {
            free(t.label);
            s->warnings++;
            continue;
        }
        if (t.state == CK_TMR_RINGING) t.state = CK_TMR_IDLE;
        if (s->n_timers == s->cap_timers) {
            s->cap_timers = s->cap_timers ? s->cap_timers * 2 : 8;
            s->timers = realloc(s->timers,
                                s->cap_timers * sizeof(ck_timer));
            s->tmr_dl = realloc(s->tmr_dl,
                                s->cap_timers * sizeof(int64_t));
        }
        s->tmr_dl[s->n_timers] = 0;
        s->timers[s->n_timers++] = t;
        if (t.id >= s->next_timer_id) s->next_timer_id = t.id + 1;
    }
    free(data);
}

ck_store *ck_open(const char *root)
{
    ck_store *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->root = xstrdup(root);
    s->next_alarm_id = 1;
    s->next_timer_id = 1;
    if (bd_mkdir_p(root) != 0) { free(s->root); free(s); return NULL; }
    bd_sweep_tmp(root);
    char path[1024];
    path_of(s, "counters", path, sizeof(path));
    size_t len;
    char *data = bd_read_all(path, &len);
    if (data) {
        char *save = NULL;
        for (char *line = strtok_r(data, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            unsigned long long v;
            if (sscanf(line, "next_alarm=%llu", &v) == 1)
                s->next_alarm_id = v;
            else if (sscanf(line, "next_timer=%llu", &v) == 1)
                s->next_timer_id = v;
        }
        free(data);
    }
    load_cities(s);
    load_alarms(s);
    load_timers(s);
    return s;
}

void ck_close(ck_store *s)
{
    if (!s) return;
    for (size_t i = 0; i < s->n_cities; i++) free(s->cities[i].name);
    for (size_t i = 0; i < s->n_alarms; i++) {
        free(s->alarms[i].sound);
        free(s->alarms[i].label);
    }
    for (size_t i = 0; i < s->n_timers; i++) free(s->timers[i].label);
    free(s->cities);
    free(s->alarms);
    free(s->timers);
    free(s->rt);
    free(s->tmr_dl);
    free(s->root);
    free(s);
}

int ck_load_warnings(const ck_store *s) { return s->warnings; }

/* ---------------- cities ---------------- */

size_t ck_city_count(const ck_store *s) { return s->n_cities; }
const ck_city *ck_city_at(const ck_store *s, size_t i)
{
    return i < s->n_cities ? &s->cities[i] : NULL;
}

int ck_city_add(ck_store *s, const char *name, int offset_min)
{
    if (!name || !*name) return 0;
    for (size_t i = 0; i < s->n_cities; i++)
        if (strcmp(s->cities[i].name, name) == 0) return 0;
    if (s->n_cities == s->cap_cities) {
        s->cap_cities = s->cap_cities ? s->cap_cities * 2 : 8;
        s->cities = realloc(s->cities, s->cap_cities * sizeof(ck_city));
    }
    s->cities[s->n_cities].name = xstrdup(name);
    s->cities[s->n_cities].offset_min = offset_min;
    s->n_cities++;
    return save_cities(s) == 0 ? 1 : 0;
}

int ck_city_remove(ck_store *s, size_t index)
{
    if (index >= s->n_cities) return -1;
    free(s->cities[index].name);
    memmove(&s->cities[index], &s->cities[index + 1],
            (s->n_cities - index - 1) * sizeof(ck_city));
    s->n_cities--;
    return save_cities(s);
}

int ck_city_set_offset(ck_store *s, size_t index, int offset_min)
{
    if (index >= s->n_cities) return -1;
    s->cities[index].offset_min = offset_min;
    return save_cities(s);
}

/* ---------------- alarms ---------------- */

size_t ck_alarm_count(const ck_store *s) { return s->n_alarms; }
const ck_alarm *ck_alarm_at(const ck_store *s, size_t i)
{
    return i < s->n_alarms ? &s->alarms[i] : NULL;
}
const ck_alarm *ck_alarm_find(const ck_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_alarms; i++)
        if (s->alarms[i].id == id) return &s->alarms[i];
    return NULL;
}

static int alarm_index(const ck_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_alarms; i++)
        if (s->alarms[i].id == id) return (int)i;
    return -1;
}

uint64_t ck_alarm_add(ck_store *s, const ck_alarm *fields)
{
    if (fields->hour < 0 || fields->hour > 23 || fields->min < 0 ||
        fields->min > 59)
        return 0;
    if (s->n_alarms == s->cap_alarms) {
        s->cap_alarms = s->cap_alarms ? s->cap_alarms * 2 : 8;
        s->alarms = realloc(s->alarms, s->cap_alarms * sizeof(ck_alarm));
    }
    ck_alarm *a = &s->alarms[s->n_alarms++];
    *a = *fields;
    a->id = s->next_alarm_id++;
    a->sound = xstrdup(fields->sound);
    a->label = xstrdup(fields->label);
    save_counters(s);
    save_alarms(s);
    return a->id;
}

int ck_alarm_update(ck_store *s, const ck_alarm *fields)
{
    int i = alarm_index(s, fields->id);
    if (i < 0) return -1;
    ck_alarm *a = &s->alarms[i];
    free(a->sound);
    free(a->label);
    *a = *fields;
    a->sound = xstrdup(fields->sound);
    a->label = xstrdup(fields->label);
    return save_alarms(s);
}

int ck_alarm_delete(ck_store *s, uint64_t id)
{
    int i = alarm_index(s, id);
    if (i < 0) return -1;
    free(s->alarms[i].sound);
    free(s->alarms[i].label);
    memmove(&s->alarms[i], &s->alarms[i + 1],
            (s->n_alarms - (size_t)i - 1) * sizeof(ck_alarm));
    s->n_alarms--;
    for (int q = 0; q < s->n_ringq; q++)
        if (s->ringq[q] == id) {
            memmove(&s->ringq[q], &s->ringq[q + 1],
                    (size_t)(s->n_ringq - q - 1) * sizeof(uint64_t));
            s->n_ringq--;
            break;
        }
    return save_alarms(s);
}

int ck_alarm_set_enabled(ck_store *s, uint64_t id, bool on)
{
    int i = alarm_index(s, id);
    if (i < 0) return -1;
    s->alarms[i].enabled = on;
    return save_alarms(s);
}

/* ---------------- timers ---------------- */

size_t ck_timer_count(const ck_store *s) { return s->n_timers; }
const ck_timer *ck_timer_at(const ck_store *s, size_t i)
{
    return i < s->n_timers ? &s->timers[i] : NULL;
}
const ck_timer *ck_timer_find(const ck_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_timers; i++)
        if (s->timers[i].id == id) return &s->timers[i];
    return NULL;
}

static int timer_index(const ck_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_timers; i++)
        if (s->timers[i].id == id) return (int)i;
    return -1;
}

uint64_t ck_timer_add(ck_store *s, const char *label, int64_t duration_sec)
{
    if (duration_sec <= 0 || duration_sec > 86400) return 0;
    if (s->n_timers == s->cap_timers) {
        s->cap_timers = s->cap_timers ? s->cap_timers * 2 : 8;
        s->timers = realloc(s->timers, s->cap_timers * sizeof(ck_timer));
        s->tmr_dl = realloc(s->tmr_dl, s->cap_timers * sizeof(int64_t));
    }
    ck_timer *t = &s->timers[s->n_timers];
    s->tmr_dl[s->n_timers] = 0;
    s->n_timers++;
    memset(t, 0, sizeof(*t));
    t->id = s->next_timer_id++;
    t->label = xstrdup(label);
    t->duration_sec = duration_sec;
    t->state = CK_TMR_IDLE;
    t->remain_ms = duration_sec * 1000;
    save_counters(s);
    save_timers(s);
    return t->id;
}

int ck_timer_delete(ck_store *s, uint64_t id)
{
    int i = timer_index(s, id);
    if (i < 0) return -1;
    free(s->timers[i].label);
    memmove(&s->timers[i], &s->timers[i + 1],
            (s->n_timers - (size_t)i - 1) * sizeof(ck_timer));
    memmove(&s->tmr_dl[i], &s->tmr_dl[i + 1],
            (s->n_timers - (size_t)i - 1) * sizeof(int64_t));
    s->n_timers--;
    return save_timers(s);
}

int ck_timer_start(ck_store *s, uint64_t id, int64_t wall, int64_t mono_ms)
{
    int i = timer_index(s, id);
    if (i < 0) return -1;
    ck_timer *t = &s->timers[i];
    if (t->state == CK_TMR_PAUSED) return ck_timer_resume(s, id, wall, mono_ms);
    if (t->state != CK_TMR_IDLE) return -1;
    t->remain_ms = t->duration_sec * 1000;
    s->tmr_dl[i] = mono_ms + t->remain_ms;
    t->end_wall = wall + t->duration_sec;
    t->state = CK_TMR_RUNNING;
    return save_timers(s);
}

int ck_timer_pause(ck_store *s, uint64_t id, int64_t mono_ms)
{
    int i = timer_index(s, id);
    if (i < 0) return -1;
    ck_timer *t = &s->timers[i];
    if (t->state != CK_TMR_RUNNING) return -1;
    t->remain_ms = s->tmr_dl[i] - mono_ms;
    if (t->remain_ms < 0) t->remain_ms = 0;
    t->state = CK_TMR_PAUSED;
    t->end_wall = 0;
    return save_timers(s);
}

int ck_timer_resume(ck_store *s, uint64_t id, int64_t wall, int64_t mono_ms)
{
    int i = timer_index(s, id);
    if (i < 0) return -1;
    ck_timer *t = &s->timers[i];
    if (t->state != CK_TMR_PAUSED) return -1;
    s->tmr_dl[i] = mono_ms + t->remain_ms;
    t->end_wall = wall + (t->remain_ms + 999) / 1000;
    t->state = CK_TMR_RUNNING;
    return save_timers(s);
}

int ck_timer_cancel(ck_store *s, uint64_t id)
{
    int i = timer_index(s, id);
    if (i < 0) return -1;
    ck_timer *t = &s->timers[i];
    t->state = CK_TMR_IDLE;
    t->remain_ms = t->duration_sec * 1000;
    t->end_wall = 0;
    s->tmr_dl[i] = 0;
    return save_timers(s);
}

int ck_timer_dismiss(ck_store *s, uint64_t id)
{
    int i = timer_index(s, id);
    if (i < 0) return -1;
    if (s->timers[i].state != CK_TMR_RINGING) return -1;
    return ck_timer_cancel(s, id);
}

/* ---------------- local offset ---------------- */

void ck_set_local_offset(ck_store *s, int offset_min)
{
    s->local_offset_min = offset_min;
}
int ck_local_offset(const ck_store *s) { return s->local_offset_min; }

/* ---------------- alarm runtime ---------------- */

static int rt_index(ck_store *s, uint64_t id, bool create)
{
    for (size_t i = 0; i < s->n_rt; i++)
        if (s->rt[i].id == id) return (int)i;
    if (!create) return -1;
    if (s->n_rt == s->cap_rt) {
        s->cap_rt = s->cap_rt ? s->cap_rt * 2 : 8;
        s->rt = realloc(s->rt, s->cap_rt * sizeof(*s->rt));
    }
    s->rt[s->n_rt].id = id;
    s->rt[s->n_rt].fired_day = -1;
    s->rt[s->n_rt].snooze_until = 0;
    return (int)s->n_rt++;
}

uint64_t ck_ringing_alarm(const ck_store *s)
{
    return s->n_ringq > 0 ? s->ringq[0] : 0;
}

static void ring_push(ck_store *s, uint64_t id)
{
    for (int i = 0; i < s->n_ringq; i++)
        if (s->ringq[i] == id) return;
    if (s->n_ringq < 16) s->ringq[s->n_ringq++] = id;
}

int ck_alarm_dismiss(ck_store *s)
{
    if (s->n_ringq == 0) return -1;
    memmove(&s->ringq[0], &s->ringq[1],
            (size_t)(s->n_ringq - 1) * sizeof(uint64_t));
    s->n_ringq--;
    return 0;
}

int ck_alarm_snooze(ck_store *s, int64_t mono_ms)
{
    if (s->n_ringq == 0) return -1;
    const ck_alarm *a = ck_alarm_find(s, s->ringq[0]);
    if (!a || a->snooze_min <= 0) return -1;
    int ri = rt_index(s, a->id, true);
    s->rt[ri].snooze_until = mono_ms + (int64_t)a->snooze_min * 60000;
    return ck_alarm_dismiss(s);
}

/* Fire epoch (wall) of alarm `a` on local day `day` (day count
 * in local civil time). */
static int64_t alarm_fire_epoch(const ck_store *s, const ck_alarm *a,
                                int64_t day)
{
    return day * 86400 + a->hour * 3600 + a->min * 60
           - (int64_t)s->local_offset_min * 60;
}

static bool alarm_day_matches(const ck_alarm *a, int64_t day)
{
    if (a->repeat_mask == 0) return true;      /* once: any day */
    int wday = ck_weekday_from_days(day);     /* 0=Sun..6=Sat */
    int bit = (wday + 6) % 7;                 /* Mon=0..Sun=6 */
    return (a->repeat_mask >> bit) & 1;
}

int64_t ck_next_alarm_epoch(const ck_store *s, int64_t wall)
{
    int64_t best = -1;
    int64_t off = (int64_t)s->local_offset_min * 60;
    int64_t day0 = (wall + off) / 86400;
    for (size_t ai = 0; ai < s->n_alarms; ai++) {
        const ck_alarm *a = &s->alarms[ai];
        if (!a->enabled) continue;
        /* 8 days covers every weekly repeat pattern. */
        for (int64_t day = day0; day <= day0 + 8; day++) {
            if (!alarm_day_matches(a, day)) continue;
            int64_t fire = alarm_fire_epoch(s, a, day);
            if (fire > wall && (best < 0 || fire < best))
                best = fire;
        }
    }
    return best;
}

int64_t ck_next_snooze_until(const ck_store *s)
{
    int64_t best = -1;
    for (size_t i = 0; i < s->n_rt; i++) {
        if (!s->rt[i].snooze_until) continue;
        if (best < 0 || s->rt[i].snooze_until < best)
            best = s->rt[i].snooze_until;
    }
    return best;
}

/* ---------------- poll ---------------- */

int ck_poll(ck_store *s, int64_t wall, int64_t mono_ms,
            ck_event *out, int max)
{
    int n = 0;
    if (!s->polled) {
        s->polled = true;
        s->last_wall = wall;
        s->last_mono = mono_ms;
        /* Re-arm persisted running timers against the wall
         * clock's persisted end; one whose end passed while the
         * device was off fires immediately below (remain <= 0). */
        for (size_t i = 0; i < s->n_timers; i++) {
            ck_timer *t = &s->timers[i];
            if (t->state != CK_TMR_RUNNING) continue;
            t->remain_ms = (t->end_wall - wall) * 1000;
            s->tmr_dl[i] = mono_ms + t->remain_ms;
        }
        s->timers_rearmed = true;
    }

    int64_t mono_delta_ms = mono_ms - s->last_mono;
    if (mono_delta_ms < 0) mono_delta_ms = 0;
    int64_t expected_wall = s->last_wall + mono_delta_ms / 1000;
    bool jump_fwd = wall > expected_wall + 2;
    bool jump_back = wall < expected_wall - 2;

    /* --- alarms --- */
    if (!jump_back) {
        int64_t off = (int64_t)s->local_offset_min * 60;
        int64_t day_from = (s->last_wall + off) / 86400 - 1;
        int64_t day_to = (wall + off) / 86400 + 1;
        for (size_t ai = 0; ai < s->n_alarms; ai++) {
            ck_alarm *a = &s->alarms[ai];
            if (!a->enabled) continue;
            int ri = rt_index(s, a->id, true);
            for (int64_t day = day_from; day <= day_to; day++) {
                if (!alarm_day_matches(a, day)) continue;
                int64_t fire = alarm_fire_epoch(s, a, day);
                if (fire <= s->last_wall || fire > wall) continue;
                if (s->rt[ri].fired_day == day) continue;
                s->rt[ri].fired_day = day;
                ring_push(s, a->id);
                if (n < max) {
                    out[n].kind = CK_EV_ALARM;
                    out[n].id = a->id;
                    out[n].catch_up = jump_fwd && fire > expected_wall;
                    n++;
                }
                if (a->repeat_mask == 0) {
                    a->enabled = false;   /* once: auto-disable */
                    save_alarms(s);
                }
            }
        }
    }
    /* snoozes (monotonic; wall jumps cannot affect them) */
    for (size_t i = 0; i < s->n_rt; i++) {
        if (s->rt[i].snooze_until && mono_ms >= s->rt[i].snooze_until) {
            s->rt[i].snooze_until = 0;
            const ck_alarm *a = ck_alarm_find(s, s->rt[i].id);
            if (a) {
                ring_push(s, a->id);
                if (n < max) {
                    out[n].kind = CK_EV_ALARM;
                    out[n].id = a->id;
                    out[n].catch_up = false;
                    n++;
                }
            }
        }
    }

    /* --- timers (monotonic deadlines) --- */
    for (size_t i = 0; i < s->n_timers; i++) {
        ck_timer *t = &s->timers[i];
        if (t->state != CK_TMR_RUNNING) continue;
        t->remain_ms = s->tmr_dl[i] - mono_ms;
        if (t->remain_ms <= 0) {
            t->remain_ms = 0;
            t->state = CK_TMR_RINGING;
            t->end_wall = 0;
            save_timers(s);
            if (n < max) {
                out[n].kind = CK_EV_TIMER;
                out[n].id = t->id;
                out[n].catch_up = false;
                n++;
            }
        }
    }

    s->last_wall = wall;
    s->last_mono = mono_ms;
    return n;
}

/* ---------------- stopwatch ---------------- */

void ck_sw_init(ck_stopwatch *sw) { memset(sw, 0, sizeof(*sw)); }

void ck_sw_start(ck_stopwatch *sw, int64_t mono_ms)
{
    if (sw->running) return;
    sw->running = true;
    sw->start_mono = mono_ms;
}

void ck_sw_stop(ck_stopwatch *sw, int64_t mono_ms)
{
    if (!sw->running) return;
    sw->accum_ms += mono_ms - sw->start_mono;
    sw->running = false;
}

void ck_sw_reset(ck_stopwatch *sw)
{
    sw->running = false;
    sw->accum_ms = 0;
    sw->n_laps = 0;
}

int64_t ck_sw_elapsed(const ck_stopwatch *sw, int64_t mono_ms)
{
    return sw->accum_ms + (sw->running ? mono_ms - sw->start_mono : 0);
}

int ck_sw_lap(ck_stopwatch *sw, int64_t mono_ms)
{
    if (!sw->running || sw->n_laps >= 99) return -1;
    sw->laps_ms[sw->n_laps] = ck_sw_elapsed(sw, mono_ms);
    return sw->n_laps++;
}

int64_t ck_sw_lap_duration(const ck_stopwatch *sw, int i)
{
    if (i < 0 || i >= sw->n_laps) return -1;
    return sw->laps_ms[i] - (i > 0 ? sw->laps_ms[i - 1] : 0);
}

int ck_sw_fastest(const ck_stopwatch *sw)
{
    if (sw->n_laps < 2) return -1;
    int best = 0;
    for (int i = 1; i < sw->n_laps; i++)
        if (ck_sw_lap_duration(sw, i) < ck_sw_lap_duration(sw, best))
            best = i;
    return best;
}

int ck_sw_slowest(const ck_stopwatch *sw)
{
    if (sw->n_laps < 2) return -1;
    int worst = 0;
    for (int i = 1; i < sw->n_laps; i++)
        if (ck_sw_lap_duration(sw, i) > ck_sw_lap_duration(sw, worst))
            worst = i;
    return worst;
}
