/* braindesk demo scenes: login, desktop+launcher, settings.
 * Rendered with libbrainui only. On the host these go to PPM files;
 * on the device the same scenes are drawn by the greeter, the desk
 * shell and the settings app.
 */
#include "bui.h"
#include <stdio.h>
#include <string.h>

#define W 854
#define H 480

static bu_theme *T;

static void wallpaper(bu_canvas *c)
{
	bu_gradient_v(c, (bu_rect){ 0, 0, W, H }, BU_RGB(13, 27, 46), BU_RGB(10, 44, 54));
	/* soft light blobs, purely procedural */
	for (int i = 0; i < 26; i++) {
		bu_color col = BU_RGBA(45, 156, 219, 7);
		bu_fill_circle(c, 700 - i * 6, 60 + i * 3, 120 - i * 3, col);
	}
	for (int i = 0; i < 20; i++) {
		bu_color col = BU_RGBA(32, 201, 151, 6);
		bu_fill_circle(c, 90 + i * 4, 420 - i * 4, 100 - i * 3, col);
	}
}

static void panel(bu_canvas *c, const char *active_app)
{
	bu_rect bar = { 0, H - 46, W, 46 };
	bu_fill_rect(c, bar, BU_RGBA(18, 23, 33, 235));
	bu_fill_rect(c, (bu_rect){ 0, H - 46, W, 1 }, BU_RGBA(255, 255, 255, 26));
	/* start button */
	bu_fill_round_rect(c, (bu_rect){ 8, H - 40, 34, 34 }, 9, T->accent);
	bu_fill_rect(c, (bu_rect){ 19, H - 29, 5, 5 }, BU_RGB(255, 255, 255));
	bu_fill_rect(c, (bu_rect){ 26, H - 29, 5, 5 }, BU_RGBA(255, 255, 255, 200));
	bu_fill_rect(c, (bu_rect){ 19, H - 22, 5, 5 }, BU_RGBA(255, 255, 255, 200));
	bu_fill_rect(c, (bu_rect){ 26, H - 22, 5, 5 }, BU_RGBA(255, 255, 255, 150));
	/* running apps */
	const char *apps[] = { "設定", "ファイル", "端末" };
	for (int i = 0; i < 3; i++) {
		bu_rect chip = { 54 + i * 118, H - 39, 110, 32 };
		bool active = strcmp(apps[i], active_app) == 0;
		bu_fill_round_rect(c, chip, 8, active ? T->surface3 : T->surface);
		bu_draw_text_centered(c, T->font, chip, apps[i], 13.5, active ? T->text : T->text_dim);
		if (active)
			bu_fill_round_rect(c, (bu_rect){ chip.x + chip.w / 2 - 12, H - 9, 24, 3 }, 1, T->accent);
	}
	/* tray */
	bu_icon_wifi(c, W - 178, H - 23, T->text_dim);
	bu_icon_volume(c, W - 148, H - 23, T->text_dim);
	bu_draw_text_right(c, T->font, (bu_rect){ 0, H - 46, W - 96, 46 }, "78%", 13, T->text_dim);
	bu_icon_battery(c, W - 92, H - 30, 0.78f, false, T->text_dim);
	bu_draw_text_right(c, T->font, (bu_rect){ 0, H - 46, W - 12, 46 }, "10:42", 14, T->text);
}

static void scene_login(void)
{
	bu_canvas *c = bu_canvas_new(W, H);
	wallpaper(c);
	/* clock */
	bu_draw_text_centered(c, T->font, (bu_rect){ 0, 58, W, 90 }, "10:42", 64, T->text);
	bu_draw_text_centered(c, T->font, (bu_rect){ 0, 138, W, 30 }, "11月19日 水曜日", 16, T->text_dim);
	/* login card */
	bu_rect card = { W / 2 - 170, 196, 340, 216 };
	bu_card(c, T, card);
	bu_fill_circle(c, W / 2, 240, 26, T->accent);
	bu_draw_text_centered(c, T->font, (bu_rect){ W / 2 - 30, 222, 60, 36 }, "B", 24, BU_RGB(255, 255, 255));
	bu_draw_text_centered(c, T->font, (bu_rect){ 0, 272, W, 26 }, "brain", 17, T->text);
	bu_text_field(c, T, (bu_rect){ W / 2 - 140, 306, 280, 42 }, "パスワード", "●●●●●●●", true);
	bu_button(c, T, (bu_rect){ W / 2 - 140, 360, 280, 40 }, "ログイン", BU_BTN_PRIMARY, false);
	/* bottom actions */
	bu_icon_power(c, W - 66, H - 66, T->text_dim);
	bu_draw_text_centered(c, T->font, (bu_rect){ W - 96, H - 52, 60, 18 }, "電源", 11, T->text_dim);
	bu_icon_gear(c, W - 126, H - 66, T->text_dim);
	bu_draw_text_centered(c, T->font, (bu_rect){ W - 156, H - 52, 60, 18 }, "再起動", 11, T->text_dim);
	bu_draw_text(c, T->font, 16, H - 30, "Brainux", 12, T->text_faint);
	bu_canvas_write_ppm(c, "login.ppm");
	bu_canvas_free(c);
}

typedef void (*icon_fn)(bu_canvas *, int, int, bu_color);
struct app { const char *name; icon_fn icon; bu_color tint; };

static void scene_desktop(void)
{
	bu_canvas *c = bu_canvas_new(W, H);
	wallpaper(c);
	/* launcher sheet */
	bu_rect sheet = { 54, 26, W - 108, H - 46 - 52 };
	bu_shadow_round_rect(c, sheet, 18, 10, BU_RGBA(0, 0, 0, 110));
	bu_fill_round_rect(c, sheet, 18, BU_RGBA(24, 30, 42, 248));
	/* search field */
	bu_rect search = { sheet.x + 22, sheet.y + 18, sheet.w - 44, 38 };
	bu_fill_round_rect(c, search, 10, T->surface2);
	bu_icon_search(c, search.x + 22, search.y + 19, T->text_faint);
	bu_draw_text(c, T->font, search.x + 40, search.y + 8, "アプリを検索", 14, T->text_faint);
	/* app grid */
	struct app apps[] = {
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
	int cols = 6, cell_w = (sheet.w - 44) / cols, cell_h = 96;
	for (int i = 0; i < 12; i++) {
		int col = i % cols, row = i / cols;
		int cx = sheet.x + 22 + col * cell_w + cell_w / 2;
		int cy = sheet.y + 76 + row * cell_h + 28;
		bu_fill_round_rect(c, (bu_rect){ cx - 26, cy - 26, 52, 52 }, 14, apps[i].tint);
		apps[i].icon(c, cx, cy, BU_RGB(255, 255, 255));
		bu_draw_text_centered(c, T->font, (bu_rect){ cx - cell_w / 2, cy + 32, cell_w, 20 }, apps[i].name, 13, T->text);
	}
	/* page dots */
	bu_fill_circle(c, W / 2 - 10, sheet.y + sheet.h - 18, 3, T->text);
	bu_fill_circle(c, W / 2 + 4, sheet.y + sheet.h - 18, 3, T->text_faint);
	panel(c, "設定");
	bu_canvas_write_ppm(c, "desktop.ppm");
	bu_canvas_free(c);
}

static void scene_settings(void)
{
	bu_canvas *c = bu_canvas_new(W, H);
	wallpaper(c);
	/* window */
	bu_rect win = { 44, 24, W - 88, H - 46 - 40 };
	bu_shadow_round_rect(c, win, 14, 10, BU_RGBA(0, 0, 0, 120));
	bu_fill_round_rect(c, win, 14, T->surface);
	/* title bar */
	bu_rect tb = { win.x, win.y, win.w, 42 };
	bu_gradient_round_v(c, tb, 14, BU_RGB(44, 53, 69), BU_RGB(36, 44, 58));
	bu_fill_rect(c, (bu_rect){ win.x, win.y + 28, win.w, 14 }, BU_RGB(36, 44, 58));
	bu_icon_gear(c, win.x + 24, win.y + 21, T->text_dim);
	bu_draw_text(c, T->font, win.x + 44, win.y + 10, "設定", 15, T->text);
	bu_fill_circle(c, win.x + win.w - 24, win.y + 21, 7, T->danger);
	bu_fill_circle(c, win.x + win.w - 48, win.y + 21, 7, T->warn);
	bu_fill_circle(c, win.x + win.w - 72, win.y + 21, 7, T->ok);
	/* sidebar */
	bu_rect side = { win.x, win.y + 42, 176, win.h - 42 };
	bu_fill_rect(c, side, BU_RGBA(255, 255, 255, 7));
	const char *cats[] = { "画面", "音声", "キーボード", "ネットワーク", "バッテリー", "入力と日本語", "システム" };
	for (int i = 0; i < 7; i++) {
		bu_rect row = { side.x + 10, side.y + 12 + i * 40, side.w - 20, 34 };
		bu_list_row(c, T, row, cats[i], NULL, false, i == 0);
	}
	/* content: 画面 */
	bu_rect pane = { win.x + 176, win.y + 42, win.w - 176, win.h - 42 };
	bu_draw_text(c, T->font, pane.x + 22, pane.y + 16, "画面", 19, T->text);
	bu_draw_text(c, T->font, pane.x + 22, pane.y + 52, "明るさ", 13, T->text_dim);
	bu_icon_brightness(c, pane.x + 36, pane.y + 92, T->text_dim);
	bu_slider(c, T, (bu_rect){ pane.x + 58, pane.y + 80, pane.w - 150, 24 }, 0.7f, true);
	bu_draw_text_right(c, T->font, (bu_rect){ pane.x, pane.y + 78, pane.x + pane.w - 22 - pane.x, 26 }, "70%", 14, T->text);
	bu_list_row(c, T, (bu_rect){ pane.x + 8, pane.y + 118, pane.w - 16, 42 }, "画面オフまでの時間", "1分", true, false);
	bu_rect r2 = { pane.x + 8, pane.y + 160, pane.w - 16, 42 };
	bu_draw_text(c, T->font, r2.x + 14, r2.y + (r2.h - 22) / 2, "蓋を閉じたらスリープ", 15, T->text);
	bu_toggle(c, T, r2.x + r2.w - 60, r2.y + 8, true);
	/* content: 音声 */
	bu_draw_text(c, T->font, pane.x + 22, pane.y + 208, "音声", 19, T->text);
	bu_draw_text(c, T->font, pane.x + 22, pane.y + 242, "音量", 13, T->text_dim);
	bu_icon_volume(c, pane.x + 36, pane.y + 280, T->text_dim);
	bu_slider(c, T, (bu_rect){ pane.x + 58, pane.y + 268, pane.w - 150, 24 }, 0.45f, false);
	bu_draw_text_right(c, T->font, (bu_rect){ pane.x, pane.y + 266, pane.w - 22, 26 }, "45%", 14, T->text);
	bu_rect r3 = { pane.x + 8, pane.y + 302, pane.w - 16, 42 };
	bu_draw_text(c, T->font, r3.x + 14, r3.y + (r3.h - 22) / 2, "キー音", 15, T->text);
	bu_toggle(c, T, r3.x + r3.w - 60, r3.y + 8, false);
	panel(c, "設定");
	bu_canvas_write_ppm(c, "settings.ppm");
	bu_canvas_free(c);
}

int main(int argc, char **argv)
{
	const char *font_path = argc > 1 ? argv[1] : "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
	T = &bu_theme_dark;
	T->font = bu_font_load(font_path);
	if (!T->font) {
		fprintf(stderr, "font load failed: %s\n", font_path);
		return 1;
	}
	scene_login();
	scene_desktop();
	scene_settings();
	printf("done\n");
	return 0;
}
