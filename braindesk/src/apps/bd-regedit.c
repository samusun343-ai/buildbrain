/* bd-regedit: graphical editor for the brainreg settings store
 * (docs/brainreg.md). Sections on the left, keys on the right.
 * Boolean values are toggles; other values are edited with the
 * on-screen keyboard. Writes always land in the user layer
 * (~/.config/brainreg.d/<section>.conf), shadowing the system
 * defaults; deleting a user value reveals the default again. */
#include "bdapp.h"
#include "../brainreg.h"
#include "../bui_osk.h"
#include <ctype.h>

#define MAXE 128
#define ROWH 38
#define VISIBLE 8

enum edit_mode { EDIT_NONE, EDIT_VALUE, EDIT_NEW_KEY, EDIT_NEW_VALUE };

struct regapp {
	brainreg_entry entries[MAXE];
	int n;
	char sections[24][64];
	int nsec, sel_sec;
	int sel_key; /* index into entries, -1 = none */
	int scroll;
	bool osk_open;
	bu_osk osk;
	enum edit_mode mode;
	char edit_buf[256];
	char new_key[64];
	bool confirm_delete;
	char status[160];
};

static void reload(struct regapp *ra)
{
	ra->n = brainreg_list(ra->entries, MAXE);
	ra->nsec = 0;
	for (int i = 0; i < ra->n; i++) {
		bool seen = false;
		for (int j = 0; j < ra->nsec; j++)
			if (!strcmp(ra->sections[j], ra->entries[i].section))
				seen = true;
		if (!seen && ra->nsec < 24)
			snprintf(ra->sections[ra->nsec++], 64, "%s",
				 ra->entries[i].section);
	}
	if (ra->sel_sec >= ra->nsec)
		ra->sel_sec = 0;
	ra->sel_key = -1;
	ra->scroll = 0;
}

static int key_count(struct regapp *ra)
{
	int c = 0;
	for (int i = 0; i < ra->n; i++)
		if (!strcmp(ra->entries[i].section,
			    ra->sections[ra->sel_sec]))
			c++;
	return c;
}

static int key_at(struct regapp *ra, int nth)
{
	for (int i = 0; i < ra->n; i++)
		if (!strcmp(ra->entries[i].section,
			    ra->sections[ra->sel_sec]) &&
		    nth-- == 0)
			return i;
	return -1;
}

static bool is_bool(const char *v)
{
	return !strcmp(v, "true") || !strcmp(v, "false");
}

static void set_status(struct regapp *ra, const char *msg)
{
	snprintf(ra->status, sizeof(ra->status), "%s", msg);
}

static void draw(bdapp *a, void *ctx)
{
	struct regapp *ra = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);

	/* sections (left) */
	for (int i = 0; i < ra->nsec && i < 11; i++) {
		bu_rect r = { 12, 56 + i * 36, 196, 33 };
		bu_fill_round_rect(c, r, 6,
				   i == ra->sel_sec ? t->surface3 : t->surface);
		bu_draw_text(c, t->font, r.x + 12, r.y + 7, ra->sections[i],
			     14, t->text);
	}
	bu_draw_text(c, t->font, 16, a->h - 78,
		     "設定ストア (brainreg)", 11.5, t->text_faint);
	bu_draw_text(c, t->font, 16, a->h - 60,
		     "ユーザ層に保存されます", 11.5, t->text_faint);

	/* keys (right) */
	int kc = key_count(ra);
	for (int vi = 0; vi < VISIBLE; vi++) {
		int nth = ra->scroll + vi;
		if (nth >= kc)
			break;
		int i = key_at(ra, nth);
		brainreg_entry *e = &ra->entries[i];
		bu_rect r = { 220, 56 + vi * ROWH, 560, ROWH - 3 };
		bu_fill_round_rect(c, r, 6,
				   i == ra->sel_key ? t->surface3 : t->surface);
		bu_draw_text(c, t->font, r.x + 12, r.y + 9, e->key, 14,
			     t->text);
		if (is_bool(e->value)) {
			bu_toggle(c, t, r.x + r.w - 58, r.y + 5,
				  !strcmp(e->value, "true"));
		} else {
			char val[300];
			snprintf(val, sizeof(val), "%s%s", e->value,
				 e->source == BRAINREG_SRC_SYSTEM
					 ? "  (既定)"
					 : "");
			int tw = bu_text_width(t->font, val, 13.5);
			bu_draw_text(c, t->font, r.x + r.w - 12 - tw, r.y + 9,
				     val, 13.5,
				     e->source == BRAINREG_SRC_SYSTEM
					     ? t->text_dim
					     : t->text);
		}
	}
	if (kc > VISIBLE) {
		bu_button(c, t, (bu_rect){ 792, 56, 50, 40 }, "▲",
			  BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 792, 104, 50, 40 }, "▼",
			  BU_BTN_NORMAL, false);
	}

	/* edit field while the OSK is open */
	if (ra->osk_open) {
		bu_rect fb = { 220, 56, 560, 36 };
		bu_fill_round_rect(c, fb, 8, t->surface3);
		bu_draw_text(c, t->font, fb.x + 12, fb.y + 8,
			     ra->edit_buf[0] ? ra->edit_buf : "入力してください",
			     13.5, ra->edit_buf[0] ? t->text : t->text_faint);
		bu_osk_draw(c, t, &ra->osk,
			    (bu_rect){ 0, a->h - bu_osk_height(), a->w,
				       bu_osk_height() });
		return;
	}

	/* bottom actions */
	bu_button(c, t, (bu_rect){ 220, a->h - 52, 150, 40 }, "＋ キー追加",
		  BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 382, a->h - 52, 150, 40 },
		  ra->confirm_delete ? "本当に削除" : "削除", BU_BTN_NORMAL,
		  ra->confirm_delete);
	if (ra->status[0])
		bu_draw_text(c, t->font, 548, a->h - 40, ra->status, 12.5,
			     t->text_dim);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void open_osk(struct regapp *ra, enum edit_mode mode,
		     const char *initial)
{
	ra->mode = mode;
	snprintf(ra->edit_buf, sizeof(ra->edit_buf), "%s",
		 initial ? initial : "");
	ra->osk_open = true;
}

static void apply_edit(struct regapp *ra)
{
	if (ra->mode == EDIT_VALUE && ra->sel_key >= 0) {
		brainreg_entry *e = &ra->entries[ra->sel_key];
		if (brainreg_set(e->section, e->key, ra->edit_buf) == 0)
			set_status(ra, "保存しました");
		else
			set_status(ra, "保存に失敗しました");
		reload(ra);
	} else if (ra->mode == EDIT_NEW_KEY) {
		if (!ra->edit_buf[0]) {
			ra->osk_open = false;
			return;
		}
		snprintf(ra->new_key, sizeof(ra->new_key), "%s", ra->edit_buf);
		open_osk(ra, EDIT_NEW_VALUE, "");
		return;
	} else if (ra->mode == EDIT_NEW_VALUE) {
		if (brainreg_set(ra->sections[ra->sel_sec], ra->new_key,
				 ra->edit_buf) == 0)
			set_status(ra, "キーを追加しました");
		else
			set_status(ra, "追加に失敗しました");
		reload(ra);
	}
	ra->osk_open = false;
	ra->mode = EDIT_NONE;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed,
		    bool *quit)
{
	struct regapp *ra = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	ra->confirm_delete = false;

	if (ra->osk_open) {
		bu_rect area = { 0, a->h - bu_osk_height(), a->w,
				 bu_osk_height() };
		if (y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&ra->osk, area, x, y, out,
					   sizeof(out));
			if (r == OSK_TEXT && out[0]) {
				strncat(ra->edit_buf, out,
					sizeof(ra->edit_buf) -
						strlen(ra->edit_buf) - 1);
			} else if (r == OSK_BACKSPACE) {
				int l = (int)strlen(ra->edit_buf);
				if (l > 0)
					ra->edit_buf[l - 1] = 0;
			} else if (r == OSK_ENTER) {
				apply_edit(ra);
			}
			return;
		}
		if (y < area.y - 8) { /* tap outside: cancel */
			ra->osk_open = false;
			ra->mode = EDIT_NONE;
		}
		return;
	}

	/* sections */
	for (int i = 0; i < ra->nsec && i < 11; i++) {
		if (inr(x, y, (bu_rect){ 12, 56 + i * 36, 196, 33 })) {
			ra->sel_sec = i;
			ra->sel_key = -1;
			ra->scroll = 0;
			return;
		}
	}
	/* scroll buttons */
	if (key_count(ra) > VISIBLE) {
		if (inr(x, y, (bu_rect){ 792, 56, 50, 40 })) {
			if (ra->scroll > 0)
				ra->scroll--;
			return;
		}
		if (inr(x, y, (bu_rect){ 792, 104, 50, 40 })) {
			if (ra->scroll + VISIBLE < key_count(ra))
				ra->scroll++;
			return;
		}
	}
	/* key rows */
	for (int vi = 0; vi < VISIBLE; vi++) {
		int nth = ra->scroll + vi;
		if (nth >= key_count(ra))
			break;
		bu_rect r = { 220, 56 + vi * ROWH, 560, ROWH - 3 };
		if (!inr(x, y, r))
			continue;
		int i = key_at(ra, nth);
		brainreg_entry *e = &ra->entries[i];
		ra->sel_key = i;
		if (is_bool(e->value)) {
			const char *nv =
				!strcmp(e->value, "true") ? "false" : "true";
			if (brainreg_set(e->section, e->key, nv) == 0)
				set_status(ra, "保存しました");
			reload(ra);
			ra->sel_key = i < ra->n ? i : -1;
		} else {
			open_osk(ra, EDIT_VALUE, e->value);
		}
		return;
	}
	/* bottom actions */
	if (inr(x, y, (bu_rect){ 220, a->h - 52, 150, 40 })) {
		open_osk(ra, EDIT_NEW_KEY, "");
		return;
	}
	if (inr(x, y, (bu_rect){ 382, a->h - 52, 150, 40 })) {
		if (ra->sel_key < 0) {
			set_status(ra, "先にキーを選んでください");
			return;
		}
		brainreg_entry *e = &ra->entries[ra->sel_key];
		if (e->source == BRAINREG_SRC_SYSTEM) {
			set_status(ra, "既定値のみのキーは削除できません");
			return;
		}
		/* first tap arms, second deletes - handled by flag set
		 * before this handler cleared it, so use status text */
		if (!strcmp(ra->status, "もう一度押すと削除します")) {
			if (brainreg_unset(e->section, e->key) == 0)
				set_status(ra, "削除しました");
			reload(ra);
		} else {
			set_status(ra, "もう一度押すと削除します");
			ra->confirm_delete = true;
		}
		return;
	}
}

int main(int argc, char **argv)
{
	struct regapp ra;
	memset(&ra, 0, sizeof(ra));
	ra.sel_key = -1;
	bu_osk_init(&ra.osk);
	reload(&ra);
	const char *shot =
		argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-regedit.ppm"
							   : NULL;
	return bdapp_run("設定エディタ (brainreg)", shot, draw, onevent,
			 NULL, &ra);
}
