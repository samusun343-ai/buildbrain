/* test_greeter_font - pins the M4 product-font fix for the greeter.
 *
 * braingreeter2's gui_main lives in greeter/braingreeter2.c and
 * normally runs against /dev/fb0 + evdev. Like the screenshot
 * harness, this test #includes that .c file with the evdev calls
 * stubbed and tk_root_paint hooked - but, unlike the harness, it
 * deliberately does NOT install a font first. Before the fix the
 * greeter never installed one either, so its first frame contained
 * no text at all (the toolkit paints nothing without a font). The
 * test asserts the product entry now installs the bundled font by
 * itself: during the first painted frame a font is current and the
 * frame contains real glyph pixels; after gui_main returns, the
 * font it installed is freed again.
 */
#define _POSIX_C_SOURCE 200809L
/* _GNU_SOURCE comes from the Makefile's CPPFLAGS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tk.h"
#include "osk.h"
#include "greeter2_auth.h"

void hook_root_paint(tk_root *r, tk_canvas *c, const tk_theme *t);
tk_evdev *hook_evdev_open(const char *path);
bool hook_evdev_poll(tk_evdev *e, tk_event *ev);
void hook_evdev_close(tk_evdev *e);

#define tk_root_paint hook_root_paint
#define tk_evdev_open hook_evdev_open
#define tk_evdev_poll hook_evdev_poll
#define tk_evdev_close hook_evdev_close
#define main greeter_product_main
#include "../greeter/braingreeter2.c"
#undef main
#undef tk_evdev_close
#undef tk_evdev_poll
#undef tk_evdev_open
#undef tk_root_paint

tk_evdev *hook_evdev_open(const char *path)
{
    (void)path;
    return (tk_evdev *)1; /* dummy non-NULL handle */
}

bool hook_evdev_poll(tk_evdev *e, tk_event *ev)
{
    (void)e; (void)ev;
    return false;
}

void hook_evdev_close(tk_evdev *e) { (void)e; }

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int frames;
static int first_frame_dark;
static int font_during_paint;

void hook_root_paint(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    tk_root_paint(r, c, t); /* real paint (macro undef'd above) */
    if (frames == 0) {
        font_during_paint = tk_font_current() != NULL;
        int dark = 0;
        int w = tk_canvas_width(c), h = tk_canvas_height(c);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                tk_color p = tk_canvas_pixel(c, x, y);
                if (p.r < 110 && p.g < 110 && p.b < 110) dark++;
            }
        first_frame_dark = dark;
    }
    frames++;
    done = 1; /* the greeter's own loop-exit flag */
}

int main(void)
{
    /* fixture passwd with two login-capable users */
    char pwpath[] = "/tmp/bdgreeterfontXXXXXX";
    int fd = mkstemp(pwpath);
    if (fd < 0) { perror("mkstemp"); return 2; }
    FILE *f = fdopen(fd, "w");
    fputs("brain:x:1000:1000:ぶれいん太郎,,,:/home/brain:/bin/bash\n", f);
    fputs("guest:x:1001:1001:ゲスト,,,:/home/guest:/bin/bash\n", f);
    fclose(f);
    setenv("BD_GREETER_PASSWD", pwpath, 1);

    /* the precondition the fix addresses: nobody installed a font */
    CHECK(tk_font_current() == NULL);

    char *argv[] = { (char *)"braingreeter2", NULL };
    int rc = greeter_product_main(1, argv);
    CHECK(rc == 0);
    CHECK(frames >= 1);
    /* the product entry installed the font by itself... */
    CHECK(font_during_paint);
    /* ...and its first frame contains real rendered text */
    CHECK(first_frame_dark > 200);
    /* a font installed by gui_main is freed when it exits */
    CHECK(tk_font_current() == NULL);

    unlink(pwpath);
    printf("test_greeter_font: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
