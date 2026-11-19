/* bd-recorder: voice recorder for braindesk.
 * Records with arecord to ~/Recordings as WAV, lists past
 * recordings with playback (aplay) and two-tap delete. If the
 * capture child dies immediately (e.g. the QEMU SAIF overrun),
 * the error is surfaced instead of pretending to record.
 */
#include "bdapp.h"
#include <dirent.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#define MAXREC 64
#define ROWH 42
#define VISIBLE 6

struct recorder {
	char dir[512];
	char names[MAXREC][256];
	int n, scroll, sel;
	pid_t rec_child;
	pid_t play_child;
	time_t started;
	bool failed;
	bool confirm_delete;
	char status[128];
};

static void load(struct recorder *r)
{
	r->n = 0;
	r->sel = -1;
	r->confirm_delete = false;
	mkdir(r->dir, 0755);
	DIR *d = opendir(r->dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && r->n < MAXREC) {
		size_t l = strlen(de->d_name);
		if (l > 4 && strcasecmp(de->d_name + l - 4, ".wav") == 0)
			snprintf(r->names[r->n++], 256, "%s", de->d_name);
	}
	closedir(d);
	for (int i = 0; i < r->n; i++)
		for (int j = i + 1; j < r->n; j++)
			if (strcmp(r->names[i], r->names[j]) < 0) {
				char tmp[256];
				snprintf(tmp, 256, "%s", r->names[i]);
				snprintf(r->names[i], 256, "%s", r->names[j]);
				snprintf(r->names[j], 256, "%s", tmp);
			}
}

static void start_rec(struct recorder *r)
{
	char name[64];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	snprintf(name, sizeof(name), "rec-%04d%02d%02d-%02d%02d%02d.wav",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	char full[768];
	snprintf(full, sizeof(full), "%s/%s", r->dir, name);
	pid_t pid = fork();
	if (pid == 0) {
		execlp("arecord", "arecord", "-q", "-D", "hw:0,1", "-f", "S16_LE",
		       "-c", "2", "-r", "44100", full, (char *)NULL);
		_exit(127);
	}
	r->rec_child = pid;
	r->started = now;
	r->failed = false;
	snprintf(r->status, sizeof(r->status), "録音中: %s", name);
}

static void stop_child(pid_t *cp)
{
	if (*cp > 0) {
		kill(*cp, SIGINT);
		int st;
		waitpid(*cp, &st, 0);
		*cp = 0;
	}
}

static void draw(bdapp *a, void *ctx)
{
	struct recorder *r = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* record control */
	bool rec = r->rec_child > 0;
	bu_fill_round_rect(c, (bu_rect){ 12, 54, 840, 74 }, 12, t->surface);
	bu_fill_circle(c, 52, 91, 16, rec ? t->danger : t->surface3);
	bu_fill_circle(c, 52, 91, 9, t->danger);
	if (rec) {
		char el[64];
		long s = (long)(time(NULL) - r->started);
		snprintf(el, sizeof(el), "録音中 %ld:%02ld", s / 60, s % 60);
		bu_draw_text(c, t->font, 84, 80, el, 17, t->danger);
		bu_button(c, t, (bu_rect){ 700, 70, 130, 42 }, "停止", BU_BTN_DANGER, false);
	} else {
		bu_draw_text(c, t->font, 84, 80, r->failed ? "録音を開始できませんでした" : "タップで録音開始", 15, t->text_dim);
		bu_button(c, t, (bu_rect){ 700, 70, 130, 42 }, "● 録音", BU_BTN_PRIMARY, false);
	}
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = r->scroll + vi;
		if (i >= r->n)
			break;
		bu_rect row = { 12, 140 + vi * ROWH, 700, ROWH - 4 };
		bu_fill_round_rect(c, row, 8, i == r->sel ? t->surface3 : t->surface);
		bu_icon_mic(c, row.x + 22, row.y + row.h / 2, t->text_dim);
		bu_draw_text(c, t->font, row.x + 44, row.y + 8, r->names[i], 14, t->text);
		char full[768];
		snprintf(full, sizeof(full), "%s/%s", r->dir, r->names[i]);
		struct stat st;
		char meta[32] = "";
		if (stat(full, &st) == 0)
			snprintf(meta, sizeof(meta), "%.0f KB", st.st_size / 1024.0);
		bu_draw_text_right(c, t->font, (bu_rect){ row.x, row.y + 8, row.w - 14, 22 }, meta, 12.5, t->text_dim);
	}
	bu_button(c, t, (bu_rect){ 724, 140, 52, 60 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 724, 332, 52, 60 }, "▼", BU_BTN_NORMAL, false);
	if (r->sel >= 0) {
		bu_button(c, t, (bu_rect){ 12, 480 - 40, 130, 34 }, r->play_child > 0 ? "再生停止" : "再生", BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ 154, 480 - 40, 150, 34 }, r->confirm_delete ? "本当に削除?" : "削除", BU_BTN_DANGER, false);
	}
	if (r->status[0])
		bu_draw_text(c, t->font, 320, 480 - 34, r->status, 12.5, t->text_faint);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct recorder *r = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (inr(x, y, (bu_rect){ 700, 70, 130, 42 })) {
		if (r->rec_child > 0) {
			stop_child(&r->rec_child);
			snprintf(r->status, sizeof(r->status), "保存しました");
			load(r);
		} else {
			start_rec(r);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 724, 140, 52, 60 })) {
		if (r->scroll > 0) r->scroll--;
		return;
	}
	if (inr(x, y, (bu_rect){ 724, 332, 52, 60 })) {
		if (r->scroll + VISIBLE < r->n) r->scroll++;
		return;
	}
	if (r->sel >= 0 && inr(x, y, (bu_rect){ 12, 440, 130, 34 })) {
		if (r->play_child > 0) {
			stop_child(&r->play_child);
		} else {
			char full[768];
			snprintf(full, sizeof(full), "%s/%s", r->dir, r->names[r->sel]);
			pid_t pid = fork();
			if (pid == 0) {
				execlp("aplay", "aplay", "-q", full, (char *)NULL);
				_exit(127);
			}
			r->play_child = pid;
		}
		return;
	}
	if (r->sel >= 0 && inr(x, y, (bu_rect){ 154, 440, 150, 34 })) {
		if (!r->confirm_delete) {
			r->confirm_delete = true;
		} else {
			char full[768];
			snprintf(full, sizeof(full), "%s/%s", r->dir, r->names[r->sel]);
			remove(full);
			load(r);
		}
		return;
	}
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = r->scroll + vi;
		if (i >= r->n)
			break;
		bu_rect row = { 12, 140 + vi * ROWH, 700, ROWH - 4 };
		if (inr(x, y, row)) {
			r->sel = i;
			r->confirm_delete = false;
			return;
		}
	}
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	struct recorder *r = ctx;
	if (r->rec_child > 0) {
		int st;
		pid_t done = waitpid(r->rec_child, &st, WNOHANG);
		if (done == r->rec_child) {
			r->rec_child = 0;
			if (time(NULL) - r->started < 2) {
				r->failed = true;
				snprintf(r->status, sizeof(r->status), "録音デバイスでエラーが発生しました");
			}
			load(r);
		}
		*changed = true;
	}
	if (r->play_child > 0) {
		int st;
		if (waitpid(r->play_child, &st, WNOHANG) == r->play_child) {
			r->play_child = 0;
			*changed = true;
		}
	}
}

int main(int argc, char **argv)
{
	struct recorder r;
	memset(&r, 0, sizeof(r));
	const char *home = getenv("HOME");
	snprintf(r.dir, sizeof(r.dir), "%s/Recordings", home ? home : "/home/user");
	load(&r);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-recorder.ppm" : NULL;
	return bdapp_run("ボイスレコーダー", shot, draw, onevent, tick, &r);
}
