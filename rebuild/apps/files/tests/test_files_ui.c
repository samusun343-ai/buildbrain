/* test_files_ui - bd-files UI drive tests (headless).
 *
 * Builds the real UI over a fixture tree and drives it through
 * the app action functions (the same ones the buttons call) plus
 * real pointer events on the toolbar buttons:
 *   - initial listing matches the fixture
 *   - navigate into a folder, back, forward, up
 *   - open a .txt file -> launch hook receives bd-notes argv with
 *     the file path appended (catalog + mime association)
 *   - new folder / rename / copy-paste / delete -> trash view,
 *     restore, permanent delete
 *   - search narrows the listing; hidden toggle; grid view builds
 *   - a full paint into a memory canvas + fb flush (smoke)
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_files.h"
#include "bdstore.h"
#include "reg.h"

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

static char ROOT[512], DATA[512], START[512];

static void write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror("fixture"); exit(2); }
    fputs(data, f);
    fclose(f);
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* launch hook capture */
static char captured[8][1024];
static int captured_n;
static bool hook(void *ud, char *const argv[])
{
    (void)ud;
    captured_n = 0;
    for (int i = 0; argv[i] && i < 8; i++) {
        snprintf(captured[i], sizeof(captured[i]), "%s", argv[i]);
        captured_n++;
    }
    return true;
}

static void click(tk_root *root, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_UP;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
}

int main(void)
{
    char tmpl[] = "/tmp/bdfiles-ui-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    snprintf(ROOT, sizeof(ROOT), "%s", tmpl);
    snprintf(DATA, sizeof(DATA), "%s/data", ROOT);
    snprintf(START, sizeof(START), "%s/start", ROOT);
    if (bd_mkdir_p(DATA) != 0 || bd_mkdir_p(START) != 0) return 2;

    /* fixture tree */
    char p[1024];
    snprintf(p, sizeof(p), "%s/sub", START);
    if (mkdir(p, 0755) != 0) return 2;
    snprintf(p, sizeof(p), "%s/note.txt", START);
    write_file(p, "hello\n");
    snprintf(p, sizeof(p), "%s/sub/inner.txt", START);
    write_file(p, "inner\n");
    snprintf(p, sizeof(p), "%s/.secret", START);
    write_file(p, "shh\n");

    /* reg + catalog fixture dirs */
    char regd[1024], appd[1024];
    snprintf(regd, sizeof(regd), "%s/reg", ROOT);
    snprintf(appd, sizeof(appd), "%s/applications", ROOT);
    if (bd_mkdir_p(regd) != 0 || bd_mkdir_p(appd) != 0) return 2;
    setenv("BRAINREG_USER_DIR", regd, 1);
    setenv("BRAINREG_SYS_DIR", regd, 1);
    setenv("BD_FILES_APP_DIRS", appd, 1);
    snprintf(p, sizeof(p), "%s/bd-notes.desktop", appd);
    write_file(p, "[Desktop Entry]\nType=Application\nName=Notes\n"
                  "Exec=bd-notes %f\nCategories=Office;\n");

    files_app *app = files_app_new(START, DATA, 480, 800);
    check_true("U build", app != NULL);
    files_app_set_launch_hook(app, hook, NULL);

    check_str("U start dir", files_app_dir(app), START);
    check_int("U rows (no hidden)", (long)files_app_row_count(app), 2);

    /* hidden toggle */
    files_app_set_show_hidden(app, true);
    check_int("U rows (hidden)", (long)files_app_row_count(app), 3);
    files_app_set_show_hidden(app, false);
    check_int("U rows again", (long)files_app_row_count(app), 2);

    /* rows are sorted by name: note.txt (0), sub (1) with dirs first
     * -> sub first. Find rows by opening each candidate is overkill;
     * navigate via the folder row: dirs_first puts sub at row 0. */
    files_app_select_row(app, 0);
    check_int("U selected", files_app_selected(app), 0);
    files_app_open_selected(app); /* sub/ */
    check_true("U navigated", files_app_dir(app) != NULL &&
                              strstr(files_app_dir(app), "sub") != NULL);
    check_int("U inner rows", (long)files_app_row_count(app), 1);
    files_app_back(app);
    check_str("U back", files_app_dir(app), START);

    /* Back key (TK_KEY_BACK from the WM): inside sub/ the key
     * is consumed and walks the history back to START */
    files_app_forward(app);
    check_true("U in sub again", strstr(files_app_dir(app), "sub") != NULL);
    {
        tk_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TK_EV_KEY_DOWN;
        ev.key = TK_KEY_BACK;
        bool consumed = tk_root_event(files_app_root(app), &ev);
        check_true("U back key consumed", consumed);
        check_str("U back key dir", files_app_dir(app), START);
    }
    files_app_forward(app);
    check_true("U forward", strstr(files_app_dir(app), "sub") != NULL);
    files_app_up(app);
    check_str("U up", files_app_dir(app), START);

    /* open note.txt through the catalog: name sort + dirs first ->
     * row 0 = sub, row 1 = note.txt */
    files_app_select_row(app, 1);
    files_app_open_selected(app);
    check_int("U hook argc", captured_n, 2);
    check_str("U hook argv0", captured[0], "bd-notes");
    check_true("U hook path", strstr(captured[1], "note.txt") != NULL);

    /* new folder + rename */
    check_int("U new folder", files_app_new_folder(app, "work"), 0);
    snprintf(p, sizeof(p), "%s/work", START);
    check_true("U folder exists", exists(p));
    files_app_refresh(app);
    /* select the work row (sorted: note.txt, sub, work -> dirs first:
     * sub, work, note.txt) */
    files_app_select_row(app, 1);
    check_int("U rename", files_app_rename_selected(app, "job"), 0);
    snprintf(p, sizeof(p), "%s/job", START);
    check_true("U renamed", exists(p));

    /* copy + paste (sorted rows now: job, sub, note.txt) */
    files_app_select_row(app, 2); /* note.txt */
    files_app_copy_selected(app, false);
    files_app_select_row(app, 1); /* sub */
    files_app_open_selected(app); /* into sub */
    check_int("U paste", files_app_paste(app), 0);
    snprintf(p, sizeof(p), "%s/sub/note.txt", START);
    check_true("U pasted", exists(p));
    files_app_back(app);

    /* delete -> trash -> restore */
    files_app_select_row(app, 2); /* note.txt in START */
    check_int("U delete", files_app_delete_selected(app), 0);
    snprintf(p, sizeof(p), "%s/note.txt", START);
    check_true("U deleted from dir", !exists(p));
    check_true("U trash has bytes", files_app_trash_bytes(app) > 0);
    files_app_open_trash(app);
    check_true("U in trash", files_app_in_trash(app));
    check_int("U trash rows", (long)files_app_row_count(app), 1);
    files_app_select_row(app, 0);
    check_int("U restore", files_app_restore_selected(app), 0);
    check_true("U restored", exists(p));

    /* search (leave the trash view first) */
    files_app_navigate(app, START);
    check_int("U search", files_app_search(app, "inner"), 0);
    check_int("U search rows", (long)files_app_row_count(app), 1);
    files_app_navigate(app, START);
    /* START now holds: sub/, job/ (renamed), note.txt (restored) */
    check_int("U rows after nav", (long)files_app_row_count(app), 3);

    /* grid view builds and selects */
    files_app_set_grid(app, true);
    check_int("U grid rows", (long)files_app_row_count(app), 3);
    files_app_set_grid(app, false);

    /* grouping builds header rows */
    files_app_set_group(app, FM_GROUP_TYPE);
    check_true("U group rows", files_app_row_count(app) > 4);
    files_app_set_group(app, FM_GROUP_NONE);

    /* paint smoke: full paint + damage flush into a memory fb */
    {
        tk_canvas *canvas = tk_canvas_new(480, 800);
        tk_fb *fb = tk_fb_open_mem(480, 800, 16);
        check_true("U paint setup", canvas != NULL && fb != NULL);
        tk_root_paint(files_app_root(app), canvas, tk_theme_default());
        size_t bytes = tk_fb_flush_damage(fb, canvas);
        check_int("U paint bytes", (long)bytes, 480L * 800 * 2);
        tk_fb_close(fb);
        tk_canvas_free(canvas);
    }

    /* a real pointer click on the first toolbar button area must
     * not crash the dispatch path (back button region) */
    {
        tk_widget *top = tk_root_top(files_app_root(app));
        tk_widget *nav = tk_child_at(top, 0);
        tk_widget *back = tk_child_at(nav, 0);
        click(files_app_root(app), back);
        check_true("U click dispatch ok", true);
    }

    /* ---------------- D: damage presentation --------------------
     * Toolkit setters do NOT auto-invalidate (SDK-verified
     * pitfall): a programmatic change made with NO events must
     * still produce damage, transferred bytes, and a visible
     * pixel change. This is the regression test for that. */
    {
        tk_font *font = tk_font_load("../fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load("fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load_default();
        if (font) tk_font_install(font);
        tk_canvas *canvas = tk_canvas_new(480, 800);
        tk_fb *fb = tk_fb_open_mem(480, 800, 16);
        check_true("D setup", canvas != NULL && fb != NULL);
        const tk_theme *theme = tk_theme_default();
        /* start from a known directory (the back-button click
         * above moved us through history), then drain all damage */
        files_app_navigate(app, START);
        tk_root_paint_damage(files_app_root(app), canvas, theme);
        tk_fb_flush_damage(fb, canvas);
        check_int("D drained", (long)tk_fb_flush_damage(fb, canvas), 0);
        size_t bytes_total = 480u * 800u * 4u;
        uint32_t *before = malloc(bytes_total);
        memcpy(before, tk_canvas_pixels(canvas), bytes_total);
        /* programmatic navigation only - no pointer/key events */
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/sub", START);
        files_app_navigate(app, sub);
        int painted = tk_root_paint_damage(files_app_root(app), canvas,
                                           theme);
        size_t shipped = tk_fb_flush_damage(fb, canvas);
        check_true("D painted after setter-only change", painted > 0);
        check_true("D bytes shipped (not 0)", shipped > 0);
        check_true("D pixels changed",
                   memcmp(before, tk_canvas_pixels(canvas),
                          bytes_total) != 0);
        free(before);
        tk_fb_close(fb);
        tk_canvas_free(canvas);
        if (font) tk_font_free(font);
    }

    /* ---------------- M: dimensions + WinUI mode --------------
     * DESIGN-apps-envfit SS3.2: header 56px, header icon
     * targets >= 48px. The same app code must also build and
     * paint under the WinUI theme (mode switch = theme switch,
     * no app code changes). */
    {
        tk_widget *top = tk_root_top(files_app_root(app));
        tk_widget *nav = tk_child_at(top, 0);
        tk_rect nr = tk_widget_rect(nav);
        check_int("M header height 56", nr.h, 56);
        tk_widget *back = tk_child_at(nav, 0);
        tk_rect br = tk_widget_rect(back);
        check_true("M back target >= 48px", br.w >= 48 && br.h >= 48);
    }
    {
        tk_theme_set_default(&tk_theme_winui);
        char data2[1024];
        snprintf(data2, sizeof(data2), "%s/data-winui", ROOT);
        if (bd_mkdir_p(data2) == 0) {
            files_app *app2 = files_app_new(START, data2, 480, 800);
            check_true("M winui build", app2 != NULL);
            if (app2) {
                check_int("M winui rows",
                          (long)files_app_row_count(app2), 3);
                tk_canvas *canvas = tk_canvas_new(480, 800);
                tk_fb *fb = tk_fb_open_mem(480, 800, 16);
                tk_root_paint(files_app_root(app2), canvas,
                              tk_theme_default());
                size_t bytes = tk_fb_flush_damage(fb, canvas);
                check_int("M winui paint bytes", (long)bytes,
                          480L * 800 * 2);
                tk_fb_close(fb);
                tk_canvas_free(canvas);
                files_app_free(app2);
            }
        } else {
            check_true("M winui data dir", false);
        }
        tk_theme_set_default(&tk_theme_v2);
    }

    files_app_free(app);
    printf("test_files_ui: %d PASS, %d FAIL\n", passes, fails);
    return fails ? 1 : 0;
}
