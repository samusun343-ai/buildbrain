/* bd-notes: notes for braindesk.
 * Plain-text notes in ~/Notes, edited with the on-screen
 * keyboard (romaji->kana conversion included) or the physical
 * keyboard. One file per note, first line is the title.
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define MAXNOTES 64
#define TEXTMAX (64 * 1024)

struct notes {
	char dir[512];
	char names[MAXNOTES][256];
	int n, sel;
	bool editing;
	char path[640];
	char *text;
	int len;
	bu_osk osk;
	bool confirm_delete;
	char status[96];
};

static void load_list(struct notes *nt)
{
	nt->n = 0;
	nt->sel = -1;
	nt->confirm_delete = false;
	mkdir(nt->dir, 0755);
	DIR *d = opendir(nt->dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && nt->n < MAXNOTES) {
		const char *nm = de->d_name;
		size_t l = strlen(nm);
		if (l > 4 && strcmp(nm + l - 4, ".txt") == 0)
			snprintf(nt->names[nt->n++], 256, "%s", nm);
	}
	closedir(d);
	/* newest first by name (names start with a timestamp) */
	for (int i = 0; i < nt->n; i++)
		for (int j = i + 1; j < nt->n; j++)
			if (strcmp(nt->names[i], nt->names[j]) < 0) {
				char tmp[256];
				snprintf(tmp, 256, "%s", nt->names[i]);
				snprintf(nt->names[i], 256, "%s", nt->names[j]);
				snprintf(nt->names[j], 256, "%s", tmp);
			}
}

static void open_note(struct notes *nt, const char *name)
{
	snprintf(nt->path, sizeof(nt->path), "%s/%s", nt->dir, name);
	FILE *fp = fopen(nt->path, "r");
	nt->len = 0;
	nt->text[0] = 0;
	if (fp) {
		nt->len = (int)fread(nt->text, 1, TEXTMAX - 1, fp);
		nt->text[nt->len] = 0;
		fclose(fp);
	}
	nt->editing = true;
	bu_osk_init(&nt->osk);
}

static void save_note(struct notes *nt)
{
	char flushbuf[16];
	if (nt->osk.pending_len) {
		const char *fl = bu_osk_flush(&nt->osk, flushbuf, sizeof(flushbuf));
		int fl2 = (int)strlen(fl);
		if (nt->len + fl2 < TEXTMAX - 1) {
			memcpy(nt->text + nt->len, fl, fl2);
			nt->len += fl2;
			nt->text[nt->len] = 0;
		}
	}
	FILE *fp = fopen(nt->path, "w");
	if (fp) {
		fwrite(nt->text, 1, nt->len, fp);
		fclose(fp);
		snprintf(nt->status, sizeof(nt->status), "保存しました");
	}
}

static void append_text(struct notes *nt, const char *utf8)
{
	int l = (int)strlen(utf8);
	if (nt->len + l >= TEXTMAX - 1)
		return;
	memcpy(nt->text + nt->len, utf8, l);
	nt->len += l;
	nt->text[nt->len] = 0;
}

static void backspace(struct notes *nt)
{
	if (nt->len == 0)
		return;
	int i = nt->len - 1;
	while (i > 0 && (nt->text[i] & 0xC0) == 0x80)
		i--;
	nt->len = i;
	nt->text[nt->len] = 0;
}

static void title_of(const char *path, char *out, int n)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		snprintf(out, n, "(空)");
		return;
	}
	if (!fgets(out, n, fp))
		snprintf(out, n, "(空)");
	fclose(fp);
	out[strcspn(out, "\n")] = 0;
	if (!out[0])
		snprintf(out, n, "(無題)");
}

/* draw wrapped text; returns lines drawn */
static void draw_text(bdapp *a, struct notes *nt, bu_rect area)
{
	bu_theme *t = a->theme;
	float px = 15;
	int line_h = 24;
	int max_lines = area.h / line_h;
	int line = 0;
	int x = area.x, y = area.y;
	const char *p = nt->text;
	while (*p && line < max_lines) {
		if (*p == '\n') {
			p++;
			line++;
			x = area.x;
			y += line_h;
			continue;
		}
		/* take one UTF-8 char */
		int clen = (*p & 0x80) == 0 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : 4;
		char ch[8];
		memcpy(ch, p, clen);
		ch[clen] = 0;
		int w = bu_text_width(t->font, ch, px);
		if (x + w > area.x + area.w) {
			line++;
			x = area.x;
			y += line_h;
			if (line >= max_lines)
				break;
		}
		bu_draw_text(a->canvas, t->font, x, y, ch, px, t->text);
		x += w;
		p += clen;
	}
	/* caret */
	bu_fill_rect(a->canvas, (bu_rect){ x + 1, y + 3, 2, 17 }, t->accent);
	if (nt->osk.pending_len)
		bu_draw_text(a->canvas, t->font, x + 6, y, nt->osk.pending, 13, t->warn);
}

static void draw(bdapp *a, void *ctx)
{
	struct notes *nt = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	if (!nt->editing) {
		bu_button(c, t, (bu_rect){ 16, 54, 150, 38 }, "＋ 新規メモ", BU_BTN_PRIMARY, false);
		for (int i = 0; i < nt->n && i < 8; i++) {
			bu_rect r = { 12, 104 + i * 44, 700, 40 };
			bu_fill_round_rect(c, r, 8, i == nt->sel ? t->surface3 : t->surface);
			char path[640], title[128];
			snprintf(path, sizeof(path), "%s/%s", nt->dir, nt->names[i]);
			title_of(path, title, sizeof(title));
			bu_draw_text(c, t->font, r.x + 14, r.y + 9, title, 14.5, t->text);
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 9, r.w - 14, 22 }, nt->names[i], 11, t->text_faint);
		}
		if (nt->sel >= 0)
			bu_button(c, t, (bu_rect){ 724, 104, 128, 40 }, nt->confirm_delete ? "本当に削除?" : "削除",
				  BU_BTN_DANGER, false);
		if (nt->n == 0)
			bu_draw_text(c, t->font, 16, 120, "メモはまだありません。「＋ 新規メモ」から作成できます。", 14, t->text_dim);
	} else {
		bu_button(c, t, (bu_rect){ 16, 52, 110, 36 }, "← 一覧", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 140, 52, 110, 36 }, "保存", BU_BTN_PRIMARY, false);
		if (nt->status[0])
			bu_draw_text(c, t->font, 270, 60, nt->status, 13, t->text_dim);
		int oskh = bu_osk_height();
		bu_rect area = { 16, 100, a->w - 32, a->h - 100 - oskh - 8 };
		bu_fill_round_rect(c, area, 10, t->surface);
		bu_rect inner = { area.x + 12, area.y + 10, area.w - 24, area.h - 20 };
		draw_text(a, nt, inner);
		bu_osk_draw(c, t, &nt->osk, (bu_rect){ 0, a->h - oskh, a->w, oskh });
	}
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct notes *nt = ctx;
	if (!nt->editing) {
		if (ev->type != BU_EV_POINTER_DOWN)
			return;
		int x = ev->x, y = ev->y;
		*changed = true;
		if (x >= 16 && x < 166 && y >= 54 && y < 92) {
			char name[64];
			time_t now = time(NULL);
			struct tm tm;
			localtime_r(&now, &tm);
			snprintf(name, sizeof(name), "%04d%02d%02d-%02d%02d%02d.txt",
				 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
			open_note(nt, name);
			return;
		}
		if (nt->sel >= 0 && x >= 724 && x < 852 && y >= 104 && y < 144) {
			if (!nt->confirm_delete) {
				nt->confirm_delete = true;
			} else {
				char path[640];
				snprintf(path, sizeof(path), "%s/%s", nt->dir, nt->names[nt->sel]);
				remove(path);
				load_list(nt);
			}
			return;
		}
		for (int i = 0; i < nt->n && i < 8; i++) {
			bu_rect r = { 12, 104 + i * 44, 700, 40 };
			if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
				if (nt->sel == i)
					open_note(nt, nt->names[i]);
				else {
					nt->sel = i;
					nt->confirm_delete = false;
				}
				return;
			}
		}
		return;
	}
	/* editing */
	if (ev->type == BU_EV_POINTER_DOWN) {
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		if (ev->y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&nt->osk, area, ev->x, ev->y, out, sizeof(out));
			if (r == OSK_TEXT && out[0])
				append_text(nt, out);
			else if (r == OSK_BACKSPACE)
				backspace(nt);
			else if (r == OSK_ENTER)
				append_text(nt, "\n");
			*changed = true;
			return;
		}
		if (ev->x >= 16 && ev->x < 126 && ev->y >= 52 && ev->y < 88) {
			nt->editing = false;
			nt->status[0] = 0;
			load_list(nt);
			*changed = true;
			return;
		}
		if (ev->x >= 140 && ev->x < 250 && ev->y >= 52 && ev->y < 88) {
			save_note(nt);
			*changed = true;
			return;
		}
	}
	if (ev->type == BU_EV_KEY && ev->value == 1) {
		/* physical keyboard: letters/digits via a small map */
		static const char *row_digits = "1234567890";
		char buf[2] = { 0, 0 };
		bool handled = true;
		if (ev->code >= 2 && ev->code <= 11)
			buf[0] = row_digits[ev->code - 2];
		else if (ev->code == 14)
			backspace(nt);
		else if (ev->code == 28)
			append_text(nt, "\n");
		else if (ev->code == 57)
			buf[0] = ' ';
		else
			handled = false;
		if (buf[0])
			append_text(nt, buf);
		if (handled)
			*changed = true;
	}
}

int main(int argc, char **argv)
{
	struct notes nt;
	memset(&nt, 0, sizeof(nt));
	const char *home = getenv("HOME");
	snprintf(nt.dir, sizeof(nt.dir), "%s/Notes", home ? home : "/home/user");
	nt.text = malloc(TEXTMAX);
	nt.sel = -1;
	load_list(&nt);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-notes.ppm" : NULL;
	return bdapp_run("メモ", shot, draw, onevent, &nt);
}
