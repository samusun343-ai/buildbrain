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
bu_canvas *bu_canvas_wrap(int w, int h, uint32_t *px);
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
void bu_bevel_rect(bu_canvas *c, bu_rect r, bool raised);	/* classic chrome bevel */
void bu_line(bu_canvas *c, int x0, int y0, int x1, int y1, int width, bu_color col);

/* text */
typedef struct bu_font bu_font;
bu_font *bu_font_load(const char *path);
void bu_font_free(bu_font *f);
int bu_text_width(bu_font *f, const char *utf8, float px);
void bu_draw_text(bu_canvas *c, bu_font *f, int x, int y, const char *utf8, float px, bu_color col);
void bu_draw_text_centered(bu_canvas *c, bu_font *f, bu_rect r, const char *utf8, float px, bu_color col);
void bu_draw_text_right(bu_canvas *c, bu_font *f, bu_rect r, const char *utf8, float px, bu_color col);

/* skin chrome modes (set by bd_skin_apply) */
enum { BU_CHROME_MODERN = 0, BU_CHROME_BEVEL = 1, BU_CHROME_LUNA = 2,
       BU_CHROME_AERO = 3, BU_CHROME_METRO = 4, BU_CHROME_DOCK = 5 };
extern int bu_chrome;

/* theme */
typedef struct {
	bu_color bg, surface, surface2, surface3, line;
	bu_color text, text_dim, text_faint;
	bu_color accent, accent2, danger, ok, warn;
	bu_color panel;		/* panel / title-bar chrome (skins) */
	int radius;
	bu_font *font;
} bu_theme;
extern bu_theme bu_theme_dark;

/* ---------- design tokens ----------
 * The single source of truth for braindesk's unified design
 * language (user design review, 2026-11-20). New UI code uses
 * these names instead of open-coded numbers. Existing widgets
 * adopt a token only where the value already matches, so
 * adopting one never changes a pixel by itself. */

/* spacing: 8 px base-unit scale */
#define BD_SPACE_1 4
#define BD_SPACE_2 8
#define BD_SPACE_3 16
#define BD_SPACE_4 24
#define BD_SPACE_5 32

/* corner radii */
#define BD_RADIUS_PANEL 12	/* panels / launcher sheets */
#define BD_RADIUS_CARD 12	/* bu_card's effective radius (skin radius + 2) */
#define BD_RADIUS_BUTTON 10	/* bu_button's effective radius (skin radius) */

/* icons and app text */
#define BD_ICON_TILE 48		/* app icon display size */
#define BD_TEXT_APP_NAME 12.5f	/* app name label under a tile */

/* controls */
#define BD_SEARCH_H 40		/* search field height */

/* animation: the design band is 150-200 ms. The shell drives
 * frames from its tick; bu_anim_ease() below is the shared
 * easing curve and bu_anim_progress() the tick-side helper. */
#define BD_ANIM_MS 175
float bu_anim_ease(float t);	/* ease-out cubic, t clamped to 0..1 */
float bu_anim_progress(uint32_t elapsed_ms, uint32_t duration_ms);

/* shadows: DY (downward offset) / blur / alpha of the standard
 * drop shadows. bu_shadow_round_rect() builds its falloff from
 * the blur and its effective downward shift is blur / 2, which
 * is what the DY values record. CARD is applied by bu_card()
 * and KNOB by bu_slider(). PANEL is codified for the shell's
 * panels; applying it there is a follow-up, because the shell's
 * panel drawing lives outside libbrainui. */
#define BD_SHADOW_CARD_DY 3
#define BD_SHADOW_CARD_BLUR 6
#define BD_SHADOW_CARD_ALPHA 70
#define BD_SHADOW_PANEL_DY 3
#define BD_SHADOW_PANEL_BLUR 6
#define BD_SHADOW_PANEL_ALPHA 70
#define BD_SHADOW_KNOB_DY 1
#define BD_SHADOW_KNOB_BLUR 3
#define BD_SHADOW_KNOB_ALPHA 90

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
void bu_icon_tasks(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_clock(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_mic(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_box(bu_canvas *c, int cx, int cy, bu_color col);
void bu_icon_keyboard(bu_canvas *c, int cx, int cy, bu_color col);

#endif

/* Draw a PNG asset scaled into the rect; 1 if the file existed. Cached. */
int bu_draw_image(bu_canvas *c, const char *path, int x, int y, int w, int h);
