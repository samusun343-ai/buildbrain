/* bd-devices: device and driver manager for braindesk.
 * Device tab: model, kernel, CPU, memory, storage, battery,
 * sound, input devices and display, read live from /proc and
 * /sys. Driver tab: loaded kernel modules from /proc/modules.
 */
#include "bdapp.h"
#include <sys/utsname.h>
#include <sys/statvfs.h>

#define MAXMOD 160
#define ROWH 34
#define VISIBLE 11

struct devices {
	int tab;		/* 0 device, 1 drivers */
	char model[128];
	char kernel[128];
	char cpu[128];
	char mem[64];
	char storage[64];
	char battery[64];
	char sound[128];
	char display[64];
	char inputs[256];
	struct {
		char name[56];
		long size;
		int used;
	} mods[MAXMOD];
	int nmod, scroll;
};

static void read_first_line(const char *path, char *out, int n)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		snprintf(out, n, "—");
		return;
	}
	if (!fgets(out, n, fp))
		snprintf(out, n, "—");
	fclose(fp);
	out[strcspn(out, "\n")] = 0;
}

static void load(struct devices *dv)
{
	read_first_line("/proc/device-tree/model", dv->model, sizeof(dv->model));
	if (dv->model[0] == 0 || strcmp(dv->model, "—") == 0)
		read_first_line("/sys/firmware/devicetree/base/model", dv->model, sizeof(dv->model));
	struct utsname un;
	uname(&un);
	snprintf(dv->kernel, sizeof(dv->kernel), "Linux %s (%s)", un.release, un.machine);
	/* CPU */
	dv->cpu[0] = 0;
	FILE *fp = fopen("/proc/cpuinfo", "r");
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "model name", 10) == 0 || strncmp(line, "Hardware", 8) == 0) {
				char *colon = strchr(line, ':');
				if (colon)
					snprintf(dv->cpu, sizeof(dv->cpu), "%s", colon + 2);
				dv->cpu[strcspn(dv->cpu, "\n")] = 0;
				if (dv->cpu[0])
					break;
			}
		}
		fclose(fp);
	}
	if (!dv->cpu[0])
		snprintf(dv->cpu, sizeof(dv->cpu), "%s", un.machine);
	long total = 0, avail = 0;
	fp = fopen("/proc/meminfo", "r");
	if (fp) {
		char line[128];
		while (fgets(line, sizeof(line), fp)) {
			sscanf(line, "MemTotal: %ld", &total);
			sscanf(line, "MemAvailable: %ld", &avail);
		}
		fclose(fp);
	}
	snprintf(dv->mem, sizeof(dv->mem), "%.1f MB (空き %.1f MB)", total / 1024.0, avail / 1024.0);
	struct statvfs sv;
	if (statvfs("/", &sv) == 0) {
		double tot = (double)sv.f_blocks * sv.f_frsize / 1048576.0;
		double free_gb = (double)sv.f_bavail * sv.f_frsize / 1048576.0;
		snprintf(dv->storage, sizeof(dv->storage), "ルート %.0f MB (空き %.0f MB)", tot, free_gb);
	} else {
		snprintf(dv->storage, sizeof(dv->storage), "—");
	}
	/* battery: first power_supply with a capacity file */
	char cap[16] = "";
	{
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "for d in /sys/class/power_supply/*; do [ -f \"$d/capacity\" ] && cat \"$d/capacity\" && break; done");
		FILE *pp = popen(cmd, "r");
		if (pp) {
			if (!fgets(cap, sizeof(cap), pp))
				cap[0] = 0;
			pclose(pp);
		}
	}
	if (cap[0])
		snprintf(dv->battery, sizeof(dv->battery), "%s%%", cap);
	else
		snprintf(dv->battery, sizeof(dv->battery), "— (電源情報なし)");
	read_first_line("/sys/class/graphics/fb0/virtual_size", dv->display, sizeof(dv->display));
	/* sound card names */
	dv->sound[0] = 0;
	fp = fopen("/proc/asound/cards", "r");
	if (fp) {
		char line[256];
		if (fgets(line, sizeof(line), fp)) {
			char *b1 = strchr(line, '[');
			char *b2 = strchr(line, ']');
			if (b1 && b2 && b2 > b1) {
				int len = (int)(b2 - b1 - 1);
				if (len > 120)
					len = 120;
				memcpy(dv->sound, b1 + 1, len);
				dv->sound[len] = 0;
			}
		}
		fclose(fp);
	}
	if (!dv->sound[0])
		snprintf(dv->sound, sizeof(dv->sound), "—");
	/* input device names */
	dv->inputs[0] = 0;
	fp = fopen("/proc/bus/input/devices", "r");
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "N: Name=", 8) == 0) {
				char nm[64];
				snprintf(nm, sizeof(nm), "%s", line + 9);
				nm[strcspn(nm, "\"\n")] = 0;
				if (dv->inputs[0])
					strncat(dv->inputs, ", ", sizeof(dv->inputs) - strlen(dv->inputs) - 1);
				strncat(dv->inputs, nm, sizeof(dv->inputs) - strlen(dv->inputs) - 1);
			}
		}
		fclose(fp);
	}
	if (!dv->inputs[0])
		snprintf(dv->inputs, sizeof(dv->inputs), "—");
	/* modules */
	dv->nmod = 0;
	fp = fopen("/proc/modules", "r");
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp) && dv->nmod < MAXMOD) {
			char nm[56];
			long sz;
			int used;
			if (sscanf(line, "%55s %ld %d", nm, &sz, &used) == 3) {
				snprintf(dv->mods[dv->nmod].name, 56, "%s", nm);
				dv->mods[dv->nmod].size = sz;
				dv->mods[dv->nmod].used = used;
				dv->nmod++;
			}
		}
		fclose(fp);
	}
}

static void row_kv(bdapp *a, int y, const char *key, const char *val)
{
	bu_theme *t = a->theme;
	bu_fill_round_rect(a->canvas, (bu_rect){ 12, y, 840, 40 }, 8, t->surface);
	bu_draw_text(a->canvas, t->font, 28, y + 9, key, 14, t->text_dim);
	bu_draw_text(a->canvas, t->font, 240, y + 9, val, 14, t->text);
}

static void draw(bdapp *a, void *ctx)
{
	struct devices *dv = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	bu_button(c, t, (bu_rect){ 16, 54, 170, 38 }, "デバイス", dv->tab == 0 ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 198, 54, 170, 38 }, "ドライバ", dv->tab == 1 ? BU_BTN_PRIMARY : BU_BTN_NORMAL, false);
	if (dv->tab == 0) {
		int y = 106;
		row_kv(a, y, "機種", dv->model); y += 46;
		row_kv(a, y, "カーネル", dv->kernel); y += 46;
		row_kv(a, y, "CPU", dv->cpu); y += 46;
		row_kv(a, y, "メモリ", dv->mem); y += 46;
		row_kv(a, y, "ストレージ", dv->storage); y += 46;
		row_kv(a, y, "バッテリー", dv->battery); y += 46;
		row_kv(a, y, "ディスプレイ", dv->display); y += 46;
		row_kv(a, y, "サウンド", dv->sound); y += 46;
		bu_fill_round_rect(c, (bu_rect){ 12, y, 840, 40 }, 8, t->surface);
		bu_draw_text(c, t->font, 28, y + 9, "入力", 14, t->text_dim);
		bu_draw_text(c, t->font, 240, y + 9, dv->inputs, 12.5, t->text);
	} else {
		char hdr[64];
		snprintf(hdr, sizeof(hdr), "読み込み済みモジュール %d 件", dv->nmod);
		bu_draw_text(c, t->font, 16, 102, hdr, 13.5, t->text_dim);
		for (int vi = 0; vi < VISIBLE; vi++) {
			int i = dv->scroll + vi;
			if (i >= dv->nmod)
				break;
			bu_rect r = { 12, 126 + vi * ROWH, 700, ROWH - 3 };
			bu_fill_round_rect(c, r, 6, t->surface);
			bu_draw_text(c, t->font, r.x + 12, r.y + 5, dv->mods[i].name, 13, t->text);
			char meta[48];
			snprintf(meta, sizeof(meta), "%.0f KB  使用 %d", dv->mods[i].size / 1024.0, dv->mods[i].used);
			bu_draw_text_right(c, t->font, (bu_rect){ r.x, r.y + 5, r.w - 12, 20 }, meta, 12, t->text_dim);
		}
		bu_button(c, t, (bu_rect){ 724, 126, 52, 56 }, "▲", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 724, 400, 52, 56 }, "▼", BU_BTN_NORMAL, false);
		if (dv->nmod == 0)
			bu_draw_text(c, t->font, 16, 140, "モジュールはありません (ドライバはカーネル組み込み)", 13.5, t->text_dim);
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
	*changed = true;
	if (inr(ev->x, ev->y, (bu_rect){ 16, 54, 170, 38 })) { dv->tab = 0; return; }
	if (inr(ev->x, ev->y, (bu_rect){ 198, 54, 170, 38 })) { dv->tab = 1; return; }
	if (dv->tab == 1) {
		if (inr(ev->x, ev->y, (bu_rect){ 724, 126, 52, 56 }) && dv->scroll > 0)
			dv->scroll -= 5;
		if (inr(ev->x, ev->y, (bu_rect){ 724, 400, 52, 56 }) && dv->scroll + VISIBLE < dv->nmod)
			dv->scroll += 5;
	}
}

int main(int argc, char **argv)
{
	struct devices dv;
	memset(&dv, 0, sizeof(dv));
	load(&dv);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-devices.ppm" : NULL;
	return bdapp_run("デバイスマネージャー", shot, draw, onevent, NULL, &dv);
}
