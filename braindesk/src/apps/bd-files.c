/* bd-files: file manager for braindesk (v3).
 * Mainstream layout: places sidebar (home, user folders, SD,
 * trash, root), toolbar with history, clickable breadcrumbs
 * (tap a crumb to jump, tap the empty bar to type a path),
 * scoped search, list/grid views, column-header sorting
 * (name/size/date/kind, tap again to reverse), an info +
 * actions bar for the selection, and a status bar with free
 * space. Copy/cut/paste (with Finder-style duplicate naming),
 * rename and new-folder use the on-screen keyboard. Delete
 * asks in a modal dialog and moves items to the trash
 * (~/.local/share/Trash), where they can be restored or
 * erased for good. A properties dialog shows kind, size,
 * location, timestamps and permissions. A toolbar toggle
 * shows or hides dotfiles.
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
	time_t mtime, atime, ctime;
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
	int sort;		/* 0 name, 1 size, 2 date, 3 kind */
	bool sort_rev;
	char clip[PATH_MAX];
	bool clip_cut;
	bool confirm_delete;
	bool show_props;
	off_t props_size;
	int props_count;	/* folder item count, -1 for files */
	bool props_capped;
	/* OSK input mode: 0 none, 1 rename, 2 new folder,
	 * 3 search, 4 go-to-path */
	int input_mode;
	char input[256];
	/* live search query: filters the current folder; kept while
	 * navigating so the scope follows the folder you are in. */
	char query[128];
	bu_osk osk;
	char status[160];
};

static int sort_mode;
static bool sort_rev_g;

/* Case-insensitive ASCII fold compare (extensions, kinds). */
static int cmp_fold(const char *a, const char *b)
{
	for (;; a++, b++) {
		unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
		if (ca >= 'A' && ca <= 'Z')
			ca += 32;
		if (cb >= 'A' && cb <= 'Z')
			cb += 32;
		if (ca != cb || ca == 0)
			return (int)ca - (int)cb;
	}
}

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

static const char *ext_of(const char *name)
{
	const char *d = strrchr(name, '.');
	return (d && d != name) ? d + 1 : "";
}

static void kind_str(const struct entry *e, char *buf, size_t n)
{
	if (e->is_dir) {
		snprintf(buf, n, "フォルダ");
		return;
	}
	const char *ext = ext_of(e->name);
	static const struct { const char *ext, *kind; } tab[] = {
		{ "txt", "テキスト" }, { "md", "テキスト" }, { "log", "テキスト" },
		{ "png", "画像" }, { "jpg", "画像" }, { "jpeg", "画像" },
		{ "gif", "画像" }, { "bmp", "画像" },
		{ "mp3", "音声" }, { "wav", "音声" }, { "flac", "音声" },
		{ "ogg", "音声" }, { "m4a", "音声" },
		{ "mp4", "動画" }, { "mkv", "動画" }, { "avi", "動画" },
		{ "mov", "動画" },
		{ "pdf", "PDF文書" }, { "zip", "アーカイブ" },
		{ "gz", "アーカイブ" }, { "tar", "アーカイブ" },
		{ "c", "ソースコード" }, { "h", "ソースコード" },
		{ "py", "ソースコード" }, { "sh", "シェルスクリプト" },
		{ "html", "HTML文書" }, { "json", "JSON" },
	};
	for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
		if (cmp_fold(ext, tab[i].ext) == 0) {
			snprintf(buf, n, "%s", tab[i].kind);
			return;
		}
	if (ext[0])
		snprintf(buf, n, "%s ファイル", ext);
	else
		snprintf(buf, n, "ファイル");
}

static int ent_cmp(const void *a, const void *b)
{
	const struct entry *ea = a, *eb = b;
	if (ea->is_dir != eb->is_dir)
		return ea->is_dir ? -1 : 1;	/* folders always on top */
	int r = 0;
	switch (sort_mode) {
	case 1:
		r = ea->size < eb->size ? 1 : ea->size > eb->size ? -1 : strcmp(ea->name, eb->name);
		break;
	case 2:
		r = ea->mtime < eb->mtime ? 1 : ea->mtime > eb->mtime ? -1 : strcmp(ea->name, eb->name);
		break;
	case 3:
		r = cmp_fold(ext_of(ea->name), ext_of(eb->name));
		if (r == 0)
			r = strcmp(ea->name, eb->name);
		break;
	default:
		r = strcmp(ea->name, eb->name);
		break;
	}
	return sort_rev_g ? -r : r;
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
	f->show_props = false;
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
		e->is_dir = false;
		e->size = 0;
		e->mtime = e->atime = e->ctime = 0;
		e->mode = 0;
		char full[PATH_MAX];
		join(full, sizeof(full), f->cwd, de->d_name);
		struct stat st;
		if (stat(full, &st) == 0) {
			e->is_dir = S_ISDIR(st.st_mode);
			e->size = st.st_size;
			e->mtime = st.st_mtime;
			e->atime = st.st_atime;
			e->ctime = st.st_ctime;
			e->mode = st.st_mode;
		}
		f->n++;
	}
	closedir(d);
	sort_mode = f->sort;
	sort_rev_g = f->sort_rev;
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

static void fmt_bytes(char *buf, size_t n, off_t size)
{
	/* exact byte count with thousands separators */
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%lld", (long long)size);
	int len = (int)strlen(tmp), out = 0;
	for (int i = 0; i < len && out < (int)n - 1; i++) {
		if (i > 0 && (len - i) % 3 == 0 && out < (int)n - 2)
			buf[out++] = ',';
		buf[out++] = tmp[i];
	}
	buf[out] = 0;
}

static void fmt_dt(char *buf, size_t n, time_t t)
{
	struct tm tm;
	localtime_r(&t, &tm);
	snprintf(buf, n, "%d/%02d/%02d %02d:%02d",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

static void fmt_perm(char *buf, size_t n, mode_t m)
{
	snprintf(buf, n, "%c%c%c %c%c%c %c%c%c",
		 m & 0400 ? 'r' : '-', m & 0200 ? 'w' : '-', m & 0100 ? 'x' : '-',
		 m & 0040 ? 'r' : '-', m & 0020 ? 'w' : '-', m & 0010 ? 'x' : '-',
		 m & 0004 ? 'r' : '-', m & 0002 ? 'w' : '-', m & 0001 ? 'x' : '-');
}

/* Draw text truncated (from the end, UTF-8 safe) to maxw px. */
static void draw_fit(bu_canvas *c, bu_font *font, int x, int y,
		     const char *s, double px, int maxw, bu_color col)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "%s", s);
	while (buf[0] && bu_text_width(font, buf, px) > maxw) {
		int l = (int)strlen(buf);
		while (l > 0 && (buf[l - 1] & 0xC0) == 0x80)
			l--;
		if (l <= 1) {
			buf[0] = 0;
			break;
		}
		buf[l - 1] = 0;
	}
	bu_draw_text(c, font, x, y, buf, px, col);
}

/* --- trash (freedesktop layout under ~/.local/share/Trash) --- */

static void trash_dir(char *out, size_t n, const char *sub)
{
	const char *home = getenv("HOME");
	if (!home)
		home = "/home/user";
	if (sub && sub[0])
		snprintf(out, n, "%s/.local/share/Trash/%s", home, sub);
	else
		snprintf(out, n, "%s/.local/share/Trash", home);
}

static void trash_ensure(void)
{
	const char *home = getenv("HOME");
	if (!home)
		home = "/home/user";
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s/.local", home);
	mkdir(p, 0755);
	snprintf(p, sizeof(p), "%s/.local/share", home);
	mkdir(p, 0755);
	trash_dir(p, sizeof(p), "");
	mkdir(p, 0755);
	trash_dir(p, sizeof(p), "files");
	mkdir(p, 0755);
	trash_dir(p, sizeof(p), "info");
	mkdir(p, 0755);
}

static bool in_trash(struct files *f)
{
	char td[PATH_MAX];
	trash_dir(td, sizeof(td), "files");
	return strcmp(f->cwd, td) == 0;
}

/* Find a free destination for `name` inside `dir`: the name
 * itself, or "stem のコピー[.ext]", "stem のコピー 2[.ext]"… */
static bool unique_name(const char *dir, const char *name, char *out, size_t n)
{
	char cand[PATH_MAX];
	join(cand, sizeof(cand), dir, name);
	struct stat st;
	if (stat(cand, &st) != 0) {
		snprintf(out, n, "%s", cand);
		return true;
	}
	char stem[256], ext[64];
	snprintf(stem, sizeof(stem), "%s", name);
	const char *dot = strrchr(name, '.');
	if (dot && dot != name) {
		snprintf(ext, sizeof(ext), "%s", dot);
		stem[dot - name] = 0;
	} else {
		ext[0] = 0;
	}
	for (int i = 1; i < 100; i++) {
		char nn[320];
		if (i == 1)
			snprintf(nn, sizeof(nn), "%s のコピー%s", stem, ext);
		else
			snprintf(nn, sizeof(nn), "%s のコピー %d%s", stem, i, ext);
		join(cand, sizeof(cand), dir, nn);
		if (stat(cand, &st) != 0) {
			snprintf(out, n, "%s", cand);
			return true;
		}
	}
	return false;
}

/* Recursive size + item count for a folder (capped walk). */
static void dir_stats(const char *path, off_t *bytes, int *count,
		      int *visited, bool *capped)
{
	if (*visited > 20000) {
		*capped = true;
		return;
	}
	DIR *d = opendir(path);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.' &&
		    (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
			continue;
		char full[PATH_MAX];
		join(full, sizeof(full), path, de->d_name);
		struct stat st;
		if (lstat(full, &st) != 0)
			continue;
		(*count)++;
		(*visited)++;
		if (S_ISDIR(st.st_mode))
			dir_stats(full, bytes, count, visited, capped);
		else
			*bytes += st.st_size;
		if (*visited > 20000) {
			*capped = true;
			break;
		}
	}
	closedir(d);
}

/* --- layout --- */
#define SIDE_W 148
#define TOOL_Y 48
#define CONT_Y 92
#define ACT_Y 398
#define STAT_Y 446

/* toolbar rects (shared by draw + hit test) */
static const bu_rect TB_BACK = { 8, TOOL_Y, 40, 36 };
static const bu_rect TB_FWD = { 52, TOOL_Y, 40, 36 };
static const bu_rect TB_UP = { 96, TOOL_Y, 40, 36 };
static const bu_rect TB_CRUMB = { 144, TOOL_Y, 326, 36 };
static const bu_rect TB_SEARCH = { 478, TOOL_Y, 112, 36 };
static const bu_rect TB_VIEW = { 598, TOOL_Y, 62, 36 };
static const bu_rect TB_SORT = { 666, TOOL_Y, 62, 36 };
static const bu_rect TB_HIDDEN = { 734, TOOL_Y, 56, 36 };
static const bu_rect TB_NEW = { 796, TOOL_Y, 60, 36 };
static const bu_rect TB_INPUT = { 144, TOOL_Y, 470, 36 };
static const bu_rect TB_OK = { 622, TOOL_Y, 74, 36 };
static const bu_rect TB_CANCEL = { 782, TOOL_Y, 74, 36 };

/* modal dialog rects (shared by draw + hit test) */
static const bu_rect DEL_PANEL = { 232, 150, 400, 176 };
static const bu_rect DEL_OK = { 492, 282, 120, 36 };
static const bu_rect DEL_CANCEL = { 252, 282, 120, 36 };
static const bu_rect PROP_PANEL = { 182, 84, 500, 318 };
static const bu_rect PROP_CLOSE = { 392, 352, 110, 36 };

static bu_rect content_rect(void) { return (bu_rect){ SIDE_W + 8, CONT_Y, 864 - SIDE_W - 16, ACT_Y - CONT_Y - 4 }; }

struct place { const char *label; char path[PATH_MAX]; };
static struct place PLACES[10];
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
	trash_dir(PLACES[NPLACES].path, PATH_MAX, "files");
	PLACES[NPLACES++].label = "ゴミ箱";
	snprintf(PLACES[NPLACES].path, PATH_MAX, "/");
	PLACES[NPLACES++].label = "ルート";
}

/* Breadcrumbs: one crumb per path segment (plus "/"), with the
 * absolute path each crumb jumps to. Used by draw and hit test
 * so the two can never disagree. */
struct crumb { bu_rect r; int tx; char path[PATH_MAX]; };

static int build_crumbs(struct files *f, bu_font *font, struct crumb *out, int max)
{
	int n = 0;
	int x = 156;
	if (n < max) {
		out[n].r = (bu_rect){ 150, TOOL_Y, 22, 36 };
		out[n].tx = 156;
		snprintf(out[n].path, PATH_MAX, "/");
		n++;
	}
	x += 14;
	char tmp[PATH_MAX];
	snprintf(tmp, sizeof(tmp), "%s", f->cwd);
	char acc[PATH_MAX] = "";
	char *save = NULL;
	for (char *seg = strtok_r(tmp, "/", &save); seg; seg = strtok_r(NULL, "/", &save)) {
		int w = bu_text_width(font, seg, 13.5) + 8;
		if (x + w > TB_CRUMB.x + TB_CRUMB.w - 6)
			break;
		strncat(acc, "/", sizeof(acc) - strlen(acc) - 1);
		strncat(acc, seg, sizeof(acc) - strlen(acc) - 1);
		if (n < max) {
			out[n].r = (bu_rect){ x - 4, TOOL_Y, w + 8, 36 };
			out[n].tx = x;
			snprintf(out[n].path, PATH_MAX, "%s", acc);
			n++;
		}
		x += w + 14;
	}
	return n;
}

/* Action-bar buttons for the current state, in display order.
 * Returns the count; out[i].r is the button rect. */
enum { ACT_OPEN, ACT_RESTORE, ACT_COPY, ACT_CUT, ACT_PASTE,
       ACT_RENAME, ACT_INFO, ACT_DELETE, ACT_CANCEL };
struct actbtn { int id; bu_rect r; const char *label; bu_btn_style style; };
struct wantspec { int id; const char *label; int w; bu_btn_style style; };

static int act_layout(struct files *f, struct actbtn *out)
{
	struct wantspec want[8];
	int m = 0;
	if (f->sel >= 0) {
		if (in_trash(f)) {
			want[m++] = (struct wantspec){ ACT_RESTORE, "元に戻す", 84, BU_BTN_NORMAL };
		} else if (f->ent[f->sel].is_dir) {
			want[m++] = (struct wantspec){ ACT_OPEN, "開く", 62, BU_BTN_PRIMARY };
		}
		want[m++] = (struct wantspec){ ACT_COPY, "コピー", 62, BU_BTN_NORMAL };
		want[m++] = (struct wantspec){ ACT_CUT, "切取", 62, BU_BTN_NORMAL };
		if (f->clip[0])
			want[m++] = (struct wantspec){ ACT_PASTE, "貼付", 62, BU_BTN_NORMAL };
		want[m++] = (struct wantspec){ ACT_RENAME, "名前変更", 80, BU_BTN_NORMAL };
		want[m++] = (struct wantspec){ ACT_INFO, "情報", 56, BU_BTN_NORMAL };
		want[m++] = (struct wantspec){ ACT_DELETE, "削除", 62, BU_BTN_DANGER };
	} else if (f->clip[0]) {
		want[m++] = (struct wantspec){ ACT_PASTE, "貼り付け", 70, BU_BTN_PRIMARY };
		want[m++] = (struct wantspec){ ACT_CANCEL, "取消", 70, BU_BTN_NORMAL };
	}
	int total = 0;
	for (int i = 0; i < m; i++)
		total += want[i].w + (i ? 6 : 0);
	int x = 856 - total;
	for (int i = 0; i < m; i++) {
		out[i].id = want[i].id;
		out[i].label = want[i].label;
		out[i].style = want[i].style;
		out[i].r = (bu_rect){ x, ACT_Y + 5, want[i].w, 32 };
		x += want[i].w + 6;
	}
	return m;
}

static void draw(bdapp *a, void *ctx)
{
	struct files *f = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* toolbar */
	bu_button(c, t, TB_BACK, "◀", BU_BTN_NORMAL, false);
	bu_button(c, t, TB_FWD, "▶", BU_BTN_NORMAL, false);
	bu_button(c, t, TB_UP, "▲", BU_BTN_NORMAL, false);
	/* breadcrumbs */
	bu_fill_round_rect(c, TB_CRUMB, 8, t->surface);
	{
		struct crumb crumbs[24];
		int nc = build_crumbs(f, t->font, crumbs, 24);
		for (int i = 0; i < nc; i++) {
			bu_draw_text(c, t->font, crumbs[i].tx, TOOL_Y + 8,
				     i == 0 ? "/" : strrchr(crumbs[i].path, '/') + 1,
				     13.5, i == 0 ? t->accent : t->text);
			if (i + 1 < nc)
				bu_draw_text(c, t->font, crumbs[i].r.x + crumbs[i].r.w - 4,
					     TOOL_Y + 8, "›", 13.5, t->text_faint);
		}
	}
	/* search field (scoped to the current folder) */
	bu_fill_round_rect(c, TB_SEARCH, 8, t->surface);
	if (f->query[0]) {
		draw_fit(c, t->font, TB_SEARCH.x + 10, TOOL_Y + 8, f->query, 13.5, TB_SEARCH.w - 16, t->text);
	} else {
		bu_draw_text(c, t->font, TB_SEARCH.x + 10, TOOL_Y + 8, "検索", 13.5, t->text_faint);
	}
	bu_button(c, t, TB_VIEW, f->grid ? "リスト" : "グリッド", BU_BTN_NORMAL, false);
	bu_button(c, t, TB_SORT, "並替", BU_BTN_NORMAL, false);
	bu_button(c, t, TB_HIDDEN, "隠し", f->show_hidden ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	bu_button(c, t, TB_NEW, "新規", BU_BTN_NORMAL, false);
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
		/* header: the active column carries a direction arrow */
		const char *arw = f->sort_rev ? " ▴" : " ▾";
		char hname[32], hsize[32], hdate[32];
		snprintf(hname, sizeof(hname), "名前%s", f->sort == 0 ? arw : "");
		snprintf(hsize, sizeof(hsize), "サイズ%s", f->sort == 1 ? arw : "");
		snprintf(hdate, sizeof(hdate), "日付%s", f->sort == 2 ? arw : "");
		bu_draw_text(c, t->font, cont.x + 44, cont.y, hname, 11.5,
			     f->sort == 0 ? t->accent : t->text_faint);
		bu_draw_text_right(c, t->font, (bu_rect){ cont.x, cont.y, cont.w - 120, 18 }, hsize, 11.5,
				   f->sort == 1 ? t->accent : t->text_faint);
		bu_draw_text_right(c, t->font, (bu_rect){ cont.x, cont.y, cont.w - 10, 18 }, hdate, 11.5,
				   f->sort == 2 ? t->accent : t->text_faint);
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
			char dt[24];
			fmt_dt(dt, sizeof(dt), e->mtime);
			dt[10] = 0;	/* keep YYYY/MM/DD in the row */
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
	{
		struct actbtn btns[8];
		int nb = act_layout(f, btns);
		int info_w = (nb ? btns[0].r.x : 856) - 26;
		if (f->sel >= 0) {
			struct entry *e = &f->ent[f->sel];
			char info[320], sz[24], kind[64], dt[32];
			if (e->is_dir)
				snprintf(sz, sizeof(sz), "フォルダ");
			else
				fmt_size(sz, sizeof(sz), e->size);
			kind_str(e, kind, sizeof(kind));
			fmt_dt(dt, sizeof(dt), e->mtime);
			snprintf(info, sizeof(info), "%s  ·  %s  ·  %s  ·  %s", e->name, sz, kind, dt);
			draw_fit(c, t->font, 18, ACT_Y + 11, info, 12, info_w, t->text);
		} else if (f->clip[0]) {
			draw_fit(c, t->font, 18, ACT_Y + 11, f->clip, 12, info_w, t->text_dim);
		} else {
			bu_draw_text(c, t->font, 18, ACT_Y + 11, "項目を選ぶと情報と操作がここに出ます", 12, t->text_faint);
		}
		for (int i = 0; i < nb; i++)
			bu_button(c, t, btns[i].r, btns[i].label, btns[i].style, false);
	}
	/* status bar */
	char st[200];
	struct statvfs sv;
	long free_mb = -1;
	if (statvfs(f->cwd, &sv) == 0)
		free_mb = (long)((double)sv.f_bavail * sv.f_frsize / 1048576.0);
	const char *sorts[] = { "名前順", "サイズ順", "日付順", "種類順" };
	if (f->query[0]) {
		char qt[32];
		trunc_utf8(qt, sizeof(qt), f->query, 24);
		snprintf(st, sizeof(st), "「%s」 %d 項目 · 並替: %s%s", qt, f->n,
			 sorts[f->sort], f->sort_rev ? "・逆順" : "");
	} else {
		snprintf(st, sizeof(st), "%d 項目 · 並替: %s%s", f->n,
			 sorts[f->sort], f->sort_rev ? "・逆順" : "");
	}
	bu_draw_text(c, t->font, 12, STAT_Y + 4, st, 11.5, t->text_faint);
	if (free_mb >= 0) {
		char fb[40];
		snprintf(fb, sizeof(fb), " · 空き %ld MB", free_mb);
		int w = bu_text_width(t->font, st, 11.5);
		bu_draw_text(c, t->font, 12 + w, STAT_Y + 4, fb, 11.5, t->text_faint);
	}
	if (f->status[0])
		bu_draw_text_right(c, t->font, (bu_rect){ 8, STAT_Y + 4, 848, 18 }, f->status, 11.5, t->warn);
	/* OSK input overlay */
	if (f->input_mode) {
		int oskh = bu_osk_height();
		bu_fill_round_rect(c, TB_INPUT, 8, t->surface3);
		const char *ph = f->input_mode == 1 ? "新しい名前" :
				 f->input_mode == 2 ? "フォルダ名" :
				 f->input_mode == 3 ? "検索する文字" : "移動先のパス";
		bu_draw_text(c, t->font, TB_INPUT.x + 12, TOOL_Y + 8, f->input[0] ? f->input : ph, 13.5, t->text);
		bu_button(c, t, TB_OK, "決定", BU_BTN_PRIMARY, false);
		bu_button(c, t, TB_CANCEL, "取消", BU_BTN_NORMAL, false);
		bu_osk_draw(c, t, &f->osk, (bu_rect){ 0, a->h - oskh, a->w, oskh });
	}
	/* delete confirmation dialog (modal) */
	if (f->confirm_delete && f->sel >= 0) {
		struct entry *e = &f->ent[f->sel];
		bu_fill_rect(c, (bu_rect){ 0, 0, a->w, a->h }, BU_RGBA(0, 0, 0, 110));
		bu_fill_round_rect(c, DEL_PANEL, 12, t->surface2);
		bu_draw_text(c, t->font, DEL_PANEL.x + 24, DEL_PANEL.y + 18, "削除の確認", 15, t->text);
		char msg[320];
		if (in_trash(f))
			snprintf(msg, sizeof(msg), "「%s」を完全に削除しますか？", e->name);
		else
			snprintf(msg, sizeof(msg), "「%s」をゴミ箱へ移動しますか？", e->name);
		draw_fit(c, t->font, DEL_PANEL.x + 24, DEL_PANEL.y + 56, msg, 13.5, DEL_PANEL.w - 48, t->text);
		if (in_trash(f))
			bu_draw_text(c, t->font, DEL_PANEL.x + 24, DEL_PANEL.y + 84,
				     "この操作は元に戻せません。", 12, t->text_dim);
		else
			bu_draw_text(c, t->font, DEL_PANEL.x + 24, DEL_PANEL.y + 84,
				     "ゴミ箱から元に戻すこともできます。", 12, t->text_dim);
		bu_button(c, t, DEL_CANCEL, "キャンセル", BU_BTN_NORMAL, false);
		bu_button(c, t, DEL_OK, "削除", BU_BTN_DANGER, false);
	}
	/* properties dialog (modal) */
	if (f->show_props && f->sel >= 0) {
		struct entry *e = &f->ent[f->sel];
		bu_fill_rect(c, (bu_rect){ 0, 0, a->w, a->h }, BU_RGBA(0, 0, 0, 110));
		bu_fill_round_rect(c, PROP_PANEL, 12, t->surface2);
		bu_draw_text(c, t->font, PROP_PANEL.x + 24, PROP_PANEL.y + 16, "プロパティ", 15, t->text);
		char kind[64], val[512], bytes[40];
		kind_str(e, kind, sizeof(kind));
		struct { const char *label; char val[512]; } rows[8];
		int nr = 0;
		snprintf(rows[nr].val, sizeof(rows[0].val), "%s", e->name);
		rows[nr++].label = "名前";
		snprintf(rows[nr].val, sizeof(rows[0].val), "%s", kind);
		rows[nr++].label = "種類";
		if (e->is_dir) {
			char hs[24];
			fmt_size(hs, sizeof(hs), f->props_size);
			snprintf(rows[nr].val, sizeof(rows[0].val), "%s（%d 項目%s）",
				 hs, f->props_count, f->props_capped ? "以上" : "");
		} else {
			char hs[24];
			fmt_size(hs, sizeof(hs), e->size);
			fmt_bytes(bytes, sizeof(bytes), e->size);
			snprintf(rows[nr].val, sizeof(rows[0].val), "%s（%s バイト）", hs, bytes);
		}
		rows[nr++].label = "サイズ";
		snprintf(rows[nr].val, sizeof(rows[0].val), "%s", f->cwd);
		rows[nr++].label = "場所";
		fmt_dt(val, sizeof(val), e->ctime);
		snprintf(rows[nr].val, sizeof(rows[0].val), "%s", val);
		rows[nr++].label = "作成";
		fmt_dt(val, sizeof(val), e->mtime);
		snprintf(rows[nr].val, sizeof(rows[0].val), "%s", val);
		rows[nr++].label = "変更";
		fmt_dt(val, sizeof(val), e->atime);
		snprintf(rows[nr].val, sizeof(rows[0].val), "%s", val);
		rows[nr++].label = "アクセス";
		{
			char perm[32];
			fmt_perm(perm, sizeof(perm), e->mode);
			snprintf(rows[nr].val, sizeof(rows[0].val), "%s（0%o）", perm, e->mode & 07777);
			rows[nr++].label = "権限";
		}
		for (int i = 0; i < nr; i++) {
			int y = PROP_PANEL.y + 56 + i * 27;
			bu_draw_text(c, t->font, PROP_PANEL.x + 24, y, rows[i].label, 12.5, t->text_dim);
			draw_fit(c, t->font, PROP_PANEL.x + 110, y, rows[i].val, 12.5,
				 PROP_PANEL.w - 134, t->text);
		}
		bu_button(c, t, PROP_CLOSE, "閉じる", BU_BTN_PRIMARY, false);
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
	struct stat st;
	if (stat(dst, &st) == 0) {
		if (f->clip_cut) {
			snprintf(f->status, sizeof(f->status), "同名の項目があります");
			return;
		}
		/* copying onto an existing name (including the same
		 * folder) duplicates with a fresh name, like Finder */
		if (!unique_name(f->cwd, base, dst, sizeof(dst))) {
			snprintf(f->status, sizeof(f->status), "貼り付けに失敗しました");
			return;
		}
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
			else
				snprintf(f->status, sizeof(f->status), "移動しました");
			f->clip[0] = 0;
		} else {
			snprintf(f->status, sizeof(f->status), "貼り付けました");
		}
		load(f);
	} else {
		snprintf(f->status, sizeof(f->status), "貼り付けに失敗しました");
	}
}

static void remove_any(struct files *f, const char *full, bool is_dir)
{
	if (remove(full) == 0 || (is_dir && rmdir(full) == 0)) {
		snprintf(f->status, sizeof(f->status), "完全に削除しました");
		return;
	}
	char cmd[PATH_MAX + 16];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", full);
	if (system(cmd) == 0)
		snprintf(f->status, sizeof(f->status), "完全に削除しました");
	else
		snprintf(f->status, sizeof(f->status), "削除できませんでした");
}

static void do_delete(struct files *f)
{
	if (f->sel < 0 || f->sel >= f->n) {
		f->confirm_delete = false;
		return;
	}
	struct entry *e = &f->ent[f->sel];
	char src[PATH_MAX];
	join(src, sizeof(src), f->cwd, e->name);
	f->confirm_delete = false;
	if (in_trash(f)) {
		remove_any(f, src, e->is_dir);
		/* drop the .trashinfo sidecar too */
		char info[PATH_MAX], idir[PATH_MAX];
		trash_dir(idir, sizeof(idir), "info");
		snprintf(info, sizeof(info), "%s/%s.trashinfo", idir, e->name);
		remove(info);
		load(f);
		return;
	}
	trash_ensure();
	char fdir[PATH_MAX], dst[PATH_MAX];
	trash_dir(fdir, sizeof(fdir), "files");
	/* unique name inside the trash: name, "name 2", "name 3"… */
	const char *base = e->name;
	snprintf(dst, sizeof(dst), "%s/%s", fdir, base);
	struct stat st;
	for (int i = 2; stat(dst, &st) == 0 && i < 100; i++)
		snprintf(dst, sizeof(dst), "%s/%s %d", fdir, base, i);
	bool moved = rename(src, dst) == 0;
	if (!moved) {
		char cmd[PATH_MAX * 2 + 32];
		snprintf(cmd, sizeof(cmd), "cp -r '%s' '%s'", src, dst);
		if (system(cmd) == 0) {
			char rm[PATH_MAX + 16];
			snprintf(rm, sizeof(rm), "rm -rf '%s'", src);
			moved = system(rm) == 0;
		}
	}
	if (!moved) {
		snprintf(f->status, sizeof(f->status), "ゴミ箱へ移動できませんでした");
		return;
	}
	/* .trashinfo sidecar: original path (percent-encoded) +
	 * deletion date, so 元に戻す can put it back. */
	const char *tbase = strrchr(dst, '/') + 1;
	char idir[PATH_MAX], info[PATH_MAX];
	trash_dir(idir, sizeof(idir), "info");
	snprintf(info, sizeof(info), "%s/%s.trashinfo", idir, tbase);
	FILE *fp = fopen(info, "w");
	if (fp) {
		fprintf(fp, "[Trash Info]\nPath=");
		for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
			unsigned char ch = *p;
			if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
			    (ch >= '0' && ch <= '9') || ch == '/' || ch == '.' ||
			    ch == '_' || ch == '-' || ch == '~')
				fputc(ch, fp);
			else
				fprintf(fp, "%%%02X", ch);
		}
		char now[32];
		time_t tnow = time(NULL);
		struct tm tm;
		localtime_r(&tnow, &tm);
		snprintf(now, sizeof(now), "%d-%02d-%02dT%02d:%02d:%02d",
			 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			 tm.tm_hour, tm.tm_min, tm.tm_sec);
		fprintf(fp, "\nDeletionDate=%s\n", now);
		fclose(fp);
	}
	snprintf(f->status, sizeof(f->status), "ゴミ箱へ移動しました");
	load(f);
}

static void do_restore(struct files *f)
{
	if (f->sel < 0 || f->sel >= f->n)
		return;
	struct entry *e = &f->ent[f->sel];
	char idir[PATH_MAX], info[PATH_MAX];
	trash_dir(idir, sizeof(idir), "info");
	snprintf(info, sizeof(info), "%s/%s.trashinfo", idir, e->name);
	FILE *fp = fopen(info, "r");
	if (!fp) {
		snprintf(f->status, sizeof(f->status), "元の場所が分かりません");
		return;
	}
	char line[PATH_MAX], orig[PATH_MAX] = "";
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "Path=", 5) == 0) {
			const char *p = line + 5;
			int o = 0;
			while (*p && *p != '\n' && o < (int)sizeof(orig) - 1) {
				if (*p == '%' && p[1] && p[2]) {
					char hx[3] = { p[1], p[2], 0 };
					orig[o++] = (char)strtol(hx, NULL, 16);
					p += 3;
				} else {
					orig[o++] = *p++;
				}
			}
			orig[o] = 0;
		}
	}
	fclose(fp);
	if (!orig[0]) {
		snprintf(f->status, sizeof(f->status), "元の場所が分かりません");
		return;
	}
	/* the original folder must still exist */
	char parent[PATH_MAX];
	snprintf(parent, sizeof(parent), "%s", orig);
	char *slash = strrchr(parent, '/');
	if (slash)
		*slash = 0;
	struct stat st;
	if (stat(parent, &st) != 0 || !S_ISDIR(st.st_mode)) {
		snprintf(f->status, sizeof(f->status), "元のフォルダがありません");
		return;
	}
	char dst[PATH_MAX];
	snprintf(dst, sizeof(dst), "%s", orig);
	if (stat(dst, &st) == 0) {
		const char *base = strrchr(orig, '/') + 1;
		if (!unique_name(parent, base, dst, sizeof(dst))) {
			snprintf(f->status, sizeof(f->status), "戻せませんでした");
			return;
		}
	}
	char src[PATH_MAX];
	join(src, sizeof(src), f->cwd, e->name);
	if (rename(src, dst) == 0) {
		remove(info);
		snprintf(f->status, sizeof(f->status), "元に戻しました");
		load(f);
	} else {
		snprintf(f->status, sizeof(f->status), "戻せませんでした");
	}
}

static void open_props(struct files *f)
{
	if (f->sel < 0 || f->sel >= f->n)
		return;
	struct entry *e = &f->ent[f->sel];
	f->show_props = true;
	if (e->is_dir) {
		char full[PATH_MAX];
		join(full, sizeof(full), f->cwd, e->name);
		f->props_size = 0;
		f->props_count = 0;
		f->props_capped = false;
		int visited = 0;
		dir_stats(full, &f->props_size, &f->props_count, &visited, &f->props_capped);
	} else {
		f->props_size = e->size;
		f->props_count = -1;
		f->props_capped = false;
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
	if (f->input_mode == 4) {
		/* go to path: absolute, or ~/ for the home folder */
		char p[PATH_MAX];
		if (f->input[0] == '~' && (f->input[1] == '/' || f->input[1] == 0)) {
			const char *home = getenv("HOME");
			snprintf(p, sizeof(p), "%s%s", home ? home : "/home/user", f->input + 1);
		} else {
			snprintf(p, sizeof(p), "%s", f->input);
		}
		struct stat st;
		if (p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode))
			navigate(f, p, true);
		else
			snprintf(f->status, sizeof(f->status), "フォルダがありません: %s", f->input);
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
		if (inr(x, y, TB_OK)) {
			commit_input(f);
			return;
		}
		if (inr(x, y, TB_CANCEL)) {
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
	/* modal dialogs swallow everything else */
	if (f->confirm_delete) {
		if (inr(x, y, DEL_OK))
			do_delete(f);
		else if (inr(x, y, DEL_CANCEL))
			f->confirm_delete = false;
		return;
	}
	if (f->show_props) {
		if (inr(x, y, PROP_CLOSE))
			f->show_props = false;
		return;
	}
	/* toolbar */
	if (inr(x, y, TB_BACK)) {
		if (f->hist_pos > 0) {
			f->hist_pos--;
			snprintf(f->cwd, sizeof(f->cwd), "%s", f->hist[f->hist_pos]);
			load(f);
		}
		return;
	}
	if (inr(x, y, TB_FWD)) {
		if (f->hist_pos < f->hist_len - 1) {
			f->hist_pos++;
			snprintf(f->cwd, sizeof(f->cwd), "%s", f->hist[f->hist_pos]);
			load(f);
		}
		return;
	}
	if (inr(x, y, TB_UP)) {
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
	if (inr(x, y, TB_VIEW)) {
		f->grid = !f->grid;
		f->scroll = 0;
		return;
	}
	/* search field: opens the OSK with the current query prefilled */
	if (inr(x, y, TB_SEARCH)) {
		f->input_mode = 3;
		snprintf(f->input, sizeof(f->input), "%s", f->query);
		bu_osk_init(&f->osk);
		f->osk.kana = false;
		return;
	}
	if (inr(x, y, TB_SORT)) {
		f->sort = (f->sort + 1) % 4;
		load(f);
		return;
	}
	if (inr(x, y, TB_HIDDEN)) {
		f->show_hidden = !f->show_hidden;
		load(f);
		return;
	}
	if (inr(x, y, TB_NEW)) {
		f->input_mode = 2;
		f->input[0] = 0;
		bu_osk_init(&f->osk);
		f->osk.kana = false;
		return;
	}
	/* breadcrumbs: tap a crumb to jump; tap the empty part of
	 * the bar to type a path instead */
	if (inr(x, y, TB_CRUMB)) {
		struct crumb crumbs[24];
		int nc = build_crumbs(f, a->theme->font, crumbs, 24);
		for (int i = 0; i < nc; i++) {
			if (inr(x, y, crumbs[i].r)) {
				if (strcmp(crumbs[i].path, f->cwd) != 0)
					navigate(f, crumbs[i].path, true);
				return;
			}
		}
		f->input_mode = 4;
		snprintf(f->input, sizeof(f->input), "%s", f->cwd);
		bu_osk_init(&f->osk);
		f->osk.kana = false;
		return;
	}
	/* sidebar */
	for (int i = 0; i < NPLACES; i++) {
		bu_rect r = { 14, CONT_Y + 6 + i * 36, SIDE_W - 12, 31 };
		if (inr(x, y, r)) {
			char td[PATH_MAX];
			trash_dir(td, sizeof(td), "files");
			if (strcmp(PLACES[i].path, td) == 0)
				trash_ensure();
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
	if (y >= ACT_Y && y < ACT_Y + 42) {
		struct actbtn btns[8];
		int nb = act_layout(f, btns);
		for (int i = 0; i < nb; i++) {
			if (!inr(x, y, btns[i].r))
				continue;
			struct entry *e = f->sel >= 0 ? &f->ent[f->sel] : NULL;
			switch (btns[i].id) {
			case ACT_OPEN:
				do_open(f, f->sel);
				break;
			case ACT_RESTORE:
				do_restore(f);
				break;
			case ACT_COPY:
			case ACT_CUT:
				join(f->clip, sizeof(f->clip), f->cwd, e->name);
				f->clip_cut = btns[i].id == ACT_CUT;
				snprintf(f->status, sizeof(f->status),
					 btns[i].id == ACT_CUT ? "切り取りました" : "コピーしました");
				f->sel = -1;
				break;
			case ACT_PASTE:
				do_paste(f);
				break;
			case ACT_RENAME:
				f->input_mode = 1;
				snprintf(f->input, sizeof(f->input), "%s", e->name);
				bu_osk_init(&f->osk);
				f->osk.kana = false;
				break;
			case ACT_INFO:
				open_props(f);
				break;
			case ACT_DELETE:
				f->confirm_delete = true;
				break;
			case ACT_CANCEL:
				f->clip[0] = 0;
				break;
			}
			return;
		}
		if (nb)
			return;
	}
	/* column headers (list view): tap to sort, tap the active
	 * column again to reverse */
	if (!f->grid && y >= cont.y && y < cont.y + 22 && x >= cont.x && x < cont.x + cont.w) {
		int mode = -1;
		if (x < cont.x + 300)
			mode = 0;
		else if (x >= cont.x + cont.w - 130)
			mode = 2;
		else if (x >= cont.x + cont.w - 280)
			mode = 1;
		if (mode >= 0) {
			if (f->sort == mode)
				f->sort_rev = !f->sort_rev;
			else {
				f->sort = mode;
				f->sort_rev = false;
			}
			load(f);
			return;
		}
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
