/* wm_sample_raw - sample WM client written against wm.h only
 * (no toolkit). Draws a solid background and a white square that
 * follows pointer-downs; a key-down cycles the background color.
 * Used by test_wm to prove the protocol end to end.
 *
 * usage: wm_sample_raw <sock> <title> <w> <h> <rrggbb>
 */
#include "wm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t palette[] = {
    0xff3060a0u, 0xffa03030u, 0xff30a050u, 0xff806030u, 0xff603080u
};

static void fill(uint32_t *px, int stride, int w, int h, uint32_t v)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * stride + x] = v;
}

static void square(uint32_t *px, int stride, int w, int h, int cx, int cy)
{
    for (int y = cy - 10; y < cy + 10; y++)
        for (int x = cx - 10; x < cx + 10; x++)
            if (x >= 0 && x < w && y >= 0 && y < h)
                px[y * stride + x] = 0xffffffffu;
}

int main(int argc, char **argv)
{
    if (argc < 6) return 2;
    const char *sock = argv[1], *title = argv[2];
    int w = atoi(argv[3]), h = atoi(argv[4]);
    unsigned int rgb = (unsigned int)strtoul(argv[5], NULL, 16);
    int pal = 0;
    /* if the caller passed a palette color, start from palette[0]
     * tinted by it: keep it simple - exact color wins */
    uint32_t bg = 0xff000000u | rgb;

    bd_wm_client *cl = bd_wm_client_open(sock, title, w, h);
    if (!cl) return 1;

    int sqx = 30, sqy = 30;
    bool dirty_full = true;
    for (;;) {
        uint32_t *px = NULL;
        int stride = 0;
        px = bd_wm_client_pixels(cl, &stride);
        if (px && dirty_full) {
            fill(px, stride, bd_wm_client_width(cl),
                 bd_wm_client_height(cl), pal ? palette[pal] : bg);
            square(px, stride, bd_wm_client_width(cl),
                   bd_wm_client_height(cl), sqx, sqy);
            tk_rect full = { 0, 0, bd_wm_client_width(cl),
                             bd_wm_client_height(cl) };
            if (bd_wm_client_commit(cl, &full, 1) == 0) dirty_full = false;
        }
        bd_wm_client_event ev;
        int rc = bd_wm_client_poll(cl, &ev, 50);
        if (rc < 0) break;
        if (rc == 0) continue;
        switch (ev.type) {
        case BD_WM_CEV_CLOSE:
            bd_wm_client_close(cl);
            return 0;
        case BD_WM_CEV_RESIZED:
            dirty_full = true;
            break;
        case BD_WM_CEV_POINTER_DOWN: {
            int ox = sqx, oy = sqy;
            sqx = ev.x; sqy = ev.y;
            uint32_t *p2 = bd_wm_client_pixels(cl, &stride);
            if (p2) {
                fill(p2, stride, bd_wm_client_width(cl),
                     bd_wm_client_height(cl), pal ? palette[pal] : bg);
                square(p2, stride, bd_wm_client_width(cl),
                       bd_wm_client_height(cl), sqx, sqy);
                tk_rect dmg[2] = {
                    { ox - 12, oy - 12, 24, 24 },
                    { sqx - 12, sqy - 12, 24, 24 }
                };
                bd_wm_client_commit(cl, dmg, 2);
            }
            break;
        }
        case BD_WM_CEV_KEY_DOWN:
            pal = (pal + 1) % (int)(sizeof(palette) / sizeof(palette[0]));
            dirty_full = true;
            break;
        default:
            break;
        }
    }
    bd_wm_client_close(cl);
    return 0;
}
