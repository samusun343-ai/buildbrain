/* bd-calc: calculator with a real expression evaluator.
 * Three modes (tabs): 基本 basic, 関数 scientific, 換算 unit
 * conversion. The evaluator is a recursive-descent parser with
 * precedence, parentheses, functions (sin/cos/tan and inverses,
 * ln/log, sqrt/cbrt, exp), powers, factorials and the constants
 * pi/e; trig honours the Deg/Rad toggle. Memory keys (MC/M+/
 * M-/MR), a history tape (tap an entry to reuse its result),
 * % and sign toggle follow the reference calculator's basics
 * (see the reference-apps research, Calculator section).
 * Physical keyboards work too: digits, operators, Enter (=),
 * Backspace, Esc (AC), numpad, Shift for % ^ ( ).
 */
#include "bdapp.h"
#include <math.h>
#include <ctype.h>

enum { MODE_BASIC, MODE_SCI, MODE_CONV };

struct histent {
	char expr[96];
	char result[32];
};

struct calc {
	int mode;
	char expr[96];
	char history[128];
	bool just_evaled;
	bool error;
	double last;
	double mem;
	bool has_mem;
	bool inv;		/* 2nd: inverse functions */
	bool deg;		/* true = degrees, false = radians */
	struct histent hist[50];
	int nhist;
	bool hist_open;
	int hist_scroll;
	/* conversion mode */
	int conv_cat, conv_from, conv_to;
	char conv_in[32];
	bool shift;		/* physical keyboard shift state */
};

/* --- evaluator --- */
static const char *p;
static bool eval_err;
static int eval_deg = 1;

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

static double fact(double v)
{
	if (v < 0 || v > 170) {
		eval_err = true;
		return 0;
	}
	return tgamma(v + 1.0);
}

static double apply_func(const char *name, double v)
{
	double r = eval_deg ? v * M_PI / 180.0 : v;
	double a = eval_deg ? 180.0 / M_PI : 1.0;
	if (strcmp(name, "sin") == 0) return sin(r);
	if (strcmp(name, "cos") == 0) return cos(r);
	if (strcmp(name, "tan") == 0) return tan(r);
	if (strcmp(name, "asin") == 0) return asin(v) * a;
	if (strcmp(name, "acos") == 0) return acos(v) * a;
	if (strcmp(name, "atan") == 0) return atan(v) * a;
	if (strcmp(name, "ln") == 0) return log(v);
	if (strcmp(name, "log") == 0) return log10(v);
	if (strcmp(name, "sqrt") == 0) return sqrt(v);
	if (strcmp(name, "cbrt") == 0) return cbrt(v);
	if (strcmp(name, "exp") == 0) return exp(v);
	eval_err = true;
	return 0;
}

static double parse_primary(void)
{
	if (*p == '(') {
		p++;
		double v = parse_expr();
		if (*p == ')')
			p++;
		else
			eval_err = true;
		return v;
	}
	if (isalpha((unsigned char)*p)) {
		char name[8];
		int n = 0;
		while (isalpha((unsigned char)*p) && n < 7)
			name[n++] = *p++;
		name[n] = 0;
		if (strcmp(name, "pi") == 0)
			return M_PI;
		if (strcmp(name, "e") == 0)
			return M_E;
		if (*p != '(') {
			eval_err = true;
			return 0;
		}
		p++;
		double v = parse_expr();
		if (*p == ')')
			p++;
		else
			eval_err = true;
		return apply_func(name, v);
	}
	return parse_number();
}

static double parse_postfix(void)
{
	double v = parse_primary();
	while (*p == '!') {
		p++;
		v = fact(v);
	}
	return v;
}

static double parse_unary(void)
{
	if (*p == '-') {
		p++;
		return -parse_unary();
	}
	if (*p == '+') {
		p++;
		return parse_unary();
	}
	return parse_postfix();
}

static double parse_power(void)
{
	double v = parse_unary();
	if (*p == '^') {
		p++;
		double e = parse_power();	/* right-associative */
		v = pow(v, e);
	}
	return v;
}

static double parse_term(void)
{
	double v = parse_power();
	while (*p == '*' || *p == '/') {
		char op = *p++;
		double r = parse_power();
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

static bool evaluate(struct calc *k, const char *expr, double *out)
{
	p = expr;
	eval_err = false;
	eval_deg = k->deg ? 1 : 0;
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

/* --- unit conversion data --- */
struct unit {
	const char *name;
	double factor;		/* to the category base unit (unused for temperature) */
};
struct convcat {
	const char *name;
	const struct unit *units;
	int nunits;
	bool temp;
};
static const struct unit U_LEN[] = {
	{ "mm", 0.001 }, { "cm", 0.01 }, { "m", 1 }, { "km", 1000 },
	{ "inch", 0.0254 }, { "ft", 0.3048 }, { "yard", 0.9144 }, { "mile", 1609.344 },
};
static const struct unit U_WGT[] = {
	{ "g", 0.001 }, { "kg", 1 }, { "t", 1000 },
	{ "oz", 0.028349523125 }, { "lb", 0.45359237 },
};
static const struct unit U_TMP[] = {
	{ "°C", 0 }, { "°F", 0 }, { "K", 0 },
};
static const struct unit U_SPD[] = {
	{ "m/s", 1 }, { "km/h", 1.0 / 3.6 }, { "mph", 0.44704 }, { "knot", 0.514444 },
};
static const struct unit U_AREA[] = {
	{ "m²", 1 }, { "cm²", 0.0001 }, { "ha", 10000 }, { "km²", 1000000 },
	{ "坪", 3.305785 }, { "acre", 4046.8564224 },
};
static const struct unit U_VOL[] = {
	{ "mL", 0.001 }, { "L", 1 }, { "gal", 3.785411784 }, { "合", 0.180390684652 },
};
static const struct convcat CONV[] = {
	{ "長さ", U_LEN, 8, false },
	{ "重さ", U_WGT, 5, false },
	{ "温度", U_TMP, 3, true },
	{ "速さ", U_SPD, 4, false },
	{ "面積", U_AREA, 6, false },
	{ "容量", U_VOL, 4, false },
};
#define NCONV 6

static double conv_compute(struct calc *k, double v)
{
	const struct convcat *cc = &CONV[k->conv_cat];
	if (cc->temp) {
		double c;	/* via Celsius */
		if (k->conv_from == 0) c = v;
		else if (k->conv_from == 1) c = (v - 32.0) * 5.0 / 9.0;
		else c = v - 273.15;
		if (k->conv_to == 0) return c;
		if (k->conv_to == 1) return c * 9.0 / 5.0 + 32.0;
		return c + 273.15;
	}
	return v * cc->units[k->conv_from].factor / cc->units[k->conv_to].factor;
}

/* --- key action handling (shared by pointer and keyboard) --- */
static void append(struct calc *k, const char *s)
{
	if (k->just_evaled) {
		char c0 = s[0];
		if ((c0 >= '0' && c0 <= '9') || c0 == '.' || c0 == '(' || isalpha((unsigned char)c0))
			k->expr[0] = 0;
		k->just_evaled = false;
	}
	k->error = false;
	if (strlen(k->expr) + strlen(s) < sizeof(k->expr) - 1)
		strcat(k->expr, s);
}

static void hist_push(struct calc *k, const char *expr, const char *result)
{
	if (k->nhist >= 50) {
		memmove(&k->hist[0], &k->hist[1], sizeof(struct histent) * 49);
		k->nhist = 49;
	}
	snprintf(k->hist[k->nhist].expr, sizeof(k->hist[0].expr), "%s", expr);
	snprintf(k->hist[k->nhist].result, sizeof(k->hist[0].result), "%s", result);
	k->nhist++;
}

static void do_equals(struct calc *k)
{
	double v;
	if (k->expr[0] && evaluate(k, k->expr, &v)) {
		char res[32];
		fmt_result(res, sizeof(res), v);
		hist_push(k, k->expr, res);
		snprintf(k->history, sizeof(k->history), "%s =", k->expr);
		snprintf(k->expr, sizeof(k->expr), "%s", res);
		k->last = v;
		k->just_evaled = true;
		k->error = false;
	} else {
		k->error = true;
		snprintf(k->history, sizeof(k->history), "式が正しくありません");
	}
}

static void do_backspace(struct calc *k)
{
	static const char *toks[] = {
		"asin(", "acos(", "atan(", "sqrt(", "cbrt(",
		"sin(", "cos(", "tan(", "log(", "ln(", "exp(", "pi",
	};
	int l = (int)strlen(k->expr);
	if (l == 0)
		return;
	for (size_t i = 0; i < sizeof(toks) / sizeof(toks[0]); i++) {
		int tl = (int)strlen(toks[i]);
		if (l >= tl && strcmp(k->expr + l - tl, toks[i]) == 0) {
			k->expr[l - tl] = 0;
			k->just_evaled = false;
			return;
		}
	}
	k->expr[l - 1] = 0;
	k->just_evaled = false;
}

static void do_negate(struct calc *k)
{
	/* negate the trailing number entry, or wrap the whole
	 * expression when it does not end in a plain number */
	char *e = k->expr;
	int l = (int)strlen(e);
	if (l == 0) {
		snprintf(e, sizeof(k->expr), "-");
		return;
	}
	int i = l;
	while (i > 0 && ((e[i - 1] >= '0' && e[i - 1] <= '9') || e[i - 1] == '.'))
		i--;
	if (i < l) {
		/* trailing number at [i, l) */
		if (i >= 1 && e[i - 1] == '-' &&
		    (i - 1 == 0 || strchr("+-*/(^", e[i - 2]))) {
			memmove(e + i - 1, e + i, strlen(e + i) + 1);
		} else {
			char tmp[128];
			snprintf(tmp, sizeof(tmp), "%.*s-%s", i, e, e + i);
			snprintf(e, sizeof(k->expr), "%s", tmp);
		}
	} else if (l > 2 && e[0] == '-' && e[1] == '(' && e[l - 1] == ')') {
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "%.*s", l - 3, e + 2);
		snprintf(e, sizeof(k->expr), "%s", tmp);
	} else {
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "-(%s)", e);
		snprintf(e, sizeof(k->expr), "%s", tmp);
	}
	k->just_evaled = false;
}

static bool current_value(struct calc *k, double *out)
{
	if (k->expr[0])
		return evaluate(k, k->expr, out);
	*out = k->last;
	return true;
}

static void press(struct calc *k, const char *act)
{
	if (strcmp(act, "AC") == 0) {
		k->expr[0] = 0;
		k->error = false;
		k->just_evaled = false;
	} else if (strcmp(act, "BS") == 0) {
		do_backspace(k);
	} else if (strcmp(act, "=") == 0) {
		do_equals(k);
	} else if (strcmp(act, "%") == 0) {
		double v;
		if (k->expr[0] && evaluate(k, k->expr, &v)) {
			snprintf(k->history, sizeof(k->history), "%s %%", k->expr);
			fmt_result(k->expr, sizeof(k->expr), v / 100.0);
			k->last = v / 100.0;
			k->just_evaled = true;
		} else {
			k->error = true;
		}
	} else if (strcmp(act, "NEG") == 0) {
		do_negate(k);
	} else if (strcmp(act, "MC") == 0) {
		k->mem = 0;
		k->has_mem = false;
		snprintf(k->history, sizeof(k->history), "メモリを消去しました");
	} else if (strcmp(act, "MR") == 0) {
		if (k->has_mem) {
			char buf[32];
			fmt_result(buf, sizeof(buf), k->mem);
			if (!k->just_evaled && k->expr[0]) {
				int l = (int)strlen(k->expr);
				char lc = k->expr[l - 1];
				if ((lc >= '0' && lc <= '9') || lc == '.' || lc == ')' || lc == '!')
					append(k, "*");
			}
			append(k, buf);
		}
	} else if (strcmp(act, "M+") == 0 || strcmp(act, "M-") == 0) {
		double v;
		if (current_value(k, &v)) {
			k->mem += (act[1] == '+') ? v : -v;
			k->has_mem = true;
			char buf[32];
			fmt_result(buf, sizeof(buf), k->mem);
			snprintf(k->history, sizeof(k->history), "メモリ: %s", buf);
		} else {
			k->error = true;
		}
	} else if (strcmp(act, "2ND") == 0) {
		k->inv = !k->inv;
	} else if (strcmp(act, "DEGRAD") == 0) {
		k->deg = !k->deg;
	} else {
		append(k, act);
	}
}

/* --- layout --- */
static bu_rect key_rect(int row, int col)	/* basic grid */
{
	return (bu_rect){ 24 + col * 164, 208 + row * 54, 156, 48 };
}
static bu_rect sci_rect(int row, int col)	/* scientific grid: 8 columns */
{
	return (bu_rect){ 24 + col * 102, 208 + row * 54, 96, 48 };
}
static bu_rect mem_rect(int i)
{
	return (bu_rect){ 24 + i * 118, 166, 110, 34 };
}
static bu_rect tab_rect(int i)
{
	return (bu_rect){ 24 + i * 116, 44, 110, 30 };
}

static const char *BASIC_KEYS[5][5] = {
	{ "AC", "(", ")", "BS", "/" },
	{ "7", "8", "9", "%", "*" },
	{ "4", "5", "6", "NEG", "-" },
	{ "1", "2", "3", "00", "+" },
	{ "0", ".", "=", NULL, NULL },	/* 0 and = span two columns */
};
static const char *BASIC_LAB[5][5] = {
	{ "AC", "(", ")", "←", "÷" },
	{ "7", "8", "9", "%", "×" },
	{ "4", "5", "6", "±", "−" },
	{ "1", "2", "3", "00", "+" },
	{ "0", ".", "=", NULL, NULL },
};
/* scientific extra columns 0..2 (labels depend on the 2nd toggle) */
static const char *SCI_KEYS[5][3] = {
	{ "2ND", "!", "e" },
	{ "sin(", "cos(", "tan(" },
	{ "ln(", "log(", "pi" },
	{ "^2", "^3", "^" },
	{ "sqrt(", "cbrt(", "1/(" },
};
static const char *SCI_KEYS_INV[5][3] = {
	{ "2ND", "!", "e" },
	{ "asin(", "acos(", "atan(" },
	{ "e^(", "10^(", "pi" },
	{ "^2", "^3", "^" },
	{ "sqrt(", "cbrt(", "1/(" },
};
static const char *SCI_LAB[5][3] = {
	{ "2nd", "x!", "e" },
	{ "sin", "cos", "tan" },
	{ "ln", "log", "π" },
	{ "x²", "x³", "x^y" },
	{ "√", "³√", "1/x" },
};
static const char *SCI_LAB_INV[5][3] = {
	{ "2nd", "x!", "e" },
	{ "asin", "acos", "atan" },
	{ "e^x", "10^x", "π" },
	{ "x²", "x³", "x^y" },
	{ "√", "³√", "1/x" },
};

static void draw_pad(bdapp *a, struct calc *k)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bool sci = k->mode == MODE_SCI;
	for (int r = 0; r < 5; r++) {
		for (int col = 0; col < 5; col++) {
			const char *act = BASIC_KEYS[r][col];
			if (!act)
				continue;
			bu_rect kr;
			if (sci && r == 4 && col == 0)
				kr = (bu_rect){ sci_rect(4, 3).x, 208 + 4 * 54, 198, 48 };
			else if (sci && r == 4 && col == 1)
				kr = sci_rect(4, 5);
			else if (sci && r == 4 && col == 2)
				kr = (bu_rect){ sci_rect(4, 6).x, 208 + 4 * 54, 198, 48 };
			else if (sci)
				kr = sci_rect(r, col + 3);
			else if (r == 4 && col == 0)
				kr = (bu_rect){ 24, 208 + 4 * 54, 320, 48 };
			else if (r == 4 && col == 2)
				kr = (bu_rect){ 24 + 3 * 164, 208 + 4 * 54, 320, 48 };
			else
				kr = key_rect(r, col);
			if (r == 4 && col == 1 && !sci)
				kr = key_rect(r, 2);
			bu_btn_style st = BU_BTN_NORMAL;
			if (strcmp(act, "=") == 0)
				st = BU_BTN_PRIMARY;
			else if (strcmp(act, "AC") == 0)
				st = BU_BTN_DANGER;
			bu_button(c, t, kr, BASIC_LAB[r][col], st, false);
		}
		if (sci) {
			const char **keys = k->inv ? SCI_KEYS_INV[r] : SCI_KEYS[r];
			const char **labs = k->inv ? SCI_LAB_INV[r] : SCI_LAB[r];
			(void)keys;
			for (int col = 0; col < 3; col++) {
				bu_btn_style st = (r == 0 && col == 0 && k->inv)
					? BU_BTN_PRIMARY : BU_BTN_NORMAL;
				bu_button(c, t, sci_rect(r, col), labs[col], st, false);
			}
		}
	}
}

static bool pad_hit(struct calc *k, int x, int y, const char **act_out)
{
	bool sci = k->mode == MODE_SCI;
	for (int r = 0; r < 5; r++) {
		for (int col = 0; col < 5; col++) {
			const char *act = BASIC_KEYS[r][col];
			if (!act)
				continue;
			bu_rect kr;
			if (sci && r == 4 && col == 0)
				kr = (bu_rect){ sci_rect(4, 3).x, 208 + 4 * 54, 198, 48 };
			else if (sci && r == 4 && col == 1)
				kr = sci_rect(4, 5);
			else if (sci && r == 4 && col == 2)
				kr = (bu_rect){ sci_rect(4, 6).x, 208 + 4 * 54, 198, 48 };
			else if (sci)
				kr = sci_rect(r, col + 3);
			else if (r == 4 && col == 0)
				kr = (bu_rect){ 24, 208 + 4 * 54, 320, 48 };
			else if (r == 4 && col == 1)
				kr = key_rect(r, 2);
			else if (r == 4 && col == 2)
				kr = (bu_rect){ 24 + 3 * 164, 208 + 4 * 54, 320, 48 };
			else
				kr = key_rect(r, col);
			if (x >= kr.x && x < kr.x + kr.w && y >= kr.y && y < kr.y + kr.h) {
				*act_out = act;
				return true;
			}
		}
		if (sci) {
			const char **keys = k->inv ? SCI_KEYS_INV[r] : SCI_KEYS[r];
			for (int col = 0; col < 3; col++) {
				bu_rect kr = sci_rect(r, col);
				if (x >= kr.x && x < kr.x + kr.w && y >= kr.y && y < kr.y + kr.h) {
					*act_out = keys[col];
					return true;
				}
			}
		}
	}
	return false;
}

static void draw_hist(bdapp *a, struct calc *k)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bu_draw_text(c, t->font, 24, 168, "計算履歴 (タップで結果を再利用)", 14, t->text_dim);
	bu_button(c, t, (bu_rect){ 700, 162, 140, 32 }, "履歴を消去", BU_BTN_DANGER, false);
	if (k->nhist == 0) {
		bu_draw_text(c, t->font, 24, 220, "まだ計算履歴がありません", 13, t->text_dim);
		return;
	}
	int visible = 6;
	for (int vi = 0; vi < visible; vi++) {
		int i = k->nhist - 1 - k->hist_scroll - vi;	/* newest first */
		if (i < 0)
			break;
		bu_rect r = { 24, 204 + vi * 44, 736, 40 };
		bu_fill_round_rect(c, r, 8, t->surface);
		char line[160];
		snprintf(line, sizeof(line), "%s = %s", k->hist[i].expr, k->hist[i].result);
		bu_draw_text(c, t->font, r.x + 14, r.y + 10, line, 14, t->text);
	}
	bu_button(c, t, (bu_rect){ 776, 204, 64, 52 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 776, 380, 64, 52 }, "▼", BU_BTN_NORMAL, false);
}

static void draw_conv(bdapp *a, struct calc *k)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	const struct convcat *cc = &CONV[k->conv_cat];
	for (int i = 0; i < NCONV; i++)
		bu_button(c, t, (bu_rect){ 24 + i * 118, 166, 110, 34 }, CONV[i].name,
			  i == k->conv_cat ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	/* from / to unit selectors (tap to cycle) */
	bu_button(c, t, (bu_rect){ 24, 212, 380, 46 }, cc->units[k->conv_from].name, BU_BTN_NORMAL, false);
	bu_draw_text_centered(c, t->font, (bu_rect){ 404, 222, 56, 26 }, "→", 18, t->text_dim);
	bu_button(c, t, (bu_rect){ 460, 212, 380, 46 }, cc->units[k->conv_to].name, BU_BTN_NORMAL, false);
	bu_draw_text(c, t->font, 24, 264, "単位ボタンをタップすると次の単位に切り替わります", 11.5, t->text_faint);
	static const char *CK[3][5] = {
		{ "7", "8", "9", "BS", "AC" },
		{ "4", "5", "6", "NEG", "SWAP" },
		{ "1", "2", "3", "0", "." },
	};
	static const char *CL[3][5] = {
		{ "7", "8", "9", "←", "AC" },
		{ "4", "5", "6", "±", "⇄ 入替" },
		{ "1", "2", "3", "0", "." },
	};
	for (int r = 0; r < 3; r++)
		for (int col = 0; col < 5; col++) {
			bu_btn_style st = BU_BTN_NORMAL;
			if (strcmp(CK[r][col], "AC") == 0)
				st = BU_BTN_DANGER;
			bu_button(c, t, (bu_rect){ 24 + col * 164, 292 + r * 54, 156, 48 },
				  CL[r][col], st, false);
		}
}

static void draw(bdapp *a, void *ctx)
{
	struct calc *k = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* mode tabs */
	static const char *tabs[3] = { "基本", "関数", "換算" };
	for (int i = 0; i < 3; i++)
		bu_button(c, t, tab_rect(i), tabs[i],
			  k->mode == i ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	if (k->mode == MODE_SCI)
		bu_button(c, t, (bu_rect){ 620, 44, 100, 30 }, k->deg ? "Deg" : "Rad",
			  k->deg ? BU_BTN_NORMAL : BU_BTN_PRIMARY, false);
	if (k->mode != MODE_CONV)
		bu_button(c, t, (bu_rect){ 744, 44, 96, 30 }, "履歴",
			  k->hist_open ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	/* display */
	bu_rect disp = { 24, 82, 816, 76 };
	bu_fill_round_rect(c, disp, 12, t->surface);
	if (k->mode == MODE_CONV) {
		const struct convcat *cc = &CONV[k->conv_cat];
		char top[128];
		snprintf(top, sizeof(top), "%s %s → %s",
			 k->conv_in[0] ? k->conv_in : "0",
			 cc->units[k->conv_from].name, cc->units[k->conv_to].name);
		bu_draw_text(c, t->font, disp.x + 14, disp.y + 8, top, 13, t->text_faint);
		double v;
		char big[96];
		if (k->conv_in[0] && evaluate(k, k->conv_in, &v)) {
			char res[32];
			fmt_result(res, sizeof(res), conv_compute(k, v));
			snprintf(big, sizeof(big), "= %s %s", res, cc->units[k->conv_to].name);
		} else {
			snprintf(big, sizeof(big), "= ? %s", cc->units[k->conv_to].name);
		}
		bu_draw_text_right(c, t->font, (bu_rect){ disp.x + 8, disp.y + 30, disp.w - 24, 40 },
				   big, 28, t->text);
	} else {
		bu_draw_text(c, t->font, disp.x + 14, disp.y + 8, k->history, 13, t->text_faint);
		if (k->has_mem)
			bu_draw_text(c, t->font, disp.x + 14, disp.y + 34, "M", 15, t->accent2);
		if (k->mode == MODE_SCI) {
			char mi[24];
			snprintf(mi, sizeof(mi), "%s%s", k->deg ? "DEG" : "RAD", k->inv ? "  2nd" : "");
			bu_draw_text_right(c, t->font, (bu_rect){ disp.x + 8, disp.y + 8, disp.w - 24, 16 },
					   mi, 11, t->text_faint);
		}
		const char *shown = k->expr[0] ? k->expr : "0";
		bu_color ink = k->error ? t->danger : t->text;
		bu_draw_text_right(c, t->font, (bu_rect){ disp.x + 8, disp.y + 30, disp.w - 24, 40 },
				   shown, 30, ink);
	}
	if (k->mode == MODE_CONV) {
		draw_conv(a, k);
		return;
	}
	if (k->hist_open) {
		draw_hist(a, k);
		return;
	}
	/* memory strip */
	static const char *mems[4] = { "MC", "M+", "M-", "MR" };
	static const char *meml[4] = { "mc", "m+", "m−", "mr" };
	for (int i = 0; i < 4; i++)
		bu_button(c, t, mem_rect(i), meml[i], BU_BTN_NORMAL, false);
	(void)mems;
	if (k->has_mem) {
		char mv[32], mb[64];
		fmt_result(mv, sizeof(mv), k->mem);
		snprintf(mb, sizeof(mb), "メモリ: %s", mv);
		bu_draw_text(c, t->font, 520, 174, mb, 13, t->text_dim);
	}
	draw_pad(a, k);
}

static void conv_press(struct calc *k, const char *act)
{
	if (strcmp(act, "AC") == 0) {
		k->conv_in[0] = 0;
	} else if (strcmp(act, "BS") == 0) {
		int l = (int)strlen(k->conv_in);
		if (l > 0)
			k->conv_in[l - 1] = 0;
	} else if (strcmp(act, "NEG") == 0) {
		char tmp[40];
		if (k->conv_in[0] == '-')
			snprintf(tmp, sizeof(tmp), "%s", k->conv_in + 1);
		else
			snprintf(tmp, sizeof(tmp), "-%s", k->conv_in);
		snprintf(k->conv_in, sizeof(k->conv_in), "%s", tmp);
	} else if (strcmp(act, "SWAP") == 0) {
		int t = k->conv_from;
		k->conv_from = k->conv_to;
		k->conv_to = t;
	} else if (strcmp(act, ".") == 0) {
		if (!strchr(k->conv_in, '.') && strlen(k->conv_in) < sizeof(k->conv_in) - 1)
			strcat(k->conv_in, ".");
	} else if (act[0] >= '0' && act[0] <= '9') {
		if (strlen(k->conv_in) < sizeof(k->conv_in) - 1)
			strcat(k->conv_in, act);
	}
}

/* physical keyboard: evdev codes, following bd-notes' convention */
static const char *key_action(struct calc *k, int code, bool shift)
{
	if (shift) {
		switch (code) {
		case 6: return "%";	/* Shift+5 */
		case 7: return "^";	/* Shift+6 */
		case 9: return "*";	/* Shift+8 */
		case 10: return "(";	/* Shift+9 */
		case 11: return ")";	/* Shift+0 */
		case 13: return "+";	/* Shift+= */
		}
	}
	if (code >= 2 && code <= 10) {
		static const char d[] = "123456789";
		static char buf[2];
		buf[0] = d[code - 2];
		buf[1] = 0;
		return buf;
	}
	switch (code) {
	case 11: return "0";
	case 12: return "-";
	case 13: return "=";
	case 14: return "BS";
	case 28: case 96: return "=";
	case 1: return "AC";
	case 52: case 83: return ".";
	case 53: case 98: return "/";
	case 55: return "*";
	case 74: return "-";
	case 78: return "+";
	case 82: return "0";
	case 79: return "1";
	case 80: return "2";
	case 81: return "3";
	case 75: return "4";
	case 76: return "5";
	case 77: return "6";
	case 71: return "7";
	case 72: return "8";
	case 73: return "9";
	case 45: return "*";	/* x */
	case 46: return "AC";	/* c */
	case 25: return k->mode == MODE_SCI ? "pi" : NULL;	/* p */
	}
	return NULL;
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct calc *k = ctx;
	(void)quit;
	if (ev->type == BU_EV_KEY) {
		if (ev->code == 42 || ev->code == 54) {
			k->shift = ev->value != 0;
			return;
		}
		if (ev->value == 0)
			return;
		if (k->mode == MODE_CONV) {
			const char *act = key_action(k, ev->code, k->shift);
			if (act && (strcmp(act, "AC") == 0 || strcmp(act, "BS") == 0 ||
				    strcmp(act, ".") == 0 || (act[0] >= '0' && act[0] <= '9'))) {
				conv_press(k, act);
				*changed = true;
			}
			return;
		}
		if (k->hist_open)
			return;
		const char *act = key_action(k, ev->code, k->shift);
		if (act) {
			press(k, act);
			*changed = true;
		}
		return;
	}
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	for (int i = 0; i < 3; i++) {
		if (inr(x, y, tab_rect(i))) {
			k->mode = i;
			k->hist_open = false;
			return;
		}
	}
	if (k->mode == MODE_SCI && inr(x, y, (bu_rect){ 620, 44, 100, 30 })) {
		press(k, "DEGRAD");
		return;
	}
	if (k->mode != MODE_CONV && inr(x, y, (bu_rect){ 744, 44, 96, 30 })) {
		k->hist_open = !k->hist_open;
		k->hist_scroll = 0;
		return;
	}
	if (k->mode == MODE_CONV) {
		for (int i = 0; i < NCONV; i++) {
			if (inr(x, y, (bu_rect){ 24 + i * 118, 166, 110, 34 })) {
				k->conv_cat = i;
				k->conv_from = 0;
				k->conv_to = CONV[i].nunits > 2 ? 2 : 1;
				return;
			}
		}
		const struct convcat *cc = &CONV[k->conv_cat];
		if (inr(x, y, (bu_rect){ 24, 212, 380, 46 })) {
			k->conv_from = (k->conv_from + 1) % cc->nunits;
			return;
		}
		if (inr(x, y, (bu_rect){ 460, 212, 380, 46 })) {
			k->conv_to = (k->conv_to + 1) % cc->nunits;
			return;
		}
		static const char *CK[3][5] = {
			{ "7", "8", "9", "BS", "AC" },
			{ "4", "5", "6", "NEG", "SWAP" },
			{ "1", "2", "3", "0", "." },
		};
		for (int r = 0; r < 3; r++)
			for (int col = 0; col < 5; col++)
				if (inr(x, y, (bu_rect){ 24 + col * 164, 292 + r * 54, 156, 48 })) {
					conv_press(k, CK[r][col]);
					return;
				}
		return;
	}
	if (k->hist_open) {
		if (inr(x, y, (bu_rect){ 700, 162, 140, 32 })) {
			k->nhist = 0;
			k->hist_scroll = 0;
			return;
		}
		if (inr(x, y, (bu_rect){ 776, 204, 64, 52 })) {
			if (k->hist_scroll > 0)
				k->hist_scroll--;
			return;
		}
		if (inr(x, y, (bu_rect){ 776, 380, 64, 52 })) {
			if (k->hist_scroll + 6 < k->nhist)
				k->hist_scroll++;
			return;
		}
		for (int vi = 0; vi < 6; vi++) {
			int i = k->nhist - 1 - k->hist_scroll - vi;
			if (i < 0)
				break;
			if (inr(x, y, (bu_rect){ 24, 204 + vi * 44, 736, 40 })) {
				snprintf(k->expr, sizeof(k->expr), "%s", k->hist[i].result);
				k->just_evaled = true;
				k->error = false;
				k->hist_open = false;
				return;
			}
		}
		return;
	}
	static const char *mems[4] = { "MC", "M+", "M-", "MR" };
	for (int i = 0; i < 4; i++) {
		if (inr(x, y, mem_rect(i))) {
			press(k, mems[i]);
			return;
		}
	}
	const char *act;
	if (pad_hit(k, x, y, &act)) {
		/* 2nd / DegRad are state keys, not expression text */
		if (strcmp(act, "2ND") == 0)
			press(k, "2ND");
		else
			press(k, act);
	}
}

int main(int argc, char **argv)
{
	struct calc k;
	memset(&k, 0, sizeof(k));
	k.mode = MODE_BASIC;
	k.deg = true;
	k.conv_cat = 0;
	k.conv_from = 2;	/* m */
	k.conv_to = 3;		/* km */
	/* shot/demo hooks (host verification, same style as BD_SHOT_*) */
	const char *m = getenv("BD_CALC_MODE");
	if (m) {
		if (strcmp(m, "sci") == 0) k.mode = MODE_SCI;
		else if (strcmp(m, "conv") == 0) k.mode = MODE_CONV;
	}
	if (getenv("BD_CALC_DEMO")) {
		snprintf(k.expr, sizeof(k.expr), "2+3*4");
		press(&k, "=");
		snprintf(k.expr, sizeof(k.expr), "sin(30)");
		press(&k, "=");
		snprintf(k.expr, sizeof(k.expr), "2^10+sqrt(144)");
		press(&k, "=");
		snprintf(k.expr, sizeof(k.expr), "5");
		press(&k, "M+");
		k.hist_open = getenv("BD_CALC_HIST") != NULL;
	}
	const char *e = getenv("BD_CALC_EXPR");
	if (e) {
		snprintf(k.expr, sizeof(k.expr), "%s", e);
		if (getenv("BD_CALC_EVAL"))
			press(&k, "=");
	}
	const char *ci = getenv("BD_CALC_CONV_IN");
	if (ci)
		snprintf(k.conv_in, sizeof(k.conv_in), "%s", ci);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-calc.ppm" : NULL;
	return bdapp_run("電卓", shot, draw, onevent, NULL, &k);
}
