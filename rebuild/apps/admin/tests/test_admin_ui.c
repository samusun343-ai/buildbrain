/* test_admin_ui - drives the REAL admin UI. Processes run
 * against the host's real /proc so the kill flow can target
 * a real child (sleep) process; storage/memory/logs use the
 * same app with fixture-backed tabs verified by content.
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_admin.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("PASS "); } \
    else { fails++; printf("FAIL "); } \
    printf(__VA_ARGS__); putchar('\n'); } while (0)

static void click(tk_root *root, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
    ev.type = TK_EV_POINTER_UP;
    tk_root_event(root, &ev);
}

int main(void)
{
    char tmpl[] = "/tmp/bdadminui-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    char logs[1100], cmd[2400], p[1200];
    snprintf(logs, sizeof(logs), "%s/logs", tmpl);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", logs);
    if (system(cmd) != 0) return 2;
    snprintf(p, sizeof(p), "%s/daemon.log", logs);
    FILE *f = fopen(p, "w");
    fputs("daemon started\nall good\nFATAL boom\n", f);
    fclose(f);

    /* a real victim process */
    pid_t victim = fork();
    if (victim == 0) { pause(); _exit(0); }

    sy_sys *sys = sy_open("/proc", logs);
    admin_app *app = admin_app_new(sys, 800, 600);
    CHECK(app != NULL, "app created");
    tk_root *root = admin_app_root(app);
    CHECK(admin_app_tab(app) == 0, "processes tab first");
    CHECK(admin_app_proc_rows(app) > 5, "host processes listed: %zu",
          admin_app_proc_rows(app));

    /* find and select the victim's row */
    int found = -1;
    for (size_t i = 0; i < admin_app_proc_rows(app); i++) {
        tk_listview_select(admin_app_proc_list(app), (int)i);
        if (admin_app_selected_pid(app) == (int)victim) {
            found = (int)i;
            break;
        }
    }
    CHECK(found >= 0, "victim row found");
    click(root, admin_app_term_button(app));
    CHECK(strstr(tk_label_text(admin_app_status_label(app)),
                 "終了") != NULL, "status: %s",
          tk_label_text(admin_app_status_label(app)));
    int status = 0;
    waitpid(victim, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM,
          "victim terminated via UI");

    /* RSS sort reorders (descending after two clicks) */
    click(root, admin_app_sort_button(app, SY_SORT_RSS));
    click(root, admin_app_sort_button(app, SY_SORT_RSS));
    CHECK(admin_app_proc_rows(app) > 5, "still listed after sort");

    /* storage tab */
    click(root, admin_app_tab_button(app, 1));
    CHECK(admin_app_tab(app) == 1, "storage tab");
    CHECK(tk_listview_count(admin_app_storage_list(app)) > 0,
          "mounts listed");

    /* memory tab */
    click(root, admin_app_tab_button(app, 2));
    CHECK(strstr(tk_label_text(admin_app_mem_label(app)),
                 "メモリ合計") != NULL, "mem label: %.40s",
          tk_label_text(admin_app_mem_label(app)));

    /* logs tab: load daemon.log, then filter to FATAL only */
    click(root, admin_app_tab_button(app, 3));
    CHECK(tk_listview_count(admin_app_log_list(app)) == 1,
          "one log file listed");
    tk_listview_select(admin_app_log_list(app), 0);
    click(root, admin_app_log_load_button(app));
    char view[4096];
    tk_textview_text(admin_app_log_view(app), view, sizeof(view));
    CHECK(strstr(view, "daemon started") != NULL, "log loaded");
    tk_textfield_set_text(admin_app_log_filter(app), "FATAL");
    click(root, admin_app_log_load_button(app));
    tk_textview_text(admin_app_log_view(app), view, sizeof(view));
    CHECK(strstr(view, "FATAL boom") != NULL &&
          strstr(view, "all good") == NULL, "log filtered");

    admin_app_free(app);
    sy_close(sys);
    printf("test_admin_ui: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
