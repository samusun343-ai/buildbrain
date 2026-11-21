/* tk_core.c - widget tree, two-pass layout, event dispatch, focus.
 * Container measure/arrange lives here; leaf-widget behaviour lives
 * in tk_widgets.c (tk_leaf_*). See DESIGN sections 4-6. */
#include "tk_internal.h"
#include <stdio.h> /* snprintf: badge count formatting (M6) */
#include <math.h>  /* lrintf: scaled-overlay footprint (M20) */

/* implemented in tk_widgets.c */
tk_size tk_leaf_measure(tk_widget *w, tk_constraints c);
void tk_leaf_paint(tk_widget *w, tk_canvas *c, const tk_theme *t);
bool tk_leaf_event(tk_widget *w, tk_root *root, tk_event *ev);

/* Token values per UI-REDESIGN 2.2 (HIG-referenced). Contrast:
 * text_dim #5A5A61 is ~6.9:1 on surface and ~5.6:1 on surface_alt
 * (WCAG AA 4.5:1); border #8A8A92 is ~3.4:1 on surface (WCAG
 * 1.4.11's 3:1 for control identification); border_line is for
 * non-identifying separators only and may stay weak. */
const tk_theme tk_theme_light = {
    .bg = {245, 245, 247, 255}, .surface = {255, 255, 255, 255},
    .surface_alt = {232, 232, 236, 255},
    .desktop_bg = {226, 230, 236, 255},
    .text = {24, 24, 28, 255}, .text_dim = {90, 90, 97, 255},
    .accent = {0, 102, 204, 255}, .on_accent = {255, 255, 255, 255},
    .border = {138, 138, 146, 255}, .border_line = {216, 216, 222, 255},
    .focus = {0, 122, 255, 255},
    .selection = {178, 215, 255, 255}, .danger = {200, 40, 40, 255},
    .unit = 8, .radius = 4, .radius_overlay = 8,
    .control_h = 48, .row_h = 48, .row_h2 = 64, .font_px = 16,
    .font_display = 28, .font_title = 20, .font_heading = 16,
    .font_caption = 13,
    /* UI-STYLE-v2 tokens: classic behaviour (see tk.h) */
    .container = {178, 215, 255, 255},
    .radius_button = -1, .radius_card = 4, .radius_list = 0,
    .slider_track_h = 4, .field_filled = 0, .panel_margin = 0,
    .style = 0,
    /* M7 tokens: the inverse pairing IS the classic text/bg pair
     * (so pre-M7 transient pixels are unchanged); containers are
     * the classic greys; the WinUI-only roles get neutral values
     * (they are consulted only by style-2 paints). */
    .inverse_surface = {24, 24, 28, 255},
    .inverse_on_surface = {245, 245, 247, 255},
    .inverse_primary = {0, 102, 204, 255},
    .container_low = {255, 255, 255, 255},
    .container_high = {232, 232, 236, 255},
    .container_highest = {220, 220, 226, 255},
    .control_fill = {255, 255, 255, 255},
    .control_stroke = {138, 138, 146, 255},
    .card_bg = {255, 255, 255, 255},
    .layer = {255, 255, 255, 255},
    .smoke = {0, 0, 0, 82},
    .subtle = {232, 232, 236, 255},
    .sys_ok = {15, 123, 15, 255}, .sys_ok_bg = {223, 246, 221, 255},
    .sys_warn = {157, 93, 0, 255}, .sys_warn_bg = {255, 244, 206, 255},
    .sys_err_bg = {253, 231, 233, 255},
    .dark = 0,
};

/* UI-STYLE-v2 theme: One UI / Material You / ChromeOS direction.
 * Colours are the Material 3 baseline light scheme (seed #6750A4)
 * roles verbatim; shapes follow the M3 scale (buttons/chips full,
 * cards medium/large, sheets extra-large 28); the shelf floats
 * with an 8px margin (ChromeOS). See UI-STYLE-v2.md section 4. */
const tk_theme tk_theme_v2 = {
    .bg = {254, 247, 255, 255}, .surface = {255, 255, 255, 255},
    .surface_alt = {230, 224, 233, 255},
    .desktop_bg = {236, 230, 240, 255},
    .text = {29, 27, 32, 255}, .text_dim = {73, 69, 79, 255},
    .accent = {103, 80, 164, 255}, .on_accent = {255, 255, 255, 255},
    .border = {121, 116, 126, 255}, .border_line = {202, 196, 208, 255},
    .focus = {103, 80, 164, 255},
    .selection = {232, 222, 248, 255}, .danger = {179, 38, 30, 255},
    .unit = 8, .radius = 12, .radius_overlay = 28,
    /* M6 device-fit (AUDIT-toolkit.md SS5): the Material 48dp
     * minimum target (7.62mm) at this panel's ppi, not 48px
     * (6.5mm). M22: the panel premise was corrected to
     * 854x480 / ~179ppi (AUDIT SS20), so 48dp = 53.8px and
     * the touch floor is 54px (was 56px at the erroneous
     * 187ppi); the caption floor of 12sp is 13.4 -> 13px.
     * row_h2 stays row + one body line. The classic theme
     * keeps the historical values. */
    .control_h = 54, .row_h = 54, .row_h2 = 70, .font_px = 16,
    .font_display = 28, .font_title = 20, .font_heading = 16,
    .font_caption = 13,
    .container = {234, 221, 255, 255},
    .radius_button = 999, .radius_card = 24, .radius_list = 24,
    .slider_track_h = 16, .field_filled = 1, .panel_margin = 8,
    .style = 1,
    /* M7 tokens: M3 baseline light roles verbatim -
     * inverseSurface #322F35, inverseOnSurface #F5EFF7,
     * inversePrimary #D0BCFF; surfaceContainerLow #F7F2FA,
     * surfaceContainerHigh #ECE6F0, surfaceContainerHighest
     * #E6E0E9 (== surface_alt). Scrim is black at 32%. */
    .inverse_surface = {50, 47, 53, 255},
    .inverse_on_surface = {245, 239, 247, 255},
    .inverse_primary = {208, 188, 255, 255},
    /* M9: surfaceContainer #F3EDF7 (the baseline ladder's
     * middle step, recorded with the M7 ladder above) and the
     * M3 small top app bar height. M22: the 75px value is
     * retained as an envfit-band choice (REPORT SS8.3 lists
     * it with (a-10) as a re-confirmation item); it is not
     * a dp conversion at the corrected 179ppi premise. */
    .container_mid = {243, 237, 247, 255},
    .appbar_h = 75,
    .container_low = {247, 242, 250, 255},
    .container_high = {236, 230, 240, 255},
    .container_highest = {230, 224, 233, 255},
    /* M13: onPrimaryContainer #21005D, onSecondaryContainer
     * #1D192B (M3 baseline light roles) */
    .on_container = {33, 0, 93, 255},
    .on_selection = {29, 25, 43, 255},
    .control_fill = {255, 255, 255, 255},
    .control_stroke = {121, 116, 126, 255},
    .card_bg = {255, 255, 255, 255},
    .layer = {255, 255, 255, 255},
    .smoke = {0, 0, 0, 82},
    .subtle = {230, 224, 233, 255},
    .sys_ok = {15, 123, 15, 255}, .sys_ok_bg = {223, 246, 221, 255},
    .sys_warn = {157, 93, 0, 255}, .sys_warn_bg = {255, 244, 206, 255},
    .sys_err_bg = {253, 231, 233, 255},
    .dark = 0,
};

/* M7: the Material 3 baseline DARK scheme (seed #6750A4), the
 * dark counterpart of tk_theme_v2 - same shapes and device-fit
 * metrics, the dark colour roles verbatim: surface #141218,
 * surfaceContainerLow #1D1B20, Container #211F26, High #2B2930,
 * Highest #36343B, primary #D0BCFF, onPrimary #381E72,
 * primaryContainer #4F378B, onSurface #E6E0E9,
 * onSurfaceVariant #CAC4D0, outline #938F99, outlineVariant
 * #49454F, error #F2B8B5, inverseSurface #E6E0E9,
 * inverseOnSurface #322F35, inversePrimary #6750A4.
 * surface_alt carries surfaceContainerHighest (the filled-field
 * colour in dark, as in the light mapping). */
const tk_theme tk_theme_v2_dark = {
    .bg = {20, 18, 24, 255}, .surface = {20, 18, 24, 255},
    .surface_alt = {54, 52, 59, 255},
    .desktop_bg = {15, 13, 19, 255},
    .text = {230, 224, 233, 255}, .text_dim = {202, 196, 208, 255},
    .accent = {208, 188, 255, 255}, .on_accent = {56, 30, 114, 255},
    .border = {147, 143, 153, 255}, .border_line = {73, 69, 79, 255},
    .focus = {208, 188, 255, 255},
    /* M13 fix: selection carried primaryContainer's value
     * (#4F378B, duplicated in .container below); the M3 dark
     * secondaryContainer is #4A4458. */
    .selection = {74, 68, 88, 255}, .danger = {242, 184, 181, 255},
    .unit = 8, .radius = 12, .radius_overlay = 28,
    /* M22: floor 54 / caption 13, as in tk_theme_v2 */
    .control_h = 54, .row_h = 54, .row_h2 = 70, .font_px = 16,
    .font_display = 28, .font_title = 20, .font_heading = 16,
    .font_caption = 13,
    .container = {79, 55, 139, 255},
    .radius_button = 999, .radius_card = 24, .radius_list = 24,
    .slider_track_h = 16, .field_filled = 1, .panel_margin = 8,
    .style = 1,
    .inverse_surface = {230, 224, 233, 255},
    .inverse_on_surface = {50, 47, 53, 255},
    .inverse_primary = {103, 80, 164, 255},
    /* M9: surfaceContainer dark #211F26 (the ladder step named
     * in the M7 comment above); app bar 75px (envfit band,
     * see the light theme's M22 note) */
    .container_mid = {33, 31, 38, 255},
    .appbar_h = 75,
    .container_low = {29, 27, 32, 255},
    .container_high = {43, 41, 48, 255},
    .container_highest = {54, 52, 59, 255},
    /* M13: onPrimaryContainer #EADDFF, onSecondaryContainer
     * #E8DEF8 (M3 baseline dark roles) */
    .on_container = {234, 221, 255, 255},
    .on_selection = {232, 222, 248, 255},
    .control_fill = {33, 31, 38, 255},
    .control_stroke = {147, 143, 153, 255},
    .card_bg = {33, 31, 38, 255},
    .layer = {43, 41, 48, 255},
    .smoke = {0, 0, 0, 82},
    .subtle = {54, 52, 59, 255},
    .sys_ok = {108, 203, 95, 255}, .sys_ok_bg = {57, 61, 27, 255},
    .sys_warn = {252, 225, 0, 255}, .sys_warn_bg = {67, 53, 25, 255},
    .sys_err_bg = {68, 39, 38, 255},
    .dark = 1,
};

/* M7: WinUI / Fluent light (style 2). Token mapping (Fluent
 * name -> field): SolidBackgroundFillColorBase #F3F3F3 -> bg,
 * CardBackgroundFillColorDefault #B3FFFFFF -> card_bg (kept
 * translucent, as in XAML), SolidBackgroundFillColorTertiary
 * #F9F9F9 -> surface_alt / container_low, TextFillColorPrimary
 * #E4000000 -> text, TextFillColorSecondary #9E000000 ->
 * text_dim, AccentFillColorDefault #0067C0 (the Windows 11
 * default accent in light) -> accent, TextOnAccentFillColor-
 * Primary #FFFFFF -> on_accent, ControlStrokeColorDefault
 * #0F000000 -> control_stroke, ControlStrokeColorSecondary
 * #29000000 -> border, DividerStrokeColorDefault #0F000000 ->
 * border_line, ControlFillColorDefault #B3FFFFFF ->
 * control_fill, LayerFillColorDefault #80FFFFFF -> layer,
 * SmokeFillColorDefault #4D000000 -> smoke, SubtleFillColor-
 * Secondary #09000000 -> subtle, SystemFillColorSuccess
 * #0F7B0F / SuccessBackground #DFF6DD, Caution #9D5D00 /
 * #FFF4CE, Critical #C42B1C / CriticalBackground #FDE7E9.
 * Metrics are the device fit of the WinUI effective-pixel
 * values (see the report's conversion table). M22: the panel
 * premise was corrected to ~179ppi (AUDIT SS20) and the epx
 * factor re-derived by anchoring 40epx to the 48dp physical
 * size (53.8px): x1.344 (was x1.43 at the erroneous 187ppi).
 * Control height 32epx -> 43px, floored at the toolkit's
 * 48dp physical target of 54px; type ramp Caption 12 -> 16,
 * Body 14 -> 19, BodyLarge 18 -> 24, Subtitle 20 -> 27,
 * Title 28 -> 38; corner radii ControlCornerRadius 4epx
 * -> 6, OverlayCornerRadius 8 -> 11 (landed values kept,
 * AUDIT SS20 lists no change for them). */
const tk_theme tk_theme_winui = {
    .bg = {243, 243, 243, 255}, .surface = {255, 255, 255, 255},
    .surface_alt = {249, 249, 249, 255},
    .desktop_bg = {238, 238, 238, 255},
    .text = {0, 0, 0, 228}, .text_dim = {0, 0, 0, 158},
    .accent = {0, 103, 192, 255}, .on_accent = {255, 255, 255, 255},
    .border = {0, 0, 0, 41}, .border_line = {0, 0, 0, 15},
    .focus = {0, 103, 192, 255},
    .selection = {0, 0, 0, 15}, .danger = {196, 43, 28, 255},
    .unit = 8, .radius = 6, .radius_overlay = 11,
    .control_h = 54, .row_h = 54, .row_h2 = 73, .font_px = 19,
    .font_display = 38, .font_title = 27, .font_heading = 24,
    .font_caption = 16,
    .container = {0, 0, 0, 15},
    .radius_button = 6, .radius_card = 6, .radius_list = 6,
    .slider_track_h = 6, .field_filled = 0, .panel_margin = 0,
    .style = 2,
    .inverse_surface = {32, 32, 32, 255},
    .inverse_on_surface = {255, 255, 255, 255},
    .inverse_primary = {76, 194, 255, 255},
    .container_low = {249, 249, 249, 255},
    .container_high = {255, 255, 255, 255},
    .container_highest = {255, 255, 255, 255},
    .control_fill = {255, 255, 255, 179},
    .control_stroke = {0, 0, 0, 15},
    .card_bg = {255, 255, 255, 179},
    .layer = {255, 255, 255, 128},
    .smoke = {0, 0, 0, 77},
    .subtle = {0, 0, 0, 9},
    .sys_ok = {15, 123, 15, 255}, .sys_ok_bg = {223, 246, 221, 255},
    .sys_warn = {157, 93, 0, 255}, .sys_warn_bg = {255, 244, 206, 255},
    .sys_err_bg = {253, 231, 233, 255},
    /* M10: state fills / strong strokes / Attention, from the
     * Microsoft Learn Fluent token table (see tk.h): Control-
     * Fill Secondary #80F9F9F9 / Tertiary #4DF9F9F9 / Disabled
     * #4DF9F9F9, TextFillDisabled #5C000000, TextOnAccentFill-
     * Disabled #B2FFFFFF, ControlStrongStroke #9C000000 /
     * Disabled #37000000, ControlStrongFill #72000000 /
     * Disabled #51000000, AccentFillDisabled #37000000,
     * SystemFillAttention #FF005FB7 / AttentionBackground
     * #80F6F6F6. */
    .control_fill2 = {249, 249, 249, 128},
    .control_fill3 = {249, 249, 249, 77},
    .control_fill_dis = {249, 249, 249, 77},
    .text_dis = {0, 0, 0, 92},
    .on_accent_dis = {255, 255, 255, 178},
    .strong_stroke = {0, 0, 0, 156},
    .strong_stroke_dis = {0, 0, 0, 55},
    .strong_fill = {0, 0, 0, 114},
    .strong_fill_dis = {0, 0, 0, 81},
    .accent_dis = {0, 0, 0, 55},
    .sys_attn = {0, 95, 183, 255},
    .sys_attn_bg = {246, 246, 246, 128},
    /* M9: the bar form is the 48epx compact CommandBar /
     * TitleBar height at the device scale. M22: 48epx at
     * x1.344 is 64.5 -> 65px (was 69px at x1.43). WinUI has
     * no surfaceContainer role, so container_mid stays unset
     * and the scrolled colour falls back as before. */
    .appbar_h = 65,
    .dark = 0,
};

/* M7: WinUI / Fluent dark (style 2). SolidBackgroundFillColor-
 * Base #202020 -> bg, Tertiary #282828 -> surface_alt, Quaternary
 * #2C2C2C -> surface, BaseAlt #0A0A0A -> desktop_bg,
 * TextFillColorPrimary #FFFFFF -> text, Secondary #C5FFFFFF ->
 * text_dim, AccentFillColorDefault #4CC2FF -> accent,
 * TextOnAccentFillColorPrimary #000000 -> on_accent,
 * ControlFillColorDefault #0FFFFFFF -> control_fill,
 * ControlStrokeColorDefault #12FFFFFF -> control_stroke,
 * CardBackgroundFillColorDefault #0DFFFFFF -> card_bg,
 * LayerFillColorDefault #4C3A3A3A -> layer, SubtleFillColor-
 * Secondary #0FFFFFFF -> subtle, SystemFill Success #6CCB5F /
 * #393D1B, Caution #FCE100 / #433519, Critical #FF99A4 /
 * #442726. */
const tk_theme tk_theme_winui_dark = {
    .bg = {32, 32, 32, 255}, .surface = {44, 44, 44, 255},
    .surface_alt = {40, 40, 40, 255},
    .desktop_bg = {10, 10, 10, 255},
    .text = {255, 255, 255, 255}, .text_dim = {255, 255, 255, 197},
    .accent = {76, 194, 255, 255}, .on_accent = {0, 0, 0, 255},
    /* M22: border alphas 18/21 -> 27/30 (ladder note below) */
    .border = {255, 255, 255, 27}, .border_line = {255, 255, 255, 30},
    .focus = {76, 194, 255, 255},
    /* M22: selection alpha 15 -> 18 (see the ladder note at
     * container_highest below) */
    .selection = {255, 255, 255, 18}, .danger = {255, 153, 164, 255},
    .unit = 8, .radius = 6, .radius_overlay = 11,
    /* M22: floor 54 / type ramp at x1.344, as in the light form */
    .control_h = 54, .row_h = 54, .row_h2 = 73, .font_px = 19,
    .font_display = 38, .font_title = 27, .font_heading = 24,
    .font_caption = 16,
    .container = {255, 255, 255, 18},
    .radius_button = 6, .radius_card = 6, .radius_list = 6,
    .slider_track_h = 6, .field_filled = 0, .panel_margin = 0,
    .style = 2,
    .inverse_surface = {243, 243, 243, 255},
    .inverse_on_surface = {0, 0, 0, 228},
    .inverse_primary = {0, 103, 192, 255},
    .container_low = {40, 40, 40, 255},
    .container_high = {44, 44, 44, 255},
    /* M22 (ladder note): the M21 SS9.1 audit found this
     * theme's surface ladder collapsing to two RGB565
     * steps on the real panel (the display path is 565):
     * surface / selection / container / control_fill /
     * card_bg / subtle / container_high all quantized to
     * one value, the next group to another, so cards and
     * subtle fills read only by their borders. The white-
     * overlay alphas (and this one solid) are nudged the
     * minimum that puts each meaning-bearing step on its
     * own 565 gray step when blended over bg, keeping the
     * 24-bit look as close to the Fluent values as the
     * quantization allows. Effective grays over bg #202020
     * (565 step): bg 32, control_fill_dis 38 (alpha 7),
     * the low group 39-40 (unchanged), surface /
     * container_high 44, card_bg / selection / subtle /
     * container 48 (alpha 18), control_fill 52 (alpha
     * 23), control_fill2 54 (alpha 25), border /
     * border_line / control_stroke 56-58 (alphas 27/30),
     * container_highest 60 (this solid). Pinned by
     * test_tk5's 565 ladder checks. */
    .container_highest = {60, 60, 60, 255},
    .control_fill = {255, 255, 255, 23},
    .control_stroke = {255, 255, 255, 27},
    .card_bg = {255, 255, 255, 18},
    .layer = {58, 58, 58, 76},
    .smoke = {0, 0, 0, 77},
    .subtle = {255, 255, 255, 18},
    .sys_ok = {108, 203, 95, 255}, .sys_ok_bg = {57, 61, 27, 255},
    .sys_warn = {252, 225, 0, 255}, .sys_warn_bg = {67, 53, 25, 255},
    .sys_err_bg = {68, 39, 38, 255},
    /* M10: the dark counterparts (same source as the light
     * set): ControlFill Secondary #15FFFFFF / Tertiary
     * #08FFFFFF / Disabled #0BFFFFFF, TextFillDisabled
     * #5DFFFFFF, TextOnAccentFillDisabled #80000000,
     * ControlStrongStroke #9AFFFFFF / Disabled #28FFFFFF,
     * ControlStrongFill #8BFFFFFF / Disabled #3FFFFFFF,
     * AccentFillDisabled #28FFFFFF, SystemFillAttention
     * #FF60CDFF / AttentionBackground #08FFFFFF. */
    /* M22: alphas 21 -> 25 and 11 -> 7 (ladder note above) */
    .control_fill2 = {255, 255, 255, 25},
    .control_fill3 = {255, 255, 255, 8},
    .control_fill_dis = {255, 255, 255, 7},
    .text_dis = {255, 255, 255, 93},
    .on_accent_dis = {0, 0, 0, 128},
    .strong_stroke = {255, 255, 255, 154},
    .strong_stroke_dis = {255, 255, 255, 40},
    .strong_fill = {255, 255, 255, 139},
    .strong_fill_dis = {255, 255, 255, 63},
    .accent_dis = {255, 255, 255, 40},
    .sys_attn = {96, 205, 255, 255},
    .sys_attn_bg = {255, 255, 255, 8},
    .appbar_h = 65, /* M9: 48epx compact bar; M22: 65px at x1.344 */
    .dark = 1,
};

static const tk_theme *tk_default_override;

const tk_theme *tk_theme_default(void)
{
    return tk_default_override ? tk_default_override : &tk_theme_light;
}

void tk_theme_set_default(const tk_theme *t)
{
    tk_default_override = t;
}

/* M5 density tokens: derive a roomier (or identical) theme from
 * a base theme. Level 0 is the base itself; level 1 scales the
 * row/control heights by 5/4, level 2 by 3/2 - the "窮屈"
 * (cramped) remedy an app or the shell can apply in ONE place
 * (tk_theme_set_default(&derived)) instead of every screen
 * hand-tuning its rows. The spacing unit stays on the 8 grid
 * (rhythm is not density) and font sizes are not scaled
 * (legibility is a style decision). Returns the theme by value;
 * keep it in a static/global if you pass it to
 * tk_theme_set_default(). */
tk_theme tk_theme_density(const tk_theme *base, int level)
{
    tk_theme out = base ? *base : tk_theme_light;
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    out.row_h     = out.row_h     * (4 + level) / 4;
    out.row_h2    = out.row_h2    * (4 + level) / 4;
    out.control_h = out.control_h * (4 + level) / 4;
    return out;
}

int tk_theme_style_px(const tk_theme *t, tk_text_style s)
{
    if (!t) t = tk_theme_default();
    switch (s) {
    case TK_TEXT_DISPLAY: return t->font_display;
    case TK_TEXT_TITLE:   return t->font_title;
    case TK_TEXT_HEADING: return t->font_heading;
    case TK_TEXT_CAPTION: return t->font_caption;
    default:              return t->font_px;
    }
}

bool tk_theme_style_bold(tk_text_style s)
{
    /* UI-REDESIGN 2.2: title and heading are the bold styles */
    return s == TK_TEXT_TITLE || s == TK_TEXT_HEADING;
}

void tk_widget_set_text_style(tk_widget *w, tk_text_style s)
{
    if (w && w->text_style != (int)s) {
        w->text_style = (int)s;
        tk_widget_invalidate(w);
    }
}

tk_text_style tk_widget_text_style(const tk_widget *w)
{
    return w ? (tk_text_style)w->text_style : TK_TEXT_BODY;
}

char *tk_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

bool tk_rect_contains(tk_rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

tk_rect tk_rect_intersect(tk_rect a, tk_rect b)
{
    int x = a.x > b.x ? a.x : b.x;
    int y = a.y > b.y ? a.y : b.y;
    int x2 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y2 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (x2 < x) x2 = x;
    if (y2 < y) y2 = y;
    return (tk_rect){x, y, x2 - x, y2 - y};
}

void tk_invoke(tk_notify_cb cb, tk_widget *w, void *ud)
{
    if (cb) cb(w, ud);
}

/* ---- damage store (M3) ---------------------------------------- */

static tk_rect dmg_union(tk_rect a, tk_rect b)
{
    int x = a.x < b.x ? a.x : b.x;
    int y = a.y < b.y ? a.y : b.y;
    int x2 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y2 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (tk_rect){x, y, x2 - x, y2 - y};
}

static bool dmg_touch(tk_rect a, tk_rect b)
{
    return a.x <= b.x + b.w && b.x <= a.x + a.w &&
           a.y <= b.y + b.h && b.y <= a.y + a.h;
}

void tk_damage_add(tk_damage_store *d, tk_rect r)
{
    if (!d || r.w <= 0 || r.h <= 0) return;
    for (;;) {
        bool merged = false;
        for (size_t i = 0; i < d->n; i++) {
            if (dmg_touch(r, d->rects[i])) {
                r = dmg_union(r, d->rects[i]);
                memmove(d->rects + i, d->rects + i + 1,
                        (d->n - i - 1) * sizeof(tk_rect));
                d->n--;
                merged = true;
                break;
            }
        }
        if (!merged) break;
    }
    if (d->n >= 64) {
        for (size_t i = 0; i < d->n; i++) r = dmg_union(r, d->rects[i]);
        d->rects[0] = r;
        d->n = 1;
        return;
    }
    if (d->n >= d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 8;
        tk_rect *na = realloc(d->rects, nc * sizeof(*na));
        if (!na) { d->full = true; return; }
        d->rects = na; d->cap = nc;
    }
    d->rects[d->n++] = r;
}

void tk_damage_clear(tk_damage_store *d)
{
    if (!d) return;
    d->n = 0;
    d->full = false;
}

void tk_root_invalidate_rect(tk_root *r, tk_rect rect)
{
    if (r) tk_damage_add(&r->dmg, rect);
}

void tk_root_invalidate(tk_root *r, tk_widget *w)
{
    if (r && w) tk_damage_add(&r->dmg, w->rect);
}

size_t tk_root_damage_count(const tk_root *r)
{
    if (!r) return 0;
    return r->dmg.full ? 1 : r->dmg.n;
}

/* M5: the root that owns w's tree, reached through the damage
 * store hung on the tree's top widget (or on w itself when w is
 * an overlay). NULL for a detached tree. */
tk_root *tk_widget_root(const tk_widget *w)
{
    if (!w) return NULL;
    for (const tk_widget *p = w; p; p = p->parent)
        if (p->dmg) return p->dmg->owner;
    if (w->dmg) return w->dmg->owner;
    return NULL;
}

/* M5: content under a ScrollView changed: every ScrollView that
 * strictly contains w must not take the shift fast path until it
 * has been fully repainted. Called from the invalidation path
 * (the invalidated widget is the content, never the SV itself -
 * invalidating an SV directly is the scroll/refresh signal) and
 * from event dispatch (a handled press changes widget state
 * without going through a setter). */
void tk_sv_mark_dirty_from(tk_widget *w)
{
    if (!w) return;
    for (tk_widget *p = w->parent; p; p = p->parent)
        if (p->kind == TK_W_SCROLLVIEW) p->sv_dirty = true;
}

void tk_widget_invalidate(tk_widget *w)
{
    if (!w) return;
    tk_sv_mark_dirty_from(w);
    for (tk_widget *p = w; p; p = p->parent) {
        if (p->dmg) {
            tk_damage_add(p->dmg, w->rect);
            return;
        }
    }
}

void tk_widget_set_tooltip(tk_widget *w, const char *text)
{
    if (!w) return;
    free(w->tooltip);
    w->tooltip = text ? tk_strdup(text) : NULL;
}

const char *tk_widget_tooltip(const tk_widget *w)
{
    return w ? w->tooltip : NULL;
}

/* ---------------------------------------------------------------- */
/* Lifecycle                                                         */
/* ---------------------------------------------------------------- */

tk_widget *tk_widget_new(tk_widget_kind kind)
{
    tk_widget *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->kind = kind;
    w->visible = true;
    w->enabled = true;
    w->sel = -1;
    w->marked = -1;
    w->backdrop_radius = -1; /* theme radius_overlay */
    w->btn_radius = -1;      /* theme radius */
    w->btn_text_align = 1;   /* M6: center (pre-M6 behaviour) */
    w->float_anchor = -1;    /* M6: not a floating overlay */
    w->bb_selected = -1;     /* M6: bottombar, no selection */
    w->lv_drag_row = -1;     /* M7: no row drag in progress */
    w->paint_alpha = 255;    /* M17: menus fade from opaque */
    w->paint_scale = 1000;   /* M20: rest form (menus scale) */
    w->seg_anim_idx = -1;    /* M17: no segment check fading */
    return w;
}

static void free_subtree(tk_widget *w)
{
    tk_m7_free_widget(w); /* M7: field-owned state of the new kinds */
    /* M5: a widget being freed must not be reached by a running
     * animation afterwards (the switch knob / overlay slide
     * callbacks borrow the widget). Cancel through the root that
     * owns the animation list; detached trees have no root and
     * therefore no running animations to cancel. */
    if (w->anim_handle) {
        tk_root *r = tk_widget_root(w);
        if (r) tk_anim_cancel(w->anim_handle);
        w->anim_handle = NULL;
    }
    if (w->prog_anim) { /* M6: indeterminate sweep */
        tk_root *r = tk_widget_root(w);
        if (r) tk_anim_cancel(w->prog_anim);
        w->prog_anim = NULL;
    }
    if (w->ripple_anim) { /* M17: press ripple */
        tk_root *r = tk_widget_root(w);
        if (r) tk_anim_cancel(w->ripple_anim);
        w->ripple_anim = NULL;
    }
    if (w->face_anim) { /* M17: WinUI brush transition */
        tk_root *r = tk_widget_root(w);
        if (r) tk_anim_cancel(w->face_anim);
        w->face_anim = NULL;
    }
    if (w->fade_anim) { /* M17: menu fade */
        tk_root *r = tk_widget_root(w);
        if (r) tk_anim_cancel(w->fade_anim);
        w->fade_anim = NULL;
    }
    if (w->seg_anim) { /* M17: segmented check fade */
        tk_root *r = tk_widget_root(w);
        if (r) tk_anim_cancel(w->seg_anim);
        w->seg_anim = NULL;
    }
    for (size_t i = 0; i < w->n_children; i++) free_subtree(w->children[i]);
    free(w->children);
    free(w->text);
    free(w->tooltip);
    free(w->placeholder);
    free(w->support_text);   /* M6 */
    free(w->tab_badges);     /* M6 */
    if (w->buffer) tc_free(w->buffer);
    for (size_t i = 0; i < w->n_items; i++) free(w->items[i]);
    free(w->items);
    free(w->item_flags); /* M5 */
    for (size_t i = 0; i < w->n_subitems; i++) free(w->subitems[i]);
    free(w->subitems);
    free(w->item_icons); /* array only: pixels are borrowed */
    if (w->ovf_menu) free_subtree(w->ovf_menu);
    for (size_t i = 0; i < w->n_tab_titles; i++) free(w->tab_titles[i]);
    free(w->tab_titles);
    for (size_t i = 0; i < w->n_menu_items; i++) {
        free(w->menu_items[i].label);
        free(w->menu_items[i].trailing); /* M7 */
        if (w->menu_items[i].submenu &&
            w->menu_items[i].submenu != w->open_sub)
            free_subtree(w->menu_items[i].submenu);
    }
    if (w->open_sub) free_subtree(w->open_sub);
    free(w->menu_items);
    for (size_t i = 0; i < w->n_bar; i++) {
        free(w->bar_titles[i]);
        /* bar menus are owned by the bar */
        if (w->bar_menus[i]) free_subtree(w->bar_menus[i]);
    }
    free(w->bar_titles);
    free(w->bar_menus);
    for (size_t i = 0; i < w->n_nodes; i++) free(w->nodes[i].label);
    free(w->nodes);
    free(w->image_px);
    if (w->kind == TK_W_OSK) tk_osk_free_state(w->ext);
    free(w->ext);
    free(w);
}

void tk_widget_free(tk_widget *w)
{
    if (!w) return;
    if (w->parent) tk_remove_child(w->parent, w);
    free_subtree(w);
}

tk_widget_kind tk_widget_kind_of(const tk_widget *w) { return w->kind; }

void tk_add_child(tk_widget *parent, tk_widget *child)
{
    if (!parent || !child || child->parent) return;
    if (parent->n_children >= parent->cap_children) {
        size_t nc = parent->cap_children ? parent->cap_children * 2 : 4;
        tk_widget **na = realloc(parent->children, nc * sizeof(*na));
        if (!na) return;
        parent->children = na; parent->cap_children = nc;
    }
    parent->children[parent->n_children++] = child;
    child->parent = parent;
}

void tk_remove_child(tk_widget *parent, tk_widget *child)
{
    if (!parent || !child) return;
    for (size_t i = 0; i < parent->n_children; i++) {
        if (parent->children[i] == child) {
            memmove(parent->children + i, parent->children + i + 1,
                    (parent->n_children - i - 1) * sizeof(*parent->children));
            parent->n_children--;
            child->parent = NULL;
            return;
        }
    }
}

tk_widget *tk_parent(const tk_widget *w) { return w->parent; }
size_t tk_child_count(const tk_widget *w) { return w->n_children; }
tk_widget *tk_child_at(const tk_widget *w, size_t i)
{
    return i < w->n_children ? w->children[i] : NULL;
}
void tk_set_visible(tk_widget *w, bool v)
{
    if (!w || w->visible == v) return;
    /* M5: damage the current rect either way - hiding must erase
     * the widget's pixels, showing must draw them. Callers that
     * re-layout right after get full damage from layout anyway. */
    tk_widget_invalidate(w);
    w->visible = v;
    tk_widget_invalidate(w);
}
bool tk_is_visible(const tk_widget *w) { return w->visible; }
void tk_set_enabled(tk_widget *w, bool e)
{
    if (!w || w->enabled == e) return;
    w->enabled = e;
    tk_widget_invalidate(w); /* M5: dimmed state is painted state */
}
bool tk_is_enabled(const tk_widget *w) { return w->enabled; }
void tk_set_can_focus(tk_widget *w, bool f) { w->can_focus = f; }
bool tk_can_focus(const tk_widget *w) { return w->can_focus; }
void tk_set_flex(tk_widget *w, int flex) { w->flex = flex < 0 ? 0 : flex; }
int tk_get_flex(const tk_widget *w) { return w->flex; }
void tk_set_fixed_rect(tk_widget *w, tk_rect r) { w->fixed_rect = r; }

/* ---------------------------------------------------------------- */
/* Containers                                                        */
/* ---------------------------------------------------------------- */

tk_widget *tk_box_new(tk_dir dir, int spacing, int padding)
{
    tk_widget *w = tk_widget_new(TK_W_BOX);
    if (w) { w->dir = dir; w->spacing = spacing; w->padding = padding;
             w->cross_align = TK_ALIGN_STRETCH; }
    return w;
}

void tk_box_set_align(tk_widget *box, tk_align a) { box->cross_align = a; }

void tk_box_set_wrap(tk_widget *box, bool wrap) { box->wrap = wrap; }

tk_widget *tk_fixed_new(void) { return tk_widget_new(TK_W_FIXED); }

void tk_grid_set_span(tk_widget *child, int col_span, int row_span)
{
    if (!child) return;
    child->span_cols = col_span < 1 ? 1 : col_span;
    child->span_rows = row_span < 1 ? 1 : row_span;
    tk_widget_invalidate(child);
}

void tk_grid_span(const tk_widget *child, int *col_span, int *row_span)
{
    if (!child) return;
    if (col_span) *col_span = child->span_cols > 1 ? child->span_cols : 1;
    if (row_span) *row_span = child->span_rows > 1 ? child->span_rows : 1;
}

tk_widget *tk_grid_new(int cols, int h_spacing, int v_spacing, int padding)
{
    tk_widget *w = tk_widget_new(TK_W_GRID);
    if (w) {
        w->cols = cols > 0 ? cols : 1;
        w->spacing = h_spacing;
        w->vspacing = v_spacing;
        w->padding = padding;
    }
    return w;
}

/* ---------------------------------------------------------------- */
/* Measure / arrange                                                 */
/* ---------------------------------------------------------------- */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static tk_size apply_constraints(tk_size s, tk_constraints c)
{
    s.w = clampi(s.w, c.min_w, c.max_w);
    s.h = clampi(s.h, c.min_h, c.max_h);
    return s;
}

static int grid_place(tk_widget *w, int cols);       /* M6, below */
static int grid_span_cols(const tk_widget *ch, int cols);
static int grid_span_rows(const tk_widget *ch);

/* M9: the dialog's content padding is a spec dimension, not
 * an app preference: M3 basic dialog 24dp, WinUI
 * ContentDialog 24epx. At the corrected panel premise
 * (~179.2ppi, AUDIT SS20) those convert to 26.9px and
 * 32.3px (x1.344); the implemented 28 / 34 are the M9
 * values, retained (REPORT-toolkit-m23.md). The
 * classic theme keeps the historical 16. Synced at measure
 * time (the M8 rule: widgets read the current default theme
 * at measure/paint time) so layout, arrange and the title
 * paint all see the same value. */
static int dialog_theme_pad(const tk_theme *t)
{
    if (t->style == 1) return 28;
    if (t->style == 2) return 34;
    return -1;
}

/* M18: the dialog's title band height. The basic form's band
 * is the control height in every mode (the M9 form). The M3
 * full-screen dialog's header is a spec dimension of its
 * own: 56dp (m3.material.io dialogs page, fetched M9),
 * 62.7px at the corrected ~179.2ppi premise; the
 * implemented 65px band is the M9/M18 value, retained
 * (REPORT-toolkit-m23.md) - so the full-screen form in the
 * M3 mode gets the taller band. Measure, arrange and paint
 * all read this
 * one function so the three never disagree. */
int tk_dialog_title_h(const tk_widget *w, const tk_theme *t)
{
    if (w && w->dlg_fullscreen && t && t->style == 1) return 65;
    return t ? t->control_h : 54; /* M22: the touch floor */
}

tk_size tk_measure(tk_widget *w, tk_constraints c)
{
    if (!w->visible) { w->desired = (tk_size){0, 0}; return w->desired; }
    w->avail_w = c.max_w;
    if (w->kind == TK_W_DIALOG) {
        int p = dialog_theme_pad(tk_theme_default());
        if (p > 0) w->padding = p;
    }
    tk_size s;
    if (w->kind == TK_W_BOX || w->kind == TK_W_DIALOG ||
        w->kind == TK_W_CARD || w->kind == TK_W_SHEET) {
        bool vert = w->dir == TK_DIR_VERTICAL;
        int main = 0, cross = 0;
        int n = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible) continue;
            tk_constraints cc = {0, vert ? c.max_w - 2 * w->padding : TK_UNLIMITED,
                                 0, vert ? TK_UNLIMITED : c.max_h - 2 * w->padding};
            if (cc.max_w < 0) cc.max_w = 0;
            if (cc.max_h < 0) cc.max_h = 0;
            tk_size cs = tk_measure(ch, cc);
            main += vert ? cs.h : cs.w;
            int ccross = vert ? cs.w : cs.h;
            if (ccross > cross) cross = ccross;
            n++;
        }
        if (n > 1) main += w->spacing * (n - 1);
        main += 2 * w->padding;
        cross += 2 * w->padding;
        s = vert ? (tk_size){cross, main} : (tk_size){main, cross};
        if (!vert && w->wrap && c.max_w < TK_UNLIMITED / 2) {
            /* horizontal wrap: simulate rows at the offered width */
            int inner_w = c.max_w - 2 * w->padding;
            int cur_w = 0, row_h = 0, total_h = 0, max_row_w = 0;
            for (size_t i = 0; i < w->n_children; i++) {
                tk_widget *ch = w->children[i];
                if (!ch->visible) continue;
                int cw = ch->desired.w;
                if (cur_w > 0 && cur_w + w->spacing + cw > inner_w) {
                    total_h += row_h + w->spacing;
                    if (cur_w > max_row_w) max_row_w = cur_w;
                    cur_w = 0; row_h = 0;
                }
                cur_w += (cur_w ? w->spacing : 0) + cw;
                if (ch->desired.h > row_h) row_h = ch->desired.h;
            }
            total_h += row_h;
            if (cur_w > max_row_w) max_row_w = cur_w;
            s = (tk_size){ max_row_w + 2 * w->padding,
                           total_h + 2 * w->padding };
            if (s.w > c.max_w) s.w = c.max_w;
        }
        if (w->kind == TK_W_DIALOG)
            s.h += tk_dialog_title_h(w, tk_theme_default()); /* title bar zone */
    } else if (w->kind == TK_W_GRID) {
        int cols = w->cols > 0 ? w->cols : 1;
        int tw = 0, th = 0;
        for (size_t i = 0; i < w->n_children; i++)
            (void)tk_measure(w->children[i],
                             (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
        int rows = grid_place(w, cols);
        for (int cx = 0; cx < cols; cx++) {
            int cw = 0;
            for (size_t i = 0; i < w->n_children; i++) {
                tk_widget *ch = w->children[i];
                if (!ch->visible || ch->cell_x != cx) continue;
                int cs = grid_span_cols(ch, cols);
                int per = (ch->desired.w + cs - 1) / cs;
                if (per > cw) cw = per;
            }
            tw += cw;
        }
        for (int ry = 0; ry < rows; ry++) {
            int rh = 0;
            for (size_t i = 0; i < w->n_children; i++) {
                tk_widget *ch = w->children[i];
                if (!ch->visible || ch->cell_y != ry) continue;
                int rs = grid_span_rows(ch);
                int per = (ch->desired.h + rs - 1) / rs;
                if (per > rh) rh = per;
            }
            th += rh;
        }
        if (cols > 1) tw += w->spacing * (cols - 1);
        if (rows > 1) th += w->vspacing * (rows - 1);
        s = (tk_size){ tw + 2 * w->padding, th + 2 * w->padding };
    } else if (w->kind == TK_W_TABS) {
        int bar_h = TK_TAB_BAR_H;
        int mw = 0, mh = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            tk_constraints cc = {0, c.max_w, 0,
                                 c.max_h > bar_h ? c.max_h - bar_h : 0};
            tk_size cs = tk_measure(ch, cc);
            if (cs.w > mw) mw = cs.w;
            if (cs.h > mh) mh = cs.h;
        }
        /* the tab bar itself needs room for the titles */
        int titles_w = 0;
        for (size_t i = 0; i < w->n_tab_titles; i++)
            titles_w += tk_text_width(tk_theme_default(), w->tab_titles[i]) + 24;
        if (titles_w > mw) mw = titles_w;
        s = (tk_size){ mw, mh + bar_h };
    } else if (w->kind == TK_W_FIXED) {
        int mw = 0, mh = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            (void)tk_measure(ch, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
            int ex = ch->fixed_rect.x + ch->fixed_rect.w;
            int ey = ch->fixed_rect.y + ch->fixed_rect.h;
            if (ex > mw) mw = ex;
            if (ey > mh) mh = ey;
        }
        s = (tk_size){mw, mh};
    } else if (w->kind == TK_W_SCROLLVIEW) {
        tk_size cs = {0, 0};
        if (w->n_children) {
            cs = tk_measure(w->children[0],
                            (tk_constraints){0, c.max_w, 0, TK_UNLIMITED});
        }
        /* the view itself wants only the viewport; content size is
         * the child's desired size (used by arrange for scrolling) */
        s = (tk_size){cs.w, cs.h};
        w->desired = apply_constraints(s, c);
        return w->desired;
    } else if (w->kind == TK_W_APPBAR) {
        /* M7: leading widget + title + trailing actions */
        const tk_theme *t = tk_theme_default();
        int mw = 2 * t->unit;
        size_t start = 0;
        if (w->appbar_leading && w->n_children) {
            tk_size cs = tk_measure(w->children[0],
                (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
            mw += cs.w + t->unit;
            start = 1;
        }
        if (w->text)
            mw += tk_text_width_st(t, TK_TEXT_TITLE, w->text) + 2 * t->unit;
        for (size_t i = start; i < w->n_children; i++) {
            tk_size cs = tk_measure(w->children[i],
                (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
            mw += cs.w + 4;
        }
        /* M9: the bar's own height is a spec dimension (M3
         * small 64dp / WinUI 48epx compact), carried by the
         * theme's appbar_h; themes without it keep control_h */
        s = (tk_size){ mw, t->appbar_h > 0 ? t->appbar_h
                                           : t->control_h };
    } else {
        s = tk_leaf_measure(w, c);
    }
    if (w->kind == TK_W_BUTTON && w->n_children) {
        /* M7: button children (trailing row) add their width to
         * the button's desired size; they are measured here
         * because the leaf measure does not descend. */
        int cw = 0, chh = 0, nvis = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible) continue;
            tk_size cs = tk_measure(ch,
                (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
            cw += cs.w;
            if (cs.h > chh) chh = cs.h;
            nvis++;
        }
        if (nvis > 1) cw += 4 * (nvis - 1);
        if (nvis) {
            s.w += cw + 4;
            if (chh + 8 > s.h) s.h = chh + 8;
        }
    }
    if (w->min_w > s.w) s.w = w->min_w;
    if (w->min_h > s.h) s.h = w->min_h;
    w->desired = apply_constraints(s, c);
    return w->desired;
}

void tk_widget_set_min_size(tk_widget *w, int min_w, int min_h)
{
    if (!w) return;
    w->min_w = min_w;
    w->min_h = min_h;
    tk_widget_invalidate(w);
}

tk_size tk_w_measure(tk_widget *w, tk_constraints c) { return tk_measure(w, c); }

/* Grid placement (M6 spans): cursor-based row-major flow of the
 * visible children into `cols` columns, honouring per-child
 * column/row spans (the CSS grid / GridLayout default: the
 * cursor only moves forward, a span that does not fit in the
 * rest of the row wraps, holes behind the cursor are not
 * back-filled). Stores cell_x/cell_y on each child (invisible
 * children get -1) and returns the number of rows used. With
 * all spans 1 this is exactly the pre-M6 index arithmetic. */
static int grid_place(tk_widget *w, int cols)
{
    if (cols < 1) cols = 1;
    size_t n = w->n_children;
    if (!n) return 0;
    int max_rows = 1;
    for (size_t i = 0; i < n; i++) {
        int rs = w->children[i]->span_rows;
        max_rows += rs > 1 ? rs : 1;
    }
    unsigned char *occ = calloc((size_t)cols * (size_t)max_rows, 1);
    if (!occ) { /* fallback: plain row-major, spans ignored */
        int rows = 0;
        for (size_t i = 0; i < n; i++) {
            w->children[i]->cell_x = (int)(i % (size_t)cols);
            w->children[i]->cell_y = (int)(i / (size_t)cols);
            rows = w->children[i]->cell_y + 1;
        }
        return rows;
    }
    int rows_used = 0;
    int cur_x = 0, cur_y = 0; /* M6: flow cursor */
    for (size_t i = 0; i < n; i++) {
        tk_widget *ch = w->children[i];
        if (!ch->visible) { ch->cell_x = -1; ch->cell_y = -1; continue; }
        int cs = ch->span_cols > 1 ? ch->span_cols : 1;
        if (cs > cols) cs = cols;
        int rs = ch->span_rows > 1 ? ch->span_rows : 1;
        int px = -1, py = -1;
        for (int y = cur_y; y + rs <= max_rows && px < 0; y++) {
            for (int x = (y == cur_y ? cur_x : 0); x + cs <= cols; x++) {
                bool fits = true;
                for (int dy = 0; dy < rs && fits; dy++)
                    for (int dx = 0; dx < cs; dx++)
                        if (occ[(size_t)(y + dy) * (size_t)cols +
                                (size_t)(x + dx)]) { fits = false; break; }
                if (fits) { px = x; py = y; break; }
            }
        }
        if (px < 0) { px = 0; py = rows_used; } /* cannot happen */
        for (int dy = 0; dy < rs; dy++)
            for (int dx = 0; dx < cs; dx++)
                occ[(size_t)(py + dy) * (size_t)cols + (size_t)(px + dx)] = 1;
        ch->cell_x = px; ch->cell_y = py;
        if (py + rs > rows_used) rows_used = py + rs;
        cur_y = py; cur_x = px + cs;
    }
    free(occ);
    return rows_used;
}

static int grid_span_cols(const tk_widget *ch, int cols)
{
    int cs = ch->span_cols > 1 ? ch->span_cols : 1;
    return cs > cols ? cols : cs;
}
static int grid_span_rows(const tk_widget *ch)
{
    return ch->span_rows > 1 ? ch->span_rows : 1;
}

/* Compute grid track sizes from children's desired sizes, then
 * stretch (equal shares of the surplus) or shrink (proportional)
 * them to fit the arranged rect. Shared by measure-less arrange.
 * A spanning child contributes its desired size shared across
 * the tracks it covers (per-track ceil share). */
static void grid_tracks(tk_widget *w, tk_rect r, int *colw, int *rowh,
                        int cols, int rows)
{
    for (int cx = 0; cx < cols; cx++) {
        int cw = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible || ch->cell_x != cx) continue;
            int cs = grid_span_cols(ch, cols);
            int per = (ch->desired.w + cs - 1) / cs;
            if (per > cw) cw = per;
        }
        colw[cx] = cw;
    }
    for (int ry = 0; ry < rows; ry++) {
        int rh = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible || ch->cell_y != ry) continue;
            int rs = grid_span_rows(ch);
            int per = (ch->desired.h + rs - 1) / rs;
            if (per > rh) rh = per;
        }
        rowh[ry] = rh;
    }
    int total_w = 0, total_h = 0;
    for (int cx = 0; cx < cols; cx++) total_w += colw[cx];
    for (int ry = 0; ry < rows; ry++) total_h += rowh[ry];
    int avail_w = r.w - 2 * w->padding - (cols > 1 ? w->spacing * (cols - 1) : 0);
    int avail_h = r.h - 2 * w->padding - (rows > 1 ? w->vspacing * (rows - 1) : 0);
    if (avail_w >= 0 && total_w > 0 && avail_w != total_w) {
        if (avail_w > total_w) {
            int extra = avail_w - total_w, per = extra / cols, rem = extra % cols;
            for (int cx = 0; cx < cols; cx++) colw[cx] += per + (cx == cols - 1 ? rem : 0);
        } else {
            int assigned = 0;
            for (int cx = 0; cx < cols; cx++) {
                if (cx == cols - 1) colw[cx] = avail_w - assigned;
                else { colw[cx] = colw[cx] * avail_w / total_w; assigned += colw[cx]; }
                if (colw[cx] < 0) colw[cx] = 0;
            }
        }
    }
    if (avail_h >= 0 && total_h > 0 && avail_h != total_h) {
        if (avail_h > total_h) {
            int extra = avail_h - total_h, per = extra / rows, rem = extra % rows;
            for (int ry = 0; ry < rows; ry++) rowh[ry] += per + (ry == rows - 1 ? rem : 0);
        } else {
            int assigned = 0;
            for (int ry = 0; ry < rows; ry++) {
                if (ry == rows - 1) rowh[ry] = avail_h - assigned;
                else { rowh[ry] = rowh[ry] * avail_h / total_h; assigned += rowh[ry]; }
                if (rowh[ry] < 0) rowh[ry] = 0;
            }
        }
    }
}

void tk_arrange(tk_widget *w, tk_rect r)
{
    w->rect = r;
    if (w->kind == TK_W_BOX && w->wrap && w->dir == TK_DIR_HORIZONTAL) {
        int inner_w = r.w - 2 * w->padding;
        int x = r.x + w->padding;
        int y = r.y + w->padding;
        size_t i = 0;
        while (i < w->n_children) {
            /* find this row's members and height */
            size_t j = i;
            int row_w = 0, row_h = 0;
            while (j < w->n_children) {
                tk_widget *ch = w->children[j];
                if (!ch->visible) { j++; continue; }
                int cw = ch->desired.w;
                if (row_w > 0 && row_w + w->spacing + cw > inner_w) break;
                row_w += (row_w ? w->spacing : 0) + cw;
                if (ch->desired.h > row_h) row_h = ch->desired.h;
                j++;
            }
            if (row_w == 0 && j == i) break; /* only invisible children */
            for (size_t k = i; k < j; k++) {
                tk_widget *ch = w->children[k];
                if (!ch->visible) { ch->rect = (tk_rect){0, 0, 0, 0}; continue; }
                int chh = ch->desired.h;
                int cy = y;
                if (w->cross_align == TK_ALIGN_CENTER) cy = y + (row_h - chh) / 2;
                else if (w->cross_align == TK_ALIGN_END) cy = y + row_h - chh;
                else if (w->cross_align == TK_ALIGN_STRETCH) chh = row_h;
                tk_arrange(ch, (tk_rect){x, cy, ch->desired.w, chh});
                x += ch->desired.w + w->spacing;
            }
            i = j;
            x = r.x + w->padding;
            y += row_h + w->spacing;
        }
    } else if (w->kind == TK_W_GRID) {
        int cols = w->cols > 0 ? w->cols : 1;
        int rows = grid_place(w, cols);
        if (rows < 1) rows = 1;
        int *colw = calloc((size_t)cols, sizeof(int));
        int *rowh = calloc((size_t)rows, sizeof(int));
        if (colw && rowh) {
            grid_tracks(w, r, colw, rowh, cols, rows);
            /* track origins (padding + preceding tracks + spacing) */
            for (size_t i = 0; i < w->n_children; i++) {
                tk_widget *ch = w->children[i];
                if (!ch->visible || ch->cell_x < 0) {
                    ch->rect = (tk_rect){0, 0, 0, 0};
                    continue;
                }
                int cs = grid_span_cols(ch, cols);
                int rs = grid_span_rows(ch);
                int x = r.x + w->padding, y = r.y + w->padding;
                int cw = 0, chh = 0;
                for (int cx = 0; cx < ch->cell_x; cx++)
                    x += colw[cx] + w->spacing;
                for (int cx = ch->cell_x; cx < ch->cell_x + cs && cx < cols; cx++)
                    cw += colw[cx] + (cx > ch->cell_x ? w->spacing : 0);
                for (int ry = 0; ry < ch->cell_y; ry++)
                    y += rowh[ry] + w->vspacing;
                for (int ry = ch->cell_y; ry < ch->cell_y + rs && ry < rows; ry++)
                    chh += rowh[ry] + (ry > ch->cell_y ? w->vspacing : 0);
                tk_arrange(ch, (tk_rect){x, y, cw, chh});
            }
        }
        free(colw); free(rowh);
    } else if (w->kind == TK_W_TABS) {
        int bar_h = TK_TAB_BAR_H;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if ((int)i == w->active_tab)
                tk_arrange(ch, (tk_rect){r.x, r.y + bar_h, r.w,
                                         r.h > bar_h ? r.h - bar_h : 0});
            else
                tk_arrange(ch, (tk_rect){r.x, r.y + bar_h, 0, 0});
        }
    } else if (w->kind == TK_W_BOX || w->kind == TK_W_DIALOG ||
               w->kind == TK_W_CARD || w->kind == TK_W_SHEET) {
        bool vert = w->dir == TK_DIR_VERTICAL;
        int title_h = (w->kind == TK_W_DIALOG && vert)
                      ? tk_dialog_title_h(w, tk_theme_default()) : 0;
        if (w->kind == TK_W_SHEET && vert)
            title_h = 30; /* M7: drag-handle zone */
        int inner_main = (vert ? r.h : r.w) - 2 * w->padding - title_h;
        int inner_cross = (vert ? r.w : r.h) - 2 * w->padding;
        int nvis = 0, fixed_main = 0, flex_total = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible) continue;
            nvis++;
            /* A wrapping horizontal box's height depends on the
             * width it will actually get, but a horizontal parent
             * measures children at UNLIMITED width (single row),
             * so by arrange time the recorded height can be one
             * row while the box wraps to two - and the second row
             * paints over the next sibling (M4: the notes format
             * toolbar lost its last buttons under the editor).
             * Re-measure wrap children at their arranged width
             * before summing fixed heights. */
            if (vert && ch->kind == TK_W_BOX && ch->wrap &&
                ch->dir == TK_DIR_HORIZONTAL) {
                int target = inner_cross;
                if (w->cross_align != TK_ALIGN_STRETCH &&
                    ch->desired.w < target)
                    target = ch->desired.w;
                if (target >= 0 && target != ch->avail_w)
                    (void)tk_measure(ch, (tk_constraints){
                        0, target, 0, TK_UNLIMITED});
            }
            if (ch->flex > 0) flex_total += ch->flex;
            else fixed_main += vert ? ch->desired.h : ch->desired.w;
        }
        int spacing_total = nvis > 1 ? w->spacing * (nvis - 1) : 0;
        int free_main = inner_main - fixed_main - spacing_total;
        if (free_main < 0) free_main = 0;
        int pos = (vert ? r.y : r.x) + w->padding + title_h;
        int cross0 = (vert ? r.x : r.y) + w->padding;
        int distributed = 0;
        int flex_seen = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible) { ch->rect = (tk_rect){0, 0, 0, 0}; continue; }
            int main_sz;
            if (ch->flex > 0 && flex_total > 0) {
                flex_seen += ch->flex;
                /* last flex child takes the remainder (no drift) */
                bool last_flex = true;
                for (size_t j = i + 1; j < w->n_children; j++)
                    if (w->children[j]->visible && w->children[j]->flex > 0)
                        last_flex = false;
                if (last_flex) main_sz = free_main - distributed;
                else { main_sz = free_main * ch->flex / flex_total; distributed += main_sz; }
            } else {
                main_sz = vert ? ch->desired.h : ch->desired.w;
            }
            if (!vert && ch->kind == TK_W_BOX && ch->wrap &&
                ch->dir == TK_DIR_HORIZONTAL && main_sz != ch->avail_w)
                (void)tk_measure(ch, (tk_constraints){
                    0, main_sz, 0, TK_UNLIMITED});
            int child_cross = vert ? ch->desired.w : ch->desired.h;
            int cross_sz, cross_pos;
            switch (w->cross_align) {
            case TK_ALIGN_STRETCH: cross_sz = inner_cross; cross_pos = cross0; break;
            case TK_ALIGN_CENTER:
                cross_sz = child_cross;
                cross_pos = cross0 + (inner_cross - child_cross) / 2; break;
            case TK_ALIGN_END:
                cross_sz = child_cross;
                cross_pos = cross0 + (inner_cross - child_cross); break;
            default: cross_sz = child_cross; cross_pos = cross0; break;
            }
            tk_rect cr = vert ? (tk_rect){cross_pos, pos, cross_sz, main_sz}
                              : (tk_rect){pos, cross_pos, main_sz, cross_sz};
            tk_arrange(ch, cr);
            pos += main_sz + w->spacing;
        }
    } else if (w->kind == TK_W_FIXED) {
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            tk_rect cr = { r.x + ch->fixed_rect.x, r.y + ch->fixed_rect.y,
                           ch->fixed_rect.w, ch->fixed_rect.h };
            tk_arrange(ch, cr);
        }
    } else if (w->kind == TK_W_SCROLLVIEW) {
        if (w->n_children) {
            tk_widget *ch = w->children[0];
            int content_h = ch->desired.h;
            int max_off = content_h - r.h;
            if (max_off < 0) max_off = 0;
            if (w->scroll_y > max_off) w->scroll_y = max_off;
            if (w->scroll_y < 0) w->scroll_y = 0;
            tk_arrange(ch, (tk_rect){r.x, r.y - w->scroll_y,
                                     r.w, content_h > r.h ? content_h : r.h});
        }
    } else if (w->kind == TK_W_OVERFLOWBAR) {
        tk_ob_arrange(w, r);
    } else if (w->kind == TK_W_APPBAR) {
        /* M7: leading at the left edge, actions right-aligned,
         * everything vertically centred at its desired size */
        const tk_theme *t = tk_theme_default();
        size_t start = 0;
        if (w->appbar_leading && w->n_children) {
            tk_widget *ch = w->children[0];
            tk_arrange(ch, (tk_rect){ r.x + t->unit,
                                      r.y + (r.h - ch->desired.h) / 2,
                                      ch->desired.w, ch->desired.h });
            start = 1;
        }
        int total = 0;
        for (size_t i = start; i < w->n_children; i++)
            if (w->children[i]->visible)
                total += w->children[i]->desired.w + 4;
        int x = r.x + r.w - t->unit - total;
        for (size_t i = start; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible) { ch->rect = (tk_rect){0, 0, 0, 0}; continue; }
            tk_arrange(ch, (tk_rect){ x,
                                      r.y + (r.h - ch->desired.h) / 2,
                                      ch->desired.w, ch->desired.h });
            x += ch->desired.w + 4;
        }
    }
    if (w->kind == TK_W_BUTTON && w->n_children) {
        /* M7: button children form a trailing row inside the
         * button (see tk.h): right-aligned, vertically centred,
         * each at its desired size. The ×-in-a-tab case. */
        int total = 0, nvis = 0;
        for (size_t i = 0; i < w->n_children; i++) {
            if (!w->children[i]->visible) continue;
            total += w->children[i]->desired.w;
            nvis++;
        }
        if (nvis > 1) total += 4 * (nvis - 1);
        int x = r.x + r.w - 8 - total;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            if (!ch->visible) { ch->rect = (tk_rect){0, 0, 0, 0}; continue; }
            tk_arrange(ch, (tk_rect){ x,
                                      r.y + (r.h - ch->desired.h) / 2,
                                      ch->desired.w, ch->desired.h });
            x += ch->desired.w + 4;
        }
    }
    /* leaf widgets have no children to arrange */
}

tk_rect tk_widget_rect(const tk_widget *w) { return w->rect; }

/* ---------------------------------------------------------------- */
/* Paint                                                             */
/* ---------------------------------------------------------------- */

/* Paint the children of w (shared by the normal and fast
 * ScrollView paths). */
static void paint_children(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    for (size_t i = 0; i < w->n_children; i++) {
        if (w->kind == TK_W_TABS && (int)i != w->active_tab) continue;
        tk_paint(w->children[i], c, t);
    }
}

void tk_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    if (!w->visible) return;
    /* M5 paint culling: skip a subtree that cannot touch the
     * current clip. Until now the walk visited every widget and
     * let the canvas discard the writes one by one, so repainting
     * one label cost a full tree walk with per-glyph clip tests.
     * The test rect is grown by 8px: the only sanctioned
     * out-of-rect paint is the backdrop shadow (+4) and 1px
     * text shadows. A culled widget paints nothing, exactly as
     * the clipped writes did before. */
    {
        tk_rect cb = tk_canvas_clip_bounds(c);
        tk_rect pr = { w->rect.x - 8, w->rect.y - 8,
                       w->rect.w + 16, w->rect.h + 16 };
        if (pr.x + pr.w <= cb.x || cb.x + cb.w <= pr.x ||
            pr.y + pr.h <= cb.y || cb.y + cb.h <= pr.y)
            return;
    }
    /* M5 ScrollView fast frame: tk_root_paint_damage's pre-shift
     * pass already moved the retained pixels in the canvas when
     * this SV's content was unchanged and only its offset moved.
     * Here we only repaint the exposed band + scrollbar strip. */
    bool sv_fast = (w->kind == TK_W_SCROLLVIEW && w->sv_shift_done);
    tk_rect sv_band = w->sv_band;
    if (w->backdrop) {
        /* Floating-surface backdrop (UI-REDESIGN 2.2/2.5): shadow
         * silhouette first (black at 25%, blended once - the fill
         * path blends when a < 255), then the opaque surface with
         * its 1px border. Painted here, before the widget itself,
         * so any container can carry a surface without its own
         * paint code. */
        tk_rect r = w->rect;
        int rad = w->backdrop_radius >= 0 ? w->backdrop_radius
                                          : t->radius_overlay;
        if (w->backdrop_shadow)
            tk_canvas_fill_round_rect(c,
                (tk_rect){ r.x + 4, r.y + 4, r.w, r.h }, rad,
                (tk_color){ 0, 0, 0, 64 });
        tk_canvas_fill_round_rect(c, r, rad,
                                  w->backdrop_has_face ? w->backdrop_face
                                                       : t->surface);
        /* large radii (v2 sheets/pills) need a border that follows
         * the corner; small radii keep the classic square stroke
         * so the v1 theme's pixels are unchanged */
        if (rad > 8)
            tk_canvas_stroke_round_rect(c, r, rad, 1, t->border_line);
        else
            tk_canvas_stroke_rect(c, r, 1, t->border);
    }
    if (sv_fast) w->sv_fast_frame = true;
    tk_leaf_paint(w, c, t);
    w->sv_fast_frame = false;
    /* M16: a bottombar paints its selection indicator itself,
     * under its cells (tk_bottombar_paint_indicator) */
    if (w->kind == TK_W_BOX) tk_bottombar_paint_indicator(w, c, t);
    /* M6: a badge pinned to a button's top-right corner (the
     * BottomBar cell form; usable on any button). The count
     * lives in the button's badge_count field. */
    if (w->kind == TK_W_BUTTON && w->badge_count != 0) {
        tk_color bgc = { 186, 26, 26, 255 };
        if (w->badge_count < 0) {
            tk_canvas_fill_round_rect(c,
                (tk_rect){ w->rect.x + w->rect.w - 12, w->rect.y + 4,
                           8, 8 }, 4, bgc);
        } else {
            char nb[8];
            if (w->badge_count > 99) snprintf(nb, sizeof nb, "99+");
            else snprintf(nb, sizeof nb, "%d", w->badge_count);
            int bw = tk_text_width_st(t, TK_TEXT_CAPTION, nb) + 10;
            tk_rect br = { w->rect.x + w->rect.w - bw - 2,
                           w->rect.y + 2, bw, 16 };
            tk_canvas_fill_round_rect(c, br, 8, bgc);
            tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                br.x + 5,
                br.y + (16 - tk_line_height_st(t, TK_TEXT_CAPTION)) / 2,
                nb, (tk_color){ 255, 255, 255, 255 });
        }
    }
    if (w->kind == TK_W_SCROLLVIEW) {
        if (sv_fast) {
            /* repaint only the exposed band, then the scrollbar
             * zone (the strip pixels the thumb's travel touched) */
            tk_canvas_push_clip(c, sv_band);
            paint_children(w, c, t);
            tk_canvas_pop_clip(c);
            tk_canvas_push_clip(c, w->sv_strip);
            paint_children(w, c, t);
            tk_canvas_pop_clip(c);
            w->sv_painted_y = w->scroll_y; /* state stays valid */
            w->sv_shift_done = false;
        } else {
            tk_canvas_push_clip(c, w->rect);
            paint_children(w, c, t);
            tk_canvas_pop_clip(c);
            /* The painted state is a trustworthy shift base only
             * when the whole viewport was in the clip; a partial
             * paint during a pending scroll leaves a mixed canvas
             * that must never be shifted from. */
            tk_rect cb = tk_canvas_clip_bounds(c);
            bool covers = cb.x <= w->rect.x && cb.y <= w->rect.y &&
                cb.x + cb.w >= w->rect.x + w->rect.w &&
                cb.y + cb.h >= w->rect.y + w->rect.h;
            if (covers) {
                w->sv_painted_y = w->scroll_y;
                w->sv_paint_valid = true;
                w->sv_dirty = false;
            } else if (w->scroll_y != w->sv_painted_y) {
                w->sv_paint_valid = false;
            }
        }
    } else {
        paint_children(w, c, t);
    }
}

/* ---------------------------------------------------------------- */
/* Hit testing + dispatch                                            */
/* ---------------------------------------------------------------- */

tk_widget *tk_hit_test(tk_widget *w, int x, int y)
{
    if (!w->visible || !w->enabled) return NULL;
    if (!tk_rect_contains(w->rect, x, y)) return NULL;
    for (size_t i = w->n_children; i-- > 0;) {
        if (w->kind == TK_W_TABS && (int)i != w->active_tab) continue;
        tk_widget *hit = tk_hit_test(w->children[i], x, y);
        if (hit) return hit;
    }
    return w;
}

bool tk_is_descendant(const tk_widget *ancestor, const tk_widget *w)
{
    for (const tk_widget *p = w; p; p = p->parent)
        if (p == ancestor) return true;
    return false;
}

static bool dispatch_chain(tk_widget *target, tk_root *root, tk_event *ev)
{
    tk_widget *w = target;
    while (w) {
        if (tk_leaf_event(w, root, ev)) { ev->handled = true; return true; }
        if (ev->handled) return true;
        w = w->parent;
    }
    return false;
}

bool tk_dispatch(tk_widget *w, tk_event *ev)
{
    ev->handled = false;
    /* standalone dispatch without a root (focus/capture unmanaged) */
    tk_root tmp = { .top = w };
    tk_widget *target = w;
    if (ev->type == TK_EV_POINTER_DOWN || ev->type == TK_EV_POINTER_UP ||
        ev->type == TK_EV_POINTER_MOVE) {
        tk_widget *hit = tk_hit_test(w, ev->x, ev->y);
        if (hit) target = hit;
    }
    return dispatch_chain(target, &tmp, ev);
}

/* ---------------------------------------------------------------- */
/* Root                                                              */
/* ---------------------------------------------------------------- */

struct tk_root *tk_root_new(tk_widget *top)
{
    tk_root *r = calloc(1, sizeof(*r));
    if (r) {
        r->top = top;
        r->anims_enabled = true; /* M5: tk_root_set_animations_enabled */
        r->dmg.owner = r;
    }
    if (top) top->dmg = &r->dmg; /* M3: invalidation reaches here */
    return r;
}

void tk_root_set_scroll_blit(tk_root *r, bool on)
{
    if (r) r->scroll_blit = on;
}

bool tk_root_scroll_blit(const tk_root *r)
{
    return r && r->scroll_blit;
}

/* M5: recursive note that a full repaint just covered the whole
 * tree, so every ScrollView's canvas state is a valid shift base
 * at its current offset and no content is outstanding. */
static void sv_note_walk(tk_widget *w)
{
    if (!w) return;
    if (w->kind == TK_W_SCROLLVIEW) {
        w->sv_painted_y = w->scroll_y;
        w->sv_paint_valid = true;
        w->sv_dirty = false;
    }
    for (size_t i = 0; i < w->n_children; i++) sv_note_walk(w->children[i]);
}

void tk_sv_note_full_paint(tk_widget *top)
{
    sv_note_walk(top);
}

void tk_root_free(tk_root *r)
{
    if (!r) return;
    tk_anims_free_all(r); /* M5 */
    if (r->snack_widget) tk_widget_free(r->snack_widget); /* M6 */
    free(r->drag_mime); /* M7 */
    free(r->drag_data);
    /* The damage store lives in the root; the top widget (and
     * any overlays still attached) borrow a pointer to it.
     * Detach them before freeing so a widget that outlives its
     * root invalidates into the void instead of into freed
     * memory (tk_widget_invalidate already tolerates no store). */
    if (r->top && r->top->dmg == &r->dmg) r->top->dmg = NULL;
    for (size_t i = 0; i < r->n_overlays; i++)
        if (r->overlays[i]->dmg == &r->dmg)
            r->overlays[i]->dmg = NULL;
    free(r->overlays);
    tk_canvas_free(r->scale_save); /* M20 */
    tk_canvas_free(r->scale_surf);
    free(r->dmg.rects);
    free(r);
}
tk_widget *tk_root_top(const tk_root *r) { return r->top; }

/* ---------------------------------------------------------------- */
/* Overlays (menus, dialogs)                                         */
/* ---------------------------------------------------------------- */

void tk_root_add_overlay(tk_root *r, tk_widget *w)
{
    if (!r || !w || tk_root_has_overlay(r, w)) return;
    if (r->n_overlays >= r->cap_overlays) {
        size_t nc = r->cap_overlays ? r->cap_overlays * 2 : 4;
        tk_widget **na = realloc(r->overlays, nc * sizeof(*na));
        if (!na) return;
        r->overlays = na; r->cap_overlays = nc;
    }
    r->overlays[r->n_overlays++] = w;
    w->dmg = &r->dmg; /* M3: invalidation reaches the root store */
}

bool tk_root_has_overlay(const tk_root *r, const tk_widget *w)
{
    if (!r) return false;
    for (size_t i = 0; i < r->n_overlays; i++)
        if (r->overlays[i] == w) return true;
    return false;
}

static void set_focus(tk_root *r, tk_widget *w); /* below */

void tk_root_remove_overlay(tk_root *r, tk_widget *w)
{
    if (!r || !w) return;
    for (size_t i = 0; i < r->n_overlays; i++) {
        if (r->overlays[i] == w) {
            tk_damage_add(&r->dmg, w->rect); /* M3: repaint beneath */
            if (w->kind == TK_W_DIALOG && r->modal == w &&
                r->w > 0 && r->h > 0)
                /* M11: the modal dialog's scrim covered the
                 * whole root; its removal repaints everywhere */
                tk_damage_add(&r->dmg, (tk_rect){0, 0, r->w, r->h});
            if (r->hover == w) r->hover = NULL;
            w->dmg = NULL;
            /* M15 fix: clear the modal BEFORE the focus
             * restore below. tk_focus applies the modal trap,
             * so while the closing overlay was still modal the
             * restore to an outside invoker was always refused
             * - the M14 return mechanism never actually fired
             * for modal overlays (dialog / drawer / sheet);
             * only non-modal popups (menus) returned focus. */
            if (r->modal == w) r->modal = NULL;
            bool focus_was_here = r->focused &&
                (r->focused == w || tk_is_descendant(w, r->focused));
            if (focus_was_here) set_focus(r, NULL);
            /* M14: an overlay that took the focus (a popup
             * menu, a dialog) hands it back to the widget
             * that held it before, instead of dropping it -
             * the reference behaviour (a dismissed suggestion
             * list leaves the field focused; a closed dialog
             * returns focus to its invoker). tk_focus applies
             * the modal trap, so a stale target outside a
             * still-active modal is refused safely. */
            if (focus_was_here && w->focus_return) {
                tk_widget *back = w->focus_return;
                w->focus_return = NULL;
                if (back != w && !tk_is_descendant(w, back))
                    (void)tk_focus(r, back);
            }
            if (r->capture == w) r->capture = NULL;
            memmove(r->overlays + i, r->overlays + i + 1,
                    (r->n_overlays - i - 1) * sizeof(*r->overlays));
            r->n_overlays--;
            return;
        }
    }
}

void tk_close_all_menus(tk_root *r)
{
    if (!r) return;
    for (size_t i = r->n_overlays; i-- > 0;) {
        tk_widget *ov = r->overlays[i];
        if (ov->kind == TK_W_MENU) {
            if (ov->menu_closing) {
                /* M17: complete a running menu exit now -
                 * this close is synchronous by contract */
                if (ov->fade_anim) {
                    tk_anim_cancel(ov->fade_anim);
                    ov->fade_anim = NULL;
                }
                ov->menu_closing = false;
            }
            if (ov->owner_ref && ov->owner_ref->open_sub == ov)
                ov->owner_ref->open_sub = NULL; /* combobox dropdown */
            tk_split_on_menu_closed(ov); /* M8: split chevron home */
            tk_root_remove_overlay(r, ov);
            if (ov->auto_free) tk_widget_free(ov);
        }
    }
}

/* Arrange one overlay: menus keep the rect set at popup time;
 * dialogs are centered at their desired size; floating widgets
 * (M6) are anchored to a root corner at their desired size. */
static void layout_overlay(tk_root *r, tk_widget *ov)
{
    if (ov->float_anchor >= 0) {
        tk_size d = tk_measure(ov, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
        int x = ov->float_mx, y = ov->float_my;
        if (ov->float_anchor == 1 || ov->float_anchor == 3)
            x = r->w - d.w - ov->float_mx;
        if (ov->float_anchor == 2 || ov->float_anchor == 3)
            y = r->h - d.h - ov->float_my;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        ov->fixed_rect = (tk_rect){x, y, d.w, d.h};
        tk_arrange(ov, ov->fixed_rect);
        return;
    }
    if (ov->kind == TK_W_DIALOG && ov->dlg_fullscreen) {
        /* M7: full-screen dialog covers the whole root */
        (void)tk_measure(ov, (tk_constraints){0, r->w, 0, r->h});
        tk_arrange(ov, (tk_rect){0, 0, r->w, r->h});
        return;
    }
    if (ov->kind == TK_W_DRAWER) {
        /* M7: the drawer widget covers the root (scrim + panel
         * are both its own paint) */
        (void)tk_measure(ov, (tk_constraints){0, TK_UNLIMITED,
                                              0, TK_UNLIMITED});
        tk_arrange(ov, (tk_rect){0, 0, r->w, r->h});
        return;
    }
    if (ov->kind == TK_W_SHEET) {
        /* M7: bottom sheet - full width (centred past 640dp =
         * 748px), desired height, at least 72dp (84px) of the
         * window left visible; side sheet - desired width
         * (capped at 360dp = 421px), full height, right edge */
        tk_size d = tk_measure(ov, (tk_constraints){0, r->w, 0, r->h});
        if (ov->sheet_edge == 1) {
            int wdt = d.w;
            if (wdt > 421) wdt = 421;
            if (wdt > r->w) wdt = r->w;
            tk_arrange(ov, (tk_rect){r->w - wdt - ov->sheet_off, 0,
                                     wdt, r->h});
        } else {
            int hgt = d.h;
            int maxh = r->h - 84;
            if (maxh < 80) maxh = r->h;
            if (hgt > maxh) hgt = maxh;
            int wdt = r->w, x = 0;
            if (wdt > 748) { x = (r->w - 748) / 2; wdt = 748; }
            tk_arrange(ov, (tk_rect){x, r->h - hgt + ov->sheet_off,
                                     wdt, hgt});
        }
        return;
    }
    if (ov->kind == TK_W_DIALOG) {
        tk_size d = tk_measure(ov, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
        int wdt = d.w < r->w ? d.w : r->w;
        int hgt = d.h < r->h ? d.h : r->h;
        if (wdt < 120) wdt = r->w < 120 ? r->w : 120;
        if (hgt < 80) hgt = r->h < 80 ? r->h : 80;
        /* M9: spec width bands at the device scale - M3 basic
         * dialog min 280dp = 327 / max 560dp = 655; WinUI
         * ContentDialog min 320epx = 458, min height 184epx
         * = 263, max width 548epx = 784 (themeresources,
         * fetched 2026-11-20). The root size still caps. */
        const tk_theme *t = tk_theme_default();
        if (t->style == 1) {
            if (wdt < 327) wdt = r->w < 327 ? r->w : 327;
            if (wdt > 655) wdt = 655;
        } else if (t->style == 2) {
            if (wdt < 458) wdt = r->w < 458 ? r->w : 458;
            if (wdt > 784) wdt = 784;
            if (hgt < 263) hgt = r->h < 263 ? r->h : 263;
        }
        tk_arrange(ov, (tk_rect){(r->w - wdt) / 2, (r->h - hgt) / 2, wdt, hgt});
    } else {
        (void)tk_measure(ov, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
        tk_arrange(ov, ov->fixed_rect);
    }
}

void tk_root_layout(tk_root *r, int w, int h)
{
    r->w = w; r->h = h;
    (void)tk_measure(r->top, (tk_constraints){0, w, 0, h});
    tk_arrange(r->top, (tk_rect){0, 0, w, h});
    for (size_t i = 0; i < r->n_overlays; i++)
        layout_overlay(r, r->overlays[i]);
    r->dmg.full = true; /* M3: geometry changed, repaint everything */
}

/* ---- floating overlays (M6) ------------------------------------ */

void tk_root_add_floating(tk_root *r, tk_widget *w, int anchor,
                          int margin_x, int margin_y)
{
    if (!r || !w) return;
    w->float_anchor = anchor < 0 ? 0 : (anchor > 3 ? 3 : anchor);
    w->float_mx = margin_x;
    w->float_my = margin_y;
    tk_root_add_overlay(r, w);
    if (r->w > 0 && r->h > 0) layout_overlay(r, w);
    tk_damage_add(&r->dmg, w->rect);
}

void tk_root_remove_floating(tk_root *r, tk_widget *w)
{
    if (!r || !w) return;
    tk_root_remove_overlay(r, w);
    w->float_anchor = -1;
}

bool tk_root_is_floating(const tk_root *r, const tk_widget *w)
{
    return w && w->float_anchor >= 0 && tk_root_has_overlay(r, w);
}

/* ---- tooltips (M3) -------------------------------------------- */

static void tooltip_hide(tk_root *r)
{
    if (!r->tt_visible) return;
    r->tt_visible = false;
    tk_damage_add(&r->dmg, r->tt_rect); /* repaint what it covered */
}

static void tooltip_paint(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    if (!r->tt_visible) return;
    tk_rect q = r->tt_rect;
    tk_canvas_fill_round_rect(c, q, 4, t->surface);
    tk_canvas_stroke_rect(c, q, 1, t->border);
    tk_canvas_push_clip(c, q);
    tk_canvas_draw_text(c, t, q.x + 6, q.y + 3, r->tt_buf, t->text);
    tk_canvas_pop_clip(c);
}

/* ---- toast / snackbar (M6) --------------------------------------
 * Root-level transient surfaces, painted last (above overlays).
 * Both use the inverse pairing of the theme (text colour as the
 * container, bg colour as the content) - the M3 snackbar/toast
 * container is inverseSurface. Geometry is computed at show time
 * from the root's laid-out size; expiry runs in tk_root_tick. */

static void toast_layout_rect(tk_root *r)
{
    const tk_theme *t = tk_theme_default();
    int rw = r->w > 0 ? r->w : 854; /* M22: panel width (AUDIT SS20) */
    int rh = r->h > 0 ? r->h : 480;
    int tw, th, yoff;
    if (t->style == 1) {
        /* M18: the M3-mode toast takes the AOSP framework
         * toast's shape (platform/frameworks/base core/res:
         * layout/transient_notification.xml and values/
         * dimens.xml, fetched 2026-11-20): text 14sp,
         * horizontal padding 16dp, text vertical padding
         * 12dp, max width 300dp (toast_width), bottom
         * offset 48dp (toast_y_offset). At the corrected
         * ~179.2ppi premise those convert to padding
         * 17.9px/side, 13.4px/side, cap 336px, offset
         * 53.8px; the implemented constants below (19/14,
         * cap 351, offset 56) are the M18 values from the
         * old 187ppi conversion, retained
         * (REPORT-toolkit-m23.md). The 14sp text is the
         * theme's caption style (13px in the v2 themes
         * since M22; the code reads the theme, so layout
         * and paint agree on it). */
        tw = tk_text_width_st(t, TK_TEXT_CAPTION, r->toast_buf) + 38;
        if (tw > 351) tw = 351;
        th = tk_line_height_st(t, TK_TEXT_CAPTION) + 28;
        yoff = 56;
    } else {
        tw = tk_text_width(t, r->toast_buf) + 32;
        th = tk_line_height(t) + 16;
        yoff = 24;
    }
    if (tw > rw - 32) tw = rw - 32;
    r->toast_rect = (tk_rect){ (rw - tw) / 2, rh - th - yoff, tw, th };
}

/* M13: the toast fade pairing. WinUI fades are Linear over
 * ControlFasterAnimationDuration 83 ms (Microsoft Learn,
 * Timing and easing - fetched M12).
 * M18: the M3 mode now uses the AOSP framework values the
 * M13 round could not retrieve (platform/frameworks/base
 * core/res/res/anim/toast_enter.xml + toast_exit.xml and
 * values/config.xml, fetched 2026-11-20): both directions
 * run config_longAnimTime = 500 ms; the enter is an alpha
 * fade over decelerate_quad, the exit over accelerate_quad.
 * The toolkit evaluates the exit as ease(remaining), and
 * 1 - accelerate_quad(1-x) == decelerate_quad(x), so the
 * single decelerate_quad below reproduces both curves
 * exactly. The classic theme keeps the M13 mapping. */
static int toast_fade_ms(const tk_theme *t)
{
    if (t && t->style == 2) return 83;
    if (t && t->style == 1) return 500;
    return 150;
}

static float toast_fade_ease(const tk_theme *t, float x)
{
    if (t && t->style == 2) return x; /* Linear */
    if (t && t->style == 1) return tk_ease_decelerate_quad(x);
    return tk_ease_standard_decelerate(x);
}

/* The fade only runs when the root's animation clock is live
 * (a session loop is ticking); a headless / one-shot root gets
 * the pre-M13 synchronous behaviour. */
static bool toast_fade_live(const tk_root *r)
{
    return r->anim_now != 0 && tk_root_animations_enabled(r);
}

void tk_toast_show(tk_root *r, const char *text, int duration_ms)
{
    if (!r || !text) return;
    if (r->toast_active) tk_damage_add(&r->dmg, r->toast_rect);
    size_t len = strlen(text);
    if (len >= sizeof(r->toast_buf)) len = sizeof(r->toast_buf) - 1;
    memcpy(r->toast_buf, text, len);
    r->toast_buf[len] = '\0';
    if (duration_ms <= 0) duration_ms = 4000;
    r->toast_until = r->now_ms + (uint32_t)duration_ms;
    r->toast_active = true;
    /* M13: fade state - the alpha timeline is anchored here
     * and evaluated in transients_tick */
    r->toast_fading_out = false;
    r->toast_show_ms = r->now_ms;
    r->toast_show_clock = r->anim_now;
    r->toast_fade_ms = toast_fade_ms(tk_theme_default());
    r->toast_alpha = toast_fade_live(r) ? 0 : 255;
    toast_layout_rect(r);
    tk_damage_add(&r->dmg, r->toast_rect);
    tk_a11y_announce(r, r->toast_buf); /* M7: live region */
}

static void toast_off_now(tk_root *r)
{
    r->toast_active = false;
    r->toast_fading_out = false;
    tk_damage_add(&r->dmg, r->toast_rect);
}

void tk_toast_hide(tk_root *r)
{
    if (!r || !r->toast_active) return;
    if (r->toast_fading_out) { toast_off_now(r); return; }
    /* M13: an explicit hide fades out - but only when the
     * animation clock advanced since the show (a live session
     * loop); a same-tick or headless hide stays synchronous,
     * the behaviour existing callers were written against. */
    if (toast_fade_live(r) && r->anim_now > r->toast_show_clock &&
        r->toast_alpha > 0) {
        r->toast_fading_out = true;
        r->toast_hide_from = r->toast_alpha;
        r->toast_hide_end = r->now_ms + (uint32_t)r->toast_fade_ms;
        return;
    }
    toast_off_now(r);
}

bool tk_toast_visible(const tk_root *r) { return r && r->toast_active; }

static void snack_layout_rects(tk_root *r)
{
    const tk_theme *t = tk_theme_default();
    int rw = r->w > 0 ? r->w : 854; /* M22: panel width (AUDIT SS20) */
    int rh = r->h > 0 ? r->h : 480;
    int w = rw - 32;
    if (w > 560) w = 560;
    if (w < 0) w = rw;
    /* M9: the WinUI form is the InfoBar, whose MinHeight is
     * 48epx at the device scale; the M3 snackbar's 48dp
     * stands in the other modes. M22: at the corrected
     * premise those are 64.5 -> 65px and 53.8 -> 54px. */
    int h = t->style == 2 ? 65 : 54;
    r->snack_rect = (tk_rect){ 16, rh - h - 16, w, h };
    r->snack_action_rect = (tk_rect){0, 0, 0, 0};
    if (r->snack_action[0]) {
        int aw = tk_text_width(t, r->snack_action) + 24;
        r->snack_action_rect = (tk_rect){
            r->snack_rect.x + r->snack_rect.w - aw - 8,
            r->snack_rect.y + 8, aw, r->snack_rect.h - 16 };
    }
    /* M15: the action's focus stand-in tracks the action
     * rect, so the focus ring lands on the action. */
    if (r->snack_widget) r->snack_widget->rect = r->snack_action_rect;
}

/* M12: snackbar entrance (M3 form): the bar rises from below
 * the bottom edge over short4 (200 ms) with Standard
 * decelerate easing. The callback damages the union of the
 * bar's previous and new positions each frame. */
static void snack_enter_cb(void *ud, int v)
{
    tk_root *r = ud;
    tk_rect old = r->snack_rect; old.y += r->snack_off;
    tk_rect nw = r->snack_rect; nw.y += v;
    r->snack_off = v;
    tk_damage_add(&r->dmg, old);
    tk_damage_add(&r->dmg, nw);
}
static void snack_enter_done(void *ud)
{
    tk_root *r = ud;
    r->snack_anim = NULL;
    r->snack_off = 0;
    tk_damage_add(&r->dmg, r->snack_rect);
}

/* M15: the snackbar action's keyboard path. The action is a
 * text button in the reference (M3 snackbar; the Android
 * Developers Snackbar contract treats moving the keyboard
 * focus indicator to it as ordinary traversal), so Enter /
 * Space on the focused action fires exactly like the pointer
 * path in tk_root_event: hide first, then the callback.
 * Escape is not a snackbar dismiss key in either reference
 * and is not consumed here. */
static bool snack_key_hook(void *ud, tk_event *ev)
{
    tk_root *r = ud;
    if (!r || !ev || ev->type != TK_EV_KEY_DOWN) return false;
    if (ev->key != TK_KEY_ENTER && ev->key != TK_KEY_SPACE)
        return false;
    if (!r->snack_active || !r->snack_action[0] || !r->snack_cb)
        return false;
    tk_notify_cb cb = r->snack_cb;
    void *cbud = r->snack_ud;
    tk_snackbar_hide(r);
    cb(r->snack_widget, cbud);
    return true;
}

static void snackbar_show_impl(tk_root *r, const char *text,
                               int severity,
                               const char *action_label,
                               tk_notify_cb cb, void *ud,
                               int duration_ms)
{
    if (!r || !text) return;
    r->snack_severity = severity;
    if (r->snack_active) tk_damage_add(&r->dmg, r->snack_rect);
    size_t len = strlen(text);
    if (len >= sizeof(r->snack_buf)) len = sizeof(r->snack_buf) - 1;
    memcpy(r->snack_buf, text, len);
    r->snack_buf[len] = '\0';
    r->snack_action[0] = '\0';
    if (action_label) {
        size_t al = strlen(action_label);
        if (al >= sizeof(r->snack_action)) al = sizeof(r->snack_action) - 1;
        memcpy(r->snack_action, action_label, al);
        r->snack_action[al] = '\0';
    }
    r->snack_cb = cb;
    r->snack_ud = ud;
    if (!r->snack_widget) {
        r->snack_widget = tk_widget_new(TK_W_LABEL);
        if (r->snack_widget) {
            /* M15: the action's focus stand-in (see
             * snack_key_hook / focus_step) */
            r->snack_widget->can_focus = true;
            tk_widget_set_event_hook(r->snack_widget,
                                     snack_key_hook, r);
        }
    }
    if (duration_ms == TK_SNACKBAR_INDEFINITE ||
        r->snack_action[0]) {
        /* M16: a bar with an action never times out. Android
         * Developers (Snackbar API reference): "Snackbars
         * with an action should not timeout or self-dismiss
         * until the user performs another action"; the WinUI
         * InfoBar likewise "will remain in the content area
         * until closed by the user" (Microsoft Learn). The
         * requested duration is honoured only for action-less
         * bars. Recorded as an observation in M15 §13.4 and
         * corrected here. */
        r->snack_forever = true;
        r->snack_until = 0;
    } else {
        r->snack_forever = false;
        if (duration_ms <= 0) duration_ms = 4000;
        r->snack_until = r->now_ms + (uint32_t)duration_ms;
    }
    r->snack_active = true;
    snack_layout_rects(r);
    /* M15: a replacement bar without an action leaves the
     * focused stand-in pointing at nothing - release it. */
    if (!r->snack_action[0] && r->focused == r->snack_widget)
        set_focus(r, NULL);
    tk_damage_add(&r->dmg, r->snack_rect);
    tk_a11y_announce(r, r->snack_buf); /* M7: live region */
    /* M12: entrance slide (M3 form only - the WinUI InfoBar
     * has no entrance motion and appears at its final rect).
     * No live clock / reduced motion: the offset stays 0. */
    if (r->snack_anim) { tk_anim_cancel(r->snack_anim); r->snack_anim = NULL; }
    r->snack_off = 0;
    {
        const tk_theme *th = tk_theme_default();
        if (th && th->style != 2 && r->anim_now != 0 &&
            r->anims_enabled) {
            int span = r->snack_rect.h + 16;
            r->snack_off = span;
            r->snack_anim = tk_anim_start(r, span, 0, 200,
                                          tk_ease_standard_decelerate,
                                          snack_enter_cb,
                                          snack_enter_done, r);
            if (!r->snack_anim) r->snack_off = 0;
        }
    }
}

void tk_snackbar_show(tk_root *r, const char *text,
                      const char *action_label,
                      tk_notify_cb cb, void *ud, int duration_ms)
{
    snackbar_show_impl(r, text, TK_SNACK_INFO, action_label,
                       cb, ud, duration_ms);
}

void tk_snackbar_show_sev(tk_root *r, const char *text,
                          tk_snack_severity sev,
                          const char *action_label,
                          tk_notify_cb cb, void *ud,
                          int duration_ms)
{
    snackbar_show_impl(r, text, (int)sev, action_label, cb, ud,
                       duration_ms);
}

void tk_snackbar_hide(tk_root *r)
{
    if (!r || !r->snack_active) return;
    if (r->snack_anim) { tk_anim_cancel(r->snack_anim); r->snack_anim = NULL; }
    r->snack_off = 0;
    /* M15: the action's focus stand-in dies with the bar */
    if (r->focused == r->snack_widget) set_focus(r, NULL);
    r->snack_active = false;
    r->snack_down_on = false; /* M7 */
    r->snack_cb = NULL;
    tk_damage_add(&r->dmg, r->snack_rect);
}

bool tk_snackbar_visible(const tk_root *r)
{
    return r && r->snack_active;
}

/* M9: 2px Bresenham for the InfoBar severity glyphs (the
 * canvas has no line primitive at this level). */
static void snack_glyph_line(tk_canvas *c, int x0, int y0, int x1,
                             int y1, tk_color col)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        tk_canvas_fill_rect(c, (tk_rect){ x0 - 1, y0 - 1, 2, 2 },
                            col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void paint_focus_ring(tk_root *r, tk_canvas *c,
                             const tk_theme *t); /* below */

static void transient_paint(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    /* M7: the container/content pairing is the theme's inverse
     * role pair (M3: inverseSurface / inverseOnSurface /
     * inversePrimary for the action). Themes that predate the
     * tokens (alpha 0) fall back to the pre-M7 text/bg pairing.
     * M9: in the WinUI mode (style 2) the snackbar is the
     * InfoBar: the severity's SystemFill background, a solid
     * severity icon disc with the inverse glyph, CardStroke
     * border (InfoBar themeresources).
     * M10: the Informational severity's Attention values,
     * unobtainable in M9, are now sourced from Microsoft's
     * Fluent token table (Microsoft Learn): the background is
     * SystemFillColorAttentionBackground and the disc is the
     * solid SystemFillColorAttention (sys_attn_bg / sys_attn
     * tokens). The accent stand-in is retired. */
    tk_color inv_bg = t->inverse_surface.a ? t->inverse_surface
                                           : t->text;
    tk_color inv_fg = t->inverse_surface.a ? t->inverse_on_surface
                                           : t->bg;
    tk_color inv_ac = t->inverse_surface.a ? t->inverse_primary
                                           : t->accent;
    if (r->snack_active) {
        tk_rect q = r->snack_rect;
        q.y += r->snack_off; /* M12: entrance slide */
        int lh = tk_line_height(t);
        int text_x = q.x + 16;
        if (t->style == 2) {
            tk_color bg, solid;
            switch (r->snack_severity) {
            case TK_SNACK_SUCCESS:
                bg = t->sys_ok_bg.a ? t->sys_ok_bg : t->surface;
                solid = t->sys_ok;
                break;
            case TK_SNACK_WARNING:
                bg = t->sys_warn_bg.a ? t->sys_warn_bg : t->surface;
                solid = t->sys_warn;
                break;
            case TK_SNACK_ERROR:
                bg = t->sys_err_bg.a ? t->sys_err_bg : t->surface;
                solid = t->danger;
                break;
            default: /* TK_SNACK_INFO */
                bg = t->sys_attn_bg.a ? t->sys_attn_bg
                     : (t->card_bg.a ? t->card_bg : t->surface);
                solid = t->sys_attn.a ? t->sys_attn : t->accent;
                break;
            }
            tk_canvas_fill_round_rect(c, q, 6, bg);
            tk_canvas_stroke_round_rect(c, q, 6, 1,
                t->control_stroke.a ? t->control_stroke
                                    : t->border);
            /* severity icon: a 23px disc (the 16epx glyph at
             * the device scale) at the 16epx content inset,
             * the glyph in the inverse text colour */
            int icx = q.x + 23 + 11, icy = q.y + q.h / 2;
            tk_canvas_fill_round_rect(c,
                (tk_rect){ icx - 11, icy - 11, 23, 23 }, 11,
                solid);
            tk_color gl = t->inverse_on_surface.a
                              ? t->inverse_on_surface
                              : (tk_color){ 255, 255, 255, 255 };
            switch (r->snack_severity) {
            case TK_SNACK_SUCCESS:
                snack_glyph_line(c, icx - 5, icy, icx - 1,
                                 icy + 4, gl);
                snack_glyph_line(c, icx - 1, icy + 4, icx + 6,
                                 icy - 4, gl);
                break;
            case TK_SNACK_ERROR:
                snack_glyph_line(c, icx - 4, icy - 4, icx + 4,
                                 icy + 4, gl);
                snack_glyph_line(c, icx - 4, icy + 4, icx + 4,
                                 icy - 4, gl);
                break;
            default: /* info "i" / warning "!" : stem + dot */
                snack_glyph_line(c, icx, icy - 5, icx, icy + 1,
                                 gl);
                tk_canvas_fill_rect(c,
                    (tk_rect){ icx - 1, icy + 4, 3, 3 }, gl);
                break;
            }
            inv_fg = t->text;
            inv_ac = t->accent;
            /* content inset 16epx + icon 16epx + margin 14epx
             * = 46epx = 66px at the device scale */
            text_x = q.x + 66;
        } else {
            tk_canvas_fill_round_rect(c, q, 12, inv_bg);
        }
        int text_w = q.x + q.w - text_x - 8 -
            (r->snack_action_rect.w ? r->snack_action_rect.w + 8 : 0);
        if (text_w < 0) text_w = 0;
        tk_canvas_push_clip(c, (tk_rect){ text_x, q.y, text_w, q.h });
        tk_canvas_draw_text(c, t, text_x, q.y + (q.h - lh) / 2,
                            r->snack_buf, inv_fg);
        tk_canvas_pop_clip(c);
        if (r->snack_action[0]) {
            tk_rect a = r->snack_action_rect;
            a.y += r->snack_off; /* M12: travels with the bar */
            int aw = tk_text_width(t, r->snack_action);
            tk_canvas_draw_text(c, t, a.x + (a.w - aw) / 2,
                                a.y + (a.h - lh) / 2,
                                r->snack_action, inv_ac);
        }
        /* M15: the action can hold keyboard focus (it is a
         * tab stop); the root's ring pass runs before this
         * transient pass, so the ring is repainted here, on
         * top of the bar, at the action's painted position. */
        if (r->focused && r->focused == r->snack_widget &&
            r->snack_action[0]) {
            tk_rect save = r->snack_widget->rect;
            r->snack_widget->rect.y += r->snack_off;
            paint_focus_ring(r, c, t);
            r->snack_widget->rect = save;
        }
    }
    if (r->toast_active) {
        tk_rect q = r->toast_rect;
        int lh = tk_line_height(t);
        int tw = tk_text_width(t, r->toast_buf);
        /* M13: the fade scales the whole toast (face, border
         * and text) by the timeline alpha */
        tk_color tbg = inv_bg, tfg = inv_fg, tbd = t->border;
        if (r->toast_alpha < 255) {
            tbg.a = (uint8_t)(tbg.a * r->toast_alpha / 255);
            tfg.a = (uint8_t)(tfg.a * r->toast_alpha / 255);
            tbd.a = (uint8_t)(tbd.a * r->toast_alpha / 255);
        }
        if (t->style == 2) {
            /* M12: the WinUI toast takes the theme's inverse
             * pairing like the M3 form (and as its own test and
             * AUDIT entry always claimed). It previously used
             * surface/text, which coincide numerically with the
             * inverse pair in the light theme only; the
             * TK10_DARK re-run exposed the divergence. The
             * WinUI corner (6) and border are kept. */
            tk_canvas_fill_round_rect(c, q, 6, tbg);
            tk_canvas_stroke_round_rect(c, q, 6, 1, tbd);
            tk_canvas_push_clip(c, q);
            tk_canvas_draw_text(c, t, q.x + (q.w - tw) / 2,
                                q.y + (q.h - lh) / 2, r->toast_buf,
                                tfg);
            tk_canvas_pop_clip(c);
        } else {
            tk_canvas_fill_round_rect(c, q, q.h / 2, tbg);
            tk_canvas_push_clip(c, q);
            if (t->style == 1) {
                /* M18: caption metrics, matching the AOSP
                 * geometry in toast_layout_rect */
                int ctw = tk_text_width_st(t, TK_TEXT_CAPTION,
                                           r->toast_buf);
                int clh = tk_line_height_st(t, TK_TEXT_CAPTION);
                tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                    q.x + (q.w - ctw) / 2, q.y + (q.h - clh) / 2,
                    r->toast_buf, tfg);
            } else {
                tk_canvas_draw_text(c, t, q.x + (q.w - tw) / 2,
                                    q.y + (q.h - lh) / 2,
                                    r->toast_buf, tfg);
            }
            tk_canvas_pop_clip(c);
        }
    }
    tk_m7_paint_drag(r, c, t); /* M7: drag shadow above all */
}

/* M13: the toast alpha timeline, evaluated per tick. The
 * fade-in occupies the first toast_fade_ms after the show,
 * the fade-out the last toast_fade_ms before the expiry; the
 * toast deactivates AT the expiry, as before. An explicit
 * hide's fade-out runs from the alpha at hide time. */
static void toast_tick(tk_root *r, uint32_t now_ms)
{
    if (!r->toast_active) return;
    const tk_theme *t = tk_theme_default();
    int f = r->toast_fade_ms > 0 ? r->toast_fade_ms : 150;
    if (r->toast_fading_out) {
        if (now_ms >= r->toast_hide_end || !toast_fade_live(r)) {
            toast_off_now(r);
            return;
        }
        float x = (float)(r->toast_hide_end - now_ms) / (float)f;
        int a = (int)(r->toast_hide_from * toast_fade_ease(t, x));
        if (a != r->toast_alpha) {
            r->toast_alpha = a;
            tk_damage_add(&r->dmg, r->toast_rect);
        }
        return;
    }
    if (now_ms >= r->toast_until) { toast_off_now(r); return; }
    if (!toast_fade_live(r)) {
        if (r->toast_alpha != 255) {
            r->toast_alpha = 255;
            tk_damage_add(&r->dmg, r->toast_rect);
        }
        return;
    }
    int a = 255;
    if (now_ms < r->toast_show_ms + (uint32_t)f) {
        float x = (float)(now_ms - r->toast_show_ms) / (float)f;
        int ai = (int)(255.0f * toast_fade_ease(t, x) + 0.5f);
        if (ai < a) a = ai;
    }
    if (now_ms + (uint32_t)f > r->toast_until) {
        float x = (float)(r->toast_until - now_ms) / (float)f;
        int ao = (int)(255.0f * toast_fade_ease(t, x) + 0.5f);
        if (ao < a) a = ao;
    }
    if (a != r->toast_alpha) {
        r->toast_alpha = a;
        tk_damage_add(&r->dmg, r->toast_rect);
    }
}

/* Expiry, driven from tk_root_tick. */
static void transients_tick(tk_root *r, uint32_t now_ms)
{
    toast_tick(r, now_ms);
    if (r->snack_active && !r->snack_forever &&
        now_ms >= r->snack_until)
        tk_snackbar_hide(r);
}

/* M7: snackbar pointer handling lives in tk_root_event: the
 * action zone fires on DOWN (the pre-M7 behaviour); a DOWN on
 * the bar body arms a pending dismissal that a swipe (MOVE
 * past 32px) or the tap's UP completes - the M3 swipe-to-
 * dismiss, with the tap form kept for this panel's tap-first
 * operation. */

void tk_root_tick(tk_root *r, uint32_t now_ms)
{
    if (!r) return;
    r->now_ms = now_ms;
    tk_anims_advance(r, now_ms); /* M5 animation engine */
    transients_tick(r, now_ms);  /* M6: toast/snackbar expiry */
    /* M6: pointer long-press - fire once at the threshold to the
     * widget that took the press; the eventual UP is clickless. */
    if (r->lp_target && !r->lp_fired &&
        now_ms - r->lp_since >= TK_LONGPRESS_MS) {
        r->lp_fired = true;
        r->suppress_click = true;
        tk_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TK_EV_LONGPRESS;
        ev.x = r->lp_x; ev.y = r->lp_y;
        tk_widget *tgt = r->lp_target;
        tk_rect tr = tgt->rect;
        (void)dispatch_chain(tgt, r, &ev);
        tk_damage_add(&r->dmg, tr);
    }
    if (r->tt_visible) return;
    tk_widget *hov = r->hover;
    if (!hov || !hov->tooltip) return;
    if (now_ms - r->hover_since < TK_TOOLTIP_DELAY_MS) return;
    /* show: snapshot the text (the widget may change it later) */
    size_t len = strlen(hov->tooltip);
    if (len >= sizeof(r->tt_buf)) len = sizeof(r->tt_buf) - 1;
    memcpy(r->tt_buf, hov->tooltip, len);
    r->tt_buf[len] = '\0';
    const tk_theme *t = tk_theme_default();
    int tw = tk_text_width(t, r->tt_buf) + 12;
    int th = tk_line_height(t) + 6;
    int x = r->hover_x + 10;
    int y = r->hover_y + 16;
    if (r->w > 0 && x + tw > r->w) x = r->w - tw;
    if (r->h > 0 && y + th > r->h) y = r->hover_y - th - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    r->tt_rect = (tk_rect){x, y, tw, th};
    r->tt_visible = true;
    tk_damage_add(&r->dmg, r->tt_rect);
}

bool tk_root_tooltip_visible(const tk_root *r)
{
    return r && r->tt_visible;
}

tk_rect tk_root_tooltip_rect(const tk_root *r)
{
    return r ? r->tt_rect : (tk_rect){0, 0, 0, 0};
}

/* Focus ring around the focused widget. The classic style
 * keeps the square ring; UI-STYLE-v2 follows the widget's shape
 * (a pill ring for single-line controls, a card-radius ring for
 * tall widgets) so the ring does not fight the rounded paints. */
static void paint_focus_ring(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    tk_rect fr = r->focused->rect;
    /* M15: a widget that covers the whole root (a modal
     * drawer's full-screen overlay, a full-screen dialog)
     * gets no ring - a stroke around the screen's edge marks
     * nothing; the focus inside such surfaces is carried by
     * their own cursor / content (the drawer's item cursor,
     * the dialog's focused control). */
    if (r->w > 0 && r->h > 0 && fr.x <= 0 && fr.y <= 0 &&
        fr.w >= r->w && fr.h >= r->h)
        return;
    tk_rect rr = { fr.x - 1, fr.y - 1, fr.w + 2, fr.h + 2 };
    if (t->style == 2) {
        /* M7 WinUI focus visual: a 2px outer stroke in the
         * FocusStrokeColorOuter role (near-black in light,
         * white in dark) with the inner stroke reversed - the
         * double-line focus rectangle of the Fluent controls. */
        tk_color outer = t->dark ? (tk_color){255, 255, 255, 255}
                                 : (tk_color){0, 0, 0, 228};
        /* M10: FocusStrokeInner is #FFFFFFFF in light and
         * #B2000000 in dark (Microsoft Learn Fluent token
         * table); the light inner was 179, now the spec 255. */
        tk_color inner = t->dark ? (tk_color){0, 0, 0, 178}
                                 : (tk_color){255, 255, 255, 255};
        tk_canvas_stroke_round_rect(c, rr, t->radius + 1, 2, outer);
        tk_canvas_stroke_round_rect(c, fr, t->radius, 1, inner);
    } else if (t->style == 1 && r->focused->kind == TK_W_SLIDER) {
        /* M21: the M3 slider's focus indicator is a ring
         * around the handle, not an outline of the whole
         * control (material-web attaches the focus ring to
         * each handle; the AndroidX-backed references agree).
         * The generic pill outline around a full-width slider
         * read as a stray frame. The ring hugs the displayed
         * thumb (the active handle on the Range form) with a
         * 2px offset, in the focus colour. */
        tk_widget *w = r->focused;
        float frac = w->sld_dual
            ? (w->sld_active == 0 ? w->sld_disp2 : w->sld_disp)
            : w->sld_disp;
        int cx = fr.x + (int)(frac * (float)fr.w + 0.5f);
        if (cx < fr.x + 14) cx = fr.x + 14;
        if (cx > fr.x + fr.w - 14) cx = fr.x + fr.w - 14;
        int cy = fr.y + fr.h / 2;
        tk_canvas_stroke_round_rect(c,
            (tk_rect){ cx - 16, cy - 16, 32, 32 }, 16, 2,
            t->focus);
    } else if (t->style == 1 && r->focused->kind == TK_W_TEXTFIELD) {
        /* UI v2 refinement: a filled pill field (the launcher /
         * settings search) already reads as a strong shape; the
         * 2px inflated accent ring on top of it read as a heavy
         * border. The M3 filled field's focus indication is a
         * restrained indicator, not a second outline - draw a
         * 1px ring on the field's own edge instead. */
        tk_canvas_stroke_round_rect(c, fr, fr.h / 2, 1, t->focus);
    } else if (t->style == 1) {
        int rad = rr.h <= 72 ? rr.h / 2
                             : (t->radius_list > 0 ? t->radius_list + 1
                                                  : t->radius + 1);
        tk_canvas_stroke_round_rect(c, rr, rad, 2, t->focus);
    } else {
        tk_canvas_stroke_rect(c, rr, 2, t->focus);
    }
}

/* M7: a modal sheet's scrim is painted by the root - the sheet
 * widget's own rect is the panel only, so the dim layer behind
 * it has to come from here (the M3 modal-sheet scrim).
 * M11: modal dialogs take the same root-painted scrim - the
 * M3 modal dialog scrim and the WinUI ContentDialog smoke
 * layer are both the theme's smoke token (the v2 themes carry
 * {0,0,0,82} = the 32% scrim, WinUI carries SmokeFillColor-
 * Default); until M11 the dialog was the only modal surface
 * without one. */
/* M20: the scaled-overlay paint path. An overlay whose
 * paint_scale is off its rest value (a menu inside its
 * entrance/exit transition) is composited at that scale about
 * its transform origin, without giving the canvas a general
 * transform: the backdrop under the overlay rect is saved,
 * the subtree paints exactly as at rest (the fade included),
 * the composited rect is captured, the backdrop is restored,
 * and the capture is transferred at the animated scale with
 * tk_canvas_blit_scaled. The scaled footprint is a subset of
 * the overlay rect, which the animation already invalidates
 * in full every tick, so the transfer adds no damage outside
 * the existing rect and nothing at rest: paint_scale == 1000
 * never reaches this path and no scratch surface is created.
 * The two scratch surfaces are root-owned, sized to the
 * largest scaled rect seen (bounded by the root surface),
 * and reused across paints.
 *
 * One transient property, recorded honestly: while the fade
 * makes the surface translucent, the backdrop showing through
 * it is part of the captured rect and is scaled with the
 * surface for the transition's duration (<= 167 ms). At rest
 * the rendering is byte-identical to the direct path. */
static void region_xfer(uint32_t *dp, int dw, int dx, int dy,
                        const uint32_t *sp, int sw, int sx, int sy,
                        int w, int h)
{
    for (int y = 0; y < h; y++)
        memcpy(dp + (size_t)(dy + y) * (size_t)dw + dx,
               sp + (size_t)(sy + y) * (size_t)sw + sx,
               (size_t)w * sizeof(uint32_t));
}

static tk_canvas *root_scratch(tk_canvas **slot, int w, int h)
{
    tk_canvas *s = *slot;
    if (s && tk_canvas_width(s) >= w && tk_canvas_height(s) >= h)
        return s;
    tk_canvas_free(s);
    *slot = tk_canvas_new(w, h);
    return *slot;
}

static void overlay_paint_scaled(tk_root *r, tk_widget *ov,
                                 tk_canvas *c, const tk_theme *t)
{
    tk_rect R = ov->rect;
    tk_rect vis = tk_rect_intersect(R, (tk_rect){0, 0,
        tk_canvas_width(c), tk_canvas_height(c)});
    if (vis.w <= 0 || vis.h <= 0) return;
    tk_canvas *save = root_scratch(&r->scale_save, vis.w, vis.h);
    tk_canvas *surf = root_scratch(&r->scale_surf, vis.w, vis.h);
    if (!save || !surf) { tk_paint(ov, c, t); return; }
    uint32_t *cp = tk_canvas_pixels(c);
    int cw = tk_canvas_width(c);
    int sw_save = tk_canvas_width(save);
    int sw_surf = tk_canvas_width(surf);
    /* 1: save the backdrop the direct paint is about to cover */
    region_xfer(tk_canvas_pixels(save), sw_save, 0, 0,
                cp, cw, vis.x, vis.y, vis.w, vis.h);
    /* 2: the subtree paints exactly as at rest (fade included) */
    tk_paint(ov, c, t);
    /* 3: capture the composited surface */
    region_xfer(tk_canvas_pixels(surf), sw_surf, 0, 0,
                cp, cw, vis.x, vis.y, vis.w, vis.h);
    /* 4: restore the backdrop */
    region_xfer(cp, cw, vis.x, vis.y,
                tk_canvas_pixels(save), sw_save, 0, 0,
                vis.w, vis.h);
    tk_canvas_invalidate(c, vis);
    /* 5: transfer at the animated scale about the origin; the
     * visible sub-rect maps through the same transform */
    float s = ov->paint_scale / 1000.0f;
    float ox = R.x + R.w * (ov->scale_ox / 1000.0f);
    float oy = R.y + R.h * (ov->scale_oy / 1000.0f);
    tk_rect dst = {
        (int)lrintf(ox + (vis.x - ox) * s),
        (int)lrintf(oy + (vis.y - oy) * s),
        (int)lrintf(vis.w * s), (int)lrintf(vis.h * s) };
    if (dst.w < 1) dst.w = 1;
    if (dst.h < 1) dst.h = 1;
    tk_canvas_blit_scaled(c, surf, (tk_rect){0, 0, vis.w, vis.h},
                          dst);
}

static void overlay_paint(tk_root *r, tk_widget *ov, tk_canvas *c,
                          const tk_theme *t)
{
    if ((ov->kind == TK_W_SHEET && ov->sheet_modal) ||
        (ov->kind == TK_W_DIALOG && r->modal == ov))
        tk_canvas_fill_rect(c, (tk_rect){0, 0, r->w, r->h}, t->smoke);
    /* M20: mid-scale overlays (menus in their transition)
     * paint through the scaled path; at rest the direct path
     * below is the pre-M20 painter, unchanged. */
    if (ov->paint_scale != 1000 && ov->paint_scale > 0)
        overlay_paint_scaled(r, ov, c, t);
    else
        tk_paint(ov, c, t);
}

void tk_root_paint(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    tk_canvas_clear(c, t->bg);
    tk_paint(r->top, c, t);
    for (size_t i = 0; i < r->n_overlays; i++)
        overlay_paint(r, r->overlays[i], c, t);
    /* focus ring for the focused widget - only while the widget
     * is actually on screen (a hidden ancestor, e.g. the closed
     * launcher around its search field, must not leave a stray
     * ring floating over the desktop) */
    if (r->focused) {
        bool vis = true;
        for (tk_widget *p = r->focused; p; p = p->parent)
            if (!p->visible) { vis = false; break; }
        if (vis) paint_focus_ring(r, c, t);
    }
    tooltip_paint(r, c, t);
    transient_paint(r, c, t); /* M6: toast/snackbar above all */
    /* M5: a full repaint is a known-good shift base for every
     * ScrollView it covered. */
    tk_sv_note_full_paint(r->top);
    for (size_t i = 0; i < r->n_overlays; i++)
        tk_sv_note_full_paint(r->overlays[i]);
}

/* M5 pre-shift pass: for each ScrollView fully inside the
 * damage rect d whose content is unchanged and whose painted
 * state is a valid shift base, shift the retained pixels in the
 * canvas NOW (before the background fill, which would erase
 * them). The shifted rects are collected so the background fill
 * can skip them; tk_paint then repaints band + strip only. */
static int preshift_walk(tk_widget *w, bool ancestors_visible,
                         tk_canvas *c, tk_rect d,
                         tk_rect *shifted, int max, int n)
{
    if (!w) return n;
    bool vis = ancestors_visible && w->visible;
    if (vis && w->kind == TK_W_SCROLLVIEW && w->n_children &&
        w->sv_paint_valid && !w->sv_dirty &&
        w->scroll_y != w->sv_painted_y) {
        int dy = w->scroll_y - w->sv_painted_y; /* content: -dy */
        bool inside = d.x <= w->rect.x && d.y <= w->rect.y &&
            d.x + d.w >= w->rect.x + w->rect.w &&
            d.y + d.h >= w->rect.y + w->rect.h;
        if (inside && dy < w->rect.h && dy > -w->rect.h) {
            tk_rect band = tk_canvas_shift_region(c, w->rect, -dy);
            if (band.w > 0 && band.h > 0) {
                w->sv_band = band;
                /* The strip repaint zone must cover the thumb's
                 * travel AND the ghost of the old thumb: the
                 * shift moved the old thumb's pixels by the full
                 * content delta, not by the thumb's own (much
                 * smaller) travel. */
                tk_rect z = tk_sv_thumb_zone(w, w->sv_painted_y,
                                             w->scroll_y);
                tk_rect g = tk_sv_thumb_rect(w, w->sv_painted_y);
                g.y -= dy;
                int y0 = z.y < g.y ? z.y : g.y;
                int y1 = z.y + z.h > g.y + g.h ? z.y + z.h : g.y + g.h;
                if (y0 < w->rect.y) y0 = w->rect.y;
                if (y1 > w->rect.y + w->rect.h) y1 = w->rect.y + w->rect.h;
                w->sv_strip = (tk_rect){ w->rect.x + w->rect.w - 8,
                                         y0, 8, y1 - y0 };
                w->sv_shift_done = true;
                if (n < max) shifted[n++] = w->rect;
            }
        }
    }
    for (size_t i = 0; i < w->n_children; i++)
        n = preshift_walk(w->children[i], vis, c, d, shifted, max, n);
    return n;
}

/* Fill rect d with col, skipping the `n` hole rects (each hole
 * splits a rect into at most 4 strips). */
static void fill_except(tk_canvas *c, tk_rect d, const tk_rect *holes,
                        int n, tk_color col)
{
    tk_rect list[32];
    int ln = 1;
    list[0] = d;
    for (int i = 0; i < n && ln > 0; i++) {
        tk_rect out[32];
        int on = 0;
        for (int j = 0; j < ln; j++) {
            tk_rect a = list[j], h = holes[i];
            tk_rect in = tk_rect_intersect(a, h);
            if (in.w <= 0 || in.h <= 0) { out[on++] = a; continue; }
            tk_rect parts[4] = {
                { a.x, a.y, a.w, in.y - a.y },                       /* top */
                { a.x, in.y + in.h, a.w, a.y + a.h - (in.y + in.h) },/* bottom */
                { a.x, in.y, in.x - a.x, in.h },                     /* left */
                { in.x + in.w, in.y, a.x + a.w - (in.x + in.w), in.h },/* right */
            };
            for (int k = 0; k < 4; k++)
                if (parts[k].w > 0 && parts[k].h > 0 && on < 32)
                    out[on++] = parts[k];
        }
        memcpy(list, out, (size_t)on * sizeof(tk_rect));
        ln = on;
    }
    for (int j = 0; j < ln; j++) tk_canvas_fill_rect(c, list[j], col);
}

/* Damage-driven repaint (M3): repaint only the accumulated damage
 * rects. The full-repaint fallback (dmg.full) is tk_root_paint(). */
int tk_root_paint_damage(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    if (!r || !c) return 0;
    if (r->dmg.full) {
        tk_root_paint(r, c, t);
        tk_damage_clear(&r->dmg);
        return 1;
    }
    if (!r->dmg.n) return 0;
    /* snapshot: painting itself must not mutate the list mid-walk */
    size_t n = r->dmg.n;
    tk_rect *rects = malloc(n * sizeof(*rects));
    if (!rects) {
        tk_root_paint(r, c, t);
        tk_damage_clear(&r->dmg);
        return 1;
    }
    memcpy(rects, r->dmg.rects, n * sizeof(*rects));
    tk_damage_clear(&r->dmg);
    int cw = tk_canvas_width(c), ch = tk_canvas_height(c);
    for (size_t i = 0; i < n; i++) {
        tk_rect d = tk_rect_intersect(rects[i], (tk_rect){0, 0, cw, ch});
        if (d.w <= 0 || d.h <= 0) continue;
        /* M5: shift eligible ScrollViews before the bg fill */
        tk_rect shifted[8];
        int nsh = 0;
        if (r->scroll_blit) {
            nsh = preshift_walk(r->top, true, c, d, shifted, 8, 0);
            for (size_t j = 0; j < r->n_overlays; j++)
                nsh = preshift_walk(r->overlays[j], true, c, d,
                                    shifted, 8, nsh);
        }
        fill_except(c, d, shifted, nsh, t->bg);
        tk_canvas_push_clip(c, d);
        tk_paint(r->top, c, t);
        for (size_t j = 0; j < r->n_overlays; j++)
            overlay_paint(r, r->overlays[j], c, t);
        if (r->focused) paint_focus_ring(r, c, t);
        tooltip_paint(r, c, t);
        transient_paint(r, c, t); /* M6 */
        tk_canvas_pop_clip(c);
    }
    free(rects);
    return (int)n;
}

void tk_root_paint_overlays(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    if (!r || !c) return;
    for (size_t i = 0; i < r->n_overlays; i++)
        overlay_paint(r, r->overlays[i], c, t);
    if (r->focused) {
        bool vis = true;
        for (tk_widget *p = r->focused; p; p = p->parent)
            if (!p->visible) { vis = false; break; }
        if (vis) paint_focus_ring(r, c, t);
    }
    tooltip_paint(r, c, t);
    transient_paint(r, c, t);
}

tk_widget *tk_focused(const tk_root *r) { return r->focused; }

static void set_focus(tk_root *r, tk_widget *w)
{
    if (r->focused == w) return;
    tk_event ev;
    if (r->focused) {
        /* M3: the old focus ring / caret must be repainted away */
        tk_rect fr = r->focused->rect;
        tk_damage_add(&r->dmg, (tk_rect){fr.x - 2, fr.y - 2,
                                         fr.w + 4, fr.h + 4});
        memset(&ev, 0, sizeof(ev)); ev.type = TK_EV_FOCUS_OUT;
        (void)tk_leaf_event(r->focused, r, &ev);
    }
    r->focused = w;
    if (w) {
        tk_rect fr = w->rect;
        tk_damage_add(&r->dmg, (tk_rect){fr.x - 2, fr.y - 2,
                                         fr.w + 4, fr.h + 4});
        memset(&ev, 0, sizeof(ev)); ev.type = TK_EV_FOCUS_IN;
        (void)tk_leaf_event(w, r, &ev);
    }
}

bool tk_focus(tk_root *r, tk_widget *w)
{
    if (!w || !w->can_focus || !w->visible || !w->enabled) return false;
    if (r->modal && !tk_is_descendant(r->modal, w) &&
        !tk_root_has_overlay(r, w) && w != r->modal)
        return false; /* modal dialog traps focus */
    set_focus(r, w);
    return true;
}

static void collect_focusable(tk_widget *w, tk_widget ***arr, size_t *n, size_t *cap)
{
    if (!w->visible || !w->enabled) return;
    if (w->can_focus) {
        if (*n >= *cap) {
            size_t nc = *cap ? *cap * 2 : 8;
            tk_widget **na = realloc(*arr, nc * sizeof(*na));
            if (!na) return;
            *arr = na; *cap = nc;
        }
        (*arr)[(*n)++] = w;
    }
    for (size_t i = 0; i < w->n_children; i++) {
        if (w->kind == TK_W_TABS && (int)i != w->active_tab) continue;
        collect_focusable(w->children[i], arr, n, cap);
    }
}

static bool focus_step(tk_root *r, int dir)
{
    tk_widget **arr = NULL; size_t n = 0, cap = 0;
    collect_focusable(r->modal ? r->modal : r->top, &arr, &n, &cap);
    /* M14: a modal dialog's own surface is not a Tab stop
     * when it has focusable content - the cycle runs through
     * the controls only (the ContentDialog model). A dialog
     * without focusable content keeps its surface stop. */
    if (r->modal && n > 1 && arr[0] == r->modal) {
        memmove(arr, arr + 1, (n - 1) * sizeof(*arr));
        n--;
    }
    /* M15: a visible snackbar's action is a tab stop of its
     * own (M3: the action is a text button, reached by
     * ordinary focus traversal; showing the bar never steals
     * focus, so the stop exists only while the bar is up).
     * A modal keeps its trap exclusive. */
    if (!r->modal && r->snack_active && r->snack_action[0] &&
        r->snack_widget) {
        if (n >= cap) {
            size_t nc = cap ? cap * 2 : 8;
            tk_widget **na = realloc(arr, nc * sizeof(*na));
            if (na) { arr = na; cap = nc; }
        }
        if (n < cap) arr[n++] = r->snack_widget;
    }
    if (n == 0) { free(arr); return false; }
    long idx = -1;
    for (size_t i = 0; i < n; i++)
        if (arr[i] == r->focused) idx = (long)i;
    long next;
    if (idx < 0) next = dir > 0 ? 0 : (long)n - 1;
    else next = (idx + dir + (long)n) % (long)n;
    set_focus(r, arr[next]);
    free(arr);
    return true;
}

/* M15: focus the first focusable widget of a subtree (see
 * tk_internal.h); mirrors the dialog presenter's scan. */
tk_widget *tk_focus_first(tk_root *r, tk_widget *w)
{
    if (!r || !w) return NULL;
    tk_widget **arr = NULL; size_t n = 0, cap = 0;
    collect_focusable(w, &arr, &n, &cap);
    tk_widget *first = NULL;
    for (size_t i = 0; i < n; i++)
        if (arr[i] != w) { first = arr[i]; break; }
    free(arr);
    if (first) { set_focus(r, first); return first; }
    if (w->can_focus && w->enabled && w->visible) {
        set_focus(r, w);
        return w;
    }
    return NULL;
}

bool tk_focus_next(tk_root *r) { return focus_step(r, 1); }
bool tk_focus_prev(tk_root *r) { return focus_step(r, -1); }

/* ---------------------------------------------------------------- */
/* Dialog show/close (modal overlay)                                 */
/* ---------------------------------------------------------------- */

static void dialog_show_impl(tk_root *r, tk_widget *dlg, bool modal)
{
    if (!r || !dlg) return;
    dlg->show_clock = r->anim_now; /* M12: exit-animation gate */
    dlg->dlg_closing = false;
    tk_root_add_overlay(r, dlg);
    if (modal) r->modal = dlg;
    if (r->w > 0 && r->h > 0) layout_overlay(r, dlg);
    tk_damage_add(&r->dmg, dlg->rect); /* M3 */
    if (modal && r->w > 0 && r->h > 0)
        /* M11: the scrim covers the whole root, so the whole
         * root is damage, not just the dialog's rect */
        tk_damage_add(&r->dmg, (tk_rect){0, 0, r->w, r->h});
    tk_overlay_slide_in(r, dlg, 12); /* M5: rises into place */
    /* M14: remember who held the focus; closing the dialog
     * returns it there (see tk_root_remove_overlay) */
    dlg->focus_return = (r->focused && r->focused != dlg &&
                         !tk_is_descendant(dlg, r->focused))
                            ? r->focused : NULL;
    /* focus the first focusable widget inside (trap starts
     * there) - the dialog's own surface only when it has no
     * focusable content (M14: collect puts the dialog first
     * because it is itself focusable) */
    tk_widget **arr = NULL; size_t n = 0, cap = 0;
    collect_focusable(dlg, &arr, &n, &cap);
    tk_widget *first = NULL;
    for (size_t i = 0; i < n; i++)
        if (arr[i] != dlg) { first = arr[i]; break; }
    if (first) set_focus(r, first);
    else if (dlg->can_focus) set_focus(r, dlg);
    free(arr);
}

void tk_dialog_show(tk_root *r, tk_widget *dlg)
{
    dialog_show_impl(r, dlg, true);
}

/* M7: non-modal form - a plain overlay; input elsewhere stays
 * live and there is no focus trap. */
void tk_dialog_show_modeless(tk_root *r, tk_widget *dlg)
{
    dialog_show_impl(r, dlg, false);
}

void tk_dialog_set_fullscreen(tk_widget *dlg, bool full)
{
    if (!dlg || dlg->kind != TK_W_DIALOG) return;
    if (dlg->dlg_fullscreen != full) {
        dlg->dlg_fullscreen = full;
        tk_widget_invalidate(dlg);
    }
}

bool tk_dialog_is_fullscreen(const tk_widget *dlg)
{
    return dlg && dlg->kind == TK_W_DIALOG && dlg->dlg_fullscreen;
}

bool tk_dialog_is_modal(const tk_root *r, const tk_widget *dlg)
{
    return r && dlg && r->modal == dlg;
}

/* M12: dialog exit - the surface slides back down the 12 px
 * it rose on entry and is removed when the slide lands. The
 * exit only engages when the root's animation clock advanced
 * while the dialog was up (proof of a live session loop);
 * headless / one-shot use keeps the synchronous close every
 * existing caller was written against. */
static void dialog_exit_done(void *ud)
{
    tk_widget *dlg = ud;
    tk_root *r = tk_widget_root(dlg);
    dlg->dlg_closing = false;
    if (r) tk_root_remove_overlay(r, dlg);
}

void tk_dialog_close(tk_root *r, tk_widget *dlg)
{
    if (!r || !dlg) return;
    if (dlg->dlg_closing) return; /* exit already running */
    if (r->anim_now > dlg->show_clock &&
        tk_root_animations_enabled(r) &&
        tk_root_has_overlay(r, dlg)) {
        uint32_t dur; tk_ease_fn ease;
        tk_anim_mode_pair(false, &dur, &ease);
        dlg->dlg_closing = true;
        tk_overlay_slide_out(r, dlg, 0, 12, dur, ease,
                             dialog_exit_done, dlg);
        return;
    }
    tk_root_remove_overlay(r, dlg); /* also clears modal + focus */
}

bool tk_dialog_is_shown(const tk_root *r, const tk_widget *dlg)
{
    return tk_root_has_overlay(r, dlg);
}

/* Hit testing that honors overlays and modality. */
static tk_widget *root_hit(tk_root *r, int x, int y)
{
    if (r->modal) return tk_hit_test(r->modal, x, y);
    for (size_t i = r->n_overlays; i-- > 0;) {
        tk_widget *hit = tk_hit_test(r->overlays[i], x, y);
        if (hit) return hit;
    }
    return tk_hit_test(r->top, x, y);
}

static bool hit_in_overlay(tk_root *r, int x, int y)
{
    for (size_t i = 0; i < r->n_overlays; i++)
        if (tk_hit_test(r->overlays[i], x, y)) return true;
    return false;
}

void tk_root_damage_hint(tk_root *r, tk_rect rect)
{
    if (!r || rect.w <= 0 || rect.h <= 0) return;
    if (r->dmg_hint_n < 3) r->dmg_hint[r->dmg_hint_n++] = rect;
}

bool tk_root_event(tk_root *r, tk_event *ev)
{
    ev->handled = false; /* callers may reuse an event struct */
    r->dmg_hint_n = 0; /* handlers may narrow this event */
    bool handled = false;
    /* M3: the handled target's rect, captured BY VALUE before
     * dispatch - a handler may free its widget (auto_free combo
     * dropdowns do), so the pointer must not be touched afterwards */
    tk_rect affected_rect = {0, 0, 0, 0};
    bool has_affected = false;
    switch (ev->type) {
    case TK_EV_POINTER_DOWN: {
        /* M9: a fresh press voids the previous press's
         * dismissal record (see the menu dismissal below) */
        r->down_dismissed_menu = NULL;
        r->down_dismissed_owner = NULL;
        /* M6/M7: the snackbar floats above everything and takes
         * its own taps first. The action zone fires on DOWN;
         * the body arms a swipe/tap dismissal (see MOVE/UP). */
        if (r->snack_active &&
            tk_rect_contains(r->snack_rect, ev->x, ev->y)) {
            if (r->snack_action[0] && r->snack_cb &&
                tk_rect_contains(r->snack_action_rect, ev->x, ev->y)) {
                tk_notify_cb cb = r->snack_cb;
                void *ud = r->snack_ud;
                tk_snackbar_hide(r);
                cb(r->snack_widget, ud);
                return true;
            }
            r->snack_down_on = true;
            r->snack_down_x = ev->x;
            r->snack_down_y = ev->y;
            return true;
        }
        tooltip_hide(r);
        /* clicking the tree while a menu is open dismisses the menu */
        if (!r->modal && r->n_overlays && !hit_in_overlay(r, ev->x, ev->y)) {
            /* M9: a press on the anchor of an already-open
             * split menu is that menu's toggle-off (the M3
             * split button's menu button toggles). Without
             * this, the dismissal below closes the menu and
             * the anchor's click handler - which runs on UP
             * and itself implements the toggle - reopens it
             * at once, so the menu could never be closed from
             * its anchor. Swallow the press after dismissing
             * so the UP finds no pressed anchor. */
            tk_widget *hit0 = tk_hit_test(r->top, ev->x, ev->y);
            bool split_anchor = false;
            for (tk_widget *a = hit0; a; a = a->parent)
                if (a->split_menu &&
                    tk_menu_is_open(r, a->split_menu)) {
                    split_anchor = true;
                    break;
                }
            /* M9: record what this press is about to dismiss
             * (the topmost open menu and its owner). Anchors
             * whose activation runs later in this same press
             * (menubar titles on UP, combobox arrows on DOWN)
             * consult the record and treat the press as the
             * menu's toggle-off instead of reopening it. */
            for (size_t oi = 0; oi < r->n_overlays; oi++)
                if (r->overlays[oi]->kind == TK_W_MENU) {
                    /* auto_free menus are freed by the close:
                     * never record their address (the owner
                     * record below covers their anchors) */
                    if (!r->overlays[oi]->auto_free)
                        r->down_dismissed_menu = r->overlays[oi];
                    r->down_dismissed_owner =
                        r->overlays[oi]->owner_ref;
                }
            tk_close_all_menus(r);
            if (split_anchor) return true;
        }
        tk_widget *hit = root_hit(r, ev->x, ev->y);
        if (!hit) {
            if (r->modal) {
                /* M7: a modal sheet dismisses on a scrim tap */
                tk_m7_overlay_outside_down(r, r->modal);
                return true; /* swallowed by modality */
            }
            set_focus(r, NULL); return false;
        }
        /* focus the nearest focusable ancestor (including hit);
         * overlay widgets (menus) have no parent chain, so an
         * overlay hit itself is the focus candidate */
        tk_widget *f = hit;
        while (f && !f->can_focus) f = f->parent;
        if (f) set_focus(r, f);
        else if (tk_root_has_overlay(r, hit)) set_focus(r, hit->can_focus ? hit : r->focused);
        r->capture = hit;
        /* M7: arm a drag when the hit widget or an ancestor is
         * a drag source; the payload is copied into the root so
         * the source may change freely during the drag */
        r->drag_armed = false;
        r->drag_on = false;
        r->drag_src = NULL;
        for (tk_widget *a = hit; a; a = a->parent) {
            if (!a->drag_mime) continue;
            free(r->drag_mime);
            free(r->drag_data);
            r->drag_mime = tk_strdup(a->drag_mime);
            r->drag_len = a->drag_len;
            r->drag_data = NULL;
            if (a->drag_data && a->drag_len) {
                r->drag_data = malloc(a->drag_len);
                if (r->drag_data)
                    memcpy(r->drag_data, a->drag_data, a->drag_len);
            }
            snprintf(r->drag_label, sizeof(r->drag_label), "%s",
                     a->drag_label ? a->drag_label : "");
            r->drag_src = a;
            r->drag_armed = true;
            r->drag_down_x = ev->x;
            r->drag_down_y = ev->y;
            r->drag_x = ev->x;
            r->drag_y = ev->y;
            break;
        }
        /* M6: long-press tracking starts at the press target */
        r->lp_target = hit;
        r->lp_since = r->now_ms;
        r->lp_x = ev->x; r->lp_y = ev->y;
        r->lp_fired = false;
        /* remember the nearest ScrollView ancestor for the
         * capture-phase drag gesture (see POINTER_MOVE) */
        r->drag_sv = NULL;
        r->down_y = ev->y;
        for (tk_widget *a = hit; a; a = a->parent)
            if (a->kind == TK_W_SCROLLVIEW) { r->drag_sv = a; break; }
        affected_rect = hit->rect; has_affected = true;
        tk_sv_mark_dirty_from(hit); /* M5: state may change inside */
        handled = dispatch_chain(hit, r, ev);
        break;
    }
    case TK_EV_POINTER_MOVE: {
        /* M7: snackbar swipe-to-dismiss - a pending body press
         * that travels past the threshold dismisses at once;
         * smaller moves are held by the bar */
        if (r->snack_down_on) {
            int dx = ev->x - r->snack_down_x;
            int dy = ev->y - r->snack_down_y;
            if (dx > 32 || dx < -32 || dy > 32 || dy < -32) {
                r->snack_down_on = false;
                tk_snackbar_hide(r);
            }
            return true;
        }
        /* M7: drag tracking - past the slop the drag shadow goes
         * live and the gesture stops being a click/scroll */
        if (r->drag_armed || r->drag_on) {
            if (!r->drag_on &&
                (ev->x - r->drag_down_x > 12 ||
                 r->drag_down_x - ev->x > 12 ||
                 ev->y - r->drag_down_y > 12 ||
                 r->drag_down_y - ev->y > 12)) {
                r->drag_on = true;
                r->suppress_click = true;
                r->lp_target = NULL; /* not a long-press either */
                /* a drag-scroll that already took over yields:
                 * the drag owns the rest of the gesture */
                if (r->drag_sv) {
                    r->drag_sv->dragging = false;
                    r->drag_sv->ptr_down = false;
                    r->drag_sv = NULL;
                }
                r->capture = r->drag_src;
            }
            if (r->drag_on) {
                tk_damage_add(&r->dmg, tk_drag_shadow_rect(r));
                r->drag_x = ev->x;
                r->drag_y = ev->y;
                tk_damage_add(&r->dmg, tk_drag_shadow_rect(r));
                return true;
            }
        }
        /* M6: leaving the slop radius cancels a pending
         * long-press (the gesture is becoming a drag) */
        if (r->lp_target && !r->lp_fired &&
            (ev->x - r->lp_x > TK_LONGPRESS_SLOP ||
             r->lp_x - ev->x > TK_LONGPRESS_SLOP ||
             ev->y - r->lp_y > TK_LONGPRESS_SLOP ||
             r->lp_y - ev->y > TK_LONGPRESS_SLOP))
            r->lp_target = NULL;
        /* drag-scroll takeover: once the finger has moved past the
         * threshold, the ScrollView ancestor steals the capture
         * from whatever child consumed the DOWN */
        if (r->drag_sv && !r->drag_sv->dragging &&
            (ev->y - r->down_y > 6 || ev->y - r->down_y < -6)) {
            r->lp_target = NULL; /* M6: a scroll is not a press */
            tk_widget *sv = r->drag_sv;
            sv->ptr_down = true;
            sv->dragging = true;
            sv->drag_start_y = r->down_y;
            sv->drag_start_scroll = sv->scroll_y;
            sv->last_y = r->down_y;
            sv->vel_y = 0;
            r->suppress_click = true;
            r->capture = sv;
        }
        /* M3: tooltip hover tracking (uncaptured moves only) */
        if (!r->capture) {
            tk_widget *hov = root_hit(r, ev->x, ev->y);
            if (hov != r->hover) {
                tooltip_hide(r);
                r->hover = hov;
                r->hover_x = ev->x; r->hover_y = ev->y;
                r->hover_since = r->now_ms;
            } else if (hov &&
                       (ev->x - r->hover_x > 3 || r->hover_x - ev->x > 3 ||
                        ev->y - r->hover_y > 3 || r->hover_y - ev->y > 3)) {
                tooltip_hide(r); /* moved: restart the rest timer */
                r->hover_x = ev->x; r->hover_y = ev->y;
                r->hover_since = r->now_ms;
            }
        }
        tk_widget *target = r->capture;
        if (!target) target = root_hit(r, ev->x, ev->y);
        if (target) { affected_rect = target->rect; has_affected = true; }
        /* M5: MOVE is deliberately not a dirty source (a drag
         * scroll dispatches a stream of them and changes no
         * content) - except a captured Slider, whose value follows
         * the pointer. */
        if (target && target->kind == TK_W_SLIDER)
            tk_sv_mark_dirty_from(target);
        if (target) handled = dispatch_chain(target, r, ev);
        break;
    }
    case TK_EV_POINTER_UP: {
        /* M7: a snackbar body tap completes its dismissal */
        if (r->snack_down_on) {
            r->snack_down_on = false;
            tk_snackbar_hide(r);
            return true;
        }
        /* M7: drop resolution - the nearest widget in the hit
         * chain with an on_drop callback receives the payload;
         * releasing anywhere else cancels the drag silently */
        if (r->drag_armed || r->drag_on) {
            bool was_on = r->drag_on;
            tk_widget *src = r->drag_src;
            r->drag_armed = false;
            r->drag_on = false;
            r->drag_src = NULL;
            r->lp_target = NULL;
            if (was_on) {
                r->capture = NULL;
                tk_damage_add(&r->dmg, tk_drag_shadow_rect(r));
                tk_widget *hit = root_hit(r, ev->x, ev->y);
                tk_widget *tgt = NULL;
                for (tk_widget *a = hit; a; a = a->parent)
                    if (a->drop_cb) { tgt = a; break; }
                if (tgt && tgt != src)
                    tgt->drop_cb(tgt, tgt->drop_ud, r->drag_mime,
                                 r->drag_data, r->drag_len,
                                 ev->x, ev->y);
                free(r->drag_mime); r->drag_mime = NULL;
                free(r->drag_data); r->drag_data = NULL;
                r->drag_len = 0;
                r->suppress_click = false; /* gesture consumed */
                return true;
            }
            /* armed but never dragged: fall through as a click */
        }
        tk_widget *target = r->capture;
        if (!target) target = root_hit(r, ev->x, ev->y);
        if (target) { affected_rect = target->rect; has_affected = true; }
        if (target) {
            tk_sv_mark_dirty_from(target); /* M5 */
            handled = dispatch_chain(target, r, ev);
        }
        r->capture = NULL;
        r->drag_sv = NULL;
        r->lp_target = NULL;   /* M6: the press sequence is over */
        r->lp_fired = false;
        r->suppress_click = false; /* consumed by handlers already */
        break;
    }
    case TK_EV_WHEEL: {
        tk_widget *target = r->focused;
        if (!target) target = root_hit(r, ev->x, ev->y);
        if (target) { affected_rect = target->rect; has_affected = true; }
        if (target) {
            tk_sv_mark_dirty_from(target); /* M5 */
            handled = dispatch_chain(target, r, ev);
        }
        break;
    }
    case TK_EV_KEY_DOWN:
    case TK_EV_KEY_UP:
    case TK_EV_TEXT_INPUT:
    case TK_EV_COMPOSITION: {
        if (ev->type == TK_EV_KEY_DOWN) tooltip_hide(r);
        tk_widget *target = r->focused ? r->focused : r->top;
        if (target) { affected_rect = target->rect; has_affected = true; }
        tk_sv_mark_dirty_from(target); /* M5 */
        handled = dispatch_chain(target, r, ev);
        if (!handled && ev->type == TK_EV_KEY_DOWN && ev->key == TK_KEY_TAB) {
            handled = (ev->modifiers & TK_MOD_SHIFT) ? tk_focus_prev(r)
                                                     : tk_focus_next(r);
            ev->handled = handled;
        }
        break;
    }
    default:
        break;
    }
    /* M3: a handled event repaints its target's rect only -
     * or the narrower rects the handler declared via
     * tk_root_damage_hint(), each clipped to the target's rect
     * (if none intersects, the full rect stands) */
    if (handled && has_affected) {
        bool narrowed = false;
        for (int i = 0; i < r->dmg_hint_n; i++) {
            tk_rect hint = r->dmg_hint[i], dmg = affected_rect;
            int x1 = hint.x > dmg.x ? hint.x : dmg.x;
            int y1 = hint.y > dmg.y ? hint.y : dmg.y;
            int x2 = hint.x + hint.w < dmg.x + dmg.w ? hint.x + hint.w
                                                     : dmg.x + dmg.w;
            int y2 = hint.y + hint.h < dmg.y + dmg.h ? hint.y + hint.h
                                                     : dmg.y + dmg.h;
            if (x2 > x1 && y2 > y1) {
                tk_damage_add(&r->dmg,
                              (tk_rect){ x1, y1, x2 - x1, y2 - y1 });
                narrowed = true;
            }
        }
        if (!narrowed) tk_damage_add(&r->dmg, affected_rect);
    }
    return handled;
}
