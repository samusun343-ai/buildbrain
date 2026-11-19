/* _GNU_SOURCE first: memfd_create() is declared in <sys/mman.h>
 * only with it, and the ARM compiler in the CI chroot treats an
 * implicit declaration as an error. */
#define _GNU_SOURCE
#include "bui_wm.h"
#include "bui_skin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* ================= client ================= */

struct bd_wm_client {
	int fd;
	bu_canvas *canvas;
	unsigned char *map;
	size_t map_len;
};

static int send_full(int fd, const void *buf, size_t n)
{
	const char *p = buf;
	while (n) {
		ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
		if (w < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		p += w; n -= (size_t)w;
	}
	return 0;
}

static int recv_full(int fd, void *buf, size_t n)
{
	char *p = buf;
	while (n) {
		ssize_t r = recv(fd, p, n, 0);
		if (r < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (r == 0) return -1;
		p += r; n -= (size_t)r;
	}
	return 0;
}

/* Receive a message header + payload; optionally one fd. */
static int recv_msg(int fd, struct wm_hdr *h, void *payload, size_t cap, int *out_fd)
{
	if (out_fd) *out_fd = -1;
	struct iovec iov = { h, sizeof(*h) };
	char ctl[CMSG_SPACE(sizeof(int))];
	struct msghdr mh = { 0 };
	mh.msg_iov = &iov; mh.msg_iovlen = 1;
	mh.msg_control = ctl; mh.msg_controllen = sizeof(ctl);
	ssize_t r;
	do { r = recvmsg(fd, &mh, 0); } while (r < 0 && errno == EINTR);
	if (r != (ssize_t)sizeof(*h)) return -1;
	if (out_fd) {
		struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
		if (cm && cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS)
			memcpy(out_fd, CMSG_DATA(cm), sizeof(int));
	}
	if (h->len > cap) return -1;
	if (h->len && recv_full(fd, payload, h->len) < 0) return -1;
	return 0;
}

static int send_msg(int fd, uint32_t type, const void *payload, uint32_t len, int pass_fd)
{
	struct wm_hdr h = { type, len };
	struct iovec iov[2] = { { &h, sizeof(h) }, { (void *)payload, len } };
	char ctl[CMSG_SPACE(sizeof(int))];
	struct msghdr mh = { 0 };
	mh.msg_iov = iov; mh.msg_iovlen = len ? 2 : 1;
	if (pass_fd >= 0) {
		mh.msg_control = ctl; mh.msg_controllen = sizeof(ctl);
		struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
		cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS;
		cm->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cm), &pass_fd, sizeof(int));
	}
	ssize_t w;
	do { w = sendmsg(fd, &mh, MSG_NOSIGNAL); } while (w < 0 && errno == EINTR);
	return w < 0 ? -1 : 0;
}

bd_wm_client *bd_wm_client_open(const char *sock_path, const char *title,
				int req_w, int req_h)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return NULL;
	struct sockaddr_un un = { 0 };
	un.sun_family = AF_UNIX;
	snprintf(un.sun_path, sizeof(un.sun_path), "%s", sock_path);
	if (connect(fd, (struct sockaddr *)&un, sizeof(un)) < 0) {
		close(fd);
		return NULL;
	}
	struct wm_hello hello = { 0 };
	snprintf(hello.title, sizeof(hello.title), "%s", title ? title : "app");
	hello.w = req_w; hello.h = req_h;
	if (send_msg(fd, WM_HELLO, &hello, sizeof(hello), -1) < 0) {
		close(fd);
		return NULL;
	}
	struct wm_hdr h;
	struct wm_geom geom = { 0, 0 };
	int memfd = -1;
	if (recv_msg(fd, &h, &geom, sizeof(geom), &memfd) < 0 || h.type != WM_GEOM || memfd < 0) {
		close(fd);
		if (memfd >= 0) close(memfd);
		return NULL;
	}
	bd_wm_client *cl = calloc(1, sizeof(*cl));
	cl->fd = fd;
	cl->map_len = (size_t)geom.w * geom.h * 4;
	cl->map = mmap(NULL, cl->map_len, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
	close(memfd);
	if (cl->map == MAP_FAILED) {
		close(fd); free(cl);
		return NULL;
	}
	cl->canvas = bu_canvas_wrap(geom.w, geom.h, (uint32_t *)cl->map);
	if (!cl->canvas) {
		munmap(cl->map, cl->map_len); close(fd); free(cl);
		return NULL;
	}
	return cl;
}

bu_canvas *bd_wm_client_canvas(bd_wm_client *cl) { return cl->canvas; }

void bd_wm_client_commit(bd_wm_client *cl)
{
	send_msg(cl->fd, WM_COMMIT, NULL, 0, -1);
}

int bd_wm_client_next(bd_wm_client *cl, bu_event *ev, int timeout_ms)
{
	struct pollfd pfd = { cl->fd, POLLIN, 0 };
	int r = poll(&pfd, 1, timeout_ms);
	if (r <= 0) return 0;
	struct wm_hdr h;
	struct wm_event we = { 0 };
	if (recv_msg(cl->fd, &h, &we, sizeof(we), NULL) < 0)
		return -1;
	if (h.type == WM_CLOSE)
		return -1;
	if (h.type == WM_EVENT) {
		ev->type = (bu_ev_type)we.type;
		ev->code = we.code;
		ev->value = we.value;
		ev->x = we.x;
		ev->y = we.y;
		return 1;
	}
	return 0;
}

void bd_wm_client_close(bd_wm_client *cl)
{
	if (!cl) return;
	if (cl->canvas) bu_canvas_free(cl->canvas);
	if (cl->map) munmap(cl->map, cl->map_len);
	close(cl->fd);
	free(cl);
}

/* ================= server ================= */

#define WM_MAX_WIN 8
static int wm_chrome_h(void)
{
	int l = bd_skin_get()->layout;
	return l == 1 ? 28 : l == 3 ? 32 : 30;	/* macOS 28 / Win11 32 / others 30 */
}

typedef struct {
	int used;
	int fd;
	int memfd;
	uint32_t *px;
	int w, h;		/* content size */
	int x, y;		/* window position (incl. chrome) */
	char title[64];
	int minimized;
	unsigned z;
} wm_win;

static wm_win wins[WM_MAX_WIN];
static int listen_fd = -1;
static char server_path[108];
static unsigned z_counter = 1;
static int focused_idx = -1;	/* index into wins, -1 = none */
static int drag_idx = -1, drag_dx, drag_dy;
static int capture_idx = -1;	/* window holding pointer capture */
/* Desktop work area (minus panel / top bar); the shell pushes it.
 * Default is the whole 854x480 panel until then. */
static bu_rect workarea = { 0, 0, 854, 480 };

void bd_wm_set_workarea(int x, int y, int w, int h)
{
	if (w > 0 && h > 0)
		workarea = (bu_rect){ x, y, w, h };
}

static bu_rect win_outer(const wm_win *w)
{
	return (bu_rect){ w->x, w->y, w->w, w->h + wm_chrome_h() };
}
static bu_rect win_content(const wm_win *w)
{
	return (bu_rect){ w->x, w->y + wm_chrome_h(), w->w, w->h };
}

/* Keep the whole outer window inside the work area, so a new
 * window never slides under the panel or off the bottom edge
 * and a dragged window stays reachable. A window larger than
 * the work area pins to its origin. */
static void win_clamp(wm_win *w)
{
	bu_rect o = win_outer(w);
	int max_x = workarea.x + workarea.w - o.w;
	int max_y = workarea.y + workarea.h - o.h;
	if (max_x < workarea.x) max_x = workarea.x;
	if (max_y < workarea.y) max_y = workarea.y;
	if (w->x < workarea.x) w->x = workarea.x;
	if (w->x > max_x) w->x = max_x;
	if (w->y < workarea.y) w->y = workarea.y;
	if (w->y > max_y) w->y = max_y;
}

int bd_wm_server_start(const char *sock_path)
{
	if (!sock_path) {
		sock_path = "/run/braindesk/wm.sock";
		/* /run/braindesk may not exist on the host; fall back */
		if (access("/run/braindesk", F_OK) < 0)
			sock_path = "/tmp/braindesk-wm.sock";
	}
	snprintf(server_path, sizeof(server_path), "%s", sock_path);
	char dir[108];
	snprintf(dir, sizeof(dir), "%s", sock_path);
	char *slash = strrchr(dir, '/');
	if (slash) { *slash = 0; mkdir(dir, 0755); }
	unlink(server_path);
	listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0) return -1;
	struct sockaddr_un un = { 0 };
	un.sun_family = AF_UNIX;
	snprintf(un.sun_path, sizeof(un.sun_path), "%s", server_path);
	if (bind(listen_fd, (struct sockaddr *)&un, sizeof(un)) < 0 ||
	    listen(listen_fd, 8) < 0) {
		close(listen_fd); listen_fd = -1;
		return -1;
	}
	fcntl(listen_fd, F_SETFL, O_NONBLOCK);
	setenv("BRAINDESK_WM", server_path, 1);
	return 0;
}

const char *bd_wm_server_path(void) { return server_path; }

static void win_remove(int i)
{
	close(wins[i].fd);
	close(wins[i].memfd);
	munmap(wins[i].px, (size_t)wins[i].w * wins[i].h * 4);
	memset(&wins[i], 0, sizeof(wins[i]));
	if (focused_idx == i) focused_idx = -1;
	if (drag_idx == i) drag_idx = -1;
	if (capture_idx == i) capture_idx = -1;
}

static int win_top(void) /* index of the topmost visible window */
{
	int best = -1;
	for (int i = 0; i < WM_MAX_WIN; i++)
		if (wins[i].used && !wins[i].minimized &&
		    (best < 0 || wins[i].z > wins[best].z))
			best = i;
	return best;
}

static void win_raise(int i)
{
	wins[i].z = ++z_counter;
	wins[i].minimized = 0;
	focused_idx = i;
}

int bd_wm_poll(void)
{
	int changed = 0;
	if (listen_fd < 0) return 0;
	/* accept */
	for (;;) {
		int fd = accept(listen_fd, NULL, NULL);
		if (fd < 0) break;
		struct wm_hdr h;
		struct wm_hello hello = { 0 };
		if (recv_msg(fd, &h, &hello, sizeof(hello), NULL) < 0 || h.type != WM_HELLO) {
			close(fd);
			continue;
		}
		int slot = -1;
		for (int i = 0; i < WM_MAX_WIN; i++)
			if (!wins[i].used) { slot = i; break; }
		if (slot < 0) { close(fd); continue; }
		wm_win *w = &wins[slot];
		memset(w, 0, sizeof(*w));
		w->w = hello.w > 80 ? hello.w : 480;
		w->h = hello.h > 60 ? hello.h : 320;
		if (w->w > 800) w->w = 800;
		if (w->h > 420) w->h = 420;
		int n = 0;
		for (int i = 0; i < WM_MAX_WIN; i++) if (wins[i].used) n++;
		w->x = 56 + (n % 5) * 38;
		w->y = 44 + (n % 5) * 30;
		/* keep the whole window inside the work area */
		if (w->h + wm_chrome_h() > 480 - 52)
			w->h = 480 - 52 - wm_chrome_h();
		{
			int maxx = 854 - w->w, maxy = 480 - 44 - (w->h + wm_chrome_h());
			if (maxx < 0) maxx = 0;
			if (maxy < 0) maxy = 0;
			if (w->x > maxx) w->x = maxx;
			if (w->y > maxy) w->y = maxy;
		}
		win_clamp(w);
		snprintf(w->title, sizeof(w->title), "%s", hello.title);
		w->memfd = memfd_create("bdwm", 0);
		if (w->memfd < 0 || ftruncate(w->memfd, (off_t)w->w * w->h * 4) < 0) {
			close(fd);
			if (w->memfd >= 0) close(w->memfd);
			continue;
		}
		w->px = mmap(NULL, (size_t)w->w * w->h * 4, PROT_READ | PROT_WRITE,
			     MAP_SHARED, w->memfd, 0);
		if (w->px == MAP_FAILED) {
			close(fd); close(w->memfd);
			continue;
		}
		struct wm_geom geom = { w->w, w->h };
		if (send_msg(fd, WM_GEOM, &geom, sizeof(geom), w->memfd) < 0) {
			munmap(w->px, (size_t)w->w * w->h * 4);
			close(fd); close(w->memfd);
			continue;
		}
		w->fd = fd;
		w->used = 1;
		fcntl(fd, F_SETFL, O_NONBLOCK);
		win_raise(slot);
		changed = 1;
	}
	/* client messages */
	for (int i = 0; i < WM_MAX_WIN; i++) {
		if (!wins[i].used) continue;
		struct pollfd pfd = { wins[i].fd, POLLIN, 0 };
		while (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
			struct wm_hdr h;
			char payload[64];
			int got_fd = -1;
			if (recv_msg(wins[i].fd, &h, payload,
				     sizeof(payload), &got_fd) < 0) {
				win_remove(i);
				changed = 1;
				break;
			}
			if (got_fd >= 0) close(got_fd);
			if (h.type == WM_COMMIT)
				changed = 1;
		}
		if (wins[i].used && (pfd.revents & POLLHUP)) {
			win_remove(i);
			changed = 1;
		}
	}
	return changed;
}

int bd_wm_window_count(void)
{
	int n = 0;
	for (int i = 0; i < WM_MAX_WIN; i++) if (wins[i].used) n++;
	return n;
}

int bd_wm_focused(void) { return focused_idx >= 0 && wins[focused_idx].used; }

/* ---------- drawing ---------- */

/* chrome buttons, shared by draw + hit: returns rects for the
 * active skin family. kind: 0=close, 1=min */
static bu_rect chrome_btn(const wm_win *w, int kind, int layout)
{
	bu_rect o = win_outer(w);
	if (layout == 1 /* MAC */) {
		/* traffic lights: red close, yellow min (12px, centres 18/38) */
		return (bu_rect){ o.x + 11 + kind * 20, o.y + wm_chrome_h() / 2 - 7, 14, 14 };
	}
	/* Windows/BD families: right side caption buttons */
	if (kind == 0)
		return (bu_rect){ o.x + o.w - 40, o.y + 4, 34, 22 };
	return (bu_rect){ o.x + o.w - 78, o.y + 4, 34, 22 };
}

void bd_wm_draw(bu_canvas *c, bu_theme *t)
{
	const bd_skin *sk = bd_skin_get();
	int layout = sk->layout; /* 1 = MAC per bui_skin.h */
	/* draw in z order, bottom first */
	int order[WM_MAX_WIN], n = 0;
	for (int i = 0; i < WM_MAX_WIN; i++)
		if (wins[i].used && !wins[i].minimized) order[n++] = i;
	for (int a = 0; a < n; a++)
		for (int b = a + 1; b < n; b++)
			if (wins[order[b]].z < wins[order[a]].z) {
				int tmp = order[a]; order[a] = order[b]; order[b] = tmp;
			}
	for (int oi = 0; oi < n; oi++) {
		wm_win *w = &wins[order[oi]];
		bu_rect o = win_outer(w);
		bool active = (order[oi] == win_top());
		bu_shadow_round_rect(c, o, 8, 10, BU_RGBA(0, 0, 0, active ? 130 : 90));
		/* content first (client pixels, opaque) */
		bu_rect ct = win_content(w);
		for (int y = 0; y < w->h; y++) {
			if (ct.y + y < c->clip.y || ct.y + y >= c->clip.y + c->clip.h)
				continue;
			uint32_t *dst = c->px + (ct.y + y) * c->w + ct.x;
			const uint32_t *src = w->px + y * w->w;
			int x0 = 0, xn = w->w;
			if (ct.x < c->clip.x) x0 = c->clip.x - ct.x;
			if (ct.x + xn > c->clip.x + c->clip.w) xn = c->clip.x + c->clip.w - ct.x;
			for (int x = x0; x < xn; x++) dst[x] = src[x] | 0xFF000000u;
		}
		/* chrome */
		bu_rect bar = { o.x, o.y, o.w, wm_chrome_h() };
		bu_color bc = active ? t->surface2 : t->surface3;
		if (sk->chrome == 2 /* LUNA */ && active) bc = t->accent;
		bu_fill_round_rect(c, (bu_rect){ o.x, o.y, o.w, wm_chrome_h() + 8 }, 8, bc);
		bu_fill_rect(c, (bu_rect){ o.x, o.y + wm_chrome_h() - 1, o.w, 1 }, t->line);
		bu_color ink = (sk->chrome == 2 && active) ? BU_RGB(255, 255, 255)
				   : active ? t->text : t->text_dim;
		if (layout == 1) {
			int lcy = o.y + wm_chrome_h() / 2;
			bu_fill_circle(c, o.x + 18, lcy, 6, BU_RGB(255, 95, 87));
			bu_fill_circle(c, o.x + 38, lcy, 6, BU_RGB(254, 188, 46));
			bu_fill_circle(c, o.x + 58, lcy, 6, BU_RGB(40, 200, 64));
			bu_draw_text_centered(c, t->font, (bu_rect){ o.x, o.y + 6, o.w, 18 },
					      w->title, 12.5, ink);
		} else {
			bu_draw_text(c, t->font, o.x + 12, o.y + 7, w->title, 12.5, ink);
			bu_rect cb = chrome_btn(w, 0, layout);
			if (sk->chrome == 2 && active)
				bu_fill_round_rect(c, cb, 3, BU_RGB(232, 108, 84));
			/* close X */
			bu_line(c, cb.x + 12, cb.y + 6, cb.x + 22, cb.y + 16, 2, ink);
			bu_line(c, cb.x + 22, cb.y + 6, cb.x + 12, cb.y + 16, 2, ink);
			bu_rect mb = chrome_btn(w, 1, layout);
			bu_line(c, mb.x + 12, mb.y + 16, mb.x + 22, mb.y + 16, 2, ink);
		}
	}
}

/* task buttons: slots for windows in creation order */
/* Task buttons are sized to their content (title text + padding),
 * like XP/7 task buttons — a fixed wide slot wastes panel space.
 * Win11 uses fixed 40 px icon buttons in the centred cluster. */
static bu_font *task_font;
static int task_x[WM_MAX_WIN], task_w[WM_MAX_WIN], task_n;

static void task_layout(int x0, int x1, int layout)
{
	int cx = x0;
	task_n = 0;
	for (int i = 0; i < WM_MAX_WIN; i++) {
		if (!wins[i].used) continue;
		int bw;
		if (layout == 3 /* WIN11 */) {
			bw = 40;
		} else {
			int tw = task_font ? bu_text_width(task_font, wins[i].title, 11.5f) : 40;
			bw = tw + 22;
			if (bw < 60) bw = 60;
			if (bw > 148) bw = 148;
		}
		if (cx + bw > x1) break;
		task_x[task_n] = cx;
		task_w[task_n] = bw;
		task_n++;
		cx += bw + 4;
	}
}

static bu_rect task_rect(int slot, int x0, int x1, int py, int ph)
{
	(void)x0; (void)x1;
	if (slot >= task_n) return (bu_rect){ 0, -100, 0, 0 };
	return (bu_rect){ task_x[slot], py + 4, task_w[slot], ph - 8 };
}

void bd_wm_draw_tasks(bu_canvas *c, bu_theme *t, int x0, int x1, int py, int ph)
{
	task_font = t->font;
	int layout = bd_skin_get()->layout;
	task_layout(x0, x1, layout);
	int slot = 0;
	for (int i = 0; i < WM_MAX_WIN; i++) {
		if (!wins[i].used || slot >= task_n) continue;
		bu_rect r = task_rect(slot, x0, x1, py, ph);
		bool active = (i == win_top());
		if (layout == 3 /* WIN11: icon button + accent pill */) {
			if (active)
				bu_fill_round_rect(c, r, 6, t->surface2);
			char initial[5];
			int n = 0;
			while (wins[i].title[n] && n < 4 &&
			       (wins[i].title[n] & 0xC0) != 0x80)
				initial[n] = wins[i].title[n], n++;
			initial[n] = 0;
			bu_draw_text_centered(c, t->font,
				(bu_rect){ r.x, r.y + 2, r.w, r.h - 8 },
				initial, 13, active ? t->text : t->text_dim);
			bu_fill_round_rect(c,
				(bu_rect){ r.x + r.w / 2 - 8, r.y + r.h - 4, 16, 3 },
				1, active ? t->accent : t->text_faint);
		} else {
			bu_fill_round_rect(c, r, bd_skin_get()->chrome == 2 ? 3 : 6,
					   active ? t->surface3 : t->surface2);
			bu_draw_text(c, t->font, r.x + 11, r.y + (r.h - 12) / 2,
				     wins[i].title, 11.5,
				     active ? t->text : t->text_dim);
		}
		slot++;
	}
}

int bd_wm_task_hit(int x, int y, int x0, int x1, int py, int ph)
{
	task_layout(x0, x1, bd_skin_get()->layout);
	int slot = 0;
	for (int i = 0; i < WM_MAX_WIN; i++) {
		if (!wins[i].used || slot >= task_n) continue;
		bu_rect r = task_rect(slot, x0, x1, py, ph);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
			return i;
		slot++;
	}
	return -1;
}

void bd_wm_raise_task(int idx)
{
	if (idx >= 0 && idx < WM_MAX_WIN && wins[idx].used)
		win_raise(idx);
}

/* ---------- input routing ---------- */

static void win_send_event(wm_win *w, int type, int code, int value, int x, int y)
{
	struct wm_event we = { type, code, value, x, y };
	send_msg(w->fd, WM_EVENT, &we, sizeof(we), -1);
}

static int in_rect(bu_rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int bd_wm_pointer(int type, int x, int y)
{
	const bd_skin *sk = bd_skin_get();
	int layout = sk->layout;
	if (type == BU_EV_POINTER_MOVE) {
		if (drag_idx >= 0) {
			wins[drag_idx].x = x - drag_dx;
			wins[drag_idx].y = y - drag_dy;
			win_clamp(&wins[drag_idx]);
			return 1;
		}
		if (capture_idx >= 0) {
			bu_rect ct = win_content(&wins[capture_idx]);
			win_send_event(&wins[capture_idx], type, 0, 0, x - ct.x, y - ct.y);
			return 1;
		}
		return 0;
	}
	if (type == BU_EV_POINTER_UP) {
		if (drag_idx >= 0) { drag_idx = -1; return 1; }
		if (capture_idx >= 0) {
			bu_rect ct = win_content(&wins[capture_idx]);
			win_send_event(&wins[capture_idx], type, 0, 0, x - ct.x, y - ct.y);
			capture_idx = -1;
			return 1;
		}
		return 0;
	}
	if (type != BU_EV_POINTER_DOWN)
		return 0;
	/* topmost first */
	int order[WM_MAX_WIN], n = 0;
	for (int i = 0; i < WM_MAX_WIN; i++)
		if (wins[i].used && !wins[i].minimized) order[n++] = i;
	for (int a = 0; a < n; a++)
		for (int b = a + 1; b < n; b++)
			if (wins[order[b]].z > wins[order[a]].z) {
				int tmp = order[a]; order[a] = order[b]; order[b] = tmp;
			}
	for (int oi = 0; oi < n; oi++) {
		wm_win *w = &wins[order[oi]];
		bu_rect o = win_outer(w);
		if (!in_rect(o, x, y)) continue;
		win_raise(order[oi]);
		if (y < o.y + wm_chrome_h()) {
			if (in_rect(chrome_btn(w, 0, layout), x, y)) {
				send_msg(w->fd, WM_CLOSE, NULL, 0, -1);
				return 1;
			}
			if (in_rect(chrome_btn(w, 1, layout), x, y)) {
				w->minimized = 1;
				focused_idx = win_top();
				return 1;
			}
			if (layout == 1 && x >= o.x + 40 && x < o.x + 62 &&
			    y >= o.y + 8 && y < o.y + 22)
				return 1; /* green light: no-op for now */
			drag_idx = order[oi];
			drag_dx = x - w->x;
			drag_dy = y - w->y;
			return 1;
		}
		bu_rect ct = win_content(w);
		capture_idx = order[oi];
		win_send_event(w, type, 0, 0, x - ct.x, y - ct.y);
		return 1;
	}
	return 0;
}

int bd_wm_key(int code, int value)
{
	if (!bd_wm_focused()) return 0;
	wm_win *w = &wins[focused_idx];
	if (w->minimized) return 0;
	win_send_event(w, BU_EV_KEY, code, value, 0, 0);
	return 1;
}
