#include "bui_skin.h"
#include "brainreg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C(r, g, b) { r, g, b, 255 }

/* Braindesk original colour themes. The OS-reproduction skins
 * (Classic Mac / macOS / Windows 95..11 / CE / Vista) were
 * retired in 2026-11 when the desktop moved to a single
 * braindesk design; only the layout and the colour themes
 * remain, and every theme pins layout/chrome/panel_h to the
 * design-system values (BD layout, MODERN chrome, 48 px bar).
 *
 * Fields: name, bg, surface, surface2, surface3, line,
 * text, text_dim, text_faint, accent, accent2, panel,
 * wall_top, wall_bottom, wall_glow, radius, panel_h, chrome,
 * layout */
static const bd_skin BUILTIN_SKINS[] = {
	{ "オーシャン",
	  C(16, 20, 28), C(28, 34, 46), C(38, 46, 61), C(50, 60, 78), C(62, 74, 94),
	  C(238, 242, 247), C(168, 178, 194), C(110, 121, 140),
	  C(45, 156, 219), C(32, 201, 151), C(18, 23, 33),
	  C(13, 27, 46), C(10, 44, 54), C(32, 201, 151), 10, 48, 0, 0 },
	{ "Brainux",
	  C(18, 26, 40), C(30, 42, 62), C(38, 52, 76), C(52, 68, 96), C(64, 84, 112),
	  C(235, 242, 250), C(170, 188, 208), C(112, 130, 152),
	  C(79, 195, 247), C(38, 198, 218), C(16, 24, 38),
	  C(10, 22, 44), C(14, 42, 74), C(79, 195, 247), 10, 48, 0, 0 },
	{ "フォレスト",
	  C(14, 22, 18), C(26, 38, 30), C(34, 50, 40), C(46, 66, 52), C(58, 82, 66),
	  C(236, 244, 238), C(170, 192, 176), C(112, 134, 118),
	  C(46, 204, 113), C(26, 188, 156), C(16, 26, 20),
	  C(10, 32, 22), C(8, 40, 30), C(46, 204, 113), 10, 48, 0, 0 },
	{ "グレープ",
	  C(22, 16, 34), C(36, 26, 52), C(46, 34, 66), C(60, 46, 84), C(74, 58, 102),
	  C(242, 238, 250), C(188, 176, 208), C(128, 116, 152),
	  C(155, 109, 255), C(214, 109, 255), C(26, 18, 40),
	  C(24, 14, 44), C(38, 12, 54), C(155, 109, 255), 10, 48, 0, 0 },
	{ "サンセット",
	  C(30, 20, 18), C(44, 30, 26), C(56, 38, 32), C(72, 50, 42), C(88, 64, 54),
	  C(250, 240, 234), C(206, 182, 168), C(148, 124, 112),
	  C(255, 122, 89), C(255, 180, 84), C(34, 22, 18),
	  C(40, 18, 24), C(54, 26, 14), C(255, 150, 84), 10, 48, 0, 0 },
	{ "モノクロ",
	  C(18, 18, 20), C(30, 30, 33), C(38, 38, 42), C(52, 52, 57), C(66, 66, 72),
	  C(240, 240, 242), C(178, 178, 184), C(120, 120, 128),
	  C(200, 200, 205), C(150, 150, 158), C(20, 20, 22),
	  C(18, 18, 20), C(28, 28, 32), C(160, 160, 168), 10, 48, 0, 0 },
	{ "ライト",
	  C(232, 238, 244), C(255, 255, 255), C(240, 244, 248), C(214, 226, 238), C(190, 200, 212),
	  C(24, 30, 38), C(84, 96, 110), C(140, 152, 166),
	  C(0, 120, 215), C(0, 176, 140), C(226, 234, 242),
	  C(214, 232, 246), C(190, 220, 238), C(90, 170, 230), 10, 48, 0, 0 },
};
#define N_BUILTIN ((int)(sizeof(BUILTIN_SKINS) / sizeof(BUILTIN_SKINS[0])))
const int BD_NBUILTIN = N_BUILTIN;

const bd_skin *BD_SKINS = BUILTIN_SKINS;
int BD_NSKINS = N_BUILTIN;
int bd_skin_current = 0;

int bd_skin_count(void)
{
	return BD_NSKINS;
}

const bd_skin *bd_skin_at(int idx)
{
	if (idx < 0 || idx >= BD_NSKINS)
		return NULL;
	return &BD_SKINS[idx];
}

const bd_skin *bd_skin_get(void)
{
	if (bd_skin_current < 0 || bd_skin_current >= BD_NSKINS)
		bd_skin_current = 0;
	return &BD_SKINS[bd_skin_current];
}

/* The file-defined skin loader (*.skin) is retired together
 * with the skin system: themes are a fixed built-in set now.
 * The entry point stays as a no-op so existing callers keep
 * linking; files under the skins directories are simply never
 * read. (Removing the callers instead would have meant
 * touching apps and tools outside this change's scope.) */
void bd_skin_load_files(void)
{
}

static void skin_path(char *out, size_t n)
{
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.config/braindesk/skin", home ? home : "/home/user");
}

void bd_skin_load(void)
{
	/* brainreg first (desktop.skin); the legacy file is the
	 * fallback for settings written before the store existed.
	 * Indices saved for retired skins (>= BD_NSKINS) are out
	 * of range in both stores and fall back to theme 0. */
	int idx = brainreg_get_int("desktop", "skin", -1);
	if (idx >= 0 && idx < BD_NSKINS) {
		bd_skin_current = idx;
		return;
	}
	char path[512];
	skin_path(path, sizeof(path));
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	if (fscanf(fp, "%d", &idx) == 1 && idx >= 0 && idx < BD_NSKINS)
		bd_skin_current = idx;
	fclose(fp);
}

void bd_skin_save(int idx)
{
	/* brainreg is the store of record; the legacy file is still
	 * written as a mirror for anything that reads it directly. */
	char val[16];
	snprintf(val, sizeof(val), "%d", idx);
	brainreg_set("desktop", "skin", val);
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
