/* bd-calc: calculator for braindesk. Touch-first keypad,
 * double-precision arithmetic with operator precedence via a
 * simple two-register model plus % and ± keys.
 */
#include "bdapp.h"
#include <math.h>

struct calc {
	char expr[64];		/* expression being built */
	char disp[64];		/* what the display shows */
	double acc;
	char pending;		/* pending operator */
	bool fresh;		/* next digit starts a new number */
	bool error;
};

static void calc_apply(struct calc *k)
{
	char *end;
	double v = strtod(k->disp, &end);
	if (end == k->disp)
		return;
	switch (k->pending) {
	case 0: k->acc = v; break;
	case '+': k->acc += v; break;
	case '-': k->acc -= v; break;
	case '*': k->acc *= v; break;
	case '/':
		if (v == 0) { k->error = true; return; }
		k->acc /= v;
		break;
	}
	k->pending = 0;
}

static void fmt(struct calc *k)
{
	if (k->error) {
		snprintf(k->disp, sizeof(k->disp), "エラー");
		return;
	}
	double v = k->acc;
	if (v == (long long)v && fabs(v) < 1e15)
		snprintf(k->disp, sizeof(k->disp), "%lld", (long long)v);
	else
		snprintf(k->disp, sizeof(k->disp), "%.10g", v);
}

static const char *KEYS[5][4] = {
	{ "C", "±", "%", "÷" },
	{ "7", "8", "9", "×" },
	{ "4", "5", "6", "-" },
	{ "1", "2", "3", "+" },
	{ "0", ".", "⌫", "=" },
};

static bu_rect key_rect(int row, int col)
{
	return (bu_rect){ 60 + col * 186, 148 + row * 58, 174, 48 };
}

static void draw(bdapp *a, void *ctx)
{
	struct calc *k = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	bu_fill_round_rect(c, (bu_rect){ 40, 58, 784, 76 }, 12, t->surface);
	bu_draw_text_right(c, t->font, (bu_rect){ 40, 74, 744, 44 }, k->disp, 34, t->text);
	for (int r = 0; r < 5; r++)
		for (int col = 0; col < 4; col++) {
			const char *label = KEYS[r][col];
			bu_btn_style st = BU_BTN_NORMAL;
			if (strcmp(label, "=") == 0)
				st = BU_BTN_PRIMARY;
			if (strcmp(label, "C") == 0)
				st = BU_BTN_DANGER;
			bu_button(c, t, key_rect(r, col), label, st, false);
		}
}

static void press(struct calc *k, const char *label)
{
	if (k->error && strcmp(label, "C") != 0)
		return;
	if (strcmp(label, "C") == 0) {
		k->acc = 0; k->pending = 0; k->fresh = true; k->error = false;
		snprintf(k->disp, sizeof(k->disp), "0");
		return;
	}
	if (strcmp(label, "⌫") == 0) {
		size_t n = strlen(k->disp);
		if (n > 0 && !k->fresh)
			k->disp[n - 1] = 0;
		if (k->disp[0] == 0)
			snprintf(k->disp, sizeof(k->disp), "0");
		return;
	}
	if (strcmp(label, "±") == 0) {
		if (k->disp[0] == '-')
			memmove(k->disp, k->disp + 1, strlen(k->disp));
		else if (strcmp(k->disp, "0") != 0) {
			char tmp[64];
			snprintf(tmp, sizeof(tmp), "-%s", k->disp);
			snprintf(k->disp, sizeof(k->disp), "%s", tmp);
		}
		return;
	}
	if (strcmp(label, "%") == 0) {
		double v = strtod(k->disp, NULL) / 100.0;
		snprintf(k->disp, sizeof(k->disp), "%.10g", v);
		k->fresh = true;
		return;
	}
	if (strcmp(label, "=") == 0) {
		calc_apply(k);
		fmt(k);
		k->pending = 0;
		k->fresh = true;
		return;
	}
	if (strcmp(label, "+") == 0 || strcmp(label, "-") == 0 ||
	    strcmp(label, "×") == 0 || strcmp(label, "÷") == 0) {
		char op = label[0] == '+' ? '+' : label[0] == '-' ? '-' :
			  strcmp(label, "×") == 0 ? '*' : '/';
		if (!k->fresh || k->pending == 0)
			calc_apply(k);
		if (k->error) { fmt(k); return; }
		k->pending = op;
		fmt(k);
		k->fresh = true;
		return;
	}
	if ((label[0] >= '0' && label[0] <= '9') || label[0] == '.') {
		if (k->fresh || strcmp(k->disp, "0") == 0) {
			snprintf(k->disp, sizeof(k->disp), "%s", label[0] == '.' ? "0." : label);
			k->fresh = false;
		} else if (strlen(k->disp) < 20) {
			if (label[0] == '.' && strchr(k->disp, '.'))
				return;
			strcat(k->disp, label);
		}
		return;
	}
	/* parentheses are accepted as display-only grouping hints today */
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct calc *k = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	for (int r = 0; r < 5; r++)
		for (int col = 0; col < 4; col++) {
			bu_rect kr = key_rect(r, col);
			if (ev->x >= kr.x && ev->x < kr.x + kr.w && ev->y >= kr.y && ev->y < kr.y + kr.h) {
				press(k, KEYS[r][col]);
				*changed = true;
				return;
			}
		}
	if (ev->type == BU_EV_KEY && ev->value == 1) {
		char buf[2] = { 0, 0 };
		const char *label = NULL;
		if (ev->code >= 2 && ev->code <= 11) { buf[0] = "1234567890"[ev->code - 2]; label = buf; }
		else if (ev->code == 28) label = "=";
		else if (ev->code == 14) label = "⌫";
		else if (ev->code == 51) label = ".";
		if (label) { press(k, label); *changed = true; }
	}
}

int main(int argc, char **argv)
{
	struct calc k;
	memset(&k, 0, sizeof(k));
	snprintf(k.disp, sizeof(k.disp), "0");
	k.fresh = true;
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-calc.ppm" : NULL;
	return bdapp_run("電卓", shot, draw, onevent, NULL, &k);
}
