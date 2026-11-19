#define _GNU_SOURCE	/* strdup() stays declared under any -std */
#include "bui_skin.h"
#include "brainreg.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define C(r, g, b) { r, g, b, 255 }

/* Fields: name, bg, surface, surface2, surface3, line,
 * text, text_dim, text_faint, accent, accent2, panel,
 * wall_top, wall_bottom, wall_glow, radius */
static const bd_skin BUILTIN_SKINS[] = {
	{ "オーシャン",
	  C(16, 20, 28), C(28, 34, 46), C(38, 46, 61), C(50, 60, 78), C(62, 74, 94),
	  C(238, 242, 247), C(168, 178, 194), C(110, 121, 140),
	  C(45, 156, 219), C(32, 201, 151), C(18, 23, 33),
	  C(13, 27, 46), C(10, 44, 54), C(32, 201, 151), 10, 42, 0, 0 },
	{ "Brainux",
	  C(18, 26, 40), C(30, 42, 62), C(38, 52, 76), C(52, 68, 96), C(64, 84, 112),
	  C(235, 242, 250), C(170, 188, 208), C(112, 130, 152),
	  C(79, 195, 247), C(38, 198, 218), C(16, 24, 38),
	  C(10, 22, 44), C(14, 42, 74), C(79, 195, 247), 10, 42, 0, 0 },
	{ "フォレスト",
	  C(14, 22, 18), C(26, 38, 30), C(34, 50, 40), C(46, 66, 52), C(58, 82, 66),
	  C(236, 244, 238), C(170, 192, 176), C(112, 134, 118),
	  C(46, 204, 113), C(26, 188, 156), C(16, 26, 20),
	  C(10, 32, 22), C(8, 40, 30), C(46, 204, 113), 10, 42, 0, 0 },
	{ "グレープ",
	  C(22, 16, 34), C(36, 26, 52), C(46, 34, 66), C(60, 46, 84), C(74, 58, 102),
	  C(242, 238, 250), C(188, 176, 208), C(128, 116, 152),
	  C(155, 109, 255), C(214, 109, 255), C(26, 18, 40),
	  C(24, 14, 44), C(38, 12, 54), C(155, 109, 255), 10, 42, 0, 0 },
	{ "サンセット",
	  C(30, 20, 18), C(44, 30, 26), C(56, 38, 32), C(72, 50, 42), C(88, 64, 54),
	  C(250, 240, 234), C(206, 182, 168), C(148, 124, 112),
	  C(255, 122, 89), C(255, 180, 84), C(34, 22, 18),
	  C(40, 18, 24), C(54, 26, 14), C(255, 150, 84), 10, 42, 0, 0 },
	{ "モノクロ",
	  C(18, 18, 20), C(30, 30, 33), C(38, 38, 42), C(52, 52, 57), C(66, 66, 72),
	  C(240, 240, 242), C(178, 178, 184), C(120, 120, 128),
	  C(200, 200, 205), C(150, 150, 158), C(20, 20, 22),
	  C(18, 18, 20), C(28, 28, 32), C(160, 160, 168), 10, 42, 0, 0 },
	{ "ライト",
	  C(232, 238, 244), C(255, 255, 255), C(240, 244, 248), C(214, 226, 238), C(190, 200, 212),
	  C(24, 30, 38), C(84, 96, 110), C(140, 152, 166),
	  C(0, 120, 215), C(0, 176, 140), C(226, 234, 242),
	  C(214, 232, 246), C(190, 220, 238), C(90, 170, 230), 10, 42, 0, 0 },
	{ "クラシック Mac",
	  C(221, 221, 221), C(238, 238, 238), C(228, 228, 228), C(204, 204, 204), C(153, 153, 153),
	  C(0, 0, 0), C(68, 68, 68), C(119, 119, 119),
	  C(51, 102, 204), C(102, 102, 102), C(221, 221, 221),
	  C(214, 214, 214), C(192, 192, 192), C(255, 255, 255), 3, 0, 1, 2 },
	{ "macOS",
	  C(245, 245, 247), C(255, 255, 255), C(240, 240, 242), C(227, 227, 232), C(210, 210, 215),
	  C(29, 29, 31), C(110, 110, 115), C(174, 174, 178),
	  C(0, 113, 227), C(52, 199, 89), C(250, 250, 252),
	  C(168, 192, 255), C(233, 213, 255), C(147, 197, 253), 10, 0, 5, 1 },
	{ "Windows 95",
	  C(192, 192, 192), C(192, 192, 192), C(212, 208, 200), C(168, 168, 168), C(128, 128, 128),
	  C(0, 0, 0), C(64, 64, 64), C(128, 128, 128),
	  C(0, 0, 128), C(0, 128, 128), C(192, 192, 192),
	  C(0, 128, 128), C(0, 128, 128), C(0, 160, 160), 0, 30, 1, 4 },
	{ "Windows XP",
	  C(236, 233, 216), C(245, 244, 236), C(236, 233, 216), C(216, 208, 192), C(172, 168, 153),
	  C(0, 0, 0), C(85, 85, 85), C(136, 136, 136),
	  C(0, 88, 230), C(74, 160, 44), C(36, 94, 219),
	  C(47, 111, 208), C(90, 160, 44), C(126, 200, 80), 8, 30, 2, 4 },
	{ "Windows CE",
	  C(192, 192, 192), C(200, 200, 200), C(212, 208, 200), C(168, 168, 168), C(128, 128, 128),
	  C(0, 0, 0), C(64, 64, 64), C(128, 128, 128),
	  C(0, 0, 128), C(0, 96, 192), C(192, 192, 192),
	  C(57, 99, 158), C(111, 150, 200), C(160, 190, 230), 2, 30, 1, 4 },
	{ "Windows Vista",
	  C(28, 39, 51), C(42, 54, 68), C(35, 47, 61), C(53, 66, 79), C(70, 88, 106),
	  C(255, 255, 255), C(199, 208, 217), C(143, 160, 176),
	  C(63, 169, 245), C(127, 186, 0), C(20, 28, 36),
	  C(15, 32, 39), C(44, 83, 100), C(56, 239, 125), 8, 30, 3, 4 },
	{ "Windows 8",
	  C(242, 242, 242), C(255, 255, 255), C(240, 240, 240), C(222, 222, 222), C(200, 200, 200),
	  C(30, 30, 30), C(90, 90, 90), C(150, 150, 150),
	  C(0, 173, 239), C(0, 120, 215), C(17, 17, 17),
	  C(13, 44, 107), C(23, 73, 168), C(0, 173, 239), 0, 40, 4, 4 },
	{ "Windows 10",
	  C(32, 32, 32), C(43, 43, 43), C(39, 39, 39), C(56, 56, 56), C(69, 69, 69),
	  C(255, 255, 255), C(200, 200, 200), C(138, 138, 138),
	  C(0, 120, 212), C(16, 124, 16), C(32, 32, 32),
	  C(0, 8, 16), C(0, 58, 107), C(0, 120, 212), 0, 40, 4, 4 },
	{ "Windows 11",
	  C(243, 243, 243), C(251, 251, 251), C(243, 243, 243), C(231, 231, 231), C(224, 224, 224),
	  C(32, 32, 32), C(93, 93, 93), C(158, 158, 158),
	  C(0, 103, 192), C(15, 108, 189), C(243, 243, 243),
	  C(166, 200, 255), C(215, 233, 255), C(122, 167, 255), 8, 48, 0, 3 },
	{ "Windows 7",
	  C(216, 228, 240), C(240, 246, 252), C(226, 236, 246), C(206, 220, 236), C(168, 186, 206),
	  C(24, 30, 38), C(84, 96, 110), C(140, 152, 166),
	  C(66, 150, 220), C(120, 190, 90), C(24, 32, 44),
	  C(74, 144, 217), C(168, 216, 248), C(120, 200, 255), 8, 40, 3, 4 },
};
#define N_BUILTIN ((int)(sizeof(BUILTIN_SKINS) / sizeof(BUILTIN_SKINS[0])))
const int BD_NBUILTIN = N_BUILTIN;

/* Until file skins load, the public table IS the built-in table;
 * bd_skin_load_files() swaps in a combined malloc'd table. */
const bd_skin *BD_SKINS = BUILTIN_SKINS;
int BD_NSKINS = N_BUILTIN;
int bd_skin_current = 0;

static bd_skin *dyn_skins;	/* combined table (never freed) */
static int files_loaded;

int bd_skin_count(void)
{
	bd_skin_load_files();
	return BD_NSKINS;
}

const bd_skin *bd_skin_at(int idx)
{
	bd_skin_load_files();
	if (idx < 0 || idx >= BD_NSKINS)
		return NULL;
	return &BD_SKINS[idx];
}

const bd_skin *bd_skin_get(void)
{
	bd_skin_load_files();
	if (bd_skin_current < 0 || bd_skin_current >= BD_NSKINS)
		bd_skin_current = 0;
	return &BD_SKINS[bd_skin_current];
}

/* ---- file-defined skins (*.skin, key=value) ---- */

static const char *const LAYOUT_NAMES[] = { "bd", "mac", "classic", "win11", "win" };
static const char *const CHROME_NAMES[] = { "modern", "bevel", "luna", "aero", "metro", "dock" };

/* colour keys, in the order color_field() maps them to fields */
static const char *const COLOR_KEYS[] = {
	"bg", "surface", "surface2", "surface3", "line",
	"text", "text_dim", "text_faint", "accent", "accent2",
	"panel", "wall_top", "wall_bottom", "wall_glow"
};
#define N_COLOR_KEYS ((int)(sizeof(COLOR_KEYS) / sizeof(COLOR_KEYS[0])))

static bu_color *color_field(bd_skin *s, int i)
{
	switch (i) {
	case 0: return &s->bg;
	case 1: return &s->surface;
	case 2: return &s->surface2;
	case 3: return &s->surface3;
	case 4: return &s->line;
	case 5: return &s->text;
	case 6: return &s->text_dim;
	case 7: return &s->text_faint;
	case 8: return &s->accent;
	case 9: return &s->accent2;
	case 10: return &s->panel;
	case 11: return &s->wall_top;
	case 12: return &s->wall_bottom;
	default: return &s->wall_glow;
	}
}

static char *trim(char *p)
{
	while (isspace((unsigned char)*p))
		p++;
	char *end = p + strlen(p);
	while (end > p && isspace((unsigned char)end[-1]))
		*--end = 0;
	return p;
}

/* "#RRGGBB" (the '#' is optional). 1 on success. */
static int parse_color(const char *v, bu_color *out)
{
	if (*v == '#')
		v++;
	if (strlen(v) != 6)
		return 0;
	for (int i = 0; i < 6; i++)
		if (!isxdigit((unsigned char)v[i]))
			return 0;
	unsigned long n = strtoul(v, NULL, 16);
	out->r = (uint8_t)((n >> 16) & 0xff);
	out->g = (uint8_t)((n >> 8) & 0xff);
	out->b = (uint8_t)(n & 0xff);
	out->a = 255;
	return 1;
}

/* Accept a name from the table or its numeric value. */
static int parse_named(const char *v, const char *const *names, int n, int *out)
{
	for (int i = 0; i < n; i++)
		if (strcmp(v, names[i]) == 0) {
			*out = i;
			return 1;
		}
	char *end;
	long num = strtol(v, &end, 10);
	if (end != v && *end == 0 && num >= 0 && num < n) {
		*out = (int)num;
		return 1;
	}
	return 0;
}

/* Built-in to inherit from when a file names no base: the
 * representative skin of its layout family. */
static int layout_base(int layout)
{
	switch (layout) {
	case BD_LAYOUT_MAC: return 8;		/* macOS */
	case BD_LAYOUT_CLASSIC: return 7;	/* クラシック Mac */
	case BD_LAYOUT_WIN11: return 15;	/* Windows 11 */
	case BD_LAYOUT_WIN: return 14;		/* Windows 10 */
	default: return 1;			/* Brainux */
	}
}

/* base= value: a built-in's exact name or its index. -1 if none. */
static int find_builtin(const char *v)
{
	char *end;
	long num = strtol(v, &end, 10);
	if (end != v && *end == 0) {
		if (num >= 0 && num < N_BUILTIN)
			return (int)num;
		return -1;
	}
	for (int i = 0; i < N_BUILTIN; i++)
		if (strcmp(v, BUILTIN_SKINS[i].name) == 0)
			return i;
	return -1;
}

/* Parse one .skin file into *out (out->name is strdup'd).
 * Returns 1 when the file held at least one recognised key;
 * unreadable or content-free files return 0 and are skipped. */
static int parse_skin_file(const char *path, const char *fallback_name, bd_skin *out)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return 0;
	char name[64] = "";
	char base_spec[64] = "";
	int have_layout = 0, layout_v = BD_LAYOUT_BD;
	int have_chrome = 0, chrome_v = 0;
	int have_radius = 0, radius_v = 0;
	int have_panel_h = 0, panel_h_v = 0;
	bu_color cols[N_COLOR_KEYS];
	int colset[N_COLOR_KEYS] = { 0 };
	int recognised = 0;
	char line[512];
	while (fgets(line, sizeof(line), fp)) {
		char *p = trim(line);
		if (!*p || *p == '#' || *p == ';')
			continue;
		char *eq = strchr(p, '=');
		if (!eq)
			continue;
		*eq = 0;
		char *key = trim(p);
		char *val = trim(eq + 1);
		if (!*key)
			continue;
		if (strcmp(key, "name") == 0) {
			snprintf(name, sizeof(name), "%s", val);
			recognised++;
		} else if (strcmp(key, "base") == 0) {
			snprintf(base_spec, sizeof(base_spec), "%s", val);
			recognised++;
		} else if (strcmp(key, "layout") == 0) {
			if (parse_named(val, LAYOUT_NAMES, 5, &layout_v)) {
				have_layout = 1;
				recognised++;
			}
		} else if (strcmp(key, "chrome") == 0) {
			if (parse_named(val, CHROME_NAMES, 6, &chrome_v)) {
				have_chrome = 1;
				recognised++;
			}
		} else if (strcmp(key, "radius") == 0) {
			radius_v = atoi(val);
			if (radius_v < 0)
				radius_v = 0;
			if (radius_v > 32)
				radius_v = 32;
			have_radius = 1;
			recognised++;
		} else if (strcmp(key, "panel_h") == 0) {
			panel_h_v = atoi(val);
			if (panel_h_v < 0)
				panel_h_v = 0;
			if (panel_h_v > 80)
				panel_h_v = 80;
			have_panel_h = 1;
			recognised++;
		} else {
			for (int i = 0; i < N_COLOR_KEYS; i++) {
				if (strcmp(key, COLOR_KEYS[i]) != 0)
					continue;
				bu_color c;
				if (parse_color(val, &c)) {
					cols[i] = c;
					colset[i] = 1;
					recognised++;
				}
				break;
			}
		}
	}
	fclose(fp);
	if (!recognised)
		return 0;

	/* Compose: start from the base built-in, then apply the
	 * keys the file actually set. */
	int base_idx = base_spec[0] ? find_builtin(base_spec) : -1;
	int layout = have_layout ? layout_v
		: (base_idx >= 0 ? BUILTIN_SKINS[base_idx].layout : BD_LAYOUT_BD);
	if (base_idx < 0)
		base_idx = layout_base(layout);
	bd_skin s = BUILTIN_SKINS[base_idx];
	s.layout = layout;
	if (have_chrome)
		s.chrome = chrome_v;
	if (have_radius)
		s.radius = radius_v;
	if (have_panel_h)
		s.panel_h = panel_h_v;
	for (int i = 0; i < N_COLOR_KEYS; i++)
		if (colset[i])
			*color_field(&s, i) = cols[i];
	s.name = strdup(name[0] ? name : fallback_name);
	if (!s.name)
		return 0;
	*out = s;
	return 1;
}

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int name_seen(char **seen, int n, const char *base)
{
	for (int i = 0; i < n; i++)
		if (strcmp(seen[i], base) == 0)
			return 1;
	return 0;
}

/* Scan one directory for *.skin files (alphabetical order) and
 * append the valid ones to extra[]. Basenames already in seen[]
 * are skipped, so an earlier directory wins over a later one. */
static void scan_skin_dir(const char *dir, bd_skin **extra, int *n, int *cap,
			  char ***seen, int *nseen)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	char **names = NULL;
	int nn = 0, ncap = 0;
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		const char *nm = de->d_name;
		size_t len = strlen(nm);
		if (len < 6 || strcmp(nm + len - 5, ".skin") != 0)
			continue;
		if (name_seen(*seen, *nseen, nm))
			continue;
		if (nn == ncap) {
			int nc = ncap ? ncap * 2 : 16;
			char **grown = realloc(names, nc * sizeof(*grown));
			if (!grown)
				break;
			names = grown;
			ncap = nc;
		}
		names[nn] = strdup(nm);
		if (names[nn])
			nn++;
	}
	closedir(d);
	if (nn > 1)
		qsort(names, (size_t)nn, sizeof(*names), cmp_str);
	for (int i = 0; i < nn; i++) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		char fb[64];
		snprintf(fb, sizeof(fb), "%s", names[i]);
		fb[strlen(fb) - 5] = 0;	/* fallback name: file stem */
		bd_skin s;
		if (parse_skin_file(path, fb, &s)) {
			if (*n == *cap) {
				int nc = *cap ? *cap * 2 : 8;
				bd_skin *grown = realloc(*extra, nc * sizeof(*grown));
				if (!grown) {
					free((void *)s.name);
					break;
				}
				*extra = grown;
				*cap = nc;
			}
			(*extra)[(*n)++] = s;
			char **grown = realloc(*seen, (*nseen + 1) * sizeof(*grown));
			if (grown) {
				*seen = grown;
				(*seen)[*nseen] = strdup(names[i]);
				if ((*seen)[*nseen])
					(*nseen)++;
			}
		}
		free(names[i]);
	}
	free(names);
}

void bd_skin_load_files(void)
{
	if (files_loaded)
		return;
	files_loaded = 1;
	bd_skin *extra = NULL;
	int n = 0, cap = 0;
	char **seen = NULL;
	int nseen = 0;
	const char *env = getenv("BRAINDESK_SKINS_DIR");
	if (env && *env)
		scan_skin_dir(env, &extra, &n, &cap, &seen, &nseen);
	char dir[512];
	const char *home = getenv("HOME");
	snprintf(dir, sizeof(dir), "%s/.config/braindesk/skins",
		 home ? home : "/home/user");
	scan_skin_dir(dir, &extra, &n, &cap, &seen, &nseen);
	scan_skin_dir("/usr/share/braindesk/skins", &extra, &n, &cap, &seen, &nseen);
	for (int i = 0; i < nseen; i++)
		free(seen[i]);
	free(seen);
	if (n == 0) {
		free(extra);
		return;
	}
	dyn_skins = malloc((size_t)(N_BUILTIN + n) * sizeof(*dyn_skins));
	if (!dyn_skins) {
		for (int i = 0; i < n; i++)
			free((void *)extra[i].name);
		free(extra);
		return;
	}
	memcpy(dyn_skins, BUILTIN_SKINS, (size_t)N_BUILTIN * sizeof(*dyn_skins));
	memcpy(dyn_skins + N_BUILTIN, extra, (size_t)n * sizeof(*dyn_skins));
	free(extra);
	BD_SKINS = dyn_skins;
	BD_NSKINS = N_BUILTIN + n;
}

static void skin_path(char *out, size_t n)
{
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.config/braindesk/skin", home ? home : "/home/user");
}

void bd_skin_load(void)
{
	/* File skins first: the saved index may point at one. */
	bd_skin_load_files();
	/* brainreg first (desktop.skin); the legacy file is the
	 * fallback for settings written before the store existed. */
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
	bd_skin_load_files();
	if (idx >= 0 && idx < BD_NSKINS)
		bd_skin_current = idx;
}
