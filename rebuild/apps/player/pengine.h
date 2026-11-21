/* pengine - bd-player playback engine.
 *
 * Pump-driven (no threads): the UI tick decodes a bounded
 * chunk per call, which keeps tests deterministic and the
 * 128 MB terminal free of decoder threads.
 *
 * Decoders (chosen by content/extension):
 *  - WAV: built in (PCM 8/16-bit), no dependency.
 *  - MP3: libmpg123 via dlopen with self-declared minimal
 *    prototypes (the host/target has the runtime library but
 *    no development headers; dlopen also degrades cleanly to
 *    "unsupported" when the library is absent).
 *  - Ogg Vorbis: libvorbisfile via dlopen, same rationale.
 * Metadata: ID3v2/ID3v1 parsed in-engine for MP3; Vorbis
 * comments via ov_comment; WAV gets the file name.
 *
 * Output is a sink vtable: ALSA (direct, no alsa-lib helpers
 * beyond the C API) or a null sink for tests (which also
 * records frame count + peak for volume checks). Software
 * volume lives in the engine so it is device-independent
 * and testable.
 */
#ifndef PENGINE_H
#define PENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    char genre[64];
    uint32_t duration_ms;
    uint32_t rate;
    uint16_t channels;
} pe_meta;

/* Probe a file's metadata without decoding. 0 on success. */
int pe_probe(const char *path, pe_meta *out);

/* ---- output sinks ---- */
typedef struct pe_out pe_out;
struct pe_out {
    void *ctx;
    int (*write)(pe_out *o, const int16_t *frames, size_t n);
    void (*close)(pe_out *o);
    uint32_t rate;
    uint16_t channels;
};

pe_out *pe_out_alsa(const char *device, uint32_t rate, uint16_t channels);
pe_out *pe_out_null(uint32_t rate, uint16_t channels);
uint64_t pe_out_null_frames(const pe_out *o);
int pe_out_null_peak(const pe_out *o);   /* max |sample| seen */
void pe_out_free(pe_out *o);

/* ---- engine ---- */
typedef struct pe_engine pe_engine;

/* The sink must be created at the probed rate/channels. */
pe_engine *pe_open(const char *path, pe_out *out);
void pe_close(pe_engine *e);
const pe_meta *pe_engine_meta(const pe_engine *e);

/* Decode + emit up to max_frames frames. Returns frames
 * emitted (0 at EOF), <0 on error. */
int pe_pump(pe_engine *e, size_t max_frames);
bool pe_eof(const pe_engine *e);
uint64_t pe_position_ms(const pe_engine *e);
int pe_seek_ms(pe_engine *e, uint64_t ms);
void pe_set_volume(pe_engine *e, double vol);   /* 0..1 */
double pe_volume(const pe_engine *e);

#ifdef __cplusplus
}
#endif
#endif /* PENGINE_H */
