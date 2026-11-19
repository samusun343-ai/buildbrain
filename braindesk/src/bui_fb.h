/* Display backends for libbrainui: Linux fbdev on the device,
 * an in-memory surface everywhere else (tests, screenshots). */
#ifndef BUI_FB_H
#define BUI_FB_H

#include "bui.h"

typedef struct bu_display bu_display;

/* Opens /dev/fb0 (or the path in BUI_FB) when available; falls back
 * to a memory surface of the requested size. */
bu_display *bu_display_open(int w, int h);
void bu_display_close(bu_display *d);
/* Re-assert KD_GRAPHICS after a child app restored KD_TEXT on exit. */
void bu_display_regrab(bu_display *d);
int bu_display_width(bu_display *d);
int bu_display_height(bu_display *d);
bool bu_display_is_real(bu_display *d);
/* Present the canvas (converts to the fb pixel format, flips when
 * the fb supports it). Only the damage rect is copied. */
void bu_display_present(bu_display *d, bu_canvas *c, bu_rect damage);
/* Memory-backend only: dump the last presented frame. */
bool bu_display_dump_ppm(bu_display *d, const char *path);

#endif
