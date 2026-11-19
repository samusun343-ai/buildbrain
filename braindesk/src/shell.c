/* braindesk shell: the desktop process. Owns the framebuffer and
 * all evdev input, composites the scenes, and applies settings to
 * the real backends (backlight sysfs today; ALSA and logind join
 * through the same hooks). No display server in between. */
#include "bui.h"
#include "bui_fb.h"
#include "bui_input.h"
#include "scenes.h"
#include "bui_alsa.h"
#include "bui_skin.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <glob.h>

static bu_theme *T;
#include <sys/utsname.h>
/* --- settings data providers (network / system pages) --- */
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
	snprintf(s->sysinfo[4], 96, "braindesk   スキン: %s", bd_skin_get()->name);
	snprintf(s->sysinfo[5], 96, "セッション   braindesk (fbdev / libbrainui)");
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
	fill_sysinfo(&s);
	int last_scan_req = 0, last_connect = -1;
	bool last_wifi_on = s.wifi_on;
	time_t last_net_poll = 0;
	update_clock(&s, &(bool){ 0 });
	bu_alsa *alsa = bu_alsa_open();
	if (alsa) {
		float v = bu_alsa_get_volume(alsa);
		if (v >= 0)
			s.volume = v;
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
	float last_brightness = s.brightness;
	float last_volume = s.volume;
	for (;;) {
		bu_event ev;
		bool changed = false;
		if (bu_input_next(input, &ev, 250)) {
			/* Power button (PSWITCH) leaves the session, back
			 * to the greeter (lock). The session wrapper owns
			 * poweroff/reboot decisions at the greeter. */
			if (ev.type == BU_EV_KEY && ev.code == 116 && ev.value == 1)
				return 0;
			int act = bd_handle_event(&s, ev.type, ev.code, ev.value, ev.x, ev.y, &changed);
			if (act == BD_ACT_LAUNCH) {
				/* Run the app in the foreground; it opens the
				 * display itself. Afterwards take the console
				 * back (the app restores KD_TEXT on exit) and
				 * drop input events queued while it ran. */
				pid_t pid = fork();
				if (pid == 0) {
					execl(s.launch_path, s.launch_path, (char *)NULL);
					_exit(127);
				}
				if (pid > 0) {
					int st;
					while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
						;
				}
				bu_display_regrab(disp);
				bu_event drop;
				while (bu_input_next(input, &drop, 0))
					;
				changed = true;
			}
		}
		update_clock(&s, &changed);
		if (s.brightness != last_brightness) {
			apply_brightness(s.brightness);
			last_brightness = s.brightness;
		}
		if (s.settings_open && s.settings_cat == 3 &&
		    (s.net_scan_req != last_scan_req || time(NULL) - last_net_poll > 10)) {
			last_scan_req = s.net_scan_req;
			last_net_poll = time(NULL);
			fill_netinfo(&s);
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
