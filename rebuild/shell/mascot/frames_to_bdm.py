#!/usr/bin/env python3
# frames_to_bdm.py - PNG frame sequences -> Brainux mascot
# pack (.bdm), format v2 (sparse delta frames) or v1 (full
# frames).
#
# Input is a directory with manifest.json + one subdir per
# clip holding f0000.png, f0001.png, ... (see bake_live2d.py
# for a producer). All frames share one canvas; the manifest
# gives the anchor (feet, source coords), clip types, the
# floor pool and the special clips by name.
#
# ---- v2 sparse-delta encoding ----
# A whole-body character at 30 fps changes only a few
# percent of its pixels per frame, but the changes are
# scattered (hair tips, chest, skirt hem, blinking eyes), so
# rectangle deltas cover the whole body and save nothing.
# v2 therefore stores, per kept pose:
#   kind 0 "full"   - clear the composed buffer, copy this
#                     rect (the frame's alpha-trim rect).
#                     Pose 0 of every clip and every
#                     keyint-th pose are full (keyframes).
#   kind 1 "sparse" - a scanline run list (skip/len/values)
#                     of exactly the pixels that changed by
#                     more than --tol per channel vs the
#                     composed buffer, applied in place.
#                     The frame record also carries up to 8
#                     damage rects (per-row change runs
#                     merged vertically while their x-ranges
#                     overlap, so they hug the moved edges)
#                     so the session repaints only where
#                     pixels moved.
#   kind 2 "same"   - nothing changed beyond --tol; no
#                     pixels stored.
# The converter SIMULATES the runtime buffer: the change
# mask is always computed against the reconstructed buffer,
# never against the previous source frame, so the error of
# every displayed pixel is bounded by --tol per channel at
# all times (verified by --verify, which reports the actual
# worst deviation). --tol is in premultiplied channel units
# (0-255); 8 is far below visibility for scattered AA-edge
# pixels, and stored patch pixels are always exact.
#
# Before encoding, an adaptive frame-drop pass merges frames
# whose visible change (same --tol mask, fraction of the
# frame's alpha-bbox area) vs the last KEPT frame is below
# --drop-thresh - slow motion (breathing) collapses to a
# few fps while fast motion keeps the full rate; a pose
# never spans more than --max-gap ms. Dropped frames lend
# their duration to the previous kept pose. The drop pass
# runs once, at the largest size, so all sizes share one
# pose table shape.
#
# The runtime keeps ONE composed sprite buffer (crop-sized)
# in RAM, applies sparse runs (memcpy-class work), and
# blits from the buffer clipped to the damage bands.
#
# v1 mode writes the classic format (every frame full,
# global dedupe) from the same frames, for A/B measurement.
#
# Scaling is done in premultiplied space (PIL 'RGBa') so
# Lanczos cannot bleed colour out of transparent edges.
#
# Usage:
#   frames_to_bdm.py FRAMES_DIR -o OUT.bdm [--sizes 248,320]
#                    [--format v2|v1] [--keyint 45] [--tol 8]
#                    [--drop-thresh 0.006] [--max-gap 83]
#   frames_to_bdm.py --verify OUT.bdm FRAMES_DIR --sizes ...

import argparse
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

KIND_FULL, KIND_SPARSE, KIND_SAME = 0, 1, 2
TYPE_MAP = {'Stay': 0, 'Move': 1, 'Animate': 2}
FRAME2_SIZE = 20    # v2 frame record bytes
PATCH_SIZE = 12     # v2 patch (damage band) record bytes
SIZE2_SIZE = 32     # v2 size record bytes
SIZE3_SIZE = 48     # v3 size record bytes (v2 + regions/565)
REGION_ALPHA = 128  # v3 region silhouette threshold (FORMAT 5b)
REGION_BAND_REF = 8   # v3 region band height at the 320 size
REGION_TIGHT_MAX = 1.15  # v3 verify: max mean tightness ratio
REGION_RECT_MAX = 100  # v3 bake/verify: max region rects per pose


def region_band_h(size_h):
    """Band height for the v3 region tiling at a given size
    height (FORMAT.md 5b): the reference 8 rows at height 320,
    scaled proportionally (height / 40, round-half-even on the
    exact rational) and clamped to [4, 8]. A flat 8 rows at
    height 248 measured mean tightness 1.163 - over the 1.15
    bound - because the same absolute slack weighs more on a
    smaller silhouette; the proportional band keeps the
    relative slack (and the rect count, ~110) in the same
    class at every size, and is exactly 8 at 320, the size
    the CE side consumes."""
    n, d = size_h, 40
    q, r = divmod(n, d)
    if 2 * r > d or (2 * r == d and q % 2 == 1):
        q += 1
    return max(4, min(REGION_BAND_REF, q))


def load_manifest(frames_dir):
    with open(os.path.join(frames_dir, 'manifest.json')) as f:
        return json.load(f)


def load_clip_frames(frames_dir, clip):
    out = []
    d = os.path.join(frames_dir, clip['dir'])
    for i in range(clip['frames']):
        out.append(Image.open(os.path.join(
            d, 'f%04d.png' % i)).convert('RGBA'))
    return out


def union_crop(images):
    x0 = y0 = None
    x1 = y1 = 0
    for im in images:
        bb = im.getchannel('A').getbbox()
        if bb is None:
            continue
        x0 = bb[0] if x0 is None else min(x0, bb[0])
        y0 = bb[1] if y0 is None else min(y0, bb[1])
        x1 = max(x1, bb[2])
        y1 = max(y1, bb[3])
    if x0 is None:
        raise ValueError('all frames fully transparent')
    return (x0, y0, x1, y1)


def compute_crop(man, frames_dir):
    crop = None
    for clip in man['clips']:
        c = union_crop(load_clip_frames(frames_dir, clip))
        crop = c if crop is None else (
            min(crop[0], c[0]), min(crop[1], c[1]),
            max(crop[2], c[2]), max(crop[3], c[3]))
    return crop


def scale_premult(im, crop, scale):
    """Crop + Lanczos-scale in premultiplied space. Returns
    (premult uint32 array, PIL RGBa image) of the crop canvas
    at this size."""
    sub = im.crop(crop).convert('RGBa')
    nw = max(1, int(round(sub.width * scale)))
    nh = max(1, int(round(sub.height * scale)))
    if (nw, nh) != sub.size:
        sub = sub.resize((nw, nh), Image.LANCZOS)
    arr = np.asarray(sub).astype(np.uint32)
    pm = (arr[..., 3] << 24) | (arr[..., 0] << 16) | \
         (arr[..., 1] << 8) | arr[..., 2]
    return pm, sub


def alpha_bbox(pm):
    ys, xs = np.nonzero(pm >> 24)
    if len(xs) == 0:
        return (0, 0, 1, 1)
    return (int(xs.min()), int(ys.min()),
            int(xs.max()) + 1, int(ys.max()) + 1)


def frame_durations(n, fps):
    out = []
    prev = 0
    for i in range(n):
        cur = int(round((i + 1) * 1000.0 / fps))
        out.append(cur - prev)
        prev = cur
    return out


def chan_max_diff(a, b):
    """Max per-channel abs difference of two packed premult
    arrays. Channels are unpacked BEFORE subtracting: packed
    subtraction borrows across channel boundaries and both
    invents differences (harmless, costs bytes) and can hide
    real ones (a large swing in a low channel cancelled by
    the opposite swing above it) - the error bound this
    converter promises is per-channel, so the arithmetic
    must be per-channel too."""
    d = np.abs(((a >> 24) & 0xFF).astype(np.int32) -
               ((b >> 24) & 0xFF).astype(np.int32))
    for sh in (16, 8, 0):
        d = np.maximum(
            d, np.abs(((a >> sh) & 0xFF).astype(np.int32) -
                      ((b >> sh) & 0xFF).astype(np.int32)))
    return d


def tol_mask(a, b, tol):
    """Pixels differing by more than tol in any premultiplied
    channel (a, b: uint32 premult arrays)."""
    return chan_max_diff(a, b) > tol


def damage_bands(mask, max_bands=8):
    """Damage rects covering the change mask. Per-row x-runs
    are merged vertically while their x-ranges overlap, so
    the rects hug the silhouette edges that actually moved
    (hair tips, skirt hem, hands) instead of spanning the
    whole body width for every changed row. Measured on the
    Hiyori bake at 320 px: mean repaint area per sparse
    frame 16,513 px (row bands) -> 14,742 px (run-merged),
    vs 26.7K px for the full extent and ~53K px for the v1
    runtime's old+new-rect damage; the remaining area is
    intrinsic (a full-body sway moves outline edges at
    almost every height).
    Beyond max_bands, y-adjacent pairs whose union adds the
    least area are merged. Every changed pixel stays
    covered."""
    rects = []
    active = []  # [x0, x1, y0, y1) open rects
    for y in range(mask.shape[0]):
        xs = np.nonzero(mask[y])[0]
        runs = []
        if len(xs):
            s = p = int(xs[0])
            for v in xs[1:]:
                v = int(v)
                if v > p + 1:
                    runs.append((s, p + 1))
                    s = v
                p = v
            runs.append((s, p + 1))
        extended = set()
        for x0, x1 in runs:
            hit = -1
            for i, a in enumerate(active):
                if i in extended:
                    continue
                if x0 <= a[1] + 1 and a[0] - 1 <= x1:
                    hit = i
                    break
            if hit >= 0:
                a = active[hit]
                a[0] = min(a[0], x0)
                a[1] = max(a[1], x1)
                a[3] = y + 1
                extended.add(hit)
            else:
                active.append([x0, x1, y, y + 1])
                extended.add(len(active) - 1)
        keep = []
        for i, a in enumerate(active):
            if i in extended or a[3] > y:
                keep.append(a)
            else:
                rects.append((a[0], a[2], a[1] - a[0],
                              a[3] - a[2]))
        active = keep
    for a in active:
        rects.append((a[0], a[2], a[1] - a[0], a[3] - a[2]))

    def area(r):
        return r[2] * r[3]

    # Merge down to max_bands. Only y-adjacent pairs (in
    # y-sorted order) are candidates: merging distant rects is
    # never what we want, and the all-pairs scan is O(n^3) on
    # scattered masks (hundreds of edge rects at 320 px).
    rects.sort(key=lambda r: (r[1], r[0]))
    while len(rects) > max_bands:
        bi, badd, bu = -1, None, None
        for i in range(len(rects) - 1):
            a, b = rects[i], rects[i + 1]
            x0 = min(a[0], b[0])
            y0 = min(a[1], b[1])
            x1 = max(a[0] + a[2], b[0] + b[2])
            y1 = max(a[1] + a[3], b[1] + b[3])
            add = (x1 - x0) * (y1 - y0) - area(a) - area(b)
            if badd is None or add < badd:
                bi, badd = i, add
                bu = (x0, y0, x1 - x0, y1 - y0)
        rects[bi:bi + 2] = [bu]
    return rects


def silhouette_regions(buf, band_h):
    """Window-region list of the composed frame for the v3
    region section (FORMAT.md 5b): the alpha >= REGION_ALPHA
    silhouette of buf, tiled by banded run union - the canvas
    is cut into horizontal bands of band_h rows (see
    region_band_h for the size-dependent definition), each
    band's silhouette is projected onto the x axis (a column
    is covered iff any row of the band has a silhouette pixel
    in it), and every maximal run of covered columns becomes
    one rect spanning the full band height. Rects come out in
    band-major, then x order, and are pairwise distinct.

    This replaces the old derivation (damage_bands over the
    alpha > 0 mask with no band limit), whose vertical merge
    EXPANDS each rect's x-range to the union of everything it
    touches, so tall shapes degenerated to their bounding box
    (measured union/silhouette 1.64x for Miku HD, 1.98x for
    Mythra, alpha > 0 basis). On WinCE the region list IS the
    window shape, so the slack showed as black blocks around
    the character. The banded union bounds the slack to the
    band height: measured mean tightness and rect counts are
    in REPORT-region-tightness.md. The alpha threshold also
    drops the sub-128 fringe pixels (AA edges and the 565
    profile's amplified straight-colour edge values) out of
    the window entirely, as the CE side requested."""
    sil = (buf >> 24) >= REGION_ALPHA
    height, width = sil.shape
    rects = []
    for y0 in range(0, height, band_h):
        y1 = min(y0 + band_h, height)
        xs = np.nonzero(sil[y0:y1].any(axis=0))[0]
        if not len(xs):
            continue
        s = p = int(xs[0])
        for v in xs[1:]:
            v = int(v)
            if v > p + 1:
                rects.append((s, y0, p + 1 - s, y1 - y0))
                s = v
            p = v
        rects.append((s, y0, p + 1 - s, y1 - y0))
    return rects


def merge_exact_rects(rects):
    """Merge vertically contiguous rects with identical (x, w)
    into single taller rects. The union is unchanged, so this
    step is free: it only removes band-boundary splits the
    banded tiling introduced inside straight vertical runs of
    the silhouette. Output is sorted by (y, x)."""
    groups = {}
    for (x, y, w, h) in rects:
        groups.setdefault((x, w), []).append((y, h))
    out = []
    for (x, w), lst in groups.items():
        lst.sort()
        cy, ch = lst[0]
        for (y, h) in lst[1:]:
            if y == cy + ch:
                ch += h
            else:
                out.append((x, cy, w, ch))
                cy, ch = y, h
        out.append((x, cy, w, ch))
    out.sort(key=lambda r: (r[1], r[0]))
    return out


def cap_region_list(rects, cap, shape):
    """Reduce a region list to at most cap rects (FORMAT.md
    5b): first the free exact merge, then greedy merges of
    adjacent pairs - vertically touching (any x relation) or
    side by side in the same band span - always picking the
    pair whose bounding box newly covers the fewest pixels
    (exact count against the current list's coverage), ties
    broken by list order. The list is kept sorted by (y, x)
    and duplicate rects (a merge can reproduce an existing
    one) are dropped, preserving the pairwise-distinct
    invariant. If no adjacent pair exists while still over
    the cap, fall back to the pair with the cheapest bounding
    -box slack overall, so the procedure always terminates
    at the cap when cap >= 1.
    Coverage only ever grows, so silhouette coverage is
    preserved; the cost is tightness, which verify measures.
    """
    if cap <= 0 or len(rects) <= cap:
        return rects
    rects = merge_exact_rects(rects)
    if len(rects) <= cap:
        return rects
    cnt = np.zeros(shape, dtype=np.int32)
    for (x, y, w, h) in rects:
        cnt[y:y + h, x:x + w] += 1

    def added_of(bb):
        x, y, w, h = bb
        return int((cnt[y:y + h, x:x + w] == 0).sum())

    while len(rects) > cap:
        n = len(rects)
        best = None      # ((added, i, j), bb): adjacent pairs
        fallback = None  # ((bbox slack, i, j), bb): all pairs
        for i in range(n):
            a = rects[i]
            for j in range(i + 1, n):
                b = rects[j]
                bb = (min(a[0], b[0]), min(a[1], b[1]),
                      max(a[0] + a[2], b[0] + b[2]) -
                      min(a[0], b[0]),
                      max(a[1] + a[3], b[1] + b[3]) -
                      min(a[1], b[1]))
                vert = a[1] + a[3] == b[1] or \
                    b[1] + b[3] == a[1]
                horiz = a[1] == b[1] and a[3] == b[3]
                if vert or horiz:
                    key = (added_of(bb), i, j)
                    if best is None or key < best[0]:
                        best = (key, bb)
                else:
                    key = (bb[2] * bb[3] - a[2] * a[3] -
                           b[2] * b[3], i, j)
                    if fallback is None or key < fallback[0]:
                        fallback = (key, bb)
        # Adjacent pairs are the candidate set; the fallback
        # (cheapest bbox slack overall) can only trigger when
        # no adjacent pair exists at all.
        _key, bb = best if best is not None else fallback
        i, j = _key[1], _key[2]
        a, b = rects[i], rects[j]
        for r in (a, b):
            cnt[r[1]:r[1] + r[3], r[0]:r[0] + r[2]] -= 1
        cnt[bb[1]:bb[1] + bb[3], bb[0]:bb[0] + bb[2]] += 1
        rects = [r for k, r in enumerate(rects)
                 if k != i and k != j]
        rects.append(bb)
        rects.sort(key=lambda r: (r[1], r[0]))
    return list(dict.fromkeys(rects))


def silhouette_regions_capped(buf, band_h, cap):
    """silhouette_regions + the FORMAT.md 5b rect cap.
    Returns (rects, capped, tightness): capped says whether
    the cap procedure ran (the pose's raw list exceeded the
    cap), tightness is the final list's union / silhouette
    ratio. Poses at or under the cap keep the raw banded
    list untouched."""
    rects = silhouette_regions(buf, band_h)
    if cap <= 0 or len(rects) <= cap:
        return rects, False, None
    sil = (buf >> 24) >= REGION_ALPHA
    out = cap_region_list(rects, cap, buf.shape)
    cover = np.zeros(sil.shape, dtype=bool)
    for (x, y, w, h) in out:
        cover[y:y + h, x:x + w] = True
    sa = int(sil.sum())
    tight = int(cover.sum()) / sa if sa else 0.0
    return out, True, tight


def sparse_payload(mask, values):
    """Run-encode the changed pixels in scanline order:
    (u16 skip, u16 len, len * u32 values)*, terminated by a
    (0, 0) run. values is the flat source premult array."""
    idx = np.nonzero(mask.ravel())[0]
    out = bytearray()
    pos = 0
    i = 0
    flat = values.ravel()
    while i < len(idx):
        j = i
        while j + 1 < len(idx) and idx[j + 1] == idx[j] + 1:
            j += 1
        skip = int(idx[i]) - pos
        run = int(idx[j]) - int(idx[i]) + 1
        while skip > 65535:
            out += struct.pack('<HH', 65535, 0)
            skip -= 65535
        out += struct.pack('<HH', skip, run)
        out += flat[idx[i]:idx[j] + 1].astype('<u4').tobytes()
        pos = int(idx[j]) + 1
        i = j + 1
    out += struct.pack('<HH', 0, 0)
    return bytes(out)


def sparse_payload_u16(mask, values):
    """sparse_payload with u16 values (RGB565 profile)."""
    idx = np.nonzero(mask.ravel())[0]
    out = bytearray()
    pos = 0
    i = 0
    flat = values.ravel()
    while i < len(idx):
        j = i
        while j + 1 < len(idx) and idx[j + 1] == idx[j] + 1:
            j += 1
        skip = int(idx[i]) - pos
        run = int(idx[j]) - int(idx[i]) + 1
        while skip > 65535:
            out += struct.pack('<HH', 65535, 0)
            skip -= 65535
        out += struct.pack('<HH', skip, run)
        out += flat[idx[i]:idx[j] + 1].astype('<u2').tobytes()
        pos = int(idx[j]) + 1
        i = j + 1
    out += struct.pack('<HH', 0, 0)
    return bytes(out)


def to565(pm):
    """Straight RGB565 quantization of a premult ARGB canvas.
    The one definition both 565 profiles share (FORMAT.md
    5b): un-premultiply with c_s = min(255, c_pm * 255 //
    max(a, 1)) per channel, then round to the nearest level,
    L = (c_s * maxlevel + 127) // 255 (maxlevel 31/63/31).
    Expansion (for error measurement) is v * 255 // maxv.
    Rounding replaced truncation on 2026-11-21 (p61): worth
    +1.55 dB (Mythra) / +1.93 dB (Miku) of silhouette
    quantization PSNR on the production bakes, matching the
    delta the BMAS converter measured for the same change."""
    a = ((pm >> 24) & 0xFF).astype(np.uint32)
    amax = np.maximum(a, 1)
    rs = np.minimum(255, (((pm >> 16) & 0xFF) * 255) // amax)
    gs = np.minimum(255, (((pm >> 8) & 0xFF) * 255) // amax)
    bs = np.minimum(255, ((pm & 0xFF) * 255) // amax)
    return (((rs * 31 + 127) // 255) << 11 |
            ((gs * 63 + 127) // 255) << 5 |
            ((bs * 31 + 127) // 255)).astype(np.uint16)


def bleed565(tgt, alpha, rects):
    """Edge colour bleed for the RGB565 profile (p61): fill
    region-covered pixels outside the alpha >= REGION_ALPHA
    silhouette with the nearest silhouette pixel's 565 colour.
    The baked region list IS the CE window shape, and its
    banded rects (plus cap-merge slack) expose sub-silhouette
    pixels whose straight 565 value is black (an alpha-0 pixel
    un-premultiplies to 0), which the guest paints as black
    dashes along the silhouette. BMAS has bled these since
    p56 (bleed_colors in to_bmas3.py); this is the same fix
    for the .bdm profile. Measured on Mythra @320 Stand f0:
    dark fringe pixels (covered, sub-silhouette, luminance
    < 64) 1365 -> 2. Only pixels inside the region union
    change. The union can overhang the alpha bbox by up to a
    band height (band rects span full bands), so the sparse
    encoder widens the FULL record's stored rect to cover it.
    Returns (target, filled_pixel_count)."""
    seed = alpha >= REGION_ALPHA
    if not seed.any() or not rects:
        return tgt, 0
    cover = np.zeros(alpha.shape, dtype=bool)
    for (x, y, w, h) in rects:
        cover[y:y + h, x:x + w] = True
    fill = cover & ~seed
    n = int(fill.sum())
    if not n:
        return tgt, 0
    near = None
    try:
        from scipy import ndimage
        _, inds = ndimage.distance_transform_edt(
            ~seed, return_indices=True)
        near = tgt[inds[0], inds[1]]
    except Exception:
        near = None
    if near is None:
        # Pure-numpy fallback: iterative 4-neighbour dilation
        # of the seed colours until every fill pixel is hit.
        filled = seed.copy()
        near = tgt.copy()
        H, W = alpha.shape
        for _ in range(H + W):
            if (fill & ~filled).sum() == 0:
                break
            fp = np.pad(filled, 1, constant_values=False)
            cp = np.pad(near, 1)
            grow = np.zeros_like(filled)
            cand = near.copy()
            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                nf = fp[1 + dy:1 + dy + H, 1 + dx:1 + dx + W]
                nc = cp[1 + dy:1 + dy + H, 1 + dx:1 + dx + W]
                take = nf & ~filled & ~grow
                cand[take] = nc[take]
                grow |= nf
            newpix = grow & ~filled
            if not newpix.any():
                break
            near[newpix] = cand[newpix]
            filled |= newpix
    out = tgt.copy()
    out[fill] = near[fill]
    return out, n


def expand565(v):
    """RGB565 -> packed 0x00RRGGBB straight values (the
    expansion of FORMAT.md 5b: c = level * 255 // maxlevel).
    Injective, so chan_max_diff on expansions is 0 iff the
    565 values are equal."""
    r = (((v >> 11) & 31).astype(np.uint32) * 255) // 31
    g = (((v >> 5) & 63).astype(np.uint32) * 255) // 63
    b = ((v & 31).astype(np.uint32) * 255) // 31
    return (r << 16) | (g << 8) | b


def decide_kept(man, frames_dir, crop, height, tol,
                drop_thresh, max_gap_ms):
    """Adaptive drop pass, run ONCE at the largest size (clip
    metadata is shared by all sizes). Returns (kept_lists,
    worst_drop_frac)."""
    scale = height / (crop[3] - crop[1])
    kept_lists = []
    worst = 0.0
    for clip in man['clips']:
        ims = load_clip_frames(frames_dir, clip)
        durs = frame_durations(len(ims), man['fps'])
        pms = [scale_premult(im, crop, scale)[0] for im in ims]
        kept = [0]
        acc = 0
        for i in range(1, len(ims)):
            ref = pms[kept[-1]]
            bb = alpha_bbox(ref)
            area = max(1, (bb[2] - bb[0]) * (bb[3] - bb[1]))
            frac = float(tol_mask(pms[i], ref, tol).sum()) / area
            if frac > drop_thresh or acc + durs[i] > max_gap_ms \
                    or i == len(ims) - 1:
                kept.append(i)
                acc = 0
            else:
                acc += durs[i]
                worst = max(worst, frac)
        kept_lists.append(kept)
    return kept_lists, worst


def kept_durations(kept, durs):
    out = []
    for k, i in enumerate(kept):
        nxt = kept[k + 1] if k + 1 < len(kept) else len(durs)
        out.append(sum(durs[i:nxt]))
    return out


class SizeBake:
    def __init__(self):
        self.frame_recs = []
        self.patch_recs = []   # (bx, by, w, h) damage bands
        self.sparse = bytearray()
        self.poses = []
        self.region_lists = []  # v3: per-pose silhouette rects
        self.region_clip_lens = []
        self.rgb565_recs = []   # v3 full form: per-pose (bb, bytes)
        self.rgb565s = bytearray()  # v3 sparse form: pose records
        self.stats565 = {'full': 0, 'sparse': 0, 'same': 0,
                         'changed_px': 0, 'bleed_px': 0}
        self.stats = {'full': 0, 'sparse': 0, 'same': 0,
                      'stored_px': 0, 'changed_px': 0,
                      'kept': 0, 'dropped': 0,
                      'region_capped': 0,
                      'region_capped_tight': 0.0}


def bake_size(man, frames_dir, height, fmt, keyint, tol,
              full_frac, kept_lists, crop,
              capture_regions=False, capture_565=False,
              capture_565_sparse=False, rgb565_tol=0,
              rgb565_bleed=False,
              region_max_rects=REGION_RECT_MAX):
    clips = man['clips']
    scale = height / (crop[3] - crop[1])
    ax_c = int(round((man['anchor'][0] - crop[0]) * scale))
    ay_c = int(round((man['anchor'][1] - crop[1]) * scale))
    sb = SizeBake()

    for ci, clip in enumerate(clips):
        ims = load_clip_frames(frames_dir, clip)
        durs = frame_durations(len(ims), man['fps'])
        pms = []
        subs = []
        for im in ims:
            pm, sub = scale_premult(im, crop, scale)
            pms.append(pm)
            subs.append(sub)

        if fmt == 'v1':
            kept = list(range(len(ims)))
            kept_durs = durs
        else:
            kept = kept_lists[ci]
            kept_durs = kept_durations(kept, durs)
            sb.stats['kept'] += len(kept)
            sb.stats['dropped'] += len(ims) - len(kept)

        buf = None   # simulated composed buffer (v2)
        ce_buf = None   # simulated CE RGB565 buffer (v3 sparse)
        for ki, i in enumerate(kept):
            pm, sub = pms[i], subs[i]
            bb = alpha_bbox(pm)
            fi = len(sb.frame_recs)
            if fmt == 'v1':
                patch = sub.crop((bb[0], bb[1], bb[2], bb[3]))
                sb.frame_recs.append(
                    {'kind': KIND_FULL, 'img': patch,
                     'bx': bb[0], 'by': bb[1],
                     'w': bb[2] - bb[0], 'h': bb[3] - bb[1],
                     'key': patch.tobytes()})
                sb.stats['full'] += 1
                sb.stats['stored_px'] += (bb[2] - bb[0]) * (bb[3] - bb[1])
            else:
                force_full = (ki % keyint == 0)
                if buf is None or force_full:
                    kind = KIND_FULL
                else:
                    mask = tol_mask(pm, buf, tol)
                    nch = int(mask.sum())
                    area = max(1, (bb[2] - bb[0]) * (bb[3] - bb[1]))
                    if nch == 0:
                        kind = KIND_SAME
                    elif nch > full_frac * area:
                        kind = KIND_FULL
                    else:
                        kind = KIND_SPARSE
                if kind == KIND_FULL:
                    patch = sub.crop((bb[0], bb[1], bb[2], bb[3]))
                    sb.frame_recs.append(
                        {'kind': kind, 'img': patch,
                         'bx': bb[0], 'by': bb[1],
                         'w': bb[2] - bb[0], 'h': bb[3] - bb[1],
                         'bands': [], 'sparse_off': 0})
                    sb.stats['full'] += 1
                    sb.stats['stored_px'] += \
                        (bb[2] - bb[0]) * (bb[3] - bb[1])
                    buf = pm.copy()
                elif kind == KIND_SPARSE:
                    bands = damage_bands(mask)
                    bstart = len(sb.patch_recs)
                    sb.patch_recs.extend(bands)
                    soff = len(sb.sparse)
                    sb.sparse += sparse_payload(mask, pm)
                    sb.frame_recs.append(
                        {'kind': kind, 'img': None, 'bx': 0,
                         'by': 0, 'w': 0, 'h': 0,
                         'bands': list(range(bstart,
                                             len(sb.patch_recs))),
                         'sparse_off': soff})
                    sb.stats['sparse'] += 1
                    sb.stats['changed_px'] += nch
                    buf[mask] = pm[mask]
                else:
                    sb.frame_recs.append(
                        {'kind': kind, 'img': None, 'bx': 0,
                         'by': 0, 'w': 0, 'h': 0, 'bands': [],
                         'sparse_off': 0})
                    sb.stats['same'] += 1
            if fmt != 'v1' and capture_regions:
                # v3: window regions of the composed frame,
                # from the same buffer the runtime will show -
                # one computation feeds damage bands (Brainux)
                # and window regions (WinCE) alike. The tiling
                # is the banded run union of silhouette_regions
                # (alpha >= 128, size-proportional bands), NOT
                # the damage band merge: regions must stay TIGHT
                # to the silhouette (FORMAT.md 5b), damage bands
                # only need to cover change. Poses whose list
                # exceeds the rect cap go through the merge
                # procedure of cap_region_list (FORMAT.md 5b);
                # poses under the cap keep the raw list.
                rects, capped, tight = \
                    silhouette_regions_capped(
                        buf, region_band_h(height),
                        region_max_rects)
                sb.region_lists.append(rects)
                if capped:
                    sb.stats['region_capped'] += 1
                    sb.stats['region_capped_tight'] = max(
                        sb.stats['region_capped_tight'], tight)
            if fmt != 'v1' and capture_565:
                # v3 WinCE profile, full form: full frame per
                # pose in straight RGB565 (region clip discards
                # the off-silhouette pixels at blit time).
                bb2 = alpha_bbox(buf)
                sub = buf[bb2[1]:bb2[3], bb2[0]:bb2[2]]
                v565 = to565(sub).astype('<u2')
                sb.rgb565_recs.append((bb2, v565.tobytes()))
            if fmt != 'v1' and capture_565_sparse:
                # v3 WinCE profile, sparse form: the pose's
                # SOURCE frame in straight RGB565 (to565 of the
                # source premult canvas), delta-encoded against
                # a simulated CE buffer with the core's run
                # format at u16 width. With rgb565_tol = 0 the
                # mask is exact in 565 space and the CE
                # reconstruction equals the 565 quantization of
                # every source frame exactly; with rgb565_tol
                # > 0 the mask is bounded-error like the core
                # (stored values stay exact). Buffer resets per
                # clip and pose 0 of a clip is FULL, mirroring
                # the core's seek semantics.
                tgt = to565(pm)
                if rgb565_bleed:
                    # The pose's region list was appended just
                    # above (regions are captured before the 565
                    # profile in this loop); without regions,
                    # derive the same list on the fly.
                    if capture_regions:
                        rects565 = sb.region_lists[-1]
                    else:
                        rects565 = silhouette_regions_capped(
                            buf, region_band_h(height),
                            region_max_rects)[0]
                    tgt, nbl = bleed565(
                        tgt, (pm >> 24) & 0xFF, rects565)
                    sb.stats565['bleed_px'] += nbl
                if ce_buf is None:
                    ce_buf = np.zeros(tgt.shape, dtype=np.uint16)
                bb2 = alpha_bbox(pm)
                if rgb565_bleed and rects565:
                    # Banded region rects span full band heights,
                    # so the region union overhangs the alpha
                    # bbox by up to a band at the top/bottom
                    # (measured 56 px on Mythra @320 f0). The
                    # FULL record's stored rect must cover the
                    # union or the bled overhang colours never
                    # reach the CE buffer (the record carries
                    # its rect explicitly, so widening it is
                    # format-legal and reader-compatible).
                    rx0 = min(r[0] for r in rects565)
                    ry0 = min(r[1] for r in rects565)
                    rx1 = max(r[0] + r[2] for r in rects565)
                    ry1 = max(r[1] + r[3] for r in rects565)
                    bb2 = (min(bb2[0], rx0), min(bb2[1], ry0),
                           max(bb2[2], rx1), max(bb2[3], ry1))
                area = max(1, (bb2[2] - bb2[0]) *
                           (bb2[3] - bb2[1]))
                m565 = None
                n565 = 0
                if ki == 0:
                    kind565 = KIND_FULL
                else:
                    if rgb565_tol > 0:
                        # Bounded-error mask, the core's
                        # philosophy at 565 width: a pixel is
                        # stored only when the CE buffer's
                        # expanded value differs from the
                        # target's by more than rgb565_tol in
                        # some channel; stored values are
                        # exact, so the displayed error stays
                        # <= rgb565_tol at all times.
                        m565 = chan_max_diff(
                            expand565(tgt),
                            expand565(ce_buf)) > rgb565_tol
                    else:
                        m565 = tgt != ce_buf
                    n565 = int(m565.sum())
                    if n565 == 0:
                        kind565 = KIND_SAME
                    elif n565 > full_frac * area:
                        kind565 = KIND_FULL
                    else:
                        kind565 = KIND_SPARSE
                sb.rgb565s += struct.pack('<H', kind565)
                if kind565 == KIND_FULL:
                    sub = tgt[bb2[1]:bb2[3], bb2[0]:bb2[2]]
                    sb.rgb565s += struct.pack(
                        '<HHhh', bb2[2] - bb2[0],
                        bb2[3] - bb2[1], bb2[0], bb2[1])
                    sb.rgb565s += sub.astype('<u2').tobytes()
                    ce_buf[:, :] = 0
                    ce_buf[bb2[1]:bb2[3],
                           bb2[0]:bb2[2]] = tgt[bb2[1]:bb2[3],
                                                bb2[0]:bb2[2]]
                    sb.stats565['full'] += 1
                elif kind565 == KIND_SPARSE:
                    payload = sparse_payload_u16(m565, tgt)
                    sb.rgb565s += struct.pack('<I', len(payload))
                    sb.rgb565s += payload
                    ce_buf[m565] = tgt[m565]
                    sb.stats565['sparse'] += 1
                    sb.stats565['changed_px'] += n565
                else:
                    sb.stats565['same'] += 1
            sb.poses.append((fi, kept_durs[ki], ax_c, ay_c))

    if fmt == 'v1':
        uniq = {}
        order = []
        remap = {}
        for i, fr in enumerate(sb.frame_recs):
            if fr['key'] not in uniq:
                uniq[fr['key']] = len(order)
                order.append(fr)
            remap[i] = uniq[fr['key']]
        sb.poses = [(remap[fi], d, ax, ay)
                    for fi, d, ax, ay in sb.poses]
        sb.frame_recs = order
        sb.stats['full'] = len(order)
        sb.stats['stored_px'] = sum(fr['w'] * fr['h']
                                     for fr in order)

    # Atlas: full-frame rects only (sparse pixels live in the
    # sparse section, bands carry no pixels).
    atlas_w = 1024
    for fr in sb.frame_recs:
        if fr['img'] is not None:
            atlas_w = max(atlas_w, fr['img'].width)
    items = [(fr['img'], i) for i, fr in enumerate(sb.frame_recs)
             if fr['img'] is not None]
    items.sort(key=lambda kv: -kv[0].height)
    pos = {}
    x = y = row_h = 0
    for im, i in items:
        if x + im.width > atlas_w:
            x = 0
            y += row_h
            row_h = 0
        pos[i] = (x, y)
        x += im.width
        row_h = max(row_h, im.height)
    atlas_h = y + row_h
    atlas = Image.new('RGBa', (atlas_w, atlas_h), (0, 0, 0, 0))
    for im, i in items:
        atlas.paste(im, pos[i])
    arr = np.asarray(atlas).astype(np.uint32)
    atlas_pm = ((arr[..., 3] << 24) | (arr[..., 0] << 16) |
                (arr[..., 1] << 8) | arr[..., 2]).astype('<u4')

    frames_blob = bytearray()
    if fmt == 'v2':
        # Record layout (20 B): x, y, w, h, bx, by (as v1,
        # FULL only), kind u16, nband u16, aux u32.
        # SPARSE: x..by are 0, aux = byte offset into the
        # sparse section. A frame's damage bands are not
        # pointed to directly: bands are stored in pose order,
        # so the runtime recovers each frame's band start by
        # prefix-summing nband over the frames before it.
        for i, fr in enumerate(sb.frame_recs):
            if fr['kind'] == KIND_SPARSE:
                frames_blob += struct.pack(
                    '<HHHHhhHHI', 0, 0, 0, 0, 0, 0,
                    fr['kind'], len(fr['bands']),
                    fr['sparse_off'])
            else:
                ax_, ay_ = pos.get(i, (0, 0))
                frames_blob += struct.pack(
                    '<HHHHhhHHI', ax_, ay_, fr['w'], fr['h'],
                    fr['bx'], fr['by'], fr['kind'], 0, 0)
    else:
        for i, fr in enumerate(sb.frame_recs):
            ax_, ay_ = pos.get(i, (0, 0))
            frames_blob += struct.pack('<HHHHhh', ax_, ay_,
                                       fr['w'], fr['h'],
                                       fr['bx'], fr['by'])
    patches_blob = bytearray()
    for (bx, by, w, h) in sb.patch_recs:
        patches_blob += struct.pack('<HHHHhh', 0, 0, w, h, bx, by)
    poses_blob = bytearray()
    for fi, d, ax_, ay_ in sb.poses:
        poses_blob += struct.pack('<HHhhhh', fi, d, ax_, ay_, 0, 0)
    regions_blob = bytearray()
    if capture_regions:
        pos_i = 0
        for ci in range(len(clips)):
            prev = None
            for _ in range(len(kept_lists[ci])):
                rects = sb.region_lists[pos_i]
                pos_i += 1
                if prev is None:
                    regions_blob += struct.pack('<HH', 0xFFFF,
                                                len(rects))
                    for (x, y, w, h) in rects:
                        regions_blob += struct.pack('<HHHH',
                                                    x, y, w, h)
                else:
                    prev_set = set(prev)
                    cur_set = set(rects)
                    rem = [i for i, r in enumerate(prev)
                           if r not in cur_set]
                    add = [r for r in rects if r not in prev_set]
                    regions_blob += struct.pack('<HH', len(rem),
                                                len(add))
                    for i in rem:
                        regions_blob += struct.pack('<H', i)
                    for (x, y, w, h) in add:
                        regions_blob += struct.pack('<HHHH',
                                                    x, y, w, h)
                    # The bookkeeping list for the NEXT delta
                    # must be the DECODER's reconstruction -
                    # survivors in previous order, then the
                    # adds appended - NOT the fresh rects order:
                    # removal indices are positional, and the
                    # two orders diverge as soon as one delta
                    # lands (the region-coverage bug: indices
                    # computed against the fresh order removed
                    # the wrong rects on the decoder side and
                    # coverage failed on ~90% of poses). The
                    # list-order semantics are part of the spec
                    # (FORMAT.md 5b): after a delta, the list IS
                    # survivors-then-adds by definition.
                    rem_set = set(rem)
                    prev = [r for i, r in enumerate(prev)
                            if i not in rem_set] + add
                    continue
                prev = rects
    rgb565_blob = bytearray()
    if capture_565:
        for (bb2, payload) in sb.rgb565_recs:
            rgb565_blob += struct.pack('<HHHHhh', 0, 0,
                                       bb2[2] - bb2[0],
                                       bb2[3] - bb2[1],
                                       bb2[0], bb2[1])
            rgb565_blob += payload
    if capture_565_sparse:
        rgb565_blob = bytes(sb.rgb565s)
    return {
        'height': height, 'atlas_w': atlas_w, 'atlas_h': atlas_h,
        'atlas_bytes': atlas_pm.tobytes(),
        'frames_blob': bytes(frames_blob),
        'patches_blob': bytes(patches_blob),
        'sparse_blob': bytes(sb.sparse),
        'poses_blob': bytes(poses_blob),
        'regions_blob': bytes(regions_blob),
        'rgb565_blob': bytes(rgb565_blob),
        'rgb565_sparse': capture_565_sparse,
        'stats565': sb.stats565,
        'frame_count': len(sb.frame_recs),
        'patch_count': len(sb.patch_recs),
        'stride': FRAME2_SIZE if fmt == 'v2' else 12,
        'scale_q8': int(round(scale * 256)), 'stats': sb.stats,
        'crop': crop, 'kept_poses': len(sb.poses),
    }


def clip_tables(man, kept_per_clip):
    meta = []
    for c, kept in zip(man['clips'], kept_per_clip):
        total = int(round(c['frames'] * 1000.0 / man['fps']))
        meta.append({'name': c['name'],
                     'type': TYPE_MAP[c['type']],
                     'loop': bool(c['loop']),
                     'total': total, 'nposes': kept})
    by_name = {c['name']: i for i, c in enumerate(man['clips'])}
    pool = [(by_name[n], int(w)) for n, w in man['pool']]
    sp = man['specials']
    specials = tuple(by_name[sp[k]] for k in
                     ('fall', 'drag', 'thrown', 'stand'))
    return meta, pool, specials


def write_pack(path, man, fmt, sizes_data, kept_per_clip):
    meta, pool, specials = clip_tables(man, kept_per_clip)
    strtab = bytearray(b'\0')
    name_offs = []
    for c in meta:
        name_offs.append(len(strtab))
        strtab += c['name'].encode('utf-8') + b'\0'
    version = 3 if fmt == 'v3' else (2 if fmt == 'v2' else 1)
    has_delta = any(s['stats']['sparse'] or s['stats']['same']
                    for s in sizes_data)
    flags = 1 | (2 if has_delta else 0)
    if fmt == 'v3':
        if any(s.get('regions_blob') for s in sizes_data):
            flags |= 4
        if any(s.get('rgb565_blob') for s in sizes_data):
            flags |= 8
        if any(s.get('rgb565_sparse') for s in sizes_data):
            flags |= 16
    crop = sizes_data[0]['crop']
    size_rec = SIZE3_SIZE if fmt == 'v3' else \
        (SIZE2_SIZE if fmt == 'v2' else 24)

    off = 80
    strtab_off = off
    off += len(strtab)
    clips_off = off
    off += 16 * len(meta)
    pool_off = off
    off += 4 * len(pool)
    sizes_off = off
    off += size_rec * len(sizes_data)
    blobs = []
    for s in sizes_data:
        # v2 per size: frames | patches | sparse | poses | atlas
        frames_off = off
        off += len(s['frames_blob'])
        patches_off = off
        off += len(s['patches_blob'])
        sparse_off = off
        off += len(s['sparse_blob'])
        poses_off = off
        off += len(s['poses_blob'])
        atlas_off = off
        off += len(s['atlas_bytes'])
        regions_off = off
        off += len(s.get('regions_blob', b''))
        rgb565_off = off
        off += len(s.get('rgb565_blob', b''))
        blobs.append((frames_off, patches_off, sparse_off,
                      poses_off, atlas_off, regions_off,
                      rgb565_off))
    out = bytearray(off)
    struct.pack_into('<4sHHI', out, 0, b'BDM1', version, flags, off)
    nb = man['name'].encode('utf-8')[:23]
    out[12:12 + len(nb)] = nb
    struct.pack_into('<HHHHHH', out, 36, crop[2] - crop[0],
                     crop[3] - crop[1], len(meta), len(pool),
                     len(sizes_data), 0)
    struct.pack_into('<hhhh', out, 48, *specials)
    struct.pack_into('<IIII', out, 56, clips_off, pool_off,
                     sizes_off, strtab_off)
    out[strtab_off:strtab_off + len(strtab)] = strtab
    pose_start = 0
    for i, c in enumerate(meta):
        struct.pack_into('<HHHHIBBBB', out, clips_off + 16 * i,
                         name_offs[i], c['nposes'], c['total'],
                         c['total'], pose_start, c['type'], 0,
                         1 if c['loop'] else 0, 0)
        pose_start += c['nposes']
    for i, (ci, w) in enumerate(pool):
        struct.pack_into('<HH', out, pool_off + 4 * i, ci, w)
    for si, s in enumerate(sizes_data):
        frames_off, patches_off, sparse_off, poses_off, \
            atlas_off, regions_off, rgb565_off = blobs[si]
        so = sizes_off + size_rec * si
        if fmt == 'v3':
            struct.pack_into('<HHHHIIIIIHHIIII', out, so,
                             s['height'], s['atlas_w'],
                             s['atlas_h'], s['frame_count'],
                             frames_off, patches_off, sparse_off,
                             poses_off, atlas_off, s['scale_q8'],
                             s['stride'], regions_off,
                             len(s.get('regions_blob', b'')),
                             rgb565_off,
                             len(s.get('rgb565_blob', b'')))
        elif fmt == 'v2':
            struct.pack_into('<HHHHIIIIIHH', out, so,
                             s['height'], s['atlas_w'],
                             s['atlas_h'], s['frame_count'],
                             frames_off, patches_off, sparse_off,
                             poses_off, atlas_off, s['scale_q8'],
                             s['stride'])
        else:
            struct.pack_into('<HHHHIIIH', out, so,
                             s['height'], s['atlas_w'],
                             s['atlas_h'], s['frame_count'],
                             frames_off, poses_off, atlas_off,
                             s['scale_q8'])
        out[frames_off:frames_off + len(s['frames_blob'])] = \
            s['frames_blob']
        out[patches_off:patches_off + len(s['patches_blob'])] = \
            s['patches_blob']
        out[sparse_off:sparse_off + len(s['sparse_blob'])] = \
            s['sparse_blob']
        out[poses_off:poses_off + len(s['poses_blob'])] = \
            s['poses_blob']
        out[atlas_off:atlas_off + len(s['atlas_bytes'])] = \
            s['atlas_bytes']
        rb = s.get('regions_blob', b'')
        out[regions_off:regions_off + len(rb)] = rb
        xb = s.get('rgb565_blob', b'')
        out[rgb565_off:rgb565_off + len(xb)] = xb
    with open(path, 'wb') as f:
        f.write(out)
    return off


def convert(args):
    man = load_manifest(args.frames)
    crop = compute_crop(man, args.frames)
    if args.format in ('v2', 'v3'):
        kept_lists, worst = decide_kept(
            man, args.frames, crop, max(args.sizes), args.tol,
            args.drop_thresh, args.max_gap)
        print('drop pass: worst dropped-frame deviation %.4f '
              '(thresh %.4f)' % (worst, args.drop_thresh))
    else:
        kept_lists = None
    sizes_data = []
    for h in args.sizes:
        sd = bake_size(man, args.frames, h,
                       'v2' if args.format == 'v3' else args.format,
                       args.keyint, args.tol, args.full_frac,
                       kept_lists, crop,
                       capture_regions=(args.format == 'v3'),
                       capture_565=(args.format == 'v3' and
                                    args.rgb565),
                       capture_565_sparse=(args.format == 'v3' and
                                           args.rgb565_sparse),
                       rgb565_tol=args.rgb565_tol,
                       rgb565_bleed=args.rgb565_bleed,
                       region_max_rects=args.region_max_rects)
        sizes_data.append(sd)
        st = sd['stats']
        print('size %d: atlas %d B, sparse %d B, poses %d '
              '(full %d / sparse %d / same %d, stored px %d, '
              'changed px %d, dropped %d)' %
              (h, len(sd['atlas_bytes']), len(sd['sparse_blob']),
               sd['kept_poses'], st['full'], st['sparse'],
               st['same'], st['stored_px'], st['changed_px'],
               st['dropped']))
        if args.format == 'v3':
            print('size %d: regions %d B, rgb565 %d B' %
                  (h, len(sd['regions_blob']),
                   len(sd['rgb565_blob'])))
            if st['region_capped']:
                print('size %d: region rect cap %d applied to '
                      '%d poses, worst capped tightness %.3f' %
                      (h, args.region_max_rects,
                       st['region_capped'],
                       st['region_capped_tight']))
            if args.rgb565_sparse:
                s5 = sd['stats565']
                print('size %d: rgb565 sparse kinds '
                      'full %d / sparse %d / same %d, '
                      'changed px %d, bleed px %d' %
                      (h, s5['full'], s5['sparse'], s5['same'],
                       s5['changed_px'], s5['bleed_px']))
    kept_per_clip = [len(k) for k in kept_lists] \
        if kept_lists else [c['frames'] for c in man['clips']]
    total = write_pack(args.output, man, args.format, sizes_data,
                       kept_per_clip)
    print('%s: %d bytes (format %s)' % (args.output, total,
                                        args.format))
    return 0


def verify(args):
    """Reconstruct every kept frame from the pack and check
    the deviation from the freshly baked source frames: it
    must be <= --tol in every channel (the encoding bound),
    and the report gives the actual worst/mean deviation."""
    data = open(args.verify, 'rb').read()
    assert data[:4] == b'BDM1'
    ver, flags, total = struct.unpack_from('<HHI', data, 4)
    nclips, npool, nsizes = struct.unpack_from('<HHH', data, 40)
    clips_off, pool_off, sizes_off, strtab_off = \
        struct.unpack_from('<IIII', data, 56)
    print('version %d flags %d clips %d sizes %d' %
          (ver, flags, nclips, nsizes))
    if ver not in (2, 3):
        print('verify supports v2/v3 packs only')
        return 1
    rec = SIZE3_SIZE if ver == 3 else SIZE2_SIZE
    man = load_manifest(args.frames)
    totals = [struct.unpack_from('<H', data,
                                 clips_off + 16 * i + 2)[0]
              for i in range(nclips)]
    heights = [struct.unpack_from('<H', data,
                                  sizes_off + rec * si)[0]
               for si in range(nsizes)]
    crop = compute_crop(man, args.frames)
    kept_lists, _ = decide_kept(man, args.frames, crop,
                                max(heights), args.tol,
                                args.drop_thresh, args.max_gap)
    ok = True
    for si in range(nsizes):
        if ver == 3:
            (h, aw, ah, nf, fo, poff, soff, psoff, ao, sq8,
             stride, roff, rlen, xoff, xlen) = \
                struct.unpack_from('<HHHHIIIIIHHIIII', data,
                                   sizes_off + rec * si)
        else:
            (h, aw, ah, nf, fo, poff, soff, psoff, ao, sq8,
             stride) = struct.unpack_from('<HHHHIIIIIHH', data,
                                          sizes_off + rec * si)
            roff = rlen = xoff = xlen = 0
        if h not in args.sizes:
            continue
        atlas = np.frombuffer(data, dtype='<u4',
                              count=aw * ah, offset=ao).reshape(ah, aw)
        scale = h / (crop[3] - crop[1])
        chh = int(round((crop[3] - crop[1]) * scale))
        cww = int(round((crop[2] - crop[0]) * scale))
        buf = np.zeros((chh, cww), dtype=np.uint32)
        gpose = 0
        worst = 0
        dev_sum = 0.0
        dev_n = 0
        rptr = roff           # v3 region stream cursor
        xptr = xoff           # v3 rgb565 stream cursor
        cur_regions = None
        region_bad = 0
        tight_sum = 0.0       # v3 region tightness (per pose)
        tight_n = 0
        tight_max = 0.0
        rect_sum = 0          # v3 region rect count (per pose)
        rect_n = 0
        rect_max = 0
        x565_worst = 0
        cbuf565 = None        # v3 sparse-565 simulated CE buffer
        s565_bad = 0          # poses failing the 565 bound
        s565_worst = 0        # worst expanded deviation (tol>0)
        for ci, clip in enumerate(man['clips']):
            cur_regions = None
            cbuf565 = None
            ims = load_clip_frames(args.frames, clip)
            pms = [scale_premult(im, crop, scale)[0] for im in ims]
            kept = kept_lists[ci]
            if len(kept) != totals[ci]:
                print('size %d clip %s: kept %d != pack %d' %
                      (h, clip['name'], len(kept), totals[ci]))
                ok = False
                break
            for i in kept:
                (fx, fy, fw, fh, bx, by, kind, nband,
                 aux) = struct.unpack_from(
                    '<HHHHhhHHI', data, fo + gpose * stride)
                if kind == KIND_FULL:
                    buf[:, :] = 0
                    buf[by:by + fh, bx:bx + fw] = \
                        atlas[fy:fy + fh, fx:fx + fw]
                elif kind == KIND_SPARSE:
                    off = soff + aux
                    pos = 0
                    flat = buf.ravel()
                    while True:
                        skip, ln = struct.unpack_from(
                            '<HH', data, off)
                        off += 4
                        if ln == 0 and skip == 0:
                            break
                        pos += skip
                        if ln:
                            vals = np.frombuffer(
                                data, dtype='<u4', count=ln,
                                offset=off)
                            flat[pos:pos + ln] = vals
                            off += 4 * ln
                            pos += ln
                d = chan_max_diff(buf, pms[i])
                mx = int(d.max())
                worst = max(worst, mx)
                if mx > args.tol:
                    print('size %d clip %s frame %d: deviation '
                          '%d > tol %d' % (h, clip['name'], i,
                                           mx, args.tol))
                    ok = False
                blue = np.abs((buf & 0xFF).astype(np.int32) -
                              (pms[i] & 0xFF).astype(np.int32))
                dev_sum += float(blue.mean())
                dev_n += 1
                if ver == 3 and rlen:
                    # Region stream: decode this pose's list and
                    # check it covers the composed silhouette.
                    ndel, nadd = struct.unpack_from('<HH', data,
                                                    rptr)
                    rptr += 4
                    if ndel == 0xFFFF:
                        cur_regions = []
                        for _ in range(nadd):
                            cur_regions.append(
                                struct.unpack_from('<HHHH', data,
                                                   rptr))
                            rptr += 8
                    else:
                        rem = [struct.unpack_from('<H', data,
                                 rptr + 2 * k)[0]
                               for k in range(ndel)]
                        rptr += 2 * ndel
                        add = []
                        for _ in range(nadd):
                            add.append(struct.unpack_from(
                                '<HHHH', data, rptr))
                            rptr += 8
                        keep = [r for k, r in
                                enumerate(cur_regions)
                                if k not in set(rem)]
                        cur_regions = keep + add
                    cover = np.zeros((chh, cww), dtype=bool)
                    for (rx, ry, rw, rh) in cur_regions:
                        cover[ry:ry + rh, rx:rx + rw] = True
                    # Coverage AND tightness are both judged
                    # against the alpha >= REGION_ALPHA
                    # silhouette the region definition uses
                    # (FORMAT.md 5b): sub-threshold fringe is
                    # intentionally outside the window.
                    sil = (buf >> 24) >= REGION_ALPHA
                    if (sil & ~cover).any():
                        region_bad += 1
                        ok = False
                    sil_area = int(sil.sum())
                    if sil_area:
                        ratio = int(cover.sum()) / sil_area
                        tight_sum += ratio
                        tight_n += 1
                        tight_max = max(tight_max, ratio)
                    rect_sum += len(cur_regions)
                    rect_n += 1
                    rect_max = max(rect_max, len(cur_regions))
                if ver == 3 and xlen and flags & 16:
                    # RGB565 profile, sparse form: decode this
                    # pose's record into the simulated CE buffer
                    # and demand EXACT equality with the 565
                    # quantization of the source frame - the
                    # encoding is lossless in 565 space by
                    # construction, so any mismatch is a bug.
                    kind565 = struct.unpack_from('<H', data,
                                                 xptr)[0]
                    xptr += 2
                    tgt = to565(pms[i])
                    if args.rgb565_bleed and rlen and \
                            cur_regions is not None:
                        tgt, _ = bleed565(
                            tgt, (pms[i] >> 24) & 0xFF,
                            cur_regions)
                    if cbuf565 is None:
                        cbuf565 = np.zeros(tgt.shape,
                                           dtype=np.uint16)
                    if kind565 == KIND_FULL:
                        (xw, xh, xbx, xby) = struct.unpack_from(
                            '<HHhh', data, xptr)
                        xptr += 8
                        vals = np.frombuffer(
                            data, dtype='<u2', count=xw * xh,
                            offset=xptr)
                        xptr += 2 * xw * xh
                        cbuf565[:, :] = 0
                        cbuf565[xby:xby + xh,
                                xbx:xbx + xw] = \
                            vals.reshape(xh, xw)
                    elif kind565 == KIND_SPARSE:
                        plen = struct.unpack_from('<I', data,
                                                  xptr)[0]
                        xptr += 4
                        endp = xptr + plen
                        pos = 0
                        flat = cbuf565.ravel()
                        while True:
                            skip, ln = struct.unpack_from(
                                '<HH', data, xptr)
                            xptr += 4
                            if ln == 0 and skip == 0:
                                break
                            pos += skip
                            if ln:
                                vals = np.frombuffer(
                                    data, dtype='<u2',
                                    count=ln, offset=xptr)
                                flat[pos:pos + ln] = vals
                                xptr += 2 * ln
                                pos += ln
                        if xptr != endp:
                            print('size %d clip %s frame %d: '
                                  'rgb565 sparse payload %d != '
                                  'recorded %d' %
                                  (h, clip['name'], i,
                                   xptr - (endp - plen), plen))
                            ok = False
                    if args.rgb565_tol > 0:
                        dev = int(chan_max_diff(
                            expand565(cbuf565),
                            expand565(tgt)).max())
                        s565_worst = max(s565_worst, dev)
                        if dev > args.rgb565_tol:
                            print('size %d clip %s frame %d: '
                                  'rgb565 sparse deviation %d '
                                  '> tol %d' %
                                  (h, clip['name'], i, dev,
                                   args.rgb565_tol))
                            s565_bad += 1
                            ok = False
                    else:
                        mism = int((cbuf565 != tgt).sum())
                        if mism:
                            print('size %d clip %s frame %d: '
                                  'rgb565 sparse mismatch %d px' %
                                  (h, clip['name'], i, mism))
                            s565_bad += 1
                            ok = False
                elif ver == 3 and xlen:
                    # RGB565 profile: decode this pose's frame
                    # and compare against the composed buffer
                    # (straight-alpha), bound = 565 quantization.
                    (xx, xy, xw, xh, xbx, xby) = \
                        struct.unpack_from('<HHHHhh', data, xptr)
                    xptr += 12
                    vals = np.frombuffer(data, dtype='<u2',
                                         count=xw * xh,
                                         offset=xptr)
                    xptr += 2 * xw * xh
                    v = vals.astype(np.uint32)
                    pr = ((v >> 11) & 31) * 255 // 31
                    pg = ((v >> 5) & 63) * 255 // 63
                    pb = (v & 31) * 255 // 31
                    sub = buf[xby:xby + xh, xbx:xbx + xw]
                    sa = np.maximum(
                        ((sub >> 24) & 0xFF).astype(np.int32), 1)
                    sr_ = np.minimum(255, ((sub >> 16) & 0xFF)
                                     .astype(np.int32) * 255 // sa)
                    sg_ = np.minimum(255, ((sub >> 8) & 0xFF)
                                     .astype(np.int32) * 255 // sa)
                    sb_ = np.minimum(255, (sub & 0xFF)
                                     .astype(np.int32) * 255 // sa)
                    err = max(
                        int(np.abs(pr.reshape(xh, xw) -
                                   sr_).max()),
                        int(np.abs(pg.reshape(xh, xw) -
                                   sg_).max()),
                        int(np.abs(pb.reshape(xh, xw) -
                                   sb_).max()))
                    x565_worst = max(x565_worst, err)
                    if err > 9:
                        print('size %d clip %s frame %d: rgb565 '
                              'deviation %d > 9' %
                              (h, clip['name'], i, err))
                        ok = False
                gpose += 1
        print('size %d: %d poses, worst channel deviation %d '
              '(tol %d), mean blue-channel deviation %.3f' %
              (h, gpose, worst, args.tol,
               dev_sum / max(1, dev_n)))
        if ver == 3:
            print('size %d: region coverage failures %d, '
                  'rgb565 worst deviation %d (bound 9), '
                  'region stream consumed %d/%d B, '
                  'rgb565 stream consumed %d/%d B' %
                  (h, region_bad, x565_worst, rptr - roff, rlen,
                   xptr - xoff, xlen))
            if tight_n:
                tight_mean = tight_sum / tight_n
                print('size %d: region tightness mean %.3f '
                      'max %.3f (bound: mean <= %.2f)' %
                      (h, tight_mean, tight_max, REGION_TIGHT_MAX))
                if tight_mean > REGION_TIGHT_MAX:
                    print('size %d: region tightness mean %.3f '
                          'exceeds bound %.2f' %
                          (h, tight_mean, REGION_TIGHT_MAX))
                    ok = False
            if rect_n:
                print('size %d: region rects mean %.1f max %d '
                      '(bound: max <= %s)' %
                      (h, rect_sum / rect_n, rect_max,
                       args.region_max_rects
                       if args.region_max_rects > 0 else 'inf'))
                if args.region_max_rects > 0 and \
                        rect_max > args.region_max_rects:
                    print('size %d: region rect max %d exceeds '
                          'cap %d' %
                          (h, rect_max, args.region_max_rects))
                    ok = False
            if flags & 16:
                if args.rgb565_tol > 0:
                    print('size %d: rgb565 sparse failures %d '
                          'poses, worst deviation %d '
                          '(tol %d)' %
                          (h, s565_bad, s565_worst,
                           args.rgb565_tol))
                else:
                    print('size %d: rgb565 sparse exact-equality '
                          'failures %d poses (bound 0)' %
                          (h, s565_bad))
            if rptr - roff != rlen or xptr - xoff != xlen:
                ok = False
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames', nargs='?')
    ap.add_argument('-o', '--output')
    ap.add_argument('--sizes', default='248,320',
                    type=lambda s: [int(x) for x in s.split(',')])
    ap.add_argument('--format', choices=('v1', 'v2', 'v3'),
                    default='v2')
    ap.add_argument('--rgb565', action='store_true',
                    help='v3: also bake the WinCE RGB565 profile '
                         '(full-frame form)')
    ap.add_argument('--rgb565-sparse', action='store_true',
                    help='v3: bake the WinCE RGB565 profile in '
                         'sparse-delta form instead of full '
                         'frames (header flag bit4)')
    ap.add_argument('--rgb565-tol', type=int, default=0,
                    help='sparse RGB565 mask tolerance in '
                         'expanded straight units (0 = exact '
                         '565 match). A bake parameter like '
                         '--tol; verify needs the same value.')
    ap.add_argument('--rgb565-bleed', action='store_true',
                    help='sparse RGB565: bleed the nearest '
                         'silhouette colour into region-covered '
                         'sub-silhouette pixels (edge fringe '
                         'fix, p61). A bake parameter like '
                         '--tol; verify needs the same flag.')
    ap.add_argument('--keyint', type=int, default=45)
    ap.add_argument('--tol', type=int, default=8)
    ap.add_argument('--full-frac', type=float, default=0.6)
    ap.add_argument('--drop-thresh', type=float, default=0.006)
    ap.add_argument('--max-gap', type=int, default=83)
    ap.add_argument('--region-max-rects', type=int,
                    default=REGION_RECT_MAX,
                    help='v3: max window-region rects per pose '
                         '(0 = no cap). A bake parameter like '
                         '--tol; verify needs the same value.')
    ap.add_argument('--verify')
    args = ap.parse_args()
    if args.rgb565 and args.rgb565_sparse:
        ap.error('--rgb565 and --rgb565-sparse are exclusive')
    if args.verify:
        if not args.frames:
            ap.error('--verify needs the frames dir too')
        return verify(args)
    if not args.frames or not args.output:
        ap.error('FRAMES_DIR and -o are required')
    return convert(args)


if __name__ == '__main__':
    sys.exit(main())
