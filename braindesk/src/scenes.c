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
static int PANEL_H = 42;

/* Skin layout family: bar placement and control arrangement
 * follow it (see bui_skin.h). MAC/CLASSIC put a slim menu bar
 * on top; MAC adds a floating dock at the bottom. */
static int skin_layout(void) { return bd_skin_get()->layout; }
static bool top_bar(void) { int l = skin_layout(); return l == BD_LAYOUT_MAC || l == BD_LAYOUT_CLASSIC; }
static int topbar_h(void) { return skin_layout() == BD_LAYOUT_MAC ? 28 : 26; }

/* Desktop work area for the window manager: the screen minus the
 * bottom panel, or minus the top bar for the Mac families. The
 * macOS dock floats, so it does not shrink the area. */
void bd_workarea(bu_rect *r)
{
	if (top_bar())
		*r = (bu_rect){ 0, topbar_h(), PW, PH - topbar_h() };
	else
		*r = (bu_rect){ 0, 0, PW, PH - PANEL_H };
}
#define DOCK_W 208
#define DOCK_H 54
static bu_rect dock_rect(void) { return (bu_rect){ (PW - DOCK_W) / 2, PH - DOCK_H - 8, DOCK_W, DOCK_H }; }

static int dock_x0(void) { return bu_chrome == BU_CHROME_DOCK && !top_bar() ? (PW - 600) / 2 : 0; }
static int panel_rx(void) { return bu_chrome == BU_CHROME_DOCK && !top_bar() ? (PW - 600) / 2 + 600 : PW; }
static bu_rect start_btn(void)
{
	int l = skin_layout();
	if (l == BD_LAYOUT_MAC) {
		bu_rect d = dock_rect();
		return (bu_rect){ d.x + 8, d.y + 7, 40, 40 };
	}
	if (l == BD_LAYOUT_CLASSIC)
		return (bu_rect){ 4, 0, 30, topbar_h() };
	if (l == BD_LAYOUT_WIN11)
		return (bu_rect){ (PW - 168) / 2, PH - PANEL_H + (PANEL_H - 34) / 2, 36, 34 };
	if (bu_chrome == BU_CHROME_LUNA)
		return (bu_rect){ 0, PH - PANEL_H, 98, PANEL_H };
	if (bu_chrome == BU_CHROME_BEVEL)
		return (bu_rect){ 4, PH - PANEL_H + 3, 86, PANEL_H - 6 };
	if (bu_chrome == BU_CHROME_AERO)
		return (bu_rect){ 6, PH - PANEL_H - 6, 44, 44 };
	return (bu_rect){ dock_x0() + 8, PH - PANEL_H + (PANEL_H - 34) / 2, 34, 34 };
}
static bu_rect ql_rect(int i)
{
	int l = skin_layout();
	if (l == BD_LAYOUT_MAC) {
		bu_rect d = dock_rect();
		return (bu_rect){ d.x + 52 + i * 44, d.y + 7, 40, 40 };
	}
	if (l == BD_LAYOUT_CLASSIC)
		return (bu_rect){ 0, -100, 0, 0 };	/* no quick launch */
	if (l == BD_LAYOUT_WIN11) {
		bu_rect sb = start_btn();
		return (bu_rect){ sb.x + 44 + i * 42, sb.y, 36, 34 };
	}
	if (bu_chrome == BU_CHROME_LUNA || bu_chrome == BU_CHROME_BEVEL ||
	    bu_chrome == BU_CHROME_AERO) {
		bu_rect sb = start_btn();
		return (bu_rect){ sb.x + sb.w + 6 + i * 40, PH - PANEL_H + (PANEL_H - 32) / 2, 34, 32 };
	}
	return (bu_rect){ dock_x0() + 48 + i * 40, PH - PANEL_H + (PANEL_H - 32) / 2, 34, 32 };
}
static bu_rect vol_icon_rect(void)
{
	if (top_bar())
		return (bu_rect){ PW - 254, 0, 28, topbar_h() };
	return (bu_rect){ panel_rx() - 168, PH - PANEL_H, 34, PANEL_H };
}
static bu_rect clock_rect(void)
{
	if (top_bar())
		return (bu_rect){ PW - 184, 0, 180, topbar_h() };
	return (bu_rect){ panel_rx() - 96, PH - PANEL_H, 96, PANEL_H };
}
static bu_rect vol_popup_rect(void)
{
	if (skin_layout() == BD_LAYOUT_MAC)
		return (bu_rect){ PW - 268, PH - DOCK_H - 84, 256, 58 };
	return (bu_rect){ PW - 268, PH - PANEL_H - 66, 256, 58 };
}
static bu_rect vol_popup_slider(void) { bu_rect p = vol_popup_rect(); return (bu_rect){ p.x + 44, p.y + 16, p.w - 90, 26 }; }
static bu_rect assist_btn(void) { return (bu_rect){ PW - 72, PH - PANEL_H - 80, 54, 54 }; }
static bu_rect assist_menu(void) { return (bu_rect){ PW - 292, PH - PANEL_H - 80 - 196, 274, 188 }; }
static bu_rect assist_item(int i) { bu_rect m = assist_menu(); return (bu_rect){ m.x + 10 + (i % 2) * 130, m.y + 10 + (i / 2) * 44, 124, 38 }; }
static bu_rect settings_win(void)
{
	int l = skin_layout();
	if (l == BD_LAYOUT_MAC)
		return (bu_rect){ 36, topbar_h() + 12, PW - 72, PH - topbar_h() - 12 - DOCK_H - 20 };
	if (l == BD_LAYOUT_CLASSIC)
		return (bu_rect){ 36, topbar_h() + 12, PW - 72, PH - topbar_h() - 24 };
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
	int l = skin_layout();
	if (l == BD_LAYOUT_MAC || l == BD_LAYOUT_CLASSIC)
		return (bu_rect){ w.x + 10, w.y + 9, 18, 18 };	/* red traffic light */
	if (l >= BD_LAYOUT_WIN11)
		return (bu_rect){ w.x + w.w - 46, w.y, 46, 36 };	/* caption button */
	return (bu_rect){ w.x + w.w - 32, w.y + 6, 24, 24 };
}
static bu_rect launcher_sheet(void) { return (bu_rect){ 54, 26, PW - 108, PH - PANEL_H - 52 }; }

static bool in_rect(bu_rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

typedef void (*icon_fn)(bu_canvas *, int, int, bu_color);
struct app { const char *name; icon_fn icon; bu_color tint; const char *exec; };
static const struct app APPS[] = {
	{ "設定", bu_icon_gear, { 134, 142, 150, 255 }, NULL },
	{ "ファイル", bu_icon_folder, { 250, 176, 5, 255 }, "/usr/local/bin/bd-files" },
	{ "タスク", bu_icon_tasks, { 255, 135, 135, 255 }, "/usr/local/bin/bd-tasks" },
	{ "メモ", bu_icon_note, { 255, 212, 59, 255 }, "/usr/local/bin/bd-notes" },
	{ "時計", bu_icon_clock, { 130, 200, 255, 255 }, "/usr/local/bin/bd-clock" },
	{ "電卓", bu_icon_calc, { 116, 192, 252, 255 }, "/usr/local/bin/bd-calc" },
	{ "音楽", bu_icon_music, { 240, 101, 149, 255 }, "/usr/local/bin/bd-player" },
	{ "録音", bu_icon_mic, { 255, 110, 110, 255 }, "/usr/local/bin/bd-recorder" },
	{ "ブラウザ", bu_icon_globe, { 32, 201, 151, 255 }, "/usr/local/bin/bd-web" },
	{ "デバイス", bu_icon_chip, { 178, 140, 240, 255 }, "/usr/local/bin/bd-devices" },
	{ "パッケージ", bu_icon_box, { 165, 215, 130, 255 }, "/usr/local/bin/bd-pkg" },
	{ "辞書", bu_icon_book, { 45, 156, 219, 255 }, NULL },
	{ "端末", bu_icon_terminal, { 90, 100, 116, 255 }, NULL },
	{ "ホーム", bu_icon_home, { 129, 230, 168, 255 }, NULL },
	{ "明るさ", bu_icon_brightness, { 255, 224, 102, 255 }, NULL },
	{ "音量", bu_icon_volume, { 99, 230, 190, 255 }, NULL },
};
#define NAPPS 16

static bu_rect tile_rect(int i)
{
	bu_rect sheet = launcher_sheet();
	int cols = 6, cell_w = (sheet.w - 44) / cols, cell_h = 96;
	int col = i % cols, row = i / cols;
	int cx = sheet.x + 22 + col * cell_w + cell_w / 2;
	int cy = sheet.y + 76 + row * cell_h + 28;
	return (bu_rect){ cx - cell_w / 2, cy - 40, cell_w, cell_h - 8 };
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
	s->panel_h = 42;
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


/* ---------- raster icon assets (real artwork, size-optimized) ---------- */
static const char *asset_dir(void)
{
	static char dir[256];
	static int done;
	if (done) return dir;
	done = 1;
	const char *cands[4];
	cands[0] = getenv("BRAINDESK_ASSETS");
	cands[1] = "/usr/share/braindesk/assets";
	cands[2] = "assets";
	cands[3] = "../assets";
	for (int i = 0; i < 4; i++) {
		if (!cands[i]) continue;
		char probe[320];
		snprintf(probe, sizeof(probe), "%s/mac/files.png", cands[i]);
		FILE *f = fopen(probe, "rb");
		if (f) { fclose(f); snprintf(dir, sizeof(dir), "%s", cands[i]); return dir; }
	}
	dir[0] = 0;
	return dir;
}
static const char *asset_fam(void)
{
	int l = skin_layout();
	if (l == BD_LAYOUT_MAC) return "mac";
	if (l == BD_LAYOUT_WIN11) return "win11";
	if (bu_chrome == BU_CHROME_LUNA) return "xp";
	return NULL;
}
static const char *app_asset_name(int idx)
{
	switch (idx) {
	case 0: return "settings"; case 1: return "files"; case 2: return "tasks";
	case 3: return "notes"; case 4: return "clock"; case 5: return "calc";
	case 6: return "music"; case 7: return "recorder"; case 8: return "browser";
	case 10: return "pkg"; case 12: return "terminal";
	}
	return NULL;
}
static int draw_named_asset(bu_canvas *c, const char *fam, const char *name, int x, int y, int sz)
{
	const char *dir = asset_dir();
	if (!dir[0]) return 0;
	char path[320];
	snprintf(path, sizeof(path), "%s/%s/%s.png", dir, fam, name);
	return bu_draw_image(c, path, x, y, sz, sz);
}
static int draw_app_asset(bu_canvas *c, int idx, int x, int y, int sz)
{
	const char *fam = asset_fam();
	const char *name = app_asset_name(idx);
	if (!fam || !name) return 0;
	if (!strcmp(fam, "xp")) return 0; /* XP set covers start/browser/files only */
	return draw_named_asset(c, fam, name, x, y, sz);
}
static const int QL_APP_IDX[3] = { 0, 1, 8 };

/* Apple-ish mark for the Mac-family menu bar, drawn from
 * circles: two-lobed body, leaf, and a bite taken out of the
 * right side in the bar colour. */
static void draw_apple(bu_canvas *c, int cx, int cy, bu_color col, bu_color bar)
{
	bu_fill_circle(c, cx - 4, cy + 2, 7, col);
	bu_fill_circle(c, cx + 4, cy + 2, 7, col);
	bu_fill_circle(c, cx, cy - 2, 6, col);
	bu_fill_circle(c, cx + 3, cy - 10, 3, col);	/* leaf */
	bu_fill_circle(c, cx + 10, cy + 1, 4, bar);	/* bite */
}

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

/* Mac-family top menu bar: mark + app name on the left,
 * status icons and date/time on the right. */
static void draw_top_bar(bu_canvas *c, bu_theme *t, bd_state *s, int l)
{
	int h = topbar_h();
	bu_color pc = t->panel;
	if (l == BD_LAYOUT_MAC)
		pc.a = 228;
	bu_fill_rect(c, (bu_rect){ 0, 0, PW, h }, pc);
	bu_fill_rect(c, (bu_rect){ 0, h - 1, PW, 1 }, l == BD_LAYOUT_CLASSIC ? BU_RGB(0, 0, 0) : t->line);
	if (!draw_named_asset(c, "mac", "apple", 9, h / 2 - 10, 20))
		draw_apple(c, 20, h / 2, t->text, pc);
	bu_draw_text(c, t->font, 38, (h - 18) / 2, "braindesk", 13, t->text);
	/* right side: wifi, volume, battery %, battery, then the
	 * date/time as the rightmost item (macOS order) */
	bu_icon_wifi(c, PW - 266, h / 2, t->text_dim);
	bu_rect vr = vol_icon_rect();
	bu_icon_volume(c, vr.x + vr.w / 2, h / 2, s->vol_popup ? t->accent : t->text_dim);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d%%", s->battery_pct);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, (h - 16) / 2, PW - 200, 16 }, pct, 11.5, t->text_dim);
	bu_icon_battery(c, PW - 196, h / 2 - 6, s->battery_pct / 100.0f, false, t->text_dim);
	char clk[64];
	snprintf(clk, sizeof(clk), "%s  %s", s->date, s->clock);
	bu_draw_text_right(c, t->font, (bu_rect){ PW - 168, (h - 17) / 2, 160, 17 }, clk, 12, t->text);
}

/* macOS-style floating dock: launcher tile + quick launch. */
static void draw_dock(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect d = dock_rect();
	bu_shadow_round_rect(c, d, 16, 8, BU_RGBA(0, 0, 0, 100));
	bu_color pc = t->surface;
	pc.a = 235;
	bu_fill_round_rect(c, d, 16, pc);
	bu_stroke_round_rect(c, d, 16, 1, t->line);
	bu_rect sb = start_btn();
	bu_fill_round_rect(c, sb, 10, t->surface2);
	if (s->start_pressed)
		bu_fill_round_rect(c, sb, 10, t->surface3);
	if (!draw_named_asset(c, "mac", "launchpad", sb.x + 2, sb.y + 2, 30)) {
		for (int gy = 0; gy < 3; gy++)
			for (int gx = 0; gx < 3; gx++)
				bu_fill_round_rect(c, (bu_rect){ sb.x + 8 + gx * 9, sb.y + 8 + gy * 9, 6, 6 }, 2, t->text_dim);
	}
	for (int i = 0; i < 3; i++) {
		bu_rect chip = ql_rect(i);
		bool active = (i == 0 && s->settings_open);
		if (active)
			bu_fill_round_rect(c, chip, 10, t->surface3);
		if (!draw_app_asset(c, QL_APP_IDX[i], chip.x + 3, chip.y + 3, 28))
			QL_ICONS[i].icon(c, chip.x + chip.w / 2, chip.y + chip.h / 2, QL_ICONS[i].tint);
		if (active)
			bu_fill_circle(c, chip.x + chip.w / 2, d.y + d.h - 4, 2, t->text_dim);
	}
}


/* Panel task-button area (running WM windows). */
static void task_area(int *x0, int *x1)
{
	if (skin_layout() == BD_LAYOUT_WIN11) {
		*x0 = PW / 2 + 118;
	} else {
		bu_rect q2 = ql_rect(2);
		*x0 = q2.x + q2.w + 12;
	}
	*x1 = panel_rx() - 208;
}

static void panel(bu_canvas *c, bu_theme *t, bd_state *s)
{
	int RX = panel_rx();
	int l = skin_layout();
	if (l == BD_LAYOUT_MAC || l == BD_LAYOUT_CLASSIC) {
		draw_top_bar(c, t, s, l);
		if (l == BD_LAYOUT_MAC)
			draw_dock(c, t, s);
		if (s->vol_popup)
			draw_vol_popup(c, t, s);
		return;
	}
	bu_rect bar = { 0, PH - PANEL_H, PW, PANEL_H };
	if (bu_chrome == BU_CHROME_DOCK) {
		bu_rect dock = { dock_x0(), PH - PANEL_H - 4, 600, PANEL_H };
		bu_shadow_round_rect(c, dock, 18, 8, BU_RGBA(0, 0, 0, 90));
		bu_color pc = t->panel; pc.a = 225;
		bu_fill_round_rect(c, dock, 18, pc);
	} else if (bu_chrome == BU_CHROME_BEVEL) {
		bu_fill_rect(c, bar, t->panel);
		bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H, PW, 1 }, BU_RGB(255, 255, 255));
		bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H + 1, PW, 1 }, BU_RGB(223, 223, 223));
	} else if (bu_chrome == BU_CHROME_LUNA) {
		bu_color deep = t->accent;
		deep.r = deep.r * 55 / 100; deep.g = deep.g * 55 / 100; deep.b = deep.b * 72 / 100;
		bu_gradient_v(c, bar, t->accent, deep);
		bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H, PW, 1 }, BU_RGBA(255, 255, 255, 90));
	} else if (bu_chrome == BU_CHROME_AERO) {
		bu_color deep = t->panel;
		deep.r = deep.r / 3; deep.g = deep.g / 3; deep.b = deep.b / 3;
		bu_gradient_v(c, bar, t->panel, deep);
		bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H, PW, 1 }, BU_RGBA(255, 255, 255, 60));
	} else {
		bu_color pc = t->panel; pc.a = 235;
		bu_fill_rect(c, bar, pc);
		if (skin_layout() == BD_LAYOUT_BD) {
			/* accent hairline: accent -> accent2 across the panel top */
			bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H, PW / 2, 2 }, t->accent);
			bu_fill_rect(c, (bu_rect){ PW / 2, PH - PANEL_H, PW - PW / 2, 2 }, t->accent2);
		}
	}
	bu_rect sb = start_btn();
	if (bu_chrome == BU_CHROME_BEVEL) {
		bu_fill_rect(c, sb, t->surface2);
		bu_bevel_rect(c, sb, !s->start_pressed);
		bu_color ink = t->text;
		bu_fill_rect(c, (bu_rect){ sb.x + 34000, sb.y + 10, 6, 6 }, ink);
		bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 10, 6, 6 }, ink);
		bu_fill_rect(c, (bu_rect){ sb.x + 10, sb.y + 18, 6, 6 }, ink);
		bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 18, 6, 6 }, ink);
		bu_draw_text(c, t->font, sb.x + 32, sb.y + (sb.h - 19) / 2, "Start", 14, t->text);
		draw_named_asset(c, "xp", "start", sb.x + 4, sb.y + 3, 28);
	} else if (bu_chrome == BU_CHROME_LUNA) {
		bu_fill_round_rect(c, sb, 10, s->start_pressed ? t->accent : t->accent2);
		bu_fill_rect(c, (bu_rect){ sb.x + 34000, sb.y + 13, 6, 6 }, BU_RGB(255, 255, 255));
		bu_fill_rect(c, (bu_rect){ sb.x + 22, sb.y + 13, 6, 6 }, BU_RGBA(255, 255, 255, 200));
		bu_fill_rect(c, (bu_rect){ sb.x + 14, sb.y + 21, 6, 6 }, BU_RGBA(255, 255, 255, 200));
		bu_fill_rect(c, (bu_rect){ sb.x + 22, sb.y + 21, 6, 6 }, BU_RGBA(255, 255, 255, 150));
		if (!draw_named_asset(c, "xp", "start", sb.x + 5, sb.y + 3, 28))
			;
		bu_draw_text(c, t->font, sb.x + 38, sb.y + (sb.h - 21) / 2, "start", 16, BU_RGB(255, 255, 255));
	} else if (skin_layout() == BD_LAYOUT_WIN11) {
		if (s->start_pressed)
			bu_fill_round_rect(c, sb, 6, t->surface3);
		if (!draw_named_asset(c, "win11", "start", sb.x + 6, sb.y + 6, 22))
			draw_panes(c, sb.x + 9, sb.y + 8, 8, 2, t->accent);
	} else if (bu_chrome == BU_CHROME_METRO) {
		/* Win8/10: bare white panes, no button box */
		if (s->start_pressed)
			bu_fill_rect(c, sb, t->surface3);
		draw_panes(c, sb.x + 9, sb.y + 9, 7, 2, BU_RGB(255, 255, 255));
	} else if (bu_chrome == BU_CHROME_AERO) {
		bu_fill_circle(c, sb.x + sb.w / 2, sb.y + sb.h / 2, 16, t->accent);
		bu_fill_circle(c, sb.x + sb.w / 2, sb.y + sb.h / 2, 12, t->accent2);
		bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 11, 5, 5 }, BU_RGB(255, 255, 255));
		bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 11, 5, 5 }, BU_RGBA(255, 255, 255, 200));
		bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 200));
		bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 150));
	} else {
		bu_fill_round_rect(c, sb, 9, s->start_pressed ? t->accent2 : t->accent);
	}
	if (bu_chrome != BU_CHROME_BEVEL && bu_chrome != BU_CHROME_LUNA && bu_chrome != BU_CHROME_AERO &&
	    bu_chrome != BU_CHROME_METRO && skin_layout() != BD_LAYOUT_WIN11) {
		bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 11, 5, 5 }, BU_RGB(255, 255, 255));
		bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 11, 5, 5 }, BU_RGBA(255, 255, 255, 200));
		bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 200));
		bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 150));
	}
	/* quick launch: settings (toggle), files, browser */
	for (int i = 0; i < 3; i++) {
		bu_rect chip = ql_rect(i);
		bool active = (i == 0 && s->settings_open);
		if (bu_chrome == BU_CHROME_BEVEL) {
			bu_fill_rect(c, chip, t->surface2);
			bu_bevel_rect(c, chip, !active);
		} else if (skin_layout() == BD_LAYOUT_WIN11) {
			if (active)
				bu_fill_round_rect(c, chip, 6, t->surface3);
		} else if (bu_chrome == BU_CHROME_LUNA) {
			if (active)
				bu_fill_round_rect(c, chip, 6, BU_RGBA(255, 255, 255, 40));
		} else {
			bu_fill_round_rect(c, chip, 8, active ? t->surface3 : t->surface);
		}
		int drawn = draw_app_asset(c, QL_APP_IDX[i], chip.x + 4, chip.y + 3, 26);
		if (!drawn && bu_chrome == BU_CHROME_LUNA && QL_APP_IDX[i] == 1)
			drawn = draw_named_asset(c, "xp", "files", chip.x + 4, chip.y + 3, 26);
		if (!drawn && bu_chrome == BU_CHROME_LUNA && QL_APP_IDX[i] == 8)
			drawn = draw_named_asset(c, "xp", "browser", chip.x + 4, chip.y + 3, 26);
		if (!drawn)
			QL_ICONS[i].icon(c, chip.x + chip.w / 2, chip.y + chip.h / 2,
					 bu_chrome == BU_CHROME_LUNA ? BU_RGB(255, 255, 255)
								     : active ? t->text : t->text_dim);
		if (active)
			bu_fill_round_rect(c, (bu_rect){ chip.x + chip.w / 2 - 10, PH - 8, 20, 3 }, 1, t->accent);
	}
	{
		int tx0, tx1;
		task_area(&tx0, &tx1);
		if (tx1 > tx0)
			bd_wm_draw_tasks(c, t, tx0, tx1, PH - PANEL_H, PANEL_H);
	}
	/* tray ink: light on dark panels (XP/Win8), theme text otherwise */
	int lum = (t->panel.r * 3 + t->panel.g * 5 + t->panel.b * 2) / 10;
	bu_color tray = lum < 110 ? BU_RGB(255, 255, 255) : t->text_dim;
	bu_color tray_main = lum < 110 ? BU_RGB(255, 255, 255) : t->text;
	bu_icon_wifi(c, RX - 186, PH - PANEL_H / 2, tray);
	bu_rect vr = vol_icon_rect();
	bu_icon_volume(c, vr.x + vr.w / 2, PH - PANEL_H / 2, s->vol_popup ? t->accent : tray);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d%%", s->battery_pct);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H, RX - 100, PANEL_H }, pct, 13, tray);
	bu_icon_battery(c, RX - 96, PH - PANEL_H / 2 - 7, s->battery_pct / 100.0f, false, tray);
	if (PANEL_H >= 52 || skin_layout() == BD_LAYOUT_WIN11) {
		/* two-line clock: time over date */
		char dbuf[40];
		const char *sp = strchr(s->date, ' ');
		snprintf(dbuf, sizeof(dbuf), "%.*s", sp ? (int)(sp - s->date) : (int)strlen(s->date), s->date);
		bool w11 = skin_layout() == BD_LAYOUT_WIN11;
		bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H + 4, RX - 10, 24 }, s->clock, w11 ? 12.5 : 16, tray_main);
		bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - 22, RX - 10, 18 }, dbuf, 10.5, tray);
	} else {
		bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H, RX - 10, PANEL_H }, s->clock, 14, tray_main);
	}
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

static void draw_launcher(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect sheet = launcher_sheet();
	int srad = t->radius > 0 ? t->radius + 6 : 18;
	bu_shadow_round_rect(c, sheet, srad, 10, BU_RGBA(0, 0, 0, 110));
	bu_color sf = t->surface;
	sf.a = 248;
	bu_fill_round_rect(c, sheet, srad, sf);
	bu_rect search = { sheet.x + 22, sheet.y + 18, sheet.w - 44, 38 };
	bu_fill_round_rect(c, search, 10, t->surface2);
	bu_icon_search(c, search.x + 22, search.y + 19, t->text_faint);
	bu_draw_text(c, t->font, search.x + 40, search.y + 8, "アプリを検索", 14, t->text_faint);
	for (int i = 0; i < NAPPS; i++) {
		bu_rect tr = tile_rect(i);
		int cx = tr.x + tr.w / 2, cy = tr.y + 40;
		if (!draw_app_asset(c, i, cx - 24, cy - 24, 48)) {
			bu_fill_round_rect(c, (bu_rect){ cx - 23, cy - 23, 46, 46 }, 12, APPS[i].tint);
			APPS[i].icon(c, cx, cy, BU_RGB(255, 255, 255));
		}
		bu_draw_text_centered(c, t->font, (bu_rect){ tr.x, cy + 28, tr.w, 20 }, APPS[i].name, 12.5, t->text);
	}
	bu_fill_circle(c, PW / 2 - 10, sheet.y + sheet.h - 18, 3, t->text);
	bu_fill_circle(c, PW / 2 + 4, sheet.y + sheet.h - 18, 3, t->text_faint);
	(void)s;
}

static void draw_settings(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect win = settings_win();
	int rad = t->radius > 0 ? t->radius + 2 : 0;
	bu_shadow_round_rect(c, win, rad, 10, BU_RGBA(0, 0, 0, 120));
	bu_fill_round_rect(c, win, rad, t->surface);
	bu_rect tb = { win.x, win.y, win.w, 36 };
	bool bevel = bu_chrome == BU_CHROME_BEVEL && skin_layout() != BD_LAYOUT_CLASSIC;
	bool luna = bu_chrome == BU_CHROME_LUNA;
	if (bevel || luna) {
		/* classic caption: coloured gradient, white text */
		bu_color bot = t->accent;
		bot.g = bot.g + 90 > 255 ? 255 : bot.g + 90;
		bot.b = bot.b + 120 > 255 ? 255 : bot.b + 120;
		if (luna) {
			bot = t->accent;
			bot.r = bot.r * 70 / 100; bot.g = bot.g * 70 / 100; bot.b = bot.b * 80 / 100;
		}
		bu_gradient_round_v(c, tb, rad, t->accent, bot);
		bu_fill_rect(c, (bu_rect){ win.x, win.y + 22, win.w, 14 }, bot);
	} else {
		bu_fill_round_rect(c, tb, rad, t->surface2);
		bu_fill_rect(c, (bu_rect){ win.x, win.y + 22, win.w, 14 }, t->surface2);
		bu_fill_rect(c, (bu_rect){ win.x, win.y + 35, win.w, 1 }, t->line);
	}
	int l = skin_layout();
	if (l == BD_LAYOUT_MAC) {
		/* traffic lights on the left, title centred */
		bu_fill_circle(c, win.x + 19, win.y + 18, 6, BU_RGB(255, 95, 87));
		bu_fill_circle(c, win.x + 39, win.y + 18, 6, BU_RGB(254, 188, 46));
		bu_fill_circle(c, win.x + 59, win.y + 18, 6, BU_RGB(40, 200, 64));
		bu_draw_text_centered(c, t->font, (bu_rect){ win.x, win.y + 8, win.w, 20 }, "設定", 13.5, t->text);
	} else if (l == BD_LAYOUT_CLASSIC) {
		/* pinstriped caption, close box on the left */
		for (int i = 0; i < 3; i++)
			bu_fill_rect(c, (bu_rect){ win.x + 70, win.y + 10 + i * 6, win.w - 140, 1 }, t->line);
		bu_stroke_round_rect(c, (bu_rect){ win.x + 14, win.y + 13, 10, 10 }, 0, 1, t->text_dim);
		bu_draw_text_centered(c, t->font, (bu_rect){ win.x, win.y + 8, win.w, 20 }, "設定", 13.5, t->text);
	} else if (l >= BD_LAYOUT_WIN11) {
		/* caption glyphs on the right: min / max / close */
		bu_color ink = (bevel || luna) ? BU_RGB(255, 255, 255) : t->text;
		bu_color inkd = (bevel || luna) ? BU_RGB(255, 255, 255) : t->text_dim;
		bu_icon_gear(c, win.x + 20, win.y + 18, inkd);
		bu_draw_text(c, t->font, win.x + 36, win.y + 9, "設定", 13.5, ink);
		int cx0 = win.x + win.w - 46;
		bu_line(c, cx0 - 82, win.y + 18, cx0 - 72, win.y + 18, 1, inkd);
		bu_stroke_round_rect(c, (bu_rect){ cx0 - 41, win.y + 13, 10, 10 }, 0, 1, inkd);
		if (luna) {
			/* Luna close: red-orange rounded box */
			bu_fill_round_rect(c, (bu_rect){ cx0 + 8, win.y + 7, 22, 22 }, 4, BU_RGB(232, 108, 84));
			bu_line(c, cx0 + 14, win.y + 13, cx0 + 24, win.y + 23, 2, BU_RGB(255, 255, 255));
			bu_line(c, cx0 + 24, win.y + 13, cx0 + 14, win.y + 23, 2, BU_RGB(255, 255, 255));
		} else {
			bu_line(c, cx0 + 14, win.y + 13, cx0 + 24, win.y + 23, 1, ink);
			bu_line(c, cx0 + 24, win.y + 13, cx0 + 14, win.y + 23, 1, ink);
		}
	} else {
		bu_icon_gear(c, win.x + 20, win.y + 18, t->text_dim);
		bu_draw_text(c, t->font, win.x + 36, win.y + 9, "設定", 13.5, t->text);
		bu_fill_circle(c, win.x + win.w - 20, win.y + 18, 6, t->danger);
		bu_fill_circle(c, win.x + win.w - 40, win.y + 18, 6, t->warn);
		bu_fill_circle(c, win.x + win.w - 60, win.y + 18, 6, t->ok);
	}
	bu_rect side = { win.x, win.y + 36, 160, win.h - 36 };
	bu_fill_rect(c, side, BU_RGBA(255, 255, 255, 7));
	const char *cats[] = { "画面", "スキン", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム", "モード" };
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
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, "スキン", 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 48, "テーマと壁紙の配色を切り替えます。全アプリに反映されます。", 12.5, t->text_dim);
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
		const char *sz = s->panel_h <= 36 ? "小" : s->panel_h >= 52 ? "大" : "中";
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
		static const char *names[] = { "画面", "スキン", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム", "モード" };
		int ci = s->settings_cat >= 0 && s->settings_cat < 9 ? s->settings_cat : 0;
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, names[ci], 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 60, "このカテゴリは今後の更新で拡充されます。", 13, t->text_dim);
	}
}

void bd_draw_shell(bu_canvas *c, bu_theme *t, bd_state *s)
{
	PANEL_H = s->panel_h > 0 ? s->panel_h : 42;
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
	draw_assist(c, t, s);
}

void bd_draw_login(bu_canvas *c, bu_theme *t, bd_state *s, const char *user)
{
	wallpaper(c);
	bu_draw_text_centered(c, t->font, (bu_rect){ 0, 58, PW, 90 }, s->clock, 64, t->text);
	bu_draw_text_centered(c, t->font, (bu_rect){ 0, 138, PW, 30 }, s->date, 16, t->text_dim);
	/* The login card follows the skin family: macOS, Classic Mac and
	 * Windows 11 show the user directly on the wallpaper, like the
	 * originals; the braindesk and Windows families keep a panel card. */
	int l = skin_layout();
	if (l == BD_LAYOUT_BD || l == BD_LAYOUT_WIN) {
		bu_rect card = { PW / 2 - 170, 196, 340, 216 };
		bu_card(c, t, card);
	}
	/* Avatar: a square user tile with a white frame on the Windows
	 * families (XP-style), a circle everywhere else. Geometry is
	 * unchanged, so the greeter's hit rects still apply. */
	if (l == BD_LAYOUT_WIN) {
		bu_fill_rect(c, (bu_rect){ PW / 2 - 28, 212, 56, 56 }, BU_RGB(255, 255, 255));
		bu_fill_rect(c, (bu_rect){ PW / 2 - 26, 214, 52, 52 }, t->accent);
	} else {
		bu_fill_circle(c, PW / 2, 240, 26, t->accent);
	}
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
	/* Title names the active skin, so the greeter shows which family
	 * is selected. */
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
	PANEL_H = s->panel_h > 0 ? s->panel_h : 42;
	*changed = false;
	if (ev_type == BU_EV_POINTER_MOVE || ev_type == BU_EV_POINTER_UP) {
		if (bd_wm_pointer(ev_type, x, y)) {
			*changed = true;
			return BD_ACT_NONE;
		}
	}
	if (ev_type == BU_EV_POINTER_DOWN && !s->settings_open &&
	    s->view == BD_VIEW_DESKTOP && !s->assist_open) {
		/* panel task buttons raise windows */
		{
			int tx0, tx1;
			task_area(&tx0, &tx1);
			int ti = bd_wm_task_hit(x, y, tx0, tx1, PH - PANEL_H, PANEL_H);
			if (ti >= 0) {
				bd_wm_raise_task(ti);
				*changed = true;
				return BD_ACT_NONE;
			}
		}
		bool on_shell_chrome;
		if (top_bar())
			on_shell_chrome = y < topbar_h() ||
				(skin_layout() == BD_LAYOUT_MAC && in_rect(dock_rect(), x, y));
		else
			on_shell_chrome = y >= PH - PANEL_H;
		if (!on_shell_chrome && bd_wm_pointer(ev_type, x, y)) {
			*changed = true;
			return BD_ACT_NONE;
		}
	}
	if (ev_type == BU_EV_KEY) {
		if (value == 1 && code == 1 /* KEY_ESC */) {
			if (s->settings_open) { s->settings_open = false; *changed = true; }
			else if (s->view == BD_VIEW_LAUNCHER) { s->view = BD_VIEW_DESKTOP; *changed = true; }
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
						if (act == BD_ASSIST_VOL_UP) { s->volume = s->volume + 0.1f > 1 ? 1 : s->volume + 0.1f; return BD_ACT_NONE; }
						if (act == BD_ASSIST_VOL_DOWN) { s->volume = s->volume - 0.1f < 0 ? 0 : s->volume - 0.1f; return BD_ACT_NONE; }
						if (act == BD_ASSIST_BRIGHT_UP) { s->brightness = s->brightness + 0.125f > 1 ? 1 : s->brightness + 0.125f; return BD_ACT_NONE; }
						if (act == BD_ASSIST_BRIGHT_DOWN) { s->brightness = s->brightness - 0.125f < 0 ? 0 : s->brightness - 0.125f; return BD_ACT_NONE; }
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
			snprintf(s->launch_path, sizeof(s->launch_path), "/usr/local/bin/bd-clock");
			*changed = true;
			return BD_ACT_LAUNCH;
		}
		/* quick launch */
		if (in_rect(ql_rect(0), x, y)) {
			s->settings_open = !s->settings_open;
			*changed = true;
			return BD_ACT_NONE;
		}
		if (in_rect(ql_rect(1), x, y)) {
			snprintf(s->launch_path, sizeof(s->launch_path), "/usr/local/bin/bd-files");
			*changed = true;
			return BD_ACT_LAUNCH;
		}
		if (in_rect(ql_rect(2), x, y)) {
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
				if (in_rect(r0, x, y)) { s->wifi_on = !s->wifi_on; *changed = true; return BD_ACT_NONE; }
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
			for (int i = 0; i < NAPPS; i++) {
				if (in_rect(tile_rect(i), x, y)) {
					if (strcmp(APPS[i].name, "設定") == 0)
						s->settings_open = true;
					s->view = BD_VIEW_DESKTOP;
					*changed = true;
					if (APPS[i].exec) {
						snprintf(s->launch_path, sizeof(s->launch_path), "%s", APPS[i].exec);
						return BD_ACT_LAUNCH;
					}
					return BD_ACT_NONE;
				}
			}
			if (!in_rect(launcher_sheet(), x, y)) {
				s->view = BD_VIEW_DESKTOP;
				*changed = true;
			}
			return BD_ACT_NONE;
		}
		return BD_ACT_NONE;
	}
	if (ev_type == BU_EV_POINTER_MOVE) {
		if (s->drag_slider == 1) { s->brightness = slider_frac(brightness_slider(), x); *changed = true; }
		if (s->drag_slider == 2) { s->volume = slider_frac(volume_slider_for(s), x); *changed = true; }
		if (s->drag_slider == 3) { s->volume = slider_frac(vol_popup_slider(), x); *changed = true; }
		return BD_ACT_NONE;
	}
	if (ev_type == BU_EV_POINTER_UP) {
		if (s->start_pressed) {
			s->start_pressed = false;
			if (in_rect(start_btn(), x, y))
				s->view = s->view == BD_VIEW_LAUNCHER ? BD_VIEW_DESKTOP : BD_VIEW_LAUNCHER;
			*changed = true;
		}
		s->drag_slider = 0;
		return BD_ACT_NONE;
	}
	return BD_ACT_NONE;
}
