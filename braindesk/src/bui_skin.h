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
 *
 * Users can add their own skins as key=value text files
 * (*.skin, see docs/skins.md in buildbrain): bd_skin_load_files()
 * appends them after the built-ins, so indices of built-in
 * skins never change. Search order (earlier wins on duplicate
 * file names): $BRAINDESK_SKINS_DIR, then
 * ~/.config/braindesk/skins, then /usr/share/braindesk/skins.
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
	int panel_h;		/* default bar height for this family (0 = shell default) */
	int chrome;		/* BU_CHROME_* */
	int layout;		/* BD_LAYOUT_* : bar/dock/window arrangement */
} bd_skin;

/* Layout families: the arrangement of the bar, start control,
 * tray and window controls follows the system each skin
 * evokes, not just its colours. */
enum {
	BD_LAYOUT_BD = 0,	/* bottom panel, start left (braindesk) */
	BD_LAYOUT_MAC = 1,	/* top menu bar + floating dock */
	BD_LAYOUT_CLASSIC = 2,	/* top menu bar only */
	BD_LAYOUT_WIN11 = 3,	/* bottom taskbar, centred icons */
	BD_LAYOUT_WIN = 4,	/* bottom taskbar, start left */
};

/* The skin table. BD_SKINS points at the built-in table until
 * file-defined skins are loaded, then at a combined table with
 * the file skins appended; BD_NSKINS is the total count. Prefer
 * bd_skin_count()/bd_skin_at() in new code. */
extern const bd_skin *BD_SKINS;
extern int BD_NSKINS;
extern const int BD_NBUILTIN;
extern int bd_skin_current;

int bd_skin_count(void);		/* total skins (loads files) */
const bd_skin *bd_skin_at(int idx);	/* NULL when out of range */
void bd_skin_load_files(void);		/* scan *.skin files (idempotent) */
const bd_skin *bd_skin_get(void);
void bd_skin_load(void);		/* read saved selection */
void bd_skin_save(int idx);		/* persist selection */
void bd_skin_apply(bu_theme *t);	/* apply current skin to a theme */
void bd_skin_set(int idx);		/* select + apply helper */

#endif
