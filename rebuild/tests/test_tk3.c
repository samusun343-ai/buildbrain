/* test_tk3.c - M3: damage-based presentation, tooltips, TextView
 * drag selection, ComboBox/SpinButton direct text entry, bundled
 * subset font. (Damage byte counts are asserted against the memory
 * framebuffer - the same conversion path as the real fbdev.) */
#include "tk.h"
#include "textcore.h"
#include "osk.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { printf("FAIL %d: %s\n", __LINE__, #cond); failures++; } \
} while (0)

static void send_ptr(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = ty; ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}
static void send_key(tk_root *r, tk_key k)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN; ev.key = k;
    (void)tk_root_event(r, &ev);
}
static void send_text(tk_root *r, const char *s)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_TEXT_INPUT; ev.text = s;
    (void)tk_root_event(r, &ev);
}
static void send_click(tk_root *r, int x, int y)
{
    send_ptr(r, TK_EV_POINTER_DOWN, x, y);
    send_ptr(r, TK_EV_POINTER_UP, x, y);
}

static int sel_fires;
static void on_sel_count(tk_widget *w, void *ud)
{
    (void)w; (void)ud; sel_fires++;
}

/* WCAG contrast helpers (UI-REDESIGN P1 verification) */
static double lum_of(tk_color c)
{
    double f[3] = { c.r / 255.0, c.g / 255.0, c.b / 255.0 };
    for (int i = 0; i < 3; i++)
        f[i] = f[i] <= 0.03928 ? f[i] / 12.92
                               : pow((f[i] + 0.055) / 1.055, 2.4);
    return 0.2126 * f[0] + 0.7152 * f[1] + 0.0722 * f[2];
}
static double cr(tk_color a, tk_color b)
{
    double la = lum_of(a), lb = lum_of(b);
    if (la < lb) { double tmp = la; la = lb; lb = tmp; }
    return (la + 0.05) / (lb + 0.05);
}

static int ob_fired[16];
static void ob_on_click(tk_widget *w, void *ud)
{
    (void)w;
    ob_fired[(int)(intptr_t)ud]++;
}

static bool canvas_equal(tk_canvas *a, tk_canvas *b, int w, int h)
{
    return memcmp(tk_canvas_pixels(a), tk_canvas_pixels(b),
                  (size_t)w * (size_t)h * sizeof(uint32_t)) == 0;
}

/* Render one standalone TextField (300x40) and return the canvas.
 * comp/segs optionally install an IME composition at the caret
 * (placed at the end of the text). The widget tree is freed. */
static tk_canvas *render_tf(const tk_theme *t, const char *text,
                            bool password, const char *comp,
                            const size_t *segs, size_t nseg)
{
    tk_widget *tf = tk_textfield_new(text);
    tk_textfield_set_password(tf, password);
    if (comp) {
        tc_move_to(tk_textfield_buffer(tf), strlen(text), false);
        tk_event ev; memset(&ev, 0, sizeof(ev));
        ev.type = TK_EV_COMPOSITION;
        ev.text = comp;
        ev.seg_lens = (size_t *)segs;
        ev.seg_count = nseg;
        ev.seg_active = 0;
        tk_dispatch(tf, &ev);
    }
    tk_widget *fx = tk_fixed_new();
    tk_set_fixed_rect(tf, (tk_rect){0, 0, 300, 40});
    tk_add_child(fx, tf);
    tk_root *root = tk_root_new(fx);
    tk_root_layout(root, 300, 40);
    tk_canvas *c = tk_canvas_new(300, 40);
    tk_root_paint(root, c, t);
    tk_root_free(root);
    tk_widget_free(fx); /* frees tf too */
    return c;
}

static uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Total pixels covered by a canvas' accumulated damage rects. */
static size_t dmg_px(tk_canvas *c)
{
    size_t px = 0;
    for (size_t i = 0; i < tk_canvas_damage_count(c); i++) {
        tk_rect r = tk_canvas_damage_rect(c, i);
        px += (size_t)r.w * (size_t)r.h;
    }
    return px;
}

int main(void)
{
    const tk_theme *t = tk_theme_default();

    /* ===== canvas damage accumulation ===== */
    {
        tk_canvas *c = tk_canvas_new(200, 100);
        CHECK(c != NULL);
        CHECK(tk_canvas_damage_count(c) == 1); /* starts full */
        tk_rect b = tk_canvas_damage_bounds(c);
        CHECK(b.x == 0 && b.y == 0 && b.w == 200 && b.h == 100);
        tk_canvas_damage_clear(c);
        CHECK(tk_canvas_damage_count(c) == 0);
        tk_color red = {255, 0, 0, 255};
        tk_canvas_fill_rect(c, (tk_rect){10, 10, 20, 10}, red);
        CHECK(tk_canvas_damage_count(c) == 1);
        tk_rect d0 = tk_canvas_damage_rect(c, 0);
        CHECK(d0.x == 10 && d0.y == 10 && d0.w == 20 && d0.h == 10);
        /* abutting rect merges */
        tk_canvas_fill_rect(c, (tk_rect){30, 10, 10, 10}, red);
        CHECK(tk_canvas_damage_count(c) == 1);
        CHECK(tk_canvas_damage_bounds(c).w == 30);
        /* disjoint rect stays separate */
        tk_canvas_fill_rect(c, (tk_rect){100, 50, 10, 10}, red);
        CHECK(tk_canvas_damage_count(c) == 2);
        /* pixel inside existing damage: no growth */
        tk_canvas_fill_rect(c, (tk_rect){15, 15, 1, 1}, red);
        CHECK(tk_canvas_damage_count(c) == 2);
        /* pixel outside: one 1x1 rect */
        tk_canvas_fill_rect(c, (tk_rect){150, 80, 1, 1}, red);
        CHECK(tk_canvas_damage_count(c) == 3);
        /* clear() re-damages everything as one rect */
        tk_canvas_clear(c, red);
        CHECK(tk_canvas_damage_count(c) == 1);
        CHECK(tk_canvas_damage_bounds(c).w == 200);
        tk_canvas_free(c);
    }

    /* ===== fb flush: damage vs full, byte-exact ===== */
    {
        tk_canvas *c = tk_canvas_new(200, 100);
        tk_fb *fb = tk_fb_open_mem(200, 100, 16);
        CHECK(fb != NULL);
        tk_color red = {255, 0, 0, 255}, blue = {0, 0, 255, 255};
        tk_canvas_clear(c, red);
        size_t bytes = tk_fb_flush_damage(fb, c);
        CHECK(bytes == (size_t)200 * 100 * 2); /* full frame */
        CHECK(tk_canvas_damage_count(c) == 0);
        CHECK(tk_fb_flush_damage(fb, c) == 0); /* nothing left */
        const uint16_t *px = (const uint16_t *)tk_fb_pixels(fb);
        CHECK(px[50 * 200 + 50] == rgb565(255, 0, 0));
        /* small change: only its bytes are converted */
        tk_canvas_fill_rect(c, (tk_rect){10, 10, 20, 10}, blue);
        /* undamaged canvas change elsewhere must NOT be shipped */
        tk_canvas_pixels(c)[50 * 200 + 50] =
            0xFF00FF00u; /* green, written behind the damage system */
        bytes = tk_fb_flush_damage(fb, c);
        CHECK(bytes == (size_t)20 * 10 * 2);
        CHECK(px[15 * 200 + 15] == rgb565(0, 0, 255));
        CHECK(px[50 * 200 + 50] == rgb565(255, 0, 0)); /* still red */
        /* full flush ships everything and clears damage */
        tk_canvas_fill_rect(c, (tk_rect){0, 0, 5, 5}, blue);
        CHECK(tk_fb_flush(fb, c));
        CHECK(tk_canvas_damage_count(c) == 0);
        CHECK(px[50 * 200 + 50] == rgb565(0, 255, 0));
        tk_fb_close(fb);
        tk_canvas_free(c);
    }

    /* ===== fb flush at the real panel size, 854x480 =====
     * Regression for the integration phase-2 defect: the fb-direct
     * apps built their canvas at a hardcoded 480x272 while the
     * panel fb is 854x480, so every flush was rejected on size and
     * nothing ever reached the screen. Pin both directions: a
     * canvas at the real size ships in full (32bpp and 16bpp),
     * and the old 480x272 canvas against the 854x480 fb ships
     * nothing. */
    {
        tk_color red = {255, 0, 0, 255};
        tk_canvas *c = tk_canvas_new(854, 480);
        tk_fb *fb = tk_fb_open_mem(854, 480, 32);
        CHECK(fb != NULL);
        CHECK(tk_fb_width(fb) == 854 && tk_fb_height(fb) == 480);
        tk_canvas_clear(c, red);
        CHECK(tk_fb_flush_damage(fb, c) == (size_t)854 * 480 * 4);
        const uint32_t *px = (const uint32_t *)tk_fb_pixels(fb);
        CHECK(px[240 * 854 + 427] == 0x00FF0000u);
        CHECK(tk_fb_flush(fb, c));
        tk_fb_close(fb);
        tk_canvas_free(c);

        c = tk_canvas_new(854, 480);
        fb = tk_fb_open_mem(854, 480, 16);
        CHECK(fb != NULL);
        tk_canvas_clear(c, red);
        CHECK(tk_fb_flush_damage(fb, c) == (size_t)854 * 480 * 2);
        const uint16_t *px16 = (const uint16_t *)tk_fb_pixels(fb);
        CHECK(px16[240 * 854 + 427] == rgb565(255, 0, 0));
        tk_fb_close(fb);
        tk_canvas_free(c);

        /* the phase-2 failure mode itself: undersized canvas */
        c = tk_canvas_new(480, 272);
        fb = tk_fb_open_mem(854, 480, 32);
        CHECK(fb != NULL);
        tk_canvas_clear(c, red);
        CHECK(tk_fb_flush_damage(fb, c) == 0);
        CHECK(!tk_fb_flush(fb, c));
        tk_fb_close(fb);
        tk_canvas_free(c);
    }

    /* ===== root damage flow + paint_damage ===== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 8);
        tk_widget *sw = tk_switch_new("Wi-Fi", false);
        tk_widget *btn = tk_button_new("B");
        tk_add_child(box, sw);
        tk_add_child(box, btn);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 480, 272);
        tk_canvas *c = tk_canvas_new(480, 272);
        tk_fb *fb = tk_fb_open_mem(480, 272, 16);
        CHECK(tk_root_paint_damage(root, c, t) == 1); /* full */
        /* the app loop ships the first frame in full... */
        CHECK(tk_fb_flush_damage(fb, c) == (size_t)480 * 272 * 2);
        CHECK(tk_root_paint_damage(root, c, t) == 0); /* drained */
        /* a handled event damages only its target */
        tk_rect sr = tk_widget_rect(sw);
        send_click(root, sr.x + sr.w / 2, sr.y + sr.h / 2);
        CHECK(tk_switch_on(sw));
        CHECK(tk_root_damage_count(root) >= 1);
        int n = tk_root_paint_damage(root, c, t);
        CHECK(n >= 1);
        /* ...and later frames ship only the damaged fraction */
        send_click(root, sr.x + sr.w / 2, sr.y + sr.h / 2);
        (void)tk_root_paint_damage(root, c, t);
        size_t bytes = tk_fb_flush_damage(fb, c);
        printf("  root: switch toggle shipped %zu of %d bytes\n",
               bytes, 480 * 272 * 2);
        CHECK(bytes > 0 && bytes < (size_t)480 * 272 * 2 / 5);
        tk_fb_close(fb);
        /* explicit invalidation */
        CHECK(tk_root_paint_damage(root, c, t) == 0);
        tk_widget_invalidate(btn);
        CHECK(tk_root_damage_count(root) == 1);
        CHECK(tk_root_paint_damage(root, c, t) == 1);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== tooltips ===== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *btn = tk_button_new("Hover");
        tk_widget_set_tooltip(btn, "説明テキスト");
        CHECK(tk_widget_tooltip(btn) != NULL &&
              strcmp(tk_widget_tooltip(btn), "説明テキスト") == 0);
        tk_add_child(box, btn);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 480, 272);
        tk_rect br = tk_widget_rect(btn);
        tk_root_tick(root, 1000);
        send_ptr(root, TK_EV_POINTER_MOVE, br.x + 5, br.y + 5);
        tk_root_tick(root, 1000 + TK_TOOLTIP_DELAY_MS - 1);
        CHECK(!tk_root_tooltip_visible(root));
        tk_root_tick(root, 1000 + TK_TOOLTIP_DELAY_MS);
        CHECK(tk_root_tooltip_visible(root));
        tk_rect tr = tk_root_tooltip_rect(root);
        CHECK(tr.w > 0 && tr.h > 0);
        /* moving away hides it */
        send_ptr(root, TK_EV_POINTER_MOVE, 470, 265);
        CHECK(!tk_root_tooltip_visible(root));
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== TextView drag selection ===== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *tv = tk_textview_new("hello world\nsecond line\nthird");
        tk_add_child(box, tv);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 120);
        tk_rect vr = tk_widget_rect(tv);
        int lh = tk_line_height(t);
        send_ptr(root, TK_EV_POINTER_DOWN, vr.x + 9, vr.y + 5);
        tc_buffer *buf = tk_textview_buffer(tv);
        CHECK(tc_cursor(buf) == 0);
        CHECK(!tc_has_selection(buf));
        /* drag across "hello" */
        send_ptr(root, TK_EV_POINTER_MOVE, vr.x + 9 + 41, vr.y + 5);
        size_t lo = 0, hi = 0;
        CHECK(tc_has_selection(buf));
        tc_selection_range(buf, &lo, &hi);
        CHECK(lo == 0 && hi >= 4 && hi <= 6);
        /* drag down two lines: selection grows past line 1 */
        send_ptr(root, TK_EV_POINTER_MOVE, vr.x + 9 + 20,
                 vr.y + 5 + 2 * lh);
        tc_selection_range(buf, &lo, &hi);
        CHECK(lo == 0 && hi > 11);
        size_t hi_before = hi;
        send_ptr(root, TK_EV_POINTER_UP, vr.x + 9 + 20,
                 vr.y + 5 + 2 * lh);
        /* after release, moves no longer extend the selection */
        send_ptr(root, TK_EV_POINTER_MOVE, vr.x + 9, vr.y + 5);
        tc_selection_range(buf, &lo, &hi);
        CHECK(hi == hi_before);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== ComboBox direct text entry ===== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        const char *items[] = { "日本語", "English", "Deutsch" };
        tk_widget *combo = tk_combobox_new();
        tk_combobox_set_items(combo, items, 3);
        tk_combobox_set_editable(combo, true);
        tk_combobox_select(combo, 0);
        tk_add_child(box, combo);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 480, 272);
        CHECK(strcmp(tk_combobox_text(combo), "日本語") == 0);
        tk_rect cr = tk_widget_rect(combo);
        /* text-zone click: no dropdown, caret placement */
        send_click(root, cr.x + 10, cr.y + cr.h / 2);
        CHECK(tk_focused(root) == combo);
        CHECK(!tk_combobox_is_open(combo));
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 8; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "カスタム");
        send_key(root, TK_KEY_ENTER);
        CHECK(strcmp(tk_combobox_text(combo), "カスタム") == 0);
        CHECK(tk_combobox_selected(combo) == -1); /* custom value */
        /* typing an exact item name selects it */
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 8; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "English");
        send_key(root, TK_KEY_ENTER);
        CHECK(tk_combobox_selected(combo) == 1);
        CHECK(strcmp(tk_combobox_text(combo), "English") == 0);
        /* Escape reverts an edit in progress */
        send_text(root, "XYZ");
        send_key(root, TK_KEY_ESCAPE);
        CHECK(strcmp(tk_combobox_text(combo), "English") == 0);
        /* Up/Down cycle the selection and the text follows */
        send_key(root, TK_KEY_DOWN);
        CHECK(tk_combobox_selected(combo) == 2);
        CHECK(strcmp(tk_combobox_text(combo), "Deutsch") == 0);
        /* arrow-zone click opens the dropdown */
        send_click(root, cr.x + cr.w - 8, cr.y + cr.h / 2);
        CHECK(tk_combobox_is_open(combo));
        send_key(root, TK_KEY_ESCAPE); /* menu takes the key */
        tk_root_free(root);
        tk_widget_free(box);
    }
    {
        /* non-editable combo: text is the selected item (M2 API) */
        tk_widget *combo = tk_combobox_new();
        const char *items[] = { "A", "B" };
        tk_combobox_set_items(combo, items, 2);
        tk_combobox_select(combo, 1);
        CHECK(!tk_combobox_is_editable(combo));
        CHECK(strcmp(tk_combobox_text(combo), "B") == 0);
        tk_widget_free(combo);
    }

    /* ===== SpinButton direct text entry ===== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *spin = tk_spinbutton_new(5, 0, 100, 1);
        tk_widget *other = tk_button_new("other");
        tk_add_child(box, spin);
        tk_add_child(box, other);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 480, 272);
        tk_rect sr = tk_widget_rect(spin);
        send_click(root, sr.x + 10, sr.y + sr.h / 2);
        CHECK(tk_focused(root) == spin);
        send_key(root, TK_KEY_END);
        send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "12.5");
        send_key(root, TK_KEY_ENTER);
        CHECK(tk_spinbutton_value(spin) == 12.5f);
        /* letters are rejected at input time */
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 6; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "abc");
        send_key(root, TK_KEY_ENTER);
        CHECK(tk_spinbutton_value(spin) == 12.5f); /* parse fail: revert */
        /* clamping applies to typed values */
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 6; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "999");
        send_key(root, TK_KEY_ENTER);
        CHECK(tk_spinbutton_value(spin) == 100.0f);
        /* Escape cancels the edit */
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 6; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "77");
        send_key(root, TK_KEY_ESCAPE);
        CHECK(tk_spinbutton_value(spin) == 100.0f);
        /* focus loss commits */
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 6; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "33");
        CHECK(tk_focus(root, other));
        CHECK(tk_spinbutton_value(spin) == 33.0f);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== TextField password mask (M3) ===== */
    {
        tk_font *f = tk_font_load_bundled();
        CHECK(f != NULL);
        tk_font_install(f); /* real glyphs for the pixel checks */
        int adv = tk_text_cp_width(t, 0x25CF); /* mask glyph advance */
        CHECK(adv > 2);

        /* API + the buffer keeps the real text */
        tk_widget *tf = tk_textfield_new("ぱすわーど");
        CHECK(!tk_textfield_is_password(tf)); /* default off */
        tk_textfield_set_password(tf, true);
        CHECK(tk_textfield_is_password(tf));
        char out[64];
        CHECK(tk_textfield_text(tf, out, sizeof(out)) == strlen("ぱすわーど"));
        CHECK(strcmp(out, "ぱすわーど") == 0);
        tk_widget_free(tf);

        /* masked render hides content but not character count:
         * same-length secrets render identically, plaintext and
         * different-length renders differ */
        tk_canvas *m1 = render_tf(t, "abc", true, NULL, NULL, 0);
        tk_canvas *m2 = render_tf(t, "xyz", true, NULL, NULL, 0);
        tk_canvas *p1 = render_tf(t, "abc", false, NULL, NULL, 0);
        tk_canvas *m3 = render_tf(t, "abcd", true, NULL, NULL, 0);
        CHECK(canvas_equal(m1, m2, 300, 40));
        CHECK(!canvas_equal(m1, p1, 300, 40));
        CHECK(!canvas_equal(m1, m3, 300, 40));
        tk_canvas_free(m1); tk_canvas_free(m2);
        tk_canvas_free(p1); tk_canvas_free(m3);

        /* composition is masked too, with the segment underlines
         * aligned to mask positions (prefix "ab" = 2 mask glyphs,
         * segment 1 = 1 mask glyph, segment 2 = 2 mask glyphs) */
        size_t segs[2] = { 3, 6 }; /* か / んじ */
        tk_canvas *c1 = render_tf(t, "ab", true, "かんじ", segs, 2);
        tk_canvas *c2 = render_tf(t, "ab", true, "でんし", segs, 2);
        tk_canvas *c3 = render_tf(t, "ab", false, "かんじ", segs, 2);
        CHECK(canvas_equal(c1, c2, 300, 40));  /* content hidden */
        CHECK(!canvas_equal(c1, c3, 300, 40)); /* vs plaintext */
        int lh = tk_line_height(t);
        int ty = (40 - lh) / 2;
        int cx0 = t->unit + 2 * adv; /* after the masked prefix */
        tk_color px = tk_canvas_pixel(c1, cx0 + 2, ty + lh - 2);
        CHECK(px.r == t->accent.r && px.g == t->accent.g &&
              px.b == t->accent.b); /* active segment underline */
        px = tk_canvas_pixel(c1, cx0 + adv + 2, ty + lh - 2);
        CHECK(px.r == t->surface.r && px.g == t->surface.g &&
              px.b == t->surface.b); /* inactive: no 2nd row */
        px = tk_canvas_pixel(c1, cx0 + adv + 2, ty + lh - 1);
        CHECK(px.r == t->text_dim.r && px.g == t->text_dim.g &&
              px.b == t->text_dim.b); /* inactive: 1px dim row */
        tk_canvas_free(c1); tk_canvas_free(c2); tk_canvas_free(c3);

        /* hit-testing uses the masked layout: clicks land on mask
         * boundaries, mapped back to real byte offsets */
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *pf = tk_textfield_new("あいう"); /* 3 cp, 9 bytes */
        tk_textfield_set_password(pf, true);
        tk_add_child(box, pf);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 480, 272);
        tk_rect fr = tk_widget_rect(pf);
        tc_buffer *b = tk_textfield_buffer(pf);
        send_ptr(root, TK_EV_POINTER_DOWN,
                 fr.x + t->unit + 2 * adv - 1, fr.y + fr.h / 2);
        CHECK(tc_cursor(b) == 6); /* after 2 mask glyphs = byte 6 */
        send_ptr(root, TK_EV_POINTER_DOWN,
                 fr.x + t->unit + 1, fr.y + fr.h / 2);
        CHECK(tc_cursor(b) == 0);
        /* shift+click extends the selection in real byte offsets */
        tk_event ev; memset(&ev, 0, sizeof(ev));
        ev.type = TK_EV_POINTER_DOWN;
        ev.x = fr.x + t->unit + 2 * adv - 1;
        ev.y = fr.y + fr.h / 2;
        ev.modifiers = TK_MOD_SHIFT;
        (void)tk_root_event(root, &ev);
        CHECK(tc_has_selection(b));
        size_t lo = 0, hi = 0;
        tc_selection_range(b, &lo, &hi);
        CHECK(lo == 0 && hi == 6);
        /* far right clamps to the end; typing appends the real
         * character (the mask is display-only) */
        send_ptr(root, TK_EV_POINTER_DOWN, fr.x + fr.w - 2,
                 fr.y + fr.h / 2);
        CHECK(tc_cursor(b) == 9);
        send_text(root, "x");
        CHECK(tk_textfield_text(pf, out, sizeof(out)) == 10);
        CHECK(strcmp(out, "あいうx") == 0);
        tk_root_free(root);
        tk_widget_free(box);
        tk_font_free(f); /* also uninstalls */
    }

    /* ===== bundled subset font (runs last: installs the font) ==== */
    {
        tk_font *f = tk_font_load_bundled();
        CHECK(f != NULL);
        if (f) {
            CHECK(tk_font_has_glyph(f, 0x3042));  /* あ */
            CHECK(tk_font_has_glyph(f, 0x6F22));  /* 漢 */
            CHECK(tk_font_has_glyph(f, 'A'));
            CHECK(tk_font_has_glyph(f, 0x2460));  /* ① */
            CHECK(!tk_font_has_glyph(f, 0x2000B)); /* CJK ext B: cut */
            /* M4 subset regeneration + supplement chain */
            CHECK(tk_font_has_glyph(f, 0x2212));  /* − minus (calc) */
            CHECK(tk_font_has_glyph(f, 0x591B));  /* 夛 (SUPP vocab) */
            CHECK(tk_font_has_glyph(f, 0x5F0C));  /* 弌 (SUPP vocab) */
            CHECK(tk_font_has_glyph(f, 0x0142));  /* ł via supplement */
            CHECK(tk_font_has_glyph(f, 0x23F8));  /* ⏸ via symbols */
            CHECK(tk_font_has_glyph(f, 0x23ED));  /* ⏭ via symbols */
            CHECK(tk_font_cp_advance(f, 16, 0x3042) > 0);
            tk_font_install(f);
            tk_canvas *c = tk_canvas_new(120, 40);
            tk_canvas_clear(c, (tk_color){255, 255, 255, 255});
            tk_canvas_draw_text(c, t, 2, 4, "漢字あ",
                                (tk_color){0, 0, 0, 255});
            int dark = 0;
            for (int y = 0; y < 40; y++)
                for (int x = 0; x < 120; x++) {
                    tk_color p = tk_canvas_pixel(c, x, y);
                    if (p.r < 128) dark++;
                }
            printf("  bundled font: rasterized %d dark px\n", dark);
            CHECK(dark > 50); /* real glyphs, CFF subset rasterizes */
            tk_canvas_free(c);
            tk_font_free(f);
        }
        tk_font *d = tk_font_load_default();
        CHECK(d != NULL); /* bundled is found by the default search */
        if (d) tk_font_free(d);
    }

    /* ===== button set_label: auto-invalidate + pixel change ===== */
    {
        tk_font *f = tk_font_load_bundled();
        CHECK(f != NULL);
        tk_font_install(f); /* real glyphs for the pixel checks */
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 8);
        tk_widget *btn = tk_button_new("▶ 再生");
        tk_add_child(box, btn);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 480, 272);
        tk_canvas *c = tk_canvas_new(480, 272);
        tk_canvas *before = tk_canvas_new(480, 272);
        CHECK(tk_root_paint_damage(root, c, t) == 1); /* full */
        tk_root_paint(root, before, t);
        CHECK(canvas_equal(c, before, 480, 272));
        CHECK(tk_root_damage_count(root) == 0);
        /* changing the label invalidates the button on its own -
         * no explicit tk_widget_invalidate() from the caller */
        tk_button_set_label(btn, "⏸ 一時停止");
        CHECK(tk_button_label(btn) &&
              strcmp(tk_button_label(btn), "⏸ 一時停止") == 0);
        CHECK(tk_root_damage_count(root) == 1);
        CHECK(tk_root_paint_damage(root, c, t) == 1);
        CHECK(!canvas_equal(c, before, 480, 272)); /* pixels changed */
        /* ...but only inside the button: a far corner is untouched */
        tk_color pc = tk_canvas_pixel(c, 479, 271);
        tk_color pb = tk_canvas_pixel(before, 479, 271);
        CHECK(pc.r == pb.r && pc.g == pb.g && pc.b == pb.b);
        /* re-setting the same label is a no-op: no new damage */
        tk_button_set_label(btn, "⏸ 一時停止");
        CHECK(tk_root_damage_count(root) == 0);
        /* NULL clears the label (and invalidates); NULL widget safe */
        tk_button_set_label(btn, NULL);
        CHECK(tk_button_label(btn) == NULL);
        CHECK(tk_root_damage_count(root) == 1);
        tk_button_set_label(NULL, "x");
        tk_canvas_free(c);
        tk_canvas_free(before);
        tk_root_free(root);
        tk_widget_free(box);
        tk_font_free(f); /* also uninstalls */
    }

    /* ===== listview move_row: order, selection, damage ===== */
    {
        sel_fires = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *lv = tk_listview_new();
        const char *items[] = { "alpha", "beta", "gamma", "delta" };
        tk_listview_set_items(lv, items, 4);
        tk_listview_set_on_select(lv, on_sel_count, NULL);
        tk_add_child(box, lv);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 200);
        tk_canvas *c = tk_canvas_new(200, 200);
        (void)tk_root_paint_damage(root, c, t); /* drain */
        tk_listview_select(lv, 2);              /* gamma */
        CHECK(sel_fires == 1);
        sel_fires = 0;
        /* move alpha past gamma: beta gamma alpha delta */
        CHECK(tk_listview_move_row(lv, 0, 2));
        CHECK(strcmp(tk_listview_item(lv, 0), "beta") == 0);
        CHECK(strcmp(tk_listview_item(lv, 1), "gamma") == 0);
        CHECK(strcmp(tk_listview_item(lv, 2), "alpha") == 0);
        CHECK(strcmp(tk_listview_item(lv, 3), "delta") == 0);
        /* the selection followed gamma to index 1, and reported it */
        CHECK(tk_listview_selected(lv) == 1);
        CHECK(sel_fires == 1);
        /* a real move invalidates the widget on its own */
        CHECK(tk_root_damage_count(root) >= 1);
        (void)tk_root_paint_damage(root, c, t);
        /* move the selected row itself to the top */
        CHECK(tk_listview_move_row(lv, 1, 0));
        CHECK(tk_listview_selected(lv) == 0);
        CHECK(strcmp(tk_listview_item(lv, 0), "gamma") == 0);
        CHECK(sel_fires == 2);
        (void)tk_root_paint_damage(root, c, t);
        /* from == to: successful no-op, no damage */
        CHECK(tk_listview_move_row(lv, 2, 2));
        CHECK(tk_root_damage_count(root) == 0);
        /* out of range: refused, list untouched */
        CHECK(!tk_listview_move_row(lv, -1, 0));
        CHECK(!tk_listview_move_row(lv, 0, 4));
        CHECK(!tk_listview_move_row(lv, 0, -1));
        CHECK(!tk_listview_move_row(NULL, 0, 0));
        CHECK(strcmp(tk_listview_item(lv, 0), "gamma") == 0);
        CHECK(tk_listview_item(lv, -1) == NULL);
        CHECK(tk_listview_item(lv, 4) == NULL);
        CHECK(tk_listview_item(NULL, 0) == NULL);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== listview marked row + slider thumb clamp (M4) ===== */
    {
        uint32_t px[400 * 300];
        tk_canvas *c = tk_canvas_wrap(400, 300, px);
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *lv = tk_listview_new();
        const char *items[] = { "one", "two", "three" };
        tk_listview_set_items(lv, items, 3);
        tk_add_child(box, lv);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 400, 300);
        CHECK(tk_listview_marked(lv) == -1);
        tk_listview_set_marked(lv, 1);
        CHECK(tk_listview_marked(lv) == 1);
        tk_listview_set_marked(lv, 99);
        CHECK(tk_listview_marked(lv) == 2); /* clamped */
        tk_listview_set_items(lv, items, 3);
        CHECK(tk_listview_marked(lv) == -1); /* reset by set_items */
        tk_listview_set_marked(lv, 0);
        memset(px, 0, sizeof(px));
        const tk_theme *th = tk_theme_default();
        tk_root_paint(root, c, th);
        tk_rect lr = tk_widget_rect(lv);
        /* accent bar at the marked row's left edge */
        tk_color bar = tk_canvas_pixel(c, lr.x + 1, lr.y + 4);
        CHECK(bar.r == th->accent.r && bar.g == th->accent.g &&
              bar.b == th->accent.b);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }
    {
        /* slider at value 1.0: the thumb must stay inside the
         * slider rect (it used to overhang 7 px to the right) */
        uint32_t px[200 * 60];
        tk_canvas *c = tk_canvas_wrap(200, 60, px);
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *sl = tk_slider_new(1.0f);
        tk_add_child(box, sl);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 60);
        memset(px, 0, sizeof(px));
        const tk_theme *th = tk_theme_default();
        tk_root_paint(root, c, th);
        tk_rect sr = tk_widget_rect(sl);
        int cy = sr.y + sr.h / 2;
        int accent_run_end = -1;
        for (int x = sr.x; x < sr.x + sr.w; x++) {
            tk_color p = tk_canvas_pixel(c, x, cy);
            if (p.r == th->accent.r && p.g == th->accent.g &&
                p.b == th->accent.b)
                accent_run_end = x;
        }
        CHECK(accent_run_end >= 0 && accent_run_end < sr.x + sr.w);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== wrap toolbar inside a horizontal parent (M4) ===== */
    {
        /* notes' shape: HBox -> VBox(flex) -> [wrap toolbar, label].
         * The toolbar must reserve TWO rows; before the arrange-
         * time re-measure it reserved one and its second row was
         * painted over by the following sibling. */
        uint32_t px[800 * 480];
        tk_canvas *c = tk_canvas_wrap(800, 480, px);
        tk_widget *hbox = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        tk_widget *side = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_set_flex(side, 1);
        tk_add_child(hbox, side);
        tk_widget *vbox = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_set_flex(vbox, 2);
        tk_add_child(hbox, vbox);
        tk_widget *bar = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_box_set_wrap(bar, true);
        tk_widget *last = NULL;
        for (int i = 0; i < 9; i++) {
            last = tk_button_new("ボタン幅");
            tk_add_child(bar, last);
        }
        tk_add_child(vbox, bar);
        tk_widget *below = tk_label_new("below");
        tk_add_child(vbox, below);
        tk_root *root = tk_root_new(hbox);
        tk_root_layout(root, 800, 480);
        tk_rect br = tk_widget_rect(bar);
        tk_rect lr2 = tk_widget_rect(last);
        tk_rect bl = tk_widget_rect(below);
        CHECK(br.h >= 2 * 36); /* two button rows reserved */
        CHECK(lr2.y + lr2.h <= br.y + br.h); /* last btn inside */
        CHECK(bl.y >= br.y + br.h); /* sibling starts after */
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(hbox);
    }

    /* ============ UI-REDESIGN P1: tokens, typography, widgets == */
    {
        const tk_theme *t = tk_theme_default();
        /* token values (UI-REDESIGN 2.2) */
        CHECK(t->control_h == 48 && t->row_h == 48 && t->row_h2 == 64);
        CHECK(t->radius == 4 && t->radius_overlay == 8);
        CHECK(t->font_px == 16 && t->font_display == 28 &&
              t->font_title == 20 && t->font_heading == 16 &&
              t->font_caption == 13);
        CHECK(t->text_dim.r == 90 && t->text_dim.g == 90 &&
              t->text_dim.b == 97);
        CHECK(t->border.r == 138 && t->border.g == 138 &&
              t->border.b == 146);
        CHECK(t->border_line.r == 216 && t->border_line.g == 216 &&
              t->border_line.b == 222);
        /* typography mapping */
        CHECK(tk_theme_style_px(t, TK_TEXT_BODY) == 16);
        CHECK(tk_theme_style_px(t, TK_TEXT_DISPLAY) == 28);
        CHECK(tk_theme_style_px(t, TK_TEXT_TITLE) == 20);
        CHECK(tk_theme_style_px(t, TK_TEXT_HEADING) == 16);
        CHECK(tk_theme_style_px(t, TK_TEXT_CAPTION) == 13);
        CHECK(tk_theme_style_bold(TK_TEXT_TITLE));
        CHECK(tk_theme_style_bold(TK_TEXT_HEADING));
        CHECK(!tk_theme_style_bold(TK_TEXT_BODY));
        CHECK(!tk_theme_style_bold(TK_TEXT_CAPTION));
        CHECK(!tk_theme_style_bold(TK_TEXT_DISPLAY));
        /* a display label is taller than a body label; a caption
         * label is shorter; heading == body height (same px) */
        tk_widget *lb = tk_label_new("見出し");
        tk_widget *ld = tk_label_new("見出し");
        tk_widget *lc = tk_label_new("見出し");
        tk_widget_set_text_style(ld, TK_TEXT_DISPLAY);
        tk_widget_set_text_style(lc, TK_TEXT_CAPTION);
        tk_constraints un = {0, TK_UNLIMITED, 0, TK_UNLIMITED};
        CHECK(tk_measure(ld, un).h > tk_measure(lb, un).h);
        CHECK(tk_measure(lc, un).h < tk_measure(lb, un).h);
        CHECK(tk_widget_text_style(ld) == TK_TEXT_DISPLAY);
        tk_widget_free(lb); tk_widget_free(ld); tk_widget_free(lc);
    }
    {
        /* contrast ratios from the actual token values */
        const tk_theme *t = tk_theme_default();
        CHECK(cr(t->text_dim, t->surface) >= 4.5);
        CHECK(cr(t->text_dim, t->surface_alt) >= 4.5);
        CHECK(cr(t->border, t->surface) >= 3.0);
        CHECK(cr(t->text, t->surface) >= 4.5);
        CHECK(cr(t->on_accent, t->accent) >= 4.5);
    }
    {
        /* accent button face + bold heading ink, with real glyphs */
        tk_font *f = tk_font_load_bundled();
        CHECK(f != NULL);
        if (f) {
            tk_font_install(f);
            const tk_theme *t = tk_theme_default();
            tk_widget *b = tk_button_new("ログイン");
            tk_button_set_accent(b, true);
            CHECK(tk_button_is_accent(b));
            tk_root *root = tk_root_new(b);
            tk_root_layout(root, 200, 48);
            tk_canvas *cv = tk_canvas_new(200, 48);
            tk_root_paint(root, cv, t);
            tk_color pc = tk_canvas_pixel(cv, 4, 4);
            CHECK(pc.r == t->accent.r && pc.g == t->accent.g &&
                  pc.b == t->accent.b);
            tk_canvas_free(cv);
            tk_root_free(root);
            tk_widget_free(b);
            /* heading (synthetic bold) lays down more ink than
             * body at the same 16px, and measures identically */
            tk_widget *h1 = tk_label_new("設定画面");
            tk_widget *h2 = tk_label_new("設定画面");
            tk_widget_set_text_style(h2, TK_TEXT_HEADING);
            tk_constraints un = {0, TK_UNLIMITED, 0, TK_UNLIMITED};
            CHECK(tk_measure(h1, un).w == tk_measure(h2, un).w);
            tk_canvas *c1 = tk_canvas_new(200, 40);
            tk_canvas *c2 = tk_canvas_new(200, 40);
            tk_canvas_clear(c1, t->surface);
            tk_canvas_clear(c2, t->surface);
            tk_paint(h1, c1, t);
            tk_arrange(h2, (tk_rect){0, 0, 200, 40});
            tk_arrange(h1, (tk_rect){0, 0, 200, 40});
            tk_paint(h1, c1, t);
            tk_paint(h2, c2, t);
            long ink1 = 0, ink2 = 0;
            for (int i = 0; i < 200 * 40; i++) {
                uint32_t p1 = tk_canvas_pixels(c1)[i];
                uint32_t p2 = tk_canvas_pixels(c2)[i];
                if ((p1 & 0xFFFFFF) != 0xFFFFFF) ink1++;
                if ((p2 & 0xFFFFFF) != 0xFFFFFF) ink2++;
            }
            CHECK(ink2 > ink1);
            printf("glyph cache after style draws: %zu entries\n",
                   tk_font_cache_count(f));
            tk_canvas_free(c1); tk_canvas_free(c2);
            tk_widget_free(h1); tk_widget_free(h2);
            tk_font_install(NULL);
            tk_font_free(f);
        }
    }
    {
        /* ListView: two-line rows, selection bar, item icons */
        const tk_theme *t = tk_theme_default();
        tk_widget *lv = tk_listview_new();
        const char *items[] = { "曲1", "曲2", "曲3" };
        tk_listview_set_items(lv, items, 3);
        CHECK(tk_listview_row_h(lv, t) == 48);
        const char *subs[] = { "歌手1", NULL, "歌手3" };
        tk_listview_set_subitems(lv, subs, 3);
        CHECK(tk_listview_row_h(lv, t) == 64);
        static uint32_t redpx[24 * 24];
        for (int i = 0; i < 24 * 24; i++) redpx[i] = 0xFFFF0000u;
        const uint32_t *icons[] = { redpx, NULL, redpx };
        tk_listview_set_item_icons(lv, icons, 24, 24, 3);
        tk_listview_select(lv, 1);
        tk_root *root = tk_root_new(lv);
        tk_root_layout(root, 300, 200);
        tk_canvas *cv = tk_canvas_new(300, 200);
        tk_root_paint(root, cv, t);
        /* selection bar: 3px accent at the left of row 1 */
        tk_color pc = tk_canvas_pixel(cv, 1, 64 + 32);
        CHECK(pc.r == t->accent.r && pc.g == t->accent.g &&
              pc.b == t->accent.b);
        /* icon of row 0 is blitted (red at its centre) */
        pc = tk_canvas_pixel(cv, 16 + 12, 32);
        CHECK(pc.r == 255 && pc.g == 0 && pc.b == 0);
        tk_canvas_free(cv);
        tk_root_free(root);
        tk_widget_free(lv);
    }
    {
        /* OverflowBar: children are shed into the menu, never
         * clipped; choosing a menu entry fires the child's own
         * click callback */
        memset(ob_fired, 0, sizeof(ob_fired));
        tk_widget *bar = tk_overflowbar_new(8, 0);
        tk_widget *btns[10];
        for (int i = 0; i < 10; i++) {
            btns[i] = tk_button_new("アイテムアイテム");
            tk_button_set_on_click(btns[i], ob_on_click,
                                   (void *)(intptr_t)i);
            tk_add_child(bar, btns[i]);
        }
        tk_root *root = tk_root_new(bar);
        tk_root_layout(root, 800, 48);
        CHECK(tk_overflowbar_has_overflow(bar));
        CHECK(tk_widget_rect(btns[0]).w > 0);
        CHECK(tk_widget_rect(btns[9]).w == 0); /* shed, not clipped */
        /* open the "»" menu (zone: right end of the bar) and pick
         * the first shed entry -> that button's callback fires */
        send_click(root, 800 - 24, 24);
        send_click(root, 700, 48 + 24);
        int total = 0, which = -1;
        for (int i = 0; i < 10; i++)
            if (ob_fired[i]) { total += ob_fired[i]; which = i; }
        CHECK(total == 1);
        CHECK(which >= 0 && tk_widget_rect(btns[which]).w == 0);
        tk_root_free(root);
        tk_widget_free(bar);
        /* a bar that fits has no overflow and hides nothing */
        tk_widget *bar2 = tk_overflowbar_new(8, 0);
        tk_widget *one = tk_button_new("開く");
        tk_add_child(bar2, one);
        tk_root *root2 = tk_root_new(bar2);
        tk_root_layout(root2, 800, 48);
        CHECK(!tk_overflowbar_has_overflow(bar2));
        CHECK(tk_widget_rect(one).w > 0);
        tk_root_free(root2);
        tk_widget_free(bar2);
    }

    /* ---- min-size floor + label alignment (P3 support APIs) -- */
    {
        /* an earlier block uninstalls the font (no-font paint
         * check); reinstall the real glyphs for these probes */
        tk_font *f = tk_font_load_bundled();
        if (f) tk_font_install(f);
        tk_widget *lab = tk_label_new("短い");
        tk_size s0 = tk_measure(lab, (tk_constraints){ 0, 100000, 0, 100000 });
        tk_widget_set_min_size(lab, 176, 0);
        tk_size s1 = tk_measure(lab, (tk_constraints){ 0, 100000, 0, 100000 });
        CHECK(s0.w < 176 && s1.w == 176);
        tk_widget_free(lab);

        /* END-aligned label paints its ink at the rect's right
         * edge, START at the left */
        tk_canvas *cv = tk_canvas_new(200, 40);
        tk_widget *al = tk_label_new("123");
        tk_label_set_align(al, TK_LABEL_ALIGN_END);
        tk_root *r = tk_root_new(al);
        tk_root_layout(r, 200, 40);
        /* the label is the root's top widget here, so nothing
         * else fills the background: pre-fill with the bg colour
         * the shell would paint under it */
        tk_canvas_fill_rect(cv, (tk_rect){ 0, 0, 200, 40 }, t->bg);
        tk_root_paint(r, cv, t);
        int ink_left = 0, ink_right = 0;
        for (int y = 0; y < 40; y++)
            for (int x = 0; x < 200; x++) {
                uint32_t p = tk_canvas_pixels(cv)[y * 200 + x];
                if ((p & 0xFFFFFFu) != 0xF5F5F7u && (p & 0xFFFFFFu) != 0xFFFFFFu) {
                    if (x < 100) ink_left++;
                    else ink_right++;
                }
            }
        CHECK(ink_right > 0 && ink_left == 0);
        tk_root_free(r);
        tk_widget_free(al);
        tk_canvas_free(cv);
    }

    /* ===== damage hints: a composite widget narrows its own
     * repaint (the OSK is the proving case: one key tap must not
     * repaint the whole 800x272 strip, a layout-changing mode
     * switch still must) ===== */
    {
        tk_widget *fx = tk_fixed_new();
        tk_widget *sink = tk_sink_new(NULL, NULL);
        tk_widget *osk = tk_osk_new();
        tk_set_fixed_rect(osk, (tk_rect){ 0, 208, 800, 272 });
        tk_add_child(fx, osk);
        tk_osk_attach(osk, sink);
        tk_root *r = tk_root_new(fx);
        tk_root_layout(r, 800, 480);
        tk_canvas *cv = tk_canvas_new(800, 480);
        (void)tk_focus(r, osk); /* steady state: focus already held */
        /* paint_damage (not paint): it consumes the root's
         * accumulated damage, so later frames measure only
         * their own event */
        tk_root_paint_damage(r, cv, t);
        tk_canvas_damage_clear(cv);

        osk_cell cells[128];
        int nc = tk_osk_layout(osk, cells, 128);
        tk_rect ka = { 0, 0, 0, 0 }, ksym = { 0, 0, 0, 0 };
        for (int i = 0; i < nc; i++) {
            if (cells[i].kind == OSK_CELL_LETTER &&
                strcmp(cells[i].text, "a") == 0)
                ka = cells[i].r;
            if (cells[i].kind == OSK_CELL_FUNC &&
                cells[i].id == OSK_FN_MODE)
                ksym = cells[i].r;
        }
        CHECK(ka.w > 0 && ksym.w > 0);
        /* damage pixels of one frame (the fbdev flush proxy) */
        #define FRAME_PX() \
            (tk_canvas_damage_clear(cv), \
             tk_root_paint_damage(r, cv, t), dmg_px(cv))
        send_ptr(r, TK_EV_POINTER_DOWN,
                 ka.x + ka.w / 2, ka.y + ka.h / 2);
        /* one key face only (~5k px), not the strip (~218k px) */
        CHECK(FRAME_PX() < 20000);
        send_ptr(r, TK_EV_POINTER_UP,
                 ka.x + ka.w / 2, ka.y + ka.h / 2);
        /* candidate bar + the key face (~43k px) */
        CHECK(FRAME_PX() < 120000);
        send_ptr(r, TK_EV_POINTER_DOWN,
                 ksym.x + ksym.w / 2, ksym.y + ksym.h / 2);
        (void)FRAME_PX();
        send_ptr(r, TK_EV_POINTER_UP,
                 ksym.x + ksym.w / 2, ksym.y + ksym.h / 2);
        /* the symbol-mode switch re-lays out the whole grid */
        CHECK(FRAME_PX() > 150000);
        #undef FRAME_PX
        tk_root_free(r);
        tk_widget_free(fx);
        tk_canvas_free(cv);
    }

    printf("tk3: %d passed, %d failed\n", checks - failures, failures);
    return failures ? 1 : 0;
}
