#!/usr/bin/env python3
"""Build a BIDF v3 dictionary (.bid3) for libbrainime M3.

BIDF v3 is a single self-contained file that integrates what M2
kept in two files (BIDF v2 + .bisc sidecar) plus M3 additions:

  DICT   the BIDF v2 file, verbatim (LOUDS tries + token stream)
  TOKID  per-token mozc lid/rid (from the M2 sidecar generation)
  CLSREP per-BIDF-class representative (lid, rid)
  CONN   mozc connection matrix, 4-bit linear-quantised and
         nibble-packed, with the mozc ids PERMUTED so that hot ids
         (frequent in the corpus / dictionary) occupy low indices:
         the hot rows then live in the first pages of the mmap and
         cold rows are rarely faulted in.  Values are read directly
         from the mmap at run time (nibble + 16-entry LUT) — no
         decompression, no load-time expansion (zero-copy).
  SUPP   supplement vocabulary from non-mozc sources (NEologd,
         SudachiDict x corpus frequency), sorted by reading so the
         loader binary-searches it in place in the mmap.
  BIGR   word-bigram statistics from a Japanese corpus (Tatoeba,
         tokenised by SudachiPy as teacher), as an open-addressing
         hash table probed in place in the mmap.
  RNK    linear reranker weights (float32), read directly from
         the mmap.  Filled by tools/train_rerank.c +
         tools/patch_rerank.py; zeros (except w[cost]=1) at build.
  PRIO   (M7, optional, --prior) per-token surface-prior deltas
         in cost-byte units: the corpus' within-reading surface
         preference woven into the word cost itself.  The loader
         adds them when decoding the token index; the same table
         also adjusts SUPP cost bytes at build time.
  META   UTF-8 build provenance text.

Design rules (project-wide): no load-time bulk copies — every
section is designed to be read in place from a MAP_SHARED mapping
shared between processes; caches must be bounded.

Usage:
  build_bidict3.py --bidf brainux2.bidf --bisc brainux2.bisc \
      --supp supp.tsv --bigram bigram_counts.tsv \
      --unigram unigram_counts.tsv --out brainux3.bid3
"""
import argparse, hashlib, math, os, struct, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_sidecar import Bidf2  # noqa: E402  (pure-Python BIDF reader)

SEC = {"DICT": 1, "TOKID": 2, "CLSREP": 3, "CONN": 4,
       "SUPP": 5, "BIGR": 6, "RNK": 7, "META": 8, "PRIO": 9}
RNK_NFEAT = 26          # keep in sync with BI_RERANK_NFEAT (engine.c)
RNK_FEAT_VER = 3
VMAX = 238              # max value of the /64-quantised matrix

def lut_lin16():
    step = VMAX / 15.0
    return [round(step * i) for i in range(16)]

LUT = lut_lin16()

def quantise(v):
    best, bd = 0, 1 << 30
    for i, lv in enumerate(LUT):
        d = abs(lv - v)
        if d < bd:
            bd, best = d, i
    return best

def fnv1a64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def bigram_key(pr, ps, cr, cs):
    h = fnv1a64(pr.encode() + b"\x01" + ps.encode() + b"\x01"
                + cr.encode() + b"\x01" + cs.encode())
    k = (h >> 32) ^ (h & 0xFFFFFFFF)
    return k if k != 0 else 1

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bidf", required=True)
    ap.add_argument("--bisc", required=True)
    ap.add_argument("--supp", required=True)
    ap.add_argument("--bigram", required=True)
    ap.add_argument("--unigram", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bigram-min-count", type=int, default=2)
    ap.add_argument("--prior", help="M7 surface-prior table "
                    "(reading\\tsurface\\tdelta_bytes): emits a "
                    "PRIO section and adjusts SUPP cost bytes")
    args = ap.parse_args()

    prior = {}
    if args.prior:
        for line in open(args.prior, encoding="utf-8"):
            f = line.rstrip("\n").split("\t")
            if len(f) == 3:
                prior[(f[0], f[1])] = int(f[2])
        print(f"prior: {len(prior)} (reading, surface) deltas "
              f"from {args.prior}")

    bidf_bytes = open(args.bidf, "rb").read()
    bisc = open(args.bisc, "rb").read()
    (magic, ver, ntok, nids, res, off_lid, off_rid, off_cls,
     off_matrix) = struct.unpack_from("<4sIIIIIIII", bisc, 0)
    assert magic == b"BISC" and ver == 1
    lid = list(struct.unpack_from(f"<{ntok}H", bisc, off_lid))
    rid = list(struct.unpack_from(f"<{ntok}H", bisc, off_rid))
    clsrep = list(struct.unpack_from("<512H", bisc, off_cls))
    matrix = bisc[off_matrix:off_matrix + nids * nids]
    assert len(matrix) == nids * nids

    # ---- id permutation by hotness -------------------------------
    uni = {}
    for line in open(args.unigram, encoding="utf-8"):
        r, s, n = line.rstrip("\n").split("\t")
        uni[(r, s)] = int(n)
    hot = [0] * nids
    bidf = Bidf2(args.bidf)
    tok_rs = []                 # (reading, surface) in token order
    for i, (reading, surface, pos, costb) in enumerate(bidf.tokens_in_order()):
        w = uni.get((reading, surface), 0) * 64 + (255 - costb)
        hot[lid[i]] += w
        hot[rid[i]] += w
        tok_rs.append((reading, surface))
    assert len(tok_rs) == ntok
    order = sorted(range(1, nids), key=lambda i: -hot[i])
    perm = [0] * nids            # old -> new
    for new, old in enumerate(order, start=1):
        perm[old] = new
    lid = [perm[x] for x in lid]
    rid = [perm[x] for x in rid]
    clsrep = [perm[x] for x in clsrep]

    # ---- sections --------------------------------------------------
    sec_dict = bidf_bytes
    sec_tokid = struct.pack("<I", ntok) + struct.pack(f"<{ntok}H", *lid) \
        + struct.pack(f"<{ntok}H", *rid)
    sec_clsrep = struct.pack("<512H", *clsrep)

    # CONN: permute + 4-bit quantise + pack (high nibble = even lid)
    packed = bytearray((nids * nids + 1) // 2)
    for orow in range(nids):
        nrow = perm[orow]
        base_o = orow * nids
        base_n = nrow * nids
        for ocol in range(nids):
            v = matrix[base_o + ocol]
            q = quantise(v)
            idx = base_n + perm[ocol]
            if idx & 1:
                packed[idx // 2] |= q
            else:
                packed[idx // 2] |= q << 4
    sec_conn = struct.pack("<I", nids) + struct.pack("<16H", *LUT) \
        + bytes(packed)

    # SUPP: sorted by (encoded reading, surface); offsets + records.
    # Readings are stored as 1-byte kana codes (see kana_encode);
    # 58% of supplement surfaces are exactly the katakana form of
    # the reading and are stored as a flag instead of bytes.
    def kana_encode(r):
        out = bytearray()
        for c in r:
            o = ord(c)
            if 0x3041 <= o <= 0x3096:
                out.append(o - 0x3041)
            elif o == 0x30FC:
                out.append(86)
            elif o == 0x309D:
                out.append(87)
            elif o == 0x309E:
                out.append(88)
            else:
                return None
        return bytes(out)

    def hira2kata(r):
        return "".join(chr(ord(c) + 0x60) if 0x3041 <= ord(c) <= 0x3096
                       else c for c in r)

    supp_rows = []
    n_skip = 0
    for line in open(args.supp, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 5:
            continue
        r, s, cls, byte = f[0], f[1], int(f[2]), int(f[3])
        if prior:
            byte = max(0, min(255, byte + prior.get((r, s), 0)))
        enc = kana_encode(r)
        if enc is None:
            n_skip += 1
            continue
        is_kata = (s == hira2kata(r))
        supp_rows.append((enc, b"" if is_kata else s.encode(),
                          cls, byte, is_kata))
    supp_rows.sort(key=lambda x: (x[0], x[1]))
    offs = []
    recs = bytearray()
    for enc, sb, cls, byte, is_kata in supp_rows:
        offs.append(len(recs))
        flags = 1 if is_kata else 0
        recs += struct.pack("<BBBB", len(enc), flags, cls, byte) + enc
        if not is_kata:
            recs += struct.pack("<B", len(sb)) + sb
    offs.append(len(recs))
    sec_supp = struct.pack("<I", len(supp_rows)) \
        + struct.pack(f"<{len(offs)}I", *offs) + bytes(recs)
    print(f"supp: {len(supp_rows)} entries ({n_skip} skipped: "
          f"unencodable reading)")

    # BIGR: open-addressing table, linear probe
    pairs = []
    for line in open(args.bigram, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 5:
            continue
        n = int(f[4])
        if n >= args.bigram_min_count:
            pairs.append((f[0], f[1], f[2], f[3], n))
    nslots = 1
    while nslots < len(pairs) / 0.6:
        nslots *= 2
    slots = [(0, 0)] * nslots   # (key, score)
    for pr, ps, cr, cs, n in pairs:
        k = bigram_key(pr, ps, cr, cs)
        score = min(32767, round(48 * math.log2(n)))
        i = k & (nslots - 1)
        while slots[i][0] != 0 and slots[i][0] != k:
            i = (i + 1) & (nslots - 1)
        if slots[i][0] == k:
            if score > slots[i][1]:
                slots[i] = (k, score)
        else:
            slots[i] = (k, score)
    sec_bigr = struct.pack("<II", nslots, len(pairs)) + b"".join(
        struct.pack("<IhH", k, sc, 0) for k, sc in slots)

    # PRIO (M7, optional): per-token surface-prior deltas in
    # cost-byte units, token order, applied by the loader when it
    # decodes the token index.  Zero = no change.
    sec_prio = None
    if prior:
        dl = bytearray()
        for rs in tok_rs:
            dl.append(prior.get(rs, 0) & 0xFF)
        sec_prio = struct.pack("<I", ntok) + bytes(dl)

    # RNK: w[cost]=1, rest 0 (== M2 behaviour until trained)
    w = [0.0] * RNK_NFEAT
    w[0] = 1.0
    # field order is (feat_ver, nfeat): the loader reads nfeat at
    # +4 and tools/patch_rerank.py writes in this order.  (M3's
    # builder wrote them swapped; the patch step masked it.)
    sec_rnk = struct.pack("<II", RNK_FEAT_VER, RNK_NFEAT) \
        + struct.pack(f"<{RNK_NFEAT}f", *w)

    meta = (
        "BIDF v3 build\n"
        f"bidf_sha256={hashlib.sha256(bidf_bytes).hexdigest()[:16]}\n"
        f"bisc_sha256={hashlib.sha256(bisc).hexdigest()[:16]}\n"
        f"tokens={ntok} ids={nids} supp={len(supp_rows)} "
        f"bigrams={len(pairs)} slots={nslots}\n"
        "conn=mozc connection_single_column (via M2 sidecar), "
        "4bit linear quantised, ids permuted by corpus+dict hotness\n"
        "supp=NEologd seed 20200910 + SudachiDict x Tatoeba frequency\n"
        "bigram=Tatoeba jpn sentences (CC BY 2.0 FR), SudachiPy teacher\n"
        + (f"prior=M7 surface prior, {len(prior)} deltas, "
           f"src={os.path.basename(args.prior)}\n" if prior else "")
    ).encode()
    sec_meta = meta

    sections = [(SEC["DICT"], sec_dict), (SEC["TOKID"], sec_tokid),
                (SEC["CLSREP"], sec_clsrep), (SEC["CONN"], sec_conn),
                (SEC["SUPP"], sec_supp), (SEC["BIGR"], sec_bigr)]
    if sec_prio is not None:
        sections.append((SEC["PRIO"], sec_prio))
    sections += [(SEC["RNK"], sec_rnk), (SEC["META"], sec_meta)]
    hdr_size = 16 + 16 * len(sections)
    off = hdr_size
    out = bytearray(b"\0" * hdr_size)
    directory = []
    for sid, blob in sections:
        directory.append((sid, off, len(blob)))
        out += blob
        off += len(blob)
        pad = (-off) % 8
        out += b"\0" * pad
        off += pad
    struct.pack_into("<4sII", out, 0, b"BID3", 3, len(sections))
    # directory entry: id u32, off u32, len u32, crc32 u32 — the
    # loader verifies every section's CRC32 at open (the device
    # resets often and the file lives on a shared FAT SD card;
    # corruption must be detected, never silently converted with).
    import zlib as _zlib
    blob_by_id = dict(sections)
    for i, (sid, o, ln) in enumerate(directory):
        crc = _zlib.crc32(blob_by_id[sid]) & 0xFFFFFFFF
        struct.pack_into("<IIII", out, 16 + i * 16, sid, o, ln, crc)
    open(args.out, "wb").write(bytes(out))
    print(f"wrote {args.out}: {len(out)} bytes")
    id2name = {v: k for k, v in SEC.items()}
    for (sid, o, ln) in directory:
        print(f"  {id2name[sid]:7s} off={o:9d} len={ln:9d}")
    print(f"  supp entries={len(supp_rows)} bigram pairs={len(pairs)} "
          f"slots={nslots}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
