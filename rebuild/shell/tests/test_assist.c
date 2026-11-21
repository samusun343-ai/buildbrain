/* test_assist - shell-level tests for the M2 additions: backend
 * attachment/polling and the single-finger assist overlay driven
 * by synthetic pointer events through the real widget tree. */
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "shellui.h"
#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void setup_reg(void)
{
    static char sysdir[256], userdir[256];
    char tmpl[] = "/tmp/bdassistXXXXXX";
    char *base = mkdtemp(tmpl);
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);
}

static void collect(tk_widget *w, tk_widget_kind kind, tk_widget **out,
                    int *n, int max)
{
    if (tk_widget_kind_of(w) == kind && *n < max) out[(*n)++] = w;
    for (size_t i = 0; i < tk_child_count(w); i++)
        collect(tk_child_at(w, i), kind, out, n, max);
}

static tk_widget *find_button(bd_shell *sh, const char *label)
{
    tk_widget *btns[96];
    int n = 0;
    collect(bd_shell_top(sh), TK_W_BUTTON, btns, &n, 96);
    for (int i = 0; i < n; i++)
        if (tk_button_label(btns[i]) &&
            strcmp(tk_button_label(btns[i]), label) == 0)
            return btns[i];
    return NULL;
}

static void click(bd_shell *sh, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event dn = {0}, up = {0};
    dn.type = TK_EV_POINTER_DOWN; dn.x = r.x + r.w / 2; dn.y = r.y + r.h / 2;
    up.type = TK_EV_POINTER_UP; up.x = dn.x; up.y = dn.y;
    tk_root_event(bd_shell_root(sh), &dn);
    tk_root_event(bd_shell_root(sh), &up);
}

static int page_keys[8];
static int n_page_keys;
static void page_sink(void *ud, int key)
{
    (void)ud;
    if (n_page_keys < 8) page_keys[n_page_keys++] = key;
}

static int lock_calls;
static void lock_cb(void *ud) { (void)ud; lock_calls++; }

int main(void)
{
    setup_reg();
    de_catalog *cat = de_catalog_new(NULL);
    de_catalog_load_dir(cat, "tests/fixtures/applications");
    ln_store *store = ln_store_new();
    bd_shell *sh = bd_shell_new(800, 480, cat, store, NULL, NULL);
    CHECK(sh != NULL);
    bd_shell_layout(sh, 800, 480);

    /* no backend: polling is a no-op, assist still works on state */
    CHECK(bd_shell_poll_backend(sh) == 0);
    CHECK(!bd_shell_assist_open(sh));

    /* attach a mock backend and sync the panel from it */
    bd_backend *be = bd_backend_mock_new();
    bd_backend_mock_set(be, 70, 50, 66, true);
    bd_shell_attach_backend(sh, be);
    int updated = bd_shell_poll_backend(sh);
    CHECK(updated >= 2);               /* battery + brightness */
    CHECK(bd_shell_battery(sh) == 66);
    CHECK(bd_shell_brightness(sh) == 70);

    bd_shell_set_page_sink(sh, page_sink, NULL);
    bd_shell_set_lock_cb(sh, lock_cb, NULL);

    /* floating button: >=56px, opens the panel */
    tk_widget *fab = find_button(sh, "補助");
    CHECK(fab != NULL);
    tk_rect fr = tk_widget_rect(fab);
    CHECK(fr.w >= 56 && fr.h >= 56);
    click(sh, fab);
    CHECK(bd_shell_assist_open(sh));
    bd_shell_layout(sh, 800, 480);

    /* every panel item is a >=56px tall target */
    tk_widget *vol_up = find_button(sh, "音量＋");
    CHECK(vol_up != NULL);
    CHECK(tk_widget_rect(vol_up).h >= 56);

    /* volume up: shell state, reg and backend all move; panel
     * stays open for repeated adjustment */
    int v0 = bd_shell_volume(sh);
    click(sh, vol_up);
    CHECK(bd_shell_volume(sh) == v0 + 10);
    CHECK(bd_backend_volume_get(be) == v0 + 10);
    CHECK(reg_get_int("shell", "volume", -1) == v0 + 10);
    CHECK(bd_shell_assist_open(sh));

    /* brightness up through the backend */
    click(sh, find_button(sh, "輝度＋"));
    CHECK(bd_shell_brightness(sh) == 80);
    CHECK(bd_backend_brightness_get(be) == 80);

    /* page down leaves through the sink */
    click(sh, find_button(sh, "次ページ"));
    CHECK(n_page_keys == 1 && page_keys[0] == TK_KEY_PAGE_DOWN);
    click(sh, find_button(sh, "前ページ"));
    CHECK(n_page_keys == 2 && page_keys[1] == TK_KEY_PAGE_UP);

    /* launcher action: switches view and closes the panel */
    click(sh, find_button(sh, "ランチャー"));
    CHECK(bd_shell_view(sh) == BD_VIEW_LAUNCHER);
    CHECK(!bd_shell_assist_open(sh));
    bd_shell_show_launcher(sh, false);

    /* lock action: callback fires, panel closes. (The launcher
     * footer also has a ロック button since UI-REDESIGN P2; the
     * assist layer is later in tree order, so take the LAST
     * match.) */
    bd_shell_set_assist_open(sh, true);
    bd_shell_layout(sh, 800, 480);
    {
        tk_widget *btns[64];
        int n = 0;
        collect(bd_shell_top(sh), TK_W_BUTTON, btns, &n, 64);
        tk_widget *lock = NULL;
        for (int i = 0; i < n; i++)
            if (tk_button_label(btns[i]) &&
                strcmp(tk_button_label(btns[i]), "ロック") == 0)
                lock = btns[i];
        CHECK(lock != NULL);
        if (lock) click(sh, lock);
    }
    CHECK(lock_calls == 1);
    CHECK(!bd_shell_assist_open(sh));

    /* the desktop underneath is still clickable (the overlay
     * layer must not swallow clicks outside its bounds) */
    bd_shell_layout(sh, 800, 480);
    tk_canvas *cv = tk_canvas_new(800, 480);
    tk_root_paint(bd_shell_root(sh), cv, tk_theme_default());
    tk_canvas_free(cv);

    bd_shell_free(sh);
    bd_backend_free(be);
    ln_store_free(store);
    de_catalog_free(cat);
    printf("test_assist: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
