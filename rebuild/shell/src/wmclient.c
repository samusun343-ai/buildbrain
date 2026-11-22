/* wmclient.c - WM client library for shell v2 (M2). See wm.h.
 *
 * Read path: messages are parsed from a byte buffer filled by
 * recvmsg. SCM_RIGHTS fd sets arrive attached to the first bytes
 * of the GEOM/RESIZE message they belong to (the server sends each
 * message with a single sendmsg); received fds are queued in
 * arrival order and consumed by GEOM/RESIZE messages in stream
 * order, which is unambiguous for this protocol because only
 * GEOM/RESIZE carry fds and they are never reordered.
 *
 * Drawing: the toolkit canvas wraps the current back buffer's shm
 * pixels directly (tk_canvas_wrap) - the client draws into shared
 * memory, the server composites from the same pages. No copy of
 * the pixel data exists on either side.
 */
#include "wm.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

enum { BUF_FREE = 0, BUF_SENT = 1 };

struct bd_wm_client {
    int fd;
    uint32_t win_id;
    uint32_t gen;
    int w, h;
    int fds[2];
    uint32_t *px[2];
    int buf_state[2];
    int back;              /* buffer we may draw into, -1 = none */
    tk_canvas *canvas;     /* wrap of px[back]; rebuilt when back/size changes */
    int canvas_buf;
    bool focused;
    bool dead;
    bool close_pending;    /* CLOSE received, not yet surfaced */
    bool resized_pending;  /* RESIZE applied, not yet surfaced */
    /* last BD_WM_IM report sent (duplicate suppression) */
    bool im_valid;
    bool im_active;
    tk_rect im_rect;
    char rbuf[8192];
    size_t rlen;
    int fdq[8];
    int n_fdq;
};

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

static int cl_send(bd_wm_client *cl, uint32_t type, const void *payload,
                   uint32_t len)
{
    bd_wm_hdr hdr = { BD_WM_MAGIC, type, len };
    if (send_all(cl->fd, &hdr, sizeof(hdr)) < 0) return -1;
    if (len && send_all(cl->fd, payload, len) < 0) return -1;
    return 0;
}

static void drop_buffers(bd_wm_client *cl)
{
    size_t len = (size_t)cl->w * (size_t)cl->h * 4;
    for (int i = 0; i < 2; i++) {
        if (cl->px[i]) munmap(cl->px[i], len);
        if (cl->fds[i] >= 0) close(cl->fds[i]);
        cl->px[i] = NULL; cl->fds[i] = -1;
        cl->buf_state[i] = BUF_FREE;
    }
    if (cl->canvas) { tk_canvas_free(cl->canvas); cl->canvas = NULL; }
    cl->back = -1;
    cl->canvas_buf = -1;
}

static void install_buffers(bd_wm_client *cl, const bd_wm_geom *g,
                            int fd0, int fd1)
{
    drop_buffers(cl);
    cl->win_id = g->win_id;
    cl->gen = g->gen;
    cl->w = g->w; cl->h = g->h;
    cl->fds[0] = fd0; cl->fds[1] = fd1;
    size_t len = (size_t)cl->w * (size_t)cl->h * 4;
    for (int i = 0; i < 2; i++) {
        cl->px[i] = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                         cl->fds[i], 0);
        if (cl->px[i] == MAP_FAILED) cl->px[i] = NULL;
        cl->buf_state[i] = BUF_FREE;
    }
    cl->back = 0;
}

/* Parse and apply one message. Returns 1 when an application event
 * was produced (filled into *ev), 0 when the message was internal,
 * -1 on protocol failure. */
static int cl_handle(bd_wm_client *cl, uint32_t type, const char *payload,
                     uint32_t len, bd_wm_client_event *ev)
{
    switch (type) {
    case BD_WM_GEOM:
    case BD_WM_RESIZE: {
        if (len < sizeof(bd_wm_geom) || cl->n_fdq < 2) return -1;
        bd_wm_geom g;
        memcpy(&g, payload, sizeof(g));
        int fd0 = cl->fdq[0], fd1 = cl->fdq[1];
        memmove(cl->fdq, cl->fdq + 2, (size_t)(cl->n_fdq - 2) * sizeof(int));
        cl->n_fdq -= 2;
        bool was_geom = type == BD_WM_GEOM;
        install_buffers(cl, &g, fd0, fd1);
        if (!was_geom) {
            cl->resized_pending = true;
            ev->type = BD_WM_CEV_RESIZED;
            ev->w = cl->w; ev->h = cl->h;
            ev->x = ev->y = ev->key = 0; ev->mods = 0; ev->text[0] = 0;
            return 1;
        }
        return 0;
    }
    case BD_WM_RELEASE: {
        if (len < sizeof(bd_wm_bufref)) return -1;
        bd_wm_bufref r;
        memcpy(&r, payload, sizeof(r));
        if (r.gen == cl->gen && r.buf >= 0 && r.buf < 2) {
            cl->buf_state[r.buf] = BUF_FREE;
            if (cl->back < 0) cl->back = r.buf;
        }
        return 0;
    }
    case BD_WM_EVENT: {
        if (len < sizeof(bd_wm_event)) return -1;
        bd_wm_event e;
        memcpy(&e, payload, sizeof(e));
        if (e.type == BD_WM_EV_FOCUS_IN) {
            cl->focused = true;
            ev->type = BD_WM_CEV_FOCUS_IN;
        } else if (e.type == BD_WM_EV_FOCUS_OUT) {
            cl->focused = false;
            ev->type = BD_WM_CEV_FOCUS_OUT;
        } else {
            ev->type = e.type; /* BD_WM_EV_* and BD_WM_CEV_* agree */
        }
        ev->x = e.x; ev->y = e.y; ev->key = e.key; ev->mods = e.mods;
        memcpy(ev->text, e.text, sizeof(ev->text));
        ev->text[sizeof(ev->text) - 1] = 0;
        ev->w = ev->h = 0;
        return 1;
    }
    case BD_WM_CLOSE:
        cl->close_pending = true;
        ev->type = BD_WM_CEV_CLOSE;
        ev->x = ev->y = ev->key = 0; ev->mods = 0; ev->text[0] = 0;
        ev->w = ev->h = 0;
        return 1;
    case BD_WM_COMP: {
        if (len < sizeof(bd_wm_comp)) return -1;
        bd_wm_comp c;
        memcpy(&c, payload, sizeof(c));
        c.text[sizeof(c.text) - 1] = 0;
        ev->type = BD_WM_CEV_COMPOSITION;
        ev->x = ev->y = ev->key = 0; ev->mods = 0; ev->text[0] = 0;
        ev->w = ev->h = 0;
        memcpy(ev->comp, c.text, sizeof(ev->comp));
        return 1;
    }
    default:
        return 0; /* unknown: ignore */
    }
}

/* Fill the read buffer once. Returns 1 = bytes read, 0 = would
 * block / timeout already handled by caller poll, -1 = dead. */
static int cl_fill(bd_wm_client *cl)
{
    char buf[4096];
    char ctrl[CMSG_SPACE(sizeof(int) * 4)];
    struct iovec iov = { buf, sizeof(buf) };
    struct msghdr msg = {0};
    msg.msg_iov = &iov; msg.msg_iovlen = 1;
    msg.msg_control = ctrl; msg.msg_controllen = sizeof(ctrl);
    ssize_t r = recvmsg(cl->fd, &msg, MSG_DONTWAIT);
    if (r == 0) return -1;
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        return -1;
    }
    if (cl->rlen + (size_t)r > sizeof(cl->rbuf)) return -1;
    memcpy(cl->rbuf + cl->rlen, buf, (size_t)r);
    cl->rlen += (size_t)r;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            int nf = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (int i = 0; i < nf && cl->n_fdq < 8; i++) {
                int fd;
                memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
                cl->fdq[cl->n_fdq++] = fd;
            }
        }
    }
    return 1;
}

/* Try to produce one event from buffered bytes. */
static int cl_parse_one(bd_wm_client *cl, bd_wm_client_event *ev)
{
    for (;;) {
        if (cl->rlen < sizeof(bd_wm_hdr)) return 0;
        bd_wm_hdr hdr;
        memcpy(&hdr, cl->rbuf, sizeof(hdr));
        if (hdr.magic != BD_WM_MAGIC) return -1;
        if (cl->rlen < sizeof(hdr) + hdr.len) return 0;
        int rc = cl_handle(cl, hdr.type, cl->rbuf + sizeof(hdr), hdr.len, ev);
        size_t used = sizeof(hdr) + hdr.len;
        memmove(cl->rbuf, cl->rbuf + used, cl->rlen - used);
        cl->rlen -= used;
        if (rc < 0) return -1;
        if (rc == 1) return 1;
        /* internal message consumed; keep parsing */
    }
}

bd_wm_client *bd_wm_client_open(const char *sock_path, const char *title,
                                int req_w, int req_h)
{
    return bd_wm_client_open_ex(sock_path, title, req_w, req_h, 0, 0, 0);
}

bd_wm_client *bd_wm_client_open_ex(const char *sock_path, const char *title,
                                   int req_w, int req_h,
                                   int min_w, int min_h, uint32_t flags)
{
    const char *path = sock_path ? sock_path : bd_wm_default_path();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_un un = {0};
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&un, sizeof(un)) < 0) {
        close(fd);
        return NULL;
    }
    bd_wm_client *cl = calloc(1, sizeof(*cl));
    if (!cl) { close(fd); return NULL; }
    cl->fd = fd;
    cl->fds[0] = cl->fds[1] = -1;
    cl->back = -1;
    cl->canvas_buf = -1;
    bd_wm_hello hello = {0};
    snprintf(hello.title, sizeof(hello.title), "%s", title ? title : "app");
    hello.req_w = req_w; hello.req_h = req_h;
    hello.min_w = min_w; hello.min_h = min_h;
    hello.flags = flags;
    if (cl_send(cl, BD_WM_HELLO, &hello, sizeof(hello)) < 0) goto fail;
    /* wait for GEOM (with fds) */
    for (;;) {
        bd_wm_client_event ev;
        int rc = cl_parse_one(cl, &ev);
        if (rc < 0) goto fail;
        if (cl->win_id != 0) break;
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 5000) <= 0) goto fail;
        if (cl_fill(cl) < 0) goto fail;
    }
    return cl;
fail:
    close(fd);
    free(cl);
    return NULL;
}

void bd_wm_client_close(bd_wm_client *cl)
{
    if (!cl) return;
    if (!cl->dead) cl_send(cl, BD_WM_BYE, NULL, 0);
    drop_buffers(cl);
    close(cl->fd);
    free(cl);
}

bool bd_wm_client_dead(const bd_wm_client *cl) { return cl->dead; }
uint32_t bd_wm_client_id(const bd_wm_client *cl) { return cl->win_id; }
int bd_wm_client_width(const bd_wm_client *cl) { return cl->w; }
int bd_wm_client_height(const bd_wm_client *cl) { return cl->h; }
bool bd_wm_client_focused(const bd_wm_client *cl) { return cl->focused; }

tk_canvas *bd_wm_client_canvas(bd_wm_client *cl)
{
    if (cl->back < 0 || !cl->px[cl->back]) return NULL;
    if (cl->canvas && cl->canvas_buf == cl->back &&
        tk_canvas_width(cl->canvas) == cl->w &&
        tk_canvas_height(cl->canvas) == cl->h)
        return cl->canvas;
    if (cl->canvas) tk_canvas_free(cl->canvas);
    cl->canvas = tk_canvas_wrap(cl->w, cl->h, cl->px[cl->back]);
    cl->canvas_buf = cl->back;
    return cl->canvas;
}

uint32_t *bd_wm_client_pixels(bd_wm_client *cl, int *stride)
{
    if (cl->back < 0 || !cl->px[cl->back]) return NULL;
    if (stride) *stride = cl->w;
    return cl->px[cl->back];
}

int bd_wm_client_commit(bd_wm_client *cl, const tk_rect *damage, int nrects)
{
    if (nrects < 1 || !damage) return -1; /* damage list is mandatory */
    if (cl->back < 0) return -1;
    int buf = cl->back;
    size_t len = sizeof(bd_wm_commit) + (size_t)nrects * sizeof(tk_rect);
    char *payload = malloc(len);
    if (!payload) return -1;
    bd_wm_commit c = { cl->gen, buf, nrects };
    memcpy(payload, &c, sizeof(c));
    memcpy(payload + sizeof(c), damage, (size_t)nrects * sizeof(tk_rect));
    int rc = cl_send(cl, BD_WM_COMMIT, payload, (uint32_t)len);
    free(payload);
    if (rc < 0) { cl->dead = true; return -1; }
    cl->buf_state[buf] = BUF_SENT;
    cl->back = -1;
    for (int i = 0; i < 2; i++)
        if (cl->buf_state[i] == BUF_FREE) { cl->back = i; break; }
    if (cl->canvas && cl->canvas_buf == buf) {
        /* the wrap canvas pointed at the buffer we just gave away;
         * it is rebuilt lazily for the new back buffer */
        tk_canvas_free(cl->canvas);
        cl->canvas = NULL;
        cl->canvas_buf = -1;
    }
    return 0;
}

int bd_wm_client_poll(bd_wm_client *cl, bd_wm_client_event *ev, int timeout_ms)
{
    if (cl->dead) return -1;
    for (;;) {
        int rc = cl_parse_one(cl, ev);
        if (rc != 0) {
            if (rc < 0) cl->dead = true;
            return rc;
        }
        struct pollfd pfd = { cl->fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr == 0) return 0;
        if (pr < 0) {
            if (errno == EINTR) continue;
            cl->dead = true;
            return -1;
        }
        int fr = cl_fill(cl);
        if (fr < 0) { cl->dead = true; return -1; }
        timeout_ms = 0; /* after first wait, drain without blocking */
    }
}

void bd_wm_client_set_title(bd_wm_client *cl, const char *title)
{
    bd_wm_title t = {0};
    snprintf(t.title, sizeof(t.title), "%s", title ? title : "");
    cl_send(cl, BD_WM_TITLE, &t, sizeof(t));
}

int bd_wm_client_im_notify(bd_wm_client *cl, bool active,
                           const tk_rect *field)
{
    bd_wm_im im = {0};
    im.active = active ? 1 : 0;
    if (active && field) {
        im.x = field->x; im.y = field->y;
        im.w = field->w; im.h = field->h;
    }
    if (cl_send(cl, BD_WM_IM, &im, sizeof(im)) < 0) {
        cl->dead = true;
        return -1;
    }
    cl->im_valid = true;
    cl->im_active = active;
    cl->im_rect = (active && field) ? *field : (tk_rect){ 0, 0, 0, 0 };
    return 0;
}

bool bd_wm_client_im_sync(bd_wm_client *cl, tk_root *root)
{
    tk_widget *fw = root ? tk_focused(root) : NULL;
    tk_widget_kind k = fw ? tk_widget_kind_of(fw) : TK_W_ROOT;
    bool active = k == TK_W_TEXTFIELD || k == TK_W_TEXTVIEW;
    tk_rect rect = { 0, 0, 0, 0 };
    if (active) rect = tk_widget_rect(fw);
    if (cl->im_valid && cl->im_active == active &&
        cl->im_rect.x == rect.x && cl->im_rect.y == rect.y &&
        cl->im_rect.w == rect.w && cl->im_rect.h == rect.h)
        return false;
    return bd_wm_client_im_notify(cl, active, active ? &rect : NULL) == 0;
}
