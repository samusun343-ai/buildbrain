/* bd-clock: clock, calendar, stopwatch and timer for braindesk. */
#include "bdapp.h"
#include <time.h>
#include <sys/time.h>

struct clockapp {
	int tab;		/* 0 clock, 1 stopwatch, 2 timer */
	bool sw_running;
	double sw_start, sw_elapsed;
	bool tm_running;
	double tm_end;
	int tm_minutes;
	char big[32];
	char sub[64];
};

static double now_s(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static void draw(bdapp *a, void *ctx)
{
	struct clockapp *k = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	const char *tabs[] = { "時計", "ストップウォッチ", "タイマー" };
	for (int i = 0; i < 3; i++)
		bu_button(c, t, (bu_rect){ 16 + i * 170, 54, 158, 38 }, tabs[i],
			  k->tab == i ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	if (k->tab == 0) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 130, a->w, 90 }, buf, 64, t->text);
		static const char *wd[] = { "日", "月", "火", "水", "木", "金", "土" };
		char dbuf[64];
		snprintf(dbuf, sizeof(dbuf), "%d年%d月%d日 (%s)", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, wd[tm.tm_wday % 7]);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 226, a->w, 34 }, dbuf, 20, t->text_dim);
		/* month calendar */
		struct tm first = tm;
		first.tm_mday = 1;
		mktime(&first);
		int start_wd = first.tm_wday;
		int days = 31;
		{
			struct tm nx = first;
			nx.tm_mon++;
			mktime(&nx);
			days = (int)((mktime(&nx) - mktime(&first)) / 86400);
		}
		int x0 = a->w / 2 - 7 * 26, y0 = 286;
		const char *hd = "日月火水木金土";
		for (int i = 0; i < 7; i++) {
			char ch[4] = { 0 };
			memcpy(ch, hd + i * 3, 3);
			bu_draw_text_centered(c, t->font, (bu_rect){ x0 + i * 52, y0, 52, 22 }, ch, 13, t->text_faint);
		}
		for (int d = 1; d <= days; d++) {
			int cell = start_wd + d - 1;
			int cx = x0 + (cell % 7) * 52, cy = y0 + 26 + (cell / 7) * 26;
			char num[8];
			snprintf(num, sizeof(num), "%d", d);
			if (d == tm.tm_mday)
				bu_fill_round_rect(c, (bu_rect){ cx + 8, cy, 36, 22 }, 6, t->accent);
			bu_draw_text_centered(c, t->font, (bu_rect){ cx, cy, 52, 22 }, num, 13,
					      d == tm.tm_mday ? BU_RGB(255, 255, 255) : t->text_dim);
		}
	} else if (k->tab == 1) {
		double el = k->sw_elapsed + (k->sw_running ? now_s() - k->sw_start : 0);
		int mm = (int)(el / 60), ss = (int)el % 60, cs = (int)(el * 100) % 100;
		char buf[32];
		snprintf(buf, sizeof(buf), "%02d:%02d.%02d", mm, ss, cs);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 150, a->w, 80 }, buf, 56, t->text);
		bu_button(c, t, (bu_rect){ a->w / 2 - 170, 280, 150, 46 }, k->sw_running ? "停止" : "開始",
			  BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ a->w / 2 + 20, 280, 150, 46 }, "リセット", BU_BTN_NORMAL, false);
	} else {
		double remain = k->tm_running ? k->tm_end - now_s() : k->tm_minutes * 60;
		if (remain < 0)
			remain = 0;
		int mm = (int)(remain / 60), ss = (int)remain % 60;
		char buf[32];
		snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 130, a->w, 80 }, buf, 56,
				      k->tm_running && remain == 0 ? t->danger : t->text);
		const char *presets[] = { "1分", "3分", "5分", "10分", "30分" };
		int pv[] = { 1, 3, 5, 10, 30 };
		for (int i = 0; i < 5; i++)
			bu_button(c, t, (bu_rect){ 92 + i * 140, 240, 128, 42 }, presets[i],
				  k->tm_minutes == pv[i] ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ a->w / 2 - 170, 310, 150, 46 }, k->tm_running ? "停止" : "開始",
			  BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ a->w / 2 + 20, 310, 150, 46 }, "リセット", BU_BTN_NORMAL, false);
	}
}

static bool in(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct clockapp *k = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	for (int i = 0; i < 3; i++)
		if (in(x, y, (bu_rect){ 16 + i * 170, 54, 158, 38 })) {
			k->tab = i;
			return;
		}
	if (k->tab == 1) {
		if (in(x, y, (bu_rect){ a->w / 2 - 170, 280, 150, 46 })) {
			if (k->sw_running) {
				k->sw_elapsed += now_s() - k->sw_start;
				k->sw_running = false;
			} else {
				k->sw_start = now_s();
				k->sw_running = true;
			}
		}
		if (in(x, y, (bu_rect){ a->w / 2 + 20, 280, 150, 46 })) {
			k->sw_running = false;
			k->sw_elapsed = 0;
		}
	} else if (k->tab == 2) {
		int pv[] = { 1, 3, 5, 10, 30 };
		for (int i = 0; i < 5; i++)
			if (in(x, y, (bu_rect){ 92 + i * 140, 240, 128, 42 })) {
				k->tm_minutes = pv[i];
				k->tm_running = false;
				return;
			}
		if (in(x, y, (bu_rect){ a->w / 2 - 170, 310, 150, 46 })) {
			if (k->tm_running) {
				k->tm_running = false;
			} else {
				k->tm_end = now_s() + k->tm_minutes * 60;
				k->tm_running = true;
			}
		}
		if (in(x, y, (bu_rect){ a->w / 2 + 20, 310, 150, 46 }))
			k->tm_running = false;
	}
}

int main(int argc, char **argv)
{
	struct clockapp k;
	memset(&k, 0, sizeof(k));
	k.tm_minutes = 5;
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-clock.ppm" : NULL;
	return bdapp_run("時計", shot, draw, onevent, NULL, &k);
}
