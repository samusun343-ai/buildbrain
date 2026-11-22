/* recmodel - see recmodel.h. */
#define _POSIX_C_SOURCE 200809L
#include "recmodel.h"
#include "bdstore.h"

#include <alsa/asoundlib.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------- WAV core ---------------- */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

/* Locate fmt fields + data chunk. Returns 0 and fills offsets. */
static int wav_locate(FILE *f, uint32_t *rate, uint16_t *channels,
                      uint16_t *bits, uint32_t *fmt_audio,
                      long *data_off, uint32_t *data_size_hdr,
                      uint32_t *riff_size_hdr)
{
    uint8_t hdr[12];
    if (fseek(f, 0, SEEK_SET) != 0) return -1;
    if (fread(hdr, 1, 12, f) != 12) return -1;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
        return -1;
    *riff_size_hdr = rd32(hdr + 4);
    long pos = 12;
    bool have_fmt = false, have_data = false;
    for (;;) {
        uint8_t ch[8];
        if (fseek(f, pos, SEEK_SET) != 0) break;
        if (fread(ch, 1, 8, f) != 8) break;
        uint32_t sz = rd32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fb[16];
            if (fread(fb, 1, 16, f) != 16) return -1;
            *fmt_audio = rd16(fb);
            *channels = rd16(fb + 2);
            *rate = rd32(fb + 4);
            *bits = rd16(fb + 14);
            have_fmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            *data_off = pos + 8;
            *data_size_hdr = sz;
            have_data = true;
            break;
        }
        pos += 8 + (long)sz + (sz & 1);
    }
    return (have_fmt && have_data) ? 0 : -1;
}

int rc_wav_info(const char *path, rc_wav_meta *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    struct stat sb;
    if (stat(path, &sb) != 0) { fclose(f); return -1; }
    uint32_t rate = 0, dsz = 0, rsz = 0, audio = 0;
    uint16_t ch = 0, bits = 0;
    long doff = 0;
    if (wav_locate(f, &rate, &ch, &bits, &audio, &doff, &dsz, &rsz) != 0 ||
        audio != 1 || (bits != 8 && bits != 16) || ch == 0 || rate == 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    uint64_t actual = (uint64_t)sb.st_size - (uint64_t)doff;
    uint32_t frame = (uint32_t)ch * (bits / 8);
    actual -= actual % frame;
    out->rate = rate;
    out->channels = ch;
    out->bits = bits;
    out->data_bytes = actual;
    out->duration_ms = frame ? actual / frame * 1000 / rate : 0;
    out->header_ok = (dsz == actual) &&
                     (rsz == (uint32_t)sb.st_size - 8);
    return 0;
}

int rc_wav_repair(const char *path)
{
    rc_wav_meta info;
    if (rc_wav_info(path, &info) != 0) return -1;
    if (info.header_ok) return 0;
    FILE *f = fopen(path, "r+b");
    if (!f) return -1;
    struct stat sb;
    if (stat(path, &sb) != 0) { fclose(f); return -1; }
    uint32_t rate, dsz, rsz, audio;
    uint16_t ch, bits;
    long doff;
    if (wav_locate(f, &rate, &ch, &bits, &audio, &doff, &dsz, &rsz) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t b4[4];
    wr32(b4, (uint32_t)sb.st_size - 8);
    if (fseek(f, 4, SEEK_SET) != 0 || fwrite(b4, 1, 4, f) != 4) {
        fclose(f);
        return -1;
    }
    wr32(b4, (uint32_t)info.data_bytes);
    if (fseek(f, doff - 4, SEEK_SET) != 0 || fwrite(b4, 1, 4, f) != 4) {
        fclose(f);
        return -1;
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return 0;
}

static void wav_write_header(FILE *f, uint32_t rate, uint16_t channels,
                             uint16_t bits, uint32_t data_bytes)
{
    uint8_t h[44];
    memset(h, 0, sizeof(h));
    memcpy(h, "RIFF", 4);
    wr32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    wr32(h + 16, 16);
    wr16(h + 20, 1);                       /* PCM */
    wr16(h + 22, channels);
    wr32(h + 24, rate);
    wr32(h + 28, rate * channels * (bits / 8));
    wr16(h + 32, (uint16_t)(channels * (bits / 8)));
    wr16(h + 34, bits);
    memcpy(h + 36, "data", 4);
    wr32(h + 40, data_bytes);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

/* ---------------- sources ---------------- */

void rc_source_free(rc_source *s)
{
    if (!s) return;
    if (s->close) s->close(s);
    free(s);
}

/* synth */
typedef struct { double phase, step; } synth_ctx;
static int synth_read(rc_source *s, int16_t *buf, size_t frames)
{
    synth_ctx *c = s->ctx;
    for (size_t i = 0; i < frames; i++) {
        double v = sin(c->phase) * 0.5;
        c->phase += c->step;
        if (c->phase > 2 * M_PI) c->phase -= 2 * M_PI;
        int16_t x = (int16_t)(v * 32767);
        for (uint16_t ch = 0; ch < s->channels; ch++)
            buf[i * s->channels + ch] = x;
    }
    return (int)frames;
}
static void synth_close(rc_source *s) { free(s->ctx); }

rc_source *rc_source_synth(double freq_hz, uint32_t rate, uint16_t channels)
{
    rc_source *s = calloc(1, sizeof(*s));
    synth_ctx *c = calloc(1, sizeof(*c));
    if (!s || !c) { free(s); free(c); return NULL; }
    c->step = 2 * M_PI * freq_hz / rate;
    s->ctx = c;
    s->read = synth_read;
    s->close = synth_close;
    s->rate = rate;
    s->channels = channels;
    return s;
}

/* ALSA capture */
typedef struct { snd_pcm_t *pcm; } alsa_ctx;
static int alsa_read(rc_source *s, int16_t *buf, size_t frames)
{
    alsa_ctx *c = s->ctx;
    snd_pcm_sframes_t n = snd_pcm_readi(c->pcm, buf, frames);
    if (n == -EPIPE) {
        s->overruns++;
        snd_pcm_prepare(c->pcm);
        return 0;
    }
    if (n == -EAGAIN) return 0;
    if (n < 0) {
        if (snd_pcm_recover(c->pcm, (int)n, 1) == 0) return 0;
        return -1;
    }
    return (int)n;
}
static void alsa_close(rc_source *s)
{
    alsa_ctx *c = s->ctx;
    if (c->pcm) snd_pcm_close(c->pcm);
    free(c);
}

static snd_pcm_t *alsa_open(const char *device, uint32_t rate,
                            uint16_t channels, snd_pcm_stream_t stream)
{
    snd_pcm_t *pcm = NULL;
    if (snd_pcm_open(&pcm, device ? device : "default", stream,
                     SND_PCM_NONBLOCK) < 0)
        return NULL;
    snd_pcm_hw_params_t *hp;
    snd_pcm_hw_params_alloca(&hp);
    if (snd_pcm_hw_params_any(pcm, hp) < 0 ||
        snd_pcm_hw_params_set_access(pcm, hp,
                                     SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        snd_pcm_hw_params_set_format(pcm, hp, SND_PCM_FORMAT_S16_LE) < 0 ||
        snd_pcm_hw_params_set_channels(pcm, hp, channels) < 0 ||
        snd_pcm_hw_params_set_rate_near(pcm, hp, &rate, NULL) < 0 ||
        snd_pcm_hw_params(pcm, hp) < 0) {
        snd_pcm_close(pcm);
        return NULL;
    }
    return pcm;
}

rc_source *rc_source_alsa(const char *device, uint32_t rate,
                          uint16_t channels)
{
    snd_pcm_t *pcm = alsa_open(device, rate, channels, SND_PCM_STREAM_CAPTURE);
    if (!pcm) return NULL;
    rc_source *s = calloc(1, sizeof(*s));
    alsa_ctx *c = calloc(1, sizeof(*c));
    if (!s || !c) { free(s); free(c); snd_pcm_close(pcm); return NULL; }
    c->pcm = pcm;
    s->ctx = c;
    s->read = alsa_read;
    s->close = alsa_close;
    s->rate = rate;
    s->channels = channels;
    return s;
}

/* ---------------- recording session ---------------- */

struct rc_recorder {
    FILE *f;
    char wav_path[1100];
    char peaks_path[1100];
    char dir[1024];
    rc_source *src;         /* not owned */
    bool paused;
    uint64_t frames;        /* frames written */
    uint64_t since_fsync;   /* frames since last fsync */
    uint8_t *peaks;
    size_t n_peaks, cap_peaks;
    uint32_t peak_win;      /* frames in current 0.1 s window */
    uint16_t peak_max;
    float level;
};

rc_recorder *rc_recorder_start(const char *dir, const char *name,
                               rc_source *src)
{
    if (!dir || !name || !src) return NULL;
    rc_recorder *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    snprintf(r->dir, sizeof(r->dir), "%s", dir);
    bd_mkdir_p(dir);
    snprintf(r->wav_path, sizeof(r->wav_path), "%s/%s.wav", dir, name);
    snprintf(r->peaks_path, sizeof(r->peaks_path), "%s/%s.peaks", dir, name);
    r->f = fopen(r->wav_path, "wb");
    if (!r->f) { free(r); return NULL; }
    wav_write_header(r->f, src->rate, src->channels, 16, 0);
    fflush(r->f);
    fsync(fileno(r->f));
    r->src = src;
    return r;
}

void rc_recorder_pause(rc_recorder *r, bool paused)
{
    if (!r) return;
    r->paused = paused;
    if (paused && r->f) { fflush(r->f); fsync(fileno(r->f)); }
}

bool rc_recorder_paused(const rc_recorder *r) { return r && r->paused; }

int rc_recorder_pump(rc_recorder *r)
{
    if (!r || r->paused || !r->f) return 0;
    uint32_t rate = r->src->rate;
    uint16_t ch = r->src->channels;
    size_t cap_frames = rate / 2;              /* <= 0.5 s per pump */
    int16_t *buf = malloc(cap_frames * ch * sizeof(int16_t));
    if (!buf) return -1;
    int total = 0;
    uint32_t win_frames = rate / 10;           /* 0.1 s peak window */
    while ((size_t)total < cap_frames) {
        size_t want = cap_frames - (size_t)total;
        if (want > 4096) want = 4096;
        int n = r->src->read(r->src, buf, want);
        if (n < 0) { free(buf); return -1; }
        if (n == 0) break;
        if (fwrite(buf, sizeof(int16_t) * ch, (size_t)n, r->f) != (size_t)n) {
            free(buf);
            return -1;
        }
        for (int i = 0; i < n; i++) {
            for (uint16_t c = 0; c < ch; c++) {
                int v = buf[i * ch + c];
                if (v < 0) v = -v;
                if (v > r->peak_max) r->peak_max = (uint16_t)v;
            }
            r->peak_win++;
            if (r->peak_win >= win_frames) {
                if (r->n_peaks == r->cap_peaks) {
                    r->cap_peaks = r->cap_peaks ? r->cap_peaks * 2 : 256;
                    r->peaks = realloc(r->peaks, r->cap_peaks);
                }
                r->peaks[r->n_peaks++] =
                    (uint8_t)(r->peak_max * 255 / 32768);
                r->peak_win = 0;
                r->peak_max = 0;
            }
        }
        r->level = 0;
        for (size_t i = 0; i < (size_t)n * ch; i++) {
            int v = buf[i];
            if (v < 0) v = -v;
            if (v / 32768.0f > r->level) r->level = v / 32768.0f;
        }
        r->frames += (uint64_t)n;
        r->since_fsync += (uint64_t)n;
        total += n;
        if ((size_t)n < want) break;   /* source drained */
    }
    if (r->since_fsync >= rate) {      /* ~1 s of audio */
        fflush(r->f);
        fsync(fileno(r->f));
        r->since_fsync = 0;
    }
    free(buf);
    return total;
}

uint64_t rc_recorder_elapsed_ms(const rc_recorder *r)
{
    return r ? r->frames * 1000 / r->src->rate : 0;
}

float rc_recorder_level(const rc_recorder *r) { return r ? r->level : 0; }

int rc_recorder_stop(rc_recorder *r)
{
    if (!r) return -1;
    /* flush the trailing partial peak window */
    if (r->peak_win > 0) {
        if (r->n_peaks == r->cap_peaks) {
            r->cap_peaks = r->cap_peaks ? r->cap_peaks * 2 : 256;
            r->peaks = realloc(r->peaks, r->cap_peaks);
        }
        r->peaks[r->n_peaks++] = (uint8_t)(r->peak_max * 255 / 32768);
    }
    uint32_t data_bytes = (uint32_t)(r->frames * r->src->channels * 2);
    wav_write_header(r->f, r->src->rate, r->src->channels, 16, data_bytes);
    fflush(r->f);
    fsync(fileno(r->f));
    fclose(r->f);
    r->f = NULL;
    if (r->n_peaks)
        bd_write_atomic(r->peaks_path, r->peaks, r->n_peaks);
    bd_fsync_dir_of(r->wav_path);
    free(r->peaks);
    free(r);
    return 0;
}

void rc_recorder_abandon(rc_recorder *r)
{
    if (!r) return;
    if (r->f) { fflush(r->f); fclose(r->f); }   /* no finalise */
    free(r->peaks);
    free(r);
}

/* ---------------- peaks ---------------- */

size_t rc_peaks(const char *wav_path, uint8_t *out, size_t max)
{
    char pk[1100];
    snprintf(pk, sizeof(pk), "%s", wav_path);
    char *dot = strrchr(pk, '.');
    if (dot && strcmp(dot, ".wav") == 0) snprintf(dot, sizeof(pk) - (size_t)(dot - pk), "%s", ".peaks");
    size_t len;
    char *data = bd_read_all(pk, &len);
    if (data) {
        size_t n = len < max ? len : max;
        memcpy(out, data, n);
        free(data);
        return n;
    }
    /* compute by streaming */
    rc_wav_meta info;
    if (rc_wav_info(wav_path, &info) != 0 || info.bits != 16) return 0;
    FILE *f = fopen(wav_path, "rb");
    if (!f) return 0;
    uint32_t rate, dsz, rsz, audio;
    uint16_t ch, bits;
    long doff;
    if (wav_locate(f, &rate, &ch, &bits, &audio, &doff, &dsz, &rsz) != 0) {
        fclose(f);
        return 0;
    }
    fseek(f, doff, SEEK_SET);
    uint32_t win = rate / 10;
    uint32_t cnt = 0;
    uint16_t mx = 0;
    size_t n = 0;
    int16_t buf[4096];
    uint64_t remain = info.data_bytes / 2;
    while (remain && n < max) {
        size_t want = remain > 4096 ? 4096 : (size_t)remain;
        size_t got = fread(buf, sizeof(int16_t), want, f);
        if (!got) break;
        remain -= got;
        for (size_t i = 0; i < got; i++) {
            int v = buf[i];
            if (v < 0) v = -v;
            if (v > mx) mx = (uint16_t)v;
            if ((i % ch) == 0 && ++cnt >= win) {
                out[n++] = (uint8_t)(mx * 255 / 32768);
                cnt = 0;
                mx = 0;
                if (n >= max) break;
            }
        }
    }
    if (cnt > 0 && n < max) out[n++] = (uint8_t)(mx * 255 / 32768);
    fclose(f);
    return n;
}

/* ---------------- library ---------------- */

typedef struct {
    char name[128];
    double speed;
    bool fav;
    bool used;
} meta_ent;

struct rc_lib {
    char root[1024];
    char rec_dir[1100];
    char del_dir[1100];
    rc_entry *entries;
    size_t n, cap;
    rc_entry *deleted;
    size_t nd, capd;
    meta_ent *meta;
    size_t nm, capm;
    uint64_t counter;        /* next-name sequence */
    uint64_t del_budget;     /* bytes; 0 = 64 MiB default */
};

static void lib_paths(rc_lib *lib)
{
    snprintf(lib->rec_dir, sizeof(lib->rec_dir), "%s/recordings", lib->root);
    snprintf(lib->del_dir, sizeof(lib->del_dir), "%s/deleted", lib->root);
}

static void meta_save(rc_lib *lib)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/meta.txt", lib->root);
    size_t cap = 256 + lib->nm * 192;
    char *buf = malloc(cap);
    if (!buf) return;
    size_t w = 0;
    w += (size_t)snprintf(buf + w, cap - w, "counter\t%llu\n",
                          (unsigned long long)lib->counter);
    for (size_t i = 0; i < lib->nm; i++) {
        char esc[256];
        bd_escape(lib->meta[i].name, esc, sizeof(esc));
        w += (size_t)snprintf(buf + w, cap - w, "%s\t%.3g\t%d\n",
                              esc, lib->meta[i].speed,
                              lib->meta[i].fav ? 1 : 0);
    }
    bd_write_atomic(path, buf, w);
    free(buf);
}

static meta_ent *meta_get(rc_lib *lib, const char *name, bool create)
{
    for (size_t i = 0; i < lib->nm; i++)
        if (strcmp(lib->meta[i].name, name) == 0) return &lib->meta[i];
    if (!create) return NULL;
    if (lib->nm == lib->capm) {
        lib->capm = lib->capm ? lib->capm * 2 : 16;
        lib->meta = realloc(lib->meta, lib->capm * sizeof(meta_ent));
    }
    meta_ent *m = &lib->meta[lib->nm++];
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "%s", name);
    m->speed = 1.0;
    return m;
}

static void meta_load(rc_lib *lib)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/meta.txt", lib->root);
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        *t1 = 0;
        if (strcmp(line, "counter") == 0) {
            lib->counter = strtoull(t1 + 1, NULL, 10);
            continue;
        }
        char name[128];
        bd_unescape(line, name, sizeof(name));
        meta_ent *m = meta_get(lib, name, true);
        char *t2 = strchr(t1 + 1, '\t');
        if (t2) *t2 = 0;
        m->speed = strtod(t1 + 1, NULL);
        if (m->speed < 0.5 || m->speed > 2.0) m->speed = 1.0;
        m->fav = t2 && t2[1] == '1';
    }
    free(data);
}

/* deleted index: "seq\tname" lines, seq = deletion order */
typedef struct { uint64_t seq; char name[128]; } del_ent;

static size_t del_index_load(rc_lib *lib, del_ent **out)
{
    *out = NULL;
    char path[1100];
    snprintf(path, sizeof(path), "%s/index.tsv", lib->del_dir);
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return 0;
    size_t n = 0, cap = 0;
    del_ent *arr = NULL;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *t = strchr(line, '\t');
        if (!t) continue;
        *t = 0;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            arr = realloc(arr, cap * sizeof(del_ent));
        }
        arr[n].seq = strtoull(line, NULL, 10);
        bd_unescape(t + 1, arr[n].name, sizeof(arr[n].name));
        n++;
    }
    free(data);
    *out = arr;
    return n;
}

static void del_index_save(rc_lib *lib, const del_ent *arr, size_t n)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/index.tsv", lib->del_dir);
    size_t cap = 64 + n * 192;
    char *buf = malloc(cap);
    if (!buf) return;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        char esc[256];
        bd_escape(arr[i].name, esc, sizeof(esc));
        w += (size_t)snprintf(buf + w, cap - w, "%llu\t%s\n",
                              (unsigned long long)arr[i].seq, esc);
    }
    bd_write_atomic(path, buf, w);
    free(buf);
}

static int entry_cmp(const void *a, const void *b)
{
    const rc_entry *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;
    return strcmp(x->name, y->name);
}

static void scan_dir_into(rc_lib *lib, const char *dir, bool deleted)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t ln = strlen(nm);
        if (ln < 5 || strcmp(nm + ln - 4, ".wav") != 0) continue;
        char path[1100];
        snprintf(path, sizeof(path), "%s/%s", dir, nm);
        rc_wav_meta info;
        if (rc_wav_info(path, &info) != 0) continue;
        struct stat sb;
        stat(path, &sb);
        rc_entry e;
        memset(&e, 0, sizeof(e));
        snprintf(e.name, sizeof(e.name), "%.*s", (int)(ln - 4), nm);
        snprintf(e.path, sizeof(e.path), "%s", path);
        e.duration_ms = info.duration_ms;
        e.size_bytes = (uint64_t)sb.st_size;
        e.mtime = (int64_t)sb.st_mtime;
        e.needs_repair = !info.header_ok;
        meta_ent *m = meta_get(lib, e.name, false);
        e.speed = m ? m->speed : 1.0;
        e.favorite = m ? m->fav : false;
        if (deleted) {
            if (lib->nd == lib->capd) {
                lib->capd = lib->capd ? lib->capd * 2 : 16;
                lib->deleted = realloc(lib->deleted,
                                       lib->capd * sizeof(rc_entry));
            }
            lib->deleted[lib->nd++] = e;
        } else {
            if (lib->n == lib->cap) {
                lib->cap = lib->cap ? lib->cap * 2 : 16;
                lib->entries = realloc(lib->entries,
                                       lib->cap * sizeof(rc_entry));
            }
            lib->entries[lib->n++] = e;
        }
    }
    closedir(d);
}

void rc_lib_rescan(rc_lib *lib)
{
    lib->n = 0;
    lib->nd = 0;
    scan_dir_into(lib, lib->rec_dir, false);
    scan_dir_into(lib, lib->del_dir, true);
    if (lib->n) qsort(lib->entries, lib->n, sizeof(rc_entry), entry_cmp);
    if (lib->nd) qsort(lib->deleted, lib->nd, sizeof(rc_entry), entry_cmp);
}

rc_lib *rc_lib_open(const char *root)
{
    rc_lib *lib = calloc(1, sizeof(*lib));
    if (!lib) return NULL;
    snprintf(lib->root, sizeof(lib->root), "%s", root);
    lib_paths(lib);
    bd_mkdir_p(lib->rec_dir);
    bd_mkdir_p(lib->del_dir);
    bd_sweep_tmp(lib->rec_dir);
    bd_sweep_tmp(lib->del_dir);
    lib->del_budget = 64ull * 1024 * 1024;
    meta_load(lib);
    rc_lib_rescan(lib);
    return lib;
}

void rc_lib_close(rc_lib *lib)
{
    if (!lib) return;
    free(lib->entries);
    free(lib->deleted);
    free(lib->meta);
    free(lib);
}

const char *rc_lib_recordings_dir(rc_lib *lib) { return lib->rec_dir; }
size_t rc_lib_count(const rc_lib *lib) { return lib->n; }
const rc_entry *rc_lib_at(const rc_lib *lib, size_t i)
{
    return i < lib->n ? &lib->entries[i] : NULL;
}
const rc_entry *rc_lib_find(const rc_lib *lib, const char *name)
{
    for (size_t i = 0; i < lib->n; i++)
        if (strcmp(lib->entries[i].name, name) == 0) return &lib->entries[i];
    return NULL;
}

size_t rc_lib_search(const rc_lib *lib, const char *needle,
                     size_t *idx_out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < lib->n && n < max; i++)
        if (bd_contains_ci(lib->entries[i].name, needle ? needle : ""))
            idx_out[n++] = i;
    return n;
}

static void sidecar_paths(const char *dir, const char *name,
                          char *wav, size_t wcap, char *pk, size_t pcap)
{
    snprintf(wav, wcap, "%s/%s.wav", dir, name);
    snprintf(pk, pcap, "%s/%s.peaks", dir, name);
}

int rc_lib_rename(rc_lib *lib, const char *name, const char *new_name)
{
    if (!rc_lib_find(lib, name) || !new_name || !*new_name) return -1;
    if (rc_lib_find(lib, new_name)) return -1;
    char ow[1100], op[1100], nw[1100], np[1100];
    sidecar_paths(lib->rec_dir, name, ow, sizeof(ow), op, sizeof(op));
    sidecar_paths(lib->rec_dir, new_name, nw, sizeof(nw), np, sizeof(np));
    if (rename(ow, nw) != 0) return -1;
    rename(op, np);   /* peaks optional */
    meta_ent *m = meta_get(lib, name, false);
    if (m) {
        snprintf(m->name, sizeof(m->name), "%s", new_name);
        meta_save(lib);
    }
    bd_fsync_dir_of(nw);
    rc_lib_rescan(lib);
    return 0;
}

static void del_evict(rc_lib *lib)
{
    for (;;) {
        uint64_t total = 0;
        for (size_t i = 0; i < lib->nd; i++) total += lib->deleted[i].size_bytes;
        if (total <= lib->del_budget || lib->nd == 0) return;
        del_ent *arr;
        size_t n = del_index_load(lib, &arr);
        /* victim = smallest deletion seq among files that exist */
        char victim[128] = "";
        uint64_t best = UINT64_MAX;
        bool found = false;
        for (size_t i = 0; i < n; i++) {
            bool exists = false;
            for (size_t j = 0; j < lib->nd; j++)
                if (strcmp(lib->deleted[j].name, arr[i].name) == 0)
                    exists = true;
            if (exists && arr[i].seq < best) {
                best = arr[i].seq;
                snprintf(victim, sizeof(victim), "%s", arr[i].name);
                found = true;
            }
        }
        free(arr);
        if (!found) return;
        rc_lib_purge_one(lib, victim);
    }
}

int rc_lib_delete(rc_lib *lib, const char *name)
{
    if (!rc_lib_find(lib, name)) return -1;
    char ow[1100], op[1100], nw[1100], np[1100];
    sidecar_paths(lib->rec_dir, name, ow, sizeof(ow), op, sizeof(op));
    /* name may collide in deleted: uniquify */
    char uniq[160];
    snprintf(uniq, sizeof(uniq), "%s", name);
    char probe[1100];
    snprintf(probe, sizeof(probe), "%s/%s.wav", lib->del_dir, uniq);
    if (access(probe, F_OK) == 0) {
        char cand[1100];
        if (bd_unique_path(probe, cand, sizeof(cand)) != 0) return -1;
        const char *base = bd_basename(cand);
        snprintf(uniq, sizeof(uniq), "%.*s", (int)strlen(base) - 4, base);
    }
    sidecar_paths(lib->del_dir, uniq, nw, sizeof(nw), np, sizeof(np));
    if (rename(ow, nw) != 0) return -1;
    rename(op, np);
    del_ent *arr;
    size_t n = del_index_load(lib, &arr);
    uint64_t seq = 1;
    for (size_t i = 0; i < n; i++)
        if (arr[i].seq >= seq) seq = arr[i].seq + 1;
    arr = realloc(arr, (n + 1) * sizeof(del_ent));
    arr[n].seq = seq;
    snprintf(arr[n].name, sizeof(arr[n].name), "%s", uniq);
    del_index_save(lib, arr, n + 1);
    free(arr);
    bd_fsync_dir_of(nw);
    rc_lib_rescan(lib);
    del_evict(lib);
    rc_lib_rescan(lib);
    return 0;
}

int rc_lib_restore(rc_lib *lib, const char *name)
{
    bool found = false;
    for (size_t i = 0; i < lib->nd; i++)
        if (strcmp(lib->deleted[i].name, name) == 0) found = true;
    if (!found) return -1;
    char ow[1100], op[1100], nw[1100], np[1100];
    sidecar_paths(lib->del_dir, name, ow, sizeof(ow), op, sizeof(op));
    char uniq[160];
    snprintf(uniq, sizeof(uniq), "%s", name);
    char probe[1100];
    snprintf(probe, sizeof(probe), "%s/%s.wav", lib->rec_dir, uniq);
    if (access(probe, F_OK) == 0) {
        char cand[1100];
        if (bd_unique_path(probe, cand, sizeof(cand)) != 0) return -1;
        const char *base = bd_basename(cand);
        snprintf(uniq, sizeof(uniq), "%.*s", (int)strlen(base) - 4, base);
    }
    sidecar_paths(lib->rec_dir, uniq, nw, sizeof(nw), np, sizeof(np));
    if (rename(ow, nw) != 0) return -1;
    rename(op, np);
    del_ent *arr;
    size_t n = del_index_load(lib, &arr);
    size_t w = 0;
    for (size_t i = 0; i < n; i++)
        if (strcmp(arr[i].name, name) != 0) arr[w++] = arr[i];
    del_index_save(lib, arr, w);
    free(arr);
    bd_fsync_dir_of(nw);
    rc_lib_rescan(lib);
    return 0;
}

int rc_lib_purge_one(rc_lib *lib, const char *name)
{
    char wv[1100], pk[1100];
    sidecar_paths(lib->del_dir, name, wv, sizeof(wv), pk, sizeof(pk));
    int rc = unlink(wv);
    unlink(pk);
    del_ent *arr;
    size_t n = del_index_load(lib, &arr);
    size_t w = 0;
    for (size_t i = 0; i < n; i++)
        if (strcmp(arr[i].name, name) != 0) arr[w++] = arr[i];
    del_index_save(lib, arr, w);
    free(arr);
    bd_fsync_dir_of(wv);
    rc_lib_rescan(lib);
    return rc == 0 ? 0 : -1;
}

size_t rc_lib_deleted_count(const rc_lib *lib) { return lib->nd; }
const rc_entry *rc_lib_deleted_at(const rc_lib *lib, size_t i)
{
    return i < lib->nd ? &lib->deleted[i] : NULL;
}
uint64_t rc_lib_deleted_bytes(const rc_lib *lib)
{
    uint64_t t = 0;
    for (size_t i = 0; i < lib->nd; i++) t += lib->deleted[i].size_bytes;
    return t;
}
void rc_lib_set_deleted_budget(rc_lib *lib, uint64_t bytes)
{
    lib->del_budget = bytes;
}

int rc_lib_set_speed(rc_lib *lib, const char *name, double speed)
{
    if (!rc_lib_find(lib, name)) return -1;
    if (speed < 0.5) speed = 0.5;
    if (speed > 2.0) speed = 2.0;
    meta_ent *m = meta_get(lib, name, true);
    m->speed = speed;
    meta_save(lib);
    rc_lib_rescan(lib);
    return 0;
}

int rc_lib_set_favorite(rc_lib *lib, const char *name, bool fav)
{
    if (!rc_lib_find(lib, name)) return -1;
    meta_ent *m = meta_get(lib, name, true);
    m->fav = fav;
    meta_save(lib);
    rc_lib_rescan(lib);
    return 0;
}

void rc_lib_next_name(rc_lib *lib, char *out, size_t cap)
{
    for (;;) {
        lib->counter++;
        snprintf(out, cap, "新規録音 %llu",
                 (unsigned long long)lib->counter);
        char probe[1100];
        snprintf(probe, sizeof(probe), "%s/%s.wav", lib->rec_dir, out);
        if (access(probe, F_OK) != 0) break;
    }
    meta_save(lib);
}

/* ---------------- trim ---------------- */

int rc_trim(const char *src_path, const char *out_path,
            uint64_t start_ms, uint64_t end_ms, int mode)
{
    rc_wav_meta info;
    if (rc_wav_info(src_path, &info) != 0 || info.bits != 16) return -1;
    FILE *in = fopen(src_path, "rb");
    if (!in) return -1;
    uint32_t rate, dsz, rsz, audio;
    uint16_t ch, bits;
    long doff;
    if (wav_locate(in, &rate, &ch, &bits, &audio, &doff, &dsz, &rsz) != 0) {
        fclose(in);
        return -1;
    }
    uint64_t total_frames = info.data_bytes / (ch * 2);
    uint64_t s = start_ms * rate / 1000;
    uint64_t e = end_ms * rate / 1000;
    if (e > total_frames) e = total_frames;
    if (s > e) { fclose(in); return -1; }
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", out_path, (long)getpid());
    FILE *out = fopen(tmp, "wb");
    if (!out) { fclose(in); return -1; }
    wav_write_header(out, rate, ch, 16, 0);
    uint64_t written = 0;
    int16_t buf[4096];
    /* copy frames [a,b) from input (already positioned via seek) */
    uint64_t ranges[2][2];
    int nr = 0;
    if (mode == RC_TRIM_KEEP) {
        ranges[0][0] = s; ranges[0][1] = e; nr = 1;
    } else {
        ranges[0][0] = 0; ranges[0][1] = s;
        ranges[1][0] = e; ranges[1][1] = total_frames; nr = 2;
    }
    int rc = 0;
    for (int ri = 0; ri < nr && rc == 0; ri++) {
        uint64_t a = ranges[ri][0], b = ranges[ri][1];
        if (b <= a) continue;
        if (fseek(in, doff + (long)(a * ch * 2), SEEK_SET) != 0) { rc = -1; break; }
        uint64_t remain = b - a;
        while (remain) {
            size_t want = remain > 4096 / ch ? 4096 / ch : (size_t)remain;
            size_t got = fread(buf, sizeof(int16_t) * ch, want, in);
            if (!got) { rc = -1; break; }
            if (fwrite(buf, sizeof(int16_t) * ch, got, out) != got) {
                rc = -1;
                break;
            }
            written += got;
            remain -= got;
        }
    }
    fclose(in);
    if (rc == 0) {
        wav_write_header(out, rate, ch, 16,
                         (uint32_t)(written * ch * 2));
        fflush(out);
        fsync(fileno(out));
    }
    fclose(out);
    if (rc != 0) { unlink(tmp); return -1; }
    if (rename(tmp, out_path) != 0) { unlink(tmp); return -1; }
    bd_fsync_dir_of(out_path);
    return 0;
}

/* ---------------- playback ---------------- */

typedef struct { snd_pcm_t *pcm; } sink_ctx;
static int alsa_sink_write(rc_sink *s, const int16_t *buf, size_t frames)
{
    sink_ctx *c = s->ctx;
    size_t done = 0;
    while (done < frames) {
        snd_pcm_sframes_t n =
            snd_pcm_writei(c->pcm, buf + done * s->channels, frames - done);
        if (n == -EPIPE) { snd_pcm_prepare(c->pcm); continue; }
        if (n == -EAGAIN) break;
        if (n < 0) {
            if (snd_pcm_recover(c->pcm, (int)n, 1) == 0) continue;
            break;
        }
        done += (size_t)n;
    }
    return (int)done;
}
static void alsa_sink_close(rc_sink *s)
{
    sink_ctx *c = s->ctx;
    if (c->pcm) { snd_pcm_drain(c->pcm); snd_pcm_close(c->pcm); }
    free(c);
}

rc_sink *rc_sink_alsa(const char *device, uint32_t rate, uint16_t channels)
{
    snd_pcm_t *pcm = alsa_open(device, rate, channels, SND_PCM_STREAM_PLAYBACK);
    if (!pcm) return NULL;
    rc_sink *s = calloc(1, sizeof(*s));
    sink_ctx *c = calloc(1, sizeof(*c));
    if (!s || !c) { free(s); free(c); snd_pcm_close(pcm); return NULL; }
    c->pcm = pcm;
    s->ctx = c;
    s->write = alsa_sink_write;
    s->close = alsa_sink_close;
    s->rate = rate;
    s->channels = channels;
    return s;
}

typedef struct { uint64_t frames; } null_ctx;
static int null_write(rc_sink *s, const int16_t *buf, size_t frames)
{
    (void)buf;
    null_ctx *c = s->ctx;
    c->frames += frames;
    return (int)frames;
}
static void null_close(rc_sink *s) { free(s->ctx); }

rc_sink *rc_sink_null(uint32_t rate, uint16_t channels)
{
    rc_sink *s = calloc(1, sizeof(*s));
    null_ctx *c = calloc(1, sizeof(*c));
    if (!s || !c) { free(s); free(c); return NULL; }
    s->ctx = c;
    s->write = null_write;
    s->close = null_close;
    s->rate = rate;
    s->channels = channels;
    return s;
}

uint64_t rc_sink_null_frames(const rc_sink *s)
{
    const null_ctx *c = s ? s->ctx : NULL;
    return c ? c->frames : 0;
}

void rc_sink_free(rc_sink *s)
{
    if (!s) return;
    if (s->close) s->close(s);
    free(s);
}

/* player: streaming decode (WAV PCM16) + linear resample */
struct rc_player {
    FILE *f;
    long data_off;
    uint32_t rate;
    uint16_t channels;
    uint64_t total_frames;
    rc_sink *sink;         /* not owned */
    double speed;
    double src_pos;        /* fractional frame position */
    int16_t *inbuf;        /* lookahead frames (2) interleaved */
    bool have[2];
    bool eof;
};

static int player_fetch(rc_player *p, int slot, uint64_t frame)
{
    if (frame >= p->total_frames) return -1;
    if (fseek(p->f, p->data_off + (long)(frame * p->channels * 2),
              SEEK_SET) != 0)
        return -1;
    if (fread(p->inbuf + (size_t)slot * p->channels, sizeof(int16_t),
              p->channels, p->f) != p->channels)
        return -1;
    return 0;
}

rc_player *rc_player_open(const char *path, rc_sink *sink)
{
    rc_wav_meta info;
    if (rc_wav_info(path, &info) != 0 || info.bits != 16) return NULL;
    rc_player *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->f = fopen(path, "rb");
    if (!p->f) { free(p); return NULL; }
    uint32_t dsz, rsz, audio;
    if (wav_locate(p->f, &p->rate, &p->channels, &info.bits, &audio,
                   &p->data_off, &dsz, &rsz) != 0) {
        fclose(p->f);
        free(p);
        return NULL;
    }
    p->total_frames = info.data_bytes / (p->channels * 2);
    p->sink = sink;
    p->speed = 1.0;
    p->inbuf = malloc(2 * p->channels * sizeof(int16_t));
    if (!p->inbuf) { fclose(p->f); free(p); return NULL; }
    return p;
}

void rc_player_close(rc_player *p)
{
    if (!p) return;
    fclose(p->f);
    free(p->inbuf);
    free(p);
}

void rc_player_set_speed(rc_player *p, double speed)
{
    if (speed < 0.5) speed = 0.5;
    if (speed > 2.0) speed = 2.0;
    p->speed = speed;
}
double rc_player_speed(const rc_player *p) { return p->speed; }
uint64_t rc_player_duration_ms(const rc_player *p)
{
    return p->total_frames * 1000 / p->rate;
}
uint64_t rc_player_position_ms(const rc_player *p)
{
    return (uint64_t)(p->src_pos) * 1000 / p->rate;
}

int rc_player_seek_ms(rc_player *p, uint64_t ms)
{
    uint64_t frame = ms * p->rate / 1000;
    if (frame > p->total_frames) frame = p->total_frames;
    p->src_pos = (double)frame;
    p->have[0] = p->have[1] = false;
    p->eof = false;
    return 0;
}

bool rc_player_eof(const rc_player *p) { return p->eof; }

int rc_player_pump(rc_player *p, size_t out_frames)
{
    if (p->eof) return 0;
    int16_t *out = malloc((out_frames ? out_frames : 1) * p->channels *
                          sizeof(int16_t));
    if (!out) return -1;
    size_t produced = 0;
    while (produced < out_frames) {
        uint64_t i0 = (uint64_t)p->src_pos;
        double frac = p->src_pos - (double)i0;
        if (i0 >= p->total_frames) { p->eof = true; break; }
        if (player_fetch(p, 0, i0) != 0) { p->eof = true; break; }
        bool have1 = player_fetch(p, 1, i0 + 1) == 0;
        for (uint16_t c = 0; c < p->channels; c++) {
            double a = p->inbuf[c];
            double b = have1 ? p->inbuf[p->channels + c] : a;
            out[produced * p->channels + c] =
                (int16_t)(a + (b - a) * frac);
        }
        produced++;
        p->src_pos += p->speed;
    }
    int written = 0;
    if (produced) {
        written = p->sink->write(p->sink, out, produced);
        if (written < 0) written = 0;
    }
    free(out);
    return written;
}
