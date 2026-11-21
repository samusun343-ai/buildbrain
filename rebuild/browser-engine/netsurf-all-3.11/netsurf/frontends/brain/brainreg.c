/*
 * Brainux "brain" frontend: brainreg bridge.
 * See brainreg.h and DESIGN-brain-frontend.md section 5.
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils/log.h"
#include "utils/nsoption.h"

#include "reg.h"

#include "brain/brainreg.h"

#define BRAINREG_SECTION "browser"

/** Default total budget when brainreg has no browser.budget_mb:
 * the M0 profile ceiling (REPORT-browser-m0.md section 2.2). */
#define BRAIN_DEFAULT_BUDGET_MB 56

static void apply_string(const char *key, enum nsoption_e opt)
{
	char *v = reg_get(BRAINREG_SECTION, key);
	if (v != NULL) {
		nsoption_set_tbl_charp(nsoptions, opt, v); /* takes ownership */
	}
}

static void store_string(const char *key, enum nsoption_e opt)
{
	const char *v = nsoptions[opt].value.s;
	if (v != NULL) {
		reg_set(BRAINREG_SECTION, key, v);
	}
}

static void store_int(const char *key, int v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", v);
	reg_set(BRAINREG_SECTION, key, buf);
}

static void store_bool(const char *key, bool v)
{
	reg_set(BRAINREG_SECTION, key, v ? "true" : "false");
}

/* exported interface documented in brainreg.h */
void brain_brainreg_apply(void)
{
	int budget_mb;

	/* Budget first: netsurf_init() derives the llcache limit,
	 * the image pixel cap and the JS heap from brain_budget_mb. */
	budget_mb = reg_get_int(BRAINREG_SECTION, "budget_mb",
			BRAIN_DEFAULT_BUDGET_MB);
	if (budget_mb < 8) {
		budget_mb = 8;
	}
	if (budget_mb > 96) {
		budget_mb = 96;
	}
	nsoption_set_int(brain_budget_mb, budget_mb);

	/* Disc cache: capacity-driven only (disc_cache_age stays at
	 * the M0 default of 0; no day-based expiry anywhere). */
	{
		int disc_mb = reg_get_int(BRAINREG_SECTION,
				"disc_cache_mb", 0);
		if (disc_mb > 0) {
			nsoption_set_uint(disc_cache_size,
					(unsigned)disc_mb * 1024U * 1024U);
		}
	}

	/* JavaScript defaults ON for this frontend (the M0 budget
	 * profile reserves a JS heap); brainreg can turn it off. */
	nsoption_set_bool(enable_javascript,
			reg_get_bool(BRAINREG_SECTION,
				"javascript_enabled", true));

	/* Images: the core master switch is suppress_images. */
	nsoption_set_bool(suppress_images,
			!reg_get_bool(BRAINREG_SECTION,
				"images_enabled", true));

	apply_string("homepage", NSOPTION_homepage_url);

	/* The URL bar doubles as a search box (Chrome model):
	 * text that is not a URL goes to the search provider. */
	nsoption_set_bool(search_url_bar, true);
	{
		int provider = reg_get_int(BRAINREG_SECTION,
				"search_provider", -1);
		if (provider >= 0) {
			nsoption_set_int(search_provider, provider);
		}
	}

	{
		int font_size = reg_get_int(BRAINREG_SECTION,
				"font_size", 0);
		if (font_size > 0) {
			nsoption_set_int(font_size, font_size);
		}
		/* Default page scale for this frontend: 125% when
		 * brainreg has no stored value.  At 100% a 16px body
		 * font is 2.2mm tall on the PW-SH6 panel (186.8ppi) -
		 * smaller than the same CSS size on a phone (~2.5mm);
		 * at 125% it is 2.7mm and the layout viewport is
		 * 800/1.25 = 640 CSS px wide.  The settings dialog's
		 * text-size control rewrites this key (100/125/150).
		 * See REPORT-brain-frontend.md section 9. */
		int scale = reg_get_int(BRAINREG_SECTION,
				"default_scale", 125);
		if (scale > 0) {
			nsoption_set_int(scale, scale);
		}
	}

	/* user_agent and download_dir have no core option in
	 * NetSurf 3.11; they live in brainreg only (DESIGN section 5
	 * lists them as registry keys) and are written back by
	 * brain_brainreg_store() when set there. */

	/* The display mode is NOT a "browser" key: its canonical
	 * home is brainreg section "ui" (key "mode", plus "dark"),
	 * owned by the shell's thememode.h contract.  The same
	 * reg_get() API used for the "browser" section above reads
	 * it (reg_get("ui","mode")); gui.c performs the actual
	 * theme selection through bd_theme_from_reg(), the shared
	 * reader, and this log line records what the store holds
	 * at startup. */
	{
		char *ui_mode = reg_get("ui", "mode");
		NSLOG(netsurf, INFO, "brainreg: ui/mode=%s, ui/dark=%s",
		      ui_mode != NULL ? ui_mode : "android (default)",
		      reg_get_bool("ui", "dark", false) ?
				"true" : "false");
		free(ui_mode);
	}

	NSLOG(netsurf, INFO,
	      "brainreg: budget %dMB, javascript %s, images %s",
	      budget_mb,
	      nsoption_bool(enable_javascript) ? "on" : "off",
	      nsoption_bool(suppress_images) ? "off" : "on");
}

/* exported interface documented in brainreg.h */
void brain_brainreg_store(void)
{
	store_int("budget_mb", nsoption_int(brain_budget_mb));
	store_int("disc_cache_mb",
			(int)(nsoption_uint(disc_cache_size) / (1024U * 1024U)));
	store_bool("javascript_enabled", nsoption_bool(enable_javascript));
	store_bool("images_enabled", !nsoption_bool(suppress_images));
	store_string("homepage", NSOPTION_homepage_url);
	store_int("search_provider", nsoption_int(search_provider));
	store_int("font_size", nsoption_int(font_size));
	store_int("default_scale", nsoption_int(scale));
}
