/* shot_greeter - screenshot of the real braingreeter2 login screen.
 *
 * braingreeter2's UI construction lives in a static gui_main()
 * inside shell/greeter/braingreeter2.c and normally runs against
 * /dev/fb0 + evdev in an endless loop. To photograph the genuine
 * product rendering headlessly, this file #includes that .c file
 * with four seams redirected:
 *   - tk_evdev_open/poll/close -> inert stubs (no input devices on
 *     the host; the greeter's "no fb and no input" refusal needs a
 *     non-NULL input handle, and poll simply never delivers events)
 *   - tk_root_paint -> a hook that, on the first frame, types a
 *     password into the product's own password field (so the M3
 *     mask renders), calls the real tk_root_paint, dumps the
 *     canvas and ends the loop via the greeter's own `done` flag
 *   - main -> renamed, ours below runs instead
 * The user list comes from the product's own bd_greeter_users()
 * reading a fixture passwd via its documented BD_GREETER_PASSWD
 * override (two login users with Japanese GECOS names).
 * Every widget, layout and painted pixel is product code.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tk.h"
#include "osk.h"
#include "greeter2_auth.h"
#include "shot_common.h"

/* hook prototypes (the macro-renamed call sites need declarations) */
void shot_root_paint(tk_root *r, tk_canvas *c, const tk_theme *t);
tk_evdev *shot_evdev_open(const char *path);
bool shot_evdev_poll(tk_evdev *e, tk_event *ev);
void shot_evdev_close(tk_evdev *e);

#define tk_root_paint shot_root_paint
#define tk_evdev_open shot_evdev_open
#define tk_evdev_poll shot_evdev_poll
#define tk_evdev_close shot_evdev_close
#define main shot_greeter_main
#include "../../shell/greeter/braingreeter2.c"
#undef main
#undef tk_evdev_close
#undef tk_evdev_poll
#undef tk_evdev_open
#undef tk_root_paint

tk_evdev *shot_evdev_open(const char *path)
{
    (void)path;
    return (tk_evdev *)1; /* dummy non-NULL handle */
}

bool shot_evdev_poll(tk_evdev *e, tk_event *ev)
{
    (void)e; (void)ev;
    return false;
}

void shot_evdev_close(tk_evdev *e) { (void)e; }

static int shot_dumped;

void shot_root_paint(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    if (!shot_dumped) {
        /* a typed password, shown through the product's mask */
        tk_textfield_set_text(pass_field, "brain2026pw");
        tk_root_paint(r, c, t); /* real paint (macro undef'd above) */
        shot_dump("screenshots/raw/v2_05_greeter.raw", c);
        shot_dumped = 1;
        done = 1; /* the greeter's own loop-exit flag */
        return;
    }
    tk_root_paint(r, c, t);
}

int main(void)
{
    const char *pw = "/tmp/shot-greeter-passwd";
    FILE *f = fopen(pw, "w");
    if (!f) { perror(pw); return 2; }
    fputs("root:x:0:0:root:/root:/bin/bash\n", f);
    fputs("brain:x:1000:1000:ぶれいん太郎,,,:/home/brain:/bin/bash\n", f);
    fputs("guest:x:1001:1001:ゲスト,,,:/home/guest:/bin/bash\n", f);
    fclose(f);
    setenv("BD_GREETER_PASSWD", pw, 1);

    shot_install_font();
    char *argv[] = { (char *)"braingreeter2", NULL };
    return shot_greeter_main(1, argv);
}
