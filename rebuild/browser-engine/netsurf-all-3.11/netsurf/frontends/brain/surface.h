/*
 * Brainux "brain" frontend: libnsfb surface lifecycle + plotters.
 * Adapted from the framebuffer frontend's framebuffer.c; the
 * plotter table is identical, the surface setup differs (XRGB8888
 * everywhere, target=shm support, toolkit-owned input in WM mode).
 */

#ifndef BRAIN_SURFACE_H
#define BRAIN_SURFACE_H

#include <stdbool.h>
#include <libnsfb.h>

#include "netsurf/plotters.h"

extern const struct plotter_table brain_plotters;

/** Create the surface for WM mode: target=shm over the given
 * caller-owned buffer.  Input is disabled (the WM delivers toolkit
 * events); geometry is the window size. */
nsfb_t *brain_surface_create_shm(void *pixels, int stride,
		int width, int height);

/** Re-point a target=shm surface at a different buffer (WM
 * double-buffer swap or resize generation change). */
void brain_surface_repoint(nsfb_t *nsfb, void *pixels, int stride);

/** Create the surface for standalone mode over a framebuffer
 * device (a regular file works, for tests).  The surface is asked
 * for XRGB8888 so the toolkit can paint into the same shadow
 * buffer the plotters use; input comes from the surface's evdev
 * reader unless input is "none". */
nsfb_t *brain_surface_create_fb(const char *fbdev, const char *input,
		int width, int height);

/** Finalise and free a surface created above. */
void brain_surface_destroy(nsfb_t *nsfb);

/** Set the surface bitmaps are rendered through (bitmap.c). */
nsfb_t *brain_set_surface(nsfb_t *nsfb);

#endif /* BRAIN_SURFACE_H */
