/* bd-files: file manager for braindesk.
 * Browse, inspect and delete files. Directories first, sizes and
 * dates shown, destructive actions need a confirming second tap.
 */
#include "bdapp.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>

#define MAXENT 512
#define ROWH 42
#define VISIBLE 9

struct entry {
	char name[256];
	bool is_dir;
	off_t size;
	time_t mtime;
};

struct files {
	char cwd[PATH_MAX];
	struct entry ent[MAXENT];
	int n, scroll, sel;
	bool confirm_delete;
	char status[128];
};

static int ent_cmp(const void *a, const void *b)
{
	const struct entry *ea = a, *eb = b;
	if (ea->is_dir != eb->is_dir)
		return ea->is_dir ? -1 : 1;
	return strcmp(ea->name, eb->name);
}

static void load(struct files *f)
{
	DIR *d = opendir(f->cwd);
	f->n = 0;
	f->sel = -1;
	f->scroll = 0;
	f->confirm_delete = false;
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && f->n < MAXENT) {
		if (de->d_name[0] == '.' && de->d_name[1] == 0)
			continue;
		if (strcmp(de->d_name, "..") == 0 && strcmp(f->cwd, "/") == 0)
			continue;
		struct entry *e = &f->ent[f->n];
		snprintf(e->name, sizeof(e->name), "%s", de->d_name);
		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", f->cwd, de->d_name);
		struct stat st;
		if (stat(full, &st) == 0) {
			e->is_dir = S_ISDIR(st.st_mode);
			e->size = st.st_size;
			e->mtime = st.st_mtime;
		}
		f->n++;
	}
	closedir(d);
	qsort(f->ent, f->n, sizeof(struct entry), ent_cmp);
}

static void fmt_size(char *buf, size_t n, off_t size)
{
	if (size < 1024)
		snprintf(buf, n, "%lld B", (long long)size);
	else if (size < 1024 * 1024)
		snprintf(buf, n, "%.1f KB", size / 1024.0);
	else if (size < 1024 * 1024 * 1024)
		snprintf(buf, n, "%.1f MB", size / 1048576.0);
	else
		snprintf(buf, n, "%.2f GB", size / 1073741824.0);
}

static bu_rect row_rect(int i)
{
	return (bu_rect){ 12, 96 + i * ROWH, 700, ROWH - 4 };
}

static void draw(bdapp *a, void *ctx)
{
	struct files *f = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* path bar + up button */
	bu_fill_round_rect(c, (bu_rect){ 12, 52, 640, 36 }, 8, t->surface);
	bu_draw_text(c, t->font, 24, 60, f->cwd, 14, t->text);
	bu_button(c, t, (bu_rect){ 664, 52, 88, 36 }, "上へ", BU_BTN_NORMAL, false);
	/* entries */
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = f->scroll + vi;
		if (i >= f->n)
			break;
		struct entry *e = &f->ent[i];
		bu_rect r = row_rect(vi);
		bu_fill_round_rect(c, r, 8, i == f->sel ? t->surface3 : t->surface);
		if (e->is_dir)
			bu_icon_folder(c, r.x + 22, r.y + r.h / 2, BU_RGB(250, 176, 5));
		else
			bu_icon_note(c, r.x + 22, r.y + r.h / 2, t->text_dim);
		bu_draw_text(c, t->font, r.x + 44, r.y + 8, e->name, 14.5, t->text);
		char meta[64];
		if (e->is_dir) {
			snprintf(meta, sizeof(meta), "フォルダ");
		} else {
			char sz[24];
			fmt_size(sz, sizeof(sz), e->size);
			struct tm tm;
			localtime_r(&e->mtime, &tm);
			snprintf(meta, sizeof(meta), "%s  %d/%02d/%02d", sz, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
		}
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 8, r.w - 14, 24 }, meta, 12.5, t->text_dim);
	}
	/* scroll buttons */
	bu_button(c, t, (bu_rect){ 724, 96, 52, 60 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 724, 400, 52, 60 }, "▼", BU_BTN_NORMAL, false);
	/* action bar */
	if (f->sel >= 0 && !f->ent[f->sel].is_dir) {
		bu_button(c, t, (bu_rect){ 12, 480 - 40, 150, 34 },
			  f->confirm_delete ? "本当に削除?" : "削除", BU_BTN_DANGER, false);
	}
	if (f->status[0])
		bu_draw_text(c, t->font, 180, 480 - 34, f->status, 13, t->text_dim);
	char cnt[32];
	snprintf(cnt, sizeof(cnt), "%d 項目", f->n);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, 480 - 34, 840, 24 }, cnt, 12.5, t->text_faint);
}

static void join(char *dst, size_t n, const char *dir, const char *name)
{
	if (strcmp(dir, "/") == 0)
		snprintf(dst, n, "/%s", name);
	else
		snprintf(dst, n, "%s/%s", dir, name);
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct files *f = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (x >= 664 && x < 752 && y >= 52 && y < 88) { /* up */
		if (strcmp(f->cwd, "/") != 0) {
			char *slash = strrchr(f->cwd, '/');
			if (slash == f->cwd)
				slash[1] = 0;
			else
				*slash = 0;
			load(f);
		}
		return;
	}
	if (x >= 724 && x < 776 && y >= 96 && y < 156) {
		if (f->scroll > 0)
			f->scroll--;
		return;
	}
	if (x >= 724 && x < 776 && y >= 400 && y < 460) {
		if (f->scroll + VISIBLE < f->n)
			f->scroll++;
		return;
	}
	if (f->sel >= 0 && !f->ent[f->sel].is_dir &&
	    x >= 12 && x < 162 && y >= 440 && y < 474) {
		char full[PATH_MAX];
		join(full, sizeof(full), f->cwd, f->ent[f->sel].name);
		if (!f->confirm_delete) {
			f->confirm_delete = true;
			snprintf(f->status, sizeof(f->status), "もう一度押すと削除します");
		} else if (remove(full) == 0) {
			snprintf(f->status, sizeof(f->status), "削除しました");
			load(f);
		} else {
			snprintf(f->status, sizeof(f->status), "削除できませんでした");
			f->confirm_delete = false;
		}
		return;
	}
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = f->scroll + vi;
		if (i >= f->n)
			break;
		bu_rect r = row_rect(vi);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
			f->confirm_delete = false;
			f->status[0] = 0;
			if (f->ent[i].is_dir) {
				char next[PATH_MAX];
				if (strcmp(f->ent[i].name, "..") == 0) {
					snprintf(next, sizeof(next), "%s", f->cwd);
					char *slash = strrchr(next, '/');
					if (slash == next)
						slash[1] = 0;
					else if (slash)
						*slash = 0;
				} else {
					join(next, sizeof(next), f->cwd, f->ent[i].name);
				}
				snprintf(f->cwd, sizeof(f->cwd), "%s", next);
				load(f);
			} else {
				f->sel = i;
			}
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct files f;
	memset(&f, 0, sizeof(f));
	const char *home = getenv("HOME");
	snprintf(f.cwd, sizeof(f.cwd), "%s", home ? home : "/");
	f.sel = -1;
	load(&f);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-files.ppm" : NULL;
	return bdapp_run("ファイル", shot, draw, onevent, &f);
}
