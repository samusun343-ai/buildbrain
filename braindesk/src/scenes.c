#include "scenes.h"
#include "bui_skin.h"
#include <time.h>
#include "bui_input.h"
#include <string.h>
#include <stdio.h>

#define PW 854
#define PH 480
static int PANEL_H = 46;

static bu_rect start_btn(void) { return (bu_rect){ 8, PH - PANEL_H + (PANEL_H - 34) / 2, 34, 34 }; }
static bu_rect ql_rect(int i) { return (bu_rect){ 52 + i * 50, PH - PANEL_H + (PANEL_H - 36) / 2, 44, 36 }; }
static bu_rect vol_icon_rect(void) { return (bu_rect){ PW - 168, PH - PANEL_H, 34, PANEL_H }; }
static bu_rect clock_rect(void) { return (bu_rect){ PW - 96, PH - PANEL_H, 96, PANEL_H }; }
static bu_rect vol_popup_rect(void) { return (bu_rect){ PW - 268, PH - PANEL_H - 66, 256, 58 }; }
static bu_rect vol_popup_slider(void) { bu_rect p = vol_popup_rect(); return (bu_rect){ p.x + 44, p.y + 16, p.w - 90, 26 }; }
static bu_rect assist_btn(void) { return (bu_rect){ PW - 72, PH - PANEL_H - 80, 54, 54 }; }
static bu_rect assist_menu(void) { return (bu_rect){ PW - 292, PH - PANEL_H - 80 - 196, 274, 188 }; }
static bu_rect assist_item(int i) { bu_rect m = assist_menu(); return (bu_rect){ m.x + 10 + (i % 2) * 130, m.y + 10 + (i / 2) * 44, 124, 38 }; }
static bu_rect settings_win(void) { return (bu_rect){ 44, 24, PW - 88, PH - PANEL_H - 40 }; }
static bu_rect pane_rect(void)
{
	bu_rect w = settings_win();
	return (bu_rect){ w.x + 176, w.y + 42, w.w - 176, w.h - 42 };
}
static bu_rect brightness_slider(void)
{
	bu_rect p = pane_rect();
	return (bu_rect){ p.x + 58, p.y + 80, p.w - 150, 24 };
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
	return (bu_rect){ w.x + w.w - 34, w.y + 8, 26, 26 };
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
	{ "ブラウザ", bu_icon_globe, { 32, 201, 151, 255 }, "/usr/local/bin/netsurf-fb" },
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
	s->panel_h = 46;
	s->wifi_on = true;
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

static void panel(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect bar = { 0, PH - PANEL_H, PW, PANEL_H };
	bu_color pc = t->panel; pc.a = 235;
	bu_fill_rect(c, bar, pc);
	/* accent hairline: accent -> accent2 across the panel top */
	bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H, PW / 2, 2 }, t->accent);
	bu_fill_rect(c, (bu_rect){ PW / 2, PH - PANEL_H, PW - PW / 2, 2 }, t->accent2);
	bu_rect sb = start_btn();
	bu_fill_round_rect(c, sb, 9, s->start_pressed ? t->accent2 : t->accent);
	bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 11, 5, 5 }, BU_RGB(255, 255, 255));
	bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 11, 5, 5 }, BU_RGBA(255, 255, 255, 200));
	bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 200));
	bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 150));
	/* quick launch: settings (toggle), files, browser */
	static const struct { icon_fn icon; bu_color tint; } QL[] = {
		{ bu_icon_gear, { 134, 142, 150, 255 } },
		{ bu_icon_folder, { 250, 176, 5, 255 } },
		{ bu_icon_globe, { 32, 201, 151, 255 } },
	};
	for (int i = 0; i < 3; i++) {
		bu_rect chip = ql_rect(i);
		bool active = (i == 0 && s->settings_open);
		bu_fill_round_rect(c, chip, 9, active ? t->surface3 : t->surface);
		QL[i].icon(c, chip.x + chip.w / 2, chip.y + chip.h / 2, active ? t->text : t->text_dim);
		if (active)
			bu_fill_round_rect(c, (bu_rect){ chip.x + chip.w / 2 - 10, PH - 8, 20, 3 }, 1, t->accent);
	}
	/* tray ink: light on dark panels (XP/Win8), theme text otherwise */
	int lum = (t->panel.r * 3 + t->panel.g * 5 + t->panel.b * 2) / 10;
	bu_color tray = lum < 110 ? BU_RGB(255, 255, 255) : t->text_dim;
	bu_color tray_main = lum < 110 ? BU_RGB(255, 255, 255) : t->text;
	bu_icon_wifi(c, PW - 186, PH - PANEL_H / 2, tray);
	bu_rect vr = vol_icon_rect();
	bu_icon_volume(c, vr.x + vr.w / 2, PH - PANEL_H / 2, s->vol_popup ? t->accent : tray);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d%%", s->battery_pct);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H, PW - 100, PANEL_H }, pct, 13, tray);
	bu_icon_battery(c, PW - 96, PH - PANEL_H / 2 - 7, s->battery_pct / 100.0f, false, tray);
	if (PANEL_H >= 52) {
		/* two-line clock: time over date */
		char dbuf[40];
		const char *sp = strchr(s->date, ' ');
		snprintf(dbuf, sizeof(dbuf), "%.*s", sp ? (int)(sp - s->date) : (int)strlen(s->date), s->date);
		bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H + 4, PW - 10, 24 }, s->clock, 16, tray_main);
		bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - 22, PW - 10, 18 }, dbuf, 10.5, tray);
	} else {
		bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H, PW - 10, PANEL_H }, s->clock, 14, tray_main);
	}
	/* volume popup */
	if (s->vol_popup) {
		bu_rect p = vol_popup_rect();
		bu_shadow_round_rect(c, p, 12, 8, BU_RGBA(0, 0, 0, 110));
		bu_fill_round_rect(c, p, 12, t->surface);
		bu_icon_volume(c, p.x + 24, p.y + p.h / 2, t->text_dim);
		bu_slider(c, t, vol_popup_slider(), s->volume, s->drag_slider == 3);
		char vp[8];
		snprintf(vp, sizeof(vp), "%d%%", (int)(s->volume * 100 + 0.5f));
		bu_draw_text_right(c, t->font, (bu_rect){ p.x, p.y + 16, p.w - 10, 24 }, vp, 12.5, t->text);
	}
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
	bu_shadow_round_rect(c, sheet, 18, 10, BU_RGBA(0, 0, 0, 110));
	bu_fill_round_rect(c, sheet, 18, BU_RGBA(24, 30, 42, 248));
	bu_rect search = { sheet.x + 22, sheet.y + 18, sheet.w - 44, 38 };
	bu_fill_round_rect(c, search, 10, t->surface2);
	bu_icon_search(c, search.x + 22, search.y + 19, t->text_faint);
	bu_draw_text(c, t->font, search.x + 40, search.y + 8, "アプリを検索", 14, t->text_faint);
	for (int i = 0; i < NAPPS; i++) {
		bu_rect tr = tile_rect(i);
		int cx = tr.x + tr.w / 2, cy = tr.y + 40;
		bu_fill_round_rect(c, (bu_rect){ cx - 26, cy - 26, 52, 52 }, 14, APPS[i].tint);
		APPS[i].icon(c, cx, cy, BU_RGB(255, 255, 255));
		bu_draw_text_centered(c, t->font, (bu_rect){ tr.x, cy + 32, tr.w, 20 }, APPS[i].name, 13, t->text);
	}
	bu_fill_circle(c, PW / 2 - 10, sheet.y + sheet.h - 18, 3, t->text);
	bu_fill_circle(c, PW / 2 + 4, sheet.y + sheet.h - 18, 3, t->text_faint);
	(void)s;
}

static void draw_settings(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect win = settings_win();
	bu_shadow_round_rect(c, win, 14, 10, BU_RGBA(0, 0, 0, 120));
	bu_fill_round_rect(c, win, 14, t->surface);
	bu_rect tb = { win.x, win.y, win.w, 42 };
	bu_gradient_round_v(c, tb, 14, BU_RGB(44, 53, 69), BU_RGB(36, 44, 58));
	bu_fill_rect(c, (bu_rect){ win.x, win.y + 28, win.w, 14 }, BU_RGB(36, 44, 58));
	bu_icon_gear(c, win.x + 24, win.y + 21, t->text_dim);
	bu_draw_text(c, t->font, win.x + 44, win.y + 10, "設定", 15, t->text);
	bu_fill_circle(c, win.x + win.w - 24, win.y + 21, 7, t->danger);
	bu_fill_circle(c, win.x + win.w - 48, win.y + 21, 7, t->warn);
	bu_fill_circle(c, win.x + win.w - 72, win.y + 21, 7, t->ok);
	bu_rect side = { win.x, win.y + 42, 176, win.h - 42 };
	bu_fill_rect(c, side, BU_RGBA(255, 255, 255, 7));
	const char *cats[] = { "画面", "スキン", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム" };
	for (int i = 0; i < 8; i++) {
		bu_rect row = { side.x + 10, side.y + 12 + i * 40, side.w - 20, 34 };
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
		const char *sz = s->panel_h <= 40 ? "小" : s->panel_h >= 64 ? "大" : "中";
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
	{
		static const char *names[] = { "画面", "スキン", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム" };
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 16, names[s->settings_cat & 7], 19, t->text);
		bu_draw_text(c, t->font, pane.x + 22, pane.y + 60, "このカテゴリは今後の更新で拡充されます。", 13, t->text_dim);
	}
}

void bd_draw_shell(bu_canvas *c, bu_theme *t, bd_state *s)
{
	PANEL_H = s->panel_h > 0 ? s->panel_h : 46;
	wallpaper(c);
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
	bu_draw_text(c, t->font, 16, PH - 30, "Brainux", 12, t->text_faint);
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
	PANEL_H = s->panel_h > 0 ? s->panel_h : 46;
	*changed = false;
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
			snprintf(s->launch_path, sizeof(s->launch_path), "/usr/local/bin/netsurf-fb");
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
					s->panel_h = s->panel_h <= 40 ? 52 : s->panel_h >= 64 ? 40 : 64;
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

			/* sidebar categories */
			bu_rect w = settings_win();
			for (int i = 0; i < 8; i++) {
				bu_rect row = { w.x + 10, w.y + 42 + 12 + i * 40, 156, 34 };
				if (in_rect(row, x, y)) { s->settings_cat = i; if (i == 4) s->net_scan_req++; *changed = true; return BD_ACT_NONE; }
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
