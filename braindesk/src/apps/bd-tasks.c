/* bd-tasks: task manager for braindesk.
 * Process list from /proc with per-process PSS (smaps_rollup),
 * memory summary, and kill with a confirming second tap.
 * Refreshes every 2 seconds while open.
 */
#include "bdapp.h"
#include <dirent.h>
#include <signal.h>
#include <ctype.h>

#define MAXP 128
#define ROWH 40
#define VISIBLE 9

struct proc {
	int pid;
	char name[64];
	long pss_kb;
	int uid;
};

struct tasks {
	struct proc p[MAXP];
	int n, scroll, sel;
	bool confirm_kill;
	long mem_total, mem_avail;
};

static long read_kb(const char *path, const char *key)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return -1;
	char line[256];
	long v = -1;
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, key, strlen(key)) == 0) {
			sscanf(line + strlen(key), "%ld", &v);
			break;
		}
	}
	fclose(fp);
	return v;
}

static int proc_cmp(const void *a, const void *b)
{
	long d = ((const struct proc *)b)->pss_kb - ((const struct proc *)a)->pss_kb;
	return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static void load(struct tasks *t)
{
	t->n = 0;
	t->sel = -1;
	t->confirm_kill = false;
	t->mem_total = read_kb("/proc/meminfo", "MemTotal:");
	t->mem_avail = read_kb("/proc/meminfo", "MemAvailable:");
	DIR *d = opendir("/proc");
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && t->n < MAXP) {
		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		struct proc *p = &t->p[t->n];
		p->pid = atoi(de->d_name);
		char path[128];
		snprintf(path, sizeof(path), "/proc/%d/status", p->pid);
		FILE *fp = fopen(path, "r");
		if (!fp)
			continue;
		char line[256];
		p->name[0] = 0;
		p->uid = -1;
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "Name:", 5) == 0)
				sscanf(line + 5, "%63s", p->name);
			if (strncmp(line, "Uid:", 4) == 0)
				sscanf(line + 4, "%d", &p->uid);
		}
		fclose(fp);
		snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", p->pid);
		p->pss_kb = read_kb(path, "Pss:");
		if (p->pss_kb < 0) {
			snprintf(path, sizeof(path), "/proc/%d/status", p->pid);
			p->pss_kb = read_kb(path, "VmRSS:");
		}
		if (p->pss_kb < 0)
			p->pss_kb = 0;
		t->n++;
	}
	closedir(d);
	qsort(t->p, t->n, sizeof(struct proc), proc_cmp);
}

static void draw(bdapp *a, void *ctx)
{
	struct tasks *t = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *th = a->theme;
	bdapp_frame(a);
	char hdr[96];
	long used = t->mem_total - t->mem_avail;
	snprintf(hdr, sizeof(hdr), "メモリ使用量 %.1f / %.1f MB  (%ld%%)",
		 used / 1024.0, t->mem_total / 1024.0,
		 t->mem_total > 0 ? used * 100 / t->mem_total : 0);
	bu_draw_text(c, th->font, 16, 56, hdr, 14, th->text);
	/* usage bar */
	bu_fill_round_rect(c, (bu_rect){ 16, 80, 600, 10 }, 5, th->surface);
	if (t->mem_total > 0)
		bu_fill_round_rect(c, (bu_rect){ 16, 80, (int)(600.0 * used / t->mem_total), 10 }, 5, th->accent);
	bu_button(c, th, (bu_rect){ 640, 52, 112, 38 }, "再読込", BU_BTN_NORMAL, false);
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = t->scroll + vi;
		if (i >= t->n)
			break;
		struct proc *p = &t->p[i];
		bu_rect r = { 12, 100 + vi * ROWH, 700, ROWH - 4 };
		bu_fill_round_rect(c, r, 8, i == t->sel ? th->surface3 : th->surface);
		char left[96];
		snprintf(left, sizeof(left), "%s", p->name);
		bu_draw_text(c, th->font, r.x + 12, r.y + 7, left, 14, th->text);
		char right[64];
		snprintf(right, sizeof(right), "PID %d  %.1f MB", p->pid, p->pss_kb / 1024.0);
		bu_draw_text_right(c, th->font, (bu_rect){ r.x, r.y + 7, r.w - 12, 22 }, right, 12.5, th->text_dim);
	}
	bu_button(c, th, (bu_rect){ 724, 100, 52, 60 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, th, (bu_rect){ 724, 396, 52, 60 }, "▼", BU_BTN_NORMAL, false);
	if (t->sel >= 0) {
		bu_button(c, th, (bu_rect){ 12, 480 - 40, 190, 34 },
			  t->confirm_kill ? "本当に終了?" : "プロセス終了", BU_BTN_DANGER, false);
	}
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct tasks *t = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (x >= 640 && x < 752 && y >= 52 && y < 90) {
		load(t);
		return;
	}
	if (x >= 724 && x < 776 && y >= 100 && y < 160) {
		if (t->scroll > 0)
			t->scroll--;
		return;
	}
	if (x >= 724 && x < 776 && y >= 396 && y < 456) {
		if (t->scroll + VISIBLE < t->n)
			t->scroll++;
		return;
	}
	if (t->sel >= 0 && x >= 12 && x < 202 && y >= 440 && y < 474) {
		if (!t->confirm_kill) {
			t->confirm_kill = true;
		} else {
			kill(t->p[t->sel].pid, SIGTERM);
			load(t);
		}
		return;
	}
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = t->scroll + vi;
		if (i >= t->n)
			break;
		bu_rect r = { 12, 100 + vi * ROWH, 700, ROWH - 4 };
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
			t->sel = i;
			t->confirm_kill = false;
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct tasks t;
	memset(&t, 0, sizeof(t));
	t.sel = -1;
	load(&t);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-tasks.ppm" : NULL;
	return bdapp_run("タスクマネージャー", shot, draw, onevent, &t);
}
