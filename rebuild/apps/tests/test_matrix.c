/* test_matrix - the layout matrix gate
 * (DESIGN-layout-contract.md SS7).
 *
 * Every windowed app is constructed at every cell of
 *   sizes  {854x480, 850x368, 480x272, 427x240, 320x200,
 *           851x367}
 * x themes {android light, android dark, winui light,
 *           winui dark}
 * and each cell must satisfy:
 *   1. tk_root_validate() == 0 - no negative sizes, no child
 *      escaping its parent outside a scroll/FIXED container
 *      (the "clipped / crushed / misaligned" class);
 *   2. a full paint leaves no sentinel pixel - every pixel
 *      of the window is painted by someone (the "black
 *      corner" class; tk_root_paint clears with the theme
 *      background first, so a survivor means a widget painted
 *      outside the canvas contract);
 *   3. for the representative cells (854x480 and 850x368 in
 *      android-light and winui-light) the pixel hash matches
 *      the golden file in tests/golden/ (LVGL ref-image
 *      style: regenerate by deleting the file and re-running
 *      after a visual review). The files app is exempt from
 *      (3): its status line paints the live free-space figure
 *      of the data directory, so its pixels are volatile by
 *      design; it is held to (1) and (2) at every cell.
 *      Golden coverage stays with calc/settings/notes/files,
 *      the original SS7 set.
 *
 * Cells below an app's declared WM minimum (bd_app_display_
 * open_ex min_w/min_h) are outside the resize contract - the
 * WM clamps window size at the declared minimum - so the six
 * apps added in the proportion pass (tasks, clock, player,
 * recorder, pkg, admin) are constructed at the sizes at or
 * above their minimum only, plus an explicit minimum-size
 * cell. alarmd has no window UI (daemon) and the browser is
 * NetSurf (not a toolkit app); both are outside this gate.
 *
 *   4. PROPORTION CHECKS (proportion pass, 2026-11-22).
 *      Checks 1-3 passed layouts whose elements were crushed
 *      to 11px keys and 0px lists, because nothing measured
 *      RATIOS. The thresholds below are external standards,
 *      not self-generated goldens:
 *      - 24px absolute floor for any visible interactive
 *        element: WCAG 2.2 SC 2.5.8 target-size minimum
 *        (EVAL C1). Checked at primary sizes and at each
 *        app's declared minimum.
 *      - 40px for visible buttons / fields at the primary
 *        sizes (854x480, 850x368): WinUI touch target floor
 *        of 40 epx (Microsoft Learn, touch targeting; EVAL
 *        C1) and the practical floor under the 48dp Material
 *        recommendation (48dp ~= 54px at this panel's
 *        179ppi; the design token control_h is 54). Dense
 *        keypad faces are pinned separately below.
 *      - a visible list surface at a primary size must show
 *        at least 2 rows: 108px = 2 x row_h 54 (EVAL C2/C7
 *        spacing family; a 0-40px list was the tasks /
 *        recorder / pkg defect).
 *      - no single visible button may exceed 60% of the
 *        window width at a primary size (the notes
 *        "new note" pill measured 838/850 = 98%; reference
 *        form is the Material extended FAB: content width
 *        + 20dp padding, M3 spec).
 *      - calculator standard face (primary sizes): key rows
 *        uniform within +/-2px and every key >= 48px high,
 *        grid at least half the window height - the Windows
 *        Calculator XAML lays its standard keypad out as
 *        equal 1* rows/columns with "=" an ordinary cell
 *        (github.com/microsoft/calculator, Calculator.xaml,
 *        retrieved 2026-11-22; EVAL C3/C6). Scientific /
 *        programmer faces: keys >= 40px, rows uniform
 *        within +/-2px (denser faces, WinUI floor).
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static int passes, fails, generated;
static char dir_root[512];

static const struct { int w, h; } SIZES[] = {
    { 854, 480 }, { 850, 368 }, { 480, 272 },
    { 427, 240 }, { 320, 200 }, { 851, 367 },
};
static struct { const tk_theme *t; const char *name; }
THEMES[] = {
    { NULL, "android" },      /* filled in main: tk_theme_v2 */
    { NULL, "android-dark" },
    { NULL, "winui" },
    { NULL, "winui-dark" },
};

/* declared WM minima (bd_app_display_open_ex) per app */
static int app_min(const char *app, int *mw, int *mh)
{
    if (strcmp(app, "calc") == 0)  { *mw = 480; *mh = 360; return 1; }
    if (strcmp(app, "admin") == 0) { *mw = 640; *mh = 360; return 1; }
    *mw = 480; *mh = 272;
    return 1;
}

#define SENTINEL 0xFFFF00FFu

static unsigned long long fnv1a(const uint32_t *px, size_t n)
{
    unsigned long long h = 1469598103934665603ULL;
    const unsigned char *b = (const unsigned char *)px;
    for (size_t i = 0; i < n * 4; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void run_cell(const char *app, tk_root *root, int w, int h,
                     const tk_theme *theme, const char *tname,
                     int golden)
{
    char what[256];
    tk_root_layout(root, w, h);
    int v = tk_root_validate(root);
    snprintf(what, sizeof(what), "%s %dx%d %s validate",
             app, w, h, tname);
    if (v == 0) { passes++; }
    else {
        fails++;
        printf("FAIL %s: %d violation(s)\n", what, v);
        tk_root_validate_dump(root, stdout);
    }

    tk_canvas *cv = tk_canvas_new(w, h);
    uint32_t *px = tk_canvas_pixels(cv);
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) px[i] = SENTINEL;
    /* paint with the CURRENT default, exactly as the shared
     * host does (bdapp_run reads tk_theme_default() at paint
     * time, after the app constructor ran - so a settings
     * compact derivation applies to paint as in production) */
    tk_root_paint(root, cv, tk_theme_default());
    (void)theme;
    size_t left = 0;
    for (size_t i = 0; i < n; i++)
        if (px[i] == SENTINEL) left++;
    snprintf(what, sizeof(what), "%s %dx%d %s sentinel",
             app, w, h, tname);
    if (left == 0) { passes++; }
    else {
        fails++;
        printf("FAIL %s: %zu unpainted pixel(s)\n", what, left);
    }

    if (golden) {
        unsigned long long hash = fnv1a(px, n);
        char path[640];
        const char *gd = getenv("MATRIX_GOLDEN_DIR");
        snprintf(path, sizeof(path), "%s/%s_%dx%d_%s.fnv",
                 gd ? gd : "tests/golden", app, w, h, tname);
        FILE *f = fopen(path, "r");
        if (!f) {
            f = fopen(path, "w");
            if (f) {
                fprintf(f, "%016llx\n", hash);
                fclose(f);
            }
            generated++;
            passes++;
            printf("GOLDEN-GEN %s %dx%d %s %016llx\n",
                   app, w, h, tname, hash);
        } else {
            unsigned long long want = 0;
            if (fscanf(f, "%llx", &want) != 1) want = 0;
            fclose(f);
            snprintf(what, sizeof(what), "%s %dx%d %s golden",
                     app, w, h, tname);
            if (want == hash) { passes++; }
            else {
                fails++;
                printf("FAIL %s: got %016llx want %016llx "
                       "(visual change? review and regenerate)\n",
                       what, hash, want);
            }
        }
    }
    tk_canvas_free(cv);
}

/* ---------------- proportion checks ---------------- */

typedef struct {
    int floor_bad;      /* interactive, visible, 0<h<24 or 0<w<24 */
    int btn40_bad;      /* button/field, visible, h<40 */
    int label_clip_bad; /* visible label narrower than its text */
    int drawer_bad;     /* drawer taller than its rect, unwrapped */
    int max_list_h;     /* largest visible listview height */
    int max_btn_w;      /* widest visible button */
    /* calculator grid keys (buttons with a GRID ancestor),
     * grouped by their grid: the scientific face has two
     * grids with different row heights by design */
    struct { tk_rect r; const void *grid; } keys[128];
    int n_keys;
    int grid_top, grid_bottom;
} rstats;

static bool interactive_kind(tk_widget_kind k)
{
    return k == TK_W_BUTTON || k == TK_W_TEXTFIELD ||
           k == TK_W_COMBOBOX || k == TK_W_CHECKBOX ||
           k == TK_W_SWITCH || k == TK_W_SLIDER ||
           k == TK_W_SPINBUTTON || k == TK_W_TABBAR ||
           k == TK_W_LISTVIEW || k == TK_W_TREEVIEW;
}

static void walk(tk_widget *w, bool vis, const void *grid,
                 bool in_scroll, rstats *s)
{
    vis = vis && tk_is_visible(w);
    tk_widget_kind k = tk_widget_kind_of(w);
    if (k == TK_W_GRID) grid = w;
    if (k == TK_W_SCROLLVIEW) in_scroll = true;
    tk_rect r = tk_widget_rect(w);
    /* an ancestor arranged to zero size (a closed dialog, a
     * collapsed page) leaves its whole subtree unarranged:
     * those zeros are not crush victims */
    if (tk_parent(w) && r.w == 0 && r.h == 0) vis = false;
    /* a visible element arranged to zero size is as crushed
     * as one arranged to 5px (the recorder trim fields) -
     * count it for the hard control kinds */
    if (vis && (r.w == 0 || r.h == 0) &&
        (k == TK_W_BUTTON || k == TK_W_TEXTFIELD ||
         k == TK_W_COMBOBOX || k == TK_W_CHECKBOX ||
         k == TK_W_LISTVIEW || k == TK_W_TABBAR)) {
        if (getenv("MATRIX_DEBUG")) {
            tk_widget *par = tk_parent(w);
            tk_rect pr = par ? tk_widget_rect(par) : r;
            printf("  floor offender (zero): kind=%d at %d,%d "
                   "parent kind=%d rect=%d,%d %dx%d label=%s\n",
                   (int)k, r.x, r.y,
                   par ? (int)tk_widget_kind_of(par) : -1,
                   pr.x, pr.y, pr.w, pr.h,
                   k == TK_W_BUTTON ? tk_button_label(w) : "");
        }
        s->floor_bad++;
    }
    if (vis && r.w > 0 && r.h > 0) {
        if (interactive_kind(k) && (r.h < 24 || r.w < 24)) {
            if (getenv("MATRIX_DEBUG")) {
                printf("  floor offender: kind=%d rect=%d,%d "
                       "%dx%d\n", (int)k, r.x, r.y, r.w, r.h);
                tk_widget *par = tk_parent(w);
                if (par) {
                    size_t nn = tk_child_count(par);
                    for (size_t q = 0; q < nn; q++) {
                        tk_widget *c = tk_child_at(par, q);
                        tk_rect cr = tk_widget_rect(c);
                        tk_size cd = tk_measure(c,
                            (tk_constraints){0, 1 << 28,
                                             0, 1 << 28});
                        printf("    sibling kind=%d rect=%d,%d "
                               "%dx%d desired=%dx%d label=%s\n",
                               (int)tk_widget_kind_of(c),
                               cr.x, cr.y, cr.w, cr.h,
                               cd.w, cd.h,
                               tk_widget_kind_of(c) == TK_W_BUTTON
                                   ? tk_button_label(c) : "");
                    }
                }
            }
            s->floor_bad++;
        }
        if ((k == TK_W_BUTTON || k == TK_W_TEXTFIELD ||
             k == TK_W_COMBOBOX) && r.h < 40)
            s->btn40_bad++;
        /* int10 (independent review B, C10): a visible label
         * arranged narrower than its own text is clipped
         * text (player WinUI empty-state sentence). */
        if (k == TK_W_LABEL) {
            tk_size d = tk_measure(w, (tk_constraints){
                0, 1 << 28, 0, 1 << 28});
            if (d.w > r.w + 2) {
                if (getenv("MATRIX_DEBUG"))
                    printf("  label clip: rect=%d,%d %dx%d "
                           "desired w=%d\n", r.x, r.y, r.w, r.h,
                           d.w);
                s->label_clip_bad++;
            }
        }
        /* int10 (review B, C5/C10): a drawer whose content
         * is taller than its rect must sit in a scrollview,
         * or its last rows are clipped at the window edge
         * (settings' 11 destinations in a 368px window). */
        if (k == TK_W_DRAWER && !in_scroll) {
            tk_size d = tk_measure(w, (tk_constraints){
                0, 1 << 28, 0, 1 << 28});
            if (d.h > r.h + 2) s->drawer_bad++;
        }
        if (k == TK_W_LISTVIEW && r.h > s->max_list_h)
            s->max_list_h = r.h;
        if (k == TK_W_BUTTON && r.w > s->max_btn_w)
            s->max_btn_w = r.w;
        if (k == TK_W_BUTTON && grid &&
            s->n_keys < (int)(sizeof(s->keys) / sizeof(s->keys[0]))) {
            s->keys[s->n_keys].r = r;
            s->keys[s->n_keys].grid = grid;
            s->n_keys++;
            if (s->n_keys == 1 || r.y < s->grid_top)
                s->grid_top = r.y;
            if (r.y + r.h > s->grid_bottom)
                s->grid_bottom = r.y + r.h;
        }
    }
    size_t n = tk_child_count(w);
    for (size_t i = 0; i < n; i++)
        walk(tk_child_at(w, i), vis, grid, in_scroll, s);
}

static void check(const char *what, bool ok, const char *detail)
{
    if (ok) { passes++; }
    else {
        fails++;
        printf("FAIL %s: %s\n", what, detail);
    }
}

/* key-row uniformity over the collected grid keys, per
 * grid: group by y, each row's height must sit within tol
 * of the row median, and every key height must be >=
 * key_min */
static void check_grid(const char *what, const rstats *s,
                       int key_min, int tol)
{
    char buf[160];
    if (s->n_keys == 0) {
        check(what, false, "no grid keys visible");
        return;
    }
    /* check each grid group in turn */
    for (int g0 = 0; g0 < s->n_keys; g0++) {
        const void *g = s->keys[g0].grid;
        bool seen = false;
        for (int q = 0; q < g0; q++)
            if (s->keys[q].grid == g) { seen = true; break; }
        if (seen) continue;
        int rows_y[32], rows_h[32], n_rows = 0;
        int key_h_min = 1 << 30;
        for (int i = 0; i < s->n_keys; i++) {
            if (s->keys[i].grid != g) continue;
            tk_rect r = s->keys[i].r;
            if (r.h < key_h_min) key_h_min = r.h;
            int j;
            for (j = 0; j < n_rows; j++)
                if (rows_y[j] == r.y) {
                    if (r.h != rows_h[j]) {
                        snprintf(buf, sizeof(buf),
                                 "row at y=%d has mixed "
                                 "heights %d/%d",
                                 rows_y[j], rows_h[j], r.h);
                        check(what, false, buf);
                        return;
                    }
                    break;
                }
            if (j == n_rows && n_rows < 32) {
                rows_y[n_rows] = r.y;
                rows_h[n_rows] = r.h;
                n_rows++;
            }
        }
        int sorted[32];
        memcpy(sorted, rows_h, sizeof(int) * (size_t)n_rows);
        for (int i = 0; i < n_rows; i++)
            for (int j = i + 1; j < n_rows; j++)
                if (sorted[j] < sorted[i]) {
                    int t = sorted[i]; sorted[i] = sorted[j];
                    sorted[j] = t;
                }
        int med = sorted[n_rows / 2];
        for (int i = 0; i < n_rows; i++)
            if (rows_h[i] < med - tol || rows_h[i] > med + tol) {
                snprintf(buf, sizeof(buf),
                         "key rows not uniform: row height %d "
                         "vs median %d (tol +/-%d)",
                         rows_h[i], med, tol);
                check(what, false, buf);
                return;
            }
        if (key_h_min < key_min) {
            snprintf(buf, sizeof(buf),
                     "smallest key height %d < %d",
                     key_h_min, key_min);
            check(what, false, buf);
            return;
        }
    }
    check(what, true, "");
}

/* mode: 0 = 24px floor only, 1 = floor + 40px targets,
 * 2 = the full primary-size set (targets, list, width, calc) */
static void run_ratio(const char *app, tk_root *root, int w, int h,
                      const char *tname, int mode, bool is_calc)
{
    bool primary = mode == 2;
    char what[256], buf[160];
    tk_root_layout(root, w, h);
    rstats s;
    memset(&s, 0, sizeof(s));
    walk(tk_root_top(root), true, NULL, false, &s);

    snprintf(what, sizeof(what), "%s %dx%d %s floor24",
             app, w, h, tname);
    snprintf(buf, sizeof(buf),
             "%d interactive element(s) under the 24px WCAG "
             "floor", s.floor_bad);
    check(what, s.floor_bad == 0, buf);

    snprintf(what, sizeof(what), "%s %dx%d %s labelclip",
             app, w, h, tname);
    snprintf(buf, sizeof(buf),
             "%d visible label(s) narrower than their text "
             "(C10)", s.label_clip_bad);
    check(what, s.label_clip_bad == 0, buf);

    snprintf(what, sizeof(what), "%s %dx%d %s drawerwrap",
             app, w, h, tname);
    snprintf(buf, sizeof(buf),
             "%d drawer(s) taller than their rect without a "
             "scrollview (C5/C10)", s.drawer_bad);
    check(what, s.drawer_bad == 0, buf);

    if (mode >= 1) {
        snprintf(what, sizeof(what), "%s %dx%d %s targets40",
                 app, w, h, tname);
        snprintf(buf, sizeof(buf),
                 "%d button/field(s) under 40px high",
                 s.btn40_bad);
        check(what, s.btn40_bad == 0, buf);
    }

    if (primary) {
        snprintf(what, sizeof(what), "%s %dx%d %s list2rows",
                 app, w, h, tname);
        snprintf(buf, sizeof(buf),
                 "largest visible list is %dpx high (< 108 = "
                 "2 rows)", s.max_list_h);
        check(what, s.max_list_h == 0 || s.max_list_h >= 108,
              buf);

        snprintf(what, sizeof(what), "%s %dx%d %s btnwidth",
                 app, w, h, tname);
        snprintf(buf, sizeof(buf),
                 "widest button %dpx > 60%% of window width %d",
                 s.max_btn_w, w);
        check(what, s.max_btn_w <= (w * 60) / 100, buf);
    }

    if (is_calc && primary) {
        snprintf(what, sizeof(what), "calc %dx%d %s stdkeys",
                 w, h, tname);
        check_grid(what, &s, 48, 2);
        snprintf(what, sizeof(what), "calc %dx%d %s gridshare",
                 w, h, tname);
        snprintf(buf, sizeof(buf),
                 "keypad grid %dpx < half the window height %d",
                 s.grid_bottom - s.grid_top, h);
        check(what, s.grid_bottom - s.grid_top >= h / 2, buf);
    }
}

/* calc scientific / programmer faces at primary sizes */
static void run_ratio_calc_faces(calc_app *calc, int w, int h,
                                 const char *tname)
{
    char what[256];
    tk_root *root = calc_app_root(calc);
    for (int tab = 1; tab <= 2; tab++) {
        calc_app_set_tab(calc, tab);
        tk_root_layout(root, w, h);
        rstats s;
        memset(&s, 0, sizeof(s));
        walk(tk_root_top(root), true, NULL, false, &s);
        snprintf(what, sizeof(what), "calc %dx%d %s face%dkeys",
                 w, h, tname, tab);
        check_grid(what, &s, 40, 2);
        snprintf(what, sizeof(what), "calc %dx%d %s face%dfloor",
                 w, h, tname, tab);
        check(what, s.floor_bad == 0,
              "interactive element under 24px on a calc face");
    }
    calc_app_set_tab(calc, 0);
}

static int golden_cell(int w, int h, const char *tname)
{
    (void)tname;
    return (w == 854 && h == 480) || (w == 850 && h == 368);
}

static bool primary_cell(int w, int h)
{
    return (w == 854 && h == 480) || (w == 850 && h == 368);
}

static bool size_ok_for(const char *app, int w, int h)
{
    int mw, mh;
    app_min(app, &mw, &mh);
    (void)0;
    if (w == 854 && h == 480) return true;
    if (w == 850 && h == 368) return true;
    if (w == 851 && h == 367) return true;
    if (w >= mw && h >= mh && w <= 854 && h <= 480 &&
        (w == 480 || w == 640)) return true;
    return false;
}

/* player / recorder fixture factories (null devices) */
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

static void copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out = fopen(dst, "wb");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
}

int main(void)
{
    THEMES[0].t = &tk_theme_v2;
    THEMES[1].t = &tk_theme_v2_dark;
    THEMES[2].t = &tk_theme_winui;
    THEMES[3].t = &tk_theme_winui_dark;

    char tmpl[] = "/tmp/bdmatrix-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    snprintf(dir_root, sizeof(dir_root), "%s", tmpl);

    /* settings fixtures: brainreg dirs + proc fixtures */
    char sysd[600], userd[600], proc[600], logs[600], p[700];
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

    /* player fixtures: two tagged tracks in a media dir */
    char pdata[600], pmedia[600];
    snprintf(pdata, sizeof(pdata), "%s/pdata", tmpl);
    snprintf(pmedia, sizeof(pmedia), "%s/pmedia", tmpl);
    mkdir(pdata, 0755); mkdir(pmedia, 0755);
    {
        char dst[700];
        snprintf(dst, sizeof(dst), "%s/a_song.mp3", pmedia);
        copy_file("player/tests/fixtures/fixture.mp3", dst);
        snprintf(dst, sizeof(dst), "%s/b_song.ogg", pmedia);
        copy_file("player/tests/fixtures/fixture.ogg", dst);
    }

    char sub[600];
    for (size_t ti = 0; ti < sizeof(THEMES) / sizeof(THEMES[0]);
         ti++) {
        tk_theme_set_default(THEMES[ti].t);
        for (size_t si = 0; si < sizeof(SIZES) / sizeof(SIZES[0]);
             si++) {
            int w = SIZES[si].w, h = SIZES[si].h;
            int gold = golden_cell(w, h, THEMES[ti].name) &&
                       (ti == 0 || ti == 2);
            bool prim = primary_cell(w, h);

            /* settings_app_new may install its compact theme
             * as the default (short windows); every app must
             * be built under THIS cell's theme, so reset the
             * default before each construction */
            tk_theme_set_default(THEMES[ti].t);
            snprintf(sub, sizeof(sub), "%s/calc", tmpl);
            mkdir(sub, 0755);
            calc_app *calc = calc_app_new(sub, w, h);
            if (calc) {
                run_cell("calc", calc_app_root(calc), w, h,
                         THEMES[ti].t, THEMES[ti].name, gold);
                if (prim) {
                    run_ratio("calc", calc_app_root(calc), w, h,
                              THEMES[ti].name, 2, true);
                    run_ratio_calc_faces(calc, w, h,
                                         THEMES[ti].name);
                }
                calc_app_free(calc);
            } else { fails++; printf("FAIL calc build %dx%d\n", w, h); }

            tk_theme_set_default(THEMES[ti].t);
            settings_app *set =
                settings_app_new(be, NULL, sys, w, h);
            if (set) {
                run_cell("settings", settings_app_root(set),
                         w, h, THEMES[ti].t, THEMES[ti].name,
                         gold);
                if (prim)
                    run_ratio("settings", settings_app_root(set),
                              w, h, THEMES[ti].name, 2, false);
                settings_app_free(set);
            } else { fails++; printf("FAIL settings build %dx%d\n", w, h); }

            tk_theme_set_default(THEMES[ti].t);
            snprintf(sub, sizeof(sub), "%s/notes", tmpl);
            mkdir(sub, 0755);
            nm_store *ns = nm_open(sub, 1 << 20);
            notes_app *notes = ns ? notes_app_new(ns, w, h, false)
                                  : NULL;
            if (notes) {
                run_cell("notes", notes_app_root(notes), w, h,
                         THEMES[ti].t, THEMES[ti].name, gold);
                if (prim)
                    run_ratio("notes", notes_app_root(notes), w, h,
                              THEMES[ti].name, 2, false);
                notes_app_free(notes);
            } else { fails++; printf("FAIL notes build %dx%d\n", w, h); }
            if (ns) nm_close(ns);

            tk_theme_set_default(THEMES[ti].t);
            char fstart[600], fdata[600];
            snprintf(fstart, sizeof(fstart), "%s/fstart", tmpl);
            snprintf(fdata, sizeof(fdata), "%s/fdata", tmpl);
            mkdir(fstart, 0755); mkdir(fdata, 0755);
            files_app *files = files_app_new(fstart, fdata, w, h);
            if (files) {
                run_cell("files", files_app_root(files), w, h,
                         THEMES[ti].t, THEMES[ti].name, 0);
                if (prim)
                    run_ratio("files", files_app_root(files), w, h,
                              THEMES[ti].name, 2, false);
                files_app_free(files);
            } else { fails++; printf("FAIL files build %dx%d\n", w, h); }

            /* ---- proportion-pass apps: cells at or above
             * the declared minimum only ---- */
            if (size_ok_for("tasks", w, h)) {
                tk_theme_set_default(THEMES[ti].t);
                snprintf(sub, sizeof(sub), "%s/tasks", tmpl);
                mkdir(sub, 0755);
                tm_store *ts = tm_open(sub);
                tasks_app *tasks = ts ? tasks_app_new(ts,
                                        "2026-11-22", w, h) : NULL;
                if (tasks) {
                    run_cell("tasks", tasks_app_root(tasks), w, h,
                             THEMES[ti].t, THEMES[ti].name, 0);
                    run_ratio("tasks", tasks_app_root(tasks), w, h,
                              THEMES[ti].name, prim ? 2 : 0, false);
                    tasks_app_free(tasks);
                } else { fails++; printf("FAIL tasks build %dx%d\n", w, h); }
                if (ts) tm_close(ts);
            }

            if (size_ok_for("clock", w, h)) {
                tk_theme_set_default(THEMES[ti].t);
                snprintf(sub, sizeof(sub), "%s/clock", tmpl);
                mkdir(sub, 0755);
                ck_store *cs = ck_open(sub);
                clock_app *clock = cs ? clock_app_new(cs, 540,
                                        w, h) : NULL;
                if (clock) {
                    run_cell("clock", clock_app_root(clock), w, h,
                             THEMES[ti].t, THEMES[ti].name, 0);
                    run_ratio("clock", clock_app_root(clock), w, h,
                              THEMES[ti].name, prim ? 2 : 0, false);
                    clock_app_free(clock);
                } else { fails++; printf("FAIL clock build %dx%d\n", w, h); }
                if (cs) ck_close(cs);
            }

            if (size_ok_for("player", w, h) &&
                !(ti >= 2 && w < 700) &&
                !(ti < 2 && h < 336)) {
                tk_theme_set_default(THEMES[ti].t);
                const char *dirs[] = { pmedia };
                player_app *player = player_app_new(pdata, dirs,
                                        1, mk_out, NULL, w, h);
                if (player) {
                    run_cell("player", player_app_root(player),
                             w, h, THEMES[ti].t, THEMES[ti].name,
                             0);
                    run_ratio("player", player_app_root(player),
                              w, h, THEMES[ti].name, prim ? 2 : 0,
                              false);
                    /* select a track so the now-playing /
                     * transport surfaces are laid out too, and
                     * hold those to the floor + target checks */
                    tk_listview_select(
                        player_app_list_widget(player), 0);
                    run_ratio("player", player_app_root(player),
                              w, h, THEMES[ti].name, 1, false);
                    player_app_free(player);
                } else { fails++; printf("FAIL player build %dx%d\n", w, h); }
            }

            if (size_ok_for("recorder", w, h)) {
                tk_theme_set_default(THEMES[ti].t);
                /* its own dir: the shared temp root accumulates
                 * the other apps' fixtures, which the recorder
                 * would read as takes */
                snprintf(sub, sizeof(sub), "%s/rec", tmpl);
                mkdir(sub, 0755);
                rec_app *rec = rec_app_new(sub, mk_src, mk_sink,
                                           NULL, w, h);
                if (rec) {
                    run_cell("recorder", rec_app_root(rec), w, h,
                             THEMES[ti].t, THEMES[ti].name, 0);
                    run_ratio("recorder", rec_app_root(rec), w, h,
                              THEMES[ti].name, prim ? 2 : 0, false);
                    rec_app_free(rec);
                } else { fails++; printf("FAIL recorder build %dx%d\n", w, h); }
            }

            if (size_ok_for("pkg", w, h) &&
                !(ti >= 2 && w < 640)) {
                tk_theme_set_default(THEMES[ti].t);
                char debs[600], proot[600], padmin[600];
                snprintf(debs, sizeof(debs), "%s/debs", tmpl);
                snprintf(proot, sizeof(proot), "%s/proot", tmpl);
                snprintf(padmin, sizeof(padmin), "%s/padmin", tmpl);
                mkdir(debs, 0755); mkdir(proot, 0755);
                mkdir(padmin, 0755);
                pkg_app *pkg = pkg_app_new(debs, proot, padmin,
                                           NULL, NULL, w, h);
                if (pkg) {
                    run_cell("pkg", pkg_app_root(pkg), w, h,
                             THEMES[ti].t, THEMES[ti].name, 0);
                    run_ratio("pkg", pkg_app_root(pkg), w, h,
                              THEMES[ti].name, prim ? 2 : 0, false);
                    pkg_app_free(pkg);
                } else { fails++; printf("FAIL pkg build %dx%d\n", w, h); }
            }

            if (size_ok_for("admin", w, h)) {
                tk_theme_set_default(THEMES[ti].t);
                admin_app *adm = admin_app_new(sys, w, h);
                if (adm) {
                    run_cell("admin", admin_app_root(adm), w, h,
                             THEMES[ti].t, THEMES[ti].name, 0);
                    run_ratio("admin", admin_app_root(adm), w, h,
                              THEMES[ti].name, prim ? 2 : 0, false);
                    admin_app_free(adm);
                } else { fails++; printf("FAIL admin build %dx%d\n", w, h); }
            }
        }
    }

    /* ---- declared-minimum cells for calc (480x360) and
     * admin (640x360), which are not in SIZES: validate +
     * sentinel + the 24px floor ---- */
    {
        static const struct { const char *app; int w, h; }
        MINC[] = {
            { "calc", 480, 360 }, { "admin", 640, 360 },
        };
        for (size_t mi = 0; mi < sizeof(MINC) / sizeof(MINC[0]);
             mi++) {
            for (size_t ti = 0; ti < 4; ti += 2) {
                tk_theme_set_default(THEMES[ti].t);
                if (strcmp(MINC[mi].app, "calc") == 0) {
                    snprintf(sub, sizeof(sub), "%s/calc", tmpl);
                    mkdir(sub, 0755);
                    calc_app *calc = calc_app_new(sub, MINC[mi].w,
                                                  MINC[mi].h);
                    if (calc) {
                        run_cell("calc", calc_app_root(calc),
                                 MINC[mi].w, MINC[mi].h,
                                 THEMES[ti].t, THEMES[ti].name, 0);
                        run_ratio("calc", calc_app_root(calc),
                                  MINC[mi].w, MINC[mi].h,
                                  THEMES[ti].name, 0, false);
                        calc_app_free(calc);
                    } else { fails++; printf("FAIL calc build min\n"); }
                } else {
                    admin_app *adm = admin_app_new(sys, MINC[mi].w,
                                                   MINC[mi].h);
                    if (adm) {
                        run_cell("admin", admin_app_root(adm),
                                 MINC[mi].w, MINC[mi].h,
                                 THEMES[ti].t, THEMES[ti].name, 0);
                        run_ratio("admin", admin_app_root(adm),
                                  MINC[mi].w, MINC[mi].h,
                                  THEMES[ti].name, 0, false);
                        admin_app_free(adm);
                    } else { fails++; printf("FAIL admin build min\n"); }
                }
            }
        }
    }

    /* pkg WinUI minimum cell: 640x360 in the winui themes
     * (the Android form's 480x272 minimum is a SIZES cell) */
    for (size_t ti = 2; ti < 4; ti++) {
        tk_theme_set_default(THEMES[ti].t);
        char debs[600], proot[600], padmin[600];
        snprintf(debs, sizeof(debs), "%s/debs", tmpl);
        snprintf(proot, sizeof(proot), "%s/proot", tmpl);
        snprintf(padmin, sizeof(padmin), "%s/padmin", tmpl);
        mkdir(debs, 0755); mkdir(proot, 0755);
        mkdir(padmin, 0755);
        pkg_app *pkg = pkg_app_new(debs, proot, padmin, NULL,
                                   NULL, 640, 360);
        if (pkg) {
            run_cell("pkg", pkg_app_root(pkg), 640, 360,
                     THEMES[ti].t, THEMES[ti].name, 0);
            run_ratio("pkg", pkg_app_root(pkg), 640, 360,
                      THEMES[ti].name, 0, false);
            pkg_app_free(pkg);
        } else { fails++; printf("FAIL pkg build min\n"); }
    }

    /* player Android minimum cell: 480x336 in the android
     * themes */
    for (size_t ti = 0; ti < 2; ti++) {
        tk_theme_set_default(THEMES[ti].t);
        const char *dirs[] = { pmedia };
        player_app *player = player_app_new(pdata, dirs, 1,
                                            mk_out, NULL,
                                            480, 336);
        if (player) {
            run_cell("player", player_app_root(player), 480,
                     336, THEMES[ti].t, THEMES[ti].name, 0);
            run_ratio("player", player_app_root(player), 480,
                      336, THEMES[ti].name, 0, false);
            tk_listview_select(player_app_list_widget(player),
                               0);
            run_ratio("player", player_app_root(player), 480,
                      336, THEMES[ti].name, 1, false);
            player_app_free(player);
        } else { fails++; printf("FAIL player build min\n"); }
    }

    /* player WinUI minimum cell: 700x360 in the winui
     * themes */
    for (size_t ti = 2; ti < 4; ti++) {
        tk_theme_set_default(THEMES[ti].t);
        const char *dirs[] = { pmedia };
        player_app *player = player_app_new(pdata, dirs, 1,
                                            mk_out, NULL,
                                            700, 360);
        if (player) {
            run_cell("player", player_app_root(player), 700,
                     360, THEMES[ti].t, THEMES[ti].name, 0);
            run_ratio("player", player_app_root(player), 700,
                      360, THEMES[ti].name, 0, false);
            tk_listview_select(player_app_list_widget(player),
                               0);
            run_ratio("player", player_app_root(player), 700,
                      360, THEMES[ti].name, 1, false);
            player_app_free(player);
        } else { fails++; printf("FAIL player build min\n"); }
    }

    printf("test_matrix: %d passed, %d failed, %d golden "
           "generated\n", passes, fails, generated);
    return fails ? 1 : 0;
}
