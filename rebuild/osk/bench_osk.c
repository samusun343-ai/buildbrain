/* bench_osk - host measurements for OSK v2 + libbrainime.
 *
 * Reports:
 *   - VmRSS at each lifecycle stage (process start, engine open,
 *     OSK session live, after a burst of conversions).  The
 *     dictionary is mmap'd read-only, so the engine's RSS growth
 *     is bounded by the pages actually touched.
 *   - Reconversion latency: tk_osk_last_convert_us() sampled over
 *     fresh OSK widgets (the value the OSK itself measured around
 *     session_set_reading).
 *   - Candidate-display latency: wall time from the start of
 *     tk_osk_input_kana() until the layout (candidate bar cells)
 *     is built — i.e. key input to candidates being drawable.
 *
 * Usage: bench_osk <dict.bidf> [sidecar.bisc]
 */
#define _POSIX_C_SOURCE 200809L
#include "tk.h"
#include "osk.h"
#include "brainime.h"
#include "osk_brainime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld kB", &v) == 1) break;
    }
    fclose(f);
    return v;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s dict.bidf [sidecar.bisc]\n", argv[0]);
        return 2;
    }
    if (argc >= 3) setenv("BI_SIDECAR", argv[2], 0);
    printf("rss start:            %ld kB\n", rss_kb());

    bi_engine *eng = bi_engine_open(argv[1]);
    if (!eng) { fprintf(stderr, "engine open failed\n"); return 1; }
    bi_stats st;
    bi_engine_stats(eng, &st);
    printf("engine: %u entries, dict %zu bytes, lexicalized=%d\n",
           st.n_entries, st.file_bytes, st.lexicalized);
    printf("rss after engine:     %ld kB\n", rss_kb());

    /* fixture */
    tk_widget *top = tk_fixed_new();
    tk_widget *tf = tk_textfield_new("");
    tk_set_fixed_rect(tf, (tk_rect){ 0, 0, 480, 44 });
    tk_widget *osk = tk_osk_new();
    tk_set_fixed_rect(osk, (tk_rect){ 0, 44, 480, 700 });
    tk_add_child(top, tf);
    tk_add_child(top, osk);
    tk_osk_attach(osk, tf);
    tk_osk_set_backend(osk, osk_brainime_backend(), eng);
    tk_root *root = tk_root_new(top);
    tk_root_layout(root, 480, 800);
    printf("rss after osk live:   %ld kB\n", rss_kb());

    /* conversion latency over fresh readings */
    static const char *const readings[] = {
        "きょうはてんき", "てがみをかきました", "わたしはがくせいです",
        "あしたのよてい", "でんしゃにのります", "ありがとうございます",
    };
    enum { NREAD = 6, NITER = 30 };
    long worst = 0;
    double sum = 0;
    int cnt = 0;
    double disp_sum = 0, disp_worst = 0;
    osk_cell cells[256];
    for (int it = 0; it < NITER; it++) {
        for (int i = 0; i < NREAD; i++) {
            /* reset the field + osk by committing everything and
             * clearing the field text */
            while (tk_osk_reading(osk)[0]) tk_osk_commit(osk);
            tk_textfield_set_text(tf, "");
            double t0 = now_ms();
            tk_osk_input_kana(osk, readings[i]);
            tk_osk_layout(osk, cells, 256); /* candidate bar built */
            double dt = now_ms() - t0;
            disp_sum += dt;
            if (dt > disp_worst) disp_worst = dt;
            long us = tk_osk_last_convert_us(osk);
            sum += us;
            if (us > worst) worst = us;
            cnt++;
        }
    }
    printf("rss after %d convs:  %ld kB\n", cnt, rss_kb());
    printf("reconvert (session_set_reading): avg %.1f us, worst %ld us"
           " over %d\n", cnt ? sum / cnt : 0.0, worst, cnt);
    printf("input->candidates drawable:      avg %.3f ms, worst %.3f ms"
           " over %d\n", cnt ? disp_sum / cnt : 0.0, disp_worst, cnt);

    tk_root_free(root);
    tk_widget_free(top);
    bi_engine_close(eng);
    printf("rss after teardown:   %ld kB\n", rss_kb());
    return 0;
}
