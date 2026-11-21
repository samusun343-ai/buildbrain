/* shot_common - shared helpers for the UI screenshot harnesses.
 *
 * These programs render REAL product UI (toolkit v2 widgets, the
 * shell, the greeter, the OSK, the apps) into a memory canvas via
 * the same tk_root_paint() path the products use, and dump the
 * resulting pixels as a raw RGB file (u32 w, u32 h, then RGB
 * triplets) for conversion to PNG/JPG by convert.py.
 *
 * No drawing code lives here beyond the pixel dump itself: every
 * pixel on screen comes from product code.
 */
#ifndef SHOT_COMMON_H
#define SHOT_COMMON_H

#include "tk.h"

#include <stdint.h>
#include <stdio.h>

static int shot_dump(const char *path, tk_canvas *cv)
{
    int w = tk_canvas_width(cv), h = tk_canvas_height(cv);
    uint32_t *px = tk_canvas_pixels(cv);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    uint32_t hdr[2] = { (uint32_t)w, (uint32_t)h };
    fwrite(hdr, 4, 2, f);
    for (int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = { (uint8_t)(px[i] >> 16),
                           (uint8_t)(px[i] >> 8),
                           (uint8_t)px[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("shot: %s (%dx%d)\n", path, w, h);
    return 0;
}

/* Paint a root at w x h into a fresh canvas and dump it. */
static int shot_root(const char *path, tk_root *root, int w, int h)
{
    tk_canvas *cv = tk_canvas_new(w, h);
    if (!cv) return -1;
    tk_root_paint(root, cv, tk_theme_default());
    int rc = shot_dump(path, cv);
    tk_canvas_free(cv);
    return rc;
}

/* Install the bundled product font (fonts/brainui-jp-subset.ttf)
 * through the toolkit's own loader - the same call the app mains
 * make. Run with cwd = rebuild/ so the relative search path hits. */
static tk_font *shot_install_font(void)
{
    tk_font *f = tk_font_load_bundled();
    if (f) tk_font_install(f);
    else fprintf(stderr, "shot: bundled font not found\n");
    return f;
}

#endif /* SHOT_COMMON_H */
