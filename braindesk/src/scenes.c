#include "scenes.h"
#include "bui_input.h"
#include <string.h>
#include <stdio.h>

#define PW 854
#define PH 480
#define PANEL_H 46

static bu_rect start_btn(void) { return (bu_rect){ 8, PH - 40, 34, 34 }; }
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
struct app { const char *name; icon_fn icon; bu_color tint; };
static const struct app APPS[] = {
	{ "辞書", bu_icon_book, { 45, 156, 219, 255 } },
	{ "設定", bu_icon_gear, { 134, 142, 150, 255 } },
	{ "ファイル", bu_icon_folder, { 250, 176, 5, 255 } },
	{ "ブラウザ", bu_icon_globe, { 32, 201, 151, 255 } },
	{ "端末", bu_icon_terminal, { 90, 100, 116, 255 } },
	{ "メモ", bu_icon_note, { 255, 212, 59, 255 } },
	{ "電卓", bu_icon_calc, { 116, 192, 252, 255 } },
	{ "音楽", bu_icon_music, { 240, 101, 149, 255 } },
	{ "ホーム", bu_icon_home, { 129, 230, 168, 255 } },
	{ "システム", bu_icon_chip, { 178, 140, 240, 255 } },
	{ "明るさ", bu_icon_brightness, { 255, 224, 102, 255 } },
	{ "音量", bu_icon_volume, { 99, 230, 190, 255 } },
};
#define NAPPS 12

static bu_rect tile_rect(int i)
{
	bu_rect sheet = launcher_sheet();
	int cols = 6, cell_w = (sheet.w - 44) / cols, cell_h = 96;
	int col = i % cols, row = i / cols;
	int cx = sheet.x + 22 + col * cell_w + cell_w / 2;
	int cy = sheet.y + 76 + row * cell_h + 28;
	return (bu_rect){ cx - cell_w / 2, cy - 40, cell_w, cell_h - 8 };
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
	s->battery_pct = 78;
}

static void wallpaper(bu_canvas *c)
{
	bu_gradient_v(c, (bu_rect){ 0, 0, PW, PH }, BU_RGB(13, 27, 46), BU_RGB(10, 44, 54));
	for (int i = 0; i < 26; i++)
		bu_fill_circle(c, 700 - i * 6, 60 + i * 3, 120 - i * 3, BU_RGBA(45, 156, 219, 7));
	for (int i = 0; i < 20; i++)
		bu_fill_circle(c, 90 + i * 4, 420 - i * 4, 100 - i * 3, BU_RGBA(32, 201, 151, 6));
}

static void panel(bu_canvas *c, bu_theme *t, bd_state *s)
{
	bu_rect bar = { 0, PH - PANEL_H, PW, PANEL_H };
	bu_fill_rect(c, bar, BU_RGBA(18, 23, 33, 235));
	bu_fill_rect(c, (bu_rect){ 0, PH - PANEL_H, PW, 1 }, BU_RGBA(255, 255, 255, 26));
	bu_rect sb = start_btn();
	bu_fill_round_rect(c, sb, 9, s->start_pressed ? t->accent2 : t->accent);
	bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 11, 5, 5 }, BU_RGB(255, 255, 255));
	bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 11, 5, 5 }, BU_RGBA(255, 255, 255, 200));
	bu_fill_rect(c, (bu_rect){ sb.x + 11, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 200));
	bu_fill_rect(c, (bu_rect){ sb.x + 18, sb.y + 18, 5, 5 }, BU_RGBA(255, 255, 255, 150));
	const char *apps[] = { "設定", "ファイル", "端末" };
	for (int i = 0; i < 3; i++) {
		bu_rect chip = { 54 + i * 118, PH - 39, 110, 32 };
		bool active = s->settings_open && i == 0;
		bu_fill_round_rect(c, chip, 8, active ? t->surface3 : t->surface);
		bu_draw_text_centered(c, t->font, chip, apps[i], 13.5, active ? t->text : t->text_dim);
		if (active)
			bu_fill_round_rect(c, (bu_rect){ chip.x + chip.w / 2 - 12, PH - 9, 24, 3 }, 1, t->accent);
	}
	bu_icon_wifi(c, PW - 178, PH - 23, t->text_dim);
	bu_icon_volume(c, PW - 148, PH - 23, t->text_dim);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d%%", s->battery_pct);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H, PW - 96, PANEL_H }, pct, 13, t->text_dim);
	bu_icon_battery(c, PW - 92, PH - 30, s->battery_pct / 100.0f, false, t->text_dim);
	bu_draw_text_right(c, t->font, (bu_rect){ 0, PH - PANEL_H, PW - 12, PANEL_H }, s->clock, 14, t->text);
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
	const char *cats[] = { "画面", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム" };
	for (int i = 0; i < 7; i++) {
		bu_rect row = { side.x + 10, side.y + 12 + i * 40, side.w - 20, 34 };
		bu_list_row(c, t, row, cats[i], NULL, false, i == s->settings_cat);
	}
	bu_rect pane = pane_rect();
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
	bu_draw_text(c, t->font, pane.x + 22, pane.y + 208, "音声", 19, t->text);
	bu_draw_text(c, t->font, pane.x + 22, pane.y + 242, "音量", 13, t->text_dim);
	bu_icon_volume(c, pane.x + 36, pane.y + 280, t->text_dim);
	bu_rect vs = volume_slider();
	bu_slider(c, t, vs, s->volume, s->drag_slider == 2);
	snprintf(pct, sizeof(pct), "%d%%", (int)(s->volume * 100 + 0.5f));
	bu_draw_text_right(c, t->font, (bu_rect){ pane.x, pane.y + 266, pane.w - 22, 26 }, pct, 14, t->text);
	bu_rect r3 = { pane.x + 8, pane.y + 302, pane.w - 16, 42 };
	bu_draw_text(c, t->font, r3.x + 14, r3.y + (r3.h - 22) / 2, "キー音", 15, t->text);
	bu_toggle(c, t, r3.x + r3.w - 60, r3.y + 8, s->key_sound);
}

void bd_draw_shell(bu_canvas *c, bu_theme *t, bd_state *s)
{
	wallpaper(c);
	if (s->view == BD_VIEW_LAUNCHER)
		draw_launcher(c, t, s);
	if (s->settings_open)
		draw_settings(c, t, s);
	panel(c, t, s);
}

void bd_draw_login(bu_canvas *c, bu_theme *t, bd_state *s, const char *user)
{
	wallpaper(c);
	bu_draw_text_centered(c, t->font, (bu_rect){ 0, 58, PW, 90 }, s->clock, 64, t->text);
	bu_draw_text_centered(c, t->font, (bu_rect){ 0, 138, PW, 30 }, "11月19日 水曜日", 16, t->text_dim);
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
		if (in_rect(start_btn(), x, y)) {
			s->start_pressed = true;
			*changed = true;
			return BD_ACT_NONE;
		}
		if (s->settings_open) {
			if (in_rect(settings_close(), x, y)) { s->settings_open = false; *changed = true; return BD_ACT_NONE; }
			if (in_rect(brightness_slider(), x, y)) { s->drag_slider = 1; s->brightness = slider_frac(brightness_slider(), x); *changed = true; return BD_ACT_NONE; }
			if (in_rect(volume_slider(), x, y)) { s->drag_slider = 2; s->volume = slider_frac(volume_slider(), x); *changed = true; return BD_ACT_NONE; }
			if (in_rect(lid_toggle_hit(), x, y)) { s->lid_sleep = !s->lid_sleep; *changed = true; return BD_ACT_NONE; }
			if (in_rect(keysound_toggle_hit(), x, y)) { s->key_sound = !s->key_sound; *changed = true; return BD_ACT_NONE; }
			/* sidebar categories */
			bu_rect w = settings_win();
			for (int i = 0; i < 7; i++) {
				bu_rect row = { w.x + 10, w.y + 42 + 12 + i * 40, 156, 34 };
				if (in_rect(row, x, y)) { s->settings_cat = i; *changed = true; return BD_ACT_NONE; }
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
		if (s->drag_slider == 2) { s->volume = slider_frac(volume_slider(), x); *changed = true; }
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
