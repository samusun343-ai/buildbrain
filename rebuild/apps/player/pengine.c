/* pengine - see pengine.h. */
#define _POSIX_C_SOURCE 200809L
#include "pengine.h"

#include <alsa/asoundlib.h>
#include <ctype.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ================= shared helpers ================= */

static bool ext_is(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    dot++;
    while (*dot && *ext) {
        if (tolower((unsigned char)*dot) != tolower((unsigned char)*ext))
            return false;
        dot++;
        ext++;
    }
    return *dot == 0 && *ext == 0;
}

static void copy_str(char *dst, size_t cap, const char *src, size_t len)
{
    if (cap == 0) return;
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
    /* trim trailing spaces/NULs */
    while (len && (dst[len - 1] == ' ' || dst[len - 1] == 0)) dst[--len] = 0;
}

/* ================= ID3 ================= */

static const char *const id3_genres[] = {
    "Blues","Classic Rock","Country","Dance","Disco","Funk","Grunge",
    "Hip-Hop","Jazz","Metal","New Age","Oldies","Other","Pop","R&B",
    "Rap","Reggae","Rock","Techno","Industrial","Alternative","Ska",
    "Death Metal","Pranks","Soundtrack","Euro-Techno","Ambient",
    "Trip-Hop","Vocal","Jazz+Funk","Fusion","Trance","Classical",
    "Instrumental","Acid","House","Game","Sound Clip","Gospel","Noise",
    "AlternRock","Bass","Soul","Punk","Space","Meditative",
    "Instrumental Pop","Instrumental Rock","Ethnic","Gothic","Darkwave",
    "Techno-Industrial","Electronic","Pop-Folk","Eurodance","Dream",
    "Southern Rock","Comedy","Cult","Gangsta","Top 40","Christian Rap",
    "Pop/Funk","Jungle","Native American","Cabaret","New Wave",
    "Psychadelic","Rave","Showtunes","Trailer","Lo-Fi","Tribal",
    "Acid Punk","Acid Jazz","Polka","Retro","Musical","Rock & Roll",
    "Hard Rock","Folk","Folk-Rock","National Folk","Swing","Fast Fusion",
    "Bebob","Latin","Revival","Celtic","Bluegrass","Avantgarde",
    "Gothic Rock","Progressive Rock","Psychedelic Rock","Symphonic Rock",
    "Slow Rock","Big Band","Chorus","Easy Listening","Acoustic","Humour",
    "Speech","Chanson","Opera","Chamber Music","Sonata","Symphony",
    "Booty Brass","Primus","Porn Groove","Satire","Slow Jam","Club",
    "Tango","Samba","Folklore","Ballad","Power Ballad","Rhythmic Soul",
    "Freestyle","Duet","Punk Rock","Drum Solo","A capella","Euro-House",
    "Dance Hall","Goa","Drum & Bass","Club-House","Hardcore","Terror",
    "Indie","BritPop","Negerpunk","Polsk Punk","Beat","Christian Gangsta Rap",
    "Heavy Metal","Black Metal","Crossover","Contemporary Christian",
    "Christian Rock","Merengue","Salsa","Thrash Metal","Anime","JPop",
    "Synthpop"
};
#define N_GENRES (sizeof(id3_genres) / sizeof(id3_genres[0]))

/* decode a text frame payload into dst (UTF-8). Handles
 * encoding bytes 0 (Latin-1, passed through), 1 (UTF-16 BOM),
 * 2 (UTF-16BE), 3 (UTF-8). */
static void id3_text(char *dst, size_t cap, const uint8_t *p, size_t len)
{
    if (!len || cap < 2) { if (cap) dst[0] = 0; return; }
    uint8_t enc = p[0];
    p++;
    len--;
    if (enc == 3 || enc == 0) {
        copy_str(dst, cap, (const char *)p, len);
        return;
    }
    bool be = (enc == 2);
    if (enc == 1 && len >= 2) {
        if (p[0] == 0xFF && p[1] == 0xFE) { be = false; p += 2; len -= 2; }
        else if (p[0] == 0xFE && p[1] == 0xFF) { be = true; p += 2; len -= 2; }
    }
    size_t w = 0;
    for (size_t i = 0; i + 1 < len && w + 4 < cap; i += 2) {
        uint32_t cp = be ? (p[i] << 8 | p[i + 1]) : (p[i] | p[i + 1] << 8);
        if (cp == 0) break;
        if (cp < 0x80) {
            dst[w++] = (char)cp;
        } else if (cp < 0x800) {
            dst[w++] = (char)(0xC0 | cp >> 6);
            dst[w++] = (char)(0x80 | (cp & 0x3F));
        } else {
            dst[w++] = (char)(0xE0 | cp >> 12);
            dst[w++] = (char)(0x80 | (cp >> 6 & 0x3F));
            dst[w++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    dst[w] = 0;
}

static void genre_from_tcon(char *dst, size_t cap, const char *raw)
{
    /* "(17)" or "(17)Rock" -> table entry; plain text passes */
    if (raw[0] == '(') {
        char *end;
        long n = strtol(raw + 1, &end, 10);
        if (end && *end == ')' && n >= 0 && (size_t)n < N_GENRES) {
            copy_str(dst, cap, id3_genres[n], strlen(id3_genres[n]));
            return;
        }
    }
    copy_str(dst, cap, raw, strlen(raw));
}

static void id3_parse_file(const char *path, pe_meta *m)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t hdr[10];
    if (fread(hdr, 1, 10, f) == 10 && memcmp(hdr, "ID3", 3) == 0) {
        int ver = hdr[3];
        uint32_t size = (uint32_t)(hdr[6] & 0x7F) << 21 |
                        (uint32_t)(hdr[7] & 0x7F) << 14 |
                        (uint32_t)(hdr[8] & 0x7F) << 7 | (hdr[9] & 0x7F);
        if (size > 4u * 1024 * 1024) size = 4u * 1024 * 1024;
        uint8_t *buf = malloc(size ? size : 1);
        if (buf) {
            size_t got = fread(buf, 1, size, f);
            size_t pos = 0;
            while (pos + 10 <= got) {
                char id[5] = {0, 0, 0, 0, 0};
                memcpy(id, buf + pos, 4);
                if (!id[0]) break;
                uint32_t fsz;
                if (ver >= 4)
                    fsz = (uint32_t)(buf[pos + 4] & 0x7F) << 21 |
                          (uint32_t)(buf[pos + 5] & 0x7F) << 14 |
                          (uint32_t)(buf[pos + 6] & 0x7F) << 7 |
                          (buf[pos + 7] & 0x7F);
                else
                    fsz = (uint32_t)buf[pos + 4] << 24 |
                          (uint32_t)buf[pos + 5] << 16 |
                          (uint32_t)buf[pos + 6] << 8 | buf[pos + 7];
                const uint8_t *payload = buf + pos + 10;
                if (pos + 10 + fsz > got) break;
                if (strcmp(id, "TIT2") == 0)
                    id3_text(m->title, sizeof(m->title), payload, fsz);
                else if (strcmp(id, "TPE1") == 0)
                    id3_text(m->artist, sizeof(m->artist), payload, fsz);
                else if (strcmp(id, "TALB") == 0)
                    id3_text(m->album, sizeof(m->album), payload, fsz);
                else if (strcmp(id, "TCON") == 0) {
                    char raw[64];
                    id3_text(raw, sizeof(raw), payload, fsz);
                    genre_from_tcon(m->genre, sizeof(m->genre), raw);
                }
                pos += 10 + fsz;
            }
            free(buf);
        }
    }
    /* ID3v1 fallback for empty fields */
    if (!m->title[0] || !m->artist[0] || !m->album[0] || !m->genre[0]) {
        if (fseek(f, -128, SEEK_END) == 0) {
            uint8_t tag[128];
            if (fread(tag, 1, 128, f) == 128 &&
                memcmp(tag, "TAG", 3) == 0) {
                if (!m->title[0])
                    copy_str(m->title, sizeof(m->title),
                             (const char *)tag + 3, 30);
                if (!m->artist[0])
                    copy_str(m->artist, sizeof(m->artist),
                             (const char *)tag + 33, 30);
                if (!m->album[0])
                    copy_str(m->album, sizeof(m->album),
                             (const char *)tag + 63, 30);
                if (!m->genre[0] && tag[127] < N_GENRES)
                    copy_str(m->genre, sizeof(m->genre),
                             id3_genres[tag[127]],
                             strlen(id3_genres[tag[127]]));
            }
        }
    }
    fclose(f);
}

/* ================= WAV ================= */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

typedef struct {
    FILE *f;
    long data_off;
    uint64_t data_bytes, data_pos;
    uint32_t rate;
    uint16_t channels, bits;
} wav_ctx;

static int wav_open_ctx(const char *path, wav_ctx *c, pe_meta *m)
{
    memset(c, 0, sizeof(*c));
    c->f = fopen(path, "rb");
    if (!c->f) return -1;
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, c->f) != 12 || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(c->f);
        return -1;
    }
    long pos = 12;
    bool have_fmt = false, have_data = false;
    uint32_t dsz = 0;
    while (!have_data) {
        uint8_t ch[8];
        if (fseek(c->f, pos, SEEK_SET) != 0) break;
        if (fread(ch, 1, 8, c->f) != 8) break;
        uint32_t sz = rd32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fb[16];
            if (fread(fb, 1, 16, c->f) != 16) break;
            if (rd16(fb) != 1) break;            /* PCM only */
            c->channels = rd16(fb + 2);
            c->rate = rd32(fb + 4);
            c->bits = rd16(fb + 14);
            have_fmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            c->data_off = pos + 8;
            dsz = sz;
            have_data = true;
            break;
        }
        pos += 8 + (long)sz + (sz & 1);
    }
    if (!have_fmt || !have_data || (c->bits != 8 && c->bits != 16) ||
        !c->channels || !c->rate) {
        fclose(c->f);
        return -1;
    }
    struct stat sb;
    stat(path, &sb);
    uint64_t avail = (uint64_t)sb.st_size - (uint64_t)c->data_off;
    c->data_bytes = dsz < avail ? dsz : avail;
    uint32_t frame = c->channels * (c->bits / 8);
    c->data_bytes -= c->data_bytes % frame;
    if (m) {
        m->rate = c->rate;
        m->channels = c->channels;
        m->duration_ms = frame ? (uint32_t)(c->data_bytes / frame * 1000 /
                                            c->rate) : 0;
    }
    fseek(c->f, c->data_off, SEEK_SET);
    return 0;
}

/* read up to `want` frames as s16; returns frames read */
static int wav_read(wav_ctx *c, int16_t *out, size_t want)
{
    uint32_t frame = c->channels * (c->bits / 8);
    uint64_t remain = (c->data_bytes - c->data_pos) / frame;
    if ((uint64_t)want > remain) want = (size_t)remain;
    if (!want) return 0;
    if (c->bits == 16) {
        size_t got = fread(out, frame, want, c->f);
        c->data_pos += got * frame;
        return (int)got;
    }
    /* 8-bit unsigned -> s16 */
    uint8_t buf[8192];
    size_t done = 0;
    while (done < want) {
        size_t chunk = want - done;
        if (chunk > sizeof(buf) / frame) chunk = sizeof(buf) / frame;
        size_t got = fread(buf, frame, chunk, c->f);
        if (!got) break;
        for (size_t i = 0; i < got * c->channels; i++)
            out[done * c->channels + i] =
                (int16_t)(((int)buf[i] - 128) << 8);
        done += got;
        c->data_pos += got * frame;
    }
    return (int)done;
}

/* ================= libmpg123 via dlopen ================= */

typedef struct mpg123_handle_s mpg123_handle;
typedef int (*fn_mpg_init)(void);
typedef mpg123_handle *(*fn_mpg_new)(const char *, int *);
typedef int (*fn_mpg_open)(mpg123_handle *, const char *);
typedef int (*fn_mpg_getformat)(mpg123_handle *, long *, int *, int *);
typedef int (*fn_mpg_format_none)(mpg123_handle *);
typedef int (*fn_mpg_format)(mpg123_handle *, long, int, int);
typedef int (*fn_mpg_read)(mpg123_handle *, unsigned char *, size_t, size_t *);
typedef int (*fn_mpg_scan)(mpg123_handle *);
typedef int64_t (*fn_mpg_length)(mpg123_handle *);
typedef int64_t (*fn_mpg_seek)(mpg123_handle *, int64_t, int);
typedef void (*fn_mpg_close)(mpg123_handle *);
typedef void (*fn_mpg_delete)(mpg123_handle *);

typedef struct {
    void *dl;
    fn_mpg_new p_new;
    fn_mpg_open p_open;
    fn_mpg_getformat p_getformat;
    fn_mpg_format_none p_format_none;
    fn_mpg_format p_format;
    fn_mpg_read p_read;
    fn_mpg_scan p_scan;
    fn_mpg_length p_length;
    fn_mpg_seek p_seek;
    fn_mpg_close p_close;
    fn_mpg_delete p_delete;
    bool ok;
} mpg_api;

static mpg_api g_mpg;
static bool g_mpg_tried;

static bool mpg_load(void)
{
    if (g_mpg_tried) return g_mpg.ok;
    g_mpg_tried = true;
    g_mpg.dl = dlopen("libmpg123.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!g_mpg.dl) return false;
#define SYM(name) *(void **)&g_mpg.p_##name = dlsym(g_mpg.dl, "mpg123_" #name)
    SYM(new); SYM(open); SYM(getformat); SYM(format_none); SYM(format);
    SYM(read); SYM(scan); SYM(length); SYM(seek); SYM(close); SYM(delete);
#undef SYM
    fn_mpg_init init = (fn_mpg_init)dlsym(g_mpg.dl, "mpg123_init");
    if (!g_mpg.p_new || !g_mpg.p_open || !g_mpg.p_read || !init)
        return false;
    if (init() != 0) return false;
    g_mpg.ok = true;
    return true;
}

#define MPG123_OK 0
#define MPG123_DONE (-12)
#define MPG123_NEW_FORMAT 1
#define MPG123_ENC_SIGNED_16 0x1050

typedef struct {
    mpg123_handle *h;
    uint32_t rate;
    uint16_t channels;
    uint64_t total_frames;
    uint8_t carry[4];
    size_t carry_n;
} mp3_ctx;

static int mp3_open_ctx(const char *path, mp3_ctx *c, pe_meta *m)
{
    if (!mpg_load()) return -1;
    memset(c, 0, sizeof(*c));
    int err = 0;
    c->h = g_mpg.p_new(NULL, &err);
    if (!c->h) return -1;
    if (g_mpg.p_open(c->h, path) != MPG123_OK) goto fail;
    long rate = 0;
    int ch = 0, enc = 0;
    if (g_mpg.p_getformat(c->h, &rate, &ch, &enc) != MPG123_OK) goto fail;
    if (enc != MPG123_ENC_SIGNED_16) {
        g_mpg.p_format_none(c->h);
        g_mpg.p_format(c->h, rate, ch, MPG123_ENC_SIGNED_16);
        if (g_mpg.p_getformat(c->h, &rate, &ch, &enc) != MPG123_OK)
            goto fail;
    }
    c->rate = (uint32_t)rate;
    c->channels = (uint16_t)ch;
    if (g_mpg.p_scan) g_mpg.p_scan(c->h);
    if (g_mpg.p_length) {
        int64_t len = g_mpg.p_length(c->h);
        if (len > 0) c->total_frames = (uint64_t)len;
    }
    if (m) {
        m->rate = c->rate;
        m->channels = c->channels;
        m->duration_ms = c->rate && c->total_frames
            ? (uint32_t)(c->total_frames * 1000 / c->rate) : 0;
    }
    return 0;
fail:
    g_mpg.p_close(c->h);
    g_mpg.p_delete(c->h);
    return -1;
}

static int mp3_read(mp3_ctx *c, int16_t *out, size_t want)
{
    size_t frame_bytes = c->channels * 2;
    size_t want_bytes = want * frame_bytes;
    uint8_t *dst = (uint8_t *)out;
    size_t have = 0;
    if (c->carry_n) {
        memcpy(dst, c->carry, c->carry_n);
        have = c->carry_n;
        c->carry_n = 0;
    }
    while (have < want_bytes) {
        size_t done = 0;
        int rc = g_mpg.p_read(c->h, dst + have, want_bytes - have, &done);
        have += done;
        if (rc == MPG123_DONE) break;
        if (rc != MPG123_OK && rc != MPG123_NEW_FORMAT) break;
        if (done == 0) break;
    }
    size_t frames = have / frame_bytes;
    size_t rem = have % frame_bytes;
    if (rem) {
        memcpy(c->carry, dst + frames * frame_bytes, rem);
        c->carry_n = rem;
    }
    return (int)frames;
}

/* ================= libsndfile via dlopen =================
 * Ogg Vorbis and FLAC decode + metadata. (libvorbisfile was
 * tried first: its OggVorbis_File is a caller-allocated struct
 * whose layout is only available from the development header,
 * and driving it with self-declared prototypes proved
 * unstable in practice. libsndfile's ABI is an opaque handle
 * plus the small, layout-stable SF_INFO - verified against
 * the runtime library on this host. WAV stays native and MP3
 * stays on libmpg123, so libsndfile is only a bonus path: if
 * it is absent, .ogg/.flac report as unprobeable.) */

typedef struct {
    int64_t frames;
    int samplerate;
    int channels;
    int format;
    int sections;
    int seekable;
} sf_info_min;

typedef void *(*fn_sf_open)(const char *, int, sf_info_min *);
typedef int64_t (*fn_sf_readf)(void *, int16_t *, int64_t);
typedef int64_t (*fn_sf_seek)(void *, int64_t, int);
typedef int (*fn_sf_close)(void *);
typedef const char *(*fn_sf_string)(void *, int);

typedef struct {
    void *dl;
    fn_sf_open p_open;
    fn_sf_readf p_readf;
    fn_sf_seek p_seek;
    fn_sf_close p_close;
    fn_sf_string p_string;
    bool ok;
} sf_api;

static sf_api g_sf;
static bool g_sf_tried;

static bool sf_load(void)
{
    if (g_sf_tried) return g_sf.ok;
    g_sf_tried = true;
    g_sf.dl = dlopen("libsndfile.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g_sf.dl) return false;
#define SYM(name) *(void **)&g_sf.p_##name = dlsym(g_sf.dl, "sf_" #name)
    SYM(open); SYM(close); SYM(seek);
    *(void **)&g_sf.p_string = dlsym(g_sf.dl, "sf_get_string");
    *(void **)&g_sf.p_readf = dlsym(g_sf.dl, "sf_readf_short");
#undef SYM
    g_sf.ok = g_sf.p_open && g_sf.p_readf && g_sf.p_close;
    return g_sf.ok;
}

#define SFM_READ 0x10
#define SF_STR_TITLE 0x01
#define SF_STR_ARTIST 0x04
#define SF_STR_ALBUM 0x07
#define SF_STR_GENRE 0x10

typedef struct {
    void *f;
    uint32_t rate;
    uint16_t channels;
    uint64_t total_frames;
} sf_ctx;

static int sf_open_ctx(const char *path, sf_ctx *c, pe_meta *m)
{
    if (!sf_load()) return -1;
    memset(c, 0, sizeof(*c));
    sf_info_min info;
    memset(&info, 0, sizeof(info));
    c->f = g_sf.p_open(path, SFM_READ, &info);
    if (!c->f) return -1;
    c->rate = (uint32_t)info.samplerate;
    c->channels = (uint16_t)info.channels;
    if (info.frames > 0) c->total_frames = (uint64_t)info.frames;
    if (m) {
        m->rate = c->rate;
        m->channels = c->channels;
        m->duration_ms = c->rate && c->total_frames
            ? (uint32_t)(c->total_frames * 1000 / c->rate) : 0;
        if (g_sf.p_string) {
            const char *s;
            if ((s = g_sf.p_string(c->f, SF_STR_TITLE)))
                copy_str(m->title, sizeof(m->title), s, strlen(s));
            if ((s = g_sf.p_string(c->f, SF_STR_ARTIST)))
                copy_str(m->artist, sizeof(m->artist), s, strlen(s));
            if ((s = g_sf.p_string(c->f, SF_STR_ALBUM)))
                copy_str(m->album, sizeof(m->album), s, strlen(s));
            if ((s = g_sf.p_string(c->f, SF_STR_GENRE)))
                copy_str(m->genre, sizeof(m->genre), s, strlen(s));
        }
    }
    return 0;
}

static int sf_read_frames(sf_ctx *c, int16_t *out, size_t want)
{
    int64_t n = g_sf.p_readf(c->f, out, (int64_t)want);
    return n < 0 ? 0 : (int)n;
}

/* ================= sinks ================= */

typedef struct { snd_pcm_t *pcm; } alsa_sink_ctx;
static int alsa_write(pe_out *o, const int16_t *buf, size_t frames)
{
    alsa_sink_ctx *c = o->ctx;
    size_t done = 0;
    while (done < frames) {
        snd_pcm_sframes_t n =
            snd_pcm_writei(c->pcm, buf + done * o->channels, frames - done);
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
static void alsa_sink_close(pe_out *o)
{
    alsa_sink_ctx *c = o->ctx;
    if (c->pcm) { snd_pcm_drain(c->pcm); snd_pcm_close(c->pcm); }
    free(c);
}

pe_out *pe_out_alsa(const char *device, uint32_t rate, uint16_t channels)
{
    snd_pcm_t *pcm = NULL;
    if (snd_pcm_open(&pcm, device ? device : "default",
                     SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0)
        return NULL;
    snd_pcm_hw_params_t *hp;
    snd_pcm_hw_params_alloca(&hp);
    uint32_t r = rate;
    if (snd_pcm_hw_params_any(pcm, hp) < 0 ||
        snd_pcm_hw_params_set_access(pcm, hp,
                                     SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        snd_pcm_hw_params_set_format(pcm, hp, SND_PCM_FORMAT_S16_LE) < 0 ||
        snd_pcm_hw_params_set_channels(pcm, hp, channels) < 0 ||
        snd_pcm_hw_params_set_rate_near(pcm, hp, &r, NULL) < 0 ||
        snd_pcm_hw_params(pcm, hp) < 0) {
        snd_pcm_close(pcm);
        return NULL;
    }
    pe_out *o = calloc(1, sizeof(*o));
    alsa_sink_ctx *c = calloc(1, sizeof(*c));
    if (!o || !c) { free(o); free(c); snd_pcm_close(pcm); return NULL; }
    c->pcm = pcm;
    o->ctx = c;
    o->write = alsa_write;
    o->close = alsa_sink_close;
    o->rate = rate;
    o->channels = channels;
    return o;
}

typedef struct { uint64_t frames; int peak; } null_ctx;
static int null_write(pe_out *o, const int16_t *buf, size_t frames)
{
    null_ctx *c = o->ctx;
    for (size_t i = 0; i < frames * o->channels; i++) {
        int v = buf[i];
        if (v < 0) v = -v;
        if (v > c->peak) c->peak = v;
    }
    c->frames += frames;
    return (int)frames;
}
static void null_sink_close(pe_out *o) { free(o->ctx); }

pe_out *pe_out_null(uint32_t rate, uint16_t channels)
{
    pe_out *o = calloc(1, sizeof(*o));
    null_ctx *c = calloc(1, sizeof(*c));
    if (!o || !c) { free(o); free(c); return NULL; }
    o->ctx = c;
    o->write = null_write;
    o->close = null_sink_close;
    o->rate = rate;
    o->channels = channels;
    return o;
}
uint64_t pe_out_null_frames(const pe_out *o)
{
    const null_ctx *c = o ? o->ctx : NULL;
    return c ? c->frames : 0;
}
int pe_out_null_peak(const pe_out *o)
{
    const null_ctx *c = o ? o->ctx : NULL;
    return c ? c->peak : 0;
}
void pe_out_free(pe_out *o)
{
    if (!o) return;
    if (o->close) o->close(o);
    free(o);
}

/* ================= engine ================= */

typedef enum { DEC_WAV, DEC_MP3, DEC_SF } dec_kind;

struct pe_engine {
    dec_kind kind;
    union { wav_ctx wav; mp3_ctx mp3; sf_ctx sf; } d;
    pe_meta meta;
    pe_out *out;            /* not owned */
    double volume;
    uint64_t frames_done;   /* frames decoded+emitted */
    bool eof;
};

static void title_from_filename(const char *path, pe_meta *m)
{
    if (m->title[0]) return;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    copy_str(m->title, sizeof(m->title), base, len);
}

int pe_probe(const char *path, pe_meta *out)
{
    memset(out, 0, sizeof(*out));
    if (ext_is(path, "wav")) {
        wav_ctx c;
        if (wav_open_ctx(path, &c, out) != 0) return -1;
        fclose(c.f);
    } else if (ext_is(path, "mp3")) {
        id3_parse_file(path, out);
        mp3_ctx c;
        if (mp3_open_ctx(path, &c, out) != 0) return -1;
        g_mpg.p_close(c.h);
        g_mpg.p_delete(c.h);
    } else if (ext_is(path, "ogg") || ext_is(path, "oga") ||
               ext_is(path, "flac")) {
        sf_ctx c;
        if (sf_open_ctx(path, &c, out) != 0) return -1;
        g_sf.p_close(c.f);
    } else {
        return -1;
    }
    title_from_filename(path, out);
    return 0;
}

pe_engine *pe_open(const char *path, pe_out *out_sink)
{
    pe_engine *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->out = out_sink;
    e->volume = 1.0;
    if (ext_is(path, "wav")) {
        e->kind = DEC_WAV;
        if (wav_open_ctx(path, &e->d.wav, &e->meta) != 0) goto fail;
    } else if (ext_is(path, "mp3")) {
        e->kind = DEC_MP3;
        id3_parse_file(path, &e->meta);
        if (mp3_open_ctx(path, &e->d.mp3, &e->meta) != 0) goto fail;
    } else if (ext_is(path, "ogg") || ext_is(path, "oga") ||
               ext_is(path, "flac")) {
        e->kind = DEC_SF;
        if (sf_open_ctx(path, &e->d.sf, &e->meta) != 0) goto fail;
    } else {
        goto fail;
    }
    title_from_filename(path, &e->meta);
    return e;
fail:
    free(e);
    return NULL;
}

void pe_close(pe_engine *e)
{
    if (!e) return;
    switch (e->kind) {
    case DEC_WAV: fclose(e->d.wav.f); break;
    case DEC_MP3:
        g_mpg.p_close(e->d.mp3.h);
        g_mpg.p_delete(e->d.mp3.h);
        break;
    case DEC_SF:
        g_sf.p_close(e->d.sf.f);
        break;
    }
    free(e);
}

const pe_meta *pe_engine_meta(const pe_engine *e) { return &e->meta; }

static int dec_read(pe_engine *e, int16_t *buf, size_t want)
{
    switch (e->kind) {
    case DEC_WAV: return wav_read(&e->d.wav, buf, want);
    case DEC_MP3: return mp3_read(&e->d.mp3, buf, want);
    case DEC_SF: return sf_read_frames(&e->d.sf, buf, want);
    }
    return -1;
}

int pe_pump(pe_engine *e, size_t max_frames)
{
    if (e->eof) return 0;
    int16_t *buf = malloc((max_frames ? max_frames : 1) *
                          e->meta.channels * sizeof(int16_t));
    if (!buf) return -1;
    int n = dec_read(e, buf, max_frames);
    if (n < 0) { free(buf); e->eof = true; return -1; }
    if (n == 0) { free(buf); e->eof = true; return 0; }
    if (e->volume < 0.999) {
        size_t total = (size_t)n * e->meta.channels;
        for (size_t i = 0; i < total; i++) {
            double v = buf[i] * e->volume;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            buf[i] = (int16_t)v;
        }
    }
    int written = e->out->write(e->out, buf, (size_t)n);
    free(buf);
    if (written < 0) { e->eof = true; return -1; }
    e->frames_done += (uint64_t)n;
    return n;
}

bool pe_eof(const pe_engine *e) { return e->eof; }

uint64_t pe_position_ms(const pe_engine *e)
{
    return e->meta.rate ? e->frames_done * 1000 / e->meta.rate : 0;
}

int pe_seek_ms(pe_engine *e, uint64_t ms)
{
    uint64_t frame = e->meta.rate ? ms * e->meta.rate / 1000 : 0;
    switch (e->kind) {
    case DEC_WAV: {
        wav_ctx *c = &e->d.wav;
        uint64_t bytes = frame * c->channels * (c->bits / 8);
        if (bytes > c->data_bytes) bytes = c->data_bytes;
        if (fseek(c->f, c->data_off + (long)bytes, SEEK_SET) != 0)
            return -1;
        c->data_pos = bytes;
        break;
    }
    case DEC_MP3:
        if (g_mpg.p_seek &&
            g_mpg.p_seek(e->d.mp3.h, (int64_t)frame, SEEK_SET) < 0)
            return -1;
        e->d.mp3.carry_n = 0;
        break;
    case DEC_SF:
        if (g_sf.p_seek && g_sf.p_seek(e->d.sf.f, (int64_t)frame, SEEK_SET) < 0)
            return -1;
        break;
    }
    e->frames_done = frame;
    e->eof = false;
    return 0;
}

void pe_set_volume(pe_engine *e, double vol)
{
    if (vol < 0) vol = 0;
    if (vol > 1) vol = 1;
    e->volume = vol;
}
double pe_volume(const pe_engine *e) { return e->volume; }
