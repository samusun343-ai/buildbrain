#include "bui_skin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const bd_skin BD_SKINS[] = {
	{ "オーシャン", { 45, 156, 219, 255 }, { 32, 201, 151, 255 },
	  { 13, 27, 46, 255 }, { 10, 44, 54, 255 }, { 32, 201, 151, 255 }, false },
	{ "フォレスト", { 46, 204, 113, 255 }, { 26, 188, 156, 255 },
	  { 10, 32, 22, 255 }, { 8, 40, 30, 255 }, { 46, 204, 113, 255 }, false },
	{ "グレープ", { 155, 109, 255, 255 }, { 214, 109, 255, 255 },
	  { 24, 14, 44, 255 }, { 38, 12, 54, 255 }, { 155, 109, 255, 255 }, false },
	{ "サンセット", { 255, 122, 89, 255 }, { 255, 180, 84, 255 },
	  { 40, 18, 24, 255 }, { 54, 26, 14, 255 }, { 255, 150, 84, 255 }, false },
	{ "モノクロ", { 200, 200, 205, 255 }, { 150, 150, 158, 255 },
	  { 18, 18, 20, 255 }, { 28, 28, 32, 255 }, { 160, 160, 168, 255 }, false },
	{ "ライト", { 0, 120, 215, 255 }, { 0, 176, 140, 255 },
	  { 214, 232, 246, 255 }, { 190, 220, 238, 255 }, { 90, 170, 230, 255 }, true },
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
	*t = pristine;
	t->font = font;
	const bd_skin *s = bd_skin_get();
	t->accent = s->accent;
	t->accent2 = s->accent2;
	if (s->light) {
		t->bg = BU_RGB(232, 238, 244);
		t->surface = BU_RGB(255, 255, 255);
		t->surface2 = BU_RGB(240, 244, 248);
		t->surface3 = BU_RGB(214, 226, 238);
		t->line = BU_RGB(190, 200, 212);
		t->text = BU_RGB(24, 30, 38);
		t->text_dim = BU_RGB(84, 96, 110);
		t->text_faint = BU_RGB(140, 152, 166);
	}
}

void bd_skin_set(int idx)
{
	if (idx >= 0 && idx < BD_NSKINS)
		bd_skin_current = idx;
}
