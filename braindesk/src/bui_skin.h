/* bui_skin: braindesk skins.
 * A skin is a full palette (surfaces, text, accents, panel),
 * a wallpaper gradient and a corner radius, so a skin changes
 * the character of the whole desktop, not just one colour.
 * The selection persists in ~/.config/braindesk/skin and is
 * applied by every braindesk process (shell, greeter, apps)
 * at startup; the shell also applies live from Settings.
 *
 * The OS-flavoured skins are original palettes inspired by
 * each era's look — no artwork or assets from those systems.
 */
#ifndef BUI_SKIN_H
#define BUI_SKIN_H

#include "bui.h"

typedef struct {
	const char *name;
	bu_color bg, surface, surface2, surface3, line;
	bu_color text, text_dim, text_faint;
	bu_color accent, accent2;
	bu_color panel;
	bu_color wall_top, wall_bottom, wall_glow;
	int radius;
} bd_skin;

extern const bd_skin BD_SKINS[];
extern const int BD_NSKINS;
extern int bd_skin_current;

const bd_skin *bd_skin_get(void);
void bd_skin_load(void);		/* read saved selection */
void bd_skin_save(int idx);		/* persist selection */
void bd_skin_apply(bu_theme *t);	/* apply current skin to a theme */
void bd_skin_set(int idx);		/* select + apply helper */

#endif
