/* bd-notes: notes for braindesk (v3).
 * Three panes: folders | note list | editor, following the
 * Apple Notes layout. Notes are plain text files in ~/Notes,
 * one file per note, first line is the title; subdirectories
 * of ~/Notes are folders (the root itself is the "メモ"
 * folder). Editing auto-saves (on switch, on close, and 2 s
 * after the last change). Lines starting with "[ ] " or
 * "[x] " are checklists: tap the line to toggle. Deleted
 * notes go to ~/Notes/.trash and stay recoverable for 30
 * days. Pin state and creation times live in ~/Notes/.meta.
 * Input: on-screen keyboard (romaji->kana) or physical keys.
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#define MAXFOLDERS 32
#define MAXNOTES 256
#define MAXTRASH 128
#define TEXTMAX 16384
#define MAXDL 640

struct nentry {
	int folder;		/* index into folders[] */
	char file[256];
	time_t mtime, created;
	bool pinned;
	char title[128];
	char snippet[128];
};

struct tentry {
	char tfile[320];	/* name inside .trash */
	char folder[128];	/* original folder ("" = root) */
	char file[256];
	time_t deleted;
	char title[128];
};

struct dline {
	int off, len;		/* byte range in the text buffer */
	int logical;		/* logical line index */
	bool first;		/* first display segment of the line */
};

enum { OT_NONE = 0, OT_NOTE, OT_SEARCH, OT_FOLDER };
enum { VIEW_ALL = -2, VIEW_TRASH = -1 };

struct notes {
	char dir[512];
	char folders[MAXFOLDERS][128];	/* [0] = "" (root) */
	int nfolders;
	struct nentry ents[MAXNOTES];
	int nents;
	struct tentry trents[MAXTRASH];
	int ntrents;
	int disp[MAXNOTES];
	int ndisp;
	int view;		/* VIEW_ALL, VIEW_TRASH or folder idx */
	int scroll, current;	/* current: index into ents[], -1 none */
	int tsel;		/* selected trash entry, -1 none */
	char text[TEXTMAX];
	int len;
	bool dirty;
	time_t last_edit;
	int sort;		/* 0 edited, 1 created, 2 title */
	bool search_on;
	char search[64];
	int match_off[256], nmatch, match_idx;
	struct dline dl[MAXDL];
	int ndl, ed_scroll;
	bu_osk osk;
	int osk_target;		/* OT_*; OT_NONE = keyboard closed */
	bool move_open;
	bool folder_sheet;	/* folder name sheet visible */
	int folder_edit;	/* folder idx being renamed, -1 = new */
	char folder_name[128];
	bool confirm_fdel, confirm_perm;
	char status[96];
};

/* ---------- small helpers ---------- */

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* ASCII case-insensitive substring search (multibyte bytes
 * are compared as-is, so Japanese matches exactly). */
static const char *ci_strstr(const char *hay, const char *needle)
{
	if (!*needle)
		return hay;
	for (const char *p = hay; *p; p++) {
		const char *a = p, *b = needle;
		while (*a && *b &&
		       tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
			a++;
			b++;
		}
		if (!*b)
			return p;
	}
	return NULL;
}

static void path_join(char *out, size_t n, const char *a, const char *b)
{
	if (b && *b)
		snprintf(out, n, "%s/%s", a, b);
	else
		snprintf(out, n, "%s", a);
}

static void note_abspath(struct notes *nt, struct nentry *e, char *out, size_t n)
{
	char rel[400];
	if (e->folder > 0 && nt->folders[e->folder][0])
		snprintf(rel, sizeof(rel), "%s/%s", nt->folders[e->folder], e->file);
	else
		snprintf(rel, sizeof(rel), "%s", e->file);
	path_join(out, n, nt->dir, rel);
}

static void rel_of(struct notes *nt, struct nentry *e, char *out, size_t n)
{
	if (e->folder > 0 && nt->folders[e->folder][0])
		snprintf(out, n, "%s/%s", nt->folders[e->folder], e->file);
	else
		snprintf(out, n, "%s", e->file);
}

/* ---------- .meta (pin + created) ---------- */

static char meta_lines[MAXNOTES + MAXTRASH][384];
static int nmeta;

static void meta_load(struct notes *nt)
{
	char path[600];
	path_join(path, sizeof(path), nt->dir, ".meta");
	nmeta = 0;
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	char line[384];
	while (nmeta < MAXNOTES + MAXTRASH && fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0])
			snprintf(meta_lines[nmeta++], 384, "%s", line);
	}
	fclose(fp);
}

static void meta_save(struct notes *nt)
{
	char path[600];
	path_join(path, sizeof(path), nt->dir, ".meta");
	FILE *fp = fopen(path, "w");
	if (!fp)
		return;
	for (int i = 0; i < nmeta; i++)
		fprintf(fp, "%s\n", meta_lines[i]);
	fclose(fp);
}

/* find meta line for relpath; returns index or -1 */
static int meta_find(const char *rel)
{
	for (int i = 0; i < nmeta; i++) {
		const char *tab = strchr(meta_lines[i], '\t');
		if (!tab)
			continue;
		if ((size_t)(tab - meta_lines[i]) == strlen(rel) &&
		    strncmp(meta_lines[i], rel, strlen(rel)) == 0)
			return i;
	}
	return -1;
}

static void meta_get(const char *rel, bool *pinned, time_t *created)
{
	*pinned = false;
	*created = 0;
	int i = meta_find(rel);
	if (i < 0)
		return;
	int pin = 0;
	long cr = 0;
	if (sscanf(meta_lines[i], "%*[^\t]\t%d\t%ld", &pin, &cr) >= 1) {
		*pinned = pin != 0;
		*created = (time_t)cr;
	}
}

static void meta_set(struct notes *nt, const char *rel, bool pinned, time_t created)
{
	int i = meta_find(rel);
	char line[384];
	snprintf(line, sizeof(line), "%s\t%d\t%ld", rel, pinned ? 1 : 0, (long)created);
	if (i >= 0)
		snprintf(meta_lines[i], 384, "%s", line);
	else if (nmeta < MAXNOTES + MAXTRASH)
		snprintf(meta_lines[nmeta++], 384, "%s", line);
	meta_save(nt);
}

/* ---------- scanning ---------- */

static void read_head(const char *path, char *title, size_t tn, char *snip, size_t sn)
{
	title[0] = 0;
	snip[0] = 0;
	FILE *fp = fopen(path, "r");
	if (!fp) {
		snprintf(title, tn, "(無題)");
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0])
			continue;
		if (!title[0]) {
			snprintf(title, tn, "%s", line);
			continue;
		}
		snprintf(snip, sn, "%s", line);
		break;
	}
	fclose(fp);
	if (!title[0])
		snprintf(title, tn, "(無題)");
}

static time_t created_from_name(const char *file, time_t fallback)
{
	/* note-YYYYMMDD-HHMMSS.txt */
	int y, mo, d, h, mi, s;
	if (sscanf(file, "note-%4d%2d%2d-%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) == 6 &&
	    y > 1970 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) {
		struct tm tm;
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = y - 1900;
		tm.tm_mon = mo - 1;
		tm.tm_mday = d;
		tm.tm_hour = h;
		tm.tm_min = mi;
		tm.tm_sec = s;
		return mktime(&tm);
	}
	return fallback;
}

static void scan_folder(struct notes *nt, int fi)
{
	char fdir[640];
	if (fi == 0)
		snprintf(fdir, sizeof(fdir), "%s", nt->dir);
	else
		path_join(fdir, sizeof(fdir), nt->dir, nt->folders[fi]);
	DIR *d = opendir(fdir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && nt->nents < MAXNOTES) {
		const char *dot = strrchr(de->d_name, '.');
		if (!dot || strcmp(dot, ".txt") != 0)
			continue;
		struct nentry *e = &nt->ents[nt->nents];
		memset(e, 0, sizeof(*e));
		e->folder = fi;
		snprintf(e->file, sizeof(e->file), "%s", de->d_name);
		char path[768];
		note_abspath(nt, e, path, sizeof(path));
		struct stat st;
		e->mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
		char rel[400];
		rel_of(nt, e, rel, sizeof(rel));
		meta_get(rel, &e->pinned, &e->created);
		if (!e->created)
			e->created = created_from_name(e->file, e->mtime);
		read_head(path, e->title, sizeof(e->title), e->snippet, sizeof(e->snippet));
		nt->nents++;
	}
	closedir(d);
}

static void scan_trash(struct notes *nt)
{
	char tdir[600];
	path_join(tdir, sizeof(tdir), nt->dir, ".trash");
	nt->ntrents = 0;
	DIR *d = opendir(tdir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && nt->ntrents < MAXTRASH) {
		/* format: <epoch>__<folder>__<file> */
		char name[320];
		snprintf(name, sizeof(name), "%s", de->d_name);
		char *p1 = strstr(name, "__");
		if (!p1)
			continue;
		char *last = strstr(p1 + 2, "__");
		if (!last)
			continue;
		struct tentry *te = &nt->trents[nt->ntrents];
		memset(te, 0, sizeof(*te));
		snprintf(te->tfile, sizeof(te->tfile), "%s", de->d_name);
		te->deleted = (time_t)strtol(name, NULL, 10);
		*p1 = 0;
		*last = 0;
		snprintf(te->folder, sizeof(te->folder), "%s", p1 + 2);
		if (strcmp(te->folder, "_root_") == 0)
			te->folder[0] = 0;
		snprintf(te->file, sizeof(te->file), "%s", last + 2);
		char path[800];
		snprintf(path, sizeof(path), "%s/%s", tdir, te->tfile);
		char snip[128];
		read_head(path, te->title, sizeof(te->title), snip, sizeof(snip));
		nt->ntrents++;
	}
	closedir(d);
	/* newest deletion first */
	for (int i = 0; i < nt->ntrents; i++)
		for (int j = i + 1; j < nt->ntrents; j++)
			if (nt->trents[j].deleted > nt->trents[i].deleted) {
				struct tentry tmp = nt->trents[i];
				nt->trents[i] = nt->trents[j];
				nt->trents[j] = tmp;
			}
}

static void purge_trash(struct notes *nt)
{
	char tdir[600];
	path_join(tdir, sizeof(tdir), nt->dir, ".trash");
	time_t limit = time(NULL) - 30 * 24 * 3600;
	DIR *d = opendir(tdir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (!strstr(de->d_name, "__"))
			continue;
		if ((time_t)strtol(de->d_name, NULL, 10) < limit) {
			char path[800];
			snprintf(path, sizeof(path), "%s/%s", tdir, de->d_name);
			remove(path);
		}
	}
	closedir(d);
}

static void scan_all(struct notes *nt)
{
	/* folders: root + subdirectories not starting with '.' */
	nt->nfolders = 1;
	nt->folders[0][0] = 0;
	DIR *d = opendir(nt->dir);
	if (d) {
		struct dirent *de;
		char names[MAXFOLDERS][128];
		int nn = 0;
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.')
				continue;
			char path[640];
			path_join(path, sizeof(path), nt->dir, de->d_name);
			struct stat st;
			if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && nn < MAXFOLDERS - 1)
				snprintf(names[nn++], 128, "%s", de->d_name);
		}
		closedir(d);
		for (int i = 0; i < nn; i++)
			for (int j = i + 1; j < nn; j++)
				if (strcmp(names[j], names[i]) < 0) {
					char tmp[128];
					snprintf(tmp, sizeof(tmp), "%s", names[i]);
					snprintf(names[i], 128, "%s", names[j]);
					snprintf(names[j], 128, "%s", tmp);
				}
		for (int i = 0; i < nn; i++)
			snprintf(nt->folders[nt->nfolders++], 128, "%s", names[i]);
	}
	meta_load(nt);
	nt->nents = 0;
	for (int fi = 0; fi < nt->nfolders; fi++)
		scan_folder(nt, fi);
	scan_trash(nt);
	if (nt->view >= nt->nfolders)
		nt->view = VIEW_ALL;
}

/* ---------- display list ---------- */

static bool note_matches(struct notes *nt, struct nentry *e)
{
	if (!nt->search[0])
		return true;
	if (ci_strstr(e->title, nt->search) || ci_strstr(e->snippet, nt->search))
		return true;
	/* full body */
	char path[768];
	note_abspath(nt, e, path, sizeof(path));
	FILE *fp = fopen(path, "r");
	if (!fp)
		return false;
	static char buf[TEXTMAX];
	int n = (int)fread(buf, 1, TEXTMAX - 1, fp);
	fclose(fp);
	if (n < 0)
		return false;
	buf[n] = 0;
	return ci_strstr(buf, nt->search) != NULL;
}

static void rebuild_disp(struct notes *nt)
{
	nt->ndisp = 0;
	for (int i = 0; i < nt->nents; i++) {
		struct nentry *e = &nt->ents[i];
		if (nt->view >= 0 && e->folder != nt->view)
			continue;
		if (!note_matches(nt, e))
			continue;
		nt->disp[nt->ndisp++] = i;
	}
	/* pinned first, then the sort key */
	for (int i = 0; i < nt->ndisp; i++)
		for (int j = i + 1; j < nt->ndisp; j++) {
			struct nentry *a = &nt->ents[nt->disp[i]];
			struct nentry *b = &nt->ents[nt->disp[j]];
			bool swap = false;
			if (a->pinned != b->pinned)
				swap = b->pinned;
			else if (nt->sort == 0)
				swap = b->mtime > a->mtime;
			else if (nt->sort == 1)
				swap = b->created > a->created;
			else
				swap = strcmp(b->title, a->title) < 0;
			if (swap) {
				int tmp = nt->disp[i];
				nt->disp[i] = nt->disp[j];
				nt->disp[j] = tmp;
			}
		}
	if (nt->scroll > nt->ndisp)
		nt->scroll = nt->ndisp > 0 ? nt->ndisp - 1 : 0;
}

/* ---------- current note io ---------- */

static void flush_pending(struct notes *nt)
{
	if (nt->osk_target == OT_NOTE && nt->osk.pending[0]) {
		char out[32];
		const char *s = bu_osk_flush(&nt->osk, out, sizeof(out));
		if (s && *s) {
			int add = (int)strlen(s);
			if (nt->len + add < TEXTMAX - 1) {
				memcpy(nt->text + nt->len, s, add + 1);
				nt->len += add;
			}
		}
	}
}

static void save_current(struct notes *nt)
{
	if (nt->current < 0 || !nt->dirty)
		return;
	flush_pending(nt);
	char path[768];
	note_abspath(nt, &nt->ents[nt->current], path, sizeof(path));
	FILE *fp = fopen(path, "w");
	if (fp) {
		fwrite(nt->text, 1, nt->len, fp);
		fclose(fp);
		nt->dirty = false;
		struct stat st;
		if (stat(path, &st) == 0)
			nt->ents[nt->current].mtime = st.st_mtime;
		snprintf(nt->status, sizeof(nt->status), "保存しました");
	}
}

static void compute_matches(struct notes *nt)
{
	nt->nmatch = 0;
	nt->match_idx = 0;
	if (!nt->search[0] || nt->current < 0)
		return;
	const char *p = nt->text;
	while (nt->nmatch < 256) {
		const char *m = ci_strstr(p, nt->search);
		if (!m)
			break;
		nt->match_off[nt->nmatch++] = (int)(m - nt->text);
		p = m + 1;
	}
}

static void open_note(struct notes *nt, int i)
{
	save_current(nt);
	nt->current = i;
	nt->tsel = -1;
	nt->len = 0;
	nt->text[0] = 0;
	nt->dirty = false;
	nt->ed_scroll = 0;
	if (i < 0)
		return;
	char path[768];
	note_abspath(nt, &nt->ents[i], path, sizeof(path));
	FILE *fp = fopen(path, "r");
	if (fp) {
		nt->len = (int)fread(nt->text, 1, TEXTMAX - 1, fp);
		nt->text[nt->len] = 0;
		fclose(fp);
	}
	compute_matches(nt);
	snprintf(nt->status, sizeof(nt->status), "%s", nt->ents[i].title);
}

static void open_trash(struct notes *nt, int i)
{
	save_current(nt);
	nt->current = -1;
	nt->tsel = i;
	nt->len = 0;
	nt->text[0] = 0;
	nt->dirty = false;
	nt->ed_scroll = 0;
	nt->nmatch = 0;
	if (i < 0)
		return;
	char path[800];
	snprintf(path, sizeof(path), "%s/.trash/%s", nt->dir, nt->trents[i].tfile);
	FILE *fp = fopen(path, "r");
	if (fp) {
		nt->len = (int)fread(nt->text, 1, TEXTMAX - 1, fp);
		nt->text[nt->len] = 0;
		fclose(fp);
	}
	snprintf(nt->status, sizeof(nt->status), "削除済み: %s", nt->trents[i].title);
}

static void refresh(struct notes *nt)
{
	int curf = -1;
	char curfile[256] = "";
	if (nt->current >= 0) {
		curf = nt->ents[nt->current].folder;
		snprintf(curfile, sizeof(curfile), "%s", nt->ents[nt->current].file);
	}
	scan_all(nt);
	rebuild_disp(nt);
	nt->current = -1;
	if (curf >= 0) {
		for (int i = 0; i < nt->nents; i++)
			if (nt->ents[i].folder == curf &&
			    strcmp(nt->ents[i].file, curfile) == 0) {
				nt->current = i;
				break;
			}
	}
	if (nt->tsel >= nt->ntrents)
		nt->tsel = -1;
	compute_matches(nt);
}

static void new_note(struct notes *nt)
{
	save_current(nt);
	int fi = nt->view >= 0 ? nt->view : 0;
	char name[256];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	snprintf(name, sizeof(name), "note-%04d%02d%02d-%02d%02d%02d.txt",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	char fdir[640];
	if (fi == 0)
		snprintf(fdir, sizeof(fdir), "%s", nt->dir);
	else
		path_join(fdir, sizeof(fdir), nt->dir, nt->folders[fi]);
	char path[768];
	snprintf(path, sizeof(path), "%s/%s", fdir, name);
	FILE *fp = fopen(path, "w");
	if (fp)
		fclose(fp);
	char rel[400];
	if (fi > 0)
		snprintf(rel, sizeof(rel), "%s/%s", nt->folders[fi], name);
	else
		snprintf(rel, sizeof(rel), "%s", name);
	meta_set(nt, rel, false, now);
	nt->search[0] = 0;
	nt->view = fi;
	refresh(nt);
	for (int i = 0; i < nt->nents; i++)
		if (nt->ents[i].folder == fi && strcmp(nt->ents[i].file, name) == 0) {
			open_note(nt, i);
			break;
		}
	nt->osk_target = OT_NOTE;
	bu_osk_init(&nt->osk);
	snprintf(nt->status, sizeof(nt->status), "新規メモ");
}

static void trash_note(struct notes *nt, int i)
{
	if (i < 0)
		return;
	struct nentry *e = &nt->ents[i];
	char src[768];
	note_abspath(nt, e, src, sizeof(src));
	char tname[320];
	const char *ftag = e->folder > 0 ? nt->folders[e->folder] : "_root_";
	snprintf(tname, sizeof(tname), "%ld__%s__%s", (long)time(NULL), ftag, e->file);
	char dst[800];
	snprintf(dst, sizeof(dst), "%s/.trash/%s", nt->dir, tname);
	if (rename(src, dst) != 0)
		return;
	if (nt->current == i) {
		nt->current = -1;
		nt->len = 0;
		nt->text[0] = 0;
		nt->dirty = false;
	}
	refresh(nt);
	snprintf(nt->status, sizeof(nt->status), "ゴミ箱へ移動しました");
}

static void restore_trash(struct notes *nt, int i)
{
	if (i < 0 || i >= nt->ntrents)
		return;
	struct tentry *te = &nt->trents[i];
	int fi = 0;
	for (int k = 1; k < nt->nfolders; k++)
		if (strcmp(nt->folders[k], te->folder) == 0) {
			fi = k;
			break;
		}
	char fdir[640];
	if (fi == 0)
		snprintf(fdir, sizeof(fdir), "%s", nt->dir);
	else
		path_join(fdir, sizeof(fdir), nt->dir, nt->folders[fi]);
	char dst[768];
	snprintf(dst, sizeof(dst), "%s/%s", fdir, te->file);
	struct stat st;
	if (stat(dst, &st) == 0) {
		char alt[256];
		snprintf(alt, sizeof(alt), "%.*s-2.txt", (int)strlen(te->file) - 4, te->file);
		snprintf(dst, sizeof(dst), "%s/%s", fdir, alt);
	}
	char src[800];
	snprintf(src, sizeof(src), "%s/.trash/%s", nt->dir, te->tfile);
	if (rename(src, dst) == 0) {
		nt->tsel = -1;
		nt->len = 0;
		nt->text[0] = 0;
		refresh(nt);
		snprintf(nt->status, sizeof(nt->status), "復元しました");
	}
}

static void delete_trash(struct notes *nt, int i)
{
	if (i < 0 || i >= nt->ntrents)
		return;
	char path[800];
	snprintf(path, sizeof(path), "%s/.trash/%s", nt->dir, nt->trents[i].tfile);
	remove(path);
	nt->tsel = -1;
	nt->len = 0;
	nt->text[0] = 0;
	refresh(nt);
	snprintf(nt->status, sizeof(nt->status), "完全に削除しました");
}

/* ---------- editor layout (wrapped display lines) ---------- */

static bool line_is_check(struct notes *nt, int logical, int *checked)
{
	/* find the start of logical line `logical` */
	int li = 0, pos = 0;
	while (pos < nt->len && li < logical) {
		if (nt->text[pos] == '\n')
			li++;
		pos++;
	}
	if (pos + 2 < nt->len &&
	    nt->text[pos] == '[' && nt->text[pos + 2] == ']' &&
	    (nt->text[pos + 1] == ' ' || nt->text[pos + 1] == 'x') &&
	    (pos + 3 >= nt->len || nt->text[pos + 3] == ' ' || nt->text[pos + 3] == '\n')) {
		*checked = nt->text[pos + 1] == 'x';
		return true;
	}
	return false;
}

static void build_dlines(struct notes *nt, bu_font *font, int maxw)
{
	nt->ndl = 0;
	int li = 0, start = 0;
	for (int i = 0; ; i++) {
		if (i > nt->len)
			break;
		char ch = i < nt->len ? nt->text[i] : 0;
		if (ch == '\n' || ch == 0) {
			/* wrap [start, i) */
			int seg = start;
			bool first = true;
			for (;;) {
				if (nt->ndl >= MAXDL)
					break;
				int remain = i - seg;
				char tmp[512];
				int take = remain > 500 ? 500 : remain;
				memcpy(tmp, nt->text + seg, take);
				tmp[take] = 0;
				if (remain == 0 ||
				    (remain <= 500 && bu_text_width(font, tmp, 14) <= maxw)) {
					nt->dl[nt->ndl].off = seg;
					nt->dl[nt->ndl].len = remain;
					nt->dl[nt->ndl].logical = li;
					nt->dl[nt->ndl].first = first;
					nt->ndl++;
					break;
				}
				/* shrink until it fits (UTF-8 safe) */
				int cut = take;
				while (cut > 1) {
					cut--;
					while (cut > 0 && (tmp[cut] & 0xC0) == 0x80)
						cut--;
					tmp[cut] = 0;
					if (bu_text_width(font, tmp, 14) <= maxw)
						break;
					tmp[cut] = nt->text[seg + cut];
				}
				if (cut <= 0)
					cut = 1;
				nt->dl[nt->ndl].off = seg;
				nt->dl[nt->ndl].len = cut;
				nt->dl[nt->ndl].logical = li;
				nt->dl[nt->ndl].first = first;
				nt->ndl++;
				first = false;
				seg += cut;
			}
			li++;
			start = i + 1;
			if (ch == 0)
				break;
		}
	}
	if (nt->ed_scroll > nt->ndl - 1)
		nt->ed_scroll = nt->ndl > 0 ? nt->ndl - 1 : 0;
	if (nt->ed_scroll < 0)
		nt->ed_scroll = 0;
}

static void toggle_check_at(struct notes *nt, int off)
{
	/* off = byte offset of '[' in "[ ] "/"[x] " */
	if (off + 2 < nt->len && nt->text[off] == '[' && nt->text[off + 2] == ']') {
		nt->text[off + 1] = nt->text[off + 1] == 'x' ? ' ' : 'x';
		nt->dirty = true;
		nt->last_edit = time(NULL);
	}
}

/* ---------- tick ---------- */

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct notes *nt = ctx;
	if (nt->dirty && time(NULL) - nt->last_edit >= 2) {
		save_current(nt);
		*changed = true;
	}
}

/* ---------- layout constants ---------- */

#define TB_Y 48
#define FOLD_X 12
#define FOLD_W 146
#define LIST_X 166
#define LIST_W 254
#define EDIT_X 428
#define EDIT_W 424
#define ROW_H 56

static int panes_top(struct notes *nt)
{
	return nt->search_on ? 128 : 94;
}

#define PANES_BOT 434

/* ---------- draw ---------- */

static void draw(bdapp *a, void *ctx)
{
	struct notes *nt = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* toolbar */
	bu_button(c, t, (bu_rect){ 12, TB_Y, 116, 38 }, "＋ 新規", BU_BTN_PRIMARY, false);
	bu_button(c, t, (bu_rect){ 136, TB_Y, 116, 38 }, "＋ フォルダ", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 260, TB_Y, 96, 38 }, "検索", nt->search_on ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	const char *sorts[3] = { "並替: 更新順", "並替: 作成順", "並替: タイトル順" };
	bu_button(c, t, (bu_rect){ 364, TB_Y, 170, 38 }, sorts[nt->sort], BU_BTN_NORMAL, false);
	if (nt->osk_target != OT_NONE)
		bu_button(c, t, (bu_rect){ 748, TB_Y, 104, 38 }, "閉じる", BU_BTN_NORMAL, false);
	if (nt->status[0])
		bu_draw_text_right(c, t->font, (bu_rect){ 544, TB_Y + 9, 196, 20 }, nt->status, 12, t->text_dim);
	/* search bar */
	if (nt->search_on) {
		char sb[160];
		if (nt->search[0]) {
			if (nt->current >= 0)
				snprintf(sb, sizeof(sb), "検索「%s」: 一覧 %d 件 · 本文に %d 件", nt->search, nt->ndisp, nt->nmatch);
			else
				snprintf(sb, sizeof(sb), "検索「%s」: %d 件", nt->search, nt->ndisp);
		} else {
			snprintf(sb, sizeof(sb), "検索: キーボードで入力 (タイトル・本文)");
		}
		bu_draw_text(c, t->font, 16, 98, sb, 12.5, t->accent);
		if (nt->nmatch > 0)
			bu_button(c, t, (bu_rect){ 640, 92, 100, 30 }, "次の一致", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 748, 92, 104, 30 }, "クリア", BU_BTN_NORMAL, false);
	}
	int top = panes_top(nt);
	int ph = PANES_BOT - top;
	/* folders pane */
	bu_rect fp = { FOLD_X, top, FOLD_W, ph };
	bu_fill_round_rect(c, fp, 10, t->surface);
	struct { int view; const char *label; int count; } frows[MAXFOLDERS + 2];
	int nf = 0;
	frows[nf].view = VIEW_ALL; frows[nf].label = "すべて"; frows[nf].count = nt->nents; nf++;
	for (int i = 0; i < nt->nfolders; i++) {
		frows[nf].view = i;
		frows[nf].label = i == 0 ? "メモ" : nt->folders[i];
		int cnt = 0;
		for (int j = 0; j < nt->nents; j++)
			if (nt->ents[j].folder == i)
				cnt++;
		frows[nf].count = cnt;
		nf++;
	}
	frows[nf].view = VIEW_TRASH; frows[nf].label = "最近削除した項目"; frows[nf].count = nt->ntrents; nf++;
	for (int i = 0; i < nf; i++) {
		bu_rect r = { fp.x + 6, fp.y + 6 + i * 40, fp.w - 12, 36 };
		if (r.y + r.h > fp.y + fp.h - 44)
			break;
		if (frows[i].view == nt->view)
			bu_fill_round_rect(c, r, 7, t->surface3);
		bu_draw_text(c, t->font, r.x + 8, r.y + 9, frows[i].label, 12.5, t->text);
		char cb[16];
		snprintf(cb, sizeof(cb), "%d", frows[i].count);
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 9, r.w - 8, 18 }, cb, 11.5, t->text_faint);
	}
	if (nt->view > 0) {
		bu_button(c, t, (bu_rect){ fp.x + 6, PANES_BOT - 40, 62, 34 }, "改名", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ fp.x + 76, PANES_BOT - 40, 64, 34 }, nt->confirm_fdel ? "本当に削除" : "削除", BU_BTN_DANGER, false);
	}
	/* list pane */
	bu_rect lp = { LIST_X, top, LIST_W, ph };
	bu_fill_round_rect(c, lp, 10, t->surface);
	int visible = (ph - 52) / ROW_H;
	if (nt->view == VIEW_TRASH) {
		for (int vi = 0; vi < visible; vi++) {
			int i = nt->scroll + vi;
			if (i >= nt->ntrents)
				break;
			bu_rect r = { lp.x + 6, lp.y + 6 + vi * ROW_H, lp.w - 12, ROW_H - 4 };
			if (i == nt->tsel)
				bu_fill_round_rect(c, r, 7, t->surface3);
			bu_draw_text(c, t->font, r.x + 10, r.y + 4, nt->trents[i].title, 13, t->text);
			struct tm tm;
			localtime_r(&nt->trents[i].deleted, &tm);
			char dt[96];
			snprintf(dt, sizeof(dt), "削除 %d/%02d/%02d · %s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				 nt->trents[i].folder[0] ? nt->trents[i].folder : "メモ");
			bu_draw_text(c, t->font, r.x + 10, r.y + 26, dt, 11, t->text_faint);
		}
		if (nt->ntrents == 0)
			bu_draw_text(c, t->font, lp.x + 14, lp.y + 16, "ゴミ箱は空です", 12.5, t->text_dim);
	} else {
		for (int vi = 0; vi < visible; vi++) {
			int di = nt->scroll + vi;
			if (di >= nt->ndisp)
				break;
			struct nentry *e = &nt->ents[nt->disp[di]];
			bu_rect r = { lp.x + 6, lp.y + 6 + vi * ROW_H, lp.w - 12, ROW_H - 4 };
			if (nt->disp[di] == nt->current)
				bu_fill_round_rect(c, r, 7, t->surface3);
			int tx = r.x + 10;
			if (e->pinned) {
				bu_fill_round_rect(c, (bu_rect){ tx, r.y + 7, 30, 15 }, 4, t->accent);
				bu_draw_text(c, t->font, tx + 3, r.y + 6, "ピン", 10.5, t->surface);
				tx += 36;
			}
			bu_draw_text(c, t->font, tx, r.y + 4, e->title, 13, t->text);
			struct tm tm;
			localtime_r(&e->mtime, &tm);
			char dt[160];
			snprintf(dt, sizeof(dt), "%d/%02d/%02d %02d:%02d  %s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				 tm.tm_hour, tm.tm_min, e->snippet);
			bu_draw_text(c, t->font, r.x + 10, r.y + 26, dt, 11, t->text_faint);
		}
		if (nt->ndisp == 0)
			bu_draw_text(c, t->font, lp.x + 14, lp.y + 16,
				     nt->search[0] ? "一致するメモがありません" : "メモがありません", 12.5, t->text_dim);
	}
	bu_button(c, t, (bu_rect){ LIST_X + 6, PANES_BOT - 40, 118, 34 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ LIST_X + 130, PANES_BOT - 40, 118, 34 }, "▼", BU_BTN_NORMAL, false);
	/* editor pane */
	bu_rect ep = { EDIT_X, top, EDIT_W, ph };
	bu_fill_round_rect(c, ep, 10, t->surface);
	bool have = nt->view == VIEW_TRASH ? nt->tsel >= 0 : nt->current >= 0;
	if (have) {
		int maxw = ep.w - 28 - 36;
		build_dlines(nt, t->font, maxw);
		int rows = (ep.h - 20) / 22;
		int cur_match_dl = -1;
		if (nt->nmatch > 0) {
			int mo = nt->match_off[nt->match_idx];
			for (int i = 0; i < nt->ndl; i++)
				if (mo >= nt->dl[i].off && mo < nt->dl[i].off + nt->dl[i].len)
					cur_match_dl = i;
		}
		for (int vi = 0; vi < rows; vi++) {
			int di = nt->ed_scroll + vi;
			if (di >= nt->ndl)
				break;
			struct dline *dl = &nt->dl[di];
			int y = ep.y + 10 + vi * 22;
			if (di == cur_match_dl)
				bu_fill_round_rect(c, (bu_rect){ ep.x + 8, y - 2, ep.w - 48, 21 }, 4, t->surface3);
			char seg[512];
			int sl = dl->len > 500 ? 500 : dl->len;
			memcpy(seg, nt->text + dl->off, sl);
			seg[sl] = 0;
			int tx = ep.x + 14;
			const char *disp = seg;
			if (dl->first && dl->len >= 3 &&
			    seg[0] == '[' && seg[2] == ']' &&
			    (seg[1] == ' ' || seg[1] == 'x') &&
			    (dl->len == 3 || seg[3] == ' ')) {
				/* checklist: draw a checkbox, skip the marker */
				bu_rect box = { tx, y + 2, 15, 15 };
				bu_stroke_round_rect(c, box, 3, 2, seg[1] == 'x' ? t->accent : t->text_dim);
				if (seg[1] == 'x') {
					bu_line(c, tx + 3, y + 9, tx + 6, y + 13, 2, t->accent);
					bu_line(c, tx + 6, y + 13, tx + 12, y + 5, 2, t->accent);
				}
				disp = dl->len >= 4 ? seg + 4 : seg + 3;
				tx += 22;
			}
			bu_draw_text(c, t->font, tx, y, disp, 14,
				     nt->view == VIEW_TRASH ? t->text_dim : t->text);
		}
		if (nt->view != VIEW_TRASH)
			bu_fill_rect(c, (bu_rect){ ep.x + 14, ep.y + ep.h - 14, 8, 3 }, t->accent);
		bu_button(c, t, (bu_rect){ EDIT_X + EDIT_W - 38, top + 8, 30, 30 }, "▲", BU_BTN_GHOST, false);
		bu_button(c, t, (bu_rect){ EDIT_X + EDIT_W - 38, PANES_BOT - 46, 30, 30 }, "▼", BU_BTN_GHOST, false);
	} else {
		bu_draw_text(c, t->font, ep.x + 16, ep.y + 16,
			     nt->view == VIEW_TRASH ? "削除したメモを選ぶと内容を表示します" :
			     "メモを選ぶか、新規で作成してください", 13, t->text_dim);
	}
	/* bottom bar */
	if (nt->view == VIEW_TRASH) {
		if (nt->tsel >= 0) {
			bu_button(c, t, (bu_rect){ EDIT_X, 442, 130, 32 }, "復元", BU_BTN_PRIMARY, false);
			bu_button(c, t, (bu_rect){ EDIT_X + 138, 442, 170, 32 }, nt->confirm_perm ? "本当に完全に削除" : "完全に削除", BU_BTN_DANGER, false);
		}
		bu_draw_text(c, t->font, 16, 449, "削除から30日で自動消去されます", 11.5, t->text_faint);
	} else if (nt->current >= 0) {
		bu_button(c, t, (bu_rect){ EDIT_X, 442, 120, 32 }, nt->osk_target == OT_NOTE ? "閉じる" : "キーボード", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ EDIT_X + 128, 442, 88, 32 }, nt->ents[nt->current].pinned ? "ピン解除" : "ピン留め", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ EDIT_X + 224, 442, 80, 32 }, "移動", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ EDIT_X + 312, 442, 112, 32 }, "削除", BU_BTN_DANGER, false);
		int chars = 0;
		for (int i = 0; i < nt->len; i++)
			if ((nt->text[i] & 0xC0) != 0x80)
				chars++;
		char st[64];
		snprintf(st, sizeof(st), "%d 文字%s", chars, nt->dirty ? " · 未保存" : "");
		bu_draw_text(c, t->font, 16, 449, st, 11.5, nt->dirty ? t->warn : t->text_faint);
	}
	/* move picker sheet */
	if (nt->move_open) {
		int sh = 52 + nt->nfolders * 40 + 8;
		bu_rect sheet = { 282, 96, 300, sh };
		bu_shadow_round_rect(c, sheet, 10, 12, BU_RGBA(0, 0, 0, 90));
		bu_fill_round_rect(c, sheet, 10, t->surface2);
		bu_draw_text(c, t->font, sheet.x + 16, sheet.y + 12, "移動先フォルダ", 13.5, t->text);
		for (int i = 0; i < nt->nfolders; i++) {
			bu_rect r = { sheet.x + 8, sheet.y + 48 + i * 40, sheet.w - 16, 36 };
			if (nt->current >= 0 && nt->ents[nt->current].folder == i)
				bu_fill_round_rect(c, r, 7, t->surface3);
			bu_draw_text(c, t->font, r.x + 10, r.y + 8, i == 0 ? "メモ" : nt->folders[i], 13, t->text);
		}
	}
	/* folder name sheet */
	if (nt->folder_sheet) {
		bu_rect sheet = { 252, 96, 360, 168 };
		bu_shadow_round_rect(c, sheet, 10, 12, BU_RGBA(0, 0, 0, 90));
		bu_fill_round_rect(c, sheet, 10, t->surface2);
		bu_draw_text(c, t->font, sheet.x + 16, sheet.y + 12,
			     nt->folder_edit < 0 ? "新規フォルダ" : "フォルダ名の変更", 13.5, t->text);
		bu_rect fld = { sheet.x + 16, sheet.y + 48, sheet.w - 32, 40 };
		bu_fill_round_rect(c, fld, 8, t->surface);
		bu_draw_text(c, t->font, fld.x + 10, fld.y + 10, nt->folder_name[0] ? nt->folder_name : "フォルダ名", 14,
			     nt->folder_name[0] ? t->text : t->text_faint);
		bu_button(c, t, (bu_rect){ sheet.x + 16, sheet.y + 108, 150, 38 }, "キャンセル", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ sheet.x + 194, sheet.y + 108, 150, 38 }, "決定", BU_BTN_PRIMARY, false);
	}
	/* OSK */
	if (nt->osk_target != OT_NONE) {
		int oskh = bu_osk_height();
		bu_osk_draw(c, t, &nt->osk, (bu_rect){ 0, a->h - oskh, a->w, oskh });
	}
}

/* ---------- text editing ---------- */

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

static void buf_backspace(char *buf)
{
	int l = (int)strlen(buf);
	while (l > 0 && (buf[l - 1] & 0xC0) == 0x80)
		l--;
	if (l > 0)
		buf[l - 1] = 0;
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
	buf_backspace(nt->text);
	nt->len = (int)strlen(nt->text);
	nt->dirty = true;
	nt->last_edit = time(NULL);
}

static void find_next(struct notes *nt, bu_font *font)
{
	if (nt->nmatch == 0)
		return;
	nt->match_idx = (nt->match_idx + 1) % nt->nmatch;
	int mo = nt->match_off[nt->match_idx];
	build_dlines(nt, font, EDIT_W - 64);
	for (int i = 0; i < nt->ndl; i++)
		if (mo >= nt->dl[i].off && mo < nt->dl[i].off + nt->dl[i].len) {
			nt->ed_scroll = i > 0 ? i - 1 : 0;
			break;
		}
}

/* ---------- folder operations ---------- */

static void folder_name_sanitise(char *s)
{
	for (char *p = s; *p; p++)
		if (*p == '/' || *p == '\\' || *p == '\t')
			*p = ' ';
	if (s[0] == '.')
		s[0] = '_';
	/* '_' would break the trash name format in runs; keep single */
}

static void folder_create(struct notes *nt)
{
	folder_name_sanitise(nt->folder_name);
	if (!nt->folder_name[0]) {
		snprintf(nt->status, sizeof(nt->status), "名前が空です");
		return;
	}
	char path[640];
	path_join(path, sizeof(path), nt->dir, nt->folder_name);
	char cmd[700];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
	if (system(cmd) != 0)
		return;
	refresh(nt);
	for (int i = 1; i < nt->nfolders; i++)
		if (strcmp(nt->folders[i], nt->folder_name) == 0)
			nt->view = i;
	snprintf(nt->status, sizeof(nt->status), "フォルダ「%s」を作成", nt->folder_name);
}

static void folder_rename(struct notes *nt)
{
	int fi = nt->folder_edit;
	if (fi <= 0 || fi >= nt->nfolders)
		return;
	folder_name_sanitise(nt->folder_name);
	if (!nt->folder_name[0] || strcmp(nt->folder_name, nt->folders[fi]) == 0)
		return;
	char oldp[640], newp[640];
	path_join(oldp, sizeof(oldp), nt->dir, nt->folders[fi]);
	path_join(newp, sizeof(newp), nt->dir, nt->folder_name);
	if (rename(oldp, newp) != 0) {
		snprintf(nt->status, sizeof(nt->status), "改名できませんでした");
		return;
	}
	/* rewrite meta relpaths under the old prefix */
	char oldpre[200], newpre[200];
	snprintf(oldpre, sizeof(oldpre), "%s/", nt->folders[fi]);
	snprintf(newpre, sizeof(newpre), "%s/", nt->folder_name);
	for (int i = 0; i < nmeta; i++) {
		if (strncmp(meta_lines[i], oldpre, strlen(oldpre)) == 0) {
			char tmp[384];
			snprintf(tmp, sizeof(tmp), "%s%s", newpre, meta_lines[i] + strlen(oldpre));
			snprintf(meta_lines[i], 384, "%s", tmp);
		}
	}
	meta_save(nt);
	refresh(nt);
	for (int i = 1; i < nt->nfolders; i++)
		if (strcmp(nt->folders[i], nt->folder_name) == 0)
			nt->view = i;
	snprintf(nt->status, sizeof(nt->status), "「%s」に改名", nt->folder_name);
}

static void folder_delete(struct notes *nt)
{
	int fi = nt->view;
	if (fi <= 0 || fi >= nt->nfolders)
		return;
	/* move all its notes to trash */
	for (int i = 0; i < nt->nents; i++) {
		struct nentry *e = &nt->ents[i];
		if (e->folder != fi)
			continue;
		char src[768];
		note_abspath(nt, e, src, sizeof(src));
		char tname[320];
		snprintf(tname, sizeof(tname), "%ld__%s__%s", (long)time(NULL), nt->folders[fi], e->file);
		char dst[800];
		snprintf(dst, sizeof(dst), "%s/.trash/%s", nt->dir, tname);
		rename(src, dst);
	}
	char fdir[640];
	path_join(fdir, sizeof(fdir), nt->dir, nt->folders[fi]);
	rmdir(fdir);
	nt->current = -1;
	nt->len = 0;
	nt->text[0] = 0;
	nt->dirty = false;
	nt->view = VIEW_ALL;
	refresh(nt);
	snprintf(nt->status, sizeof(nt->status), "フォルダを削除 (メモはゴミ箱へ)");
}

/* ---------- events ---------- */

static void osk_feed(struct notes *nt, int r, char *out, bool *changed)
{
	if (nt->osk_target == OT_NOTE) {
		if (nt->current < 0)
			return;
		if (r == OSK_TEXT && out[0])
			text_append(nt, out);
		else if (r == OSK_BACKSPACE)
			text_backspace(nt);
		else if (r == OSK_ENTER)
			text_append(nt, "\n");
	} else if (nt->osk_target == OT_SEARCH) {
		if (r == OSK_TEXT && out[0]) {
			if (strlen(nt->search) + strlen(out) < sizeof(nt->search) - 1) {
				strcat(nt->search, out);
				rebuild_disp(nt);
				compute_matches(nt);
			}
		} else if (r == OSK_BACKSPACE) {
			if (nt->osk.pending[0]) {
				nt->osk.pending[strlen(nt->osk.pending) - 1] = 0;
			} else {
				buf_backspace(nt->search);
				rebuild_disp(nt);
				compute_matches(nt);
			}
		} else if (r == OSK_ENTER) {
			nt->osk_target = OT_NONE;
		}
	} else if (nt->osk_target == OT_FOLDER) {
		if (r == OSK_TEXT && out[0]) {
			if (strlen(nt->folder_name) + strlen(out) < sizeof(nt->folder_name) - 1)
				strcat(nt->folder_name, out);
		} else if (r == OSK_BACKSPACE) {
			if (nt->osk.pending[0])
				nt->osk.pending[strlen(nt->osk.pending) - 1] = 0;
			else
				buf_backspace(nt->folder_name);
		} else if (r == OSK_ENTER) {
			if (nt->folder_edit < 0)
				folder_create(nt);
			else
				folder_rename(nt);
			nt->folder_sheet = false;
			nt->osk_target = OT_NONE;
		}
	}
	*changed = true;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct notes *nt = ctx;
	if (ev->type == BU_EV_KEY && ev->value == 1 && nt->osk_target == OT_NONE &&
	    nt->current >= 0 && nt->view != VIEW_TRASH) {
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
	int top = panes_top(nt);
	/* OSK area first */
	if (nt->osk_target != OT_NONE) {
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		if (y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&nt->osk, area, x, y, out, sizeof(out));
			if (r != OSK_NONE)
				osk_feed(nt, r, out, changed);
			return;
		}
	}
	/* close-keyboard button (always reachable above the OSK) */
	if (nt->osk_target != OT_NONE && inr(x, y, (bu_rect){ 748, TB_Y, 104, 38 })) {
		if (nt->osk_target == OT_NOTE)
			save_current(nt);
		nt->osk_target = OT_NONE;
		return;
	}
	/* folder sheet buttons */
	if (nt->folder_sheet) {
		if (inr(x, y, (bu_rect){ 268, 204, 150, 38 })) {
			nt->folder_sheet = false;
			nt->osk_target = OT_NONE;
			return;
		}
		if (inr(x, y, (bu_rect){ 446, 204, 150, 38 })) {
			if (nt->folder_edit < 0)
				folder_create(nt);
			else
				folder_rename(nt);
			nt->folder_sheet = false;
			nt->osk_target = OT_NONE;
			return;
		}
		return; /* modal */
	}
	/* move picker */
	if (nt->move_open) {
		for (int i = 0; i < nt->nfolders; i++) {
			bu_rect r = { 290, 96 + 48 + i * 40, 284, 36 };
			if (inr(x, y, r)) {
				nt->move_open = false;
				if (nt->current >= 0 && nt->ents[nt->current].folder != i) {
					struct nentry *e = &nt->ents[nt->current];
					save_current(nt);
					char src[768];
					note_abspath(nt, e, src, sizeof(src));
					char fdir[640];
					if (i == 0)
						snprintf(fdir, sizeof(fdir), "%s", nt->dir);
					else
						path_join(fdir, sizeof(fdir), nt->dir, nt->folders[i]);
					char dst[768];
					snprintf(dst, sizeof(dst), "%s/%s", fdir, e->file);
					struct stat st;
					if (stat(dst, &st) == 0) {
						snprintf(nt->status, sizeof(nt->status), "移動先に同名メモがあります");
						return;
					}
					char oldrel[400], newrel[400];
					rel_of(nt, e, oldrel, sizeof(oldrel));
					if (rename(src, dst) == 0) {
						bool pin = e->pinned;
						time_t cr = e->created;
						int mi = meta_find(oldrel);
						if (mi >= 0) {
							meta_lines[mi][0] = 0; /* drop; re-add below */
						}
						if (i > 0)
							snprintf(newrel, sizeof(newrel), "%s/%s", nt->folders[i], e->file);
						else
							snprintf(newrel, sizeof(newrel), "%s", e->file);
						/* compact meta */
						int w = 0;
						for (int k = 0; k < nmeta; k++)
							if (meta_lines[k][0])
								snprintf(meta_lines[w++], 384, "%s", meta_lines[k]);
						nmeta = w;
						meta_set(nt, newrel, pin, cr);
						char fname[256];
						snprintf(fname, sizeof(fname), "%s", e->file);
						nt->view = i;
						refresh(nt);
						for (int k = 0; k < nt->nents; k++)
							if (nt->ents[k].folder == i &&
							    strcmp(nt->ents[k].file, fname) == 0) {
								open_note(nt, k);
								break;
							}
						snprintf(nt->status, sizeof(nt->status), "移動しました");
					}
				}
				return;
			}
		}
		nt->move_open = false;
		return;
	}
	/* toolbar */
	if (inr(x, y, (bu_rect){ 12, TB_Y, 116, 38 })) {
		nt->osk_target = OT_NONE;
		new_note(nt);
		return;
	}
	if (inr(x, y, (bu_rect){ 136, TB_Y, 116, 38 })) {
		nt->folder_edit = -1;
		nt->folder_name[0] = 0;
		nt->folder_sheet = true;
		nt->osk_target = OT_FOLDER;
		bu_osk_init(&nt->osk);
		return;
	}
	if (inr(x, y, (bu_rect){ 260, TB_Y, 96, 38 })) {
		nt->search_on = !nt->search_on;
		if (nt->search_on) {
			nt->osk_target = OT_SEARCH;
			bu_osk_init(&nt->osk);
			nt->osk.kana = true;
		} else {
			nt->search[0] = 0;
			if (nt->osk_target == OT_SEARCH)
				nt->osk_target = OT_NONE;
			rebuild_disp(nt);
			compute_matches(nt);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 364, TB_Y, 170, 38 })) {
		nt->sort = (nt->sort + 1) % 3;
		rebuild_disp(nt);
		return;
	}
	/* search bar */
	if (nt->search_on) {
		if (nt->nmatch > 0 && inr(x, y, (bu_rect){ 640, 92, 100, 30 })) {
			find_next(nt, a->theme->font);
			return;
		}
		if (inr(x, y, (bu_rect){ 748, 92, 104, 30 })) {
			nt->search[0] = 0;
			rebuild_disp(nt);
			compute_matches(nt);
			return;
		}
	}
	/* folder management */
	if (nt->view > 0) {
		if (inr(x, y, (bu_rect){ FOLD_X + 6, PANES_BOT - 40, 62, 34 })) {
			nt->folder_edit = nt->view;
			snprintf(nt->folder_name, sizeof(nt->folder_name), "%s", nt->folders[nt->view]);
			nt->folder_sheet = true;
			nt->osk_target = OT_FOLDER;
			bu_osk_init(&nt->osk);
			return;
		}
		if (inr(x, y, (bu_rect){ FOLD_X + 76, PANES_BOT - 40, 64, 34 })) {
			if (!nt->confirm_fdel) {
				nt->confirm_fdel = true;
			} else {
				nt->confirm_fdel = false;
				folder_delete(nt);
			}
			return;
		}
	}
	/* folder rows */
	{
		int nf = nt->nfolders + 2;
		for (int i = 0; i < nf; i++) {
			bu_rect r = { FOLD_X + 6, top + 6 + i * 40, FOLD_W - 12, 36 };
			if (r.y + r.h > PANES_BOT - 44)
				break;
			if (inr(x, y, r)) {
				int v = i == 0 ? VIEW_ALL : i <= nt->nfolders ? i - 1 : VIEW_TRASH;
				save_current(nt);
				nt->view = v;
				nt->scroll = 0;
				nt->confirm_fdel = false;
				nt->confirm_perm = false;
				rebuild_disp(nt);
				if (v == VIEW_TRASH) {
					nt->current = -1;
					nt->tsel = -1;
					nt->len = 0;
					nt->text[0] = 0;
				}
				return;
			}
		}
	}
	/* list scroll */
	if (inr(x, y, (bu_rect){ LIST_X + 6, PANES_BOT - 40, 118, 34 })) {
		if (nt->scroll >= 3) nt->scroll -= 3; else nt->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ LIST_X + 130, PANES_BOT - 40, 118, 34 })) {
		int total = nt->view == VIEW_TRASH ? nt->ntrents : nt->ndisp;
		int visible = (PANES_BOT - top - 52) / ROW_H;
		if (nt->scroll + visible < total) nt->scroll += 3;
		return;
	}
	/* list rows */
	{
		int visible = (PANES_BOT - top - 52) / ROW_H;
		for (int vi = 0; vi < visible; vi++) {
			int i = nt->scroll + vi;
			bu_rect r = { LIST_X + 6, top + 6 + vi * ROW_H, LIST_W - 12, ROW_H - 4 };
			if (!inr(x, y, r))
				continue;
			if (nt->view == VIEW_TRASH) {
				if (i < nt->ntrents) {
					nt->confirm_perm = false;
					open_trash(nt, i);
				}
			} else if (i < nt->ndisp) {
				open_note(nt, nt->disp[i]);
			}
			return;
		}
	}
	/* editor scroll */
	bool have = nt->view == VIEW_TRASH ? nt->tsel >= 0 : nt->current >= 0;
	if (have) {
		if (inr(x, y, (bu_rect){ EDIT_X + EDIT_W - 38, top + 8, 30, 30 })) {
			if (nt->ed_scroll >= 5) nt->ed_scroll -= 5; else nt->ed_scroll = 0;
			return;
		}
		if (inr(x, y, (bu_rect){ EDIT_X + EDIT_W - 38, PANES_BOT - 46, 30, 30 })) {
			int rows = (PANES_BOT - top - 20) / 22;
			if (nt->ed_scroll + rows < nt->ndl) nt->ed_scroll += 5;
			return;
		}
		/* checklist toggle: tap a display line */
		if (nt->view != VIEW_TRASH && nt->current >= 0 &&
		    x >= EDIT_X + 8 && x < EDIT_X + EDIT_W - 44 && y >= top + 6 && y < PANES_BOT - 6) {
			build_dlines(nt, a->theme->font, EDIT_W - 64);
			int vi = (y - (top + 10)) / 22;
			int di = nt->ed_scroll + vi;
			if (di >= 0 && di < nt->ndl && nt->dl[di].first) {
				int chk;
				/* byte offset of the logical line start = dl.off when first */
				if (line_is_check(nt, nt->dl[di].logical, &chk))
					toggle_check_at(nt, nt->dl[di].off);
			}
			return;
		}
	}
	/* bottom bar */
	if (nt->view == VIEW_TRASH) {
		if (nt->tsel >= 0) {
			if (inr(x, y, (bu_rect){ EDIT_X, 442, 130, 32 })) {
				restore_trash(nt, nt->tsel);
				return;
			}
			if (inr(x, y, (bu_rect){ EDIT_X + 138, 442, 170, 32 })) {
				if (!nt->confirm_perm)
					nt->confirm_perm = true;
				else {
					nt->confirm_perm = false;
					delete_trash(nt, nt->tsel);
				}
				return;
			}
		}
		return;
	}
	if (nt->current >= 0) {
		if (inr(x, y, (bu_rect){ EDIT_X, 442, 120, 32 })) {
			if (nt->osk_target == OT_NOTE) {
				save_current(nt);
				nt->osk_target = OT_NONE;
			} else {
				nt->osk_target = OT_NOTE;
				bu_osk_init(&nt->osk);
			}
			return;
		}
		if (inr(x, y, (bu_rect){ EDIT_X + 128, 442, 88, 32 })) {
			struct nentry *e = &nt->ents[nt->current];
			e->pinned = !e->pinned;
			char rel[400];
			rel_of(nt, e, rel, sizeof(rel));
			meta_set(nt, rel, e->pinned, e->created);
			rebuild_disp(nt);
			snprintf(nt->status, sizeof(nt->status), e->pinned ? "ピン留めしました" : "ピンを外しました");
			return;
		}
		if (inr(x, y, (bu_rect){ EDIT_X + 224, 442, 80, 32 })) {
			nt->move_open = true;
			return;
		}
		if (inr(x, y, (bu_rect){ EDIT_X + 312, 442, 112, 32 })) {
			trash_note(nt, nt->current);
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct notes nt;
	memset(&nt, 0, sizeof(nt));
	nt.current = -1;
	nt.tsel = -1;
	nt.view = VIEW_ALL;
	const char *home = getenv("HOME");
	snprintf(nt.dir, sizeof(nt.dir), "%s/Notes", home ? home : "/home/user");
	char cmd[700];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s' '%s/.trash'", nt.dir, nt.dir);
	if (system(cmd) != 0)
		return 1;
	purge_trash(&nt);
	scan_all(&nt);
	rebuild_disp(&nt);
	if (nt.ndisp > 0)
		open_note(&nt, nt.disp[0]);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-notes.ppm" : NULL;
	int rc = bdapp_run("メモ", shot, draw, onevent, tick, &nt);
	save_current(&nt);
	return rc;
}
