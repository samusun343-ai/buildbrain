/* test_matrix - the layout matrix gate
 * (DESIGN-layout-contract.md SS7).
 *
 * Every app is constructed at every cell of
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

static int golden_cell(int w, int h, const char *tname)
{
    return (w == 854 && h == 480) || (w == 850 && h == 368);
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

    char sub[600];
    for (size_t ti = 0; ti < sizeof(THEMES) / sizeof(THEMES[0]);
         ti++) {
        tk_theme_set_default(THEMES[ti].t);
        for (size_t si = 0; si < sizeof(SIZES) / sizeof(SIZES[0]);
             si++) {
            int w = SIZES[si].w, h = SIZES[si].h;
            int gold = golden_cell(w, h, THEMES[ti].name) &&
                       (ti == 0 || ti == 2);

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
                calc_app_free(calc);
            } else { fails++; printf("FAIL calc build %dx%d\n", w, h); }

            tk_theme_set_default(THEMES[ti].t);
            settings_app *set =
                settings_app_new(be, NULL, sys, w, h);
            if (set) {
                run_cell("settings", settings_app_root(set),
                         w, h, THEMES[ti].t, THEMES[ti].name,
                         gold);
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
                files_app_free(files);
            } else { fails++; printf("FAIL files build %dx%d\n", w, h); }
        }
    }

    printf("test_matrix: %d passed, %d failed, %d golden "
           "generated\n", passes, fails, generated);
    return fails ? 1 : 0;
}
