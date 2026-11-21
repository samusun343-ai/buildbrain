/* recmodel - bd-recorder model (no toolkit dependency).
 *
 * Recording is built for a device that loses power without
 * warning:
 *   - audio is APPENDED to the .wav file and fsynced about once
 *     per second of audio (and on pause/stop), so a power cut
 *     loses at most the tail;
 *   - the WAV header is finalised on stop, but the library does
 *     not trust it: a recording whose header sizes disagree
 *     with the file length (unfinalised = power was cut) is
 *     listed and played with the duration derived from the
 *     file, and rc_wav_repair() rewrites the header in place;
 *   - deletes go to a "recently deleted" area evicted by total
 *     BYTES (oldest deletion first, by sequence number) - never
 *     by age: the device clock is untrusted.
 *
 * Capture and playback go through small vtables. The real
 * capture source is ALSA (rc_source_alsa); a synthetic tone
 * source is built in for mic checks and tests. Playback output
 * is ALSA or a null sink. NOTE: on the QEMU guest the capture
 * path is known-broken on the VIRTUALISATION side (arecord
 * dies with EIO); this model's correctness is proven on the
 * host and against mocks, guest proof is integration-phase.
 */
#ifndef RECMODEL_H
#define RECMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RC_RATE 44100
#define RC_CHANNELS 1

/* ---------------- WAV core ---------------- */

typedef struct {
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint64_t data_bytes;
    uint64_t duration_ms;
    bool header_ok;      /* sizes in header match the file */
} rc_wav_meta;

/* Parse a WAV file. Duration is derived from the DATA CHUNK
 * POSITION + file length, so an unfinalised file still reports
 * the true duration (header_ok=false then). 0 ok, -1 not WAV. */
int rc_wav_info(const char *path, rc_wav_meta *out);
/* Rewrite RIFF/data sizes from the actual file length.
 * 0 ok (or already ok), -1 not a WAV file. */
int rc_wav_repair(const char *path);

/* ---------------- capture sources ---------------- */

typedef struct rc_source rc_source;
struct rc_source {
    void *ctx;
    /* Fill buf with up to `frames` frames (interleaved s16).
     * Returns frames read, 0 = no data right now, -1 = error. */
    int (*read)(rc_source *s, int16_t *buf, size_t frames);
    void (*close)(rc_source *s);
    uint32_t rate;
    uint16_t channels;
    unsigned overruns;   /* ALSA source: recovered overruns */
};

/* ALSA capture (device e.g. "default"). NULL on open failure -
 * callers fall back gracefully. */
rc_source *rc_source_alsa(const char *device, uint32_t rate,
                          uint16_t channels);
/* Synthetic sine source (mic check / tests). Never blocks. */
rc_source *rc_source_synth(double freq_hz, uint32_t rate,
                           uint16_t channels);
void rc_source_free(rc_source *s);

/* ---------------- recording session ---------------- */

typedef struct rc_recorder rc_recorder;

/* Create <dir>/<name>.wav (name without extension) with a
 * placeholder header and start appending. Peaks (one u8 per
 * 0.1 s, for the waveform view) are kept in memory and saved
 * to <name>.peaks on stop. */
rc_recorder *rc_recorder_start(const char *dir, const char *name,
                               rc_source *src);
void rc_recorder_pause(rc_recorder *r, bool paused);
bool rc_recorder_paused(const rc_recorder *r);
/* Pull available audio from the source and append it.
 * Returns frames written by this call. fsyncs internally
 * about once per second of audio. */
int rc_recorder_pump(rc_recorder *r);
uint64_t rc_recorder_elapsed_ms(const rc_recorder *r);
float rc_recorder_level(const rc_recorder *r);   /* 0..1 peak */
/* Finish: finalise header, write peaks, fsync, close. */
int rc_recorder_stop(rc_recorder *r);
void rc_recorder_abandon(rc_recorder *r);  /* no finalise (crash sim) */

/* ---------------- library ---------------- */

typedef struct {
    char name[128];        /* without .wav */
    char path[1024];
    uint64_t duration_ms;
    uint64_t size_bytes;
    int64_t mtime;         /* display only */
    bool needs_repair;
    double speed;          /* per-recording playback speed */
    bool favorite;
} rc_entry;

typedef struct rc_lib rc_lib;

/* Root layout: recordings dir holds .wav files (+ .peaks sidecars),
 * deleted dir + meta.txt (speed/favorite TSV) live under the root. */
rc_lib *rc_lib_open(const char *root);
void rc_lib_close(rc_lib *lib);
const char *rc_lib_recordings_dir(rc_lib *lib);

size_t rc_lib_count(const rc_lib *lib);
const rc_entry *rc_lib_at(const rc_lib *lib, size_t i);  /* newest first */
const rc_entry *rc_lib_find(const rc_lib *lib, const char *name);
void rc_lib_rescan(rc_lib *lib);
/* Filtered view: indices into the full list, name substring. */
size_t rc_lib_search(const rc_lib *lib, const char *needle,
                     size_t *idx_out, size_t max);

int rc_lib_rename(rc_lib *lib, const char *name, const char *new_name);
int rc_lib_delete(rc_lib *lib, const char *name);       /* to deleted */
int rc_lib_restore(rc_lib *lib, const char *name);
int rc_lib_purge_one(rc_lib *lib, const char *name);    /* forever */
size_t rc_lib_deleted_count(const rc_lib *lib);
const rc_entry *rc_lib_deleted_at(const rc_lib *lib, size_t i);
uint64_t rc_lib_deleted_bytes(const rc_lib *lib);
/* Set the deleted-area byte budget; eviction runs on delete. */
void rc_lib_set_deleted_budget(rc_lib *lib, uint64_t bytes);

int rc_lib_set_speed(rc_lib *lib, const char *name, double speed);
int rc_lib_set_favorite(rc_lib *lib, const char *name, bool fav);
/* Next default recording name: "新規録音 N" (sequence counter
 * persisted in meta; NOT derived from the clock). */
void rc_lib_next_name(rc_lib *lib, char *out, size_t cap);

/* Waveform peaks (one u8 per 0.1 s, 0..255): loads the .peaks
 * sidecar when present, otherwise computes them by streaming
 * the WAV (constant memory). Returns the bucket count. */
size_t rc_peaks(const char *wav_path, uint8_t *out, size_t max);

/* Trim by sample-exact ranges, streamed (constant memory):
 * mode RC_TRIM_KEEP writes [start_ms,end_ms) to out_path;
 * RC_TRIM_DELETE writes everything except that range.
 * out_path is written atomically (tmp + rename). */
#define RC_TRIM_KEEP 0
#define RC_TRIM_DELETE 1
int rc_trim(const char *src_path, const char *out_path,
            uint64_t start_ms, uint64_t end_ms, int mode);

/* ---------------- playback ---------------- */

typedef struct rc_sink rc_sink;
struct rc_sink {
    void *ctx;
    int (*write)(rc_sink *s, const int16_t *buf, size_t frames);
    void (*close)(rc_sink *s);
    uint32_t rate;
    uint16_t channels;
};
rc_sink *rc_sink_alsa(const char *device, uint32_t rate, uint16_t channels);
rc_sink *rc_sink_null(uint32_t rate, uint16_t channels);
void rc_sink_free(rc_sink *s);
/* Frames the null sink has consumed (test/introspection). */
uint64_t rc_sink_null_frames(const rc_sink *s);

typedef struct rc_player rc_player;
rc_player *rc_player_open(const char *path, rc_sink *sink);
void rc_player_close(rc_player *p);
void rc_player_set_speed(rc_player *p, double speed);  /* 0.5..2.0 */
double rc_player_speed(const rc_player *p);
uint64_t rc_player_duration_ms(const rc_player *p);
uint64_t rc_player_position_ms(const rc_player *p);
int rc_player_seek_ms(rc_player *p, uint64_t ms);
/* Decode+resample+write up to `out_frames` frames to the sink.
 * Returns frames written; 0 at end of stream. */
int rc_player_pump(rc_player *p, size_t out_frames);
bool rc_player_eof(const rc_player *p);

#endif /* RECMODEL_H */
