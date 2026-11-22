/* mascot.c - 2D desktop mascot runtime. See mascot.h and
 * shell/mascot/FORMAT.md for the .bdm pack format.
 *
 * The pack is mmap'd read-only; all tables and the atlas are
 * used in place (zero-copy). The simulation advances in 40 ms
 * quanta (the Shimeji tick) driven by bd_mascot_tick(); pose
 * timing runs on the same clock the session passes in, so
 * tests with a virtual clock are deterministic.
 */
#include "mascot.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- pack layout (FORMAT.md sections 2 and 4) ---- */
#define BDM_HDR_SIZE 80
#define BDM_CLIP_SIZE 16
#define BDM_POOL_SIZE 4
#define BDM_SIZE_SIZE 24   /* v1 size record */
#define BDM_SIZE2_SIZE 32  /* v2 size record */
#define BDM_SIZE3_SIZE 48  /* v3 size record */
#define BDM_FRAME_SIZE 12
#define BDM_FRAME2_SIZE 20
#define BDM_PATCH_SIZE 12
#define BDM_POSE_SIZE 12

#define CLIP_STAY 0
#define CLIP_MOVE 1
#define CLIP_ANIMATE 2

/* v2 frame kinds */
#define KIND_FULL 0
#define KIND_SPARSE 1
#define KIND_SAME 2

#define MAX_TICK_BANDS 8

#define GRAV_Q8 160        /* 0.625 px/tick^2 in q8 */
#define FALL_MAX_Q8 (20 * 256)

typedef struct {
    uint16_t frame, dur_ms;
    int16_t ax, ay, vx_q8, vy_q8;
} bdm_pose;

typedef struct {
    uint16_t x, y, w, h;
    int16_t bx, by;
} bdm_frame;

struct bd_mascot {
    uint8_t *map;
    size_t map_len;
    int version;
    /* pack views (into the mmap) */
    const uint8_t *clipmeta, *pooltab, *strtab;
    int nclips, npool;
    int fall_clip, drag_clip, thrown_clip, stand_clip;
    int crop_w, crop_h;
    /* chosen size */
    int size_h, atlas_w, atlas_h, nframes, scale_q8, crop_w_s;
    const uint8_t *frames_tab, *poses_tab;
    const uint32_t *atlas;
    uint32_t *atlas_copy;   /* aligned copy, made at load when
                               the pack's atlas offset is not
                               4-byte aligned (see bd_mascot_new) */
    /* v2: sparse-delta composition */
    int frame_stride;              /* 12 (v1) or 20 (v2) */
    const uint8_t *patches_tab;    /* damage band records */
    const uint8_t *sparse_tab;     /* sparse run payloads */
    uint32_t *band_start;          /* per-frame band prefix sums */
    uint32_t *comp;                /* composed sprite buffer
                                      (crop_w_s x size_h) */
    /* v3: common-format extension sections (may be empty) */
    const uint8_t *regions_tab;    /* delta-encoded region lists */
    uint32_t regions_len;
    const uint8_t *rgb565_tab;     /* WinCE-profile full frames */
    uint32_t rgb565_len;
    tk_rect *rgn_scratch;          /* region decode scratch */
    int rgn_scratch_cap;
    bool comp_valid;
    int comp_clip, comp_pose;      /* pose the buffer shows */
    tk_rect comp_extent;           /* content extent, crop coords */
    tk_rect tick_bands[MAX_TICK_BANDS]; /* bands applied this tick */
    int tick_nbands;
    bool tick_full;                /* a FULL frame landed this tick */
    /* simulation */
    tk_rect floor_area;
    bool visible;
    int state;
    int clip, pose;
    long pose_end_ms, state_end_ms;
    int clip_total_ms, clip_natural_ms;
    long last_ms, acc_ms;
    int ax_q8, ay_q8;      /* anchor position, q8 fixed point */
    int vx_q8, vy_q8;      /* ballistic velocity (fall/throw) */
    int dir;               /* walk direction: -1 left, +1 right */
    bool look_right;
    /* pointer capture */
    bool captured, dragged;
    int grab_dx, grab_dy;  /* anchor - pointer at grab */
    int down_x, down_y;
    long down_ms;
    int last_px, last_py;
    long last_pmove_ms;
    int throw_vx_q8;
    uint32_t rng;
    tk_rect last_rect;     /* sprite rect as last reported */
    int last_pose_key;     /* (clip, pose, flip) last reported */
    bool hide_dirty;       /* a hide needs its rect reported */
};

static void start_clip(bd_mascot *m, int clip, long now);

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static int16_t rd16s(const uint8_t *p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rng_next(bd_mascot *m)
{
    uint32_t x = m->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x;
    return x;
}
static int rng_range(bd_mascot *m, int lo, int hi) /* [lo, hi] */
{
    if (hi <= lo) return lo;
    return lo + (int)(rng_next(m) % (uint32_t)(hi - lo + 1));
}

static const uint8_t *clip_meta(const bd_mascot *m, int i)
{
    return m->clipmeta + (size_t)i * BDM_CLIP_SIZE;
}
static int clip_type(const bd_mascot *m, int i)
{
    return clip_meta(m, i)[12];
}
static int clip_poses(const bd_mascot *m, int i)
{
    return rd16(clip_meta(m, i) + 2);
}
static bool clip_loop(const bd_mascot *m, int i)
{
    return clip_meta(m, i)[14] != 0;
}
static const bdm_pose *pose_at(const bd_mascot *m, int clip, int pose)
{
    uint32_t start = rd32(clip_meta(m, clip) + 8);
    return (const bdm_pose *)(m->poses_tab +
                              (size_t)(start + pose) * BDM_POSE_SIZE);
}
static const bdm_frame *frame_at(const bd_mascot *m, int idx)
{
    return (const bdm_frame *)(m->frames_tab +
                               (size_t)idx * BDM_FRAME_SIZE);
}

/* Unified frame view across format versions. v1 frames are
 * always KIND_FULL with a 12-byte record; v2 frames carry a
 * kind, a damage-band count and (for SPARSE) a byte offset
 * into the sparse section. */
typedef struct {
    uint16_t x, y, w, h;
    int16_t bx, by;
    int kind, nband;
    uint32_t sparse_off;
} frame_info;

static void frame_info_at(const bd_mascot *m, int idx,
                          frame_info *o)
{
    const uint8_t *p = m->frames_tab +
                       (size_t)idx * (size_t)m->frame_stride;
    o->x = rd16(p);
    o->y = rd16(p + 2);
    o->w = rd16(p + 4);
    o->h = rd16(p + 6);
    o->bx = rd16s(p + 8);
    o->by = rd16s(p + 10);
    if (m->version >= 2) {
        o->kind = rd16(p + 12);
        o->nband = rd16(p + 14);
        o->sparse_off = rd32(p + 16);
    } else {
        o->kind = KIND_FULL;
        o->nband = 0;
        o->sparse_off = 0;
    }
}

static int pose_frame(const bd_mascot *m, int clip, int pose)
{
    return pose_at(m, clip, pose)->frame;
}

const char *bd_mascot_clip_name(const bd_mascot *m)
{
    if (!m || m->clip < 0) return "";
    return (const char *)(m->strtab + rd16(clip_meta(m, m->clip)));
}

/* ---------------- loading ---------------- */

bd_mascot *bd_mascot_new(const bd_mascot_config *cfg)
{
    if (!cfg || !cfg->pack_path) return NULL;
    int fd = open(cfg->pack_path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < BDM_HDR_SIZE) {
        close(fd);
        return NULL;
    }
    uint8_t *map = mmap(NULL, (size_t)st.st_size, PROT_READ,
                        MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return NULL;
    if (memcmp(map, "BDM1", 4) != 0 ||
        (rd16(map + 4) != 1 && rd16(map + 4) != 2 &&
         rd16(map + 4) != 3) ||
        rd32(map + 8) != (uint32_t)st.st_size) {
        munmap(map, (size_t)st.st_size);
        return NULL;
    }
    bd_mascot *m = calloc(1, sizeof(*m));
    if (!m) {
        munmap(map, (size_t)st.st_size);
        return NULL;
    }
    m->map = map;
    m->map_len = (size_t)st.st_size;
    m->version = rd16(map + 4);
    m->crop_w = rd16(map + 36);
    m->crop_h = rd16(map + 38);
    m->nclips = rd16(map + 40);
    m->npool = rd16(map + 42);
    int nsizes = rd16(map + 44);
    m->fall_clip = rd16s(map + 48);
    m->drag_clip = rd16s(map + 50);
    m->thrown_clip = rd16s(map + 52);
    m->stand_clip = rd16s(map + 54);
    m->clipmeta = map + rd32(map + 56);
    m->pooltab = map + rd32(map + 60);
    const uint8_t *sizes = map + rd32(map + 64);
    m->strtab = map + rd32(map + 68);
    if (m->nclips <= 0 || nsizes <= 0) {
        bd_mascot_free(m);
        return NULL;
    }
    /* nearest baked size to the requested height */
    int rec_size = m->version >= 3 ? BDM_SIZE3_SIZE
                   : m->version >= 2 ? BDM_SIZE2_SIZE
                                     : BDM_SIZE_SIZE;
    int want = cfg->size_px > 0 ? cfg->size_px : 208;
    int best = 0, bestdiff = 1 << 30;
    for (int i = 0; i < nsizes; i++) {
        int h = rd16(sizes + (size_t)i * (size_t)rec_size);
        int d = h > want ? h - want : want - h;
        if (d < bestdiff) {
            bestdiff = d;
            best = i;
        }
    }
    const uint8_t *sr = sizes + (size_t)best * (size_t)rec_size;
    m->size_h = rd16(sr);
    m->atlas_w = rd16(sr + 2);
    m->atlas_h = rd16(sr + 4);
    m->nframes = rd16(sr + 6);
    if (m->version >= 2) {
        m->frames_tab = map + rd32(sr + 8);
        m->patches_tab = map + rd32(sr + 12);
        m->sparse_tab = map + rd32(sr + 16);
        m->poses_tab = map + rd32(sr + 20);
        m->atlas = (const uint32_t *)(map + rd32(sr + 24));
        m->scale_q8 = rd16(sr + 28);
        m->frame_stride = rd16(sr + 30);
        if (!m->frame_stride) m->frame_stride = BDM_FRAME2_SIZE;
        if (m->version >= 3) {
            m->regions_tab = map + rd32(sr + 32);
            m->regions_len = rd32(sr + 36);
            m->rgb565_tab = map + rd32(sr + 40);
            m->rgb565_len = rd32(sr + 44);
        }
    } else {
        m->frames_tab = map + rd32(sr + 8);
        m->poses_tab = map + rd32(sr + 12);
        m->atlas = (const uint32_t *)(map + rd32(sr + 16));
        m->scale_q8 = rd16(sr + 20);
        m->frame_stride = BDM_FRAME_SIZE;
    }
    /* The composed canvas must have EXACTLY the width the
     * converter baked at: nw = round(crop_w * size_h / crop_h)
     * with the converter's scale = size_h / crop_h. The sparse
     * payload is a flat pixel stream over that width, so a
     * 1 px disagreement shears every sparse write diagonally
     * (the Mao glitch: crop 829x1200 at 320 px baked 221 wide,
     * while the old q8 formula below gave 220). Compute the
     * width on the exact rational with round-half-even, which
     * reproduces the converter's round() for every real pack;
     * FORMAT.md now defines the width this way. */
    {
        long n = (long)m->crop_w * (long)m->size_h;
        long d = m->crop_h > 0 ? (long)m->crop_h : 1;
        long q = n / d, r = n % d;
        if (2 * r > d || (2 * r == d && (q & 1))) q++;
        m->crop_w_s = (int)q;
    }
    /* sanity: the tables we will read must lie in the file */
    {
        const uint8_t *end = map + m->map_len;
        bool ok = m->frames_tab >= map &&
                  m->frames_tab +
                          (size_t)m->nframes * m->frame_stride <=
                      end &&
                  m->poses_tab >= map && m->poses_tab < end &&
                  (const uint8_t *)m->atlas >= map &&
                  (const uint8_t *)m->atlas +
                          (size_t)m->atlas_w * m->atlas_h * 4 <=
                      end;
        if (ok && m->version >= 2)
            ok = m->patches_tab >= map &&
                 m->patches_tab <= m->sparse_tab &&
                 m->sparse_tab <= m->poses_tab;
        if (ok && m->version >= 3)
            ok = m->regions_tab >= map &&
                 m->regions_tab + m->regions_len <= end &&
                 m->rgb565_tab >= map &&
                 m->rgb565_tab + m->rgb565_len <= end;
        if (!ok) {
            bd_mascot_free(m);
            return NULL;
        }
    }
    /* The pack format does not guarantee the atlas offset is
     * 4-byte aligned, but the blit paths read atlas pixels as
     * uint32_t (blit_sprite). On the ARMv5 target an unaligned
     * word load silently returns rotated data, so an unaligned
     * atlas is copied once into an owned aligned buffer at
     * load; aligned packs keep the zero-copy mmap path. (Found
     * by UBSan's alignment check, foundation audit.) */
    if ((uintptr_t)m->atlas & 3u) {
        size_t bytes = (size_t)m->atlas_w * (size_t)m->atlas_h * 4;
        m->atlas_copy = malloc(bytes ? bytes : 4);
        if (!m->atlas_copy) {
            bd_mascot_free(m);
            return NULL;
        }
        memcpy(m->atlas_copy, m->atlas, bytes);
        m->atlas = m->atlas_copy;
    }
    if (m->version >= 2) {
        m->comp = calloc((size_t)m->crop_w_s *
                             (size_t)m->size_h,
                         sizeof(uint32_t));
        m->band_start = malloc(sizeof(uint32_t) *
                               (size_t)(m->nframes + 1));
        if (!m->comp || !m->band_start) {
            bd_mascot_free(m);
            return NULL;
        }
        uint32_t acc = 0;
        for (int i = 0; i < m->nframes; i++) {
            frame_info fi;
            m->band_start[i] = acc;
            frame_info_at(m, i, &fi);
            acc += (uint32_t)fi.nband;
        }
        m->band_start[m->nframes] = acc;
        if (m->patches_tab + (size_t)acc * BDM_PATCH_SIZE >
            m->sparse_tab) {
            bd_mascot_free(m);
            return NULL;
        }
    }

    m->floor_area = cfg->floor_area;
    m->rng = cfg->seed ? cfg->seed : 0x9E3779B9u;
    m->visible = true;
    m->clip = -1;
    m->last_pose_key = -1;
    /* Spawn in the air over the middle of the floor (feet one
     * body-height below the area top, so the sprite is fully
     * on screen) and fall. */
    int cx = m->floor_area.x + m->floor_area.w / 2;
    m->ax_q8 = cx * 256;
    m->ay_q8 = (m->floor_area.y + m->size_h) * 256;
    m->state = BD_MASCOT_FALL;
    if (m->fall_clip >= 0)
        start_clip(m, m->fall_clip, 0);
    if (m->version >= 2 && m->clip >= 0) {
        /* pre-tick extent: pose 0 of the starting clip is a
         * FULL frame by construction */
        frame_info fi;
        frame_info_at(m, pose_frame(m, m->clip, 0), &fi);
        m->comp_extent = (tk_rect){ fi.bx, fi.by, fi.w, fi.h };
    }
    return m;
}

void bd_mascot_free(bd_mascot *m)
{
    if (!m) return;
    if (m->map) munmap(m->map, m->map_len);
    free(m->atlas_copy);
    free(m->comp);
    free(m->band_start);
    free(m->rgn_scratch);
    free(m);
}

/* ---------------- v3 common-format accessors ---------------- */

int bd_mascot_pack_version(const bd_mascot *m)
{
    return m ? m->version : 0;
}

bool bd_mascot_has_regions(const bd_mascot *m)
{
    return m && m->regions_len > 0;
}

static const uint8_t *rgn_skip(const uint8_t *p)
{
    uint16_t ndel = rd16(p), nadd = rd16(p + 2);
    p += 4;
    if (ndel == 0xFFFF)
        p += (size_t)nadd * 8;
    else
        p += (size_t)ndel * 2 + (size_t)nadd * 8;
    return p;
}

static bool rgn_reserve(bd_mascot *m, int cap)
{
    if (cap <= m->rgn_scratch_cap) return true;
    tk_rect *nb = realloc(m->rgn_scratch,
                           sizeof(tk_rect) * (size_t)cap);
    if (!nb) return false;
    m->rgn_scratch = nb;
    m->rgn_scratch_cap = cap;
    return true;
}

/* Decode the precomputed window-region list for (clip, pose)
 * from the v3 region section: entries are sequential per clip,
 * the first entry of a clip is a full list, later entries are
 * remove/add deltas against the previous pose's list. Returns
 * the rect count (writing up to max into out), or -1 when the
 * pack has no region section or the pose is out of range. The
 * Brainux runtime itself paints from damage bands and does not
 * need regions; this exists for cross-verification with the
 * WinCE profile and for tests. */
int bd_mascot_regions_at(bd_mascot *m, int clip, int pose,
                         tk_rect *out, int max)
{
    if (!m || !m->regions_tab || clip < 0 || clip >= m->nclips)
        return -1;
    const uint8_t *p = m->regions_tab;
    const uint8_t *end = m->regions_tab + m->regions_len;
    for (int c = 0; c < clip; c++) {
        int np = rd16(m->clipmeta + (size_t)c * BDM_CLIP_SIZE + 2);
        for (int i = 0; i < np; i++) {
            if (p + 4 > end) return -1;
            p = rgn_skip(p);
        }
    }
    int np = rd16(m->clipmeta + (size_t)clip * BDM_CLIP_SIZE + 2);
    if (pose < 0 || pose >= np) return -1;
    int n = 0;
    for (int i = 0; i <= pose; i++) {
        if (p + 4 > end) return -1;
        uint16_t ndel = rd16(p), nadd = rd16(p + 2);
        p += 4;
        if (ndel == 0xFFFF) {
            if (!rgn_reserve(m, nadd)) return -1;
            n = 0;
            for (int k = 0; k < nadd; k++, p += 8) {
                if (p + 8 > end) return -1;
                m->rgn_scratch[n++] = (tk_rect){
                    rd16(p), rd16(p + 2), rd16(p + 4),
                    rd16(p + 6)};
            }
        } else {
            if (p + (size_t)ndel * 2 + (size_t)nadd * 8 > end)
                return -1;
            const uint8_t *idx = p;
            p += (size_t)ndel * 2;
            int w = 0;
            for (int r = 0; r < n; r++) {
                bool drop = false;
                for (int k = 0; k < ndel; k++)
                    if (rd16(idx + (size_t)k * 2) == r) {
                        drop = true;
                        break;
                    }
                if (!drop) m->rgn_scratch[w++] = m->rgn_scratch[r];
            }
            n = w;
            if (!rgn_reserve(m, n + nadd)) return -1;
            for (int k = 0; k < nadd; k++, p += 8)
                m->rgn_scratch[n++] = (tk_rect){
                    rd16(p), rd16(p + 2), rd16(p + 4),
                    rd16(p + 6)};
        }
    }
    if (out && max > 0) {
        int c = n < max ? n : max;
        memcpy(out, m->rgn_scratch, sizeof(tk_rect) * (size_t)c);
    }
    return n;
}

int bd_mascot_size(const bd_mascot *m) { return m ? m->size_h : 0; }

void bd_mascot_stats(const bd_mascot *m, size_t *map_bytes,
                     int *clips, int *frames, int *pool)
{
    if (!m) return;
    if (map_bytes) *map_bytes = m->map_len;
    if (clips) *clips = m->nclips;
    if (frames) *frames = m->nframes;
    if (pool) *pool = m->npool;
}

void bd_mascot_set_floor(bd_mascot *m, tk_rect floor_area)
{
    if (m) m->floor_area = floor_area;
}

void bd_mascot_set_visible(bd_mascot *m, bool visible)
{
    if (!m || m->visible == visible) return;
    m->visible = visible;
    if (!visible) m->hide_dirty = true;
}

bool bd_mascot_visible(const bd_mascot *m)
{
    return m && m->visible;
}

int bd_mascot_state(const bd_mascot *m) { return m ? m->state : -1; }

bool bd_mascot_animating(const bd_mascot *m)
{
    if (!m || !m->visible || m->clip < 0) return false;
    return clip_poses(m, m->clip) > 1;
}

/* ---------------- behaviour ---------------- */

static int floor_y(const bd_mascot *m)
{
    return m->floor_area.y + m->floor_area.h;
}

/* Start a clip: pick a total duration inside its baked range
 * and remember the natural (pose-sum) duration for scaling. */
static void start_clip(bd_mascot *m, int clip, long now)
{
    m->clip = clip;
    m->pose = 0;
    int np = clip_poses(m, clip);
    int natural = 0;
    for (int i = 0; i < np; i++) natural += pose_at(m, clip, i)->dur_ms;
    m->clip_natural_ms = natural > 0 ? natural : 1;
    int tmin = rd16(clip_meta(m, clip) + 4);
    int tmax = rd16(clip_meta(m, clip) + 6);
    if (tmax < tmin) tmax = tmin;
    m->clip_total_ms = (tmin == tmax) ? tmin
                                      : rng_range(m, tmin, tmax);
    if (m->clip_total_ms <= 0) m->clip_total_ms = m->clip_natural_ms;
    const bdm_pose *p = pose_at(m, clip, 0);
    m->pose_end_ms = now + (long)p->dur_ms * m->clip_total_ms /
                           m->clip_natural_ms;
}

static void pick_next(bd_mascot *m, long now)
{
    /* weighted pool pick */
    int total = 0;
    for (int i = 0; i < m->npool; i++)
        total += rd16(m->pooltab + (size_t)i * BDM_POOL_SIZE + 2);
    int clip = -1;
    if (total > 0) {
        int r = rng_range(m, 0, total - 1);
        for (int i = 0; i < m->npool; i++) {
            int w = rd16(m->pooltab + (size_t)i * BDM_POOL_SIZE + 2);
            if (r < w) {
                clip = rd16(m->pooltab + (size_t)i * BDM_POOL_SIZE);
                break;
            }
            r -= w;
        }
    }
    if (clip < 0 || clip >= m->nclips)
        clip = m->stand_clip >= 0 ? m->stand_clip : 0;
    start_clip(m, clip, now);
    if (clip_type(m, clip) == CLIP_MOVE) {
        m->state = BD_MASCOT_WALK;
        m->dir = (rng_next(m) & 1) ? 1 : -1;
        m->state_end_ms = now + rng_range(m, 4000, 12000);
    } else {
        m->state = BD_MASCOT_IDLE;
        m->state_end_ms = now + rng_range(m, 3000, 9000);
    }
}

/* Advance pose playback; returns true when the clip finished
 * (non-looping and ran past the last pose). */
static bool advance_pose(bd_mascot *m, long now)
{
    if (m->clip < 0) return true;
    int np = clip_poses(m, m->clip);
    while (now >= m->pose_end_ms) {
        m->pose++;
        if (m->pose >= np) {
            if (clip_loop(m, m->clip) &&
                (m->state == BD_MASCOT_IDLE ||
                 m->state == BD_MASCOT_WALK ||
                 m->state == BD_MASCOT_DRAG ||
                 m->state == BD_MASCOT_FALL)) {
                m->pose = 0;
            } else {
                m->pose = np - 1;
                return true;
            }
        }
        const bdm_pose *p = pose_at(m, m->clip, m->pose);
        long d = (long)p->dur_ms * m->clip_total_ms /
                 m->clip_natural_ms;
        m->pose_end_ms += d > 0 ? d : 1;
        if (m->pose_end_ms <= now && d <= 0) break;
    }
    return false;
}

/* One 40 ms simulation quantum. */
static void step40(bd_mascot *m)
{
    int fy = floor_y(m);
    switch (m->state) {
    case BD_MASCOT_FALL: {
        m->vy_q8 += GRAV_Q8;
        if (m->vy_q8 > FALL_MAX_Q8) m->vy_q8 = FALL_MAX_Q8;
        m->ax_q8 += m->vx_q8;
        m->ay_q8 += m->vy_q8;
        int minx = (m->floor_area.x - m->size_h / 2) * 256;
        int maxx = (m->floor_area.x + m->floor_area.w +
                    m->size_h / 2) * 256;
        if (m->ax_q8 < minx) { m->ax_q8 = minx; m->vx_q8 = 0; }
        if (m->ax_q8 > maxx) { m->ax_q8 = maxx; m->vx_q8 = 0; }
        if (m->ay_q8 >= fy * 256) {
            m->ay_q8 = fy * 256;
            m->vx_q8 = m->vy_q8 = 0;
            m->state = BD_MASCOT_IDLE;
            if (m->stand_clip >= 0) {
                start_clip(m, m->stand_clip, m->last_ms);
                m->state_end_ms = m->last_ms +
                                  rng_range(m, 1200, 3000);
            } else {
                pick_next(m, m->last_ms);
            }
        }
        break;
    }
    case BD_MASCOT_WALK:
    case BD_MASCOT_IDLE:
    case BD_MASCOT_REACT: {
        if (m->clip >= 0) {
            const bdm_pose *p = pose_at(m, m->clip, m->pose);
            int vx = m->look_right ? -p->vx_q8 : p->vx_q8;
            int vy = p->vy_q8;
            m->ax_q8 += vx;
            m->ay_q8 += vy;
            if (m->ay_q8 > fy * 256) m->ay_q8 = fy * 256;
            int x = m->ax_q8 / 256;
            int lo = m->floor_area.x + 8;
            int hi = m->floor_area.x + m->floor_area.w - 8;
            if (x < lo || x > hi) {
                m->ax_q8 = (x < lo ? lo : hi) * 256;
                if (m->state == BD_MASCOT_WALK) {
                    m->dir = -m->dir;
                    m->look_right = m->dir > 0;
                }
            }
        }
        break;
    }
    default:
        break; /* DRAG: the pointer drives the anchor */
    }
    if (m->state == BD_MASCOT_WALK)
        m->look_right = m->dir > 0;
}

/* Current sprite rect from the current pose (empty if none).
 * v1: the pose's own frame. v2: the composed buffer's content
 * extent (the last FULL frame's rect grown by the patches
 * applied since), positioned by the current pose's anchor. */
static tk_rect sprite_rect(const bd_mascot *m)
{
    if (m->clip < 0) return (tk_rect){ 0, 0, 0, 0 };
    const bdm_pose *p = pose_at(m, m->clip, m->pose);
    int ax = m->ax_q8 / 256, ay = m->ay_q8 / 256;
    if (m->version >= 2) {
        tk_rect e = m->comp_extent;
        if (e.w <= 0) return (tk_rect){ 0, 0, 0, 0 };
        int dx = m->look_right ? ax + p->ax - e.x - e.w
                               : ax + e.x - p->ax;
        int dy = ay + e.y - p->ay;
        return (tk_rect){ dx, dy, e.w, e.h };
    }
    const bdm_frame *f = frame_at(m, p->frame);
    int dx = m->look_right ? ax + p->ax - f->bx - (int)f->w
                           : ax + f->bx - p->ax;
    int dy = ay + f->by - p->ay;
    return (tk_rect){ dx, dy, f->w, f->h };
}

/* ---------------- v2 composition ---------------- */

static tk_rect rect_union(tk_rect a, tk_rect b)
{
    if (a.w <= 0) return b;
    if (b.w <= 0) return a;
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (tk_rect){ x0, y0, x1 - x0, y1 - y0 };
}

static void tick_band_add(bd_mascot *m, tk_rect band)
{
    if (band.w <= 0 || band.h <= 0) return;
    if (m->tick_nbands < MAX_TICK_BANDS) {
        m->tick_bands[m->tick_nbands++] = band;
    } else {
        /* overflow: grow the last band into the union */
        m->tick_bands[MAX_TICK_BANDS - 1] =
            rect_union(m->tick_bands[MAX_TICK_BANDS - 1], band);
    }
}

/* Copy an atlas rect into the composed buffer at (bx, by),
 * clipped to the buffer. */
static void comp_copy_rect(bd_mascot *m, int ax_, int ay_,
                           int w, int h, int bx, int by)
{
    if (bx < 0) { ax_ -= bx; w += bx; bx = 0; }
    if (by < 0) { ay_ -= by; h += by; by = 0; }
    if (bx + w > m->crop_w_s) w = m->crop_w_s - bx;
    if (by + h > m->size_h) h = m->size_h - by;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++)
        memcpy(m->comp + (size_t)(by + r) * m->crop_w_s + bx,
               m->atlas + (size_t)(ay_ + r) * m->atlas_w + ax_,
               (size_t)w * sizeof(uint32_t));
}

/* Apply one frame to the composed buffer, recording this
 * tick's damage side effects (tick_full / tick_bands) and
 * growing the content extent. */
static void comp_apply(bd_mascot *m, int frame_idx)
{
    frame_info fi;
    frame_info_at(m, frame_idx, &fi);
    if (fi.kind == KIND_FULL) {
        memset(m->comp, 0, (size_t)m->crop_w_s *
                               (size_t)m->size_h *
                               sizeof(uint32_t));
        comp_copy_rect(m, fi.x, fi.y, fi.w, fi.h, fi.bx, fi.by);
        m->comp_extent = (tk_rect){ fi.bx, fi.by, fi.w, fi.h };
        m->tick_full = true;
    } else if (fi.kind == KIND_SPARSE) {
        const uint8_t *sp = m->sparse_tab + fi.sparse_off;
        const uint8_t *send = m->poses_tab; /* section end */
        size_t total = (size_t)m->crop_w_s * (size_t)m->size_h;
        size_t pos = 0;
        while (sp + 4 <= send) {
            uint16_t skip = rd16(sp), len = rd16(sp + 2);
            sp += 4;
            if (skip == 0 && len == 0) break;
            pos += skip;
            if (len) {
                if (pos + len > total ||
                    sp + (size_t)len * 4 > send)
                    break; /* corrupt pack: stop, stay safe */
                memcpy(m->comp + pos, sp,
                       (size_t)len * sizeof(uint32_t));
                sp += (size_t)len * sizeof(uint32_t);
                pos += len;
            }
        }
        uint32_t bs = m->band_start[frame_idx];
        for (int k = 0; k < fi.nband; k++) {
            const uint8_t *pr = m->patches_tab +
                                (size_t)(bs + k) * BDM_PATCH_SIZE;
            tk_rect band = { rd16s(pr + 8), rd16s(pr + 10),
                             rd16(pr + 4), rd16(pr + 6) };
            m->comp_extent = rect_union(m->comp_extent, band);
            tick_band_add(m, band);
        }
    }
    /* KIND_SAME: the buffer already shows this pose */
}

/* Bring the composed buffer to the current (clip, pose).
 * Forward walks apply each intermediate frame (cheap: sparse
 * runs are memcpy-class); anything else resyncs from the
 * nearest FULL frame at or before the target pose - pose 0
 * of every clip is FULL, so the scan always terminates. */
static void comp_sync(bd_mascot *m)
{
    if (m->version < 2 || m->clip < 0) return;
    m->tick_full = false;
    m->tick_nbands = 0;
    if (m->comp_valid && m->comp_clip == m->clip) {
        if (m->comp_pose == m->pose) return;
        if (m->pose > m->comp_pose) {
            for (int p = m->comp_pose + 1; p <= m->pose; p++)
                comp_apply(m, pose_frame(m, m->clip, p));
            m->comp_pose = m->pose;
            return;
        }
    }
    int start = m->pose;
    while (start > 0) {
        frame_info fi;
        frame_info_at(m, pose_frame(m, m->clip, start), &fi);
        if (fi.kind == KIND_FULL) break;
        start--;
    }
    for (int p = start; p <= m->pose; p++)
        comp_apply(m, pose_frame(m, m->clip, p));
    m->comp_clip = m->clip;
    m->comp_pose = m->pose;
    m->comp_valid = true;
}

tk_rect bd_mascot_rect(const bd_mascot *m)
{
    if (!m || !m->visible) return (tk_rect){ 0, 0, 0, 0 };
    return sprite_rect(m);
}

int bd_mascot_tick(bd_mascot *m, long now_ms, tk_rect *dmg, int max)
{
    if (!m) return 0;
    if (!m->visible) {
        if (m->hide_dirty) {
            m->hide_dirty = false;
            if (max > 0 && m->last_rect.w > 0) {
                dmg[0] = m->last_rect;
                m->last_rect = (tk_rect){ 0, 0, 0, 0 };
                m->last_pose_key = -1;
                return 1;
            }
            m->last_rect = (tk_rect){ 0, 0, 0, 0 };
        }
        m->last_ms = now_ms;
        return 0;
    }
    if (m->last_ms == 0) m->last_ms = now_ms;
    /* behaviour timers */
    if (m->state == BD_MASCOT_IDLE || m->state == BD_MASCOT_WALK) {
        if (now_ms >= m->state_end_ms) pick_next(m, now_ms);
    }
    bool clip_done = advance_pose(m, now_ms);
    if (clip_done && m->state == BD_MASCOT_REACT)
        pick_next(m, now_ms);
    /* physics in 40 ms quanta (cap catch-up at 5 steps) */
    m->acc_ms += now_ms - m->last_ms;
    m->last_ms = now_ms;
    int steps = 0;
    while (m->acc_ms >= 40 && steps < 5) {
        step40(m);
        m->acc_ms -= 40;
        steps++;
    }
    if (m->acc_ms > 200) m->acc_ms = 200;
    /* damage */
    if (m->version >= 2) comp_sync(m);
    tk_rect cur = sprite_rect(m);
    int key = m->clip < 0 ? -1
                          : (m->clip * 4096 + m->pose * 2 +
                             (m->look_right ? 1 : 0));
    int n = 0;
    bool rect_same = cur.x == m->last_rect.x &&
                     cur.y == m->last_rect.y &&
                     cur.w == m->last_rect.w &&
                     cur.h == m->last_rect.h;
    if (m->version >= 2 && m->comp_valid && rect_same &&
        m->last_pose_key >= 0 &&
        ((key ^ m->last_pose_key) & 1) == 0) {
        /* Same rect, same facing: only the pixels this tick's
         * frames touched can differ. A FULL frame (clip start,
         * keyframe, resync) repaints the whole extent; sparse
         * bands repaint just their rows - this is what keeps a
         * 30 fps full-body clip at a few KB per step. */
        if (m->tick_full) {
            if (cur.w > 0 && n < max) dmg[n++] = cur;
        } else {
            for (int i = 0; i < m->tick_nbands && n < max; i++) {
                tk_rect b = m->tick_bands[i];
                int sx = m->look_right
                             ? cur.x + m->comp_extent.w -
                                   (b.x - m->comp_extent.x) - b.w
                             : cur.x + (b.x - m->comp_extent.x);
                dmg[n++] = (tk_rect){
                    sx, cur.y + (b.y - m->comp_extent.y),
                    b.w, b.h };
            }
        }
        m->last_rect = cur;
        m->last_pose_key = key;
    } else if (key != m->last_pose_key || !rect_same) {
        if (m->last_rect.w > 0 && n < max) dmg[n++] = m->last_rect;
        if (cur.w > 0 && n < max) dmg[n++] = cur;
        m->last_rect = cur;
        m->last_pose_key = key;
    }
    return n;
}

/* ---------------- painting ---------------- */

/* Blend from a premultiplied source rect (v1: the pose's
 * atlas frame; v2: the composed buffer's extent) into the
 * canvas. dst (sprite_rect) always matches srect's size. */
/* Premultiplied src-over blend of one pixel. */
static uint32_t blend_over(uint32_t s, uint32_t d)
{
    uint32_t a = s >> 24;
    if (a == 0) return d;
    if (a == 255) return s;
    uint32_t ia = 255 - a;
    uint32_t r = (s >> 16 & 0xFF) + ((d >> 16 & 0xFF) * ia) / 255;
    uint32_t g = (s >> 8 & 0xFF) + ((d >> 8 & 0xFF) * ia) / 255;
    uint32_t b = (s & 0xFF) + ((d & 0xFF) * ia) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void blit_sprite(bd_mascot *m, tk_canvas *c, tk_rect clip_r,
                        const uint32_t *src, int src_stride,
                        tk_rect srect)
{
    tk_rect dst = sprite_rect(m);
    int x0 = dst.x > clip_r.x ? dst.x : clip_r.x;
    int y0 = dst.y > clip_r.y ? dst.y : clip_r.y;
    int x1 = dst.x + dst.w < clip_r.x + clip_r.w
                 ? dst.x + dst.w
                 : clip_r.x + clip_r.w;
    int y1 = dst.y + dst.h < clip_r.y + clip_r.h
                 ? dst.y + dst.h
                 : clip_r.y + clip_r.h;
    int cw = tk_canvas_width(c), ch = tk_canvas_height(c);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cw) x1 = cw;
    if (y1 > ch) y1 = ch;
    if (x1 <= x0 || y1 <= y0) return;
    uint32_t *px = tk_canvas_pixels(c);
    for (int y = y0; y < y1; y++) {
        int sy = srect.y + (y - dst.y);
        const uint32_t *srow = src + (size_t)sy * (size_t)src_stride;
        uint32_t *drow = px + (size_t)y * cw;
        for (int x = x0; x < x1; x++) {
            int sx = m->look_right
                         ? srect.x + (dst.w - 1 - (x - dst.x))
                         : srect.x + (x - dst.x);
            drow[x] = blend_over(srow[sx], drow[x]);
        }
    }
}

/* Paint the sprite restricted to the UNION of the clip rects,
 * blending every canvas pixel at most once. The v2 damage
 * bands overlap each other by design (they are bounding boxes
 * of merged change runs), so painting rect by rect blends the
 * semi-transparent pixels inside the overlaps twice, leaving
 * bright/dark fringes along band edges (the second half of
 * the Mao glitch). Instead, walk the sprite row by row, merge
 * the clips' x-spans for that row, and blend each merged span
 * exactly once. */
static void blit_sprite_clipped(bd_mascot *m, tk_canvas *c,
                                const tk_rect *clips, size_t nclips,
                                const uint32_t *src, int src_stride,
                                tk_rect srect)
{
    tk_rect dst = sprite_rect(m);
    int cw = tk_canvas_width(c), ch = tk_canvas_height(c);
    uint32_t *px = tk_canvas_pixels(c);
    int y0 = dst.y < 0 ? 0 : dst.y;
    int y1 = dst.y + dst.h > ch ? ch : dst.y + dst.h;
    for (int y = y0; y < y1; y++) {
        struct { int x0, x1; } spans[128];
        int ns = 0;
        for (size_t i = 0; i < nclips; i++) {
            tk_rect r = clips[i];
            if (y < r.y || y >= r.y + r.h) continue;
            int x0 = r.x > dst.x ? r.x : dst.x;
            int x1 = r.x + r.w < dst.x + dst.w
                         ? r.x + r.w
                         : dst.x + dst.w;
            if (x0 < 0) x0 = 0;
            if (x1 > cw) x1 = cw;
            if (x1 <= x0) continue;
            if (ns < 128) {
                spans[ns].x0 = x0;
                spans[ns].x1 = x1;
                ns++;
            } else {
                /* Unreachable with real callers (the session
                 * caps its list at 64): widen the last span
                 * rather than dropping coverage. */
                if (x0 < spans[127].x0) spans[127].x0 = x0;
                if (x1 > spans[127].x1) spans[127].x1 = x1;
            }
        }
        if (!ns) continue;
        for (int i = 1; i < ns; i++) {   /* insertion sort */
            int ax0 = spans[i].x0, ax1 = spans[i].x1, j = i - 1;
            while (j >= 0 && spans[j].x0 > ax0) {
                spans[j + 1] = spans[j];
                j--;
            }
            spans[j + 1].x0 = ax0;
            spans[j + 1].x1 = ax1;
        }
        int sy = srect.y + (y - dst.y);
        const uint32_t *srow = src + (size_t)sy * (size_t)src_stride;
        uint32_t *drow = px + (size_t)y * cw;
        int cur0 = spans[0].x0, cur1 = spans[0].x1;
        for (int i = 1; i <= ns; i++) {
            if (i < ns && spans[i].x0 <= cur1) {
                if (spans[i].x1 > cur1) cur1 = spans[i].x1;
                continue;
            }
            for (int x = cur0; x < cur1; x++) {
                int sx = m->look_right
                             ? srect.x + (dst.w - 1 - (x - dst.x))
                             : srect.x + (x - dst.x);
                drow[x] = blend_over(srow[sx], drow[x]);
            }
            if (i < ns) {
                cur0 = spans[i].x0;
                cur1 = spans[i].x1;
            }
        }
    }
}

void bd_mascot_paint(bd_mascot *m, tk_canvas *c,
                     const tk_rect *clips, size_t nclips)
{
    if (!m || !c || !m->visible || m->clip < 0) return;
    const uint32_t *src;
    int src_stride;
    tk_rect srect;
    if (m->version >= 2) {
        if (!m->comp_valid) comp_sync(m);
        src = m->comp;
        src_stride = m->crop_w_s;
        srect = m->comp_extent;
    } else {
        const bdm_pose *p = pose_at(m, m->clip, m->pose);
        const bdm_frame *f = frame_at(m, p->frame);
        src = m->atlas;
        src_stride = m->atlas_w;
        srect = (tk_rect){ f->x, f->y, f->w, f->h };
    }
    if (!clips) {
        tk_rect full = { 0, 0, tk_canvas_width(c),
                         tk_canvas_height(c) };
        blit_sprite(m, c, full, src, src_stride, srect);
        return;
    }
    blit_sprite_clipped(m, c, clips, nclips, src, src_stride,
                        srect);
}

/* ---------------- pointer ---------------- */

bool bd_mascot_pointer(bd_mascot *m, int type, int x, int y,
                       long now_ms)
{
    if (!m || !m->visible) return false;
    switch (type) {
    case TK_EV_POINTER_DOWN: {
        tk_rect r = sprite_rect(m);
        if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h)
            return false;
        m->captured = true;
        m->dragged = false;
        m->down_x = x;
        m->down_y = y;
        m->down_ms = now_ms;
        m->grab_dx = m->ax_q8 / 256 - x;
        m->grab_dy = m->ay_q8 / 256 - y;
        m->last_px = x;
        m->last_py = y;
        m->last_pmove_ms = now_ms;
        m->throw_vx_q8 = 0;
        return true;
    }
    case TK_EV_POINTER_MOVE: {
        if (!m->captured) return false;
        int dx = x - m->down_x, dy = y - m->down_y;
        if (!m->dragged && dx * dx + dy * dy > 64) {
            m->dragged = true;
            m->state = BD_MASCOT_DRAG;
            if (m->drag_clip >= 0) start_clip(m, m->drag_clip, now_ms);
        }
        if (m->dragged) {
            long dt = now_ms - m->last_pmove_ms;
            if (dt >= 20) {
                /* pointer velocity in px per 40 ms tick, q8 */
                m->throw_vx_q8 = (x - m->last_px) * 256 * 40 /
                                 (int)dt;
                m->last_px = x;
                m->last_py = y;
                m->last_pmove_ms = now_ms;
            }
            m->ax_q8 = (x + m->grab_dx) * 256;
            m->ay_q8 = (y + m->grab_dy) * 256;
            int fy = floor_y(m);
            if (m->ay_q8 > fy * 256) m->ay_q8 = fy * 256;
            if (m->ay_q8 < 0) m->ay_q8 = 0;
        }
        return true;
    }
    case TK_EV_POINTER_UP: {
        if (!m->captured) return false;
        m->captured = false;
        if (m->dragged) {
            m->dragged = false;
            int tvx = m->throw_vx_q8;
            if (tvx > 12 * 256) tvx = 12 * 256;
            if (tvx < -12 * 256) tvx = -12 * 256;
            m->vx_q8 = tvx;
            m->vy_q8 = 0;
            m->state = BD_MASCOT_FALL;
            if (m->thrown_clip >= 0)
                start_clip(m, m->thrown_clip, now_ms);
            else if (m->fall_clip >= 0)
                start_clip(m, m->fall_clip, now_ms);
        } else if (now_ms - m->down_ms < 300) {
            /* tap: reaction - a random Animate clip from the
             * pool, else the stand clip */
            int best = -1, n = 0;
            for (int i = 0; i < m->npool; i++) {
                int ci = rd16(m->pooltab + (size_t)i * BDM_POOL_SIZE);
                if (ci < m->nclips && clip_type(m, ci) == CLIP_ANIMATE) {
                    n++;
                    if (rng_range(m, 0, n - 1) == 0) best = ci;
                }
            }
            if (best < 0)
                best = m->stand_clip >= 0 ? m->stand_clip : m->clip;
            if (best >= 0) {
                m->state = BD_MASCOT_REACT;
                start_clip(m, best, now_ms);
            }
        }
        return true;
    }
    default:
        return false;
    }
}
