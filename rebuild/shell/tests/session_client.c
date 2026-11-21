/* session_client - WM client driven by test_session through a
 * stdout line protocol:
 *   READY <win-id>   after connecting (window created)
 *   COMMITTED        after filling the window and committing
 *   KEY <key>        a key-down event was delivered
 *   POINTER <x> <y>  a pointer-down in content coordinates
 *   CLOSED           the server asked us to close; exit 0
 * The window is filled with a solid color so the parent can
 * verify the session composited it onto its canvas. */
#include "wm.h"
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>

static const tk_color FILL = { 200, 30, 40, 255 };

static void draw_and_commit(bd_wm_client *cl)
{
    tk_canvas *cv = bd_wm_client_canvas(cl);
    if (!cv) return;
    tk_canvas_clear(cv, FILL);
    tk_rect full = { 0, 0, bd_wm_client_width(cl),
                     bd_wm_client_height(cl) };
    (void)bd_wm_client_commit(cl, &full, 1);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    bd_wm_client *cl = bd_wm_client_open(argv[1], "Session App", 200, 100);
    if (!cl) return 3;
    printf("READY %u\n", bd_wm_client_id(cl));
    fflush(stdout);
    draw_and_commit(cl);
    printf("COMMITTED\n");
    fflush(stdout);
    for (;;) {
        bd_wm_client_event ev;
        int rc = bd_wm_client_poll(cl, &ev, 200);
        if (rc < 0) break;
        if (rc == 0) continue;
        switch (ev.type) {
        case BD_WM_CEV_KEY_DOWN:
            printf("KEY key=%d text=%s\n", ev.key, ev.text);
            fflush(stdout);
            break;
        case BD_WM_CEV_POINTER_DOWN:
            printf("POINTER %d %d\n", ev.x, ev.y);
            fflush(stdout);
            break;
        case BD_WM_CEV_RESIZED:
            draw_and_commit(cl);
            break;
        case BD_WM_CEV_CLOSE:
            printf("CLOSED\n");
            fflush(stdout);
            bd_wm_client_close(cl);
            return 0;
        default:
            break;
        }
    }
    bd_wm_client_close(cl);
    return 1;
}
