/* wm.c - WM server for shell v2 (M2). See wm.h for the protocol
 * contract and DESIGN-shell-v2.md section 5.
 *
 * Implementation notes:
 *  - Messages are parsed from a per-connection byte buffer; pixel
 *    buffers only ever travel server -> client (memfds), so the
 *    server read path needs no ancillary-data handling.
 *  - The server mmaps every client buffer and composites by
 *    reading the committed ("front") buffer directly. There is no
 *    staging copy of client pixels anywhere in this file.
 *  - All socket writes are single sendmsg calls per message, which
 *    is what the client's fd-association relies on (see wmclient.c).
 */
#include "wm.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

/* UI-REDESIGN 2.2: titlebar 40 with 40x40 button hit areas,
 * resize handle 32 (single-finger grab size). */
#define TITLEBAR_H 40
#define BORDER 2
#define HANDLE 32
#define MIN_CW 64
#define MIN_CH 48
#define MAX_DMG 64
#define MAX_COMMIT_RECTS 32

/* ---------------- wire helpers ---------------- */

static int send_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n) {
        ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += r; n -= (size_t)r;
    }
    return 0;
}

static int send_msg(int fd, uint32_t type, const void *payload, uint32_t len,
                    const int *fds, int nfds)
{
    bd_wm_hdr hdr = { BD_WM_MAGIC, type, len };
    if (!fds || nfds == 0) {
        if (send_all(fd, &hdr, sizeof(hdr)) < 0) return -1;
        if (len && send_all(fd, payload, len) < 0) return -1;
        return 0;
    }
    /* header + payload + fds in one sendmsg so the fds are
     * unambiguously attached to this message's first bytes */
    size_t total = sizeof(hdr) + len;
    char *buf = malloc(total);
    if (!buf) return -1;
    memcpy(buf, &hdr, sizeof(hdr));
    if (len) memcpy(buf + sizeof(hdr), payload, len);
    struct iovec iov = { buf, total };
    char ctrl[CMSG_SPACE(sizeof(int) * 2)];
    struct msghdr msg = {0};
    msg.msg_iov = &iov; msg.msg_iovlen = 1;
    msg.msg_control = ctrl; msg.msg_controllen = sizeof(ctrl);
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)nfds);
    memcpy(CMSG_DATA(c), fds, sizeof(int) * (size_t)nfds);
    msg.msg_controllen = c->cmsg_len;
    int rc = 0;
    size_t sent = 0;
    while (sent < total) {
        struct iovec iv = { buf + sent, total - sent };
        msg.msg_iov = &iv;
        ssize_t r = sendmsg(fd, &msg, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EINTR) continue;
            rc = -1; break;
        }
        sent += (size_t)r;
        msg.msg_control = NULL; msg.msg_controllen = 0; /* fds once */
    }
    free(buf);
    return rc;
}

/* ---------------- window state ---------------- */

typedef struct win {
    uint32_t id;
    int fd;
    char title[64];
    int x, y;                 /* frame origin */
    int cw, ch;               /* committed content size */
    bd_win_state state;
    bd_win_state state_before_min;
    tk_rect prev_frame;       /* before maximize/fullscreen */
    int prev_cw, prev_ch;

    uint32_t gen;
    int fds[2];
    uint32_t *px[2];
    int front;                /* committed buffer index, -1 = none */

    bool has_pending;         /* resize in flight */
    uint32_t pgen;
    int pfds[2];
    uint32_t *ppx[2];
    int pcw, pch;

    tk_rect last_damage[MAX_COMMIT_RECTS]; /* content coords */
    int n_last_damage;

    /* text-input state last reported with BD_WM_IM */
    bool im_active;
    bool im_has_rect;
    tk_rect im_rect;          /* content coordinates */

    char rbuf[8192];
    size_t rlen;
    bool dead;
} win;

struct bd_wm_server {
    int listen_fd;
    char path[108];
    tk_rect workarea;
    tk_rect screen;
    win **wins;               /* bottom -> top */
    size_t n, cap;
    uint32_t next_id;
    uint32_t focused_id;
    uint32_t im_serial;       /* bumps on every IM state change */
    unsigned proto_errors;
    unsigned cascade;
    int inset_bottom;         /* OSK strip reserved at the bottom */
    tk_rect damage[MAX_DMG];
    size_t n_damage;
    /* pointer gesture state */
    uint32_t drag_id;
    int drag_mode;            /* 0 none, 1 move, 2 resize */
    int drag_dx, drag_dy;
};

static void add_damage(bd_wm_server *s, tk_rect r)
{
    if (r.w <= 0 || r.h <= 0) return;
    /* clip to screen */
    int x2 = r.x + r.w, y2 = r.y + r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    if (x2 > s->screen.x + s->screen.w) x2 = s->screen.x + s->screen.w;
    if (y2 > s->screen.y + s->screen.h) y2 = s->screen.y + s->screen.h;
    r.w = x2 - r.x; r.h = y2 - r.y;
    if (r.w <= 0 || r.h <= 0) return;
    if (s->n_damage < MAX_DMG) {
        s->damage[s->n_damage++] = r;
        return;
    }
    /* full: fold into one bounding box to stay bounded */
    tk_rect *b = &s->damage[0];
    int bx2 = b->x + b->w > r.x + r.w ? b->x + b->w : r.x + r.w;
    int by2 = b->y + b->h > r.y + r.h ? b->y + b->h : r.y + r.h;
    b->x = b->x < r.x ? b->x : r.x;
    b->y = b->y < r.y ? b->y : r.y;
    b->w = bx2 - b->x; b->h = by2 - b->y;
    s->n_damage = 1;
}

static tk_rect frame_of(const win *w)
{
    if (w->state == BD_WIN_FULLSCREEN)
        return (tk_rect){ w->x, w->y, w->cw, w->ch };
    return (tk_rect){ w->x, w->y, w->cw + 2 * BORDER,
                      TITLEBAR_H + w->ch + BORDER };
}

static tk_rect content_of(const win *w)
{
    if (w->state == BD_WIN_FULLSCREEN)
        return (tk_rect){ w->x, w->y, w->cw, w->ch };
    return (tk_rect){ w->x + BORDER, w->y + TITLEBAR_H, w->cw, w->ch };
}

/* Areas with the OSK bottom inset applied. Fullscreen windows
 * occupy the screen above the strip; the workarea is clipped so
 * it never extends into the strip either. With inset 0 these are
 * exactly the configured areas. */
static tk_rect eff_screen(const bd_wm_server *s)
{
    tk_rect r = s->screen;
    if (s->inset_bottom > 0) {
        r.h -= s->inset_bottom;
        if (r.h < 0) r.h = 0;
    }
    return r;
}

static tk_rect eff_workarea(const bd_wm_server *s)
{
    tk_rect r = s->workarea;
    tk_rect es = eff_screen(s);
    int bottom = r.y + r.h, ebottom = es.y + es.h;
    if (bottom > ebottom) r.h = ebottom - r.y;
    if (r.h < 0) r.h = 0;
    return r;
}

static win *find_win(bd_wm_server *s, uint32_t id)
{
    for (size_t i = 0; i < s->n; i++)
        if (s->wins[i]->id == id) return s->wins[i];
    return NULL;
}

static int alloc_bufs(int w, int h, int fds[2], uint32_t *px[2])
{
    fds[0] = fds[1] = -1; px[0] = px[1] = NULL;
    size_t len = (size_t)w * (size_t)h * 4;
    for (int i = 0; i < 2; i++) {
        fds[i] = memfd_create("bdwm", MFD_CLOEXEC);
        if (fds[i] < 0) goto fail;
        if (ftruncate(fds[i], (off_t)len) < 0) goto fail;
        px[i] = mmap(NULL, len, PROT_READ, MAP_SHARED, fds[i], 0);
        if (px[i] == MAP_FAILED) { px[i] = NULL; goto fail; }
    }
    return 0;
fail:
    for (int i = 0; i < 2; i++) {
        if (px[i]) munmap(px[i], len);
        if (fds[i] >= 0) close(fds[i]);
        fds[i] = -1; px[i] = NULL;
    }
    return -1;
}

static void free_bufs(int w, int h, int fds[2], uint32_t *px[2])
{
    size_t len = (size_t)w * (size_t)h * 4;
    for (int i = 0; i < 2; i++) {
        if (px[i]) munmap(px[i], len);
        if (fds[i] >= 0) close(fds[i]);
        fds[i] = -1; px[i] = NULL;
    }
}

/* ---------------- focus ---------------- */

static void send_event(win *w, const bd_wm_event *ev)
{
    send_msg(w->fd, BD_WM_EVENT, ev, sizeof(*ev), NULL, 0);
}

static void set_focus(bd_wm_server *s, uint32_t id)
{
    if (s->focused_id == id) return;
    win *old = find_win(s, s->focused_id);
    if (old) {
        bd_wm_event ev = {0}; ev.type = BD_WM_EV_FOCUS_OUT;
        send_event(old, &ev);
        add_damage(s, frame_of(old));
    }
    s->focused_id = id;
    win *nw = find_win(s, id);
    if (nw) {
        bd_wm_event ev = {0}; ev.type = BD_WM_EV_FOCUS_IN;
        send_event(nw, &ev);
        add_damage(s, frame_of(nw));
    }
}

static void raise_win(bd_wm_server *s, win *w)
{
    size_t i;
    for (i = 0; i < s->n; i++) if (s->wins[i] == w) break;
    if (i >= s->n || i == s->n - 1) return;
    memmove(&s->wins[i], &s->wins[i + 1], (s->n - i - 1) * sizeof(win *));
    s->wins[s->n - 1] = w;
}

/* ---------------- window lifecycle ---------------- */

static void remove_win(bd_wm_server *s, win *w)
{
    add_damage(s, frame_of(w));
    size_t i;
    for (i = 0; i < s->n; i++) if (s->wins[i] == w) break;
    if (i < s->n) {
        memmove(&s->wins[i], &s->wins[i + 1], (s->n - i - 1) * sizeof(win *));
        s->n--;
    }
    if (s->focused_id == w->id) {
        s->focused_id = 0;
        /* focus the topmost visible remaining window */
        for (size_t j = s->n; j-- > 0;) {
            if (s->wins[j]->state != BD_WIN_MINIMIZED) {
                set_focus(s, s->wins[j]->id);
                break;
            }
        }
    }
    if (s->drag_id == w->id) { s->drag_id = 0; s->drag_mode = 0; }
    free_bufs(w->cw, w->ch, w->fds, w->px);
    if (w->has_pending) free_bufs(w->pcw, w->pch, w->pfds, w->ppx);
    close(w->fd);
    free(w);
}

static void handle_hello(bd_wm_server *s, int fd, const bd_wm_hello *hello)
{
    win *w = calloc(1, sizeof(*w));
    if (!w) { close(fd); return; }
    w->fd = fd;
    w->id = s->next_id++;
    w->front = -1;
    snprintf(w->title, sizeof(w->title), "%s",
             hello->title[0] ? hello->title : "window");
    tk_rect wa = eff_workarea(s);
    int cw = hello->req_w > 0 ? hello->req_w : 320;
    int ch = hello->req_h > 0 ? hello->req_h : 240;
    if (cw < MIN_CW) cw = MIN_CW;
    if (ch < MIN_CH) ch = MIN_CH;
    if (cw > wa.w - 2 * BORDER) cw = wa.w - 2 * BORDER;
    if (ch > wa.h - TITLEBAR_H - BORDER)
        ch = wa.h - TITLEBAR_H - BORDER;
    w->cw = cw; w->ch = ch;
    unsigned off = (s->cascade++ % 5) * 24;
    w->x = wa.x + 16 + (int)off;
    w->y = wa.y + 12 + (int)off;
    w->state = BD_WIN_NORMAL;
    w->gen = 0;
    if (alloc_bufs(cw, ch, w->fds, w->px) < 0) { close(fd); free(w); return; }
    if (s->n >= s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 8;
        win **na = realloc(s->wins, nc * sizeof(win *));
        if (!na) { free_bufs(cw, ch, w->fds, w->px); close(fd); free(w); return; }
        s->wins = na; s->cap = nc;
    }
    s->wins[s->n++] = w;
    bd_wm_geom g = { w->id, w->gen, cw, ch };
    int fds[2] = { w->fds[0], w->fds[1] };
    send_msg(fd, BD_WM_GEOM, &g, sizeof(g), fds, 2);
    add_damage(s, frame_of(w));
    set_focus(s, w->id);
}

static void handle_commit(bd_wm_server *s, win *w, const char *payload,
                          uint32_t len)
{
    if (len < sizeof(bd_wm_commit)) { s->proto_errors++; return; }
    bd_wm_commit c;
    memcpy(&c, payload, sizeof(c));
    if (c.nrects < 1 || c.nrects > MAX_COMMIT_RECTS ||
        len < sizeof(c) + (uint32_t)c.nrects * sizeof(tk_rect)) {
        /* damage list is mandatory - a commit without one is a
         * protocol error and changes nothing */
        s->proto_errors++;
        return;
    }
    const tk_rect *rects = (const tk_rect *)(payload + sizeof(c));
    if (c.buf < 0 || c.buf > 1) { s->proto_errors++; return; }

    if (w->has_pending && c.gen == w->pgen) {
        /* resize completes: swap generations */
        tk_rect old_frame = frame_of(w);
        free_bufs(w->cw, w->ch, w->fds, w->px);
        w->fds[0] = w->pfds[0]; w->fds[1] = w->pfds[1];
        w->px[0] = w->ppx[0]; w->px[1] = w->ppx[1];
        w->pfds[0] = w->pfds[1] = -1; w->ppx[0] = w->ppx[1] = NULL;
        w->cw = w->pcw; w->ch = w->pch;
        w->gen = w->pgen;
        w->has_pending = false;
        w->front = c.buf;
        add_damage(s, old_frame);
        add_damage(s, frame_of(w));
    } else if (c.gen == w->gen) {
        int old_front = w->front;
        w->front = c.buf;
        tk_rect co = content_of(w);
        for (int i = 0; i < c.nrects; i++) {
            tk_rect r = rects[i];
            /* clip damage to the content area */
            int x2 = r.x + r.w, y2 = r.y + r.h;
            if (r.x < 0) r.x = 0;
            if (r.y < 0) r.y = 0;
            if (x2 > w->cw) x2 = w->cw;
            if (y2 > w->ch) y2 = w->ch;
            r.w = x2 - r.x; r.h = y2 - r.y;
            if (r.w <= 0 || r.h <= 0) continue;
            if (w->n_last_damage < MAX_COMMIT_RECTS)
                w->last_damage[w->n_last_damage++] = r;
            add_damage(s, (tk_rect){ co.x + r.x, co.y + r.y, r.w, r.h });
        }
        if (old_front >= 0 && old_front != c.buf) {
            bd_wm_bufref rel = { w->gen, old_front };
            send_msg(w->fd, BD_WM_RELEASE, &rel, sizeof(rel), NULL, 0);
        }
    } else {
        s->proto_errors++; /* stale generation */
    }
}

static void handle_msg(bd_wm_server *s, win *w, uint32_t type,
                       const char *payload, uint32_t len)
{
    switch (type) {
    case BD_WM_COMMIT:
        handle_commit(s, w, payload, len);
        break;
    case BD_WM_TITLE:
        if (len >= sizeof(bd_wm_title)) {
            bd_wm_title t;
            memcpy(&t, payload, sizeof(t));
            t.title[sizeof(t.title) - 1] = 0;
            snprintf(w->title, sizeof(w->title), "%s", t.title);
            tk_rect f = frame_of(w);
            add_damage(s, (tk_rect){ f.x, f.y, f.w, TITLEBAR_H });
        }
        break;
    case BD_WM_BYE:
        w->dead = true;
        break;
    case BD_WM_IM:
        if (len >= sizeof(bd_wm_im)) {
            bd_wm_im im;
            memcpy(&im, payload, sizeof(im));
            bool active = im.active != 0;
            bool has_rect = im.w > 0 && im.h > 0;
            tk_rect r = { im.x, im.y, im.w, im.h };
            if (active != w->im_active || has_rect != w->im_has_rect ||
                (has_rect && (r.x != w->im_rect.x || r.y != w->im_rect.y ||
                              r.w != w->im_rect.w || r.h != w->im_rect.h)))
                s->im_serial++; /* new input target, or target gone */
            w->im_active = active;
            w->im_has_rect = has_rect;
            w->im_rect = r;
        }
        break;
    default:
        s->proto_errors++;
        break;
    }
}

/* returns -1 when the connection died */
static int win_service(bd_wm_server *s, win *w)
{
    ssize_t r = recv(w->fd, w->rbuf + w->rlen, sizeof(w->rbuf) - w->rlen, 0);
    if (r == 0) return -1;
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        return -1;
    }
    w->rlen += (size_t)r;
    size_t off = 0;
    int changes = 0;
    while (w->rlen - off >= sizeof(bd_wm_hdr)) {
        bd_wm_hdr hdr;
        memcpy(&hdr, w->rbuf + off, sizeof(hdr));
        if (hdr.magic != BD_WM_MAGIC) return -1;
        if (hdr.len > sizeof(w->rbuf)) return -1;
        if (w->rlen - off < sizeof(hdr) + hdr.len) break;
        handle_msg(s, w, hdr.type, w->rbuf + off + sizeof(hdr), hdr.len);
        changes++;
        off += sizeof(hdr) + hdr.len;
        if (w->dead) break;
    }
    memmove(w->rbuf, w->rbuf + off, w->rlen - off);
    w->rlen -= off;
    return changes;
}

/* ---------------- public server API ---------------- */

const char *bd_wm_default_path(void)
{
    static char buf[108];
    const char *e = getenv("BD_WM_SOCK");
    if (e && e[0]) return e;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && rt[0]) {
        snprintf(buf, sizeof(buf), "%s/bdwm.sock", rt);
        return buf;
    }
    snprintf(buf, sizeof(buf), "/tmp/bdwm-%u.sock", (unsigned)getuid());
    return buf;
}

bd_wm_server *bd_wm_server_start(const char *sock_path)
{
    bd_wm_server *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    /* M22: the panel is 854x480 (AUDIT SS20); the workarea
     * keeps the 32px shelf reservation at the bottom. */
    s->workarea = (tk_rect){ 0, 0, 854, 448 };
    s->screen = (tk_rect){ 0, 0, 854, 480 };
    s->next_id = 1;
    const char *path = sock_path ? sock_path : bd_wm_default_path();
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->listen_fd < 0) { free(s); return NULL; }
    unlink(s->path);
    struct sockaddr_un un = {0};
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", s->path);
    if (bind(s->listen_fd, (struct sockaddr *)&un, sizeof(un)) < 0 ||
        listen(s->listen_fd, 8) < 0) {
        close(s->listen_fd); free(s); return NULL;
    }
    int fl = fcntl(s->listen_fd, F_GETFL, 0);
    fcntl(s->listen_fd, F_SETFL, fl | O_NONBLOCK);
    return s;
}

void bd_wm_server_free(bd_wm_server *s)
{
    if (!s) return;
    while (s->n) remove_win(s, s->wins[s->n - 1]);
    close(s->listen_fd);
    unlink(s->path);
    free(s->wins);
    free(s);
}

const char *bd_wm_server_path(const bd_wm_server *s) { return s->path; }

void bd_wm_server_set_workarea(bd_wm_server *s, tk_rect r) { s->workarea = r; }
void bd_wm_server_set_screen(bd_wm_server *s, tk_rect r)
{
    s->screen = r;
    add_damage(s, r);
}

int bd_wm_server_poll(bd_wm_server *s, int timeout_ms)
{
    int changes = 0;
    size_t nfds = s->n + 1;
    struct pollfd *pf = calloc(nfds, sizeof(*pf));
    if (!pf) return 0;
    pf[0].fd = s->listen_fd; pf[0].events = POLLIN;
    for (size_t i = 0; i < s->n; i++) {
        pf[i + 1].fd = s->wins[i]->fd;
        pf[i + 1].events = POLLIN;
    }
    int r = poll(pf, (nfds_t)nfds, timeout_ms);
    if (r > 0) {
        /* Snapshot readability BEFORE the accept loop runs:
         * accepting appends to s->wins, so indexing pf by the
         * (grown) window list afterwards would read past the
         * allocation - ASan caught exactly that. Windows accepted
         * in this round are serviced from the next poll. */
        size_t n0 = s->n;
        int *rfds = malloc((n0 ? n0 : 1) * sizeof(int));
        short *rrev = malloc((n0 ? n0 : 1) * sizeof(short));
        if (rfds && rrev) {
            for (size_t i = 0; i < n0; i++) {
                rfds[i] = pf[i + 1].fd;
                rrev[i] = pf[i + 1].revents;
            }
        } else {
            n0 = 0;
        }
        if (pf[0].revents & POLLIN) {
            for (;;) {
                int fd = accept(s->listen_fd, NULL, NULL);
                if (fd < 0) break;
                /* a new connection must say HELLO first; read it
                 * synchronously (tiny, and clients send it
                 * immediately on connect) */
                bd_wm_hdr hdr; bd_wm_hello hello;
                struct pollfd apf = { fd, POLLIN, 0 };
                if (poll(&apf, 1, 1000) <= 0 ||
                    recv(fd, &hdr, sizeof(hdr), MSG_WAITALL) != (ssize_t)sizeof(hdr) ||
                    hdr.magic != BD_WM_MAGIC || hdr.type != BD_WM_HELLO ||
                    hdr.len != sizeof(hello) ||
                    recv(fd, &hello, sizeof(hello), MSG_WAITALL) != (ssize_t)sizeof(hello)) {
                    close(fd);
                    continue;
                }
                int fl = fcntl(fd, F_GETFL, 0);
                fcntl(fd, F_SETFL, fl | O_NONBLOCK);
                handle_hello(s, fd, &hello);
                changes++;
            }
        }
        /* service the windows that existed at poll time, using
         * the snapshot (removals during service are deferred via
         * the dead flag) */
        for (size_t i = 0; i < s->n; i++) {
            win *w = s->wins[i];
            bool readable = false;
            for (size_t j = 0; j < n0; j++)
                if (rfds[j] == w->fd && (rrev[j] & (POLLIN | POLLHUP)))
                    readable = true;
            if (!readable) continue;
            int c = win_service(s, w);
            if (c < 0) w->dead = true;
            else changes += c;
        }
        for (size_t i = s->n; i-- > 0;)
            if (s->wins[i]->dead) { remove_win(s, s->wins[i]); changes++; }
        free(rfds);
        free(rrev);
    }
    free(pf);
    return changes;
}

size_t bd_wm_server_count(const bd_wm_server *s) { return s->n; }

static void fill_info(const win *w, bd_wm_server *s, bd_wm_win_info *out)
{
    out->id = w->id;
    snprintf(out->title, sizeof(out->title), "%s", w->title);
    out->frame = frame_of(w);
    out->content = content_of(w);
    out->state = w->state;
    out->focused = s->focused_id == w->id;
}

bool bd_wm_server_info(const bd_wm_server *s, size_t idx, bd_wm_win_info *out)
{
    if (idx >= s->n) return false;
    fill_info(s->wins[idx], (bd_wm_server *)s, out);
    return true;
}

bool bd_wm_server_info_id(const bd_wm_server *s, uint32_t id, bd_wm_win_info *out)
{
    win *w = find_win((bd_wm_server *)s, id);
    if (!w) return false;
    fill_info(w, (bd_wm_server *)s, out);
    return true;
}

uint32_t bd_wm_server_focused(const bd_wm_server *s) { return s->focused_id; }
unsigned bd_wm_server_proto_errors(const bd_wm_server *s) { return s->proto_errors; }

bool bd_wm_server_thumbnail(bd_wm_server *srv, uint32_t id,
                            uint32_t *px, int w, int h)
{
    win *wp = find_win(srv, id);
    if (!wp || wp->front < 0 || !px || w <= 0 || h <= 0) return false;
    if (wp->cw <= 0 || wp->ch <= 0) return false;
    const uint32_t *sp = wp->px[wp->front];
    for (int y = 0; y < h; y++) {
        const uint32_t *srow = sp + (size_t)(y * wp->ch / h) * (size_t)wp->cw;
        uint32_t *drow = px + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
            drow[x] = srow[x * wp->cw / w];
    }
    return true;
}

bool bd_wm_server_focus(bd_wm_server *s, uint32_t id)
{
    win *w = find_win(s, id);
    if (!w) return false;
    if (w->state == BD_WIN_MINIMIZED) return false;
    raise_win(s, w);
    set_focus(s, id);
    add_damage(s, frame_of(w));
    return true;
}

bool bd_wm_server_move(bd_wm_server *s, uint32_t id, int x, int y)
{
    win *w = find_win(s, id);
    if (!w || w->state != BD_WIN_NORMAL) return false;
    add_damage(s, frame_of(w));
    tk_rect f = frame_of(w);
    if (x < s->screen.x - f.w + 48) x = s->screen.x - f.w + 48;
    if (x > s->screen.x + s->screen.w - 48) x = s->screen.x + s->screen.w - 48;
    if (y < s->screen.y) y = s->screen.y;
    if (y > s->screen.y + s->screen.h - TITLEBAR_H)
        y = s->screen.y + s->screen.h - TITLEBAR_H;
    w->x = x; w->y = y;
    add_damage(s, frame_of(w));
    return true;
}

bool bd_wm_server_resize(bd_wm_server *s, uint32_t id, int content_w, int content_h)
{
    win *w = find_win(s, id);
    if (!w || w->state == BD_WIN_MINIMIZED) return false;
    if (content_w < MIN_CW) content_w = MIN_CW;
    if (content_h < MIN_CH) content_h = MIN_CH;
    if (content_w == w->cw && content_h == w->ch && !w->has_pending)
        return true;
    if (w->has_pending) free_bufs(w->pcw, w->pch, w->pfds, w->ppx);
    w->pcw = content_w; w->pch = content_h;
    w->pgen = w->gen + 1;
    if (w->has_pending) w->pgen++; /* superseded generation */
    if (alloc_bufs(content_w, content_h, w->pfds, w->ppx) < 0) {
        w->has_pending = false;
        return false;
    }
    w->has_pending = true;
    bd_wm_geom g = { w->id, w->pgen, content_w, content_h };
    int fds[2] = { w->pfds[0], w->pfds[1] };
    send_msg(w->fd, BD_WM_RESIZE, &g, sizeof(g), fds, 2);
    return true;
}

bool bd_wm_server_set_state(bd_wm_server *s, uint32_t id, bd_win_state st)
{
    win *w = find_win(s, id);
    if (!w) return false;
    if (st == w->state) return true;
    add_damage(s, frame_of(w));
    switch (st) {
    case BD_WIN_MINIMIZED:
        w->state_before_min = w->state;
        w->state = BD_WIN_MINIMIZED;
        if (s->focused_id == id) {
            s->focused_id = 0;
            for (size_t j = s->n; j-- > 0;) {
                if (s->wins[j] != w && s->wins[j]->state != BD_WIN_MINIMIZED) {
                    set_focus(s, s->wins[j]->id);
                    break;
                }
            }
        }
        return true;
    case BD_WIN_NORMAL:
        if (w->state == BD_WIN_MINIMIZED) {
            w->state = w->state_before_min == BD_WIN_MINIMIZED ?
                BD_WIN_NORMAL : w->state_before_min;
            if (w->state != BD_WIN_NORMAL) {
                /* restoring into max/full: geometry already there */
                add_damage(s, frame_of(w));
                raise_win(s, w);
                set_focus(s, id);
                return true;
            }
            add_damage(s, frame_of(w));
            raise_win(s, w);
            set_focus(s, id);
            return true;
        }
        /* from maximized/fullscreen: restore saved geometry */
        {
            int pcw = w->prev_cw, pch = w->prev_ch;
            w->state = BD_WIN_NORMAL;
            w->x = w->prev_frame.x; w->y = w->prev_frame.y;
            add_damage(s, frame_of(w));
            return bd_wm_server_resize(s, id, pcw, pch);
        }
    case BD_WIN_MAXIMIZED:
    case BD_WIN_FULLSCREEN: {
        if (w->state == BD_WIN_MINIMIZED) w->state = w->state_before_min;
        if (w->state == BD_WIN_NORMAL) {
            w->prev_frame = frame_of(w);
            w->prev_cw = w->cw; w->prev_ch = w->ch;
        }
        w->state = st;
        tk_rect area = st == BD_WIN_MAXIMIZED ? eff_workarea(s)
                                              : eff_screen(s);
        w->x = area.x; w->y = area.y;
        int ncw = st == BD_WIN_MAXIMIZED ? area.w - 2 * BORDER : area.w;
        int nch = st == BD_WIN_MAXIMIZED ? area.h - TITLEBAR_H - BORDER : area.h;
        add_damage(s, area);
        raise_win(s, w);
        set_focus(s, id);
        return bd_wm_server_resize(s, id, ncw, nch);
    }
    }
    return false;
}

bool bd_wm_server_close(bd_wm_server *s, uint32_t id)
{
    win *w = find_win(s, id);
    if (!w) return false;
    send_msg(w->fd, BD_WM_CLOSE, NULL, 0, NULL, 0);
    return true;
}

/* ---------------- on-screen keyboard support ---------------- */

/* Re-derive a maximized/fullscreen window's geometry from its
 * state and the current (inset-adjusted) areas, resizing the
 * client when the content size changed. prev_frame/prev_cw/
 * prev_ch are NOT touched: they hold the user's normal geometry,
 * which an inset change must not rewrite. */
static void relayout_stated(bd_wm_server *s, win *w)
{
    if (w->state != BD_WIN_MAXIMIZED && w->state != BD_WIN_FULLSCREEN)
        return;
    tk_rect area = w->state == BD_WIN_MAXIMIZED ? eff_workarea(s)
                                                : eff_screen(s);
    add_damage(s, frame_of(w));
    w->x = area.x; w->y = area.y;
    int ncw = w->state == BD_WIN_MAXIMIZED ? area.w - 2 * BORDER : area.w;
    int nch = w->state == BD_WIN_MAXIMIZED ? area.h - TITLEBAR_H - BORDER
                                           : area.h;
    add_damage(s, area);
    (void)bd_wm_server_resize(s, w->id, ncw, nch);
}

void bd_wm_server_set_bottom_inset(bd_wm_server *s, int px)
{
    if (px < 0) px = 0;
    if (px == s->inset_bottom) return;
    s->inset_bottom = px;
    for (size_t i = 0; i < s->n; i++)
        relayout_stated(s, s->wins[i]);
}

int bd_wm_server_bottom_inset(const bd_wm_server *s)
{
    return s->inset_bottom;
}

bool bd_wm_server_im_active(const bd_wm_server *s)
{
    win *w = find_win((bd_wm_server *)s, s->focused_id);
    return w && w->state != BD_WIN_MINIMIZED && w->im_active;
}

uint32_t bd_wm_server_im_serial(const bd_wm_server *s)
{
    return s ? s->im_serial : 0;
}

bool bd_wm_server_im_field(const bd_wm_server *s, tk_rect *out)
{
    win *w = find_win((bd_wm_server *)s, s->focused_id);
    if (!w || !w->im_active || !w->im_has_rect) return false;
    tk_rect co = content_of(w);
    if (out) *out = (tk_rect){ co.x + w->im_rect.x, co.y + w->im_rect.y,
                               w->im_rect.w, w->im_rect.h };
    return true;
}

bool bd_wm_server_send_composition(bd_wm_server *s, const char *text)
{
    win *w = find_win(s, s->focused_id);
    if (!w || w->state == BD_WIN_MINIMIZED) return false;
    bd_wm_comp c = {0};
    if (text) snprintf(c.text, sizeof(c.text), "%s", text);
    return send_msg(w->fd, BD_WM_COMP, &c, sizeof(c), NULL, 0) == 0;
}

/* ---------------- pointer / key routing ---------------- */

static bool rect_has(tk_rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static tk_rect close_box(const win *w)
{
    tk_rect f = frame_of(w);
    return (tk_rect){ f.x + f.w - TITLEBAR_H, f.y, TITLEBAR_H, TITLEBAR_H };
}
static tk_rect max_box(const win *w)
{
    tk_rect f = frame_of(w);
    return (tk_rect){ f.x + f.w - 2 * TITLEBAR_H, f.y, TITLEBAR_H, TITLEBAR_H };
}
static tk_rect min_box(const win *w)
{
    tk_rect f = frame_of(w);
    return (tk_rect){ f.x + f.w - 3 * TITLEBAR_H, f.y, TITLEBAR_H, TITLEBAR_H };
}
static tk_rect handle_box(const win *w)
{
    tk_rect f = frame_of(w);
    return (tk_rect){ f.x + f.w - HANDLE, f.y + f.h - HANDLE, HANDLE, HANDLE };
}

bool bd_wm_server_pointer(bd_wm_server *s, int ev_type, int x, int y)
{
    if (ev_type == BD_WM_EV_POINTER_MOVE && s->drag_id) {
        win *w = find_win(s, s->drag_id);
        if (!w) { s->drag_id = 0; s->drag_mode = 0; return false; }
        if (s->drag_mode == 1) {
            bd_wm_server_move(s, w->id, x - s->drag_dx, y - s->drag_dy);
            return true;
        }
        if (s->drag_mode == 2) {
            tk_rect f = frame_of(w);
            int ncw = x - f.x - 2 * BORDER + HANDLE / 2;
            int nch = y - f.y - TITLEBAR_H - BORDER + HANDLE / 2;
            if (ncw != w->cw || nch != w->ch)
                bd_wm_server_resize(s, w->id, ncw, nch);
            return true;
        }
    }
    if (ev_type == BD_WM_EV_POINTER_UP && s->drag_id) {
        s->drag_id = 0; s->drag_mode = 0;
        return true;
    }
    /* topmost visible window under the point */
    win *hit = NULL;
    for (size_t i = s->n; i-- > 0;) {
        win *w = s->wins[i];
        if (w->state == BD_WIN_MINIMIZED) continue;
        if (rect_has(frame_of(w), x, y)) { hit = w; break; }
    }
    if (!hit) return false;
    if (ev_type == BD_WM_EV_POINTER_DOWN) {
        raise_win(s, hit);
        set_focus(s, hit->id);
        if (hit->state != BD_WIN_FULLSCREEN) {
            if (rect_has(close_box(hit), x, y)) {
                bd_wm_server_close(s, hit->id);
                return true;
            }
            if (rect_has(min_box(hit), x, y)) {
                bd_wm_server_set_state(s, hit->id, BD_WIN_MINIMIZED);
                return true;
            }
            if (rect_has(max_box(hit), x, y)) {
                bd_wm_server_set_state(s, hit->id,
                    hit->state == BD_WIN_MAXIMIZED ? BD_WIN_NORMAL
                                                   : BD_WIN_MAXIMIZED);
                return true;
            }
            tk_rect f = frame_of(hit);
            if (y < f.y + TITLEBAR_H) {
                if (hit->state == BD_WIN_NORMAL) {
                    s->drag_id = hit->id; s->drag_mode = 1;
                    s->drag_dx = x - f.x; s->drag_dy = y - f.y;
                }
                return true;
            }
            if (hit->state == BD_WIN_NORMAL && rect_has(handle_box(hit), x, y)) {
                s->drag_id = hit->id; s->drag_mode = 2;
                return true;
            }
        }
    }
    tk_rect co = content_of(hit);
    if (rect_has(co, x, y)) {
        bd_wm_event ev = {0};
        ev.type = ev_type;
        ev.x = x - co.x; ev.y = y - co.y;
        send_event(hit, &ev);
        return true;
    }
    return true; /* chrome border: consumed, no action */
}

bool bd_wm_server_key(bd_wm_server *s, int ev_type, int key, uint32_t mods,
                      const char *text)
{
    win *w = find_win(s, s->focused_id);
    if (!w || w->state == BD_WIN_MINIMIZED) return false;
    bd_wm_event ev = {0};
    ev.type = ev_type; ev.key = key; ev.mods = mods;
    if (text) snprintf(ev.text, sizeof(ev.text), "%s", text);
    send_event(w, &ev);
    return true;
}

/* ---------------- compositing ---------------- */

size_t bd_wm_server_damage(const bd_wm_server *s, tk_rect *out, size_t max)
{
    size_t n = s->n_damage < max ? s->n_damage : max;
    if (out) memcpy(out, s->damage, n * sizeof(tk_rect));
    return s->n_damage;
}

void bd_wm_server_invalidate(bd_wm_server *s, tk_rect r)
{
    if (r.w > 0 && r.h > 0) add_damage(s, r);
}

static void draw_chrome(bd_wm_server *s, win *w, tk_canvas *dst,
                        const tk_theme *t)
{
    tk_rect f = frame_of(w);
    bool focused = s->focused_id == w->id;
    /* UI-REDESIGN 2.3: floating windows get overlay corners (8),
     * maximized windows are square (they touch the panel edge).
     * The titlebar is a neutral surface_alt band; focus reads
     * through the title colour (text vs text_dim), not a coloured
     * bar. Buttons are neutral faces with stroked glyphs. (The
     * spec's app icon at the titlebar's left needs an icon field
     * in the window protocol, which does not exist yet.) */
    int rad = w->state == BD_WIN_NORMAL ? t->radius_overlay : 0;
    tk_canvas_fill_round_rect(dst, f, rad, t->border);
    tk_rect inner = { f.x + 1, f.y + 1, f.w - 2, f.h - 2 };
    tk_canvas_fill_round_rect(dst, inner, rad > 0 ? rad - 1 : 0,
                              t->surface);
    /* titlebar band: rounded top, squared bottom */
    tk_rect tb = { f.x + 1, f.y + 1, f.w - 2, TITLEBAR_H - 1 };
    tk_canvas_fill_round_rect(dst, tb, rad > 0 ? rad - 1 : 0,
                              t->surface_alt);
    tk_canvas_fill_rect(dst, (tk_rect){ tb.x, tb.y + tb.h - 8,
                                        tb.w, 8 }, t->surface_alt);
    int lh = tk_line_height_st(t, TK_TEXT_HEADING);
    tk_canvas_draw_text_st(dst, t, TK_TEXT_HEADING, f.x + 12,
                           f.y + (TITLEBAR_H - lh) / 2, w->title,
                           focused ? t->text : t->text_dim);
    /* window buttons: minimize (bar), maximize (square),
     * close (x) - glyphs stroked, faces neutral */
    tk_rect mb = min_box(w);
    tk_canvas_fill_rect(dst, (tk_rect){ mb.x + 13, mb.y + 26, 14, 3 },
                        t->text);
    tk_rect xb = max_box(w);
    if (w->state == BD_WIN_MAXIMIZED)
        tk_canvas_stroke_rect(dst, (tk_rect){ xb.x + 14, xb.y + 12, 12, 12 },
                              2, t->text);
    else
        tk_canvas_stroke_rect(dst, (tk_rect){ xb.x + 13, xb.y + 13, 14, 14 },
                              2, t->text);
    tk_rect cb = close_box(w);
    for (int i = 0; i < 12; i++) {
        tk_canvas_fill_rect(dst, (tk_rect){ cb.x + 14 + i, cb.y + 14 + i, 2, 2 },
                            t->text);
        tk_canvas_fill_rect(dst, (tk_rect){ cb.x + 25 - i, cb.y + 14 + i, 2, 2 },
                            t->text);
    }
}

static void blit_content(win *w, tk_canvas *dst, tk_rect clip)
{
    tk_rect co = content_of(w);
    int x0 = clip.x > co.x ? clip.x : co.x;
    int y0 = clip.y > co.y ? clip.y : co.y;
    int x1 = clip.x + clip.w < co.x + co.w ? clip.x + clip.w : co.x + co.w;
    int y1 = clip.y + clip.h < co.y + co.h ? clip.y + clip.h : co.y + co.h;
    if (x1 <= x0 || y1 <= y0) return;
    uint32_t *dp = tk_canvas_pixels(dst);
    int dw = tk_canvas_width(dst);
    int dh = tk_canvas_height(dst);
    /* The raw memcpy below does not honour the canvas clip stack,
     * so the destination bounds are applied here explicitly: a
     * frame may extend past the screen (dragged/resized), and
     * writing past the canvas was a real heap overflow (ASan). */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dw) x1 = dw;
    if (y1 > dh) y1 = dh;
    if (x1 <= x0 || y1 <= y0) return;
    if (w->front < 0) {
        tk_canvas_fill_rect(dst, (tk_rect){ x0, y0, x1 - x0, y1 - y0 },
                            (tk_color){ 32, 32, 36, 255 });
        return;
    }
    const uint32_t *sp = w->px[w->front];
    for (int y = y0; y < y1; y++) {
        const uint32_t *srow = sp + (size_t)(y - co.y) * (size_t)w->cw +
                               (size_t)(x0 - co.x);
        uint32_t *drow = dp + (size_t)y * (size_t)dw + (size_t)x0;
        memcpy(drow, srow, (size_t)(x1 - x0) * 4);
    }
}

static void composite_clip(bd_wm_server *s, tk_canvas *dst,
                           const tk_theme *t, tk_rect clip)
{
    for (size_t i = 0; i < s->n; i++) {
        win *w = s->wins[i];
        if (w->state == BD_WIN_MINIMIZED) continue;
        tk_rect f = frame_of(w);
        if (!rect_has((tk_rect){ clip.x, clip.y, 1, 1 }, f.x, f.y) &&
            !(clip.x < f.x + f.w && clip.x + clip.w > f.x &&
              clip.y < f.y + f.h && clip.y + clip.h > f.y))
            continue;
        tk_canvas_push_clip(dst, clip);
        if (w->state != BD_WIN_FULLSCREEN) draw_chrome(s, w, dst, t);
        blit_content(w, dst, clip);
        tk_canvas_pop_clip(dst);
    }
}

void bd_wm_composite(bd_wm_server *s, tk_canvas *dst, const tk_theme *t)
{
    for (size_t i = 0; i < s->n_damage; i++)
        composite_clip(s, dst, t, s->damage[i]);
    s->n_damage = 0;
}

void bd_wm_composite_full(bd_wm_server *s, tk_canvas *dst, const tk_theme *t)
{
    for (size_t i = 0; i < s->n; i++) {
        win *w = s->wins[i];
        if (w->state == BD_WIN_MINIMIZED) continue;
        tk_canvas_push_clip(dst, frame_of(w));
        if (w->state != BD_WIN_FULLSCREEN) draw_chrome(s, w, dst, t);
        blit_content(w, dst, frame_of(w));
        tk_canvas_pop_clip(dst);
    }
    s->n_damage = 0;
}
