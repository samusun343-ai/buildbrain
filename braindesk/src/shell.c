/* braindesk shell: the desktop process. Owns the framebuffer and
 * all evdev input, composites the scenes, and applies settings to
 * the real backends (backlight sysfs today; ALSA and logind join
 * through the same hooks). No display server in between. */
#include "bui.h"
#include "bui_fb.h"
#include "bui_input.h"
#include "scenes.h"
#include "bui_wm.h"
#include "bui_alsa.h"
#include "bui_skin.h"
#include "brainreg.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <time.h>
#include <glob.h>

static bu_theme *T;

/* scenes.c overlay hooks: task switcher, control centre and
 * toasts. Their state lives inside scenes.c, so they are
 * declared here instead of in scenes.h. */
extern void bd_scenes_tick(bd_state *s, bool *changed);
extern void bd_toast_push(const char *text);
extern void bd_switcher_cycle(void);
extern int bd_switcher_is_open(void);
extern void bd_switcher_open(void);
extern void bd_cc_show(void);
extern void bd_launcher_show_all(void);
extern void bd_recent_seed(void);

#include <sys/utsname.h>
#include <linux/uinput.h>
#include <fcntl.h>

/* --- uinput key injection for the assist menu (PgUp/PgDn) --- */
static int uinput_fd = -1;
static void uinput_setup(void)
{
	uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (uinput_fd < 0)
		return;
	ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(uinput_fd, UI_SET_KEYBIT, KEY_PAGEUP);
	ioctl(uinput_fd, UI_SET_KEYBIT, KEY_PAGEDOWN);
	struct uinput_user_dev udev;
	memset(&udev, 0, sizeof(udev));
	snprintf(udev.name, UINPUT_MAX_NAME_SIZE, "braindesk-assist");
	udev.id.bustype = BUS_VIRTUAL;
	if (write(uinput_fd, &udev, sizeof(udev)) < 0 || ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
		close(uinput_fd);
		uinput_fd = -1;
	}
}
static void uinput_key(int code)
{
	if (uinput_fd < 0)
		uinput_setup();
	if (uinput_fd < 0)
		return;
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = EV_KEY;
	ev.code = code;
	ev.value = 1;
	if (write(uinput_fd, &ev, sizeof(ev)) < 0) return;
	ev.value = 0;
	if (write(uinput_fd, &ev, sizeof(ev)) < 0) return;
	memset(&ev, 0, sizeof(ev));
	ev.type = EV_SYN;
	ev.code = SYN_REPORT;
	if (write(uinput_fd, &ev, sizeof(ev)) < 0) return;
}

/* --- panel preferences (size + assist) --- */
static void panel_prefs_path(char *out, size_t n)
{
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.config/braindesk/panel", home ? home : "/home/user");
}
static void panel_prefs_load(bd_state *s)
{
	/* brainreg first; legacy file as fallback (see bd_skin_load).
	 * Each key is independent: either one present in the store
	 * is enough to take the store's values. */
	char *reg_ph = brainreg_get("desktop", "panel_height");
	char *reg_as = brainreg_get("desktop", "assist");
	if (reg_ph || reg_as) {
		if (reg_ph) {
			int ph = atoi(reg_ph);
			if (ph == 36 || ph == 42 || ph == 52)
				s->panel_h = ph;
		}
		if (reg_as)
			s->assist_on = strcmp(reg_as, "true") == 0 ||
				       strcmp(reg_as, "1") == 0;
		free(reg_ph);
		free(reg_as);
		return;
	}
	char path[512];
	panel_prefs_path(path, sizeof(path));
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	int ph, as;
	if (fscanf(fp, "%d %d", &ph, &as) == 2) {
		if (ph == 36 || ph == 42 || ph == 52)
			s->panel_h = ph;
		s->assist_on = as != 0;
	}
	fclose(fp);
}
static void panel_prefs_save(bd_state *s)
{
	char val[16];
	snprintf(val, sizeof(val), "%d", s->panel_h);
	brainreg_set("desktop", "panel_height", val);
	brainreg_set("desktop", "assist", s->assist_on ? "true" : "false");
	char path[512];
	panel_prefs_path(path, sizeof(path));
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
		fprintf(fp, "%d %d\n", s->panel_h, s->assist_on ? 1 : 0);
		fclose(fp);
	}
}
/* --- settings data providers (network / system pages) --- */
static void fill_mode(bd_state *s)
{
	FILE *fp = popen("brain-mode list 2>/dev/null", "r");
	if (fp) {
		char line[256];
		int n = 0;
		while (n < 8 && fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\r\n")] = 0;
			/* id|name|desc|launch|launch_label */
			char *f[5] = { 0 };
			char *p = line;
			for (int k = 0; k < 5; k++) {
				f[k] = p;
				char *bar = strchr(p, '|');
				if (bar) { *bar = 0; p = bar + 1; }
				else break;
			}
			if (!f[0] || !f[0][0]) continue;
			snprintf(s->mode_ids[n], 24, "%s", f[0]);
			snprintf(s->mode_names[n], 40, "%s", f[1] ? f[1] : f[0]);
			snprintf(s->mode_descs[n], 96, "%s", f[2] ? f[2] : "");
			snprintf(s->mode_launch[n], 32, "%s", f[3] ? f[3] : "");
			snprintf(s->mode_launch_label[n], 48, "%s", f[4] ? f[4] : "");
			n++;
		}
		if (n > 0) s->mode_count = n;
		pclose(fp);
	}
	fp = popen("brain-mode get 2>/dev/null", "r");
	if (!fp) return;
	char buf[32] = "";
	if (fgets(buf, sizeof(buf), fp)) {
		buf[strcspn(buf, "\r\n")] = 0;
		if (buf[0]) snprintf(s->mode_name, sizeof(s->mode_name), "%s", buf);
	}
	pclose(fp);
}

static void fill_sysinfo(bd_state *s)
{
	struct utsname un;
	uname(&un);
	snprintf(s->sysinfo[0], 96, "カーネル   Linux %s", un.release);
	snprintf(s->sysinfo[1], 96, "アーキテクチャ   %s", un.machine);
	FILE *fp = fopen("/proc/device-tree/model", "r");
	if (fp) {
		char m[80] = "";
		if (fgets(m, sizeof(m), fp))
			snprintf(s->sysinfo[2], 96, "機種   %s", m);
		fclose(fp);
	}
	if (!s->sysinfo[2][0])
		snprintf(s->sysinfo[2], 96, "機種   SHARP Brain (Brainux)");
	long total = 0;
	fp = fopen("/proc/meminfo", "r");
	if (fp) {
		char line[128];
		while (fgets(line, sizeof(line), fp))
			sscanf(line, "MemTotal: %ld", &total);
		fclose(fp);
	}
	snprintf(s->sysinfo[3], 96, "メモリ   %.0f MB", total / 1024.0);
	snprintf(s->sysinfo[4], 96, "braindesk   テーマ: %s", bd_skin_get()->name);
	snprintf(s->sysinfo[5], 96, "セッション   braindesk (fbdev / libbrainui)");
}

static long ps_read_long(const char *dir, const char *name)
{
	char path[320];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *fp = fopen(path, "r");
	if (!fp)
		return -1;
	long v = -1;
	if (fscanf(fp, "%ld", &v) != 1)
		v = -1;
	fclose(fp);
	return v;
}

static void ps_read_str(const char *dir, const char *name, char *out, size_t n)
{
	char path[320];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *fp = fopen(path, "r");
	if (!fp) {
		out[0] = 0;
		return;
	}
	if (!fgets(out, n, fp))
		out[0] = 0;
	fclose(fp);
	out[strcspn(out, "\n")] = 0;
}

/* Keyboard device display name for the settings page: the
 * first /sys/class/input node whose name mentions a keyboard. */
static void fill_kbd_name(bd_state *s)
{
	s->kbd_name[0] = 0;
	DIR *d = opendir("/sys/class/input");
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5))
			continue;
		char path[320], name[64];
		snprintf(path, sizeof(path), "/sys/class/input/%s/device", e->d_name);
		ps_read_str(path, "name", name, sizeof(name));
		if (strstr(name, "keyboard") || strstr(name, "kbd") ||
		    strstr(name, "Keyboard")) {
			snprintf(s->kbd_name, sizeof(s->kbd_name), "%s", name);
			break;
		}
		if (!s->kbd_name[0] && name[0])
			snprintf(s->kbd_name, sizeof(s->kbd_name), "%s", name);
	}
	closedir(d);
}

static void input_prefs_load(bd_state *s)
{
	s->kbd_repeat = brainreg_get_bool("keyboard", "repeat", true);
	s->kbd_rate = brainreg_get_int("keyboard", "rate", 1);
	if (s->kbd_rate < 0 || s->kbd_rate > 2)
		s->kbd_rate = 1;
	s->osk_kana = brainreg_get_bool("input", "osk_kana_default", true);
}

static void apply_kbd_repeat(bu_input *input, bd_state *s)
{
	static const int periods[3] = { 400, 250, 120 };
	bu_input_set_repeat(input, 400,
			    s->kbd_repeat ? periods[s->kbd_rate] : 0);
}

/* Battery state from /sys/class/power_supply. The Brain's gauge
 * may expose charge_* (uAh) or only capacity (%); the time
 * estimate is computed only when both charge and current are
 * available, and the page degrades gracefully without them. */
static void fill_battery(bd_state *s)
{
	s->batt_present = 0;
	s->batt_pct = 0;
	s->ac_online = 0;
	s->batt_status[0] = 0;
	s->batt_line1[0] = 0;
	s->batt_line2[0] = 0;
	DIR *d = opendir("/sys/class/power_supply");
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		char dir[280], type[32];
		snprintf(dir, sizeof(dir), "/sys/class/power_supply/%s", e->d_name);
		ps_read_str(dir, "type", type, sizeof(type));
		if (!strcmp(type, "Battery")) {
			char st[32];
			ps_read_str(dir, "status", st, sizeof(st));
			long cap = ps_read_long(dir, "capacity");
			long volt = ps_read_long(dir, "voltage_now");
			long cur = ps_read_long(dir, "current_now");
			long chg = ps_read_long(dir, "charge_now");
			long full = ps_read_long(dir, "charge_full");
			s->batt_present = 1;
			s->batt_pct = cap < 0 ? 0 : cap > 100 ? 100 : (int)cap;
			const char *jp = "不明";
			if (!strcmp(st, "Charging"))
				jp = "充電中";
			else if (!strcmp(st, "Discharging"))
				jp = "放電中 (バッテリー駆動)";
			else if (!strcmp(st, "Full"))
				jp = "満充電";
			else if (!strcmp(st, "Not charging"))
				jp = "充電していません";
			snprintf(s->batt_status, sizeof(s->batt_status), "%s", jp);
			if (volt > 0 && cur >= 0)
				snprintf(s->batt_line1, sizeof(s->batt_line1),
					 "電圧 %.2f V   電流 %.0f mA   消費 %.1f W",
					 volt / 1e6, cur / 1e3,
					 volt / 1e6 * cur / 1e6);
			else if (volt > 0)
				snprintf(s->batt_line1, sizeof(s->batt_line1),
					 "電圧 %.2f V", volt / 1e6);
			if (cur > 0 && chg > 0 && !strcmp(st, "Discharging")) {
				double h = (double)chg / cur;
				snprintf(s->batt_line2, sizeof(s->batt_line2),
					 "推定残り時間: 約 %d 時間 %02d 分",
					 (int)h, (int)((h - (int)h) * 60));
			} else if (cur > 0 && chg > 0 && full > chg &&
				   !strcmp(st, "Charging")) {
				double h = (double)(full - chg) / cur;
				snprintf(s->batt_line2, sizeof(s->batt_line2),
					 "満充電まで: 約 %d 時間 %02d 分",
					 (int)h, (int)((h - (int)h) * 60));
			}
		} else if (!strcmp(type, "Mains") || !strcmp(type, "USB")) {
			if (ps_read_long(dir, "online") == 1)
				s->ac_online = 1;
		}
	}
	closedir(d);
}

static void fill_netinfo(bd_state *s)
{
	FILE *pp = popen("nmcli -t -f ACTIVE,SSID dev wifi 2>/dev/null | grep '^yes' | cut -d: -f2", "r");
	if (pp) {
		char ssid[64] = "";
		if (fgets(ssid, sizeof(ssid), pp))
			ssid[strcspn(ssid, "\n")] = 0;
		pclose(pp);
		snprintf(s->wifi_ssid, sizeof(s->wifi_ssid), "%s", ssid[0] ? ssid : "未接続");
	}
	pp = popen("hostname -I 2>/dev/null | awk '{print $1}'", "r");
	if (pp) {
		char ip[48] = "";
		if (fgets(ip, sizeof(ip), pp))
			ip[strcspn(ip, "\n")] = 0;
		pclose(pp);
		snprintf(s->ip_addr, sizeof(s->ip_addr), "%s", ip[0] ? ip : "—");
	}
	pp = popen("nmcli -t -f SSID,SIGNAL dev wifi list 2>/dev/null | head -10", "r");
	if (pp) {
		char line[128];
		s->net_count = 0;
		while (fgets(line, sizeof(line), pp) && s->net_count < 10) {
			char *colon = strrchr(line, ':');
			if (!colon)
				continue;
			*colon = 0;
			if (!line[0])
				continue;
			snprintf(s->net_list[s->net_count], 64, "%s", line);
			s->net_signal[s->net_count] = atoi(colon + 1);
			s->net_count++;
		}
		pclose(pp);
	}
	/* connect result from the helper child, if any */
	FILE *st = fopen("/tmp/bd-net-status", "r");
	if (st) {
		if (fgets(s->net_status, sizeof(s->net_status), st))
			s->net_status[strcspn(s->net_status, "\n")] = 0;
		fclose(st);
		remove("/tmp/bd-net-status");
	}
}


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
	char before_c[8], before_d[40];
	strcpy(before_c, s->clock);
	strcpy(before_d, s->date);
	bd_update_datetime(s);
	if (strcmp(before_c, s->clock) != 0 || strcmp(before_d, s->date) != 0)
		*changed = true;
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
	bd_skin_load();
	bd_skin_apply(T);
	bd_state s;
	bd_state_init(&s);
	s.skin = bd_skin_current;
	int last_skin = s.skin;
	panel_prefs_load(&s);
	input_prefs_load(&s);
	fill_kbd_name(&s);
	bool last_kbd_repeat = s.kbd_repeat;
	int last_kbd_rate = s.kbd_rate;
	bool last_osk_kana = s.osk_kana;
	{
		const char *e;
		if ((e = getenv("BD_SHOT_PANEL"))) s.panel_h = atoi(e);
		if (getenv("BD_SHOT_LAUNCH")) s.view = BD_VIEW_LAUNCHER;
		if (getenv("BD_SHOT_ALLAPPS")) { s.view = BD_VIEW_LAUNCHER; bd_launcher_show_all(); }
		if (getenv("BD_SHOT_RECENT")) bd_recent_seed();
		if (getenv("BD_SHOT_ASSIST")) s.assist_on = true;
		if (getenv("BD_SHOT_ASSIST_OPEN")) { s.assist_on = true; s.assist_open = true; }
		if (getenv("BD_SHOT_VOLPOP")) s.vol_popup = true;
		if (getenv("BD_SHOT_CC")) {
			bd_cc_show();
			s.mode_count = 4;
			const char *ids[4] = { "normal", "deck", "server", "share" };
			const char *nms[4] = { "通常", "デッキ", "サーバ", "共有" };
			for (int i = 0; i < 4; i++) {
				snprintf(s.mode_ids[i], 24, "%s", ids[i]);
				snprintf(s.mode_names[i], 40, "%s", nms[i]);
			}
			snprintf(s.mode_name, sizeof(s.mode_name), "deck");
		}
		if (getenv("BD_SHOT_TOAST"))
			bd_toast_push("音量 45%");
		if (getenv("BD_SHOT_MODE")) {
			s.settings_open = true; s.settings_cat = 8;
			s.mode_count = 4;
			const char *ids[4] = { "normal", "deck", "server", "share" };
			const char *nms[4] = { "通常", "デッキ", "サーバ", "共有" };
			const char *dss[4] = { "デスクトップとして使う。USB はネットワークのみ", "PC のコントロールデッキになる (HID キーボード+メディアキー)", "小型サーバの束を起動する (DNS / MQTT / 共有)", "USB メモリとしてファイルを渡す + ネットワーク" };
			for (int i = 0; i < 4; i++) {
				snprintf(s.mode_ids[i], 24, "%s", ids[i]);
				snprintf(s.mode_names[i], 40, "%s", nms[i]);
				snprintf(s.mode_descs[i], 96, "%s", dss[i]);
			}
			snprintf(s.mode_launch[1], 32, "bd-deck");
			snprintf(s.mode_launch_label[1], 48, "コントロールデッキを開く");
			snprintf(s.mode_name, sizeof(s.mode_name), "deck");
		}
		if (getenv("BD_SHOT_KBD")) {
			s.settings_open = true; s.settings_cat = 3;
			snprintf(s.kbd_name, sizeof(s.kbd_name), "brain-keyboard");
			s.kbd_repeat = true; s.kbd_rate = 1;
		}
		if (getenv("BD_SHOT_INPUT")) {
			s.settings_open = true; s.settings_cat = 6;
			s.osk_kana = true;
		}
		if (getenv("BD_SHOT_BATT")) {
			s.settings_open = true; s.settings_cat = 5;
			s.batt_present = 1; s.batt_pct = 68; s.ac_online = 0;
			snprintf(s.batt_status, sizeof(s.batt_status), "放電中 (バッテリー駆動)");
			snprintf(s.batt_line1, sizeof(s.batt_line1), "電圧 3.92 V   電流 412 mA   消費 1.6 W");
			snprintf(s.batt_line2, sizeof(s.batt_line2), "推定残り時間: 約 2 時間 41 分");
		}
	}
	fill_mode(&s);
	fill_sysinfo(&s);
	if (!getenv("BD_SHOT_BATT"))
		fill_battery(&s);
	int last_panel_h = s.panel_h;
	bool last_assist_on = s.assist_on;
	int last_scan_req = 0, last_connect = -1, last_mode_poll = 0;
	bool last_wifi_on = s.wifi_on;
	time_t last_net_poll = 0;
	time_t last_batt_poll = 0;
	update_clock(&s, &(bool){ 0 });
	bu_alsa *alsa = bu_alsa_open();
	if (alsa) {
		float v = bu_alsa_get_volume(alsa);
		if (v >= 0)
			s.volume = v;
	}

	bd_wm_server_start(NULL);
	{
		/* Push the work area before any window can be placed
		 * (the smoke test launches apps before the first draw). */
		bu_rect wa;
		bd_workarea(&wa);
		bd_wm_set_workarea(wa.x, wa.y, wa.w, wa.h);
	}
	if (shot && getenv("BD_SHOT_WM")) {
		/* WM smoke test: launch two apps windowed, let them
		 * draw, and dump the composited desktop. */
		const char *apps[2] = { "./bd-calc", "./bd-clock" };
		for (int i = 0; i < 2; i++) {
			pid_t p = fork();
			if (p == 0) {
				execl(apps[i], apps[i], (char *)NULL);
				_exit(127);
			}
		}
		for (int i = 0; i < 50; i++) {
			bd_wm_poll();
			usleep(100000);
		}
		if (getenv("BD_SHOT_WM_MAX")) {
			bd_wm_set_state(0, BD_WM_MAX);
			bd_wm_raise_task(0);
		}
		if (getenv("BD_SHOT_WM_SNAP")) {
			bd_wm_set_state(0, BD_WM_SNAP_L);
			bd_wm_set_state(1, BD_WM_SNAP_R);
		}
		if (getenv("BD_SHOT_SWITCHER"))
			bd_switcher_open();
		bd_draw_shell(canvas, T, &s);
		bu_display_present(disp, canvas, (bu_rect){ 0, 0, W, H });
		bu_display_dump_ppm(disp, shot_path);
		fprintf(stderr, "braindesk: WM test frame, %d windows\n",
			bd_wm_window_count());
		return 0;
	}
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
	apply_kbd_repeat(input, &s);
	float last_brightness = s.brightness;
	float last_volume = s.volume;
	bool alt_held = false;
	time_t last_toast_poll = 0;
	for (;;) {
		bu_event ev;
		bool changed = false;
		if (bu_input_next(input, &ev, 250)) {
			/* Power button (PSWITCH) leaves the session, back
			 * to the greeter (lock). The session wrapper owns
			 * poweroff/reboot decisions at the greeter. */
			if (ev.type == BU_EV_KEY && ev.code == 116 && ev.value == 1)
				return 0;
			int act = BD_ACT_NONE;
			bool forwarded = false;
			bool consumed = false;
			if (ev.type == BU_EV_KEY && (ev.code == 56 || ev.code == 100))
				alt_held = ev.value != 0;
			if (ev.type == BU_EV_KEY && ev.code == 15 /* TAB */ &&
			    ev.value == 1 && alt_held) {
				/* Alt+Tab opens / cycles the task switcher */
				bd_switcher_cycle();
				changed = true;
				consumed = true;
			}
			if (!consumed) {
				if (ev.type == BU_EV_KEY && ev.code != 139 /* MENU */ &&
				    s.view == BD_VIEW_DESKTOP && !s.settings_open &&
				    !bd_switcher_is_open())
					forwarded = bd_wm_key(ev.code, ev.value) != 0;
				if (forwarded)
					changed = true;
				else
					act = bd_handle_event(&s, ev.type, ev.code, ev.value, ev.x, ev.y, &changed);
			}
			if (act == BD_ACT_ASSIST) {
				if (s.assist_action == BD_ASSIST_PGUP)
					uinput_key(KEY_PAGEUP);
				else if (s.assist_action == BD_ASSIST_PGDN)
					uinput_key(KEY_PAGEDOWN);
				else if (s.assist_action == BD_ASSIST_LOCK)
					return 0; /* back to the greeter */
			}
			if (act == BD_ACT_LAUNCH) {
				bool fullscreen_app = strstr(s.launch_path, "bd-deck") != NULL;
				pid_t pid = fork();
				if (pid == 0) {
					execl(s.launch_path, s.launch_path, (char *)NULL);
					_exit(127);
				}
				if (pid > 0 && fullscreen_app) {
					/* Full-screen apps (the control deck) own
					 * the display directly, as before: wait,
					 * then take the console back. Windowed
					 * apps instead run concurrently under the
					 * WM and are reaped in the loop. */
					int st;
					while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
						;
					bu_display_regrab(disp);
					bu_event drop;
					while (bu_input_next(input, &drop, 0))
						;
				}
				changed = true;
			}
		}
		bd_scenes_tick(&s, &changed);
		/* Toast drop file: external tools (brain-usb-mount and
		 * friends) post one notification per line; the shell
		 * picks them up about once a second and shows them. */
		if (time(NULL) != last_toast_poll) {
			last_toast_poll = time(NULL);
			const char *tf = getenv("BRAINDESK_TOAST_FILE");
			if (!tf) tf = "/tmp/braindesk.toast";
			FILE *tfp = fopen(tf, "r");
			if (tfp) {
				char line[128];
				while (fgets(line, sizeof(line), tfp)) {
					line[strcspn(line, "\r\n")] = 0;
					if (line[0])
						bd_toast_push(line);
				}
				fclose(tfp);
				remove(tf);
				changed = true;
			}
		}
		if (bd_wm_poll())
			changed = true;
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		update_clock(&s, &changed);
		if (s.brightness != last_brightness) {
			apply_brightness(s.brightness);
			last_brightness = s.brightness;
		}
		if (s.settings_open && s.settings_cat == 4 &&
		    (s.net_scan_req != last_scan_req || time(NULL) - last_net_poll > 10)) {
			last_scan_req = s.net_scan_req;
			last_net_poll = time(NULL);
			fill_netinfo(&s);
			changed = true;
		}
		if (s.settings_open && s.settings_cat == 5 &&
		    time(NULL) - last_batt_poll > 5) {
			last_batt_poll = time(NULL);
			fill_battery(&s);
			changed = true;
		}
		if (s.wifi_on != last_wifi_on) {
			last_wifi_on = s.wifi_on;
			if (system(s.wifi_on ? "nmcli radio wifi on 2>/dev/null" : "nmcli radio wifi off 2>/dev/null") != 0)
				snprintf(s.net_status, sizeof(s.net_status), "Wi-Fi 切替に失敗");
			s.net_scan_req++;
		}
		if (s.net_connect != last_connect && s.net_connect >= 0) {
			last_connect = s.net_connect;
			if (s.net_connect < s.net_count) {
				char cmd[256];
				snprintf(cmd, sizeof(cmd),
					 "(nmcli dev wifi connect '%s' >/dev/null 2>&1 && echo '接続しました' > /tmp/bd-net-status || echo '接続に失敗しました (保存済み/オープンのみ)' > /tmp/bd-net-status) &",
					 s.net_list[s.net_connect]);
				if (system(cmd) != 0)
					snprintf(s.net_status, sizeof(s.net_status), "接続を開始できませんでした");
				else
					snprintf(s.net_status, sizeof(s.net_status), "接続中...");
			}
			s.net_connect = -1;
			last_connect = -1;
		}
		if (s.settings_open && s.settings_cat == 8 && s.mode_poll_req != last_mode_poll) {
			last_mode_poll = s.mode_poll_req;
			fill_mode(&s);
			changed = true;
		}
		if (s.mode_set >= 0 && s.mode_set < s.mode_count) {
			char cmd[96];
			snprintf(cmd, sizeof(cmd), "brain-mode set %s >/dev/null 2>&1", s.mode_ids[s.mode_set]);
			if (system(cmd) == 0) {
				char tb[96];
				snprintf(tb, sizeof(tb), "モード: %s", s.mode_names[s.mode_set]);
				bd_toast_push(tb);
				snprintf(s.mode_name, sizeof(s.mode_name), "%s", s.mode_ids[s.mode_set]);
			}
			s.mode_set = -1;
			changed = true;
		}
		if (s.kbd_repeat != last_kbd_repeat || s.kbd_rate != last_kbd_rate) {
			apply_kbd_repeat(input, &s);
			brainreg_set("keyboard", "repeat", s.kbd_repeat ? "true" : "false");
			char rb[8];
			snprintf(rb, sizeof(rb), "%d", s.kbd_rate);
			brainreg_set("keyboard", "rate", rb);
			last_kbd_repeat = s.kbd_repeat;
			last_kbd_rate = s.kbd_rate;
		}
		if (s.osk_kana != last_osk_kana) {
			brainreg_set("input", "osk_kana_default", s.osk_kana ? "true" : "false");
			last_osk_kana = s.osk_kana;
		}
		if (s.panel_h != last_panel_h || s.assist_on != last_assist_on) {
			panel_prefs_save(&s);
			last_panel_h = s.panel_h;
			last_assist_on = s.assist_on;
		}
		if (s.skin != last_skin) {
			bd_skin_set(s.skin);
			bd_skin_apply(T);
			bd_skin_save(s.skin);
			last_skin = s.skin;
		}
		if (alsa && s.volume != last_volume) {
			bu_alsa_set_volume(alsa, s.volume);
			last_volume = s.volume;
		}
		if (changed) {
			bd_draw_shell(canvas, T, &s);
			bu_display_present(disp, canvas, (bu_rect){ 0, 0, W, H });
		}
	}
	return 0;
}
