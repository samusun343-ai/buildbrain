/* bench_mascot - mascot runtime performance measurements.
 *
 * Usage: bench_mascot <pack.bdm> [virtual_seconds] [size_px]
 *        [step_ms]
 *
 * Simulates the session loop (50 ms steps by default; 33 ms
 * for v2 packs, matching the embedder's animation cadence)
 * against a memory canvas: mascot tick + background repaint
 * of the damage + mascot paint, accumulating the 16bpp
 * transfer volume the session would flush. Reports damage
 * statistics, CPU time per step, RSS, and a blitter
 * micro-benchmark comparing the three candidate pixel
 * layouts for the atlas:
 *   (a) premultiplied ARGB8888 (the .bdm choice)
 *   (b) straight RGBA8888 (toolkit blend arithmetic)
 *   (c) RGB565 + separate 8-bit alpha
 * over a sprite-sized buffer with a realistic alpha mix
 * (~60% transparent / ~30% opaque / ~10% partial).
 */
#define _POSIX_C_SOURCE 200809L
#include "mascot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

static long rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[128];
    long v = -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "VmRSS: %ld kB", &v) == 1) break;
    fclose(f);
    return v;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pack.bdm> [seconds] "
                        "[size_px] [step_ms]\n", argv[0]);
        return 2;
    }
    int seconds = argc > 2 ? atoi(argv[2]) : 60;
    int want_size = argc > 3 ? atoi(argv[3]) : 208;
    int step_ms = argc > 4 ? atoi(argv[4]) : 50;
    if (step_ms <= 0) step_ms = 50;
    long rss0 = rss_kb();
    bd_mascot_config cfg = { 0 };
    cfg.pack_path = argv[1];
    cfg.size_px = want_size;
    cfg.floor_area = (tk_rect){ 0, 0, 800, 424 };
    cfg.seed = 777;
    bd_mascot *m = bd_mascot_new(&cfg);
    if (!m) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        return 1;
    }
    long rss1 = rss_kb();
    size_t mapb = 0;
    int nclips = 0, nframes = 0, npool = 0;
    bd_mascot_stats(m, &mapb, &nclips, &nframes, &npool);
    tk_canvas *cv = tk_canvas_new(800, 480);
    tk_canvas_clear(cv, (tk_color){ 16, 32, 48, 255 });

    long steps = seconds * 1000L / step_ms;
    long dmg_frames = 0;
    double dmg_px = 0, dmg_px_max = 0;
    size_t xfer = 0;
    long t0 = now_us();
    for (long i = 0; i < steps; i++) {
        long now = i * step_ms;
        tk_rect dmg[8];
        int nd = bd_mascot_tick(m, now, dmg, 8);
        if (nd > 0) {
            dmg_frames++;
            double step_px = 0;
            for (int j = 0; j < nd; j++) {
                dmg_px += (double)dmg[j].w * dmg[j].h;
                step_px += (double)dmg[j].w * dmg[j].h;
                xfer += (size_t)dmg[j].w * dmg[j].h * 2;
                tk_canvas_fill_rect(cv, dmg[j],
                                    (tk_color){ 16, 32, 48, 255 });
            }
            if (step_px > dmg_px_max) dmg_px_max = step_px;
            bd_mascot_paint(m, cv, dmg, (size_t)nd);
        }
    }
    long wall = now_us() - t0;
    long rss2 = rss_kb();
    tk_rect sr = bd_mascot_rect(m);
    printf("pack: %s (map %zu B, clips %d, frames %d, pool %d, "
           "size %d)\n", argv[1], mapb, nclips, nframes, npool,
           bd_mascot_size(m));
    printf("sim: %d s at %d ms steps, final state %d clip %s "
           "rect %dx%d\n", seconds, step_ms, bd_mascot_state(m),
           bd_mascot_clip_name(m), sr.w, sr.h);
    printf("damage frames: %ld/%ld (%.0f%%), avg %.0f px/frame, "
           "max %.0f px, transfer %.1f B/step avg, %.0f B max "
           "(16bpp)\n",
           dmg_frames, steps,
           100.0 * (double)dmg_frames / (double)steps,
           dmg_frames ? dmg_px / dmg_frames : 0,
           dmg_px_max,
           steps ? (double)xfer / steps : 0,
           dmg_px_max * 2);
    printf("cpu: %.1f us/step (tick+repaint+paint), rss %ld -> "
           "%ld -> %ld kB (load delta %ld kB, sim delta %ld kB)\n",
           (double)wall / (double)steps, rss0, rss1, rss2,
           rss1 - rss0, rss2 - rss1);

    /* ---- blitter format micro-benchmark ---- */
    const int W = 176, H = 208, N = W * H;
    uint32_t *dst = malloc((size_t)N * 4);
    uint32_t *src_pm = malloc((size_t)N * 4);
    uint8_t *src_a = malloc((size_t)N);
    uint16_t *src_565 = malloc((size_t)N * 2);
    for (int i = 0; i < N; i++) {
        int kind = i % 10;
        uint32_t a = kind < 6 ? 0 : (kind < 9 ? 255 : 128);
        uint32_t r = (uint32_t)(i * 7) & 0xFF, g = (uint32_t)(i * 3) & 0xFF,
                 b = (uint32_t)(i * 11) & 0xFF;
        src_pm[i] = (a << 24) | ((r * a / 255) << 16) |
                    ((g * a / 255) << 8) | (b * a / 255);
        src_a[i] = (uint8_t)a;
        src_565[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) |
                                (b >> 3));
        dst[i] = 0xFF102030u;
    }
    const int REPS = 300;
    volatile uint32_t sink = 0;
    long t;
    /* (a) premultiplied ARGB */
    t = now_us();
    for (int rep = 0; rep < REPS; rep++)
        for (int i = 0; i < N; i++) {
            uint32_t s = src_pm[i], a = s >> 24;
            if (!a) continue;
            if (a == 255) { dst[i] = s; continue; }
            uint32_t d = dst[i], ia = 255 - a;
            dst[i] = 0xFF000000u |
                     (((s >> 16 & 0xFF) + ((d >> 16 & 0xFF) * ia) / 255) << 16) |
                     (((s >> 8 & 0xFF) + ((d >> 8 & 0xFF) * ia) / 255) << 8) |
                     ((s & 0xFF) + ((d & 0xFF) * ia) / 255);
        }
    long t_pm = now_us() - t;
    sink += dst[N / 2];
    /* (b) straight RGBA, toolkit arithmetic */
    t = now_us();
    for (int rep = 0; rep < REPS; rep++)
        for (int i = 0; i < N; i++) {
            uint32_t pm = src_pm[i], a = pm >> 24;
            if (!a) continue;
            uint32_t sr = a == 255 ? (pm >> 16 & 0xFF)
                                   : ((pm >> 16 & 0xFF) * 255) / a;
            uint32_t sg = a == 255 ? (pm >> 8 & 0xFF)
                                   : ((pm >> 8 & 0xFF) * 255) / a;
            uint32_t sb = a == 255 ? (pm & 0xFF)
                                   : ((pm & 0xFF) * 255) / a;
            if (a == 255) {
                dst[i] = 0xFF000000u | (sr << 16) | (sg << 8) | sb;
                continue;
            }
            uint32_t d = dst[i], ia = 255 - a;
            dst[i] = 0xFF000000u |
                     ((sr * a + ((d >> 16 & 0xFF) * ia)) / 255 << 16) |
                     ((sg * a + ((d >> 8 & 0xFF) * ia)) / 255 << 8) |
                     ((sb * a + (d & 0xFF) * ia) / 255);
        }
    long t_rgba = now_us() - t;
    sink += dst[N / 3];
    /* (c) RGB565 + alpha8 */
    t = now_us();
    for (int rep = 0; rep < REPS; rep++)
        for (int i = 0; i < N; i++) {
            uint32_t a = src_a[i];
            if (!a) continue;
            uint32_t v = src_565[i];
            uint32_t sr = ((v >> 11) & 0x1F) * 255 / 31;
            uint32_t sg = ((v >> 5) & 0x3F) * 255 / 63;
            uint32_t sb = (v & 0x1F) * 255 / 31;
            if (a == 255) {
                dst[i] = 0xFF000000u | (sr << 16) | (sg << 8) | sb;
                continue;
            }
            uint32_t d = dst[i], ia = 255 - a;
            dst[i] = 0xFF000000u |
                     (((sr * a) / 255 + ((d >> 16 & 0xFF) * ia) / 255) << 16) |
                     (((sg * a) / 255 + ((d >> 8 & 0xFF) * ia) / 255) << 8) |
                     ((sb * a) / 255 + (d & 0xFF) * ia / 255);
        }
    long t_565 = now_us() - t;
    sink += dst[N / 4];
    printf("blit %dx%d x%d reps: premult %.0f Mpx/s | straight "
           "%.0f Mpx/s | 565+a %.0f Mpx/s (sink %u)\n",
           W, H, REPS,
           (double)N * REPS / t_pm * 1e6 / 1e6,
           (double)N * REPS / t_rgba * 1e6 / 1e6,
           (double)N * REPS / t_565 * 1e6 / 1e6, sink);
    free(dst); free(src_pm); free(src_a); free(src_565);
    tk_canvas_free(cv);
    bd_mascot_free(m);
    return 0;
}
