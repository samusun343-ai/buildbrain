/* bd-tasks: reminders for braindesk (v3).
 * A to-do / reminders app following Apple Reminders:
 * multiple named, coloured lists in a sidebar; smart views
 * (Today, Scheduled, Flagged, All, Completed); one-tap
 * completion circle; due dates with overdue highlighting;
 * priority (!/!!/!!!) and flag as separate concepts;
 * subtasks indented under a parent with progress; a notes
 * field per task; repeating tasks spawn the next occurrence
 * when completed. Data is a single tab-separated file at
 * ~/.local/share/braindesk/tasks.txt, saved on every change.
 * (The v2 process-monitor UI moved out: process/PSS info is
 * covered by the brain-measure tool.)
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <time.h>
#include <ctype.h>

#define MAXTASKS 512
#define MAXLISTS 24

struct rtask {
	int id, list, parent;
	bool done, flag;
	int prio;		/* 0 none, 1 low, 2 med, 3 high */
	int due;		/* YYYYMMDD or 0 */
	int repeat;		/* 0 none, 1 daily, 2 weekly, 3 monthly, 4 yearly */
	int seq;
	char title[128];
	char notes[256];
};

struct rlist {
	int id, color;
	char name[64];
};

enum { V_TODAY = 0, V_SCHED, V_FLAG, V_ALL, V_DONE, V_LIST };
enum { OT_NONE = 0, OT_TITLE, OT_NOTES, OT_SEARCH, OT_LISTNAME };

static const bu_color LIST_COLORS[8] = {
	{ 0, 122, 255, 255 }, { 255, 59, 48, 255 }, { 255, 149, 0, 255 },
	{ 255, 204, 0, 255 }, { 52, 199, 89, 255 }, { 90, 200, 250, 255 },
	{ 191, 90, 242, 255 }, { 142, 142, 147, 255 },
};

struct tasks {
	char file[600];
	struct rtask tk[MAXTASKS];
	int ntk;
	struct rlist ls[MAXLISTS];
	int nls;
	int next_id, next_seq;
	int view;		/* V_* */
	int view_list;		/* list id when view == V_LIST */
	bool show_done;
	int disp[MAXTASKS];
	int dindent[MAXTASKS];
	int ndisp, scroll;
	char search[64];
	bool search_on;
	/* editor sheet */
	bool sheet;
	int edit_id;		/* task id being edited, -1 = new */
	int e_list, e_parent, e_prio, e_repeat, e_due;
	bool e_flag;
	char e_title[128], e_notes[256];
	bool confirm_del;
	/* list sheet */
	bool lsheet;
	int l_edit;		/* list id being edited, -1 = new */
	int l_color;
	char l_name[64];
	bool confirm_ldel;
	bu_osk osk;
	int osk_target;
	char status[96];
};

/* ---------- helpers ---------- */

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

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

static void buf_backspace(char *buf)
{
	int l = (int)strlen(buf);
	while (l > 0 && (buf[l - 1] & 0xC0) == 0x80)
		l--;
	if (l > 0)
		buf[l - 1] = 0;
}

static void sanitise(char *s)
{
	for (char *p = s; *p; p++)
		if (*p == '\t' || *p == '\n' || *p == '\r')
			*p = ' ';
}

/* ---------- dates (YYYYMMDD ints) ---------- */

static int date_today(void)
{
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

static int date_add(int ymd, int days, int months, int years)
{
	if (!ymd)
		ymd = date_today();
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = ymd / 10000 - 1900;
	tm.tm_mon = (ymd / 100) % 100 - 1;
	tm.tm_mday = ymd % 100;
	tm.tm_hour = 12;
	tm.tm_mday += days;
	tm.tm_mon += months;
	tm.tm_year += years;
	mktime(&tm); /* normalises overflow */
	return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

static void date_fmt(int ymd, char *out, size_t n)
{
	if (!ymd) {
		snprintf(out, n, "なし");
		return;
	}
	int today = date_today();
	if (ymd == today)
		snprintf(out, n, "今日 %d/%d", (ymd / 100) % 100, ymd % 100);
	else if (ymd == date_add(today, 1, 0, 0))
		snprintf(out, n, "明日 %d/%d", (ymd / 100) % 100, ymd % 100);
	else
		snprintf(out, n, "%d/%d/%d", ymd / 10000, (ymd / 100) % 100, ymd % 100);
}

/* ---------- storage ---------- */

static void save_all(struct tasks *g)
{
	FILE *fp = fopen(g->file, "w");
	if (!fp)
		return;
	fprintf(fp, "# bd-tasks v1\n");
	for (int i = 0; i < g->nls; i++)
		fprintf(fp, "L %d %d %s\n", g->ls[i].id, g->ls[i].color, g->ls[i].name);
	for (int i = 0; i < g->ntk; i++) {
		struct rtask *t = &g->tk[i];
		fprintf(fp, "T %d %d %d %d %d %d %d %d %d %s\t%s\n",
			t->id, t->list, t->parent, t->done ? 1 : 0, t->prio,
			t->flag ? 1 : 0, t->due, t->repeat, t->seq, t->title, t->notes);
	}
	fclose(fp);
}

static void load_all(struct tasks *g)
{
	g->ntk = 0;
	g->nls = 0;
	g->next_id = 1;
	g->next_seq = 1;
	FILE *fp = fopen(g->file, "r");
	if (fp) {
		char line[768];
		while (fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\n")] = 0;
			if (line[0] == 'L' && line[1] == ' ' && g->nls < MAXLISTS) {
				struct rlist *l = &g->ls[g->nls];
				memset(l, 0, sizeof(*l));
				char *p = line + 2;
				l->id = (int)strtol(p, &p, 10);
				l->color = (int)strtol(p, &p, 10);
				if (l->color < 0 || l->color > 7)
					l->color = 0;
				while (*p == ' ')
					p++;
				snprintf(l->name, sizeof(l->name), "%s", p);
				if (l->id >= g->next_id)
					g->next_id = l->id + 1;
				g->nls++;
			} else if (line[0] == 'T' && line[1] == ' ' && g->ntk < MAXTASKS) {
				struct rtask *t = &g->tk[g->ntk];
				memset(t, 0, sizeof(*t));
				char *p = line + 2;
				long v[9];
				for (int k = 0; k < 9; k++)
					v[k] = strtol(p, &p, 10);
				t->id = (int)v[0];
				t->list = (int)v[1];
				t->parent = (int)v[2];
				t->done = v[3] != 0;
				t->prio = (int)v[4];
				t->flag = v[5] != 0;
				t->due = (int)v[6];
				t->repeat = (int)v[7];
				t->seq = (int)v[8];
				while (*p == ' ')
					p++;
				char *tab = strchr(p, '\t');
				if (tab) {
					*tab = 0;
					snprintf(t->notes, sizeof(t->notes), "%s", tab + 1);
				}
				snprintf(t->title, sizeof(t->title), "%s", p);
				if (t->id >= g->next_id)
					g->next_id = t->id + 1;
				if (t->seq >= g->next_seq)
					g->next_seq = t->seq + 1;
				g->ntk++;
			}
		}
		fclose(fp);
	}
	if (g->nls == 0) {
		struct rlist *l = &g->ls[g->nls++];
		memset(l, 0, sizeof(*l));
		l->id = g->next_id++;
		l->color = 0;
		snprintf(l->name, sizeof(l->name), "リマインダー");
		save_all(g);
	}
}

static struct rtask *find_task(struct tasks *g, int id)
{
	for (int i = 0; i < g->ntk; i++)
		if (g->tk[i].id == id)
			return &g->tk[i];
	return NULL;
}

static struct rlist *find_list(struct tasks *g, int id)
{
	for (int i = 0; i < g->nls; i++)
		if (g->ls[i].id == id)
			return &g->ls[i];
	return NULL;
}

static int children_count(struct tasks *g, int id, int *done_out)
{
	int n = 0, dn = 0;
	for (int i = 0; i < g->ntk; i++)
		if (g->tk[i].parent == id) {
			n++;
			if (g->tk[i].done)
				dn++;
		}
	if (done_out)
		*done_out = dn;
	return n;
}

/* ---------- display list ---------- */

static bool task_visible(struct tasks *g, struct rtask *t)
{
	int today = date_today();
	if (g->search_on && g->search[0]) {
		return ci_strstr(t->title, g->search) || ci_strstr(t->notes, g->search);
	}
	switch (g->view) {
	case V_TODAY:
		if (t->done)
			return g->show_done;
		return t->due != 0 && t->due <= today;
	case V_SCHED:
		if (t->done)
			return g->show_done;
		return t->due != 0;
	case V_FLAG:
		if (t->done)
			return g->show_done;
		return t->flag;
	case V_ALL:
		return !t->done || g->show_done;
	case V_DONE:
		return t->done;
	case V_LIST:
		if (t->list != g->view_list)
			return false;
		return !t->done || g->show_done;
	}
	return false;
}

static int task_cmp(struct tasks *g, struct rtask *a, struct rtask *b)
{
	if (a->done != b->done)
		return a->done ? 1 : -1;
	if (g->view == V_DONE)
		return b->seq - a->seq;
	/* undated last, earlier due first */
	if ((a->due == 0) != (b->due == 0))
		return a->due == 0 ? 1 : -1;
	if (a->due != b->due)
		return a->due - b->due;
	if (a->prio != b->prio)
		return b->prio - a->prio;
	return a->seq - b->seq;
}

static void emit_task(struct tasks *g, int idx, int indent)
{
	if (g->ndisp >= MAXTASKS)
		return;
	g->disp[g->ndisp] = idx;
	g->dindent[g->ndisp] = indent;
	g->ndisp++;
}

static void emit_children(struct tasks *g, int parent_id, int indent)
{
	/* collect children, sort, emit recursively (depth <= 3) */
	int kids[MAXTASKS];
	int nk = 0;
	for (int i = 0; i < g->ntk; i++)
		if (g->tk[i].parent == parent_id && task_visible(g, &g->tk[i]))
			kids[nk++] = i;
	for (int i = 0; i < nk; i++)
		for (int j = i + 1; j < nk; j++)
			if (task_cmp(g, &g->tk[kids[j]], &g->tk[kids[i]]) < 0) {
				int tmp = kids[i];
				kids[i] = kids[j];
				kids[j] = tmp;
			}
	for (int i = 0; i < nk; i++) {
		emit_task(g, kids[i], indent);
		if (indent < 3)
			emit_children(g, g->tk[kids[i]].id, indent + 1);
	}
}

static void rebuild_disp(struct tasks *g)
{
	g->ndisp = 0;
	bool hierarchical = g->view == V_LIST && !(g->search_on && g->search[0]);
	if (hierarchical) {
		int roots[MAXTASKS];
		int nr = 0;
		for (int i = 0; i < g->ntk; i++) {
			struct rtask *t = &g->tk[i];
			if (!task_visible(g, t))
				continue;
			/* root = no parent, or parent outside this list */
			struct rtask *p = t->parent ? find_task(g, t->parent) : NULL;
			if (t->parent && p && p->list == g->view_list)
				continue;
			roots[nr++] = i;
		}
		for (int i = 0; i < nr; i++)
			for (int j = i + 1; j < nr; j++)
				if (task_cmp(g, &g->tk[roots[j]], &g->tk[roots[i]]) < 0) {
					int tmp = roots[i];
					roots[i] = roots[j];
					roots[j] = tmp;
				}
		for (int i = 0; i < nr; i++) {
			emit_task(g, roots[i], 0);
			emit_children(g, g->tk[roots[i]].id, 1);
		}
	} else {
		int flat[MAXTASKS];
		int nf = 0;
		for (int i = 0; i < g->ntk; i++)
			if (task_visible(g, &g->tk[i]))
				flat[nf++] = i;
		for (int i = 0; i < nf; i++)
			for (int j = i + 1; j < nf; j++)
				if (task_cmp(g, &g->tk[flat[j]], &g->tk[flat[i]]) < 0) {
					int tmp = flat[i];
					flat[i] = flat[j];
					flat[j] = tmp;
				}
		for (int i = 0; i < nf; i++)
			emit_task(g, flat[i], 0);
	}
	if (g->scroll > g->ndisp)
		g->scroll = g->ndisp > 0 ? g->ndisp - 1 : 0;
}

static int view_count(struct tasks *g, int view, int list_id)
{
	int save_view = g->view, save_list = g->view_list;
	bool save_search = g->search_on;
	g->view = view;
	g->view_list = list_id;
	g->search_on = false;
	int n = 0;
	for (int i = 0; i < g->ntk; i++)
		if (task_visible(g, &g->tk[i]))
			n++;
	g->view = save_view;
	g->view_list = save_list;
	g->search_on = save_search;
	return n;
}

/* ---------- mutations ---------- */

static void complete_task(struct tasks *g, struct rtask *t)
{
	t->done = !t->done;
	if (t->done && t->repeat != 0 && g->ntk < MAXTASKS) {
		/* spawn the next occurrence */
		struct rtask nt2 = *t;
		nt2.id = g->next_id++;
		nt2.seq = g->next_seq++;
		nt2.done = false;
		nt2.parent = t->parent;
		int base = t->due ? t->due : date_today();
		switch (t->repeat) {
		case 1: nt2.due = date_add(base, 1, 0, 0); break;
		case 2: nt2.due = date_add(base, 7, 0, 0); break;
		case 3: nt2.due = date_add(base, 0, 1, 0); break;
		case 4: nt2.due = date_add(base, 0, 0, 1); break;
		}
		g->tk[g->ntk++] = nt2;
		snprintf(g->status, sizeof(g->status), "完了 · 次回 %d/%d を作成",
			 (nt2.due / 100) % 100, nt2.due % 100);
	} else {
		snprintf(g->status, sizeof(g->status), t->done ? "完了しました" : "未完了に戻しました");
	}
	save_all(g);
	rebuild_disp(g);
}

static void delete_task(struct tasks *g, int id)
{
	/* children become top-level */
	for (int i = 0; i < g->ntk; i++)
		if (g->tk[i].parent == id)
			g->tk[i].parent = 0;
	int w = 0;
	for (int i = 0; i < g->ntk; i++)
		if (g->tk[i].id != id)
			g->tk[w++] = g->tk[i];
	g->ntk = w;
	save_all(g);
	rebuild_disp(g);
}

static void delete_list(struct tasks *g, int id)
{
	int w = 0;
	for (int i = 0; i < g->ntk; i++)
		if (g->tk[i].list != id)
			g->tk[w++] = g->tk[i];
	g->ntk = w;
	w = 0;
	for (int i = 0; i < g->nls; i++)
		if (g->ls[i].id != id)
			g->ls[w++] = g->ls[i];
	g->nls = w;
	if (g->view == V_LIST && g->view_list == id)
		g->view = V_ALL;
	save_all(g);
	rebuild_disp(g);
}

/* ---------- layout ---------- */

#define TB_Y 48
#define SIDE_X 12
#define SIDE_W 200
#define MAIN_X 224
#define MAIN_W 628
#define PANE_TOP 94
#define PANE_BOT 434
#define ROW_H 54

static const char *view_title(struct tasks *g)
{
	if (g->search_on && g->search[0])
		return "検索結果";
	switch (g->view) {
	case V_TODAY: return "今日";
	case V_SCHED: return "予定あり";
	case V_FLAG: return "フラグ付き";
	case V_ALL: return "すべて";
	case V_DONE: return "完了済み";
	case V_LIST: {
		struct rlist *l = find_list(g, g->view_list);
		return l ? l->name : "リスト";
	}
	}
	return "";
}

/* ---------- draw ---------- */

static void draw_circle_check(bu_canvas *c, bu_theme *t, int cx, int cy, bool done)
{
	bu_fill_circle(c, cx, cy, 11, done ? t->accent : t->text_dim);
	bu_fill_circle(c, cx, cy, 9, t->surface);
	if (done) {
		bu_fill_circle(c, cx, cy, 8, t->accent);
		bu_line(c, cx - 4, cy, cx - 1, cy + 3, 2, t->surface);
		bu_line(c, cx - 1, cy + 3, cx + 5, cy - 3, 2, t->surface);
	}
}

static void draw_flag(bu_canvas *c, int x, int y, bu_color col)
{
	bu_line(c, x, y, x, y + 12, 2, col);
	bu_fill_rect(c, (bu_rect){ x + 1, y, 8, 5 }, col);
}

static void draw_sheet(struct tasks *g, bdapp *a);

static void draw(bdapp *a, void *ctx)
{
	struct tasks *g = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* toolbar */
	bu_button(c, t, (bu_rect){ 12, TB_Y, 130, 38 }, "＋ タスク", BU_BTN_PRIMARY, false);
	bu_button(c, t, (bu_rect){ 150, TB_Y, 96, 38 }, "検索", g->search_on ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	if (g->view != V_DONE)
		bu_button(c, t, (bu_rect){ 254, TB_Y, 170, 38 }, g->show_done ? "完了済み: 表示中" : "完了済み: 隠す", BU_BTN_NORMAL, false);
	if (g->osk_target != OT_NONE)
		bu_button(c, t, (bu_rect){ 748, TB_Y, 104, 38 }, "閉じる", BU_BTN_NORMAL, false);
	if (g->search_on) {
		char sb[128];
		snprintf(sb, sizeof(sb), "検索「%s」: %d 件", g->search, g->ndisp);
		bu_draw_text(c, t->font, 440, TB_Y + 9, sb, 12.5, t->accent);
	} else if (g->status[0]) {
		bu_draw_text_right(c, t->font, (bu_rect){ 440, TB_Y + 9, 300, 20 }, g->status, 12, t->text_dim);
	}
	/* sidebar */
	bu_rect sp = { SIDE_X, PANE_TOP, SIDE_W, PANE_BOT - PANE_TOP };
	bu_fill_round_rect(c, sp, 10, t->surface);
	struct { int view; const char *label; } smart[5] = {
		{ V_TODAY, "今日" }, { V_SCHED, "予定あり" }, { V_FLAG, "フラグ付き" },
		{ V_ALL, "すべて" }, { V_DONE, "完了済み" },
	};
	for (int i = 0; i < 5; i++) {
		bu_rect r = { sp.x + 6, sp.y + 6 + i * 38, sp.w - 12, 34 };
		if (g->view == smart[i].view && !(g->search_on && g->search[0]))
			bu_fill_round_rect(c, r, 7, t->surface3);
		bu_draw_text(c, t->font, r.x + 10, r.y + 8, smart[i].label, 13, t->text);
		char cb[16];
		snprintf(cb, sizeof(cb), "%d", view_count(g, smart[i].view, 0));
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 8, r.w - 8, 18 }, cb, 11.5, t->text_faint);
	}
	int ly = sp.y + 6 + 5 * 38 + 8;
	bu_fill_rect(c, (bu_rect){ sp.x + 12, ly, sp.w - 24, 1 }, t->line);
	bu_draw_text(c, t->font, sp.x + 12, ly + 6, "マイリスト", 11.5, t->text_faint);
	ly += 28;
	for (int i = 0; i < g->nls; i++) {
		bu_rect r = { sp.x + 6, ly + i * 38, sp.w - 12, 34 };
		if (r.y + r.h > PANE_BOT - 46)
			break;
		if (g->view == V_LIST && g->view_list == g->ls[i].id && !(g->search_on && g->search[0]))
			bu_fill_round_rect(c, r, 7, t->surface3);
		bu_fill_circle(c, r.x + 14, r.y + 17, 6, LIST_COLORS[g->ls[i].color & 7]);
		bu_draw_text(c, t->font, r.x + 28, r.y + 8, g->ls[i].name, 13, t->text);
		char cb[16];
		snprintf(cb, sizeof(cb), "%d", view_count(g, V_LIST, g->ls[i].id));
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 8, r.w - 8, 18 }, cb, 11.5, t->text_faint);
	}
	bu_button(c, t, (bu_rect){ sp.x + 6, PANE_BOT - 40, 92, 34 }, "＋ リスト", BU_BTN_NORMAL, false);
	if (g->view == V_LIST)
		bu_button(c, t, (bu_rect){ sp.x + 104, PANE_BOT - 40, 90, 34 }, "リスト編集", BU_BTN_NORMAL, false);
	/* main header */
	bu_draw_text(c, t->font, MAIN_X + 4, PANE_TOP + 2, view_title(g), 16, t->text);
	char cnt[32];
	snprintf(cnt, sizeof(cnt), "%d 件", g->ndisp);
	bu_draw_text_right(c, t->font, (bu_rect){ MAIN_X, PANE_TOP + 4, MAIN_W - 8, 20 }, cnt, 12, t->text_faint);
	/* rows */
	int rows_top = PANE_TOP + 34;
	int visible = (PANE_BOT - rows_top - 6) / ROW_H;
	int today = date_today();
	for (int vi = 0; vi < visible; vi++) {
		int di = g->scroll + vi;
		if (di >= g->ndisp)
			break;
		struct rtask *tk = &g->tk[g->disp[di]];
		int indent = g->dindent[di];
		bu_rect r = { MAIN_X + indent * 26, rows_top + vi * ROW_H, MAIN_W - indent * 26 - 40, ROW_H - 5 };
		bu_fill_round_rect(c, r, 8, t->surface);
		draw_circle_check(c, t, r.x + 20, r.y + 24, tk->done);
		int tx = r.x + 40;
		bu_draw_text(c, t->font, tx, r.y + 5, tk->title, 13.5, tk->done ? t->text_faint : t->text);
		if (tk->done) {
			int w = bu_text_width(t->font, tk->title, 13.5);
			if (w > r.w - 60)
				w = r.w - 60;
			bu_line(c, tx, r.y + 14, tx + w, r.y + 14, 1, t->text_faint);
		}
		/* sub-line */
		char sub[256];
		sub[0] = 0;
		int so = 0;
		if (tk->due) {
			char df[48];
			date_fmt(tk->due, df, sizeof(df));
			so += snprintf(sub + so, sizeof(sub) - so, "%s%s", so ? " · " : "", df);
		}
		if (tk->repeat)
			so += snprintf(sub + so, sizeof(sub) - so, "%s繰返し", so ? " · " : "");
		int dn = 0;
		int nc = children_count(g, tk->id, &dn);
		if (nc > 0)
			so += snprintf(sub + so, sizeof(sub) - so, "%sサブタスク %d/%d", so ? " · " : "", dn, nc);
		if (g->view != V_LIST || (g->search_on && g->search[0])) {
			struct rlist *l = find_list(g, tk->list);
			if (l)
				so += snprintf(sub + so, sizeof(sub) - so, "%s%s", so ? " · " : "", l->name);
		}
		if (tk->notes[0]) {
			char np[64];
			snprintf(np, sizeof(np), "%.*s", 24, tk->notes);
			so += snprintf(sub + so, sizeof(sub) - so, "%s%s", so ? " · " : "", np);
		}
		bu_color subcol = t->text_faint;
		if (tk->due && !tk->done && tk->due < today)
			subcol = t->danger;
		else if (tk->due == today && !tk->done)
			subcol = t->accent;
		bu_draw_text(c, t->font, tx, r.y + 27, sub, 11.5, subcol);
		/* priority + flag at the right */
		int rx = r.x + r.w - 12;
		if (tk->flag) {
			draw_flag(c, rx - 10, r.y + 8, BU_RGB(255, 149, 0));
			rx -= 22;
		}
		if (tk->prio > 0) {
			char pb[8] = "!!!";
			pb[tk->prio] = 0;
			int pw = bu_text_width(t->font, pb, 14);
			bu_draw_text(c, t->font, rx - pw, r.y + 6, pb, 14, tk->prio >= 3 ? t->danger : t->warn);
		}
	}
	if (g->ndisp == 0)
		bu_draw_text(c, t->font, MAIN_X + 12, rows_top + 14,
			     g->search_on && g->search[0] ? "一致するタスクがありません" : "タスクがありません", 13, t->text_dim);
	bu_button(c, t, (bu_rect){ MAIN_X + MAIN_W - 34, rows_top, 30, 44 }, "▲", BU_BTN_GHOST, false);
	bu_button(c, t, (bu_rect){ MAIN_X + MAIN_W - 34, PANE_BOT - 50, 30, 44 }, "▼", BU_BTN_GHOST, false);
	if (g->sheet || g->lsheet)
		draw_sheet(g, a);
	if (g->osk_target != OT_NONE) {
		int oskh = bu_osk_height();
		bu_osk_draw(c, t, &g->osk, (bu_rect){ 0, a->h - oskh, a->w, oskh });
	}
}

/* ---------- editor sheets ---------- */

#define SHEET_X 172
#define SHEET_Y 64
#define SHEET_W 520
#define SHEET_H 352

static void field_row(bu_canvas *c, bu_theme *t, int y, const char *label, const char *value, bool focused)
{
	bu_draw_text(c, t->font, SHEET_X + 20, y + 8, label, 13, t->text_dim);
	bu_rect f = { SHEET_X + 100, y, SHEET_W - 120, 36 };
	bu_fill_round_rect(c, f, 8, focused ? t->surface3 : t->surface);
	bu_draw_text(c, t->font, f.x + 10, f.y + 8, value[0] ? value : "（未入力）", 13.5,
		     value[0] ? t->text : t->text_faint);
}

static void draw_sheet(struct tasks *g, bdapp *a)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bu_rect sheet = { SHEET_X, SHEET_Y, SHEET_W, SHEET_H };
	bu_shadow_round_rect(c, sheet, 12, 14, BU_RGBA(0, 0, 0, 100));
	bu_fill_round_rect(c, sheet, 12, t->surface2);
	if (g->lsheet) {
		bu_draw_text(c, t->font, SHEET_X + 20, SHEET_Y + 14, g->l_edit < 0 ? "新規リスト" : "リスト編集", 15, t->text);
		field_row(c, t, SHEET_Y + 52, "名前", g->l_name, g->osk_target == OT_LISTNAME);
		bu_draw_text(c, t->font, SHEET_X + 20, SHEET_Y + 104, "色", 13, t->text_dim);
		for (int i = 0; i < 8; i++) {
			int cx = SHEET_X + 116 + i * 44;
			bu_fill_circle(c, cx, SHEET_Y + 118, i == g->l_color ? 13 : 10, LIST_COLORS[i]);
			if (i == g->l_color)
				bu_line(c, cx - 4, SHEET_Y + 118, cx - 1, SHEET_Y + 121, 2, t->surface);
		}
		if (g->l_edit >= 0 && g->nls > 1)
			bu_button(c, t, (bu_rect){ SHEET_X + 20, SHEET_Y + SHEET_H - 52, 130, 38 },
				  g->confirm_ldel ? "本当に削除" : "リスト削除", BU_BTN_DANGER, false);
		bu_button(c, t, (bu_rect){ SHEET_X + SHEET_W - 300, SHEET_Y + SHEET_H - 52, 130, 38 }, "キャンセル", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ SHEET_X + SHEET_W - 156, SHEET_Y + SHEET_H - 52, 136, 38 }, "保存", BU_BTN_PRIMARY, false);
		return;
	}
	bu_draw_text(c, t->font, SHEET_X + 20, SHEET_Y + 12, g->edit_id < 0 ? "新規タスク" : "タスク編集", 15, t->text);
	field_row(c, t, SHEET_Y + 46, "タイトル", g->e_title, g->osk_target == OT_TITLE);
	field_row(c, t, SHEET_Y + 88, "メモ", g->e_notes, g->osk_target == OT_NOTES);
	/* due row */
	int dy = SHEET_Y + 130;
	bu_draw_text(c, t->font, SHEET_X + 20, dy + 8, "期限", 13, t->text_dim);
	char df[48];
	date_fmt(g->e_due, df, sizeof(df));
	bu_button(c, t, (bu_rect){ SHEET_X + 100, dy, 118, 36 }, df, BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ SHEET_X + 224, dy, 62, 36 }, "-1日", BU_BTN_GHOST, false);
	bu_button(c, t, (bu_rect){ SHEET_X + 292, dy, 62, 36 }, "+1日", BU_BTN_GHOST, false);
	bu_button(c, t, (bu_rect){ SHEET_X + 360, dy, 62, 36 }, "+1月", BU_BTN_GHOST, false);
	bu_button(c, t, (bu_rect){ SHEET_X + 428, dy, 62, 36 }, "なし", BU_BTN_GHOST, false);
	/* priority row */
	int py = SHEET_Y + 172;
	bu_draw_text(c, t->font, SHEET_X + 20, py + 8, "優先度", 13, t->text_dim);
	const char *pr[4] = { "なし", "低 !", "中 !!", "高 !!!" };
	for (int i = 0; i < 4; i++)
		bu_button(c, t, (bu_rect){ SHEET_X + 100 + i * 100, py, 94, 36 }, pr[i],
			  g->e_prio == i ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	/* flag + repeat row */
	int fy = SHEET_Y + 214;
	bu_draw_text(c, t->font, SHEET_X + 20, fy + 8, "フラグ", 13, t->text_dim);
	bu_button(c, t, (bu_rect){ SHEET_X + 100, fy, 94, 36 }, g->e_flag ? "オン" : "オフ",
		  g->e_flag ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	bu_draw_text(c, t->font, SHEET_X + 216, fy + 8, "繰り返し", 13, t->text_dim);
	const char *rp[5] = { "なし", "毎日", "毎週", "毎月", "毎年" };
	bu_button(c, t, (bu_rect){ SHEET_X + 300, fy, 100, 36 }, rp[g->e_repeat], BU_BTN_NORMAL, false);
	/* list + parent row */
	int ly2 = SHEET_Y + 256;
	bu_draw_text(c, t->font, SHEET_X + 20, ly2 + 8, "リスト", 13, t->text_dim);
	struct rlist *el = find_list(g, g->e_list);
	bu_button(c, t, (bu_rect){ SHEET_X + 100, ly2, 150, 36 }, el ? el->name : "-", BU_BTN_NORMAL, false);
	bu_draw_text(c, t->font, SHEET_X + 272, ly2 + 8, "親", 13, t->text_dim);
	struct rtask *ep = g->e_parent ? find_task(g, g->e_parent) : NULL;
	bu_button(c, t, (bu_rect){ SHEET_X + 310, ly2, 180, 36 }, ep ? ep->title : "なし", BU_BTN_NORMAL, false);
	/* buttons */
	if (g->edit_id >= 0)
		bu_button(c, t, (bu_rect){ SHEET_X + 20, SHEET_Y + SHEET_H - 52, 110, 38 },
			  g->confirm_del ? "本当に削除" : "削除", BU_BTN_DANGER, false);
	bu_button(c, t, (bu_rect){ SHEET_X + SHEET_W - 300, SHEET_Y + SHEET_H - 52, 130, 38 }, "キャンセル", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ SHEET_X + SHEET_W - 156, SHEET_Y + SHEET_H - 52, 136, 38 }, "保存", BU_BTN_PRIMARY, false);
}

/* ---------- sheet logic ---------- */

static void sheet_open_new(struct tasks *g, int parent)
{
	g->sheet = true;
	g->edit_id = -1;
	g->confirm_del = false;
	g->e_title[0] = 0;
	g->e_notes[0] = 0;
	g->e_prio = 0;
	g->e_flag = false;
	g->e_repeat = 0;
	g->e_due = g->view == V_TODAY || g->view == V_SCHED ? date_today() : 0;
	g->e_list = g->view == V_LIST ? g->view_list : (g->nls > 0 ? g->ls[0].id : 0);
	g->e_parent = parent;
	if (parent) {
		struct rtask *p = find_task(g, parent);
		if (p)
			g->e_list = p->list;
	}
	g->osk_target = OT_TITLE;
	bu_osk_init(&g->osk);
}

static void sheet_open_edit(struct tasks *g, struct rtask *t)
{
	g->sheet = true;
	g->edit_id = t->id;
	g->confirm_del = false;
	snprintf(g->e_title, sizeof(g->e_title), "%s", t->title);
	snprintf(g->e_notes, sizeof(g->e_notes), "%s", t->notes);
	g->e_prio = t->prio;
	g->e_flag = t->flag;
	g->e_repeat = t->repeat;
	g->e_due = t->due;
	g->e_list = t->list;
	g->e_parent = t->parent;
	g->osk_target = OT_NONE;
}

static void sheet_save(struct tasks *g)
{
	sanitise(g->e_title);
	sanitise(g->e_notes);
	if (!g->e_title[0]) {
		snprintf(g->status, sizeof(g->status), "タイトルが空です");
		return;
	}
	if (g->edit_id < 0) {
		if (g->ntk >= MAXTASKS)
			return;
		struct rtask *t = &g->tk[g->ntk++];
		memset(t, 0, sizeof(*t));
		t->id = g->next_id++;
		t->seq = g->next_seq++;
		t->list = g->e_list;
		t->parent = g->e_parent;
		t->prio = g->e_prio;
		t->flag = g->e_flag;
		t->repeat = g->e_repeat;
		t->due = g->e_due;
		snprintf(t->title, sizeof(t->title), "%s", g->e_title);
		snprintf(t->notes, sizeof(t->notes), "%s", g->e_notes);
		snprintf(g->status, sizeof(g->status), "タスクを追加しました");
	} else {
		struct rtask *t = find_task(g, g->edit_id);
		if (t) {
			t->list = g->e_list;
			t->parent = g->e_parent;
			t->prio = g->e_prio;
			t->flag = g->e_flag;
			t->repeat = g->e_repeat;
			t->due = g->e_due;
			snprintf(t->title, sizeof(t->title), "%s", g->e_title);
			snprintf(t->notes, sizeof(t->notes), "%s", g->e_notes);
		}
		snprintf(g->status, sizeof(g->status), "保存しました");
	}
	g->sheet = false;
	g->osk_target = OT_NONE;
	save_all(g);
	rebuild_disp(g);
}

static void cycle_parent(struct tasks *g)
{
	/* candidates: tasks in e_list, excluding self and descendants */
	int cand[MAXTASKS + 1];
	int nc = 0;
	cand[nc++] = 0;
	for (int i = 0; i < g->ntk; i++) {
		struct rtask *t = &g->tk[i];
		if (t->list != g->e_list || t->id == g->edit_id)
			continue;
		/* exclude descendants of the edited task */
		bool desc = false;
		int p = t->parent;
		while (p) {
			if (p == g->edit_id) {
				desc = true;
				break;
			}
			struct rtask *pt = find_task(g, p);
			p = pt ? pt->parent : 0;
		}
		if (!desc)
			cand[nc++] = t->id;
	}
	for (int i = 0; i < nc; i++)
		if (cand[i] == g->e_parent) {
			g->e_parent = cand[(i + 1) % nc];
			return;
		}
	g->e_parent = 0;
}

static void cycle_list(struct tasks *g)
{
	if (g->nls == 0)
		return;
	for (int i = 0; i < g->nls; i++)
		if (g->ls[i].id == g->e_list) {
			g->e_list = g->ls[(i + 1) % g->nls].id;
			if (g->e_parent) {
				struct rtask *p = find_task(g, g->e_parent);
				if (!p || p->list != g->e_list)
					g->e_parent = 0;
			}
			return;
		}
	g->e_list = g->ls[0].id;
}

/* ---------- events ---------- */

static char *osk_buffer(struct tasks *g)
{
	switch (g->osk_target) {
	case OT_TITLE: return g->e_title;
	case OT_NOTES: return g->e_notes;
	case OT_SEARCH: return g->search;
	case OT_LISTNAME: return g->l_name;
	}
	return NULL;
}

static size_t osk_buffer_size(struct tasks *g)
{
	switch (g->osk_target) {
	case OT_TITLE: return sizeof(g->e_title);
	case OT_NOTES: return sizeof(g->e_notes);
	case OT_SEARCH: return sizeof(g->search);
	case OT_LISTNAME: return sizeof(g->l_name);
	}
	return 0;
}

static void osk_feed(struct tasks *g, int r, char *out)
{
	char *buf = osk_buffer(g);
	if (!buf)
		return;
	if (r == OSK_TEXT && out[0]) {
		if (strlen(buf) + strlen(out) < osk_buffer_size(g) - 1) {
			strcat(buf, out);
			if (g->osk_target == OT_SEARCH)
				rebuild_disp(g);
		}
	} else if (r == OSK_BACKSPACE) {
		if (g->osk.pending[0])
			g->osk.pending[strlen(g->osk.pending) - 1] = 0;
		else {
			buf_backspace(buf);
			if (g->osk_target == OT_SEARCH)
				rebuild_disp(g);
		}
	} else if (r == OSK_ENTER) {
		if (g->osk_target == OT_SEARCH)
			g->osk_target = OT_NONE;
		else if (g->osk_target == OT_TITLE)
			g->osk_target = OT_NOTES;
		else
			g->osk_target = OT_NONE;
	}
}

static bool sheet_event(struct tasks *g, int x, int y)
{
	if (g->lsheet) {
		if (inr(x, y, (bu_rect){ SHEET_X + 100, SHEET_Y + 52, SHEET_W - 120, 36 })) {
			g->osk_target = OT_LISTNAME;
			bu_osk_init(&g->osk);
			return true;
		}
		for (int i = 0; i < 8; i++) {
			int cx = SHEET_X + 116 + i * 44;
			if (x >= cx - 16 && x < cx + 16 && y >= SHEET_Y + 100 && y < SHEET_Y + 136) {
				g->l_color = i;
				return true;
			}
		}
		if (g->l_edit >= 0 && g->nls > 1 &&
		    inr(x, y, (bu_rect){ SHEET_X + 20, SHEET_Y + SHEET_H - 52, 130, 38 })) {
			if (!g->confirm_ldel) {
				g->confirm_ldel = true;
			} else {
				delete_list(g, g->l_edit);
				g->lsheet = false;
				g->osk_target = OT_NONE;
				snprintf(g->status, sizeof(g->status), "リストを削除しました");
			}
			return true;
		}
		if (inr(x, y, (bu_rect){ SHEET_X + SHEET_W - 300, SHEET_Y + SHEET_H - 52, 130, 38 })) {
			g->lsheet = false;
			g->osk_target = OT_NONE;
			return true;
		}
		if (inr(x, y, (bu_rect){ SHEET_X + SHEET_W - 156, SHEET_Y + SHEET_H - 52, 136, 38 })) {
			sanitise(g->l_name);
			if (!g->l_name[0]) {
				snprintf(g->status, sizeof(g->status), "リスト名が空です");
				return true;
			}
			if (g->l_edit < 0) {
				if (g->nls < MAXLISTS) {
					struct rlist *l = &g->ls[g->nls++];
					memset(l, 0, sizeof(*l));
					l->id = g->next_id++;
					l->color = g->l_color;
					snprintf(l->name, sizeof(l->name), "%s", g->l_name);
					g->view = V_LIST;
					g->view_list = l->id;
				}
			} else {
				struct rlist *l = find_list(g, g->l_edit);
				if (l) {
					l->color = g->l_color;
					snprintf(l->name, sizeof(l->name), "%s", g->l_name);
				}
			}
			g->lsheet = false;
			g->osk_target = OT_NONE;
			save_all(g);
			rebuild_disp(g);
			return true;
		}
		return true; /* modal: swallow other taps */
	}
	/* task sheet */
	if (inr(x, y, (bu_rect){ SHEET_X + 100, SHEET_Y + 46, SHEET_W - 120, 36 })) {
		g->osk_target = OT_TITLE;
		bu_osk_init(&g->osk);
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + 100, SHEET_Y + 88, SHEET_W - 120, 36 })) {
		g->osk_target = OT_NOTES;
		bu_osk_init(&g->osk);
		return true;
	}
	int dy = SHEET_Y + 130;
	if (inr(x, y, (bu_rect){ SHEET_X + 100, dy, 118, 36 })) {
		g->e_due = g->e_due ? date_add(g->e_due, 1, 0, 0) : date_today();
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + 224, dy, 62, 36 })) {
		g->e_due = date_add(g->e_due, -1, 0, 0);
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + 292, dy, 62, 36 })) {
		g->e_due = date_add(g->e_due, 1, 0, 0);
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + 360, dy, 62, 36 })) {
		g->e_due = date_add(g->e_due, 0, 1, 0);
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + 428, dy, 62, 36 })) {
		g->e_due = 0;
		return true;
	}
	int py = SHEET_Y + 172;
	for (int i = 0; i < 4; i++)
		if (inr(x, y, (bu_rect){ SHEET_X + 100 + i * 100, py, 94, 36 })) {
			g->e_prio = i;
			return true;
		}
	int fy = SHEET_Y + 214;
	if (inr(x, y, (bu_rect){ SHEET_X + 100, fy, 94, 36 })) {
		g->e_flag = !g->e_flag;
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + 300, fy, 100, 36 })) {
		g->e_repeat = (g->e_repeat + 1) % 5;
		return true;
	}
	int ly2 = SHEET_Y + 256;
	if (inr(x, y, (bu_rect){ SHEET_X + 100, ly2, 150, 36 })) {
		cycle_list(g);
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + 310, ly2, 180, 36 })) {
		cycle_parent(g);
		return true;
	}
	if (g->edit_id >= 0 &&
	    inr(x, y, (bu_rect){ SHEET_X + 20, SHEET_Y + SHEET_H - 52, 110, 38 })) {
		if (!g->confirm_del) {
			g->confirm_del = true;
		} else {
			delete_task(g, g->edit_id);
			g->sheet = false;
			g->osk_target = OT_NONE;
			snprintf(g->status, sizeof(g->status), "削除しました");
		}
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + SHEET_W - 300, SHEET_Y + SHEET_H - 52, 130, 38 })) {
		g->sheet = false;
		g->osk_target = OT_NONE;
		return true;
	}
	if (inr(x, y, (bu_rect){ SHEET_X + SHEET_W - 156, SHEET_Y + SHEET_H - 52, 136, 38 })) {
		sheet_save(g);
		return true;
	}
	return true; /* modal */
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct tasks *g = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	/* OSK first */
	if (g->osk_target != OT_NONE) {
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		if (y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&g->osk, area, x, y, out, sizeof(out));
			if (r != OSK_NONE)
				osk_feed(g, r, out);
			return;
		}
	}
	/* close-keyboard button (always reachable above the OSK) */
	if (g->osk_target != OT_NONE && inr(x, y, (bu_rect){ 748, TB_Y, 104, 38 })) {
		g->osk_target = OT_NONE;
		return;
	}
	if (g->sheet || g->lsheet) {
		sheet_event(g, x, y);
		return;
	}
	/* toolbar */
	if (inr(x, y, (bu_rect){ 12, TB_Y, 130, 38 })) {
		sheet_open_new(g, 0);
		return;
	}
	if (inr(x, y, (bu_rect){ 150, TB_Y, 96, 38 })) {
		g->search_on = !g->search_on;
		if (g->search_on) {
			g->osk_target = OT_SEARCH;
			bu_osk_init(&g->osk);
			g->osk.kana = true;
		} else {
			g->search[0] = 0;
			if (g->osk_target == OT_SEARCH)
				g->osk_target = OT_NONE;
		}
		rebuild_disp(g);
		return;
	}
	if (g->view != V_DONE && inr(x, y, (bu_rect){ 254, TB_Y, 170, 38 })) {
		g->show_done = !g->show_done;
		rebuild_disp(g);
		return;
	}
	/* sidebar: smart views */
	for (int i = 0; i < 5; i++) {
		bu_rect r = { SIDE_X + 6, PANE_TOP + 6 + i * 38, SIDE_W - 12, 34 };
		if (inr(x, y, r)) {
			g->view = i; /* V_TODAY..V_DONE are 0..4 in the same order */
			g->scroll = 0;
			rebuild_disp(g);
			return;
		}
	}
	/* sidebar: lists */
	{
		int ly = PANE_TOP + 6 + 5 * 38 + 8 + 28;
		for (int i = 0; i < g->nls; i++) {
			bu_rect r = { SIDE_X + 6, ly + i * 38, SIDE_W - 12, 34 };
			if (r.y + r.h > PANE_BOT - 46)
				break;
			if (inr(x, y, r)) {
				g->view = V_LIST;
				g->view_list = g->ls[i].id;
				g->scroll = 0;
				rebuild_disp(g);
				return;
			}
		}
	}
	if (inr(x, y, (bu_rect){ SIDE_X + 6, PANE_BOT - 40, 92, 34 })) {
		g->lsheet = true;
		g->l_edit = -1;
		g->l_color = 0;
		g->l_name[0] = 0;
		g->confirm_ldel = false;
		g->osk_target = OT_LISTNAME;
		bu_osk_init(&g->osk);
		return;
	}
	if (g->view == V_LIST && inr(x, y, (bu_rect){ SIDE_X + 104, PANE_BOT - 40, 90, 34 })) {
		struct rlist *l = find_list(g, g->view_list);
		if (l) {
			g->lsheet = true;
			g->l_edit = l->id;
			g->l_color = l->color;
			snprintf(g->l_name, sizeof(g->l_name), "%s", l->name);
			g->confirm_ldel = false;
			g->osk_target = OT_NONE;
		}
		return;
	}
	/* main scroll */
	int rows_top = PANE_TOP + 34;
	if (inr(x, y, (bu_rect){ MAIN_X + MAIN_W - 34, rows_top, 30, 44 })) {
		if (g->scroll >= 3) g->scroll -= 3; else g->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ MAIN_X + MAIN_W - 34, PANE_BOT - 50, 30, 44 })) {
		int visible = (PANE_BOT - rows_top - 6) / ROW_H;
		if (g->scroll + visible < g->ndisp) g->scroll += 3;
		return;
	}
	/* rows: circle toggles, rest opens the editor */
	int visible = (PANE_BOT - rows_top - 6) / ROW_H;
	for (int vi = 0; vi < visible; vi++) {
		int di = g->scroll + vi;
		if (di >= g->ndisp)
			break;
		int indent = g->dindent[di];
		bu_rect r = { MAIN_X + indent * 26, rows_top + vi * ROW_H, MAIN_W - indent * 26 - 40, ROW_H - 5 };
		if (!inr(x, y, r))
			continue;
		struct rtask *tk = &g->tk[g->disp[di]];
		if (x >= r.x + 4 && x < r.x + 38) {
			complete_task(g, tk);
		} else {
			sheet_open_edit(g, tk);
		}
		return;
	}
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	(void)ctx;
	(void)changed;
}

int main(int argc, char **argv)
{
	struct tasks g;
	memset(&g, 0, sizeof(g));
	g.view = V_TODAY;
	g.edit_id = -1;
	g.l_edit = -1;
	const char *home = getenv("HOME");
	char dir[560];
	snprintf(dir, sizeof(dir), "%s/.local/share/braindesk", home ? home : "/home/user");
	char cmd[640];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
	if (system(cmd) != 0)
		return 1;
	snprintf(g.file, sizeof(g.file), "%s/tasks.txt", dir);
	load_all(&g);
	rebuild_disp(&g);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-tasks.ppm" : NULL;
	return bdapp_run("タスク", shot, draw, onevent, tick, &g);
}
