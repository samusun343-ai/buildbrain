#!/usr/bin/env python3
# convert_shimeji.py - Shimeji pack -> Brainux desktop mascot pack (.bdm)
#
# See FORMAT.md in this directory for the .bdm specification and
# the mapping decisions (what is kept, flattened or dropped).
#
# Everything device-specific happens HERE, at convert time:
#   - frames are trimmed (set-wide union crop, then per-frame),
#   - downscaled with Lanczos to each baked display height,
#   - packed into one atlas per size,
#   - converted to premultiplied ARGB8888 (the toolkit canvas
#     packing, 0xAARRGGBB) so the runtime blitter is a single
#     multiply per channel with no decode and no scaling,
#   - actions/behaviors XML is flattened into binary clip and
#     pool tables (no XML parser on the device).
#
# Usage:
#   convert_shimeji.py PACK_DIR -o OUT.bdm [--name NAME]
#                      [--sizes 168,208,248] [--char NAME]
#                      [--report REPORT.md]
#   convert_shimeji.py --verify OUT.bdm
#
# PACK_DIR layouts accepted:
#   - a Shimeji-ee install dir (img/<Char>/shime*.png + conf/)
#   - a character dir itself (shime*.png [+ conf/])
#   - a pack dir with img/<Char>/ and conf/ at the top
# Local validation only: converted packs embed the source art;
# do not redistribute them (see FORMAT.md, licensing note).

import argparse
import json
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

from PIL import Image

TICK_MS = 40  # Shimeji-ee Manager.TICK_INTERVAL (verified in source)

CLIP_STAY, CLIP_MOVE, CLIP_ANIMATE, CLIP_SEQ = 0, 1, 2, 3
BORDER_NONE, BORDER_FLOOR, BORDER_WALL, BORDER_CEILING = 0, 1, 2, 3


def strip_ns(tag):
    return tag.rsplit('}', 1)[-1]


# The original Kilkakon schema also ships in a Japanese
# localization (many distributed packs, including the common
# Hatsune Miku set, use it): element/attribute names and the
# Type/BorderType enumerations are Japanese. Normalize such a
# tree to the English names once, at parse time.
JA_TAGS = {
    'マスコット': 'Mascot', '動作リスト': 'ActionList',
    '動作': 'Action', 'アニメーション': 'Animation',
    'ポーズ': 'Pose', '動作参照': 'ActionReference',
    '行動リスト': 'BehaviorList', '行動': 'Behavior',
    '条件': 'Condition', '次の行動リスト': 'NextBehaviorList',
    '行動参照': 'BehaviorReference',
}
JA_ATTRS = {
    '名前': 'Name', '種類': 'Type', '枠': 'BorderType',
    'クラス': 'Class', '条件': 'Condition', '画像': 'Image',
    '基準座標': 'ImageAnchor', '移動速度': 'Velocity',
    '長さ': 'Duration', '頻度': 'Frequency', '追加': 'Add',
    '繰り返し': 'Loop', '非表示': 'Hidden',
}
JA_TYPES = {
    '静止': 'Stay', '移動': 'Move', '固定': 'Animate',
    '複合': 'Sequence', '選択': 'Select', '組み込み': 'Embedded',
}
JA_BORDERS = {'地面': 'Floor', '壁': 'Wall', '天井': 'Ceiling',
              'なし': 'None'}


def normalize_tree(el):
    tag = strip_ns(el.tag)
    el.tag = JA_TAGS.get(tag, tag)
    for k in list(el.attrib):
        nk = JA_ATTRS.get(k, k)
        if nk != k:
            el.attrib[nk] = el.attrib.pop(k)
    if el.get('Type') in JA_TYPES:
        el.set('Type', JA_TYPES[el.get('Type')])
    if el.get('BorderType') in JA_BORDERS:
        el.set('BorderType', JA_BORDERS[el.get('BorderType')])
    for child in el:
        normalize_tree(child)


def read_xml(path):
    raw = open(path, 'rb').read()
    for enc in ('utf-8-sig', 'shift_jis'):
        try:
            text = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    else:
        raise ValueError('cannot decode %s as utf-8/shift_jis' % path)
    # ElementTree rejects the encoding declaration after manual
    # decode; drop the prolog.
    text = re.sub(r'<\?xml[^>]*\?>', '', text, count=1)
    root = ET.fromstring(text)
    normalize_tree(root)
    return root


def parse_pair(s, default=(0, 0)):
    if not s:
        return default
    parts = s.split(',')
    try:
        return (int(float(parts[0])), int(float(parts[1])))
    except (ValueError, IndexError):
        return default


def parse_duration_expr(expr):
    """ActionReference Duration: milliseconds, possibly an
    expression like ${500+Math.random()*1000}. Returns
    (min_ms, max_ms) or None when absent/unparseable."""
    if not expr:
        return None
    e = expr.strip()
    if e.startswith('${') and e.endswith('}'):
        e = e[2:-1]
    rand = 0.0
    m = re.search(r'Math\.random\(\)\s*\*\s*([0-9.]+)', e)
    if m:
        rand = float(m.group(1))
        e = e[:m.start()] + '0' + e[m.end():]
    nums = re.findall(r'[0-9.]+', e)
    if not nums and not rand:
        return None
    base = sum(float(n) for n in nums)
    lo = int(base)
    hi = int(base + rand)
    return (lo, max(lo, hi))


class Pose:
    __slots__ = ('image', 'ax', 'ay', 'vx', 'vy', 'dur_ms')

    def __init__(self, image, ax, ay, vx, vy, dur_ms):
        self.image, self.ax, self.ay = image, ax, ay
        self.vx, self.vy, self.dur_ms = vx, vy, dur_ms


class RawAction:
    """One <Action> from actions.xml, unflattened."""
    def __init__(self, name, atype, border, loop):
        self.name, self.type, self.border, self.loop = name, atype, border, loop
        self.poses = []      # pose actions: list[Pose]
        self.items = []      # sequence/select: list of dicts
        self.cond = None     # Condition attr (anonymous actions)


def parse_actions(root):
    actions = {}
    order = []
    for el in root.iter():
        if strip_ns(el.tag) != 'Action':
            continue
        name = el.get('Name')
        if not name:
            continue  # anonymous nested action: handled via parent
        act = RawAction(name, el.get('Type', 'Stay'),
                        el.get('BorderType'), el.get('Loop'))
        act.cond = el.get('Condition')
        _fill_action(el, act, actions)
        actions[name] = act
        order.append(name)
    return actions, order


def _fill_action(el, act, actions):
    animations = []
    for child in el:
        tag = strip_ns(child.tag)
        if tag == 'Animation':
            animations.append(child)
        elif tag == 'ActionReference':
            act.items.append({'ref': child.get('Name'),
                              'cond': child.get('Condition'),
                              'dur': parse_duration_expr(
                                  child.get('Duration'))})
        elif tag == 'Action':
            sub = RawAction(None, child.get('Type', 'Select'),
                            child.get('BorderType'), child.get('Loop'))
            sub.cond = child.get('Condition')
            _fill_action(child, sub, actions)
            act.items.append({'action': sub, 'dur': None})
    # Prefer the first Animation without a Condition (conditional
    # variants test cursor/environment state we do not model);
    # fall back to the first when all are conditional.
    chosen = None
    for a in animations:
        if not a.get('Condition'):
            chosen = a
            break
    if chosen is None and animations:
        chosen = animations[0]
    if chosen is not None:
        for p in chosen:
            if strip_ns(p.tag) != 'Pose':
                continue
            img = (p.get('Image') or '').lstrip('/')
            ax, ay = parse_pair(p.get('ImageAnchor'))
            vx, vy = parse_pair(p.get('Velocity'))
            dur = int(p.get('Duration') or 1) * TICK_MS
            act.poses.append(Pose(img, ax, ay, vx, vy, dur))


def flatten(act, actions, depth, seen):
    """Flatten an action to a clip: list of segments, each segment
    a list of Poses plus an optional (min_ms, max_ms) total-time
    override from an ActionReference Duration. Returns
    (segments, root_type, border) or None when nothing pose-bearing
    is reachable (Embedded-only actions land here)."""
    if act.poses:
        # Stay / Move / Animate - and Embedded actions that still
        # carry poses in the XML (Pinched, Falling: the Java class
        # adds physics, which our runtime reimplements; the poses
        # themselves are data). Embedded without poses (Look,
        # Offset, Resisting) has nothing to bake.
        return ([(list(act.poses), None)],
                act.type if act.type in ('Stay', 'Move', 'Animate')
                else 'Animate', act.border)
    if act.type in ('Stay', 'Move', 'Animate', 'Embedded'):
        return None
    # Sequence / Select: walk items in order. Select keeps the
    # first branch that yields poses (deterministic; the original
    # picks by runtime conditions we do not model).
    segs = []
    rtype, rborder = None, act.border
    for item in act.items:
        sub = None
        if 'ref' in item and item['ref']:
            if item['ref'] in seen or depth > 8:
                continue
            if item.get('cond') and \
                    classify_condition(item['cond']) == 'skip':
                continue
            target = actions.get(item['ref'])
            if target is None:
                continue
            sub = flatten(target, actions, depth + 1,
                          seen | {item['ref']})
        elif 'action' in item:
            # A branch gated on a surface we do not model
            # (window edges, walls, ceiling) is skipped; for a
            # Select the next branch gets its chance.
            if item['action'].cond and \
                    classify_condition(item['action'].cond) == 'skip':
                continue
            sub = flatten(item['action'], actions, depth + 1, seen)
        if not sub:
            continue
        sub_segs, st, sb = sub
        if rtype is None:
            rtype, rborder = st, sb or rborder
        if item.get('dur') and len(sub_segs) == 1:
            poses, _ = sub_segs[0]
            segs.append((poses, item['dur']))
        else:
            segs.extend(sub_segs)
        if act.type == 'Select':
            break
    if not segs:
        return None
    return (segs, rtype or 'Animate', rborder)


BORDER_MAP = {None: BORDER_NONE, 'Floor': BORDER_FLOOR,
              'Wall': BORDER_WALL, 'Ceiling': BORDER_CEILING}
TYPE_MAP = {'Stay': CLIP_STAY, 'Move': CLIP_MOVE,
            'Animate': CLIP_ANIMATE}


class Clip:
    def __init__(self, name, ctype, border, loop):
        self.name, self.type, self.border, self.loop = name, ctype, border, loop
        self.poses = []        # list[Pose] in source coordinates
        self.total_min = 0
        self.total_max = 0


def build_clips(actions, order):
    clips = []
    by_name = {}
    dropped = []
    for name in order:
        act = actions[name]
        if act.cond and classify_condition(act.cond) == 'skip':
            dropped.append(name)  # gated on an unmodelled surface
            continue
        flat = flatten(act, actions, 0, {name})
        if not flat:
            dropped.append(name)
            continue
        segs, rtype, rborder = flat
        clip = Clip(name, TYPE_MAP.get(rtype, CLIP_ANIMATE),
                    BORDER_MAP.get(rborder, BORDER_NONE),
                    act.loop != 'false')
        tmin = tmax = 0
        for poses, override in segs:
            nat = sum(p.dur_ms for p in poses)
            if override:
                lo, hi = override
                scale = (lo / nat) if nat else 1.0
                for p in poses:
                    p = Pose(p.image, p.ax, p.ay, p.vx, p.vy,
                             max(1, int(round(p.dur_ms * scale))))
                    clip.poses.append(p)
                tmin += lo
                tmax += hi
            else:
                clip.poses.extend(poses)
                tmin += nat
                tmax += nat
        clip.total_min, clip.total_max = min(tmin, 65535), min(tmax, 65535)
        by_name[name] = len(clips)
        clips.append(clip)
    return clips, by_name, dropped


def classify_condition(cond):
    """Classify a Condition expression by the surface it gates
    on: 'floor', 'skip' (a surface we do not model: walls,
    ceiling, window/IE borders), or None (unrelated, e.g. cursor
    tests - inherit the enclosing context)."""
    if 'floor.isOn' in cond or 'workArea.bottomBorder.isOn' in cond:
        return 'floor'
    for token in ('activeie', 'topborder', 'leftborder',
                  'rightborder', 'ceiling', 'wall'):
        if token in cond.lower():
            return 'skip'
    return None


def parse_behaviors(root):
    """name -> (frequency, hidden, [next refs (name, freq)],
    surface) where surface is 'floor', 'skip' or None (no gating
    condition). The NEAREST gating condition wins."""
    out = {}

    def walk(el, surface):
        for child in el:
            tag = strip_ns(child.tag)
            if tag == 'Behavior':
                name = child.get('Name')
                if not name:
                    continue
                freq = int(child.get('Frequency') or 0)
                hidden = (child.get('Hidden') or '') == 'true'
                nexts = []
                for sub in child.iter():
                    if strip_ns(sub.tag) == 'BehaviorReference':
                        nexts.append((sub.get('Name'),
                                      int(sub.get('Frequency') or 0)))
                # A Behavior may carry its own Condition attr;
                # it is more specific than any enclosing one.
                own = classify_condition(child.get('Condition') or '')
                out[name] = (freq, hidden, nexts,
                             own if own is not None else surface)
            elif tag == 'Condition':
                cls = classify_condition(child.get('Condition') or '')
                walk(child, cls if cls is not None else surface)
            else:
                walk(child, surface)
    walk(root, None)
    return out


def build_pool(behaviors, clips_by_name, clips):
    """Weighted floor pool: (clip_index, weight) entries."""
    pool = {}

    def add(clip_idx, w):
        if w > 0:
            pool[clip_idx] = pool.get(clip_idx, 0) + w

    if behaviors:
        # Hidden only hides a behavior from Shimeji-ee's menus;
        # the engine still selects hidden behaviors (the standard
        # walking behaviors are Hidden="true"), so it plays no
        # role here. Frequency 0 = reached only via references.
        for name, (freq, hidden, nexts, surface) in behaviors.items():
            if surface == 'skip':
                continue
            idx = clips_by_name.get(name)
            if idx is None:
                continue
            if clips[idx].border not in (BORDER_FLOOR, BORDER_NONE):
                continue
            add(idx, freq)
            for nname, nfreq in nexts:
                # A chain target gated on a surface we do not
                # model (e.g. SitOnTheRightEdgeOfIE) stays out
                # even when a floor behavior references it.
                tgt = behaviors.get(nname)
                if tgt is not None and tgt[3] == 'skip':
                    continue
                nidx = clips_by_name.get(nname)
                if nidx is not None and \
                        clips[nidx].border in (BORDER_FLOOR, BORDER_NONE):
                    add(nidx, nfreq)
    if not pool:
        # Fallback when behaviors.xml is missing or nothing
        # matched: all floor clips with type-based weights.
        for i, c in enumerate(clips):
            if c.border in (BORDER_FLOOR, BORDER_NONE):
                add(i, {CLIP_STAY: 100, CLIP_MOVE: 60,
                        CLIP_ANIMATE: 30}[c.type])
    return sorted(pool.items())[:64]


# ---------------- image baking ----------------

def load_frames(clips, imgdir):
    """Load every referenced image once. Returns (images, report)
    with images: name -> PIL Image (RGBA, source canvas)."""
    wanted = []
    seen = set()
    for c in clips:
        for p in c.poses:
            if p.image not in seen:
                seen.add(p.image)
                wanted.append(p.image)
    images = {}
    missing = []
    for name in wanted:
        path = os.path.join(imgdir, name)
        if not os.path.isfile(path):
            # case-insensitive fallback (packs in the wild vary)
            low = name.lower()
            hit = None
            for f in os.listdir(imgdir):
                if f.lower() == low:
                    hit = os.path.join(imgdir, f)
                    break
            if hit is None:
                missing.append(name)
                continue
            path = hit
        images[name] = Image.open(path).convert('RGBA')
    return images, missing


def union_crop(images):
    x0 = y0 = None
    x1 = y1 = 0
    for im in images.values():
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


def bake_size(images, crop, height):
    """Bake one display height. Returns (atlas_bytes, atlas_w,
    atlas_h, frames, scale) where frames maps image name ->
    (x, y, w, h, bearing_x, bearing_y) in atlas coordinates."""
    cx0, cy0, cx1, cy1 = crop
    crop_w, crop_h = cx1 - cx0, cy1 - cy0
    scale = height / crop_h
    # Per-frame: crop to union box, trim transparent margins
    # (bearing keeps the offset), Lanczos-downscale, premultiply.
    baked = {}
    for name, im in images.items():
        sub = im.crop((cx0, cy0, cx1, cy1))
        bb = sub.getchannel('A').getbbox()
        if bb is None:
            bb = (0, 0, 1, 1)
        sub = sub.crop(bb)
        nw = max(1, int(round(sub.width * scale)))
        nh = max(1, int(round(sub.height * scale)))
        if (nw, nh) != sub.size:
            sub = sub.resize((nw, nh), Image.LANCZOS)
        baked[name] = (sub, int(round(bb[0] * scale)),
                       int(round(bb[1] * scale)))
    # Dedupe pixel-identical baked frames (shimeji sets reuse
    # poses heavily); duplicates share one atlas rect.
    uniq = {}
    alias = {}
    for name, (im, bx, by) in baked.items():
        key = (im.size, im.tobytes())
        if key in uniq:
            alias[name] = uniq[key]
        else:
            uniq[key] = name
    # Shelf packing, tallest first, atlas width 1024 (or the
    # widest frame when larger).
    order = sorted(((n, baked[n]) for n in uniq.values()),
                   key=lambda kv: -kv[1][0].height)
    atlas_w = max(1024, max(im.width for im, _, _ in baked.values()))
    frames = {}
    x = y = row_h = 0
    for name, (im, bx, by) in order:
        if x + im.width > atlas_w:
            x = 0
            y += row_h
            row_h = 0
        frames[name] = (x, y, im.width, im.height, bx, by, im)
        x += im.width
        row_h = max(row_h, im.height)
    atlas_h = y + row_h
    atlas = Image.new('RGBA', (atlas_w, atlas_h), (0, 0, 0, 0))
    for name, (fx, fy, fw, fh, bx, by, im) in frames.items():
        atlas.paste(im, (fx, fy))
    for name, src in alias.items():
        fx, fy, fw, fh, _, _, _ = frames[src]
        _, bx, by = baked[name]
        frames[name] = (fx, fy, fw, fh, bx, by, baked[name][0])
    # Premultiply into 0xAARRGGBB uint32 LE (canvas packing).
    px = atlas.load()
    buf = bytearray(atlas_w * atlas_h * 4)
    i = 0
    for yy in range(atlas_h):
        for xx in range(atlas_w):
            r, g, b, a = px[xx, yy]
            if a == 0:
                i += 4
                continue
            if a != 255:
                r = r * a // 255
                g = g * a // 255
                b = b * a // 255
            struct.pack_into('<I', buf, i,
                             (a << 24) | (r << 16) | (g << 8) | b)
            i += 4
    frames = {k: v[:6] for k, v in frames.items()}
    return bytes(buf), atlas_w, atlas_h, frames, scale


# ---------------- binary writer ----------------

def write_bdm(path, name, clips, pool, specials, sizes_data, crop):
    """sizes_data: list of (height, atlas_bytes, atlas_w, atlas_h,
    frames{name->rect}, scale, frame_order)"""
    strtab = bytearray(b'\0')
    name_offs = []
    for c in clips:
        name_offs.append(len(strtab))
        strtab += c.name.encode('utf-8') + b'\0'

    n_sizes = len(sizes_data)
    header_size = 80
    clipmeta_size = 16 * len(clips)
    pool_size = 4 * len(pool)
    sizerec_size = 24 * n_sizes

    # Layout: header | strtab | clipmeta | pool | size records |
    # then per size: frames table, poses table, atlas.
    off = header_size
    strtab_off = off
    off += len(strtab)
    clips_off = off
    off += clipmeta_size
    pool_off = off
    off += pool_size
    sizes_off = off
    off += sizerec_size

    size_blobs = []
    for (height, atlas_bytes, aw, ah, frames, scale,
         frame_order, poses_blob) in sizes_data:
        frames_off = off
        off += 12 * len(frame_order)
        poses_off = off
        off += len(poses_blob)
        atlas_off = off
        off += len(atlas_bytes)
        size_blobs.append((height, aw, ah, len(frame_order),
                           frames_off, poses_off, atlas_off,
                           int(round(scale * 256)), atlas_bytes,
                           frames, frame_order))

    total = off
    out = bytearray(total)
    # Header (80 bytes; layout is FORMAT.md section 2):
    #   0 magic, 4 u16 ver, 6 u16 flags, 8 u32 total,
    #   12 name[24], 36 u16 crop_w, 38 u16 crop_h,
    #   40 u16 clips, 42 u16 pool, 44 u16 sizes, 46 u16 rsvd,
    #   48 i16 fall, 50 i16 drag, 52 i16 thrown, 54 i16 stand,
    #   56 u32 clips_off, 60 u32 pool_off, 64 u32 sizes_off,
    #   68 u32 strtab_off, 72..79 reserved
    struct.pack_into('<4sHHI', out, 0, b'BDM1', 1, 1 if pool else 0,
                     total)
    nb = name.encode('utf-8')[:23]
    out[12:12 + len(nb)] = nb
    struct.pack_into('<HHHHHH', out, 36, crop[2] - crop[0],
                     crop[3] - crop[1], len(clips), len(pool),
                     n_sizes, 0)
    struct.pack_into('<hhhh', out, 48, *specials)
    struct.pack_into('<IIII', out, 56, clips_off, pool_off,
                     sizes_off, strtab_off)
    out[strtab_off:strtab_off + len(strtab)] = strtab

    # clip metadata: u16 name_off, u16 pose_count,
    # u16 total_min_ms, u16 total_max_ms, u32 pose_start,
    # u8 type, u8 border, u8 flags(loop), u8 reserved
    pose_start = 0
    for i, c in enumerate(clips):
        struct.pack_into('<HHHHIBBBB', out, clips_off + 16 * i,
                         name_offs[i], len(c.poses), c.total_min,
                         c.total_max, pose_start, c.type, c.border,
                         1 if c.loop else 0, 0)
        pose_start += len(c.poses)
    for i, (ci, w) in enumerate(pool):
        struct.pack_into('<HH', out, pool_off + 4 * i, ci, w)

    for si, (height, aw, ah, nf, frames_off, poses_off, atlas_off,
             scale_q8, atlas_bytes, frames, frame_order) in \
            enumerate(size_blobs):
        struct.pack_into('<HHHHIIIH', out, sizes_off + 24 * si,
                         height, aw, ah, nf, frames_off, poses_off,
                         atlas_off, scale_q8)
        for fi, img_name in enumerate(frame_order):
            fx, fy, fw, fh, bx, by = frames[img_name]
            struct.pack_into('<HHHHhh', out, frames_off + 12 * fi,
                             fx, fy, fw, fh, bx, by)
        out[poses_off:poses_off + len(sizes_data[si][7])] = \
            sizes_data[si][7]
        out[atlas_off:atlas_off + len(atlas_bytes)] = atlas_bytes

    with open(path, 'wb') as f:
        f.write(out)
    return total


def build_poses_blob(clips, frame_index, scale):
    """Per-size pose table: for every clip in order, for every
    pose: u16 frame, u16 dur_ms, i16 ax, i16 ay (scaled),
    i16 vx_q8, i16 vy_q8 (source px/tick * scale * 256)."""
    buf = bytearray()
    for c in clips:
        for p in c.poses:
            buf += struct.pack(
                '<HHhhhh', frame_index[p.image], p.dur_ms,
                int(round(p.ax * scale)), int(round(p.ay * scale)),
                int(round(p.vx * scale * 256)),
                int(round(p.vy * scale * 256)))
    return bytes(buf)


# ---------------- discovery ----------------

def find_char_dir(pack, want_char):
    cands = []
    img_root = os.path.join(pack, 'img')
    if os.path.isdir(img_root):
        for d in sorted(os.listdir(img_root)):
            dd = os.path.join(img_root, d)
            if os.path.isdir(dd) and \
                    os.path.isfile(os.path.join(dd, 'shime1.png')):
                cands.append((d, dd))
    if os.path.isfile(os.path.join(pack, 'shime1.png')):
        cands.append((os.path.basename(os.path.abspath(pack)), pack))
    if not cands:
        raise ValueError('no character dir with shime1.png under %s'
                         % pack)
    if want_char:
        for name, dd in cands:
            if name == want_char:
                return name, dd
        raise ValueError('character %r not found (have: %s)'
                         % (want_char, [c[0] for c in cands]))
    return cands[0]


def find_conf(pack, char_name, char_dir):
    def ci(directory, wanted):
        """Case-insensitive lookup: packs ship Actions.xml as
        often as actions.xml."""
        try:
            entries = os.listdir(directory)
        except OSError:
            return None
        for e in entries:
            if e.lower() == wanted:
                return os.path.join(directory, e)
        return None

    for base in (os.path.join(char_dir, 'conf'),
                 os.path.join(pack, 'conf', char_name),
                 os.path.join(pack, 'conf')):
        a = ci(base, 'actions.xml')
        if a:
            return a, ci(base, 'behaviors.xml') or \
                ci(base, 'behavior.xml')
    return None, None


def convert(args):
    char_name, char_dir = find_char_dir(args.pack, args.char)
    actions_path, behaviors_path = find_conf(args.pack, char_name,
                                             char_dir)
    if not actions_path:
        raise ValueError('actions.xml not found for %s' % char_name)
    actions, order = parse_actions(read_xml(actions_path))
    clips, by_name, dropped = build_clips(actions, order)
    behaviors = parse_behaviors(read_xml(behaviors_path)) \
        if behaviors_path else {}
    pool = build_pool(behaviors, by_name, clips)
    images, missing = load_frames(clips, char_dir)
    if missing:
        print('warning: missing images skipped: %s' % missing,
              file=sys.stderr)
        # drop poses referencing missing images
        for c in clips:
            c.poses = [p for p in c.poses if p.image in images]
        clips = [c for c in clips if c.poses]
        by_name = {c.name: i for i, c in enumerate(clips)}
        pool = [(i, w) for (i, w) in pool if i < len(clips)]
    crop = union_crop(images)
    frame_order = sorted(images.keys(),
                         key=lambda n: (len(n), n))
    frame_index = {n: i for i, n in enumerate(frame_order)}

    sizes_data = []
    for height in args.sizes:
        atlas_bytes, aw, ah, frames, scale = bake_size(
            images, crop, height)
        poses_blob = build_poses_blob(clips, frame_index, scale)
        sizes_data.append((height, atlas_bytes, aw, ah, frames,
                           scale, frame_order, poses_blob))

    def special(*names):
        for n in names:
            if n in by_name:
                return by_name[n]
        return -1

    specials = (special('Fall', 'Falling', '落下する'),
                special('Dragged', 'Pinched', 'ドラッグされる'),
                special('Thrown', '投げられる'),
                special('Stand', '立つ'))
    name = args.name or char_name
    total = write_bdm(args.output, name, clips, pool, specials,
                      sizes_data, crop)
    report = {
        'character': name,
        'source_dir': char_dir,
        'actions_total': len(order),
        'clips_baked': len(clips),
        'actions_dropped': dropped,
        'pool_entries': len(pool),
        'specials': {'fall': specials[0], 'drag': specials[1],
                     'thrown': specials[2], 'stand': specials[3]},
        'frames': len(frame_order),
        'crop': crop,
        'sizes': [{'height': s[0], 'atlas': [s[2], s[3]],
                   'atlas_bytes': len(s[1])} for s in sizes_data],
        'output': args.output,
        'output_bytes': total,
    }
    print(json.dumps(report, indent=1, ensure_ascii=False))
    if args.report:
        with open(args.report, 'w', encoding='utf-8') as f:
            json.dump(report, f, indent=1, ensure_ascii=False)
    return 0


def verify(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'BDM1', 'bad magic'
    ver, flags, total = struct.unpack_from('<HHI', data, 4)
    name = data[12:36].split(b'\0')[0].decode('utf-8')
    cw, ch, nclips, npool, nsizes, _ = struct.unpack_from(
        '<HHHHHH', data, 36)
    specials = struct.unpack_from('<hhhh', data, 48)
    clips_off, pool_off, sizes_off, strtab_off = struct.unpack_from(
        '<IIII', data, 56)
    print('%s: version %d flags %d total %d (file %d)' %
          (path, ver, flags, total, len(data)))
    print('name=%r source canvas %dx%d clips=%d pool=%d sizes=%d'
          % (name, cw, ch, nclips, npool, nsizes))
    print('specials fall/drag/thrown/stand = %s' % (specials,))
    for si in range(nsizes):
        h, aw, ah, nf, fo, po, ao, sq8 = struct.unpack_from(
            '<HHHHIIIH', data, sizes_off + 24 * si)
        print('size %d: height %d atlas %dx%d frames %d scale_q8 %d'
              % (si, h, aw, ah, nf, sq8))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pack', nargs='?')
    ap.add_argument('-o', '--output')
    ap.add_argument('--name')
    ap.add_argument('--char')
    ap.add_argument('--sizes', default='168,208,248',
                    type=lambda s: [int(x) for x in s.split(',')])
    ap.add_argument('--report')
    ap.add_argument('--verify')
    args = ap.parse_args()
    if args.verify:
        return verify(args.verify)
    if not args.pack or not args.output:
        ap.error('PACK and -o are required')
    return convert(args)


if __name__ == '__main__':
    sys.exit(main())
