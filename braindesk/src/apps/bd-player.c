/* bd-player: music player for braindesk (v2).
 * Now-playing card with elapsed time, transport controls,
 * shuffle/repeat, and a recursive scan of ~/Music. Playback
 * uses the format's standard CLI player as a child process;
 * volume goes to the real ALSA Master via bui_alsa.
 */
#include "bdapp.h"
#include "../bui_alsa.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

#define MAXTRACKS 256

struct track {
	char path[512];
	char title[128];
	int duration;		/* seconds, WAV header only; -1 unknown */
};

struct player {
	struct track tracks[MAXTRACKS];
	int n, scroll, current;
	pid_t pid;
	time_t started;
	bool paused;
	bool shuffle, repeat;
	float volume;
	bu_alsa *alsa;
	char status[96];
};

static bool has_ext(const char *name, const char *ext)
{
	const char *dot = strrchr(name, '.');
	return dot && strcasecmp(dot + 1, ext) == 0;
}

static bool is_audio(const char *name)
{
	return has_ext(name, "wav") || has_ext(name, "mp3") ||
	       has_ext(name, "ogg") || has_ext(name, "flac");
}

static int wav_duration(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return -1;
	unsigned char hdr[64];
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

static void scan_rec(struct player *pl, const char *dir, int depth)
{
	if (depth > 3 || pl->n >= MAXTRACKS)
		return;
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && pl->n < MAXTRACKS) {
		if (de->d_name[0] == '.')
			continue;
		char full[512];
		snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
		struct stat st;
		if (stat(full, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			scan_rec(pl, full, depth + 1);
		} else if (is_audio(de->d_name)) {
			struct track *tr = &pl->tracks[pl->n];
			snprintf(tr->path, sizeof(tr->path), "%s", full);
			snprintf(tr->title, sizeof(tr->title), "%s", de->d_name);
			char *dot = strrchr(tr->title, '.');
			if (dot)
				*dot = 0;
			tr->duration = has_ext(de->d_name, "wav") ? wav_duration(full) : -1;
			pl->n++;
		}
	}
	closedir(d);
}

static int track_cmp(const void *a, const void *b)
{
	return strcmp(((const struct track *)a)->title, ((const struct track *)b)->title);
}

static const char *player_cmd(const char *path)
{
	if (has_ext(path, "wav"))
		return "aplay";
	if (has_ext(path, "mp3"))
		return "mpg123";
	if (has_ext(path, "ogg"))
		return "ogg123";
	if (has_ext(path, "flac"))
		return "flac";
	return NULL;
}

static void stop_track(struct player *pl)
{
	if (pl->pid > 0) {
		kill(pl->pid, SIGTERM);
		waitpid(pl->pid, NULL, 0);
		pl->pid = -1;
	}
	pl->paused = false;
}

static void play_track(struct player *pl, int idx)
{
	if (idx < 0 || idx >= pl->n)
		return;
	stop_track(pl);
	const char *cmd = player_cmd(pl->tracks[idx].path);
	if (!cmd) {
		snprintf(pl->status, sizeof(pl->status), "対応していない形式です");
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		if (strcmp(cmd, "flac") == 0)
			execlp("sh", "sh", "-c", "flac -d -c -s \"$1\" | aplay -q", "sh", pl->tracks[idx].path, (char *)NULL);
		else if (strcmp(cmd, "mpg123") == 0)
			execlp("mpg123", "mpg123", "-q", pl->tracks[idx].path, (char *)NULL);
		else if (strcmp(cmd, "ogg123") == 0)
			execlp("ogg123", "ogg123", "-q", pl->tracks[idx].path, (char *)NULL);
		else
			execlp("aplay", "aplay", "-q", pl->tracks[idx].path, (char *)NULL);
		_exit(127);
	}
	pl->pid = pid;
	pl->current = idx;
	pl->started = time(NULL);
	pl->paused = false;
	snprintf(pl->status, sizeof(pl->status), "再生中");
}

static int next_index(struct player *pl)
{
	if (pl->n == 0)
		return -1;
	if (pl->repeat)
		return pl->current;
	if (pl->shuffle) {
		if (pl->n == 1)
			return pl->current;
		int nxt;
		do {
			nxt = rand() % pl->n;
		} while (nxt == pl->current);
		return nxt;
	}
	return pl->current + 1 < pl->n ? pl->current + 1 : -1;
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct player *pl = ctx;
	if (pl->pid > 0) {
		int status;
		pid_t r = waitpid(pl->pid, &status, WNOHANG);
		if (r == pl->pid) {
			pl->pid = -1;
			int nxt = next_index(pl);
			if (nxt >= 0)
				play_track(pl, nxt);
			else {
				pl->current = -1;
				snprintf(pl->status, sizeof(pl->status), "再生が終わりました");
			}
		}
		*changed = true; /* elapsed time display */
	}
}

static void fmt_time(char *buf, size_t n, int sec)
{
	if (sec < 0) {
		snprintf(buf, n, "--:--");
		return;
	}
	snprintf(buf, n, "%d:%02d", sec / 60, sec % 60);
}

static void draw(bdapp *a, void *ctx)
{
	struct player *pl = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* now playing card */
	bu_rect card = { 16, 54, 832, 96 };
	bu_card(c, t, card);
	if (pl->current >= 0) {
		struct track *tr = &pl->tracks[pl->current];
		bu_icon_note(c, card.x + 34, card.y + 40, t->accent);
		char ttl[160];
		snprintf(ttl, sizeof(ttl), "%s", tr->title);
		bu_draw_text(c, t->font, card.x + 60, card.y + 14, ttl, 17, t->text);
		int elapsed = pl->pid > 0 ? (int)(time(NULL) - pl->started) : 0;
		char ebuf[16], dbuf[16], prog[48];
		fmt_time(ebuf, sizeof(ebuf), elapsed);
		fmt_time(dbuf, sizeof(dbuf), tr->duration);
		snprintf(prog, sizeof(prog), "%s / %s", ebuf, dbuf);
		bu_draw_text(c, t->font, card.x + 60, card.y + 44, prog, 13, t->text_dim);
		/* progress bar */
		bu_rect bar = { card.x + 60, card.y + 70, card.w - 90, 8 };
		bu_fill_round_rect(c, bar, 4, t->surface3);
		if (tr->duration > 0) {
			float frac = (float)elapsed / (float)tr->duration;
			if (frac > 1)
				frac = 1;
			bu_fill_round_rect(c, (bu_rect){ bar.x, bar.y, (int)(bar.w * frac), bar.h }, 4, t->accent);
		}
		bu_draw_text_right(c, t->font, (bu_rect){ card.x, card.y + 14, card.w - 18, 22 },
				   pl->status, 12.5, t->accent2);
	} else {
		bu_draw_text(c, t->font, card.x + 24, card.y + 20, "曲を選んで再生", 16, t->text);
		bu_draw_text(c, t->font, card.x + 24, card.y + 50, "~/Music 以下を再帰的に探します (wav / mp3 / ogg / flac)", 12.5, t->text_dim);
	}
	/* transport */
	bu_button(c, t, (bu_rect){ 16, 160, 76, 44 }, "|◀", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 100, 160, 96, 44 }, pl->pid > 0 ? "■ 停止" : "▶ 再生", BU_BTN_PRIMARY, false);
	bu_button(c, t, (bu_rect){ 204, 160, 76, 44 }, "▶|", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 300, 160, 118, 44 }, pl->shuffle ? "シャッフル ON" : "シャッフル", pl->shuffle ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 426, 160, 118, 44 }, pl->repeat ? "リピート ON" : "リピート", pl->repeat ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	/* volume */
	bu_icon_volume(c, 580, 182, t->text_dim);
	bu_slider(c, t, (bu_rect){ 606, 170, 170, 24 }, pl->volume, false);
	char vp[8];
	snprintf(vp, sizeof(vp), "%d%%", (int)(pl->volume * 100 + 0.5f));
	bu_draw_text(c, t->font, 784, 172, vp, 13, t->text);
	/* playlist */
	bu_draw_text(c, t->font, 16, 218, "プレイリスト", 14, t->text_dim);
	int visible = 5;
	for (int vi = 0; vi < visible; vi++) {
		int i = pl->scroll + vi;
		if (i >= pl->n)
			break;
		struct track *tr = &pl->tracks[i];
		bu_rect r = { 16, 244 + vi * 42, 784, 38 };
		bool cur = i == pl->current;
		bu_fill_round_rect(c, r, 8, cur ? t->surface3 : t->surface);
		if (cur)
			bu_draw_text(c, t->font, r.x + 12, r.y + 9, "▶", 13, t->accent);
		bu_draw_text(c, t->font, r.x + 36, r.y + 9, tr->title, 13.5, cur ? t->text : t->text_dim);
		char db[16];
		fmt_time(db, sizeof(db), tr->duration);
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 9, r.w - 14, 20 }, db, 12, t->text_faint);
	}
	bu_button(c, t, (bu_rect){ 808, 244, 44, 52 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 808, 402, 44, 52 }, "▼", BU_BTN_NORMAL, false);
	if (pl->n == 0)
		bu_draw_text(c, t->font, 16, 260, "~/Music に音楽ファイルがありません", 13, t->text_dim);
	char cnt[32];
	snprintf(cnt, sizeof(cnt), "%d 曲", pl->n);
	bu_draw_text_right(c, t->font, (bu_rect){ 16, 462, 832, 16 }, cnt, 11.5, t->text_faint);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct player *pl = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (inr(x, y, (bu_rect){ 16, 160, 76, 44 })) {
		if (pl->current > 0)
			play_track(pl, pl->current - 1);
		return;
	}
	if (inr(x, y, (bu_rect){ 100, 160, 96, 44 })) {
		if (pl->pid > 0) {
			stop_track(pl);
			snprintf(pl->status, sizeof(pl->status), "停止しました");
		} else if (pl->current >= 0) {
			play_track(pl, pl->current);
		} else if (pl->n > 0) {
			play_track(pl, 0);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 204, 160, 76, 44 })) {
		int nxt = pl->current + 1;
		if (nxt < pl->n)
			play_track(pl, nxt);
		return;
	}
	if (inr(x, y, (bu_rect){ 300, 160, 118, 44 })) {
		pl->shuffle = !pl->shuffle;
		return;
	}
	if (inr(x, y, (bu_rect){ 426, 160, 118, 44 })) {
		pl->repeat = !pl->repeat;
		return;
	}
	if (inr(x, y, (bu_rect){ 606, 170, 170, 24 })) {
		pl->volume = (float)(x - 606) / 170.0f;
		if (pl->volume < 0) pl->volume = 0;
		if (pl->volume > 1) pl->volume = 1;
		if (pl->alsa)
			bu_alsa_set_volume(pl->alsa, pl->volume);
		return;
	}
	if (inr(x, y, (bu_rect){ 808, 244, 44, 52 })) {
		if (pl->scroll >= 3) pl->scroll -= 3; else pl->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ 808, 402, 44, 52 })) {
		if (pl->scroll + 5 < pl->n) pl->scroll += 3;
		return;
	}
	int visible = 5;
	for (int vi = 0; vi < visible; vi++) {
		int i = pl->scroll + vi;
		if (i >= pl->n)
			break;
		bu_rect r = { 16, 244 + vi * 42, 784, 38 };
		if (inr(x, y, r)) {
			play_track(pl, i);
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct player pl;
	memset(&pl, 0, sizeof(pl));
	pl.pid = -1;
	pl.current = -1;
	srand((unsigned)time(NULL) ^ (unsigned)getpid());
	const char *home = getenv("HOME");
	char music[512];
	snprintf(music, sizeof(music), "%s/Music", home ? home : "/home/user");
	char cmd[560];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", music);
	if (system(cmd) == 0)
		scan_rec(&pl, music, 0);
	qsort(pl.tracks, pl.n, sizeof(struct track), track_cmp);
	pl.alsa = bu_alsa_open();
	pl.volume = 0.8f;
	if (pl.alsa) {
		float v = bu_alsa_get_volume(pl.alsa);
		if (v >= 0)
			pl.volume = v;
	}
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-player.ppm" : NULL;
	return bdapp_run("音楽", shot, draw, onevent, tick, &pl);
}
