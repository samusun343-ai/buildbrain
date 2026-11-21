/* test_tasks - bd-tasks model tests (tmodel).
 *
 * Groups:
 *   L lists: create/rename/color/delete semantics, default list
 *   T tasks: add/update/find, field round-trip through disk
 *   V views: Today/Scheduled/Flagged/All/Completed + search
 *   R repeat: completion spawns the next occurrence, date math
 *   U subtasks: progress, re-parenting on delete
 *   P persistence + recovery: escaping round-trip, torn lines,
 *     lost counters, dangling list references
 */
#define _POSIX_C_SOURCE 200809L
#include "tmodel.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
static void check_str(const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) {
        passes++; printf("PASS %s -> %s\n", what, got);
    } else {
        fails++;
        printf("FAIL %s: got %s, want %s\n", what,
               got ? got : "(null)", want ? want : "(null)");
    }
}

static char ROOT[512];
static void path_of(char *out, size_t cap, const char *rel)
{
    snprintf(out, cap, "%s/%s", ROOT, rel);
}

static tm_task mk(const char *title, uint64_t list_id)
{
    tm_task t;
    memset(&t, 0, sizeof(t));
    t.title = (char *)title;
    t.list_id = list_id;
    return t;
}

static size_t query_ids(tm_store *s, tm_view v, uint64_t list,
                        const char *today, const char *q,
                        uint64_t *ids, size_t max)
{
    const tm_task **out = NULL;
    size_t n = tm_query(s, v, list, today, q, &out);
    size_t c = n < max ? n : max;
    for (size_t i = 0; i < c; i++) ids[i] = out[i]->id;
    free(out);
    return n;
}

int main(void)
{
    char tmpl[] = "/tmp/bdtasks-test-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    snprintf(ROOT, sizeof(ROOT), "%s", tmpl);

    tm_store *s = tm_open(ROOT);
    check_true("L open", s != NULL);
    check_int("L default list exists", (long)tm_list_count(s), 1);
    uint64_t def = tm_default_list(s);
    check_true("L default id", def != 0);
    check_str("L default name", tm_list_find(s, def)->name, "Reminders");

    /* ---------------- L: lists ---------------- */
    uint64_t work = tm_list_create(s, "仕事", 3);
    check_true("L create", work != 0 && work != def);
    check_int("L count 2", (long)tm_list_count(s), 2);
    check_int("L rename", tm_list_rename(s, work, "業務"), 0);
    check_str("L renamed", tm_list_find(s, work)->name, "業務");
    check_int("L color", tm_list_set_color(s, work, 5), 0);
    check_int("L color value", tm_list_find(s, work)->color, 5);

    /* ---------------- T: tasks ---------------- */
    tm_task f = mk("牛乳を買う", def);
    f.notes = "低脂肪のもの";
    f.priority = TM_PRIO_HIGH;
    f.flagged = true;
    snprintf(f.due_date, sizeof(f.due_date), "2026-11-20");
    snprintf(f.due_time, sizeof(f.due_time), "09:00");
    uint64_t t1 = tm_task_add(s, &f);
    check_true("T add", t1 != 0);
    const tm_task *got = tm_task_find(s, t1);
    check_true("T find", got != NULL);
    if (got) {
        check_str("T title", got->title, "牛乳を買う");
        check_str("T notes", got->notes, "低脂肪のもの");
        check_int("T prio", got->priority, TM_PRIO_HIGH);
        check_true("T flagged", got->flagged);
        check_str("T due", got->due_date, "2026-11-20");
        check_str("T time", got->due_time, "09:00");
    }
    check_int("T open count", (long)tm_list_open_count(s, def), 1);

    /* update: change priority/flag independently (separate concepts) */
    tm_task up = *got;
    up.priority = TM_PRIO_LOW;
    up.flagged = false;
    check_int("T update", tm_task_update(s, &up), 0);
    got = tm_task_find(s, t1);
    check_int("T updated prio", got->priority, TM_PRIO_LOW);
    check_true("T updated flag", !got->flagged);
    /* invalid date rejected without clobbering */
    tm_task bad = *got;
    snprintf(bad.due_date, sizeof(bad.due_date), "2026-02-30");
    check_true("T update bad date", tm_task_update(s, &bad) != 0);
    check_str("T date kept", tm_task_find(s, t1)->due_date, "2026-11-20");
    /* restore prio/flag for view tests */
    up = *tm_task_find(s, t1);
    up.priority = TM_PRIO_HIGH;
    up.flagged = true;
    tm_task_update(s, &up);

    /* more tasks for views */
    tm_task f2 = mk("昨日の宿題", def);
    snprintf(f2.due_date, sizeof(f2.due_date), "2026-11-19");
    uint64_t t2 = tm_task_add(s, &f2);
    tm_task f3 = mk("明日の会議", work);
    snprintf(f3.due_date, sizeof(f3.due_date), "2026-11-21");
    uint64_t t3 = tm_task_add(s, &f3);
    tm_task f4 = mk("いつかやる", def);
    uint64_t t4 = tm_task_add(s, &f4);
    check_true("T adds", t2 && t3 && t4);

    /* ---------------- V: views ---------------- */
    {
        uint64_t ids[8];
        size_t n = query_ids(s, TM_VIEW_TODAY, 0, "2026-11-20", NULL,
                             ids, 8);
        check_int("V today count", (long)n, 2); /* t1 today + t2 overdue */
        check_true("V today has t1/t2",
                   (ids[0] == t1 || ids[1] == t1) &&
                   (ids[0] == t2 || ids[1] == t2));
        /* sorted by date: overdue (11-19) before today (11-20) */
        check_int("V today order", (long)ids[0], (long)t2);
        n = query_ids(s, TM_VIEW_SCHEDULED, 0, "2026-11-20", NULL, ids, 8);
        check_int("V scheduled count", (long)n, 3);
        check_int("V scheduled order", (long)ids[0], (long)t2);
        n = query_ids(s, TM_VIEW_FLAGGED, 0, "2026-11-20", NULL, ids, 8);
        check_int("V flagged count", (long)n, 1);
        check_int("V flagged id", (long)ids[0], (long)t1);
        n = query_ids(s, TM_VIEW_ALL, 0, "2026-11-20", NULL, ids, 8);
        check_int("V all count", (long)n, 4);
        n = query_ids(s, TM_VIEW_LIST, def, "2026-11-20", NULL, ids, 8);
        check_int("V list count", (long)n, 3);
        n = query_ids(s, TM_VIEW_ALL, 0, "2026-11-20", "宿題", ids, 8);
        check_int("V search count", (long)n, 1);
        check_int("V search id", (long)ids[0], (long)t2);
        n = query_ids(s, TM_VIEW_COMPLETED, 0, "2026-11-20", NULL, ids, 8);
        check_int("V completed empty", (long)n, 0);
    }

    /* complete + completed view */
    uint64_t next = 99;
    check_int("V complete", tm_task_complete(s, t4, &next), 0);
    check_int("V complete no repeat", (long)next, 0);
    {
        uint64_t ids[8];
        size_t n = query_ids(s, TM_VIEW_COMPLETED, 0, "2026-11-20",
                             NULL, ids, 8);
        check_int("V completed count", (long)n, 1);
        check_int("V completed id", (long)ids[0], (long)t4);
        n = query_ids(s, TM_VIEW_ALL, 0, "2026-11-20", NULL, ids, 8);
        check_int("V all after complete", (long)n, 3);
        check_int("V open count after", (long)tm_list_open_count(s, def), 2);
    }
    check_int("V uncomplete", tm_task_uncomplete(s, t4), 0);
    check_int("V open count back", (long)tm_list_open_count(s, def), 3);

    /* ---------------- R: repeat + date math -------------------- */
    check_true("R valid leap", tm_date_valid("2024-02-29"));
    check_true("R invalid leap", !tm_date_valid("2026-02-29"));
    check_true("R invalid fmt", !tm_date_valid("2026/11/20"));
    {
        char out[11];
        check_true("R daily", tm_date_advance("2026-11-20", TM_REP_DAILY,
                                              out) &&
                              !strcmp(out, "2026-11-21"));
        check_true("R weekly month-end",
                   tm_date_advance("2026-11-30", TM_REP_WEEKLY, out) &&
                   !strcmp(out, "2026-12-07"));
        check_true("R biweekly year-end",
                   tm_date_advance("2026-12-25", TM_REP_BIWEEKLY, out) &&
                   !strcmp(out, "2027-01-08"));
        check_true("R monthly clamp",
                   tm_date_advance("2026-01-31", TM_REP_MONTHLY, out) &&
                   !strcmp(out, "2026-02-28"));
        check_true("R yearly leap clamp",
                   tm_date_advance("2024-02-29", TM_REP_YEARLY, out) &&
                   !strcmp(out, "2025-02-28"));
        check_true("R hourly same date",
                   tm_date_advance("2026-11-20", TM_REP_HOURLY, out) &&
                   !strcmp(out, "2026-11-20"));
        check_true("R none fails", !tm_date_advance("2026-11-20",
                                                    TM_REP_NONE, out));
    }
    tm_task fr = mk("週次レポート", work);
    fr.repeat = TM_REP_WEEKLY;
    snprintf(fr.due_date, sizeof(fr.due_date), "2026-11-20");
    uint64_t tr = tm_task_add(s, &fr);
    next = 0;
    check_int("R complete", tm_task_complete(s, tr, &next), 0);
    check_true("R next spawned", next != 0 && next != tr);
    const tm_task *nt = tm_task_find(s, next);
    check_true("R next exists", nt != NULL);
    if (nt) {
        check_str("R next due", nt->due_date, "2026-11-27");
        check_true("R next active", nt->done_seq == 0);
        check_str("R next title", nt->title, "週次レポート");
    }
    check_true("R original done", tm_task_find(s, tr)->done_seq != 0);
    /* completing again does not spawn twice */
    uint64_t next2 = 0;
    check_int("R complete again", tm_task_complete(s, tr, &next2), 0);
    check_int("R no double spawn", (long)next2, 0);

    /* ---------------- U: subtasks ------------------------------ */
    tm_task fp = mk("引っ越し", def);
    uint64_t tp = tm_task_add(s, &fp);
    tm_task fc = mk("段ボール調達", def);
    fc.parent_id = tp;
    uint64_t tc1 = tm_task_add(s, &fc);
    tm_task fc2 = mk("住所変更届", def);
    fc2.parent_id = tp;
    uint64_t tc2 = tm_task_add(s, &fc2);
    {
        int done = -1, total = -1;
        tm_subtask_progress(s, tp, &done, &total);
        check_int("U total", total, 2);
        check_int("U done 0", done, 0);
        tm_task_complete(s, tc1, NULL);
        tm_subtask_progress(s, tp, &done, &total);
        check_int("U done 1", done, 1);
    }
    /* deleting the parent re-parents children to top level */
    check_int("U delete parent", tm_task_delete(s, tp), 0);
    check_int("U child reparented", (long)tm_task_find(s, tc1)->parent_id, 0);
    check_int("U child2 reparented", (long)tm_task_find(s, tc2)->parent_id, 0);

    /* list deletion moves tasks (before persistence checks) */
    size_t work_open = tm_list_open_count(s, work);
    check_true("U work has tasks", work_open >= 1);
    check_int("L delete work list", tm_list_delete(s, work), 0);
    check_true("L work gone", tm_list_find(s, work) == NULL);
    check_int("L tasks moved", (long)tm_task_find(s, t3)->list_id, (long)def);
    check_true("L last list delete refused",
               tm_list_delete(s, def) != 0);

    tm_close(s);

    /* ---------------- P: persistence + recovery ---------------- */
    s = tm_open(ROOT);
    check_true("P reopen", s != NULL);
    check_int("P no warnings", tm_load_warnings(s), 0);
    const tm_task *p1 = tm_task_find(s, t1);
    check_true("P task survived", p1 != NULL);
    if (p1) {
        check_str("P title", p1->title, "牛乳を買う");
        check_str("P notes", p1->notes, "低脂肪のもの");
        check_true("P flagged", p1->flagged);
    }
    /* escaping round-trip: tabs/newlines/backslashes in fields */
    tm_task fe = mk("tab\tnewline\nbackslash\\end", def);
    fe.notes = "line1\nline2\ttabbed";
    uint64_t te = tm_task_add(s, &fe);
    tm_close(s);
    s = tm_open(ROOT);
    const tm_task *pe = tm_task_find(s, te);
    check_true("P escape survived", pe != NULL);
    if (pe) {
        check_str("P escape title", pe->title, "tab\tnewline\nbackslash\\end");
        check_str("P escape notes", pe->notes, "line1\nline2\ttabbed");
    }
    tm_close(s);

    /* torn last line in tasks.txt is skipped, data intact */
    {
        char p[600];
        path_of(p, sizeof(p), "tasks.txt");
        FILE *f = fopen(p, "ab");
        fputs("999\t1\t0\ttorn-partial-line", f); /* no newline, too few fields */
        fclose(f);
        s = tm_open(ROOT);
        check_true("P torn tolerated", tm_load_warnings(s) >= 1);
        check_true("P torn task absent", tm_task_find(s, 999) == NULL);
        check_true("P data intact", tm_task_find(s, t1) != NULL);
        tm_close(s);
    }
    /* lost counters are rebuilt; new ids do not collide */
    {
        char p[600];
        path_of(p, sizeof(p), "counters");
        if (unlink(p) != 0) perror("unlink counters");
        s = tm_open(ROOT);
        tm_task fn2 = mk("カウンタ復旧後の追加", def);
        uint64_t tn = tm_task_add(s, &fn2);
        check_true("P new id unique", tn > te && tn != t1);
        tm_close(s);
    }
    /* dangling list reference healed to default */
    {
        char p[600];
        path_of(p, sizeof(p), "tasks.txt");
        FILE *f = fopen(p, "ab");
        /* id 555, list 4242 (nonexistent), minimal valid fields */
        fputs("555\t4242\t0\t迷子タスク\t\t\t\t\t\t0\t0\t0\t9999\t0\n", f);
        fclose(f);
        s = tm_open(ROOT);
        const tm_task *lost = tm_task_find(s, 555);
        check_true("P orphan healed", lost != NULL &&
                                      lost->list_id == tm_default_list(s));
        tm_close(s);
    }

    printf("test_tasks: %d PASS, %d FAIL\n", passes, fails);
    return fails ? 1 : 0;
}
