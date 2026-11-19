#include "scenes.h"
#include "bui_skin.h"
#include <time.h>
#include "bui_input.h"
#include "bui_wm.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PW 854
#define PH 480
static int PANEL_H = 48;

/* The desktop has ONE layout (the braindesk design system):
 * a 48 px taskbar at the bottom, a centred launcher panel,
 * categorised app grids. Colour themes change the palette
 * only, never the arrangement. */

/* Desktop work area for the window manager: the screen minus
 * the bottom taskbar. */
void bd_workarea(bu_rect *r)
{
	*r = (bu_rect){ 0, 0, PW, PH - PANEL_H };
}

/* ---- taskbar geometry ----
 * Centred cluster: the start button, three favourite pins,
 * then the running apps (drawn by bui_wm). The tray sits at
 * the right edge: quick settings (Wi-Fi), volume, battery and
 * the clock. The cluster origin depends on how wide the
 * running-app buttons are, so panel() computes it with the
 * theme font and caches it here; the hit code uses the cached
 * values, which keeps draw and hit geometry identical. */
#define TB_BTN 40	/* start / pin button size */
#define TB_GAP 8	/* the 8 px spacing grid */
#define TB_PINS 3
static int tb_cluster_x = (PW - (TB_BTN + TB_PINS * TB_BTN + TB_PINS * TB_GAP)) / 2;
static int tb_task_x0, tb_task_x1;

static int tb_cluster_w(int tasks_w)
{
	int w = TB_BTN + TB_GAP + TB_PINS * TB_BTN + (TB_PINS - 1) * TB_GAP;
	if (tasks_w > 0)
		w += 12 + tasks_w;
	return w;
}

/* Width of the running-app buttons, replicating bui_wm's
 * task_layout(): text width at 11.5 px + 22, clamped to
 * [60, 148], with 4 px gaps. */
static int tb_tasks_width(bu_font *font)
{
	struct bd_wm_info infos[8];
	int n = bd_wm_list(infos, 8);
	int w = 0;
	for (int i = 0; i < n; i++) {
		int bw = font ? bu_text_width(font, infos[i].title, 11.5f) : 40;
		bw += 22;
		if (bw < 60) bw = 60;
		if (bw > 148) bw = 148;
		w += (i ? 4 : 0) + bw;
	}
	return w;
}

static void tb_layout(bu_font *font)
{
	int tw = tb_tasks_width(font);
	int x = (PW - tb_cluster_w(tw)) / 2;
	if (x < TB_GAP)
		x = TB_GAP;
	tb_cluster_x = x;
	tb_task_x0 = x + TB_BTN + TB_GAP + TB_PINS * TB_BTN + (TB_PINS - 1) * TB_GAP + 12;
	tb_task_x1 = tb_task_x0 + tw;
	if (tb_task_x1 > PW - 210)
		tb_task_x1 = PW - 210;
}

static bu_rect start_btn(void)
{
	return (bu_rect){ tb_cluster_x, PH - PANEL_H + (PANEL_H - TB_BTN) / 2, TB_BTN, TB_BTN };
}
static bu_rect ql_rect(int i)
{
	bu_rect sb = start_btn();
	return (bu_rect){ sb.x + TB_BTN + TB_GAP + i * (TB_BTN + TB_GAP), sb.y, TB_BTN, TB_BTN };
}
/* Panel task-button area (running WM windows), cached by
 * tb_layout() during panel(). */
static void task_area(int *x0, int *x1)
{
	*x0 = tb_task_x0;
	*x1 = tb_task_x1;
}
static bu_rect vol_icon_rect(void)
{
	return (bu_rect){ PW - 168, PH - PANEL_H, 34, PANEL_H };
}
static bu_rect clock_rect(void)
{
	return (bu_rect){ PW - 96, PH - PANEL_H, 96, PANEL_H };
}
static bu_rect vol_popup_rect(void)
{
	return (bu_rect){ PW - 268, PH - PANEL_H - 66, 256, 58 };
}
static bu_rect vol_popup_slider(void) { bu_rect p = vol_popup_rect(); return (bu_rect){ p.x + 44, p.y + 16, p.w - 90, 26 }; }
static bu_rect assist_btn(void) { return (bu_rect){ PW - 72, PH - PANEL_H - 80, 54, 54 }; }
static bu_rect assist_menu(void) { return (bu_rect){ PW - 292, PH - PANEL_H - 80 - 196, 274, 188 }; }
static bu_rect assist_item(int i) { bu_rect m = assist_menu(); return (bu_rect){ m.x + 10 + (i % 2) * 130, m.y + 10 + (i / 2) * 44, 124, 38 }; }
static bu_rect settings_win(void)
{
	return (bu_rect){ 36, 16, PW - 72, PH - PANEL_H - 26 };
}
static bu_rect pane_rect(void)
{
	bu_rect w = settings_win();
	return (bu_rect){ w.x + 160, w.y + 36, w.w - 160, w.h - 36 };
}
static bu_rect brightness_slider(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 58, p.y + 80, p.w - 150, 24 };
}

static bu_rect mode_row(int vi)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 8, p.y + 76 + vi * 56, p.w - 70, 50 };
}
static bu_rect kbd_repeat_row(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 8, p.y + 84, p.w - 16, 42 };
}
static bu_rect kbd_rate_row(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 8, p.y + 132, p.w - 16, 42 };
}
static bu_rect osk_mode_row(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 8, p.y + 84, p.w - 16, 42 };
}
static int mode_current(bd_state *s)
{
	for (int i = 0; i < s->mode_count; i++)
		if (strcmp(s->mode_name, s->mode_ids[i]) == 0) return i;
	return -1;
}
static bu_rect mode_deck_btn(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 8, p.y + 304, p.w - 16, 42 };
}
static bu_rect volume_slider(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 58, p.y + 268, p.w - 150, 24 };
}
static bu_rect keysound_toggle_hit(void);
static bu_rect volume_slider_for(bd_state *s)
{
	bu_rect p = pane_rect();
	if (s->settings_cat == 2)
		return (bu_rect){ p.x + 58, p.y + 80, p.w - 150, 24 };
	return volume_slider();
}
static bu_rect keysound_hit_for(bd_state *s)
{
	bu_rect p = pane_rect();
	if (s->settings_cat == 2)
		return (bu_rect){ p.x + p.w - 76, p.y + 140, 62, 42 };
	return keysound_toggle_hit();
}
static bu_rect panel_size_row(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 8, p.y + 208, p.w - 16, 42 };
}
static bu_rect assist_row(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 8, p.y + 256, p.w - 16, 42 };
}
static bu_rect lid_toggle_hit(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + p.w - 76, p.y + 160, 62, 42 };
}
static bu_rect keysound_toggle_hit(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + p.w - 76, p.y + 302, 62, 42 };
}
static bu_rect settings_close(void)
{
	bu_rect w = settings_win();
	return (bu_rect){ w.x + w.w - 32, w.y + 6, 24, 24 };
}

static bool in_rect(bu_rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

typedef void (*icon_fn)(bu_canvas *, int, int, bu_color);
struct app { const char *name; icon_fn icon; bu_color tint; const char *exec; const char *keys; };
static const struct app APPS[] = {
	{ "設定", bu_icon_gear, { 134, 142, 150, 255 }, NULL, "settei settings gear" },
	{ "ファイル", bu_icon_folder, { 250, 176, 5, 255 }, "/usr/local/bin/bd-files", "files fairu file folder" },
	{ "タスク", bu_icon_tasks, { 255, 135, 135, 255 }, "/usr/local/bin/bd-tasks", "tasks tasuku todo reminders" },
	{ "メモ", bu_icon_note, { 255, 212, 59, 255 }, "/usr/local/bin/bd-notes", "notes memo" },
	{ "時計", bu_icon_clock, { 130, 200, 255, 255 }, "/usr/local/bin/bd-clock", "clock tokei timer" },
	{ "電卓", bu_icon_calc, { 116, 192, 252, 255 }, "/usr/local/bin/bd-calc", "calc dentaku calculator" },
	{ "音楽", bu_icon_music, { 240, 101, 149, 255 }, "/usr/local/bin/bd-player", "music ongaku player" },
	{ "録音", bu_icon_mic, { 255, 110, 110, 255 }, "/usr/local/bin/bd-recorder", "recorder rokuon mic voice" },
	{ "ブラウザ", bu_icon_globe, { 32, 201, 151, 255 }, "/usr/local/bin/bd-web", "browser burauza web" },
	{ "デバイス", bu_icon_chip, { 178, 140, 240, 255 }, "/usr/local/bin/bd-devices", "devices debaisu chip" },
	{ "パッケージ", bu_icon_box, { 165, 215, 130, 255 }, "/usr/local/bin/bd-pkg", "package pakkeji pkg" },
	{ "辞書", bu_icon_book, { 45, 156, 219, 255 }, NULL, "dict jisyo book dictionary" },
	{ "端末", bu_icon_terminal, { 90, 100, 116, 255 }, NULL, "terminal tanmatsu console" },
	{ "ホーム", bu_icon_home, { 129, 230, 168, 255 }, NULL, "home" },
	{ "明るさ", bu_icon_brightness, { 255, 224, 102, 255 }, NULL, "brightness akarusa" },
	{ "音量", bu_icon_volume, { 99, 230, 190, 255 }, NULL, "volume onryo sound" },
};
#define NAPPS 16

/* App categories (design system): general apps and system
 * functions are listed under separate headings so the two
 * kinds read differently at a glance. */
static const int APP_CAT_APPS[9] = { 1, 8, 6, 3, 7, 4, 5, 11, 2 };
static const int APP_CAT_SYS[7] = { 0, 9, 14, 15, 13, 10, 12 };
/* Launcher pins: related apps sit next to each other
 * (ファイル↔パッケージ, 設定↔デバイス, 音楽↔録音). */
static const int LN_PINS[12] = { 1, 10, 0, 9, 6, 7, 8, 3, 2, 4, 5, 11 };

/* ---- launcher ----
 * One centred panel (radius 12): a 40 px search field on top,
 * a "ピン留め" pin grid, a "最近使ったアプリ" strip and a
 * footer with the user, a prominent すべてのアプリ button and
 * power. The all-apps view swaps the body for the two
 * categorised grids (アプリ / システム). A non-empty search
 * query swaps the body for one flat results grid. */
#define LN_W 640
#define LN_H 416
static bu_rect launcher_panel(void)
{
	return (bu_rect){ (PW - LN_W) / 2, PH - PANEL_H - 8 - LN_H, LN_W, LN_H };
}
static bu_rect ln_search_rect(void)
{
	bu_rect p = launcher_panel();
	return (bu_rect){ p.x + 24, p.y + 20, p.w - 48, 40 };
}
/* 6-column grid cell (pins, recent, search results). */
static bu_rect ln_cell(int pos, int y0, int ch)
{
	bu_rect p = launcher_panel();
	int cw = 88;
	int x0 = p.x + (p.w - 6 * cw) / 2;
	return (bu_rect){ x0 + (pos % 6) * cw, y0 + (pos / 6) * ch, cw, ch };
}
/* 8-column grid cell (all-apps view). */
static bu_rect ln_cell8(int pos, int y0, int ch)
{
	bu_rect p = launcher_panel();
	int cw = 74;
	return (bu_rect){ p.x + 24 + (pos % 8) * cw, y0 + (pos / 8) * ch, cw, ch };
}
static bu_rect ln_recent_rect(int pos)
{
	bu_rect p = launcher_panel();
	return ln_cell(pos, p.y + 284, 64);
}
static bu_rect ln_allapps_btn(void)
{
	bu_rect p = launcher_panel();
	return (bu_rect){ p.x + p.w - 24 - 44 - 16 - 168, p.y + p.h - 48, 168, 36 };
}
static bu_rect ln_back_btn(void)
{
	bu_rect p = launcher_panel();
	return (bu_rect){ p.x + 24, p.y + p.h - 48, 120, 36 };
}
static bu_rect ln_power_rect(void)
{
	bu_rect p = launcher_panel();
	return (bu_rect){ p.x + p.w - 24 - 36, p.y + p.h - 46, 36, 32 };
}

static bool ln_all;		/* all-apps view */
static char ln_query[64];	/* search text */
static bool ln_search_focus;
static int ln_recent[6];
static int ln_nrecent;
static bool ln_recent_loaded;

static void ln_recent_path(char *out, size_t n)
{
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.config/braindesk/recent", home ? home : "/home/user");
}
static void ln_recent_load(void)
{
	if (ln_recent_loaded)
		return;
	ln_recent_loaded = true;
	ln_nrecent = 0;
	char path[512];
	ln_recent_path(path, sizeof(path));
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	int v;
	while (ln_nrecent < 6 && fscanf(fp, "%d", &v) == 1)
		if (v >= 0 && v < NAPPS)
			ln_recent[ln_nrecent++] = v;
	fclose(fp);
}
static void ln_recent_save(void)
{
	char path[512], dir[512];
	ln_recent_path(path, sizeof(path));
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = 0;
		char cmd[600];
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
		if (system(cmd) != 0)
			return;
	}
	FILE *fp = fopen(path, "w");
	if (!fp)
		return;
	for (int i = 0; i < ln_nrecent; i++)
		fprintf(fp, "%d\n", ln_recent[i]);
	fclose(fp);
}
/* Record an app launch in the recent list (most recent first,
 * de-duplicated, capped at 6) and persist it. */
static void ln_record(int app)
{
	ln_recent_load();
	int out[6], n = 0;
	out[n++] = app;
	for (int i = 0; i < ln_nrecent && n < 6; i++)
		if (ln_recent[i] != app)
			out[n++] = ln_recent[i];
	memcpy(ln_recent, out, sizeof(out));
	ln_nrecent = n;
	ln_recent_save();
}
static void ln_reset(void)
{
	ln_all = false;
	ln_query[0] = 0;
	ln_search_focus = false;
}
/* Hooks for shell.c (BD_SHOT_*) and the synthetic tap harness;
 * the launcher state itself stays file-static. */
void bd_launcher_show_all(void) { ln_all = true; }
int bd_launcher_is_all(void) { return ln_all; }
int bd_recent_count(void) { ln_recent_load(); return ln_nrecent; }
int bd_recent_at(int i)
{
	ln_recent_load();
	return (i >= 0 && i < ln_nrecent) ? ln_recent[i] : -1;
}
void bd_recent_seed(void)
{
	static const int seed[4] = { 1, 8, 6, 3 };
	ln_recent_loaded = true;
	memcpy(ln_recent, seed, sizeof(seed));
	ln_nrecent = 4;
}
static bool ln_matches(int app)
{
	if (!ln_query[0])
		return true;
	if (strstr(APPS[app].name, ln_query))
		return true;
	return APPS[app].keys && strstr(APPS[app].keys, ln_query);
}
/* Launch an app from the launcher / taskbar: records the
 * recent entry, opens Settings for app 0, execs otherwise. */
static int ln_launch(bd_state *s, int app)
{
	ln_record(app);
	if (app == 0) {
		s->settings_open = true;
		s->view = BD_VIEW_DESKTOP;
		return BD_ACT_NONE;
	}
	if (APPS[app].exec) {
		snprintf(s->launch_path, sizeof(s->launch_path), "%s", APPS[app].exec);
		s->view = BD_VIEW_DESKTOP;
		return BD_ACT_LAUNCH;
	}
	s->view = BD_VIEW_DESKTOP;
	return BD_ACT_NONE;
}
/* Keyboard input for the search field (evdev codes). */
static void ln_query_key(int code)
{
	if (code == 14) {	/* backspace: drop the last UTF-8 char */
		int l = (int)strlen(ln_query);
		while (l > 0 && (ln_query[l - 1] & 0xC0) == 0x80)
			l--;
		if (l > 0)
			ln_query[l - 1] = 0;
		return;
	}
	char ch = 0;
	if (code >= 16 && code <= 25)
		ch = "qwertyuiop"[code - 16];
	else if (code >= 30 && code <= 38)
		ch = "asdfghjkl"[code - 30];
	else if (code >= 44 && code <= 50)
		ch = "zxcvbnm"[code - 44];
	else if (code >= 2 && code <= 11)
		ch = "1234567890"[code - 2];
	else if (code == 57)
		ch = ' ';
	if (ch && strlen(ln_query) < sizeof(ln_query) - 1) {
		int l = (int)strlen(ln_query);
		ln_query[l] = ch;
		ln_query[l + 1] = 0;
	}
}

void bd_update_datetime(bd_state *s)
{
	static const char *wday[] = { "日", "月", "火", "水", "木", "金", "土" };
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	snprintf(s->clock, sizeof(s->clock), "%02d:%02d", tm.tm_hour, tm.tm_min);
	snprintf(s->date, sizeof(s->date), "%d月%d日 %s曜日",
		 tm.tm_mon + 1, tm.tm_mday, wday[tm.tm_wday % 7]);
}

void bd_state_init(bd_state *s)
{
	memset(s, 0, sizeof(*s));
	s->view = BD_VIEW_DESKTOP;
	s->brightness = 0.7f;
	s->volume = 0.45f;
	s->lid_sleep = true;
	s->key_sound = false;
	snprintf(s->clock, sizeof(s->clock), "10:42");
	snprintf(s->date, sizeof(s->date), "1月1日 木曜日");
	s->battery_pct = 78;
	s->panel_h = 0;	/* 0 = follow the skin family */
	s->wifi_on = true;
	s->mode_set = -1;
	snprintf(s->mode_name, sizeof(s->mode_name), "normal");
	s->net_connect = -1;
	snprintf(s->wifi_ssid, sizeof(s->wifi_ssid), "—");
	snprintf(s->ip_addr, sizeof(s->ip_addr), "—");
}

static void wallpaper(bu_canvas *c)
{
	const bd_skin *sk = bd_skin_get();
	bu_gradient_v(c, (bu_rect){ 0, 0, PW, PH }, sk->wall_top, sk->wall_bottom);
	bu_color glow = sk->wall_glow;
	glow.a = 7;
	bu_color glow2 = sk->accent;
	glow2.a = 6;
	for (int i = 0; i < 26; i++)
		bu_fill_circle(c, 700 - i * 6, 60 + i * 3, 120 - i * 3, glow);
	for (int i = 0; i < 20; i++)
		bu_fill_circle(c, 90 + i * 4, 420 - i * 4, 100 - i * 3, glow2);
}

static const struct { icon_fn icon; bu_color tint; } QL_ICONS[] = {
	{ bu_icon_gear, { 134, 142, 150, 255 } },
	{ bu_icon_folder, { 250, 176, 5, 255 } },
	{ bu_icon_globe, { 32, 201, 151, 255 } },
};

/* Four window panes (start / launch glyphs). */
static void draw_panes(bu_canvas *c, int x, int y, int s, int gap, bu_color col)
{
	bu_fill_rect(c, (bu_rect){ x, y, s, s }, col);
	bu_fill_rect(c, (bu_rect){ x + s + gap, y, s, s }, col);
	bu_fill_rect(c, (bu_rect){ x, y + s + gap, s, s }, col);
	bu_fill_rect(c, (bu_rect){ x + s + gap, y + s + gap, s, s }, col);
}

/* ---------- raster icon assets (unified app set) ---------- */
static const char *asset_dir(void)
{
	static char dir[256];
	static int done;
	if (done)
		return dir;
	done = 1;
	const char *home = getenv("HOME");
	char local[320];
	snprintf(local, sizeof(local), "%s/.local/share/braindesk/assets",
		 home ? home : "/home/user");
	const char *cands[5];
	cands[0] = getenv("BRAINDESK_ASSETS");
	cands[1] = local;
	cands[2] = "/usr/share/braindesk/assets";
	cands[3] = "assets";
	cands[4] = "../assets";
	for (int i = 0; i < 5; i++) {
		if (!cands[i] || !cands[i][0])
			continue;
		char probe[384];
		snprintf(probe, sizeof(probe), "%s/app/files.png", cands[i]);
		FILE *f = fopen(probe, "rb");
		if (f) {
			fclose(f);
			snprintf(dir, sizeof(dir), "%s", cands[i]);
			return dir;
		}
	}
	dir[0] = 0;
	return dir;
}

/* Base file names in the unified app set (design-system
 * manifest). NULL entries have no raster icon. */
static const char *app_asset_name(int app)
{
	switch (app) {
	case 0: return "settings";
	case 1: return "files";
	case 2: return "tasks";
	case 3: return "notes";
	case 4: return "clock";
	case 5: return "calc";
	case 6: return "player";
	case 7: return "recorder";
	case 8: return "web";
	case 9: return "devices";
	case 10: return "pkg";
	case 11: return "dict";
	case 12: return "terminal";
	case 13: return "home";
	case 14: return "brightness";
	case 15: return "volume";
	default: return NULL;
	}
}

/* Draw the unified-set icon for an app; false when the set
 * has no file for it (caller draws the glyph fallback). */
static bool draw_app_asset(bu_canvas *c, int app, int x, int y, int size)
{
	const char *nm = app_asset_name(app);
	if (!nm)
		return false;
	const char *dir = asset_dir();
	if (!dir[0])
		return false;
	char path[640];
	snprintf(path, sizeof(path), "%s/app/%s.png", dir, nm);
	return bu_draw_image(c, path, x, y, size, size);
}

/* Quick-launch pins on the taskbar: 設定 / ファイル / ブラウザ. */
static const int QL_APP_IDX[3] = { 0, 1, 8 };

static void draw_vol_popup(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect p = vol_popup_rect();
	bu_shadow_round_rect(c, p, 12, 8, BU_RGBA(0, 0, 0, 110));
	bu_fill_round_rect(c, p, 12, t->surface);
	bu_icon_volume(c, p.x + 24, p.y + p.h / 2, t->text_dim);
	bu_slider(c, t, vol_popup_slider(), s->volume, s->drag_slider == 3);
	char vp[8];
	snprintf(vp, sizeof(vp), "%d%%", (int)(s->volume * 100 + 0.5f));
	bu_draw_text_right(c, t->font, (bu_rect){ p.x, p.y + 16, p.w - 10, 24 }, vp, 12.5, t->text);
}

static void panel(bu_canvas *c, bu_theme *t, bd_state *s)
{
	tb_layout(t->font);
	bu_rect bar = { 0, PH - PANEL_H, PW, PANEL_H };
	bu_color pc = t->panel;
	pc.a = 235;
	bu_fill_rect(c, bar, pc);
	/* accent hairline along the top edge: accent -> accent2 */
	bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H, PW / 2, 2 }, t->accent);
	bu_fill_rect(c, (bu_rect){ PW / 2, PH - PANEL_H, PW - PW / 2, 2 }, t->accent2);

	/* centred cluster: start button */
	bu_rect sb = start_btn();
	bu_fill_round_rect(c, sb, 10, s->start_pressed ? t->accent2 : t->accent);
	draw_panes(c, sb.x + 11, sb.y + 11, 8, 2, BU_RGB(255, 255, 255));

	/* favourite pins */
	for (int i = 0; i < TB_PINS; i++) {
		bu_rect chip = ql_rect(i);
		bool active = (i == 0 && s->settings_open);
		bu_fill_round_rect(c, chip, 10, active ? t->surface3 : t->surface2);
		int isz = 26;
		if (!draw_app_asset(c, QL_APP_IDX[i], chip.x + (chip.w - isz) / 2,
				    chip.y + (chip.h - isz) / 2, isz))
			QL_ICONS[i].icon(c, chip.x + chip.w / 2, chip.y + chip.h / 2,
					 active ? t->text : t->text_dim);
		if (active)
			bu_fill_round_rect(c, (bu_rect){ chip.x + chip.w / 2 - 10, PH - 8, 20, 3 },
					   1, t->accent);
	}

	/* running apps (bui_wm draws the running indicator) */
	if (tb_task_x1 > tb_task_x0)
		bd_wm_draw_tasks(c, t, tb_task_x0, tb_task_x1, PH - PANEL_H, PANEL_H);

	/* tray ink: light on dark panels, theme text otherwise */
	int lum = (t->panel.r * 3 + t->panel.g * 5 + t->panel.b * 2) / 10;
	bu_color tray = lum < 110 ? BU_RGB(255, 255, 255) : t->text_dim;
	bu_color tray_main = lum < 110 ? BU_RGB(255, 255, 255) : t->text;
	/* quick settings (Wi-Fi), volume, battery, clock */
	bu_icon_wifi(c, PW - 185, PH - PANEL_H / 2, tray);
	bu_rect vr = vol_icon_rect();
	bu_icon_volume(c, vr.x + vr.w / 2, PH - PANEL_H / 2, s->vol_popup ? t->accent : tray);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d%%", s->battery_pct);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H, PW - 100, PANEL_H }, pct, 13, tray);
	bu_icon_battery(c, PW - 88, PH - PANEL_H / 2 - 7, s->battery_pct / 100.0f, false, tray);
	/* two-line clock: time over date */
	char dbuf[40];
	const char *sp = strchr(s->date, ' ');
	snprintf(dbuf, sizeof(dbuf), "%.*s", sp ? (int)(sp - s->date) : (int)strlen(s->date), s->date);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H + 4, PW - 10, 24 }, s->clock, 13, tray_main);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - 22, PW - 10, 18 }, dbuf, 10.5, tray);
	/* volume popup */
	if (s->vol_popup)
		draw_vol_popup(c, t, s);
}

static void draw_assist(bu_canvas *c, bu_theme *t, bd_state *s)
{
	if (!s->assist_on)
		return;
	if (s->assist_open) {
		bu_rect m = assist_menu();
		bu_shadow_round_rect(c, m, 14, 8, BU_RGBA(0, 0, 0, 110));
		bu_fill_round_rect(c, m, 14, t->surface);
		static const char *labels[] = {
			"音量 ＋", "音量 －", "輝度 ＋", "輝度 －",
			"ページ上", "ページ下", "ランチャ", "ロック",
		};
		for (int i = 0; i < 8; i++)
			bu_button(c, t, assist_item(i), labels[i], BU_BTN_NORMAL, false);
	}
	bu_rect b = assist_btn();
	bu_shadow_round_rect(c, b, 27, 6, BU_RGBA(0, 0, 0, 90));
	bu_fill_round_rect(c, b, 27, s->assist_open ? t->accent2 : t->accent);
	/* four-dot grip */
	for (int dy = 0; dy < 2; dy++)
		for (int dx = 0; dx < 2; dx++)
			bu_fill_circle(c, b.x + 21 + dx * 12, b.y + 21 + dy * 12, 3, BU_RGB(255, 255, 255));
}

/* One launcher tile: the unified-set icon (or the glyph on
 * the app's tint tile) with the 12.5 px name under it. */
static void ln_draw_tile(bu_canvas *c, bu_theme *t, int app, bu_rect cell, int isz)
{
	int cx = cell.x + cell.w / 2;
	if (!draw_app_asset(c, app, cx - isz / 2, cell.y + 2, isz)) {
		bu_fill_round_rect(c, (bu_rect){ cx - isz / 2, cell.y + 2, isz, isz }, 12, APPS[app].tint);
		APPS[app].icon(c, cx, cell.y + 2 + isz / 2, BU_RGB(255, 255, 255));
	}
	bu_draw_text_centered(c, t->font, (bu_rect){ cell.x, cell.y + isz + 6, cell.w, 16 },
			      APPS[app].name, 12.5, t->text);
}

static void draw_launcher(bu_canvas *c, bu_theme *t, bd_state *s)
{
	(void)s;
	ln_recent_load();
	bu_rect p = launcher_panel();
	bu_shadow_round_rect(c, p, 12, 10, BU_RGBA(0, 0, 0, 110));
	bu_color sf = t->surface;
	sf.a = 248;
	bu_fill_round_rect(c, p, 12, sf);
	bu_stroke_round_rect(c, p, 12, 1, t->line);

	/* search field (40 px) */
	bu_rect sr = ln_search_rect();
	bu_fill_round_rect(c, sr, 10, t->surface2);
	if (ln_search_focus)
		bu_stroke_round_rect(c, sr, 10, 1, t->accent);
	bu_icon_search(c, sr.x + 22, sr.y + sr.h / 2, t->text_faint);
	if (ln_query[0])
		bu_draw_text(c, t->font, sr.x + 40, sr.y + 10, ln_query, 14, t->text);
	else
		bu_draw_text(c, t->font, sr.x + 40, sr.y + 10, "アプリを検索", 14, t->text_faint);

	/* footer hairline */
	bu_fill_rect(c, (bu_rect){ p.x + 1, p.y + p.h - 57, p.w - 2, 1 }, t->line);
	int fcy = p.y + p.h - 28;

	if (ln_query[0]) {
		bu_draw_text(c, t->font, p.x + 56, p.y + 74, "検索結果", 13, t->text);
		int pos = 0;
		for (int i = 0; i < NAPPS && pos < 12; i++) {
			if (!ln_matches(i))
				continue;
			ln_draw_tile(c, t, i, ln_cell(pos, p.y + 96, 76), 48);
			pos++;
		}
		if (pos == 0)
			bu_draw_text(c, t->font, p.x + 56, p.y + 100, "見つかりませんでした", 13, t->text_dim);
	} else if (ln_all) {
		bu_draw_text(c, t->font, p.x + 56, p.y + 74, "アプリ", 13, t->text);
		for (int i = 0; i < 9; i++)
			ln_draw_tile(c, t, APP_CAT_APPS[i], ln_cell8(i, p.y + 96, 68), 40);
		bu_draw_text(c, t->font, p.x + 56, p.y + 246, "システム", 13, t->text);
		for (int i = 0; i < 7; i++)
			ln_draw_tile(c, t, APP_CAT_SYS[i], ln_cell8(i, p.y + 268, 68), 40);
		bu_button(c, t, ln_back_btn(), "‹ 戻る", BU_BTN_NORMAL, false);
		bu_rect pr = ln_power_rect();
		bu_icon_power(c, pr.x + pr.w / 2, fcy, t->text_dim);
	} else {
		bu_draw_text(c, t->font, p.x + 56, p.y + 74, "ピン留め", 13, t->text);
		for (int i = 0; i < 12; i++)
			ln_draw_tile(c, t, LN_PINS[i], ln_cell(i, p.y + 96, 76), 48);
		bu_draw_text(c, t->font, p.x + 56, p.y + 262, "最近使ったアプリ", 13, t->text);
		if (ln_nrecent == 0)
			bu_draw_text(c, t->font, p.x + 56, p.y + 292, "まだありません", 12.5, t->text_faint);
		for (int i = 0; i < ln_nrecent; i++)
			ln_draw_tile(c, t, ln_recent[i], ln_recent_rect(i), 40);
		/* footer: user left, すべてのアプリ + power right */
		bu_fill_circle(c, p.x + 40, fcy, 13, t->accent);
		bu_draw_text_centered(c, t->font, (bu_rect){ p.x + 27, fcy - 10, 26, 20 },
				      "U", 13, BU_RGB(255, 255, 255));
		bu_draw_text(c, t->font, p.x + 62, fcy - 10, "user", 13, t->text);
		bu_button(c, t, ln_allapps_btn(), "すべてのアプリ ›", BU_BTN_PRIMARY, false);
		bu_rect pr = ln_power_rect();
		bu_icon_power(c, pr.x + pr.w / 2, fcy, t->text_dim);
	}
}

static void draw_settings(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect win = settings_win();
	int rad = t->radius > 0 ? t->radius + 2 : 0;
	bu_shadow_round_rect(c, win, rad, 10, BU_RGBA(0, 0, 0, 120));
	bu_fill_round_rect(c, win, rad, t->surface);
	bu_rect tb = { win.x, win.y, win.w, 36 };
	bu_fill_round_rect(c, tb, rad, t->surface2);
	bu_fill_rect(c, (bu_rect){ win.x, win.y + 22, win.w, 14 }, t->surface2);
	bu_fill_rect(c, (bu_rect){ win.x, win.y + 35, win.w, 1 }, t->line);
	bu_icon_gear(c, win.x + 20, win.y + 18, t->text_dim);
	bu_draw_text(c, t->font, win.x + 36, win.y + 9, "設定", 13.5, t->text);
	/* close: a danger-red circle with a white x */
	{
		bu_rect cr = settings_close();
		bu_fill_circle(c, cr.x + cr.w / 2, cr.y + cr.h / 2, 11, t->danger);
		bu_line(c, cr.x + 7, cr.y + 7, cr.x + 17, cr.y + 17, 2, BU_RGB(255, 255, 255));
		bu_line(c, cr.x + 17, cr.y + 7, cr.x + 7, cr.y + 17, 2, BU_RGB(255, 255, 255));
	}
	bu_rect side = { win.x, win.y + 36, 160, win.h - 36 };
	bu_fill_rect(c, side, BU_RGBA(255, 255, 255, 7));
	const char *cats[] = { "画面", "テーマ", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム", "モード" };
	for (int i = 0; i < 9; i++) {
		bu_rect row = { side.x + 8, side.y + 8 + i * 32, side.w - 16, 27 };
		bu_list_row(c, t, row, cats[i], NULL, false, i == s->settings_cat);
	}
	bu_rect pane = pane_rect();
	if (s->settings_cat == 4) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "ネットワーク", 19, t->text);
		bu_rect r0 = { pane.x + 8, pane.y + 52, pane.w - 16, 42 };
		bu_draw_text(c, t->font, r0.x + 14, r0.y + 10, "Wi-Fi", 15, t->text);
		bu_toggle(c, t, r0.x + r0.w - 60, r0.y + 8, s->wifi_on);
		char conn[128];
		snprintf(conn, sizeof(conn), "接続先: %s   IP: %s", s->wifi_ssid, s->ip_addr);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 108, conn, 13, t->text_dim);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 136, "ネットワーク一覧 (タップで接続)", 13, t->text_dim);
		for (int i = 0; i < s->net_count && i < 4; i++) {
			bu_rect row = { pane.x + 8, pane.y + 162 + i * 44, pane.w - 16, 40 };
			bu_fill_round_rect(c, row, 8, t->surface2);
			bu_icon_wifi(c, row.x + 24, row.y + row.h / 2, t->text_dim);
			bu_draw_text(c, t->font, row.x + 48, row.y + 9, s->net_list[i], 14.5, t->text);
			char sig[16];
			snprintf(sig, sizeof(sig), "%d%%", s->net_signal[i]);
			bu_draw_text_right(c, t->font, (bu_rect){ row.x, row.y + 9, row.w - 14, 22 }, sig, 12.5, t->text_dim);
		}
		if (s->net_count == 0)
			bu_draw_text(c, t->font, pane.x + 22, pane.y + 170, "走査中または見つかりません", 13, t->text_faint);
		if (s->net_status[0])
			bu_draw_text(c, t->font, pane.x + 240, pane.y + 136, s->net_status, 12.5, t->warn);
		return;
	}
	if (s->settings_cat == 7) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "システム", 19, t->text);
		for (int i = 0; i < 6; i++) {
			if (!s->sysinfo[i][0])
				continue;
			bu_rect row = { pane.x + 8, pane.y + 56 + i * 46, pane.w - 16, 42 };
			bu_fill_round_rect(c, row, 8, t->surface2);
			bu_draw_text(c, t->font, row.x + 14, row.y + 10, s->sysinfo[i], 13.5, t->text);
		}
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 350, "braindesk — Brainux 用デスクトップ環境", 12.5, t->text_faint);
		return;
	}
	if (s->settings_cat == 1) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "テーマ", 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 48, "色テーマを切り替えます。レイアウトは共通で、全アプリに反映されます。", 12.5, t->text_dim);
		for (int vi = 0; vi < 6; vi++) {
			int i = s->skin_scroll + vi;
			if (i >= BD_NSKINS)
				break;
			bu_rect row = { pane.x + 8, pane.y + 76 + vi * 46, pane.w - 70, 42 };
			bu_fill_round_rect(c, row, 8, i == s->skin ? t->surface3 : t->surface2);
			bu_fill_circle(c, row.x + 26, row.y + row.h / 2, 11, BD_SKINS[i].accent);
			bu_fill_circle(c, row.x + 40, row.y + row.h / 2, 7, BD_SKINS[i].wall_bottom);
			bu_draw_text(c, t->font, row.x + 58, row.y + 10, BD_SKINS[i].name, 15, t->text);
			if (i == s->skin)
				bu_draw_text_right(c, t->font, (bu_rect){ row.x, row.y + 10, row.w - 16, 22 }, "使用中", 12.5, t->accent);
		}
		bu_button(c, t, (bu_rect){ pane.x + pane.w - 54, pane.y + 76, 46, 60 }, "▲", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ pane.x + pane.w - 54, pane.y + 292, 46, 60 }, "▼", BU_BTN_NORMAL, false);
		return;
	}
	if (s->settings_cat == 0) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "画面", 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 52, "明るさ", 13, t->text_dim);
		bu_icon_brightness(c, pane.x + 36, pane.y + 92, t->text_dim);
		bu_rect bs = brightness_slider();
		bu_slider(c, t, bs, s->brightness, s->drag_slider == 1);
		char pct[8];
		snprintf(pct, sizeof(pct), "%d%%", (int)(s->brightness * 100 + 0.5f));
		bu_draw_text_right(c, t->font, (bu_rect){ pane.x, pane.y + 78, pane.w - 22, 26 }, pct, 14, t->text);
		bu_list_row(c, t, (bu_rect){ pane.x + 8, pane.y + 118, pane.w - 16, 42 }, "画面オフまでの時間", "1分", true, false);
		bu_rect r2 = { pane.x + 8, pane.y + 160, pane.w - 16, 42 };
		bu_draw_text(c, t->font, r2.x + 14, r2.y + (r2.h - 22) / 2, "蓋を閉じたらスリープ", 15, t->text);
		bu_toggle(c, t, r2.x + r2.w - 60, r2.y + 8, s->lid_sleep);
		bu_rect pr = panel_size_row();
		bu_fill_round_rect(c, pr, 8, t->surface2);
		bu_draw_text(c, t->font, pr.x + 14, pr.y + (pr.h - 22) / 2, "パネルの大きさ", 15, t->text);
		const char *sz = PANEL_H <= 36 ? "小" : PANEL_H >= 52 ? "大" : "中";
		bu_draw_text_right(c, t->font, (bu_rect){ pr.x, pr.y + (pr.h - 22) / 2, pr.w - 14, 22 }, sz, 14, t->text_dim);
		bu_rect ar = assist_row();
		bu_fill_round_rect(c, ar, 8, t->surface2);
		bu_draw_text(c, t->font, ar.x + 14, ar.y + (ar.h - 22) / 2, "アシストボタン (片手操作)", 15, t->text);
		bu_toggle(c, t, ar.x + ar.w - 60, ar.y + 8, s->assist_on);
		return;
	}
	if (s->settings_cat == 2) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "音声", 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 52, "音量", 13, t->text_dim);
		bu_icon_volume(c, pane.x + 36, pane.y + 92, t->text_dim);
		bu_rect vs = volume_slider_for(s);
		bu_slider(c, t, vs, s->volume, s->drag_slider == 2);
		char pct[8];
		snprintf(pct, sizeof(pct), "%d%%", (int)(s->volume * 100 + 0.5f));
		bu_draw_text_right(c, t->font, (bu_rect){ pane.x, pane.y + 78, pane.w - 22, 26 }, pct, 14, t->text);
		bu_rect r3 = { pane.x + 8, pane.y + 140, pane.w - 16, 42 };
		bu_fill_round_rect(c, r3, 8, t->surface2);
		bu_draw_text(c, t->font, r3.x + 14, r3.y + (r3.h - 22) / 2, "キー音", 15, t->text);
		bu_toggle(c, t, r3.x + r3.w - 60, r3.y + 8, s->key_sound);
		return;
	}
	if (s->settings_cat == 8) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "モード", 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 48, "用途に合わせて切り替えるプロファイル。/etc/brain-modes/ に置くと自作も追加できます。", 12.5, t->text_dim);
		for (int vi = 0; vi < 4; vi++) {
			int i = s->mode_scroll + vi;
			if (i >= s->mode_count)
				break;
			bu_rect row = mode_row(vi);
			bool cur = i == mode_current(s);
			bu_fill_round_rect(c, row, 8, cur ? t->surface3 : t->surface2);
			bu_draw_text(c, t->font, row.x + 14, row.y + 5, s->mode_names[i], 15, t->text);
			bu_draw_text(c, t->font, row.x + 14, row.y + 27, s->mode_descs[i], 11.5, t->text_dim);
			if (cur)
				bu_draw_text_right(c, t->font, (bu_rect){ row.x, row.y + 5, row.w - 14, 22 }, "使用中", 12.5, t->accent);
		}
		if (s->mode_count == 0)
			bu_draw_text(c, t->font, pane.x + 22, pane.y + 90, "プロファイルが見つかりません (brain-mode list)", 13, t->text_faint);
		if (s->mode_count > 4) {
			bu_button(c, t, (bu_rect){ pane.x + pane.w - 54, pane.y + 76, 46, 60 }, "▲", BU_BTN_NORMAL, false);
			bu_button(c, t, (bu_rect){ pane.x + pane.w - 54, pane.y + 240, 46, 60 }, "▼", BU_BTN_NORMAL, false);
		}
		int cur = mode_current(s);
		if (cur >= 0 && s->mode_launch[cur][0])
			bu_button(c, t, mode_deck_btn(), s->mode_launch_label[cur][0] ? s->mode_launch_label[cur] : "アプリを開く", BU_BTN_PRIMARY, false);
		return;
	}
	if (s->settings_cat == 3) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "キーボード", 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 50,
			     s->kbd_name[0] ? s->kbd_name : "Brain マトリクスキーボード", 12.5, t->text_dim);
		bu_rect rr = kbd_repeat_row();
		bu_fill_round_rect(c, rr, 8, t->surface2);
		bu_draw_text(c, t->font, rr.x + 14, rr.y + (rr.h - 22) / 2, "キーリピート", 15, t->text);
		bu_toggle(c, t, rr.x + rr.w - 60, rr.y + 8, s->kbd_repeat);
		bu_rect kr = kbd_rate_row();
		bu_fill_round_rect(c, kr, 8, t->surface2);
		bu_draw_text(c, t->font, kr.x + 14, kr.y + (kr.h - 22) / 2, "リピートの速さ", 15,
			     s->kbd_repeat ? t->text : t->text_faint);
		static const char *rates[] = { "遅い", "普通", "速い" };
		bu_draw_text_right(c, t->font, (bu_rect){ kr.x, kr.y + (kr.h - 22) / 2, kr.w - 14, 22 },
				   rates[s->kbd_rate >= 0 && s->kbd_rate < 3 ? s->kbd_rate : 1], 14, t->text_dim);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 200, "特殊キーの割り当て (ドライバ)", 13, t->text_dim);
		static const char *combos[] = {
			"記号 + ← →   音量の上げ下げ",
			"記号 + ↑ ↓   画面の輝度",
			"記号 + Space   ミュート",
			"戻る / Home   前の画面 / ランチャ",
		};
		for (int i = 0; i < 4; i++)
			bu_draw_text(c, t->font, pane.x + 22, pane.y + 228 + i * 26, combos[i], 13, t->text);
		return;
	}
	if (s->settings_cat == 6) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "入力と日本語", 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 48, "スクリーンキーボード (OSK) の動作を決めます。全アプリ共通です。", 12.5, t->text_dim);
		bu_rect orow = osk_mode_row();
		bu_fill_round_rect(c, orow, 8, t->surface2);
		bu_draw_text(c, t->font, orow.x + 14, orow.y + (orow.h - 22) / 2, "OSK の初期モード", 15, t->text);
		bu_draw_text_right(c, t->font, (bu_rect){ orow.x, orow.y + (orow.h - 22) / 2, orow.w - 14, 22 },
				   s->osk_kana ? "かな" : "ABC", 14, t->text_dim);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 150, "かな入力: ローマ字 → ひらがな変換 (Hepburn)", 13, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 178, "X セッションの入力メソッド: fcitx + anthy", 13, t->text_dim);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 206, "物理キーボードはドライバ側で変換され、そのまま使えます。", 13, t->text_dim);
		return;
	}
	if (s->settings_cat == 5) {
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "バッテリー", 19, t->text);
		if (!s->batt_present) {
			bu_draw_text(c, t->font, pane.x + 22, pane.y + 64, "バッテリーが検出されません。", 14, t->text_dim);
			bu_draw_text(c, t->font, pane.x + 22, pane.y + 92,
				     s->ac_online ? "AC アダプタが接続されています。" : "AC アダプタも接続されていません。",
				     13, t->text_faint);
			return;
		}
		char pct[16];
		snprintf(pct, sizeof(pct), "%d%%", s->batt_pct);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 56, pct, 34, t->text);
		bu_draw_text(c, t->font, pane.x + 130, pane.y + 74, s->batt_status, 14, t->text_dim);
		bu_rect bar = { pane.x + 22, pane.y + 116, pane.w - 44, 24 };
		bu_stroke_round_rect(c, bar, 6, 2, t->line);
		bu_rect fill = { bar.x + 3, bar.y + 3, (bar.w - 6) * s->batt_pct / 100, bar.h - 6 };
		if (fill.w > 6)
			bu_fill_round_rect(c, fill, 4,
					   s->batt_pct <= 20 ? t->warn : t->accent);
		if (s->batt_line1[0])
			bu_draw_text(c, t->font, pane.x + 22, pane.y + 164, s->batt_line1, 13.5, t->text);
		if (s->batt_line2[0])
			bu_draw_text(c, t->font, pane.x + 22, pane.y + 192, s->batt_line2, 13.5, t->text_dim);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 232,
			     s->ac_online ? "AC アダプタ: 接続中" : "AC アダプタ: 未接続", 13, t->text_dim);
		return;
	}
	{
		static const char *names[] = { "画面", "テーマ", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム", "モード" };
		int ci = s->settings_cat >= 0 && s->settings_cat < 9 ? s->settings_cat : 0;
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, names[ci], 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 60, "このカテゴリは今後の更新で拡充されます。", 13, t->text_dim);
	}
}


static int eff_panel_h(bd_state *s)
{
	if (s->panel_h > 0)
		return s->panel_h;
	int ph = bd_skin_get()->panel_h;
	return ph > 0 ? ph : 48;
}

/* ================= shell overlays =================
 * Task switcher, control centre and toasts. Their state is
 * file-static (bd_state is shared with the greeter/demo, which
 * never open them); the shell drives time-based behaviour
 * through bd_scenes_tick() and posts toasts through
 * bd_toast_push(). Both are declared in shell.c. */

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ---------- toasts ---------- */
#define TOAST_MS 2500
static char toast_cur[128];
static long toast_deadline;
static char toast_queue[2][128];
static int toast_nqueue;

void bd_toast_push(const char *text)
{
	if (!text || !text[0])
		return;
	if (!toast_cur[0]) {
		snprintf(toast_cur, sizeof(toast_cur), "%s", text);
		toast_deadline = now_ms() + TOAST_MS;
	} else if (toast_nqueue < 2) {
		snprintf(toast_queue[toast_nqueue], sizeof(toast_queue[0]), "%s", text);
		toast_nqueue++;
	} else {
		snprintf(toast_queue[1], sizeof(toast_queue[0]), "%s", text);
	}
}

int bd_toast_active(void) { return toast_cur[0] != 0; }

static void toast_tick(bool *changed)
{
	if (toast_cur[0] && now_ms() >= toast_deadline) {
		if (toast_nqueue > 0) {
			snprintf(toast_cur, sizeof(toast_cur), "%s", toast_queue[0]);
			toast_nqueue--;
			if (toast_nqueue > 0)
				snprintf(toast_queue[0], sizeof(toast_queue[0]), "%s", toast_queue[1]);
			toast_deadline = now_ms() + TOAST_MS;
		} else {
			toast_cur[0] = 0;
		}
		*changed = true;
	}
}

static void draw_toast(bu_canvas *c, bu_theme *t)
{
	if (!toast_cur[0])
		return;
	int tw = bu_text_width(t->font, toast_cur, 13.5f);
	int w = tw + 48;
	if (w < 140) w = 140;
	if (w > 720) w = 720;
	int y = PH - PANEL_H - 56;
	bu_rect r = { (PW - w) / 2, y, w, 42 };
	bu_shadow_round_rect(c, r, 21, 8, BU_RGBA(0, 0, 0, 120));
	bu_color bg = t->surface;
	bg.a = 242;
	bu_fill_round_rect(c, r, 21, bg);
	bu_stroke_round_rect(c, r, 21, 1, t->line);
	bu_draw_text_centered(c, t->font, (bu_rect){ r.x, r.y + 10, r.w, 22 },
			      toast_cur, 13.5, t->text);
}

/* ---------- task switcher ---------- */
static bool sw_open;
static int sw_sel;
static struct bd_wm_info sw_items[8];
static int sw_n;

static void sw_refresh(void)
{
	sw_n = bd_wm_list(sw_items, 8);
	if (sw_sel >= sw_n)
		sw_sel = sw_n - 1;
	if (sw_sel < 0)
		sw_sel = 0;
}

void bd_switcher_open(void)
{
	sw_refresh();
	sw_sel = 0;
	for (int i = 0; i < sw_n; i++)
		if (sw_items[i].active)
			sw_sel = i;
	sw_open = true;
}

void bd_switcher_cycle(void)
{
	if (!sw_open)
		bd_switcher_open();
	sw_refresh();
	if (sw_n > 0)
		sw_sel = (sw_sel + 1) % sw_n;
	sw_open = true;
}

int bd_switcher_is_open(void) { return sw_open; }

static void sw_activate(void)
{
	if (sw_sel >= 0 && sw_sel < sw_n)
		bd_wm_raise_task(sw_items[sw_sel].idx);
	sw_open = false;
}

static bu_rect switcher_rect(void)
{
	bu_rect wa;
	bd_workarea(&wa);
	int n = sw_n > 0 ? sw_n : 1;
	int h = 44 + n * 46 + 10;
	return (bu_rect){ wa.x + (wa.w - 460) / 2, wa.y + (wa.h - h) / 2, 460, h };
}
static bu_rect switcher_row(int i)
{
	bu_rect r = switcher_rect();
	return (bu_rect){ r.x + 10, r.y + 44 + i * 46, r.w - 20, 40 };
}

static void draw_switcher(bu_canvas *c, bu_theme *t)
{
	if (!sw_open)
		return;
	sw_refresh();
	bu_rect r = switcher_rect();
	bu_shadow_round_rect(c, r, 14, 10, BU_RGBA(0, 0, 0, 130));
	bu_color sf = t->surface;
	sf.a = 248;
	bu_fill_round_rect(c, r, 14, sf);
	bu_stroke_round_rect(c, r, 14, 1, t->line);
	bu_draw_text(c, t->font, r.x + 18, r.y + 12, "ウィンドウを切り替え", 14, t->text);
	if (sw_n == 0) {
		bu_draw_text(c, t->font, r.x + 18, r.y + 56,
			     "開いているウィンドウはありません", 13, t->text_dim);
		return;
	}
	for (int i = 0; i < sw_n; i++) {
		bu_rect row = switcher_row(i);
		if (i == sw_sel) {
			bu_fill_round_rect(c, row, 8, t->surface3);
			bu_fill_round_rect(c, (bu_rect){ row.x, row.y + 6, 4, row.h - 12 }, 2, t->accent);
		}
		bu_fill_round_rect(c, (bu_rect){ row.x + 10, row.y + 6, 28, 28 }, 7,
				   sw_items[i].minimized ? t->surface2 : t->accent);
		/* first UTF-8 character of the title */
		char initial[5];
		int n = 0;
		const unsigned char *tp = (const unsigned char *)sw_items[i].title;
		if (tp[0]) {
			n = (tp[0] & 0xE0) == 0xC0 ? 2
			  : (tp[0] & 0xF0) == 0xE0 ? 3
			  : (tp[0] & 0xF8) == 0xF0 ? 4 : 1;
			for (int k = 0; k < n && tp[k]; k++)
				initial[k] = (char)tp[k];
		}
		initial[n] = 0;
		bu_draw_text_centered(c, t->font, (bu_rect){ row.x + 10, row.y + 11, 28, 18 },
				      initial, 12.5, BU_RGB(255, 255, 255));
		bu_draw_text(c, t->font, row.x + 50, row.y + 10, sw_items[i].title, 14,
			     sw_items[i].minimized ? t->text_dim : t->text);
		const char *st = sw_items[i].active ? "表示中"
			: sw_items[i].minimized ? "最小化"
			: sw_items[i].state == BD_WM_MAX ? "最大化"
			: sw_items[i].state != BD_WM_NORMAL ? "スナップ" : "";
		if (st[0])
			bu_draw_text_right(c, t->font, (bu_rect){ row.x, row.y + 11, row.w - 14, 18 },
					   st, 12, t->text_faint);
	}
}

/* ---------- control centre ---------- */
static bool cc_open;

void bd_cc_show(void) { cc_open = true; }
int bd_cc_is_open(void) { return cc_open; }

static bu_rect cc_btn_rect(void)
{
	return (bu_rect){ PW - 202, PH - PANEL_H, 34, PANEL_H };
}
static bu_rect cc_rect(void)
{
	return (bu_rect){ PW - 316, PH - PANEL_H - 252, 300, 244 };
}
static bu_rect cc_vol_slider(void) { bu_rect r = cc_rect(); return (bu_rect){ r.x + 52, r.y + 46, r.w - 110, 26 }; }
static bu_rect cc_bri_slider(void) { bu_rect r = cc_rect(); return (bu_rect){ r.x + 52, r.y + 90, r.w - 110, 26 }; }
static bu_rect cc_wifi_row(void) { bu_rect r = cc_rect(); return (bu_rect){ r.x + 10, r.y + 132, r.w - 20, 42 }; }
static bu_rect cc_mode_row(void) { bu_rect r = cc_rect(); return (bu_rect){ r.x + 10, r.y + 182, r.w - 20, 42 }; }

static void draw_cc(bu_canvas *c, bu_theme *t, bd_state *s)
{
	if (!cc_open)
		return;
	bu_rect r = cc_rect();
	bu_shadow_round_rect(c, r, 14, 10, BU_RGBA(0, 0, 0, 120));
	bu_color sf = t->surface;
	sf.a = 248;
	bu_fill_round_rect(c, r, 14, sf);
	bu_stroke_round_rect(c, r, 14, 1, t->line);
	bu_draw_text(c, t->font, r.x + 16, r.y + 10, "コントロールセンター", 13.5, t->text);
	/* volume */
	bu_icon_volume(c, r.x + 28, r.y + 59, t->text_dim);
	bu_slider(c, t, cc_vol_slider(), s->volume, s->drag_slider == 4);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d%%", (int)(s->volume * 100 + 0.5f));
	bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 48, r.w - 12, 22 }, pct, 12.5, t->text);
	/* brightness */
	bu_icon_brightness(c, r.x + 28, r.y + 103, t->text_dim);
	bu_slider(c, t, cc_bri_slider(), s->brightness, s->drag_slider == 5);
	snprintf(pct, sizeof(pct), "%d%%", (int)(s->brightness * 100 + 0.5f));
	bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 92, r.w - 12, 22 }, pct, 12.5, t->text);
	/* Wi-Fi */
	bu_rect wr = cc_wifi_row();
	bu_fill_round_rect(c, wr, 8, t->surface2);
	bu_icon_wifi(c, wr.x + 24, wr.y + wr.h / 2, t->text_dim);
	bu_draw_text(c, t->font, wr.x + 44, wr.y + (wr.h - 22) / 2, "Wi-Fi", 14, t->text);
	bu_toggle(c, t, wr.x + wr.w - 60, wr.y + 8, s->wifi_on);
	/* mode: tap cycles to the next profile */
	bu_rect mr = cc_mode_row();
	bu_fill_round_rect(c, mr, 8, t->surface2);
	bu_draw_text(c, t->font, mr.x + 14, mr.y + (mr.h - 22) / 2, "モード", 14, t->text);
	int cur = mode_current(s);
	const char *mn = cur >= 0 ? s->mode_names[cur] : (s->mode_count > 0 ? s->mode_name : "—");
	bu_draw_text_right(c, t->font, (bu_rect){ mr.x, mr.y + (mr.h - 22) / 2, mr.w - 14, 22 },
			   mn, 13.5, t->accent);
}

/* ---------- task-button long press ---------- */
static int task_press_idx = -1;
static long task_press_ms;
static int task_press_x, task_press_y;

/* Time-based overlay behaviour, driven by the shell's main
 * loop: toast expiry and the task-button long press that opens
 * the switcher. */
void bd_scenes_tick(bd_state *s, bool *changed)
{
	(void)s;
	toast_tick(changed);
	if (task_press_idx >= 0 && now_ms() - task_press_ms >= 600) {
		task_press_idx = -1;
		bd_switcher_open();
		*changed = true;
	}
}

void bd_draw_shell(bu_canvas *c, bu_theme *t, bd_state *s)
{
	PANEL_H = eff_panel_h(s);	/* taskbar height: theme value (48), s->panel_h overrides */
	/* Keep the WM's work area in sync every frame, so a skin
	 * change (bottom panel vs top bar) applies at once. */
	bu_rect wa;
	bd_workarea(&wa);
	bd_wm_set_workarea(wa.x, wa.y, wa.w, wa.h);
	wallpaper(c);
	bd_wm_draw(c, t);
	if (s->view == BD_VIEW_LAUNCHER)
		draw_launcher(c, t, s);
	if (s->settings_open)
		draw_settings(c, t, s);
	panel(c, t, s);
	draw_cc(c, t, s);
	draw_switcher(c, t);
	draw_toast(c, t);
	draw_assist(c, t, s);
}

void bd_draw_login(bu_canvas *c, bu_theme *t, bd_state *s, const char *user)
{
	wallpaper(c);
	bu_draw_text_centered(c, t->font, (bu_rect){ 0, 58, PW, 90 }, s->clock, 64, t->text);
	bu_draw_text_centered(c, t->font, (bu_rect){ 0, 138, PW, 30 }, s->date, 16, t->text_dim);
	/* One design: the login card is always shown (the greeter's
	 * hit rects are built on this geometry). */
	bu_rect card = { PW / 2 - 170, 196, 340, 216 };
	bu_card(c, t, card);
	bu_fill_circle(c, PW / 2, 240, 26, t->accent);
	{
		char initial[2] = { '?', '\0' };
		if (user && user[0])
			initial[0] = (user[0] >= 'a' && user[0] <= 'z') ? user[0] - 32 : user[0];
		bu_draw_text_centered(c, t->font, (bu_rect){ PW / 2 - 30, 222, 60, 36 }, initial, 24, BU_RGB(255, 255, 255));
	}
	bu_draw_text_centered(c, t->font, (bu_rect){ 0, 272, PW, 26 }, user, 17, t->text);
	bu_text_field(c, t, (bu_rect){ PW / 2 - 140, 306, 280, 42 }, "パスワード", "●●●●●●●", true);
	bu_button(c, t, (bu_rect){ PW / 2 - 140, 360, 280, 40 }, "ログイン", BU_BTN_PRIMARY, false);
	bu_icon_power(c, PW - 66, PH - 66, t->text_dim);
	bu_draw_text_centered(c, t->font, (bu_rect){ PW - 96, PH - 52, 60, 18 }, "電源", 11, t->text_dim);
	bu_icon_gear(c, PW - 126, PH - 66, t->text_dim);
	bu_draw_text_centered(c, t->font, (bu_rect){ PW - 156, PH - 52, 60, 18 }, "再起動", 11, t->text_dim);
	/* Title names the active colour theme. */
	char title[64];
	snprintf(title, sizeof(title), "Brainux · %s", bd_skin_get()->name);
	bu_draw_text(c, t->font, 16, PH - 30, title, 12, t->text_faint);
}

static float slider_frac(bu_rect r, int x)
{
	float f = (float)(x - r.x) / (float)r.w;
	if (f < 0) f = 0;
	if (f > 1) f = 1;
	return f;
}

int bd_handle_event(bd_state *s, int ev_type, int code, int value, int x, int y, bool *changed)
{
	PANEL_H = eff_panel_h(s);
	*changed = false;
	/* modal overlays first: the task switcher takes every tap */
	if (ev_type == BU_EV_POINTER_DOWN && sw_open) {
		sw_refresh();
		for (int i = 0; i < sw_n; i++) {
			if (in_rect(switcher_row(i), x, y)) {
				sw_sel = i;
				sw_activate();
				*changed = true;
				return BD_ACT_NONE;
			}
		}
		sw_open = false;
		*changed = true;
		return BD_ACT_NONE;
	}
	if (ev_type == BU_EV_POINTER_DOWN && cc_open) {
		if (in_rect(cc_btn_rect(), x, y)) {
			cc_open = false;
			*changed = true;
			return BD_ACT_NONE;
		}
		if (in_rect(cc_rect(), x, y)) {
			if (in_rect(cc_vol_slider(), x, y)) {
				s->drag_slider = 4;
				s->volume = slider_frac(cc_vol_slider(), x);
			} else if (in_rect(cc_bri_slider(), x, y)) {
				s->drag_slider = 5;
				s->brightness = slider_frac(cc_bri_slider(), x);
			} else if (in_rect(cc_wifi_row(), x, y)) {
				s->wifi_on = !s->wifi_on;
				bd_toast_push(s->wifi_on ? "Wi-Fi オン" : "Wi-Fi オフ");
			} else if (in_rect(cc_mode_row(), x, y)) {
				if (s->mode_count > 0)
					s->mode_set = (mode_current(s) + 1) % s->mode_count;
			}
			*changed = true;
			return BD_ACT_NONE;
		}
		cc_open = false;
		*changed = true;
		/* fall through: the tap itself still counts */
	}
	if (ev_type == BU_EV_POINTER_MOVE || ev_type == BU_EV_POINTER_UP) {
		if (bd_wm_pointer(ev_type, x, y)) {
			*changed = true;
			return BD_ACT_NONE;
		}
	}
	if (ev_type == BU_EV_POINTER_DOWN && !s->settings_open &&
	    s->view == BD_VIEW_DESKTOP && !s->assist_open) {
		/* panel task buttons: a tap raises on release; a
		 * long press (>= 600 ms, via bd_scenes_tick) opens
		 * the task switcher instead */
		{
			int tx0, tx1;
			task_area(&tx0, &tx1);
			int ti = bd_wm_task_hit(x, y, tx0, tx1, PH - PANEL_H, PANEL_H);
			if (ti >= 0) {
				task_press_idx = ti;
				task_press_ms = now_ms();
				task_press_x = x;
				task_press_y = y;
				*changed = true;
				return BD_ACT_NONE;
			}
		}
		bool on_shell_chrome = y >= PH - PANEL_H;
		if (!on_shell_chrome && bd_wm_pointer(ev_type, x, y)) {
			*changed = true;
			return BD_ACT_NONE;
		}
	}
	if (ev_type == BU_EV_KEY) {
		if (s->view == BD_VIEW_LAUNCHER && ln_search_focus && value == 1) {
			/* launcher search input: Esc clears the query,
			 * Enter ends input, other keys edit the query */
			if (code == 1 /* ESC */) {
				ln_query[0] = 0;
				ln_search_focus = false;
			} else if (code == 28 /* ENTER */) {
				ln_search_focus = false;
			} else {
				ln_query_key(code);
			}
			*changed = true;
			return BD_ACT_NONE;
		}
		if (sw_open) {
			/* switcher keys: Tab / arrows cycle, Enter
			 * activates, Esc cancels */
			if (value == 1 || value == 2) {
				sw_refresh();
				if (code == 15 /* TAB */ || code == 108 /* DOWN */) {
					if (sw_n > 0) sw_sel = (sw_sel + 1) % sw_n;
					*changed = true;
				} else if (code == 103 /* UP */) {
					if (sw_n > 0) sw_sel = (sw_sel + sw_n - 1) % sw_n;
					*changed = true;
				} else if (code == 28 /* ENTER */ && value == 1) {
					sw_activate();
					*changed = true;
				} else if (code == 1 /* ESC */ && value == 1) {
					sw_open = false;
					*changed = true;
				}
			}
			return BD_ACT_NONE;
		}
		if (value == 1 && code == 1 /* KEY_ESC */) {
			if (s->settings_open) { s->settings_open = false; *changed = true; }
			else if (s->view == BD_VIEW_LAUNCHER) { s->view = BD_VIEW_DESKTOP; ln_reset(); *changed = true; }
		}
		if (value == 1 && code == 139 /* KEY_MENU */) {
			s->view = s->view == BD_VIEW_LAUNCHER ? BD_VIEW_DESKTOP : BD_VIEW_LAUNCHER;
			*changed = true;
		}
		return BD_ACT_NONE;
	}
	if (ev_type == BU_EV_POINTER_DOWN) {
		/* assist overlay first: it floats above everything */
		if (s->assist_on) {
			if (s->assist_open) {
				static const int ACTS[] = {
					BD_ASSIST_VOL_UP, BD_ASSIST_VOL_DOWN, BD_ASSIST_BRIGHT_UP, BD_ASSIST_BRIGHT_DOWN,
					BD_ASSIST_PGUP, BD_ASSIST_PGDN, BD_ASSIST_LAUNCHER, BD_ASSIST_LOCK,
				};
				for (int i = 0; i < 8; i++) {
					if (in_rect(assist_item(i), x, y)) {
						int act = ACTS[i];
						s->assist_open = false;
						*changed = true;
						if (act == BD_ASSIST_VOL_UP) { s->volume = s->volume + 0.1f > 1 ? 1 : s->volume + 0.1f; char tb[32]; snprintf(tb, sizeof(tb), "音量 %d%%", (int)(s->volume * 100 + 0.5f)); bd_toast_push(tb); return BD_ACT_NONE; }
						if (act == BD_ASSIST_VOL_DOWN) { s->volume = s->volume - 0.1f < 0 ? 0 : s->volume - 0.1f; char tb[32]; snprintf(tb, sizeof(tb), "音量 %d%%", (int)(s->volume * 100 + 0.5f)); bd_toast_push(tb); return BD_ACT_NONE; }
						if (act == BD_ASSIST_BRIGHT_UP) { s->brightness = s->brightness + 0.125f > 1 ? 1 : s->brightness + 0.125f; char tb[32]; snprintf(tb, sizeof(tb), "明るさ %d%%", (int)(s->brightness * 100 + 0.5f)); bd_toast_push(tb); return BD_ACT_NONE; }
						if (act == BD_ASSIST_BRIGHT_DOWN) { s->brightness = s->brightness - 0.125f < 0 ? 0 : s->brightness - 0.125f; char tb[32]; snprintf(tb, sizeof(tb), "明るさ %d%%", (int)(s->brightness * 100 + 0.5f)); bd_toast_push(tb); return BD_ACT_NONE; }
						if (act == BD_ASSIST_LAUNCHER) { s->view = BD_VIEW_LAUNCHER; return BD_ACT_NONE; }
						s->assist_action = act;
						return BD_ACT_ASSIST;
					}
				}
			}
			if (in_rect(assist_btn(), x, y)) {
				s->assist_open = !s->assist_open;
				*changed = true;
				return BD_ACT_NONE;
			}
			if (s->assist_open) {
				s->assist_open = false;
				*changed = true;
				return BD_ACT_NONE;
			}
		}
		/* control centre toggle (the tray Wi-Fi icon) */
		if (in_rect(cc_btn_rect(), x, y)) {
			cc_open = !cc_open;
			if (cc_open)
				s->vol_popup = false;
			*changed = true;
			return BD_ACT_NONE;
		}
		/* volume popup */
		if (s->vol_popup) {
			if (in_rect(vol_popup_slider(), x, y)) {
				s->drag_slider = 3;
				s->volume = slider_frac(vol_popup_slider(), x);
				*changed = true;
				return BD_ACT_NONE;
			}
			if (!in_rect(vol_popup_rect(), x, y) && !in_rect(vol_icon_rect(), x, y)) {
				s->vol_popup = false;
				*changed = true;
			}
		}
		if (in_rect(vol_icon_rect(), x, y)) {
			s->vol_popup = !s->vol_popup;
			*changed = true;
			return BD_ACT_NONE;
		}
		if (in_rect(clock_rect(), x, y)) {
			ln_record(4);
			snprintf(s->launch_path, sizeof(s->launch_path), "/usr/local/bin/bd-clock");
			*changed = true;
			return BD_ACT_LAUNCH;
		}
		/* taskbar favourite pins */
		if (in_rect(ql_rect(0), x, y)) {
			s->settings_open = !s->settings_open;
			if (s->settings_open)
				ln_record(0);
			*changed = true;
			return BD_ACT_NONE;
		}
		if (in_rect(ql_rect(1), x, y)) {
			ln_record(1);
			snprintf(s->launch_path, sizeof(s->launch_path), "/usr/local/bin/bd-files");
			*changed = true;
			return BD_ACT_LAUNCH;
		}
		if (in_rect(ql_rect(2), x, y)) {
			ln_record(8);
			snprintf(s->launch_path, sizeof(s->launch_path), "/usr/local/bin/bd-web");
			*changed = true;
			return BD_ACT_LAUNCH;
		}
		if (in_rect(start_btn(), x, y)) {
			s->start_pressed = true;
			*changed = true;
			return BD_ACT_NONE;
		}
		if (s->settings_open) {
			if (in_rect(settings_close(), x, y)) { s->settings_open = false; *changed = true; return BD_ACT_NONE; }
			if (s->settings_cat == 4) {
				bu_rect pane = pane_rect();
				bu_rect r0 = { pane.x + 8, pane.y + 52, pane.w - 16, 42 };
				if (in_rect(r0, x, y)) { s->wifi_on = !s->wifi_on; bd_toast_push(s->wifi_on ? "Wi-Fi オン" : "Wi-Fi オフ"); *changed = true; return BD_ACT_NONE; }
				for (int i = 0; i < s->net_count && i < 4; i++) {
					bu_rect row = { pane.x + 8, pane.y + 162 + i * 44, pane.w - 16, 40 };
					if (in_rect(row, x, y)) { s->net_connect = i; *changed = true; return BD_ACT_NONE; }
				}
				return BD_ACT_NONE;
			}
			if (s->settings_cat == 7)
				return BD_ACT_NONE;
			if (s->settings_cat == 5)
				return BD_ACT_NONE;
			if (s->settings_cat == 3) {
				if (in_rect(kbd_repeat_row(), x, y)) { s->kbd_repeat = !s->kbd_repeat; *changed = true; return BD_ACT_NONE; }
				if (in_rect(kbd_rate_row(), x, y)) { s->kbd_rate = (s->kbd_rate + 1) % 3; *changed = true; return BD_ACT_NONE; }
				return BD_ACT_NONE;
			}
			if (s->settings_cat == 6) {
				if (in_rect(osk_mode_row(), x, y)) { s->osk_kana = !s->osk_kana; *changed = true; return BD_ACT_NONE; }
				return BD_ACT_NONE;
			}
			if (s->settings_cat == 1) {
				bu_rect pane = pane_rect();
				if (in_rect((bu_rect){ pane.x + pane.w - 54, pane.y + 76, 46, 60 }, x, y)) {
					if (s->skin_scroll > 0) s->skin_scroll -= 3;
					*changed = true;
					return BD_ACT_NONE;
				}
				if (in_rect((bu_rect){ pane.x + pane.w - 54, pane.y + 292, 46, 60 }, x, y)) {
					if (s->skin_scroll + 6 < BD_NSKINS) s->skin_scroll += 3;
					*changed = true;
					return BD_ACT_NONE;
				}
				for (int vi = 0; vi < 6; vi++) {
					int i = s->skin_scroll + vi;
					if (i >= BD_NSKINS)
						break;
					bu_rect row = { pane.x + 8, pane.y + 76 + vi * 46, pane.w - 70, 42 };
					if (in_rect(row, x, y)) { s->skin = i; *changed = true; return BD_ACT_NONE; }
				}
				return BD_ACT_NONE; /* skin page swallows other pane taps */
			}
			if (s->settings_cat == 0) {
				if (in_rect(brightness_slider(), x, y)) { s->drag_slider = 1; s->brightness = slider_frac(brightness_slider(), x); *changed = true; return BD_ACT_NONE; }
				if (in_rect(lid_toggle_hit(), x, y)) { s->lid_sleep = !s->lid_sleep; *changed = true; return BD_ACT_NONE; }
				if (in_rect(panel_size_row(), x, y)) {
					s->panel_h = s->panel_h <= 36 ? 42 : s->panel_h >= 52 ? 36 : 52;
					PANEL_H = s->panel_h;
					*changed = true;
					return BD_ACT_NONE;
				}
				if (in_rect(assist_row(), x, y)) { s->assist_on = !s->assist_on; if (!s->assist_on) s->assist_open = false; *changed = true; return BD_ACT_NONE; }
			}
			if (s->settings_cat == 2) {
				if (in_rect(volume_slider_for(s), x, y)) { s->drag_slider = 2; s->volume = slider_frac(volume_slider_for(s), x); *changed = true; return BD_ACT_NONE; }
				if (in_rect(keysound_hit_for(s), x, y)) { s->key_sound = !s->key_sound; *changed = true; return BD_ACT_NONE; }
			}

			if (s->settings_cat == 8) {
				bu_rect pane = pane_rect();
				if (s->mode_count > 4) {
					if (in_rect((bu_rect){ pane.x + pane.w - 54, pane.y + 76, 46, 60 }, x, y)) {
						if (s->mode_scroll > 0) s->mode_scroll -= 2;
						*changed = true;
						return BD_ACT_NONE;
					}
					if (in_rect((bu_rect){ pane.x + pane.w - 54, pane.y + 240, 46, 60 }, x, y)) {
						if (s->mode_scroll + 4 < s->mode_count) s->mode_scroll += 2;
						*changed = true;
						return BD_ACT_NONE;
					}
				}
				for (int vi = 0; vi < 4; vi++) {
					int i = s->mode_scroll + vi;
					if (i >= s->mode_count)
						break;
					if (in_rect(mode_row(vi), x, y)) { s->mode_set = i; *changed = true; return BD_ACT_NONE; }
				}
				int cur = mode_current(s);
				if (cur >= 0 && s->mode_launch[cur][0] && in_rect(mode_deck_btn(), x, y)) {
					snprintf(s->launch_path, sizeof(s->launch_path), "%s", s->mode_launch[cur]);
					s->settings_open = false;
					*changed = true;
					return BD_ACT_LAUNCH;
				}
				/* other taps fall through to the sidebar handling */
			}

			/* sidebar categories */
			bu_rect w = settings_win();
			for (int i = 0; i < 9; i++) {
				bu_rect row = { w.x + 8, w.y + 36 + 8 + i * 32, 144, 27 };
				if (in_rect(row, x, y)) { s->settings_cat = i; if (i == 4) s->net_scan_req++; if (i == 8) s->mode_poll_req++; *changed = true; return BD_ACT_NONE; }
			}
			return BD_ACT_NONE; /* clicks inside the window don't fall through */
		}
		if (s->view == BD_VIEW_LAUNCHER) {
			bu_rect p = launcher_panel();
			ln_recent_load();
			if (in_rect(ln_search_rect(), x, y)) {
				ln_search_focus = true;
				*changed = true;
				return BD_ACT_NONE;
			}
			if (ln_query[0]) {
				/* search results grid */
				int pos = 0;
				for (int i = 0; i < NAPPS && pos < 12; i++) {
					if (!ln_matches(i))
						continue;
					if (in_rect(ln_cell(pos, p.y + 96, 76), x, y)) {
						ln_reset();
						*changed = true;
						return ln_launch(s, i);
					}
					pos++;
				}
			} else if (ln_all) {
				if (in_rect(ln_back_btn(), x, y)) {
					ln_all = false;
					*changed = true;
					return BD_ACT_NONE;
				}
				for (int i = 0; i < 9; i++) {
					if (in_rect(ln_cell8(i, p.y + 96, 68), x, y)) {
						int app = APP_CAT_APPS[i];
						ln_reset();
						*changed = true;
						return ln_launch(s, app);
					}
				}
				for (int i = 0; i < 7; i++) {
					if (in_rect(ln_cell8(i, p.y + 268, 68), x, y)) {
						int app = APP_CAT_SYS[i];
						ln_reset();
						*changed = true;
						if (app == 14) {	/* brightness: settings, display */
							s->settings_open = true;
							s->settings_cat = 0;
							s->view = BD_VIEW_DESKTOP;
							return BD_ACT_NONE;
						}
						if (app == 15) {	/* volume: settings, audio */
							s->settings_open = true;
							s->settings_cat = 2;
							s->view = BD_VIEW_DESKTOP;
							return BD_ACT_NONE;
						}
						return ln_launch(s, app);
					}
				}
			} else {
				if (in_rect(ln_allapps_btn(), x, y)) {
					ln_all = true;
					*changed = true;
					return BD_ACT_NONE;
				}
				for (int i = 0; i < 12; i++) {
					if (in_rect(ln_cell(i, p.y + 96, 76), x, y)) {
						int app = LN_PINS[i];
						ln_reset();
						*changed = true;
						return ln_launch(s, app);
					}
				}
				for (int i = 0; i < ln_nrecent; i++) {
					if (in_rect(ln_recent_rect(i), x, y)) {
						int app = ln_recent[i];
						ln_reset();
						*changed = true;
						return ln_launch(s, app);
					}
				}
			}
			if (!in_rect(p, x, y)) {
				/* a tap outside the panel closes the launcher */
				s->view = BD_VIEW_DESKTOP;
				ln_reset();
				*changed = true;
			}
			return BD_ACT_NONE;
		}
		return BD_ACT_NONE;
	}
	if (ev_type == BU_EV_POINTER_MOVE) {
		if (task_press_idx >= 0 &&
		    (x - task_press_x > 16 || task_press_x - x > 16 ||
		     y - task_press_y > 16 || task_press_y - y > 16))
			task_press_idx = -1;	/* moved: not a long press */
		if (s->drag_slider == 1) { s->brightness = slider_frac(brightness_slider(), x); *changed = true; }
		if (s->drag_slider == 2) { s->volume = slider_frac(volume_slider_for(s), x); *changed = true; }
		if (s->drag_slider == 3) { s->volume = slider_frac(vol_popup_slider(), x); *changed = true; }
		if (s->drag_slider == 4) { s->volume = slider_frac(cc_vol_slider(), x); *changed = true; }
		if (s->drag_slider == 5) { s->brightness = slider_frac(cc_bri_slider(), x); *changed = true; }
		return BD_ACT_NONE;
	}
	if (ev_type == BU_EV_POINTER_UP) {
		if (task_press_idx >= 0) {
			/* plain task-button tap: raise on release */
			bd_wm_raise_task(task_press_idx);
			task_press_idx = -1;
			*changed = true;
		}
		if (s->start_pressed) {
			s->start_pressed = false;
			if (in_rect(start_btn(), x, y)) {
				s->view = s->view == BD_VIEW_LAUNCHER ? BD_VIEW_DESKTOP : BD_VIEW_LAUNCHER;
				if (s->view == BD_VIEW_DESKTOP)
					ln_reset();
			}
			*changed = true;
		}
		if (s->drag_slider == 2 || s->drag_slider == 3 || s->drag_slider == 4) {
			char tb[32];
			snprintf(tb, sizeof(tb), "音量 %d%%", (int)(s->volume * 100 + 0.5f));
			bd_toast_push(tb);
			*changed = true;
		}
		if (s->drag_slider == 1 || s->drag_slider == 5) {
			char tb[32];
			snprintf(tb, sizeof(tb), "明るさ %d%%", (int)(s->brightness * 100 + 0.5f));
			bd_toast_push(tb);
			*changed = true;
		}
		s->drag_slider = 0;
		return BD_ACT_NONE;
	}
	return BD_ACT_NONE;
}
