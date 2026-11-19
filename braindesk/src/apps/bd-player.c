/* bd-player: music player for braindesk.
 * Lists audio files in ~/Music and plays them through the
 * format's standard command-line player (aplay / mpg123 /
 * ogg123 / flac) as a child process. Volume goes to the real
 * ALSA Master via bui_alsa.
 */
#include "bdapp.h"
#include "../bui_alsa.h"
#include <dirent.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define MAXTRACKS 128
#define ROWH 42
#define VISIBLE 6

struct player {
	char dir[512];
	char names[MAXTRACKS][256];
	int n, scroll, sel;
	pid_t child;
	time_t started;
	bool paused;
	float volume;
	bu_alsa *alsa;
	char status[96];
};

static bool has_ext(const char *name, const char *ext)
{
	size_t ln = strlen(name), le = strlen(ext);
	return ln > le && strcasecmp(name + ln - le, ext) == 0;
}

static bool is_audio(const char *name)
{
	return has_ext(name, ".wav") || has_ext(name, ".mp3") ||
	       has_ext(name, ".ogg") || has_ext(name, ".flac");
}

static void load(struct player *p)
{
	p->n = 0;
	p->sel = -1;
	DIR *d = opendir(p->dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && p->n < MAXTRACKS)
		if (is_audio(de->d_name))
			snprintf(p->names[p->n++], 256, "%s", de->d_name);
	closedir(d);
	for (int i = 0; i < p->n; i++)
		for (int j = i + 1; j < p->n; j++)
			if (strcmp(p->names[i], p->names[j]) > 0) {
				char tmp[256];
				snprintf(tmp, 256, "%s", p->names[i]);
				snprintf(p->names[i], 256, "%s", p->names[j]);
				snprintf(p->names[j], 256, "%s", tmp);
			}
}

static void stop_child(struct player *p)
{
	if (p->child > 0) {
		kill(p->child, SIGTERM);
		int st;
		waitpid(p->child, &st, 0);
		p->child = 0;
	}
	p->paused = false;
}

static void play(struct player *p, int idx)
{
	stop_child(p);
	if (idx < 0 || idx >= p->n)
		return;
	p->sel = idx;
	char full[768];
	snprintf(full, sizeof(full), "%s/%s", p->dir, p->names[idx]);
	const char *name = p->names[idx];
	pid_t pid = fork();
	if (pid == 0) {
		if (has_ext(name, ".wav"))
			execlp("aplay", "aplay", "-q", full, (char *)NULL);
		else if (has_ext(name, ".mp3"))
			execlp("mpg123", "mpg123", "-q", full, (char *)NULL);
		else if (has_ext(name, ".ogg"))
			execlp("ogg123", "ogg123", "-q", full, (char *)NULL);
		else if (has_ext(name, ".flac"))
			execlp("flac", "flac", "-dc", full, (char *)NULL);
		_exit(127);
	}
	p->child = pid;
	p->started = time(NULL);
	snprintf(p->status, sizeof(p->status), "再生中");
}

static void draw(bdapp *a, void *ctx)
{
	struct player *p = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	bu_draw_text(c, t->font, 16, 54, p->dir, 12.5, t->text_faint);
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = p->scroll + vi;
		if (i >= p->n)
			break;
		bu_rect r = { 12, 76 + vi * ROWH, 700, ROWH - 4 };
		bool cur = p->child > 0 && i == p->sel;
		bu_fill_round_rect(c, r, 8, cur ? t->surface3 : t->surface);
		bu_icon_music(c, r.x + 22, r.y + r.h / 2, cur ? t->accent : t->text_dim);
		bu_draw_text(c, t->font, r.x + 44, r.y + 8, p->names[i], 14, cur ? t->accent : t->text);
	}
	bu_button(c, t, (bu_rect){ 724, 76, 52, 60 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 724, 268, 52, 60 }, "▼", BU_BTN_NORMAL, false);
	if (p->n == 0)
		bu_draw_text(c, t->font, 16, 90, "音楽ファイルがありません (~/Music に .wav/.mp3/.ogg/.flac を置いてください)", 13.5, t->text_dim);
	/* transport */
	int y = 480 - 96;
	bu_fill_round_rect(c, (bu_rect){ 12, y, 840, 84 }, 12, t->surface);
	const char *now = p->sel >= 0 ? p->names[p->sel] : "—";
	bu_draw_text(c, t->font, 28, y + 8, now, 14, t->text);
	if (p->child > 0) {
		char el[32];
		snprintf(el, sizeof(el), "%ld:%02ld", (long)(time(NULL) - p->started) / 60, (long)(time(NULL) - p->started) % 60);
		bu_draw_text_right(c, t->font, (bu_rect){ 12, y + 8, 700, 22 }, el, 13, t->text_dim);
	}
	bu_button(c, t, (bu_rect){ 28, y + 38, 90, 38 }, "前へ", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 128, y + 38, 110, 38 }, p->child > 0 ? "停止" : "再生", BU_BTN_PRIMARY, false);
	bu_button(c, t, (bu_rect){ 248, y + 38, 90, 38 }, "次へ", BU_BTN_NORMAL, false);
	bu_icon_volume(c, 400, y + 57, t->text_dim);
	bu_slider(c, t, (bu_rect){ 424, y + 44, 220, 26 }, p->volume, false);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d%%", (int)(p->volume * 100 + 0.5f));
	bu_draw_text(c, t->font, 656, y + 46, pct, 13, t->text);
	if (p->status[0])
		bu_draw_text_right(c, t->font, (bu_rect){ 12, y + 44, 820, 22 }, p->status, 12, t->text_faint);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct player *p = ctx;
	if (ev->type == BU_EV_POINTER_DOWN) {
		int x = ev->x, y = ev->y;
		*changed = true;
		int ty = 480 - 96;
		if (inr(x, y, (bu_rect){ 724, 76, 52, 60 })) {
			if (p->scroll > 0) p->scroll--;
			return;
		}
		if (inr(x, y, (bu_rect){ 724, 268, 52, 60 })) {
			if (p->scroll + VISIBLE < p->n) p->scroll++;
			return;
		}
		if (inr(x, y, (bu_rect){ 28, ty + 38, 90, 38 })) {
			if (p->sel > 0) play(p, p->sel - 1);
			return;
		}
		if (inr(x, y, (bu_rect){ 128, ty + 38, 110, 38 })) {
			if (p->child > 0) {
				stop_child(p);
				snprintf(p->status, sizeof(p->status), "停止");
			} else if (p->sel >= 0) {
				play(p, p->sel);
			} else if (p->n > 0) {
				play(p, 0);
			}
			return;
		}
		if (inr(x, y, (bu_rect){ 248, ty + 38, 90, 38 })) {
			if (p->sel + 1 < p->n) play(p, p->sel + 1);
			return;
		}
		bu_rect vs = { 424, ty + 44, 220, 26 };
		if (inr(x, y, vs)) {
			p->volume = (float)(x - vs.x) / vs.w;
			if (p->volume < 0) p->volume = 0;
			if (p->volume > 1) p->volume = 1;
			if (p->alsa)
				bu_alsa_set_volume(p->alsa, p->volume);
			return;
		}
		for (int vi = 0; vi < VISIBLE; vi++) {
			int i = p->scroll + vi;
			if (i >= p->n)
				break;
			bu_rect r = { 12, 76 + vi * ROWH, 700, ROWH - 4 };
			if (inr(x, y, r)) {
				if (p->sel == i && p->child > 0)
					stop_child(p);
				else
					play(p, i);
				return;
			}
		}
	}
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	struct player *p = ctx;
	if (p->child > 0) {
		int st;
		pid_t r = waitpid(p->child, &st, WNOHANG);
		if (r == p->child) {
			p->child = 0;
			/* auto-advance */
			if (p->sel + 1 < p->n)
				play(p, p->sel + 1);
			else
				snprintf(p->status, sizeof(p->status), "終了");
			*changed = true;
		} else {
			*changed = true; /* elapsed timer */
		}
	}
}

int main(int argc, char **argv)
{
	struct player p;
	memset(&p, 0, sizeof(p));
	const char *home = getenv("HOME");
	snprintf(p.dir, sizeof(p.dir), "%s/Music", home ? home : "/home/user");
	p.volume = 0.5f;
	p.alsa = bu_alsa_open();
	if (p.alsa) {
		float v = bu_alsa_get_volume(p.alsa);
		if (v >= 0)
			p.volume = v;
	}
	load(&p);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-player.ppm" : NULL;
	int rc = bdapp_run("音楽プレイヤー", shot, draw, onevent, tick, &p);
	stop_child(&p);
	return rc;
}
