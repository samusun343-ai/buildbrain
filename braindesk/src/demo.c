/* Host demo: renders the three braindesk scenes to PPM files. */
#include "bui.h"
#include "scenes.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	const char *font_path = argc > 1 ? argv[1] : "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
	bu_theme *T = &bu_theme_dark;
	T->font = bu_font_load(font_path);
	if (!T->font) {
		fprintf(stderr, "font load failed: %s\n", font_path);
		return 1;
	}
	bu_canvas *c = bu_canvas_new(854, 480);
	bd_state s;
	bd_state_init(&s);

	bd_draw_login(c, T, &s, "brain");
	bu_canvas_write_ppm(c, "login.ppm");

	bd_draw_shell(c, T, &s);
	bu_canvas_write_ppm(c, "desktop_base.ppm");

	s.view = BD_VIEW_LAUNCHER;
	bd_draw_shell(c, T, &s);
	bu_canvas_write_ppm(c, "desktop.ppm");

	s.view = BD_VIEW_DESKTOP;
	s.settings_open = true;
	bd_draw_shell(c, T, &s);
	bu_canvas_write_ppm(c, "settings.ppm");

	printf("done\n");
	return 0;
}
