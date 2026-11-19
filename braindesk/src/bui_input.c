#include "bui_input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <dirent.h>
#include <linux/input.h>

#define MAX_DEV 16

struct bu_input {
	struct pollfd fds[MAX_DEV];
	int n;
	int screen_w, screen_h;
	/* per-device abs ranges, indexed like fds */
	int minx[MAX_DEV], maxx[MAX_DEV], miny[MAX_DEV], maxy[MAX_DEV];
	bool is_touch[MAX_DEV];
	/* current pointer state */
	int px, py;
	bool down;
	int cur_x, cur_y;
	bool have_x, have_y;
};

bu_input *bu_input_open(int screen_w, int screen_h)
{
	bu_input *in = calloc(1, sizeof(*in));
	in->screen_w = screen_w;
	in->screen_h = screen_h;
	DIR *dir = opendir("/dev/input");
	if (!dir) return in;
	struct dirent *e;
	while ((e = readdir(dir)) != NULL && in->n < MAX_DEV) {
		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		char path[300];
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		int i = in->n;
		in->fds[i].fd = fd;
		in->fds[i].events = POLLIN;
		in->minx[i] = 0; in->maxx[i] = screen_w - 1;
		in->miny[i] = 0; in->maxy[i] = screen_h - 1;
		struct input_absinfo ai;
		if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0) {
			in->minx[i] = ai.minimum; in->maxx[i] = ai.maximum;
			in->is_touch[i] = true;
		}
		if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0) {
			in->miny[i] = ai.minimum; in->maxy[i] = ai.maximum;
		}
		in->n++;
	}
	closedir(dir);
	return in;
}

void bu_input_close(bu_input *in)
{
	if (!in) return;
	for (int i = 0; i < in->n; i++)
		close(in->fds[i].fd);
	free(in);
}

static int scale(int v, int mn, int mx, int out)
{
	if (mx <= mn) return 0;
	int s = (v - mn) * out / (mx - mn);
	if (s < 0) s = 0;
	if (s >= out) s = out - 1;
	return s;
}

bool bu_input_next(bu_input *in, bu_event *ev, int timeout_ms)
{
	if (in->n == 0) {
		poll(NULL, 0, timeout_ms);
		return false;
	}
	int r = poll(in->fds, in->n, timeout_ms);
	if (r <= 0) return false;
	for (int i = 0; i < in->n; i++) {
		if (!(in->fds[i].revents & POLLIN)) continue;
		struct input_event ie;
		ssize_t got = read(in->fds[i].fd, &ie, sizeof(ie));
		if (got != sizeof(ie)) continue;
		if (ie.type == EV_KEY) {
			if (ie.code == BTN_TOUCH || ie.code == BTN_LEFT) {
				in->down = ie.value != 0;
				ev->type = in->down ? BU_EV_POINTER_DOWN : BU_EV_POINTER_UP;
				ev->x = in->px; ev->y = in->py;
				ev->code = ie.code; ev->value = ie.value;
				return true;
			}
			ev->type = BU_EV_KEY;
			ev->code = ie.code;
			ev->value = ie.value;
			ev->x = in->px; ev->y = in->py;
			return true;
		}
		if (ie.type == EV_ABS) {
			if (ie.code == ABS_X || ie.code == ABS_MT_POSITION_X) {
				in->cur_x = scale(ie.value, in->minx[i], in->maxx[i], in->screen_w);
				in->have_x = true;
			} else if (ie.code == ABS_Y || ie.code == ABS_MT_POSITION_Y) {
				in->cur_y = scale(ie.value, in->miny[i], in->maxy[i], in->screen_h);
				in->have_y = true;
			}
			continue;
		}
		if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
			if (in->have_x || in->have_y) {
				in->px = in->cur_x; in->py = in->cur_y;
				in->have_x = in->have_y = false;
				if (in->down) {
					ev->type = BU_EV_POINTER_MOVE;
					ev->x = in->px; ev->y = in->py;
					ev->code = 0; ev->value = 0;
					return true;
				}
			}
			continue;
		}
	}
	return false;
}
