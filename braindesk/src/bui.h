/* libbrainui - a from-scratch UI toolkit for Brainux.
 *
 * No X11, no GTK, no cairo. libbrainui renders into its own pixel
 * buffer (on the device this is blitted to the LCD framebuffer;
 * the compositor and apps share this one renderer, so the whole
 * desktop has a single visual language and a single font stack).
 *
 * Design constraints it is built for: 854x480 landscape panel,
 * 128 MiB RAM, single-core ARMv5, resistive touch + physical
 * keyboard, battery power. Damage-tracked, double-buffered,
 * glyph-cached text, zero allocations in the steady-state draw
 * path.
 */
#ifndef BUI_H
#define BUI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct { uint8_t r, g, b, a; } bu_color;
#define BU_RGB(r, g, b) ((bu_color){ (r), (g), (b), 255 })
#define BU_RGBA(r, g, b, a) ((bu_color){ (r), (g), (b), (a) })

typedef struct { int x, y, w, h; } bu_rect;

typedef struct {
	int w, h;
	uint32_t *px;		/* RGBA, row-major */
	bu_rect clip;
} bu_canvas;

bu_canvas *bu_canvas_new(int w, int h);
void bu_canvas_free(bu_canvas *c);
void bu_canvas_clear(bu_canvas *c, bu_color col);
bool bu_canvas_write_ppm(bu_canvas *c, const char *path);

/* drawing primitives */
void bu_fill_rect(bu_canvas *c, bu_rect r, bu_color col);
void bu_fill_round_rect(bu_canvas *c, bu_rect r, int radius, bu_color col);
void bu_stroke_round_rect(bu_canvas *c, bu_rect r, int radius, int width, bu_color col);
void bu_gradient_v(bu_canvas *c, bu_rect r, bu_color top, bu_color bottom);
void bu_gradient_round_v(bu_canvas *c, bu_rect r, int radius, bu_color top, bu_color bottom);
void bu_shadow_round_rect(bu_canvas *c, bu_rect r, int radius, int blur, bu_color col);
void bu_fill_circle(bu_canvas *c, int cx, int cy, int radius, bu_color col);
void bu_line(bu_canvas *c, int x0, int y0, int x1, int y1, int width, bu_color col);

/* text */
typedef struct bu_font bu_font;
bu_font *bu_font_load(const char *path);
void bu_font_free(bu_font *f);
int bu_text_width(bu_font *f, const char *utf8, float px);
void bu_draw_text(bu_canvas *c, bu_font *f, int x, int y, const char *utf8, float px, bu_color col);
void bu_draw_text_centered(bu_canvas *c, bu_font *f, bu_rect r, const char *utf8, float px, bu_color col);
void bu_draw_text_right(bu_canvas *c, bu_font *f, bu_rect r, const char *utf8, float px, bu_color col);

/* theme */
typedef struct {
	bu_color bg, surface, surface2, surface3, line;
	bu_color text, text_dim, text_faint;
	bu_color accent, accent2, danger, ok, warn;
	int radius;
	bu_font *font;
} bu_theme;
extern bu_theme bu_theme_dark;

/* widgets (immediate-mode drawing; state lives in the caller) */
typedef enum { BU_BTN_NORMAL, BU_BTN_PRIMARY, BU_BTN_GHOST, BU_BTN_DANGER } bu_btn_style;
void bu_button(bu_canvas *c, bu_theme *t, bu_rect r, const char *label, bu_btn_style style, bool pressed);
void bu_toggle(bu_canvas *c, bu_theme *t, int x, int y, bool on);
void bu_slider(bu_canvas *c, bu_theme *t, bu_rect r, float frac, bool active);
void bu_card(bu_canvas *c, bu_theme *t, bu_rect r);
void bu_list_row(bu_canvas *c, bu_theme *t, bu_rect r, const char *title, const char *value, bool chevron, bool selected);
void bu_text_field(bu_canvas *c, bu_theme *t, bu_rect r, const char *placeholder, const char *masked, bool focused);

/* icons (drawn, not images: zero asset weight, theme-tinted) */
void bu_icon_battery(bu_canvas *c, int x, int y, float level, bool charging, bu_color col);
void bu_icon_volume(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_brightness(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_wifi(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_power(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_gear(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_home(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_search(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_folder(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_terminal(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_book(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_note(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_music(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_calc(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_globe(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_chip(bu_canvas *c, int cx, int cy, bu_color col);

#endif
