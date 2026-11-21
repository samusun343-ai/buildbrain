/* tmodel - bd-tasks reminders/tasks model (no toolkit dependency).
 *
 * Storage (Brainux area on the microSD only):
 *   <root>/lists.txt   one list per line, TSV with escaped fields
 *   <root>/tasks.txt   one task per line, same escaping
 *   <root>/counters    next ids + global sequence, "k=v" lines
 *
 * Every mutation rewrites the affected file atomically (bdstore:
 * tmp + fsync + rename + dir fsync), so a power cut leaves the old
 * or the new file, never a torn one. Loading is lenient: malformed
 * lines (e.g. a torn tail written by something else) are skipped
 * and counted in tm_load_warnings(); missing/corrupt counters are
 * rebuilt from the data itself. The store heals, it does not die.
 *
 * Dates: due dates are DATA (display, sorting, smart views). They
 * are never used to expire or delete anything - the device clock
 * is untrusted. "Today" is always supplied by the caller as a
 * YYYY-MM-DD string, which keeps the model deterministic and
 * testable.
 */
#ifndef TMODEL_H
#define TMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TM_PRIO_NONE = 0, TM_PRIO_LOW, TM_PRIO_MEDIUM, TM_PRIO_HIGH
} tm_priority;

typedef enum {
    TM_REP_NONE = 0, TM_REP_HOURLY, TM_REP_DAILY, TM_REP_WEEKLY,
    TM_REP_BIWEEKLY, TM_REP_MONTHLY, TM_REP_YEARLY
} tm_repeat;

typedef struct {
    uint64_t id;
    char *name;
    int color;               /* palette index 0..7 */
    uint64_t seq;            /* creation order */
} tm_list;

typedef struct {
    uint64_t id;
    uint64_t list_id;
    uint64_t parent_id;      /* 0 = top-level */
    char *title;
    char *notes;             /* per-task notes field */
    char *url;
    char *section;           /* section heading inside the list */
    char due_date[11];       /* "" or "YYYY-MM-DD" */
    char due_time[6];        /* "" or "HH:MM" */
    int priority;            /* tm_priority */
    bool flagged;            /* separate concept from priority */
    int repeat;              /* tm_repeat */
    uint64_t seq;            /* creation order */
    uint64_t done_seq;       /* 0 = active; else completion order */
} tm_task;

typedef struct tm_store tm_store;

tm_store *tm_open(const char *root);
void tm_close(tm_store *s);
/* Lines skipped while loading (torn/corrupt data tolerated). */
int tm_load_warnings(const tm_store *s);

/* ---------------- lists ---------------- */
size_t tm_list_count(const tm_store *s);
const tm_list *tm_list_at(const tm_store *s, size_t i);
const tm_list *tm_list_find(const tm_store *s, uint64_t id);
/* The default list: the earliest-created one. Quick capture and
 * tasks orphaned by a list deletion land here. */
uint64_t tm_default_list(const tm_store *s);
uint64_t tm_list_create(tm_store *s, const char *name, int color);
int tm_list_rename(tm_store *s, uint64_t id, const char *name);
int tm_list_set_color(tm_store *s, uint64_t id, int color);
/* Deleting a list moves its tasks to the default list (or, when
 * deleting the default itself, to the next-earliest list). The
 * last remaining list cannot be deleted. */
int tm_list_delete(tm_store *s, uint64_t id);
/* Incomplete-task count for a list (sidebar badge). */
size_t tm_list_open_count(const tm_store *s, uint64_t list_id);

/* ---------------- tasks ---------------- */
/* Add a task; fields.id/seq/done_seq are ignored (assigned by the
 * store). Returns the new id or 0. */
uint64_t tm_task_add(tm_store *s, const tm_task *fields);
/* Replace the editable fields of task fields.id. */
int tm_task_update(tm_store *s, const tm_task *fields);
const tm_task *tm_task_find(const tm_store *s, uint64_t id);
/* Deleting a task re-parents its subtasks to the deleted task's
 * parent (nothing silently vanishes two levels down). */
int tm_task_delete(tm_store *s, uint64_t id);
/* Complete a task. When it repeats and has a due date, the next
 * occurrence is created (same fields, due advanced by the repeat
 * interval) and its id is returned via next_out (0 when none).
 * Completing a parent does not complete its subtasks. */
int tm_task_complete(tm_store *s, uint64_t id, uint64_t *next_out);
int tm_task_uncomplete(tm_store *s, uint64_t id);
/* Subtask progress of a parent: direct children only. */
void tm_subtask_progress(const tm_store *s, uint64_t id,
                         int *done, int *total);

/* ---------------- views / queries ---------------- */

typedef enum {
    TM_VIEW_LIST = 0,        /* one list (list_id), sections kept */
    TM_VIEW_TODAY,           /* due today or overdue, active */
    TM_VIEW_SCHEDULED,       /* has a due date, active, by date */
    TM_VIEW_FLAGGED,         /* flagged, active */
    TM_VIEW_ALL,             /* all active */
    TM_VIEW_COMPLETED        /* all completed, newest first */
} tm_view;

/* Query tasks. `today` is "YYYY-MM-DD" (only used by TM_VIEW_TODAY).
 * `query` (NULL/"") filters title+notes case-insensitively.
 * Returns pointers to store-owned tasks in view order; the array
 * itself is malloc'd and caller-freed (tasks are NOT copied).
 * Ordering: LIST = active first (seq), then completed (done_seq);
 * TODAY = due date, priority desc, seq; SCHEDULED = due date, seq;
 * FLAGGED/ALL = seq; COMPLETED = done_seq desc. */
size_t tm_query(tm_store *s, tm_view view, uint64_t list_id,
                const char *today, const char *query,
                const tm_task ***out);

/* Advance a YYYY-MM-DD date by a repeat interval. Monthly/yearly
 * clamp the day to the target month's length (Jan 31 -> Feb 28).
 * Hourly keeps the date (the time-of-day lives in due_time).
 * Returns false on a malformed date or TM_REP_NONE. */
bool tm_date_advance(const char *date, int repeat, char out[11]);
/* Validate a YYYY-MM-DD string (real calendar date). */
bool tm_date_valid(const char *date);

#endif /* TMODEL_H */
