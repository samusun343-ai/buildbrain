#define _POSIX_C_SOURCE 200809L
#include "tmodel.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TM_PATH_MAX 4096

struct tm_store {
    char *root;
    tm_list *lists;
    size_t n_lists, cap_lists;
    tm_task *tasks;
    size_t n_tasks, cap_tasks;
    uint64_t next_list_id, next_task_id, seq;
    int warnings;
};

/* ---------------------------------------------------------------- */
/* dates                                                             */
/* ---------------------------------------------------------------- */

static bool is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m)
{
    static const int dim[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2) return dim[m - 1] + (is_leap(y) ? 1 : 0);
    return dim[m - 1];
}

static bool parse_date(const char *d, int *y, int *m, int *dd)
{
    if (!d || strlen(d) != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (d[i] < '0' || d[i] > '9') return false;
    }
    *y = atoi(d);
    *m = atoi(d + 5);
    *dd = atoi(d + 8);
    if (*m < 1 || *m > 12) return false;
    if (*dd < 1 || *dd > days_in_month(*y, *m)) return false;
    return true;
}

bool tm_date_valid(const char *date)
{
    int y, m, d;
    return parse_date(date, &y, &m, &d);
}

/* days-from-civil (Howard Hinnant's algorithm, public domain math) */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 +
                   (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468L;
}

static void civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468L;
    long era = (z >= 0 ? z : z - 146096L) / 146097L;
    unsigned doe = (unsigned)(z - era * 146097L);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + (mp < 10 ? 3 : -9));
    *y = (int)(yy + (*m <= 2));
}

bool tm_date_advance(const char *date, int repeat, char out[11])
{
    int y, m, d;
    if (!parse_date(date, &y, &m, &d)) return false;
    switch (repeat) {
    case TM_REP_HOURLY:
        break; /* date unchanged; due_time carries the hour step */
    case TM_REP_DAILY:
    case TM_REP_WEEKLY:
    case TM_REP_BIWEEKLY: {
        long add = repeat == TM_REP_DAILY ? 1 :
                   repeat == TM_REP_WEEKLY ? 7 : 14;
        civil_from_days(days_from_civil(y, m, d) + add, &y, &m, &d);
        break;
    }
    case TM_REP_MONTHLY: {
        m++;
        if (m > 12) { m = 1; y++; }
        int dim = days_in_month(y, m);
        if (d > dim) d = dim;
        break;
    }
    case TM_REP_YEARLY: {
        y++;
        int dim = days_in_month(y, m);
        if (d > dim) d = dim;
        break;
    }
    default:
        return false;
    }
    snprintf(out, 11, "%04d-%02d-%02d", y, m, d);
    return true;
}

/* ---------------------------------------------------------------- */
/* persistence                                                       */
/* ---------------------------------------------------------------- */

static int store_path(tm_store *s, const char *name, char *out, size_t cap)
{
    return bd_path_join(out, cap, s->root, name);
}

static void save_counters(tm_store *s)
{
    char p[TM_PATH_MAX];
    if (store_path(s, "counters", p, sizeof(p)) != 0) return;
    char body[160];
    snprintf(body, sizeof(body),
             "next_list=%llu\nnext_task=%llu\nseq=%llu\n",
             (unsigned long long)s->next_list_id,
             (unsigned long long)s->next_task_id,
             (unsigned long long)s->seq);
    (void)bd_write_atomic_str(p, body);
}

static void save_lists(tm_store *s)
{
    char p[TM_PATH_MAX];
    if (store_path(s, "lists.txt", p, sizeof(p)) != 0) return;
    size_t cap = 256 + s->n_lists * 320;
    char *buf = malloc(cap);
    if (!buf) return;
    size_t w = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < s->n_lists; i++) {
        char esc[512];
        bd_escape(s->lists[i].name, esc, sizeof(esc));
        int n = snprintf(buf + w, cap - w, "%llu\t%s\t%d\t%llu\n",
                         (unsigned long long)s->lists[i].id, esc,
                         s->lists[i].color,
                         (unsigned long long)s->lists[i].seq);
        if (n < 0 || (size_t)n >= cap - w) break;
        w += (size_t)n;
    }
    (void)bd_write_atomic(p, buf, w);
    free(buf);
}

static void save_tasks(tm_store *s)
{
    char p[TM_PATH_MAX];
    if (store_path(s, "tasks.txt", p, sizeof(p)) != 0) return;
    size_t cap = 512 + s->n_tasks * 1024;
    char *buf = malloc(cap);
    if (!buf) return;
    size_t w = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < s->n_tasks; i++) {
        const tm_task *t = &s->tasks[i];
        char et[1024], en[2048], eu[512], es[256];
        bd_escape(t->title, et, sizeof(et));
        bd_escape(t->notes, en, sizeof(en));
        bd_escape(t->url, eu, sizeof(eu));
        bd_escape(t->section, es, sizeof(es));
        int n = snprintf(buf + w, cap - w,
                         "%llu\t%llu\t%llu\t%s\t%s\t%s\t%s\t%s\t%s\t"
                         "%d\t%d\t%d\t%llu\t%llu\n",
                         (unsigned long long)t->id,
                         (unsigned long long)t->list_id,
                         (unsigned long long)t->parent_id,
                         et, en, eu, es,
                         t->due_date, t->due_time,
                         t->priority, t->flagged ? 1 : 0, t->repeat,
                         (unsigned long long)t->seq,
                         (unsigned long long)t->done_seq);
        if (n < 0 || (size_t)n >= cap - w) break;
        w += (size_t)n;
    }
    (void)bd_write_atomic(p, buf, w);
    free(buf);
}

static void save_all(tm_store *s)
{
    save_lists(s);
    save_tasks(s);
    save_counters(s);
}

/* Split a line into at most `max` TAB-separated fields in place.
 * Returns the field count. */
static int split_fields(char *line, char **fields, int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

static char *dup_unescape(const char *s)
{
    size_t cap = strlen(s) + 1;
    char *out = malloc(cap);
    if (out) bd_unescape(s, out, cap);
    return out;
}

static void load_lists(tm_store *s)
{
    char p[TM_PATH_MAX];
    if (store_path(s, "lists.txt", p, sizeof(p)) != 0) return;
    size_t len = 0;
    char *text = bd_read_all(p, &len);
    if (!text) return;
    for (char *line = strtok(text, "\n"); line;
         line = strtok(NULL, "\n")) {
        char *f[4];
        int nf = split_fields(line, f, 4);
        if (nf < 4) { s->warnings++; continue; }
        if (s->n_lists == s->cap_lists) {
            s->cap_lists = s->cap_lists ? s->cap_lists * 2 : 8;
            s->lists = realloc(s->lists, s->cap_lists * sizeof(tm_list));
        }
        tm_list *l = &s->lists[s->n_lists];
        l->id = strtoull(f[0], NULL, 10);
        l->name = dup_unescape(f[1]);
        l->color = atoi(f[2]);
        l->seq = strtoull(f[3], NULL, 10);
        if (l->id == 0 || !l->name) { free(l->name); s->warnings++; continue; }
        s->n_lists++;
        if (l->id >= s->next_list_id) s->next_list_id = l->id + 1;
        if (l->seq > s->seq) s->seq = l->seq;
    }
    free(text);
}

static void load_tasks(tm_store *s)
{
    char p[TM_PATH_MAX];
    if (store_path(s, "tasks.txt", p, sizeof(p)) != 0) return;
    size_t len = 0;
    char *text = bd_read_all(p, &len);
    if (!text) return;
    for (char *line = strtok(text, "\n"); line;
         line = strtok(NULL, "\n")) {
        char *f[14];
        int nf = split_fields(line, f, 14);
        if (nf < 14) { s->warnings++; continue; }
        tm_task t;
        memset(&t, 0, sizeof(t));
        t.id = strtoull(f[0], NULL, 10);
        t.list_id = strtoull(f[1], NULL, 10);
        t.parent_id = strtoull(f[2], NULL, 10);
        t.title = dup_unescape(f[3]);
        t.notes = dup_unescape(f[4]);
        t.url = dup_unescape(f[5]);
        t.section = dup_unescape(f[6]);
        snprintf(t.due_date, sizeof(t.due_date), "%s", f[7]);
        snprintf(t.due_time, sizeof(t.due_time), "%s", f[8]);
        t.priority = atoi(f[9]);
        t.flagged = atoi(f[10]) != 0;
        t.repeat = atoi(f[11]);
        t.seq = strtoull(f[12], NULL, 10);
        t.done_seq = strtoull(f[13], NULL, 10);
        if (t.id == 0 || !t.title || !t.notes || !t.url || !t.section) {
            free(t.title); free(t.notes); free(t.url); free(t.section);
            s->warnings++;
            continue;
        }
        if (t.due_date[0] && !tm_date_valid(t.due_date)) {
            t.due_date[0] = '\0'; /* corrupt date degrades to none */
            s->warnings++;
        }
        if (s->n_tasks == s->cap_tasks) {
            s->cap_tasks = s->cap_tasks ? s->cap_tasks * 2 : 16;
            s->tasks = realloc(s->tasks, s->cap_tasks * sizeof(tm_task));
        }
        s->tasks[s->n_tasks++] = t;
        if (t.id >= s->next_task_id) s->next_task_id = t.id + 1;
        if (t.seq > s->seq) s->seq = t.seq;
        if (t.done_seq > s->seq) s->seq = t.done_seq;
    }
    free(text);
}

tm_store *tm_open(const char *root)
{
    tm_store *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->root = strdup(root);
    s->next_list_id = 1;
    s->next_task_id = 1;
    if (bd_mkdir_p(root) != 0) { tm_close(s); return NULL; }
    (void)bd_sweep_tmp(root);
    /* counters first (they seed the rebuild), then data (which may
     * raise them further when the counter file was lost/stale). */
    {
        char p[TM_PATH_MAX];
        if (store_path(s, "counters", p, sizeof(p)) == 0) {
            size_t len = 0;
            char *text = bd_read_all(p, &len);
            if (text) {
                for (char *line = strtok(text, "\n"); line;
                     line = strtok(NULL, "\n")) {
                    if (strncmp(line, "next_list=", 10) == 0)
                        s->next_list_id = strtoull(line + 10, NULL, 10);
                    else if (strncmp(line, "next_task=", 10) == 0)
                        s->next_task_id = strtoull(line + 10, NULL, 10);
                    else if (strncmp(line, "seq=", 4) == 0)
                        s->seq = strtoull(line + 4, NULL, 10);
                }
                free(text);
            }
        }
    }
    if (s->next_list_id == 0) s->next_list_id = 1;
    if (s->next_task_id == 0) s->next_task_id = 1;
    load_lists(s);
    load_tasks(s);
    /* heal: tasks pointing at a vanished list move to the default */
    if (s->n_lists == 0) {
        tm_list_create(s, "Reminders", 0);
    } else {
        uint64_t def = tm_default_list(s);
        bool changed = false;
        for (size_t i = 0; i < s->n_tasks; i++) {
            if (!tm_list_find(s, s->tasks[i].list_id)) {
                s->tasks[i].list_id = def;
                changed = true;
            }
            if (s->tasks[i].parent_id &&
                !tm_task_find(s, s->tasks[i].parent_id)) {
                s->tasks[i].parent_id = 0;
                changed = true;
            }
        }
        if (changed) save_tasks(s);
    }
    return s;
}

void tm_close(tm_store *s)
{
    if (!s) return;
    for (size_t i = 0; i < s->n_lists; i++) free(s->lists[i].name);
    free(s->lists);
    for (size_t i = 0; i < s->n_tasks; i++) {
        free(s->tasks[i].title);
        free(s->tasks[i].notes);
        free(s->tasks[i].url);
        free(s->tasks[i].section);
    }
    free(s->tasks);
    free(s->root);
    free(s);
}

int tm_load_warnings(const tm_store *s) { return s->warnings; }

/* ---------------------------------------------------------------- */
/* lists                                                             */
/* ---------------------------------------------------------------- */

size_t tm_list_count(const tm_store *s) { return s->n_lists; }

const tm_list *tm_list_at(const tm_store *s, size_t i)
{
    return i < s->n_lists ? &s->lists[i] : NULL;
}

const tm_list *tm_list_find(const tm_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_lists; i++)
        if (s->lists[i].id == id) return &s->lists[i];
    return NULL;
}

static tm_list *list_mut(tm_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_lists; i++)
        if (s->lists[i].id == id) return &s->lists[i];
    return NULL;
}

uint64_t tm_default_list(const tm_store *s)
{
    uint64_t best = 0, best_seq = UINT64_MAX;
    for (size_t i = 0; i < s->n_lists; i++) {
        if (s->lists[i].seq < best_seq) {
            best_seq = s->lists[i].seq;
            best = s->lists[i].id;
        }
    }
    return best;
}

uint64_t tm_list_create(tm_store *s, const char *name, int color)
{
    if (s->n_lists == s->cap_lists) {
        s->cap_lists = s->cap_lists ? s->cap_lists * 2 : 8;
        s->lists = realloc(s->lists, s->cap_lists * sizeof(tm_list));
    }
    tm_list *l = &s->lists[s->n_lists++];
    l->id = s->next_list_id++;
    l->name = strdup(name ? name : "List");
    l->color = color;
    l->seq = ++s->seq;
    save_all(s);
    return l->id;
}

int tm_list_rename(tm_store *s, uint64_t id, const char *name)
{
    tm_list *l = list_mut(s, id);
    if (!l) return -1;
    free(l->name);
    l->name = strdup(name ? name : "");
    save_lists(s);
    return 0;
}

int tm_list_set_color(tm_store *s, uint64_t id, int color)
{
    tm_list *l = list_mut(s, id);
    if (!l) return -1;
    l->color = color;
    save_lists(s);
    return 0;
}

int tm_list_delete(tm_store *s, uint64_t id)
{
    if (s->n_lists <= 1) return -1;
    size_t idx = 0;
    bool found = false;
    for (size_t i = 0; i < s->n_lists; i++)
        if (s->lists[i].id == id) { idx = i; found = true; break; }
    if (!found) return -1;
    /* target for orphaned tasks: the default, or the next list
     * when the default itself is being deleted */
    uint64_t target = tm_default_list(s);
    if (target == id) {
        uint64_t best_seq = UINT64_MAX;
        for (size_t i = 0; i < s->n_lists; i++) {
            if (s->lists[i].id == id) continue;
            if (s->lists[i].seq < best_seq) {
                best_seq = s->lists[i].seq;
                target = s->lists[i].id;
            }
        }
    }
    for (size_t i = 0; i < s->n_tasks; i++)
        if (s->tasks[i].list_id == id) s->tasks[i].list_id = target;
    free(s->lists[idx].name);
    memmove(&s->lists[idx], &s->lists[idx + 1],
            (s->n_lists - idx - 1) * sizeof(tm_list));
    s->n_lists--;
    save_all(s);
    return 0;
}

size_t tm_list_open_count(const tm_store *s, uint64_t list_id)
{
    size_t n = 0;
    for (size_t i = 0; i < s->n_tasks; i++)
        if (s->tasks[i].list_id == list_id && s->tasks[i].done_seq == 0)
            n++;
    return n;
}

/* ---------------------------------------------------------------- */
/* tasks                                                             */
/* ---------------------------------------------------------------- */

static tm_task *task_mut(tm_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_tasks; i++)
        if (s->tasks[i].id == id) return &s->tasks[i];
    return NULL;
}

const tm_task *tm_task_find(const tm_store *s, uint64_t id)
{
    for (size_t i = 0; i < s->n_tasks; i++)
        if (s->tasks[i].id == id) return &s->tasks[i];
    return NULL;
}

uint64_t tm_task_add(tm_store *s, const tm_task *fields)
{
    if (s->n_tasks == s->cap_tasks) {
        s->cap_tasks = s->cap_tasks ? s->cap_tasks * 2 : 16;
        s->tasks = realloc(s->tasks, s->cap_tasks * sizeof(tm_task));
    }
    tm_task *t = &s->tasks[s->n_tasks];
    memset(t, 0, sizeof(*t));
    t->id = s->next_task_id++;
    t->list_id = tm_list_find(s, fields->list_id)
                     ? fields->list_id
                     : tm_default_list(s);
    t->parent_id = fields->parent_id &&
                           tm_task_find(s, fields->parent_id)
                       ? fields->parent_id
                       : 0;
    t->title = strdup(fields->title ? fields->title : "");
    t->notes = strdup(fields->notes ? fields->notes : "");
    t->url = strdup(fields->url ? fields->url : "");
    t->section = strdup(fields->section ? fields->section : "");
    if (fields->due_date[0] && tm_date_valid(fields->due_date))
        snprintf(t->due_date, sizeof(t->due_date), "%s", fields->due_date);
    snprintf(t->due_time, sizeof(t->due_time), "%s", fields->due_time);
    t->priority = fields->priority;
    t->flagged = fields->flagged;
    t->repeat = fields->repeat;
    t->seq = ++s->seq;
    t->done_seq = 0;
    s->n_tasks++;
    save_all(s);
    return t->id;
}

int tm_task_update(tm_store *s, const tm_task *fields)
{
    tm_task *t = task_mut(s, fields->id);
    if (!t) return -1;
    if (fields->due_date[0] && !tm_date_valid(fields->due_date))
        return -1; /* validate before mutating anything */
    /* Duplicate first, free second: callers legitimately pass a
     * shallow copy of the stored task itself (tm_task_find result
     * with a few fields tweaked), whose string pointers alias the
     * ones being replaced. Freeing first would strdup from freed
     * memory. */
    char *nt = strdup(fields->title ? fields->title : "");
    char *nn = strdup(fields->notes ? fields->notes : "");
    char *nu = strdup(fields->url ? fields->url : "");
    char *ns = strdup(fields->section ? fields->section : "");
    free(t->title);
    free(t->notes);
    free(t->url);
    free(t->section);
    t->title = nt;
    t->notes = nn;
    t->url = nu;
    t->section = ns;
    if (tm_list_find(s, fields->list_id)) t->list_id = fields->list_id;
    t->parent_id = fields->parent_id &&
                           tm_task_find(s, fields->parent_id)
                       ? fields->parent_id
                       : 0;
    snprintf(t->due_date, sizeof(t->due_date), "%s", fields->due_date);
    snprintf(t->due_time, sizeof(t->due_time), "%s", fields->due_time);
    t->priority = fields->priority;
    t->flagged = fields->flagged;
    t->repeat = fields->repeat;
    save_tasks(s);
    return 0;
}

int tm_task_delete(tm_store *s, uint64_t id)
{
    size_t idx = 0;
    bool found = false;
    for (size_t i = 0; i < s->n_tasks; i++)
        if (s->tasks[i].id == id) { idx = i; found = true; break; }
    if (!found) return -1;
    uint64_t parent = s->tasks[idx].parent_id;
    for (size_t i = 0; i < s->n_tasks; i++)
        if (s->tasks[i].parent_id == id) s->tasks[i].parent_id = parent;
    free(s->tasks[idx].title);
    free(s->tasks[idx].notes);
    free(s->tasks[idx].url);
    free(s->tasks[idx].section);
    memmove(&s->tasks[idx], &s->tasks[idx + 1],
            (s->n_tasks - idx - 1) * sizeof(tm_task));
    s->n_tasks--;
    save_tasks(s);
    return 0;
}

int tm_task_complete(tm_store *s, uint64_t id, uint64_t *next_out)
{
    if (next_out) *next_out = 0;
    tm_task *t = task_mut(s, id);
    if (!t) return -1;
    if (t->done_seq != 0) return 0; /* already completed */
    t->done_seq = ++s->seq;
    uint64_t new_id = 0;
    if (t->repeat != TM_REP_NONE && t->due_date[0]) {
        char nd[11];
        if (tm_date_advance(t->due_date, t->repeat, nd)) {
            tm_task copy = *t; /* shallow: strings shared, add dups */
            copy.due_date[0] = '\0';
            snprintf(copy.due_date, sizeof(copy.due_date), "%s", nd);
            /* tm_task_add reads fields only; safe with shared ptrs */
            new_id = tm_task_add(s, &copy);
        }
    }
    save_tasks(s);
    if (next_out) *next_out = new_id;
    return 0;
}

int tm_task_uncomplete(tm_store *s, uint64_t id)
{
    tm_task *t = task_mut(s, id);
    if (!t) return -1;
    t->done_seq = 0;
    save_tasks(s);
    return 0;
}

void tm_subtask_progress(const tm_store *s, uint64_t id,
                         int *done, int *total)
{
    int d = 0, t = 0;
    for (size_t i = 0; i < s->n_tasks; i++) {
        if (s->tasks[i].parent_id != id) continue;
        t++;
        if (s->tasks[i].done_seq) d++;
    }
    if (done) *done = d;
    if (total) *total = t;
}

/* ---------------------------------------------------------------- */
/* queries                                                           */
/* ---------------------------------------------------------------- */

static tm_view g_view;

static int task_cmp(const void *a, const void *b)
{
    const tm_task *x = *(const tm_task *const *)a;
    const tm_task *y = *(const tm_task *const *)b;
    switch (g_view) {
    case TM_VIEW_COMPLETED:
        return (x->done_seq < y->done_seq) ? 1 :
               (x->done_seq > y->done_seq) ? -1 : 0;
    case TM_VIEW_TODAY:
    case TM_VIEW_SCHEDULED: {
        int c = strcmp(x->due_date, y->due_date);
        if (c) return c;
        if (x->priority != y->priority) return y->priority - x->priority;
        return (x->seq < y->seq) ? -1 : (x->seq > y->seq) ? 1 : 0;
    }
    case TM_VIEW_LIST: {
        bool xd = x->done_seq != 0, yd = y->done_seq != 0;
        if (xd != yd) return xd ? 1 : -1;
        if (xd) return (x->done_seq < y->done_seq) ? -1 :
                       (x->done_seq > y->done_seq) ? 1 : 0;
        return (x->seq < y->seq) ? -1 : (x->seq > y->seq) ? 1 : 0;
    }
    default:
        return (x->seq < y->seq) ? -1 : (x->seq > y->seq) ? 1 : 0;
    }
}

size_t tm_query(tm_store *s, tm_view view, uint64_t list_id,
                const char *today, const char *query,
                const tm_task ***out)
{
    *out = NULL;
    const tm_task **arr = malloc((s->n_tasks ? s->n_tasks : 1) *
                                 sizeof(tm_task *));
    if (!arr) return 0;
    size_t n = 0;
    for (size_t i = 0; i < s->n_tasks; i++) {
        const tm_task *t = &s->tasks[i];
        bool active = t->done_seq == 0;
        bool keep = false;
        switch (view) {
        case TM_VIEW_LIST:
            keep = t->list_id == list_id;
            break;
        case TM_VIEW_TODAY:
            keep = active && t->due_date[0] && today &&
                   strcmp(t->due_date, today) <= 0;
            break;
        case TM_VIEW_SCHEDULED:
            keep = active && t->due_date[0];
            break;
        case TM_VIEW_FLAGGED:
            keep = active && t->flagged;
            break;
        case TM_VIEW_ALL:
            keep = active;
            break;
        case TM_VIEW_COMPLETED:
            keep = !active;
            break;
        }
        if (!keep) continue;
        if (query && *query &&
            !bd_contains_ci(t->title, query) &&
            !bd_contains_ci(t->notes, query))
            continue;
        arr[n++] = t;
    }
    g_view = view;
    if (n > 1) qsort(arr, n, sizeof(tm_task *), task_cmp);
    *out = arr;
    return n;
}
