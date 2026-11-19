/* braindesk scenes: the shell's screens, drawn with libbrainui.
 * The same drawing code is used by the host demo (screenshots),
 * the greeter and the shell — one visual language everywhere. */
#ifndef BD_SCENES_H
#define BD_SCENES_H

#include "bui.h"

enum { BD_VIEW_DESKTOP = 0, BD_VIEW_LAUNCHER = 1 };
enum { BD_ACT_NONE = 0, BD_ACT_QUIT = 1, BD_ACT_LOGIN = 2, BD_ACT_LAUNCH = 3, BD_ACT_ASSIST = 4 };
enum { BD_ASSIST_NONE = 0, BD_ASSIST_VOL_UP, BD_ASSIST_VOL_DOWN, BD_ASSIST_BRIGHT_UP,
       BD_ASSIST_BRIGHT_DOWN, BD_ASSIST_PGUP, BD_ASSIST_PGDN, BD_ASSIST_LAUNCHER, BD_ASSIST_LOCK };

typedef struct {
	int view;
	bool settings_open;
	int settings_cat;	/* 0 = 画面 */
	float brightness;	/* 0..1, mirrors the backlight */
	float volume;		/* 0..1, mirrors ALSA master */
	bool lid_sleep;
	bool key_sound;
	char clock[8];
	char date[40];
	int battery_pct;
	char launch_path[128];	/* set when BD_ACT_LAUNCH is returned */
	int skin;		/* current skin index (bui_skin) */
	int skin_scroll;	/* skin page scroll offset */
	/* panel + assist */
	int panel_h;		/* panel height px: 40 / 52 / 64 */
	bool vol_popup;
	bool assist_on;
	bool assist_open;
	int assist_action;	/* BD_ASSIST_* when BD_ACT_ASSIST returns */
	/* network page (filled by the shell from nmcli) */
	bool wifi_on;
	char wifi_ssid[64];
	char ip_addr[48];
	char net_list[10][64];
	int net_signal[10];
	int net_count;
	int net_scan_req;	/* bumped by scenes when the page opens */
	int net_connect;	/* SSID index to connect, -1 none */
	char net_status[96];
	/* system page (filled by the shell) */
	char sysinfo[6][96];
	/* mode page: profiles are user-defined files (brain-mode list);
	 * the shell fills these arrays, scenes only displays them */
	char mode_name[24];	/* active profile id */
	int mode_set;		/* profile index to apply, -1 = none pending */
	int mode_poll_req;	/* bumped when the mode page opens */
	char mode_ids[8][24];
	char mode_names[8][40];
	char mode_descs[8][96];
	char mode_launch[8][32];
	char mode_launch_label[8][48];
	int mode_count;
	int mode_scroll;
	/* interaction */
	int drag_slider;	/* 0 none, 1 brightness, 2 volume */
	bool start_pressed;
} bd_state;

void bd_state_init(bd_state *s);
void bd_update_datetime(bd_state *s); /* fills clock + date from local time */
void bd_draw_login(bu_canvas *c, bu_theme *t, bd_state *s, const char *user);
void bd_draw_shell(bu_canvas *c, bu_theme *t, bd_state *s);
/* Feed one pointer/key event; returns an action for the caller. */
int bd_handle_event(bd_state *s, int ev_type, int code, int value, int x, int y, bool *changed);

#endif
