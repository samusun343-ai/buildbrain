/* setmodel - see setmodel.h. */
#define _POSIX_C_SOURCE 200809L
#include "setmodel.h"
#include "kbdsettings.h"
#include "reg.h"
#include "tk_kbd.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct set_model {
    bd_backend *be;
    bd_wifi *wifi;
    sy_sys *sys;
    bd_wifi_ap aps[32];
    int n_aps;
    bool scanned;
    char psk[64];
};

set_model *set_model_new(bd_backend *backend, bd_wifi *wifi,
                         sy_sys *sys)
{
    set_model *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->be = backend;
    m->wifi = wifi;
    m->sys = sys;
    return m;
}
void set_model_free(set_model *m) { free(m); }

const char *set_page_name(int page)
{
    static const char *names[SET_PAGE_COUNT] = {
        "キーボード", "画面", "サウンド", "Wi-Fi", "電源",
        "日付と時刻", "マスコット", "端末情報",
        "個人用設定", "ストレージ"
    };
    return (page >= 0 && page < SET_PAGE_COUNT) ? names[page] : "?";
}

/* ---- display mode (DESIGN-settings-rebuild.md section 6) ----
 * The settings app is the WRITER side of the display-mode
 * switch; the shell and every app are the followers. The
 * canonical key was fixed by the shell line's contract
 * (shell/REPORT-status-icons.md section 4): brainreg
 * "ui"/"mode", read once at process start via
 * bd_theme_from_reg(). The write stays isolated here behind
 * these constants (aligned to the canonical key 2026-11-21
 * by the apps dual-mode line). Default when unset:
 * "android". */
#define SET_UI_MODE_SECTION "ui"
#define SET_UI_MODE_KEY     "mode"
#define SET_UI_MODE_ANDROID "android"
#define SET_UI_MODE_WINUI   "winui"

static int ui_mode_current(void)
{
    char *v = reg_get(SET_UI_MODE_SECTION, SET_UI_MODE_KEY);
    int idx = (v && strcmp(v, SET_UI_MODE_WINUI) == 0) ? 1 : 0;
    free(v);
    return idx;
}

/* wallpaper colour presets (shell/wallpaper in its colour
 * form; the shell parses "#RRGGBB"). Index 0 of the cycle is
 * not a colour: it is "default", which unsets the key so the
 * shell falls back to the theme's desktop colour. An existing
 * image-path value is NOT a preset: the row reports it as a
 * custom value and choosing a preset is an explicit user act
 * that replaces it (recorded in the design, item 9). */
static const struct { const char *name, *hex; } wp_presets[] = {
    { "紺",   "#2E4A6B" },
    { "深緑", "#2F5D50" },
    { "紫",   "#5B4A8A" },
    { "ワイン", "#7A3B4F" },
    { "グレー", "#4A4A4A" },
    { "黒",   "#101418" },
};
#define N_WP_PRESETS (sizeof(wp_presets) / sizeof(wp_presets[0]))
/* cycle entries: 0 = 既定, 1..N = presets */
#define WP_N_CHOICES (1 + (int)N_WP_PRESETS)

static bool hex_eq(const char *a, const char *b)
{
    if (a && *a == '#') a++;
    if (b && *b == '#') b++;
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'f') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'f') cb -= 'a' - 'A';
        if (ca != cb) return false;
    }
    return *a == *b;
}

/* the volume the shell would restore on unmute: its
 * persisted level (shell/volume), falling back to the live
 * backend level, then the shell's own load default of 50 */
static int preserved_volume(set_model *m)
{
    int v = reg_get_int("shell", "volume", -1);
    if (v < 0 && m->be) v = bd_backend_volume_get(m->be);
    if (v < 0) v = 50;
    if (v > 100) v = 100;
    return v;
}

static set_row *row_new(set_row *out, size_t max, size_t *n,
                        const char *id, const char *label,
                        set_row_kind kind)
{
    if (*n >= max) return NULL;
    set_row *r = &out[(*n)++];
    memset(r, 0, sizeof(*r));
    snprintf(r->id, sizeof(r->id), "%s", id);
    snprintf(r->label, sizeof(r->label), "%s", label);
    r->kind = kind;
    return r;
}
static void row_val(set_row *r, const char *fmt, ...)
{
    if (!r) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->value, sizeof(r->value), fmt, ap);
    va_end(ap);
}

/* sleep timeout choices (minutes; 0 = never) */
static const int sleep_choices[] = { 0, 1, 5, 10, 30 };
#define N_SLEEP (sizeof(sleep_choices) / sizeof(sleep_choices[0]))

/* ---- mascot page ----
 * The session reads brainreg section "mascot" once at startup
 * (shell/src/session.c): enabled (bool, default true), pack
 * (path; fallback $HOME/.local/share/brainux/mascots/mascot.bdm,
 * then /usr/share/brainux/mascots/mascot.bdm), size (int,
 * default 208), fullscreen (bool, default false). The rows
 * below read and write exactly those keys - no private copy,
 * so what the settings app shows is what the next session
 * start will use. Pack enumeration mirrors the session's two
 * directories (user layer wins on a name clash, like the
 * session fallback); BRAINUX_MASCOT_DIR replaces them with a
 * single directory (tests - the session's own override is
 * BRAINUX_MASCOT_PACK). */

#define MASCOT_MAX_PACKS SET_MAX_CHOICES

typedef struct {
    char path[512];
    char name[64];   /* basename without the ".bdm" suffix */
} mascot_pack;

static bool has_suffix(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a > b && strcmp(s + a - b, suf) == 0;
}

static int mascot_pack_list(mascot_pack *out, int max)
{
    const char *env = getenv("BRAINUX_MASCOT_DIR");
    char user_dir[512] = "";
    const char *dirs[2];
    int ndirs = 0;
    if (env && *env) {
        dirs[ndirs++] = env;
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(user_dir, sizeof(user_dir),
                     "%s/.local/share/brainux/mascots", home);
            dirs[ndirs++] = user_dir;
        }
        dirs[ndirs++] = "/usr/share/brainux/mascots";
    }
    int n = 0;
    for (int d = 0; d < ndirs; d++) {
        struct dirent **ents = NULL;
        int cnt = scandir(dirs[d], &ents, NULL, alphasort);
        if (cnt < 0) continue;
        for (int i = 0; i < cnt; i++) {
            const char *fn = ents[i]->d_name;
            if (has_suffix(fn, ".bdm") && n < max) {
                char name[64];
                snprintf(name, sizeof(name), "%s", fn);
                if (has_suffix(name, ".bdm"))
                    name[strlen(name) - 4] = '\0';   /* strip .bdm */
                bool dup = false;
                for (int j = 0; j < n; j++)
                    if (strcmp(out[j].name, name) == 0) dup = true;
                if (!dup) {
                    snprintf(out[n].path, sizeof(out[n].path),
                             "%s/%s", dirs[d], fn);
                    snprintf(out[n].name, sizeof(out[n].name),
                             "%s", name);
                    n++;
                }
            }
            free(ents[i]);
        }
        free(ents);
    }
    return n;
}

/* The pack the next session start would load, resolved with
 * the same fallback chain as session.c. Empty when none. */
static void mascot_current_pack(char *out, size_t outsz)
{
    char *reg = reg_get("mascot", "pack");
    if (reg) {
        snprintf(out, outsz, "%s", reg);
        free(reg);
        return;
    }
    const char *home = getenv("HOME");
    char cand[512];
    if (home && *home) {
        snprintf(cand, sizeof(cand),
                 "%s/.local/share/brainux/mascots/mascot.bdm", home);
        if (access(cand, R_OK) == 0) {
            snprintf(out, outsz, "%s", cand);
            return;
        }
    }
    if (access("/usr/share/brainux/mascots/mascot.bdm", R_OK) == 0) {
        snprintf(out, outsz, "%s",
                 "/usr/share/brainux/mascots/mascot.bdm");
        return;
    }
    if (outsz) out[0] = '\0';
}

static void pack_display_name(const char *path, char *out,
                              size_t outsz)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(out, outsz, "%s", base);
    size_t len = strlen(out);
    if (len > 4 && strcmp(out + len - 4, ".bdm") == 0)
        out[len - 4] = '\0';
}

/* display sizes (px) - the heights packs are baked at */
static const int mascot_sizes[] = { 168, 208, 248, 320 };
#define N_MASCOT_SIZES (sizeof(mascot_sizes) / sizeof(mascot_sizes[0]))

size_t set_model_rows(set_model *m, int page, set_row *out, size_t max)
{
    size_t n = 0;
    set_row *r;
    switch (page) {
    case SET_PAGE_KEYBOARD: {
        tk_kbd_config cfg;
        bd_kbd_settings_load(&cfg);
        int pi = bd_kbd_repeat_preset_of(&cfg);
        r = row_new(out, max, &n, "preset", "リピートプリセット",
                    SR_CYCLE);
        row_val(r, "%s", pi >= 0
                ? bd_kbd_repeat_preset_at(pi)->name : "カスタム");
        if (r) {
            int pc = bd_kbd_repeat_preset_count();
            r->n_choices = pc < SET_MAX_CHOICES ? pc
                             : SET_MAX_CHOICES;
            for (int i = 0; i < r->n_choices; i++)
                snprintf(r->choices[i], sizeof(r->choices[i]),
                         "%s", bd_kbd_repeat_preset_at(i)->name);
            r->choice_cur = pi;
        }
        /* repeat_source: a real store key (keyboard/
         * repeat_source) the old UI never exposed - the
         * design's item table, row 14 */
        r = row_new(out, max, &n, "repeat_source", "リピート方式",
                    SR_CYCLE);
        if (r) {
            r->n_choices = 2;
            snprintf(r->choices[0], sizeof(r->choices[0]),
                     "ソフトウェア");
            snprintf(r->choices[1], sizeof(r->choices[1]),
                     "カーネル");
            r->choice_cur =
                cfg.repeat_mode == TK_KBD_REPEAT_KERNEL ? 1 : 0;
            row_val(r, "%s", r->choices[r->choice_cur]);
        }
        r = row_new(out, max, &n, "delay", "リピート開始 (ms)",
                    SR_SLIDER);
        if (r) {
            r->slider_min = 100; r->slider_max = 1000;
            r->slider_cur = cfg.repeat_delay_ms;
            row_val(r, "%d ms", cfg.repeat_delay_ms);
        }
        r = row_new(out, max, &n, "interval", "リピート間隔 (ms)",
                    SR_SLIDER);
        if (r) {
            r->slider_min = 10; r->slider_max = 200;
            r->slider_cur = cfg.repeat_interval_ms;
            row_val(r, "%d ms", cfg.repeat_interval_ms);
        }
        r = row_new(out, max, &n, "longpress", "長押し判定 (ms)",
                    SR_SLIDER);
        if (r) {
            r->slider_min = 0; r->slider_max = 1500;
            r->slider_cur = cfg.longpress_ms;
            row_val(r, "%d ms", cfg.longpress_ms);
        }
        r = row_new(out, max, &n, "layout", "キー配列", SR_INFO);
        row_val(r, "Brain 固定配列（字面どおり）");
        break;
    }
    case SET_PAGE_DISPLAY: {
        int b = m->be ? bd_backend_brightness_get(m->be) : -1;
        r = row_new(out, max, &n, "brightness", "輝度", SR_SLIDER);
        if (r) {
            r->slider_min = 0; r->slider_max = 100;
            r->slider_cur = b < 0 ? 0 : b;
            row_val(r, b < 0 ? "取得不可" : "%d %%", b);
        }
        /* no "backend" row: which driver object serves the
         * value is a developer detail, not a user setting -
         * it read "mock" on screen in the pilot shots */
        break;
    }
    case SET_PAGE_SOUND: {
        bool muted = reg_get_bool("shell", "muted", false);
        int v = m->be ? bd_backend_volume_get(m->be) : -1;
        r = row_new(out, max, &n, "volume", "音量", SR_SLIDER);
        if (r) {
            r->slider_min = 0; r->slider_max = 100;
            /* while muted the hardware sits at 0 (the shell
             * drives it there); the slider shows the level
             * that unmuting restores, like the QS slider */
            r->slider_cur = muted ? preserved_volume(m)
                                  : (v < 0 ? 0 : v);
            if (v < 0 && !muted) row_val(r, "取得不可");
            else if (muted)
                row_val(r, "%d %%（ミュート中）",
                        preserved_volume(m));
            else row_val(r, "%d %%", v);
        }
        r = row_new(out, max, &n, "mute", "ミュート", SR_TOGGLE);
        if (r) r->toggle_on = muted;
        row_val(r, "%s", muted ? "オン" : "オフ");
        break;
    }
    case SET_PAGE_WIFI: {
        bd_wifi_status st;
        memset(&st, 0, sizeof(st));
        bool ok = m->wifi && bd_wifi_get_status(m->wifi, &st) == 0;
        /* the Wi-Fi on/off switch heads the page (One UI /
         * ChromeOS form). Its store is the SAME brainreg key
         * the shell's quick-settings tile drives
         * (shell/wifi), so the two can never disagree; the
         * supplicant side effect mirrors bd_shell_set_wifi. */
        bool won = reg_get_bool("shell", "wifi", true);
        r = row_new(out, max, &n, "wifi_on", "Wi-Fi", SR_TOGGLE);
        if (r) r->toggle_on = won;
        row_val(r, "%s", won ? "オン" : "オフ");
        r = row_new(out, max, &n, "state", "状態", SR_INFO);
        row_val(r, "%s", !m->wifi ? "利用不可"
                : ok ? st.state : "応答なし");
        r = row_new(out, max, &n, "ssid", "接続先", SR_INFO);
        row_val(r, "%s", ok && st.connected ? st.ssid : "（未接続）");
        r = row_new(out, max, &n, "ip", "IP アドレス", SR_INFO);
        row_val(r, "%s", ok && st.ip[0] ? st.ip : "—");
        r = row_new(out, max, &n, "scan", "ネットワークをスキャン",
                    SR_ACTION);
        row_val(r, "タップで実行");
        if (m->scanned) {
            for (int i = 0; i < m->n_aps && n < max; i++) {
                char id[32];
                snprintf(id, sizeof(id), "ap%d", i);
                r = row_new(out, max, &n, id, m->aps[i].ssid[0]
                            ? m->aps[i].ssid : "（非公開 SSID）",
                            SR_ACTION);
                row_val(r, "%d dBm %s", m->aps[i].signal_dbm,
                        m->aps[i].flags);
            }
        }
        r = row_new(out, max, &n, "disconnect", "切断", SR_ACTION);
        row_val(r, "タップで実行");
        break;
    }
    case SET_PAGE_POWER: {
        bd_battery_info bi;
        bool ok = m->be && bd_backend_battery(m->be, &bi) && bi.present;
        r = row_new(out, max, &n, "battery", "電池残量", SR_INFO);
        row_val(r, "%s", ok ? "" : "不明");
        if (r && ok) {
            snprintf(r->value, sizeof(r->value), "%d %%（%s）",
                     bi.capacity, bi.status);
        }
        int cur = reg_get_int("power", "sleep_timeout_min", 5);
        r = row_new(out, max, &n, "sleep", "スリープまでの時間",
                    SR_CYCLE);
        row_val(r, "%s", cur == 0 ? "なし" : "");
        if (r && cur != 0)
            snprintf(r->value, sizeof(r->value), "%d 分", cur);
        if (r) {
            r->n_choices = (int)N_SLEEP < SET_MAX_CHOICES
                ? (int)N_SLEEP : SET_MAX_CHOICES;
            r->choice_cur = -1;
            for (int i = 0; i < r->n_choices; i++) {
                if (sleep_choices[i] == 0)
                    snprintf(r->choices[i], sizeof(r->choices[i]),
                             "なし");
                else
                    snprintf(r->choices[i], sizeof(r->choices[i]),
                             "%d 分", sleep_choices[i]);
                if (sleep_choices[i] == cur) r->choice_cur = i;
            }
        }
        break;
    }
    case SET_PAGE_DATETIME: {
        int off = reg_get_int("datetime", "local_offset_min", 540);
        r = row_new(out, max, &n, "offset", "UTC オフセット (分)",
                    SR_SLIDER);
        if (r) {
            r->slider_min = -720; r->slider_max = 840;
            r->slider_cur = off;
            row_val(r, "UTC%+d 分（%+d:%02d）", off, off / 60,
                    abs(off % 60));
        }
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        r = row_new(out, max, &n, "utc", "現在時刻 (UTC)", SR_INFO);
        row_val(r, "%s", buf);
        time_t local = now + off * 60;
        gmtime_r(&local, &tmv);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        r = row_new(out, max, &n, "local", "現在時刻 (ローカル)",
                    SR_INFO);
        row_val(r, "%s", buf);
        r = row_new(out, max, &n, "note", "システム時刻の設定",
                    SR_INFO);
        row_val(r, "特権が必要なため OS 統合フェーズで対応");
        break;
    }
    case SET_PAGE_MASCOT: {
        bool en = reg_get_bool("mascot", "enabled", true);
        r = row_new(out, max, &n, "enabled", "マスコットを表示",
                    SR_TOGGLE);
        if (r) r->toggle_on = en;
        row_val(r, "%s", en ? "オン" : "オフ");

        mascot_pack packs[MASCOT_MAX_PACKS];
        int np = mascot_pack_list(packs, MASCOT_MAX_PACKS);
        char cur[512];
        mascot_current_pack(cur, sizeof(cur));
        if (np > 0) {
            r = row_new(out, max, &n, "pack", "キャラクター",
                        SR_CYCLE);
            if (r) {
                r->n_choices = np;
                r->choice_cur = -1;
                for (int i = 0; i < np; i++) {
                    snprintf(r->choices[i], sizeof(r->choices[i]),
                             "%s", packs[i].name);
                    if (strcmp(packs[i].path, cur) == 0)
                        r->choice_cur = i;
                }
            }
            char disp[64];
            pack_display_name(cur, disp, sizeof(disp));
            row_val(r, "%s", cur[0] ? disp : "（未選択）");
        } else {
            r = row_new(out, max, &n, "pack", "キャラクター",
                        SR_INFO);
            row_val(r, "パックが見つかりません");
        }

        int sz = reg_get_int("mascot", "size", 208);
        r = row_new(out, max, &n, "size", "表示サイズ", SR_CYCLE);
        row_val(r, "%d px", sz);
        if (r) {
            r->n_choices = (int)N_MASCOT_SIZES;
            r->choice_cur = -1;
            for (int i = 0; i < r->n_choices; i++) {
                snprintf(r->choices[i], sizeof(r->choices[i]),
                         "%d px", mascot_sizes[i]);
                if (mascot_sizes[i] == sz) r->choice_cur = i;
            }
        }

        bool fs = reg_get_bool("mascot", "fullscreen", false);
        r = row_new(out, max, &n, "fullscreen",
                    "全画面アプリの上にも表示", SR_TOGGLE);
        if (r) r->toggle_on = fs;
        row_val(r, "%s", fs ? "オン" : "オフ");

        r = row_new(out, max, &n, "note", "反映", SR_INFO);
        row_val(r, "変更は次回起動時から反映されます");
        break;
    }
    case SET_PAGE_ABOUT: {
        char sn[64], rel[128], mach[64], ver[128];
        if (m->sys && sy_system_info(m->sys, NULL, sn, sizeof(sn),
                                     rel, sizeof(rel), mach,
                                     sizeof(mach), ver,
                                     sizeof(ver)) == 0) {
            r = row_new(out, max, &n, "os", "OS", SR_INFO);
            row_val(r, "%s %s", sn, rel);
            r = row_new(out, max, &n, "machine", "機種 (uname)",
                        SR_INFO);
            row_val(r, "%s", mach);
            r = row_new(out, max, &n, "rootfs", "rootfs バージョン",
                        SR_INFO);
            row_val(r, "%s", ver[0] ? ver : "不明");
        }
        uint64_t total, avail, st_, sf_;
        if (m->sys && sy_meminfo(m->sys, &total, &avail, &st_, &sf_) == 0) {
            r = row_new(out, max, &n, "mem", "メモリ", SR_INFO);
            row_val(r, "合計 %llu MB / 利用可能 %llu MB",
                    (unsigned long long)(total / 1024),
                    (unsigned long long)(avail / 1024));
            r = row_new(out, max, &n, "swap", "スワップ", SR_INFO);
            row_val(r, "合計 %llu MB / 空き %llu MB",
                    (unsigned long long)(st_ / 1024),
                    (unsigned long long)(sf_ / 1024));
        }
        sy_mount mounts[16];
        size_t nm = m->sys ? sy_mounts(m->sys, mounts, 16) : 0;
        for (size_t i = 0; i < nm && n < max; i++) {
            if (mounts[i].total_bytes == 0) continue;
            char id[32];
            snprintf(id, sizeof(id), "fs%zu", i);
            r = row_new(out, max, &n, id, mounts[i].mountpoint,
                        SR_INFO);
            row_val(r, "%s: 合計 %llu MB / 空き %llu MB",
                    mounts[i].fstype,
                    (unsigned long long)(mounts[i].total_bytes >> 20),
                    (unsigned long long)(mounts[i].avail_bytes >> 20));
        }
        break;
    }
    case SET_PAGE_PERSONAL: {
        /* display mode (design section 6): the settings app
         * writes, the shell and apps follow */
        int mode = ui_mode_current();
        r = row_new(out, max, &n, "ui_mode", "表示モード",
                    SR_CYCLE);
        if (r) {
            r->n_choices = 2;
            snprintf(r->choices[0], sizeof(r->choices[0]),
                     "Android");
            snprintf(r->choices[1], sizeof(r->choices[1]),
                     "Windows");
            r->choice_cur = mode;
            row_val(r, "%s", r->choices[mode]);
        }
        /* wallpaper: colour presets or the existing value,
         * reported honestly (design items 8/9) */
        char *wp = reg_get("shell", "wallpaper");
        r = row_new(out, max, &n, "wallpaper", "壁紙の色",
                    SR_CYCLE);
        if (r) {
            r->n_choices = WP_N_CHOICES < SET_MAX_CHOICES
                ? WP_N_CHOICES : SET_MAX_CHOICES;
            snprintf(r->choices[0], sizeof(r->choices[0]), "既定");
            for (int i = 1; i < r->n_choices; i++)
                snprintf(r->choices[i], sizeof(r->choices[i]),
                         "%s", wp_presets[i - 1].name);
            r->choice_cur = -1;
            if (!wp || !wp[0]) {
                r->choice_cur = 0;
                row_val(r, "既定（テーマの色）");
            } else if (wp[0] == '/') {
                row_val(r, "画像（%s）", wp);
            } else {
                for (int i = 0; i < (int)N_WP_PRESETS; i++)
                    if (hex_eq(wp, wp_presets[i].hex)) {
                        r->choice_cur = i + 1;
                        break;
                    }
                row_val(r, "%s", r->choice_cur >= 0
                        ? wp_presets[r->choice_cur - 1].name : wp);
            }
        }
        free(wp);
        /* panel (taskbar) position: shell/panel_position,
         * "top" or anything else = bottom */
        char *pos = reg_get("shell", "panel_position");
        int top = pos && strcmp(pos, "top") == 0;
        free(pos);
        r = row_new(out, max, &n, "panel", "パネルの位置",
                    SR_CYCLE);
        if (r) {
            r->n_choices = 2;
            snprintf(r->choices[0], sizeof(r->choices[0]), "下");
            snprintf(r->choices[1], sizeof(r->choices[1]), "上");
            r->choice_cur = top ? 1 : 0;
            row_val(r, "%s", top ? "上" : "下");
        }
        break;
    }
    case SET_PAGE_STORAGE: {
        sy_mount mounts[16];
        size_t nm = m->sys ? sy_mounts(m->sys, mounts, 16) : 0;
        for (size_t i = 0; i < nm && n < max; i++) {
            if (mounts[i].total_bytes == 0) continue;
            char id[32];
            snprintf(id, sizeof(id), "fs%zu", i);
            r = row_new(out, max, &n, id, mounts[i].mountpoint,
                        SR_INFO);
            if (r) {
                uint64_t used = mounts[i].total_bytes -
                                mounts[i].avail_bytes;
                int pct = (int)(used * 100 / mounts[i].total_bytes);
                row_val(r, "%s: 合計 %llu MB / 空き %llu MB"
                        "（使用 %d %%）",
                        mounts[i].fstype,
                        (unsigned long long)(mounts[i].total_bytes >> 20),
                        (unsigned long long)(mounts[i].avail_bytes >> 20),
                        pct);
            }
        }
        if (n == 0) {
            r = row_new(out, max, &n, "storage", "ストレージ",
                        SR_INFO);
            row_val(r, "情報を取得できません");
        }
        break;
    }
    default:
        break;
    }
    return n;
}

int set_brightness(set_model *m)
{
    return m->be ? bd_backend_brightness_get(m->be) : -1;
}
int set_volume(set_model *m)
{
    return m->be ? bd_backend_volume_get(m->be) : -1;
}

int set_model_set_slider(set_model *m, int page, const char *id,
                         int value)
{
    switch (page) {
    case SET_PAGE_KEYBOARD: {
        tk_kbd_config cfg;
        bd_kbd_settings_load(&cfg);
        /* clamp to the ranges the rows advertise */
        if (strcmp(id, "delay") == 0) {
            if (value < 100) value = 100;
            if (value > 1000) value = 1000;
            cfg.repeat_delay_ms = value;
        } else if (strcmp(id, "interval") == 0) {
            if (value < 10) value = 10;
            if (value > 200) value = 200;
            cfg.repeat_interval_ms = value;
        } else if (strcmp(id, "longpress") == 0) {
            if (value < 0) value = 0;
            if (value > 1500) value = 1500;
            cfg.longpress_ms = value;
        }
        else return -1;
        tk_kbd_config_sanitize(&cfg);
        bd_kbd_settings_save(&cfg);
        return 0;
    }
    case SET_PAGE_DISPLAY:
        if (strcmp(id, "brightness") == 0 && m->be)
            return bd_backend_brightness_set(m->be, value);
        return -1;
    case SET_PAGE_SOUND:
        if (strcmp(id, "volume") == 0 && m->be) {
            /* mirror bd_shell_set_volume: persist the level,
             * and a change to a DIFFERENT level clears mute
             * (unmute-on-adjust); the backend then receives
             * the level itself (mute is already cleared) */
            if (value < 0) value = 0;
            if (value > 100) value = 100;
            if (reg_get_bool("shell", "muted", false) &&
                value != preserved_volume(m))
                reg_set("shell", "muted", "false");
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", value);
            reg_set("shell", "volume", buf);
            return bd_backend_volume_set(m->be, value);
        }
        return -1;
    case SET_PAGE_DATETIME:
        if (strcmp(id, "offset") == 0) {
            /* keep to 30-minute steps like real tz offsets */
            value = (value / 30) * 30;
            char buf[24];
            snprintf(buf, sizeof(buf), "%d", value);
            return reg_set("datetime", "local_offset_min", buf);
        }
        return -1;
    default:
        return -1;
    }
}

/* PERSONAL page choice application, shared by cycle and
 * set_choice. idx is the row's choice index. */
static int personal_apply(const char *id, int idx)
{
    if (strcmp(id, "ui_mode") == 0) {
        if (idx < 0 || idx > 1) return -1;
        return reg_set(SET_UI_MODE_SECTION, SET_UI_MODE_KEY,
                       idx == 1 ? SET_UI_MODE_WINUI
                                : SET_UI_MODE_ANDROID);
    }
    if (strcmp(id, "wallpaper") == 0) {
        if (idx < 0 || idx >= WP_N_CHOICES) return -1;
        if (idx == 0) return reg_unset("shell", "wallpaper");
        return reg_set("shell", "wallpaper",
                       wp_presets[idx - 1].hex);
    }
    if (strcmp(id, "panel") == 0) {
        if (idx < 0 || idx > 1) return -1;
        return reg_set("shell", "panel_position",
                       idx == 1 ? "top" : "bottom");
    }
    return -1;
}

static int kbd_repeat_source_apply(int idx)
{
    if (idx < 0 || idx > 1) return -1;
    tk_kbd_config cfg;
    bd_kbd_settings_load(&cfg);
    cfg.repeat_mode = idx == 1 ? TK_KBD_REPEAT_KERNEL
                               : TK_KBD_REPEAT_SYNTHETIC;
    bd_kbd_settings_save(&cfg);
    return 0;
}

int set_model_cycle(set_model *m, int page, const char *id)
{
    if (page == SET_PAGE_PERSONAL) {
        /* advance from the row's current choice */
        set_row rows[16];
        size_t n = set_model_rows(m, page, rows, 16);
        for (size_t i = 0; i < n; i++) {
            if (strcmp(rows[i].id, id) != 0) continue;
            if (rows[i].kind != SR_CYCLE || rows[i].n_choices <= 0)
                return -1;
            int next = (rows[i].choice_cur + 1) % rows[i].n_choices;
            return personal_apply(id, next);
        }
        return -1;
    }
    if (page == SET_PAGE_KEYBOARD &&
        strcmp(id, "repeat_source") == 0) {
        tk_kbd_config cfg;
        bd_kbd_settings_load(&cfg);
        return kbd_repeat_source_apply(
            cfg.repeat_mode == TK_KBD_REPEAT_KERNEL ? 0 : 1);
    }
    if (page == SET_PAGE_KEYBOARD && strcmp(id, "preset") == 0) {
        tk_kbd_config cfg;
        bd_kbd_settings_load(&cfg);
        int cur = bd_kbd_repeat_preset_of(&cfg);
        int next = (cur + 1) % bd_kbd_repeat_preset_count();
        const bd_kbd_repeat_preset *p = bd_kbd_repeat_preset_at(next);
        cfg.repeat_delay_ms = p->delay_ms;
        cfg.repeat_interval_ms = p->interval_ms;
        tk_kbd_config_sanitize(&cfg);
        bd_kbd_settings_save(&cfg);
        return 0;
    }
    if (page == SET_PAGE_POWER && strcmp(id, "sleep") == 0) {
        int cur = reg_get_int("power", "sleep_timeout_min", 5);
        size_t at = 0;
        for (size_t i = 0; i < N_SLEEP; i++)
            if (sleep_choices[i] == cur) at = i;
        int next = sleep_choices[(at + 1) % N_SLEEP];
        char buf[24];
        snprintf(buf, sizeof(buf), "%d", next);
        return reg_set("power", "sleep_timeout_min", buf);
    }
    if (page == SET_PAGE_MASCOT && strcmp(id, "size") == 0) {
        int cur = reg_get_int("mascot", "size", 208);
        size_t at = 0;
        for (size_t i = 0; i < N_MASCOT_SIZES; i++)
            if (mascot_sizes[i] == cur) at = i;
        int next = mascot_sizes[(at + 1) % N_MASCOT_SIZES];
        char buf[24];
        snprintf(buf, sizeof(buf), "%d", next);
        return reg_set("mascot", "size", buf);
    }
    if (page == SET_PAGE_MASCOT && strcmp(id, "pack") == 0) {
        mascot_pack packs[MASCOT_MAX_PACKS];
        int np = mascot_pack_list(packs, MASCOT_MAX_PACKS);
        if (np <= 0) return -1;
        char cur[512];
        mascot_current_pack(cur, sizeof(cur));
        int at = -1;
        for (int i = 0; i < np; i++)
            if (strcmp(packs[i].path, cur) == 0) at = i;
        int next = (at + 1) % np;
        return reg_set("mascot", "pack", packs[next].path);
    }
    return -1;
}

int set_model_set_choice(set_model *m, int page, const char *id,
                         int idx)
{
    (void)m; /* choices write through reg / kbdsettings */
    if (page == SET_PAGE_PERSONAL)
        return personal_apply(id, idx);
    if (page == SET_PAGE_KEYBOARD &&
        strcmp(id, "repeat_source") == 0)
        return kbd_repeat_source_apply(idx);
    if (page == SET_PAGE_KEYBOARD && strcmp(id, "preset") == 0) {
        if (idx < 0 || idx >= bd_kbd_repeat_preset_count())
            return -1;
        tk_kbd_config cfg;
        bd_kbd_settings_load(&cfg);
        const bd_kbd_repeat_preset *p =
            bd_kbd_repeat_preset_at(idx);
        cfg.repeat_delay_ms = p->delay_ms;
        cfg.repeat_interval_ms = p->interval_ms;
        tk_kbd_config_sanitize(&cfg);
        bd_kbd_settings_save(&cfg);
        return 0;
    }
    if (page == SET_PAGE_POWER && strcmp(id, "sleep") == 0) {
        if (idx < 0 || (size_t)idx >= N_SLEEP) return -1;
        char buf[24];
        snprintf(buf, sizeof(buf), "%d", sleep_choices[idx]);
        return reg_set("power", "sleep_timeout_min", buf);
    }
    if (page == SET_PAGE_MASCOT && strcmp(id, "pack") == 0) {
        mascot_pack packs[MASCOT_MAX_PACKS];
        int np = mascot_pack_list(packs, MASCOT_MAX_PACKS);
        if (idx < 0 || idx >= np) return -1;
        return reg_set("mascot", "pack", packs[idx].path);
    }
    if (page == SET_PAGE_MASCOT && strcmp(id, "size") == 0) {
        if (idx < 0 || (size_t)idx >= N_MASCOT_SIZES) return -1;
        char buf[24];
        snprintf(buf, sizeof(buf), "%d", mascot_sizes[idx]);
        return reg_set("mascot", "size", buf);
    }
    return -1;
}

int set_model_set_toggle(set_model *m, int page, const char *id,
                         bool on)
{
    if (page == SET_PAGE_SOUND && strcmp(id, "mute") == 0) {
        /* mirror bd_shell_set_muted: persist the flag, then
         * drive the backend to the effective level - 0 while
         * muted, the preserved level on unmute. The shell
         * holds the preserved level in memory; the shared
         * store is shell/volume, so when muting FROM HERE
         * first align the store with the live level (on a
         * fresh device the key may not exist yet, and the
         * backend is about to read 0). */
        if (on && m->be) {
            int cur = bd_backend_volume_get(m->be);
            if (cur >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", cur);
                reg_set("shell", "volume", buf);
            }
        }
        if (reg_set("shell", "muted", on ? "true" : "false") != 0)
            return -1;
        if (m->be)
            bd_backend_volume_set(m->be,
                                  on ? 0 : preserved_volume(m));
        return 0;
    }
    if (page == SET_PAGE_WIFI && strcmp(id, "wifi_on") == 0) {
        /* same store + same supplicant side effects as the
         * shell's quick-settings tile (bd_shell_set_wifi) */
        if (reg_set("shell", "wifi", on ? "true" : "false") != 0)
            return -1;
        if (m->wifi) {
            if (on) (void)bd_wifi_reconnect(m->wifi);
            else (void)bd_wifi_disconnect(m->wifi);
        }
        return 0;
    }
    if (page == SET_PAGE_MASCOT &&
        (strcmp(id, "enabled") == 0 ||
         strcmp(id, "fullscreen") == 0)) {
        /* the row id IS the brainreg key in section "mascot";
         * reg_set is the atomic tmp+fsync+rename write */
        return reg_set("mascot", id, on ? "true" : "false");
    }
    return -1;
}

void set_model_set_psk(set_model *m, const char *psk)
{
    snprintf(m->psk, sizeof(m->psk), "%s", psk ? psk : "");
}

int set_model_action(set_model *m, int page, const char *id,
                     const char *arg)
{
    if (page != SET_PAGE_WIFI || !m->wifi) return -1;
    if (strcmp(id, "scan") == 0) {
        if (bd_wifi_scan(m->wifi) != 0) return -1;
        int got = bd_wifi_scan_results(m->wifi, m->aps, 32);
        if (got < 0) got = 0;
        m->n_aps = got;
        m->scanned = true;
        return 0;
    }
    if (strcmp(id, "disconnect") == 0)
        return bd_wifi_disconnect(m->wifi);
    if (strncmp(id, "ap", 2) == 0) {
        int i = atoi(id + 2);
        if (i < 0 || i >= m->n_aps) return -1;
        return bd_wifi_connect(m->wifi, m->aps[i].ssid,
                               m->psk[0] ? m->psk : NULL);
    }
    if (strcmp(id, "connect") == 0 && arg)
        return bd_wifi_connect(m->wifi, arg, m->psk[0] ? m->psk : NULL);
    return -1;
}
