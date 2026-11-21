/* alarmd - resident alarm/timer evaluation for Brainux (OS
 * internals phase 2, following REPORT-os-internals-1.md 3.3).
 *
 * Until now ck_poll() was only called by bd-clock's own main
 * loop, so an alarm could not fire unless the clock app happened
 * to be open. This module is the evaluation half split out of
 * the app: it owns a ck_store on the SAME data root bd-clock
 * uses and evaluates it whenever the embedder calls
 * alarmd_poll(). Fired events leave through the sink:
 *
 *   - notify(): a user-visible notification (bd-alarmd's main
 *     wires this to the shell's notification IPC, notify.h;
 *     tests wire a recorder). A -1 return (shell unreachable)
 *     is counted but never fatal - the daemon keeps evaluating.
 *   - ring(): the "make it prominent" hook, called once per
 *     fired event after notify(). The rebuild stack currently
 *     has no audio playback path for alarms (bd-clock itself
 *     only shows a banner), so bd-alarmd's main implements this
 *     as a log line; the seam is where a future sound player
 *     plugs in. Nothing here pretends to make noise.
 *
 * Division of labour with bd-clock: outputs (notification /
 * ring) exist ONLY here, so there is no double output when
 * bd-clock is also open - bd-clock's banner is a view of its
 * own store instance and produces no notification or sound.
 * Snooze/dismiss act on whichever store instance receives
 * them; the daemon notifies once per firing and otherwise
 * leaves the model's ringing state alone.
 *
 * Sleep discipline: the embedder's main loop is event-driven
 * (timerfd + inotify in bd-alarmd). alarmd_next_sleep_ms()
 * computes the wait from the next alarm (wall), running timers
 * (monotonic remainder) and pending snoozes, capped at
 * ALARMD_MAX_SLEEP_MS so an embedder that cannot watch the
 * store files still notices edits within the cap.
 */
#ifndef ALARMD_H
#define ALARMD_H

#include <stdbool.h>
#include <stdint.h>

#include "clockmodel.h"

#define ALARMD_MAX_SLEEP_MS 30000

typedef struct {
    /* Post a notification. Returns its id (> 0) or -1 when the
     * notification channel is unavailable. */
    int (*notify)(void *ud, const char *app, const char *title,
                  const char *body, int urgency);
    /* Ring hook: kind is a ck_event_kind (CK_EV_ALARM /
     * CK_EV_TIMER), catch_up mirrors the poll event. May be
     * NULL. */
    void (*ring)(void *ud, int kind, uint64_t id, bool catch_up);
} alarmd_sink;

typedef struct alarmd alarmd;

/* Open the store at `root` (bd-clock's data root) and set the
 * local UTC offset (minutes) the alarms are stated in - the
 * same value bd-clock injects from brainreg "datetime". */
alarmd *alarmd_new(const char *root, int local_offset_min,
                   const alarmd_sink *sink, void *ud);
void alarmd_free(alarmd *d);

/* The owned store (inspection, and the embedder's snooze /
 * dismiss calls). Do not free it separately. */
ck_store *alarmd_store(alarmd *d);

/* Advance evaluation to (wall, mono_ms) - a thin, honest
 * wrapper over ck_poll(): every event it returns is notified
 * (and rung) exactly once here. Returns the number of events
 * fired, or -1 on misuse. Call it once before consulting
 * alarmd_next_sleep_ms() so timer remainders are current. */
int alarmd_poll(alarmd *d, int64_t wall, int64_t mono_ms);

/* Milliseconds the main loop may sleep before the next
 * evaluation is due: min(next alarm, running timers, pending
 * snoozes), clamped to [0, ALARMD_MAX_SLEEP_MS]. */
int64_t alarmd_next_sleep_ms(alarmd *d, int64_t wall, int64_t mono_ms);

/* Re-read the store from disk after an external edit (bd-clock
 * changed alarms.txt / timers.txt). Runtime state (ringing
 * queue, fired-day marks, snoozes) is reset and the next poll
 * re-baselines: an alarm whose fire time fell inside the edit
 * window is NOT caught up. Returns 0 on success. */
int alarmd_reload(alarmd *d);

/* Notifications that failed to post (channel down) so far. */
unsigned alarmd_notify_failures(const alarmd *d);

#endif /* ALARMD_H */
