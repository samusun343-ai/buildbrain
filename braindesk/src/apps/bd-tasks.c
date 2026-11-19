/* bd-tasks: task manager for braindesk (v2).
 * Summary header (memory bar, process count, load average),
 * process list with PSS and CPU% (two samples 2 s apart),
 * sort by memory or CPU, kill with a confirming second tap.
 * Refreshes every 2 seconds while open.
 */
#include "bdapp.h"
#include <dirent.h>
#include <signal.h>
#include <time.h>

#define MAXPROC 256

struct proc {
	int pid;
	char name[64];
	long pss_kb;
	long jiffies;
	double cpu_pct;
};

struct tasks {
	struct proc procs[MAXPROC];
	int n, scroll, sel;
	int sort;		/* 0 = PSS, 1 = CPU */
	long mem_total_kb, mem_avail_kb;
	double load1;
	time_t last_sample;
	long last_total_jiffies;
	bool confirm_kill;
};

static long read_pss(int pid)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", pid);
	FILE *fp = fopen(path, "r");
	if (!fp)
		return -1;
	char line[256];
	long pss = -1;
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "Pss:", 4) == 0) {
			pss = strtol(line + 4, NULL, 10);
			break;
		}
	}
	fclose(fp);
	return pss;
}

static long read_jiffies(int pid, char *name, size_t nn)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	FILE *fp = fopen(path, "r");
	if (!fp)
		return -1;
	char buf[1024];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	if (len == 0)
		return -1;
	buf[len] = 0;
	char *lp = strchr(buf, '(');
	char *rp = strrchr(buf, ')');
	if (!lp || !rp)
		return -1;
	snprintf(name, nn, "%.*s", (int)(rp - lp - 1), lp + 1);
	/* fields after ')': state ppid ... utime(14) stime(15) */
	char *save = NULL;
	char *tok = strtok_r(rp + 2, " ", &save);
	long utime = 0, stime = 0;
	for (int field = 3; tok; field++) {
		if (field == 14)
			utime = strtol(tok, NULL, 10);
		if (field == 15)
			stime = strtol(tok, NULL, 10);
		tok = strtok_r(NULL, " ", &save);
	}
	return utime + stime;
}

static long total_jiffies(void)
{
	FILE *fp = fopen("/proc/stat", "r");
	if (!fp)
		return 0;
	char line[512];
	long total = 0;
	if (fgets(line, sizeof(line), fp)) {
		char *save = NULL;
		char *tok = strtok_r(line, " ", &save); /* "cpu" */
		while ((tok = strtok_r(NULL, " ", &save)))
			total += strtol(tok, NULL, 10);
	}
	fclose(fp);
	return total;
}

static void sample(struct tasks *tk)
{
	/* previous jiffies by pid */
	static long prev_jiff[MAXPROC];
	static int prev_pid[MAXPROC];
	static int prev_n;
	long tot = total_jiffies();
	long dtot = tot - tk->last_total_jiffies;
	tk->last_total_jiffies = tot;
	DIR *d = opendir("/proc");
	tk->n = 0;
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) && tk->n < MAXPROC) {
			if (de->d_name[0] < '0' || de->d_name[0] > '9')
				continue;
			int pid = atoi(de->d_name);
			struct proc *pr = &tk->procs[tk->n];
			pr->pid = pid;
			pr->jiffies = read_jiffies(pid, pr->name, sizeof(pr->name));
			if (pr->jiffies < 0)
				continue;
			pr->pss_kb = read_pss(pid);
			pr->cpu_pct = 0;
			for (int i = 0; i < prev_n; i++) {
				if (prev_pid[i] == pid && dtot > 0) {
					pr->cpu_pct = 100.0 * (double)(pr->jiffies - prev_jiff[i]) / (double)dtot;
					break;
				}
			}
			tk->n++;
		}
		closedir(d);
	}
	prev_n = tk->n;
	for (int i = 0; i < tk->n; i++) {
		prev_pid[i] = tk->procs[i].pid;
		prev_jiff[i] = tk->procs[i].jiffies;
	}
	/* sort */
	for (int i = 0; i < tk->n; i++)
		for (int j = i + 1; j < tk->n; j++) {
			bool swap;
			if (tk->sort == 0)
				swap = tk->procs[j].pss_kb > tk->procs[i].pss_kb;
			else
				swap = tk->procs[j].cpu_pct > tk->procs[i].cpu_pct;
			if (swap) {
				struct proc tmp = tk->procs[i];
				tk->procs[i] = tk->procs[j];
				tk->procs[j] = tmp;
			}
		}
	FILE *fp = fopen("/proc/meminfo", "r");
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "MemTotal:", 9) == 0)
				tk->mem_total_kb = strtol(line + 9, NULL, 10);
			if (strncmp(line, "MemAvailable:", 13) == 0)
				tk->mem_avail_kb = strtol(line + 13, NULL, 10);
		}
		fclose(fp);
	}
	fp = fopen("/proc/loadavg", "r");
	if (fp) {
		if (fscanf(fp, "%lf", &tk->load1) != 1)
			tk->load1 = 0;
		fclose(fp);
	}
	tk->last_sample = time(NULL);
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct tasks *tk = ctx;
	if (time(NULL) - tk->last_sample >= 2) {
		int sel_pid = tk->sel >= 0 && tk->sel < tk->n ? tk->procs[tk->sel].pid : -1;
		sample(tk);
		tk->sel = -1;
		for (int i = 0; i < tk->n; i++)
			if (tk->procs[i].pid == sel_pid)
				tk->sel = i;
		*changed = true;
	}
}

static void draw(bdapp *a, void *ctx)
{
	struct tasks *tk = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* summary card */
	bu_rect card = { 16, 54, 832, 74 };
	bu_card(c, t, card);
	long used = tk->mem_total_kb - tk->mem_avail_kb;
	char buf[128];
	snprintf(buf, sizeof(buf), "メモリ %ld / %ld MB 使用中 · プロセス %d · 負荷 %.2f",
		 used / 1024, tk->mem_total_kb / 1024, tk->n, tk->load1);
	bu_draw_text(c, t->font, card.x + 18, card.y + 10, buf, 13.5, t->text);
	bu_rect bar = { card.x + 18, card.y + 40, card.w - 36, 14 };
	bu_fill_round_rect(c, bar, 7, t->surface3);
	if (tk->mem_total_kb > 0) {
		float frac = (float)used / (float)tk->mem_total_kb;
		bu_color mc = frac > 0.85f ? t->danger : frac > 0.65f ? t->warn : t->accent;
		bu_fill_round_rect(c, (bu_rect){ bar.x, bar.y, (int)(bar.w * frac), bar.h }, 7, mc);
	}
	/* controls */
	bu_button(c, t, (bu_rect){ 16, 138, 130, 38 }, "今すぐ更新", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 154, 138, 170, 38 }, tk->sort == 0 ? "並替: メモリ順" : "並替: CPU順", BU_BTN_NORMAL, false);
	if (tk->sel >= 0)
		bu_button(c, t, (bu_rect){ 640, 138, 208, 38 }, tk->confirm_kill ? "本当に終了しますか?" : "選択中を終了", BU_BTN_DANGER, false);
	/* header */
	bu_draw_text(c, t->font, 32, 188, "プロセス", 11.5, t->text_faint);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, 188, 640, 16 }, "CPU", 11.5, t->text_faint);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, 188, 840, 16 }, "メモリ (PSS)", 11.5, t->text_faint);
	int visible = 6;
	for (int vi = 0; vi < visible; vi++) {
		int i = tk->scroll + vi;
		if (i >= tk->n)
			break;
		struct proc *pr = &tk->procs[i];
		bu_rect r = { 16, 210 + vi * 40, 784, 36 };
		bu_fill_round_rect(c, r, 7, i == tk->sel ? t->surface3 : t->surface);
		char nb[96];
		snprintf(nb, sizeof(nb), "%s", pr->name);
		bu_draw_text(c, t->font, r.x + 12, r.y + 8, nb, 13, t->text);
		char pidb[16];
		snprintf(pidb, sizeof(pidb), "%d", pr->pid);
		bu_draw_text(c, t->font, r.x + 300, r.y + 8, pidb, 12, t->text_faint);
		char cb[16];
		snprintf(cb, sizeof(cb), "%.0f%%", pr->cpu_pct);
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 8, 610, 20 }, cb, 12.5, pr->cpu_pct > 50 ? t->warn : t->text_dim);
		char mb[24];
		if (pr->pss_kb >= 1024)
			snprintf(mb, sizeof(mb), "%.1f MB", pr->pss_kb / 1024.0);
		else if (pr->pss_kb >= 0)
			snprintf(mb, sizeof(mb), "%ld KB", pr->pss_kb);
		else
			snprintf(mb, sizeof(mb), "-");
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 8, r.w - 12, 20 }, mb, 12.5, t->text_dim);
	}
	bu_button(c, t, (bu_rect){ 808, 210, 44, 52 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 808, 402, 44, 52 }, "▼", BU_BTN_NORMAL, false);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct tasks *tk = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (inr(x, y, (bu_rect){ 16, 138, 130, 38 })) {
		sample(tk);
		return;
	}
	if (inr(x, y, (bu_rect){ 154, 138, 170, 38 })) {
		tk->sort = 1 - tk->sort;
		sample(tk);
		return;
	}
	if (tk->sel >= 0 && inr(x, y, (bu_rect){ 640, 138, 208, 38 })) {
		if (!tk->confirm_kill) {
			tk->confirm_kill = true;
		} else {
			kill(tk->procs[tk->sel].pid, SIGTERM);
			tk->confirm_kill = false;
			tk->sel = -1;
			sample(tk);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 808, 210, 44, 52 })) {
		if (tk->scroll >= 3) tk->scroll -= 3; else tk->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ 808, 402, 44, 52 })) {
		if (tk->scroll + 6 < tk->n) tk->scroll += 3;
		return;
	}
	int visible = 6;
	for (int vi = 0; vi < visible; vi++) {
		int i = tk->scroll + vi;
		if (i >= tk->n)
			break;
		bu_rect r = { 16, 210 + vi * 40, 784, 36 };
		if (inr(x, y, r)) {
			tk->sel = i;
			tk->confirm_kill = false;
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct tasks tk;
	memset(&tk, 0, sizeof(tk));
	tk.sel = -1;
	sample(&tk);
	sample(&tk); /* second sample so CPU% has a baseline */
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-tasks.ppm" : NULL;
	return bdapp_run("タスク", shot, draw, onevent, tick, &tk);
}
