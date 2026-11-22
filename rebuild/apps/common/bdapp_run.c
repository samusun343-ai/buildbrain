/* bdapp_run.c - see bdapp_run.h for the design. */
#include "bdapp_run.h"
#include "wm.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct bd_app_display {
    bd_wm_client *wm;   /* mode 1 */
    tk_fb *fb;          /* mode 2 */
    tk_canvas *canvas;  /* fb mode: the app's private canvas */
    tk_evdev *ev;       /* fb mode: the app's own evdev reader */
    int w, h;
};

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

bd_app_display *bd_app_display_open(const char *title,
                                    int req_w, int req_h,
                                    int fb_w, int fb_h,
                                    int *out_w, int *out_h)
{
    return bd_app_display_open_ex(title, req_w, req_h, 0, 0,
                                  fb_w, fb_h, out_w, out_h);
}

bd_app_display *bd_app_display_open_ex(const char *title,
                                       int req_w, int req_h,
                                       int min_w, int min_h,
                                       int fb_w, int fb_h,
                                       int *out_w, int *out_h)
{
    bd_app_display *d = calloc(1, sizeof(*d));
    if (!d) {
        if (out_w) *out_w = fb_w;
        if (out_h) *out_h = fb_h;
        return NULL;
    }
    /* Mode 1: the shell's WM. A NULL return means no server is
     * listening (or it did not answer the handshake in time) -
     * fall through to the historical standalone run. A 0x0
     * request is the explicit maximize intent (contract 2.3),
     * sent as the HELLO flag so it cannot be mistaken for a
     * size. */
    uint32_t flags = (req_w == 0 && req_h == 0)
                     ? BD_WM_HELLO_MAXIMIZE : 0;
    d->wm = bd_wm_client_open_ex(NULL, title, req_w, req_h,
                                 min_w, min_h, flags);
    if (d->wm) {
        d->w = bd_wm_client_width(d->wm);
        d->h = bd_wm_client_height(d->wm);
    } else {
        /* Mode 2/3: fbdev, exactly the old probe. */
        d->fb = tk_fb_open("/dev/fb0");
        if (d->fb) {
            d->w = tk_fb_width(d->fb);
            d->h = tk_fb_height(d->fb);
        } else {
            d->w = fb_w;
            d->h = fb_h;
        }
    }
    if (out_w) *out_w = d->w;
    if (out_h) *out_h = d->h;
    return d;
}

bool bd_app_display_live(const bd_app_display *d)
{
    return d && (d->wm || d->fb);
}

/* ---------------- WM mode ---------------- */

/* Double-buffer damage journal (the same discipline the
 * browser's WM frontend uses): the server composites the
 * front buffer over ANY damaged screen region - a window
 * moving away, the mascot crossing - not just over the rects
 * of the last commit, so each buffer must always hold a
 * complete current image. A buffer that was away for one
 * commit missed exactly that commit's rects; before painting
 * into it again those rects are re-invalidated in the root so
 * they are repainted into it too. `journal` holds the last
 * commit's rect list (bounded: beyond the protocol's 32-rect
 * cap it collapses to the bounding box, which is always safe,
 * just coarser). */
typedef struct {
    tk_rect rects[32];
    int n;
} rect_journal;

static void journal_set(rect_journal *j, const tk_rect *rects, int n)
{
    if (n > 32) {
        tk_rect b = rects[0];
        for (int i = 1; i < n; i++) {
            int x0 = b.x < rects[i].x ? b.x : rects[i].x;
            int y0 = b.y < rects[i].y ? b.y : rects[i].y;
            int x1 = b.x + b.w > rects[i].x + rects[i].w ?
                     b.x + b.w : rects[i].x + rects[i].w;
            int y1 = b.y + b.h > rects[i].y + rects[i].h ?
                     b.y + b.h : rects[i].y + rects[i].h;
            b = (tk_rect){ x0, y0, x1 - x0, y1 - y0 };
        }
        j->rects[0] = b;
        j->n = 1;
        return;
    }
    memcpy(j->rects, rects, (size_t)n * sizeof(tk_rect));
    j->n = n;
}

/* Paint into the client's back buffer and commit what was
 * painted. First frame (and any resize) is a full paint: a
 * fresh buffer generation has no valid content anywhere. */
static void wm_present(bd_app_display *d, tk_root *root,
                       rect_journal *journal, bool full)
{
    tk_canvas *cv = bd_wm_client_canvas(d->wm);
    if (!cv) return; /* both buffers with the server; next poll
                        surfaces the RELEASE as a 0 return, and
                        root damage + journal stay pending */
    const tk_theme *theme = tk_theme_default();
    if (full) {
        tk_root_paint(root, cv, theme);
    } else {
        for (int i = 0; i < journal->n; i++)
            tk_root_invalidate_rect(root, journal->rects[i]);
        tk_root_paint_damage(root, cv, theme);
    }
    size_t n = tk_canvas_damage_count(cv);
    if (!n) { journal->n = 0; return; }
    tk_rect rects[32];
    int nr;
    if (n <= 32) {
        nr = (int)n;
        for (size_t i = 0; i < n; i++) rects[i] = tk_canvas_damage_rect(cv, i);
    } else {
        rects[0] = tk_canvas_damage_bounds(cv);
        nr = 1;
    }
    tk_canvas_damage_clear(cv);
    if (bd_wm_client_commit(d->wm, rects, nr) == 0)
        journal_set(journal, rects, nr);
    else
        journal->n = 0; /* connection dead; the run loop exits */
}

static void wm_run(bd_app_display *d, tk_root *root,
                   bd_app_tick_fn tick, void *tick_ud, int poll_ms)
{
    rect_journal journal = { .n = 0 };
    bool full = true;
    for (;;) {
        wm_present(d, root, &journal, full);
        full = false;
        bd_wm_client_event ev;
        int rc = bd_wm_client_poll(d->wm, &ev, poll_ms);
        if (tick) tick(tick_ud, mono_ms());
        if (rc < 0) break;      /* connection dead */
        if (rc == 0) continue;  /* timeout / internal traffic */
        tk_event tev = {0};
        bool feed = true;
        switch (ev.type) {
        case BD_WM_CEV_CLOSE:
            return;
        case BD_WM_CEV_RESIZED:
            tk_root_layout(root, ev.w, ev.h);
            (void)bd_wm_client_im_sync(d->wm, root);
            journal.n = 0; /* new buffer generation: nothing to
                              re-present, the full paint covers it */
            full = true;
            feed = false;
            break;
        case BD_WM_CEV_COMPOSITION:
            tev.type = TK_EV_COMPOSITION;
            tev.text = ev.comp[0] ? ev.comp : NULL;
            break;
        case BD_WM_CEV_POINTER_DOWN:
            tev.type = TK_EV_POINTER_DOWN; tev.x = ev.x; tev.y = ev.y;
            break;
        case BD_WM_CEV_POINTER_UP:
            tev.type = TK_EV_POINTER_UP; tev.x = ev.x; tev.y = ev.y;
            break;
        case BD_WM_CEV_POINTER_MOVE:
            tev.type = TK_EV_POINTER_MOVE; tev.x = ev.x; tev.y = ev.y;
            break;
        case BD_WM_CEV_KEY_DOWN:
            tev.type = TK_EV_KEY_DOWN;
            tev.key = (tk_key)ev.key; tev.modifiers = ev.mods;
            tev.text = ev.text[0] ? ev.text : NULL;
            break;
        case BD_WM_CEV_KEY_UP:
            tev.type = TK_EV_KEY_UP;
            tev.key = (tk_key)ev.key; tev.modifiers = ev.mods;
            tev.text = ev.text[0] ? ev.text : NULL;
            break;
        default: /* FOCUS_IN / FOCUS_OUT: no widget event, but
                    the IM report below still applies */
            feed = false;
            break;
        }
        if (feed) tk_root_event(root, &tev);
        /* keep the shell's idea of our text-input state current
         * (drives its on-screen keyboard), as every toolkit WM
         * client does */
        if (ev.type == BD_WM_CEV_POINTER_DOWN ||
            ev.type == BD_WM_CEV_POINTER_UP ||
            ev.type == BD_WM_CEV_KEY_DOWN ||
            ev.type == BD_WM_CEV_FOCUS_IN ||
            ev.type == BD_WM_CEV_FOCUS_OUT)
            (void)bd_wm_client_im_sync(d->wm, root);
    }
}

/* ---------------- fbdev mode (the historical run) ---------------- */

static void fb_run(bd_app_display *d, tk_root *root,
                   bd_app_tick_fn tick, void *tick_ud, int poll_ms)
{
    d->canvas = tk_canvas_new(d->w, d->h);
    if (!d->canvas) return;
    d->ev = tk_evdev_open("/dev/input/event0");
    const tk_theme *theme = tk_theme_default();
    bool first = true;
    for (;;) {
        tk_event e;
        if (d->ev) {
            while (tk_evdev_poll(d->ev, &e)) {
                if (e.type == TK_EV_ACTION && e.action == TK_ACTION_HOME)
                    return;
                tk_root_event(root, &e);
            }
        }
        if (tick) tick(tick_ud, mono_ms());
        /* Paint responsibility (DESIGN-layout-contract.md
         * 3.1): the first frame on a fresh canvas is a FULL
         * paint, exactly as in WM mode - coverage of every
         * pixel is the host's explicit act, not a side effect
         * of the initial damage state. */
        if (first) {
            tk_root_paint(root, d->canvas, theme);
            first = false;
        } else {
            tk_root_paint_damage(root, d->canvas, theme);
        }
        tk_fb_flush_damage(d->fb, d->canvas);
        struct timespec ts = { 0, (long)poll_ms * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

void bd_app_display_run(bd_app_display *d, tk_root *root,
                        bd_app_tick_fn tick, void *tick_ud,
                        int poll_ms)
{
    if (!d || !bd_app_display_live(d)) return;
    if (poll_ms <= 0) poll_ms = 20;
    if (d->wm) wm_run(d, root, tick, tick_ud, poll_ms);
    else fb_run(d, root, tick, tick_ud, poll_ms);
}

void bd_app_display_close(bd_app_display *d)
{
    if (!d) return;
    if (d->ev) tk_evdev_close(d->ev);
    if (d->canvas) tk_canvas_free(d->canvas);
    if (d->fb) tk_fb_close(d->fb);
    if (d->wm) bd_wm_client_close(d->wm);
    free(d);
}
