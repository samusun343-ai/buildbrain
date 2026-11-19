/* bd-player: music player for braindesk (v3).
 * Library scanned recursively from ~/Music with real metadata:
 * ID3v1 tags for MP3, RIFF LIST/INFO for WAV, streaminfo for
 * FLAC, "Artist - Title" filename fallback. Three library
 * views (曲 / アルバム / アーティスト) with group drill-down;
 * the track list is the play queue. Transport: prev (restarts
 * the track when >3 s in), play/pause (SIGSTOP/SIGCONT on the
 * player child), stop, next; shuffle and a 3-state repeat
 * (off / all / one). The progress bar is a seek bar: tapping
 * restarts the format's CLI player at the offset (WAV via a
 * dd pipe, MP3 via mpg123 -k, OGG via ogg123 -k, FLAC via
 * flac --skip). Volume still goes to the real ALSA Master
 * via bui_alsa — the audio path is unchanged.
 */
#include "bdapp.h"
#include "../bui_alsa.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

#define MAXTRACKS 256
#define MAXGROUPS 64

struct track {
	char path[512];
	char title[128];
	char artist[128];
	char album[128];
	char genre[64];
	int duration;		/* seconds; -1 unknown */
	int rate;		/* sample rate (seek math) */
	int data_off;		/* WAV: data chunk offset */
	int byte_rate;		/* WAV: bytes per second of data */
};

enum { VIEW_SONGS, VIEW_ALBUMS, VIEW_ARTISTS };
enum { REP_OFF, REP_ALL, REP_ONE };

struct player {
	struct track tracks[MAXTRACKS];
	int n, scroll, current;
	pid_t pid;
	int play_base;		/* offset (s) where this segment started */
	time_t seg_start;	/* when this segment started/resumed */
	bool paused;
	int frozen;		/* elapsed (s) frozen while paused */
	int repeat;		/* REP_* */
	bool shuffle;
	float volume;
	bu_alsa *alsa;
	char status[96];
	int view;
	char groups[MAXGROUPS][128];
	int group_count[MAXGROUPS];
	int ngroups;
	int group_sel;		/* -1 = group list, else group index */
	int view_list[MAXTRACKS];	/* track indices in the current list */
	int view_n;
	bool seeking;
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

static void trim_tag(char *s)
{
	int l = (int)strlen(s);
	while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == 0))
		s[--l] = 0;
}

static const char *GENRES[] = {
	"Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk",
	"Grunge", "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies",
	"Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno",
	"Industrial", "Alternative", "Ska", "Death Metal", "Pranks",
	"Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal",
	"Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental",
	"Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
	"AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative",
	"Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
	"Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk",
	"Eurodance", "Dream", "Southern Rock", "Comedy", "Cult",
	"Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle",
	"Native American", "Cabaret", "New Wave", "Psychadelic",
	"Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal",
	"Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical",
	"Rock & Roll", "Hard Rock",
};
#define NGENRES (int)(sizeof(GENRES) / sizeof(GENRES[0]))

static void id3v1_parse(const char *path, struct track *tr)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return;
	if (fseek(fp, -128, SEEK_END) != 0) {
		fclose(fp);
		return;
	}
	unsigned char tag[128];
	if (fread(tag, 1, 128, fp) == 128 && memcmp(tag, "TAG", 3) == 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%.*s", 30, (const char *)tag + 3);
		trim_tag(buf);
		if (buf[0]) snprintf(tr->title, sizeof(tr->title), "%s", buf);
		snprintf(buf, sizeof(buf), "%.*s", 30, (const char *)tag + 33);
		trim_tag(buf);
		if (buf[0]) snprintf(tr->artist, sizeof(tr->artist), "%s", buf);
		snprintf(buf, sizeof(buf), "%.*s", 30, (const char *)tag + 63);
		trim_tag(buf);
		if (buf[0]) snprintf(tr->album, sizeof(tr->album), "%s", buf);
		unsigned g = tag[127];
		if (g < NGENRES)
			snprintf(tr->genre, sizeof(tr->genre), "%s", GENRES[g]);
	}
	fclose(fp);
	/* duration estimate + sample rate from the first MPEG frame */
	fp = fopen(path, "rb");
	if (!fp)
		return;
	unsigned char h[4];
	struct stat st;
	for (int i = 0; i < 512; i++) {
		if (fread(h, 1, 2, fp) != 2)
			break;
		if (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) {
			if (fread(h + 2, 1, 2, fp) != 2)
				break;
			int ver = (h[1] >> 3) & 3;	/* 3=V1, 2=V2, 0=V2.5 */
			int layer = (h[1] >> 1) & 3;	/* 1=Layer III */
			int bri = (h[2] >> 4) & 15;
			int sri = (h[2] >> 2) & 3;
			static const int rates[4][3] = {
				{ 11025, 12000, 8000 }, { 0, 0, 0 },
				{ 22050, 24000, 16000 }, { 44100, 48000, 32000 },
			};
			if (ver != 1 && sri < 3)
				tr->rate = rates[ver][sri];
			if (layer == 1 && bri >= 1 && bri < 15 && stat(path, &st) == 0) {
				static const int br1[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
				static const int br2[15] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };
				int kbps = ver == 3 ? br1[bri] : br2[bri];
				if (kbps > 0)
					tr->duration = (int)(st.st_size * 8 / (kbps * 1000));
			}
			break;
		}
		if (fseek(fp, -1, SEEK_CUR) != 0)
			break;
	}
	fclose(fp);
}

static unsigned rd32(const unsigned char *b)
{
	return b[0] | (b[1] << 8) | ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
}
static unsigned rd16(const unsigned char *b)
{
	return b[0] | (b[1] << 8);
}

static void wav_parse(const char *path, struct track *tr)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return;
	unsigned char hdr[12];
	if (fread(hdr, 1, 12, fp) != 12 || memcmp(hdr, "RIFF", 4) != 0) {
		fclose(fp);
		return;
	}
	unsigned rate = 0, ch = 0, bits = 0;
	long data_size = -1;
	for (;;) {
		unsigned char ch8[8];
		if (fread(ch8, 1, 8, fp) != 8)
			break;
		unsigned csize = rd32(ch8 + 4);
		long body = ftell(fp);
		if (memcmp(ch8, "fmt ", 4) == 0 && csize >= 16) {
			unsigned char f[16];
			if (fread(f, 1, 16, fp) == 16) {
				ch = rd16(f + 2);
				rate = rd32(f + 4);
				bits = rd16(f + 14);
			}
		} else if (memcmp(ch8, "data", 4) == 0) {
			tr->data_off = (int)body;
			data_size = (long)csize;
		} else if (memcmp(ch8, "LIST", 4) == 0 && csize >= 4) {
			unsigned char lt[4];
			if (fread(lt, 1, 4, fp) == 4 && memcmp(lt, "INFO", 4) == 0) {
				long end = body + (long)csize;
				while (ftell(fp) + 8 <= end) {
					unsigned char sh[8];
					if (fread(sh, 1, 8, fp) != 8)
						break;
					unsigned ss = rd32(sh + 4);
					char *dst = NULL;
					size_t dn = 0;
					if (memcmp(sh, "INAM", 4) == 0) { dst = tr->title; dn = sizeof(tr->title); }
					if (memcmp(sh, "IART", 4) == 0) { dst = tr->artist; dn = sizeof(tr->artist); }
					if (memcmp(sh, "IPRD", 4) == 0) { dst = tr->album; dn = sizeof(tr->album); }
					if (dst && ss > 0) {
						char buf[128];
						unsigned rd = ss < sizeof(buf) - 1 ? ss : sizeof(buf) - 1;
						if (fread(buf, 1, rd, fp) == rd) {
							buf[rd] = 0;
							trim_tag(buf);
							if (buf[0])
								snprintf(dst, dn, "%s", buf);
						}
						if (ss > rd && fseek(fp, (long)(ss - rd), SEEK_CUR) != 0)
							break;
					} else if (fseek(fp, (long)ss, SEEK_CUR) != 0) {
						break;
					}
					if (ss & 1)	/* chunks are word-aligned */
						fseek(fp, 1, SEEK_CUR);
				}
			}
		}
		if (fseek(fp, body + (long)csize + (long)(csize & 1), SEEK_SET) != 0)
			break;
		if (memcmp(ch8, "data", 4) == 0)
			break;
	}
	fclose(fp);
	if (rate && ch && bits) {
		tr->rate = (int)rate;
		tr->byte_rate = (int)(rate * ch * (bits / 8));
		if (data_size >= 0 && tr->byte_rate > 0)
			tr->duration = (int)(data_size / tr->byte_rate);
	}
}

static void flac_parse(const char *path, struct track *tr)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return;
	unsigned char b[42];
	if (fread(b, 1, 42, fp) == 42 && memcmp(b, "fLaC", 4) == 0) {
		/* streaminfo starts at offset 8; rate is 20 bits at +10,
		 * total samples 36 bits at +14 (low nibble first) */
		int rate = (b[18] << 12) | (b[19] << 4) | (b[20] >> 4);
		long long total = ((long long)(b[22] & 0xF) << 32) |
			((long long)b[23] << 24) | ((long long)b[24] << 16) |
			((long long)b[25] << 8) | (long long)b[26];
		tr->rate = rate;
		if (rate > 0 && total > 0)
			tr->duration = (int)(total / rate);
	}
	fclose(fp);
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
			memset(tr, 0, sizeof(*tr));
			tr->duration = -1;
			snprintf(tr->path, sizeof(tr->path), "%s", full);
			snprintf(tr->title, sizeof(tr->title), "%s", de->d_name);
			char *dot = strrchr(tr->title, '.');
			if (dot)
				*dot = 0;
			if (has_ext(de->d_name, "wav"))
				wav_parse(full, tr);
			else if (has_ext(de->d_name, "mp3"))
				id3v1_parse(full, tr);
			else if (has_ext(de->d_name, "flac"))
				flac_parse(full, tr);
			/* filename fallback: "Artist - Title" */
			if (!tr->artist[0]) {
				char *sep = strstr(tr->title, " - ");
				if (sep && sep != tr->title) {
					snprintf(tr->artist, sizeof(tr->artist), "%.*s",
						 (int)(sep - tr->title), tr->title);
					char nt[128];
					snprintf(nt, sizeof(nt), "%s", sep + 3);
					snprintf(tr->title, sizeof(tr->title), "%s", nt);
				}
			}
			pl->n++;
		}
	}
	closedir(d);
}

static int track_cmp(const void *a, const void *b)
{
	return strcmp(((const struct track *)a)->title, ((const struct track *)b)->title);
}

static const char *group_key(struct player *pl, struct track *tr)
{
	if (pl->view == VIEW_ALBUMS)
		return tr->album[0] ? tr->album : "不明なアルバム";
	return tr->artist[0] ? tr->artist : "不明なアーティスト";
}

static void rebuild_groups(struct player *pl)
{
	pl->ngroups = 0;
	if (pl->view == VIEW_SONGS)
		return;
	for (int i = 0; i < pl->n; i++) {
		const char *key = group_key(pl, &pl->tracks[i]);
		int g;
		for (g = 0; g < pl->ngroups; g++)
			if (strcmp(pl->groups[g], key) == 0)
				break;
		if (g == pl->ngroups) {
			if (pl->ngroups >= MAXGROUPS)
				continue;
			snprintf(pl->groups[g], sizeof(pl->groups[g]), "%s", key);
			pl->group_count[g] = 0;
			pl->ngroups++;
		}
		pl->group_count[g]++;
	}
	/* sort groups by name, keeping counts attached */
	for (int i = 0; i < pl->ngroups; i++)
		for (int j = i + 1; j < pl->ngroups; j++)
			if (strcmp(pl->groups[j], pl->groups[i]) < 0) {
				char tn[128];
				snprintf(tn, sizeof(tn), "%s", pl->groups[i]);
				snprintf(pl->groups[i], sizeof(pl->groups[i]), "%s", pl->groups[j]);
				snprintf(pl->groups[j], sizeof(pl->groups[j]), "%s", tn);
				int tc = pl->group_count[i];
				pl->group_count[i] = pl->group_count[j];
				pl->group_count[j] = tc;
			}
}

static void rebuild_view_list(struct player *pl)
{
	pl->view_n = 0;
	for (int i = 0; i < pl->n; i++) {
		if (pl->view != VIEW_SONGS && pl->group_sel >= 0 &&
		    strcmp(group_key(pl, &pl->tracks[i]), pl->groups[pl->group_sel]) != 0)
			continue;
		pl->view_list[pl->view_n++] = i;
	}
}

static bool in_group_mode(struct player *pl)
{
	return pl->view != VIEW_SONGS && pl->group_sel < 0;
}

static int list_count(struct player *pl)
{
	return in_group_mode(pl) ? pl->ngroups : pl->view_n;
}

/* --- playback --- */
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
		kill(pl->pid, SIGCONT);	/* in case it is pause-stopped */
		waitpid(pl->pid, NULL, 0);
		pl->pid = -1;
	}
	pl->paused = false;
}

static void start_track(struct player *pl, int idx, int offset)
{
	if (idx < 0 || idx >= pl->n)
		return;
	stop_track(pl);
	struct track *tr = &pl->tracks[idx];
	const char *cmd = player_cmd(tr->path);
	if (!cmd) {
		snprintf(pl->status, sizeof(pl->status), "対応していない形式です");
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		if (has_ext(tr->path, "flac")) {
			if (offset > 0 && tr->rate > 0) {
				char skip[32];
				snprintf(skip, sizeof(skip), "--skip=%lld",
					 (long long)offset * tr->rate);
				execlp("sh", "sh", "-c",
				       "flac -d -c -s \"$2\" -- \"$1\" | aplay -q", "sh",
				       tr->path, skip, (char *)NULL);
			} else {
				execlp("sh", "sh", "-c", "flac -d -c -s \"$1\" | aplay -q", "sh",
				       tr->path, (char *)NULL);
			}
		} else if (has_ext(tr->path, "mp3")) {
			if (offset > 0 && tr->rate > 0) {
				char fr[16];
				snprintf(fr, sizeof(fr), "%d", offset * tr->rate / 1152);
				execlp("mpg123", "mpg123", "-q", "-k", fr, tr->path, (char *)NULL);
			} else {
				execlp("mpg123", "mpg123", "-q", tr->path, (char *)NULL);
			}
		} else if (has_ext(tr->path, "ogg")) {
			if (offset > 0) {
				char sk[16];
				snprintf(sk, sizeof(sk), "%d", offset);
				execlp("ogg123", "ogg123", "-q", "-k", sk, tr->path, (char *)NULL);
			} else {
				execlp("ogg123", "ogg123", "-q", tr->path, (char *)NULL);
			}
		} else {	/* wav */
			if (offset > 0 && tr->byte_rate > 0 && tr->data_off > 0) {
				char cmd2[256];
				snprintf(cmd2, sizeof(cmd2),
					 "(head -c %d -- \"$1\"; dd if=\"$1\" iflag=skip_bytes skip=%lld bs=65536 2>/dev/null) | aplay -q",
					 tr->data_off,
					 (long long)tr->data_off + (long long)offset * tr->byte_rate);
				execlp("sh", "sh", "-c", cmd2, "sh", tr->path, (char *)NULL);
			} else {
				execlp("aplay", "aplay", "-q", tr->path, (char *)NULL);
			}
		}
		_exit(127);
	}
	pl->pid = pid;
	pl->current = idx;
	pl->play_base = offset;
	pl->seg_start = time(NULL);
	pl->paused = false;
	pl->frozen = offset;
	snprintf(pl->status, sizeof(pl->status), "再生中");
}

static int elapsed_now(struct player *pl)
{
	if (pl->pid <= 0)
		return 0;
	if (pl->paused)
		return pl->frozen;
	return pl->play_base + (int)(time(NULL) - pl->seg_start);
}

static void pause_toggle(struct player *pl)
{
	if (pl->pid <= 0)
		return;
	if (pl->paused) {
		pl->play_base = pl->frozen;
		pl->seg_start = time(NULL);
		kill(pl->pid, SIGCONT);
		pl->paused = false;
		snprintf(pl->status, sizeof(pl->status), "再生中");
	} else {
		pl->frozen = elapsed_now(pl);
		kill(pl->pid, SIGSTOP);
		pl->paused = true;
		snprintf(pl->status, sizeof(pl->status), "一時停止中");
	}
}

static void seek_to(struct player *pl, int sec)
{
	if (pl->current < 0)
		return;
	struct track *tr = &pl->tracks[pl->current];
	if (tr->duration > 0) {
		if (sec < 0) sec = 0;
		if (sec > tr->duration - 1) sec = tr->duration - 1;
	}
	bool was_paused = pl->paused;
	int idx = pl->current;
	start_track(pl, idx, sec);
	if (was_paused && pl->pid > 0) {
		kill(pl->pid, SIGSTOP);
		pl->paused = true;
		pl->frozen = sec;
		snprintf(pl->status, sizeof(pl->status), "一時停止中");
	}
}

static int auto_next(struct player *pl)
{
	if (pl->n == 0 || pl->current < 0)
		return -1;
	if (pl->repeat == REP_ONE)
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
	if (pl->current + 1 < pl->n)
		return pl->current + 1;
	return pl->repeat == REP_ALL ? 0 : -1;
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct player *pl = ctx;
	if (pl->pid > 0) {
		if (!pl->paused) {
			int status;
			pid_t r = waitpid(pl->pid, &status, WNOHANG);
			if (r == pl->pid) {
				pl->pid = -1;
				int nxt = auto_next(pl);
				if (nxt >= 0)
					start_track(pl, nxt, 0);
				else
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

/* --- layout --- */
static bu_rect tab_rect(int i)
{
	static const int xs[3] = { 16, 124, 252 };
	static const int ws[3] = { 100, 120, 140 };
	return (bu_rect){ xs[i], 44, ws[i], 30 };
}
#define CARD_X 16
#define CARD_Y 80

static void draw(bdapp *a, void *ctx)
{
	struct player *pl = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* library view tabs */
	static const char *tabs[3] = { "曲", "アルバム", "アーティスト" };
	for (int i = 0; i < 3; i++)
		bu_button(c, t, tab_rect(i), tabs[i],
			  pl->view == i ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	/* now playing card */
	bu_rect card = { CARD_X, CARD_Y, 832, 106 };
	bu_card(c, t, card);
	if (pl->current >= 0) {
		struct track *tr = &pl->tracks[pl->current];
		bu_icon_note(c, card.x + 34, card.y + 40, t->accent);
		bu_draw_text(c, t->font, card.x + 60, card.y + 12, tr->title, 17, t->text);
		char sub[300];
		if (tr->artist[0] || tr->album[0])
			snprintf(sub, sizeof(sub), "%s%s%s%s%s",
				 tr->artist, tr->artist[0] && tr->album[0] ? " — " : "",
				 tr->album, tr->genre[0] ? " · " : "", tr->genre);
		else
			snprintf(sub, sizeof(sub), "%s", "タグなし");
		bu_draw_text(c, t->font, card.x + 60, card.y + 40, sub, 13, t->text_dim);
		int elapsed = elapsed_now(pl);
		char ebuf[16], rbuf[16];
		fmt_time(ebuf, sizeof(ebuf), elapsed);
		if (tr->duration > 0) {
			int rem = tr->duration - elapsed;
			if (rem < 0)
				rem = 0;
			snprintf(rbuf, sizeof(rbuf), "-%d:%02d", rem / 60, rem % 60);
		} else {
			snprintf(rbuf, sizeof(rbuf), "--:--");
		}
		bu_draw_text(c, t->font, card.x + 60, card.y + 70, ebuf, 12, t->text_dim);
		bu_rect bar = { card.x + 120, card.y + 74, card.w - 240, 10 };
		bu_fill_round_rect(c, bar, 5, t->surface3);
		if (tr->duration > 0 && elapsed > 0) {
			float frac = (float)elapsed / (float)tr->duration;
			if (frac > 1)
				frac = 1;
			bu_fill_round_rect(c, (bu_rect){ bar.x, bar.y, (int)(bar.w * frac), bar.h }, 5, t->accent);
			bu_fill_circle(c, bar.x + (int)(bar.w * frac), bar.y + 5, 7, t->text);
		}
		bu_draw_text_right(c, t->font, (bu_rect){ card.x, card.y + 70, card.w - 18, 20 },
				   rbuf, 12, t->text_dim);
		bu_draw_text_right(c, t->font, (bu_rect){ card.x, card.y + 12, card.w - 18, 22 },
				   pl->status, 12.5, t->accent2);
	} else {
		bu_draw_text(c, t->font, card.x + 24, card.y + 20, "曲を選んで再生", 16, t->text);
		bu_draw_text(c, t->font, card.x + 24, card.y + 50, "~/Music 以下を再帰的に探します (wav / mp3 / ogg / flac)", 12.5, t->text_dim);
	}
	/* transport */
	bu_button(c, t, (bu_rect){ 16, 196, 64, 44 }, "|◀", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 88, 196, 96, 44 },
		  pl->pid > 0 && !pl->paused ? "一時停止" : "▶ 再生", BU_BTN_PRIMARY, false);
	bu_button(c, t, (bu_rect){ 192, 196, 60, 44 }, "■", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 260, 196, 64, 44 }, "▶|", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 340, 196, 118, 44 }, pl->shuffle ? "シャッフル ON" : "シャッフル",
		  pl->shuffle ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	static const char *repl[3] = { "リピート: 切", "リピート: 全曲", "リピート: 1曲" };
	bu_button(c, t, (bu_rect){ 466, 196, 150, 44 }, repl[pl->repeat],
		  pl->repeat != REP_OFF ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	bu_icon_volume(c, 640, 218, t->text_dim);
	bu_slider(c, t, (bu_rect){ 662, 206, 130, 24 }, pl->volume, false);
	char vp[8];
	snprintf(vp, sizeof(vp), "%d%%", (int)(pl->volume * 100 + 0.5f));
	bu_draw_text(c, t->font, 800, 208, vp, 13, t->text);
	/* list header */
	const char *hdr = "プレイリスト";
	char hdrbuf[200];
	if (pl->view == VIEW_ALBUMS)
		hdr = pl->group_sel >= 0 ? pl->groups[pl->group_sel] : "アルバム";
	else if (pl->view == VIEW_ARTISTS)
		hdr = pl->group_sel >= 0 ? pl->groups[pl->group_sel] : "アーティスト";
	if (pl->group_sel >= 0) {
		snprintf(hdrbuf, sizeof(hdrbuf), "← %s", hdr);
		hdr = hdrbuf;
	}
	bu_draw_text(c, t->font, 16, 250, hdr, 14, t->text_dim);
	if (pl->current >= 0 && pl->pid > 0) {
		int nxt = pl->shuffle ? -2 : auto_next(pl);
		char nb[200];
		if (nxt == -2)
			snprintf(nb, sizeof(nb), "次: シャッフル");
		else if (nxt >= 0)
			snprintf(nb, sizeof(nb), "次: %s", pl->tracks[nxt].title);
		else
			snprintf(nb, sizeof(nb), "次: なし");
		bu_draw_text_right(c, t->font, (bu_rect){ 16, 250, 780, 18 }, nb, 12, t->text_faint);
	}
	/* list */
	int visible = 4;
	int count = list_count(pl);
	for (int vi = 0; vi < visible; vi++) {
		int i = pl->scroll + vi;
		if (i >= count)
			break;
		bu_rect r = { 16, 276 + vi * 42, 784, 38 };
		if (in_group_mode(pl)) {
			bu_fill_round_rect(c, r, 8, t->surface);
			bu_draw_text(c, t->font, r.x + 14, r.y + 9, pl->groups[i], 13.5, t->text);
			char cb[32];
			snprintf(cb, sizeof(cb), "%d 曲", pl->group_count[i]);
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 9, r.w - 14, 20 }, cb, 12, t->text_faint);
		} else {
			int ti = pl->view_list[i];
			struct track *tr = &pl->tracks[ti];
			bool cur = ti == pl->current;
			bu_fill_round_rect(c, r, 8, cur ? t->surface3 : t->surface);
			if (cur)
				bu_draw_text(c, t->font, r.x + 12, r.y + 9, pl->paused ? "II" : "▶", 13, t->accent);
			char line[300];
			if (tr->artist[0])
				snprintf(line, sizeof(line), "%s — %s", tr->title, tr->artist);
			else
				snprintf(line, sizeof(line), "%s", tr->title);
			bu_draw_text(c, t->font, r.x + 36, r.y + 9, line, 13.5, cur ? t->text : t->text_dim);
			char db[16];
			fmt_time(db, sizeof(db), tr->duration);
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 9, r.w - 14, 20 }, db, 12, t->text_faint);
		}
	}
	bu_button(c, t, (bu_rect){ 808, 276, 44, 76 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 808, 364, 44, 76 }, "▼", BU_BTN_NORMAL, false);
	if (pl->n == 0)
		bu_draw_text(c, t->font, 16, 292, "~/Music に音楽ファイルがありません", 13, t->text_dim);
	char cnt[64];
	if (in_group_mode(pl))
		snprintf(cnt, sizeof(cnt), "%d グループ · 全 %d 曲", pl->ngroups, pl->n);
	else
		snprintf(cnt, sizeof(cnt), "%d 曲", pl->view_n);
	bu_draw_text_right(c, t->font, (bu_rect){ 16, 452, 832, 16 }, cnt, 11.5, t->text_faint);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static bu_rect seek_hit_rect(void)
{
	return (bu_rect){ CARD_X + 110, CARD_Y + 64, 832 - 220, 30 };
}

static void do_seek_pointer(struct player *pl, int x)
{
	struct track *tr = &pl->tracks[pl->current];
	bu_rect r = seek_hit_rect();
	bu_rect bar = { CARD_X + 120, CARD_Y + 74, 832 - 240, 10 };
	(void)r;
	if (tr->duration <= 0)
		return;
	float frac = (float)(x - bar.x) / (float)bar.w;
	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	seek_to(pl, (int)(frac * tr->duration));
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct player *pl = ctx;
	(void)quit;
	int x = ev->x, y = ev->y;
	if (ev->type == BU_EV_POINTER_UP) {
		pl->seeking = false;
		return;
	}
	if (ev->type == BU_EV_POINTER_MOVE) {
		if (pl->seeking && pl->current >= 0) {
			do_seek_pointer(pl, x);
			*changed = true;
		}
		return;
	}
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	*changed = true;
	/* seek bar */
	if (pl->current >= 0 && inr(x, y, seek_hit_rect())) {
		pl->seeking = true;
		do_seek_pointer(pl, x);
		return;
	}
	for (int i = 0; i < 3; i++) {
		if (inr(x, y, tab_rect(i))) {
			pl->view = i;
			pl->group_sel = -1;
			pl->scroll = 0;
			rebuild_groups(pl);
			rebuild_view_list(pl);
			return;
		}
	}
	if (inr(x, y, (bu_rect){ 16, 196, 64, 44 })) {	/* prev */
		if (pl->current >= 0) {
			if (elapsed_now(pl) > 3 || pl->current == 0)
				seek_to(pl, 0);
			else
				start_track(pl, pl->current - 1, 0);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 88, 196, 96, 44 })) {	/* play/pause */
		if (pl->pid > 0)
			pause_toggle(pl);
		else if (pl->current >= 0)
			start_track(pl, pl->current, 0);
		else if (pl->n > 0)
			start_track(pl, 0, 0);
		return;
	}
	if (inr(x, y, (bu_rect){ 192, 196, 60, 44 })) {	/* stop */
		if (pl->pid > 0) {
			stop_track(pl);
			snprintf(pl->status, sizeof(pl->status), "停止しました");
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 260, 196, 64, 44 })) {	/* next */
		if (pl->current >= 0) {
			int nxt = pl->current + 1;
			if (nxt < pl->n)
				start_track(pl, nxt, 0);
			else if (pl->repeat == REP_ALL)
				start_track(pl, 0, 0);
		} else if (pl->n > 0) {
			start_track(pl, 0, 0);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 340, 196, 118, 44 })) {
		pl->shuffle = !pl->shuffle;
		return;
	}
	if (inr(x, y, (bu_rect){ 466, 196, 150, 44 })) {
		pl->repeat = (pl->repeat + 1) % 3;
		return;
	}
	if (inr(x, y, (bu_rect){ 662, 206, 130, 24 })) {
		pl->volume = (float)(x - 662) / 130.0f;
		if (pl->volume < 0) pl->volume = 0;
		if (pl->volume > 1) pl->volume = 1;
		if (pl->alsa)
			bu_alsa_set_volume(pl->alsa, pl->volume);
		return;
	}
	int count = list_count(pl);
	if (inr(x, y, (bu_rect){ 808, 276, 44, 76 })) {
		if (pl->scroll >= 3) pl->scroll -= 3; else pl->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ 808, 364, 44, 76 })) {
		if (pl->scroll + 4 < count) pl->scroll += 3;
		return;
	}
	/* list header: back to group list */
	if (pl->group_sel >= 0 && inr(x, y, (bu_rect){ 16, 244, 400, 26 })) {
		pl->group_sel = -1;
		pl->scroll = 0;
		rebuild_view_list(pl);
		return;
	}
	int visible = 4;
	for (int vi = 0; vi < visible; vi++) {
		int i = pl->scroll + vi;
		if (i >= count)
			break;
		bu_rect r = { 16, 276 + vi * 42, 784, 38 };
		if (inr(x, y, r)) {
			if (in_group_mode(pl)) {
				pl->group_sel = i;
				pl->scroll = 0;
				rebuild_view_list(pl);
			} else {
				start_track(pl, pl->view_list[i], 0);
			}
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
	pl.group_sel = -1;
	srand((unsigned)time(NULL) ^ (unsigned)getpid());
	const char *home = getenv("HOME");
	char music[512];
	snprintf(music, sizeof(music), "%s/Music", home ? home : "/home/user");
	char cmd[560];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", music);
	if (system(cmd) == 0)
		scan_rec(&pl, music, 0);
	qsort(pl.tracks, pl.n, sizeof(struct track), track_cmp);
	rebuild_groups(&pl);
	rebuild_view_list(&pl);
	pl.alsa = bu_alsa_open();
	pl.volume = 0.8f;
	if (pl.alsa) {
		float v = bu_alsa_get_volume(pl.alsa);
		if (v >= 0)
			pl.volume = v;
	}
	/* shot hooks (host verification) */
	const char *vw = getenv("BD_PLAYER_VIEW");
	if (vw) {
		if (strcmp(vw, "albums") == 0) pl.view = VIEW_ALBUMS;
		else if (strcmp(vw, "artists") == 0) pl.view = VIEW_ARTISTS;
		rebuild_groups(&pl);
		rebuild_view_list(&pl);
	}
	const char *cur = getenv("BD_PLAYER_CUR");
	if (cur) {
		int i = atoi(cur);
		if (i >= 0 && i < pl.n) {
			pl.current = i;
			snprintf(pl.status, sizeof(pl.status), "一時停止中");
			pl.paused = true;
		}
	}
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-player.ppm" : NULL;
	return bdapp_run("音楽", shot, draw, onevent, tick, &pl);
}
