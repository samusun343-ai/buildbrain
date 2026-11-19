/* bd-deck: control deck for a host PC.
 *
 * In deck mode (brain-mode set deck) the Brain enumerates as a
 * composite USB gadget: HID keyboard + HID consumer control + ACM
 * serial + NCM network. This app drives the two HID functions:
 *
 *   /dev/hidg0  boot keyboard   (8-byte reports)
 *   /dev/hidg1  consumer control (1-byte bitmask reports)
 *
 * Big touch pads fire media keys and common host shortcuts, and the
 * physical keyboard can be forwarded to the host as-is — the Brain
 * becomes a keyboard with a screen. See docs/gadget-modes.md.
 */
#include "bdapp.h"
#include <fcntl.h>
#include <linux/input.h>

struct deck {
	int kbd_fd, cc_fd;
	bool connected;
	bool forward;		/* forward physical keys to the host */
	uint8_t mods;		/* currently held HID modifiers */
	uint8_t keys[6];	/* currently held HID usages */
	char status[96];
	int tick_count;
};

/* consumer control bits (see brain-gadget's hid.usb1 descriptor) */
#define CC_PLAYPAUSE 0x01
#define CC_NEXT      0x02
#define CC_PREV      0x04
#define CC_STOP      0x08
#define CC_MUTE      0x10
#define CC_VOLUP     0x20
#define CC_VOLDOWN   0x40

static void hid_open(struct deck *d)
{
	if (d->kbd_fd < 0)
		d->kbd_fd = open("/dev/hidg0", O_WRONLY | O_NONBLOCK);
	if (d->cc_fd < 0)
		d->cc_fd = open("/dev/hidg1", O_WRONLY | O_NONBLOCK);
	d->connected = d->kbd_fd >= 0;
}

static void send_kbd(struct deck *d)
{
	if (d->kbd_fd < 0) return;
	uint8_t rep[8] = { d->mods, 0, d->keys[0], d->keys[1], d->keys[2],
			   d->keys[3], d->keys[4], d->keys[5] };
	if (write(d->kbd_fd, rep, 8) < 0) { /* host not listening yet */ }
}

static void tap_combo(struct deck *d, uint8_t mods, uint8_t usage)
{
	if (!d->connected) return;
	uint8_t save_mods = d->mods;
	uint8_t save_keys[6];
	memcpy(save_keys, d->keys, 6);
	d->mods = mods;
	memset(d->keys, 0, 6);
	d->keys[0] = usage;
	send_kbd(d);
	usleep(30000);
	d->mods = save_mods;
	memcpy(d->keys, save_keys, 6);
	send_kbd(d);
}

static void tap_cc(struct deck *d, uint8_t bits)
{
	if (d->cc_fd < 0) return;
	uint8_t on = bits, off = 0;
	if (write(d->cc_fd, &on, 1) < 0) return;
	usleep(30000);
	if (write(d->cc_fd, &off, 1) < 0) { }
}

/* evdev keycode -> HID usage (0 = none) and modifier bit */
static uint8_t ev_to_usage(int code)
{
	switch (code) {
	case KEY_1: return 30; case KEY_2: return 31; case KEY_3: return 32;
	case KEY_4: return 33; case KEY_5: return 34; case KEY_6: return 35;
	case KEY_7: return 36; case KEY_8: return 37; case KEY_9: return 38;
	case KEY_0: return 39;
	case KEY_Q: return 20; case KEY_W: return 26; case KEY_E: return 8;
	case KEY_R: return 21; case KEY_T: return 23; case KEY_Y: return 28;
	case KEY_U: return 24; case KEY_I: return 12; case KEY_O: return 18;
	case KEY_P: return 19;
	case KEY_A: return 4;  case KEY_S: return 22; case KEY_D: return 7;
	case KEY_F: return 9;  case KEY_G: return 10; case KEY_H: return 11;
	case KEY_J: return 13; case KEY_K: return 14; case KEY_L: return 15;
	case KEY_Z: return 29; case KEY_X: return 27; case KEY_C: return 6;
	case KEY_V: return 25; case KEY_B: return 5;  case KEY_N: return 17;
	case KEY_M: return 16;
	case KEY_ENTER: return 40; case KEY_ESC: return 41;
	case KEY_BACKSPACE: return 42; case KEY_TAB: return 43;
	case KEY_SPACE: return 44; case KEY_MINUS: return 45;
	case KEY_EQUAL: return 46; case KEY_LEFTBRACE: return 47;
	case KEY_RIGHTBRACE: return 48; case KEY_BACKSLASH: return 49;
	case KEY_SEMICOLON: return 51; case KEY_APOSTROPHE: return 52;
	case KEY_GRAVE: return 53; case KEY_COMMA: return 54;
	case KEY_DOT: return 55; case KEY_SLASH: return 56;
	case KEY_F1: return 58; case KEY_F2: return 59; case KEY_F3: return 60;
	case KEY_F4: return 61; case KEY_F5: return 62; case KEY_F6: return 63;
	case KEY_F7: return 64; case KEY_F8: return 65; case KEY_F9: return 66;
	case KEY_F10: return 67; case KEY_F11: return 68; case KEY_F12: return 69;
	case KEY_UP: return 82; case KEY_DOWN: return 81;
	case KEY_LEFT: return 80; case KEY_RIGHT: return 79;
	case KEY_HOME: return 74; case KEY_END: return 77;
	case KEY_PAGEUP: return 75; case KEY_PAGEDOWN: return 78;
	case KEY_DELETE: return 76;
	default: return 0;
	}
}

static uint8_t ev_to_modbit(int code)
{
	switch (code) {
	case KEY_LEFTCTRL: return 0x01; case KEY_LEFTSHIFT: return 0x02;
	case KEY_LEFTALT: return 0x04;  case KEY_LEFTMETA: return 0x08;
	case KEY_RIGHTCTRL: return 0x10; case KEY_RIGHTSHIFT: return 0x20;
	case KEY_RIGHTALT: return 0x40; case KEY_RIGHTMETA: return 0x80;
	default: return 0;
	}
}

static void forward_key(struct deck *d, int code, int value)
{
	uint8_t bit = ev_to_modbit(code);
	if (bit) {
		if (value) d->mods |= bit; else d->mods &= (uint8_t)~bit;
		send_kbd(d);
		return;
	}
	uint8_t u = ev_to_usage(code);
	if (!u) return;
	if (value == 1) {
		for (int i = 0; i < 6; i++)
			if (d->keys[i] == 0) { d->keys[i] = u; break; }
		send_kbd(d);
	} else if (value == 0) {
		for (int i = 0; i < 6; i++)
			if (d->keys[i] == u) d->keys[i] = 0;
		send_kbd(d);
	}
}

struct pad { const char *label; int kind; uint8_t a, b; };
/* kind 0 = consumer bit (a), kind 1 = key combo (a=mods, b=usage) */
static const struct pad PADS[] = {
	{ "再生 / 一時停止", 0, CC_PLAYPAUSE, 0 },
	{ "次へ",           0, CC_NEXT, 0 },
	{ "前へ",           0, CC_PREV, 0 },
	{ "停止",           0, CC_STOP, 0 },
	{ "音量 +",         0, CC_VOLUP, 0 },
	{ "音量 −",         0, CC_VOLDOWN, 0 },
	{ "ミュート",       0, CC_MUTE, 0 },
	{ "スクショ",       1, 0x0A, 22 },	/* GUI+Shift+S */
	{ "タスク切替",     1, 0x04, 43 },	/* Alt+Tab */
	{ "デスクトップ",   1, 0x08, 7 },	/* GUI+D */
	{ "ロック",         1, 0x08, 15 },	/* GUI+L */
	{ "全画面",         1, 0x00, 68 },	/* F11 */
};
#define NPADS (sizeof(PADS) / sizeof(PADS[0]))

static bu_rect pad_rect(int i)
{
	int col = i % 4, row = i / 4;
	return (bu_rect){ 16 + col * 208, 108 + row * 96, 192, 84 };
}

static bu_rect fwd_rect(void)  { return (bu_rect){ 16, 404, 460, 56 }; }
static bu_rect mode_rect(void) { return (bu_rect){ 492, 404, 356, 56 }; }

static bool in_rect(bu_rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void draw(bdapp *a, void *ctx)
{
	struct deck *d = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);

	bu_card(c, t, (bu_rect){ 16, 56, a->w - 32, 40 });
	char line[160];
	if (d->connected)
		snprintf(line, sizeof(line), "接続中: USB HID デッキとして動作しています");
	else
		snprintf(line, sizeof(line), "未接続: デッキモードになっていません (下のボタンで切替)");
	bu_draw_text(c, t->font, 28, 66, line, 15, t->text);
	if (d->status[0])
		bu_draw_text_right(c, t->font, (bu_rect){ 0, 66, a->w - 28, 20 },
				   d->status, 14, t->text_dim);

	for (unsigned i = 0; i < NPADS; i++) {
		bu_rect r = pad_rect(i);
		bu_fill_round_rect(c, r, 12, d->connected ? t->surface : t->surface);
		bu_draw_text_centered(c, t->font, r, PADS[i].label, 17, t->text);
	}

	bu_rect fr = fwd_rect();
	bu_fill_round_rect(c, fr, 12, t->surface);
	bu_draw_text(c, t->font, fr.x + 14, fr.y + 18, "物理キーをそのまま送る", 16, t->text);
	bu_toggle(c, t, fr.x + fr.w - 62, fr.y + 13, d->forward);

	bu_rect mr = mode_rect();
	bu_button(c, t, mr, d->connected ? "モード設定を開く" : "デッキモードにする",
		  BU_BTN_NORMAL, false);
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct deck *d = ctx;
	(void)a; (void)quit;
	if (ev->type == BU_EV_KEY) {
		if (d->forward && d->connected && ev->value != 2) {
			forward_key(d, ev->code, ev->value);
			snprintf(d->status, sizeof(d->status), "キーを送信しました");
			*changed = true;
		}
		return;
	}
	if (ev->type != BU_EV_POINTER_DOWN) return;
	for (unsigned i = 0; i < NPADS; i++) {
		if (in_rect(pad_rect(i), ev->x, ev->y)) {
			if (!d->connected) {
				snprintf(d->status, sizeof(d->status), "まずデッキモードにしてください");
			} else if (PADS[i].kind == 0) {
				tap_cc(d, PADS[i].a);
				snprintf(d->status, sizeof(d->status), "%s を送りました", PADS[i].label);
			} else {
				tap_combo(d, PADS[i].a, PADS[i].b);
				snprintf(d->status, sizeof(d->status), "%s を送りました", PADS[i].label);
			}
			*changed = true;
			return;
		}
	}
	if (in_rect(fwd_rect(), ev->x, ev->y)) {
		d->forward = !d->forward;
		if (!d->forward) { /* release everything held */
			d->mods = 0;
			memset(d->keys, 0, 6);
			send_kbd(d);
		}
		*changed = true;
		return;
	}
	if (in_rect(mode_rect(), ev->x, ev->y)) {
		if (!d->connected) {
			int rc = system("brain-mode set deck >/dev/null 2>&1");
			if (rc == 0) {
				hid_open(d);
				snprintf(d->status, sizeof(d->status), "デッキモードにしました");
			} else {
				snprintf(d->status, sizeof(d->status), "切替に失敗しました (要 root)");
			}
		} else {
			snprintf(d->status, sizeof(d->status), "設定アプリの「モード」で変更できます");
		}
		*changed = true;
	}
}

static void tick(bdapp *a, void *ctx, bool *changed)
{
	struct deck *d = ctx;
	(void)a;
	/* mode may be switched from Settings while we are open */
	if (!d->connected && ++d->tick_count % 8 == 0) {
		hid_open(d);
		if (d->connected) *changed = true;
	}
}

int main(int argc, char **argv)
{
	struct deck d;
	memset(&d, 0, sizeof(d));
	d.kbd_fd = d.cc_fd = -1;
	d.forward = true;
	const char *shot = NULL;
	if (argc > 1 && strcmp(argv[1], "--shot") == 0) shot = "bd-deck.ppm";
	if (getenv("BD_SHOT_DECK")) d.connected = true; /* host screenshot of the live state */
	else hid_open(&d);
	return bdapp_run("コントロールデッキ", shot, draw, onevent, tick, &d);
}
