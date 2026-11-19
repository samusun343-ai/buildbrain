/* braindesk shell: the desktop process. Owns the framebuffer and
 * all evdev input, composites the scenes, and applies settings to
 * the real backends (backlight sysfs today; ALSA and logind join
 * through the same hooks). No display server in between. */
#include "bui.h"
#include "bui_fb.h"
#include "bui_input.h"
#include "scenes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <glob.h>

static bu_theme *T;

static void apply_brightness(float frac)
{
	/* /sys/class/backlight/<dev>/brightness, scaled by max_brightness */
	glob_t g;
	if (glob("/sys/class/backlight/*/max_brightness", 0, NULL, &g) != 0 || g.gl_pathc == 0)
		return;
	char path[256];
	snprintf(path, sizeof(path), "%s", g.gl_pathv[0]);
	FILE *f = fopen(path, "r");
	int max = 0;
	if (f) { if (fscanf(f, "%d", &max) != 1) max = 0; fclose(f); }
	char *slash = strrchr(path, '/');
	if (slash) strcpy(slash + 1, "brightness");
	f = fopen(path, "w");
	if (f && max > 0) {
		fprintf(f, "%d\n", (int)(frac * max + 0.5f));
		fclose(f);
	}
	globfree(&g);
}

static void update_clock(bd_state *s, bool *changed)
{
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char buf[8];
	snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
	if (strcmp(buf, s->clock) != 0) {
		strcpy(s->clock, buf);
		*changed = true;
	}
}

int main(int argc, char **argv)
{
	const char *font_path = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
	bool shot = false;
	const char *shot_path = "shell.ppm";
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--font") == 0 && i + 1 < argc)
			font_path = argv[++i];
		else if (strcmp(argv[i], "--shot") == 0) {
			shot = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				shot_path = argv[++i];
		}
	}
	T = &bu_theme_dark;
	T->font = bu_font_load(font_path);
	if (!T->font) {
		fprintf(stderr, "braindesk: font load failed: %s\n", font_path);
		return 1;
	}
	bu_display *disp = bu_display_open(854, 480);
	int W = bu_display_width(disp), H = bu_display_height(disp);
	bu_canvas *canvas = bu_canvas_new(W, H);
	bd_state s;
	bd_state_init(&s);
	update_clock(&s, &(bool){ 0 });

	bd_draw_shell(canvas, T, &s);
	bu_display_present(disp, canvas, (bu_rect){ 0, 0, W, H });
	if (shot) {
		if (!bu_display_is_real(disp))
			bu_display_dump_ppm(disp, shot_path);
		fprintf(stderr, "braindesk: rendered one frame (%s, fb=%s)\n",
			shot_path, bu_display_is_real(disp) ? "real" : "memory");
		return 0;
	}

	bu_input *input = bu_input_open(W, H);
	float last_brightness = s.brightness;
	for (;;) {
		bu_event ev;
		bool changed = false;
		if (bu_input_next(input, &ev, 250)) {
			bd_handle_event(&s, ev.type, ev.code, ev.value, ev.x, ev.y, &changed);
		}
		update_clock(&s, &changed);
		if (s.brightness != last_brightness) {
			apply_brightness(s.brightness);
			last_brightness = s.brightness;
		}
		if (changed) {
			bd_draw_shell(canvas, T, &s);
			bu_display_present(disp, canvas, (bu_rect){ 0, 0, W, H });
		}
	}
	return 0;
}
