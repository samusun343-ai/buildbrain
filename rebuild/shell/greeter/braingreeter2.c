/* braingreeter2 - greeter for the shell v2 session (M2 prototype).
 *
 * Design: DESIGN-shell-v2.md section 14. In short:
 *   - toolkit v2 UI: user list (from /etc/passwd via
 *     greeter2_auth), password TextField, OSK v2 for touch input
 *     (libbrainime backend can be injected for Japanese input;
 *     passwords are expected ASCII, so the kana fallback suffices
 *     and no IME session is started by default).
 *   - verification is greeter2_auth (crypt() against shadow).
 *   - on success the chosen username is printed on stdout and the
 *     process exits 0; the session script (integration/
 *     brainux-session.sh) starts the shell for that user. On
 *     failure the password field is cleared and an error label is
 *     shown; the greeter never exits on failure.
 *   - display: /dev/fb0 via tk_fb when present; input: evdev
 *     devices under /dev/input (first keyboard + first touch).
 *     With neither present (host build check), GUI mode refuses to
 *     start instead of idling - use --check for headless testing.
 *
 * Password display (M3, resolved): the field is masked with
 * tk_textfield_set_password() - one U+25CF per character for the
 * committed text and the IME composition alike; the buffer keeps
 * the real password for verification (see DESIGN-shell-v2.md
 * section 14.1). This clears the M2 ship blocker for masking;
 * the remaining default-session precondition is the X-compat PSS
 * measurement (DESIGN section 14.3).
 *
 * Clipboard policy (M3, decided here): the password field NEVER
 * gets a clipboard provider - tk_textfield_set_clipboard(NULL)
 * is called explicitly below. The mask is display-only, so a
 * Ctrl+C would otherwise place the plaintext password on a
 * shared clipboard for any later reader; textcore's copy/cut/
 * paste are no-ops without a provider, which disables all three
 * for this field. Losing paste is accepted (the standard login-
 * screen trade-off; the OSK has no clipboard operations either).
 * This is a greeter-local policy: ordinary toolkit fields keep
 * their normal clipboard behaviour.
 *
 * usage:
 *   braingreeter2                 GUI mode
 *   braingreeter2 --check U P     exit 0 iff P verifies for U
 *   braingreeter2 --list          list login-capable users
 */
#include "greeter2_auth.h"
#include "thememode.h"
#include "tk.h"
#include "osk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bd_greeter_user users[32];
static int n_users;
static int selected_user;
static tk_widget *pass_field;
static tk_widget *error_label;
static bool done;
static int exit_code;

/* UI-REDESIGN 2.3 layout state */
static tk_widget *clock_label;
static tk_widget *date_label;
static tk_widget *card;
static tk_widget *avatar_view;
static tk_widget *name_label;
static tk_widget *user_list;
static tk_widget *osk_widget;
static tk_widget *kbd_btn;
static tk_widget *reboot_btn_w;
static tk_widget *power_btn_w;
static tk_widget *login_btn_w;
static bool osk_open;
static int last_clock_min = -1;

/* Position every element for screen W x H and the current OSK
 * state. Normal form (H >= 360): clock in the top third, card
 * 400 wide centred (shifted to the top when the OSK sheet is
 * open), switch list under the card, power at the bottom right.
 * Compact form (H < 360): the avatar row collapses and the clock
 * shrinks so the card and sheet still fit. */
static void greeter_place(int W, int H)
{
    bool compact = H < 360;
    int sheet_h = compact ? H * 45 / 100 : 236;
    int card_w = 400;
    if (card_w > W - 16) card_w = W - 16;
    int card_x = (W - card_w) / 2;
    int card_h = compact ? 184 : 248;
    int card_y = osk_open ? 6 : (compact ? 40 : 150);
    tk_set_fixed_rect(card, (tk_rect){ card_x, card_y, card_w, card_h });
    if (compact) {
        tk_set_visible(avatar_view, false);
        tk_set_fixed_rect(name_label, (tk_rect){ 24, 12, card_w - 48, 26 });
        tk_set_fixed_rect(pass_field, (tk_rect){ 24, 46, card_w - 48, 48 });
        tk_set_fixed_rect(error_label, (tk_rect){ 24, 98, card_w - 48, 16 });
        tk_set_fixed_rect(login_btn_w, (tk_rect){ 24, 118, card_w - 48, 48 });
    } else {
        tk_set_visible(avatar_view, true);
        tk_set_fixed_rect(avatar_view, (tk_rect){ 24, 24, 64, 64 });
        tk_set_fixed_rect(name_label, (tk_rect){ 104, 42, card_w - 128, 28 });
        tk_set_fixed_rect(pass_field, (tk_rect){ 24, 108, card_w - 48, 48 });
        tk_set_fixed_rect(error_label, (tk_rect){ 24, 160, card_w - 48, 18 });
        tk_set_fixed_rect(login_btn_w, (tk_rect){ 24, 184, card_w - 48, 48 });
    }
    /* clock + date, centred, in the top third */
    {
        tk_size cs = tk_measure(clock_label,
                                (tk_constraints){ 0, TK_UNLIMITED, 0, TK_UNLIMITED });
        int cy = compact ? 4 : 20;
        tk_set_fixed_rect(clock_label,
                          (tk_rect){ (W - cs.w) / 2, cy, cs.w, cs.h });
        tk_size ds = tk_measure(date_label,
                                (tk_constraints){ 0, TK_UNLIMITED, 0, TK_UNLIMITED });
        tk_set_fixed_rect(date_label,
                          (tk_rect){ (W - ds.w) / 2, cy + cs.h + 4, ds.w, ds.h });
        tk_set_visible(clock_label, !osk_open);
        tk_set_visible(date_label, !osk_open);
    }
    /* user switch list under the card: when it would collide
     * with the power row, the card moves up and the list shrinks
     * (it scrolls internally) - nothing may run off the bottom
     * edge or under the power buttons */
    if (n_users > 1 && !osk_open) {
        int lh = n_users * 48;
        if (lh > 144) lh = 144;
        if (card_y + card_h + 12 + lh > H - 72) {
            card_y = H - 72 - 12 - card_h - lh;
            if (!compact && card_y < 100) card_y = 100;
            if (compact && card_y < 40) card_y = 40;
            lh = H - 72 - (card_y + card_h + 12);
            if (lh < 0) lh = 0;
            tk_set_fixed_rect(card,
                              (tk_rect){ card_x, card_y, card_w, card_h });
        }
        tk_set_fixed_rect(user_list,
                          (tk_rect){ card_x, card_y + card_h + 12,
                                     card_w, lh });
    }
    tk_set_visible(user_list, n_users > 1 && !osk_open);
    /* int12: corner buttons use the 18px page margin and a
     * 10px pair gap (blind round 3 counted 14-16px margins
     * and an 8px gap as C7/C2 findings). */
    tk_set_fixed_rect(kbd_btn, (tk_rect){ W - 18 - 176, 18, 176, 48 });
    tk_set_fixed_rect(reboot_btn_w,
                      (tk_rect){ W - 18 - 104 - 10 - 104, H - 66, 104, 48 });
    tk_set_fixed_rect(power_btn_w,
                      (tk_rect){ W - 18 - 104, H - 66, 104, 48 });
    tk_set_visible(reboot_btn_w, !osk_open);
    tk_set_visible(power_btn_w, !osk_open);
    tk_set_fixed_rect(osk_widget, (tk_rect){ 0, H - sheet_h, W, sheet_h });
}

/* Power actions leave through the exit code: the session script
 * owns actually powering off (the greeter must not gain its own
 * privileged behaviour). 4 = power off requested, 5 = reboot. */
#define GREETER_EXIT_POWEROFF 4
#define GREETER_EXIT_REBOOT 5

static void attempt_login(void)
{
    char pw[256];
    tk_textfield_text(pass_field, pw, sizeof(pw));
    const char *user = users[selected_user].name;
    if (bd_greeter_verify(user, pw)) {
        printf("%s\n", user);
        fflush(stdout);
        done = true;
        exit_code = 0;
    } else {
        tk_textfield_set_text(pass_field, "");
        tk_label_set_text(error_label, "パスワードが違います");
    }
    /* do not keep the password around longer than needed */
    memset(pw, 0, sizeof(pw));
}

static void on_login_btn(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    attempt_login();
}

static const char *display_name(int i)
{
    return users[i].gecos[0] ? users[i].gecos : users[i].name;
}

/* Avatar: a 64x64 canvas with a surface_alt disc and the user's
 * initial - rendered once per selection through the toolkit's own
 * text path, then handed to the ImageView (which copies it). */
static void avatar_update(void)
{
    tk_canvas *cv = tk_canvas_new(64, 64);
    const tk_theme *t = tk_theme_default();
    /* the disc is the theme ACCENT with the initial in
     * on_accent (the Material / Windows avatar treatment):
     * surface_alt on the card's surface measured as no
     * visible circle at all (eval F-11) */
    tk_canvas_fill_round_rect(cv, (tk_rect){ 0, 0, 64, 64 }, 32,
                              t->accent);
    char init[8];
    const char *nm = display_name(selected_user);
    /* first UTF-8 character */
    int len = 1;
    unsigned char c0 = (unsigned char)nm[0];
    if (c0 >= 0xF0) len = 4;
    else if (c0 >= 0xE0) len = 3;
    else if (c0 >= 0xC0) len = 2;
    if ((size_t)len > strlen(nm)) len = 1;
    memcpy(init, nm, (size_t)len);
    init[len] = '\0';
    int tw = tk_text_width_px(t, 28, init);
    tk_canvas_draw_text_px(cv, t, 28, false, (64 - tw) / 2,
                           (64 - tk_line_height_px(t, 28)) / 2,
                           init, t->on_accent);
    tk_imageview_set_pixels(avatar_view, tk_canvas_pixels(cv), 64, 64);
    tk_canvas_free(cv);
}

static void on_user_select(tk_widget *w, void *ud)
{
    (void)ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0 && sel < n_users) {
        selected_user = sel;
        tk_label_set_text(name_label, display_name(sel));
        avatar_update();
        tk_textfield_set_text(pass_field, "");
    }
}

static void on_power_btn(tk_widget *w, void *ud)
{
    (void)w;
    done = true;
    exit_code = (int)(long)ud;
}

static void on_kbd_btn(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    osk_open = !osk_open;
    tk_button_set_label(kbd_btn, osk_open ? "キーボードを閉じる"
                                          : "キーボードを開く");
    /* layout is re-run by the main loop when this flag changes */
}

static void clock_update(void)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_min == last_clock_min) return;
    last_clock_min = tmv.tm_min;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    tk_label_set_text(clock_label, buf);
    static const char *const wday[7] = {
        "日", "月", "火", "水", "木", "金", "土"
    };
    snprintf(buf, sizeof(buf), "%d月%d日（%s）",
             tmv.tm_mon + 1, tmv.tm_mday, wday[tmv.tm_wday % 7]);
    tk_label_set_text(date_label, buf);
}

static int gui_main(void)
{
    /* Product font (M4 fix): the toolkit paints NO text until a
     * font is installed; use the same product loader as the app
     * mains (tk_font_load_default + tk_font_install). Only install
     * when the embedder has not installed one already, and free
     * only a font installed here. */
    tk_font *font = NULL;
    if (!tk_font_current()) {
        font = tk_font_load_default();
        if (font) tk_font_install(font);
    }
    n_users = bd_greeter_users(users, 32);
    if (n_users == 0) {
        fprintf(stderr, "braingreeter2: no login-capable users\n");
        if (font) tk_font_free(font);
        return 1;
    }
    tk_fb *fb = tk_fb_open("/dev/fb0");
    tk_evdev *keys = tk_evdev_open(NULL);
    if (!fb && !keys) {
        fprintf(stderr,
                "braingreeter2: no framebuffer or input device; "
                "refusing to idle headless (use --check)\n");
        if (font) tk_font_free(font);
        return 3;
    }
    int W = 854, H = 480; /* the panel's design size (M22) */
    tk_canvas *cv = tk_canvas_new(W, H);

    /* UI-REDESIGN 2.3 greeter: clock/date in the top third, a
     * centred authentication card (avatar + name + password +
     * login), a user switch list under the card, a power menu at
     * the bottom right, and the OSK as an opt-in bottom sheet
     * that shifts the card up instead of covering it (the M4
     * cut-off bottom row cannot recur: the OSK compresses into
     * the sheet rect it is given). */
    tk_widget *top = tk_fixed_new();
    clock_label = tk_label_new("--:--");
    tk_widget_set_text_style(clock_label,
                             H >= 360 ? TK_TEXT_DISPLAY : TK_TEXT_TITLE);
    date_label = tk_label_new("");
    tk_add_child(top, clock_label);
    tk_add_child(top, date_label);

    card = tk_fixed_new();
    /* radius in the same family as the pill-shaped field
     * and button it holds (eval F-11: a tight 8px card
     * around full pills read as mixed shapes; the ChromeOS
     * / M3 sign-in card pairs a large radius with pills) */
    tk_widget_set_backdrop(card, false, 20);
    avatar_view = tk_imageview_new();
    tk_add_child(card, avatar_view);
    name_label = tk_label_new(display_name(0));
    tk_widget_set_text_style(name_label, TK_TEXT_TITLE);
    tk_add_child(card, name_label);
    pass_field = tk_textfield_new("");
    /* Mask the echo (buffer keeps the real password) and pin the
     * no-clipboard policy from the header comment. */
    tk_textfield_set_password(pass_field, true);
    tk_textfield_set_clipboard(pass_field, NULL);
    tk_textfield_set_placeholder(pass_field, "パスワード");
    tk_add_child(card, pass_field);
    error_label = tk_label_new("");
    tk_add_child(card, error_label);
    login_btn_w = tk_button_new("ログイン");
    tk_button_set_accent(login_btn_w, true);
    tk_button_set_on_click(login_btn_w, on_login_btn, NULL);
    tk_add_child(card, login_btn_w);
    tk_add_child(top, card);

    user_list = tk_listview_new();
    {
        const char *names[32];
        for (int i = 0; i < n_users; i++) names[i] = display_name(i);
        tk_listview_set_items(user_list, names, (size_t)n_users);
        tk_listview_select(user_list, 0);
    }
    tk_listview_set_on_select(user_list, on_user_select, NULL);
    tk_add_child(top, user_list);

    kbd_btn = tk_button_new("キーボードを開く");
    tk_button_set_on_click(kbd_btn, on_kbd_btn, NULL);
    tk_add_child(top, kbd_btn);
    reboot_btn_w = tk_button_new("再起動");
    tk_button_set_on_click(reboot_btn_w, on_power_btn,
                           (void *)(long)GREETER_EXIT_REBOOT);
    tk_add_child(top, reboot_btn_w);
    power_btn_w = tk_button_new("電源オフ");
    tk_button_set_on_click(power_btn_w, on_power_btn,
                           (void *)(long)GREETER_EXIT_POWEROFF);
    tk_add_child(top, power_btn_w);

    osk_widget = tk_osk_new();
    /* Password entry wants Latin direct input, not kana
     * conversion: open the OSK on QWERTY in its English (direct)
     * sub-mode so keys commit straight to the field. */
    tk_osk_set_mode(osk_widget, OSK_MODE_QWERTY);
    tk_osk_set_direct(osk_widget, true);
    tk_osk_attach(osk_widget, pass_field);
    tk_set_visible(osk_widget, false);
    tk_add_child(top, osk_widget);

    tk_root *root = tk_root_new(top);
    avatar_update();
    clock_update();
    greeter_place(W, H);
    tk_root_layout(root, W, H);
    tk_focus(root, pass_field);

    const tk_theme *theme = tk_theme_default();
    bool last_osk_open = osk_open;
    while (!done) {
        if (osk_open != last_osk_open) {
            last_osk_open = osk_open;
            tk_set_visible(osk_widget, osk_open);
            greeter_place(W, H);
            tk_root_layout(root, W, H);
        }
        clock_update();
        tk_root_paint(root, cv, theme);
        if (fb) tk_fb_flush(fb, cv);
        tk_event ev;
        bool got = keys ? tk_evdev_poll(keys, &ev) : false;
        if (got) {
            if (ev.type == TK_EV_KEY_DOWN && ev.key == TK_KEY_ENTER)
                attempt_login();
            else
                tk_root_event(root, &ev);
        } else {
            usleep(20000);
        }
    }
    tk_root_free(root);
    tk_widget_free(top);
    tk_canvas_free(cv);
    if (keys) tk_evdev_close(keys);
    if (fb) tk_fb_close(fb);
    if (font) tk_font_free(font);
    return exit_code;
}

int main(int argc, char **argv)
{
    /* display mode from brainreg (ui/mode; thememode.h) */
    tk_theme_set_default(bd_theme_from_reg());
    if (argc >= 2 && strcmp(argv[1], "--check") == 0) {
        if (argc != 4) {
            fprintf(stderr, "usage: braingreeter2 --check USER PASS\n");
            return 2;
        }
        return bd_greeter_verify(argv[2], argv[3]) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--list") == 0) {
        bd_greeter_user u[32];
        int n = bd_greeter_users(u, 32);
        for (int i = 0; i < n; i++) printf("%s\n", u[i].name);
        return n > 0 ? 0 : 1;
    }
    return gui_main();
}
