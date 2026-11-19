/* bd-recorder: voice recorder for braindesk (v2).
 * Big record button with a live timer, quality selection
 * (44.1k / 22.05k / 16k), and a recordings list showing each
 * file's duration and size with playback and two-tap delete.
 * If the capture child dies immediately (e.g. the QEMU SAIF
 * overrun), the error is surfaced instead of faking success.
 */
#include "bdapp.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

#define MAXREC 64

struct rec {
	char name[128];
	int duration;
	off_t size;
};

struct recorder {
	char dir[512];
	struct rec recs[MAXREC];
	int n, scroll;
	pid_t pid;
	time_t started;
	int rate;		/* 44100 / 22050 / 16000 */
	pid_t play_pid;
	int play_idx;
	bool confirm_delete;
	int del_idx;
	char status[128];
};

static int wav_duration(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return -1;
	unsigned char hdr[44];
	if (fread(hdr, 1, 44, fp) < 44) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	if (memcmp(hdr, "RIFF", 4) != 0)
		return -1;
	unsigned rate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | ((unsigned)hdr[27] << 24);
	unsigned data = hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | ((unsigned)hdr[43] << 24);
	unsigned ch = hdr[22] | (hdr[23] << 8);
	unsigned bits = hdr[34] | (hdr[35] << 8);
	if (!rate || !ch || !bits)
		return -1;
	return (int)(data / (rate * ch * (bits / 8)));
}

static void load_list(struct recorder *rc)
{
	DIR *d = opendir(rc->dir);
	rc->n = 0;
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && rc->n < MAXREC) {
		const char *dot = strrchr(de->d_name, '.');
		if (!dot || strcasecmp(dot, ".wav") != 0)
			continue;
		snprintf(rc->recs[rc->n].name, 128, "%s", de->d_name);
		char full[640];
		snprintf(full, sizeof(full), "%s/%s", rc->dir, de->d_name);
		struct stat st;
		rc->recs[rc->n].size = stat(full, &st) == 0 ? st.st_size : 0;
		rc->recs[rc->n].duration = wav_duration(full);
		rc->n++;
	}
	closedir(d);
	for (int i = 0; i < rc->n; i++)
		for (int j = i + 1; j < rc->n; j++)
			if (strcmp(rc->recs[j].name, rc->recs[i].name) > 0) {
				struct rec tmp = rc->recs[i];
				rc->recs[i] = rc->recs[j];
				rc->recs[j] = tmp;
			}
}

static void stop_playback(struct recorder *rc)
{
	if (rc->play_pid > 0) {
		kill(rc->play_pid, SIGTERM);
		waitpid(rc->play_pid, NULL, 0);
		rc->play_pid = -1;
	}
	rc->play_idx = -1;
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct recorder *rc = ctx;
	if (rc->pid > 0) {
		int status;
		pid_t r = waitpid(rc->pid, &status, WNOHANG);
		if (r == rc->pid) {
			rc->pid = -1;
			if (time(NULL) - rc->started < 2)
				snprintf(rc->status, sizeof(rc->status), "録音デバイスでエラーが発生しました");
			else {
				snprintf(rc->status, sizeof(rc->status), "保存しました");
				load_list(rc);
			}
			*changed = true;
		} else {
			*changed = true; /* live timer */
		}
	}
	if (rc->play_pid > 0) {
		int status;
		if (waitpid(rc->play_pid, &status, WNOHANG) == rc->play_pid) {
			rc->play_pid = -1;
			rc->play_idx = -1;
			*changed = true;
		}
	}
}

static void draw(bdapp *a, void *ctx)
{
	struct recorder *rc = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	bool recording = rc->pid > 0;
	/* record card */
	bu_rect card = { 16, 54, 832, 128 };
	bu_card(c, t, card);
	/* big round button */
	int bcx = card.x + 70, bcy = card.y + 64;
	bu_fill_circle(c, bcx, bcy, 34, recording ? t->danger : t->surface3);
	bu_fill_circle(c, bcx, bcy, 26, recording ? BU_RGB(255, 255, 255) : t->danger);
	if (recording) {
		int el = (int)(time(NULL) - rc->started);
		char tb[32];
		snprintf(tb, sizeof(tb), "%d:%02d", el / 60, el % 60);
		bu_draw_text(c, t->font, card.x + 130, card.y + 22, "録音中", 18, t->danger);
		bu_draw_text(c, t->font, card.x + 130, card.y + 56, tb, 26, t->text);
		bu_draw_text(c, t->font, card.x + 130, card.y + 96, "ボタンを押すと停止します", 12, t->text_dim);
	} else {
		bu_draw_text(c, t->font, card.x + 130, card.y + 22, "ボイスレコーダー", 18, t->text);
		bu_draw_text(c, t->font, card.x + 130, card.y + 56, "赤いボタンで録音開始", 13, t->text_dim);
		/* quality selector */
		static const int rates[] = { 44100, 22050, 16000 };
		static const char *qlab[] = { "高音質 44.1k", "標準 22k", "長時間 16k" };
		for (int i = 0; i < 3; i++)
			bu_button(c, t, (bu_rect){ card.x + 380 + i * 148, card.y + 40, 140, 42 }, qlab[i],
				  rc->rate == rates[i] ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	}
	if (rc->status[0])
		bu_draw_text(c, t->font, card.x + 130, card.y + 100, rc->status, 12, t->warn);
	/* list */
	bu_draw_text(c, t->font, 16, 196, "録音一覧", 14, t->text_dim);
	int visible = 5;
	for (int vi = 0; vi < visible; vi++) {
		int i = rc->scroll + vi;
		if (i >= rc->n)
			break;
		struct rec *r = &rc->recs[i];
		bu_rect row = { 16, 222 + vi * 44, 700, 40 };
		bu_fill_round_rect(c, row, 8, rc->play_idx == i ? t->surface3 : t->surface);
		bu_icon_mic(c, row.x + 22, row.y + 20, t->text_dim);
		bu_draw_text(c, t->font, row.x + 44, row.y + 10, r->name, 13, t->text);
		char meta[64];
		if (r->duration >= 0)
			snprintf(meta, sizeof(meta), "%d:%02d · %.0f KB", r->duration / 60, r->duration % 60, r->size / 1024.0);
		else
			snprintf(meta, sizeof(meta), "%.0f KB", r->size / 1024.0);
		bu_draw_text_right(c, t->font, (bu_rect){ row.x, row.y + 10, row.w - 130, 20 }, meta, 12, t->text_faint);
		bu_button(c, t, (bu_rect){ 724, 222 + vi * 44, 62, 40 }, rc->play_idx == i ? "■" : "▶", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 792, 222 + vi * 44, 56, 40 },
			  rc->confirm_delete && rc->del_idx == i ? "本当に?" : "削除", BU_BTN_DANGER, false);
	}
	bu_button(c, t, (bu_rect){ 724, 448, 56, 26 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 792, 448, 56, 26 }, "▼", BU_BTN_NORMAL, false);
	if (rc->n == 0)
		bu_draw_text(c, t->font, 16, 240, "まだ録音がありません", 13, t->text_dim);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct recorder *rc = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	/* big record button (circle approximated by its bbox) */
	if (inr(x, y, (bu_rect){ 16 + 70 - 36, 54 + 64 - 36, 72, 72 })) {
		if (rc->pid > 0) {
			kill(rc->pid, SIGINT); /* arecord finalizes the WAV on SIGINT */
		} else {
			stop_playback(rc);
			char name[160], full[640];
			time_t now = time(NULL);
			struct tm tm;
			localtime_r(&now, &tm);
			snprintf(name, sizeof(name), "rec-%04d%02d%02d-%02d%02d%02d.wav",
				 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
			snprintf(full, sizeof(full), "%s/%s", rc->dir, name);
			char rate[16];
			snprintf(rate, sizeof(rate), "%d", rc->rate);
			pid_t pid = fork();
			if (pid == 0) {
				execlp("arecord", "arecord", "-D", "hw:0,1", "-f", "S16_LE", "-c", "2", "-r", rate, "-q", full, (char *)NULL);
				_exit(127);
			}
			rc->pid = pid;
			rc->started = now;
			snprintf(rc->status, sizeof(rc->status), "録音しています…");
		}
		return;
	}
	if (rc->pid <= 0) {
		static const int rates[] = { 44100, 22050, 16000 };
		for (int i = 0; i < 3; i++)
			if (inr(x, y, (bu_rect){ 16 + 380 + i * 148, 54 + 40, 140, 42 })) {
				rc->rate = rates[i];
				return;
			}
	}
	if (inr(x, y, (bu_rect){ 724, 448, 56, 26 })) {
		if (rc->scroll >= 2) rc->scroll -= 2; else rc->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ 792, 448, 56, 26 })) {
		if (rc->scroll + 5 < rc->n) rc->scroll += 2;
		return;
	}
	int visible = 5;
	for (int vi = 0; vi < visible; vi++) {
		int i = rc->scroll + vi;
		if (i >= rc->n)
			break;
		if (inr(x, y, (bu_rect){ 724, 222 + vi * 44, 62, 40 })) {
			if (rc->play_idx == i) {
				stop_playback(rc);
			} else {
				stop_playback(rc);
				char full[640];
				snprintf(full, sizeof(full), "%s/%s", rc->dir, rc->recs[i].name);
				pid_t pid = fork();
				if (pid == 0) {
					execlp("aplay", "aplay", "-q", full, (char *)NULL);
					_exit(127);
				}
				rc->play_pid = pid;
				rc->play_idx = i;
			}
			return;
		}
		if (inr(x, y, (bu_rect){ 792, 222 + vi * 44, 56, 40 })) {
			if (rc->confirm_delete && rc->del_idx == i) {
				char full[640];
				snprintf(full, sizeof(full), "%s/%s", rc->dir, rc->recs[i].name);
				remove(full);
				rc->confirm_delete = false;
				if (rc->play_idx == i)
					stop_playback(rc);
				load_list(rc);
				snprintf(rc->status, sizeof(rc->status), "削除しました");
			} else {
				rc->confirm_delete = true;
				rc->del_idx = i;
			}
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct recorder rc;
	memset(&rc, 0, sizeof(rc));
	rc.pid = -1;
	rc.play_pid = -1;
	rc.play_idx = -1;
	rc.del_idx = -1;
	rc.rate = 44100;
	const char *home = getenv("HOME");
	snprintf(rc.dir, sizeof(rc.dir), "%s/Recordings", home ? home : "/home/user");
	char cmd[560];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", rc.dir);
	if (system(cmd) == 0)
		load_list(&rc);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-recorder.ppm" : NULL;
	return bdapp_run("レコーダー", shot, draw, onevent, tick, &rc);
}
