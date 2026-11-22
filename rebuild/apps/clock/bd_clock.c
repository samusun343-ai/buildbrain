#define _POSIX_C_SOURCE 200809L
#include "bd_clock.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"
#include "bdstore.h"
#ifdef BD_CLOCK_MAIN
#include "reg.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *const wd_names[7] = {
    "日", "月", "火", "水", "木", "金", "土"   /* index: wday 0=Sun */
};

/* One navigation destination slot (bottom-bar cell / nav-rail
 * item): carries the app and the PUBLIC page index it opens,
 * because the navigation order differs from the page order
 * (see clock_app_new). */
typedef struct {
    clock_app *app;
    int page;
} ck_navslot;

struct clock_app {
    ck_store *store;
    int local_off;
    bool winui;                 /* tk_theme_default()->style == 2,
                                 * read once in the constructor
                                 * (bdappmode contract) */
    int tab_idx;                /* public: 0 world, 1 alarms,
                                 * 2 stopwatch, 3 timers */
    tk_widget *pages[4];
    tk_widget *navbar;          /* Android: bottom bar;
                                 * WinUI: navigation rail */
    int nav_page_for_pos[4];    /* nav position -> page index */
    int nav_pos_for_page[4];    /* page index -> nav position */
    ck_navslot nav_slots[4];
    int win_w, win_h;
    tk_root *root;
    tk_widget *top;
    tk_widget *tabs;
    tk_widget *now_label;
    tk_widget *banner;
    tk_widget *snooze_btn;
    /* world */
    tk_widget *city_list;
    tk_widget *city_combo;
    /* alarms */
    tk_widget *alarm_list;
    uint64_t *alarm_ids;
    size_t n_alarm_ids;
    uint64_t selected_alarm;
    tk_widget *e_hour, *e_min, *e_label, *e_sound, *e_snooze;
    tk_widget *e_wd[7];          /* checkboxes Mon..Sun */
    /* stopwatch */
    ck_stopwatch sw;
    tk_widget *sw_label;
    tk_widget *sw_laps_list;
    int64_t last_mono;
    int64_t last_wall;
    /* timers */
    tk_widget *timer_list;
    tk_widget *t_h, *t_m, *t_s, *t_label;
    /* ringing */
    uint64_t ringing_timer;
};

static tk_widget *btn(const char *label, tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    tk_button_set_on_click(b, cb, ud);
    return b;
}

/* Real icon from the established vector sets (bdicon.h):
 * Material in Android mode, Fluent in WinUI mode, tinted in
 * the theme text colour. NULL when the icon file is missing
 * (callers fall back to label-only forms, as before). */
static const uint32_t *mode_icon(clock_app *app, const char *material,
                                 const char *fluent, int box)
{
    const tk_theme *t = tk_theme_default();
    uint32_t tint = ((uint32_t)t->text.r << 16) |
                    ((uint32_t)t->text.g << 8) | (uint32_t)t->text.b;
    return bd_icon_mode(app->winui, material, fluent, box, tint);
}

/* Button with a real icon left of its label (add actions). */
static tk_widget *icon_text_btn(clock_app *app, const char *label,
                                const char *material,
                                const char *fluent,
                                tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    const uint32_t *px = mode_icon(app, material, fluent, 24);
    if (px) {
        tk_button_set_icon(b, px, 24, 24);
        tk_button_set_icon_layout(b, TK_BTN_ICON_ROW);
    }
    tk_button_set_on_click(b, cb, ud);
    return b;
}

static void fmt_offset(int off, char *out, size_t cap)
{
    int a = off < 0 ? -off : off;
    snprintf(out, cap, "%s%d:%02d", off < 0 ? "-" : "+", a / 60, a % 60);
}

/* ---------------------------------------------------------------- */
/* refresh helpers (setters + explicit invalidate, wave-1 rule)      */
/* ---------------------------------------------------------------- */

static void refresh_cities(clock_app *app, int64_t wall)
{
    size_t n = ck_city_count(app->store);
    const char **items = malloc((n ? n : 1) * sizeof(char *));
    char **owned = calloc(n ? n : 1, sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        const ck_city *c = ck_city_at(app->store, i);
        ck_broken b;
        ck_break_down(wall, c->offset_min, &b);
        char hm[8], off[16], buf[256];
        ck_format_hm(b.hour, b.min, hm, sizeof(hm));
        fmt_offset(c->offset_min - app->local_off, off, sizeof(off));
        int ddiff = (int)((wall + (int64_t)c->offset_min * 60) / 86400
                          - (wall + (int64_t)app->local_off * 60) / 86400);
        snprintf(buf, sizeof(buf), "%s %s (%s%s)", c->name, hm,
                 ddiff > 0 ? "翌日 " : ddiff < 0 ? "前日 " : "",
                 off);
        items[i] = owned[i] = strdup(buf);
    }
    tk_listview_set_items(app->city_list, items, n);
    for (size_t i = 0; i < n; i++) free(owned[i]);
    free(owned);
    free(items);
    tk_widget_invalidate(app->city_list);
}

static void repeat_summary(int mask, char *out, size_t cap)
{
    if (mask == 0) { snprintf(out, cap, "1回のみ"); return; }
    if (mask == 0x7F) { snprintf(out, cap, "毎日"); return; }
    if (mask == 0x1F) { snprintf(out, cap, "平日"); return; }
    if (mask == 0x60) { snprintf(out, cap, "週末"); return; }
    /* bits Mon=0..Sun=6 -> wday indices 1..6,0 */
    static const int bit_wday[7] = { 1, 2, 3, 4, 5, 6, 0 };
    out[0] = 0;
    for (int bit = 0; bit < 7; bit++)
        if (mask & (1 << bit))
            strncat(out, wd_names[bit_wday[bit]],
                    cap - strlen(out) - 1);
}

static void refresh_alarms(clock_app *app)
{
    size_t n = ck_alarm_count(app->store);
    free(app->alarm_ids);
    app->alarm_ids = malloc((n ? n : 1) * sizeof(uint64_t));
    app->n_alarm_ids = n;
    const char **items = malloc((n ? n : 1) * sizeof(char *));
    char **owned = calloc(n ? n : 1, sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        const ck_alarm *a = ck_alarm_at(app->store, i);
        app->alarm_ids[i] = a->id;
        char hm[8], rep[64], buf[512];
        ck_format_hm(a->hour, a->min, hm, sizeof(hm));
        repeat_summary(a->repeat_mask, rep, sizeof(rep));
        snprintf(buf, sizeof(buf), "%s [%s] %s%s%s", hm,
                 a->enabled ? "ON" : "OFF", rep,
                 a->label[0] ? " " : "", a->label);
        items[i] = owned[i] = strdup(buf);
    }
    tk_listview_set_items(app->alarm_list, items, n);
    for (size_t i = 0; i < n; i++) free(owned[i]);
    free(owned);
    free(items);
    tk_widget_invalidate(app->alarm_list);
}

static void refresh_timers(clock_app *app)
{
    size_t n = ck_timer_count(app->store);
    const char **items = malloc((n ? n : 1) * sizeof(char *));
    char **owned = calloc(n ? n : 1, sizeof(char *));
    static const char *const st_names[] = {
        "待機", "実行中", "一時停止", "終了"
    };
    for (size_t i = 0; i < n; i++) {
        const ck_timer *t = ck_timer_at(app->store, i);
        char dur[16], rem[16], buf[512];
        ck_format_hms(t->duration_sec, dur, sizeof(dur));
        ck_format_hms((t->remain_ms + 999) / 1000, rem, sizeof(rem));
        snprintf(buf, sizeof(buf), "%s %s [%s] 残り %s",
                 dur, t->label, st_names[t->state & 3], rem);
        items[i] = owned[i] = strdup(buf);
    }
    tk_listview_set_items(app->timer_list, items, n);
    for (size_t i = 0; i < n; i++) free(owned[i]);
    free(owned);
    free(items);
    tk_widget_invalidate(app->timer_list);
}

static void refresh_sw(clock_app *app, int64_t mono)
{
    int64_t el = ck_sw_elapsed(&app->sw, mono);
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%d",
             (int)(el / 3600000), (int)((el / 60000) % 60),
             (int)((el / 1000) % 60), (int)((el / 100) % 10));
    tk_label_set_text(app->sw_label, buf);
    tk_widget_invalidate(app->sw_label);
    int fast = ck_sw_fastest(&app->sw), slow = ck_sw_slowest(&app->sw);
    const char **items = malloc((size_t)(app->sw.n_laps ? app->sw.n_laps : 1)
                                * sizeof(char *));
    char **owned = calloc((size_t)(app->sw.n_laps ? app->sw.n_laps : 1),
                          sizeof(char *));
    for (int i = 0; i < app->sw.n_laps; i++) {
        char d[16], t[16], b2[128];
        ck_format_hms(ck_sw_lap_duration(&app->sw, i) / 1000, d, sizeof(d));
        ck_format_hms(app->sw.laps_ms[i] / 1000, t, sizeof(t));
        snprintf(b2, sizeof(b2), "ラップ %d: %s (合計 %s)%s%s",
                 i + 1, d, t,
                 i == fast ? " 最速" : "", i == slow ? " 最遅" : "");
        items[i] = owned[i] = strdup(b2);
    }
    tk_listview_set_items(app->sw_laps_list, items,
                          (size_t)app->sw.n_laps);
    for (int i = 0; i < app->sw.n_laps; i++) free(owned[i]);
    free(owned);
    free(items);
    tk_widget_invalidate(app->sw_laps_list);
}

static void refresh_banner(clock_app *app)
{
    uint64_t aid = ck_ringing_alarm(app->store);
    char buf[512] = "";
    if (aid) {
        const ck_alarm *a = ck_alarm_find(app->store, aid);
        if (a) {
            char hm[8];
            ck_format_hm(a->hour, a->min, hm, sizeof(hm));
            snprintf(buf, sizeof(buf), "アラーム %s %s", hm, a->label);
        }
    } else if (app->ringing_timer) {
        const ck_timer *t = ck_timer_find(app->store, app->ringing_timer);
        snprintf(buf, sizeof(buf), "タイマー終了 %s", t ? t->label : "");
    }
    tk_label_set_text(app->banner, buf);
    tk_widget_invalidate(app->banner);
    bool ring = aid != 0 || app->ringing_timer != 0;
    tk_set_visible(app->snooze_btn,
                   aid != 0 && ck_alarm_find(app->store, aid) &&
                   ck_alarm_find(app->store, aid)->snooze_min > 0);
    tk_set_visible(app->banner, ring);
    tk_root_invalidate(app->root, app->top);
}

/* ---------------------------------------------------------------- */
/* tick                                                              */
/* ---------------------------------------------------------------- */

int clock_app_tick(clock_app *app, int64_t wall, int64_t mono_ms)
{
    app->last_mono = mono_ms;
    app->last_wall = wall;
    ck_event evs[8];
    int nev = ck_poll(app->store, wall, mono_ms, evs, 8);
    for (int i = 0; i < nev; i++)
        if (evs[i].kind == CK_EV_TIMER) app->ringing_timer = evs[i].id;
    ck_broken lb;
    ck_break_down(wall, app->local_off, &lb);
    char buf[128];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d (%s) %02d:%02d:%02d",
             lb.year, lb.mon, lb.mday, wd_names[lb.wday & 7],
             lb.hour, lb.min, lb.sec);
    tk_label_set_text(app->now_label, buf);
    tk_widget_invalidate(app->now_label);
    refresh_cities(app, wall);
    refresh_timers(app);
    if (app->sw.running) refresh_sw(app, mono_ms);
    refresh_banner(app);
    return nev;
}

/* ---------------------------------------------------------------- */
/* world clock actions                                               */
/* ---------------------------------------------------------------- */

/* Single page-switch path for both modes: only the selector
 * chrome differs (Android bottom bar / WinUI nav rail); the
 * pages and the public tab indices are the same. */
static void clock_show_page(clock_app *app, int tab);

int clock_app_tab(const clock_app *app) { return app->tab_idx; }
void clock_app_set_tab(clock_app *app, int tab)
{
    clock_show_page(app, tab);
}

static void clock_show_page(clock_app *app, int tab)
{
    if (tab < 0) tab = 0;
    if (tab > 3) tab = 3;
    app->tab_idx = tab;
    for (int i = 0; i < 4; i++)
        if (app->pages[i]) {
            tk_set_visible(app->pages[i], i == tab);
            tk_widget_invalidate(app->pages[i]);
        }
    if (app->navbar) {
        int pos = app->nav_pos_for_page[tab];
        if (app->winui) tk_navrail_select(app->navbar, pos);
        else tk_bottombar_set_selected(app->navbar, pos);
    }
    if (app->root && app->win_w > 0)
        tk_root_layout(app->root, app->win_w, app->win_h);
}

static void on_nav_cell(tk_widget *w, void *ud)
{
    (void)w;
    ck_navslot *slot = ud;
    if (slot && slot->app) clock_show_page(slot->app, slot->page);
}

static void on_nav_rail(tk_widget *w, void *ud)
{
    clock_app *app = ud;
    int pos = tk_navrail_selected(w);
    if (pos >= 0 && pos < 4)
        clock_show_page(app, app->nav_page_for_pos[pos]);
}
size_t clock_app_city_rows(const clock_app *app)
{
    return ck_city_count(app->store);
}

int clock_app_add_chosen_city(clock_app *app)
{
    int sel = tk_combobox_selected(app->city_combo);
    if (sel < 0) sel = 0;
    const ck_city *b = ck_builtin_at((size_t)sel);
    if (!b) return -1;
    return ck_city_add(app->store, b->name, b->offset_min) ? 0 : -1;
}

int clock_app_remove_city_at(clock_app *app, size_t row)
{
    int rc = ck_city_remove(app->store, row);
    if (rc == 0) refresh_cities(app, 0);
    return rc;
}

/* ---------------------------------------------------------------- */
/* alarm actions                                                     */
/* ---------------------------------------------------------------- */

size_t clock_app_alarm_rows(const clock_app *app)
{
    return app->n_alarm_ids;
}
uint64_t clock_app_alarm_id_at(const clock_app *app, size_t row)
{
    return row < app->n_alarm_ids ? app->alarm_ids[row] : 0;
}

static void load_editor(clock_app *app)
{
    const ck_alarm *a = app->selected_alarm
                            ? ck_alarm_find(app->store, app->selected_alarm)
                            : NULL;
    char hb[8], mb[8];
    snprintf(hb, sizeof(hb), "%02d", a ? a->hour : 7);
    snprintf(mb, sizeof(mb), "%02d", a ? a->min : 0);
    tk_textfield_set_text(app->e_hour, hb);
    tk_textfield_set_text(app->e_min, mb);
    tk_textfield_set_text(app->e_label, a ? a->label : "");
    tk_textfield_set_text(app->e_sound, a ? a->sound : "");
    for (int i = 0; i < 7; i++)
        tk_checkbox_set_checked(app->e_wd[i],
                                a && (a->repeat_mask & (1 << i)));
    static const int snooze_vals[4] = { 0, 5, 10, 15 };
    int sidx = 0;
    if (a) for (int i = 0; i < 4; i++)
        if (a->snooze_min == snooze_vals[i]) sidx = i;
    tk_combobox_select(app->e_snooze, sidx);
    tk_widget_invalidate(app->e_hour);
    tk_widget_invalidate(app->e_min);
    tk_widget_invalidate(app->e_label);
    tk_widget_invalidate(app->e_sound);
    tk_widget_invalidate(app->e_snooze);
    for (int i = 0; i < 7; i++) tk_widget_invalidate(app->e_wd[i]);
}

void clock_app_select_alarm_row(clock_app *app, size_t row)
{
    if (row >= app->n_alarm_ids) return;
    tk_listview_select(app->alarm_list, (int)row);
    app->selected_alarm = app->alarm_ids[row];
    load_editor(app);
}

int clock_app_toggle_alarm_row(clock_app *app, size_t row)
{
    if (row >= app->n_alarm_ids) return -1;
    const ck_alarm *a = ck_alarm_find(app->store, app->alarm_ids[row]);
    if (!a) return -1;
    int rc = ck_alarm_set_enabled(app->store, a->id, !a->enabled);
    refresh_alarms(app);
    return rc;
}

/* lb/sb are CALLER-owned buffers that must outlive the
 * model call: out->label/out->sound point into them, and the
 * model dups them synchronously. They used to be locals of
 * this function - the pointers dangled the moment it returned
 * and only worked by stack-layout luck (ASan
 * stack-use-after-return, foundation audit). */
static int editor_fields(clock_app *app, ck_alarm *out,
                         char *lb, size_t lbn,
                         char *sb, size_t sbn)
{
    char hb[16], mb[16];
    tk_textfield_text(app->e_hour, hb, sizeof(hb));
    tk_textfield_text(app->e_min, mb, sizeof(mb));
    tk_textfield_text(app->e_label, lb, lbn);
    tk_textfield_text(app->e_sound, sb, sbn);
    memset(out, 0, sizeof(*out));
    out->hour = atoi(hb);
    out->min = atoi(mb);
    if (out->hour < 0 || out->hour > 23 || out->min < 0 ||
        out->min > 59)
        return -1;
    out->repeat_mask = 0;
    for (int i = 0; i < 7; i++)
        if (tk_checkbox_checked(app->e_wd[i]))
            out->repeat_mask |= 1 << i;
    static const int snooze_vals[4] = { 0, 5, 10, 15 };
    int sidx = tk_combobox_selected(app->e_snooze);
    out->snooze_min = snooze_vals[sidx >= 0 && sidx < 4 ? sidx : 0];
    out->sound = sb;
    out->label = lb;
    out->enabled = true;
    return 0;
}

uint64_t clock_app_editor_new(clock_app *app)
{
    ck_alarm f;
    char lb[512], sb[256];   /* must outlive the model call */
    if (editor_fields(app, &f, lb, sizeof(lb),
                      sb, sizeof(sb)) != 0) return 0;
    uint64_t id = ck_alarm_add(app->store, &f);
    if (id) {
        app->selected_alarm = id;
        refresh_alarms(app);
    }
    return id;
}

int clock_app_editor_save(clock_app *app)
{
    if (!app->selected_alarm) return -1;
    const ck_alarm *cur = ck_alarm_find(app->store, app->selected_alarm);
    if (!cur) return -1;
    ck_alarm f;
    char lb[512], sb[256];   /* must outlive the model call */
    if (editor_fields(app, &f, lb, sizeof(lb),
                      sb, sizeof(sb)) != 0) return -1;
    f.id = cur->id;
    f.enabled = cur->enabled;
    int rc = ck_alarm_update(app->store, &f);
    refresh_alarms(app);
    return rc;
}

int clock_app_delete_selected_alarm(clock_app *app)
{
    if (!app->selected_alarm) return -1;
    int rc = ck_alarm_delete(app->store, app->selected_alarm);
    app->selected_alarm = 0;
    refresh_alarms(app);
    return rc;
}

/* ---------------------------------------------------------------- */
/* stopwatch / timer actions                                         */
/* ---------------------------------------------------------------- */

void clock_app_sw_toggle(clock_app *app, int64_t mono_ms)
{
    if (app->sw.running) ck_sw_stop(&app->sw, mono_ms);
    else ck_sw_start(&app->sw, mono_ms);
    refresh_sw(app, mono_ms);
}

void clock_app_sw_lap_or_reset(clock_app *app, int64_t mono_ms)
{
    if (app->sw.running) ck_sw_lap(&app->sw, mono_ms);
    else ck_sw_reset(&app->sw);
    refresh_sw(app, mono_ms);
}

int clock_app_sw_laps(const clock_app *app) { return app->sw.n_laps; }
bool clock_app_sw_running(const clock_app *app) { return app->sw.running; }

size_t clock_app_timer_rows(const clock_app *app)
{
    return ck_timer_count(app->store);
}

uint64_t clock_app_editor_new_timer(clock_app *app)
{
    char hb[16], mb[16], sb[16], lb[256];
    tk_textfield_text(app->t_h, hb, sizeof(hb));
    tk_textfield_text(app->t_m, mb, sizeof(mb));
    tk_textfield_text(app->t_s, sb, sizeof(sb));
    tk_textfield_text(app->t_label, lb, sizeof(lb));
    int64_t dur = (int64_t)atoi(hb) * 3600 + (int64_t)atoi(mb) * 60
                  + atoi(sb);
    uint64_t id = ck_timer_add(app->store, lb, dur);
    if (id) refresh_timers(app);
    return id;
}

static const ck_timer *timer_at(clock_app *app, size_t row)
{
    return ck_timer_at(app->store, row);
}
int clock_app_timer_start(clock_app *app, size_t row,
                          int64_t wall, int64_t mono_ms)
{
    const ck_timer *t = timer_at(app, row);
    if (!t) return -1;
    int rc = ck_timer_start(app->store, t->id, wall, mono_ms);
    refresh_timers(app);
    return rc;
}
int clock_app_timer_pause(clock_app *app, size_t row, int64_t mono_ms)
{
    const ck_timer *t = timer_at(app, row);
    if (!t) return -1;
    int rc = ck_timer_pause(app->store, t->id, mono_ms);
    refresh_timers(app);
    return rc;
}
int clock_app_timer_resume(clock_app *app, size_t row,
                           int64_t wall, int64_t mono_ms)
{
    const ck_timer *t = timer_at(app, row);
    if (!t) return -1;
    int rc = ck_timer_resume(app->store, t->id, wall, mono_ms);
    refresh_timers(app);
    return rc;
}
int clock_app_timer_cancel(clock_app *app, size_t row)
{
    const ck_timer *t = timer_at(app, row);
    if (!t) return -1;
    int rc = ck_timer_cancel(app->store, t->id);
    if (app->ringing_timer == t->id) app->ringing_timer = 0;
    refresh_timers(app);
    refresh_banner(app);
    return rc;
}
int clock_app_timer_delete(clock_app *app, size_t row)
{
    const ck_timer *t = timer_at(app, row);
    if (!t) return -1;
    uint64_t id = t->id;
    int rc = ck_timer_delete(app->store, id);
    if (app->ringing_timer == id) app->ringing_timer = 0;
    refresh_timers(app);
    refresh_banner(app);
    return rc;
}

bool clock_app_ringing(const clock_app *app)
{
    return ck_ringing_alarm(app->store) != 0 || app->ringing_timer != 0;
}

int clock_app_snooze(clock_app *app, int64_t mono_ms)
{
    int rc = ck_alarm_snooze(app->store, mono_ms);
    refresh_banner(app);
    return rc;
}

int clock_app_dismiss(clock_app *app)
{
    int rc = 0;
    if (ck_ringing_alarm(app->store)) rc = ck_alarm_dismiss(app->store);
    else if (app->ringing_timer) {
        rc = ck_timer_dismiss(app->store, app->ringing_timer);
        app->ringing_timer = 0;
    } else return -1;
    refresh_timers(app);
    refresh_banner(app);
    return rc;
}

/* ---------------------------------------------------------------- */
/* widget callbacks                                                  */
/* ---------------------------------------------------------------- */

static void on_city_add(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    if (clock_app_add_chosen_city(app) == 0)
        refresh_cities(app, 0);
}
static void on_city_del(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    int sel = tk_listview_selected(app->city_list);
    if (sel >= 0) clock_app_remove_city_at(app, (size_t)sel);
}
static void on_alarm_select(tk_widget *w, void *ud)
{
    clock_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0) clock_app_select_alarm_row(app, (size_t)sel);
}
static void on_alarm_activate(tk_widget *w, void *ud)
{
    clock_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0) clock_app_toggle_alarm_row(app, (size_t)sel);
}
static void on_alarm_new(tk_widget *w, void *ud)
{
    (void)w;
    clock_app_editor_new(ud);
}
static void on_alarm_save(tk_widget *w, void *ud)
{
    (void)w;
    clock_app_editor_save(ud);
}
static void on_alarm_del(tk_widget *w, void *ud)
{
    (void)w;
    clock_app_delete_selected_alarm(ud);
}
static void on_sw_toggle(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    clock_app_sw_toggle(app, app->last_mono);
}
static void on_sw_lap(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    clock_app_sw_lap_or_reset(app, app->last_mono);
}
static void on_timer_new(tk_widget *w, void *ud)
{
    (void)w;
    clock_app_editor_new_timer(ud);
}
static int timer_sel_row(clock_app *app)
{
    return tk_listview_selected(app->timer_list);
}
static void on_timer_start(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    int r = timer_sel_row(app);
    /* button path uses the last tick's clocks (kept in the app) */
    if (r >= 0)
        clock_app_timer_start(app, (size_t)r, app->last_wall,
                              app->last_mono);
}
static void on_timer_pause(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    int r = timer_sel_row(app);
    if (r >= 0) clock_app_timer_pause(app, (size_t)r, app->last_mono);
}
static void on_timer_cancel(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    int r = timer_sel_row(app);
    if (r >= 0) clock_app_timer_cancel(app, (size_t)r);
}
static void on_timer_del(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    int r = timer_sel_row(app);
    if (r >= 0) clock_app_timer_delete(app, (size_t)r);
}
static void on_snooze(tk_widget *w, void *ud)
{
    (void)w;
    clock_app *app = ud;
    clock_app_snooze(app, app->last_mono);
}
static void on_dismiss(tk_widget *w, void *ud)
{
    (void)w;
    clock_app_dismiss(ud);
}

/* ---------------------------------------------------------------- */
/* construction                                                      */
/* ---------------------------------------------------------------- */

clock_app *clock_app_new(ck_store *store, int local_offset_min,
                         int w, int h)
{
    clock_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->store = store;
    app->local_off = local_offset_min;
    app->win_w = w;
    app->win_h = h;
    ck_set_local_offset(store, local_offset_min);
    ck_sw_init(&app->sw);

    /* Mode: the constructor reads ONLY the toolkit default
     * theme's style (set by main() via bd_app_theme(), or
     * directly by tests / shot harnesses) - never the registry. */
    app->winui = tk_theme_default()->style == 2;

    /* Content column (now / banner / pages). In Android mode
     * it is the whole window with a bottom bar under it; in
     * WinUI mode it sits right of the navigation rail. */
    tk_widget *content = tk_box_new(TK_DIR_VERTICAL, 4, 6);
    app->now_label = tk_label_new("");
    tk_add_child(content, app->now_label);

    tk_widget *brow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->banner = tk_label_new("");
    tk_set_flex(app->banner, 1);
    app->snooze_btn = btn("スヌーズ", on_snooze, app);
    tk_add_child(brow, app->banner);
    tk_add_child(brow, app->snooze_btn);
    tk_add_child(brow, btn("止める", on_dismiss, app));
    tk_add_child(content, brow);
    tk_set_visible(app->banner, false);
    tk_set_visible(app->snooze_btn, false);

    app->tabs = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_set_flex(app->tabs, 1);
    tk_add_child(content, app->tabs);

    /* --- world clock page --- */
    tk_widget *world = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    app->city_list = tk_listview_new();
    tk_set_flex(app->city_list, 1);
    tk_add_child(world, app->city_list);
    tk_widget *crow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->city_combo = tk_combobox_new();
    {
        size_t nb = ck_builtin_count();
        const char **names = malloc(nb * sizeof(char *));
        for (size_t i = 0; i < nb; i++) names[i] = ck_builtin_at(i)->name;
        tk_combobox_set_items(app->city_combo, names, nb);
        free(names);
    }
    tk_set_flex(app->city_combo, 1);
    tk_add_child(crow, app->city_combo);
    tk_add_child(crow, icon_text_btn(app, "追加", "add", "add",
                                     on_city_add, app));
    tk_add_child(crow, btn("削除", on_city_del, app));
    tk_add_child(world, crow);
    app->pages[0] = world;

    /* --- alarms page --- */
    tk_widget *al = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    app->alarm_list = tk_listview_new();
    tk_listview_set_on_select(app->alarm_list, on_alarm_select, app);
    tk_listview_set_on_activate(app->alarm_list, on_alarm_activate, app);
    tk_set_flex(app->alarm_list, 1);
    tk_add_child(al, app->alarm_list);
    tk_widget *erow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->e_hour = tk_textfield_new("07");
    app->e_min = tk_textfield_new("00");
    app->e_label = tk_textfield_new("");
    tk_textfield_set_placeholder(app->e_label, "ラベル");
    tk_set_flex(app->e_label, 1);
    tk_add_child(erow, app->e_hour);
    tk_add_child(erow, tk_label_new(":"));
    tk_add_child(erow, app->e_min);
    tk_add_child(erow, app->e_label);
    tk_add_child(al, erow);
    tk_widget *wdrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    static const char *const wd_labels[7] = {
        "月", "火", "水", "木", "金", "土", "日"
    };
    for (int i = 0; i < 7; i++) {
        app->e_wd[i] = tk_checkbox_new(wd_labels[i], false);
        tk_add_child(wdrow, app->e_wd[i]);
    }
    tk_add_child(al, wdrow);
    tk_widget *erow2 = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    const char *snz[] = { "スヌーズなし", "5分", "10分", "15分" };
    app->e_snooze = tk_combobox_new();
    tk_combobox_set_items(app->e_snooze, snz, 4);
    app->e_sound = tk_textfield_new("");
    tk_textfield_set_placeholder(app->e_sound, "サウンド名");
    tk_add_child(erow2, app->e_snooze);
    tk_add_child(erow2, app->e_sound);
    tk_add_child(al, erow2);
    tk_widget *abtns = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_add_child(abtns, icon_text_btn(app, "新規", "add", "add",
                                     on_alarm_new, app));
    tk_add_child(abtns, btn("保存", on_alarm_save, app));
    tk_add_child(abtns, btn("削除", on_alarm_del, app));
    tk_add_child(al, abtns);
    app->pages[1] = al;

    /* --- stopwatch page --- */
    tk_widget *swp = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    app->sw_label = tk_label_new("00:00:00.0");
    tk_add_child(swp, app->sw_label);
    tk_widget *swrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_add_child(swrow, btn("開始/停止", on_sw_toggle, app));
    tk_add_child(swrow, btn("ラップ/リセット", on_sw_lap, app));
    tk_add_child(swp, swrow);
    app->sw_laps_list = tk_listview_new();
    tk_set_flex(app->sw_laps_list, 1);
    tk_add_child(swp, app->sw_laps_list);
    app->pages[2] = swp;

    /* --- timers page --- */
    tk_widget *tp = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    app->timer_list = tk_listview_new();
    tk_set_flex(app->timer_list, 1);
    tk_add_child(tp, app->timer_list);
    tk_widget *trow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->t_h = tk_textfield_new("0");
    app->t_m = tk_textfield_new("5");
    app->t_s = tk_textfield_new("0");
    app->t_label = tk_textfield_new("");
    tk_textfield_set_placeholder(app->t_label, "ラベル");
    tk_set_flex(app->t_label, 1);
    tk_add_child(trow, app->t_h);
    tk_add_child(trow, tk_label_new("時"));
    tk_add_child(trow, app->t_m);
    tk_add_child(trow, tk_label_new("分"));
    tk_add_child(trow, app->t_s);
    tk_add_child(trow, tk_label_new("秒"));
    tk_add_child(trow, app->t_label);
    tk_add_child(trow, icon_text_btn(app, "作成", "add", "add",
                                     on_timer_new, app));
    tk_add_child(tp, trow);
    tk_widget *tbtns = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_add_child(tbtns, btn("開始", on_timer_start, app));
    tk_add_child(tbtns, btn("一時停止", on_timer_pause, app));
    tk_add_child(tbtns, btn("キャンセル", on_timer_cancel, app));
    tk_add_child(tbtns, btn("削除", on_timer_del, app));
    tk_add_child(tp, tbtns);
    app->pages[3] = tp;

    for (int i = 0; i < 4; i++) {
        tk_set_flex(app->pages[i], 1);
        tk_add_child(app->tabs, app->pages[i]);
    }

    /* Navigation. The destination ORDER is the reference
     * apps' order in both modes - alarm / clock / timer /
     * stopwatch - while the public page indices stay
     * 0 world, 1 alarms, 2 stopwatch, 3 timers:
     *   nav position:  0       1      2       3
     *   page:          1       0      3       2
     * Icons (real vector icons + labels in both modes):
     *   Android (Google Clock form, bottom bar):
     *     material alarm / schedule / timer / hourglass_top
     *   WinUI (Windows Clock form, left nav rail):
     *     fluent alarm / clock / timer / timer
     *     (Fluent has no stopwatch icon; timer substitutes
     *      for the stopwatch destination - recorded in the
     *      dual-mode report.) */
    static const int nav_order[4] = { 1, 0, 3, 2 };
    static const char *const nav_labels[4] = {
        "アラーム", "時計", "タイマー", "ストップウォッチ"
    };
    static const char *const nav_mat[4] = {
        "alarm", "schedule", "timer", "hourglass_top"
    };
    static const char *const nav_flu[4] = {
        "alarm", "clock", "timer", "timer"
    };
    for (int pos = 0; pos < 4; pos++) {
        app->nav_page_for_pos[pos] = nav_order[pos];
        app->nav_pos_for_page[nav_order[pos]] = pos;
        app->nav_slots[pos].app = app;
        app->nav_slots[pos].page = nav_order[pos];
    }
    tk_widget *top;
    if (app->winui) {
        app->navbar = tk_navrail_new();
        for (int pos = 0; pos < 4; pos++)
            tk_navrail_add(app->navbar, nav_labels[pos],
                           mode_icon(app, nav_mat[pos], nav_flu[pos],
                                     24),
                           24, 24);
        tk_navrail_set_on_select(app->navbar, on_nav_rail, app);
        top = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_add_child(top, app->navbar);
        tk_set_flex(content, 1);
        tk_add_child(top, content);
    } else {
        app->navbar = tk_bottombar_new();
        tk_bottombar_set_indicator_mode(app->navbar, 1);
        for (int pos = 0; pos < 4; pos++)
            tk_bottombar_add_cell(app->navbar,
                                  mode_icon(app, nav_mat[pos],
                                            nav_flu[pos], 24),
                                  24, 24, nav_labels[pos],
                                  on_nav_cell, &app->nav_slots[pos]);
        top = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_set_flex(content, 1);
        tk_add_child(top, content);
        tk_add_child(top, app->navbar);
    }
    app->top = top;
    clock_show_page(app, 0);
    app->root = tk_root_new(top);
    tk_root_layout(app->root, w, h);
    refresh_alarms(app);
    refresh_timers(app);
    load_editor(app);
    return app;
}

void clock_app_free(clock_app *app)
{
    if (!app) return;
    tk_root_free(app->root);
    tk_widget_free(app->top);
    free(app->alarm_ids);
    free(app);
}

tk_root *clock_app_root(clock_app *app) { return app->root; }
tk_widget *clock_app_city_list(clock_app *app) { return app->city_list; }
tk_widget *clock_app_city_combo(clock_app *app) { return app->city_combo; }
tk_widget *clock_app_alarm_list(clock_app *app) { return app->alarm_list; }
tk_widget *clock_app_sw_label(clock_app *app) { return app->sw_label; }
tk_widget *clock_app_status_label(clock_app *app) { return app->banner; }
tk_widget *clock_app_editor_hour(clock_app *app) { return app->e_hour; }
tk_widget *clock_app_editor_min(clock_app *app) { return app->e_min; }
tk_widget *clock_app_editor_label(clock_app *app) { return app->e_label; }
tk_widget *clock_app_timer_h(clock_app *app) { return app->t_h; }
tk_widget *clock_app_timer_m(clock_app *app) { return app->t_m; }
tk_widget *clock_app_timer_s(clock_app *app) { return app->t_s; }
tk_widget *clock_app_timer_label(clock_app *app) { return app->t_label; }
tk_widget *clock_app_timer_list(clock_app *app) { return app->timer_list; }

/* ---------------------------------------------------------------- */
#ifdef BD_CLOCK_MAIN
static void clock_tick_cb(void *ud, uint64_t mono)
{
    clock_app_tick((clock_app *)ud, (int64_t)time(NULL), (int64_t)mono);
}

int main(void)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    const char *home = getenv("HOME");
    char root[1024];
    const char *env_root = getenv("BRAINUX_CLOCK_DIR");
    if (env_root) snprintf(root, sizeof(root), "%s", env_root);
    else if (home)
        snprintf(root, sizeof(root), "%s/.local/share/brainux/clock", home);
    else snprintf(root, sizeof(root), ".local/share/brainux/clock");
    ck_store *store = ck_open(root);
    if (!store) { fprintf(stderr, "bd-clock: cannot open store\n"); return 1; }

    /* Local offset: brainreg "datetime" section is the single
     * source (the settings app edits it). Default +540 (Tokyo). */
    int local_off = reg_get_int("datetime", "local_offset_min", 540);

    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    /* Display first, real size (see bd_calc.c main note) */
    int vw, vh;
    bd_app_display *disp = bd_app_display_open_ex("時計", 0, 0, 480, 272, 480, 272, &vw, &vh);
    clock_app *app = clock_app_new(store, local_off, vw, vh);

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, clock_tick_cb, app, 200);
    } else {
        clock_app_tick(app, (int64_t)time(NULL), 0);
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-clock: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    clock_app_free(app);
    ck_close(store);
    if (font) tk_font_free(font);
    return 0;
}
#endif /* BD_CLOCK_MAIN */
