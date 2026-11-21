/* tk_evdev.c - evdev input backend (new implementation).
 * Design follows the audited "keep" parts of the old bui_input:
 * read <linux/input.h> events, scale resistive-touch ABS axes into
 * screen coordinates. All keyboard semantics - JIS/US layouts, the
 * Brain's dedicated keys, modifiers, the single-source repeat
 * policy and long press - live in the keyboard layer (tk_kbd.c);
 * this file only moves raw events between the device and that
 * layer, and merges its time-driven output (tk_kbd_poll) into the
 * event stream using the monotonic clock.
 *
 * tk_evdev_new_virtual() creates a device-less backend so tests
 * and embedders can push synthetic evdev events through the exact
 * production path (tk_evdev_feed_key + tk_evdev_poll_at). */
#include "tk.h"
#include "tk_kbd.h"

#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct tk_evdev {
    int fd;              /* -1 for a virtual backend */
    tk_kbd *kbd;
    int raw_max_x, raw_max_y;
    int screen_w, screen_h;
    int cur_x, cur_y;
    bool btn_down;
    bool have_abs_x, have_abs_y;
    /* one raw key event queued by feed_key, consumed by poll_at */
    bool pend_valid;
    int pend_code, pend_value;
    long pend_ms;
};

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static tk_evdev *evdev_alloc(void)
{
    tk_evdev *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->fd = -1;
    e->kbd = tk_kbd_new(NULL);
    if (!e->kbd) { free(e); return NULL; }
    e->raw_max_x = 4095; e->raw_max_y = 4095;
    /* M22: the panel is 854x480 (AUDIT SS20); the setter
     * below still overrides this from the real fb size. */
    e->screen_w = 854; e->screen_h = 480;
    return e;
}

tk_evdev *tk_evdev_open(const char *dev)
{
    if (!dev) dev = "/dev/input/event0";
    tk_evdev *e = evdev_alloc();
    if (!e) return NULL;
    e->fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (e->fd < 0) { tk_evdev_close(e); return NULL; }
    return e;
}

tk_evdev *tk_evdev_new_virtual(void)
{
    return evdev_alloc();
}

void tk_evdev_close(tk_evdev *e)
{
    if (!e) return;
    if (e->fd >= 0) close(e->fd);
    tk_kbd_free(e->kbd);
    free(e);
}

void tk_evdev_set_touch_cal(tk_evdev *e, int raw_max_x, int raw_max_y,
                            int screen_w, int screen_h)
{
    if (raw_max_x > 0) e->raw_max_x = raw_max_x;
    if (raw_max_y > 0) e->raw_max_y = raw_max_y;
    if (screen_w > 0) e->screen_w = screen_w;
    if (screen_h > 0) e->screen_h = screen_h;
}

void tk_evdev_kbd_set_config(tk_evdev *e, const tk_kbd_config *cfg)
{
    tk_kbd_set_config(e->kbd, cfg);
}

void tk_evdev_kbd_config(const tk_evdev *e, tk_kbd_config *out)
{
    tk_kbd_config_of(e->kbd, out);
}

bool tk_evdev_feed_key(tk_evdev *e, int code, int value, long at_ms)
{
    if (e->pend_valid) return false;
    e->pend_valid = true;
    e->pend_code = code;
    e->pend_value = value;
    e->pend_ms = at_ms;
    return true;
}

/* Handle one raw input_event; returns true when *out was filled. */
static bool handle_raw(tk_evdev *e, const struct input_event *ie,
                       long ms, tk_event *out)
{
    if (ie->type == EV_ABS) {
        if (ie->code == ABS_X) {
            e->cur_x = ie->value * e->screen_w / (e->raw_max_x + 1);
            e->have_abs_x = true;
        } else if (ie->code == ABS_Y) {
            e->cur_y = ie->value * e->screen_h / (e->raw_max_y + 1);
            e->have_abs_y = true;
        }
        return false;
    }
    if (ie->type == EV_KEY && ie->code == BTN_TOUCH) {
        memset(out, 0, sizeof(*out));
        out->x = e->cur_x; out->y = e->cur_y;
        if (ie->value) { e->btn_down = true; out->type = TK_EV_POINTER_DOWN; }
        else { e->btn_down = false; out->type = TK_EV_POINTER_UP; }
        return true;
    }
    if (ie->type == EV_KEY)
        return tk_kbd_feed(e->kbd, ie->code, ie->value, ms, out);
    if (ie->type == EV_REL && ie->code == REL_WHEEL) {
        memset(out, 0, sizeof(*out));
        out->type = TK_EV_WHEEL;
        out->x = e->cur_x; out->y = e->cur_y;
        out->wheel_dy = -ie->value;
        return true;
    }
    return false;  /* EV_SYN and others */
}

bool tk_evdev_poll_at(tk_evdev *e, long ms, tk_event *out)
{
    /* 1. a queued synthetic key event (virtual backend) */
    if (e->pend_valid) {
        struct input_event ie;
        memset(&ie, 0, sizeof(ie));
        ie.type = EV_KEY;
        ie.code = (uint16_t)e->pend_code;
        ie.value = e->pend_value;
        e->pend_valid = false;
        if (handle_raw(e, &ie, e->pend_ms, out)) return true;
        /* fell through (modifier etc.): time-driven output may
         * still be due at the queued event's timestamp */
        return tk_kbd_poll(e->kbd, e->pend_ms, out);
    }
    /* 2. real device input */
    if (e->fd >= 0) {
        struct input_event ie;
        for (;;) {
            ssize_t n = read(e->fd, &ie, sizeof(ie));
            if (n != (ssize_t)sizeof(ie)) break;
            if (handle_raw(e, &ie, ms, out)) return true;
        }
    }
    /* 3. time-driven keyboard output (synthetic repeat, long press) */
    return tk_kbd_poll(e->kbd, ms, out);
}

bool tk_evdev_poll(tk_evdev *e, tk_event *out)
{
    return tk_evdev_poll_at(e, now_ms(), out);
}
