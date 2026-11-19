#include "bui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

/* ---------- canvas ---------- */

bu_canvas *bu_canvas_new(int w, int h)
{
	bu_canvas *c = calloc(1, sizeof(*c));
	c->w = w; c->h = h;
	c->px = calloc((size_t)w * h, sizeof(uint32_t));
	c->clip = (bu_rect){ 0, 0, w, h };
	return c;
}

void bu_canvas_free(bu_canvas *c)
{
	if (!c) return;
	free(c->px);
	free(c);
}

static inline uint32_t pack(bu_color col)
{
	return ((uint32_t)col.a << 24) | ((uint32_t)col.r << 16) |
	       ((uint32_t)col.g << 8) | col.b;
}

static inline void blend_px(bu_canvas *c, int x, int y, bu_color col)
{
	if (x < c->clip.x || y < c->clip.y ||
	    x >= c->clip.x + c->clip.w || y >= c->clip.y + c->clip.h)
		return;
	uint32_t dst = c->px[y * c->w + x];
	uint32_t da = (dst >> 24) & 0xff;
	uint32_t dr = (dst >> 16) & 0xff, dg = (dst >> 8) & 0xff, db = dst & 0xff;
	uint32_t sa = col.a;
	uint32_t r = (col.r * sa + dr * (255 - sa)) / 255;
	uint32_t g = (col.g * sa + dg * (255 - sa)) / 255;
	uint32_t b = (col.b * sa + db * (255 - sa)) / 255;
	uint32_t a = sa + da * (255 - sa) / 255;
	c->px[y * c->w + x] = (a << 24) | (r << 16) | (g << 8) | b;
}

void bu_canvas_clear(bu_canvas *c, bu_color col)
{
	uint32_t v = pack(col);
	for (int i = 0; i < c->w * c->h; i++)
		c->px[i] = v;
}

bool bu_canvas_write_ppm(bu_canvas *c, const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
	for (int i = 0; i < c->w * c->h; i++) {
		uint32_t v = c->px[i];
		fputc((v >> 16) & 0xff, f);
		fputc((v >> 8) & 0xff, f);
		fputc(v & 0xff, f);
	}
	fclose(f);
	return true;
}

/* ---------- primitives ---------- */

void bu_fill_rect(bu_canvas *c, bu_rect r, bu_color col)
{
	for (int y = r.y; y < r.y + r.h; y++)
		for (int x = r.x; x < r.x + r.w; x++)
			blend_px(c, x, y, col);
}

static float sdf_round_rect(float px, float py, float cx, float cy, float hw, float hh, float rad)
{
	float qx = fabsf(px - cx) - (hw - rad);
	float qy = fabsf(py - cy) - (hh - rad);
	float ax = fmaxf(qx, 0.0f), ay = fmaxf(qy, 0.0f);
	return sqrtf(ax * ax + ay * ay) + fminf(fmaxf(qx, qy), 0.0f) - rad;
}

static void round_rect_impl(bu_canvas *c, bu_rect r, int radius, bu_color col, bool stroke, int swidth)
{
	float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f;
	float hw = r.w / 2.0f, hh = r.h / 2.0f;
	int pad = 2;
	for (int y = r.y - pad; y < r.y + r.h + pad; y++) {
		for (int x = r.x - pad; x < r.x + r.w + pad; x++) {
			float d = sdf_round_rect(x + 0.5f, y + 0.5f, cx, cy, hw, hh, radius);
			float cov;
			if (stroke)
				cov = 0.5f - fabsf(d + swidth / 2.0f) + swidth / 2.0f;
			else
				cov = 0.5f - d;
			if (cov <= 0.0f) continue;
			if (cov > 1.0f) cov = 1.0f;
			bu_color cc = col;
			cc.a = (uint8_t)(col.a * cov);
			blend_px(c, x, y, cc);
		}
	}
}

void bu_fill_round_rect(bu_canvas *c, bu_rect r, int radius, bu_color col)
{
	round_rect_impl(c, r, radius, col, false, 0);
}

void bu_stroke_round_rect(bu_canvas *c, bu_rect r, int radius, int width, bu_color col)
{
	round_rect_impl(c, r, radius, col, true, width);
}

void bu_gradient_v(bu_canvas *c, bu_rect r, bu_color top, bu_color bottom)
{
	for (int y = r.y; y < r.y + r.h; y++) {
		float f = r.h > 1 ? (float)(y - r.y) / (r.h - 1) : 0;
		bu_color col = BU_RGBA(
			(uint8_t)(top.r + (bottom.r - top.r) * f),
			(uint8_t)(top.g + (bottom.g - top.g) * f),
			(uint8_t)(top.b + (bottom.b - top.b) * f),
			(uint8_t)(top.a + (bottom.a - top.a) * f));
		for (int x = r.x; x < r.x + r.w; x++)
			blend_px(c, x, y, col);
	}
}

void bu_gradient_round_v(bu_canvas *c, bu_rect r, int radius, bu_color top, bu_color bottom)
{
	float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f;
	float hw = r.w / 2.0f, hh = r.h / 2.0f;
	for (int y = r.y - 1; y < r.y + r.h + 1; y++) {
		float f = r.h > 1 ? (float)(y - r.y) / (r.h - 1) : 0;
		if (f < 0) f = 0;
		if (f > 1) f = 1;
		bu_color col = BU_RGBA(
			(uint8_t)(top.r + (bottom.r - top.r) * f),
			(uint8_t)(top.g + (bottom.g - top.g) * f),
			(uint8_t)(top.b + (bottom.b - top.b) * f), 255);
		for (int x = r.x - 1; x < r.x + r.w + 1; x++) {
			float d = sdf_round_rect(x + 0.5f, y + 0.5f, cx, cy, hw, hh, radius);
			float cov = 0.5f - d;
			if (cov <= 0) continue;
			if (cov > 1) cov = 1;
			bu_color cc = col; cc.a = (uint8_t)(255 * cov);
			blend_px(c, x, y, cc);
		}
	}
}

void bu_shadow_round_rect(bu_canvas *c, bu_rect r, int radius, int blur, bu_color col)
{
	for (int i = blur; i > 0; i--) {
		bu_rect e = { r.x - i, r.y - i + blur / 2, r.w + 2 * i, r.h + 2 * i };
		bu_color cc = col;
		cc.a = (uint8_t)(col.a / (blur + 1));
		bu_fill_round_rect(c, e, radius + i, cc);
	}
}

void bu_fill_circle(bu_canvas *c, int cx, int cy, int radius, bu_color col)
{
	for (int y = cy - radius - 1; y <= cy + radius + 1; y++) {
		for (int x = cx - radius - 1; x <= cx + radius + 1; x++) {
			float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
			float cov = radius + 0.5f - d;
			if (cov <= 0) continue;
			if (cov > 1) cov = 1;
			bu_color cc = col; cc.a = (uint8_t)(col.a * cov);
			blend_px(c, x, y, cc);
		}
	}
}

void bu_line(bu_canvas *c, int x0, int y0, int x1, int y1, int width, bu_color col)
{
	int steps = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
	if (steps == 0) { bu_fill_circle(c, x0, y0, width / 2, col); return; }
	for (int i = 0; i <= steps; i++) {
		int x = x0 + (x1 - x0) * i / steps;
		int y = y0 + (y1 - y0) * i / steps;
		bu_fill_circle(c, x, y, width / 2, col);
	}
}

/* ---------- text ---------- */

#define GLYPH_CACHE 1024

typedef struct {
	int codepoint;
	int size;		/* pixel height bucket */
	int w, h, xoff, yoff, advance;
	unsigned char *bmp;
} glyph_entry;

struct bu_font {
	stbtt_fontinfo info;
	unsigned char *data;
	long data_len;
	int ascent, descent, linegap;
	glyph_entry cache[GLYPH_CACHE];
	int cache_len;
};

static int utf8_next(const char **sp)
{
	const unsigned char *s = (const unsigned char *)*sp;
	if (!*s) return 0;
	int cp;
	if (s[0] < 0x80) { cp = s[0]; *sp += 1; }
	else if ((s[0] & 0xe0) == 0xc0) { cp = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f); *sp += 2; }
	else if ((s[0] & 0xf0) == 0xe0) { cp = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f); *sp += 3; }
	else { cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f); *sp += 4; }
	return cp;
}

bu_font *bu_font_load(const char *path)
{
	/* The font is mmap'ed, never read into anonymous memory:
	 * NotoSansCJK is ~19 MB as a TTC, but a desktop session only
	 * touches the cmap and the glyphs it actually draws, so the
	 * resident cost is a few hundred KiB of file-backed pages
	 * the kernel can reclaim under pressure. Reading the whole
	 * file into malloc'ed memory cost 19 MB of PSS (measured:
	 * braindesk 22.2 MB before this change). */
	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) < 0 || st.st_size == 0) { close(fd); return NULL; }
	unsigned char *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (data == MAP_FAILED) return NULL;
	bu_font *font = calloc(1, sizeof(*font));
	font->data = data;
	font->data_len = st.st_size;
	int off = stbtt_GetFontOffsetForIndex(data, 0);
	if (!stbtt_InitFont(&font->info, data, off)) { munmap(data, st.st_size); free(font); return NULL; }
	stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->linegap);
	return font;
}

void bu_font_free(bu_font *f)
{
	if (!f) return;
	for (int i = 0; i < f->cache_len; i++)
		free(f->cache[i].bmp);
	munmap(f->data, f->data_len);
	free(f);
}

static glyph_entry *glyph_get(bu_font *f, int cp, int size)
{
	for (int i = 0; i < f->cache_len; i++)
		if (f->cache[i].codepoint == cp && f->cache[i].size == size)
			return &f->cache[i];
	if (f->cache_len >= GLYPH_CACHE)
		return NULL;
	glyph_entry *g = &f->cache[f->cache_len];
	float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);
	int adv, lsb;
	stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
	g->codepoint = cp;
	g->size = size;
	g->advance = (int)(adv * scale + 0.5f);
	g->bmp = stbtt_GetCodepointBitmap(&f->info, scale, scale, cp,
					   &g->w, &g->h, &g->xoff, &g->yoff);
	f->cache_len++;
	return g;
}

int bu_text_width(bu_font *f, const char *utf8, float px)
{
	int size = (int)(px + 0.5f);
	int w = 0;
	const char *s = utf8;
	int cp;
	while ((cp = utf8_next(&s)) != 0) {
		glyph_entry *g = glyph_get(f, cp, size);
		if (g) w += g->advance;
	}
	return w;
}

void bu_draw_text(bu_canvas *c, bu_font *f, int x, int y, const char *utf8, float px, bu_color col)
{
	int size = (int)(px + 0.5f);
	float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);
	int baseline = y + (int)(f->ascent * scale + 0.5f);
	int pen = x;
	const char *s = utf8;
	int cp;
	while ((cp = utf8_next(&s)) != 0) {
		glyph_entry *g = glyph_get(f, cp, size);
		if (!g) continue;
		if (g->bmp) {
			for (int gy = 0; gy < g->h; gy++) {
				for (int gx = 0; gx < g->w; gx++) {
					unsigned char cov = g->bmp[gy * g->w + gx];
					if (!cov) continue;
					bu_color cc = col;
					cc.a = (uint8_t)(col.a * cov / 255);
					blend_px(c, pen + g->xoff + gx, baseline + g->yoff + gy, cc);
				}
			}
		}
		pen += g->advance;
	}
}

void bu_draw_text_centered(bu_canvas *c, bu_font *f, bu_rect r, const char *utf8, float px, bu_color col)
{
	int w = bu_text_width(f, utf8, px);
	int size = (int)(px + 0.5f);
	float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);
	int th = (int)((f->ascent - f->descent) * scale + 0.5f);
	bu_draw_text(c, f, r.x + (r.w - w) / 2, r.y + (r.h - th) / 2, utf8, px, col);
}

void bu_draw_text_right(bu_canvas *c, bu_font *f, bu_rect r, const char *utf8, float px, bu_color col)
{
	int w = bu_text_width(f, utf8, px);
	int size = (int)(px + 0.5f);
	float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);
	int th = (int)((f->ascent - f->descent) * scale + 0.5f);
	bu_draw_text(c, f, r.x + r.w - w, r.y + (r.h - th) / 2, utf8, px, col);
}

/* ---------- theme ---------- */

bu_theme bu_theme_dark = {
	.bg = { 16, 20, 28, 255 },
	.surface = { 28, 34, 46, 255 },
	.surface2 = { 38, 46, 61, 255 },
	.surface3 = { 50, 60, 78, 255 },
	.line = { 62, 74, 94, 255 },
	.text = { 238, 242, 247, 255 },
	.text_dim = { 168, 178, 194, 255 },
	.text_faint = { 110, 121, 140, 255 },
	.accent = { 45, 156, 219, 255 },
	.accent2 = { 32, 201, 151, 255 },
	.danger = { 224, 82, 82, 255 },
	.ok = { 81, 207, 102, 255 },
	.warn = { 250, 176, 5, 255 },
	.radius = 10,
	.font = NULL,
};

/* ---------- widgets ---------- */

void bu_button(bu_canvas *c, bu_theme *t, bu_rect r, const char *label, bu_btn_style style, bool pressed)
{
	bu_color fill, fg = t->text;
	switch (style) {
	case BU_BTN_PRIMARY: fill = t->accent; fg = BU_RGB(255, 255, 255); break;
	case BU_BTN_DANGER: fill = t->danger; fg = BU_RGB(255, 255, 255); break;
	case BU_BTN_GHOST: fill = BU_RGBA(0, 0, 0, 0); break;
	default: fill = t->surface2; break;
	}
	if (pressed) {
		fill.r = fill.r * 4 / 5; fill.g = fill.g * 4 / 5; fill.b = fill.b * 4 / 5;
		r.y += 1;
	}
	if (style != BU_BTN_GHOST)
		bu_fill_round_rect(c, r, t->radius, fill);
	else
		bu_stroke_round_rect(c, r, t->radius, 1, t->line);
	bu_draw_text_centered(c, t->font, r, label, 15, fg);
}

void bu_toggle(bu_canvas *c, bu_theme *t, int x, int y, bool on)
{
	bu_rect track = { x, y, 46, 26 };
	bu_fill_round_rect(c, track, 13, on ? t->accent : t->surface3);
	int kx = on ? x + 23 : x + 3;
	bu_fill_circle(c, kx + 10, y + 13, 10, BU_RGB(255, 255, 255));
}

void bu_slider(bu_canvas *c, bu_theme *t, bu_rect r, float frac, bool active)
{
	int cy = r.y + r.h / 2;
	bu_rect track = { r.x, cy - 3, r.w, 6 };
	bu_fill_round_rect(c, track, 3, t->surface3);
	bu_rect fill = track;
	fill.w = (int)(r.w * frac);
	bu_fill_round_rect(c, fill, 3, active ? t->accent : t->text_faint);
	int kx = r.x + fill.w;
	bu_shadow_round_rect(c, (bu_rect){ kx - 10, cy - 10, 20, 20 }, 10, 3, BU_RGBA(0, 0, 0, 90));
	bu_fill_circle(c, kx, cy, 10, BU_RGB(255, 255, 255));
}

void bu_card(bu_canvas *c, bu_theme *t, bu_rect r)
{
	bu_shadow_round_rect(c, r, t->radius + 2, 6, BU_RGBA(0, 0, 0, 70));
	bu_fill_round_rect(c, r, t->radius + 2, t->surface);
}

void bu_list_row(bu_canvas *c, bu_theme *t, bu_rect r, const char *title, const char *value, bool chevron, bool selected)
{
	if (selected)
		bu_fill_round_rect(c, r, 8, t->surface2);
	bu_draw_text(c, t->font, r.x + 14, r.y + (r.h - 22) / 2, title, 15, t->text);
	int right = r.x + r.w - 14;
	if (chevron) {
		bu_line(c, right - 6, r.y + r.h / 2 - 5, right, r.y + r.h / 2, 2, t->text_faint);
		bu_line(c, right, r.y + r.h / 2, right - 6, r.y + r.h / 2 + 5, 2, t->text_faint);
		right -= 16;
	}
	if (value)
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y, right - r.x, r.h }, value, 14, t->text_dim);
	bu_rect sep = { r.x + 14, r.y + r.h - 1, r.w - 28, 1 };
	bu_fill_rect(c, sep, BU_RGBA(255, 255, 255, 14));
}

void bu_text_field(bu_canvas *c, bu_theme *t, bu_rect r, const char *placeholder, const char *masked, bool focused)
{
	bu_fill_round_rect(c, r, t->radius, t->surface2);
	if (focused)
		bu_stroke_round_rect(c, r, t->radius, 2, t->accent);
	const char *s = masked ? masked : placeholder;
	bu_color col = masked ? t->text : t->text_faint;
	bu_draw_text(c, t->font, r.x + 14, r.y + (r.h - 22) / 2, s, 15, col);
	if (focused && !masked) {
		int w = 0;
		bu_fill_rect(c, (bu_rect){ r.x + 14 + w, r.y + 8, 2, r.h - 16 }, t->accent);
	}
}

/* ---------- icons ---------- */

void bu_icon_battery(bu_canvas *c, int x, int y, float level, bool charging, bu_color col)
{
	bu_rect body = { x, y, 25, 13 };
	bu_stroke_round_rect(c, body, 3, 1, col);
	bu_fill_rect(c, (bu_rect){ x + 26, y + 4, 3, 5 }, col);
	int fw = (int)(21 * level);
	bu_color fc = level < 0.2f ? BU_RGB(224, 82, 82) : col;
	bu_fill_round_rect(c, (bu_rect){ x + 2, y + 2, fw, 9 }, 2, fc);
	(void)charging;
}

void bu_icon_volume(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_fill_rect(c, (bu_rect){ cx - 9, cy - 3, 4, 7 }, col);
	bu_line(c, cx - 5, cy - 3, cx - 1, cy - 7, 2, col);
	bu_line(c, cx - 1, cy - 7, cx - 1, cy + 7, 2, col);
	bu_line(c, cx - 1, cy + 7, cx - 5, cy + 3, 2, col);
	bu_line(c, cx + 3, cy - 4, cx + 6, cy, 2, col);
	bu_line(c, cx + 6, cy, cx + 3, cy + 4, 2, col);
	bu_line(c, cx + 6, cy - 7, cx + 10, cy, 2, col);
	bu_line(c, cx + 10, cy, cx + 6, cy + 7, 2, col);
}

void bu_icon_brightness(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_fill_circle(c, cx, cy, 5, col);
	for (int i = 0; i < 8; i++) {
		double a = i * 3.14159265 / 4.0;
		int x0 = cx + (int)(8 * cos(a)), y0 = cy + (int)(8 * sin(a));
		int x1 = cx + (int)(11 * cos(a)), y1 = cy + (int)(11 * sin(a));
		bu_line(c, x0, y0, x1, y1, 2, col);
	}
}

void bu_icon_wifi(bu_canvas *c, int cx, int cy, bu_color col)
{
	for (int r = 12; r >= 5; r -= 3) {
		for (int i = 0; i <= 24; i++) {
			double a = 3.14159265 * (0.75 + 0.5 * i / 24.0);
			int x = cx + (int)(r * cos(a)), y = cy + 4 + (int)(r * sin(a));
			blend_px(c, x, y, col);
			blend_px(c, x + 1, y, col);
		}
	}
	bu_fill_circle(c, cx, cy + 6, 2, col);
}

void bu_icon_power(bu_canvas *c, int cx, int cy, bu_color col)
{
	for (int i = 0; i <= 40; i++) {
		double a = -3.14159265 / 2 + 3.14159265 * 1.5 * i / 40.0;
		int x = cx + (int)(9 * cos(a)), y = cy + 1 + (int)(9 * sin(a));
		blend_px(c, x, y, col);
		blend_px(c, x + 1, y, col);
		blend_px(c, x, y + 1, col);
	}
	bu_line(c, cx, cy - 11, cx, cy - 1, 3, col);
}

void bu_icon_gear(bu_canvas *c, int cx, int cy, bu_color col)
{
	for (int i = 0; i < 6; i++) {
		double a = i * 3.14159265 / 3.0;
		int x = cx + (int)(9 * cos(a)), y = cy + (int)(9 * sin(a));
		bu_fill_circle(c, x, y, 3, col);
	}
	bu_fill_circle(c, cx, cy, 8, col);
	bu_fill_circle(c, cx, cy, 4, BU_RGBA(0, 0, 0, 160));
}

void bu_icon_home(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_line(c, cx - 10, cy + 1, cx, cy - 8, 3, col);
	bu_line(c, cx, cy - 8, cx + 10, cy + 1, 3, col);
	bu_fill_rect(c, (bu_rect){ cx - 7, cy + 1, 14, 8 }, col);
	bu_fill_rect(c, (bu_rect){ cx - 2, cy + 4, 4, 5 }, BU_RGBA(0, 0, 0, 170));
}

void bu_icon_search(bu_canvas *c, int cx, int cy, bu_color col)
{
	for (int i = 0; i <= 48; i++) {
		double a = 2 * 3.14159265 * i / 48.0;
		int x = cx - 2 + (int)(7 * cos(a)), y = cy - 2 + (int)(7 * sin(a));
		blend_px(c, x, y, col);
		blend_px(c, x + 1, y, col);
		blend_px(c, x, y + 1, col);
	}
	bu_line(c, cx + 3, cy + 3, cx + 10, cy + 10, 3, col);
}

void bu_icon_folder(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_fill_round_rect(c, (bu_rect){ cx - 11, cy - 6, 22, 13 }, 3, col);
	bu_fill_round_rect(c, (bu_rect){ cx - 11, cy - 9, 10, 5 }, 2, col);
}

void bu_icon_terminal(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_stroke_round_rect(c, (bu_rect){ cx - 11, cy - 9, 22, 18 }, 3, 2, col);
	bu_line(c, cx - 7, cy - 4, cx - 3, cy, 2, col);
	bu_line(c, cx - 3, cy, cx - 7, cy + 4, 2, col);
	bu_line(c, cx + 1, cy + 5, cx + 7, cy + 5, 2, col);
}

void bu_icon_book(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_fill_round_rect(c, (bu_rect){ cx - 10, cy - 10, 9, 20 }, 2, col);
	bu_fill_round_rect(c, (bu_rect){ cx + 1, cy - 10, 9, 20 }, 2, col);
	bu_fill_rect(c, (bu_rect){ cx - 1, cy - 10, 2, 20 }, BU_RGBA(0, 0, 0, 120));
}

void bu_icon_note(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_stroke_round_rect(c, (bu_rect){ cx - 8, cy - 11, 16, 22 }, 2, 2, col);
	bu_line(c, cx - 4, cy - 5, cx + 4, cy - 5, 2, col);
	bu_line(c, cx - 4, cy, cx + 4, cy, 2, col);
	bu_line(c, cx - 4, cy + 5, cx + 1, cy + 5, 2, col);
}

void bu_icon_music(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_line(c, cx + 2, cy - 10, cx + 2, cy + 4, 3, col);
	bu_line(c, cx + 2, cy - 10, cx + 10, cy - 8, 3, col);
	bu_line(c, cx + 10, cy - 8, cx + 10, cy + 6, 3, col);
	bu_fill_circle(c, cx - 1, cy + 5, 4, col);
	bu_fill_circle(c, cx + 7, cy + 7, 4, col);
}

void bu_icon_calc(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_stroke_round_rect(c, (bu_rect){ cx - 8, cy - 11, 16, 22 }, 2, 2, col);
	bu_fill_rect(c, (bu_rect){ cx - 5, cy - 8, 10, 4 }, col);
	for (int r = 0; r < 3; r++)
		for (int i = 0; i < 3; i++)
			bu_fill_circle(c, cx - 4 + i * 4, cy - 1 + r * 4, 1, col);
}

void bu_icon_globe(bu_canvas *c, int cx, int cy, bu_color col)
{
	for (int i = 0; i <= 64; i++) {
		double a = 2 * 3.14159265 * i / 64.0;
		int x = cx + (int)(10 * cos(a)), y = cy + (int)(10 * sin(a));
		blend_px(c, x, y, col);
		blend_px(c, x, y + 1, col);
	}
	bu_line(c, cx - 10, cy, cx + 10, cy, 1, col);
	bu_line(c, cx, cy - 10, cx, cy + 10, 1, col);
}

void bu_icon_chip(bu_canvas *c, int cx, int cy, bu_color col)
{
	bu_stroke_round_rect(c, (bu_rect){ cx - 7, cy - 7, 14, 14 }, 2, 2, col);
	for (int i = -1; i <= 1; i++) {
		bu_line(c, cx + i * 5, cy - 11, cx + i * 5, cy - 7, 2, col);
		bu_line(c, cx + i * 5, cy + 7, cx + i * 5, cy + 11, 2, col);
		bu_line(c, cx - 11, cy + i * 5, cx - 7, cy + i * 5, 2, col);
		bu_line(c, cx + 7, cy + i * 5, cx + 11, cy + i * 5, 2, col);
	}
}
