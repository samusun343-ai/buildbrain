/* bd-devices: device manager for braindesk (v2).
 * Tabs: 概要 (model/kernel/CPU/memory/sound/input/display),
 * ドライバ (loaded modules), ストレージ (mounts with usage
 * bars), 電源 (battery sysfs detail). Everything is read
 * live from /proc and /sys; a refresh button re-reads.
 */
#include "bdapp.h"
#include <sys/statvfs.h>
#include <dirent.h>

#define MAXROWS 40
#define MAXMODS 128
#define MAXMOUNTS 12

struct kv {
	char key[48];
	char value[160];
};

struct mount {
	char dev[64];
	char path[128];
	long total_mb, used_mb;
};

struct devices {
	int tab;		/* 0 overview, 1 drivers, 2 storage, 3 power */
	struct kv rows[MAXROWS];
	int nrows;
	char mods[MAXMODS][96];
	long mod_size[MAXMODS];
	int mod_use[MAXMODS];
	int nmods, mod_scroll;
	struct mount mounts[MAXMOUNTS];
	int nmounts;
	struct kv power[MAXROWS];
	int npower;
};

static void read_file(const char *path, char *out, size_t n)
{
	FILE *fp = fopen(path, "r");
	out[0] = 0;
	if (!fp)
		return;
	if (!fgets(out, (int)n, fp))
		out[0] = 0;
	fclose(fp);
	int l = (int)strlen(out);
	while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r' || out[l - 1] == ' '))
		out[--l] = 0;
}

static void add_kv(struct kv *rows, int *n, const char *key, const char *value)
{
	if (*n >= MAXROWS || !value[0])
		return;
	snprintf(rows[*n].key, 48, "%s", key);
	snprintf(rows[*n].value, 160, "%s", value);
	(*n)++;
}

static void load_overview(struct devices *dv)
{
	dv->nrows = 0;
	char buf[512];
	read_file("/sys/firmware/devicetree/base/model", buf, sizeof(buf));
	if (!buf[0])
		read_file("/proc/device-tree/model", buf, sizeof(buf));
	add_kv(dv->rows, &dv->nrows, "機種", buf[0] ? buf : "不明");
	/* kernel via /proc/version */
	FILE *fp = fopen("/proc/version", "r");
	buf[0] = 0;
	if (fp) {
		if (!fgets(buf, sizeof(buf), fp))
			buf[0] = 0;
		fclose(fp);
		char *paren = strchr(buf, '(');
		if (paren)
			*paren = 0;
	}
	add_kv(dv->rows, &dv->nrows, "カーネル", buf);
	/* CPU */
	fp = fopen("/proc/cpuinfo", "r");
	char model[160] = "", hw[160] = "";
	int cores = 0;
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "model name", 10) == 0) {
				char *colon = strchr(line, ':');
				if (colon)
					snprintf(model, sizeof(model), "%s", colon + 2);
			}
			if (strncmp(line, "Hardware", 8) == 0) {
				char *colon = strchr(line, ':');
				if (colon)
					snprintf(hw, sizeof(hw), "%s", colon + 2);
			}
			if (strncmp(line, "processor", 9) == 0)
				cores++;
		}
		fclose(fp);
	}
	{
		int l = (int)strlen(model);
		while (l > 0 && model[l - 1] == '\n')
			model[--l] = 0;
		l = (int)strlen(hw);
		while (l > 0 && hw[l - 1] == '\n')
			hw[--l] = 0;
		char cpu[200];
		snprintf(cpu, sizeof(cpu), "%s%s (%d コア)", hw[0] ? hw : model, "", cores > 0 ? cores : 1);
		add_kv(dv->rows, &dv->nrows, "CPU", cpu);
	}
	/* memory */
	fp = fopen("/proc/meminfo", "r");
	long mt = 0, ma = 0;
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "MemTotal:", 9) == 0)
				mt = strtol(line + 9, NULL, 10);
			if (strncmp(line, "MemAvailable:", 13) == 0)
				ma = strtol(line + 13, NULL, 10);
		}
		fclose(fp);
	}
	{
		char mem[96];
		snprintf(mem, sizeof(mem), "%ld MB (空き %ld MB)", mt / 1024, ma / 1024);
		add_kv(dv->rows, &dv->nrows, "メモリ", mem);
	}
	/* sound card */
	read_file("/proc/asound/card0/id", buf, sizeof(buf));
	add_kv(dv->rows, &dv->nrows, "サウンド", buf);
	/* display size from fb0 */
	{
		char sz[64] = "";
		read_file("/sys/class/graphics/fb0/virtual_size", sz, sizeof(sz));
		if (sz[0]) {
			char disp[96];
			snprintf(disp, sizeof(disp), "%s", sz);
			for (char *q = disp; *q; q++)
				if (*q == ',')
					*q = 'x';
			add_kv(dv->rows, &dv->nrows, "ディスプレイ", disp);
		}
	}
	/* input devices */
	fp = fopen("/proc/bus/input/devices", "r");
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "N: Name=", 8) == 0) {
				char nm[160];
				snprintf(nm, sizeof(nm), "%s", line + 9);
				int l = (int)strlen(nm);
				while (l > 0 && (nm[l - 1] == '\n' || nm[l - 1] == '"'))
					nm[--l] = 0;
				add_kv(dv->rows, &dv->nrows, "入力", nm);
			}
		}
		fclose(fp);
	}
}

static void load_drivers(struct devices *dv)
{
	FILE *fp = fopen("/proc/modules", "r");
	dv->nmods = 0;
	if (!fp)
		return;
	char line[512];
	while (fgets(line, sizeof(line), fp) && dv->nmods < MAXMODS) {
		char name[96];
		long size;
		int use;
		if (sscanf(line, "%95s %ld %d", name, &size, &use) == 3) {
			snprintf(dv->mods[dv->nmods], 96, "%s", name);
			dv->mod_size[dv->nmods] = size;
			dv->mod_use[dv->nmods] = use;
			dv->nmods++;
		}
	}
	fclose(fp);
}

static void load_storage(struct devices *dv)
{
	FILE *fp = fopen("/proc/mounts", "r");
	dv->nmounts = 0;
	if (!fp)
		return;
	char line[1024];
	while (fgets(line, sizeof(line), fp) && dv->nmounts < MAXMOUNTS) {
		char dev[256], path[256], fstype[64];
		if (sscanf(line, "%255s %255s %63s", dev, path, fstype) != 3)
			continue;
		if (strcmp(fstype, "ext4") != 0 && strcmp(fstype, "vfat") != 0 &&
		    strcmp(fstype, "exfat") != 0 && strcmp(fstype, "f2fs") != 0)
			continue;
		struct statvfs sv;
		if (statvfs(path, &sv) != 0 || sv.f_blocks == 0)
			continue;
		struct mount *m = &dv->mounts[dv->nmounts];
		snprintf(m->dev, sizeof(m->dev), "%s", dev);
		snprintf(m->path, sizeof(m->path), "%s", path);
		m->total_mb = (long)((double)sv.f_blocks * sv.f_frsize / 1048576.0);
		m->used_mb = m->total_mb - (long)((double)sv.f_bavail * sv.f_frsize / 1048576.0);
		dv->nmounts++;
	}
	fclose(fp);
}

static void load_power(struct devices *dv)
{
	dv->npower = 0;
	/* find the first battery under /sys/class/power_supply */
	const char *base = "/sys/class/power_supply";
	DIR *d = opendir(base);
	char batdir[256] = "";
	if (d) {
		struct dirent *de;
		while ((de = readdir(d))) {
			char tp[320];
			snprintf(tp, sizeof(tp), "%s/%s/type", base, de->d_name);
			char tv[32];
			read_file(tp, tv, sizeof(tv));
			if (strcmp(tv, "Battery") == 0) {
				snprintf(batdir, sizeof(batdir), "%s/%s", base, de->d_name);
				break;
			}
		}
		closedir(d);
	}
	if (!batdir[0]) {
		add_kv(dv->power, &dv->npower, "バッテリー", "見つかりません");
		return;
	}
	char p[384], v[160];
	snprintf(p, sizeof(p), "%s/status", batdir);
	read_file(p, v, sizeof(v));
	add_kv(dv->power, &dv->npower, "状態", v);
	snprintf(p, sizeof(p), "%s/capacity", batdir);
	read_file(p, v, sizeof(v));
	if (v[0]) {
		char cap[192];
		snprintf(cap, sizeof(cap), "%s %%", v);
		add_kv(dv->power, &dv->npower, "残量", cap);
	}
	snprintf(p, sizeof(p), "%s/voltage_now", batdir);
	read_file(p, v, sizeof(v));
	if (v[0]) {
		char vv[192];
		snprintf(vv, sizeof(vv), "%.2f V", strtol(v, NULL, 10) / 1000000.0);
		add_kv(dv->power, &dv->npower, "電圧", vv);
	}
	snprintf(p, sizeof(p), "%s/charge_full", batdir);
	read_file(p, v, sizeof(v));
	if (v[0]) {
		char cf[192];
		snprintf(cf, sizeof(cf), "%ld mAh", strtol(v, NULL, 10) / 1000);
		add_kv(dv->power, &dv->npower, "満充電容量", cf);
	}
	snprintf(p, sizeof(p), "%s/charge_now", batdir);
	read_file(p, v, sizeof(v));
	if (v[0]) {
		char cn[192];
		snprintf(cn, sizeof(cn), "%ld mAh", strtol(v, NULL, 10) / 1000);
		add_kv(dv->power, &dv->npower, "現在の容量", cn);
	}
	snprintf(p, sizeof(p), "%s/technology", batdir);
	read_file(p, v, sizeof(v));
	add_kv(dv->power, &dv->npower, "種類", v);
}

static void load_all(struct devices *dv)
{
	load_overview(dv);
	load_drivers(dv);
	load_storage(dv);
	load_power(dv);
}

static const char *TABS[] = { "概要", "ドライバ", "ストレージ", "電源" };

static void draw_kv_list(bdapp *a, struct kv *rows, int n, int y0)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	for (int i = 0; i < n && y0 + i * 34 < 470; i++) {
		bu_rect r = { 16, y0 + i * 34, 832, 30 };
		if (i % 2 == 0)
			bu_fill_round_rect(c, r, 6, t->surface);
		bu_draw_text(c, t->font, r.x + 14, r.y + 5, rows[i].key, 13, t->text_dim);
		bu_draw_text(c, t->font, r.x + 170, r.y + 5, rows[i].value, 13, t->text);
	}
	if (n == 0)
		bu_draw_text(c, t->font, 16, y0 + 8, "情報がありません", 13, t->text_dim);
}

static void draw(bdapp *a, void *ctx)
{
	struct devices *dv = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	for (int i = 0; i < 4; i++) {
		bu_rect r = { 16 + i * 172, 52, 164, 38 };
		bu_fill_round_rect(c, r, 9, i == dv->tab ? t->accent : t->surface);
		bu_draw_text_centered(c, t->font, r, TABS[i], 13.5, i == dv->tab ? BU_RGB(255, 255, 255) : t->text_dim);
	}
	bu_button(c, t, (bu_rect){ 720, 52, 128, 38 }, "更新", BU_BTN_NORMAL, false);
	if (dv->tab == 0) {
		draw_kv_list(a, dv->rows, dv->nrows, 104);
	} else if (dv->tab == 1) {
		char hdr[96];
		snprintf(hdr, sizeof(hdr), "読み込み済みモジュール: %d", dv->nmods);
		bu_draw_text(c, t->font, 16, 104, hdr, 13, t->text_dim);
		int visible = 9;
		for (int vi = 0; vi < visible; vi++) {
			int i = dv->mod_scroll + vi;
			if (i >= dv->nmods)
				break;
			bu_rect r = { 16, 132 + vi * 34, 784, 30 };
			if (vi % 2 == 0)
				bu_fill_round_rect(c, r, 6, t->surface);
			bu_draw_text(c, t->font, r.x + 14, r.y + 5, dv->mods[i], 13, t->text);
			char meta[64];
			snprintf(meta, sizeof(meta), "%ld KB · 使用 %d", dv->mod_size[i] / 1024, dv->mod_use[i]);
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 5, r.w - 14, 20 }, meta, 12, t->text_faint);
		}
		bu_button(c, t, (bu_rect){ 808, 132, 44, 52 }, "▲", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 808, 402, 44, 52 }, "▼", BU_BTN_NORMAL, false);
	} else if (dv->tab == 2) {
		for (int i = 0; i < dv->nmounts; i++) {
			struct mount *m = &dv->mounts[i];
			bu_rect r = { 16, 108 + i * 64, 832, 56 };
			bu_fill_round_rect(c, r, 9, t->surface);
			char head[200];
			snprintf(head, sizeof(head), "%s  (%s)", m->path, m->dev);
			bu_draw_text(c, t->font, r.x + 14, r.y + 6, head, 13, t->text);
			bu_rect bar = { r.x + 14, r.y + 32, r.w - 200, 12 };
			bu_fill_round_rect(c, bar, 6, t->surface3);
			if (m->total_mb > 0) {
				float frac = (float)m->used_mb / (float)m->total_mb;
				bu_fill_round_rect(c, (bu_rect){ bar.x, bar.y, (int)(bar.w * frac), bar.h }, 6,
						   frac > 0.9f ? t->danger : t->accent);
			}
			char sz[64];
			snprintf(sz, sizeof(sz), "%ld / %ld MB", m->used_mb, m->total_mb);
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 28, r.w - 14, 20 }, sz, 12.5, t->text_dim);
		}
		if (dv->nmounts == 0)
			bu_draw_text(c, t->font, 16, 116, "マウントが見つかりません", 13, t->text_dim);
	} else {
		draw_kv_list(a, dv->power, dv->npower, 104);
	}
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct devices *dv = ctx;
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	for (int i = 0; i < 4; i++)
		if (inr(x, y, (bu_rect){ 16 + i * 172, 52, 164, 38 })) {
			dv->tab = i;
			return;
		}
	if (inr(x, y, (bu_rect){ 720, 52, 128, 38 })) {
		load_all(dv);
		return;
	}
	if (dv->tab == 1) {
		if (inr(x, y, (bu_rect){ 808, 132, 44, 52 })) {
			if (dv->mod_scroll >= 5) dv->mod_scroll -= 5; else dv->mod_scroll = 0;
		} else if (inr(x, y, (bu_rect){ 808, 402, 44, 52 })) {
			if (dv->mod_scroll + 9 < dv->nmods) dv->mod_scroll += 5;
		}
	}
}

int main(int argc, char **argv)
{
	struct devices dv;
	memset(&dv, 0, sizeof(dv));
	load_all(&dv);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-devices.ppm" : NULL;
	return bdapp_run("デバイス", shot, draw, onevent, NULL, &dv);
}
