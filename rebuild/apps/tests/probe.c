/* probe - dump arranged widget geometry for one app/size/theme */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tk.h"
#include "bd_calc.h"
#include "bd_settings.h"
#include "bd_notes.h"
#include "nmodel.h"
#include "bd_files.h"
#include "backend.h"
#include "sysmodel.h"
#include "bd_tasks.h"
#include "tmodel.h"
#include "bd_clock.h"
#include "clockmodel.h"
#include "bd_player.h"
#include "pengine.h"
#include "bd_recorder.h"
#include "recmodel.h"
#include "bd_pkg.h"
#include "bd_admin.h"

static pe_out *mk_out(void *ud, uint32_t rate, uint16_t ch)
{
    (void)ud;
    return pe_out_null(rate, ch);
}
static rc_source *mk_src(void *ud)
{
    (void)ud;
    return rc_source_synth(440, RC_RATE, RC_CHANNELS);
}
static rc_sink *mk_sink(void *ud)
{
    (void)ud;
    return rc_sink_null(RC_RATE, RC_CHANNELS);
}

static const char *kind_name(tk_widget_kind k)
{
    static const char *names[] = {
        "ROOT","BOX","FIXED","LABEL","BUTTON","CHECKBOX","RADIO",
        "SLIDER","TEXTFIELD","SCROLLVIEW","LISTVIEW","SWITCH",
        "PROGRESS","TEXTVIEW","TABS","MENU","MENUBAR","DIALOG",
        "COMBOBOX","SPINBUTTON","SEPARATOR","IMAGEVIEW","TREEVIEW",
        "GRID","OVERFLOWBAR","OSK","SINK","DATEPICKER","TIMEPICKER",
        "CHIP","BADGE","TABBAR","SPINNER","CARD","SEARCHBAR",
        "SEGMENTED","CAROUSEL","NAVRAIL","RING","APPBAR","DRAWER",
        "SHEET"
    };
    if (k < 0 || k >= (tk_widget_kind)(sizeof(names)/sizeof(names[0])))
        return "?";
    return names[k];
}

static void dump(tk_widget *w, int depth)
{
    tk_rect r = tk_widget_rect(w);
    tk_size d = tk_measure(w, (tk_constraints){0, 1 << 28, 0, 1 << 28});
    printf("%*s%-10s rect=%4d,%4d %4dx%-4d desired=%4dx%-4d vis=%d",
           depth * 2, "", kind_name(tk_widget_kind_of(w)),
           r.x, r.y, r.w, r.h, d.w, d.h, tk_is_visible(w) ? 1 : 0);
    if (tk_widget_kind_of(w) == TK_W_LABEL)
        printf(" text=%s", tk_label_text(w));
    if (tk_widget_kind_of(w) == TK_W_BUTTON)
        printf(" label=%s", tk_button_label(w));
    printf("\n");
    size_t n = tk_child_count(w);
    for (size_t i = 0; i < n; i++) dump(tk_child_at(w, i), depth + 1);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: probe APP W H THEME(0=and,1=and-dark,2=winui,3=winui-dark)\n");
        return 2;
    }
    const char *app = argv[1];
    int w = atoi(argv[2]), h = atoi(argv[3]), ti = atoi(argv[4]);
    const tk_theme *themes[4] = { &tk_theme_v2, &tk_theme_v2_dark,
                                  &tk_theme_winui, &tk_theme_winui_dark };
    tk_theme_set_default(themes[ti]);

    char tmplbuf[] = "/tmp/bdprobe-XXXXXX";
    char *tmpl = getenv("PROBE_DIR");
    if (!tmpl) {
        if (!mkdtemp(tmplbuf)) { perror("mkdtemp"); return 2; }
        tmpl = tmplbuf;
    }
    char sub[600], p[700];
    tk_root *root = NULL;

    if (strcmp(app, "calc") == 0) {
        snprintf(sub, sizeof(sub), "%s/calc", tmpl);
        mkdir(sub, 0755);
        calc_app *a = calc_app_new(sub, w, h);
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        if (argc > 5) calc_app_set_tab(a, atoi(argv[5]));
        root = calc_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "settings") == 0) {
        char sysd[600], userd[600], proc[600], logs[600];
        snprintf(sysd, sizeof(sysd), "%s/regsys", tmpl);
        snprintf(userd, sizeof(userd), "%s/reguser", tmpl);
        snprintf(proc, sizeof(proc), "%s/proc", tmpl);
        snprintf(logs, sizeof(logs), "%s/logs", tmpl);
        mkdir(sysd, 0755); mkdir(userd, 0755);
        mkdir(proc, 0755); mkdir(logs, 0755);
        setenv("BRAINREG_SYS_DIR", sysd, 1);
        setenv("BRAINREG_USER_DIR", userd, 1);
        snprintf(p, sizeof(p), "%s/meminfo", proc);
        FILE *f = fopen(p, "w");
        fputs("MemTotal: 131072 kB\nMemAvailable: 65536 kB\n", f);
        fclose(f);
        snprintf(p, sizeof(p), "%s/mounts", proc);
        f = fopen(p, "w");
        fputs("/dev/root / ext4 rw 0 0\n", f);
        fclose(f);
        bd_backend *be = bd_backend_mock_new();
        bd_backend_mock_set(be, 40, 30, 55, false);
        sy_sys *sys = sy_open(proc, logs);
        settings_app *a = settings_app_new(be, NULL, sys, w, h);
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = settings_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "notes") == 0) {
        snprintf(sub, sizeof(sub), "%s/notes", tmpl);
        mkdir(sub, 0755);
        nm_store *ns = nm_open(sub, 1 << 20);
        notes_app *a = ns ? notes_app_new(ns, w, h, false) : NULL;
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = notes_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "files") == 0) {
        char fstart[600], fdata[600];
        snprintf(fstart, sizeof(fstart), "%s/fstart", tmpl);
        snprintf(fdata, sizeof(fdata), "%s/fdata", tmpl);
        mkdir(fstart, 0755); mkdir(fdata, 0755);
        files_app *a = files_app_new(fstart, fdata, w, h);
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = files_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "tasks") == 0) {
        snprintf(sub, sizeof(sub), "%s/tasks", tmpl);
        mkdir(sub, 0755);
        tm_store *s = tm_open(sub);
        tasks_app *a = s ? tasks_app_new(s, "2026-11-22", w, h) : NULL;
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = tasks_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "clock") == 0) {
        snprintf(sub, sizeof(sub), "%s/clock", tmpl);
        mkdir(sub, 0755);
        ck_store *s = ck_open(sub);
        clock_app *a = s ? clock_app_new(s, 540, w, h) : NULL;
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = clock_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "player") == 0) {
        char data[600], media[600];
        snprintf(data, sizeof(data), "%s/pdata", tmpl);
        snprintf(media, sizeof(media), "%s/pmedia", tmpl);
        mkdir(data, 0755); mkdir(media, 0755);
        const char *dirs[] = { media };
        player_app *a = player_app_new(data, dirs, 1, mk_out, NULL, w, h);
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = player_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "recorder") == 0) {
        rec_app *a = rec_app_new(tmpl, mk_src, mk_sink, NULL, w, h);
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = rec_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "pkg") == 0) {
        char debs[600], proot[600], padmin[600];
        snprintf(debs, sizeof(debs), "%s/debs", tmpl);
        snprintf(proot, sizeof(proot), "%s/proot", tmpl);
        snprintf(padmin, sizeof(padmin), "%s/padmin", tmpl);
        mkdir(debs, 0755); mkdir(proot, 0755); mkdir(padmin, 0755);
        pkg_app *a = pkg_app_new(debs, proot, padmin, NULL, NULL, w, h);
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = pkg_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    if (strcmp(app, "admin") == 0) {
        char proc[600], logs[600];
        snprintf(proc, sizeof(proc), "%s/proc", tmpl);
        snprintf(logs, sizeof(logs), "%s/logs", tmpl);
        mkdir(proc, 0755); mkdir(logs, 0755);
        snprintf(p, sizeof(p), "%s/meminfo", proc);
        FILE *f = fopen(p, "w");
        fputs("MemTotal: 131072 kB\nMemAvailable: 65536 kB\n", f);
        fclose(f);
        snprintf(p, sizeof(p), "%s/mounts", proc);
        f = fopen(p, "w");
        fputs("/dev/root / ext4 rw 0 0\n", f);
        fclose(f);
        sy_sys *sys = sy_open(proc, logs);
        admin_app *a = admin_app_new(sys, w, h);
        if (!a) { printf("BUILD FAIL\n"); return 1; }
        root = admin_app_root(a);
        tk_root_layout(root, w, h);
        dump(tk_root_top(root), 0);
        return 0;
    }
    fprintf(stderr, "unknown app %s\n", app);
    return 2;
}
