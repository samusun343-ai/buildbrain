/* bd-recorder: voice recorder for braindesk (v3).
 * Recording keeps the existing arecord path (44.1k / 22.05k /
 * 16k), now with a live scrolling waveform, pause/resume in
 * the same file (SIGSTOP/SIGCONT, paused time excluded from
 * the timer) and the "child died instantly" error surfaced
 * as before. Selecting a recording opens a player panel with
 * its waveform: tap/drag to scrub, ±15 s skip buttons, play/
 * pause, and a per-recording playback-speed slider (0.5–2x,
 * remembered in speeds.txt; applied by patching the WAV
 * header rate in the playback pipe). Trim mode offers two
 * draggable handles with keep-selection / delete-selection,
 * written back as a real WAV. Rename and list search use the
 * shared OSK; delete moves files to .trash (Recently
 * Deleted) with restore / erase for good.
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>

#define MAXREC 64
#define MAXPEAKS 1024
#define MAXSPEEDS 64

struct rec {
	char name[128];
	int duration;
	off_t size;
	time_t mtime;
	int rate, data_off, byte_rate;
};

struct speedent {
	char name[128];
	float speed;
};

enum { OSKM_NONE, OSKM_RENAME, OSKM_SEARCH };
enum { DRAG_NONE, DRAG_SEEK, DRAG_TS, DRAG_TE };

struct recorder {
	char dir[512];
	char trashdir[512];
	struct rec recs[MAXREC];
	int n, scroll;
	/* recording */
	pid_t pid;
	time_t started;
	int rate;		/* 44100 / 22050 / 16000 */
	bool rec_paused;
	time_t pause_start;
	int pause_accum;
	char cur_path[640];
	long rec_read_off;	/* bytes of WAV data consumed for live peaks */
	float live_peaks[MAXPEAKS];
	int live_n;
	/* selection / playback */
	int sel;
	float peaks[MAXPEAKS];
	int npeaks;
	pid_t play_pid;
	int play_idx;
	double play_base;	/* content seconds where this segment started */
	time_t seg_start;
	bool play_paused;
	double frozen;
	double pos_base;	/* display position while stopped */
	struct speedent speeds[MAXSPEEDS];
	int nspeeds;
	/* trim */
	bool trim_mode;
	double trim_s, trim_e;
	int drag;
	/* delete / trash */
	bool confirm_delete;
	int del_idx;
	bool trash_view;
	/* OSK (rename / search) */
	int osk_mode;
	bu_osk osk;
	char edit[128];
	char filter[64];
	char status[128];
};

static unsigned rd32(const unsigned char *b)
{
	return b[0] | (b[1] << 8) | ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
}
static unsigned rd16(const unsigned char *b)
{
	return b[0] | (b[1] << 8);
}

/* parse WAV chunk structure; fills rate/data_off/byte_rate/duration */
static bool wav_parse(const char *path, struct rec *r)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return false;
	unsigned char hdr[12];
	if (fread(hdr, 1, 12, fp) != 12 || memcmp(hdr, "RIFF", 4) != 0) {
		fclose(fp);
		return false;
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
			fseek(fp, body, SEEK_SET);
		} else if (memcmp(ch8, "data", 4) == 0) {
			r->data_off = (int)body;
			data_size = (long)csize;
			break;
		}
		if (fseek(fp, body + (long)csize + (long)(csize & 1), SEEK_SET) != 0)
			break;
	}
	fclose(fp);
	if (!rate || !ch || !bits)
		return false;
	r->rate = (int)rate;
	r->byte_rate = (int)(rate * ch * (bits / 8));
	if (data_size >= 0 && r->byte_rate > 0)
		r->duration = (int)(data_size / r->byte_rate);
	return true;
}

static bool ci_contains(const char *hay, const char *needle)
{
	if (!needle[0])
		return true;
	int nl = (int)strlen(needle);
	for (const char *p = hay; *p; p++) {
		int i;
		for (i = 0; i < nl; i++) {
			if (!p[i])
				return false;
			if (tolower((unsigned char)p[i]) != tolower((unsigned char)needle[i]))
				break;
		}
		if (i == nl)
			return true;
	}
	return false;
}

static void load_list(struct recorder *rc)
{
	const char *dir = rc->trash_view ? rc->trashdir : rc->dir;
	DIR *d = opendir(dir);
	rc->n = 0;
	rc->sel = -1;
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && rc->n < MAXREC) {
		const char *dot = strrchr(de->d_name, '.');
		if (!dot || strcasecmp(dot, ".wav") != 0)
			continue;
		if (rc->filter[0] && !ci_contains(de->d_name, rc->filter))
			continue;
		struct rec *r = &rc->recs[rc->n];
		memset(r, 0, sizeof(*r));
		r->duration = -1;
		snprintf(r->name, 128, "%s", de->d_name);
		char full[640];
		snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
		struct stat st;
		if (stat(full, &st) == 0) {
			r->size = st.st_size;
			r->mtime = st.st_mtime;
		}
		wav_parse(full, r);
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
	if (rc->scroll > rc->n)
		rc->scroll = 0;
}

/* --- per-recording speed store (speeds.txt in the recordings dir) --- */
static void speeds_load(struct recorder *rc)
{
	char path[640];
	snprintf(path, sizeof(path), "%s/speeds.txt", rc->dir);
	FILE *fp = fopen(path, "r");
	rc->nspeeds = 0;
	if (!fp)
		return;
	char line[192];
	while (fgets(line, sizeof(line), fp) && rc->nspeeds < MAXSPEEDS) {
		char nm[128];
		float v;
		if (sscanf(line, "%127s %f", nm, &v) == 2) {
			snprintf(rc->speeds[rc->nspeeds].name, 128, "%s", nm);
			rc->speeds[rc->nspeeds].speed = v;
			rc->nspeeds++;
		}
	}
	fclose(fp);
}

static void speeds_save(struct recorder *rc)
{
	char path[640];
	snprintf(path, sizeof(path), "%s/speeds.txt", rc->dir);
	FILE *fp = fopen(path, "w");
	if (!fp)
		return;
	for (int i = 0; i < rc->nspeeds; i++)
		fprintf(fp, "%s %.3f\n", rc->speeds[i].name, rc->speeds[i].speed);
	fclose(fp);
}

static float speed_of(struct recorder *rc, const char *name)
{
	for (int i = 0; i < rc->nspeeds; i++)
		if (strcmp(rc->speeds[i].name, name) == 0)
			return rc->speeds[i].speed;
	return 1.0f;
}

static void speed_set(struct recorder *rc, const char *name, float v)
{
	for (int i = 0; i < rc->nspeeds; i++)
		if (strcmp(rc->speeds[i].name, name) == 0) {
			rc->speeds[i].speed = v;
			speeds_save(rc);
			return;
		}
	if (rc->nspeeds < MAXSPEEDS) {
		snprintf(rc->speeds[rc->nspeeds].name, 128, "%s", name);
		rc->speeds[rc->nspeeds].speed = v;
		rc->nspeeds++;
		speeds_save(rc);
	}
}

/* --- waveform peaks --- */
static int compute_peaks(const char *path, const struct rec *r, float *peaks, int maxn)
{
	if (r->data_off <= 0 || r->byte_rate <= 0 || r->duration <= 0)
		return 0;
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return 0;
	struct stat st;
	if (stat(path, &st) != 0) {
		fclose(fp);
		return 0;
	}
	long data_bytes = st.st_size - r->data_off;
	if (data_bytes <= 0) {
		fclose(fp);
		return 0;
	}
	int n = maxn;
	long per = data_bytes / n;
	if (per < 2)
		per = 2;
	n = (int)(data_bytes / per);
	if (n > maxn)
		n = maxn;
	fseek(fp, r->data_off, SEEK_SET);
	unsigned char buf[8192];
	for (int i = 0; i < n; i++) {
		long left = per;
		int peak = 0;
		while (left > 0) {
			long want = left > (long)sizeof(buf) ? (long)sizeof(buf) : left;
			size_t got = fread(buf, 1, want, fp);
			if (got == 0)
				break;
			for (size_t j = 0; j + 1 < got; j += 2) {
				int s = (short)(buf[j] | (buf[j + 1] << 8));
				if (s < 0)
					s = -s;
				if (s > peak)
					peak = s;
			}
			left -= (long)got;
		}
		peaks[i] = peak / 32768.0f;
	}
	fclose(fp);
	return n;
}

static void load_peaks(struct recorder *rc)
{
	rc->npeaks = 0;
	if (rc->sel < 0 || rc->sel >= rc->n)
		return;
	char full[640];
	snprintf(full, sizeof(full), "%s/%s", rc->dir, rc->recs[rc->sel].name);
	rc->npeaks = compute_peaks(full, &rc->recs[rc->sel], rc->peaks, MAXPEAKS);
}

/* --- playback --- */
static void stop_playback(struct recorder *rc)
{
	if (rc->play_pid > 0) {
		kill(rc->play_pid, SIGTERM);
		kill(rc->play_pid, SIGCONT);
		waitpid(rc->play_pid, NULL, 0);
		rc->play_pid = -1;
	}
	rc->play_idx = -1;
	rc->play_paused = false;
}

static double play_pos(struct recorder *rc)
{
	if (rc->play_pid > 0 && rc->play_idx >= 0) {
		if (rc->play_paused)
			return rc->frozen;
		float sp = speed_of(rc, rc->recs[rc->play_idx].name);
		return rc->play_base + (double)(time(NULL) - rc->seg_start) * sp;
	}
	return rc->pos_base;
}

/* write a copy of the WAV header with a patched sample rate so
 * aplay plays the data faster/slower (pitch shifts with speed,
 * like the simple players do) */
static bool write_patched_header(const char *src, const struct rec *r,
				 float speed, char *outpath, size_t outn)
{
	if (r->data_off < 36)
		return false;
	FILE *in = fopen(src, "rb");
	if (!in)
		return false;
	unsigned char hdr[512];
	if (r->data_off > (int)sizeof(hdr) || fread(hdr, 1, r->data_off, in) != (size_t)r->data_off) {
		fclose(in);
		return false;
	}
	fclose(in);
	if (memcmp(hdr + 12, "fmt ", 4) != 0)
		return false;
	unsigned nr = (unsigned)(r->rate * speed + 0.5f);
	unsigned ch = rd16(hdr + 22);
	unsigned bits = rd16(hdr + 34);
	hdr[24] = nr & 0xFF;
	hdr[25] = (nr >> 8) & 0xFF;
	hdr[26] = (nr >> 16) & 0xFF;
	hdr[27] = (nr >> 24) & 0xFF;
	unsigned br = nr * ch * (bits / 8);
	hdr[28] = br & 0xFF;
	hdr[29] = (br >> 8) & 0xFF;
	hdr[30] = (br >> 16) & 0xFF;
	hdr[31] = (br >> 24) & 0xFF;
	snprintf(outpath, outn, "/tmp/bdrec-hdr-%d.bin", (int)getpid());
	FILE *out = fopen(outpath, "wb");
	if (!out)
		return false;
	bool ok = fwrite(hdr, 1, r->data_off, out) == (size_t)r->data_off;
	fclose(out);
	return ok;
}

static void start_play(struct recorder *rc, int idx, double offset)
{
	if (idx < 0 || idx >= rc->n)
		return;
	stop_playback(rc);
	struct rec *r = &rc->recs[idx];
	char full[640];
	snprintf(full, sizeof(full), "%s/%s", rc->dir, r->name);
	float speed = speed_of(rc, r->name);
	if (r->duration > 0) {
		if (offset < 0)
			offset = 0;
		if (offset > r->duration - 0.2)
			offset = r->duration - 0.2 > 0 ? r->duration - 0.2 : 0;
	}
	pid_t pid = fork();
	if (pid == 0) {
		if ((speed == 1.0f && offset <= 0.01) || r->byte_rate <= 0 || r->data_off <= 0) {
			execlp("aplay", "aplay", "-q", full, (char *)NULL);
		} else {
			char hdrpath[128] = "";
			bool have_hdr = false;
			if (speed != 1.0f)
				have_hdr = write_patched_header(full, r, speed, hdrpath, sizeof(hdrpath));
			char cmd[1024];
			long long skip = (long long)r->data_off + (long long)(offset * r->byte_rate);
			if (have_hdr)
				snprintf(cmd, sizeof(cmd),
					 "(cat -- \"$2\"; dd if=\"$1\" iflag=skip_bytes skip=%lld bs=65536 2>/dev/null) | aplay -q",
					 skip);
			else
				snprintf(cmd, sizeof(cmd),
					 "(head -c %d -- \"$1\"; dd if=\"$1\" iflag=skip_bytes skip=%lld bs=65536 2>/dev/null) | aplay -q",
					 r->data_off, skip);
			execlp("sh", "sh", "-c", cmd, "sh", full, hdrpath, (char *)NULL);
		}
		_exit(127);
	}
	rc->play_pid = pid;
	rc->play_idx = idx;
	rc->play_base = offset;
	rc->seg_start = time(NULL);
	rc->play_paused = false;
	rc->frozen = offset;
	rc->pos_base = offset;
}

static void play_pause_toggle(struct recorder *rc)
{
	if (rc->play_pid > 0) {
		if (rc->play_paused) {
			rc->play_base = rc->frozen;
			rc->seg_start = time(NULL);
			kill(rc->play_pid, SIGCONT);
			rc->play_paused = false;
		} else {
			rc->frozen = play_pos(rc);
			kill(rc->play_pid, SIGSTOP);
			rc->play_paused = true;
		}
	} else if (rc->sel >= 0) {
		start_play(rc, rc->sel, rc->pos_base);
	}
}

static void seek_play(struct recorder *rc, double pos)
{
	if (rc->sel < 0)
		return;
	struct rec *r = &rc->recs[rc->sel];
	if (pos < 0)
		pos = 0;
	if (r->duration > 0 && pos > r->duration)
		pos = r->duration;
	rc->pos_base = pos;
	if (rc->play_pid > 0) {
		bool was_paused = rc->play_paused;
		start_play(rc, rc->sel, pos);
		if (was_paused && rc->play_pid > 0) {
			kill(rc->play_pid, SIGSTOP);
			rc->play_paused = true;
			rc->frozen = pos;
		}
	}
}

/* --- trim: rewrite the WAV keeping or deleting [s0, s1] seconds --- */
static bool wav_trim(const char *path, const struct rec *r, double s0, double s1, bool keep_sel)
{
	if (r->byte_rate <= 0 || r->data_off <= 0)
		return false;
	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	long data_size = st.st_size - r->data_off;
	long align = r->byte_rate / r->rate;	/* block align */
	if (align <= 0)
		align = 1;
	long b0 = (long)(s0 * r->byte_rate) / align * align;
	long b1 = (long)(s1 * r->byte_rate) / align * align;
	if (b0 < 0) b0 = 0;
	if (b1 > data_size) b1 = data_size;
	if (b1 <= b0)
		return false;
	long new_size = keep_sel ? (b1 - b0) : (data_size - (b1 - b0));
	if (new_size <= 0)
		return false;
	FILE *in = fopen(path, "rb");
	if (!in)
		return false;
	char tmp[700];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *out = fopen(tmp, "wb");
	if (!out) {
		fclose(in);
		return false;
	}
	unsigned char hdr[512];
	bool ok = r->data_off <= (int)sizeof(hdr) &&
		fread(hdr, 1, r->data_off, in) == (size_t)r->data_off;
	if (ok) {
		unsigned riff = (unsigned)(r->data_off + new_size - 8);
		hdr[4] = riff & 0xFF;
		hdr[5] = (riff >> 8) & 0xFF;
		hdr[6] = (riff >> 16) & 0xFF;
		hdr[7] = (riff >> 24) & 0xFF;
		unsigned ds = (unsigned)new_size;
		int doff = r->data_off - 4;	/* data chunk size field */
		hdr[doff] = ds & 0xFF;
		hdr[doff + 1] = (ds >> 8) & 0xFF;
		hdr[doff + 2] = (ds >> 16) & 0xFF;
		hdr[doff + 3] = (ds >> 24) & 0xFF;
		ok = fwrite(hdr, 1, r->data_off, out) == (size_t)r->data_off;
	}
	unsigned char buf[16384];
	struct { long from, len; } segs[2];
	int nseg = 0;
	if (keep_sel) {
		segs[0].from = b0; segs[0].len = b1 - b0; nseg = 1;
	} else {
		segs[0].from = 0; segs[0].len = b0;
		segs[1].from = b1; segs[1].len = data_size - b1;
		nseg = 2;
	}
	for (int s = 0; ok && s < nseg; s++) {
		if (segs[s].len <= 0)
			continue;
		if (fseek(in, r->data_off + segs[s].from, SEEK_SET) != 0) {
			ok = false;
			break;
		}
		long left = segs[s].len;
		while (left > 0) {
			long want = left > (long)sizeof(buf) ? (long)sizeof(buf) : left;
			size_t got = fread(buf, 1, want, in);
			if (got == 0 || fwrite(buf, 1, got, out) != got) {
				ok = false;
				break;
			}
			left -= (long)got;
		}
	}
	fclose(in);
	if (fclose(out) != 0)
		ok = false;
	if (ok && rename(tmp, path) == 0)
		return true;
	remove(tmp);
	return false;
}

/* --- recording helpers --- */
static int rec_elapsed(struct recorder *rc)
{
	if (rc->pid <= 0)
		return 0;
	int el = (int)(time(NULL) - rc->started) - rc->pause_accum;
	if (rc->rec_paused)
		el -= (int)(time(NULL) - rc->pause_start);
	return el < 0 ? 0 : el;
}

static void live_wave_tick(struct recorder *rc)
{
	if (rc->pid <= 0 || rc->rec_paused)
		return;
	struct stat st;
	if (stat(rc->cur_path, &st) != 0)
		return;
	int byte_rate = rc->rate * 2 * 2;	/* S16_LE stereo */
	long bucket = byte_rate / 4;		/* 0.25 s per peak */
	long avail = st.st_size - 44 - rc->rec_read_off;
	FILE *fp = NULL;
	while (avail >= bucket && rc->live_n < MAXPEAKS) {
		if (!fp) {
			fp = fopen(rc->cur_path, "rb");
			if (!fp)
				return;
			fseek(fp, 44 + rc->rec_read_off, SEEK_SET);
		}
		int peak = 0;
		long left = bucket;
		unsigned char buf[8192];
		while (left > 0) {
			long want = left > (long)sizeof(buf) ? (long)sizeof(buf) : left;
			size_t got = fread(buf, 1, want, fp);
			if (got == 0)
				break;
			for (size_t j = 0; j + 1 < got; j += 2) {
				int s = (short)(buf[j] | (buf[j + 1] << 8));
				if (s < 0)
					s = -s;
				if (s > peak)
					peak = s;
			}
			left -= (long)got;
		}
		rc->live_peaks[rc->live_n++] = peak / 32768.0f;
		rc->rec_read_off += bucket;
		avail -= bucket;
	}
	if (fp)
		fclose(fp);
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct recorder *rc = ctx;
	if (rc->pid > 0) {
		int status;
		pid_t r = waitpid(rc->pid, &status, WNOHANG);
		if (r == rc->pid) {
			int el = rec_elapsed(rc);	/* before pid is cleared */
			rc->pid = -1;
			if (el < 2)
				snprintf(rc->status, sizeof(rc->status), "録音デバイスでエラーが発生しました");
			else {
				snprintf(rc->status, sizeof(rc->status), "保存しました");
				load_list(rc);
			}
			*changed = true;
		} else {
			live_wave_tick(rc);
			*changed = true; /* live timer + waveform */
		}
	}
	if (rc->play_pid > 0) {
		int status;
		if (waitpid(rc->play_pid, &status, WNOHANG) == rc->play_pid) {
			rc->play_pid = -1;
			rc->play_idx = -1;
			rc->play_paused = false;
			rc->pos_base = 0;
			*changed = true;
		} else if (!rc->play_paused) {
			*changed = true; /* playhead */
		}
	}
}

/* --- layout --- */
static bu_rect wave_rect(void)
{
	return (bu_rect){ 16, 356, 832, 54 };
}
static bu_rect row_rect(int vi)
{
	return (bu_rect){ 16, 222 + vi * 44, 700, 40 };
}

static void draw_waveform(bdapp *a, struct recorder *rc, bu_rect wr,
			  const float *peaks, int npeaks, double pos, double dur)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bu_fill_round_rect(c, wr, 6, t->surface);
	if (npeaks > 0) {
		int mid = wr.y + wr.h / 2;
		for (int x = 0; x < wr.w; x++) {
			int idx = x * npeaks / wr.w;
			float p = peaks[idx];
			int h = (int)(p * (wr.h / 2 - 2));
			if (h < 1)
				h = 1;
			bu_color col = t->accent;
			if (rc->trim_mode) {
				double tt = dur * x / wr.w;
				if (tt < rc->trim_s || tt > rc->trim_e)
					col = t->text_faint;
			} else if (dur > 0 && pos > 0) {
				double tt = dur * x / wr.w;
				if (tt > pos)
					col = t->text_dim;
			}
			bu_fill_rect(c, (bu_rect){ wr.x + x, mid - h, 1, h * 2 }, col);
		}
	}
	if (rc->trim_mode && dur > 0) {
		int xs = wr.x + (int)(rc->trim_s / dur * wr.w);
		int xe = wr.x + (int)(rc->trim_e / dur * wr.w);
		bu_fill_rect(c, (bu_rect){ xs - 2, wr.y, 4, wr.h }, t->warn);
		bu_fill_rect(c, (bu_rect){ xe - 2, wr.y, 4, wr.h }, t->warn);
	} else if (dur > 0 && pos > 0) {
		int xp = wr.x + (int)(pos / dur * wr.w);
		bu_fill_rect(c, (bu_rect){ xp, wr.y, 2, wr.h }, t->text);
	}
}

static void draw_panel(bdapp *a, struct recorder *rc)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	struct rec *r = &rc->recs[rc->sel];
	bu_draw_text(c, t->font, 16, 320, r->name, 13.5, t->text);
	bu_button(c, t, (bu_rect){ 472, 314, 108, 32 }, "名前変更", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 588, 314, 108, 32 }, rc->trim_mode ? "編集中…" : "トリミング",
		  rc->trim_mode ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 704, 314, 128, 32 }, "閉じる", BU_BTN_NORMAL, false);
	double pos = play_pos(rc);
	double dur = r->duration > 0 ? r->duration : 0;
	draw_waveform(a, rc, wave_rect(), rc->peaks, rc->npeaks, pos, dur);
	if (rc->trim_mode) {
		bu_button(c, t, (bu_rect){ 16, 418, 150, 40 }, "選択を残す", BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ 174, 418, 150, 40 }, "選択を削除", BU_BTN_DANGER, false);
		bu_button(c, t, (bu_rect){ 332, 418, 120, 40 }, "キャンセル", BU_BTN_NORMAL, false);
		char tb[96];
		snprintf(tb, sizeof(tb), "%d:%02d – %d:%02d (%d秒)",
			 (int)rc->trim_s / 60, (int)rc->trim_s % 60,
			 (int)rc->trim_e / 60, (int)rc->trim_e % 60,
			 (int)(rc->trim_e - rc->trim_s));
		bu_draw_text_right(c, t->font, (bu_rect){ 460, 428, 388, 22 }, tb, 13, t->text_dim);
		return;
	}
	bu_button(c, t, (bu_rect){ 16, 418, 72, 40 }, "-15秒", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 96, 418, 96, 40 },
		  rc->play_pid > 0 && !rc->play_paused ? "一時停止" : "▶ 再生", BU_BTN_PRIMARY, false);
	bu_button(c, t, (bu_rect){ 200, 418, 72, 40 }, "+15秒", BU_BTN_NORMAL, false);
	char pb[48];
	snprintf(pb, sizeof(pb), "%d:%02d / %d:%02d",
		 (int)pos / 60, (int)pos % 60,
		 r->duration / 60, r->duration < 0 ? 0 : r->duration % 60);
	bu_draw_text(c, t->font, 284, 428, pb, 13, t->text_dim);
	float sp = speed_of(rc, r->name);
	bu_draw_text(c, t->font, 420, 428, "速度", 13, t->text_dim);
	bu_slider(c, t, (bu_rect){ 462, 426, 170, 24 }, (sp - 0.5f) / 1.5f, false);
	char sv[16];
	snprintf(sv, sizeof(sv), "%.2f×", sp);
	bu_draw_text(c, t->font, 644, 428, sv, 13, t->text);
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
	int bcx = card.x + 70, bcy = card.y + 64;
	bu_fill_circle(c, bcx, bcy, 34, recording ? t->danger : t->surface3);
	bu_fill_circle(c, bcx, bcy, 26, recording ? BU_RGB(255, 255, 255) : t->danger);
	if (recording) {
		int el = rec_elapsed(rc);
		char tb[32];
		snprintf(tb, sizeof(tb), "%d:%02d", el / 60, el % 60);
		bu_draw_text(c, t->font, card.x + 130, card.y + 22,
			     rc->rec_paused ? "一時停止中" : "録音中",
			     18, rc->rec_paused ? t->warn : t->danger);
		bu_draw_text(c, t->font, card.x + 130, card.y + 56, tb, 26, t->text);
		/* live waveform */
		bu_rect wr = { card.x + 330, card.y + 18, 330, 60 };
		bu_fill_round_rect(c, wr, 6, t->surface);
		if (rc->live_n > 0) {
			int mid = wr.y + wr.h / 2;
			int show = rc->live_n < wr.w ? rc->live_n : wr.w;
			for (int x = 0; x < show; x++) {
				int idx = rc->live_n - show + x;
				int h = (int)(rc->live_peaks[idx] * (wr.h / 2 - 2));
				if (h < 1)
					h = 1;
				bu_fill_rect(c, (bu_rect){ wr.x + x, mid - h, 1, h * 2 }, t->danger);
			}
		}
		bu_button(c, t, (bu_rect){ card.x + 676, card.y + 42, 140, 44 },
			  rc->rec_paused ? "再開" : "一時停止", BU_BTN_NORMAL, false);
	} else {
		bu_draw_text(c, t->font, card.x + 130, card.y + 22, "ボイスレコーダー", 18, t->text);
		bu_draw_text(c, t->font, card.x + 130, card.y + 56, "赤いボタンで録音開始", 13, t->text_dim);
		static const int rates[] = { 44100, 22050, 16000 };
		static const char *qlab[] = { "高音質 44.1k", "標準 22k", "長時間 16k" };
		for (int i = 0; i < 3; i++)
			bu_button(c, t, (bu_rect){ card.x + 380 + i * 148, card.y + 40, 140, 42 }, qlab[i],
				  rc->rate == rates[i] ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	}
	if (rc->status[0] && !recording)
		bu_draw_text(c, t->font, card.x + 130, card.y + 100, rc->status, 12, t->warn);
	/* list header */
	bu_draw_text(c, t->font, 16, 196, rc->trash_view ? "最近削除した項目" : "録音一覧", 14, t->text_dim);
	if (rc->filter[0]) {
		char fb[96];
		snprintf(fb, sizeof(fb), "検索: %s ×", rc->filter);
		bu_button(c, t, (bu_rect){ 150, 190, 260, 30 }, fb, BU_BTN_PRIMARY, false);
	}
	bu_button(c, t, (bu_rect){ 560, 190, 110, 30 }, "検索", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 684, 190, 150, 30 }, rc->trash_view ? "録音一覧へ" : "最近削除", BU_BTN_NORMAL, false);
	/* list */
	bool panel = rc->sel >= 0 && !rc->trash_view;
	int visible = panel ? 2 : 5;
	for (int vi = 0; vi < visible; vi++) {
		int i = rc->scroll + vi;
		if (i >= rc->n)
			break;
		struct rec *r = &rc->recs[i];
		bu_rect row = row_rect(vi);
		bu_fill_round_rect(c, row, 8,
				   rc->play_idx == i || rc->sel == i ? t->surface3 : t->surface);
		bu_icon_mic(c, row.x + 22, row.y + 20, t->text_dim);
		bu_draw_text(c, t->font, row.x + 44, row.y + 10, r->name, 13, t->text);
		char meta[96];
		struct tm tm;
		localtime_r(&r->mtime, &tm);
		if (r->duration >= 0)
			snprintf(meta, sizeof(meta), "%d/%02d %02d:%02d · %d:%02d · %.0f KB",
				 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
				 r->duration / 60, r->duration % 60, r->size / 1024.0);
		else
			snprintf(meta, sizeof(meta), "%d/%02d %02d:%02d · %.0f KB",
				 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, r->size / 1024.0);
		bu_draw_text_right(c, t->font, (bu_rect){ row.x, row.y + 10, row.w - 130, 20 }, meta, 12, t->text_faint);
		if (rc->trash_view) {
			bu_button(c, t, (bu_rect){ 724, 222 + vi * 44, 62, 40 }, "復元", BU_BTN_NORMAL, false);
			bu_button(c, t, (bu_rect){ 792, 222 + vi * 44, 56, 40 },
				  rc->confirm_delete && rc->del_idx == i ? "本当に?" : "消去", BU_BTN_DANGER, false);
		} else {
			bu_button(c, t, (bu_rect){ 724, 222 + vi * 44, 62, 40 },
				  rc->play_idx == i ? "■" : "▶", BU_BTN_NORMAL, false);
			bu_button(c, t, (bu_rect){ 792, 222 + vi * 44, 56, 40 },
				  rc->confirm_delete && rc->del_idx == i ? "本当に?" : "削除", BU_BTN_DANGER, false);
		}
	}
	bu_button(c, t, (bu_rect){ 724, 448, 56, 26 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 792, 448, 56, 26 }, "▼", BU_BTN_NORMAL, false);
	if (rc->n == 0)
		bu_draw_text(c, t->font, 16, 240,
			     rc->trash_view ? "最近削除した項目はありません" :
			     rc->filter[0] ? "検索に一致する録音がありません" : "まだ録音がありません",
			     13, t->text_dim);
	if (panel)
		draw_panel(a, rc);
	/* OSK overlay */
	if (rc->osk_mode != OSKM_NONE) {
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		bu_fill_rect(c, (bu_rect){ 0, area.y - 38, a->w, 38 }, t->panel);
		char lb[192];
		snprintf(lb, sizeof(lb), "%s: %s",
			 rc->osk_mode == OSKM_RENAME ? "名前変更" : "検索", rc->edit);
		bu_draw_text(c, t->font, 14, area.y - 28, lb, 14, t->text);
		bu_button(c, t, (bu_rect){ a->w - 124, area.y - 34, 110, 30 }, "キャンセル", BU_BTN_NORMAL, false);
		bu_osk_draw(c, t, &rc->osk, area);
	}
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void edit_backspace(char *buf)
{
	int l = (int)strlen(buf);
	if (l == 0)
		return;
	l--;
	while (l > 0 && (buf[l] & 0xC0) == 0x80)
		l--;
	buf[l] = 0;
}

static void osk_commit(struct recorder *rc)
{
	if (rc->osk_mode == OSKM_RENAME && rc->sel >= 0) {
		char flushed[32];
		const char *tail = bu_osk_flush(&rc->osk, flushed, sizeof(flushed));
		if (tail && tail[0] && strlen(rc->edit) + strlen(tail) < sizeof(rc->edit))
			strcat(rc->edit, tail);
		/* sanitize: no slashes, no leading dot */
		char clean[128];
		int j = 0;
		for (int i = 0; rc->edit[i] && j < 100; i++) {
			if (rc->edit[i] == '/' || (j == 0 && rc->edit[i] == '.'))
				continue;
			clean[j++] = rc->edit[i];
		}
		clean[j] = 0;
		if (clean[0]) {
			char old[640], nw[640], newname[160];
			snprintf(newname, sizeof(newname), "%s.wav", clean);
			snprintf(old, sizeof(old), "%s/%s", rc->dir, rc->recs[rc->sel].name);
			snprintf(nw, sizeof(nw), "%s/%s", rc->dir, newname);
			if (strcmp(newname, rc->recs[rc->sel].name) == 0) {
				/* unchanged */
			} else if (access(nw, F_OK) == 0) {
				snprintf(rc->status, sizeof(rc->status), "同名のファイルがあります");
			} else if (rename(old, nw) == 0) {
				/* carry the speed setting to the new name */
				for (int i = 0; i < rc->nspeeds; i++)
					if (strcmp(rc->speeds[i].name, rc->recs[rc->sel].name) == 0) {
						snprintf(rc->speeds[i].name, 128, "%s", newname);
						speeds_save(rc);
					}
				snprintf(rc->status, sizeof(rc->status), "名前を変更しました");
				char keep[160];
				snprintf(keep, sizeof(keep), "%s", newname);
				load_list(rc);
				for (int i = 0; i < rc->n; i++)
					if (strcmp(rc->recs[i].name, keep) == 0) {
						rc->sel = i;
						load_peaks(rc);
					}
			} else {
				snprintf(rc->status, sizeof(rc->status), "名前を変更できませんでした");
			}
		}
	} else if (rc->osk_mode == OSKM_SEARCH) {
		snprintf(rc->filter, sizeof(rc->filter), "%s", rc->edit);
		load_list(rc);
	}
	rc->osk_mode = OSKM_NONE;
}

static void move_to_trash(struct recorder *rc, int i)
{
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", rc->trashdir);
	if (system(cmd) != 0)
		return;
	char from[640], to[640];
	snprintf(from, sizeof(from), "%s/%s", rc->dir, rc->recs[i].name);
	snprintf(to, sizeof(to), "%s/%s", rc->trashdir, rc->recs[i].name);
	if (access(to, F_OK) == 0) {
		/* name clash in trash: suffix with a counter */
		char alt[640];
		for (int k = 2; k < 100; k++) {
			snprintf(alt, sizeof(alt), "%s/%.*s-%d.wav", rc->trashdir,
				 (int)strlen(rc->recs[i].name) - 4, rc->recs[i].name, k);
			if (access(alt, F_OK) != 0) {
				snprintf(to, sizeof(to), "%s", alt);
				break;
			}
		}
	}
	if (rename(from, to) == 0)
		snprintf(rc->status, sizeof(rc->status), "最近削除した項目へ移動しました");
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct recorder *rc = ctx;
	(void)quit;
	int x = ev->x, y = ev->y;
	/* waveform dragging */
	if (ev->type == BU_EV_POINTER_UP) {
		rc->drag = DRAG_NONE;
		return;
	}
	if (ev->type == BU_EV_POINTER_MOVE) {
		if (rc->drag != DRAG_NONE && rc->sel >= 0) {
			bu_rect wr = wave_rect();
			struct rec *r = &rc->recs[rc->sel];
			if (r->duration > 0) {
				double tt = (double)(x - wr.x) / wr.w * r->duration;
				if (tt < 0) tt = 0;
				if (tt > r->duration) tt = r->duration;
				if (rc->drag == DRAG_SEEK)
					seek_play(rc, tt);
				else if (rc->drag == DRAG_TS) {
					rc->trim_s = tt < rc->trim_e ? tt : rc->trim_e;
				} else if (rc->drag == DRAG_TE) {
					rc->trim_e = tt > rc->trim_s ? tt : rc->trim_s;
				}
				*changed = true;
			}
		}
		return;
	}
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	*changed = true;
	/* OSK overlay captures everything */
	if (rc->osk_mode != OSKM_NONE) {
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		if (inr(x, y, (bu_rect){ a->w - 124, area.y - 34, 110, 30 })) {
			if (rc->osk_mode == OSKM_SEARCH) {
				/* cancel restores the previous (empty) filter state */
			}
			rc->osk_mode = OSKM_NONE;
			return;
		}
		if (y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&rc->osk, area, x, y, out, sizeof(out));
			if (r == OSK_TEXT && out[0]) {
				if (strlen(rc->edit) + strlen(out) < sizeof(rc->edit)) {
					strcat(rc->edit, out);
					if (rc->osk_mode == OSKM_SEARCH) {
						snprintf(rc->filter, sizeof(rc->filter), "%s", rc->edit);
						load_list(rc);
					}
				}
			} else if (r == OSK_BACKSPACE) {
				edit_backspace(rc->edit);
				if (rc->osk_mode == OSKM_SEARCH) {
					snprintf(rc->filter, sizeof(rc->filter), "%s", rc->edit);
					load_list(rc);
				}
			} else if (r == OSK_ENTER) {
				osk_commit(rc);
			}
			return;
		}
		return;
	}
	/* big record button */
	if (inr(x, y, (bu_rect){ 16 + 70 - 36, 54 + 64 - 36, 72, 72 })) {
		if (rc->pid > 0) {
			if (rc->rec_paused) {	/* finalize cleanly */
				kill(rc->pid, SIGCONT);
				rc->pause_accum += (int)(time(NULL) - rc->pause_start);
				rc->rec_paused = false;
			}
			kill(rc->pid, SIGINT); /* arecord finalizes the WAV on SIGINT */
		} else {
			stop_playback(rc);
			rc->sel = -1;
			char name[160];
			time_t now = time(NULL);
			struct tm tm;
			localtime_r(&now, &tm);
			snprintf(name, sizeof(name), "rec-%04d%02d%02d-%02d%02d%02d.wav",
				 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
			snprintf(rc->cur_path, sizeof(rc->cur_path), "%s/%s", rc->dir, name);
			char rate[16];
			snprintf(rate, sizeof(rate), "%d", rc->rate);
			pid_t pid = fork();
			if (pid == 0) {
				execlp("arecord", "arecord", "-D", "hw:0,1", "-f", "S16_LE", "-c", "2", "-r", rate, "-q", rc->cur_path, (char *)NULL);
				_exit(127);
			}
			rc->pid = pid;
			rc->started = now;
			rc->rec_paused = false;
			rc->pause_accum = 0;
			rc->rec_read_off = 0;
			rc->live_n = 0;
			snprintf(rc->status, sizeof(rc->status), "録音しています…");
		}
		return;
	}
	/* record pause/resume */
	if (rc->pid > 0 && inr(x, y, (bu_rect){ 16 + 676, 54 + 42, 140, 44 })) {
		if (rc->rec_paused) {
			kill(rc->pid, SIGCONT);
			rc->pause_accum += (int)(time(NULL) - rc->pause_start);
			rc->rec_paused = false;
		} else {
			kill(rc->pid, SIGSTOP);
			rc->pause_start = time(NULL);
			rc->rec_paused = true;
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
	/* list header buttons */
	if (inr(x, y, (bu_rect){ 560, 190, 110, 30 })) {
		bu_osk_init(&rc->osk);
		rc->osk.kana = false;
		rc->osk_mode = OSKM_SEARCH;
		snprintf(rc->edit, sizeof(rc->edit), "%s", rc->filter);
		return;
	}
	if (inr(x, y, (bu_rect){ 684, 190, 150, 30 })) {
		stop_playback(rc);
		rc->trash_view = !rc->trash_view;
		rc->scroll = 0;
		rc->confirm_delete = false;
		load_list(rc);
		return;
	}
	if (rc->filter[0] && inr(x, y, (bu_rect){ 150, 190, 260, 30 })) {
		rc->filter[0] = 0;
		load_list(rc);
		return;
	}
	/* selection panel */
	bool panel = rc->sel >= 0 && !rc->trash_view;
	if (panel) {
		struct rec *r = &rc->recs[rc->sel];
		if (inr(x, y, (bu_rect){ 704, 314, 128, 32 })) {
			stop_playback(rc);
			rc->sel = -1;
			rc->trim_mode = false;
			return;
		}
		if (inr(x, y, (bu_rect){ 472, 314, 108, 32 })) {
			bu_osk_init(&rc->osk);
			rc->osk.kana = false;
			rc->osk_mode = OSKM_RENAME;
			snprintf(rc->edit, sizeof(rc->edit), "%s", r->name);
			char *dot = strrchr(rc->edit, '.');
			if (dot)
				*dot = 0;
			return;
		}
		if (inr(x, y, (bu_rect){ 588, 314, 108, 32 })) {
			rc->trim_mode = !rc->trim_mode;
			if (rc->trim_mode && r->duration > 0) {
				rc->trim_s = 0;
				rc->trim_e = r->duration;
			}
			return;
		}
		bu_rect wr = wave_rect();
		if (inr(x, y, wr) && r->duration > 0) {
			double tt = (double)(x - wr.x) / wr.w * r->duration;
			if (rc->trim_mode) {
				int xs = wr.x + (int)(rc->trim_s / r->duration * wr.w);
				int xe = wr.x + (int)(rc->trim_e / r->duration * wr.w);
				if (abs(x - xs) <= abs(x - xe)) {
					rc->drag = DRAG_TS;
					rc->trim_s = tt < rc->trim_e ? tt : rc->trim_e;
				} else {
					rc->drag = DRAG_TE;
					rc->trim_e = tt > rc->trim_s ? tt : rc->trim_s;
				}
			} else {
				rc->drag = DRAG_SEEK;
				seek_play(rc, tt);
			}
			return;
		}
		if (rc->trim_mode) {
			char full[640];
			snprintf(full, sizeof(full), "%s/%s", rc->dir, r->name);
			if (inr(x, y, (bu_rect){ 16, 418, 150, 40 }) ||
			    inr(x, y, (bu_rect){ 174, 418, 150, 40 })) {
				bool keep = inr(x, y, (bu_rect){ 16, 418, 150, 40 });
				stop_playback(rc);
				if (wav_trim(full, r, rc->trim_s, rc->trim_e, keep)) {
					snprintf(rc->status, sizeof(rc->status),
						 keep ? "選択部分を残して保存しました" : "選択部分を削除して保存しました");
					rc->trim_mode = false;
					char keep_name[128];
					snprintf(keep_name, sizeof(keep_name), "%s", r->name);
					load_list(rc);
					for (int i = 0; i < rc->n; i++)
						if (strcmp(rc->recs[i].name, keep_name) == 0) {
							rc->sel = i;
							load_peaks(rc);
						}
					rc->pos_base = 0;
				} else {
					snprintf(rc->status, sizeof(rc->status), "トリミングに失敗しました");
				}
				return;
			}
			if (inr(x, y, (bu_rect){ 332, 418, 120, 40 })) {
				rc->trim_mode = false;
				return;
			}
		} else {
			if (inr(x, y, (bu_rect){ 16, 418, 72, 40 })) {
				seek_play(rc, play_pos(rc) - 15);
				return;
			}
			if (inr(x, y, (bu_rect){ 96, 418, 96, 40 })) {
				play_pause_toggle(rc);
				return;
			}
			if (inr(x, y, (bu_rect){ 200, 418, 72, 40 })) {
				seek_play(rc, play_pos(rc) + 15);
				return;
			}
			if (inr(x, y, (bu_rect){ 462, 426, 170, 24 })) {
				float sp = 0.5f + (float)(x - 462) / 170.0f * 1.5f;
				if (sp < 0.5f) sp = 0.5f;
				if (sp > 2.0f) sp = 2.0f;
				sp = (float)((int)(sp * 20 + 0.5f)) / 20.0f;	/* 0.05 steps */
				bool was_playing = rc->play_pid > 0;
				bool was_paused = rc->play_paused;
				double pos = play_pos(rc);
				speed_set(rc, r->name, sp);
				if (was_playing) {
					start_play(rc, rc->sel, pos);
					if (was_paused && rc->play_pid > 0) {
						kill(rc->play_pid, SIGSTOP);
						rc->play_paused = true;
						rc->frozen = pos;
					}
				}
				return;
			}
		}
	}
	/* scroll buttons */
	if (inr(x, y, (bu_rect){ 724, 448, 56, 26 })) {
		if (rc->scroll >= 2) rc->scroll -= 2; else rc->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ 792, 448, 56, 26 })) {
		if (rc->scroll + 5 < rc->n) rc->scroll += 2;
		return;
	}
	/* list rows */
	int visible = panel ? 2 : 5;
	for (int vi = 0; vi < visible; vi++) {
		int i = rc->scroll + vi;
		if (i >= rc->n)
			break;
		if (inr(x, y, (bu_rect){ 724, 222 + vi * 44, 62, 40 })) {
			if (rc->trash_view) {
				/* restore */
				char from[640], to[640];
				snprintf(from, sizeof(from), "%s/%s", rc->trashdir, rc->recs[i].name);
				snprintf(to, sizeof(to), "%s/%s", rc->dir, rc->recs[i].name);
				if (access(to, F_OK) == 0)
					snprintf(rc->status, sizeof(rc->status), "同名の録音があるため復元できません");
				else if (rename(from, to) == 0) {
					snprintf(rc->status, sizeof(rc->status), "復元しました");
					load_list(rc);
				}
			} else if (rc->play_idx == i) {
				stop_playback(rc);
			} else {
				rc->sel = i;
				rc->pos_base = 0;
				load_peaks(rc);
				start_play(rc, i, 0);
			}
			return;
		}
		if (inr(x, y, (bu_rect){ 792, 222 + vi * 44, 56, 40 })) {
			if (rc->confirm_delete && rc->del_idx == i) {
				rc->confirm_delete = false;
				if (rc->trash_view) {
					char full[640];
					snprintf(full, sizeof(full), "%s/%s", rc->trashdir, rc->recs[i].name);
					remove(full);
					snprintf(rc->status, sizeof(rc->status), "完全に削除しました");
				} else {
					if (rc->play_idx == i)
						stop_playback(rc);
					if (rc->sel == i) {
						rc->sel = -1;
						rc->trim_mode = false;
					}
					move_to_trash(rc, i);
				}
				load_list(rc);
			} else {
				rc->confirm_delete = true;
				rc->del_idx = i;
			}
			return;
		}
		if (!rc->trash_view && inr(x, y, row_rect(vi))) {
			rc->sel = i;
			rc->pos_base = 0;
			rc->trim_mode = false;
			load_peaks(rc);
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
	rc.sel = -1;
	rc.del_idx = -1;
	rc.rate = 44100;
	const char *home = getenv("HOME");
	snprintf(rc.dir, sizeof(rc.dir), "%s/Recordings", home ? home : "/home/user");
	snprintf(rc.trashdir, sizeof(rc.trashdir), "%s/.trash", rc.dir);
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", rc.dir);
	if (system(cmd) == 0)
		load_list(&rc);
	speeds_load(&rc);
	/* shot hooks (host verification) */
	const char *selname = getenv("BD_REC_SEL");
	if (selname) {
		for (int i = 0; i < rc.n; i++)
			if (strstr(rc.recs[i].name, selname)) {
				rc.sel = i;
				load_peaks(&rc);
				if (getenv("BD_REC_TRIM") && rc.recs[i].duration > 0) {
					rc.trim_mode = true;
					rc.trim_s = rc.recs[i].duration * 0.2;
					rc.trim_e = rc.recs[i].duration * 0.8;
				}
				break;
			}
	}
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-recorder.ppm" : NULL;
	return bdapp_run("レコーダー", shot, draw, onevent, tick, &rc);
}
