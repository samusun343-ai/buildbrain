/* tk - libbrainui2 toolkit core (retained-mode widget tree).
 * See DESIGN-toolkit-v2.md. This header is the public SDK surface;
 * widget internals live in tk_internal.h. */
#ifndef TK_H
#define TK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>   /* FILE for tk_root_validate_dump */
#include "textcore.h"

typedef struct tk_widget tk_widget;
typedef struct tk_root tk_root;
typedef struct tk_canvas tk_canvas;

typedef struct { int x, y, w, h; } tk_rect;
typedef struct { int w, h; } tk_size;
typedef struct { int min_w, max_w, min_h, max_h; } tk_constraints;
typedef struct { uint8_t r, g, b, a; } tk_color;

#define TK_UNLIMITED 100000

/* Invariant assertions (DESIGN-layout-contract.md section 5).
 * Library boundaries state their preconditions with TK_ASSERT;
 * the test builds keep them live, release builds compile them
 * out via NDEBUG (assert.h semantics), so the 128 MB target
 * pays nothing. Before this contract the tree had ZERO asserts:
 * broken preconditions failed silently as drawing corruption. */
#ifndef TK_ASSERT
#include <assert.h>
#define TK_ASSERT(x) assert(x)
#endif

/* Bounded string copy (BSD strlcpy semantics): always
 * NUL-terminates when cap > 0, returns strlen(src). The tree's
 * path handling used raw strcpy/strcat on caller-sized buffers
 * (AUDIT-foundation.md Q-2); new code uses this, and the old
 * call sites are migrated to it. */
size_t tk_strlcpy(char *dst, const char *src, size_t cap);
size_t tk_strlcat(char *dst, const char *src, size_t cap);

/* ------------------------------------------------------------------ */
/* Theme tokens (DESIGN section 7; values re-based by UI-REDESIGN     */
/* section 2.2 on the platform HIGs - see UI-REDESIGN.md)              */
/* ------------------------------------------------------------------ */
typedef struct {
    tk_color bg, surface, surface_alt, desktop_bg, text, text_dim;
    tk_color accent, on_accent, border, border_line, focus, selection,
             danger;
    int unit;          /* base spacing unit, 8 (scale: 4/8/12/16/24) */
    int radius;        /* control corner radius (4) */
    int radius_overlay;/* floating-surface corner radius (8) */
    int control_h;     /* default control height / min touch target */
    int row_h;         /* list row height, one line of text */
    int row_h2;        /* list row height, two lines (main + sub) */
    int font_px;       /* body text size */
    int font_display;  /* display text size (28) */
    int font_title;    /* title text size (20, bold) */
    int font_heading;  /* heading text size (16, bold) */
    int font_caption;  /* caption text size (13; UI text floor is 12) */
    /* UI-STYLE-v2 tokens (One UI / Material You / ChromeOS
     * direction; see UI-STYLE-v2.md section 4). In tk_theme_light
     * these keep the classic behaviour: radius_button -1 falls
     * back to radius, radius_card/list are unused by the classic
     * paints, slider_track_h is 4, field_filled 0, panel_margin 0,
     * style 0. New paints consult them only through the theme, so
     * selecting tk_theme_v2 restyles without code changes. */
    tk_color container;  /* tonal emphasis surface (primaryContainer) */
    int radius_button;   /* button corner radius; -1 = use radius */
    int radius_card;     /* grouped-card / QS tile corner radius */
    int radius_list;     /* list surface radius; 0 = square (classic) */
    int slider_track_h;  /* slider track thickness */
    int field_filled;    /* 1 = filled tonal pill fields, no outline */
    int panel_margin;    /* floating shelf margin from screen edge */
    int style;           /* 0 = classic, 1 = UI-STYLE-v2 (Material 3),
                          * 2 = WinUI / Fluent (M7) */
    /* M7 token completion (audit M7): the M3 inverse roles and
     * surface-container ladder, plus the WinUI/Fluent token set.
     * Appended at the end so existing designated initialisers are
     * unaffected; a zero alpha means "unset" for the inverse and
     * WinUI roles, and paints fall back to the pre-M7 pairing.
     * The WinUI fields hold the Fluent values VERBATIM (including
     * alpha: ControlFillColorDefault is translucent white/black);
     * the canvas blends them over what is beneath, as XAML does. */
    tk_color inverse_surface;    /* M3 inverseSurface / toast+snackbar */
    tk_color inverse_on_surface; /* M3 inverseOnSurface */
    tk_color inverse_primary;    /* M3 inversePrimary (snackbar action) */
    tk_color container_low;      /* M3 surfaceContainerLow / WinUI
                                  * SolidBackgroundFillColorTertiary */
    tk_color container_high;     /* M3 surfaceContainerHigh */
    tk_color container_highest;  /* M3 surfaceContainerHighest */
    tk_color control_fill;       /* WinUI ControlFillColorDefault */
    tk_color control_stroke;     /* WinUI ControlStrokeColorDefault */
    tk_color card_bg;            /* WinUI CardBackgroundFillColorDefault */
    tk_color layer;              /* WinUI LayerFillColorDefault */
    tk_color smoke;              /* WinUI SmokeFillColorDefault (scrim) */
    tk_color subtle;             /* WinUI SubtleFillColorSecondary */
    tk_color sys_ok, sys_ok_bg;      /* WinUI SystemFill Success(+Bg) */
    tk_color sys_warn, sys_warn_bg;  /* WinUI SystemFill Caution(+Bg) */
    tk_color sys_err_bg;             /* WinUI SystemFill CriticalBg
                                      * (danger itself is sys Critical) */
    int dark;                /* 1 = dark scheme (M3 dark / WinUI dark) */
    /* M9 additions (appended; zero alpha / zero value = unset,
     * paints fall back to the pre-M9 role): */
    tk_color container_mid;  /* M3 surfaceContainer (the ladder's
                              * middle step; the top app bar's
                              * scrolled container colour) */
    int appbar_h;            /* top app bar height; 0 = control_h
                              * (M3 small 64dp / WinUI 48epx at the
                              * device scale in the spec themes) */
    /* M10 additions (appended; zero alpha = unset, paints fall
     * back to the pre-M10 role): the WinUI state fills, strong
     * strokes and the InfoBar Attention pair, whose values the
     * earlier lines could not obtain. Source: Microsoft's
     * Fluent token table (Microsoft Learn, "Visual Studio
     * theme color tokens", fetched 2026-11-20 - the values
     * listed there are the Windows 11 Fluent defaults). */
    tk_color control_fill2;    /* ControlFillColorSecondary */
    tk_color control_fill3;    /* ControlFillColorTertiary */
    tk_color control_fill_dis; /* ControlFillColorDisabled */
    tk_color text_dis;         /* TextFillColorDisabled */
    tk_color on_accent_dis;    /* TextOnAccentFillColorDisabled */
    tk_color strong_stroke;    /* ControlStrongStrokeColorDefault */
    tk_color strong_stroke_dis;/* ControlStrongStrokeColorDisabled */
    tk_color strong_fill;      /* ControlStrongFillColorDefault */
    tk_color strong_fill_dis;  /* ControlStrongFillColorDisabled */
    tk_color accent_dis;       /* AccentFillColorDisabled */
    tk_color sys_attn;         /* SystemFillColorAttention */
    tk_color sys_attn_bg;      /* SystemFillColorAttentionBackground */
    /* M13 additions (appended; zero alpha = unset, paints fall
     * back to the pre-M13 role): the M3 on-container content
     * roles - onPrimaryContainer pairs with the primaryContainer
     * face (the FAB's default container), onSecondaryContainer
     * pairs with the secondaryContainer indicator. */
    tk_color on_container;     /* M3 onPrimaryContainer */
    tk_color on_selection;     /* M3 onSecondaryContainer */
    /* Foundation contract (DESIGN-layout-contract.md section
     * 1.5): root heights below compact_below ask apps to switch
     * to the compact derivation (tk_theme_compact). 0 = unset,
     * the helper substitutes the default 420. */
    int compact_below;
} tk_theme;

extern const tk_theme tk_theme_light;
extern const tk_theme tk_theme_v2;
/* M7 themes: the M3 baseline DARK scheme (the v2 token set with
 * the dark colour roles) and the WinUI/Fluent token sets (style
 * 2) in light and dark. Mode switching is a theme switch: an app
 * built once renders in the Android-equivalent mode under
 * tk_theme_v2 / tk_theme_v2_dark and in the Windows-equivalent
 * mode under tk_theme_winui / tk_theme_winui_dark, with no code
 * changes (see REPORT-toolkit-complete.md for the token mapping
 * and the device-fit dimension conversions). */
extern const tk_theme tk_theme_v2_dark;
extern const tk_theme tk_theme_winui;
extern const tk_theme tk_theme_winui_dark;
const tk_theme *tk_theme_default(void);
/* Override the process-wide default theme (NULL restores
 * tk_theme_light). Products keep the classic default; the v2
 * pilot harness selects tk_theme_v2 through this. Widgets read
 * the default at paint/measure time, and the shell reads it at
 * construction time, so set it before building any UI. */
void tk_theme_set_default(const tk_theme *t);
/* M5 density: derive a theme with roomier row/control heights
 * from a base theme (level 0 = base, 1 = x5/4, 2 = x3/2; clamped).
 * The spacing unit and font sizes are not scaled. Typical use:
 *   static tk_theme roomy; roomy = tk_theme_density(&tk_theme_v2, 1);
 *   tk_theme_set_default(&roomy); */
tk_theme tk_theme_density(const tk_theme *base, int level);
/* The compact derivation (DESIGN-layout-contract.md section
 * 1.5), the counterpart of tk_theme_density: for windows whose
 * height is below the theme's compact_below token, fixed chrome
 * costs shrink FIRST - two-line list rows take the one-line
 * height (row_h2 -> row_h) - while touch targets (control_h)
 * never shrink. Returns a copy; install it with
 * tk_theme_set_default() as with the density derivation. */
tk_theme tk_theme_compact(const tk_theme *base);
/* The root height below which an app should use the compact
 * derivation (theme token compact_below, default 420). */
int tk_theme_compact_below(const tk_theme *t);

/* Typography levels (UI-REDESIGN 2.2). Widgets carry one style
 * (default TK_TEXT_BODY) and measure/draw their text with it, so
 * sizes are never hard-coded at call sites. Bold styles (title,
 * heading) are rendered with a synthetic-bold overdraw - the
 * bundled font ships a single weight - which does not change
 * glyph advances, so layout is identical to the regular weight. */
typedef enum {
    TK_TEXT_BODY = 0, TK_TEXT_DISPLAY, TK_TEXT_TITLE,
    TK_TEXT_HEADING, TK_TEXT_CAPTION
} tk_text_style;
int tk_theme_style_px(const tk_theme *t, tk_text_style s);
bool tk_theme_style_bold(tk_text_style s);
void tk_widget_set_text_style(tk_widget *w, tk_text_style s);
tk_text_style tk_widget_text_style(const tk_widget *w);
/* Style-parameterised text metrics / drawing (the plain
 * tk_text_width / tk_line_height / tk_canvas_draw_text below are
 * the BODY-style forms kept for editors and existing callers). */
int tk_text_width_st(const tk_theme *t, tk_text_style s, const char *utf8);
int tk_line_height_st(const tk_theme *t, tk_text_style s);
void tk_canvas_draw_text_st(tk_canvas *c, const tk_theme *t,
                            tk_text_style s, int x, int y,
                            const char *utf8, tk_color col);
/* Pixel-size forms, for callers that scale a style themselves
 * (e.g. the OSK symbol page). bold = synthetic-bold overdraw. */
int tk_text_width_px(const tk_theme *t, int px, const char *utf8);
int tk_line_height_px(const tk_theme *t, int px);
void tk_canvas_draw_text_px(tk_canvas *c, const tk_theme *t, int px,
                            bool bold, int x, int y,
                            const char *utf8, tk_color col);

/* ------------------------------------------------------------------ */
/* Canvas (memory backend; fbdev backend flushes the same pixels)      */
/* ------------------------------------------------------------------ */
tk_canvas *tk_canvas_new(int w, int h);
/* Non-owning canvas over caller pixels (RGBA8888, row-major, no
 * stride padding). tk_canvas_free() releases only the canvas
 * struct, never the pixel buffer. This is the zero-copy path for
 * drawing straight into shared memory (e.g. a WM client buffer);
 * added for shell M2 at the request recorded in
 * DESIGN-shell-v2.md section 8. */
tk_canvas *tk_canvas_wrap(int w, int h, uint32_t *px);
void tk_canvas_free(tk_canvas *c);
int tk_canvas_width(const tk_canvas *c);
int tk_canvas_height(const tk_canvas *c);
uint32_t *tk_canvas_pixels(tk_canvas *c); /* RGBA8888, row-major */
void tk_canvas_clear(tk_canvas *c, tk_color col);
void tk_canvas_fill_rect(tk_canvas *c, tk_rect r, tk_color col);
void tk_canvas_stroke_rect(tk_canvas *c, tk_rect r, int width, tk_color col);
void tk_canvas_fill_round_rect(tk_canvas *c, tk_rect r, int radius, tk_color col);
/* M13: per-corner radii (top-left, top-right, bottom-right,
 * bottom-left) - the split button's inner corners are smaller
 * than its outer corners in the M3 spec. */
void tk_canvas_fill_round_rect4(tk_canvas *c, tk_rect r,
                                int rtl, int rtr, int rbr, int rbl,
                                tk_color col);
/* Rounded-rect outline (UI-STYLE-v2): strokes the inside of r
 * following the same pixel-center corner geometry as the fill,
 * so large radii (pills, sheets) get a border that actually
 * follows the corner instead of a square frame poking out. */
void tk_canvas_stroke_round_rect(tk_canvas *c, tk_rect r, int radius,
                                 int width, tk_color col);
void tk_canvas_push_clip(tk_canvas *c, tk_rect r);
void tk_canvas_pop_clip(tk_canvas *c);
/* Rotated image blit (M8): draw the iw*ih ARGB image (0xAARRGGBB
 * words, the blit_image format) rotated by angle_deg - clockwise
 * on screen - about the centre of dst, where dst is the rect the
 * unrotated image would occupy. Inverse-mapped, nearest sample;
 * opaque source pixels are stored, translucent ones blended,
 * exactly like the unrotated blit. No allocation. Added for the
 * split button's chevron, which the M3 guidelines require to
 * rotate 180 degrees as its menu opens and closes. */
void tk_canvas_draw_image_rot(tk_canvas *c, const uint32_t *px,
                              int iw, int ih, tk_rect dst,
                              float angle_deg);
/* Scaled blit (M20): draw src_rect of the source canvas into
 * dst_rect of c with bilinear sampling (pixel-centre mapping,
 * edge clamping). Source pixels are stored as-is (the source
 * is a fully composited, opaque surface); the write honours
 * c's clip stack and records damage for the clipped dst_rect
 * once. No allocation. This is the transfer step of the
 * scaled-overlay paint path (menu entrance/exit scale). */
void tk_canvas_blit_scaled(tk_canvas *c, const tk_canvas *src,
                           tk_rect src_rect, tk_rect dst_rect);
tk_color tk_canvas_pixel(const tk_canvas *c, int x, int y); /* test/debug */
/* Damage accumulation (M3): every canvas write records the area it
 * touched. Rect-level ops record their clipped rect; pixel ops
 * coalesce into neighbouring rects. The list merges overlapping or
 * abutting rects and collapses to one bounding box past 64 rects.
 * tk_canvas_clear() damages the whole canvas, and a new canvas
 * starts fully damaged. tk_fb_flush_damage() transfers only these
 * areas to the framebuffer (see the fb backend section). */
void tk_canvas_invalidate(tk_canvas *c, tk_rect r);
size_t tk_canvas_damage_count(const tk_canvas *c);
tk_rect tk_canvas_damage_rect(const tk_canvas *c, size_t i);
tk_rect tk_canvas_damage_bounds(const tk_canvas *c); /* empty if none */
void tk_canvas_damage_clear(tk_canvas *c);
/* Text metrics and rendering (M2): when a font is installed with
 * tk_font_install(), widths are real glyph advances from that font
 * and tk_canvas_draw_text rasterizes glyphs (with a glyph cache).
 * With no font installed, a monospace approximation is used
 * (ASCII=font_px/2, wide=font_px) and draw_text paints nothing;
 * that fallback exists for headless tests, not as a product path. */
int tk_text_width(const tk_theme *t, const char *utf8);
int tk_text_cp_width(const tk_theme *t, uint32_t codepoint);
int tk_line_height(const tk_theme *t);
void tk_canvas_draw_text(tk_canvas *c, const tk_theme *t, int x, int y,
                         const char *utf8, tk_color col);

/* ------------------------------------------------------------------ */
/* Fonts (M2, tk_font.c; FreeType + HarfBuzz backend since M24)       */
/*                                                                     */
/* Loading policy (revised in M3, search order revised in M23):     */
/* the library bundles ONE subset Japanese font as data               */
/* (fonts/brainui-jp-subset.ttf, built from Noto Sans CJK JP under    */
/* the SIL OFL; installed to share/fonts/libbrainui2/).               */
/* tk_font_load_bundled() finds it via $BRAINUI_FONT_PATH, then       */
/* locations relative to the executable and to the library itself,    */
/* then the installed share dirs; a CWD-relative ./fonts/ is only a    */
/* last-resort development fallback (pre-M23 it came first, so a       */
/* product launched from an arbitrary directory lost its font).       */
/* tk_font_load_default() tries $BRAINUI_FONT_PATH, then the bundled  */
/* subset, then host Noto Sans CJK, DejaVu Sans and Liberation Sans.  */
/* The app may still pass an explicit path to tk_font_load (a          */
/* .ttf/.otf, or a .ttc with a face index). Fallback: fonts form a    */
/* chain via tk_font_set_fallback - a codepoint missing from the      */
/* primary font is measured/rasterized from the fallback; missing     */
/* everywhere, the font's .notdef glyph is used.                      */
/* ------------------------------------------------------------------ */
typedef struct tk_font tk_font;
tk_font *tk_font_load(const char *path, int face_index);
tk_font *tk_font_load_bundled(void);
tk_font *tk_font_load_default(void);
void tk_font_free(tk_font *f);
void tk_font_set_fallback(tk_font *primary, tk_font *fallback);
bool tk_font_has_glyph(const tk_font *f, uint32_t codepoint); /* chain-aware */
int tk_font_cp_advance(const tk_font *f, int px, uint32_t codepoint);
/* Install as the toolkit-wide current font (NULL uninstalls). */
void tk_font_install(tk_font *f);
tk_font *tk_font_current(void);
/* Number of glyphs currently in the raster cache (diagnostics). */
size_t tk_font_cache_count(const tk_font *f);

/* Line wrapping with Japanese kinsoku rules (no line may start with
 * a closing/prohibited char, no line may end with an opening one),
 * word wrap at spaces, and hard breaks as a last resort. Operates on
 * one logical line (no '\n' inside). cb receives each visual line as
 * a byte range of `utf8`. Returns the visual line count. Widths come
 * from the installed font, or the approximation without one. */
typedef void (*tk_wrap_cb)(void *userdata, size_t start, size_t len);
size_t tk_text_wrap(const tk_theme *t, const char *utf8, int max_w_px,
                    tk_wrap_cb cb, void *userdata);

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    TK_EV_POINTER_DOWN, TK_EV_POINTER_UP, TK_EV_POINTER_MOVE, TK_EV_WHEEL,
    TK_EV_KEY_DOWN, TK_EV_KEY_UP,
    TK_EV_FOCUS_IN, TK_EV_FOCUS_OUT,
    TK_EV_TEXT_INPUT,      /* committed UTF-8 text */
    TK_EV_COMPOSITION,     /* IME preedit update; text may be NULL=clear */
    TK_EV_ACTION,          /* shell-level action key (tk_action); M2 kbd */
    /* Pointer long-press (audit M6): delivered once by
     * tk_root_tick() to the widget that took the pointer down
     * when the press has been held for TK_LONGPRESS_MS without
     * moving past the slop radius. The press's click is then
     * suppressed (the UP fires no click callback), matching the
     * Android behaviour where a long-press consumes the gesture.
     * Widgets that do not handle it let it bubble as usual. */
    TK_EV_LONGPRESS
} tk_event_type;
#define TK_LONGPRESS_MS 600
#define TK_LONGPRESS_SLOP 10

/* Shell-level actions carried by TK_EV_ACTION. These come from
 * dedicated hardware keys (Brain: home / search / volume / brightness
 * / power ...) mapped by the keyboard layer (tk_kbd.h). Editors and
 * text widgets ignore them; the shell (or the embedding app) handles
 * them. Function keys map to TK_ACTION_FKEY_BASE + n (n = 1..24). */
typedef enum {
    TK_ACTION_NONE = 0,
    TK_ACTION_HOME, TK_ACTION_SEARCH, TK_ACTION_BACK, TK_ACTION_FORWARD,
    TK_ACTION_MENU, TK_ACTION_AUDIO,
    TK_ACTION_VOLUME_UP, TK_ACTION_VOLUME_DOWN, TK_ACTION_MUTE,
    TK_ACTION_BRIGHTNESS_UP, TK_ACTION_BRIGHTNESS_DOWN,
    TK_ACTION_POWER, TK_ACTION_SLEEP,
    TK_ACTION_IME_TOGGLE, TK_ACTION_CONVERT, TK_ACTION_NO_CONVERT,
    TK_ACTION_KANA_TOGGLE,
    TK_ACTION_FKEY_BASE = 64
} tk_action;

typedef enum {
    TK_KEY_NONE = 0, TK_KEY_TAB, TK_KEY_ENTER, TK_KEY_ESCAPE,
    TK_KEY_BACKSPACE, TK_KEY_DELETE, TK_KEY_LEFT, TK_KEY_RIGHT,
    TK_KEY_UP, TK_KEY_DOWN, TK_KEY_HOME, TK_KEY_END,
    TK_KEY_PAGE_UP, TK_KEY_PAGE_DOWN, TK_KEY_SPACE, TK_KEY_INSERT,
    TK_KEY_BACK   /* appended: the Brain back key (evdev KEY_BACK) */
} tk_key;

typedef struct {
    tk_event_type type;
    int x, y;              /* pointer coords (root space) */
    int wheel_dy;          /* wheel delta rows */
    tk_key key;
    uint32_t modifiers;    /* TK_MOD_* */
    const char *text;      /* TEXT_INPUT / COMPOSITION / printable KEY */
    bool handled;
    /* Keyboard layer (M2, tk_kbd.h): TK_EV_ACTION carries a
     * tk_action in `action`. KEY_DOWN events produced by autorepeat
     * (kernel evdev repeat or the synthetic generator - never both)
     * have repeat=true; a one-shot KEY_DOWN with longpress=true is
     * delivered once when a key has been held for the configured
     * long-press time (the original press event is unaffected). */
    int action;            /* TK_EV_ACTION: tk_action value */
    bool repeat;
    bool longpress;
    /* COMPOSITION segment info (M2): when seg_count > 0, `text` is
     * divided into seg_count segments whose byte lengths are
     * seg_lens[0..seg_count) (borrowed; valid only during dispatch -
     * receivers copy what they need synchronously). seg_active is
     * the segment under conversion; seg_highlight marks the active
     * segment as candidate-browsing (rendered selected). */
    const size_t *seg_lens;
    int seg_count;
    int seg_active;
    bool seg_highlight;
} tk_event;

#define TK_MOD_SHIFT 1u
#define TK_MOD_CTRL  2u
#define TK_MOD_ALT   4u

/* ------------------------------------------------------------------ */
/* Widget base                                                         */
/* ------------------------------------------------------------------ */
typedef enum {
    TK_W_ROOT, TK_W_BOX, TK_W_FIXED, TK_W_LABEL, TK_W_BUTTON,
    TK_W_CHECKBOX, TK_W_RADIO, TK_W_SLIDER, TK_W_TEXTFIELD,
    TK_W_SCROLLVIEW, TK_W_LISTVIEW,
    /* M2 additions */
    TK_W_SWITCH, TK_W_PROGRESS, TK_W_TEXTVIEW, TK_W_TABS, TK_W_MENU,
    TK_W_MENUBAR, TK_W_DIALOG, TK_W_COMBOBOX, TK_W_SPINBUTTON,
    TK_W_SEPARATOR, TK_W_IMAGEVIEW, TK_W_TREEVIEW, TK_W_GRID,
    /* UI-redesign foundation: toolbar that sheds overflow into a
     * menu instead of clipping (UI-REDESIGN P1-4) */
    TK_W_OVERFLOWBAR,
    /* OSK v2 (input integration): on-screen keyboard widget.
     * Its public API lives in osk.h, not here, so the IME-facing
     * surface can evolve without touching every tk consumer. */
    TK_W_OSK,
    /* Event sink: invisible, zero-size widget that forwards the
     * events an IME delivers to its target (TEXT_INPUT,
     * COMPOSITION, KEY_DOWN, KEY_UP) to a callback. It exists so
     * an OSK can be attached to a target whose owner re-routes
     * the output elsewhere - the shell forwards commits to the
     * focused WM window instead of a local editor. */
    TK_W_SINK,
    /* M5 additions: segmented date/time pickers (leaf widgets)
     * and the file-dialog kind is TK_W_DIALOG (composed). */
    TK_W_DATEPICKER, TK_W_TIMEPICKER,
    /* Audit M6 additions (Material component parity pass):
     * Chip (assist/filter/input/suggestion forms), Badge (dot /
     * count pill), TabBar (equal-width selector with the M3
     * active indicator - distinct from TK_W_TABS, which owns
     * pages), Spinner (circular indeterminate progress). */
    TK_W_CHIP, TK_W_BADGE, TK_W_TABBAR, TK_W_SPINNER,
    /* Audit M7 additions (full completion pass): Card (first-
     * class container kind), SearchBar (M3 search bar + docked
     * suggestion view), Segmented (M3 segmented buttons), Carousel,
     * NavRail (M3 navigation rail / WinUI NavigationView compact),
     * Ring (circular determinate progress), AppBar (M3 app bar /
     * WinUI CommandBar container), Drawer (M3 navigation drawer /
     * NavigationView pane overlay), Sheet (M3 bottom sheet; edge
     * 1 = side sheet). Kinds >= TK_W_CARD are dispatched to
     * src/tk_m7.c except where tk_core.c gives them container
     * layout (CARD / APPBAR / SHEET). */
    TK_W_CARD, TK_W_SEARCHBAR, TK_W_SEGMENTED, TK_W_CAROUSEL,
    TK_W_NAVRAIL, TK_W_RING, TK_W_APPBAR, TK_W_DRAWER, TK_W_SHEET
} tk_widget_kind;

typedef void (*tk_notify_cb)(tk_widget *w, void *userdata);

/* Create/destroy. Widgets are owned by their parent once added;
 * removing a widget detaches it (caller owns it again). */
tk_widget *tk_widget_new(tk_widget_kind kind);
void tk_widget_free(tk_widget *w);           /* frees subtree if detached */
tk_widget_kind tk_widget_kind_of(const tk_widget *w);
void tk_add_child(tk_widget *parent, tk_widget *child);
void tk_remove_child(tk_widget *parent, tk_widget *child);
tk_widget *tk_parent(const tk_widget *w);
size_t tk_child_count(const tk_widget *w);
tk_widget *tk_child_at(const tk_widget *w, size_t i);
/* Event sink (TK_W_SINK): the callback receives TEXT_INPUT,
 * COMPOSITION, KEY_DOWN and KEY_UP events dispatched to the sink
 * (typically by an OSK attached to it) and returns whether the
 * event was consumed. The sink never takes focus and paints
 * nothing. */
typedef bool (*tk_event_cb)(void *userdata, tk_event *ev);
tk_widget *tk_sink_new(tk_event_cb cb, void *userdata);
/* Generic event hook (audit M6): invoked for every event
 * dispatched to this widget, before the widget's own handler;
 * returning true consumes the event. This is the delivery path
 * for TK_EV_LONGPRESS (which has no dedicated setter) and for
 * app-level gestures on composite widgets. One hook per widget;
 * NULL clears it. The hook must not free the widget. */
void tk_widget_set_event_hook(tk_widget *w, tk_event_cb cb, void *ud);
void tk_set_visible(tk_widget *w, bool v);
bool tk_is_visible(const tk_widget *w);
void tk_set_enabled(tk_widget *w, bool e);
bool tk_is_enabled(const tk_widget *w);
void tk_set_can_focus(tk_widget *w, bool f);
bool tk_can_focus(const tk_widget *w);
/* Tooltip text (M3): the root shows it after the pointer rests on
 * the widget for TK_TOOLTIP_DELAY_MS of tk_root_tick() time.
 * The string is copied. NULL clears the tooltip. */
#define TK_TOOLTIP_DELAY_MS 800
void tk_widget_set_tooltip(tk_widget *w, const char *text);
const char *tk_widget_tooltip(const tk_widget *w);
/* Request a repaint of this widget's arranged rect (M3): records
 * the rect in the damage store of the root this widget's tree is
 * attached to. No-op for a detached tree (nothing to repaint into).
 * Painting itself happens in tk_root_paint_damage(). */
void tk_widget_invalidate(tk_widget *w);
/* flex weight inside a Box parent (0 = natural size) */
void tk_set_flex(tk_widget *w, int flex);
/* Floor for the measured size (0 = no floor). Used for fixed-
 * width regions like sidebars (UI-REDESIGN: files/settings
 * sidebar 176) whose content measure would otherwise wander. */
void tk_widget_set_min_size(tk_widget *w, int min_w, int min_h);
int tk_get_flex(const tk_widget *w);
/* explicit rect inside a Fixed parent */
void tk_set_fixed_rect(tk_widget *w, tk_rect r);

/* Two-pass layout + paint + dispatch. Normally driven via tk_root. */
tk_size tk_measure(tk_widget *w, tk_constraints c);
void tk_arrange(tk_widget *w, tk_rect r);
tk_rect tk_widget_rect(const tk_widget *w);  /* arranged rect, root space */
void tk_paint(tk_widget *w, tk_canvas *c, const tk_theme *t);
bool tk_dispatch(tk_widget *w, tk_event *ev); /* returns handled */
/* Deepest widget at root-space point, or NULL. */
tk_widget *tk_hit_test(tk_widget *w, int x, int y);

/* ------------------------------------------------------------------ */
/* Root: holds focus, pointer capture, and the top widget.             */
/* Ownership (made explicit by the foundation audit,                   */
/* AUDIT-foundation.md Q-3: the rule existed only implicitly           */
/* and the test suites guessed wrong in five places): the root         */
/* does NOT own the top widget tree or the overlays -                 */
/* tk_root_free releases only root-internal state. The caller          */
/* frees the tree with tk_widget_free, after tk_root_free.             */
/* ------------------------------------------------------------------ */
tk_root *tk_root_new(tk_widget *top);
void tk_root_free(tk_root *r);
tk_widget *tk_root_top(const tk_root *r);
void tk_root_layout(tk_root *r, int w, int h);
/* Layout validator (DESIGN-layout-contract.md section 5.2):
 * walks the laid-out tree and counts contract violations -
 * negative sizes anywhere, and (outside scroll containers and
 * explicitly-positioned FIXED children) a child rect escaping
 * its parent's rect. 0 means the layout honours the contract
 * at the current root size. Tests call this at every size of
 * the layout matrix; apps may call it in debug builds. */
int tk_root_validate(const tk_root *r);
/* Human-readable dump of the violations tk_root_validate()
 * counts (one line per violation: kind, rect, parent rect). */
void tk_root_validate_dump(const tk_root *r, FILE *out);
void tk_root_paint(tk_root *r, tk_canvas *c, const tk_theme *t);
/* Damage-driven repaint (M3): layout, handled events, focus moves,
 * overlay open/close and explicit invalidation accumulate damage
 * rects in the root. tk_root_paint_damage() repaints only those
 * areas (background + clipped tree/overlay paint) and returns how
 * many rects it painted (0 = nothing to do). Pair it with
 * tk_fb_flush_damage(): repaint cost is bounded by the damage, and
 * so is the framebuffer transfer. tk_root_paint() above remains the
 * unconditional full repaint. Note the paint walk itself is not
 * culled yet: widgets outside the damage are visited but their
 * canvas writes are clipped away. */
void tk_root_invalidate_rect(tk_root *r, tk_rect rect);
void tk_root_invalidate(tk_root *r, tk_widget *w); /* a widget's rect */
/* Narrow the damage the CURRENT event will produce. Without a
 * hint, a handled event damages its target's full rect - right
 * for simple widgets, wasteful for composite ones (a keyboard,
 * a list, a canvas) whose visual change is much smaller than
 * their rect. A handler calls this during dispatch with a rect
 * that actually changed; up to three rects are kept (further
 * calls are ignored), each clipped to the target's rect by the
 * core. No hint = full rect. */
void tk_root_damage_hint(tk_root *r, tk_rect rect);
size_t tk_root_damage_count(const tk_root *r);
int tk_root_paint_damage(tk_root *r, tk_canvas *c, const tk_theme *t);
/* Advance the root's clock (drives tooltip timing and the M5
 * animation engine). now_ms is a monotonic millisecond counter
 * supplied by the app's main loop; call it once per loop
 * iteration (or whenever time passes). */
void tk_root_tick(tk_root *r, uint32_t now_ms);
/* Repaint ONLY the overlay stack (menus, dialogs, floating
 * widgets) into the canvas, without the tree pass (browser SS6-2:
 * an app presenting through its own surface needs the overlay
 * layer separately). The caller clears/composites; overlays are
 * painted in stack order, followed by the focus ring and any
 * toast/snackbar. */
void tk_root_paint_overlays(tk_root *r, tk_canvas *c, const tk_theme *t);
/* Generic floating placement (audit M6; apps-envfit SS6-3):
 * attach any widget as a non-modal overlay anchored to a root
 * corner with margins - the FAB pattern, but open to any
 * floating widget. The widget is measured at its desired size,
 * positioned by the anchor at every tk_root_layout(), painted
 * above the tree, and hit-tested before it. It stays attached
 * until tk_root_remove_floating(). Anchors: 0 top-left,
 * 1 top-right, 2 bottom-left, 3 bottom-right. */
void tk_root_add_floating(tk_root *r, tk_widget *w, int anchor,
                          int margin_x, int margin_y);
void tk_root_remove_floating(tk_root *r, tk_widget *w);
bool tk_root_is_floating(const tk_root *r, const tk_widget *w);
/* Toast (audit M6): a transient, non-interactive message pill
 * centred above the bottom edge. duration_ms <= 0 uses the
 * default 4000ms (the Material short duration). Showing a new
 * toast replaces the current one. Expiry is driven by
 * tk_root_tick(); the covered area is damaged on show and on
 * expiry so damage-driven presenters pick it up. */
void tk_toast_show(tk_root *r, const char *text, int duration_ms);
void tk_toast_hide(tk_root *r);
bool tk_toast_visible(const tk_root *r);
/* Snackbar (audit M6): the M3 bottom message bar with ONE
 * optional action. duration_ms <= 0 uses 4000ms; pass
 * TK_SNACKBAR_INDEFINITE for no auto-dismiss. M16 contract:
 * a bar WITH an action never auto-dismisses, whatever
 * duration was requested - it stays until the action is
 * taken, the bar is tapped/swiped away, a new bar replaces
 * it, or tk_snackbar_hide is called (Android Developers:
 * snackbars with an action "should not timeout or
 * self-dismiss"; the WinUI InfoBar likewise remains until
 * closed by the user). Showing replaces any current
 * snackbar. Tapping the action fires cb (with the
 * snackbar's internal widget as w) and dismisses; tapping
 * elsewhere on the bar dismisses without firing. */
#define TK_SNACKBAR_INDEFINITE (-1)
void tk_snackbar_show(tk_root *r, const char *text,
                      const char *action_label,
                      tk_notify_cb cb, void *ud, int duration_ms);
/* M9: severity form. In the WinUI mode the snackbar is the
 * InfoBar, whose severity selects the SystemFill background
 * and the icon (InfoBar themeresources: severity background =
 * SystemFillColor{Attention,Caution,Success,Critical}-
 * Background, icon on the matching solid fill). The M3 form
 * has no severity concept and is unchanged. */
typedef enum {
    TK_SNACK_INFO = 0, TK_SNACK_SUCCESS, TK_SNACK_WARNING,
    TK_SNACK_ERROR
} tk_snack_severity;
void tk_snackbar_show_sev(tk_root *r, const char *text,
                          tk_snack_severity sev,
                          const char *action_label,
                          tk_notify_cb cb, void *ud,
                          int duration_ms);
void tk_snackbar_hide(tk_root *r);
bool tk_snackbar_visible(const tk_root *r);
/* Scroll-blit opt-in (M5): when enabled, a ScrollView whose
 * content is unchanged and whose painted state is known shifts
 * the retained pixels instead of repainting the whole viewport,
 * and the fbdev flush replays the shift in device memory (only
 * the exposed band is converted). Enable it only where the
 * canvas IS the presented surface (direct fbdev frontends). A
 * client that presents through a separate compositor buffer
 * keyed by damage rects (the WM protocol) must leave it off
 * until the protocol carries shifts. Default: off. */
void tk_root_set_scroll_blit(tk_root *r, bool on);
bool tk_root_scroll_blit(const tk_root *r);

/* ------------------------------------------------------------------ */
/* Animation (M5, tk_anim.c)                                           */
/*                                                                     */
/* One integer value, eased over wall-clock time, delivered       */
/* through a callback. Driven by tk_root_tick(); a coarse main     */
/* loop degrades to fewer intermediate frames over the same        */
/* duration, never to a longer animation. While anything is        */
/* running, tk_root_anim_active() lets the main loop shorten its   */
/* wait (target ~30 fps on device; each frame repaints only the    */
/* swept damage). Disabling animations (the settings kill switch   */
/* UI-REDESIGN requires) makes tk_anim_start() deliver the final   */
/* value synchronously and return NULL. In-toolkit users (M12):    */
/* the Switch knob slides on toggle (M3 200 ms Standard / WinUI    */
/* 167 ms); Dialogs and Menus slide into place on open and         */
/* Dialogs slide out on close; the Drawer and the Sheet slide      */
/* in and out; the Snackbar slides up on show (M3 form); the       */
/* Split chevron rotates; the indeterminate Progress sweep is      */
/* phase-driven from the same clock. Durations and curves are      */
/* the reference frameworks' token values per mode.                */
/* ------------------------------------------------------------------ */
typedef float (*tk_ease_fn)(float t);   /* t in [0,1] -> [0,1] */
float tk_ease_linear(float t);
float tk_ease_out_cubic(float t);
float tk_ease_in_out_cubic(float t);
/* M12: the reference frameworks' named curves, evaluated as
 * true cubic beziers (control points in the comments; values
 * fetched from the primary sources 2026-11-20, see
 * REPORT-toolkit-m12.md SS0):
 * - tk_ease_standard:              M3 Standard, cubic-bezier(0.2, 0, 0, 1)
 * - tk_ease_standard_decelerate:   M3 Standard decelerate, cubic-bezier(0, 0, 0, 1);
 *                                  the same curve is WinUI's "Direct Entrance" easing
 * - tk_ease_emphasized_decelerate: M3 Emphasized decelerate, cubic-bezier(0.05, 0.7, 0.1, 1) */
float tk_ease_standard(float t);
float tk_ease_standard_decelerate(float t);
float tk_ease_emphasized_decelerate(float t);
/* M18: AOSP framework interpolators (decelerate_quad /
 * accelerate_quad); the toast fade pair in the M3 mode. */
float tk_ease_decelerate_quad(float t);
float tk_ease_accelerate_quad(float t);
typedef struct tk_anim tk_anim;
typedef void (*tk_anim_cb)(void *ud, int value);
typedef void (*tk_anim_done_cb)(void *ud);
#define TK_ANIM_DURATION_MS 160
/* Start an animation on root r. The handle is owned by the root
 * and stays valid until completion, tk_anim_cancel(), or root
 * free. `ud` is borrowed (the caller keeps it alive). Returns
 * NULL when the final value was delivered synchronously
 * (animations disabled, zero duration, or from == to). */
tk_anim *tk_anim_start(tk_root *r, int from, int to, uint32_t dur_ms,
                       tk_ease_fn ease, tk_anim_cb cb,
                       tk_anim_done_cb done, void *ud);
void tk_anim_cancel(tk_anim *a);
bool tk_root_anim_active(const tk_root *r);
void tk_root_set_animations_enabled(tk_root *r, bool on);
bool tk_root_animations_enabled(const tk_root *r);
bool tk_root_tooltip_visible(const tk_root *r);
tk_rect tk_root_tooltip_rect(const tk_root *r);
bool tk_root_event(tk_root *r, tk_event *ev);
tk_widget *tk_focused(const tk_root *r);
bool tk_focus(tk_root *r, tk_widget *w);   /* false if not focusable */
bool tk_focus_next(tk_root *r);
bool tk_focus_prev(tk_root *r);

/* ------------------------------------------------------------------ */
/* Containers                                                          */
/* ------------------------------------------------------------------ */
typedef enum { TK_DIR_VERTICAL, TK_DIR_HORIZONTAL } tk_dir;
typedef enum { TK_ALIGN_START, TK_ALIGN_CENTER, TK_ALIGN_END, TK_ALIGN_STRETCH } tk_align;
tk_widget *tk_box_new(tk_dir dir, int spacing, int padding);
void tk_box_set_align(tk_widget *box, tk_align cross_align);
tk_widget *tk_fixed_new(void);

/* OverflowBar: a horizontal bar (children added with tk_add_child,
 * normally Buttons). At arrange time the children are laid out
 * left to right at their desired widths; the ones that do not fit
 * are hidden and a "»" button appears at the right end. Activating
 * it pops a menu listing the hidden children by label; choosing
 * one invokes that child's click callback. A child is therefore
 * never painted clipped - it is fully visible or in the menu
 * (UI-REDESIGN 2.4 common rule). */
tk_widget *tk_overflowbar_new(int spacing, int padding);
/* True when the last arrange overflowed at least one child. */
bool tk_overflowbar_has_overflow(const tk_widget *w);

/* ------------------------------------------------------------------ */
/* Widgets                                                             */
/* ------------------------------------------------------------------ */
tk_widget *tk_label_new(const char *text);
void tk_label_set_text(tk_widget *w, const char *text);
/* Label horizontal alignment inside its rect (default START).
 * END is the calculator-display / value-column alignment. */
enum { TK_LABEL_ALIGN_START = 0, TK_LABEL_ALIGN_CENTER = 1,
       TK_LABEL_ALIGN_END = 2 };
void tk_label_set_align(tk_widget *w, int align);
const char *tk_label_text(const tk_widget *w);

tk_widget *tk_button_new(const char *label);
void tk_button_set_on_click(tk_widget *w, tk_notify_cb cb, void *ud);
/* Give a button an icon (pixels in canvas packing, copied). A
 * button with an icon paints as an icon cell - the icon centered
 * at the top, the label below it, and NO card background/border
 * (a subtle pressed wash only) - and measures to fit both. This
 * is the desktop-icon form; plain buttons are unaffected. */
void tk_button_set_icon(tk_widget *w, const uint32_t *px, int iw, int ih);
bool tk_button_has_icon(const tk_widget *w);
/* Per-widget text color override for labels and buttons placed
 * on a surface whose contrast differs from the theme's (e.g.
 * desktop icon labels on a dark wallpaper, where the theme text
 * color would be unreadable). Clear restores the theme color. */
void tk_widget_set_text_color(tk_widget *w, tk_color c);
void tk_widget_clear_text_color(tk_widget *w);
const char *tk_button_label(const tk_widget *w);
/* Replace the button's label (the string is copied; NULL clears).
 * Unlike the older setters, this one invalidates the widget itself
 * when the label actually changes: setters added after M3
 * auto-invalidate as a rule (DESIGN-toolkit-v2.md SS13). If the new
 * label's natural size matters, the caller still re-runs layout -
 * the invalidation covers the button's current rect. */
void tk_button_set_label(tk_widget *w, const char *label);
/* Accent (primary-action) button face: accent background with
 * on_accent text instead of the neutral surface+border form -
 * the login button, the calculator "=", the player play button.
 * The pressed state draws the accent darkened toward black. */
void tk_button_set_accent(tk_widget *w, bool accent);
bool tk_button_is_accent(const tk_widget *w);
/* Shell presentation overrides (UI-REDESIGN 2.3): flat buttons
 * paint no face/border until pressed (panel chrome); icon-only
 * buttons center their icon and do not paint the label (the label
 * string remains as the accessible name); a face color replaces
 * the neutral surface (active task); an indicator draws a 3px bar
 * along the bottom edge (running task). NULL clears face/indicator.
 * Corner radius override: -1 restores the theme radius (a 56px
 * button with radius 28 is the assist circle). */
void tk_button_set_flat(tk_widget *w, bool flat);
void tk_button_set_icon_only(tk_widget *w, bool icon_only);
void tk_button_set_face(tk_widget *w, const tk_color *face);
void tk_button_set_indicator(tk_widget *w, const tk_color *color);
void tk_button_set_corner_radius(tk_widget *w, int radius);

/* Button variants (audit M6; apps-envfit SS6-1/SS6-2). The M3
 * button hierarchy as a first-class choice instead of the old
 * accent flag / flat+face workarounds:
 *   FILLED   - accent fill, on_accent content (the one primary
 *              action on a screen; same face as set_accent)
 *   TONAL    - theme container fill, theme text (secondary)
 *   OUTLINED - no fill, 1px border, accent content
 *   TEXT     - no fill or border, accent content
 * LEGACY (the default) keeps the pre-M6 behaviour exactly,
 * including set_accent / set_flat / set_face. In every variant
 * an icon set with tk_button_set_icon() IS drawn (the old accent
 * path silently dropped it), laid out per tk_button_set_icon_layout. */
typedef enum {
    TK_BTN_LEGACY = 0, TK_BTN_FILLED, TK_BTN_TONAL,
    TK_BTN_OUTLINED, TK_BTN_TEXT,
    /* M7: the fifth M3 colour style - a surface-container-low
     * face with an elevation shadow silhouette and a primary
     * content colour. In the WinUI mode (style 2) it renders as
     * the standard button, which is WinUI's resting form. */
    TK_BTN_ELEVATED
} tk_button_variant;
void tk_button_set_variant(tk_widget *w, tk_button_variant v);
tk_button_variant tk_button_variant_of(const tk_widget *w);
/* Icon placement when the button has both an icon and a label:
 *   CELL - icon over label, no face (desktop-icon form; default,
 *          the pre-M6 behaviour)
 *   ROW  - icon left of the label, one line (bottom-bar primary
 *          buttons, dialog actions)
 * Icon-only buttons (set_icon_only) are unaffected. */
typedef enum { TK_BTN_ICON_CELL = 0, TK_BTN_ICON_ROW } tk_button_icon_layout;
void tk_button_set_icon_layout(tk_widget *w, tk_button_icon_layout l);
tk_button_icon_layout tk_button_icon_layout_of(const tk_widget *w);
/* Text alignment for the button's label (browser SS6-3 request):
 * 0 = start, 1 = center (default, the pre-M6 behaviour),
 * 2 = end. In ROW layout it positions the icon+label group. */
void tk_button_set_text_align(tk_widget *w, int align);
int tk_button_text_align(const tk_widget *w);
/* FAB constructor (audit M6; apps-envfit SS6-3): a FILLED-variant
 * icon button with a fully round face, sized size x size px
 * (pass 0 for the device default, 65px: the M3 56dp FAB
 * converts to 62.7px at this panel's ~179ppi, and 65px is
 * the retained device-fit value, AUDIT (a-1)). It is an
 * ordinary widget: put it in a
 * Fixed parent, or float it over the content with
 * tk_root_add_floating(). The label is the accessible name and
 * is not painted. */
tk_widget *tk_fab_new(const uint32_t *icon_px, int iw, int ih, int size);
#define TK_FAB_DEFAULT_SIZE 65
/* Badge pinned to a button's top-right corner (audit M6): the
 * count semantics of tk_badge_new() (-1 = dot, 0 = hidden,
 * >0 = count capped at "99+"). Painted by the core after the
 * button itself, so it works in every button form. */
void tk_button_set_badge(tk_widget *w, int count);
int tk_button_badge(const tk_widget *w);
/* Toggle button (M7): the M3 toggle form of every button style
 * (and WinUI's ToggleButton). A toggle button keeps a checked
 * state: a click flips it BEFORE on_click fires, and the paint
 * uses the selected colours of its variant (FILLED stays accent,
 * TONAL switches to the secondary container, OUTLINED/TEXT gain
 * a tonal wash, LEGACY gains the container face). */
void tk_button_set_toggle(tk_widget *w, bool toggle);
bool tk_button_is_toggle(const tk_widget *w);
bool tk_button_toggled(const tk_widget *w);
void tk_button_set_toggled(tk_widget *w, bool on);
/* Button children (M7; browser SS6-1): a button may carry child
 * widgets (tk_add_child). Children are laid out as a trailing
 * row inside the button - vertically centred, right-aligned,
 * each at its desired size - measured into the button's desired
 * width, painted above the button's own face/label, and hit-
 * tested before the button itself. The tab-strip form (a label
 * button with a small close button at its right edge) is the
 * motivating case; the button's own label stays left/centre per
 * tk_button_set_text_align(). */

/* Floating-surface backdrop (UI-REDESIGN 2.2/2.5): the widget's
 * rect is painted as an opaque surface with a 1px border and
 * overlay corners before the widget and its children; floating
 * surfaces additionally get the offset shadow silhouette (solid
 * black at 25% blended once, damage grows by the offset only).
 * radius < 0 uses the theme's radius_overlay. */
void tk_widget_set_backdrop(tk_widget *w, bool floating, int radius);
void tk_widget_clear_backdrop(tk_widget *w);
/* Backdrop face override (UI-STYLE-v2): paint the backdrop with
 * this colour instead of the theme surface - e.g. the tonal
 * container colour of an active quick-settings tile, whose
 * contents (icon, labels) are children painted above the
 * backdrop. NULL restores the theme surface. */
void tk_widget_set_backdrop_face(tk_widget *w, const tk_color *col);
/* Icon-cell label shadow: draw the label with a 1px dark offset
 * copy underneath (desktop icons on wallpaper, UI-REDESIGN 2.3). */
void tk_widget_set_text_shadow(tk_widget *w, bool shadow);

tk_widget *tk_checkbox_new(const char *label, bool checked);
bool tk_checkbox_checked(const tk_widget *w);
void tk_checkbox_set_checked(tk_widget *w, bool v);
void tk_checkbox_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);

tk_widget *tk_radio_new(const char *label, int group_id, int value);
int tk_radio_value(const tk_widget *w);
bool tk_radio_selected(const tk_widget *w);
void tk_radio_select(tk_widget *w); /* selects within its sibling group */
void tk_radio_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);

tk_widget *tk_slider_new(float value); /* 0..1 */
float tk_slider_value(const tk_widget *w);
void tk_slider_set_value(tk_widget *w, float v);
/* M13: opt-in animation for PROGRAMMATIC value changes (the
 * mode pair: M3 200ms Standard decelerate / WinUI 167ms).
 * Default off: the set snaps, as it did before M13. Widgets
 * that mirror external state (backend-synced sliders, seek
 * bars, meters) must leave it off - an animated tail breaks
 * the settled-frames-transfer-zero contract they rely on;
 * enable it for user-facing controlled values. Pointer and
 * keyboard changes are always direct. */
void tk_slider_set_animate_values(tk_widget *w, bool on);
void tk_slider_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);
/* Slider range form (audit M6): the M3 slider selects from a
 * value range - continuous, or discrete when step > 0 (stop
 * indicators are drawn at each step). tk_slider_set_range()
 * switches the slider to range mode: values are then expressed
 * in [min, max] units through tk_slider_set_value_range() /
 * tk_slider_value_range(), pointer drags and arrow keys step
 * by `step` (or move continuously when step <= 0), and on_change
 * fires as before. The fraction API above keeps working in both
 * modes (fraction = (value - min) / (max - min)). Calling
 * tk_slider_clear_range() returns to the plain 0..1 form. */
void tk_slider_set_range(tk_widget *w, float min, float max, float step);
void tk_slider_clear_range(tk_widget *w);
bool tk_slider_has_range(const tk_widget *w);
void tk_slider_set_value_range(tk_widget *w, float v);
float tk_slider_value_range(const tk_widget *w);
/* Dual-handle Range slider (M7): the M3 Range form. Requires
 * range mode (tk_slider_set_range). tk_slider_set_values2()
 * switches the slider to two handles selecting [lo, hi]; the
 * track between the handles is filled, each handle drags
 * independently (clamped so lo <= hi), and while a handle is
 * dragged or stepped its value indicator bubble (the M3 value
 * indicator: a 48x44dp label container in the inverse colours)
 * is painted above it. Arrow keys move the ACTIVE handle (the
 * last one touched; tk_slider_set_active_handle chooses it).
 * on_change (tk_slider_set_on_change) fires for either handle;
 * tk_slider_active_handle() says which one moved last. The
 * single-value API keeps working: it addresses the hi handle,
 * and tk_slider_value_range() returns hi. */
void tk_slider_set_values2(tk_widget *w, float lo, float hi);
void tk_slider_clear_dual(tk_widget *w);
bool tk_slider_is_dual(const tk_widget *w);
float tk_slider_value_lo(const tk_widget *w);
float tk_slider_value_hi(const tk_widget *w);
int tk_slider_active_handle(const tk_widget *w); /* 0 = lo, 1 = hi */
void tk_slider_set_active_handle(tk_widget *w, int which);

/* TextField: single-line editor backed by textcore. */
tk_widget *tk_textfield_new(const char *initial);
tc_buffer *tk_textfield_buffer(tk_widget *w);
void tk_textfield_set_text(tk_widget *w, const char *text);
size_t tk_textfield_text(const tk_widget *w, char *out, size_t cap);
void tk_textfield_set_placeholder(tk_widget *w, const char *text);
void tk_textfield_set_clipboard(tk_widget *w, const tc_clipboard *cb);
void tk_textfield_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);
/* Password mask (M3): when on, the field renders one mask glyph
 * (U+25CF) per character instead of the real glyphs - committed
 * text and the IME composition alike. The buffer keeps the real
 * text (tk_textfield_text() still returns it); only the display
 * and the pointer hit-testing use the mask, and both use the same
 * per-character mask advance, so caret placement and selection
 * (byte offsets in textcore) stay consistent with what is shown.
 * The placeholder is not masked (it is not user input). */
void tk_textfield_set_password(tk_widget *w, bool password);
bool tk_textfield_is_password(const tk_widget *w);
/* Supporting / error text (audit M6): the M3 text field carries
 * a caption line under the container - helper text normally, an
 * error message (danger colour, shown until fixed) in the error
 * state. The line is part of the widget: measure grows by one
 * caption line when text is set, and the container keeps the
 * theme control height at the top of the rect. NULL/"" clears.
 * set_error(true) with no supporting text shows nothing extra;
 * pair it with set_supporting_text() for the message. */
void tk_textfield_set_supporting_text(tk_widget *w, const char *text);
const char *tk_textfield_supporting_text(const tk_widget *w);
void tk_textfield_set_error(tk_widget *w, bool error);
bool tk_textfield_is_error(const tk_widget *w);
/* Prefix / suffix (M7): the M3 text field's fixed adornments -
 * a prefix is painted inside the container before the text
 * (the text and caret shift right by its width), a suffix after
 * the text area, right-aligned. Both are dim-coloured and are
 * not part of the buffer. NULL clears. */
/* Character counter (M7): the M3 counter sits at the right end
 * of the supporting-text line as "n / max" (or plain "n" with
 * no maximum). tk_textfield_set_max_length() also caps input:
 * text input and paste that would exceed the maximum are
 * truncated at the cap, as in the M3 field. The supporting line
 * is measured in when a counter is enabled even without
 * supporting text. */
/* Validator (M7): the Android Validator contract (cf.
 * AutoCompleteTextView.Validator.isValid): the callback runs
 * when the field loses focus and on tk_textfield_validate();
 * a false result puts the field into its error state showing
 * the message set with tk_textfield_set_error_message() (the
 * M3 rule: the error text replaces the supporting text until
 * the input is fixed). A later successful validation, or any
 * edit of the text, clears the error state. */
typedef bool (*tk_text_validator_cb)(tk_widget *field,
                                     const char *text, void *ud);
void tk_textfield_set_validator(tk_widget *w, tk_text_validator_cb cb,
                                void *ud);
void tk_textfield_set_error_message(tk_widget *w, const char *text);
bool tk_textfield_validate(tk_widget *w);
bool tk_textfield_is_valid(const tk_widget *w);
void tk_textfield_set_prefix(tk_widget *w, const char *text);
void tk_textfield_set_suffix(tk_widget *w, const char *text);
void tk_textfield_set_max_length(tk_widget *w, int max_chars);
int tk_textfield_max_length(const tk_widget *w);
void tk_textfield_set_counter(tk_widget *w, bool show);
/* Auto-completion (M7): the AutoCompleteTextView form. A
 * completion source (a static string list, copied) plus a
 * threshold (default 1, the Android default): while the user
 * types, a dropdown anchored under the field lists the entries
 * that START WITH the current text (the Android ArrayAdapter
 * filter); choosing one replaces the field text with it and
 * fires on_change. Escape or focus loss dismisses the dropdown.
 * The dropdown is the toolkit Menu overlay, so it inherits the
 * menu's painting and dismissal rules. */
void tk_textfield_set_completions(tk_widget *w,
                                  const char *const *items, size_t n);
void tk_textfield_set_completion_threshold(tk_widget *w, int chars);
bool tk_textfield_completion_open(const tk_widget *w);

/* ScrollView: exactly one child; content taller/wider than the view
 * is clipped and offset. Wheel, keyboard and one-finger drag-scroll
 * (with a click-suppression threshold) are supported. After a drag
 * ends with velocity, the app calls tk_scrollview_tick() per frame;
 * it applies one deceleration step and returns true while moving. */
tk_widget *tk_scrollview_new(void);
void tk_scrollview_set_child(tk_widget *sv, tk_widget *child);
int tk_scrollview_offset_y(const tk_widget *sv);
void tk_scrollview_scroll_to(tk_widget *sv, int y);
bool tk_scrollview_tick(tk_widget *sv);

/* ListView: string items, single selection. */
tk_widget *tk_listview_new(void);
void tk_listview_set_items(tk_widget *w, const char *const *items, size_t n);
size_t tk_listview_count(const tk_widget *w);
const char *tk_listview_item(const tk_widget *w, int idx); /* NULL if out of range */
int tk_listview_selected(const tk_widget *w); /* -1 = none */
void tk_listview_select(tk_widget *w, int idx);
/* Marked row: an app-level highlight distinct from the selection
 * (e.g. the track currently playing in a media list). Painted
 * with an accent bar + tinted background. -1 = none. Reset by
 * tk_listview_set_items. */
void tk_listview_set_marked(tk_widget *w, int idx);
int tk_listview_marked(const tk_widget *w);
/* Move the item at `from` to position `to`; the items in between
 * shift by one and the strings themselves are not copied. The
 * selection follows the selected item, and on_select fires if its
 * index changed. An actual move invalidates the widget (post-M3
 * setters auto-invalidate, DESIGN-toolkit-v2.md SS13). Returns
 * false for out-of-range indices (the list is left untouched);
 * from == to is a successful no-op. */
bool tk_listview_move_row(tk_widget *w, int from, int to);
void tk_listview_set_on_select(tk_widget *w, tk_notify_cb cb, void *ud);
void tk_listview_set_on_activate(tk_widget *w, tk_notify_cb cb, void *ud);
/* M5 composite rows: checkable rows and section headers.
 * Checkable: every row gains the CheckBox widget's 18px box at
 * its left edge; a tap on the box toggles the check WITHOUT
 * changing the selection and fires on_check (read the row with
 * tk_listview_last_checked). Programmatic set_row_checked does
 * not fire on_check. Headers: caption-weight section labels -
 * pointer taps on them are consumed without selecting, and key
 * navigation steps over them (programmatic select is unaffected).
 * Row flags are reset by tk_listview_set_items and travel with
 * their row in tk_listview_move_row. */
void tk_listview_set_checkable(tk_widget *w, bool on);
bool tk_listview_checkable(const tk_widget *w);
bool tk_listview_row_checked(const tk_widget *w, int idx);
void tk_listview_set_row_checked(tk_widget *w, int idx, bool on);
int tk_listview_last_checked(const tk_widget *w); /* -1 = none yet */
void tk_listview_set_on_check(tk_widget *w, tk_notify_cb cb, void *ud);
void tk_listview_set_header(tk_widget *w, int idx, bool header);
bool tk_listview_is_header(const tk_widget *w, int idx);
/* Multi-select mode (audit M6): the M3 list selection modes are
 * single-action / single-select / multi-select. In multi-select
 * mode a row tap toggles that row's checked state (rows render
 * their checkbox regardless of the checkable flag) and fires
 * on_check; the single selection still follows the last tap.
 * Keyboard Up/Down moves the selection without toggling.
 * tk_listview_checked_count() counts the checked rows. */
void tk_listview_set_multi_select(tk_widget *w, bool on);
bool tk_listview_multi_select(const tk_widget *w);
int tk_listview_checked_count(const tk_widget *w);
/* Optional per-item sub-text (copied; NULL entries allowed). When
 * any item has a sub-text, every row uses the taller two-line row
 * height (theme row_h2): main text in body style, sub-text in
 * caption style under it. Pass n=0/NULL to clear. */
void tk_listview_set_subitems(tk_widget *w, const char *const *subs,
                              size_t n);
/* Optional per-item icons: canvas-packed pixels, BORROWED (not
 * copied) - the caller keeps them alive while the list shows
 * them (the shell icon cache is the intended owner). icons[i]
 * may be NULL (no icon for that row). All icons share the given
 * pixel size and are drawn at their natural size, vertically
 * centred, at the row's left. Pass icons=NULL to clear. */
void tk_listview_set_item_icons(tk_widget *w,
                                const uint32_t *const *icons,
                                int iw, int ih, size_t n);
/* The row height this list uses under theme t (row_h, or row_h2
 * when sub-texts are present). */
int tk_listview_row_h(const tk_widget *w, const tk_theme *t);

/* ------------------------------------------------------------------ */
/* M2 widgets                                                          */
/* ------------------------------------------------------------------ */

/* Switch: slider-style two-state toggle (state model shared with
 * CheckBox semantics, separate widget kind and rendering). */
tk_widget *tk_switch_new(const char *label, bool on);
bool tk_switch_on(const tk_widget *w);
void tk_switch_set_on(tk_widget *w, bool v);
void tk_switch_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);

/* ProgressBar: deterministic progress, value 0..1. */
tk_widget *tk_progress_new(float value);
float tk_progress_value(const tk_widget *w);
void tk_progress_set_value(tk_widget *w, float v);
/* M13: opt-in for the M3 determinate transition (250ms,
 * cubic-bezier(0.4, 0, 0.6, 1)) on programmatic sets in the
 * M3 mode. Default off (snap) for the same reason as
 * tk_slider_set_animate_values: level meters and status
 * bars mirror external state. */
void tk_progress_set_animate_values(tk_widget *w, bool on);
/* Indeterminate mode (audit M6): the M3 linear indicator's
 * second behaviour - a segment sweeps the track while the total
 * is unknown. Driven by tk_root_tick() time through the
 * animation engine (period 1200ms); a detached/no-root widget
 * paints a static segment. Setting a value does not leave
 * indeterminate mode; clear it explicitly. */
void tk_progress_set_indeterminate(tk_widget *w, bool on);
bool tk_progress_is_indeterminate(const tk_widget *w);
/* Spinner: the circular indeterminate indicator (audit M6).
 * A rotating arc (270 degrees) in the accent colour, sized
 * size x size px (0 = 40px default). Same tick driving as the
 * indeterminate linear bar. */
tk_widget *tk_spinner_new(int size);

/* Separator: layout participant; vertical=false draws a horizontal
 * rule, vertical=true a vertical one. */
tk_widget *tk_separator_new(bool vertical);

/* TextView: multi-line editor/viewer backed by textcore. Wrapping
 * uses tk_text_wrap (real font metrics + kinsoku). Editable by
 * default; read-only mode still allows navigation and selection.
 * Pointer: click places the caret, drag selects (M3), Shift+click
 * extends the selection. */
tk_widget *tk_textview_new(const char *initial);
tc_buffer *tk_textview_buffer(tk_widget *w);
void tk_textview_set_text(tk_widget *w, const char *text);
size_t tk_textview_text(const tk_widget *w, char *out, size_t cap);
void tk_textview_set_editable(tk_widget *w, bool editable);
bool tk_textview_is_editable(const tk_widget *w);
void tk_textview_set_clipboard(tk_widget *w, const tc_clipboard *cb);
void tk_textview_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);
/* Line-tap callback (audit M6; apps wave2 SS8 - notes checklist
 * glyphs): fired on pointer-down with the VISUAL (wrapped) line
 * index under the tap, in the same numbering as
 * tk_textview_visual_lines(). The caret placement of the tap
 * still happens; the callback is an additional notification. */
typedef void (*tk_line_cb)(tk_widget *w, int visual_line, void *ud);
void tk_textview_set_on_line_tap(tk_widget *w, tk_line_cb cb, void *ud);
/* Visual (wrapped) line count at the current arranged width. */
int tk_textview_visual_lines(const tk_widget *w);
/* Scroll so the cursor's visual line is visible. */
void tk_textview_ensure_cursor_visible(tk_widget *w);

/* ImageView: shows an RGBA image, nearest-neighbour scaled into its
 * rect (aspect not preserved unless the rect matches). Pixels are
 * copied. Load uses stb_image (PNG/JPEG/...); decode errors -> false. */
tk_widget *tk_imageview_new(void);
bool tk_imageview_set_pixels(tk_widget *w, const uint32_t *px, int iw, int ih);
bool tk_imageview_load(tk_widget *w, const char *path);
int tk_imageview_image_w(const tk_widget *w);
int tk_imageview_image_h(const tk_widget *w);
/* M5 thumbnails: decode + area-average downscale so the decoded
 * buffer is sized to the display cell, not the camera. The fit
 * never upscales. tk_image_scale_to_fit works on in-memory pixels
 * (also the unit the tests pin down); alpha-weighted averaging. */
bool tk_image_scale_to_fit(const uint32_t *px, int iw, int ih,
                           int max_w, int max_h,
                           uint32_t **out_px, int *ow, int *oh);
bool tk_image_load_thumbnail(const char *path, int max_w, int max_h,
                             uint32_t **out_px, int *ow, int *oh);
bool tk_imageview_load_thumbnail(tk_widget *w, const char *path,
                                 int max_w, int max_h);

/* TreeView: hierarchical list. Nodes are added with a parent id
 * (-1 = root level); ids are stable insertion indices. Rows flatten
 * depth-first, skipping collapsed subtrees. */
tk_widget *tk_treeview_new(void);
int tk_treeview_add(tk_widget *w, int parent_id, const char *label);
size_t tk_treeview_node_count(const tk_widget *w);
int tk_treeview_visible_rows(const tk_widget *w);
int tk_treeview_row_node(const tk_widget *w, int row); /* node id or -1 */
int tk_treeview_node_depth(const tk_widget *w, int node_id);
bool tk_treeview_expanded(const tk_widget *w, int node_id);
void tk_treeview_set_expanded(tk_widget *w, int node_id, bool expanded);
int tk_treeview_selected(const tk_widget *w);          /* node id or -1 */
void tk_treeview_select(tk_widget *w, int node_id);
void tk_treeview_set_on_select(tk_widget *w, tk_notify_cb cb, void *ud);
void tk_treeview_set_on_activate(tk_widget *w, tk_notify_cb cb, void *ud);
/* Model interface (M7): the Adapter form of the TreeView. When
 * a model is set, the view's content comes from the callbacks
 * instead of tk_treeview_add(): the view walks the model from
 * the root (parent_id -1), materialising the visible rows;
 * expansion state is kept per model id inside the view, and
 * selection/activation report MODEL ids. child_id must return
 * a stable, unique id per node (never -1, which means root).
 * label() returns a borrowed string (the view copies it).
 * tk_treeview_set_model(w, NULL) returns to the manual form
 * (clearing the materialised nodes); tk_treeview_model_refresh()
 * re-walks the model after its data changed, preserving the
 * expansion and selection of ids that still exist. */
typedef struct {
    void *ud;
    int (*child_count)(void *ud, int parent_id);
    int (*child_id)(void *ud, int parent_id, int index);
    const char *(*label)(void *ud, int id);
    bool (*is_leaf)(void *ud, int id);
} tk_tree_model;
void tk_treeview_set_model(tk_widget *w, const tk_tree_model *model);
bool tk_treeview_has_model(const tk_widget *w);
void tk_treeview_model_refresh(tk_widget *w);

/* Tabs: pages are children; only the active page is arranged,
 * painted, hit-tested and focusable. Titles are kept by the Tabs. */
tk_widget *tk_tabs_new(void);
void tk_tabs_add_page(tk_widget *tabs, const char *title, tk_widget *page);
int tk_tabs_active(const tk_widget *w);
void tk_tabs_set_active(tk_widget *w, int idx);
void tk_tabs_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);

/* TabBar (audit M6; apps-envfit SS6-4): the M3 primary-tab
 * selector as a standalone bar - equal-width segments, an
 * optional icon over each label, an optional badge count, and
 * the active indicator (3px accent pill under the active
 * segment, inset at both ends, animated between segments when
 * the root has animations on). Unlike TK_W_TABS it owns no
 * pages: it reports the selection through on_select and the app
 * swaps the content. Bar height is the theme control height for
 * label-only items and row_h2 when any item has an icon. */
tk_widget *tk_tabbar_new(void);
void tk_tabbar_add(tk_widget *w, const char *label);
void tk_tabbar_set_item_icon(tk_widget *w, int idx,
                             const uint32_t *px, int iw, int ih);
void tk_tabbar_set_item_badge(tk_widget *w, int idx, int count);
int tk_tabbar_selected(const tk_widget *w);
void tk_tabbar_select(tk_widget *w, int idx);
void tk_tabbar_set_on_select(tk_widget *w, tk_notify_cb cb, void *ud);

/* Chip (audit M6): the M3 chip family in one widget -
 *   ASSIST     - an action chip; a tap fires on_click
 *   FILTER     - selectable; a tap toggles the selected state
 *                (tonal container fill + check mark) and fires
 *                on_click
 *   INPUT      - a token with a trailing close affordance; the
 *                close tap fires on_close, a body tap on_click
 *   SUGGESTION - like ASSIST (kept distinct for call-site intent)
 * Height is the theme's chip height (37px in the M3 themes:
 * the 32dp container converts to 35.8px at this panel's
 * ~179ppi and 37px is the retained value, AUDIT (a-7); 40px
 * in the WinUI and classic themes, a shared form with no
 * reference conversion), with the hit rect being the full
 * arranged rect. The leading icon (borrowed pixels,
 * drawn at natural size) is optional. */
typedef enum {
    TK_CHIP_ASSIST = 0, TK_CHIP_FILTER, TK_CHIP_INPUT,
    TK_CHIP_SUGGESTION
} tk_chip_kind;
tk_widget *tk_chip_new(tk_chip_kind kind, const char *label);
void tk_chip_set_icon(tk_widget *w, const uint32_t *px, int iw, int ih);
bool tk_chip_selected(const tk_widget *w);
void tk_chip_set_selected(tk_widget *w, bool on);
void tk_chip_set_on_click(tk_widget *w, tk_notify_cb cb, void *ud);
void tk_chip_set_on_close(tk_widget *w, tk_notify_cb cb, void *ud);

/* Badge (audit M6): the M3 badge - a small dot (count < 0) or a
 * count pill (count > 0, rendered "99+" past two digits by
 * default cap 99). count == 0 hides the badge (it measures
 * 0x0 and paints nothing). It is a leaf widget: place it over
 * an icon's corner with a Fixed parent, or inside a TabBar /
 * BottomBar cell, which render their own badges. */
tk_widget *tk_badge_new(int count);
void tk_badge_set_count(tk_widget *w, int count);
int tk_badge_count(const tk_widget *w);

/* Menu: overlay popup (not a tree child). Items may be plain,
 * checkable, separators, or submenus. Popup it with tk_menu_popup;
 * selection fires on_select and closes the menu automatically. */
tk_widget *tk_menu_new(void);
void tk_menu_add_item(tk_widget *menu, const char *label, int id);
void tk_menu_add_check_item(tk_widget *menu, const char *label, int id,
                            bool checked);
void tk_menu_add_separator(tk_widget *menu);
void tk_menu_add_submenu(tk_widget *menu, const char *label,
                         tk_widget *submenu);
void tk_menu_set_item_enabled(tk_widget *menu, int id, bool enabled);
bool tk_menu_item_checked(const tk_widget *menu, int id);
int tk_menu_last_id(const tk_widget *menu);   /* last selected id, -1 none */
void tk_menu_set_on_select(tk_widget *menu, tk_notify_cb cb, void *ud);
/* Item icons (audit M6; apps-envfit SS6-6): the M3 menu item
 * anatomy has a leading icon slot (24dp). Pixels are BORROWED
 * (the caller keeps them alive while the menu exists, the same
 * rule as ListView item icons) and drawn at their natural size,
 * vertically centred, in a fixed leading slot; item text shifts
 * right when any item in the menu has an icon. NULL clears. */
void tk_menu_set_item_icon(tk_widget *menu, int id,
                           const uint32_t *px, int iw, int ih);
/* Item trailing content (M7): the M3 menu item anatomy also has
 * a trailing slot - a text (a shortcut hint, a value) painted
 * right-aligned in the dim colour, and/or a badge (the badge
 * count semantics of tk_badge_new: -1 dot, 0 none, >0 count).
 * The menu's measured width grows to fit trailing content. */
void tk_menu_set_item_trailing(tk_widget *menu, int id,
                               const char *text);
void tk_menu_set_item_badge(tk_widget *menu, int id, int count);
void tk_menu_popup(tk_root *r, tk_widget *menu, int x, int y);
void tk_menu_close(tk_root *r, tk_widget *menu);
bool tk_menu_is_open(const tk_root *r, const tk_widget *menu);

/* MenuBar: in-tree bar; clicking a title pops up its menu. */
tk_widget *tk_menubar_new(void);
void tk_menubar_add_menu(tk_widget *bar, const char *title, tk_widget *menu);

/* Dialog: container (vertical box layout) shown as a modal overlay.
 * While shown, pointer input outside is swallowed and the focus
 * chain is trapped inside the dialog. */
tk_widget *tk_dialog_new(const char *title);
void tk_dialog_show(tk_root *r, tk_widget *dlg);
void tk_dialog_close(tk_root *r, tk_widget *dlg);
bool tk_dialog_is_shown(const tk_root *r, const tk_widget *dlg);
/* Non-modal and full-screen dialogs (M7): the M3 dialog family
 * also has a non-modal form (input elsewhere stays live; the
 * dialog is a plain overlay, closed with tk_dialog_close) and
 * a full-screen form (the container covers the whole root -
 * on this 854x480 panel the M3 full-screen dialog IS the
 * natural form for editors). The two combine: a full-screen
 * dialog may be modal or modeless. Full-screen is a property
 * of the dialog widget and takes effect at show/layout time. */
void tk_dialog_show_modeless(tk_root *r, tk_widget *dlg);
void tk_dialog_set_fullscreen(tk_widget *dlg, bool full);
bool tk_dialog_is_fullscreen(const tk_widget *dlg);
bool tk_dialog_is_modal(const tk_root *r, const tk_widget *dlg);

/* FileDialog (M5): a composed Dialog for picking one file.
 * Directories sort first and show a trailing '/', dotfiles are
 * hidden, and files are filtered by extension (case-insensitive;
 * exts entries with or without the dot; n_exts == 0 shows all
 * files). ".." navigates to the parent; activating a directory
 * (Enter on it, or the Select button) navigates into it;
 * activating a file - or pressing Select with a file selected -
 * delivers its full path through on_result and closes the dialog.
 * Cancel delivers NULL. The path pointer is valid only during
 * the callback. Entries are capped at 256 per directory. */
typedef void (*tk_filedialog_cb)(tk_widget *dlg, const char *path,
                                 void *ud);
tk_widget *tk_filedialog_new(const char *title, const char *start_dir,
                             const char *const *exts, size_t n_exts);
void tk_filedialog_show(tk_root *r, tk_widget *dlg);
void tk_filedialog_set_on_result(tk_widget *dlg, tk_filedialog_cb cb,
                                 void *ud);
const char *tk_filedialog_dir(const tk_widget *dlg);
void tk_filedialog_refresh(tk_widget *dlg);

/* ColorPicker dialog (audit M6, completed M7): a composed
 * Dialog with a grid of preset swatches (the Material baseline
 * palette, 18 hues x the picked shade row plus black/white/
 * grays), a preview strip showing the current pick, a custom
 * hex entry row and (optionally) an alpha slider, and Cancel /
 * OK buttons. The result callback receives the chosen RGBA
 * (0xRRGGBBAA packing) and chosen=false on cancel. */
typedef void (*tk_colorpicker_cb)(tk_widget *dlg, uint32_t rgba,
                                  bool chosen, void *ud);
tk_widget *tk_colorpicker_new(const char *title, uint32_t initial_rgba);
void tk_colorpicker_show(tk_root *r, tk_widget *dlg);
void tk_colorpicker_set_on_result(tk_widget *dlg, tk_colorpicker_cb cb,
                                  void *ud);
uint32_t tk_colorpicker_value(const tk_widget *dlg);
/* Custom colours (M7): a hex entry row ("#RRGGBB" or
 * "#RRGGBBAA") under the swatch grid - editing it to a valid
 * value updates the pick and the preview immediately - and an
 * optional alpha slider row. tk_colorpicker_set_value() sets
 * the pick programmatically (syncing swatches, hex and
 * preview). Alpha is OFF by default (the pre-M7 behaviour:
 * results are always opaque); when enabled, the slider row is
 * visible and the returned value carries the chosen alpha. */
void tk_colorpicker_set_value(tk_widget *dlg, uint32_t rgba);
void tk_colorpicker_set_alpha_enabled(tk_widget *dlg, bool on);
bool tk_colorpicker_alpha_enabled(const tk_widget *dlg);

/* BottomBar (audit M6; apps-envfit SS6-5): the standard bottom
 * bar as a composed container - a tonal surface strip holding
 * icon-over-label cells (the M3 navigation-bar cell form).
 * Cells are Buttons: add them with tk_bottombar_add_cell(),
 * reach them with tk_bottombar_cell() for label changes, mark
 * one selected (the navigation indicator form below) with
 * tk_bottombar_set_selected(), and badge a cell with
 * tk_bottombar_set_cell_badge(). The bar measures 64-76px tall
 * depending on icon size and is meant to sit at the bottom of a
 * vertical box (or flex-end of the screen layout); show/hide it
 * with tk_set_visible() and re-run layout, as apps do today. */
tk_widget *tk_bottombar_new(void);
int tk_bottombar_add_cell(tk_widget *bar, const uint32_t *icon_px,
                          int iw, int ih, const char *label,
                          tk_notify_cb cb, void *ud);
tk_widget *tk_bottombar_cell(tk_widget *bar, int idx);
void tk_bottombar_set_selected(tk_widget *bar, int idx); /* -1 = none */
int tk_bottombar_selected(const tk_widget *bar);
void tk_bottombar_set_cell_badge(tk_widget *bar, int idx, int count);
/* Selection indicator form (M7): 1 = the M3 navigation-bar
 * form - a pill (secondary container) behind the selected
 * cell's ICON only, sized from the icon (64x32dp for a 24dp
 * icon; the label sits below the pill, outside it) - and the
 * DEFAULT since M23 (the user's 2026-11-21 send-back: the
 * pre-M23 default painted a wide oval over the whole cell,
 * icon and label included, which matches no reference form).
 * 0 = the tonal cell face (the pre-M7 apps-envfit rule),
 * opt-in legacy. In the WinUI mode the pill form renders as
 * the NavigationView selection: a subtle fill over the whole
 * cell with a 3px accent bar at its left edge. Switching
 * re-applies the current selection. */
void tk_bottombar_set_indicator_mode(tk_widget *bar, int mode);
int tk_bottombar_indicator_mode(const tk_widget *bar);

/* DatePicker / TimePicker (M5): segmented field editors. Tap a
 * segment to make it active, tap the right-edge stepper (upper
 * half +1, lower half -1) or use Left/Right + Up/Down / wheel to
 * edit. Fields wrap within their range except the year (clamped
 * 1970..2100); the day clamps to the month length with the full
 * leap-year rule. on_change fires on user edits and on
 * programmatic set_value changes. */
tk_widget *tk_datepicker_new(int year, int month, int day);
void tk_datepicker_value(const tk_widget *w, int *y, int *m, int *d);
void tk_datepicker_set_value(tk_widget *w, int y, int m, int d);
void tk_datepicker_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);
tk_widget *tk_timepicker_new(bool show_seconds);
void tk_timepicker_value(const tk_widget *w, int *h, int *m, int *s);
void tk_timepicker_set_value(tk_widget *w, int h, int m, int s);
void tk_timepicker_set_show_seconds(tk_widget *w, bool on);
void tk_timepicker_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);

/* ComboBox: text + dropdown built on Menu. Non-editable (default):
 * the text is the selected item. Editable (M3): the user can type a
 * custom value - clicking the text zone places a caret, clicking
 * the arrow zone opens the dropdown; Enter commits the text (an
 * exact item match selects that item, otherwise the text stays as
 * a custom value with selection -1 and on_change fires); Escape
 * reverts to the selected item's text. Up/Down cycle the selection
 * and the text follows it. tk_combobox_text() returns the current
 * text in both modes (item text, custom text, or ""). */
tk_widget *tk_combobox_new(void);
void tk_combobox_set_items(tk_widget *w, const char *const *items, size_t n);
int tk_combobox_selected(const tk_widget *w); /* -1 = none/custom */
void tk_combobox_select(tk_widget *w, int idx);
void tk_combobox_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);
bool tk_combobox_is_open(const tk_widget *w);
void tk_combobox_set_editable(tk_widget *w, bool editable);
bool tk_combobox_is_editable(const tk_widget *w);
const char *tk_combobox_text(const tk_widget *w);
void tk_combobox_set_text(tk_widget *w, const char *text);

/* SpinButton: numeric value with step buttons and keyboard stepping.
 * Direct text entry (M3): while focused, the value is edited as
 * text - digits, '.', '-', '+', 'e'/'E' are accepted, Backspace /
 * Delete / Left / Right / Home / End edit, Enter commits (parse,
 * clamp, on_change; unparseable input reverts), Escape reverts,
 * Up/Down commit and step, focus loss commits. */
tk_widget *tk_spinbutton_new(float value, float min, float max, float step);
float tk_spinbutton_value(const tk_widget *w);
void tk_spinbutton_set_value(tk_widget *w, float v);
void tk_spinbutton_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud);

/* Grid container: children flow row-major into `cols` columns.
 * Column widths/row heights start from the largest desired size per
 * track and stretch/shrink proportionally to the arranged rect. */
tk_widget *tk_grid_new(int cols, int h_spacing, int v_spacing, int padding);
/* Cell spans (audit M6): a grid child may occupy several columns
 * and/or rows (the calculator's wide "0" key). Placement is
 * row-major first-fit: a child that does not fit in the rest of
 * its row starts on the next row. Spans are clamped to the grid's
 * column count; 1x1 (the default) is the pre-M6 behaviour. Set
 * before adding the child to the grid. */
void tk_grid_set_span(tk_widget *child, int col_span, int row_span);
void tk_grid_span(const tk_widget *child, int *col_span, int *row_span);
/* Uniform tracks (proportion pass): every column gets an equal
 * share of the arranged width and every row an equal share of
 * the arranged height, ignoring per-track desired sizes - the
 * XAML Grid "1*" star-sizing behaviour the reference keypads
 * use (microsoft/calculator: all rows Height="1*", all columns
 * Width="1*"). For keypads and other cell-uniform grids where
 * content-sized tracks would make the cells visibly unequal.
 * The grid's desired size still derives from the largest
 * child's desired size times the track count. */
void tk_grid_set_uniform(tk_widget *grid, bool uniform);
bool tk_grid_uniform(const tk_widget *grid);
/* Box wrap: horizontal boxes may wrap children onto multiple rows.
 * (Vertical wrap is not implemented; documented in DESIGN.) */
void tk_box_set_wrap(tk_widget *box, bool wrap);

/* ------------------------------------------------------------------ */
/* M7 components and platform services                                 */
/* ------------------------------------------------------------------ */

/* Card (M7): the M3 card as a first-class CONTAINER kind (a
 * vertical box). The three colour styles of the spec:
 *   ELEVATED - surface-container-low fill + shadow silhouette
 *   FILLED   - surface-container-highest fill, no border
 *   OUTLINED - surface fill + outline-variant border
 * Corner radius is the theme's card radius (M3: 12dp). Content
 * padding defaults to 16dp at this panel (19px). In the WinUI
 * mode the card renders with the CardBackground fill, the card
 * stroke and the 4epx control radius, per the Fluent card
 * treatment. Children are added with tk_add_child and laid out
 * like a vertical box (spacing/padding are the box fields). */
typedef enum {
    TK_CARD_ELEVATED = 0, TK_CARD_FILLED, TK_CARD_OUTLINED
} tk_card_variant;
tk_widget *tk_card_new(tk_card_variant v);
void tk_card_set_variant(tk_widget *w, tk_card_variant v);
tk_card_variant tk_card_variant_of(const tk_widget *w);

/* SearchBar (M7): the M3 search component. The bar form is a
 * 56dp container (surface-container-high, full-round in the M3
 * mode; the WinUI mode renders the AutoSuggestBox form: control
 * fill, control stroke, control radius) with a leading search
 * glyph, the editable text, and a trailing clear affordance
 * while text is present. Setting suggestions turns it into the
 * SearchView form: while typing, a docked container under the
 * bar lists the entries starting with the query (the search
 * spec's suggestion container); choosing one fills the bar and
 * fires on_search, as does Enter. Escape dismisses the list. */
tk_widget *tk_searchbar_new(const char *placeholder);
tc_buffer *tk_searchbar_buffer(tk_widget *w);
void tk_searchbar_set_text(tk_widget *w, const char *text);
size_t tk_searchbar_text(const tk_widget *w, char *out, size_t cap);
void tk_searchbar_set_suggestions(tk_widget *w,
                                  const char *const *items, size_t n);
void tk_searchbar_set_on_search(tk_widget *w, tk_notify_cb cb,
                                void *ud);
void tk_searchbar_set_on_change(tk_widget *w, tk_notify_cb cb,
                                void *ud);
bool tk_searchbar_suggestions_open(const tk_widget *w);

/* Segmented buttons (M7): the M3 segmented button - a row of
 * equal-width segments in one outlined container (40dp tall,
 * 48dp targets), the selected segment carrying the secondary-
 * container fill and a check mark. Single-select (radio-like)
 * or multi-select (toggle set) form. In the WinUI mode the
 * segments render as connected ToggleButtons (accent fill when
 * checked). Segments: label plus an optional leading icon
 * (borrowed pixels). */
tk_widget *tk_segmented_new(bool multi_select);
void tk_segmented_add(tk_widget *w, const char *label);
void tk_segmented_set_item_icon(tk_widget *w, int idx,
                                const uint32_t *px, int iw, int ih);
int tk_segmented_selected(const tk_widget *w);   /* single: idx/-1 */
uint32_t tk_segmented_mask(const tk_widget *w);  /* multi: bitset */
void tk_segmented_select(tk_widget *w, int idx);
void tk_segmented_set_mask(tk_widget *w, uint32_t mask);
void tk_segmented_set_on_change(tk_widget *w, tk_notify_cb cb,
                                void *ud);

/* Carousel (M7): the M3 carousel, multi-browse layout - one
 * large item with smaller items trailing, all with the 28dp
 * item corner radius and 8dp spacing; items resize as they
 * scroll (an item leaving the large slot shrinks to the small
 * width). Dragging scrolls with snap-to-item on release; a tap
 * selects. Item images are BORROWED pixels (the ListView rule);
 * the label, when given, is painted over the item's bottom
 * edge on the on-image fill. */
tk_widget *tk_carousel_new(void);
void tk_carousel_add(tk_widget *w, const char *label,
                     const uint32_t *px, int iw, int ih);
int tk_carousel_index(const tk_widget *w);
void tk_carousel_set_index(tk_widget *w, int idx);
void tk_carousel_set_on_select(tk_widget *w, tk_notify_cb cb,
                               void *ud);

/* Navigation rail (M7): the M3 navigation rail - a vertical
 * strip (80dp) of icon-over-label destinations with the pill
 * active indicator (secondary container) and optional badges.
 * In the WinUI mode it renders as the NavigationView compact
 * pane: full-cell subtle selection with the accent bar at the
 * left edge. Items carry borrowed icon pixels. */
tk_widget *tk_navrail_new(void);
void tk_navrail_add(tk_widget *w, const char *label,
                    const uint32_t *px, int iw, int ih);
void tk_navrail_set_item_badge(tk_widget *w, int idx, int count);
int tk_navrail_selected(const tk_widget *w);
void tk_navrail_select(tk_widget *w, int idx);
void tk_navrail_set_on_select(tk_widget *w, tk_notify_cb cb,
                              void *ud);

/* Ring (M7): the circular DETERMINATE progress indicator - a
 * track ring with an accent arc for the value fraction (the
 * linear bar's circular sibling; the Spinner stays the
 * indeterminate form). Size defaults to 48px. In the WinUI
 * mode this is the determinate ProgressRing. */
tk_widget *tk_ring_new(float value);
float tk_ring_value(const tk_widget *w);
void tk_ring_set_value(tk_widget *w, float v);
void tk_ring_set_size(tk_widget *w, int px);

/* AppBar (M7): the top app bar as a container kind - an
 * optional leading widget (navigation), a title, and trailing
 * action widgets (tk_add_child). M3 small app bar form: 64dp
 * container in the surface colour (surface-container when its
 * content scrolls - apps toggle with tk_appbar_set_scrolled).
 * WinUI mode renders the CommandBar form (layer fill, 4epx
 * radius on the actions, which stay ordinary buttons). */
tk_widget *tk_appbar_new(const char *title);
void tk_appbar_set_title(tk_widget *w, const char *title);
const char *tk_appbar_title(const tk_widget *w);
void tk_appbar_set_leading(tk_widget *w, tk_widget *child);
void tk_appbar_set_scrolled(tk_widget *w, bool scrolled);

/* Navigation drawer (M7): the M3 navigation drawer as an
 * overlay - a 360dp container (16dp far-side radius) over a
 * scrim in the modal form, or without the scrim in the standard
 * form; items are icon+label rows (56dp) with the pill active
 * indicator and optional badges, under an optional headline.
 * Selecting an item fires on_select (read the id with
 * tk_drawer_last_id) and closes a modal drawer. In the WinUI
 * mode this is the NavigationView expanded pane (layer fill,
 * subtle selection, accent bar). */
tk_widget *tk_drawer_new(const char *headline);
void tk_drawer_add_item(tk_widget *w, const char *label, int id,
                        const uint32_t *px, int iw, int ih);
void tk_drawer_add_separator(tk_widget *w);
void tk_drawer_set_item_badge(tk_widget *w, int id, int count);
void tk_drawer_select(tk_widget *w, int id);
int tk_drawer_selected(const tk_widget *w);  /* id or -1 */
int tk_drawer_last_id(const tk_widget *w);
void tk_drawer_set_on_select(tk_widget *w, tk_notify_cb cb,
                             void *ud);
void tk_drawer_show(tk_root *r, tk_widget *w, bool modal);
void tk_drawer_close(tk_root *r, tk_widget *w);
bool tk_drawer_is_shown(const tk_root *r, const tk_widget *w);
/* M9: the M3 STANDARD drawer form - a persistent panel inside
 * the layout (not an overlay): attach it as a child of a
 * horizontal box and it takes its measured width (<= 360dp)
 * and the full height, square-cornered, no scrim, selection
 * does not dismiss it. This is the form the specs' standard
 * measurements describe; tk_drawer_show remains the modal /
 * overlay form. */
void tk_drawer_attach(tk_widget *parent, tk_widget *w);
bool tk_drawer_is_standard(const tk_widget *w);

/* Bottom sheet / side sheet (M7): a container (vertical box)
 * shown as an overlay anchored to the bottom edge - the M3
 * bottom sheet: surface-container-low fill, 28dp top corners,
 * a centred drag handle, content inset from the top edge (72dp
 * of the window stays visible). The modal form adds the scrim
 * and traps input; dragging the handle down past a third of
 * the sheet (or tapping the scrim) dismisses it and fires
 * on_dismiss. Edge 1 makes it a side sheet: anchored right,
 * 360dp wide, full height, dismissed by a leftward handle drag.
 * In the WinUI mode sheets render as Flyout-style surfaces
 * (overlay radius, surface stroke, layer fill). */
tk_widget *tk_sheet_new(void);
void tk_sheet_set_edge(tk_widget *w, int edge); /* 0 bottom, 1 end */
int tk_sheet_edge(const tk_widget *w);
void tk_sheet_show(tk_root *r, tk_widget *w, bool modal);
void tk_sheet_close(tk_root *r, tk_widget *w);
bool tk_sheet_is_shown(const tk_root *r, const tk_widget *w);
void tk_sheet_set_on_dismiss(tk_widget *w, tk_notify_cb cb,
                             void *ud);

/* FAB menu (M7): the M3 FAB menu (speed dial) - a vertical
 * stack of labelled mini actions (56dp rows, full-round,
 * primary-container) that opens from a FAB. The menu is a
 * composed overlay: show it anchored to the bottom-right
 * corner (above where the FAB floats), pick an item (its own
 * callback fires, the menu hides) or tap the scrim sink to
 * dismiss. Pair it with a tk_fab_new() button whose click
 * toggles tk_fabmenu_show/hide. */
tk_widget *tk_fabmenu_new(void);
int tk_fabmenu_add_item(tk_widget *w, const uint32_t *px, int iw,
                        int ih, const char *label,
                        tk_notify_cb cb, void *ud);
void tk_fabmenu_show(tk_root *r, tk_widget *w);
void tk_fabmenu_hide(tk_root *r, tk_widget *w);
bool tk_fabmenu_is_shown(const tk_root *r, const tk_widget *w);

/* Split button (M7): the M3 split button as a composed pair -
 * a leading action button and a trailing chevron button (2dp
 * gap, the pair reading as one control) that pops the menu set
 * with tk_splitbutton_set_menu(). The returned widget is the
 * pair's box; tk_splitbutton_main/arrow reach the two buttons
 * for styling. M8: the chevron rotates 180 degrees as the menu
 * opens and back as it closes (the M3 split-button guideline),
 * animated on tk_anim through tk_canvas_draw_image_rot(); the
 * arrow button toggles the menu. */
tk_widget *tk_splitbutton_new(const char *label, tk_notify_cb cb,
                              void *ud);
void tk_splitbutton_set_menu(tk_widget *w, tk_widget *menu);
tk_widget *tk_splitbutton_main(const tk_widget *w);
tk_widget *tk_splitbutton_arrow(const tk_widget *w);

/* Shared clipboard (M7): the toolkit-level clipboard - the
 * Android ClipboardManager's one-primary-clip model reduced to
 * its text form (the only form this panel's editors exchange):
 * one clip, replaced wholesale by each set. Every TextField
 * and TextView created by the toolkit uses it by default (an
 * explicit tk_textfield_set_clipboard still overrides per
 * widget), so copy in one app widget pastes in another.
 * tk_clipboard_text() returns the clip's text or NULL when the
 * clipboard is empty; the pointer is owned by the clipboard
 * and stays valid until the next set/clear. */
void tk_clipboard_set_text(const char *text);
const char *tk_clipboard_text(void);
bool tk_clipboard_has_text(void);
void tk_clipboard_clear(void);

/* Localisation (M7): the Android resource model in miniature -
 * string CATALOGS registered per locale ("ja", "en", "ja-JP"),
 * a current locale, and lookup with the Android fallback chain
 * (exact locale, then language, then the default catalog, then
 * the key itself). Catalog entries are copied. The default
 * catalog is the first one registered (register the source
 * language first, as res/values is the default in Android).
 * tk_tr_plural picks "<key>.one" / "<key>.other" by the
 * locale's plural rule (Japanese: always other; English-like:
 * one iff count == 1) and falls back to the plain key. */
typedef struct { const char *key; const char *text; } tk_i18n_entry;
void tk_i18n_add_locale(const char *locale,
                        const tk_i18n_entry *entries, size_t n);
void tk_i18n_set_locale(const char *locale);
const char *tk_i18n_locale(void);
const char *tk_tr(const char *key);
const char *tk_tr_plural(const char *key, long count);
void tk_i18n_clear(void);

/* Accessibility semantics (M7): the AccessibilityNodeInfo /
 * UIAutomation model. Every widget exposes a semantics node:
 * a ROLE (derived from its kind unless overridden), a LABEL
 * (the content description if set, else the widget's text /
 * selected item / value text), a STATE description ("checked",
 * "selected", "on", "off", "disabled", or a percentage for
 * sliders and progress), its bounds and its enabled/focused
 * flags. tk_a11y_collect() walks a root's tree and overlays in
 * traversal (reading) order - the tree a screen reader or the
 * shell's assist layer consumes. Announcements model Android
 * live regions: tk_toast_show / tk_snackbar_show announce
 * automatically; apps announce with tk_a11y_announce(). */
typedef enum {
    TK_ROLE_NONE = 0, TK_ROLE_BUTTON, TK_ROLE_CHECKBOX,
    TK_ROLE_RADIO, TK_ROLE_SWITCH, TK_ROLE_SLIDER, TK_ROLE_TEXT,
    TK_ROLE_EDITTEXT, TK_ROLE_LIST, TK_ROLE_LISTITEM, TK_ROLE_TAB,
    TK_ROLE_IMAGE, TK_ROLE_HEADING, TK_ROLE_PROGRESS,
    TK_ROLE_MENU, TK_ROLE_MENUITEM, TK_ROLE_DIALOG, TK_ROLE_LINK
} tk_a11y_role;
typedef struct {
    tk_widget *widget;
    tk_a11y_role role;
    const char *label;    /* owned by the widget / toolkit */
    const char *state;    /* static string or value buffer */
    tk_rect bounds;
    bool enabled, focused, heading;
} tk_a11y_node;
void tk_widget_set_content_description(tk_widget *w,
                                       const char *text);
const char *tk_widget_content_description(const tk_widget *w);
void tk_widget_set_a11y_role(tk_widget *w, tk_a11y_role role);
void tk_widget_set_a11y_heading(tk_widget *w, bool heading);
size_t tk_a11y_collect(tk_root *r, tk_a11y_node *out, size_t cap);
const char *tk_a11y_role_name(tk_a11y_role role);  /* Android class */
const char *tk_a11y_uia_type(tk_a11y_role role);   /* UIA control type */
void tk_a11y_announce(tk_root *r, const char *text);
const char *tk_a11y_last_announcement(const tk_root *r);

/* Drag and drop (M7): the Android drag framework's shape - a
 * drag SOURCE declares a MIME type, an opaque payload (copied,
 * up to 64KB) and a label; the drag starts when a press on the
 * source (or a descendant) moves past the slop, showing the
 * drag shadow (the label pill that follows the pointer); a
 * DROP TARGET under the release point - the nearest ancestor
 * chain widget with an on_drop callback - receives the payload.
 * Releasing elsewhere cancels silently. While a drag runs,
 * clicks are suppressed, as with a drag-scroll. The ListView
 * row-reorder below is built on the same shadow rendering. */
typedef void (*tk_drop_cb)(tk_widget *target, void *ud,
                           const char *mime, const void *data,
                           size_t len, int x, int y);
void tk_widget_set_drag_source(tk_widget *w, const char *mime,
                               const void *data, size_t len,
                               const char *label);
void tk_widget_clear_drag_source(tk_widget *w);
void tk_widget_set_on_drop(tk_widget *w, tk_drop_cb cb, void *ud);
bool tk_drag_active(const tk_root *r);
/* ListView row drag (M7): with drag-reorder on, pressing a row
 * and moving vertically drags the row (its ghost follows the
 * finger, an insertion line marks the landing slot); releasing
 * performs tk_listview_move_row(). A plain tap still selects. */
void tk_listview_set_drag_reorder(tk_widget *w, bool on);
bool tk_listview_drag_reorder(const tk_widget *w);

/* Scaled JPEG decode diagnostics (M7): the peak intermediate
 * bytes the LAST tk_image_load_thumbnail() call held (the
 * decoded output of the scaled path, or the full decode of the
 * fallback path). Lets callers prove the memory claim of the
 * IDCT-scaled path on device-sized images. */
size_t tk_image_last_decode_peak(void);

/* ------------------------------------------------------------------ */
/* fbdev / evdev backends (new implementations; see tk_fb.c/tk_evdev.c)*/
/* ------------------------------------------------------------------ */
typedef struct tk_fb tk_fb;
tk_fb *tk_fb_open(const char *dev);          /* /dev/fb0 */
void tk_fb_close(tk_fb *fb);
int tk_fb_width(const tk_fb *fb);
int tk_fb_height(const tk_fb *fb);
/* Flush a canvas (must match fb size) converting RGBA8888 -> RGB565. */
bool tk_fb_flush(tk_fb *fb, tk_canvas *c);
/* Memory-backed framebuffer (M3): the same conversion/flush path as
 * the real device, for host testing and emulation. bpp: 16 or 32. */
tk_fb *tk_fb_open_mem(int w, int h, int bits_per_pixel);
const uint8_t *tk_fb_pixels(const tk_fb *fb); /* raw device memory */
int tk_fb_line_len(const tk_fb *fb);
/* Flush only the canvas's accumulated damage (see the canvas damage
 * API), converting just those rects to the device format. Returns
 * the number of device bytes written - 0 when nothing was damaged
 * or on size mismatch. On success the canvas damage is cleared;
 * tk_fb_flush() clears it too. */
size_t tk_fb_flush_damage(tk_fb *fb, tk_canvas *c);

typedef struct tk_evdev tk_evdev;
struct tk_kbd_config;  /* full definition in tk_kbd.h */
tk_evdev *tk_evdev_open(const char *dev);   /* /dev/input/eventX */
void tk_evdev_close(tk_evdev *e);
void tk_evdev_set_touch_cal(tk_evdev *e, int raw_max_x, int raw_max_y,
                            int screen_w, int screen_h);
/* Poll one event (non-blocking). Returns false when none pending.
 * out is filled in root coordinates using the touch calibration. */
bool tk_evdev_poll(tk_evdev *e, tk_event *out);
/* Keyboard configuration of the backend's keyboard layer (M2):
 * layout, repeat source/timing, long press. See tk_kbd.h. */
void tk_evdev_kbd_set_config(tk_evdev *e, const struct tk_kbd_config *cfg);
void tk_evdev_kbd_config(const tk_evdev *e, struct tk_kbd_config *out);
/* Device-less backend for tests/embedders (M2): create with
 * tk_evdev_new_virtual(), queue raw evdev key events with
 * tk_evdev_feed_key(), and drain them with tk_evdev_poll_at()
 * under a caller-controlled clock - the exact production path,
 * deterministically. feed_key returns false when an event is
 * already queued (drain it first). */
tk_evdev *tk_evdev_new_virtual(void);
bool tk_evdev_feed_key(tk_evdev *e, int code, int value, long at_ms);
bool tk_evdev_poll_at(tk_evdev *e, long at_ms, tk_event *out);

#endif /* TK_H */
