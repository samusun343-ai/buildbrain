/* bd-calc: calculator with a real expression evaluator.
 * The display shows the expression as typed plus a history
 * line of the last calculation; = evaluates with correct
 * precedence and parentheses (recursive descent).
 */
#include "bdapp.h"
#include <math.h>

struct calc {
	char expr[96];
	char history[128];
	bool just_evaled;
	bool error;
};

/* --- evaluator --- */
static const char *p;
static bool eval_err;

static double parse_expr(void);

static double parse_number(void)
{
	double v = 0;
	bool any = false, frac = false;
	double scale = 0.1;
	while ((*p >= '0' && *p <= '9') || *p == '.') {
		if (*p == '.') {
			frac = true;
		} else if (!frac) {
			v = v * 10 + (*p - '0');
			any = true;
		} else {
			v += (*p - '0') * scale;
			scale *= 0.1;
			any = true;
		}
		p++;
	}
	if (!any)
		eval_err = true;
	return v;
}

static double parse_factor(void)
{
	if (*p == '-') {
		p++;
		return -parse_factor();
	}
	if (*p == '+') {
		p++;
		return parse_factor();
	}
	if (*p == '(') {
		p++;
		double v = parse_expr();
		if (*p == ')')
			p++;
		else
			eval_err = true;
		return v;
	}
	return parse_number();
}

static double parse_term(void)
{
	double v = parse_factor();
	while (*p == '*' || *p == '/') {
		char op = *p++;
		double r = parse_factor();
		if (op == '/') {
			if (r == 0)
				eval_err = true;
			else
				v /= r;
		} else {
			v *= r;
		}
	}
	return v;
}

static double parse_expr(void)
{
	double v = parse_term();
	while (*p == '+' || *p == '-') {
		char op = *p++;
		double r = parse_term();
		v = op == '+' ? v + r : v - r;
	}
	return v;
}

static bool evaluate(const char *expr, double *out)
{
	p = expr;
	eval_err = false;
	double v = parse_expr();
	if (*p != 0)
		eval_err = true;
	if (eval_err || isnan(v) || isinf(v))
		return false;
	*out = v;
	return true;
}

static void fmt_result(char *buf, size_t n, double v)
{
	if (v == (long long)v && fabs(v) < 1e15)
		snprintf(buf, n, "%lld", (long long)v);
	else
		snprintf(buf, n, "%.10g", v);
}

/* --- UI --- */
static bu_rect key_rect(int row, int col)
{
	return (bu_rect){ 24 + col * 166, 168 + row * 62, 158, 56 };
}

static void draw(bdapp *a, void *ctx)
{
	struct calc *k = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	bu_rect disp = { 24, 56, 816, 100 };
	bu_fill_round_rect(c, disp, 12, t->surface);
	bu_draw_text_right(c, t->font, (bu_rect){ disp.x + 8, disp.y + 8, disp.w - 24, 24 },
			   k->history, 13, t->text_faint);
	const char *shown = k->expr[0] ? k->expr : "0";
	bu_color ink = k->error ? t->danger : t->text;
	bu_draw_text_right(c, t->font, (bu_rect){ disp.x + 8, disp.y + 34, disp.w - 24, 52 }, shown, 34, ink);
	static const char *keys[5][5] = {
		{ "C", "(", ")", "←", "÷" },
		{ "7", "8", "9", "%", "×" },
		{ "4", "5", "6", "±", "−" },
		{ "1", "2", "3", "", "+" },
		{ "0", "00", ".", "", "=" },
	};
	for (int r = 0; r < 5; r++) {
		for (int col = 0; col < 5; col++) {
			const char *lab = keys[r][col];
			if (!lab[0])
				continue;
			bu_btn_style st = BU_BTN_NORMAL;
			if (strcmp(lab, "=") == 0)
				st = BU_BTN_PRIMARY;
			else if (strcmp(lab, "C") == 0)
				st = BU_BTN_DANGER;
			bu_button(c, t, key_rect(r, col), lab, st, false);
		}
	}
}

static void append(struct calc *k, const char *s)
{
	if (k->just_evaled) {
		if ((s[0] >= '0' && s[0] <= '9') || s[0] == '.' || s[0] == '(')
			k->expr[0] = 0;
		k->just_evaled = false;
	}
	k->error = false;
	if (strlen(k->expr) + strlen(s) < sizeof(k->expr) - 1)
		strcat(k->expr, s);
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct calc *k = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	static const char *keys[5][5] = {
		{ "C", "(", ")", "BS", "/" },
		{ "7", "8", "9", "%", "*" },
		{ "4", "5", "6", "+/-", "-" },
		{ "1", "2", "3", "", "+" },
		{ "0", "00", ".", "", "=" },
	};
	for (int r = 0; r < 5; r++) {
		for (int col = 0; col < 5; col++) {
			bu_rect kr = key_rect(r, col);
			if (!(ev->x >= kr.x && ev->x < kr.x + kr.w && ev->y >= kr.y && ev->y < kr.y + kr.h))
				continue;
			const char *act = keys[r][col];
			if (!act[0])
				return;
			*changed = true;
			if (strcmp(act, "C") == 0) {
				k->expr[0] = 0;
				k->error = false;
				k->just_evaled = false;
			} else if (strcmp(act, "BS") == 0) {
				int l = (int)strlen(k->expr);
				if (l > 0)
					k->expr[l - 1] = 0;
				k->just_evaled = false;
			} else if (strcmp(act, "=") == 0) {
				double v;
				if (k->expr[0] && evaluate(k->expr, &v)) {
					snprintf(k->history, sizeof(k->history), "%s =", k->expr);
					fmt_result(k->expr, sizeof(k->expr), v);
					k->just_evaled = true;
					k->error = false;
				} else {
					k->error = true;
					snprintf(k->history, sizeof(k->history), "式が正しくありません");
				}
			} else if (strcmp(act, "%") == 0) {
				double v;
				if (k->expr[0] && evaluate(k->expr, &v)) {
					snprintf(k->history, sizeof(k->history), "%s %%", k->expr);
					fmt_result(k->expr, sizeof(k->expr), v / 100.0);
					k->just_evaled = true;
				} else {
					k->error = true;
				}
			} else if (strcmp(act, "+/-") == 0) {
				char tmp[128];
				if (k->expr[0] == '-')
					snprintf(tmp, sizeof(tmp), "%s", k->expr + 1);
				else
					snprintf(tmp, sizeof(tmp), "-%s", k->expr);
				snprintf(k->expr, sizeof(k->expr), "%s", tmp);
				k->just_evaled = false;
			} else {
				append(k, act);
			}
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct calc k;
	memset(&k, 0, sizeof(k));
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-calc.ppm" : NULL;
	return bdapp_run("電卓", shot, draw, onevent, NULL, &k);
}
