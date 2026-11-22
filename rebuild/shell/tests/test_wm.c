/* test_wm - end-to-end tests for the M2 window manager: a real
 * server in this process, real client processes (wm_sample_raw,
 * wm_sample_tk) forked and driven through the socket protocol.
 * Verifies connection, geometry, compositing from shm, focus,
 * pointer routing, key routing, titlebar drag, handle resize
 * (buffer reallocation, not scaling), minimize/maximize/restore,
 * damage accounting, protocol-error rejection and close. */
#include "wm.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static bd_wm_server *srv;
static char sockpath[128];

static void pump(int ms)
{
    int step = 20;
    while (ms > 0) {
        bd_wm_server_poll(srv, step < ms ? step : ms);
        ms -= step;
    }
}

static bool wait_count(size_t n, int timeout_ms)
{
    while (timeout_ms > 0) {
        if (bd_wm_server_count(srv) == n) return true;
        bd_wm_server_poll(srv, 20);
        timeout_ms -= 20;
    }
    return bd_wm_server_count(srv) == n;
}

static bool info_by_title(const char *title, bd_wm_win_info *out)
{
    size_t n = bd_wm_server_count(srv);
    for (size_t i = 0; i < n; i++) {
        if (bd_wm_server_info(srv, i, out) &&
            strcmp(out->title, title) == 0)
            return true;
    }
    return false;
}

static tk_canvas *cv;
static const tk_color BG = { 10, 20, 30, 255 };

static void repaint(void)
{
    tk_canvas_clear(cv, BG);
    bd_wm_composite_full(srv, cv, tk_theme_default());
}

static bool wait_pixel(int x, int y, int r, int g, int b, int timeout_ms)
{
    while (timeout_ms > 0) {
        bd_wm_server_poll(srv, 20);
        timeout_ms -= 20;
        repaint();
        tk_color c = tk_canvas_pixel(cv, x, y);
        if (c.r == r && c.g == g && c.b == b) return true;
    }
    return false;
}

static pid_t spawn(char *const args[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execv(args[0], args);
        _exit(127);
    }
    return pid;
}

/* --- raw protocol reader (for the IM/inset section): buffered
 * message reads over a bare socket; ancillary fds are closed on
 * arrival (the assertions care about geometry, not pixels). --- */
typedef struct { int fd; char buf[8192]; size_t len; } raw_reader;

static bool raw_next(raw_reader *r, uint32_t *type, char *payload,
                     uint32_t *len_out, int timeout_ms)
{
    for (;;) {
        if (r->len >= sizeof(bd_wm_hdr)) {
            bd_wm_hdr h;
            memcpy(&h, r->buf, sizeof(h));
            if (h.magic != BD_WM_MAGIC) return false;
            if (h.len > 1024) return false;
            if (r->len >= sizeof(h) + h.len) {
                *type = h.type;
                *len_out = h.len;
                if (h.len) memcpy(payload, r->buf + sizeof(h), h.len);
                size_t used = sizeof(h) + h.len;
                memmove(r->buf, r->buf + used, r->len - used);
                r->len -= used;
                return true;
            }
        }
        struct pollfd pfd = { r->fd, POLLIN, 0 };
        if (poll(&pfd, 1, timeout_ms) <= 0) return false;
        char ctrl[CMSG_SPACE(sizeof(int) * 4)];
        char tmp[4096];
        struct iovec iov = { tmp, sizeof(tmp) };
        struct msghdr msg = {0};
        msg.msg_iov = &iov; msg.msg_iovlen = 1;
        msg.msg_control = ctrl; msg.msg_controllen = sizeof(ctrl);
        ssize_t n = recvmsg(r->fd, &msg, 0);
        if (n <= 0) return false;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c;
             c = CMSG_NXTHDR(&msg, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                int nf = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
                for (int i = 0; i < nf; i++) {
                    int f;
                    memcpy(&f, CMSG_DATA(c) + (size_t)i * sizeof(int),
                           sizeof(int));
                    close(f);
                }
            }
        }
        if (r->len + (size_t)n > sizeof(r->buf)) return false;
        memcpy(r->buf + r->len, tmp, (size_t)n);
        r->len += (size_t)n;
        timeout_ms = 200;
    }
}

/* Wait for one message of type `want`, skipping others. */
static bool raw_wait(raw_reader *r, uint32_t want, char *payload,
                     uint32_t *len, int timeout_ms)
{
    while (timeout_ms > 0) {
        uint32_t t;
        if (raw_next(r, &t, payload, len, 200) && t == want)
            return true;
        timeout_ms -= 200;
    }
    return false;
}

static void raw_send(int fd, uint32_t type, const void *payload,
                     uint32_t len)
{
    bd_wm_hdr h = { BD_WM_MAGIC, type, len };
    (void)!write(fd, &h, sizeof(h));
    if (len) (void)!write(fd, payload, len);
}

int main(void)
{
    char dir[] = "/tmp/bdwmtestXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    snprintf(sockpath, sizeof(sockpath), "%s/wm.sock", dir);
    srv = bd_wm_server_start(sockpath);
    CHECK(srv != NULL);
    if (!srv) return 1;
    bd_wm_server_set_workarea(srv, (tk_rect){ 0, 0, 800, 448 });
    bd_wm_server_set_screen(srv, (tk_rect){ 0, 0, 800, 480 });
    cv = tk_canvas_new(800, 480);

    /* --- connect two real clients --- */
    char *raw_args[] = { "./wm_sample_raw", sockpath, "RawApp",
                         "240", "160", "3060a0", NULL };
    pid_t raw_pid = spawn(raw_args);
    CHECK(wait_count(1, 3000));
    char *tk_args[] = { "./wm_sample_tk", sockpath, "TkApp",
                        "260", "180", NULL };
    pid_t tk_pid = spawn(tk_args);
    CHECK(wait_count(2, 3000));

    bd_wm_win_info raw, tk;
    CHECK(info_by_title("RawApp", &raw));
    CHECK(info_by_title("TkApp", &tk));
    CHECK(raw.content.w == 240 && raw.content.h == 160);
    CHECK(tk.content.w == 260 && tk.content.h == 180);
    CHECK(bd_wm_server_focused(srv) == tk.id); /* newest is focused */

    /* --- compositing reads client shm: raw bg color shows ---
     * (probe points are chosen in the part of RawApp that the
     * cascaded TkApp window does not cover) */
    CHECK(wait_pixel(raw.content.x + 8, raw.content.y + 140,
                     0x30, 0x60, 0xa0, 3000));

    /* --- switcher thumbnail: the committed frame, scaled ---
     * (the raw client fills its window with 0x3060a0, so every
     * thumbnail pixel must be that colour; an unknown id and a
     * never-committed window have no thumbnail) */
    {
        static uint32_t thumb[60 * 40];
        CHECK(bd_wm_server_thumbnail(srv, raw.id, thumb, 60, 40));
        CHECK(thumb[20 * 60 + 30] == 0xFF3060A0u);
        CHECK(thumb[0] == 0xFF3060A0u);
        CHECK(!bd_wm_server_thumbnail(srv, 9999, thumb, 60, 40));
    }

    /* tk client painted toolkit widgets: some content pixel
     * differs from its clear color */
    {
        repaint();
        bool any = false;
        for (int y = tk.content.y + 2; y < tk.content.y + tk.content.h - 2 && !any; y++)
            for (int x = tk.content.x + 2; x < tk.content.x + tk.content.w - 2; x++) {
                tk_color c = tk_canvas_pixel(cv, x, y);
                if (!(c.r == 235 && c.g == 238 && c.b == 242)) { any = true; break; }
            }
        CHECK(any);
    }

    /* --- pointer routing: click moves raw's square --- */
    {
        int cx = raw.content.x + 14, cy = raw.content.y + 42;
        CHECK(bd_wm_server_pointer(srv, BD_WM_EV_POINTER_DOWN, cx, cy));
        CHECK(bd_wm_server_pointer(srv, BD_WM_EV_POINTER_UP, cx, cy));
        CHECK(wait_pixel(cx, cy, 255, 255, 255, 3000));
        CHECK(bd_wm_server_focused(srv) == raw.id);
    }

    /* --- key routing: focused window only --- */
    bd_wm_server_key(srv, BD_WM_EV_KEY_DOWN, 0, 0, "x");
    /* raw bg cycles to palette[1] = a03030 */
    CHECK(wait_pixel(raw.content.x + 8, raw.content.y + 150,
                     0xa0, 0x30, 0x30, 3000));
    CHECK(bd_wm_server_focus(srv, tk.id));
    pump(100);
    bd_wm_server_key(srv, BD_WM_EV_KEY_DOWN, 0, 0, "x");
    pump(300);
    repaint();
    tk_color c = tk_canvas_pixel(cv, raw.content.x + 8, raw.content.y + 150);
    CHECK(c.r == 0xa0 && c.g == 0x30 && c.b == 0x30); /* unchanged */

    /* --- titlebar drag moves the frame --- */
    {
        bd_wm_win_info before, after;
        CHECK(info_by_title("TkApp", &before));
        int gx = before.frame.x + 40, gy = before.frame.y + 16;
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_DOWN, gx, gy);
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_MOVE, gx + 40, gy + 25);
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_UP, gx + 40, gy + 25);
        CHECK(info_by_title("TkApp", &after));
        CHECK(after.frame.x == before.frame.x + 40);
        CHECK(after.frame.y == before.frame.y + 25);
    }

    /* --- move RawApp to a free area first (its resize handle is
     * covered by TkApp at the cascade position) --- */
    {
        bd_wm_win_info before, after;
        CHECK(info_by_title("RawApp", &before));
        int gx = before.frame.x + 76, gy = before.frame.y + 16;
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_DOWN, gx, gy);
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_MOVE, gx + 420, gy + 230);
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_UP, gx + 420, gy + 230);
        CHECK(info_by_title("RawApp", &after));
        CHECK(after.frame.x == before.frame.x + 420);
        CHECK(after.frame.y == before.frame.y + 230);
    }

    /* --- resize through the corner handle: real realloc --- */
    {
        bd_wm_win_info before, after;
        CHECK(info_by_title("RawApp", &before));
        int hx = before.frame.x + before.frame.w - 12;
        int hy = before.frame.y + before.frame.h - 12;
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_DOWN, hx, hy);
        /* +56/+36 (not +60/+40): the 32px handle's grab-centre
         * compensation (HANDLE/2, UI-REDESIGN P2-5) widens the
         * result by 4px, and the probed corner must stay on the
         * 800px canvas */
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_MOVE, hx + 56, hy + 36);
        bd_wm_server_pointer(srv, BD_WM_EV_POINTER_UP, hx + 56, hy + 36);
        bool grew = false;
        for (int t = 0; t < 3000 && !grew; t += 20) {
            bd_wm_server_poll(srv, 20);
            if (info_by_title("RawApp", &after) &&
                after.content.w >= before.content.w + 50 &&
                after.content.h >= before.content.h + 30)
                grew = true;
        }
        CHECK(grew);
        /* client redrew at the new size: new corner area has bg */
        CHECK(wait_pixel(after.content.x + after.content.w - 6,
                         after.content.y + after.content.h - 6,
                         0xa0, 0x30, 0x30, 3000));
    }

    /* --- minimize hides, restore brings back --- */
    {
        bd_wm_win_info cur;
        CHECK(info_by_title("RawApp", &cur));
        CHECK(bd_wm_server_set_state(srv, raw.id, BD_WIN_MINIMIZED));
        pump(50);
        repaint();
        tk_color at = tk_canvas_pixel(cv, cur.content.x + 100,
                                      cur.content.y + 100);
        CHECK(at.r == BG.r && at.g == BG.g && at.b == BG.b);
        CHECK(bd_wm_server_count(srv) == 2); /* still connected */
        CHECK(bd_wm_server_set_state(srv, raw.id, BD_WIN_NORMAL));
        CHECK(wait_pixel(cur.content.x + 100, cur.content.y + 100,
                         0xa0, 0x30, 0x30, 3000));
    }

    /* --- maximize to the workarea, then restore --- */
    {
        bd_wm_win_info before, maxed, back;
        CHECK(info_by_title("TkApp", &before));
        CHECK(bd_wm_server_set_state(srv, tk.id, BD_WIN_MAXIMIZED));
        bool ok = false;
        for (int t = 0; t < 3000 && !ok; t += 20) {
            bd_wm_server_poll(srv, 20);
            if (info_by_title("TkApp", &maxed) &&
                maxed.content.w == 800 - 4 && maxed.content.h == 448 - 42)
                ok = true;
        }
        CHECK(ok);
        CHECK(bd_wm_server_set_state(srv, tk.id, BD_WIN_NORMAL));
        ok = false;
        for (int t = 0; t < 3000 && !ok; t += 20) {
            bd_wm_server_poll(srv, 20);
            if (info_by_title("TkApp", &back) &&
                back.content.w == before.content.w &&
                back.content.h == before.content.h)
                ok = true;
        }
        CHECK(ok);
    }

    /* --- damage accounting --- */
    {
        bd_wm_server_focus(srv, raw.id);
        pump(50);
        tk_rect dmg[16];
        size_t nd = bd_wm_server_damage(srv, dmg, 16);
        CHECK(nd >= 1);
        tk_canvas_clear(cv, BG);
        bd_wm_composite(srv, cv, tk_theme_default());
        CHECK(bd_wm_server_damage(srv, dmg, 16) == 0);
    }

    /* --- protocol error: COMMIT without damage is rejected --- */
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un un = {0};
        un.sun_family = AF_UNIX;
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", sockpath);
        CHECK(connect(fd, (struct sockaddr *)&un, sizeof(un)) == 0);
        bd_wm_hdr hdr = { BD_WM_MAGIC, BD_WM_HELLO, sizeof(bd_wm_hello) };
        bd_wm_hello hello = {0};
        snprintf(hello.title, sizeof(hello.title), "BadProto");
        hello.req_w = 100; hello.req_h = 80;
        (void)!write(fd, &hdr, sizeof(hdr));
        (void)!write(fd, &hello, sizeof(hello));
        CHECK(wait_count(3, 3000));
        /* consume GEOM */
        char buf[256];
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 2000) > 0) (void)recv(fd, buf, sizeof(buf), 0);
        unsigned before = bd_wm_server_proto_errors(srv);
        bd_wm_hdr ch = { BD_WM_MAGIC, BD_WM_COMMIT, sizeof(bd_wm_commit) };
        bd_wm_commit commit = { 0, 0, 0 }; /* nrects = 0: illegal */
        (void)!write(fd, &ch, sizeof(ch));
        (void)!write(fd, &commit, sizeof(commit));
        pump(300);
        CHECK(bd_wm_server_proto_errors(srv) > before);
        close(fd);
        CHECK(wait_count(2, 3000)); /* disconnect removes it */
    }

    /* --- IM state, composition, bottom inset (raw protocol) --- */
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un un = {0};
        un.sun_family = AF_UNIX;
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", sockpath);
        CHECK(connect(fd, (struct sockaddr *)&un, sizeof(un)) == 0);
        bd_wm_hello hello = {0};
        snprintf(hello.title, sizeof(hello.title), "ImRaw");
        hello.req_w = 200; hello.req_h = 120;
        raw_send(fd, BD_WM_HELLO, &hello, sizeof(hello));
        CHECK(wait_count(3, 3000));
        raw_reader rr = { .fd = fd };
        char payload[1024];
        uint32_t mlen = 0;
        CHECK(raw_wait(&rr, BD_WM_GEOM, payload, &mlen, 3000));
        bd_wm_geom g;
        memcpy(&g, payload, sizeof(g));
        uint32_t imid = g.win_id;
        CHECK(bd_wm_server_focused(srv) == imid);
        bd_wm_win_info ii;
        CHECK(bd_wm_server_info_id(srv, imid, &ii));
        CHECK(!bd_wm_server_im_active(srv));

        /* IM report: active + field rect in content coords */
        unsigned pe0 = bd_wm_server_proto_errors(srv);
        bd_wm_im im = { 1, 10, 20, 180, 44 };
        raw_send(fd, BD_WM_IM, &im, sizeof(im));
        pump(200);
        CHECK(bd_wm_server_im_active(srv));
        tk_rect sf;
        CHECK(bd_wm_server_im_field(srv, &sf));
        CHECK(sf.x == ii.content.x + 10 && sf.y == ii.content.y + 20 &&
              sf.w == 180 && sf.h == 44);
        /* a well-formed IM report is not a protocol error */
        CHECK(bd_wm_server_proto_errors(srv) == pe0);
        /* the IM serial identifies the input target: it bumped
         * for the activation, and an identical re-report (a
         * client re-syncing) must not bump it again */
        uint32_t serial0 = bd_wm_server_im_serial(srv);
        CHECK(serial0 >= 1);
        raw_send(fd, BD_WM_IM, &im, sizeof(im));
        pump(200);
        CHECK(bd_wm_server_im_serial(srv) == serial0);

        /* composition is delivered to the focused window */
        CHECK(bd_wm_server_send_composition(srv, "あい"));
        CHECK(raw_wait(&rr, BD_WM_COMP, payload, &mlen, 3000));
        bd_wm_comp comp;
        memcpy(&comp, payload, sizeof(comp));
        CHECK(strcmp(comp.text, "あい") == 0);
        CHECK(bd_wm_server_send_composition(srv, NULL));
        CHECK(raw_wait(&rr, BD_WM_COMP, payload, &mlen, 3000));
        memcpy(&comp, payload, sizeof(comp));
        CHECK(comp.text[0] == 0);

        /* fullscreen geometry follows the bottom inset */
        CHECK(bd_wm_server_set_state(srv, imid, BD_WIN_FULLSCREEN));
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        memcpy(&g, payload, sizeof(g));
        CHECK(g.w == 800 && g.h == 480);
        bd_wm_server_set_bottom_inset(srv, 272);
        CHECK(bd_wm_server_bottom_inset(srv) == 272);
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        memcpy(&g, payload, sizeof(g));
        CHECK(g.w == 800 && g.h == 480 - 272);
        bd_wm_server_set_bottom_inset(srv, 0);
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        memcpy(&g, payload, sizeof(g));
        CHECK(g.w == 800 && g.h == 480); /* restored exactly */

        /* maximized geometry follows it too (workarea 800x448) */
        CHECK(bd_wm_server_set_state(srv, imid, BD_WIN_MAXIMIZED));
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        memcpy(&g, payload, sizeof(g));
        CHECK(g.w == 800 - 4 && g.h == 448 - 42);
        bd_wm_server_set_bottom_inset(srv, 272);
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        memcpy(&g, payload, sizeof(g));
        /* the workarea (448 of the 480 screen) is clipped at the
         * strip's top edge: content = 480 - 272 - chrome 42 */
        CHECK(g.w == 800 - 4 && g.h == 480 - 272 - 42);
        bd_wm_server_set_bottom_inset(srv, 0);
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        memcpy(&g, payload, sizeof(g));
        CHECK(g.h == 448 - 42);

        /* IM off */
        bd_wm_im off = { 0, 0, 0, 0, 0 };
        raw_send(fd, BD_WM_IM, &off, sizeof(off));
        pump(200);
        CHECK(!bd_wm_server_im_active(srv));
        /* off and on again are new targets: the serial moved */
        CHECK(bd_wm_server_im_serial(srv) > serial0);
        raw_send(fd, BD_WM_IM, &im, sizeof(im));
        pump(200);
        CHECK(bd_wm_server_im_active(srv));
        uint32_t serial1 = bd_wm_server_im_serial(srv);
        CHECK(serial1 > serial0 + 1);
        raw_send(fd, BD_WM_IM, &off, sizeof(off));
        pump(200);
        CHECK(!bd_wm_server_im_active(srv));
        close(fd);
        CHECK(wait_count(2, 3000));
    }

    /* --- born-maximized: a client that asks for at least the
     * whole work area starts MAXIMIZED with exactly the
     * geometry set_state would give it (the DE's own apps open
     * this way - they request the panel size); a client asking
     * for less in either dimension keeps the floating placement
     * (the browser's 800-wide ask on an 854 panel is the
     * standing example) --- */
    {
        char *big_args[] = { "./wm_sample_raw", sockpath, "BigApp",
                             "800", "480", "3060a0", NULL };
        pid_t big_pid = spawn(big_args);
        CHECK(wait_count(3, 3000));
        bd_wm_win_info big;
        CHECK(info_by_title("BigApp", &big));
        CHECK(big.state == BD_WIN_MAXIMIZED);
        CHECK(big.frame.x == 0 && big.frame.y == 0 &&
              big.frame.w == 800 && big.frame.h == 448);
        CHECK(big.content.w == 800 - 4 && big.content.h == 448 - 42);
        CHECK(bd_wm_server_focused(srv) == big.id);

        char *short_args[] = { "./wm_sample_raw", sockpath, "ShortApp",
                               "800", "400", "3060a0", NULL };
        pid_t short_pid = spawn(short_args);
        CHECK(wait_count(4, 3000));
        bd_wm_win_info shrt;
        CHECK(info_by_title("ShortApp", &shrt));
        CHECK(shrt.state == BD_WIN_NORMAL);
        CHECK(shrt.content.w == 800 - 4 && shrt.content.h == 400);

        /* minimize -> restore returns the born-maximized window
         * to MAXIMIZED (state_before_min), not to floating */
        CHECK(bd_wm_server_set_state(srv, big.id, BD_WIN_MINIMIZED));
        CHECK(bd_wm_server_set_state(srv, big.id, BD_WIN_NORMAL));
        CHECK(info_by_title("BigApp", &big));
        CHECK(big.state == BD_WIN_MAXIMIZED);
        /* an explicit unmaximize lands on the floating restore
         * geometry: the clamped request size */
        CHECK(bd_wm_server_set_state(srv, big.id, BD_WIN_NORMAL));
        CHECK(info_by_title("BigApp", &big));
        CHECK(big.state == BD_WIN_NORMAL);
        CHECK(big.content.w == 800 - 4 && big.content.h == 448 - 42);
        CHECK(bd_wm_server_set_state(srv, big.id, BD_WIN_MAXIMIZED));
        CHECK(info_by_title("BigApp", &big));
        CHECK(big.state == BD_WIN_MAXIMIZED &&
              big.frame.x == 0 && big.frame.y == 0);

        CHECK(bd_wm_server_close(srv, big.id));
        CHECK(bd_wm_server_close(srv, shrt.id));
        int status = 0;
        bool big_out = false, short_out = false;
        for (int t = 0; t < 3000 && !(big_out && short_out); t += 20) {
            bd_wm_server_poll(srv, 20);
            if (!big_out && waitpid(big_pid, &status, WNOHANG) == big_pid)
                big_out = true;
            if (!short_out &&
                waitpid(short_pid, &status, WNOHANG) == short_pid)
                short_out = true;
        }
        CHECK(big_out && short_out);
        CHECK(wait_count(2, 3000));
    }

    /* --- layout contract: declared minimum is ENFORCED by
     * the WM, the maximize intent is a flag (not a size), and
     * a floating window's initial frame lands inside the
     * work area (DESIGN-layout-contract.md SS2) --- */
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un un = {0};
        un.sun_family = AF_UNIX;
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", sockpath);
        CHECK(connect(fd, (struct sockaddr *)&un, sizeof(un)) == 0);
        bd_wm_hello hello = {0};
        snprintf(hello.title, sizeof(hello.title), "MinWin");
        hello.req_w = 300; hello.req_h = 200;
        hello.min_w = 480; hello.min_h = 272;
        raw_send(fd, BD_WM_HELLO, &hello, sizeof(hello));
        CHECK(wait_count(3, 3000));
        bd_wm_win_info mw;
        CHECK(info_by_title("MinWin", &mw));
        CHECK(mw.state == BD_WIN_NORMAL);
        /* the declared minimum applies from birth: the
         * 300x200 request was clamped UP to 480x272 */
        CHECK(mw.content.w == 480 && mw.content.h == 272);
        /* A resize is negotiated: the server answers with a
         * RESIZE message carrying the size it will accept.
         * Below the minimum it clamps UP... */
        raw_reader rr = { .fd = fd };
        char payload[256];
        uint32_t mlen = 0;
        /* move off the minimum first (a clamped resize that
         * lands on the current size is a no-op by design) */
        CHECK(bd_wm_server_resize(srv, mw.id, 600, 300));
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        bd_wm_geom g0;
        memcpy(&g0, payload, sizeof(g0));
        CHECK(g0.w == 600 && g0.h == 300);
        CHECK(bd_wm_server_resize(srv, mw.id, 100, 100));
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        bd_wm_geom g;
        memcpy(&g, payload, sizeof(g));
        CHECK(g.w == 480 && g.h == 272);
        /* ...and past the work area it clamps DOWN to it */
        CHECK(bd_wm_server_resize(srv, mw.id, 5000, 5000));
        CHECK(raw_wait(&rr, BD_WM_RESIZE, payload, &mlen, 3000));
        memcpy(&g, payload, sizeof(g));
        CHECK(g.w == 800 - 4 && g.h == 448 - 42);
        close(fd);
        CHECK(wait_count(2, 3000));
    }
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un un = {0};
        un.sun_family = AF_UNIX;
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", sockpath);
        CHECK(connect(fd, (struct sockaddr *)&un, sizeof(un)) == 0);
        bd_wm_hello hello = {0};
        snprintf(hello.title, sizeof(hello.title), "MaxFlag");
        hello.req_w = 200; hello.req_h = 120;
        hello.flags = BD_WM_HELLO_MAXIMIZE;
        raw_send(fd, BD_WM_HELLO, &hello, sizeof(hello));
        CHECK(wait_count(3, 3000));
        bd_wm_win_info mf;
        CHECK(info_by_title("MaxFlag", &mf));
        CHECK(mf.state == BD_WIN_MAXIMIZED);
        CHECK(mf.frame.x == 0 && mf.frame.y == 0 &&
              mf.frame.w == 800 && mf.frame.h == 448);
        CHECK(mf.content.w == 800 - 4 && mf.content.h == 448 - 42);
        close(fd);
        CHECK(wait_count(2, 3000));
    }
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un un = {0};
        un.sun_family = AF_UNIX;
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", sockpath);
        CHECK(connect(fd, (struct sockaddr *)&un, sizeof(un)) == 0);
        bd_wm_hello hello = {0};
        snprintf(hello.title, sizeof(hello.title), "PlaceWin");
        hello.req_w = 796; hello.req_h = 400;
        raw_send(fd, BD_WM_HELLO, &hello, sizeof(hello));
        CHECK(wait_count(3, 3000));
        bd_wm_win_info pw;
        CHECK(info_by_title("PlaceWin", &pw));
        CHECK(pw.state == BD_WIN_NORMAL);
        /* the whole frame is inside the work area */
        CHECK(pw.frame.x >= 0 && pw.frame.y >= 0 &&
              pw.frame.x + pw.frame.w <= 800 &&
              pw.frame.y + pw.frame.h <= 448);
        close(fd);
        CHECK(wait_count(2, 3000));
    }

    /* --- protocol compat: the ORIGINAL 72-byte HELLO (title
     * + req size only) must still be accepted. nsfb-brain is
     * a frozen binary linking the old client; when the
     * extended HELLO was first required it silently fell
     * back to direct framebuffer painting in the guest
     * (phase-8 finding). Frozen clients are forever. --- */
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un un = {0};
        un.sun_family = AF_UNIX;
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", sockpath);
        CHECK(connect(fd, (struct sockaddr *)&un, sizeof(un)) == 0);
        struct { char title[64]; int32_t req_w, req_h; } old;
        memset(&old, 0, sizeof(old));
        snprintf(old.title, sizeof(old.title), "LegacyWin");
        old.req_w = 200; old.req_h = 120;
        bd_wm_hdr hdr = { BD_WM_MAGIC, BD_WM_HELLO, sizeof(old) };
        (void)!write(fd, &hdr, sizeof(hdr));
        (void)!write(fd, &old, sizeof(old));
        CHECK(wait_count(3, 3000));
        bd_wm_win_info lw;
        CHECK(info_by_title("LegacyWin", &lw));
        CHECK(lw.state == BD_WIN_NORMAL);
        CHECK(lw.content.w == 200 && lw.content.h == 120);
        close(fd);
        CHECK(wait_count(2, 3000));
    }

    /* --- close: server request exits the client, window goes --- */
    CHECK(bd_wm_server_close(srv, tk.id));
    {
        int status = 0;
        bool exited = false;
        for (int t = 0; t < 3000 && !exited; t += 20) {
            bd_wm_server_poll(srv, 20);
            if (waitpid(tk_pid, &status, WNOHANG) == tk_pid) exited = true;
        }
        CHECK(exited && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(wait_count(1, 3000));
    CHECK(bd_wm_server_close(srv, raw.id));
    {
        int status = 0;
        bool exited = false;
        for (int t = 0; t < 3000 && !exited; t += 20) {
            bd_wm_server_poll(srv, 20);
            if (waitpid(raw_pid, &status, WNOHANG) == raw_pid) exited = true;
        }
        CHECK(exited && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(wait_count(0, 3000));

    bd_wm_server_free(srv);
    tk_canvas_free(cv);
    printf("test_wm: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
