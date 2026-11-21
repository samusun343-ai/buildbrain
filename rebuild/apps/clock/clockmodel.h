/* clockmodel - bd-clock model (no toolkit dependency).
 *
 * Four functions: world clock, alarms, stopwatch, timers.
 *
 * Time model - the device reality drives this design:
 *   - The wall clock is UNTRUSTED (offline by default, no NTP,
 *     the user may set it by hand at any moment). The monotonic
 *     clock never jumps. Callers therefore always supply BOTH:
 *     wall time as epoch seconds and a monotonic millisecond
 *     counter, and the model decides per feature which to trust.
 *   - Timers and snoozes run on the MONOTONIC clock: a wall jump
 *     can never make a 5-minute timer fire after 3 minutes or
 *     skip it entirely.
 *   - Alarms are inherently wall-clock ("7:00 on weekdays"), so
 *     they run on wall time, but the poll loop cross-checks wall
 *     against monotonic: if the wall clock moved by more than
 *     the monotonic delta allows (a jump), the schedule is
 *     recomputed from the new wall time. On a forward jump,
 *     alarms whose fire time fell inside the jumped interval
 *     fire ONCE (catch-up) instead of being silently missed;
 *     on a backward jump they simply re-arm.
 *   - World clock offsets are fixed minute offsets (standard
 *     time). There is no tz database on this device and no
 *     network to update one, so DST transitions are NOT applied;
 *     this is a documented limitation, and every city entry's
 *     offset is user-editable to compensate.
 *
 * Storage (Brainux area on the microSD only), all writes atomic
 * via bdstore:
 *   <root>/cities.txt  name \t offset_min
 *   <root>/alarms.txt  id \t hh \t mm \t repeat_mask \t enabled
 *                      \t snooze_min \t sound \t label(escaped)
 *   <root>/timers.txt  id \t duration_sec \t state \t remain_sec
 *                      \t end_wall \t label(escaped)
 *   <root>/counters    next ids
 * Loading is lenient (torn/corrupt lines are skipped and
 * counted), like the wave-1 stores.
 *
 * Dates are computed with civil-date arithmetic (days from
 * civil), never with localtime(): the model is deterministic
 * given the caller's (wall, mono) pair, which is what makes it
 * testable.
 */
#ifndef CLOCKMODEL_H
#define CLOCKMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------- civil time ---------------- */

/* Days since 1970-01-01 for a civil date (proleptic Gregorian). */
int64_t ck_days_from_civil(int y, int m, int d);
/* Inverse of ck_days_from_civil. */
void ck_civil_from_days(int64_t z, int *y, int *m, int *d);
/* Weekday for a day count: 0 = Sunday .. 6 = Saturday. */
int ck_weekday_from_days(int64_t z);
/* Break an epoch second + fixed offset into civil fields. */
typedef struct {
    int year, mon, mday, hour, min, sec, wday; /* wday 0=Sun */
} ck_broken;
void ck_break_down(int64_t epoch, int offset_min, ck_broken *out);
/* "HH:MM" / "HH:MM:SS" formatting into out (NUL-terminated). */
void ck_format_hm(int hour, int min, char *out, size_t cap);
void ck_format_hms(int64_t total_sec, char *out, size_t cap);

/* ---------------- world clock ---------------- */

typedef struct {
    char *name;
    int offset_min;          /* minutes east of UTC */
} ck_city;

/* Built-in city table (standard-time offsets; see header note
 * about DST). Returns count; entries are static storage. */
size_t ck_builtin_count(void);
const ck_city *ck_builtin_at(size_t i);
const ck_city *ck_builtin_find(const char *name);

/* ---------------- alarms ---------------- */

/* repeat_mask: 0 = once; otherwise bit per weekday with
 * bit 0 = Monday .. bit 6 = Sunday. */
#define CK_WD_MON 0
#define CK_WD_SUN 6
typedef struct {
    uint64_t id;
    int hour, min;           /* local wall time */
    int repeat_mask;
    bool enabled;
    int snooze_min;          /* 0 = snooze disabled */
    char *sound;             /* sound name ("" = default) */
    char *label;
} ck_alarm;

/* ---------------- timers ---------------- */

typedef enum {
    CK_TMR_IDLE = 0,         /* set but not started */
    CK_TMR_RUNNING,
    CK_TMR_PAUSED,
    CK_TMR_RINGING           /* fired, waiting for dismiss */
} ck_timer_state;

typedef struct {
    uint64_t id;
    char *label;
    int64_t duration_sec;
    int state;               /* ck_timer_state */
    int64_t remain_ms;       /* runtime: ms left (mono-based) */
    int64_t end_wall;        /* persisted wall end while running */
} ck_timer;

/* ---------------- events ---------------- */

typedef enum { CK_EV_ALARM = 1, CK_EV_TIMER } ck_event_kind;
typedef struct {
    int kind;                /* ck_event_kind */
    uint64_t id;             /* alarm or timer id */
    bool catch_up;           /* alarm fired after a forward jump */
} ck_event;

/* ---------------- store ---------------- */

typedef struct ck_store ck_store;

ck_store *ck_open(const char *root);
void ck_close(ck_store *s);
int ck_load_warnings(const ck_store *s);

/* The device's local UTC offset (minutes). Alarms are stated
 * in local wall time, so the model needs this to compute fire
 * epochs from the caller's UTC epoch seconds. The settings app
 * owns the value (brainreg "datetime" section); the clock app
 * injects it here. Default 0 (UTC). */
void ck_set_local_offset(ck_store *s, int offset_min);
int ck_local_offset(const ck_store *s);

/* cities (the user's world-clock list) */
size_t ck_city_count(const ck_store *s);
const ck_city *ck_city_at(const ck_store *s, size_t i);
/* Add a city (name must be non-empty; duplicates by name are
 * rejected with 0). Returns 1 on success. */
int ck_city_add(ck_store *s, const char *name, int offset_min);
int ck_city_remove(ck_store *s, size_t index);
int ck_city_set_offset(ck_store *s, size_t index, int offset_min);

/* alarms */
size_t ck_alarm_count(const ck_store *s);
const ck_alarm *ck_alarm_at(const ck_store *s, size_t i);
const ck_alarm *ck_alarm_find(const ck_store *s, uint64_t id);
/* fields.id is ignored (assigned). Returns the new id or 0. */
uint64_t ck_alarm_add(ck_store *s, const ck_alarm *fields);
int ck_alarm_update(ck_store *s, const ck_alarm *fields);
int ck_alarm_delete(ck_store *s, uint64_t id);
int ck_alarm_set_enabled(ck_store *s, uint64_t id, bool on);

/* timers */
size_t ck_timer_count(const ck_store *s);
const ck_timer *ck_timer_at(const ck_store *s, size_t i);
const ck_timer *ck_timer_find(const ck_store *s, uint64_t id);
uint64_t ck_timer_add(ck_store *s, const char *label,
                      int64_t duration_sec);
int ck_timer_delete(ck_store *s, uint64_t id);
/* Start (from idle/paused semantics: start always re-arms the
 * full duration when idle, resumes the remainder when paused). */
int ck_timer_start(ck_store *s, uint64_t id, int64_t wall, int64_t mono_ms);
int ck_timer_pause(ck_store *s, uint64_t id, int64_t mono_ms);
int ck_timer_resume(ck_store *s, uint64_t id, int64_t wall, int64_t mono_ms);
int ck_timer_cancel(ck_store *s, uint64_t id);   /* back to idle */
int ck_timer_dismiss(ck_store *s, uint64_t id);  /* ringing -> idle */

/* ---------------- ringing / snooze ---------------- */

/* The alarm currently ringing (0 = none) and the timer ids are
 * reported through poll events; ringing state is runtime-only
 * except that a fired once-alarm disables itself persistently. */
uint64_t ck_ringing_alarm(const ck_store *s);
/* Snooze the ringing alarm: it stops ringing now and fires
 * again after the alarm's snooze_min minutes of MONOTONIC time.
 * Returns -1 when nothing is ringing or snooze is disabled. */
int ck_alarm_snooze(ck_store *s, int64_t mono_ms);
int ck_alarm_dismiss(ck_store *s);  /* stop ringing, no snooze */

/* ---------------- poll ---------------- */

/* Advance the model to (wall, mono_ms) and collect events.
 * Must be called with non-decreasing mono_ms. Fills up to `max`
 * events and returns how many were filled; events beyond max
 * stay pending for the next call. The first call establishes
 * the (wall, mono) baseline and re-arms persisted running
 * timers (a timer whose persisted wall end already passed
 * fires immediately as an expired timer). */
int ck_poll(ck_store *s, int64_t wall, int64_t mono_ms,
            ck_event *out, int max);

/* The earliest fire epoch (wall seconds) strictly after `wall`
 * among enabled alarms, or -1 when none is scheduled within the
 * next 8 days. Pure query (no state change); the alarm daemon
 * uses it to size its event-driven sleep. */
int64_t ck_next_alarm_epoch(const ck_store *s, int64_t wall);
/* The earliest pending snooze deadline (monotonic ms) among
 * snoozed alarms, or -1 when none is snoozing. Pure query. */
int64_t ck_next_snooze_until(const ck_store *s);

/* ---------------- stopwatch (runtime only) ---------------- */

typedef struct {
    bool running;
    int64_t start_mono;      /* mono at (re)start */
    int64_t accum_ms;        /* elapsed banked before this run */
    int64_t laps_ms[99];     /* total elapsed at each lap */
    int n_laps;
} ck_stopwatch;

void ck_sw_init(ck_stopwatch *sw);
void ck_sw_start(ck_stopwatch *sw, int64_t mono_ms);
void ck_sw_stop(ck_stopwatch *sw, int64_t mono_ms);
void ck_sw_reset(ck_stopwatch *sw);
int64_t ck_sw_elapsed(const ck_stopwatch *sw, int64_t mono_ms);
/* Record a lap (running only). Returns lap index or -1. */
int ck_sw_lap(ck_stopwatch *sw, int64_t mono_ms);
/* Duration of lap i (lap 0 = from start). */
int64_t ck_sw_lap_duration(const ck_stopwatch *sw, int i);
/* Indices of fastest / slowest lap by duration (-1 when < 2). */
int ck_sw_fastest(const ck_stopwatch *sw);
int ck_sw_slowest(const ck_stopwatch *sw);

#endif /* CLOCKMODEL_H */
