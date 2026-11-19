/* evdev input for libbrainui: keyboard matrix, resistive touch,
 * PSWITCH power button — all arrive as evdev on the device. */
#ifndef BUI_INPUT_H
#define BUI_INPUT_H

#include <stdbool.h>

typedef enum { BU_EV_KEY, BU_EV_POINTER_DOWN, BU_EV_POINTER_UP, BU_EV_POINTER_MOVE } bu_ev_type;

typedef struct {
	bu_ev_type type;
	int code;		/* EV_KEY code for BU_EV_KEY */
	int value;		/* 1 press, 0 release, 2 repeat */
	int x, y;		/* pointer position in panel pixels */
} bu_event;

typedef struct bu_input bu_input;

/* screen_w/h: panel size used to scale absolute touch axes. */
bu_input *bu_input_open(int screen_w, int screen_h);
void bu_input_close(bu_input *in);
/* Wait up to timeout_ms for an event. Returns false on timeout. */
bool bu_input_next(bu_input *in, bu_event *ev, int timeout_ms);
/* Set key autorepeat (EVIOCSREP) on all non-touch devices.
 * period_ms = 0 disables autorepeat. */
void bu_input_set_repeat(bu_input *in, int delay_ms, int period_ms);

#endif
