/* test_recorder - bd-recorder model tests.
 *
 * The durability story is the point: record through a synth
 * source, pause/resume in one file, simulate a power cut
 * (abandon without finalising) and prove the library still
 * lists/plays the recording with the true duration and that
 * rc_wav_repair fixes the header. Plus trim (frame-exact),
 * recently-deleted with byte-budget eviction by deletion
 * sequence, restore, rename, per-recording speed, search, and
 * the resampling player against a null sink.
 */
#define _POSIX_C_SOURCE 200809L
#include "recmodel.h"
#include "bdstore.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_u64(const char *what, uint64_t got, uint64_t want)
{
    if (got == want) { passes++; printf("PASS %s -> %llu\n", what, (unsigned long long)got); }
    else { fails++; printf("FAIL %s: got %llu, want %llu\n", what, (unsigned long long)got, (unsigned long long)want); }
}

/* record `ms` of synth tone into lib's recordings dir */
static void record_ms(rc_lib *lib, const char *name, int ms)
{
    rc_source *src = rc_source_synth(440, RC_RATE, RC_CHANNELS);
    rc_recorder *r = rc_recorder_start(rc_lib_recordings_dir(lib), name, src);
    if (!r) { check_true("record start", 0); return; }
    uint64_t target = (uint64_t)ms * RC_RATE / 1000;
    uint64_t got = 0;
    while (got < target) {
        int n = rc_recorder_pump(r);
        if (n <= 0) break;
        got += (uint64_t)n;
    }
    rc_recorder_stop(r);
    rc_source_free(src);
}

int main(void)
{
    char tmpl[] = "/tmp/bdrec-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    rc_lib *lib = rc_lib_open(tmpl);
    check_true("lib open", lib != NULL);
    check_u64("lib empty", rc_lib_count(lib), 0);

    /* ---- basic recording: 1000 ms of tone ---- */
    record_ms(lib, "テスト録音", 1000);
    rc_lib_rescan(lib);
    check_u64("one recording", rc_lib_count(lib), 1);
    const rc_entry *e = rc_lib_find(lib, "テスト録音");
    check_true("found", e != NULL);
    check_true("duration ~1000ms",
               e && e->duration_ms >= 980 && e->duration_ms <= 1020);
    check_true("header ok", e && !e->needs_repair);
    /* peaks sidecar exists and covers ~10 buckets */
    uint8_t peaks[64];
    size_t np = rc_peaks(e->path, peaks, sizeof(peaks));
    check_true("peaks ~10", np >= 9 && np <= 11);
    check_true("peaks non-silent", peaks[0] > 50);

    /* ---- pause/resume in one file ---- */
    {
        rc_source *src = rc_source_synth(440, RC_RATE, RC_CHANNELS);
        rc_recorder *r = rc_recorder_start(rc_lib_recordings_dir(lib),
                                           "一時停止", src);
        check_true("pause rec start", r != NULL);
        for (int i = 0; i < 10; i++) rc_recorder_pump(r);   /* ~0.5 s worth max */
        uint64_t before = rc_recorder_elapsed_ms(r);
        rc_recorder_pause(r, true);
        check_true("pump while paused writes 0", rc_recorder_pump(r) == 0);
        check_u64("elapsed frozen while paused",
                  rc_recorder_elapsed_ms(r), before);
        rc_recorder_pause(r, false);
        for (int i = 0; i < 10; i++) rc_recorder_pump(r);
        check_true("elapsed grows after resume",
                   rc_recorder_elapsed_ms(r) > before);
        check_true("level in range",
                   rc_recorder_level(r) > 0.1f && rc_recorder_level(r) <= 1.0f);
        rc_recorder_stop(r);
        rc_source_free(src);
    }

    /* ---- power cut: abandon without finalising ---- */
    {
        rc_source *src = rc_source_synth(330, RC_RATE, RC_CHANNELS);
        rc_recorder *r = rc_recorder_start(rc_lib_recordings_dir(lib),
                                           "電源断", src);
        /* pump ~2.5 s worth: enough to cross two fsync points */
        for (int i = 0; i < 6; i++) rc_recorder_pump(r);
        uint64_t expect_ms = rc_recorder_elapsed_ms(r);
        rc_recorder_abandon(r);      /* <- power cut: no header finalise */
        rc_source_free(src);
        rc_lib_rescan(lib);
        const rc_entry *cut = rc_lib_find(lib, "電源断");
        check_true("cut recording listed", cut != NULL);
        check_true("cut flagged needs_repair", cut && cut->needs_repair);
        check_true("cut duration true (from file length)",
                   cut && llabs((long long)cut->duration_ms -
                                (long long)expect_ms) <= 30);
        check_true("repair ok", rc_wav_repair(cut->path) == 0);
        rc_lib_rescan(lib);
        cut = rc_lib_find(lib, "電源断");
        check_true("after repair header ok", cut && !cut->needs_repair);
        check_true("duration kept after repair",
                   cut && llabs((long long)cut->duration_ms -
                                (long long)expect_ms) <= 30);
    }

    /* ---- playback through null sink, speed + seek ---- */
    {
        const rc_entry *t = rc_lib_find(lib, "テスト録音");
        rc_sink *sink = rc_sink_null(RC_RATE, RC_CHANNELS);
        rc_player *p = rc_player_open(t->path, sink);
        check_true("player open", p != NULL);
        uint64_t dur = rc_player_duration_ms(p);
        check_true("player duration ~1000", dur >= 980 && dur <= 1020);
        int n;
        while ((n = rc_player_pump(p, 4096)) > 0) { }
        check_true("player eof", rc_player_eof(p));
        uint64_t frames = rc_sink_null_frames(sink);
        check_true("sink got ~1 s of frames",
                   frames >= RC_RATE * 98 / 100 && frames <= RC_RATE + 16);
        /* speed 2x: same file consumes in half the output frames */
        rc_player_seek_ms(p, 0);
        rc_player_set_speed(p, 2.0);
        check_true("speed set", rc_player_speed(p) == 2.0);
        uint64_t f0 = rc_sink_null_frames(sink);
        while (rc_player_pump(p, 4096) > 0) { }
        uint64_t used = rc_sink_null_frames(sink) - f0;
        check_true("2x speed halves output",
                   used >= RC_RATE / 2 - 32 && used <= RC_RATE / 2 + 32);
        /* seek to 500 ms: position reports back */
        rc_player_seek_ms(p, 500);
        check_true("seek position ~500",
                   llabs((long long)rc_player_position_ms(p) - 500) <= 25);
        rc_player_close(p);
        rc_sink_free(sink);
    }

    /* ---- trim: keep [200,600) of the 1 s tone -> 400 ms ---- */
    {
        const rc_entry *t = rc_lib_find(lib, "テスト録音");
        char out[1200];
        snprintf(out, sizeof(out), "%s/recordings/トリム済.wav",
                 tmpl);
        check_true("trim keep", rc_trim(t->path, out, 200, 600,
                                        RC_TRIM_KEEP) == 0);
        rc_wav_meta info;
        check_true("trim info", rc_wav_info(out, &info) == 0);
        check_true("trim duration 400ms",
                   info.duration_ms >= 395 && info.duration_ms <= 405);
        /* delete [200,600) -> 600 ms */
        char out2[1200];
        snprintf(out2, sizeof(out2), "%s/recordings/トリム削除.wav",
                 tmpl);
        check_true("trim delete", rc_trim(t->path, out2, 200, 600,
                                          RC_TRIM_DELETE) == 0);
        check_true("trim2 info", rc_wav_info(out2, &info) == 0);
        check_true("trim2 duration 600ms",
                   info.duration_ms >= 595 && info.duration_ms <= 605);
        check_true("trim bad range",
                   rc_trim(t->path, out2, 900, 100, RC_TRIM_KEEP) != 0);
    }
    rc_lib_rescan(lib);

    /* ---- rename / speed / favorite / search ---- */
    check_true("rename", rc_lib_rename(lib, "トリム済", "会議メモ") == 0);
    check_true("renamed found", rc_lib_find(lib, "会議メモ") != NULL);
    check_true("rename clash rejected",
               rc_lib_rename(lib, "会議メモ", "テスト録音") != 0);
    check_true("set speed", rc_lib_set_speed(lib, "会議メモ", 1.5) == 0);
    const rc_entry *km = rc_lib_find(lib, "会議メモ");
    check_true("speed persisted in entry", km && km->speed == 1.5);
    check_true("set favorite", rc_lib_set_favorite(lib, "会議メモ", true) == 0);
    km = rc_lib_find(lib, "会議メモ");
    check_true("favorite persisted", km && km->favorite);
    size_t idx[16];
    size_t hits = rc_lib_search(lib, "会議", idx, 16);
    check_u64("search hits", hits, 1);
    hits = rc_lib_search(lib, "", idx, 16);
    check_u64("search all", hits, rc_lib_count(lib));

    /* ---- default names are sequential, not clock-based ---- */
    char nm[128];
    rc_lib_next_name(lib, nm, sizeof(nm));
    check_true("next name 1", strcmp(nm, "新規録音 1") == 0);
    record_ms(lib, nm, 100);
    rc_lib_next_name(lib, nm, sizeof(nm));
    check_true("next name 2", strcmp(nm, "新規録音 2") == 0);

    /* ---- delete -> recently deleted, budget eviction ---- */
    rc_lib_set_deleted_budget(lib, 1);   /* 1 byte: evict everything */
    check_true("delete", rc_lib_delete(lib, "トリム削除") == 0);
    check_true("deleted evicted at tiny budget",
               rc_lib_deleted_count(lib) == 0);
    check_true("gone from library", rc_lib_find(lib, "トリム削除") == NULL);
    rc_lib_set_deleted_budget(lib, 64ull * 1024 * 1024);
    check_true("delete 2", rc_lib_delete(lib, "一時停止") == 0);
    check_u64("deleted count 1", rc_lib_deleted_count(lib), 1);
    check_true("deleted bytes > 0", rc_lib_deleted_bytes(lib) > 0);
    check_true("restore", rc_lib_restore(lib, "一時停止") == 0);
    check_u64("deleted count 0", rc_lib_deleted_count(lib), 0);
    check_true("restored listed", rc_lib_find(lib, "一時停止") != NULL);
    check_true("delete 3", rc_lib_delete(lib, "一時停止") == 0);
    check_true("purge", rc_lib_purge_one(lib, "一時停止") == 0);
    check_u64("deleted empty after purge", rc_lib_deleted_count(lib), 0);

    /* eviction order: oldest deletion leaves first */
    record_ms(lib, "排出A", 300);
    record_ms(lib, "排出B", 300);
    rc_lib_rescan(lib);
    const rc_entry *ea = rc_lib_find(lib, "排出A");
    uint64_t one_size = ea ? ea->size_bytes : 0;
    check_true("sizes known", one_size > 0);
    rc_lib_set_deleted_budget(lib, one_size + one_size / 2);  /* fits one */
    rc_lib_delete(lib, "排出A");
    rc_lib_delete(lib, "排出B");
    check_u64("one survives eviction", rc_lib_deleted_count(lib), 1);
    check_true("survivor is B (newest deletion)",
               rc_lib_deleted_at(lib, 0) &&
               strcmp(rc_lib_deleted_at(lib, 0)->name, "排出B") == 0);

    rc_lib_close(lib);

    /* persistence across reopen: speed + counter */
    lib = rc_lib_open(tmpl);
    km = rc_lib_find(lib, "会議メモ");
    check_true("speed survives reopen", km && km->speed == 1.5);
    rc_lib_next_name(lib, nm, sizeof(nm));
    check_true("counter survives reopen", strcmp(nm, "新規録音 3") == 0);
    rc_lib_close(lib);

    printf("test_recorder: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
