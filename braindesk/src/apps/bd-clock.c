/* bd-clock: clock for braindesk (v2).
 * Tabs: 時計 (big clock + month calendar), 世界時計 (major
 * cities from UTC), ストップウォッチ, タイマー.
 */
#include "bdapp.h"
#include <time.h>
#include <sys/time.h>

struct clockapp {
	int tab;		/* 0 clock, 1 world, 2 stopwatch, 3 timer */
	/* stopwatch */
	bool sw_running;
	struct timeval sw_start;
	long sw_elapsed_ms;
	/* timer */
	int timer_set_sec;
	long timer_left_sec;
	bool timer_running;
	struct timeval timer_end;
	bool timer_done;
};

static long now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	(void)a;
	struct clockapp *k = ctx;
	*changed = true; /* the clock face updates every tick */
	if (k->timer_running) {
		long left = (k->timer_end.tv_sec - time(NULL));
		k->timer_left_sec = left > 0 ? left : 0;
		if (left <= 0) {
			k->timer_running = false;
			k->timer_done = true;
		}
	}
}

static const char *TABS[] = { "時計", "世界時計", "ストップウォッチ", "タイマー" };

static bu_rect tab_rect(int i) { return (bu_rect){ 16 + i * 208, 52, 200, 40 }; }

static void draw_tabs(bdapp *a, struct clockapp *k)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	for (int i = 0; i < 4; i++) {
		bu_rect r = tab_rect(i);
		bu_fill_round_rect(c, r, 9, i == k->tab ? t->accent : t->surface);
		bu_draw_text_centered(c, t->font, r, TABS[i], 14, i == k->tab ? BU_RGB(255, 255, 255) : t->text_dim);
	}
}

static void draw(bdapp *a, void *ctx)
{
	struct clockapp *k = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	draw_tabs(a, k);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	if (k->tab == 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 108, 864, 84 }, buf, 62, t->text);
		static const char *wd[] = { "日", "月", "火", "水", "木", "金", "土" };
		snprintf(buf, sizeof(buf), "%d年%d月%d日 (%s)", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, wd[tm.tm_wday]);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 192, 864, 30 }, buf, 17, t->text_dim);
		/* month calendar */
		struct tm first = tm;
		first.tm_mday = 1;
		mktime(&first);
		int start_wd = first.tm_wday;
		static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		int dim = mdays[tm.tm_mon];
		if (tm.tm_mon == 1 && ((tm.tm_year + 1900) % 4 == 0 && ((tm.tm_year + 1900) % 100 != 0 || (tm.tm_year + 1900) % 400 == 0)))
			dim = 29;
		int cx0 = 864 / 2 - 7 * 36 / 2, cy0 = 244;
		for (int i = 0; i < 7; i++)
			bu_draw_text_centered(c, t->font, (bu_rect){ cx0 + i * 36, cy0, 36, 20 }, wd[i], 12,
					      i == 0 ? t->danger : i == 6 ? t->accent : t->text_faint);
		for (int d = 1; d <= dim; d++) {
			int cell = start_wd + d - 1;
			int x = cx0 + (cell % 7) * 36, y = cy0 + 22 + (cell / 7) * 30;
			if (d == tm.tm_mday)
				bu_fill_round_rect(c, (bu_rect){ x + 3, y, 30, 26 }, 8, t->accent);
			char db[4];
			snprintf(db, sizeof(db), "%d", d);
			bu_draw_text_centered(c, t->font, (bu_rect){ x, y + 2, 36, 22 }, db, 13.5,
					      d == tm.tm_mday ? BU_RGB(255, 255, 255) : t->text);
		}
	} else if (k->tab == 1) {
		/* world clock from UTC (standard offsets; DST noted) */
		static const struct { const char *city; int off_min; } CITIES[] = {
			{ "東京", 9 * 60 }, { "北京", 8 * 60 }, { "バンコク", 7 * 60 },
			{ "ドバイ", 4 * 60 }, { "ロンドン", 0 }, { "パリ", 60 },
			{ "ニューヨーク", -5 * 60 }, { "ロサンゼルス", -8 * 60 },
			{ "シドニー", 10 * 60 }, { "ホノルル", -10 * 60 },
		};
		time_t utc = time(NULL);
		for (int i = 0; i < 10; i++) {
			int col = i % 2, row = i / 2;
			bu_rect r = { 16 + col * 424, 108 + row * 62, 408, 54 };
			bu_fill_round_rect(c, r, 9, t->surface);
			time_t lt = utc + CITIES[i].off_min * 60;
			struct tm ltm;
			gmtime_r(&lt, &ltm);
			bu_draw_text(c, t->font, r.x + 16, r.y + 15, CITIES[i].city, 15, t->text);
			int daydiff = ltm.tm_mday - tm.tm_mday;
			char tb[64];
			snprintf(tb, sizeof(tb), "%02d:%02d%s", ltm.tm_hour, ltm.tm_min,
				 daydiff > 0 ? " (翌日)" : daydiff < 0 ? " (前日)" : "");
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 12, r.w - 16, 30 }, tb, 19, t->accent);
		}
		bu_draw_text(c, t->font, 16, 432, "標準時のオフセットで計算しています (夏時間は未調整)", 11.5, t->text_faint);
	} else if (k->tab == 2) {
		long ms = k->sw_elapsed_ms;
		if (k->sw_running)
			ms += now_ms() - (k->sw_start.tv_sec * 1000L + k->sw_start.tv_usec / 1000);
		char buf[64];
		snprintf(buf, sizeof(buf), "%02ld:%02ld.%ld", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 150, 864, 84 }, buf, 58, t->text);
		bu_button(c, t, (bu_rect){ 232, 280, 190, 56 }, k->sw_running ? "ストップ" : "スタート", BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ 442, 280, 190, 56 }, "リセット", BU_BTN_NORMAL, false);
	} else {
		if (k->timer_done) {
			bu_draw_text_centered(c, t->font, (bu_rect){ 0, 130, 864, 60 }, "時間になりました", 34, t->warn);
		}
		char buf[64];
		long show = k->timer_running || k->timer_left_sec > 0 ? k->timer_left_sec : k->timer_set_sec;
		snprintf(buf, sizeof(buf), "%02ld:%02ld", show / 60, show % 60);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 190, 864, 70 }, buf, 52, t->text);
		static const int presets[] = { 60, 180, 300, 600, 1800 };
		static const char *plabels[] = { "1分", "3分", "5分", "10分", "30分" };
		for (int i = 0; i < 5; i++)
			bu_button(c, t, (bu_rect){ 52 + i * 156, 290, 140, 48 }, plabels[i],
				  k->timer_set_sec == presets[i] ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 232, 360, 190, 52 }, k->timer_running ? "停止" : "スタート", BU_BTN_PRIMARY, false);
		bu_button(c, t, (bu_rect){ 442, 360, 190, 52 }, "リセット", BU_BTN_NORMAL, false);
	}
}

static bool inr(int x, int y, bu_rect r)
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
	for (int i = 0; i < 4; i++)
		if (inr(x, y, tab_rect(i))) {
			k->tab = i;
			return;
		}
	if (k->tab == 2) {
		if (inr(x, y, (bu_rect){ 232, 280, 190, 56 })) {
			if (k->sw_running) {
				k->sw_elapsed_ms += now_ms() - (k->sw_start.tv_sec * 1000L + k->sw_start.tv_usec / 1000);
				k->sw_running = false;
			} else {
				gettimeofday(&k->sw_start, NULL);
				k->sw_running = true;
			}
		} else if (inr(x, y, (bu_rect){ 442, 280, 190, 56 })) {
			k->sw_running = false;
			k->sw_elapsed_ms = 0;
		}
	} else if (k->tab == 3) {
		static const int presets[] = { 60, 180, 300, 600, 1800 };
		for (int i = 0; i < 5; i++)
			if (inr(x, y, (bu_rect){ 52 + i * 156, 290, 140, 48 })) {
				k->timer_set_sec = presets[i];
				k->timer_left_sec = presets[i];
				k->timer_running = false;
				k->timer_done = false;
				return;
			}
		if (inr(x, y, (bu_rect){ 232, 360, 190, 52 })) {
			if (k->timer_running) {
				k->timer_running = false;
			} else if (k->timer_set_sec > 0) {
				k->timer_end.tv_sec = time(NULL) + k->timer_set_sec;
				k->timer_left_sec = k->timer_set_sec;
				k->timer_running = true;
				k->timer_done = false;
			}
		} else if (inr(x, y, (bu_rect){ 442, 360, 190, 52 })) {
			k->timer_running = false;
			k->timer_left_sec = k->timer_set_sec;
			k->timer_done = false;
		}
	}
}

int main(int argc, char **argv)
{
	struct clockapp k;
	memset(&k, 0, sizeof(k));
	k.timer_set_sec = 300;
	k.timer_left_sec = 300;
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-clock.ppm" : NULL;
	return bdapp_run("時計", shot, draw, onevent, tick, &k);
}
