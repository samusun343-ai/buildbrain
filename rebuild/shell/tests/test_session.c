/* test_session - end-to-end tests for the M3 session runtime
 * (src/session.c): the braindesk2 main loop that runs the shell
 * UI, the WM server, the assist overlay wiring, the hardware
 * backends and evdev input as one process.
 *
 * Verifies:
 *   - the first frame transfers bytes; quiet frames transfer
 *     exactly ZERO bytes (damage-driven presentation)
 *   - backend sync (mock) and Wi-Fi degradation (dead supplicant)
 *   - catalog refresh with no changes reports 0
 *   - a real WM client process (tests/session_client.c) connects
 *     through the session's WM server, its window is composited
 *     onto the session canvas, and a settled desktop goes quiet
 *     again with the window up
 *   - key and pointer routing reach the focused window
 *   - shell action events drive the hardware state (volume)
 *   - the production evdev path: a physical key fed through the
 *     virtual backend is drained by the step and routed onward
 *   - server-initiated close ends the client cleanly
 */
#include "session.h"
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "osk.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void setup_reg(void)
{
    static char sysdir[256], userdir[256];
    char tmpl[] = "/tmp/bdsessXXXXXX";
    char *base = mkdtemp(tmpl);
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);
}

static bool mock_exec(void *ud, const de_entry *e, char *const argv[],
                      bool terminal)
{
    (void)ud; (void)e; (void)argv; (void)terminal;
    return true;
}

/* ---- child client plumbing ---- */

static char child_out[16384];
static size_t child_out_len;
static int child_fd = -1;

static void drain_child(void)
{
    char buf[1024];
    ssize_t n;
    while ((n = read(child_fd, buf, sizeof(buf))) > 0) {
        if (child_out_len + (size_t)n < sizeof(child_out)) {
            memcpy(child_out + child_out_len, buf, (size_t)n);
            child_out_len += (size_t)n;
            child_out[child_out_len] = 0;
        }
    }
}

/* Step the session (5 ms virtual ticks) until the child's output
 * contains needle, or the step budget runs out. Returns the total
 * framebuffer bytes transferred while waiting. */
static size_t pump_until(bd_session *s, const char *needle, int max_steps,
                         bool *found)
{
    size_t bytes = 0;
    *found = false;
    for (int i = 0; i < max_steps; i++) {
        bytes += bd_session_step(s, 5);
        drain_child();
        if (needle && strstr(child_out, needle)) { *found = true; break; }
    }
    drain_child();
    if (needle && strstr(child_out, needle)) *found = true;
    return bytes;
}

/* Count dark (text-like) pixels in a canvas rect. */
static int count_dark(tk_canvas *cv, tk_rect r)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) {
            tk_color p = tk_canvas_pixel(cv, x, y);
            if (p.r < 110 && p.g < 110 && p.b < 110) n++;
        }
    return n;
}

/* OSK scenario helpers (used by the IM-client section in main) */

/* One tap (down+up) at screen coords through the session's
 * real input path. */
static void tap_at(bd_session *s, int x, int y)
{
    tk_event d = {0};
    d.type = TK_EV_POINTER_DOWN;
    d.x = x;
    d.y = y;
    (void)bd_session_event(s, &d);
    tk_event u = {0};
    u.type = TK_EV_POINTER_UP;
    u.x = x;
    u.y = y;
    (void)bd_session_event(s, &u);
}

/* Find the LAST "<tag><a> <b> <c> <d>" quad in the child output
 * (the IM client reprints its rects on resize; the newest wins). */
static bool last_quad(const char *tag, int *a, int *b, int *c, int *d)
{
    const char *p = child_out, *hit = NULL;
    size_t tl = strlen(tag);
    while ((p = strstr(p, tag)) != NULL) { hit = p; p += tl; }
    if (!hit) return false;
    return sscanf(hit + tl, "%d %d %d %d", a, b, c, d) == 4;
}

/* ---- mascot settings wiring: synthetic two-size pack ---------
 * A v1 .bdm with TWO size records (heights 160 and 240) sharing
 * one frames/poses/atlas set. The session picks the record
 * nearest to the requested height, which makes the brainreg
 * "mascot"/"size" value observable through bd_mascot_size():
 * a settings value of 168 must land on 160, 248 on 240. The
 * writer follows the FORMAT.md v1 layout independently of
 * test_mascot's (same approach, different geometry). */
static void sput16(uint8_t *p, size_t off, uint16_t v)
{
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)(v >> 8);
}
static void sput32(uint8_t *p, size_t off, uint32_t v)
{
    sput16(p, off, (uint16_t)v);
    sput16(p, off + 2, (uint16_t)(v >> 16));
}

static char *write_session_pack(void)
{
    const char *names = "\0Stand\0Walk\0Fall\0Drag\0"; /* 22 */
    size_t strtab_off = 80;
    size_t clips_off = strtab_off + 22;
    size_t pool_off = clips_off + 4 * 16;
    size_t sizes_off = pool_off + 2 * 4;
    size_t frames_off = sizes_off + 2 * 24;
    size_t poses_off = frames_off + 2 * 12;
    size_t atlas_off = poses_off + 5 * 12;
    size_t total = atlas_off + 64 * 64 * 4;
    uint8_t *b = calloc(1, total);
    if (!b) return NULL;
    memcpy(b, "BDM1", 4);
    sput16(b, 4, 1);
    sput16(b, 6, 1);
    sput32(b, 8, (uint32_t)total);
    memcpy(b + 12, "SessChar", 8);
    sput16(b, 36, 32);          /* crop w */
    sput16(b, 38, 40);          /* crop h */
    sput16(b, 40, 4);           /* clips */
    sput16(b, 42, 2);           /* pool */
    sput16(b, 44, 2);           /* sizes = 2 */
    sput16(b, 48, 2);           /* fall */
    sput16(b, 50, 3);           /* drag */
    sput16(b, 52, 2);           /* thrown */
    sput16(b, 54, 0);           /* stand */
    sput32(b, 56, (uint32_t)clips_off);
    sput32(b, 60, (uint32_t)pool_off);
    sput32(b, 64, (uint32_t)sizes_off);
    sput32(b, 68, (uint32_t)strtab_off);
    memcpy(b + strtab_off, names, 22);
    static const struct {
        uint16_t name, np, tmin, tmax;
        uint32_t start;
        uint8_t type, border, loop;
    } cm[4] = {
        { 1, 1, 10000, 10000, 0, 0, 1, 1 },
        { 7, 2, 480, 480, 1, 1, 1, 1 },
        { 12, 1, 400, 400, 3, 2, 0, 1 },
        { 17, 1, 10000, 10000, 4, 0, 1, 1 },
    };
    for (int i = 0; i < 4; i++) {
        uint8_t *p = b + clips_off + (size_t)i * 16;
        sput16(p, 0, cm[i].name);
        sput16(p, 2, cm[i].np);
        sput16(p, 4, cm[i].tmin);
        sput16(p, 6, cm[i].tmax);
        sput32(p, 8, cm[i].start);
        p[12] = cm[i].type;
        p[13] = cm[i].border;
        p[14] = cm[i].loop;
    }
    sput16(b, pool_off + 0, 0); sput16(b, pool_off + 2, 100);
    sput16(b, pool_off + 4, 1); sput16(b, pool_off + 6, 60);
    for (int r = 0; r < 2; r++) {
        uint8_t *sr = b + sizes_off + (size_t)r * 24;
        sput16(sr, 0, r == 0 ? 160 : 240);   /* height */
        sput16(sr, 2, 64);                   /* atlas w */
        sput16(sr, 4, 64);                   /* atlas h */
        sput16(sr, 6, 2);                    /* frames */
        sput32(sr, 8, (uint32_t)frames_off);
        sput32(sr, 12, (uint32_t)poses_off);
        sput32(sr, 16, (uint32_t)atlas_off);
        sput16(sr, 20, r == 0 ? 1024 : 1536); /* scale q8 = H/40 */
    }
    sput16(b, frames_off + 0, 0);  sput16(b, frames_off + 2, 0);
    sput16(b, frames_off + 4, 32); sput16(b, frames_off + 6, 40);
    sput16(b, frames_off + 8, 0);  sput16(b, frames_off + 10, 0);
    sput16(b, frames_off + 12, 32); sput16(b, frames_off + 14, 0);
    sput16(b, frames_off + 16, 32); sput16(b, frames_off + 18, 40);
    sput16(b, frames_off + 20, 0);  sput16(b, frames_off + 22, 0);
    static const int16_t poses[5][6] = {
        { 0, 10000, 40, 100, 0, 0 },
        { 0, 240, 40, 100, -1280, 0 },
        { 1, 240, 40, 100, -1280, 0 },
        { 1, 400, 40, 100, 0, 0 },
        { 1, 10000, 40, 100, 0, 0 },
    };
    for (int i = 0; i < 5; i++) {
        uint8_t *p = b + poses_off + (size_t)i * 12;
        for (int j = 0; j < 6; j++)
            sput16(p, (size_t)j * 2, (uint16_t)poses[i][j]);
    }
    /* atlas_off is not 4-byte aligned in this synthetic pack;
     * write through memcpy (the loader reads the atlas the
     * same way - a direct uint32_t* store is UB and would
     * fault on the ARMv5 target) */
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t red = 0xFFFF0000u, half = 0x80008000u;
            memcpy(b + atlas_off + ((size_t)y * 64 + x) * 4,
                   &red, 4);
            memcpy(b + atlas_off + ((size_t)y * 64 + 32 + x) * 4,
                   &half, 4);
        }
    char tmpl[] = "/tmp/bdsesspackXXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(b); return NULL; }
    if (write(fd, b, total) != (ssize_t)total) { /* ignore */ }
    close(fd);
    free(b);
    return strdup(tmpl);
}

static bd_session *mascot_test_session(de_catalog *cat, ln_store *store)
{
    static int seq;
    char wmp[256], ntp[256];
    snprintf(wmp, sizeof(wmp), "/tmp/bdmsc-wm-%ld-%d.sock",
             (long)getpid(), seq);
    snprintf(ntp, sizeof(ntp), "/tmp/bdmsc-nt-%ld-%d.sock",
             (long)getpid(), seq);
    seq++;
    bd_session_config c2 = { 0 };
    c2.width = 800;
    c2.height = 480;
    c2.catalog = cat;
    c2.store = store;
    c2.exec_fn = mock_exec;
    c2.wm_path = wmp;
    c2.notify_path = ntp;
    return bd_session_new(&c2);
}

static bd_session *alarmd_test_session(de_catalog *cat, ln_store *store,
                                       const char *alarmd_path)
{
    static int seq;
    char wmp[256], ntp[256];
    snprintf(wmp, sizeof(wmp), "/tmp/bdald-wm-%ld-%d.sock",
             (long)getpid(), seq);
    snprintf(ntp, sizeof(ntp), "/tmp/bdald-nt-%ld-%d.sock",
             (long)getpid(), seq);
    seq++;
    bd_session_config c2 = { 0 };
    c2.width = 800;
    c2.height = 480;
    c2.catalog = cat;
    c2.store = store;
    c2.exec_fn = mock_exec;
    c2.wm_path = wmp;
    c2.notify_path = ntp;
    c2.alarmd_path = alarmd_path;
    return bd_session_new(&c2);
}

/* Poll a file until it contains needle (or the budget runs out). */
static bool wait_for_text(const char *path, const char *needle,
                          int timeout_ms)
{
    char buf[512];
    for (int waited = 0; waited <= timeout_ms; waited += 10) {
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            buf[n] = 0;
            if (strstr(buf, needle)) return true;
        }
        usleep(10000);
    }
    return false;
}

int main(void)
{
    setup_reg();

    /* --- product font (M4): with no font installed the toolkit
     * paints NO text at all (the fallback metrics exist for
     * headless tests only). A bare label root therefore renders
     * zero glyph pixels. The product entry - bd_session_new via
     * bd_shell_new - must install the bundled product font by
     * itself, so the very same screen renders text without the
     * embedder doing anything. --- */
    CHECK(tk_font_current() == NULL);
    {
        tk_widget *lbl = tk_label_new("アプリ");
        tk_root *r = tk_root_new(lbl);
        tk_root_layout(r, 800, 480);
        tk_canvas *tcv = tk_canvas_new(800, 480);
        tk_root_paint(r, tcv, tk_theme_default());
        CHECK(count_dark(tcv, tk_widget_rect(lbl)) == 0);
        tk_canvas_free(tcv);
        tk_root_free(r);
        tk_widget_free(lbl);
    }

    de_catalog *cat = de_catalog_new(NULL);
    de_catalog_load_dir(cat, "tests/fixtures/applications");
    ln_store *store = ln_store_new();

    char wmdir[] = "/tmp/bdwmsessXXXXXX";
    CHECK(mkdtemp(wmdir) != NULL);
    char wmpath[256];
    snprintf(wmpath, sizeof(wmpath), "%s/wm.sock", wmdir);

    tk_fb *fb = tk_fb_open_mem(800, 480, 16);
    CHECK(fb != NULL);
    tk_evdev *vinput = tk_evdev_new_virtual();
    CHECK(vinput != NULL);
    bd_backend *be = bd_backend_mock_new();
    bd_backend_mock_set(be, 50, 40, 80, false);
    /* a supplicant that does not exist: Wi-Fi must degrade silently */
    bd_wifi *wifi = bd_wifi_new("/nonexistent/wpa/wlan0");

    bd_session_config cfg = {0};
    cfg.width = 800;
    cfg.height = 480;
    cfg.catalog = cat;
    cfg.store = store;
    cfg.exec_fn = mock_exec;
    cfg.wm_path = wmpath;
    cfg.fb = fb;
    cfg.input_virtual = vinput;
    cfg.backend = be;
    cfg.wifi = wifi;
    bd_session *s = bd_session_new(&cfg);
    CHECK(s != NULL);
    if (!s) { printf("test_session: %d passed, %d failed\n", passes, fails); return 1; }
    bd_shell *sh = bd_session_shell(s);
    bd_wm_server *wm = bd_session_wm(s);
    CHECK(sh != NULL && wm != NULL && bd_session_canvas(s) != NULL);

    /* the product entry installed the font on its own */
    CHECK(tk_font_current() != NULL);

    /* --- frames: first paints, quiet frames transfer zero --- */
    size_t first = bd_session_step(s, 10);
    CHECK(first > 0);
    /* and the first frame actually contains rendered text */
    CHECK(count_dark(bd_session_canvas(s), (tk_rect){0, 0, 800, 480}) > 200);
    for (int i = 0; i < 5; i++)
        CHECK(bd_session_step(s, 10) == 0);

    /* --- backend sync + Wi-Fi degradation --- */
    bd_backend_mock_set(be, 33, 44, 66, true);
    bd_session_sync_backend(s);
    CHECK(bd_shell_brightness(sh) == 33);
    CHECK(bd_shell_volume(sh) == 44);
    CHECK(bd_shell_battery(sh) == 66);
    CHECK(bd_shell_poll_wifi(sh) == 0);
    CHECK(!bd_shell_wifi_connected(sh));

    /* --- catalog refresh: nothing changed --- */
    CHECK(bd_session_refresh_catalog(s) == 0);

    /* --- a real WM client through the session --- */
    int pipefd[2];
    CHECK(pipe(pipefd) == 0);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl("./session_client", "session_client", wmpath, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    child_fd = pipefd[0];
    fcntl(child_fd, F_SETFL, O_NONBLOCK);

    bool found = false;
    (void)pump_until(s, "READY", 100, &found);
    CHECK(found);
    CHECK(bd_wm_server_count(wm) == 1);
    uint32_t win_id = bd_wm_server_focused(wm);
    CHECK(win_id != 0);
    bd_wm_win_info info;
    CHECK(bd_wm_server_info_id(wm, win_id, &info));
    CHECK(strcmp(info.title, "Session App") == 0);

    /* the committed window content appears on the session canvas */
    size_t xfer = pump_until(s, "COMMITTED", 100, &found);
    CHECK(found);
    CHECK(xfer > 0);
    tk_canvas *cv = bd_session_canvas(s);
    int cx = info.content.x + info.content.w / 2;
    int cy = info.content.y + info.content.h / 2;
    tk_color px = tk_canvas_pixel(cv, cx, cy);
    CHECK(px.r == 200 && px.g == 30 && px.b == 40);

    /* settled again: window up, nothing moving, zero transfer */
    for (int i = 0; i < 3; i++)
        CHECK(bd_session_step(s, 5) == 0);

    /* --- routing: keys go to the focused window --- */
    tk_event kev = {0};
    kev.type = TK_EV_KEY_DOWN;
    kev.key = TK_KEY_ENTER;
    CHECK(bd_session_event(s, &kev));
    {
        char needle[32];
        snprintf(needle, sizeof(needle), "KEY key=%d", TK_KEY_ENTER);
        (void)pump_until(s, needle, 60, &found);
        CHECK(found);
    }

    /* --- routing: pointer into the window content --- */
    tk_event pev = {0};
    pev.type = TK_EV_POINTER_DOWN;
    pev.x = cx;
    pev.y = cy;
    CHECK(bd_session_event(s, &pev));
    (void)pump_until(s, "POINTER", 60, &found);
    CHECK(found);

    /* --- shell action routing (direct event injection) --- */
    int vol0 = bd_shell_volume(sh);
    tk_event aev = {0};
    aev.type = TK_EV_ACTION;
    aev.action = TK_ACTION_VOLUME_UP;
    CHECK(bd_session_event(s, &aev));
    CHECK(bd_shell_volume(sh) == vol0 + 10);

    /* mute toggles through the same action path (the physical
     * route is symbol+Space -> KEY_MUTE -> TK_ACTION_MUTE in
     * tk_kbd; the evdev side is an integration-phase evtest
     * item). The set level survives a mute/unmute round trip. */
    tk_event mev = {0};
    mev.type = TK_EV_ACTION;
    mev.action = TK_ACTION_MUTE;
    int mvol = bd_shell_volume(sh);
    CHECK(!bd_shell_muted(sh));
    CHECK(bd_session_event(s, &mev));
    CHECK(bd_shell_muted(sh));
    CHECK(bd_shell_volume(sh) == mvol);
    CHECK(bd_shell_effective_volume(sh) == 0);
    CHECK(bd_session_event(s, &mev));
    CHECK(!bd_shell_muted(sh));
    CHECK(bd_shell_effective_volume(sh) == mvol);

    /* a volume key while muted unmutes and steps (the shell's
     * unmute-on-adjust rule, reached here via the session) */
    CHECK(bd_session_event(s, &mev));
    CHECK(bd_shell_muted(sh));
    CHECK(bd_session_event(s, &aev));
    CHECK(!bd_shell_muted(sh));
    CHECK(bd_shell_volume(sh) == mvol + 10);

    /* --- production evdev path: a physical text key fed through
     * the virtual backend is drained by the session step and
     * routed to the focused window. (The corrected Brain key
     * table has no volume keys - volume is a shell/assist
     * function - so the evdev path is verified with a letter key,
     * and actions are verified by direct injection above.) The
     * virtual backend queues ONE raw event: feed, step to drain,
     * then feed the release. --- */
    CHECK(tk_evdev_feed_key(vinput, KEY_A, 1, 0));
    (void)bd_session_step(s, 10);
    CHECK(tk_evdev_feed_key(vinput, KEY_A, 0, 0));
    (void)bd_session_step(s, 10);
    (void)pump_until(s, "text=a", 60, &found);
    CHECK(found);

    /* --- server-initiated close --- */
    CHECK(bd_wm_server_close(wm, win_id));
    (void)pump_until(s, "CLOSED", 100, &found);
    CHECK(found);
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    for (int i = 0; i < 10 && bd_wm_server_count(wm) != 0; i++)
        (void)bd_session_step(s, 5);
    CHECK(bd_wm_server_count(wm) == 0);

    /* --- OSK: the system-wide on-screen keyboard over a real
     * WM client (tests/im_client.c). The client is a toolkit
     * window with a text field; tapping the field must raise
     * the shell OSK above the window, OSK keys must reach the
     * field (composition, then commit), physical typing must
     * dismiss it, a covered field must move its window up, and
     * a fullscreen window must shrink above the strip and
     * restore exactly. --- */
    {
        close(child_fd);
        child_out_len = 0;
        child_out[0] = 0;
        int pfd2[2];
        CHECK(pipe(pfd2) == 0);
        pid_t pid2 = fork();
        if (pid2 == 0) {
            dup2(pfd2[1], STDOUT_FILENO);
            close(pfd2[0]);
            close(pfd2[1]);
            execl("./im_client", "im_client", wmpath, "400", "300",
                  (char *)NULL);
            _exit(127);
        }
        close(pfd2[1]);
        child_fd = pfd2[0];
        fcntl(child_fd, F_SETFL, O_NONBLOCK);

        bool f2 = false;
        (void)pump_until(s, "READY", 100, &f2);
        CHECK(f2);
        /* the rect lines follow READY on their own flush */
        (void)pump_until(s, "BTNRECT", 100, &f2);
        CHECK(f2);
        CHECK(bd_wm_server_count(wm) == 1);
        uint32_t imid = bd_wm_server_focused(wm);
        bd_wm_win_info im;
        CHECK(bd_wm_server_info_id(wm, imid, &im));
        CHECK(strcmp(im.title, "IM App") == 0);
        CHECK(!bd_shell_osk_open(sh));
        CHECK(bd_wm_server_bottom_inset(wm) == 0);

        /* last FIELD / BTNRECT quad printed by the client */
        #define LASTQUAD(tag, a, b, c, d) \
            last_quad(tag, a, b, c, d)
        int fx = 0, fy = 0, fw = 0, fh = 0;
        CHECK(LASTQUAD("FIELD ", &fx, &fy, &fw, &fh));
        CHECK(fw > 100 && fh > 20);

        /* tap the field: OSK opens above the window */
        tap_at(s, im.content.x + fx + fw / 2, im.content.y + fy + fh / 2);
        bool up = false;
        size_t open_bytes = 0, open_frame = 0;
        for (int i = 0; i < 900 && !up; i++) {
            size_t b = bd_session_step(s, 5);
            open_bytes += b;
            drain_child();
            if (bd_shell_osk_open(sh)) { up = true; open_frame = b; }
        }
        CHECK(up);
        CHECK(open_bytes > 0);
        tk_rect oskr = bd_shell_osk_rect(sh);
        CHECK(oskr.x == 0 && oskr.w == 800);
        CHECK(oskr.y + oskr.h == 480);
        CHECK(oskr.h == 272); /* cand 48 + 3x56 QWERTY + bar 56 */
        CHECK(bd_wm_server_bottom_inset(wm) == 272);
        /* the OSK strip is really painted on the session canvas:
         * key glyphs/outlines are dark on the light key faces */
        CHECK(count_dark(bd_session_canvas(s), oskr) > 200);
        /* settles to quiet with the OSK up (late client frames
         * may land for a step or two after the open) */
        {
            int zeros = 0;
            for (int i = 0; i < 200 && zeros < 3; i++)
                zeros = bd_session_step(s, 5) == 0 ? zeros + 1 : 0;
            CHECK(zeros == 3);
        }

        /* type 'a' on the OSK: the client field gets the
         * composition, then the commit */
        osk_cell cells[128];
        tk_widget *oskw = bd_shell_osk_widget(sh);
        int nc = tk_osk_layout(oskw, cells, 128);
        tk_rect ka = { 0, 0, 0, 0 }, kc = { 0, 0, 0, 0 };
        for (int i = 0; i < nc; i++) {
            if (cells[i].kind == OSK_CELL_LETTER &&
                strcmp(cells[i].text, "a") == 0)
                ka = cells[i].r;
            if (cells[i].kind == OSK_CELL_FUNC &&
                cells[i].id == OSK_FN_COMMIT)
                kc = cells[i].r;
        }
        CHECK(ka.w > 0 && kc.w > 0);
        tap_at(s, ka.x + ka.w / 2, ka.y + ka.h / 2);
        /* the first frame after the tap carries the key press +
         * candidate bar update (measurement) */
        size_t key_frame = bd_session_step(s, 5);
        (void)pump_until(s, "COMP あ", 200, &f2);
        CHECK(f2);
        /* one settled keystroke's transfer (measurement) */
        size_t key_bytes = 0;
        tap_at(s, kc.x + kc.w / 2, kc.y + kc.h / 2);
        (void)pump_until(s, "TEXT あ", 200, &f2);
        CHECK(f2);
        for (int i = 0; i < 3; i++) key_bytes += bd_session_step(s, 5);
        printf("osk: open transfer %zu B (final frame %zu B), "
               "key frame %zu B, commit settle %zu B\n",
               open_bytes, open_frame, key_frame, key_bytes);

        /* physical typing dismisses the OSK; the latch keeps it
         * down while the same field stays focused */
        tk_event phys = {0};
        phys.type = TK_EV_KEY_DOWN;
        phys.key = TK_KEY_NONE;
        phys.text = "x";
        CHECK(bd_session_event(s, &phys));
        (void)pump_until(s, "TEXT あx", 200, &f2);
        CHECK(f2);
        CHECK(!bd_shell_osk_open(sh));
        CHECK(bd_wm_server_bottom_inset(wm) == 0);
        for (int i = 0; i < 20; i++) (void)bd_session_step(s, 5);
        CHECK(!bd_shell_osk_open(sh)); /* latch held */

        /* focus cycle reopens it: tap the button (focus leaves
         * the field), then the field again */
        int bx = 0, by = 0, bw = 0, bh = 0;
        CHECK(LASTQUAD("BTNRECT ", &bx, &by, &bw, &bh));
        CHECK(bd_wm_server_info_id(wm, imid, &im));
        tap_at(s, im.content.x + bx + bw / 2, im.content.y + by + bh / 2);
        (void)pump_until(s, "BTN", 200, &f2);
        CHECK(f2);
        tap_at(s, im.content.x + fx + fw / 2, im.content.y + fy + fh / 2);
        up = false;
        for (int i = 0; i < 900 && !up; i++) {
            (void)bd_session_step(s, 5);
            drain_child();
            up = bd_shell_osk_open(sh);
        }
        CHECK(up);

        /* covered field: close, move the window down so the
         * field sits under the strip, tap it - the window must
         * move up until the field is fully above the strip */
        CHECK(bd_wm_server_info_id(wm, imid, &im));
        tap_at(s, im.content.x + bx + bw / 2, im.content.y + by + bh / 2);
        for (int i = 0; i < 900 && bd_shell_osk_open(sh); i++) {
            (void)bd_session_step(s, 5);
            drain_child();
        }
        CHECK(!bd_shell_osk_open(sh));
        CHECK(bd_wm_server_info_id(wm, imid, &im));
        CHECK(bd_wm_server_move(wm, imid, im.frame.x, 300));
        CHECK(bd_wm_server_info_id(wm, imid, &im));
        CHECK(im.frame.y == 300);
        int field_bottom = im.content.y + fy + fh;
        CHECK(field_bottom > 480 - 272); /* covered by the strip */
        tap_at(s, im.content.x + fx + fw / 2, im.content.y + fy + fh / 2);
        up = false;
        for (int i = 0; i < 900 && !up; i++) {
            (void)bd_session_step(s, 5);
            drain_child();
            up = bd_shell_osk_open(sh);
        }
        CHECK(up);
        CHECK(bd_wm_server_info_id(wm, imid, &im));
        CHECK(im.content.y + fy + fh <= 480 - 272);
        CHECK(im.frame.y == 300 - (field_bottom - (480 - 272)));

        /* fullscreen: the window shrinks above the strip while
         * the OSK is up and restores exactly when it closes */
        child_out_len = 0; child_out[0] = 0;
        CHECK(bd_wm_server_set_state(wm, imid, BD_WIN_FULLSCREEN));
        (void)pump_until(s, "RESIZED 800 208", 300, &f2);
        CHECK(f2);
        /* server-side geometry lands with the client's commit */
        for (int i = 0; i < 400; i++) {
            (void)bd_session_step(s, 5);
            if (bd_wm_server_info_id(wm, imid, &im) &&
                im.content.w == 800 && im.content.h == 480 - 272)
                break;
        }
        CHECK(im.content.w == 800 && im.content.h == 480 - 272);
        CHECK(bd_shell_osk_open(sh));
        CHECK(LASTQUAD("BTNRECT ", &bx, &by, &bw, &bh));
        tap_at(s, im.content.x + bx + bw / 2, im.content.y + by + bh / 2);
        (void)pump_until(s, "RESIZED 800 480", 300, &f2);
        CHECK(f2);
        CHECK(!bd_shell_osk_open(sh));
        /* the server applies the restored size once the client
         * commits a frame at it (the client is a separate
         * process; give it a generous window) */
        for (int i = 0; i < 400; i++) {
            (void)bd_session_step(s, 5);
            if (bd_wm_server_info_id(wm, imid, &im) &&
                im.content.w == 800 && im.content.h == 480)
                break;
        }
        CHECK(im.content.w == 800 && im.content.h == 480);

        /* close the IM client */
        CHECK(bd_wm_server_close(wm, imid));
        (void)pump_until(s, "CLOSED", 100, &f2);
        CHECK(f2);
        int st2 = 0;
        CHECK(waitpid(pid2, &st2, 0) == pid2);
        CHECK(WIFEXITED(st2) && WEXITSTATUS(st2) == 0);
        for (int i = 0; i < 10 && bd_wm_server_count(wm) != 0; i++)
            (void)bd_session_step(s, 5);
        CHECK(bd_wm_server_count(wm) == 0);
        #undef LASTQUAD
    }

    /* --- Android-mode navigation, end to end: the shell's nav
     * actions reach the real WM through the session's sinks.
     * Back must arrive at the focused client as TK_KEY_BACK;
     * Home must minimize its window; Recents opens and is
     * consumed by Back like any shell panel. --- */
    {
        close(child_fd);
        child_out_len = 0;
        child_out[0] = 0;
        int pfd3[2];
        CHECK(pipe(pfd3) == 0);
        pid_t pid3 = fork();
        if (pid3 == 0) {
            dup2(pfd3[1], STDOUT_FILENO);
            close(pfd3[0]);
            close(pfd3[1]);
            /* session_client: the raw client that logs the
             * keys it receives (im_client forwards them to its
             * widgets and logs nothing) */
            execl("./session_client", "session_client", wmpath,
                  (char *)NULL);
            _exit(127);
        }
        close(pfd3[1]);
        child_fd = pfd3[0];
        fcntl(child_fd, F_SETFL, O_NONBLOCK);
        bool f3 = false;
        (void)pump_until(s, "READY", 100, &f3);
        CHECK(f3);
        CHECK(bd_wm_server_count(wm) == 1);
        uint32_t nid = bd_wm_server_focused(wm);
        bd_wm_win_info nw;
        CHECK(bd_wm_server_info_id(wm, nid, &nw));

        /* no shell panel may eat the first Back */
        bd_shell_set_quick_open(sh, false);
        bd_shell_show_launcher(sh, false);
        bd_shell_set_recents_open(sh, false);
        bd_shell_notify_dismiss(sh);

        bd_shell_nav(sh, BD_NAV_BACK);
        char needle[64];
        snprintf(needle, sizeof(needle), "KEY key=%d", TK_KEY_BACK);
        (void)pump_until(s, needle, 200, &f3);
        CHECK(f3);

        bd_shell_nav(sh, BD_NAV_HOME);
        for (int i = 0; i < 50; i++) {
            (void)bd_session_step(s, 5);
            if (bd_wm_server_info_id(wm, nid, &nw) &&
                nw.state == BD_WIN_MINIMIZED)
                break;
        }
        CHECK(nw.state == BD_WIN_MINIMIZED);

        bd_shell_nav(sh, BD_NAV_RECENTS);
        CHECK(bd_shell_recents_open(sh));
        (void)bd_session_step(s, 5);
        bd_shell_nav(sh, BD_NAV_BACK);
        CHECK(!bd_shell_recents_open(sh));

        CHECK(bd_wm_server_close(wm, nid));
        (void)pump_until(s, "CLOSED", 100, &f3);
        CHECK(f3);
        int st3 = 0;
        CHECK(waitpid(pid3, &st3, 0) == pid3);
        for (int i = 0; i < 10 && bd_wm_server_count(wm) != 0; i++)
            (void)bd_session_step(s, 5);
        CHECK(bd_wm_server_count(wm) == 0);
    }

    /* --- mascot settings wiring, end to end (phase 1 -> 2):
     * the settings app writes brainreg section "mascot" (the
     * exact reg_set calls setmodel.c makes); a session created
     * afterwards must act on those values. Live reconfiguration
     * of a running session is out of scope by design (the
     * settings page says so). --- */
    {
        char *pack = write_session_pack();
        CHECK(pack != NULL);
        CHECK(reg_set("mascot", "pack", pack) == 0);
        CHECK(reg_set("mascot", "size", "168") == 0);

        /* disabled in settings -> no mascot in the session */
        CHECK(reg_set("mascot", "enabled", "false") == 0);
        bd_session *ms = mascot_test_session(cat, store);
        CHECK(ms != NULL);
        CHECK(bd_session_mascot(ms) == NULL);
        bd_session_free(ms);

        /* enabled + pack + size 168 -> the 160 size record */
        CHECK(reg_set("mascot", "enabled", "true") == 0);
        ms = mascot_test_session(cat, store);
        CHECK(ms != NULL);
        CHECK(bd_session_mascot(ms) != NULL);
        CHECK(bd_mascot_size(bd_session_mascot(ms)) == 160);
        bd_session_free(ms);

        /* size 248 -> the 240 record: the written value, not a
         * default, drives the pick */
        CHECK(reg_set("mascot", "size", "248") == 0);
        ms = mascot_test_session(cat, store);
        CHECK(ms != NULL);
        CHECK(bd_session_mascot(ms) != NULL);
        CHECK(bd_mascot_size(bd_session_mascot(ms)) == 240);
        bd_session_free(ms);

        /* fullscreen has no public getter on the session (it is
         * consumed at creation into the visibility policy); what
         * is observable here is the settings-side round trip of
         * the very key the session reads, plus a session that
         * still builds its mascot with the flag set */
        CHECK(reg_set("mascot", "fullscreen", "true") == 0);
        CHECK(reg_get_bool("mascot", "fullscreen", false) == true);
        ms = mascot_test_session(cat, store);
        CHECK(ms != NULL);
        CHECK(bd_session_mascot(ms) != NULL);
        bd_session_free(ms);

        /* a pack path that does not exist: feature off */
        CHECK(reg_set("mascot", "pack", "/nonexistent/none.bdm") == 0);
        ms = mascot_test_session(cat, store);
        CHECK(ms != NULL);
        CHECK(bd_session_mascot(ms) == NULL);
        bd_session_free(ms);

        reg_unset("mascot", "pack");
        reg_unset("mascot", "size");
        reg_unset("mascot", "enabled");
        reg_unset("mascot", "fullscreen");
        unlink(pack);
        free(pack);
    }

    /* --- bd-alarmd helper wiring (integration prep): the
     * session spawns the resident alarm evaluator at creation,
     * reaps it when it exits on its own, and terminates it at
     * free. Disabled / missing helpers are not fatal. --- */
    {
        char adir[] = "/tmp/bdalarmdXXXXXX";
        CHECK(mkdtemp(adir) != NULL);
        char marker[256], helper[256];
        snprintf(marker, sizeof(marker), "%s/marker", adir);
        snprintf(helper, sizeof(helper), "%s/fake-alarmd", adir);

        /* disabled: "" never spawns */
        bd_session *as = alarmd_test_session(cat, store, "");
        CHECK(as != NULL);
        CHECK(bd_session_alarmd_pid(as) == 0);
        bd_session_free(as);

        /* missing binary: session still builds, no helper */
        as = alarmd_test_session(cat, store, "/nonexistent/bd-alarmd");
        CHECK(as != NULL);
        CHECK(bd_session_alarmd_pid(as) == 0);
        bd_session_free(as);

        /* default path (NULL): on a host without the installed
         * daemon the session must run helper-less; where the
         * daemon IS installed it must come up and be torn down
         * by free (no assertion on the pid there). */
        as = alarmd_test_session(cat, store, NULL);
        CHECK(as != NULL);
        if (access("/usr/local/bin/bd-alarmd", X_OK) != 0)
            CHECK(bd_session_alarmd_pid(as) == 0);
        bd_session_free(as);

        /* a helper that exits at once (/bin/true): spawned, then
         * reaped by stepping; the session reports 0 afterwards */
        as = alarmd_test_session(cat, store, "/bin/true");
        CHECK(as != NULL);
        for (int i = 0; i < 300 && bd_session_alarmd_pid(as) > 0; i++) {
            bd_session_step(as, 5);
            usleep(10000);
        }
        CHECK(bd_session_alarmd_pid(as) == 0);
        bd_session_free(as);

        /* a long-lived helper: started at creation, terminated
         * (SIGTERM) and reaped by bd_session_free */
        FILE *hf = fopen(helper, "w");
        CHECK(hf != NULL);
        if (hf) {
            fprintf(hf, "#!/bin/sh\n"
                        "echo started >> %s\n"
                        "trap 'echo term >> %s; exit 0' TERM\n"
                        "while :; do sleep 1 & wait $!; done\n",
                    marker, marker);
            fclose(hf);
            chmod(helper, 0755);
            as = alarmd_test_session(cat, store, helper);
            CHECK(as != NULL);
            pid_t apid = bd_session_alarmd_pid(as);
            CHECK(apid > 0);
            CHECK(wait_for_text(marker, "started", 2000));
            bd_session_free(as);
            CHECK(wait_for_text(marker, "term", 3000));
            /* reaped by free: no zombie, no orphan */
            CHECK(kill(apid, 0) == -1 && errno == ESRCH);
            unlink(helper);
            unlink(marker);
        }
        rmdir(adir);
    }

    close(child_fd);
    bd_session_free(s);
    /* the shell owned the font it installed: freed with it */
    CHECK(tk_font_current() == NULL);
    tk_fb_close(fb);
    tk_evdev_close(vinput);
    bd_backend_free(be);
    bd_wifi_free(wifi);
    ln_store_free(store);
    de_catalog_free(cat);
    unlink(wmpath);
    rmdir(wmdir);

    printf("test_session: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
