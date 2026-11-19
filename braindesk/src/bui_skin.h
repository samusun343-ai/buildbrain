/* bui_skin: braindesk colour themes.
 * A theme is a full palette (surfaces, text, accents, panel)
 * plus a wallpaper gradient, so a theme changes the colour
 * character of the whole desktop. The DESKTOP LAYOUT is a
 * single braindesk design and does not change with the theme
 * (the OS-reproduction skins were retired in 2026-11; see
 * docs/design-system.md in buildbrain).
 * The selection persists in brainreg (desktop.skin) and is
 * applied by every braindesk process (shell, greeter, apps)
 * at startup; the shell also applies live from Settings.
 *
 * The layout/chrome fields stay in the struct because the WM
 * and the app frame still read them; every theme pins them to
 * the braindesk values (layout BD, chrome MODERN), so those
 * readers always take their braindesk branch. The BD_LAYOUT_*
 * constants likewise stay defined for those readers.
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
	int panel_h;		/* taskbar height (48 in the design system) */
	int chrome;		/* BU_CHROME_* (always MODERN now) */
	int layout;		/* BD_LAYOUT_* (always BD_LAYOUT_BD now) */
} bd_skin;

/* Layout families. Only BD_LAYOUT_BD is used by the shell;
 * the other values remain because bui_wm.c and apps/bdapp.h
 * still name them (they read bd_skin.layout, which is always
 * BD_LAYOUT_BD, so their family branches are unreachable). */
enum {
	BD_LAYOUT_BD = 0,	/* the braindesk design (only layout) */
	BD_LAYOUT_MAC = 1,	/* retired */
	BD_LAYOUT_CLASSIC = 2,	/* retired */
	BD_LAYOUT_WIN11 = 3,	/* retired */
	BD_LAYOUT_WIN = 4,	/* retired */
};

/* The theme table (braindesk originals only). */
extern const bd_skin *BD_SKINS;
extern int BD_NSKINS;
extern const int BD_NBUILTIN;
extern int bd_skin_current;

int bd_skin_count(void);		/* total themes */
const bd_skin *bd_skin_at(int idx);	/* NULL when out of range */
void bd_skin_load_files(void);		/* retired .skin loader: no-op */
const bd_skin *bd_skin_get(void);
void bd_skin_load(void);		/* read saved selection */
void bd_skin_save(int idx);		/* persist selection */
void bd_skin_apply(bu_theme *t);	/* apply current theme to a theme */
void bd_skin_set(int idx);		/* select + apply helper */

#endif
