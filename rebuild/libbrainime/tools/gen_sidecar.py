#!/usr/bin/env python3
"""Generate the libbrainime M2 sidecar (.bisc) for a BIDF v2 dictionary.

WHY: BIDF v2 tokens carry only (surface, BIDF class, cost byte).
convert_mozc.py dropped mozc's lid/rid, so the shipped dictionary
has no lexicalised connection information at all (its 256x256 class
connection section is all zero).  This tool re-derives, for every
BIDF token in BIDF token order, the mozc (lid, rid) of the source
entry, and packages mozc's real connection matrix
(connection_single_column.txt, 2672x2672, row-major
index = rid_of_left * 2672 + lid_of_right — layout confirmed against
mozc's src/data_manager/connection_file_reader.h) into a compact
sidecar the engine mmaps next to the dictionary.

Matching: a BIDF token (reading, surface, cls, cost_byte) is matched
to the mozc source entry that produced it by replaying
convert_mozc.py's dedup rule: among raw mozc entries with the same
reading and surface whose lid classifies (classify() below, copied
from buildbrain tools/ime/convert_mozc.py) to the token's BIDF
class, the minimum-cost entry is the dedup survivor.  The match is
verified by re-quantising that entry's mozc cost to a byte
(hi = p99 of deduped costs, recomputed here) and comparing with the
token's stored cost byte.

Output layout (little-endian):
  header 64 B: "BISC", ver u32=1, n_tokens u32, n_ids u32,
               matrix_resolution u32 (64), off_tok_lid u32,
               off_tok_rid u32, off_cls u32, off_matrix u32, reserved
  tok_lid:  n_tokens * u16      (mozc lid per BIDF token, token order)
  tok_rid:  n_tokens * u16
  cls:      256 * (lid u16, rid u16)  per-BIDF-class representatives
            (mode lid / mode rid over matched tokens; used for
            unknown-word nodes and unmatched tokens)
  matrix:   n_ids * n_ids * u8, value = min(255, round(cost / 64))

Usage:
  gen_sidecar.py --bidf brainux2.bidf --mozc-dir /path/to/dictionary_oss \
                 --out data/brainux2.bisc
Provenance of the mozc data is recorded in REPORT-m2.md (source
repo + commit) and in buildbrain's tools/ime/MOZC-NOTICE.md.
"""
import argparse
import array
import os
import re
import struct
import sys
from collections import Counter, defaultdict

# ---------------------------------------------------------------- classify
# Copied verbatim from buildbrain tools/ime/convert_mozc.py (same
# project) so the class of a mozc lid here is identical to the class
# the BIDF token was built with.

def classify(pos: str) -> int:
    f = (pos.split(",") + ["*"] * 7)[:7]
    major, sub1, sub2, conj = f[0], f[1], f[2], f[4]
    if major == "名詞":
        if sub1 == "固有名詞":
            return {"人名": 2, "地域": 3, "組織": 4}.get(sub2, 5)
        if sub1 == "接尾":
            if sub2 == "助数詞":
                return 10
            if sub2 in ("人名", "地域"):
                return 11
            return 9
        return {
            "一般": 1, "サ変接続": 6, "形容動詞語幹": 7, "数": 8,
            "代名詞": 12, "副詞可能": 13, "非自立": 14, "接続詞的": 15,
        }.get(sub1, 16)
    if major == "動詞":
        if sub1 == "非自立":
            return 25
        if sub1 == "接尾":
            return 26
        if conj.startswith("五段"):
            return 24 if "特殊" in conj else 20
        if conj.startswith("一段"):
            return 24 if conj == "一段・クレ" else 21
        if conj.startswith("サ変"):
            return 22
        if conj.startswith("カ変"):
            return 23
        if conj.startswith("ラ変") or conj.startswith("文語"):
            return 24
        return 27
    if major == "形容詞":
        return {"自立": 30, "非自立": 31, "接尾": 32}.get(sub1, 30)
    if major == "助詞":
        return {
            "格助詞": 60, "係助詞": 61, "副助詞": 62, "接続助詞": 63,
            "終助詞": 64, "副詞化": 65, "連体化": 66, "並立助詞": 67,
            "特殊": 68, "副助詞／並立助詞／終助詞": 69,
        }.get(sub1, 70)
    if major == "助動詞":
        return 71
    if major == "副詞":
        return 41 if sub1 == "助詞類接続" else 40
    if major == "連体詞":
        return 42
    if major == "接続詞":
        return 43
    if major == "感動詞":
        return 44
    if major == "フィラー":
        return 45
    if major == "接頭詞":
        return {"名詞接続": 50, "動詞接続": 51, "形容詞接続": 52,
                "数接続": 53}.get(sub1, 54)
    if major == "記号":
        return {"一般": 80, "アルファベット": 81, "括弧開": 82,
                "括弧閉": 83, "句点": 84, "読点": 85, "空白": 86}.get(sub1, 87)
    if major == "その他":
        return 46 if sub1 == "間投" else 0
    return 0


# ---------------------------------------------------------------- BIDF reader
# Pure-Python BIDF v2 reader, copied from buildbrain
# tools/ime/test_bidict2.py (same project) and trimmed to what the
# generator needs: token enumeration in stored order with reading
# and surface materialised.

HDR = struct.Struct("<4sIIIIIIIII")
TRIE_HDR = struct.Struct("<III")
SAMPLE_BLOCK = 1024


class BitVec:
    def __init__(self, data: bytes, nbits: int):
        self.nbits = nbits
        self.words = []
        nwords = (nbits + 63) // 64
        for w in range(nwords):
            word = 0
            for b in range(8):
                i = w * 8 + b
                if i < len(data):
                    word = (word << 8) | data[i]
                else:
                    word <<= 8
            self.words.append(word)
        self.prefix = [0]
        for w in self.words:
            self.prefix.append(self.prefix[-1] + bin(w).count("1"))

    def get(self, p: int) -> int:
        return (self.words[p // 64] >> (63 - (p % 64))) & 1

    def rank1(self, p: int) -> int:
        if p < 0:
            return 0
        w, off = p // 64, p % 64
        mask = (~0 << (63 - off)) & ((1 << 64) - 1)
        return self.prefix[w] + bin(self.words[w] & mask).count("1")

    def rank0_exclusive(self, p: int) -> int:
        if p <= 0:
            return 0
        return p - self.rank1(p - 1)

    def _select(self, k: int, want_one: bool) -> int:
        lo, hi = 0, len(self.words)
        if want_one:
            total = self.prefix[-1]
            if k < 1 or k > total:
                return -1
            while lo < hi:
                mid = (lo + hi) // 2
                if self.prefix[mid + 1] < k:
                    lo = mid + 1
                else:
                    hi = mid
            w = lo
            before = self.prefix[w]
            word = self.words[w]
            need = k - before
            for off in range(64):
                if (word >> (63 - off)) & 1:
                    need -= 1
                    if need == 0:
                        return w * 64 + off
        else:
            total = self.nbits - self.prefix[-1]
            if k < 1 or k > total:
                return -1
            while lo < hi:
                mid = (lo + hi) // 2
                zeros = (mid + 1) * 64 - self.prefix[mid + 1]
                if zeros < k:
                    lo = mid + 1
                else:
                    hi = mid
            w = lo
            before = w * 64 - self.prefix[w]
            word = self.words[w]
            need = k - before
            for off in range(64):
                if not ((word >> (63 - off)) & 1):
                    need -= 1
                    if need == 0:
                        return w * 64 + off
        return -1

    def select1(self, k: int) -> int:
        return self._select(k, True)

    def select0(self, k: int) -> int:
        return self._select(k + 1, False)


class Trie:
    def __init__(self, data: bytes, off: int, has_counts: bool):
        self.n_nodes, self.label_bytes, self.n_term = TRIE_HDR.unpack_from(data, off)
        p = off + TRIE_HDR.size
        n_louds_bytes = (2 * self.n_nodes + 7) // 8
        self.louds = BitVec(data[p:p + n_louds_bytes], 2 * self.n_nodes)
        p += n_louds_bytes
        n_labels = self.n_nodes - 1
        if self.label_bytes == 1:
            self.labels = list(data[p:p + n_labels])
        else:
            self.labels = list(struct.unpack_from(f"<{n_labels}H", data, p))
        p += n_labels * self.label_bytes
        n_term_bytes = (self.n_nodes + 7) // 8
        self.terminal = BitVec(data[p:p + n_term_bytes], self.n_nodes)
        p += n_term_bytes
        self.counts = None
        self.samples = None
        if has_counts:
            self.counts = list(data[p:p + self.n_term])
            p += self.n_term
            nsamp = (self.n_term + SAMPLE_BLOCK - 1) // SAMPLE_BLOCK
            self.samples = list(struct.unpack_from(f"<{nsamp}I", data, p))
            p += 4 * nsamp
        self.end = p

    def parent(self, v: int) -> int:
        p = self.louds.select1(v + 1)
        return self.louds.rank0_exclusive(p)

    def label_path(self, v: int):
        path = []
        while v > 0:
            path.append(self.labels[v - 1])
            v = self.parent(v)
        path.reverse()
        return path

    def node_of_terminal(self, r: int) -> int:
        return self.terminal.select1(r + 1)


class Bidf2:
    def __init__(self, path: str):
        self.data = open(path, "rb").read()
        (magic, ver, n_entries, n_readings, n_surfaces, cb_off, rd_off,
         sf_off, tok_off, conn_off) = HDR.unpack_from(self.data, 0)
        assert magic == b"BIDF" and ver == 2, (magic, ver)
        self.n_entries = n_entries
        self.tok_off = tok_off
        p = cb_off
        (nchars,) = struct.unpack_from("<I", self.data, p)
        p += 4
        self.book = []
        for _ in range(nchars):
            ln = self.data[p]
            p += 1
            self.book.append(self.data[p:p + ln].decode("utf-8"))
            p += ln
        self.reading_trie = Trie(self.data, rd_off, has_counts=True)
        self.surface_trie = Trie(self.data, sf_off, has_counts=False)

    def decode(self, codes):
        return "".join(self.book[c] for c in codes)

    def parse_tokens(self):
        toks = []
        p = self.tok_off
        data = self.data
        for _ in range(self.n_entries):
            sid = 0
            shift = 0
            while True:
                b = data[p]
                p += 1
                sid |= (b & 0x7F) << shift
                if not b & 0x80:
                    break
                shift += 7
            pos = data[p]
            cost = data[p + 1]
            p += 2
            toks.append((sid, pos, cost))
        return toks

    def tokens_in_order(self):
        """Yield (reading, surface, pos, cost_byte) per BIDF token,
        in stored token order."""
        toks = self.parse_tokens()
        rt, st = self.reading_trie, self.surface_trie
        surf_cache = {}
        idx = 0
        for rank in range(rt.n_term):
            node = rt.node_of_terminal(rank)
            reading = self.decode(rt.label_path(node))
            for _ in range(rt.counts[rank]):
                sid, pos, cost = toks[idx]
                idx += 1
                if sid not in surf_cache:
                    snode = st.node_of_terminal(sid)
                    surf_cache[sid] = self.decode(st.label_path(snode))
                yield reading, surf_cache[sid], pos, cost
        assert idx == self.n_entries, (idx, self.n_entries)


# ---------------------------------------------------------------- mozc side

def _kana_ok(ch: str) -> bool:
    o = ord(ch)
    return (0x3041 <= o <= 0x3096 or 0x30A1 <= o <= 0x30F6
            or o in (0x309D, 0x309E, 0x30FC))


def normalize_reading(r: str) -> str:
    return "".join(chr(ord(c) - 0x60) if 0x30A1 <= ord(c) <= 0x30F6 else c
                   for c in r)


def load_mozc(mozc_dir):
    """Replay convert_mozc.py's dedup.  Returns (groups, hi) where
    groups maps (reading, surface, cls) -> [min_cost, lid, rid,
    n_distinct_lids] and hi is the p99 quantisation bound."""
    lid_pos = {}
    with open(os.path.join(mozc_dir, "id.def"), encoding="utf-8") as fp:
        for line in fp:
            lid_s, pos = line.rstrip("\n").split(" ", 1)
            lid_pos[int(lid_s)] = pos
    lid_cls = {lid: classify(pos) for lid, pos in lid_pos.items()}
    n_ids_pre = max(lid_pos) + 1 if lid_pos else 0
    freq_lid = [0] * n_ids_pre
    freq_rid = [0] * n_ids_pre

    filters = []
    fpath = os.path.join(mozc_dir, "dictionary_filter.tsv")
    if os.path.exists(fpath):
        with open(fpath, encoding="utf-8") as fp:
            for line in fp:
                line = line.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                k, v = line.split("\t")
                filters.append((re.compile(k), re.compile(v)))

    groups = {}
    lidsets = {}
    import glob
    n_raw = 0
    for fn in sorted(glob.glob(os.path.join(mozc_dir, "dictionary[0-9][0-9].txt"))):
        with open(fn, encoding="utf-8") as fp:
            for line in fp:
                n_raw += 1
                parts = line.rstrip("\n").split("\t")
                if len(parts) < 5:
                    continue
                reading, lid_s, rid_s, cost_s, surface = parts[:5]
                if any(krx.fullmatch(reading) and vrx.fullmatch(surface)
                       for krx, vrx in filters):
                    continue
                if not reading or not all(_kana_ok(c) for c in reading):
                    continue
                lid, rid, cost = int(lid_s), int(rid_s), int(cost_s)
                cls = lid_cls.get(lid)
                if cls is None:
                    continue
                if lid < n_ids_pre:
                    freq_lid[lid] += 1
                if rid < n_ids_pre:
                    freq_rid[rid] += 1
                key = (normalize_reading(reading), surface, cls)
                cur = groups.get(key)
                if cur is None or cost < cur[0]:
                    groups[key] = [cost, lid, rid, None]
                elif cost == cur[0] and (lid, rid) != (cur[1], cur[2]):
                    # min-cost tie: several source entries map
                    # to this one BIDF token (classic case:
                    # まし = 特殊・マス 命令形 lid 234 vs 連用形
                    # lid 260, both cost 0).  The token's true
                    # form is unrecoverable from BIDF; resolve
                    # later against the connection matrix
                    # (resolve_ties()).
                    if cur[3] is None:
                        cur[3] = [(cur[1], cur[2]), (lid, rid)]
                    elif (lid, rid) not in cur[3]:
                        cur[3].append((lid, rid))
                s = lidsets.get(key)
                if s is None:
                    lidsets[key] = {lid}
                else:
                    s.add(lid)
    costs = sorted(g[0] for g in groups.values())
    hi = costs[int(len(costs) * 0.99)]
    print(f"mozc raw lines: {n_raw}; dedup groups: {len(groups)}; "
          f"quantisation hi (p99) = {hi}")
    return groups, lidsets, hi, freq_lid, freq_rid


def resolve_ties(groups, n_ids, matrix, freq_lid, freq_rid):
    """Pick, for every min-cost-tied group, the (lid, rid) with the
    lowest score = col_mean[lid] + row_p25[rid] over mozc's matrix.

    Why this statistic (two measured failures led here):
    - Plain col_mean + row_mean picks lid 234 （特殊・マス 命令形）
      for the token まし over the real stem lid 260 （連用形）:
      a stem's row MEAN is terrible because it connects cheaply
      only to a handful of followers （た at cost 24, ...), so the
      mean punishes exactly the selective ids that are correct.
    - Frequency weighting by dictionary entry counts also fails:
      auxiliaries/particles have very few dictionary entries, so
      the frequent-in-text followers carry almost no weight.
    col_mean separates the pair cleanly (6383 vs 7797, since verb
    renyou lefts connect to 260 at ~1400 and to 234 at ~9200), and
    row_p25 (25th percentile, not the mean) keeps a voice for the
    outgoing side without letting selectivity dominate.
    Statistics are computed on demand only for ids that appear in
    a tie.  Returns the number of tied groups."""
    col_mean_cache = {}
    row_p25_cache = {}

    def col_mean(lid):
        if lid not in col_mean_cache:
            col = matrix[lid::n_ids]
            col_mean_cache[lid] = sum(col) / n_ids
        return col_mean_cache[lid]

    def row_p25(rid):
        if rid not in row_p25_cache:
            row = sorted(matrix[rid * n_ids:(rid + 1) * n_ids])
            row_p25_cache[rid] = row[n_ids // 4]
        return row_p25_cache[rid]

    n_tied = 0
    for g in groups.values():
        if g[3]:
            n_tied += 1
            best = min(g[3],
                       key=lambda p: col_mean(p[0]) + row_p25(p[1]))
            g[1], g[2] = best
            g[3] = None
    return n_tied


def load_matrix(mozc_dir):
    path = os.path.join(mozc_dir, "connection_single_column.txt")
    vals = array.array("i")
    with open(path, encoding="utf-8") as fp:
        n = int(fp.readline())
        for line in fp:
            vals.append(int(line))
    assert len(vals) == n * n, (len(vals), n)
    print(f"connection matrix: {n}x{n}, raw cost range "
          f"{min(vals)}..{max(vals)}")
    return n, vals


# ---------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bidf", required=True)
    ap.add_argument("--mozc-dir", required=True,
                    help="mozc src/data/dictionary_oss directory")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    groups, lidsets, hi, freq_lid, freq_rid = load_mozc(args.mozc_dir)
    n_ids, matrix = load_matrix(args.mozc_dir)
    freq_lid = (freq_lid + [0] * n_ids)[:n_ids]
    freq_rid = (freq_rid + [0] * n_ids)[:n_ids]
    n_tied = resolve_ties(groups, n_ids, matrix, freq_lid, freq_rid)
    print(f"min-cost tied groups resolved by connection profile: {n_tied}")

    def q(cost: int) -> int:
        return min(255, round(cost * 255 / hi)) if hi else 0

    bidf = Bidf2(args.bidf)
    tok_lid = array.array("H", bytes(2 * bidf.n_entries))
    tok_rid = array.array("H", bytes(2 * bidf.n_entries))
    cls_lid_count = defaultdict(Counter)
    cls_rid_count = defaultdict(Counter)
    n_verified = n_matched = n_ambiguous = n_unmatched = 0
    diag = {}
    i = 0
    for reading, surface, pos, cost_byte in bidf.tokens_in_order():
        key = (reading, surface, pos)
        g = groups.get(key)
        if g is None:
            n_unmatched += 1
            lid = rid = 0
        else:
            cost, lid, rid = g[0], g[1], g[2]
            n_matched += 1
            if q(cost) == cost_byte:
                n_verified += 1
            if len(lidsets[key]) > 1:
                n_ambiguous += 1
            cls_lid_count[pos][lid] += 1
            cls_rid_count[pos][rid] += 1
        tok_lid[i] = lid
        tok_rid[i] = rid
        if (reading, surface, pos) in (("かい", "書い", 20), ("て", "て", 63),
                                        ("で", "で", 63),
                                        ("わたし", "私", 12), ("は", "は", 61),
                                        ("すん", "住ん", 20), ("き", "来", 23)):
            diag[(reading, surface, pos)] = (lid, rid)
        i += 1
    print(f"tokens: {bidf.n_entries}; matched {n_matched} "
          f"({n_matched/bidf.n_entries:.1%}), cost-byte verified "
          f"{n_verified} ({n_verified/bidf.n_entries:.1%}), "
          f"ambiguous-lid groups {n_ambiguous}, unmatched {n_unmatched}")

    cls_rep = []
    for cls in range(256):
        lid = cls_lid_count[cls].most_common(1)[0][0] if cls_lid_count[cls] else 0
        rid = cls_rid_count[cls].most_common(1)[0][0] if cls_rid_count[cls] else 0
        cls_rep.append((lid, rid))

    # diagnostics: mozc raw connection costs for famous pairs
    def conn_of(a, b):
        la = diag.get(a)
        lb = diag.get(b)
        if la and lb:
            return matrix[la[1] * n_ids + lb[0]]
        return None
    print("diag tokens (reading, surface, cls) -> (lid, rid):", diag)
    print("mozc conn 書い->て:", conn_of(("かい", "書い", 20), ("て", "て", 63)),
          " 私->は:", conn_of(("わたし", "私", 12), ("は", "は", 61)),
          " 住ん->で:", conn_of(("すん", "住ん", 20), ("で", "で", 63)))

    res = 64
    mtx = bytes(min(255, round(v / res)) for v in matrix)

    hdr_size = 64
    off_lid = hdr_size
    off_rid = off_lid + 2 * bidf.n_entries
    off_cls = off_rid + 2 * bidf.n_entries
    off_mtx = off_cls + 256 * 4
    hdr = struct.pack("<4sIIIIIIII", b"BISC", 1, bidf.n_entries, n_ids,
                      res, off_lid, off_rid, off_cls, off_mtx)
    hdr = hdr + bytes(hdr_size - len(hdr))
    with open(args.out, "wb") as fp:
        fp.write(hdr)
        tok_lid.tofile(fp)
        tok_rid.tofile(fp)
        for lid, rid in cls_rep:
            fp.write(struct.pack("<HH", lid, rid))
        fp.write(mtx)
    print(f"wrote {args.out}: {off_mtx + len(mtx)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
