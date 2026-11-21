/* test_player - bd-player engine + model tests.
 *
 * Engine: WAV (generated in-test), MP3 and Ogg Vorbis
 * (committed fixtures with known ID3/Vorbis metadata) are
 * probed and pumped into a null sink; frame counts, metadata,
 * seek and software volume are checked against ground truth.
 * Model: recursive scan with a stat-checked metadata cache
 * (second scan must not re-probe), byte-budget eviction,
 * views/search/sort, playlist CRUD with persistence, the
 * queue/shuffle/repeat order machine, and playback-state
 * persistence.
 *
 * argv[1] = fixtures directory (player/tests/fixtures).
 */
#define _POSIX_C_SOURCE 200809L
#include "pengine.h"
#include "playmodel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("PASS "); } \
    else { fails++; printf("FAIL "); } \
    printf(__VA_ARGS__); putchar('\n'); } while (0)

static void write_wav(const char *path, int seconds, double freq)
{
    const int rate = 22050;
    FILE *f = fopen(path, "wb");
    uint32_t frames = (uint32_t)(rate * seconds);
    uint32_t data = frames * 2;
    uint8_t h[44] = {0};
    memcpy(h, "RIFF", 4);
    h[4] = (36 + data) & 0xFF; h[5] = ((36 + data) >> 8) & 0xFF;
    h[6] = ((36 + data) >> 16) & 0xFF; h[7] = ((36 + data) >> 24) & 0xFF;
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16;
    h[20] = 1; h[22] = 1;
    h[24] = rate & 0xFF; h[25] = (rate >> 8) & 0xFF;
    h[26] = (rate >> 16) & 0xFF; h[27] = (rate >> 24) & 0xFF;
    h[28] = (rate * 2) & 0xFF; h[29] = ((rate * 2) >> 8) & 0xFF;
    h[30] = ((rate * 2) >> 16) & 0xFF; h[31] = ((rate * 2) >> 24) & 0xFF;
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4);
    h[40] = data & 0xFF; h[41] = (data >> 8) & 0xFF;
    h[42] = (data >> 16) & 0xFF; h[43] = (data >> 24) & 0xFF;
    fwrite(h, 1, 44, f);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t v = (int16_t)(sin(2 * 3.14159265 * freq * i / rate) *
                              0.5 * 32767);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
}

static void copy_file(const char *src, const char *dst)
{
    FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
}

static uint64_t pump_all(pe_engine *e)
{
    uint64_t total = 0;
    int n;
    while ((n = pe_pump(e, 4096)) > 0) total += (uint64_t)n;
    return total;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s FIXTURES\n", argv[0]); return 2; }
    const char *fx = argv[1];
    char tmpl[] = "/tmp/bdplayer-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    char media[1100], sub[1100], p[1200], q[1200];
    snprintf(media, sizeof(media), "%s/media", tmpl);
    snprintf(sub, sizeof(sub), "%s/sub", media);
    mkdir(media, 0777);
    mkdir(sub, 0777);

    /* ---------- engine: WAV ---------- */
    snprintf(p, sizeof(p), "%s/tone.wav", media);
    write_wav(p, 1, 440);
    pe_meta meta;
    CHECK(pe_probe(p, &meta) == 0, "wav probe");
    CHECK(meta.rate == 22050 && meta.channels == 1, "wav format");
    CHECK(meta.duration_ms == 1000, "wav duration %u", meta.duration_ms);
    CHECK(strcmp(meta.title, "tone") == 0, "wav title from filename");
    {
        pe_out *out = pe_out_null(22050, 1);
        pe_engine *e = pe_open(p, out);
        CHECK(e != NULL, "wav open");
        uint64_t frames = pump_all(e);
        CHECK(frames == 22050, "wav frames %llu", (unsigned long long)frames);
        CHECK(pe_eof(e), "wav eof");
        int peak = pe_out_null_peak(out);
        CHECK(peak > 15000 && peak < 17000, "wav peak %d", peak);
        /* seek back to 500 ms and pump the rest */
        CHECK(pe_seek_ms(e, 500) == 0, "wav seek");
        CHECK(pe_position_ms(e) == 500, "wav position 500");
        frames = pump_all(e);
        CHECK(frames >= 11000 && frames <= 11050,
              "wav remaining frames %llu", (unsigned long long)frames);
        pe_close(e);
        pe_out_free(out);
        /* software volume halves the peak */
        out = pe_out_null(22050, 1);
        e = pe_open(p, out);
        pe_set_volume(e, 0.5);
        pump_all(e);
        peak = pe_out_null_peak(out);
        CHECK(peak > 7000 && peak < 9000, "volume peak %d", peak);
        pe_close(e);
        pe_out_free(out);
    }

    /* ---------- engine: MP3 fixture ---------- */
    snprintf(p, sizeof(p), "%s/fixture.mp3", fx);
    CHECK(pe_probe(p, &meta) == 0, "mp3 probe");
    CHECK(strcmp(meta.title, "テスト曲") == 0, "mp3 title '%s'", meta.title);
    CHECK(strcmp(meta.artist, "テスト歌手") == 0, "mp3 artist");
    CHECK(strcmp(meta.album, "テスト盤") == 0, "mp3 album");
    CHECK(strcmp(meta.genre, "Rock") == 0, "mp3 genre '%s'", meta.genre);
    CHECK(meta.duration_ms > 1800 && meta.duration_ms < 2200,
          "mp3 duration %u", meta.duration_ms);
    {
        pe_out *out = pe_out_null(meta.rate, meta.channels);
        pe_engine *e = pe_open(p, out);
        CHECK(e != NULL, "mp3 open");
        uint64_t frames = pump_all(e);
        uint64_t expect = (uint64_t)meta.rate * 2;
        CHECK(frames > expect * 9 / 10 && frames < expect * 11 / 10,
              "mp3 frames %llu vs ~%llu", (unsigned long long)frames,
              (unsigned long long)expect);
        pe_close(e);
        pe_out_free(out);
    }

    /* ---------- engine: Ogg fixture ---------- */
    snprintf(p, sizeof(p), "%s/fixture.ogg", fx);
    CHECK(pe_probe(p, &meta) == 0, "ogg probe");
    CHECK(strcmp(meta.title, "テスト曲OGG") == 0, "ogg title '%s'",
          meta.title);
    CHECK(strcmp(meta.artist, "テスト歌手") == 0, "ogg artist");
    CHECK(meta.duration_ms > 1800 && meta.duration_ms < 2200,
          "ogg duration %u", meta.duration_ms);
    {
        pe_out *out = pe_out_null(meta.rate, meta.channels);
        pe_engine *e = pe_open(p, out);
        CHECK(e != NULL, "ogg open");
        uint64_t frames = pump_all(e);
        uint64_t expect = (uint64_t)meta.rate * 2;
        CHECK(frames > expect * 9 / 10 && frames < expect * 11 / 10,
              "ogg frames %llu vs ~%llu", (unsigned long long)frames,
              (unsigned long long)expect);
        pe_close(e);
        pe_out_free(out);
    }
    CHECK(pe_probe("/nonexistent/x.mp3", &meta) != 0, "probe missing fails");

    /* ---------- model: scan + cache ---------- */
    snprintf(p, sizeof(p), "%s/fixture.mp3", fx);
    snprintf(q, sizeof(q), "%s/song1.mp3", media);
    copy_file(p, q);
    snprintf(p, sizeof(p), "%s/fixture.ogg", fx);
    snprintf(q, sizeof(q), "%s/song2.ogg", media);
    copy_file(p, q);
    snprintf(q, sizeof(q), "%s/deep.mp3", sub);
    snprintf(p, sizeof(p), "%s/fixture.mp3", fx);
    copy_file(p, q);
    /* media now holds: tone.wav, song1.mp3, song2.ogg, sub/deep.mp3 */

    char root[1100];
    snprintf(root, sizeof(root), "%s/appdata", tmpl);
    pl_lib *lib = pl_lib_open(root);
    CHECK(lib != NULL, "lib open");
    CHECK(pl_lib_scan(lib, media) == 0, "scan");
    CHECK(pl_lib_count(lib) == 4, "scan count %zu", pl_lib_count(lib));
    CHECK(pl_lib_probes(lib) == 4, "probes 4, got %zu", pl_lib_probes(lib));
    CHECK(pl_lib_scan(lib, media) == 0, "rescan");
    CHECK(pl_lib_probes(lib) == 4, "cache hit: probes still %zu",
          pl_lib_probes(lib));
    /* change one file's size -> exactly one re-probe */
    snprintf(q, sizeof(q), "%s/song1.mp3", media);
    FILE *af = fopen(q, "ab");
    fputc(0, af);
    fclose(af);
    CHECK(pl_lib_scan(lib, media) == 0, "rescan 2");
    CHECK(pl_lib_probes(lib) == 5, "one re-probe, got %zu",
          pl_lib_probes(lib));

    /* views / search / sort */
    const char *vals[16];
    size_t nv = pl_lib_values(lib, PL_F_ARTIST, vals, 16);
    CHECK(nv == 1 && strcmp(vals[0], "テスト歌手") == 0,
          "artist values %zu", nv);
    size_t idx[16];
    size_t nf = pl_lib_filter(lib, PL_F_ALBUM, "テスト盤", idx, 16);
    CHECK(nf == 3, "album filter %zu (mp3 x2 + ogg)", nf);
    nf = pl_lib_search(lib, "OGG", idx, 16);
    CHECK(nf == 1, "search OGG %zu", nf);
    const pl_track *t0 = pl_lib_at(lib, idx[0]);
    CHECK(t0 && strstr(t0->path, "song2.ogg") != NULL, "search found ogg");
    pl_lib_sort(lib, PL_F_DURATION, false);
    CHECK(pl_lib_at(lib, 0)->duration_ms <=
          pl_lib_at(lib, 1)->duration_ms, "sorted by duration");

    /* cache budget eviction */
    pl_lib_set_cache_budget(lib, 1);
    CHECK(pl_lib_scan(lib, media) == 0, "scan tiny budget");
    CHECK(pl_lib_count(lib) == 0, "all evicted at 1-byte budget");
    pl_lib_set_cache_budget(lib, 4ull * 1024 * 1024);
    CHECK(pl_lib_scan(lib, media) == 0, "scan restored budget");
    CHECK(pl_lib_count(lib) == 4, "library restored %zu",
          pl_lib_count(lib));

    /* ---------- playlists ---------- */
    char mp3path[1100], oggpath[1100];
    snprintf(mp3path, sizeof(mp3path), "%s/song1.mp3", media);
    snprintf(oggpath, sizeof(oggpath), "%s/song2.ogg", media);
    CHECK(pl_lists_create(lib, "お気に入り") == 0, "list create");
    CHECK(pl_lists_create(lib, "お気に入り") != 0, "dup create rejected");
    CHECK(pl_lists_create(lib, "空リスト") == 0, "empty list create");
    CHECK(pl_lists_add(lib, "お気に入り", mp3path) == 0, "list add 1");
    CHECK(pl_lists_add(lib, "お気に入り", oggpath) == 0, "list add 2");
    CHECK(pl_lists_items(lib, "お気に入り") == 2, "list items 2");
    CHECK(pl_lists_move(lib, "お気に入り", 1, 0) == 0, "list move");
    CHECK(strcmp(pl_lists_item(lib, "お気に入り", 0), oggpath) == 0,
          "moved to front");
    CHECK(pl_lists_remove_at(lib, "お気に入り", 0) == 0, "list remove");
    CHECK(pl_lists_rename(lib, "お気に入り", "ベスト") == 0, "list rename");
    pl_lib_close(lib);
    lib = pl_lib_open(root);
    CHECK(pl_lists_count(lib) == 2, "lists persist %zu",
          pl_lists_count(lib));
    CHECK(pl_lists_items(lib, "ベスト") == 1, "renamed list persists");
    CHECK(pl_lists_items(lib, "空リスト") == 0, "empty list persists");
    CHECK(pl_lists_delete(lib, "ベスト") == 0, "list delete");
    CHECK(pl_lists_count(lib) == 1, "one list left");
    pl_lib_close(lib);

    /* ---------- play order ---------- */
    const char *ctx[] = { "a", "b", "c", "d" };
    pl_order *o = pl_order_new();
    pl_order_set_context(o, ctx, 4, 0);
    CHECK(strcmp(pl_order_current(o), "a") == 0, "order starts at a");
    CHECK(strcmp(pl_order_next(o, false), "b") == 0, "next b");
    pl_order_set_repeat(o, PL_REPEAT_ONE);
    CHECK(strcmp(pl_order_next(o, true), "b") == 0,
          "repeat-one auto stays");
    CHECK(strcmp(pl_order_next(o, false), "c") == 0,
          "repeat-one manual advances");
    pl_order_set_repeat(o, PL_REPEAT_OFF);
    CHECK(strcmp(pl_order_next(o, false), "d") == 0, "next d");
    CHECK(pl_order_next(o, false) == NULL, "repeat-off stops at end");
    pl_order_set_repeat(o, PL_REPEAT_ALL);
    pl_order_set_context(o, ctx, 4, 3);
    CHECK(strcmp(pl_order_next(o, false), "a") == 0, "repeat-all wraps");
    CHECK(strcmp(pl_order_prev(o), "d") == 0, "prev wraps with repeat-all");
    /* queue precedence */
    pl_order_queue_later(o, "q1");
    pl_order_queue_next(o, "q0");
    CHECK(pl_order_queue_count(o) == 2, "queue 2");
    CHECK(strcmp(pl_order_queue_at(o, 0), "q0") == 0, "queue front q0");
    CHECK(strcmp(pl_order_next(o, false), "q0") == 0, "queue plays first");
    CHECK(strcmp(pl_order_next(o, false), "q1") == 0, "queue second");
    CHECK(pl_order_queue_count(o) == 0, "queue drained");
    pl_order_queue_later(o, "x");
    pl_order_queue_later(o, "y");
    CHECK(pl_order_queue_move(o, 1, 0) == 0, "queue move");
    CHECK(strcmp(pl_order_queue_at(o, 0), "y") == 0, "queue moved");
    CHECK(pl_order_queue_remove(o, 0) == 0, "queue remove");
    pl_order_queue_clear(o);
    CHECK(pl_order_queue_count(o) == 0, "queue cleared");
    /* shuffle: deterministic permutation, current preserved */
    pl_order *o2 = pl_order_new();
    pl_order_set_context(o2, ctx, 4, 0);
    pl_order_set_shuffle(o2, true, 42);
    CHECK(pl_order_shuffle(o2), "shuffle on");
    CHECK(strcmp(pl_order_current(o2), "a") == 0,
          "shuffle keeps current track");
    bool seen[4] = {0, 0, 0, 0};
    const char *cur = pl_order_current(o2);
    seen[cur[0] - 'a'] = true;
    for (int i = 0; i < 3; i++) {
        cur = pl_order_next(o2, false);
        if (cur) seen[cur[0] - 'a'] = true;
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3],
          "shuffle visits all tracks exactly once");
    pl_order *o3 = pl_order_new();
    pl_order_set_context(o3, ctx, 4, 0);
    pl_order_set_shuffle(o3, true, 42);
    bool same = true;
    const char *c2 = pl_order_current(o2);
    pl_order_set_context(o2, ctx, 4, 0);
    pl_order_set_shuffle(o2, true, 42);
    for (int i = 0; i < 4; i++) {
        const char *x = i ? pl_order_next(o2, false) : pl_order_current(o2);
        const char *y = i ? pl_order_next(o3, false) : pl_order_current(o3);
        if (!x || !y || strcmp(x, y) != 0) same = false;
    }
    (void)c2;
    CHECK(same, "same seed -> same shuffle order");
    pl_order_free(o);
    pl_order_free(o2);
    pl_order_free(o3);

    /* ---------- persisted state ---------- */
    pl_state st;
    memset(&st, 0, sizeof(st));
    snprintf(st.path, sizeof(st.path), "%s", mp3path);
    st.position_ms = 12345;
    st.volume = 0.4;
    st.shuffle = true;
    st.repeat = PL_REPEAT_ALL;
    CHECK(pl_state_save(root, &st) == 0, "state save");
    pl_state ld;
    CHECK(pl_state_load(root, &ld) == 0, "state load");
    CHECK(strcmp(ld.path, mp3path) == 0 && ld.position_ms == 12345 &&
          ld.shuffle && ld.repeat == PL_REPEAT_ALL &&
          ld.volume > 0.39 && ld.volume < 0.41, "state roundtrip");
    CHECK(pl_state_load("/nonexistent-dir", &ld) != 0,
          "state load missing fails");

    printf("test_player: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
