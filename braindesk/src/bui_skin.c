#include "bui_skin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define C(r, g, b) { r, g, b, 255 }

/* Fields: name, bg, surface, surface2, surface3, line,
 * text, text_dim, text_faint, accent, accent2, panel,
 * wall_top, wall_bottom, wall_glow, radius */
const bd_skin BD_SKINS[] = {
	{ "オーシャン",
	  C(16, 20, 28), C(28, 34, 46), C(38, 46, 61), C(50, 60, 78), C(62, 74, 94),
	  C(238, 242, 247), C(168, 178, 194), C(110, 121, 140),
	  C(45, 156, 219), C(32, 201, 151), C(18, 23, 33),
	  C(13, 27, 46), C(10, 44, 54), C(32, 201, 151), 10, 0 },
	{ "Brainux",
	  C(18, 26, 40), C(30, 42, 62), C(38, 52, 76), C(52, 68, 96), C(64, 84, 112),
	  C(235, 242, 250), C(170, 188, 208), C(112, 130, 152),
	  C(79, 195, 247), C(38, 198, 218), C(16, 24, 38),
	  C(10, 22, 44), C(14, 42, 74), C(79, 195, 247), 10, 0 },
	{ "フォレスト",
	  C(14, 22, 18), C(26, 38, 30), C(34, 50, 40), C(46, 66, 52), C(58, 82, 66),
	  C(236, 244, 238), C(170, 192, 176), C(112, 134, 118),
	  C(46, 204, 113), C(26, 188, 156), C(16, 26, 20),
	  C(10, 32, 22), C(8, 40, 30), C(46, 204, 113), 10, 0 },
	{ "グレープ",
	  C(22, 16, 34), C(36, 26, 52), C(46, 34, 66), C(60, 46, 84), C(74, 58, 102),
	  C(242, 238, 250), C(188, 176, 208), C(128, 116, 152),
	  C(155, 109, 255), C(214, 109, 255), C(26, 18, 40),
	  C(24, 14, 44), C(38, 12, 54), C(155, 109, 255), 10, 0 },
	{ "サンセット",
	  C(30, 20, 18), C(44, 30, 26), C(56, 38, 32), C(72, 50, 42), C(88, 64, 54),
	  C(250, 240, 234), C(206, 182, 168), C(148, 124, 112),
	  C(255, 122, 89), C(255, 180, 84), C(34, 22, 18),
	  C(40, 18, 24), C(54, 26, 14), C(255, 150, 84), 10, 0 },
	{ "モノクロ",
	  C(18, 18, 20), C(30, 30, 33), C(38, 38, 42), C(52, 52, 57), C(66, 66, 72),
	  C(240, 240, 242), C(178, 178, 184), C(120, 120, 128),
	  C(200, 200, 205), C(150, 150, 158), C(20, 20, 22),
	  C(18, 18, 20), C(28, 28, 32), C(160, 160, 168), 10, 0 },
	{ "ライト",
	  C(232, 238, 244), C(255, 255, 255), C(240, 244, 248), C(214, 226, 238), C(190, 200, 212),
	  C(24, 30, 38), C(84, 96, 110), C(140, 152, 166),
	  C(0, 120, 215), C(0, 176, 140), C(226, 234, 242),
	  C(214, 232, 246), C(190, 220, 238), C(90, 170, 230), 10, 0 },
	{ "クラシック Mac",
	  C(221, 221, 221), C(238, 238, 238), C(228, 228, 228), C(204, 204, 204), C(153, 153, 153),
	  C(0, 0, 0), C(68, 68, 68), C(119, 119, 119),
	  C(51, 102, 204), C(102, 102, 102), C(221, 221, 221),
	  C(214, 214, 214), C(192, 192, 192), C(255, 255, 255), 3, 1 },
	{ "macOS",
	  C(245, 245, 247), C(255, 255, 255), C(240, 240, 242), C(227, 227, 232), C(210, 210, 215),
	  C(29, 29, 31), C(110, 110, 115), C(174, 174, 178),
	  C(0, 113, 227), C(52, 199, 89), C(250, 250, 252),
	  C(168, 192, 255), C(233, 213, 255), C(147, 197, 253), 10, 5 },
	{ "Windows 95 風",
	  C(192, 192, 192), C(192, 192, 192), C(212, 208, 200), C(168, 168, 168), C(128, 128, 128),
	  C(0, 0, 0), C(64, 64, 64), C(128, 128, 128),
	  C(0, 0, 128), C(0, 128, 128), C(192, 192, 192),
	  C(0, 128, 128), C(0, 128, 128), C(0, 160, 160), 0, 1 },
	{ "Windows XP 風",
	  C(236, 233, 216), C(245, 244, 236), C(236, 233, 216), C(216, 208, 192), C(172, 168, 153),
	  C(0, 0, 0), C(85, 85, 85), C(136, 136, 136),
	  C(0, 88, 230), C(74, 160, 44), C(36, 94, 219),
	  C(47, 111, 208), C(90, 160, 44), C(126, 200, 80), 8, 2 },
	{ "Windows CE 風",
	  C(192, 192, 192), C(200, 200, 200), C(212, 208, 200), C(168, 168, 168), C(128, 128, 128),
	  C(0, 0, 0), C(64, 64, 64), C(128, 128, 128),
	  C(0, 0, 128), C(0, 96, 192), C(192, 192, 192),
	  C(57, 99, 158), C(111, 150, 200), C(160, 190, 230), 2, 1 },
	{ "Windows Vista 風",
	  C(28, 39, 51), C(42, 54, 68), C(35, 47, 61), C(53, 66, 79), C(70, 88, 106),
	  C(255, 255, 255), C(199, 208, 217), C(143, 160, 176),
	  C(63, 169, 245), C(127, 186, 0), C(20, 28, 36),
	  C(15, 32, 39), C(44, 83, 100), C(56, 239, 125), 8, 3 },
	{ "Windows 8 風",
	  C(242, 242, 242), C(255, 255, 255), C(240, 240, 240), C(222, 222, 222), C(200, 200, 200),
	  C(30, 30, 30), C(90, 90, 90), C(150, 150, 150),
	  C(0, 173, 239), C(0, 120, 215), C(17, 17, 17),
	  C(13, 44, 107), C(23, 73, 168), C(0, 173, 239), 0, 4 },
	{ "Windows 10 風",
	  C(32, 32, 32), C(43, 43, 43), C(39, 39, 39), C(56, 56, 56), C(69, 69, 69),
	  C(255, 255, 255), C(200, 200, 200), C(138, 138, 138),
	  C(0, 120, 212), C(16, 124, 16), C(32, 32, 32),
	  C(0, 8, 16), C(0, 58, 107), C(0, 120, 212), 0, 4 },
	{ "Windows 11 風",
	  C(243, 243, 243), C(251, 251, 251), C(243, 243, 243), C(231, 231, 231), C(224, 224, 224),
	  C(32, 32, 32), C(93, 93, 93), C(158, 158, 158),
	  C(0, 103, 192), C(15, 108, 189), C(243, 243, 243),
	  C(166, 200, 255), C(215, 233, 255), C(122, 167, 255), 8, 0 },
};
const int BD_NSKINS = sizeof(BD_SKINS) / sizeof(BD_SKINS[0]);
int bd_skin_current = 0;

const bd_skin *bd_skin_get(void)
{
	if (bd_skin_current < 0 || bd_skin_current >= BD_NSKINS)
		bd_skin_current = 0;
	return &BD_SKINS[bd_skin_current];
}

static void skin_path(char *out, size_t n)
{
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.config/braindesk/skin", home ? home : "/home/user");
}

void bd_skin_load(void)
{
	char path[512];
	skin_path(path, sizeof(path));
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	int idx;
	if (fscanf(fp, "%d", &idx) == 1 && idx >= 0 && idx < BD_NSKINS)
		bd_skin_current = idx;
	fclose(fp);
}

void bd_skin_save(int idx)
{
	char path[512];
	skin_path(path, sizeof(path));
	char dir[512];
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
	if (fp) {
		fprintf(fp, "%d\n", idx);
		fclose(fp);
	}
}

static bu_theme pristine;
static bool have_pristine;

void bd_skin_apply(bu_theme *t)
{
	if (!have_pristine) {
		pristine = *t;
		have_pristine = true;
	}
	bu_font *font = t->font;
	bu_color danger = t->danger, ok = t->ok, warn = t->warn;
	*t = pristine;
	t->font = font;
	t->danger = danger;
	t->ok = ok;
	t->warn = warn;
	const bd_skin *s = bd_skin_get();
	t->bg = s->bg;
	t->surface = s->surface;
	t->surface2 = s->surface2;
	t->surface3 = s->surface3;
	t->line = s->line;
	t->text = s->text;
	t->text_dim = s->text_dim;
	t->text_faint = s->text_faint;
	t->accent = s->accent;
	t->accent2 = s->accent2;
	t->panel = s->panel;
	t->radius = s->radius;
	bu_chrome = s->chrome;
}

void bd_skin_set(int idx)
{
	if (idx >= 0 && idx < BD_NSKINS)
		bd_skin_current = idx;
}
