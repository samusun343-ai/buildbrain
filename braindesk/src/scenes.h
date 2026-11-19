/* braindesk scenes: the shell's screens, drawn with libbrainui.
 * The same drawing code is used by the host demo (screenshots),
 * the greeter and the shell — one visual language everywhere. */
#ifndef BD_SCENES_H
#define BD_SCENES_H

#include "bui.h"

enum { BD_VIEW_DESKTOP = 0, BD_VIEW_LAUNCHER = 1 };
enum { BD_ACT_NONE = 0, BD_ACT_QUIT = 1, BD_ACT_LOGIN = 2 };

typedef struct {
	int view;
	bool settings_open;
	int settings_cat;	/* 0 = 画面 */
	float brightness;	/* 0..1, mirrors the backlight */
	float volume;		/* 0..1, mirrors ALSA master */
	bool lid_sleep;
	bool key_sound;
	char clock[8];
	int battery_pct;
	/* interaction */
	int drag_slider;	/* 0 none, 1 brightness, 2 volume */
	bool start_pressed;
} bd_state;

void bd_state_init(bd_state *s);
void bd_draw_login(bu_canvas *c, bu_theme *t, bd_state *s, const char *user);
void bd_draw_shell(bu_canvas *c, bu_theme *t, bd_state *s);
/* Feed one pointer/key event; returns an action for the caller. */
int bd_handle_event(bd_state *s, int ev_type, int code, int value, int x, int y, bool *changed);

#endif
