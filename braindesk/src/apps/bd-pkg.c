/* bd-pkg: package manager for braindesk.
 * Lists installed packages (dpkg), filters them with the
 * on-screen keyboard, shows details, and installs/removes via
 * apt when administrator rights are available (pkexec or
 * passwordless sudo). Without rights it stays a fully working
 * read-only browser and says so.
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <ctype.h>

#define MAXPKG 1024
#define ROWH 38
#define VISIBLE 8

struct pkg {
	char name[96];
	char version[64];
};

struct pkgapp {
	struct pkg pkgs[MAXPKG];
	int n, scroll, sel;
	char filter[64];
	bool osk_open;
	bu_osk osk;
	bool can_admin;
	bool confirm_remove;
	char detail[512];
	char status[160];
};

static void load(struct pkgapp *pa)
{
	pa->n = 0;
	pa->sel = -1;
	FILE *pp = popen("dpkg-query -W -f='${Package}\\t${Version}\\n' 2>/dev/null", "r");
	if (!pp)
		return;
	char line[256];
	while (fgets(line, sizeof(line), pp) && pa->n < MAXPKG) {
		char *tab = strchr(line, '\t');
		if (!tab)
			continue;
		*tab = 0;
		line[strcspn(line, "\n")] = 0;
		snprintf(pa->pkgs[pa->n].name, 96, "%s", line);
		snprintf(pa->pkgs[pa->n].version, 64, "%s", tab + 1);
		pa->pkgs[pa->n].version[strcspn(pa->pkgs[pa->n].version, "\n")] = 0;
		pa->n++;
	}
	pclose(pp);
	/* admin check: pkexec present or passwordless sudo */
	pa->can_admin = system("command -v pkexec >/dev/null 2>&1 || sudo -n true 2>/dev/null") == 0;
}

static bool matches(struct pkgapp *pa, int i)
{
	if (!pa->filter[0])
		return true;
	const char *hay = pa->pkgs[i].name;
	const char *needle = pa->filter;
	for (const char *h = hay; *h; h++) {
		const char *a = h, *b = needle;
		while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
			a++;
			b++;
		}
		if (!*b)
			return true;
	}
	return false;
}

static int visible_index(struct pkgapp *pa, int vi)
{
	int seen = -1;
	for (int i = 0; i < pa->n; i++) {
		if (!matches(pa, i))
			continue;
		seen++;
		if (seen == pa->scroll + vi)
			return i;
	}
	return -1;
}

static int visible_count(struct pkgapp *pa)
{
	int cnt = 0;
	for (int i = 0; i < pa->n; i++)
		if (matches(pa, i))
			cnt++;
	return cnt;
}

static void load_detail(struct pkgapp *pa)
{
	pa->detail[0] = 0;
	if (pa->sel < 0)
		return;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "dpkg-query -s '%s' 2>/dev/null | grep -E '^(Description|Installed-Size|Version):' | head -3", pa->pkgs[pa->sel].name);
	FILE *pp = popen(cmd, "r");
	if (!pp)
		return;
	size_t off = 0;
	char line[256];
	while (fgets(line, sizeof(line), pp) && off < sizeof(pa->detail) - 200) {
		line[strcspn(line, "\n")] = 0;
		off += snprintf(pa->detail + off, sizeof(pa->detail) - off, "%s  ", line);
	}
	pclose(pp);
}

static void run_apt(struct pkgapp *pa, const char *verb, const char *name)
{
	char cmd[512];
	const char *prefix = "pkexec";
	if (system("command -v pkexec >/dev/null 2>&1") != 0)
		prefix = "sudo -n";
	snprintf(cmd, sizeof(cmd), "%s apt-get %s -y '%s' >/tmp/bd-pkg.log 2>&1",
		 prefix, verb, name);
	snprintf(pa->status, sizeof(pa->status), "%s中... しばらくお待ちください", strcmp(verb, "remove") == 0 ? "削除" : "インストール");
	int rc = system(cmd);
	if (rc == 0) {
		snprintf(pa->status, sizeof(pa->status), "完了しました: %s %s", verb, name);
		load(pa);
	} else {
		snprintf(pa->status, sizeof(pa->status), "失敗しました (詳細: /tmp/bd-pkg.log)");
	}
}

static void draw(bdapp *a, void *ctx)
{
	struct pkgapp *pa = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	char hdr[128];
	snprintf(hdr, sizeof(hdr), "インストール済み %d 件", visible_count(pa));
	bu_draw_text(c, t->font, 16, 54, hdr, 13.5, t->text_dim);
	/* filter box */
	bu_rect fb = { 380, 50, 330, 36 };
	bu_fill_round_rect(c, fb, 8, pa->osk_open ? t->surface3 : t->surface);
	bu_draw_text(c, t->font, fb.x + 12, fb.y + 8, pa->filter[0] ? pa->filter : "絞り込み (タップで入力)", 13.5,
		     pa->filter[0] ? t->text : t->text_faint);
	bu_button(c, t, (bu_rect){ 722, 50, 130, 36 }, "インストール", BU_BTN_NORMAL, false);
	int list_bottom = pa->osk_open ? a->h - bu_osk_height() - 92 : 480 - 96;
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = visible_index(pa, vi);
		if (i < 0)
			break;
		bu_rect r = { 12, 94 + vi * ROWH, 700, ROWH - 3 };
		if (r.y + r.h > list_bottom + 40)
			break;
		bu_fill_round_rect(c, r, 6, i == pa->sel ? t->surface3 : t->surface);
		bu_icon_box(c, r.x + 20, r.y + r.h / 2, t->text_dim);
		bu_draw_text(c, t->font, r.x + 40, r.y + 6, pa->pkgs[i].name, 13.5, t->text);
		bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 6, r.w - 12, 20 }, pa->pkgs[i].version, 12, t->text_dim);
	}
	bu_button(c, t, (bu_rect){ 724, 94, 52, 56 }, "▲", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 724, 300, 52, 56 }, "▼", BU_BTN_NORMAL, false);
	/* detail bar */
	if (!pa->osk_open) {
		bu_fill_round_rect(c, (bu_rect){ 12, 480 - 88, 840, 76 }, 10, t->surface);
		if (pa->sel >= 0) {
			bu_draw_text(c, t->font, 28, 480 - 80, pa->pkgs[pa->sel].name, 14.5, t->text);
			bu_draw_text(c, t->font, 28, 480 - 56, pa->detail, 12, t->text_dim);
			if (pa->can_admin)
				bu_button(c, t, (bu_rect){ 700, 480 - 76, 140, 36 },
					  pa->confirm_remove ? "本当に削除?" : "アンインストール", BU_BTN_DANGER, false);
			else
				bu_draw_text_right(c, t->font, (bu_rect){ 12, 480 - 74, 820, 20 }, "削除には管理者権限が必要です", 11.5, t->text_faint);
		} else if (pa->status[0]) {
			bu_draw_text(c, t->font, 28, 480 - 66, pa->status, 13, t->text_dim);
		}
	} else {
		bu_osk_draw(c, t, &pa->osk, (bu_rect){ 0, a->h - bu_osk_height(), a->w, bu_osk_height() });
		bu_button(c, t, (bu_rect){ 722, 50, 130, 36 }, "閉じる", BU_BTN_PRIMARY, false);
	}
	if (pa->status[0] && pa->sel < 0 && !pa->osk_open)
		bu_draw_text(c, t->font, 28, 480 - 66, pa->status, 13, t->text_dim);
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct pkgapp *pa = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (pa->osk_open) {
		if (inr(x, y, (bu_rect){ 722, 50, 130, 36 })) {
			pa->osk_open = false;
			return;
		}
		bu_rect area = { 0, a->h - bu_osk_height(), a->w, bu_osk_height() };
		if (y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&pa->osk, area, x, y, out, sizeof(out));
			if (r == OSK_TEXT && out[0]) {
				strncat(pa->filter, out, sizeof(pa->filter) - strlen(pa->filter) - 1);
				pa->scroll = 0;
			} else if (r == OSK_BACKSPACE) {
				int l = (int)strlen(pa->filter);
				if (l > 0)
					pa->filter[l - 1] = 0;
				pa->scroll = 0;
			} else if (r == OSK_ENTER) {
				pa->osk_open = false;
			}
			return;
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 380, 50, 330, 36 })) {
		pa->osk_open = true;
		pa->osk.kana = false; /* package names are latin */
		return;
	}
	if (inr(x, y, (bu_rect){ 722, 50, 130, 36 })) {
		if (!pa->filter[0]) {
			snprintf(pa->status, sizeof(pa->status), "絞り込み欄にパッケージ名を入力してから押してください");
		} else if (!pa->can_admin) {
			snprintf(pa->status, sizeof(pa->status), "インストールには管理者権限が必要です");
		} else {
			char name[64];
			snprintf(name, sizeof(name), "%s", pa->filter);
			run_apt(pa, "install", name);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 724, 94, 52, 56 })) {
		if (pa->scroll > 0) pa->scroll -= 4;
		return;
	}
	if (inr(x, y, (bu_rect){ 724, 300, 52, 56 })) {
		if (pa->scroll + VISIBLE < visible_count(pa)) pa->scroll += 4;
		return;
	}
	if (pa->sel >= 0 && pa->can_admin && inr(x, y, (bu_rect){ 700, 404, 140, 36 })) {
		if (!pa->confirm_remove) {
			pa->confirm_remove = true;
		} else {
			char name[96];
			snprintf(name, sizeof(name), "%s", pa->pkgs[pa->sel].name);
			pa->confirm_remove = false;
			run_apt(pa, "remove", name);
		}
		return;
	}
	for (int vi = 0; vi < VISIBLE; vi++) {
		int i = visible_index(pa, vi);
		if (i < 0)
			break;
		bu_rect r = { 12, 94 + vi * ROWH, 700, ROWH - 3 };
		if (inr(x, y, r)) {
			pa->sel = i;
			pa->confirm_remove = false;
			load_detail(pa);
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct pkgapp pa;
	memset(&pa, 0, sizeof(pa));
	pa.sel = -1;
	bu_osk_init(&pa.osk);
	load(&pa);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-pkg.ppm" : NULL;
	return bdapp_run("パッケージ管理", shot, draw, onevent, NULL, &pa);
}
