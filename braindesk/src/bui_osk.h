/* bui_osk: on-screen keyboard for libbrainui.
 * Latin layout plus a kana mode with romaji-to-hiragana
 * conversion (Hepburn), so Japanese text can be entered from
 * the touch panel alone. The widget edits a caller-owned UTF-8
 * buffer through the feed/erase helpers.
 */
#ifndef BUI_OSK_H
#define BUI_OSK_H

#include "bui.h"

typedef struct {
	bool kana;		/* kana conversion mode */
	bool shift;
	char pending[8];	/* pending romaji in kana mode */
	int pending_len;
} bu_osk;

void bu_osk_init(bu_osk *o);
/* Height the keyboard occupies for a given width. */
int bu_osk_height(void);
void bu_osk_draw(bu_canvas *c, bu_theme *t, bu_osk *o, bu_rect area);

/* Result of a tap: text to append (utf8, may be "" when a romaji
 * syllable is still pending), or a control action. */
enum { OSK_NONE = 0, OSK_TEXT = 1, OSK_BACKSPACE = 2, OSK_ENTER = 3 };
int bu_osk_hit(bu_osk *o, bu_rect area, int x, int y, char *out_utf8, int out_size);

/* Flush pending romaji (e.g. before saving): returns text. */
const char *bu_osk_flush(bu_osk *o, char *out_utf8, int out_size);

#endif
