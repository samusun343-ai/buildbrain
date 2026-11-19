/* bd-notes: notes for braindesk (v2).
 * Two-pane layout: note list on the left (with search), the
 * editor on the right. Notes are plain text in ~/Notes, one
 * file per note, first line is the title. Editing auto-saves
 * (on switch, on close, and 2 s after the last change).
 * Input: on-screen keyboard (romaji->kana) or physical keys.
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define MAXNOTES 128
#define TEXTMAX 16384

struct notes {
	char dir[512];
	char files[MAXNOTES][256];
	time_t mtimes[MAXNOTES];
	int n, scroll, current;
	char text[TEXTMAX];
	int len;
	bool dirty;
	time_t last_edit;
	char search[64];
	bu_osk osk;
	bool osk_open;
	char status[96];
};

static void note_path(struct notes *nt, int i, char *out, size_t n)
{
	snprintf(out, n, "%s/%s", nt->dir, nt->files[i]);
}

static void load_list(struct notes *nt)
{
	DIR *d = opendir(nt->dir);
	nt->n = 0;
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && nt->n < MAXNOTES) {
		const char *dot = strrchr(de->d_name, '.');
		if (!dot || strcmp(dot, ".txt") != 0)
			continue;
		if (nt->search[0]) {
			/* filter by file name or first line */
			char path[768];
			snprintf(path, sizeof(path), "%s/%s", nt->dir, de->d_name);
			FILE *fp = fopen(path, "r");
			char first[128] = "";
			if (fp) {
				if (!fgets(first, sizeof(first), fp)) first[0] = 0;
				fclose(fp);
			}
			if (!strstr(de->d_name, nt->search) && !strstr(first, nt->search))
				continue;
		}
		snprintf(nt->files[nt->n], 256, "%s", de->d_name);
		char path[768];
		note_path(nt, nt->n, path, sizeof(path));
		struct stat st;
		nt->mtimes[nt->n] = stat(path, &st) == 0 ? st.st_mtime : 0;
		nt->n++;
	}
	closedir(d);
	/* newest first */
	for (int i = 0; i < nt->n; i++)
		for (int j = i + 1; j < nt->n; j++)
			if (nt->mtimes[j] > nt->mtimes[i]) {
				char tf[256];
				snprintf(tf, sizeof(tf), "%s", nt->files[i]);
				snprintf(nt->files[i], 256, "%s", nt->files[j]);
				snprintf(nt->files[j], 256, "%s", tf);
				time_t tt = nt->mtimes[i];
				nt->mtimes[i] = nt->mtimes[j];
				nt->mtimes[j] = tt;
			}
}

static void save_current(struct notes *nt)
{
	if (nt->current < 0 || !nt->dirty)
		return;
	char path[768];
	note_path(nt, nt->current, path, sizeof(path));
	FILE *fp = fopen(path, "w");
	if (fp) {
		fwrite(nt->text, 1, nt->len, fp);
		fclose(fp);
		nt->dirty = false;
		snprintf(nt->status, sizeof(nt->status), "保存しました");
	}
}

static void open_note(struct notes *nt, int i)
{
	save_current(nt);
	nt->current = i;
	nt->len = 0;
	nt->text[0] = 0;
	nt->dirty = false;
	if (i < 0)
		return;
	char path[768];
	note_path(nt, i, path, sizeof(path));
	FILE *fp = fopen(path, "r");
	if (fp) {
		nt->len = (int)fread(nt->text, 1, TEXTMAX - 1, fp);
		nt->text[nt->len] = 0;
		fclose(fp);
	}
	snprintf(nt->status, sizeof(nt->status), "%s", nt->files[i]);
}

static void new_note(struct notes *nt)
{
	save_current(nt);
	char name[256];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	snprintf(name, sizeof(name), "note-%04d%02d%02d-%02d%02d%02d.txt",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	char path[768];
	snprintf(path, sizeof(path), "%s/%s", nt->dir, name);
	FILE *fp = fopen(path, "w");
	if (fp)
		fclose(fp);
	nt->search[0] = 0; /* the new note must be visible in the list */
	load_list(nt);
	for (int i = 0; i < nt->n; i++)
		if (strcmp(nt->files[i], name) == 0) {
			open_note(nt, i);
			break;
		}
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct notes *nt = ctx;
	if (nt->dirty && time(NULL) - nt->last_edit >= 2) {
		save_current(nt);
		*changed = true;
	}
}

/* --- layout --- */
#define LIST_X 12
#define LIST_W 252
#define EDIT_X 276
#define EDIT_W (864 - EDIT_X - 12)

static void first_line(const char *path, char *out, size_t n)
{
	FILE *fp = fopen(path, "r");
	out[0] = 0;
	if (fp) {
		if (!fgets(out, (int)n, fp))
			out[0] = 0;
		fclose(fp);
	}
	int l = (int)strlen(out);
	while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r'))
		out[--l] = 0;
	if (!out[0])
		snprintf(out, n, "(無題)");
}

static void draw(bdapp *a, void *ctx)
{
	struct notes *nt = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* toolbar */
	bu_button(c, t, (bu_rect){ LIST_X, 52, 120, 38 }, "＋ 新規", BU_BTN_PRIMARY, false);
	bu_button(c, t, (bu_rect){ 140, 52, 124, 38 }, "検索", BU_BTN_NORMAL, false);
	if (nt->search[0]) {
		char sb[96];
		snprintf(sb, sizeof(sb), "「%s」で絞り込み中", nt->search);
		bu_draw_text(c, t->font, EDIT_X, 62, sb, 12.5, t->accent);
	}
	/* list */
	bu_rect lp = { LIST_X, 98, LIST_W, 330 };
	bu_fill_round_rect(c, lp, 10, t->surface);
	int visible = 7;
	for (int vi = 0; vi < visible; vi++) {
		int i = nt->scroll + vi;
		if (i >= nt->n)
			break;
		bu_rect r = { lp.x + 6, lp.y + 6 + vi * 46, lp.w - 12, 42 };
		if (i == nt->current)
			bu_fill_round_rect(c, r, 7, t->surface3);
		char path[768], title[128];
		note_path(nt, i, path, sizeof(path));
		first_line(path, title, sizeof(title));
		bu_draw_text(c, t->font, r.x + 10, r.y + 3, title, 13, t->text);
		struct tm tm;
		localtime_r(&nt->mtimes[i], &tm);
		char dt[32];
		snprintf(dt, sizeof(dt), "%d/%02d/%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
		bu_draw_text(c, t->font, r.x + 10, r.y + 22, dt, 11, t->text_faint);
	}
	bu_button(c, t, (bu_rect){ LIST_X, 434, 120, 36 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 140, 434, 124, 36 }, "▼", BU_BTN_NORMAL, false);
	/* editor */
	bu_rect ep = { EDIT_X, 98, EDIT_W, 330 };
	bu_fill_round_rect(c, ep, 10, t->surface);
	if (nt->current >= 0) {
		/* wrap text into lines of the editor width */
		int maxw = ep.w - 28;
		int y = ep.y + 12;
		const char *s = nt->text;
		char line[256];
		int li = 0;
		for (int i = 0; ; i++) {
			char ch = s[i];
			if (ch == '\n' || ch == 0 || li >= 250) {
				line[li] = 0;
				/* hard-wrap long lines by width */
				char *rest = line;
				while (bu_text_width(t->font, rest, 14) > maxw && strlen(rest) > 1) {
					char save[256];
					snprintf(save, sizeof(save), "%s", rest);
					int cut = (int)strlen(save);
					while (cut > 1 && bu_text_width(t->font, save, 14) > maxw) {
						cut--;
						/* don't split a UTF-8 sequence */
						while (cut > 0 && (save[cut] & 0xC0) == 0x80)
							cut--;
						save[cut] = 0;
					}
					bu_draw_text(c, t->font, ep.x + 14, y, save, 14, t->text);
					y += 22;
					rest += cut;
					if (y > ep.y + ep.h - 24)
						break;
				}
				if (y <= ep.y + ep.h - 24)
					bu_draw_text(c, t->font, ep.x + 14, y, rest, 14, t->text);
				y += 22;
				li = 0;
				if (ch == 0 || y > ep.y + ep.h - 24)
					break;
				continue;
			}
			line[li++] = ch;
		}
		/* caret blink block at end */
		bu_fill_rect(c, (bu_rect){ ep.x + 14, ep.y + ep.h - 26, 8, 3 }, t->accent);
	} else {
		bu_draw_text(c, t->font, ep.x + 16, ep.y + 16, "メモを選ぶか、新規で作成してください", 13, t->text_dim);
	}
	/* editor toolbar */
	bu_button(c, t, (bu_rect){ EDIT_X, 434, 130, 36 }, nt->osk_open ? "キーボードを閉じる" : "キーボード", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ EDIT_X + 138, 434, 90, 36 }, "削除", BU_BTN_DANGER, false);
	/* status */
	char st[160];
	int chars = 0;
	for (int i = 0; i < nt->len; i++)
		if ((nt->text[i] & 0xC0) != 0x80)
			chars++;
	snprintf(st, sizeof(st), "%d 文字%s", chars, nt->dirty ? " · 未保存" : "");
	bu_draw_text(c, t->font, EDIT_X + 240, 442, st, 12, nt->dirty ? t->warn : t->text_faint);
	if (nt->status[0])
		bu_draw_text_right(c, t->font, (bu_rect){ EDIT_X, 442, EDIT_W, 18 }, nt->status, 12, t->text_dim);
	/* OSK */
	if (nt->osk_open) {
		int oskh = bu_osk_height();
		bu_osk_draw(c, t, &nt->osk, (bu_rect){ 0, a->h - oskh, a->w, oskh });
	}
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void text_append(struct notes *nt, const char *s)
{
	int add = (int)strlen(s);
	if (nt->len + add >= TEXTMAX - 1)
		return;
	memcpy(nt->text + nt->len, s, add + 1);
	nt->len += add;
	nt->dirty = true;
	nt->last_edit = time(NULL);
}

static void text_backspace(struct notes *nt)
{
	if (nt->osk.pending[0]) {
		int l = (int)strlen(nt->osk.pending);
		nt->osk.pending[l - 1] = 0;
		return;
	}
	if (nt->len == 0)
		return;
	nt->len--;
	while (nt->len > 0 && (nt->text[nt->len] & 0xC0) == 0x80)
		nt->len--;
	nt->text[nt->len] = 0;
	nt->dirty = true;
	nt->last_edit = time(NULL);
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct notes *nt = ctx;
	if (ev->type == BU_EV_KEY && ev->value == 1 && nt->current >= 0 && !nt->osk_open) {
		/* physical keyboard: basic latin + controls */
		int code = ev->code;
		char ch = 0;
		if (code >= 16 && code <= 25) ch = "qwertyuiop"[code - 16];
		else if (code >= 30 && code <= 38) ch = "asdfghjkl"[code - 30];
		else if (code >= 44 && code <= 50) ch = "zxcvbnm"[code - 44];
		else if (code == 57) ch = ' ';
		else if (code == 28) ch = '\n';
		if (code == 14) {
			text_backspace(nt);
			*changed = true;
		} else if (ch) {
			char buf[2] = { ch, 0 };
			text_append(nt, buf);
			*changed = true;
		}
		return;
	}
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (nt->osk_open) {
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		if (y >= area.y) {
			if (nt->current < 0) {
				/* OSK doubles as the search input */
			}
			char out[32];
			int r = bu_osk_hit(&nt->osk, area, x, y, out, sizeof(out));
			if (r == OSK_TEXT && out[0]) {
				if (nt->current >= 0)
					text_append(nt, out);
				else if (strlen(nt->search) + strlen(out) < sizeof(nt->search) - 1)
					strcat(nt->search, out);
			} else if (r == OSK_BACKSPACE) {
				if (nt->current >= 0) {
					text_backspace(nt);
				} else {
					int l = (int)strlen(nt->search);
					while (l > 0 && (nt->search[l - 1] & 0xC0) == 0x80)
						l--;
					if (l > 0)
						nt->search[l - 1] = 0;
					load_list(nt);
				}
			} else if (r == OSK_ENTER) {
				if (nt->current >= 0)
					text_append(nt, "\n");
				else {
					load_list(nt);
					nt->osk_open = false;
				}
			}
			if (nt->current < 0 && (r == OSK_TEXT || r == OSK_BACKSPACE))
				load_list(nt);
			return;
		}
	}
	if (inr(x, y, (bu_rect){ LIST_X, 52, 120, 38 })) {
		new_note(nt);
		return;
	}
	if (inr(x, y, (bu_rect){ 140, 52, 124, 38 })) {
		save_current(nt);
		nt->current = -1;
		nt->search[0] = 0;
		load_list(nt);
		nt->osk_open = true;
		bu_osk_init(&nt->osk);
		nt->osk.kana = true;
		snprintf(nt->status, sizeof(nt->status), "検索: キーボードで入力");
		return;
	}
	if (inr(x, y, (bu_rect){ EDIT_X, 434, 130, 36 })) {
		nt->osk_open = !nt->osk_open;
		if (nt->osk_open)
			bu_osk_init(&nt->osk);
		return;
	}
	if (inr(x, y, (bu_rect){ EDIT_X + 138, 434, 90, 36 })) {
		if (nt->current >= 0) {
			char path[768];
			note_path(nt, nt->current, path, sizeof(path));
			remove(path);
			nt->current = -1;
			nt->len = 0;
			nt->text[0] = 0;
			nt->dirty = false;
			load_list(nt);
			snprintf(nt->status, sizeof(nt->status), "削除しました");
		}
		return;
	}
	if (inr(x, y, (bu_rect){ LIST_X, 434, 120, 36 })) {
		if (nt->scroll >= 3) nt->scroll -= 3; else nt->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ 140, 434, 124, 36 })) {
		if (nt->scroll + 7 < nt->n) nt->scroll += 3;
		return;
	}
	int visible = 7;
	for (int vi = 0; vi < visible; vi++) {
		int i = nt->scroll + vi;
		if (i >= nt->n)
			break;
		bu_rect r = { LIST_X + 6, 98 + 6 + vi * 46, LIST_W - 12, 42 };
		if (inr(x, y, r)) {
			open_note(nt, i);
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct notes nt;
	memset(&nt, 0, sizeof(nt));
	nt.current = -1;
	const char *home = getenv("HOME");
	snprintf(nt.dir, sizeof(nt.dir), "%s/Notes", home ? home : "/home/user");
	char cmd[560];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", nt.dir);
	if (system(cmd) != 0)
		return 1;
	load_list(&nt);
	if (nt.n > 0)
		open_note(&nt, 0);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-notes.ppm" : NULL;
	int rc = bdapp_run("メモ", shot, draw, onevent, tick, &nt);
	save_current(&nt);
	return rc;
}
