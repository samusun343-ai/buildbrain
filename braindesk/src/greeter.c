/* braingreeter: the login screen process. Draws the login scene
 * with libbrainui, reads the password from evdev (the Brain
 * keyboard matrix), verifies it against /etc/shadow with crypt(),
 * and on success records the session user in
 * /run/braindesk/session-user and exits 0. Exit codes:
 *   0 logged in, 10 poweroff requested, 11 reboot requested.
 * The session wrapper (systemd service) acts on the exit code.
 *
 * Test hooks: BUI_SHADOW overrides the shadow path;
 * `braingreeter --auth-only USER PASS` checks credentials only;
 * `braingreeter --shot out.ppm` renders one frame and exits.
 */
#define _GNU_SOURCE
#include "bui.h"
#include "bui_fb.h"
#include "bui_input.h"
#include "scenes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <crypt.h>
#include <time.h>

static bu_theme *T;

static int check_password(const char *shadow_path, const char *user, const char *pass)
{
	FILE *f = fopen(shadow_path, "r");
	if (!f) return 0;
	char line[512];
	int ok = 0;
	size_t ulen = strlen(user);
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, user, ulen) != 0 || line[ulen] != ':')
			continue;
		char *hash = line + ulen + 1;
		char *end = strchr(hash, ':');
		if (end) *end = '\0';
		if (hash[0] == '!' || hash[0] == '*' || hash[0] == '\0')
			break;
		char *res = crypt(pass, hash);
		if (res && strcmp(res, hash) == 0)
			ok = 1;
		break;
	}
	fclose(f);
	return ok;
}

/* Linux keycode -> character for the password field (unshifted,
 * shifted). Letters/digits follow the standard evdev layout the
 * Brain matrix driver emits. */
static char key_to_char(int code, bool shift)
{
	static const struct { int code; char lo, hi; } tab[] = {
		{ 2, '1', '!' }, { 3, '2', '"' }, { 4, '3', '#' }, { 5, '4', '$' },
		{ 6, '5', '%' }, { 7, '6', '&' }, { 8, '7', '\'' }, { 9, '8', '(' },
		{ 10, '9', ')' }, { 11, '0', '0' }, { 12, '-', '=' }, { 13, '^', '~' },
		{ 16, 'q', 'Q' }, { 17, 'w', 'W' }, { 18, 'e', 'E' }, { 19, 'r', 'R' },
		{ 20, 't', 'T' }, { 21, 'y', 'Y' }, { 22, 'u', 'U' }, { 23, 'i', 'I' },
		{ 24, 'o', 'O' }, { 25, 'p', 'P' }, { 26, '@', '`' }, { 27, '[', '{' },
		{ 30, 'a', 'A' }, { 31, 's', 'S' }, { 32, 'd', 'D' }, { 33, 'f', 'F' },
		{ 34, 'g', 'G' }, { 35, 'h', 'H' }, { 36, 'j', 'J' }, { 37, 'k', 'K' },
		{ 38, 'l', 'L' }, { 39, ';', '+' }, { 40, ':', '*' }, { 41, ']', '}' },
		{ 44, 'z', 'Z' }, { 45, 'x', 'X' }, { 46, 'c', 'C' }, { 47, 'v', 'V' },
		{ 48, 'b', 'B' }, { 49, 'n', 'N' }, { 50, 'm', 'M' }, { 51, ',', '<' },
		{ 52, '.', '>' }, { 53, '/', '?' }, { 57, ' ', ' ' }, { 43, '\\', '|' },
	};
	for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
		if (tab[i].code == code)
			return shift ? tab[i].hi : tab[i].lo;
	return 0;
}


/* First regular user (uid >= 1000) from /etc/passwd; the greeter
 * shows that account by default. */
static const char *pick_user(void)
{
	static char name[64] = "user";
	FILE *f = fopen("/etc/passwd", "r");
	if (!f) return name;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char *c1 = strchr(line, ':');
		if (!c1) continue;
		char *c2 = strchr(c1 + 1, ':');
		if (!c2) continue;
		int uid = atoi(c2 + 1);
		if (uid >= 1000 && uid < 60000) {
			size_t n = c1 - line;
			if (n >= sizeof(name)) n = sizeof(name) - 1;
			memcpy(name, line, n);
			name[n] = '\0';
			break;
		}
	}
	fclose(f);
	return name;
}

static void draw_login_state(bu_canvas *c, bd_state *s, const char *user,
			     const char *masked, bool error)
{
	bd_draw_login(c, T, s, user);
	/* redraw the field with the live mask / error state */
	bu_rect field = { 854 / 2 - 140, 306, 280, 42 };
	bu_text_field(c, T, field, "パスワード", masked[0] ? masked : NULL, true);
	if (error)
		bu_draw_text_centered(c, T->font, (bu_rect){ 0, 416, 854, 20 },
				      "パスワードが違います", 13, T->danger);
}

int main(int argc, char **argv)
{
	const char *font_path = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
	const char *shadow_path = getenv("BUI_SHADOW");
	if (!shadow_path) shadow_path = "/etc/shadow";
	const char *user = NULL;
	bool shot = false;
	const char *shot_path = "greeter.ppm";

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--font") == 0 && i + 1 < argc)
			font_path = argv[++i];
		else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc)
			user = argv[++i];
		else if (strcmp(argv[i], "--auth-only") == 0 && i + 2 < argc) {
			int ok = check_password(shadow_path, argv[i + 1], argv[i + 2]);
			printf("auth %s\n", ok ? "OK" : "NG");
			return ok ? 0 : 1;
		} else if (strcmp(argv[i], "--shot") == 0) {
			shot = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				shot_path = argv[++i];
		}
	}

	if (!user)
		user = pick_user();
	T = &bu_theme_dark;
	T->font = bu_font_load(font_path);
	if (!T->font) {
		fprintf(stderr, "braingreeter: font load failed: %s\n", font_path);
		return 1;
	}
	bu_display *disp = bu_display_open(854, 480);
	int W = bu_display_width(disp), H = bu_display_height(disp);
	bu_canvas *canvas = bu_canvas_new(W, H);
	bd_state s;
	bd_state_init(&s);
	{
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		snprintf(s.clock, sizeof(s.clock), "%02d:%02d", tm.tm_hour, tm.tm_min);
	}

	char pass[65] = "";
	char masked[256] = "";
	bool error = false;
	draw_login_state(canvas, &s, user, masked, error);
	bu_display_present(disp, canvas, (bu_rect){ 0, 0, W, H });
	if (shot) {
		if (!bu_display_is_real(disp))
			bu_display_dump_ppm(disp, shot_path);
		return 0;
	}

	bu_input *input = bu_input_open(W, H);
	bool shift = false;
	for (;;) {
		bu_event ev;
		if (!bu_input_next(input, &ev, 500)) {
			/* refresh the clock once in a while */
			char before[8];
			strcpy(before, s.clock);
			time_t now = time(NULL);
			struct tm tm;
			localtime_r(&now, &tm);
			snprintf(s.clock, sizeof(s.clock), "%02d:%02d", tm.tm_hour, tm.tm_min);
			if (strcmp(before, s.clock) != 0) {
				draw_login_state(canvas, &s, user, masked, error);
				bu_display_present(disp, canvas, (bu_rect){ 0, 0, W, H });
			}
			continue;
		}
		bool changed = false;
		if (ev.type == BU_EV_KEY) {
			if (ev.code == 42 || ev.code == 54) { /* shifts */
				shift = ev.value != 0;
				continue;
			}
			if (ev.value != 1 && ev.value != 2)
				continue;
			error = false;
			if (ev.code == 14) { /* backspace */
				size_t n = strlen(pass);
				if (n) { pass[n - 1] = '\0'; masked[(n - 1) * 3] = '\0'; }
				changed = true;
			} else if (ev.code == 28 || ev.code == 96) { /* enter */
				if (check_password(shadow_path, user, pass)) {
					mkdir("/run/braindesk", 0755);
					FILE *f = fopen("/run/braindesk/session-user", "w");
					if (f) { fprintf(f, "%s\n", user); fclose(f); }
					return 0;
				}
				error = true;
				pass[0] = masked[0] = '\0';
				changed = true;
			} else if (ev.code == 1) { /* esc clears */
				pass[0] = masked[0] = '\0';
				changed = true;
			} else {
				char ch = key_to_char(ev.code, shift);
				size_t n = strlen(pass);
				if (ch && n < 64) {
					pass[n] = ch; pass[n + 1] = '\0';
					strcat(masked, "●");	/* one dot per char */
					changed = true;
				}
			}
		} else if (ev.type == BU_EV_POINTER_DOWN) {
			/* power / reboot buttons (bottom right) */
			if (ev.x >= W - 100 && ev.y >= H - 100)
				return 10;
			if (ev.x >= W - 170 && ev.x < W - 100 && ev.y >= H - 100)
				return 11;
			/* login button */
			if (ev.x >= W / 2 - 140 && ev.x < W / 2 + 140 &&
			    ev.y >= 360 && ev.y < 400) {
				if (check_password(shadow_path, user, pass)) {
					mkdir("/run/braindesk", 0755);
					FILE *f = fopen("/run/braindesk/session-user", "w");
					if (f) { fprintf(f, "%s\n", user); fclose(f); }
					return 0;
				}
				error = true;
				pass[0] = masked[0] = '\0';
				changed = true;
			}
		}
		if (changed) {
			draw_login_state(canvas, &s, user, masked, error);
			bu_display_present(disp, canvas, (bu_rect){ 0, 0, W, H });
		}
	}
	return 0;
}
