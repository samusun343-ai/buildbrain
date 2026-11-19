#include "bui_osk.h"
#include <stdio.h>
#include <string.h>

/* --- romaji -> hiragana (Hepburn, common syllables) --- */
struct conv { const char *romaji; const char *kana; };
static const struct conv CONV[] = {
	{ "kya", "きゃ" }, { "kyu", "きゅ" }, { "kyo", "きょ" },
	{ "sha", "しゃ" }, { "shu", "しゅ" }, { "sho", "しょ" },
	{ "sya", "しゃ" }, { "syu", "しゅ" }, { "syo", "しょ" },
	{ "cha", "ちゃ" }, { "chu", "ちゅ" }, { "cho", "ちょ" },
	{ "tya", "ちゃ" }, { "tyu", "ちゅ" }, { "tyo", "ちょ" },
	{ "nya", "にゃ" }, { "nyu", "にゅ" }, { "nyo", "にょ" },
	{ "hya", "ひゃ" }, { "hyu", "ひゅ" }, { "hyo", "ひょ" },
	{ "mya", "みゃ" }, { "myu", "みゅ" }, { "myo", "みょ" },
	{ "rya", "りゃ" }, { "ryu", "りゅ" }, { "ryo", "りょ" },
	{ "gya", "ぎゃ" }, { "gyu", "ぎゅ" }, { "gyo", "ぎょ" },
	{ "ja", "じゃ" }, { "ju", "じゅ" }, { "jo", "じょ" },
	{ "jya", "じゃ" }, { "jyu", "じゅ" }, { "jyo", "じょ" },
	{ "bya", "びゃ" }, { "byu", "びゅ" }, { "byo", "びょ" },
	{ "pya", "ぴゃ" }, { "pyu", "ぴゅ" }, { "pyo", "ぴょ" },
	{ "ka", "か" }, { "ki", "き" }, { "ku", "く" }, { "ke", "け" }, { "ko", "こ" },
	{ "sa", "さ" }, { "shi", "し" }, { "su", "す" }, { "se", "せ" }, { "so", "そ" },
	{ "si", "し" },
	{ "ta", "た" }, { "chi", "ち" }, { "tsu", "つ" }, { "te", "て" }, { "to", "と" },
	{ "ti", "ち" }, { "tu", "つ" },
	{ "na", "な" }, { "ni", "に" }, { "nu", "ぬ" }, { "ne", "ね" }, { "no", "の" },
	{ "ha", "は" }, { "hi", "ひ" }, { "fu", "ふ" }, { "he", "へ" }, { "ho", "ほ" },
	{ "hu", "ふ" },
	{ "ma", "ま" }, { "mi", "み" }, { "mu", "む" }, { "me", "め" }, { "mo", "も" },
	{ "ya", "や" }, { "yu", "ゆ" }, { "yo", "よ" },
	{ "ra", "ら" }, { "ri", "り" }, { "ru", "る" }, { "re", "れ" }, { "ro", "ろ" },
	{ "wa", "わ" }, { "wi", "ゐ" }, { "we", "ゑ" }, { "wo", "を" },
	{ "ga", "が" }, { "gi", "ぎ" }, { "gu", "ぐ" }, { "ge", "げ" }, { "go", "ご" },
	{ "za", "ざ" }, { "ji", "じ" }, { "zu", "ず" }, { "ze", "ぜ" }, { "zo", "ぞ" },
	{ "zi", "じ" },
	{ "da", "だ" }, { "di", "ぢ" }, { "du", "づ" }, { "de", "で" }, { "do", "ど" },
	{ "ba", "ば" }, { "bi", "び" }, { "bu", "ぶ" }, { "be", "べ" }, { "bo", "ぼ" },
	{ "pa", "ぱ" }, { "pi", "ぴ" }, { "pu", "ぷ" }, { "pe", "ぺ" }, { "po", "ぽ" },
	{ "a", "あ" }, { "i", "い" }, { "u", "う" }, { "e", "え" }, { "o", "お" },
	{ "vu", "ゔ" },
};
#define NCONV (sizeof(CONV) / sizeof(CONV[0]))

static const struct conv *conv_find(const char *s, int len)
{
	for (size_t i = 0; i < NCONV; i++)
		if ((int)strlen(CONV[i].romaji) == len && strncmp(CONV[i].romaji, s, len) == 0)
			return &CONV[i];
	return NULL;
}

static bool conv_is_prefix(const char *s, int len)
{
	for (size_t i = 0; i < NCONV; i++)
		if ((int)strlen(CONV[i].romaji) > len && strncmp(CONV[i].romaji, s, len) == 0)
			return true;
	return false;
}

void bu_osk_init(bu_osk *o)
{
	memset(o, 0, sizeof(*o));
	o->kana = true;
}

/* Feed one romaji letter; writes converted kana (or the letter
 * itself when it cannot convert) into out. Returns byte count. */
static int osk_feed(bu_osk *o, char ch, char *out, int out_size)
{
	if (!o->kana) {
		if (out_size > 1) {
			out[0] = ch;
			out[1] = 0;
			return 1;
		}
		return 0;
	}
	/* 'n' handling: nn -> ん, n before non-vowel/y -> ん */
	o->pending[o->pending_len++] = ch;
	o->pending[o->pending_len] = 0;
	/* doubled consonant -> っ (keep the consonant pending) */
	if (o->pending_len == 2 && o->pending[0] == o->pending[1] &&
	    strchr("bcdfghjklmpqrstvwz", o->pending[0])) {
		o->pending[0] = o->pending[1];
		o->pending_len = 1;
		o->pending[1] = 0;
		snprintf(out, out_size, "っ");
		return 3;
	}
	const struct conv *cv = conv_find(o->pending, o->pending_len);
	if (cv) {
		o->pending_len = 0;
		o->pending[0] = 0;
		snprintf(out, out_size, "%s", cv->kana);
		return (int)strlen(out);
	}
	if (o->pending_len >= 2 && o->pending[0] == 'n' &&
	    strchr("bcdfghjklmpqrstvwxz", o->pending[1])) {
		/* ん + restart with the consonant */
		char rest = o->pending[1];
		o->pending[0] = rest;
		o->pending_len = 1;
		o->pending[1] = 0;
		snprintf(out, out_size, "ん");
		return 3;
	}
	if (conv_is_prefix(o->pending, o->pending_len)) {
		out[0] = 0;
		return 0;
	}
	/* cannot convert: emit pending as-is (or ん for lone n) */
	if (o->pending_len == 2 && o->pending[0] == 'n') {
		char rest = o->pending[1];
		o->pending[0] = rest;
		o->pending_len = 1;
		o->pending[1] = 0;
		snprintf(out, out_size, "ん");
		return 3;
	}
	snprintf(out, out_size, "%s", o->pending);
	int n = (int)strlen(out);
	o->pending_len = 0;
	o->pending[0] = 0;
	return n;
}

const char *bu_osk_flush(bu_osk *o, char *out, int out_size)
{
	if (o->pending_len == 1 && o->pending[0] == 'n')
		snprintf(out, out_size, "ん");
	else
		snprintf(out, out_size, "%s", o->pending);
	o->pending_len = 0;
	o->pending[0] = 0;
	return out;
}

/* --- layout --- */
static const char *ROW0 = "1234567890";
static const char *ROW1 = "qwertyuiop";
static const char *ROW2 = "asdfghjkl";
/* row3: z x c v b n m + backspace ; row4: mode, space, enter */

int bu_osk_height(void)
{
	return 5 * 36 + 8;
}

static bu_rect key_at(bu_rect area, int row, int idx, int count)
{
	int kw = area.w / 10;
	if (row < 2)
		return (bu_rect){ area.x + idx * kw, area.y + 4 + row * 36, kw - 4, 32 };
	if (row == 2) {
		int off = kw / 2;
		return (bu_rect){ area.x + off + idx * kw, area.y + 4 + row * 36, kw - 4, 32 };
	}
	if (row == 3) {
		int off = kw / 2;
		if (idx < 7)
			return (bu_rect){ area.x + off + idx * kw, area.y + 4 + row * 36, kw - 4, 32 };
		return (bu_rect){ area.x + off + 7 * kw, area.y + 4 + row * 36, kw + kw / 2 - 4, 32 };
	}
	/* row 4: mode (2 keys wide), space (5), enter (2.5) */
	if (idx == 0)
		return (bu_rect){ area.x, area.y + 4 + row * 36, kw * 2 - 4, 32 };
	if (idx == 1)
		return (bu_rect){ area.x + kw * 2, area.y + 4 + row * 36, kw * 5 - 4, 32 };
	return (bu_rect){ area.x + kw * 7, area.y + 4 + row * 36, kw * 3 - 4, 32 };
	(void)count;
}

static void draw_key(bu_canvas *c, bu_theme *t, bu_rect r, const char *label, bool accent)
{
	bu_fill_round_rect(c, r, 6, accent ? t->accent : t->surface);
	bu_draw_text_centered(c, t->font, (bu_rect){ r.x, r.y + 3, r.w, r.h - 6 }, label, 15,
			      accent ? BU_RGB(255, 255, 255) : t->text);
}

void bu_osk_draw(bu_canvas *c, bu_theme *t, bu_osk *o, bu_rect area)
{
	bu_fill_rect(c, area, BU_RGBA(10, 14, 22, 245));
	char buf[2] = { 0, 0 };
	for (int i = 0; i < 10; i++) {
		buf[0] = ROW0[i];
		draw_key(c, t, key_at(area, 0, i, 10), buf, false);
	}
	for (int i = 0; i < 10; i++) {
		char ch = ROW1[i];
		buf[0] = o->shift ? ch - 32 : ch;
		draw_key(c, t, key_at(area, 1, i, 10), buf, false);
	}
	for (int i = 0; i < 9; i++) {
		char ch = ROW2[i];
		buf[0] = o->shift ? ch - 32 : ch;
		draw_key(c, t, key_at(area, 2, i, 9), buf, false);
	}
	const char *row3 = "zxcvbnm";
	for (int i = 0; i < 7; i++) {
		char ch = row3[i];
		buf[0] = o->shift ? ch - 32 : ch;
		draw_key(c, t, key_at(area, 3, i, 8), buf, false);
	}
	draw_key(c, t, key_at(area, 3, 7, 8), "⌫", false);
	draw_key(c, t, key_at(area, 4, 0, 3), o->kana ? "ABC" : "かな", true);
	draw_key(c, t, key_at(area, 4, 1, 3), o->pending_len ? o->pending : "スペース", false);
	draw_key(c, t, key_at(area, 4, 2, 3), "改行", false);
}

int bu_osk_hit(bu_osk *o, bu_rect area, int x, int y, char *out, int out_size)
{
	out[0] = 0;
	for (int row = 0; row < 5; row++) {
		int count = row == 0 || row == 1 ? 10 : row == 2 ? 9 : row == 3 ? 8 : 3;
		for (int i = 0; i < count; i++) {
			bu_rect r = key_at(area, row, i, count);
			if (!(x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h))
				continue;
			if (row == 4 && i == 0) {
				char flushbuf[16];
				bu_osk_flush(o, flushbuf, sizeof(flushbuf));
				o->kana = !o->kana;
				snprintf(out, out_size, "%s", flushbuf);
				return OSK_TEXT;
			}
			if (row == 4 && i == 1) {
				char flushbuf[16];
				bu_osk_flush(o, flushbuf, sizeof(flushbuf));
				snprintf(out, out_size, "%s ", flushbuf);
				return OSK_TEXT;
			}
			if (row == 4 && i == 2)
				return OSK_ENTER;
			if (row == 3 && i == 7) {
				if (o->pending_len > 0) {
					o->pending[--o->pending_len] = 0;
					return OSK_TEXT; /* out stays empty */
				}
				return OSK_BACKSPACE;
			}
			char ch;
			if (row == 0)
				ch = ROW0[i];
			else if (row == 1)
				ch = ROW1[i];
			else if (row == 2)
				ch = ROW2[i];
			else
				ch = "zxcvbnm"[i];
			if (o->shift && ch >= 'a' && ch <= 'z')
				ch -= 32;
			osk_feed(o, ch, out, out_size);
			return OSK_TEXT;
		}
	}
	return OSK_NONE;
}
