/* test_notify - notification IPC (src/notify.c) end-to-end:
 * client -> Unix socket -> server -> shell notification model.
 *
 * The production client (bd_notify_send) blocks waiting for the
 * server's ack, so it is exercised from a forked child while the
 * parent pumps the server - the same shape as test_session's
 * child WM client. Everything else uses raw sockets so message
 * ordering, partial sends and malformed input are controllable.
 */
#include "notify.h"
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void setup_reg(void)
{
    static char sysdir[256], userdir[256];
    char tmpl[] = "/tmp/bdntXXXXXX";
    char *base = mkdtemp(tmpl);
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);
}

static bool mock_exec(void *ud, const de_entry *e, char *const argv[],
                      bool terminal)
{
    (void)ud; (void)e; (void)argv; (void)terminal;
    return true;
}

/* the shell borrows the catalog and the store (bd_shell_free
 * does not free them); keep them reachable so the test can
 * release them after the shell */
static de_catalog *g_cat;
static ln_store *g_store;

static bd_shell *make_shell(void)
{
    g_cat = de_catalog_new(NULL);
    de_catalog_load_dir(g_cat, "tests/fixtures/applications");
    g_store = ln_store_new();
    bd_shell *sh = bd_shell_new(800, 480, g_cat, g_store,
                                mock_exec, NULL);
    if (sh) bd_shell_layout(sh, 800, 480);
    return sh;
}

static void sock_path(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "/tmp/bdnt-%s-%ld.sock", name, (long)getpid());
}

/* ---- raw client helpers ---- */

static int raw_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un un = { 0 };
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&un, sizeof(un)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void raw_write(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return;
        p += (size_t)n;
        len -= (size_t)n;
    }
}

/* Send one POST over a fresh raw connection and read the ack id
 * after the caller pumped the server. The fd stays open in
 * *fd_out so the caller controls when the ack is read. */
static void raw_post(int fd, const char *app, const char *title,
                     const char *body, int urgency)
{
    bd_nt_post p;
    memset(&p, 0, sizeof(p));
    p.urgency = urgency;
    snprintf(p.app, sizeof(p.app), "%s", app ? app : "");
    snprintf(p.title, sizeof(p.title), "%s", title ? title : "");
    snprintf(p.body, sizeof(p.body), "%s", body ? body : "");
    bd_nt_hdr hdr = { BD_NT_MAGIC, BD_NT_POST, sizeof(p) };
    raw_write(fd, &hdr, sizeof(hdr));
    raw_write(fd, &p, sizeof(p));
}

static uint32_t raw_read_ack(int fd)
{
    bd_nt_hdr hdr;
    bd_nt_id aid;
    uint8_t *p = (uint8_t *)&hdr;
    size_t left = sizeof(hdr);
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n <= 0) return 0;
        p += (size_t)n;
        left -= (size_t)n;
    }
    if (hdr.magic != BD_NT_MAGIC || hdr.type != BD_NT_POSTED ||
        hdr.len != sizeof(aid))
        return 0;
    p = (uint8_t *)&aid;
    left = sizeof(aid);
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n <= 0) return 0;
        p += (size_t)n;
        left -= (size_t)n;
    }
    return aid.id;
}

/* Run bd_notify_send in a child (it waits for the ack) while the
 * parent pumps the server. Returns the id the child received,
 * or -1. */
static int child_send(bd_nt_server *srv, bd_shell *sh,
                      const char *path, const char *app,
                      const char *title, const char *body, int urgency)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        int id = bd_notify_send(path, app, title, body, urgency);
        (void)!write(pipefd[1], &id, sizeof(id));
        _exit(0);
    }
    close(pipefd[1]);
    int status = 0;
    for (int i = 0; i < 1000; i++) {
        bd_nt_server_poll(srv, sh);
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        usleep(1000);
    }
    int id = -1;
    ssize_t n = read(pipefd[0], &id, sizeof(id));
    close(pipefd[0]);
    if (n != (ssize_t)sizeof(id)) return -1;
    return id;
}

static int child_cancel(bd_nt_server *srv, bd_shell *sh,
                        const char *path, uint32_t id)
{
    pid_t pid = fork();
    if (pid == 0) {
        int rc = bd_notify_cancel(path, id);
        _exit(rc == 0 ? 0 : 1);
    }
    int status = 0;
    for (int i = 0; i < 1000; i++) {
        bd_nt_server_poll(srv, sh);
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        usleep(1000);
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

int main(void)
{
    setup_reg();
    bd_shell *sh = make_shell();
    CHECK(sh != NULL);

    char path[128];
    sock_path(path, sizeof(path), "main");
    bd_nt_server *srv = bd_nt_server_start(path);
    CHECK(srv != NULL);
    CHECK(bd_nt_server_path(srv) &&
          strcmp(bd_nt_server_path(srv), path) == 0);

    /* --- one post end to end (production client) --- */
    int id1 = child_send(srv, sh, path, "時計",
                         "アラーム 07:00", "起きる時間", BD_NT_CRITICAL);
    CHECK(id1 > 0);
    CHECK(bd_shell_notify_count(sh) == 1);
    CHECK(bd_shell_notify_at(sh, 0) &&
          strcmp(bd_shell_notify_at(sh, 0),
                 "アラーム 07:00 - 起きる時間") == 0);
    CHECK(bd_shell_notify_app_at(sh, 0) &&
          strcmp(bd_shell_notify_app_at(sh, 0), "時計") == 0);
    CHECK(bd_shell_notify_urgency_at(sh, 0) == BD_NT_CRITICAL);
    CHECK(bd_shell_notify_id_at(sh, 0) == (uint32_t)id1);

    /* --- title-only post: display text is the bare title --- */
    int id2 = child_send(srv, sh, path, "bd-files",
                         "ダウンロード完了", NULL, BD_NT_NORMAL);
    CHECK(id2 > id1);
    CHECK(bd_shell_notify_count(sh) == 2);
    CHECK(bd_shell_notify_at(sh, 1) &&
          strcmp(bd_shell_notify_at(sh, 1), "ダウンロード完了") == 0);

    /* --- several clients connected at once, one poll drains all --- */
    int fds[3];
    for (int i = 0; i < 3; i++) {
        fds[i] = raw_connect(path);
        CHECK(fds[i] >= 0);
    }
    raw_post(fds[0], "a", "n3", "", BD_NT_LOW);
    raw_post(fds[1], "b", "n4", "", BD_NT_NORMAL);
    raw_post(fds[2], "c", "n5", "", BD_NT_NORMAL);
    int applied = bd_nt_server_poll(srv, sh);
    CHECK(applied == 3);
    /* list is capped at BD_NOTIFY_MAX=4: the first post (id1)
     * was evicted by the third of these */
    CHECK(bd_shell_notify_count(sh) == BD_NOTIFY_MAX);
    uint32_t ack0 = raw_read_ack(fds[0]);
    uint32_t ack1 = raw_read_ack(fds[1]);
    uint32_t ack2 = raw_read_ack(fds[2]);
    CHECK(ack0 > 0 && ack1 > ack0 && ack2 > ack1);
    CHECK(bd_shell_notify_id_at(sh, 0) == (uint32_t)id2);
    CHECK(bd_shell_notify_id_at(sh, 1) == ack0);
    CHECK(bd_shell_notify_id_at(sh, 3) == ack2);
    for (int i = 0; i < 3; i++) close(fds[i]);

    /* --- cancel by id (production client) --- */
    CHECK(child_cancel(srv, sh, path, ack0) == 0);
    CHECK(bd_shell_notify_count(sh) == 3);
    CHECK(bd_shell_notify_id_at(sh, 0) == (uint32_t)id2);
    CHECK(bd_shell_notify_id_at(sh, 1) == ack1);
    /* cancelling an unknown id is well-formed: applied, no error,
     * no state change */
    unsigned errs = bd_nt_server_errors(srv);
    CHECK(child_cancel(srv, sh, path, 99999) == 0);
    CHECK(bd_shell_notify_count(sh) == 3);
    CHECK(bd_nt_server_errors(srv) == errs);
    /* client-side: id 0 never connects */
    CHECK(bd_notify_cancel(path, 0) == -1);

    /* --- malformed input: bad magic --- */
    int bad = raw_connect(path);
    CHECK(bad >= 0);
    bd_nt_hdr bh = { 0xDEADBEEF, BD_NT_POST, 0 };
    raw_write(bad, &bh, sizeof(bh));
    (void)bd_nt_server_poll(srv, sh);
    CHECK(bd_nt_server_errors(srv) == errs + 1);
    close(bad);

    /* --- malformed input: oversized payload length --- */
    int big = raw_connect(path);
    CHECK(big >= 0);
    bd_nt_hdr gh = { BD_NT_MAGIC, BD_NT_POST, 0x7FFFFFFF };
    raw_write(big, &gh, sizeof(gh));
    (void)bd_nt_server_poll(srv, sh);
    CHECK(bd_nt_server_errors(srv) == errs + 2);
    close(big);

    /* --- malformed input: truncated message then EOF --- */
    size_t before = bd_shell_notify_count(sh);
    int tr = raw_connect(path);
    CHECK(tr >= 0);
    bd_nt_hdr th = { BD_NT_MAGIC, BD_NT_POST, sizeof(bd_nt_post) };
    raw_write(tr, &th, sizeof(th));
    raw_write(tr, "half", 4); /* far less than a full post */
    close(tr);
    (void)bd_nt_server_poll(srv, sh);
    (void)bd_nt_server_poll(srv, sh);
    CHECK(bd_nt_server_errors(srv) == errs + 3);
    CHECK(bd_shell_notify_count(sh) == before);

    /* --- the server is still alive and serving after all that --- */
    int id3 = child_send(srv, sh, path, "生存確認", "まだ動く", "",
                         BD_NT_NORMAL);
    CHECK(id3 > 0);
    CHECK(bd_shell_notify_at(sh, bd_shell_notify_count(sh) - 1) &&
          strcmp(bd_shell_notify_at(sh, bd_shell_notify_count(sh) - 1),
                 "まだ動く") == 0);

    /* --- no server: client calls fail cleanly --- */
    char none[128];
    sock_path(none, sizeof(none), "absent");
    CHECK(bd_notify_send(none, "x", "y", "", BD_NT_NORMAL) == -1);
    CHECK(bd_notify_cancel(none, 1) == -1);
    CHECK(bd_notify_send(path, "x", "", "", BD_NT_NORMAL) == -1);

    bd_nt_server_free(srv);
    bd_shell_free(sh);
    ln_store_free(g_store);   /* the shell does not own these */
    de_catalog_free(g_cat);

    /* --- session wiring: a real session step delivers a post --- */
    char wmp[128], ntp[128];
    sock_path(wmp, sizeof(wmp), "wm");
    sock_path(ntp, sizeof(ntp), "sess");
    de_catalog *cat = de_catalog_new(NULL);
    de_catalog_load_dir(cat, "tests/fixtures/applications");
    ln_store *store = ln_store_new();
    bd_session_config cfg = { 0 };
    cfg.width = 800;
    cfg.height = 480;
    cfg.catalog = cat;
    cfg.store = store;
    cfg.exec_fn = mock_exec;
    cfg.wm_path = wmp;
    cfg.notify_path = ntp;
    bd_session *sess = bd_session_new(&cfg);
    CHECK(sess != NULL);
    CHECK(bd_session_notify_server(sess) != NULL);
    bd_shell *ssh = bd_session_shell(sess);
    size_t n0 = bd_shell_notify_count(ssh);
    pid_t pid = fork();
    if (pid == 0) {
        int id = bd_notify_send(ntp, "セッション", "通し確認", "",
                                BD_NT_NORMAL);
        _exit(id > 0 ? 0 : 1);
    }
    int status = 0;
    for (int i = 0; i < 1000; i++) {
        bd_session_step(sess, 1);
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        usleep(1000);
    }
    bd_session_step(sess, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(bd_shell_notify_count(ssh) == n0 + 1);
    CHECK(bd_shell_notify_at(ssh, n0) &&
          strcmp(bd_shell_notify_at(ssh, n0), "通し確認") == 0);
    bd_session_free(sess);
    ln_store_free(store);
    de_catalog_free(cat);

    printf("test_notify: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
