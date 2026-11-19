/* bd-files: file manager for braindesk (v2).
 * Mainstream layout: places sidebar, toolbar with history and
 * breadcrumbs, list/grid views, an info + actions bar for the
 * selection, and a status bar with free space. Copy/cut/paste,
 * rename and new-folder use the on-screen keyboard. The toolbar
 * search field filters the current folder (case-insensitive
 * substring, live as you type) and stays scoped to the folder
 * you are in when you navigate.
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

#define MAXENT 512
#define HIST 32

struct entry {
	char name[256];
	bool is_dir;
	off_t size;
	time_t mtime;
	mode_t mode;
};

struct files {
	char cwd[PATH_MAX];
	struct entry ent[MAXENT];
	int n, scroll, sel;
	char hist[HIST][PATH_MAX];
	int hist_len, hist_pos;
	bool grid;
	bool show_hidden;
	int sort;		/* 0 name, 1 size, 2 date */
	char clip[PATH_MAX];
	bool clip_cut;
	bool confirm_delete;
	/* OSK input mode: 0 none, 1 rename, 2 new folder, 3 search */
	int input_mode;
	char input[256];
	/* live search query: filters the current folder; kept while
	 * navigating so the scope follows the folder you are in. */
	char query[128];
	bu_osk osk;
	char status[160];
};

static int sort_mode;

/* Case-insensitive substring match. Only ASCII A-Z is folded;
 * multibyte UTF-8 bytes compare as-is, which is correct for a
 * byte-substring search because UTF-8 is self-synchronising. */
static bool contains_ci(const char *hay, const char *needle)
{
	if (!needle[0])
		return true;
	size_t nl = strlen(needle);
	for (const char *p = hay; *p; p++) {
		if (strlen(p) < nl)
			return false;
		size_t i;
		for (i = 0; i < nl; i++) {
			unsigned char hc = (unsigned char)p[i], nc = (unsigned char)needle[i];
			if (hc >= 'A' && hc <= 'Z')
				hc += 32;
			if (nc >= 'A' && nc <= 'Z')
				nc += 32;
			if (hc != nc)
				break;
		}
		if (i == nl)
			return true;
	}
	return false;
}

/* Copy at most maxb bytes, never splitting a UTF-8 character. */
static void trunc_utf8(char *dst, size_t dn, const char *src, size_t maxb)
{
	size_t l = strlen(src);
	if (l > maxb) {
		l = maxb;
		while (l > 0 && (src[l] & 0xC0) == 0x80)
			l--;
	}
	if (l >= dn)
		l = dn - 1;
	memcpy(dst, src, l);
	dst[l] = 0;
}

static int ent_cmp(const void *a, const void *b)
{
	const struct entry *ea = a, *eb = b;
	if (ea->is_dir != eb->is_dir)
		return ea->is_dir ? -1 : 1;
	switch (sort_mode) {
	case 1:
		return ea->size < eb->size ? 1 : ea->size > eb->size ? -1 : strcmp(ea->name, eb->name);
	case 2:
		return ea->mtime < eb->mtime ? 1 : ea->mtime > eb->mtime ? -1 : strcmp(ea->name, eb->name);
	default:
		return strcmp(ea->name, eb->name);
	}
}

static void join(char *dst, size_t n, const char *dir, const char *name)
{
	if (strcmp(dir, "/") == 0)
		snprintf(dst, n, "/%s", name);
	else
		snprintf(dst, n, "%s/%s", dir, name);
}

static void load(struct files *f)
{
	DIR *d = opendir(f->cwd);
	f->n = 0;
	f->sel = -1;
	f->scroll = 0;
	f->confirm_delete = false;
	if (!d) {
		snprintf(f->status, sizeof(f->status), "開けません: %s", f->cwd);
		return;
	}
	struct dirent *de;
	while ((de = readdir(d)) && f->n < MAXENT) {
		if (de->d_name[0] == '.' && de->d_name[1] == 0)
			continue;
		if (strcmp(de->d_name, "..") == 0)
			continue;
		if (de->d_name[0] == '.' && !f->show_hidden)
			continue;
		if (f->query[0] && !contains_ci(de->d_name, f->query))
			continue;
		struct entry *e = &f->ent[f->n];
		snprintf(e->name, sizeof(e->name), "%s", de->d_name);
		char full[PATH_MAX];
		join(full, sizeof(full), f->cwd, de->d_name);
		struct stat st;
		if (stat(full, &st) == 0) {
			e->is_dir = S_ISDIR(st.st_mode);
			e->size = st.st_size;
			e->mtime = st.st_mtime;
			e->mode = st.st_mode;
		}
		f->n++;
	}
	closedir(d);
	sort_mode = f->sort;
	qsort(f->ent, f->n, sizeof(struct entry), ent_cmp);
}

static void navigate(struct files *f, const char *path, bool push)
{
	if (push) {
		if (f->hist_pos < f->hist_len - 1)
			f->hist_len = f->hist_pos + 1;
		if (f->hist_len < HIST) {
			snprintf(f->hist[f->hist_len], PATH_MAX, "%s", path);
			f->hist_pos = f->hist_len;
			f->hist_len++;
		}
	}
	snprintf(f->cwd, sizeof(f->cwd), "%s", path);
	load(f);
}

static void fmt_size(char *buf, size_t n, off_t size)
{
	if (size < 1024)
		snprintf(buf, n, "%lld B", (long long)size);
	else if (size < 1024 * 1024)
		snprintf(buf, n, "%.1f KB", size / 1024.0);
	else if (size < 1048576 * 1024)
		snprintf(buf, n, "%.1f MB", size / 1048576.0);
	else
		snprintf(buf, n, "%.2f GB", size / 1073741824.0);
}

static void fmt_perm(char *buf, size_t n, mode_t m)
{
	snprintf(buf, n, "%c%c%c %c%c%c %c%c%c",
		 m & 0400 ? 'r' : '-', m & 0200 ? 'w' : '-', m & 0100 ? 'x' : '-',
		 m & 0040 ? 'r' : '-', m & 0020 ? 'w' : '-', m & 0010 ? 'x' : '-',
		 m & 0004 ? 'r' : '-', m & 0002 ? 'w' : '-', m & 0001 ? 'x' : '-');
}

/* --- layout --- */
#define SIDE_W 148
#define TOOL_Y 48
#define CONT_Y 92
#define ACT_Y 398
#define STAT_Y 446

static bu_rect content_rect(void) { return (bu_rect){ SIDE_W + 8, CONT_Y, 864 - SIDE_W - 16, ACT_Y - CONT_Y - 4 }; }

struct place { const char *label; char path[PATH_MAX]; };
static struct place PLACES[8];
static int NPLACES;

static void build_places(void)
{
	const char *home = getenv("HOME");
	if (!home)
		home = "/home/user";
	NPLACES = 0;
	snprintf(PLACES[NPLACES].path, PATH_MAX, "%s", home);
	PLACES[NPLACES++].label = "ホーム";
	const char *subs[] = { "Documents", "Downloads", "Pictures", "Music", "Recordings" };
	const char *labs[] = { "ドキュメント", "ダウンロード", "画像", "音楽", "録音" };
	for (int i = 0; i < 5; i++) {
		snprintf(PLACES[NPLACES].path, PATH_MAX, "%s/%s", home, subs[i]);
		PLACES[NPLACES++].label = labs[i];
	}
	/* SD card candidates */
	const char *sd[] = { "/media", "/mnt/sd", "/run/media" };
	for (int i = 0; i < 3; i++) {
		struct stat st;
		if (stat(sd[i], &st) == 0 && S_ISDIR(st.st_mode)) {
			snprintf(PLACES[NPLACES].path, PATH_MAX, "%s", sd[i]);
			PLACES[NPLACES++].label = "SD カード";
			break;
		}
	}
	snprintf(PLACES[NPLACES].path, PATH_MAX, "/");
	PLACES[NPLACES++].label = "ルート";
}

static void draw(bdapp *a, void *ctx)
{
	struct files *f = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* toolbar */
	bu_button(c, t, (bu_rect){ 8, TOOL_Y, 40, 36 }, "◀", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 52, TOOL_Y, 40, 36 }, "▶", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 96, TOOL_Y, 40, 36 }, "▲", BU_BTN_NORMAL, false);
	/* breadcrumbs */
	bu_fill_round_rect(c, (bu_rect){ 144, TOOL_Y, 360, 36 }, 8, t->surface);
	{
		char tmp[PATH_MAX];
		snprintf(tmp, sizeof(tmp), "%s", f->cwd);
		int x = 156;
		bu_draw_text(c, t->font, x, TOOL_Y + 8, "/", 13.5, t->accent);
		x += 14;
		char *save = NULL;
		for (char *seg = strtok_r(tmp, "/", &save); seg; seg = strtok_r(NULL, "/", &save)) {
			int w = bu_text_width(t->font, seg, 13.5) + 8;
			bu_draw_text(c, t->font, x, TOOL_Y + 8, seg, 13.5, t->text);
			x += w;
			bu_draw_text(c, t->font, x, TOOL_Y + 8, "›", 13.5, t->text_faint);
			x += 14;
			if (x > 480)
				break;
		}
	}
	/* search field (scoped to the current folder) */
	bu_fill_round_rect(c, (bu_rect){ 512, TOOL_Y, 102, 36 }, 8, t->surface);
	if (f->query[0]) {
		char qd[128];
		snprintf(qd, sizeof(qd), "%s", f->query);
		while (bu_text_width(t->font, qd, 13.5) > 86 && qd[0]) {
			int l = (int)strlen(qd);
			while (l > 0 && (qd[l - 1] & 0xC0) == 0x80)
				l--;
			if (l > 0)
				qd[l - 1] = 0;
		}
		bu_draw_text(c, t->font, 522, TOOL_Y + 8, qd, 13.5, t->text);
	} else {
		bu_draw_text(c, t->font, 522, TOOL_Y + 8, "検索", 13.5, t->text_faint);
	}
	bu_button(c, t, (bu_rect){ 622, TOOL_Y, 74, 36 }, f->grid ? "リスト" : "グリッド", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 702, TOOL_Y, 74, 36 }, "並替", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 782, TOOL_Y, 74, 36 }, "新規", BU_BTN_NORMAL, false);
	/* sidebar */
	bu_fill_round_rect(c, (bu_rect){ 8, CONT_Y, SIDE_W, ACT_Y - CONT_Y - 4 }, 10, t->surface);
	for (int i = 0; i < NPLACES; i++) {
		bu_rect r = { 14, CONT_Y + 6 + i * 36, SIDE_W - 12, 31 };
		bool cur = strcmp(f->cwd, PLACES[i].path) == 0;
		if (cur)
			bu_fill_round_rect(c, r, 7, t->surface3);
		bu_draw_text(c, t->font, r.x + 10, r.y + 6, PLACES[i].label, 13, cur ? t->text : t->text_dim);
	}
	/* content */
	bu_rect cont = content_rect();
	if (!f->grid) {
		int visible = (cont.h - 26) / 40;
		/* header */
		bu_draw_text(c, t->font, cont.x + 44, cont.y, "名前", 11.5, t->text_faint);
		bu_draw_text_right(c, t->font, (bu_rect){ cont.x, cont.y, cont.w - 120, 18 }, "サイズ", 11.5, t->text_faint);
		bu_draw_text_right(c, t->font, (bu_rect){ cont.x, cont.y, cont.w - 10, 18 }, "日付", 11.5, t->text_faint);
		for (int vi = 0; vi < visible; vi++) {
			int i = f->scroll + vi;
			if (i >= f->n)
				break;
			struct entry *e = &f->ent[i];
			bu_rect r = { cont.x, cont.y + 24 + vi * 40, cont.w - 56, 36 };
			bu_fill_round_rect(c, r, 7, i == f->sel ? t->surface3 : t->surface);
			if (e->is_dir)
				bu_icon_folder(c, r.x + 20, r.y + r.h / 2, BU_RGB(250, 176, 5));
			else
				bu_icon_note(c, r.x + 20, r.y + r.h / 2, t->text_dim);
			bu_draw_text(c, t->font, r.x + 40, r.y + 7, e->name, 13.5, t->text);
			if (!e->is_dir) {
				char sz[24];
				fmt_size(sz, sizeof(sz), e->size);
				bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 7, r.w - 116, 20 }, sz, 12, t->text_dim);
			}
			struct tm tm;
			localtime_r(&e->mtime, &tm);
			char dt[24];
			snprintf(dt, sizeof(dt), "%d/%02d/%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 7, r.w - 10, 20 }, dt, 12, t->text_dim);
		}
		bu_button(c, t, (bu_rect){ cont.x + cont.w - 50, cont.y + 24, 46, 52 }, "▲", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ cont.x + cont.w - 50, cont.y + cont.h - 56, 46, 52 }, "▼", BU_BTN_NORMAL, false);
	} else {
		int cols = 4, cell_w = (cont.w - 8) / cols, cell_h = 92;
		int rows = cont.h / cell_h;
		for (int vi = 0; vi < rows * cols; vi++) {
			int i = f->scroll * cols + vi;
			if (i >= f->n)
				break;
			int cx = cont.x + (vi % cols) * cell_w, cy = cont.y + (vi / cols) * cell_h;
			bu_rect r = { cx + 4, cy + 2, cell_w - 8, cell_h - 6 };
			bu_fill_round_rect(c, r, 8, i == f->sel ? t->surface3 : t->surface);
			struct entry *e = &f->ent[i];
			if (e->is_dir)
				bu_icon_folder(c, r.x + r.w / 2, r.y + 30, BU_RGB(250, 176, 5));
			else
				bu_icon_note(c, r.x + r.w / 2, r.y + 30, t->text_dim);
			/* truncate name to fit */
			char nm[40];
			snprintf(nm, sizeof(nm), "%s", e->name);
			if (bu_text_width(t->font, nm, 11.5) > r.w - 8 && strlen(nm) > 12)
				snprintf(nm, sizeof(nm), "%.11s…", e->name);
			bu_draw_text_centered(c, t->font, (bu_rect){ r.x, r.y + 52, r.w, 18 }, nm, 11.5, t->text);
		}
		bu_button(c, t, (bu_rect){ cont.x + cont.w - 50, cont.y + 24, 46, 52 }, "▲", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ cont.x + cont.w - 50, cont.y + cont.h - 56, 46, 52 }, "▼", BU_BTN_NORMAL, false);
	}
	if (f->n == 0) {
		if (f->query[0]) {
			char qt[32], msg[160];
			trunc_utf8(qt, sizeof(qt), f->query, 24);
			snprintf(msg, sizeof(msg), "「%s」に一致する項目がありません", qt);
			bu_draw_text(c, t->font, cont.x + 16, cont.y + 40, msg, 13.5, t->text_dim);
		} else {
			bu_draw_text(c, t->font, cont.x + 16, cont.y + 40, "このフォルダは空です", 13.5, t->text_dim);
		}
	}
	/* action + info bar */
	bu_fill_round_rect(c, (bu_rect){ 8, ACT_Y, 848, 42 }, 10, t->surface);
	if (f->sel >= 0) {
		struct entry *e = &f->ent[f->sel];
		char info[256], sz[24], perm[32];
		struct tm tm;
		localtime_r(&e->mtime, &tm);
		if (e->is_dir)
			snprintf(sz, sizeof(sz), "フォルダ");
		else
			fmt_size(sz, sizeof(sz), e->size);
		fmt_perm(perm, sizeof(perm), e->mode);
		snprintf(info, sizeof(info), "%s  ·  %s  ·  %d/%02d/%02d %02d:%02d  ·  %s",
			 e->name, sz, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, perm);
		bu_draw_text(c, t->font, 18, ACT_Y + 11, info, 12, t->text);
		int bx = 470;
		if (e->is_dir) {
			bu_button(c, t, (bu_rect){ bx, ACT_Y + 5, 62, 32 }, "開く", BU_BTN_PRIMARY, false);
			bx += 68;
		}
		bu_button(c, t, (bu_rect){ bx, ACT_Y + 5, 62, 32 }, "コピー", BU_BTN_NORMAL, false); bx += 68;
		bu_button(c, t, (bu_rect){ bx, ACT_Y + 5, 62, 32 }, "切取", BU_BTN_NORMAL, false); bx += 68;
		bu_button(c, t, (bu_rect){ bx, ACT_Y + 5, 74, 32 }, "名前変更", BU_BTN_NORMAL, false); bx += 80;
		bu_button(c, t, (bu_rect){ bx, ACT_Y + 5, 62, 32 }, f->confirm_delete ? "本当に?" : "削除", BU_BTN_DANGER, false);
	} else if (f->clip[0]) {
		bu_draw_text(c, t->font, 18, ACT_Y + 11, f->clip, 12, t->text_dim);
		bu_button(c, t, (bu_rect){ 700, ACT_Y + 5, 70, 32 }, "貼り付け", BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ 778, ACT_Y + 5, 70, 32 }, "取消", BU_BTN_NORMAL, false);
	} else {
		bu_draw_text(c, t->font, 18, ACT_Y + 11, "項目を選ぶと情報と操作がここに出ます", 12, t->text_faint);
	}
	/* status bar */
	char st[160];
	struct statvfs sv;
	long free_mb = -1;
	if (statvfs(f->cwd, &sv) == 0)
		free_mb = (long)((double)sv.f_bavail * sv.f_frsize / 1048576.0);
	const char *sorts[] = { "名前順", "サイズ順", "日付順" };
	if (f->query[0]) {
		char qt[32];
		trunc_utf8(qt, sizeof(qt), f->query, 24);
		snprintf(st, sizeof(st), "「%s」 %d 項目 · 並替: %s%s", qt, f->n, sorts[f->sort],
			 free_mb >= 0 ? " · 空き " : "");
	} else {
		snprintf(st, sizeof(st), "%d 項目 · 並替: %s%s%s", f->n, sorts[f->sort],
			 free_mb >= 0 ? " · 空き " : "", "");
	}
	bu_draw_text(c, t->font, 12, STAT_Y + 4, st, 11.5, t->text_faint);
	if (free_mb >= 0) {
		char fb[32];
		snprintf(fb, sizeof(fb), "%ld MB", free_mb);
		int w = bu_text_width(t->font, st, 11.5);
		bu_draw_text(c, t->font, 12 + w, STAT_Y + 4, fb, 11.5, t->text_faint);
	}
	if (f->status[0])
		bu_draw_text_right(c, t->font, (bu_rect){ 8, STAT_Y + 4, 848, 18 }, f->status, 11.5, t->warn);
	/* OSK input overlay */
	if (f->input_mode) {
		int oskh = bu_osk_height();
		bu_fill_round_rect(c, (bu_rect){ 144, TOOL_Y, 470, 36 }, 8, t->surface3);
		bu_draw_text(c, t->font, 156, TOOL_Y + 8, f->input[0] ? f->input : (f->input_mode == 1 ? "新しい名前" : f->input_mode == 2 ? "フォルダ名" : "検索する文字"), 13.5, t->text);
		bu_button(c, t, (bu_rect){ 622, TOOL_Y, 74, 36 }, "決定", BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ 782, TOOL_Y, 74, 36 }, "取消", BU_BTN_NORMAL, false);
		bu_osk_draw(c, t, &f->osk, (bu_rect){ 0, a->h - oskh, a->w, oskh });
	}
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void do_open(struct files *f, int i)
{
	if (i < 0 || i >= f->n)
		return;
	if (f->ent[i].is_dir) {
		char next[PATH_MAX];
		join(next, sizeof(next), f->cwd, f->ent[i].name);
		navigate(f, next, true);
	} else {
		char full[PATH_MAX];
		join(full, sizeof(full), f->cwd, f->ent[i].name);
		pid_t pid = fork();
		if (pid == 0) {
			execlp("xdg-open", "xdg-open", full, (char *)NULL);
			_exit(127);
		}
		snprintf(f->status, sizeof(f->status), "開いています: %s", f->ent[i].name);
	}
}

static void do_paste(struct files *f)
{
	char dst[PATH_MAX];
	const char *base = strrchr(f->clip, '/');
	base = base ? base + 1 : f->clip;
	join(dst, sizeof(dst), f->cwd, base);
	if (strcmp(dst, f->clip) == 0) {
		snprintf(f->status, sizeof(f->status), "同じ場所には貼れません");
		return;
	}
	if (f->clip_cut) {
		if (rename(f->clip, dst) == 0) {
			snprintf(f->status, sizeof(f->status), "移動しました");
			f->clip[0] = 0;
			load(f);
			return;
		}
	}
	char cmd[PATH_MAX * 2 + 32];
	snprintf(cmd, sizeof(cmd), "cp -r '%s' '%s'", f->clip, dst);
	if (system(cmd) == 0) {
		if (f->clip_cut) {
			char rm[PATH_MAX + 16];
			snprintf(rm, sizeof(rm), "rm -rf '%s'", f->clip);
			if (system(rm) != 0)
				snprintf(f->status, sizeof(f->status), "コピーしましたが元の削除に失敗");
		}
		snprintf(f->status, sizeof(f->status), "貼り付けました");
		f->clip[0] = 0;
		load(f);
	} else {
		snprintf(f->status, sizeof(f->status), "貼り付けに失敗しました");
	}
}

static void commit_input(struct files *f)
{
	if (f->input_mode == 3) {
		/* search: the list is already filtered live; 決定 just
		 * closes the keyboard and keeps the query. */
		snprintf(f->query, sizeof(f->query), "%s", f->input);
		if (f->query[0])
			load(f);
		f->input_mode = 0;
		f->input[0] = 0;
		return;
	}
	if (!f->input[0]) {
		f->input_mode = 0;
		return;
	}
	char dst[PATH_MAX];
	join(dst, sizeof(dst), f->cwd, f->input);
	if (f->input_mode == 2) {
		if (mkdir(dst, 0755) == 0) {
			snprintf(f->status, sizeof(f->status), "フォルダを作成しました");
			load(f);
		} else {
			snprintf(f->status, sizeof(f->status), "作成できませんでした");
		}
	} else if (f->input_mode == 1 && f->sel >= 0) {
		char src[PATH_MAX];
		join(src, sizeof(src), f->cwd, f->ent[f->sel].name);
		if (rename(src, dst) == 0) {
			snprintf(f->status, sizeof(f->status), "名前を変更しました");
			load(f);
		} else {
			snprintf(f->status, sizeof(f->status), "変更できませんでした");
		}
	}
	f->input_mode = 0;
	f->input[0] = 0;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct files *f = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	/* OSK overlay first */
	if (f->input_mode) {
		if (inr(x, y, (bu_rect){ 622, TOOL_Y, 74, 36 })) {
			commit_input(f);
			return;
		}
		if (inr(x, y, (bu_rect){ 782, TOOL_Y, 74, 36 })) {
			if (f->input_mode == 3) {
				/* 取消 in search clears the query too */
				f->query[0] = 0;
				load(f);
			}
			f->input_mode = 0;
			f->input[0] = 0;
			return;
		}
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		if (y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&f->osk, area, x, y, out, sizeof(out));
			if (r == OSK_TEXT && out[0]) {
				strncat(f->input, out, sizeof(f->input) - strlen(f->input) - 1);
				if (f->input_mode == 3) {
					snprintf(f->query, sizeof(f->query), "%s", f->input);
					load(f);
				}
			} else if (r == OSK_BACKSPACE) {
				int l = (int)strlen(f->input);
				while (l > 0 && (f->input[l - 1] & 0xC0) == 0x80)
					l--;
				if (l > 0)
					f->input[l - 1] = 0;
				if (f->input_mode == 3) {
					snprintf(f->query, sizeof(f->query), "%s", f->input);
					load(f);
				}
			} else if (r == OSK_ENTER) {
				commit_input(f);
			}
			return;
		}
		return;
	}
	/* toolbar */
	if (inr(x, y, (bu_rect){ 8, TOOL_Y, 40, 36 })) {
		if (f->hist_pos > 0) {
			f->hist_pos--;
			snprintf(f->cwd, sizeof(f->cwd), "%s", f->hist[f->hist_pos]);
			load(f);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 52, TOOL_Y, 40, 36 })) {
		if (f->hist_pos < f->hist_len - 1) {
			f->hist_pos++;
			snprintf(f->cwd, sizeof(f->cwd), "%s", f->hist[f->hist_pos]);
			load(f);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 96, TOOL_Y, 40, 36 })) {
		if (strcmp(f->cwd, "/") != 0) {
			char up[PATH_MAX];
			snprintf(up, sizeof(up), "%s", f->cwd);
			char *slash = strrchr(up, '/');
			if (slash == up)
				slash[1] = 0;
			else if (slash)
				*slash = 0;
			navigate(f, up, true);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 622, TOOL_Y, 74, 36 })) {
		f->grid = !f->grid;
		f->scroll = 0;
		return;
	}
	/* search field: opens the OSK with the current query prefilled */
	if (inr(x, y, (bu_rect){ 512, TOOL_Y, 102, 36 })) {
		f->input_mode = 3;
		snprintf(f->input, sizeof(f->input), "%s", f->query);
		bu_osk_init(&f->osk);
		f->osk.kana = false;
		return;
	}
	if (inr(x, y, (bu_rect){ 702, TOOL_Y, 74, 36 })) {
		f->sort = (f->sort + 1) % 3;
		load(f);
		return;
	}
	if (inr(x, y, (bu_rect){ 782, TOOL_Y, 74, 36 })) {
		f->input_mode = 2;
		f->input[0] = 0;
		bu_osk_init(&f->osk);
		f->osk.kana = false;
		return;
	}
	/* sidebar */
	for (int i = 0; i < NPLACES; i++) {
		bu_rect r = { 14, CONT_Y + 6 + i * 36, SIDE_W - 12, 31 };
		if (inr(x, y, r)) {
			struct stat st;
			if (stat(PLACES[i].path, &st) == 0)
				navigate(f, PLACES[i].path, true);
			else
				snprintf(f->status, sizeof(f->status), "ありません: %s", PLACES[i].path);
			return;
		}
	}
	bu_rect cont = content_rect();
	/* scroll buttons */
	if (inr(x, y, (bu_rect){ cont.x + cont.w - 50, cont.y + 24, 46, 52 })) {
		int step = f->grid ? 1 : 4;
		if (f->scroll >= step)
			f->scroll -= step;
		else
			f->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ cont.x + cont.w - 50, cont.y + cont.h - 56, 46, 52 })) {
		int step = f->grid ? 1 : 4;
		f->scroll += step;
		int max_first = f->grid ? (f->n + 3) / 4 : f->n;
		if (f->scroll > max_first)
			f->scroll = max_first;
		return;
	}
	/* action bar */
	if (f->sel >= 0 && y >= ACT_Y && y < ACT_Y + 42) {
		struct entry *e = &f->ent[f->sel];
		int bx = 470;
		if (e->is_dir) {
			if (inr(x, y, (bu_rect){ bx, ACT_Y + 5, 62, 32 })) {
				do_open(f, f->sel);
				return;
			}
			bx += 68;
		}
		if (inr(x, y, (bu_rect){ bx, ACT_Y + 5, 62, 32 })) {
			join(f->clip, sizeof(f->clip), f->cwd, e->name);
			f->clip_cut = false;
			snprintf(f->status, sizeof(f->status), "コピーしました");
			f->sel = -1;
			return;
		}
		bx += 68;
		if (inr(x, y, (bu_rect){ bx, ACT_Y + 5, 62, 32 })) {
			join(f->clip, sizeof(f->clip), f->cwd, e->name);
			f->clip_cut = true;
			snprintf(f->status, sizeof(f->status), "切り取りました");
			f->sel = -1;
			return;
		}
		bx += 68;
		if (inr(x, y, (bu_rect){ bx, ACT_Y + 5, 74, 32 })) {
			f->input_mode = 1;
			snprintf(f->input, sizeof(f->input), "%s", e->name);
			bu_osk_init(&f->osk);
			f->osk.kana = false;
			return;
		}
		bx += 80;
		if (inr(x, y, (bu_rect){ bx, ACT_Y + 5, 62, 32 })) {
			char full[PATH_MAX];
			join(full, sizeof(full), f->cwd, e->name);
			if (!f->confirm_delete) {
				f->confirm_delete = true;
			} else if (remove(full) == 0 || (e->is_dir && rmdir(full) == 0)) {
				snprintf(f->status, sizeof(f->status), "削除しました");
				load(f);
			} else {
				char cmd[PATH_MAX + 16];
				snprintf(cmd, sizeof(cmd), "rm -rf '%s'", full);
				if (system(cmd) == 0) {
					snprintf(f->status, sizeof(f->status), "削除しました");
					load(f);
				} else {
					snprintf(f->status, sizeof(f->status), "削除できませんでした");
					f->confirm_delete = false;
				}
			}
			return;
		}
		return;
	}
	if (f->sel < 0 && f->clip[0] && y >= ACT_Y && y < ACT_Y + 42) {
		if (inr(x, y, (bu_rect){ 700, ACT_Y + 5, 70, 32 })) {
			do_paste(f);
			return;
		}
		if (inr(x, y, (bu_rect){ 778, ACT_Y + 5, 70, 32 })) {
			f->clip[0] = 0;
			return;
		}
		return;
	}
	/* content items */
	if (!f->grid) {
		int visible = (cont.h - 26) / 40;
		for (int vi = 0; vi < visible; vi++) {
			int i = f->scroll + vi;
			if (i >= f->n)
				break;
			bu_rect r = { cont.x, cont.y + 24 + vi * 40, cont.w - 56, 36 };
			if (inr(x, y, r)) {
				if (f->sel == i)
					do_open(f, i);
				else {
					f->sel = i;
					f->confirm_delete = false;
				}
				return;
			}
		}
	} else {
		int cols = 4, cell_w = (cont.w - 8) / cols, cell_h = 92;
		int rows = cont.h / cell_h;
		for (int vi = 0; vi < rows * cols; vi++) {
			int i = f->scroll * cols + vi;
			if (i >= f->n)
				break;
			int cx = cont.x + (vi % cols) * cell_w, cy = cont.y + (vi / cols) * cell_h;
			bu_rect r = { cx + 4, cy + 2, cell_w - 8, cell_h - 6 };
			if (inr(x, y, r)) {
				if (f->sel == i)
					do_open(f, i);
				else {
					f->sel = i;
					f->confirm_delete = false;
				}
				return;
			}
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
	build_places();
	snprintf(f.hist[0], PATH_MAX, "%s", f.cwd);
	f.hist_len = 1;
	f.hist_pos = 0;
	load(&f);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-files.ppm" : NULL;
	return bdapp_run("ファイル", shot, draw, onevent, NULL, &f);
}
