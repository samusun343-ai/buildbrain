/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
 *
 * Surface and plotter glue for the Brainux "brain" frontend.
 * The plotter implementations are adapted from the framebuffer
 * frontend (frontends/framebuffer/framebuffer.c); the surface
 * lifecycle is brain-specific: XRGB8888 only (bit-identical to
 * the toolkit's pixel packing, so page and chrome share one
 * buffer with zero copies), target=shm for WM mode, and no
 * software cursor (single-finger touch device).
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/utf8.h"
#include "netsurf/browser_window.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"

#include "brain/gui.h"
#include "brain/surface.h"
#include "brain/font.h"
#include "brain/bitmap.h"

/* surface the plotters render through; swapped by bitmap.c when
 * it renders into an off-screen bitmap surface */
static nsfb_t *nsfb;

static nserror
brain_plot_clip(const struct redraw_context *ctx, const struct rect *clip)
{
	nsfb_bbox_t nsfb_clip;
	nsfb_clip.x0 = clip->x0;
	nsfb_clip.y0 = clip->y0;
	nsfb_clip.x1 = clip->x1;
	nsfb_clip.y1 = clip->y1;

	if (!nsfb_plot_set_clip(nsfb, &nsfb_clip)) {
		return NSERROR_INVALID;
	}
	return NSERROR_OK;
}

static nserror
brain_plot_arc(const struct redraw_context *ctx,
	       const plot_style_t *style,
	       int x, int y, int radius, int angle1, int angle2)
{
	if (!nsfb_plot_arc(nsfb, x, y, radius, angle1, angle2,
			   style->fill_colour)) {
		return NSERROR_INVALID;
	}
	return NSERROR_OK;
}

static nserror
brain_plot_disc(const struct redraw_context *ctx,
		const plot_style_t *style,
		int x, int y, int radius)
{
	nsfb_bbox_t ellipse;
	ellipse.x0 = x - radius;
	ellipse.y0 = y - radius;
	ellipse.x1 = x + radius;
	ellipse.y1 = y + radius;

	if (style->fill_type != PLOT_OP_TYPE_NONE) {
		nsfb_plot_ellipse_fill(nsfb, &ellipse, style->fill_colour);
	}

	if (style->stroke_type != PLOT_OP_TYPE_NONE) {
		nsfb_plot_ellipse(nsfb, &ellipse, style->stroke_colour);
	}
	return NSERROR_OK;
}

static nserror
brain_plot_line(const struct redraw_context *ctx,
		const plot_style_t *style,
		const struct rect *line)
{
	nsfb_bbox_t rect;
	nsfb_plot_pen_t pen;

	rect.x0 = line->x0;
	rect.y0 = line->y0;
	rect.x1 = line->x1;
	rect.y1 = line->y1;

	if (style->stroke_type != PLOT_OP_TYPE_NONE) {
		if (style->stroke_type == PLOT_OP_TYPE_DOT) {
			pen.stroke_type = NFSB_PLOT_OPTYPE_PATTERN;
			pen.stroke_pattern = 0xAAAAAAAA;
		} else if (style->stroke_type == PLOT_OP_TYPE_DASH) {
			pen.stroke_type = NFSB_PLOT_OPTYPE_PATTERN;
			pen.stroke_pattern = 0xF0F0F0F0;
		} else {
			pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
		}

		pen.stroke_colour = style->stroke_colour;
		pen.stroke_width = plot_style_fixed_to_int(style->stroke_width);
		nsfb_plot_line(nsfb, &rect, &pen);
	}

	return NSERROR_OK;
}

static nserror
brain_plot_rectangle(const struct redraw_context *ctx,
		     const plot_style_t *style,
		     const struct rect *nsrect)
{
	nsfb_bbox_t rect;
	bool dotted = false;
	bool dashed = false;

	rect.x0 = nsrect->x0;
	rect.y0 = nsrect->y0;
	rect.x1 = nsrect->x1;
	rect.y1 = nsrect->y1;

	if (style->fill_type != PLOT_OP_TYPE_NONE) {
		nsfb_plot_rectangle_fill(nsfb, &rect, style->fill_colour);
	}

	if (style->stroke_type != PLOT_OP_TYPE_NONE) {
		if (style->stroke_type == PLOT_OP_TYPE_DOT) {
			dotted = true;
		}
		if (style->stroke_type == PLOT_OP_TYPE_DASH) {
			dashed = true;
		}
		nsfb_plot_rectangle(nsfb, &rect,
				plot_style_fixed_to_int(style->stroke_width),
				style->stroke_colour, dotted, dashed);
	}
	return NSERROR_OK;
}

static nserror
brain_plot_polygon(const struct redraw_context *ctx,
		   const plot_style_t *style,
		   const int *p,
		   unsigned int n)
{
	if (!nsfb_plot_polygon(nsfb, p, n, style->fill_colour)) {
		return NSERROR_INVALID;
	}
	return NSERROR_OK;
}

static nserror
brain_plot_path(const struct redraw_context *ctx,
		const plot_style_t *pstyle,
		const float *p,
		unsigned int n,
		const float transform[6])
{
	NSLOG(netsurf, INFO, "path unimplemented");
	return NSERROR_OK;
}

static nserror
brain_plot_bitmap(const struct redraw_context *ctx,
		  struct bitmap *bitmap,
		  int x, int y,
		  int width,
		  int height,
		  colour bg,
		  bitmap_flags_t flags)
{
	nsfb_bbox_t loc;
	nsfb_bbox_t clipbox;
	bool repeat_x = (flags & BITMAPF_REPEAT_X);
	bool repeat_y = (flags & BITMAPF_REPEAT_Y);
	int bmwidth;
	int bmheight;
	int bmstride;
	enum nsfb_format_e bmformat;
	unsigned char *bmptr;
	nsfb_t *bm = (nsfb_t *)bitmap;


	if (!(repeat_x || repeat_y)) {
		/* Not repeating at all, so just plot it */
		loc.x0 = x;
		loc.y0 = y;
		loc.x1 = loc.x0 + width;
		loc.y1 = loc.y0 + height;

		if (!nsfb_plot_copy(bm, NULL, nsfb, &loc)) {
			return NSERROR_INVALID;
		}
		return NSERROR_OK;
	}

	nsfb_plot_get_clip(nsfb, &clipbox);
	nsfb_get_geometry(bm, &bmwidth, &bmheight, &bmformat);
	nsfb_get_buffer(bm, &bmptr, &bmstride);

	/* Optimise tiled plots of 1x1 bitmaps by replacing with a flat
	 * fill of the area. Only when the image is fully opaque. */
	if ((bmwidth == 1) && (bmheight == 1)) {
		if ((*(nsfb_colour_t *)bmptr & 0xff000000) != 0) {
			if (!nsfb_plot_rectangle_fill(nsfb, &clipbox,
					      *(nsfb_colour_t *)bmptr)) {
				return NSERROR_INVALID;
			}
			return NSERROR_OK;
		}
	}

	/* Optimise tiled plots of bitmaps scaled to 1x1 similarly. */
	if ((width == 1) && (height == 1)) {
		if (brain_bitmap_get_opaque(bm)) {
			if (!nsfb_plot_rectangle_fill(nsfb, &clipbox,
					      *(nsfb_colour_t *)bmptr)) {
				return NSERROR_INVALID;
			}
			return NSERROR_OK;
		}
	}

	/* get left most tile position */
	if (repeat_x) {
		for (; x > clipbox.x0; x -= width);
	}

	/* get top most tile position */
	if (repeat_y) {
		for (; y > clipbox.y0; y -= height);
	}

	/* set up top left tile location */
	loc.x0 = x;
	loc.y0 = y;
	loc.x1 = loc.x0 + width;
	loc.y1 = loc.y0 + height;

	/* plot tiling across and down to extents */
	nsfb_plot_bitmap_tiles(nsfb, &loc,
			repeat_x ? ((clipbox.x1 - x) + width  - 1) / width  : 1,
			repeat_y ? ((clipbox.y1 - y) + height - 1) / height : 1,
			(nsfb_colour_t *)bmptr, bmwidth, bmheight,
			bmstride * 8 / 32, bmformat == NSFB_FMT_ABGR8888);

	return NSERROR_OK;
}

static nserror
brain_plot_text(const struct redraw_context *ctx,
		const struct plot_font_style *fstyle,
		int x,
		int y,
		const char *text,
		size_t length)
{
	uint32_t ucs4;
	size_t nxtchr = 0;
	FT_Glyph glyph;
	FT_BitmapGlyph bglyph;
	nsfb_bbox_t loc;

	while (nxtchr < length) {
		ucs4 = utf8_to_ucs4(text + nxtchr, length - nxtchr);
		nxtchr = utf8_next(text, length, nxtchr);

		glyph = fb_getglyph(fstyle, ucs4);
		if (glyph == NULL)
			continue;

		if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
			bglyph = (FT_BitmapGlyph)glyph;

			loc.x0 = x + bglyph->left;
			loc.y0 = y - bglyph->top;
			loc.x1 = loc.x0 + bglyph->bitmap.width;
			loc.y1 = loc.y0 + bglyph->bitmap.rows;

			/* now, draw to our target surface */
			if (bglyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
			    nsfb_plot_glyph1(nsfb,
					     &loc,
					     bglyph->bitmap.buffer,
					     bglyph->bitmap.pitch,
					     fstyle->foreground);
			} else {
			    nsfb_plot_glyph8(nsfb,
					     &loc,
					     bglyph->bitmap.buffer,
					     bglyph->bitmap.pitch,
					     fstyle->foreground);
			}
		}
		x += glyph->advance.x >> 16;

	}
	return NSERROR_OK;
}

/** brain frontend plot operation table */
const struct plotter_table brain_plotters = {
	.clip = brain_plot_clip,
	.arc = brain_plot_arc,
	.disc = brain_plot_disc,
	.line = brain_plot_line,
	.rectangle = brain_plot_rectangle,
	.polygon = brain_plot_polygon,
	.path = brain_plot_path,
	.bitmap = brain_plot_bitmap,
	.text = brain_plot_text,
	.option_knockout = true,
};

/* ---- surface lifecycle ---- */

static nsfb_t *brain_surface_alloc(void)
{
	enum nsfb_type_e type = nsfb_type_from_name("brain");

	if (type == NSFB_SURFACE_NONE) {
		NSLOG(netsurf, INFO,
		      "The brain surface is not available from libnsfb");
		return NULL;
	}
	return nsfb_new(type);
}

nsfb_t *brain_surface_create_shm(void *pixels, int stride,
		int width, int height)
{
	nsfb_t *s;
	char params[128];

	s = brain_surface_alloc();
	if (s == NULL) {
		return NULL;
	}
	snprintf(params, sizeof(params),
		 "target=shm;shmptr=%p;shmstride=%d;input=none;",
		 pixels, stride);
	nsfb_set_parameters(s, params);
	if (nsfb_set_geometry(s, width, height, NSFB_FMT_XRGB8888) == -1) {
		NSLOG(netsurf, INFO, "Unable to set shm surface geometry");
		nsfb_free(s);
		return NULL;
	}
	if (nsfb_init(s) == -1) {
		NSLOG(netsurf, INFO, "Unable to initialise shm surface");
		nsfb_free(s);
		return NULL;
	}
	nsfb = s;
	return s;
}

void brain_surface_repoint(nsfb_t *s, void *pixels, int stride)
{
	char params[128];

	if (s == NULL) {
		return;
	}
	snprintf(params, sizeof(params), "shmptr=%p;shmstride=%d;",
		 pixels, stride);
	nsfb_set_parameters(s, params);
}

nsfb_t *brain_surface_create_fb(const char *fbdev, const char *input,
		int width, int height)
{
	nsfb_t *s;
	char params[1024];
	size_t used = 0;

	s = brain_surface_alloc();
	if (s == NULL) {
		return NULL;
	}
	params[0] = '\0';
	if (fbdev != NULL && fbdev[0] != '\0' &&
	    strchr(fbdev, ';') == NULL) {
		used += snprintf(params + used, sizeof(params) - used,
				 "fbdev=%s;", fbdev);
	}
	if (input != NULL && input[0] != '\0' &&
	    strchr(input, ';') == NULL) {
		snprintf(params + used, sizeof(params) - used,
			 "input=%s;", input);
	}
	if (params[0] != '\0') {
		nsfb_set_parameters(s, params);
	}
	if (nsfb_set_geometry(s, width, height, NSFB_FMT_XRGB8888) == -1) {
		NSLOG(netsurf, INFO, "Unable to set surface geometry");
		nsfb_free(s);
		return NULL;
	}
	if (nsfb_init(s) == -1) {
		NSLOG(netsurf, INFO, "Unable to initialise nsfb surface");
		nsfb_free(s);
		return NULL;
	}
	nsfb = s;
	return s;
}

void brain_surface_destroy(nsfb_t *s)
{
	if (s == NULL) {
		return;
	}
	if (nsfb == s) {
		nsfb = NULL;
	}
	nsfb_free(s);
}

nsfb_t *brain_set_surface(nsfb_t *new_nsfb)
{
	nsfb_t *old_nsfb;
	old_nsfb = nsfb;
	nsfb = new_nsfb;
	return old_nsfb;
}
