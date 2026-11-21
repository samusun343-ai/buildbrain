/*
 * Copyright 2012 Vincent Sanders <vince@simtec.co.uk>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation. See the GNU General Public License
 * for more details; if in doubt, see http://www.gnu.org/licenses/
 */

#ifndef _NETSURF_BRAIN_OPTIONS_H_
#define _NETSURF_BRAIN_OPTIONS_H_

/* currently nothing here */

#endif

/***** brain frontend options *****/

/** Hotlist (bookmarks) file, below $HOME/.netsurf by default. */
NSOPTION_STRING(hotlist_path, NULL)
/** URL database (history) file, below $HOME/.netsurf by default. */
NSOPTION_STRING(url_file, NULL)

/** Display mode: "auto" (WM client when a braindesk2 server is
 * reachable, standalone framebuffer otherwise), "wm" or "fb".
 * Overridable with the BRAIN_MODE environment variable. */
NSOPTION_STRING(brain_mode, NULL)
/** Framebuffer device for standalone mode; defaults to the
 * BRAIN_FBDEV environment variable or /dev/fb0.  A regular file
 * is accepted (the libnsfb brain surface fake-device mode), which
 * is how the host screenshot harness drives standalone mode. */
NSOPTION_STRING(brain_fbdev, NULL)
/** evdev path list for standalone mode input; "none" disables
 * surface input (toolkit backend only). */
NSOPTION_STRING(brain_input, NULL)

/* The memory budget options (brain_budget_mb, brain_max_image_pixels,
 * brain_js_heap_kb) are core options defined in desktop/options.h;
 * the derivation from brain_budget_mb happens in netsurf_init()
 * (M0).  brainreg.c only has to feed browser.budget_mb into the
 * core option before netsurf_init() runs. */

/***** font options *****/

/** render all fonts monochrome */
NSOPTION_BOOL(fb_font_monochrome, false)
/** size of font glyph cache in kilobytes. */
NSOPTION_INTEGER(fb_font_cachesize, 2048)

/* Font face paths. These are treated as absolute paths if they start
 * with a / otherwise the compile time resource path is searched.
 * The brain frontend fills these in at startup with the first font
 * file that exists (bundled subset font first), so page text and
 * toolkit text use the same typeface.
 */
NSOPTION_STRING(fb_face_sans_serif, NULL)
NSOPTION_STRING(fb_face_sans_serif_bold, NULL)
NSOPTION_STRING(fb_face_sans_serif_italic, NULL)
NSOPTION_STRING(fb_face_sans_serif_italic_bold, NULL)
NSOPTION_STRING(fb_face_serif, NULL)
NSOPTION_STRING(fb_face_serif_bold, NULL)
NSOPTION_STRING(fb_face_monospace, NULL)
NSOPTION_STRING(fb_face_monospace_bold, NULL)
NSOPTION_STRING(fb_face_cursive, NULL)
NSOPTION_STRING(fb_face_fantasy, NULL)

/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
