/* bdicon - product icons from established vector sets.
 *
 * Policy (user direction, 2026-11-21): no code-drawn raster
 * glyphs and no font-glyph / text-character substitutes for
 * icons. Icons come from established vector sets, rasterised
 * offline with anti-aliasing (assets/icons/, see its MANIFEST):
 *   "material/<name>"  Material Symbols (Rounded, filled)
 *   "fluent/<name>"    Fluent UI System Icons
 *   "papirus/<name>"   Papirus (full colour)
 * The device side only decodes the pre-rendered PNGs; no SVG
 * parser ships to the device.
 *
 * bd_icon() returns a borrowed box*box RGBA buffer (0xAARRGGBB,
 * the toolkit's blit format), owned by a per-process cache.
 * Monochrome sets are stored black-on-transparent; pass `tint`
 * (0xRRGGBB) to colour them (the stored alpha is the shape).
 * Pass tint == BD_ICON_NATURAL to keep the source colours
 * (Papirus). Scaling from the stored render down to `box` uses
 * the toolkit's area-average scaler, so edges stay smooth.
 *
 * Cache discipline (standing rule): the cache is byte-budgeted
 * (BD_ICON_CACHE_MAX). When the budget would be exceeded the
 * load is refused (NULL) instead of growing without bound;
 * callers must tolerate NULL exactly as they tolerated the old
 * no-font NULL icons.
 */
#ifndef BD_ICON_H
#define BD_ICON_H

#include "tk.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BD_ICON_NATURAL 0xFFFFFFFFu
#define BD_ICON_CACHE_MAX ((size_t)1024 * 1024)
#define BD_ICON_ENTRIES 128

typedef struct {
    char spec[96];
    int box;
    uint32_t tint;
    uint32_t *px;
} bd_icon_entry;

static bd_icon_entry bd_icon_cache[BD_ICON_ENTRIES];
static size_t bd_icon_cache_used;

static const char *bd_icon_prefixes[] = {
    "assets/icons/", "../assets/icons/", "../../assets/icons/",
    "../../../assets/icons/",
    /* device image placement (integration): the icon set is
     * installed under the same assets root as the shell's
     * status icons, /usr/share/braindesk/assets/ */
    "/usr/share/braindesk/assets/icons/",
};

static const uint32_t *bd_icon(const char *spec, int box, uint32_t tint)
{
    if (!spec || box <= 0) return NULL;
    for (int i = 0; i < BD_ICON_ENTRIES; i++) {
        bd_icon_entry *e = &bd_icon_cache[i];
        if (e->px && e->box == box && e->tint == tint &&
            strcmp(e->spec, spec) == 0)
            return e->px;
    }
    /* resolve the file */
    char path[512];
    bool found = false;
    for (size_t p = 0;
         p < sizeof(bd_icon_prefixes) / sizeof(bd_icon_prefixes[0]);
         p++) {
        snprintf(path, sizeof(path), "%s%s.png",
                 bd_icon_prefixes[p], spec);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); found = true; break; }
    }
    if (!found) return NULL;
    uint32_t *src = NULL;
    int sw = 0, sh = 0;
    if (!tk_image_load_thumbnail(path, 192, 192, &src, &sw, &sh) ||
        !src)
        return NULL;
    uint32_t *out = NULL;
    int ow = 0, oh = 0;
    if (sw == box && sh == box) {
        out = src;
        ow = box;
        oh = box;
    } else if (tk_image_scale_to_fit(src, sw, sh, box, box,
                                     &out, &ow, &oh)) {
        free(src);
    } else {
        free(src);
        return NULL;
    }
    if (ow != box || oh != box) { /* non-square source: refuse */
        free(out);
        return NULL;
    }
    if (tint != BD_ICON_NATURAL) {
        uint32_t rgb = tint & 0x00FFFFFFu;
        for (int i = 0; i < box * box; i++) {
            uint32_t a = out[i] & 0xFF000000u;
            out[i] = a | rgb;
        }
    }
    size_t bytes = (size_t)box * (size_t)box * sizeof(uint32_t);
    if (bd_icon_cache_used + bytes > BD_ICON_CACHE_MAX) {
        free(out);
        return NULL;
    }
    for (int i = 0; i < BD_ICON_ENTRIES; i++) {
        bd_icon_entry *e = &bd_icon_cache[i];
        if (!e->px) {
            snprintf(e->spec, sizeof(e->spec), "%s", spec);
            e->box = box;
            e->tint = tint;
            e->px = out;
            bd_icon_cache_used += bytes;
            return e->px;
        }
    }
    free(out);
    return NULL;
}

/* Mode pair convenience: pick the Fluent name in WinUI mode,
 * the Material name otherwise. Names are bare icon names; the
 * set prefix is added here. */
static const uint32_t *bd_icon_mode(bool winui, const char *material,
                                    const char *fluent, int box,
                                    uint32_t tint)
{
    char spec[96];
    snprintf(spec, sizeof(spec), "%s/%s",
             winui ? "fluent" : "material",
             winui ? fluent : material);
    return bd_icon(spec, box, tint);
}

/* Full-colour (Papirus) icon. */
static const uint32_t *bd_icon_color(const char *name, int box)
{
    char spec[96];
    snprintf(spec, sizeof(spec), "papirus/%s", name);
    return bd_icon(spec, box, BD_ICON_NATURAL);
}

static size_t bd_icon_cache_bytes(void) { return bd_icon_cache_used; }

static void bd_icon_clear(void)
{
    for (int i = 0; i < BD_ICON_ENTRIES; i++) {
        free(bd_icon_cache[i].px);
        bd_icon_cache[i].px = NULL;
        bd_icon_cache[i].spec[0] = '\0';
    }
    bd_icon_cache_used = 0;
}

#endif /* BD_ICON_H */
