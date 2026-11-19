/* bui_skin: braindesk skins.
 * A skin is an accent pair, a wallpaper gradient and an
 * optional light palette. The selection persists in
 * ~/.config/braindesk/skin and is applied by every braindesk
 * process (shell, greeter, apps) at startup; the shell also
 * applies live when the user picks a skin in Settings.
 */
#ifndef BUI_SKIN_H
#define BUI_SKIN_H

#include "bui.h"

typedef struct {
	const char *name;
	bu_color accent, accent2;
	bu_color wall_top, wall_bottom, wall_glow;
	bool light;
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
