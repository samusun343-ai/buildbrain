/* test_mascot - desktop mascot runtime tests.
 *
 * Builds a tiny synthetic .bdm pack in C (a second, independent
 * implementation of the FORMAT.md layout: if the writer here
 * and the reader in mascot.c disagree, the tests fail), then
 * exercises loading, falling/landing, behaviour damage, paint
 * blending, drag/throw, tap reaction and hide/show.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "mascot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

/* ---- synthetic pack ------------------------------------------------
 * One size (height 100), atlas 64x64, two 32x40 frames:
 * frame 0 = opaque red, frame 1 = half-alpha green.
 * Clips: 0 Stand (Stay, frame 0), 1 Walk (Move, frames 0/1,
 * vx -2 px/tick), 2 Fall (Animate, frame 1), 3 Drag (Stay,
 * frame 1). Pool: Stand 100, Walk 60. Specials: fall=2,
 * drag=3, thrown=2, stand=0. Crop (source canvas) 32x40. */
static void put16(uint8_t *p, size_t off, uint16_t v)
{
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, size_t off, uint32_t v)
{
    put16(p, off, (uint16_t)v);
    put16(p, off + 2, (uint16_t)(v >> 16));
}

static char *write_pack(void)
{
    /* layout: hdr 80 | strtab | clips 4*16 | pool 2*4 |
     * size rec 24 | frames 2*12 | poses 5*12 | atlas 64*64*4 */
    const char *names = "\0Stand\0Walk\0Fall\0Drag\0"; /* 22 bytes */
    size_t strtab_off = 80;
    size_t clips_off = strtab_off + 22;
    size_t pool_off = clips_off + 4 * 16;
    size_t sizes_off = pool_off + 2 * 4;
    size_t frames_off = sizes_off + 24;
    size_t poses_off = frames_off + 2 * 12;
    size_t atlas_off = poses_off + 5 * 12;
    size_t total = atlas_off + 64 * 64 * 4;
    uint8_t *b = calloc(1, total);
    memcpy(b, "BDM1", 4);
    put16(b, 4, 1);
    put16(b, 6, 1);
    put32(b, 8, (uint32_t)total);
    memcpy(b + 12, "TestChar", 8);
    put16(b, 36, 32);          /* crop w */
    put16(b, 38, 40);          /* crop h */
    put16(b, 40, 4);           /* clips */
    put16(b, 42, 2);           /* pool */
    put16(b, 44, 1);           /* sizes */
    put16(b, 48, 2);           /* fall */
    put16(b, 50, 3);           /* drag */
    put16(b, 52, 2);           /* thrown */
    put16(b, 54, 0);           /* stand */
    put32(b, 56, (uint32_t)clips_off);
    put32(b, 60, (uint32_t)pool_off);
    put32(b, 64, (uint32_t)sizes_off);
    put32(b, 68, (uint32_t)strtab_off);
    memcpy(b + strtab_off, names, 22);
    /* clip meta: name_off, poses, tmin, tmax, pose_start,
     * type, border, flags(loop), rsvd */
    static const struct {
        uint16_t name, np, tmin, tmax;
        uint32_t start;
        uint8_t type, border, loop;
    } cm[4] = {
        { 1, 1, 10000, 10000, 0, 0, 1, 1 },   /* Stand */
        { 7, 2, 480, 480, 1, 1, 1, 1 },       /* Walk */
        { 12, 1, 400, 400, 3, 2, 0, 1 },      /* Fall */
        { 17, 1, 10000, 10000, 4, 0, 1, 1 },  /* Drag */
    };
    for (int i = 0; i < 4; i++) {
        uint8_t *p = b + clips_off + (size_t)i * 16;
        put16(p, 0, cm[i].name);
        put16(p, 2, cm[i].np);
        put16(p, 4, cm[i].tmin);
        put16(p, 6, cm[i].tmax);
        put32(p, 8, cm[i].start);
        p[12] = cm[i].type;
        p[13] = cm[i].border;
        p[14] = cm[i].loop;
    }
    /* pool: (clip, weight) */
    put16(b, pool_off + 0, 0); put16(b, pool_off + 2, 100);
    put16(b, pool_off + 4, 1); put16(b, pool_off + 6, 60);
    /* size record */
    uint8_t *sr = b + sizes_off;
    put16(sr, 0, 100);         /* height */
    put16(sr, 2, 64);          /* atlas w */
    put16(sr, 4, 64);          /* atlas h */
    put16(sr, 6, 2);           /* frames */
    put32(sr, 8, (uint32_t)frames_off);
    put32(sr, 12, (uint32_t)poses_off);
    put32(sr, 16, (uint32_t)atlas_off);
    put16(sr, 20, 640);        /* scale q8: 100/40 = 2.5 */
    /* frames: x,y,w,h,bx,by */
    put16(b, frames_off + 0, 0);  put16(b, frames_off + 2, 0);
    put16(b, frames_off + 4, 32); put16(b, frames_off + 6, 40);
    put16(b, frames_off + 8, 0);  put16(b, frames_off + 10, 0);
    put16(b, frames_off + 12, 32); put16(b, frames_off + 14, 0);
    put16(b, frames_off + 16, 32); put16(b, frames_off + 18, 40);
    put16(b, frames_off + 20, 0);  put16(b, frames_off + 22, 0);
    /* poses: frame, dur_ms, ax, ay, vx_q8, vy_q8.
     * Anchors are scaled coords: source anchor (16,40) * 2.5 =
     * (40,100). Walk vx: -2 px/tick * 2.5 * 256 = -1280. */
    static const int16_t poses[5][6] = {
        { 0, 10000, 40, 100, 0, 0 },        /* Stand */
        { 0, 240, 40, 100, -1280, 0 },      /* Walk a */
        { 1, 240, 40, 100, -1280, 0 },      /* Walk b */
        { 1, 400, 40, 100, 0, 0 },          /* Fall */
        { 1, 10000, 40, 100, 0, 0 },        /* Drag */
    };
    for (int i = 0; i < 5; i++) {
        uint8_t *p = b + poses_off + (size_t)i * 12;
        for (int j = 0; j < 6; j++)
            put16(p, (size_t)j * 2, (uint16_t)poses[i][j]);
    }
    /* atlas: frame 0 opaque red (premult ARGB), frame 1
     * half-alpha green: a=128, g premult = 255*128/255 = 128 */
    /* atlas_off is not 4-byte aligned here; write through
     * memcpy (same fix as test_session.c: a direct uint32_t*
     * store is UB and would fault on the ARMv5 target) */
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t red = 0xFFFF0000u, half = 0x80008000u;
            memcpy(b + atlas_off + ((size_t)y * 64 + x) * 4,
                   &red, 4);
            memcpy(b + atlas_off + ((size_t)y * 64 + 32 + x) * 4,
                   &half, 4);
        }
    char tmpl[] = "/tmp/test-mascot-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(b); return NULL; }
    if (write(fd, b, total) != (ssize_t)total) { /* ignore */ }
    close(fd);
    free(b);
    return strdup(tmpl);
}

static bd_mascot *new_test_mascot(const char *path)
{
    bd_mascot_config cfg = { 0 };
    cfg.pack_path = path;
    cfg.size_px = 100;
    cfg.floor_area = (tk_rect){ 0, 0, 800, 424 }; /* floor y=424 */
    cfg.seed = 12345;
    return bd_mascot_new(&cfg);
}

/* run ticks of 40 ms until the predicate state is reached */
static void run_ms(bd_mascot *m, long from, long ms)
{
    tk_rect dmg[2];
    for (long t = from; t < from + ms; t += 40)
        (void)bd_mascot_tick(m, t, dmg, 2);
}

/* ---- synthetic v2 pack ---------------------------------------------
 * Same character geometry as the v1 pack (crop 32x40, size
 * height 100, scale 2.5 -> composed canvas 80x100), but the
 * Anim clip exercises the sparse-delta path:
 *   frame 0 FULL   - opaque red 32x40 at canvas (10,20)
 *   frame 1 SPARSE - a 4x4 blue block at (12,22) plus one
 *                    white pixel at (40,50); 2 damage bands
 *   frame 2 SAME   - identical to frame 1
 * Clips: 0 Stand (Stay, f0), 1 Anim (Animate, f0/f1/f2 at
 * 100 ms each, tmin=tmax=300 so no time scaling), 2 Fall,
 * 3 Drag. Pool: Stand 1000, Anim 1 (tap picks the only
 * Animate entry deterministically). */
static char *write_pack_v2(void)
{
    const char *names = "\0Stand\0Anim\0Fall\0Drag\0"; /* 22 */
    size_t strtab_off = 80;
    size_t clips_off = strtab_off + 22;
    size_t pool_off = clips_off + 4 * 16;
    size_t sizes_off = pool_off + 2 * 4;
    size_t frames_off = sizes_off + 32;
    size_t patches_off = frames_off + 3 * 20;
    size_t sparse_off = patches_off + 2 * 12;
    size_t poses_off = sparse_off + 92;
    size_t atlas_off = poses_off + 6 * 12;
    size_t total = atlas_off + 64 * 64 * 4;
    uint8_t *b = calloc(1, total);
    memcpy(b, "BDM1", 4);
    put16(b, 4, 2);            /* version 2 */
    put16(b, 6, 1);
    put32(b, 8, (uint32_t)total);
    memcpy(b + 12, "TestChar", 8);
    put16(b, 36, 32);
    put16(b, 38, 40);
    put16(b, 40, 4);
    put16(b, 42, 2);
    put16(b, 44, 1);
    put16(b, 48, 2);
    put16(b, 50, 3);
    put16(b, 52, 2);
    put16(b, 54, 0);
    put32(b, 56, (uint32_t)clips_off);
    put32(b, 60, (uint32_t)pool_off);
    put32(b, 64, (uint32_t)sizes_off);
    put32(b, 68, (uint32_t)strtab_off);
    memcpy(b + strtab_off, names, 22);
    static const struct {
        uint16_t name, np, tmin, tmax;
        uint32_t start;
        uint8_t type, border, loop;
    } cm[4] = {
        { 1, 1, 10000, 10000, 0, 0, 1, 1 },  /* Stand */
        { 7, 3, 300, 300, 1, 2, 0, 0 },      /* Anim */
        { 12, 1, 400, 400, 4, 2, 0, 1 },     /* Fall */
        { 17, 1, 10000, 10000, 5, 0, 1, 1 }, /* Drag */
    };
    for (int i = 0; i < 4; i++) {
        uint8_t *p = b + clips_off + (size_t)i * 16;
        put16(p, 0, cm[i].name);
        put16(p, 2, cm[i].np);
        put16(p, 4, cm[i].tmin);
        put16(p, 6, cm[i].tmax);
        put32(p, 8, cm[i].start);
        p[12] = cm[i].type;
        p[13] = cm[i].border;
        p[14] = cm[i].loop;
    }
    put16(b, pool_off + 0, 0); put16(b, pool_off + 2, 1000);
    put16(b, pool_off + 4, 1); put16(b, pool_off + 6, 1);
    /* v2 size record (32 B) */
    uint8_t *sr = b + sizes_off;
    put16(sr, 0, 100);
    put16(sr, 2, 64);
    put16(sr, 4, 64);
    put16(sr, 6, 3);           /* frames */
    put32(sr, 8, (uint32_t)frames_off);
    put32(sr, 12, (uint32_t)patches_off);
    put32(sr, 16, (uint32_t)sparse_off);
    put32(sr, 20, (uint32_t)poses_off);
    put32(sr, 24, (uint32_t)atlas_off);
    put16(sr, 28, 640);        /* scale q8 */
    put16(sr, 30, 20);         /* frame stride */
    /* frames (20 B): x,y,w,h,bx,by,kind,nband,aux */
    uint8_t *f0 = b + frames_off;
    put16(f0, 4, 32); put16(f0, 6, 40);
    put16(f0, 8, 10); put16(f0, 10, 20);   /* bx, by */
    /* kind 0 (FULL), nband 0, aux 0 already zero */
    uint8_t *f1 = b + frames_off + 20;
    put16(f1, 12, 1);          /* SPARSE */
    put16(f1, 14, 2);          /* bands */
    put32(f1, 16, 0);          /* sparse offset */
    uint8_t *f2 = b + frames_off + 40;
    put16(f2, 12, 2);          /* SAME */
    /* patches (12 B): x,y,w,h,bx,by - band A then band B */
    uint8_t *pa = b + patches_off;
    put16(pa, 4, 4); put16(pa, 6, 4);
    put16(pa, 8, 12); put16(pa, 10, 22);
    uint8_t *pb = b + patches_off + 12;
    put16(pb, 4, 1); put16(pb, 6, 1);
    put16(pb, 8, 40); put16(pb, 10, 50);
    /* sparse runs over the 80-wide canvas: blue block rows
     * y=22..25 x=12..15 (linear 1772/1852/1932/2012), white
     * pixel at (40,50) = 4040 */
    uint8_t *sp = b + sparse_off;
    size_t o = 0;
    for (int r = 0; r < 4; r++) {
        put16(sp, o, r == 0 ? 1772 : 76);
        put16(sp, o + 2, 4);
        o += 4;
        for (int k = 0; k < 4; k++) {
            put32(sp, o, 0xFF0000FFu); /* opaque blue premult */
            o += 4;
        }
    }
    put16(sp, o, 2024); put16(sp, o + 2, 1); o += 4;
    put32(sp, o, 0xFFFFFFFFu); o += 4;
    put16(sp, o, 0); put16(sp, o + 2, 0); o += 4;
    CHECK(o == 92);
    /* poses: frame, dur, ax, ay, vx, vy (anchor 40,100) */
    static const int16_t poses[6][6] = {
        { 0, 10000, 40, 100, 0, 0 },        /* Stand */
        { 0, 100, 40, 100, 0, 0 },          /* Anim 0 */
        { 1, 100, 40, 100, 0, 0 },          /* Anim 1 */
        { 2, 100, 40, 100, 0, 0 },          /* Anim 2 */
        { 0, 400, 40, 100, 0, 0 },          /* Fall */
        { 0, 10000, 40, 100, 0, 0 },        /* Drag */
    };
    for (int i = 0; i < 6; i++) {
        uint8_t *p = b + poses_off + (size_t)i * 12;
        for (int j = 0; j < 6; j++)
            put16(p, (size_t)j * 2, (uint16_t)poses[i][j]);
    }
    /* unaligned atlas offset: memcpy writes, as above */
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t red = 0xFFFF0000u; /* opaque red */
            memcpy(b + atlas_off + ((size_t)y * 64 + x) * 4,
                   &red, 4);
        }
    char tmpl[] = "/tmp/test-mascot2-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(b); return NULL; }
    if (write(fd, b, total) != (ssize_t)total) { /* ignore */ }
    close(fd);
    free(b);
    return strdup(tmpl);
}

/* v3: same body as the v2 pack, 48-byte size record, plus a
 * delta-encoded region section (74 B):
 *   Stand p0: full [(10,20,32,40)]
 *   Anim  p0: full [(10,20,32,40)]
 *   Anim  p1: -[0] +[(12,22,4,4),(40,50,1,1)]
 *   Anim  p2: (no change)
 *   Fall  p0: full [(10,20,32,40)]
 *   Drag  p0: full [(10,20,32,40)] */
static char *write_pack_v3(void)
{
    const char *names = "\0Stand\0Anim\0Fall\0Drag\0"; /* 22 */
    size_t strtab_off = 80;
    size_t clips_off = strtab_off + 22;
    size_t pool_off = clips_off + 4 * 16;
    size_t sizes_off = pool_off + 2 * 4;
    size_t frames_off = sizes_off + 48;
    size_t patches_off = frames_off + 3 * 20;
    size_t sparse_off = patches_off + 2 * 12;
    size_t poses_off = sparse_off + 92;
    size_t atlas_off = poses_off + 6 * 12;
    size_t regions_off = atlas_off + 64 * 64 * 4;
    size_t total = regions_off + 74;
    uint8_t *b = calloc(1, total);
    memcpy(b, "BDM1", 4);
    put16(b, 4, 3);            /* version 3 */
    put16(b, 6, 7);            /* flags: premult|delta|regions */
    put32(b, 8, (uint32_t)total);
    memcpy(b + 12, "TestChar", 8);
    put16(b, 36, 32);
    put16(b, 38, 40);
    put16(b, 40, 4);
    put16(b, 42, 2);
    put16(b, 44, 1);
    put16(b, 48, 2);
    put16(b, 50, 3);
    put16(b, 52, 2);
    put16(b, 54, 0);
    put32(b, 56, (uint32_t)clips_off);
    put32(b, 60, (uint32_t)pool_off);
    put32(b, 64, (uint32_t)sizes_off);
    put32(b, 68, (uint32_t)strtab_off);
    memcpy(b + strtab_off, names, 22);
    static const struct {
        uint16_t name, np, tmin, tmax;
        uint32_t start;
        uint8_t type, border, loop;
    } cm[4] = {
        { 1, 1, 10000, 10000, 0, 0, 1, 1 },
        { 7, 3, 300, 300, 1, 2, 0, 0 },
        { 12, 1, 400, 400, 4, 2, 0, 1 },
        { 17, 1, 10000, 10000, 5, 0, 1, 1 },
    };
    for (int i = 0; i < 4; i++) {
        uint8_t *p = b + clips_off + (size_t)i * 16;
        put16(p, 0, cm[i].name);
        put16(p, 2, cm[i].np);
        put16(p, 4, cm[i].tmin);
        put16(p, 6, cm[i].tmax);
        put32(p, 8, cm[i].start);
        p[12] = cm[i].type;
        p[13] = cm[i].border;
        p[14] = cm[i].loop;
    }
    put16(b, pool_off + 0, 0); put16(b, pool_off + 2, 1000);
    put16(b, pool_off + 4, 1); put16(b, pool_off + 6, 1);
    /* v3 size record (48 B) */
    uint8_t *sr = b + sizes_off;
    put16(sr, 0, 100);
    put16(sr, 2, 64);
    put16(sr, 4, 64);
    put16(sr, 6, 3);
    put32(sr, 8, (uint32_t)frames_off);
    put32(sr, 12, (uint32_t)patches_off);
    put32(sr, 16, (uint32_t)sparse_off);
    put32(sr, 20, (uint32_t)poses_off);
    put32(sr, 24, (uint32_t)atlas_off);
    put16(sr, 28, 640);
    put16(sr, 30, 20);
    put32(sr, 32, (uint32_t)regions_off);
    put32(sr, 36, 74);
    put32(sr, 40, (uint32_t)total);  /* rgb565: empty, at EOF */
    put32(sr, 44, 0);
    uint8_t *f0 = b + frames_off;
    put16(f0, 4, 32); put16(f0, 6, 40);
    put16(f0, 8, 10); put16(f0, 10, 20);
    uint8_t *f1 = b + frames_off + 20;
    put16(f1, 12, 1);
    put16(f1, 14, 2);
    put32(f1, 16, 0);
    uint8_t *f2 = b + frames_off + 40;
    put16(f2, 12, 2);
    uint8_t *pa = b + patches_off;
    put16(pa, 4, 4); put16(pa, 6, 4);
    put16(pa, 8, 12); put16(pa, 10, 22);
    uint8_t *pb = b + patches_off + 12;
    put16(pb, 4, 1); put16(pb, 6, 1);
    put16(pb, 8, 40); put16(pb, 10, 50);
    uint8_t *sp = b + sparse_off;
    size_t o = 0;
    for (int r = 0; r < 4; r++) {
        put16(sp, o, r == 0 ? 1772 : 76);
        put16(sp, o + 2, 4);
        o += 4;
        for (int k = 0; k < 4; k++) {
            put32(sp, o, 0xFF0000FFu);
            o += 4;
        }
    }
    put16(sp, o, 2024); put16(sp, o + 2, 1); o += 4;
    put32(sp, o, 0xFFFFFFFFu); o += 4;
    put16(sp, o, 0); put16(sp, o + 2, 0); o += 4;
    static const int16_t poses[6][6] = {
        { 0, 10000, 40, 100, 0, 0 },
        { 0, 100, 40, 100, 0, 0 },
        { 1, 100, 40, 100, 0, 0 },
        { 2, 100, 40, 100, 0, 0 },
        { 0, 400, 40, 100, 0, 0 },
        { 0, 10000, 40, 100, 0, 0 },
    };
    for (int i = 0; i < 6; i++) {
        uint8_t *p = b + poses_off + (size_t)i * 12;
        for (int j = 0; j < 6; j++)
            put16(p, (size_t)j * 2, (uint16_t)poses[i][j]);
    }
    /* unaligned atlas offset: memcpy writes, as above */
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t red = 0xFFFF0000u;
            memcpy(b + atlas_off + ((size_t)y * 64 + x) * 4,
                   &red, 4);
        }
    /* region stream */
    uint8_t *rg = b + regions_off;
    size_t ro = 0;
    /* full list: sentinel + 1 rect */
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 32); put16(rg, ro + 6, 40); ro += 8;
    /* Anim p0: full again */
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 32); put16(rg, ro + 6, 40); ro += 8;
    /* Anim p1: remove 0, add 2 */
    put16(rg, ro, 1); put16(rg, ro + 2, 2); ro += 4;
    put16(rg, ro, 0); ro += 2;
    put16(rg, ro, 12); put16(rg, ro + 2, 22);
    put16(rg, ro + 4, 4); put16(rg, ro + 6, 4); ro += 8;
    put16(rg, ro, 40); put16(rg, ro + 2, 50);
    put16(rg, ro + 4, 1); put16(rg, ro + 6, 1); ro += 8;
    /* Anim p2: no change */
    put16(rg, ro, 0); put16(rg, ro + 2, 0); ro += 4;
    /* Fall p0 + Drag p0: full */
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 32); put16(rg, ro + 6, 40); ro += 8;
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 32); put16(rg, ro + 6, 40); ro += 8;
    CHECK(ro == 74);
    char tmpl[] = "/tmp/test-mascot3-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(b); return NULL; }
    if (write(fd, b, total) != (ssize_t)total) { /* ignore */ }
    close(fd);
    free(b);
    return strdup(tmpl);
}

static void test_v3(void)
{
    char *path = write_pack_v3();
    CHECK(path != NULL);
    bd_mascot *m = new_test_mascot(path);
    CHECK(m != NULL);
    if (!m) return;
    /* v3 loads through the v2 composition path */
    CHECK(bd_mascot_pack_version(m) == 3);
    CHECK(bd_mascot_size(m) == 100);
    CHECK(bd_mascot_has_regions(m));
    tk_rect out[8];
    /* Stand pose 0: the full silhouette rect */
    int n = bd_mascot_regions_at(m, 0, 0, out, 8);
    CHECK(n == 1 && out[0].x == 10 && out[0].y == 20 &&
          out[0].w == 32 && out[0].h == 40);
    /* Anim pose 0: full list again (per-clip reset) */
    n = bd_mascot_regions_at(m, 1, 0, out, 8);
    CHECK(n == 1 && out[0].x == 10 && out[0].w == 32);
    /* Anim pose 1: delta applied - the two sparse bands */
    n = bd_mascot_regions_at(m, 1, 1, out, 8);
    CHECK(n == 2);
    CHECK(out[0].x == 12 && out[0].y == 22 && out[0].w == 4 &&
          out[0].h == 4);
    CHECK(out[1].x == 40 && out[1].y == 50 && out[1].w == 1 &&
          out[1].h == 1);
    /* Anim pose 2: empty delta keeps the list */
    n = bd_mascot_regions_at(m, 1, 2, out, 8);
    CHECK(n == 2 && out[0].x == 12);
    /* clips after Anim still decode (stream stayed in sync) */
    n = bd_mascot_regions_at(m, 3, 0, out, 8);
    CHECK(n == 1 && out[0].w == 32);
    /* out of range */
    CHECK(bd_mascot_regions_at(m, 4, 0, out, 8) == -1);
    CHECK(bd_mascot_regions_at(m, 1, 3, out, 8) == -1);
    bd_mascot_free(m);
    unlink(path);
    free(path);

    /* a v2 pack reports no regions */
    char *p2 = write_pack_v2();
    bd_mascot *m2 = new_test_mascot(p2);
    CHECK(m2 != NULL);
    if (m2) {
        CHECK(bd_mascot_pack_version(m2) == 2);
        CHECK(!bd_mascot_has_regions(m2));
        CHECK(bd_mascot_regions_at(m2, 0, 0, out, 8) == -1);
        bd_mascot_free(m2);
    }
    unlink(p2);
    free(p2);
}

/* v3 region list ORDER regression (the coverage bug): the
 * region delta indices are positional against the decoder's
 * reconstructed list, defined as "survivors in previous
 * order, then adds appended" (FORMAT.md 5b). The converter
 * once computed indices against the fresh geometric order;
 * from the second delta on, decoders removed the wrong
 * rects and silhouette coverage failed on ~90% of poses in
 * real packs (Mythra 155/168, Miku HD 145/204). This pack's
 * Anim clip pins the decoder semantics:
 *   p0: full [A(0,0,10,10) B(20,0,10,10) C(40,0,10,10)]
 *   p1: -[1] +[D(10,20,5,5)]  -> [A, C, D]
 *   p2: -[2] +[E(30,20,5,5)]  -> [A, C, E]
 * (index 2 at p2 is D, the rect appended by p1 - under the
 * geometric order [A, D, C] it would have been index 1.) */
static char *write_pack_v3_order(void)
{
    const char *names = "\0Stand\0Anim\0Fall\0Drag\0"; /* 22 */
    size_t strtab_off = 80;
    size_t clips_off = strtab_off + 22;
    size_t pool_off = clips_off + 4 * 16;
    size_t sizes_off = pool_off + 2 * 4;
    size_t frames_off = sizes_off + 48;
    size_t patches_off = frames_off + 3 * 20;
    size_t sparse_off = patches_off + 2 * 12;
    size_t poses_off = sparse_off + 92;
    size_t atlas_off = poses_off + 6 * 12;
    size_t regions_off = atlas_off + 64 * 64 * 4;
    size_t total = regions_off + 92;
    uint8_t *b = calloc(1, total);
    memcpy(b, "BDM1", 4);
    put16(b, 4, 3);            /* version 3 */
    put16(b, 6, 7);            /* flags: premult|delta|regions */
    put32(b, 8, (uint32_t)total);
    memcpy(b + 12, "TestChar", 8);
    put16(b, 36, 32);
    put16(b, 38, 40);
    put16(b, 40, 4);
    put16(b, 42, 2);
    put16(b, 44, 1);
    put16(b, 48, 2);
    put16(b, 50, 3);
    put16(b, 52, 2);
    put16(b, 54, 0);
    put32(b, 56, (uint32_t)clips_off);
    put32(b, 60, (uint32_t)pool_off);
    put32(b, 64, (uint32_t)sizes_off);
    put32(b, 68, (uint32_t)strtab_off);
    memcpy(b + strtab_off, names, 22);
    static const struct {
        uint16_t name, np, tmin, tmax;
        uint32_t start;
        uint8_t type, border, loop;
    } cm[4] = {
        { 1, 1, 10000, 10000, 0, 0, 1, 1 },
        { 7, 3, 300, 300, 1, 2, 0, 0 },
        { 12, 1, 400, 400, 4, 2, 0, 1 },
        { 17, 1, 10000, 10000, 5, 0, 1, 1 },
    };
    for (int i = 0; i < 4; i++) {
        uint8_t *p = b + clips_off + (size_t)i * 16;
        put16(p, 0, cm[i].name);
        put16(p, 2, cm[i].np);
        put16(p, 4, cm[i].tmin);
        put16(p, 6, cm[i].tmax);
        put32(p, 8, cm[i].start);
        p[12] = cm[i].type;
        p[13] = cm[i].border;
        p[14] = cm[i].loop;
    }
    put16(b, pool_off + 0, 0); put16(b, pool_off + 2, 1000);
    put16(b, pool_off + 4, 1); put16(b, pool_off + 6, 1);
    uint8_t *sr = b + sizes_off;
    put16(sr, 0, 100);
    put16(sr, 2, 64);
    put16(sr, 4, 64);
    put16(sr, 6, 3);
    put32(sr, 8, (uint32_t)frames_off);
    put32(sr, 12, (uint32_t)patches_off);
    put32(sr, 16, (uint32_t)sparse_off);
    put32(sr, 20, (uint32_t)poses_off);
    put32(sr, 24, (uint32_t)atlas_off);
    put16(sr, 28, 640);
    put16(sr, 30, 20);
    put32(sr, 32, (uint32_t)regions_off);
    put32(sr, 36, 92);
    put32(sr, 40, (uint32_t)total);  /* rgb565: empty, at EOF */
    put32(sr, 44, 0);
    uint8_t *f0 = b + frames_off;
    put16(f0, 4, 32); put16(f0, 6, 40);
    put16(f0, 8, 10); put16(f0, 10, 20);
    uint8_t *f1 = b + frames_off + 20;
    put16(f1, 12, 1);
    put16(f1, 14, 2);
    put32(f1, 16, 0);
    uint8_t *f2 = b + frames_off + 40;
    put16(f2, 12, 2);
    uint8_t *pa = b + patches_off;
    put16(pa, 4, 4); put16(pa, 6, 4);
    put16(pa, 8, 12); put16(pa, 10, 22);
    uint8_t *pb = b + patches_off + 12;
    put16(pb, 4, 1); put16(pb, 6, 1);
    put16(pb, 8, 40); put16(pb, 10, 50);
    uint8_t *sp = b + sparse_off;
    size_t o = 0;
    for (int r = 0; r < 4; r++) {
        put16(sp, o, r == 0 ? 1772 : 76);
        put16(sp, o + 2, 4);
        o += 4;
        for (int k = 0; k < 4; k++) {
            put32(sp, o, 0xFF0000FFu);
            o += 4;
        }
    }
    put16(sp, o, 2024); put16(sp, o + 2, 1); o += 4;
    put32(sp, o, 0xFFFFFFFFu); o += 4;
    put16(sp, o, 0); put16(sp, o + 2, 0); o += 4;
    static const int16_t poses[6][6] = {
        { 0, 10000, 40, 100, 0, 0 },
        { 0, 100, 40, 100, 0, 0 },
        { 1, 100, 40, 100, 0, 0 },
        { 2, 100, 40, 100, 0, 0 },
        { 0, 400, 40, 100, 0, 0 },
        { 0, 10000, 40, 100, 0, 0 },
    };
    for (int i = 0; i < 6; i++) {
        uint8_t *p = b + poses_off + (size_t)i * 12;
        for (int j = 0; j < 6; j++)
            put16(p, (size_t)j * 2, (uint16_t)poses[i][j]);
    }
    /* unaligned atlas offset: memcpy writes, as above */
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t red = 0xFFFF0000u;
            memcpy(b + atlas_off + ((size_t)y * 64 + x) * 4,
                   &red, 4);
        }
    /* region stream (92 B) */
    uint8_t *rg = b + regions_off;
    size_t ro = 0;
    /* Stand p0: full [(10,20,32,40)] */
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 32); put16(rg, ro + 6, 40); ro += 8;
    /* Anim p0: full [A B C] */
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 3); ro += 4;
    put16(rg, ro, 0); put16(rg, ro + 2, 0);
    put16(rg, ro + 4, 10); put16(rg, ro + 6, 10); ro += 8;
    put16(rg, ro, 20); put16(rg, ro + 2, 0);
    put16(rg, ro + 4, 10); put16(rg, ro + 6, 10); ro += 8;
    put16(rg, ro, 40); put16(rg, ro + 2, 0);
    put16(rg, ro + 4, 10); put16(rg, ro + 6, 10); ro += 8;
    /* Anim p1: -[1] +[D(10,20,5,5)] -> [A, C, D] */
    put16(rg, ro, 1); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 1); ro += 2;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 5); put16(rg, ro + 6, 5); ro += 8;
    /* Anim p2: -[2] +[E(30,20,5,5)] -> [A, C, E] */
    put16(rg, ro, 1); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 2); ro += 2;
    put16(rg, ro, 30); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 5); put16(rg, ro + 6, 5); ro += 8;
    /* Fall p0 + Drag p0: full [(10,20,32,40)] */
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 32); put16(rg, ro + 6, 40); ro += 8;
    put16(rg, ro, 0xFFFF); put16(rg, ro + 2, 1); ro += 4;
    put16(rg, ro, 10); put16(rg, ro + 2, 20);
    put16(rg, ro + 4, 32); put16(rg, ro + 6, 40); ro += 8;
    CHECK(ro == 92);
    char tmpl[] = "/tmp/test-mascot3o-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(b); return NULL; }
    if (write(fd, b, total) != (ssize_t)total) { /* ignore */ }
    close(fd);
    free(b);
    return strdup(tmpl);
}

static void test_v3_region_order(void)
{
    char *path = write_pack_v3_order();
    CHECK(path != NULL);
    bd_mascot *m = new_test_mascot(path);
    CHECK(m != NULL);
    if (!m) return;
    tk_rect out[8];
    int n = bd_mascot_regions_at(m, 1, 0, out, 8);
    CHECK(n == 3 && out[0].x == 0 && out[1].x == 20 &&
          out[2].x == 40);
    /* p1: B removed, D appended AFTER the survivors */
    n = bd_mascot_regions_at(m, 1, 1, out, 8);
    CHECK(n == 3 && out[0].x == 0 && out[1].x == 40 &&
          out[2].x == 10 && out[2].y == 20);
    /* p2: index 2 removes D (the appended rect), E appended */
    n = bd_mascot_regions_at(m, 1, 2, out, 8);
    CHECK(n == 3 && out[0].x == 0 && out[1].x == 40 &&
          out[2].x == 30 && out[2].y == 20);
    bd_mascot_free(m);
    unlink(path);
    free(path);
}

/* v2 canvas-width regression (the Mao glitch): the composed
 * canvas width must be round(crop_w * size_h / crop_h) - the
 * width the converter baked the flat sparse stream over.
 * This pack uses Mao's geometry (crop 829x1200, size 320,
 * scale_q8 68): the exact width is 221, while the old q8
 * formula gave 220, shearing every sparse write by one more
 * pixel per row. The Anim clip's SPARSE frame writes a single
 * green pixel at converter coords (220,1) = flat index
 * 1*221 + 220 = 441; with a 220-wide buffer it would land at
 * (1,2), outside the painted extent, and never show. */
static char *write_pack_v4(void)
{
    const char *names = "\0Stand\0Anim\0Fall\0Drag\0"; /* 22 */
    size_t strtab_off = 80;
    size_t clips_off = strtab_off + 22;
    size_t pool_off = clips_off + 4 * 16;
    size_t sizes_off = pool_off + 2 * 4;
    size_t frames_off = sizes_off + 32;
    size_t patches_off = frames_off + 4 * 20;
    size_t sparse_off = patches_off + 1 * 12;
    size_t poses_off = sparse_off + 12;
    size_t atlas_off = poses_off + 5 * 12;
    size_t total = atlas_off + 64 * 64 * 4;
    uint8_t *b = calloc(1, total);
    memcpy(b, "BDM1", 4);
    put16(b, 4, 2);
    put16(b, 6, 3);
    put32(b, 8, (uint32_t)total);
    memcpy(b + 12, "TestChar", 8);
    put16(b, 36, 829);         /* crop_w: Mao's */
    put16(b, 38, 1200);        /* crop_h */
    put16(b, 40, 4);
    put16(b, 42, 2);
    put16(b, 44, 1);
    put16(b, 48, 2);
    put16(b, 50, 3);
    put16(b, 52, 2);
    put16(b, 54, 0);
    put32(b, 56, (uint32_t)clips_off);
    put32(b, 60, (uint32_t)pool_off);
    put32(b, 64, (uint32_t)sizes_off);
    put32(b, 68, (uint32_t)strtab_off);
    memcpy(b + strtab_off, names, 22);
    static const struct {
        uint16_t name, np, tmin, tmax;
        uint32_t start;
        uint8_t type, border, loop;
    } cm[4] = {
        { 1, 1, 10000, 10000, 0, 0, 1, 1 },  /* Stand */
        { 7, 2, 300, 300, 1, 2, 0, 0 },      /* Anim */
        { 12, 1, 400, 400, 3, 2, 0, 1 },     /* Fall */
        { 17, 1, 10000, 10000, 4, 0, 1, 1 }, /* Drag */
    };
    for (int i = 0; i < 4; i++) {
        uint8_t *p = b + clips_off + (size_t)i * 16;
        put16(p, 0, cm[i].name);
        put16(p, 2, cm[i].np);
        put16(p, 4, cm[i].tmin);
        put16(p, 6, cm[i].tmax);
        put32(p, 8, cm[i].start);
        p[12] = cm[i].type;
        p[13] = cm[i].border;
        p[14] = cm[i].loop;
    }
    put16(b, pool_off + 0, 0); put16(b, pool_off + 2, 1000);
    put16(b, pool_off + 4, 1); put16(b, pool_off + 6, 1);
    uint8_t *sr = b + sizes_off;
    put16(sr, 0, 320);
    put16(sr, 2, 64);
    put16(sr, 4, 64);
    put16(sr, 6, 4);           /* frames */
    put32(sr, 8, (uint32_t)frames_off);
    put32(sr, 12, (uint32_t)patches_off);
    put32(sr, 16, (uint32_t)sparse_off);
    put32(sr, 20, (uint32_t)poses_off);
    put32(sr, 24, (uint32_t)atlas_off);
    put16(sr, 28, 68);         /* scale q8: 320/1200 * 256 */
    put16(sr, 30, 20);
    /* frames 0,1,3: FULL 1x1 at canvas (0,0); frame 2: SPARSE */
    for (int i = 0; i < 4; i++) {
        uint8_t *f = b + frames_off + (size_t)i * 20;
        if (i == 2) {
            put16(f, 12, 1);   /* SPARSE */
            put16(f, 14, 1);   /* one band */
            put32(f, 16, 0);   /* sparse offset */
        } else {
            put16(f, 4, 1);    /* w */
            put16(f, 6, 1);    /* h */
        }
    }
    /* the one band: (220,1) 1x1 */
    uint8_t *pa = b + patches_off;
    put16(pa, 4, 1); put16(pa, 6, 1);
    put16(pa, 8, 220); put16(pa, 10, 1);
    /* sparse payload: skip 441, len 1, a semi-transparent
     * pixel (a=182, premult rgb 42,40,64); terminator */
    uint8_t *sp = b + sparse_off;
    put16(sp, 0, 441); put16(sp, 2, 1);
    put32(sp, 4, 0xB62A2840u);
    put16(sp, 8, 0); put16(sp, 10, 0);
    static const int16_t poses[5][6] = {
        { 0, 10000, 110, 320, 0, 0 },       /* Stand */
        { 1, 150, 110, 320, 0, 0 },         /* Anim 0 */
        { 2, 150, 110, 320, 0, 0 },         /* Anim 1 */
        { 3, 400, 110, 320, 0, 0 },         /* Fall */
        { 3, 10000, 110, 320, 0, 0 },       /* Drag */
    };
    for (int i = 0; i < 5; i++) {
        uint8_t *p = b + poses_off + (size_t)i * 12;
        for (int j = 0; j < 6; j++)
            put16(p, (size_t)j * 2, (uint16_t)poses[i][j]);
    }
    { uint32_t red = 0xFFFF0000u;   /* opaque red */
      memcpy(b + atlas_off, &red, 4); }
    char tmpl[] = "/tmp/test-mascot4-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(b); return NULL; }
    if (write(fd, b, total) != (ssize_t)total) { /* ignore */ }
    close(fd);
    free(b);
    return strdup(tmpl);
}

static void test_canvas_width(void)
{
    char *path = write_pack_v4();
    CHECK(path != NULL);
    bd_mascot *m = new_test_mascot(path);
    CHECK(m != NULL);
    if (!m) return;
    CHECK(bd_mascot_size(m) == 320);
    tk_rect dmg[8];
    long t = 0;
    for (int i = 0; i < 100 && bd_mascot_state(m) == BD_MASCOT_FALL;
         i++) {
        t += 40;
        (void)bd_mascot_tick(m, t, dmg, 8);
    }
    CHECK(bd_mascot_state(m) != BD_MASCOT_FALL);
    /* tap the 1x1 sprite -> Anim */
    tk_rect r1 = bd_mascot_rect(m);
    t += 50;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_DOWN, r1.x, r1.y, t));
    t += 80;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_UP, r1.x, r1.y, t));
    CHECK(bd_mascot_state(m) == BD_MASCOT_REACT);
    /* advance into Anim pose 1 (SPARSE), window [150,300) ms */
    for (int i = 0; i < 5; i++) {
        t += 40;
        (void)bd_mascot_tick(m, t, dmg, 8);
    }
    tk_rect r = bd_mascot_rect(m);
    CHECK(r.w == 221 && r.h == 2);   /* extent incl. the band */
    tk_canvas *cv = tk_canvas_new(800, 480);
    tk_canvas_clear(cv, (tk_color){ 0, 0, 255, 255 });
    bd_mascot_paint(m, cv, NULL, 0);
    /* the FULL frame's red pixel at the extent origin */
    tk_color rd = tk_canvas_pixel(cv, r.x, r.y);
    CHECK(rd.r == 255 && rd.g == 0 && rd.b == 0);
    /* the sparse pixel at converter coords (220,1): only
     * lands there when the composed canvas is 221 wide.
     * Blended once over the blue canvas: (42, 40, 64+73). */
    tk_color gn = tk_canvas_pixel(cv, r.x + 220, r.y + 1);
    CHECK(gn.r == 42 && gn.g == 40 && gn.b == 137);
    /* Overlapping clip rects must not blend twice: the v2
     * damage bands overlap by design, and painting rect by
     * rect used to darken semi-transparent pixels in the
     * overlaps. Paint with the sprite rect listed twice and
     * expect the single-blend value again. */
    tk_canvas_clear(cv, (tk_color){ 0, 0, 255, 255 });
    tk_rect twice[2] = { r, r };
    bd_mascot_paint(m, cv, twice, 2);
    gn = tk_canvas_pixel(cv, r.x + 220, r.y + 1);
    CHECK(gn.r == 42 && gn.g == 40 && gn.b == 137);
    tk_canvas_free(cv);
    bd_mascot_free(m);
    unlink(path);
    free(path);
}

static void test_v2(void)
{
    char *path = write_pack_v2();
    CHECK(path != NULL);
    bd_mascot *m = new_test_mascot(path);
    CHECK(m != NULL);
    if (!m) return;
    CHECK(bd_mascot_size(m) == 100);
    size_t mb = 0;
    int nc = 0, nf = 0, np = 0;
    bd_mascot_stats(m, &mb, &nc, &nf, &np);
    CHECK(nc == 4 && nf == 3);

    /* pre-tick rect comes from the FULL frame's extent */
    tk_rect r0 = bd_mascot_rect(m);
    CHECK(r0.w == 32 && r0.h == 40);

    /* land */
    tk_rect dmg[8];
    long t = 0;
    for (int i = 0; i < 100 && bd_mascot_state(m) == BD_MASCOT_FALL;
         i++) {
        t += 40;
        (void)bd_mascot_tick(m, t, dmg, 8);
    }
    CHECK(bd_mascot_state(m) != BD_MASCOT_FALL);
    tk_rect r1 = bd_mascot_rect(m);
    CHECK(r1.w == 32 && r1.h == 40);
    CHECK(r1.y == 424 - 100 + 20); /* anchor 424, ay 100, by 20 */
    CHECK(!bd_mascot_animating(m)); /* Stand: single pose */

    /* tap -> the Anim clip (only Animate entry in the pool) */
    int cx = r1.x + r1.w / 2, cy = r1.y + r1.h / 2;
    t += 50;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_DOWN, cx, cy, t));
    t += 80;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_UP, cx, cy, t));
    CHECK(bd_mascot_state(m) == BD_MASCOT_REACT);
    CHECK(strcmp(bd_mascot_clip_name(m), "Anim") == 0);
    CHECK(bd_mascot_animating(m));

    /* step at 40 ms: pose 1 (SPARSE) must report exactly the
     * two band rects in screen coords; pose 2 (SAME) must
     * report no damage at all */
    int saw_bands = 0, saw_same = 0;
    tk_canvas *cv = tk_canvas_new(800, 480);
    for (int i = 0; i < 8 && !(saw_bands && saw_same); i++) {
        t += 40;
        int nd = bd_mascot_tick(m, t, dmg, 8);
        if (nd == 2 && !saw_bands) {
            saw_bands = 1;
            CHECK(dmg[0].x == r1.x + 2 && dmg[0].y == r1.y + 2 &&
                  dmg[0].w == 4 && dmg[0].h == 4);
            CHECK(dmg[1].x == r1.x + 30 && dmg[1].y == r1.y + 30 &&
                  dmg[1].w == 1 && dmg[1].h == 1);
            /* composed image: blue block, white pixel, red base */
            tk_canvas_clear(cv, (tk_color){ 0, 0, 255, 255 });
            bd_mascot_paint(m, cv, NULL, 0);
            tk_color bl = tk_canvas_pixel(cv, r1.x + 3, r1.y + 3);
            CHECK(bl.r == 0 && bl.g == 0 && bl.b == 255);
            tk_color wh = tk_canvas_pixel(cv, r1.x + 30,
                                          r1.y + 30);
            CHECK(wh.r == 255 && wh.g == 255 && wh.b == 255);
            tk_color rd = tk_canvas_pixel(cv, r1.x + 16,
                                          r1.y + 20);
            CHECK(rd.r == 255 && rd.g == 0 && rd.b == 0);
        } else if (nd == 0 && saw_bands) {
            saw_same = 1;
        }
    }
    CHECK(saw_bands);
    CHECK(saw_same);

    /* clip ends -> back to a Stay clip; the FULL frame must
     * clear the sparse changes out of the buffer */
    for (int i = 0; i < 12 && bd_mascot_state(m) == BD_MASCOT_REACT;
         i++) {
        t += 40;
        (void)bd_mascot_tick(m, t, dmg, 8);
    }
    CHECK(bd_mascot_state(m) != BD_MASCOT_REACT);
    tk_canvas_clear(cv, (tk_color){ 0, 0, 255, 255 });
    bd_mascot_paint(m, cv, NULL, 0);
    tk_rect r2 = bd_mascot_rect(m);
    tk_color bl = tk_canvas_pixel(cv, r2.x + 3, r2.y + 3);
    CHECK(bl.r == 255 && bl.g == 0 && bl.b == 0); /* red again */
    tk_canvas_free(cv);

    /* big time jump into the middle of Anim: the forward walk
     * must apply FULL+SPARSE so the image is the pose-2 one */
    tk_rect rr = bd_mascot_rect(m);
    cx = rr.x + rr.w / 2;
    cy = rr.y + rr.h / 2;
    t += 50;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_DOWN, cx, cy, t));
    t += 80;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_UP, cx, cy, t));
    CHECK(bd_mascot_state(m) == BD_MASCOT_REACT);
    t += 250; /* lands on pose 2 (SAME over SPARSE) */
    (void)bd_mascot_tick(m, t, dmg, 8);
    cv = tk_canvas_new(800, 480);
    tk_canvas_clear(cv, (tk_color){ 0, 0, 255, 255 });
    bd_mascot_paint(m, cv, NULL, 0);
    tk_rect r3 = bd_mascot_rect(m);
    bl = tk_canvas_pixel(cv, r3.x + 3, r3.y + 3);
    CHECK(bl.r == 0 && bl.g == 0 && bl.b == 255); /* blue kept */
    tk_canvas_free(cv);

    bd_mascot_free(m);
    unlink(path);
    free(path);
}

int main(void)
{
    /* bad inputs */
    CHECK(bd_mascot_new(NULL) == NULL);
    bd_mascot_config bad = { 0 };
    bad.pack_path = "/nonexistent/none.bdm";
    bad.floor_area = (tk_rect){ 0, 0, 800, 424 };
    CHECK(bd_mascot_new(&bad) == NULL);

    char *path = write_pack();
    CHECK(path != NULL);
    bd_mascot *m = new_test_mascot(path);
    CHECK(m != NULL);
    if (!m) return 1;
    CHECK(bd_mascot_size(m) == 100);
    size_t mb = 0;
    int nc = 0, nf = 0, np = 0;
    bd_mascot_stats(m, &mb, &nc, &nf, &np);
    CHECK(mb > 16000 && nc == 4 && nf == 2 && np == 2);

    /* spawn: falls from the top of the floor area to y=424 */
    CHECK(bd_mascot_state(m) == BD_MASCOT_FALL);
    tk_rect r0 = bd_mascot_rect(m);
    CHECK(r0.w == 32 && r0.h == 40);
    CHECK(r0.y < 100); /* near the top (anchor y=8, ay=100) */
    tk_rect dmg[2];
    int nd = bd_mascot_tick(m, 40, dmg, 2);
    CHECK(nd > 0); /* falling = damage every tick */
    run_ms(m, 80, 4000);
    CHECK(bd_mascot_state(m) != BD_MASCOT_FALL);
    tk_rect r1 = bd_mascot_rect(m);
    CHECK(r1.y + r1.h <= 424 + 40); /* sprite bottom near floor:
        rect bottom = anchor(424) - ay(100) + h(40) + ... check
        the anchor sits on the floor via the rect geometry */
    CHECK(r1.y == 424 - 100 + 0);   /* ay=100, by=0 -> y=324 */

    /* paint over a blue background: frame 0 is opaque red;
     * frame 1 (if behaviour already switched to Walk) is
     * half-alpha green premultiplied: g=128, b=255*127/255=127 */
    tk_canvas *cv = tk_canvas_new(800, 480);
    tk_canvas_clear(cv, (tk_color){ 0, 0, 255, 255 });
    bd_mascot_paint(m, cv, NULL, 0);
    tk_color in = tk_canvas_pixel(cv, r1.x + 16, r1.y + 20);
    CHECK((in.r == 255 && in.g == 0 && in.b == 0) ||
          (in.r == 0 && in.g == 128 && in.b == 127));
    tk_color out = tk_canvas_pixel(cv, 10, 10);
    CHECK(out.b == 255 && out.r == 0);
    tk_canvas_free(cv);

    /* idle/walk behaviour runs and produces bounded damage */
    long t = 4200;
    int moving_frames = 0;
    for (int i = 0; i < 250; i++) { /* 10 s */
        t += 40;
        if (bd_mascot_tick(m, t, dmg, 2) > 0) moving_frames++;
    }
    CHECK(moving_frames > 0);

    /* drag: grab the sprite, move it up, release -> falls */
    tk_rect rr = bd_mascot_rect(m);
    int cx = rr.x + rr.w / 2, cy = rr.y + rr.h / 2;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_DOWN, cx, cy, t));
    t += 50;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_MOVE, cx, 120, t));
    CHECK(bd_mascot_state(m) == BD_MASCOT_DRAG);
    tk_rect rd = bd_mascot_rect(m);
    CHECK(rd.y < rr.y); /* lifted */
    t += 50;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_UP, cx, 120, t));
    CHECK(bd_mascot_state(m) == BD_MASCOT_FALL);
    run_ms(m, t + 40, 4000);
    CHECK(bd_mascot_state(m) != BD_MASCOT_FALL);

    /* tap: quick down/up on the sprite -> reaction state or a
     * clip change */
    rr = bd_mascot_rect(m);
    cx = rr.x + rr.w / 2;
    cy = rr.y + rr.h / 2;
    const char *before = bd_mascot_clip_name(m);
    char before_buf[32];
    snprintf(before_buf, sizeof(before_buf), "%s", before);
    t += 100;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_DOWN, cx, cy, t));
    t += 80;
    CHECK(bd_mascot_pointer(m, TK_EV_POINTER_UP, cx, cy, t));
    CHECK(bd_mascot_state(m) == BD_MASCOT_REACT ||
          strcmp(bd_mascot_clip_name(m), before_buf) != 0 ||
          bd_mascot_state(m) == BD_MASCOT_IDLE);

    /* hide: one damage rect (the vacated sprite), then quiet */
    tk_rect last = bd_mascot_rect(m);
    bd_mascot_set_visible(m, false);
    CHECK(bd_mascot_rect(m).w == 0);
    nd = bd_mascot_tick(m, t + 200, dmg, 2);
    CHECK(nd == 1 && dmg[0].x == last.x && dmg[0].y == last.y);
    nd = bd_mascot_tick(m, t + 240, dmg, 2);
    CHECK(nd == 0);
    bd_mascot_set_visible(m, true);
    CHECK(bd_mascot_rect(m).w > 0);

    bd_mascot_free(m);
    unlink(path);
    free(path);

    test_v2();
    test_v3();
    test_v3_region_order();
    test_canvas_width();

    printf("test_mascot: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
