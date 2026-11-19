#!/usr/bin/env python3
"""Build a BIDF v2 dictionary from a TSV word list.

Input TSV columns: reading <TAB> surface <TAB> pos_class(0-255) <TAB> cost(0-255)
Lines starting with # are comments. Output: a single mmap-able file:

  header    64 bytes: magic BIDF, version=2, entry/readings/surface
            counts, section offsets (codebook, reading, surface,
            tokens, conn)
  codebook  u32 nchars; per code (in code order): u8 len + UTF-8 bytes.
            Codes 0..127 are single-byte, 128.. are two-byte:
            first byte 0x80 + (code-128)>>8, second (code-128)&0xFF.
            All reading characters are assigned codes < 128 by
            construction (there are 86 in the mozc snapshot), so the
            reading trie stores 1-byte labels.
  reading   LOUDS trie over reading code sequences (see below)
  surface   LOUDS trie over surface code sequences; a surface's id
            is its terminal rank in this trie (BFS order)
  tokens    per entry: ULEB128 surface id, pos u8, cost u8.
            Grouped by reading in reading-trie BFS terminal order,
            sorted by cost within the group.
  conn      256*256 int8 POS-class connection costs (zeros unless
            a --conn TSV of 'left right cost' lines is given)

LOUDS trie section layout:
  u32 n_nodes, u32 label_bytes (1 or 2), u32 n_terminals
  louds bits: 2*n_nodes bits, MSB-first per byte. Bit 0 is 1 (the
    root's virtual edge); then for each node in BFS order, one 1
    per child followed by one 0. Node v's children are the 1-bits
    between select0(v-1) and select0(v); a 1-bit at position p is
    the edge to node rank1(p)-1. Parent of node v>=1: p =
    select1(v+1), parent = rank0(p) (zeros in [0, p)).
  labels: (n_nodes-1) entries of label_bytes, indexed by node id - 1
  terminal bits: n_nodes bits, MSB-first (1 = terminal node)
  reading trie only: counts (n_terminals u8, entries per reading)
    and samples (ceil(n_terminals/1024) u32 first_token values,
    one per 1024-terminal block; first_token(r) = sample[r//1024]
    + sum(counts[r//1024*1024 : r]))

v2 replaces the v1 sorted index + raw string pool: measured on the
180k-entry mozc dictionary, 6.35 MB -> ~1.95 MB (see docs/ime.md).
"""
import struct
import sys
from collections import Counter, defaultdict, deque

HDR = struct.Struct("<4sIIIIIIIII")  # magic, ver, entries, readings, surfaces, cb, rd, sf, tok, conn
HDR_SIZE = 64
TRIE_HDR = struct.Struct("<III")  # n_nodes, label_bytes, n_terminals
SAMPLE_BLOCK = 1024


def build_codebook(readings, surfaces):
    """Assign codes: reading chars first (codes < 128, by frequency),
    then all other chars by frequency (codes 128+ are two-byte)."""
    rchars = Counter()
    for r in readings:
        rchars.update(r)
    if len(rchars) > 128:
        raise ValueError(f"too many reading chars for 1-byte labels: {len(rchars)}")
    allchars = Counter()
    for r in readings:
        allchars.update(r)
    for s in surfaces:
        allchars.update(s)
    codes = {}
    # reading chars first, most frequent first
    for ch, _ in rchars.most_common():
        codes[ch] = len(codes)
    # remaining chars, most frequent first (fills 1-byte codes 86..127 first)
    for ch, _ in allchars.most_common():
        if ch not in codes:
            codes[ch] = len(codes)
    # codebook ordered by code
    book = [""] * len(codes)
    for ch, code in codes.items():
        book[code] = ch
    return codes, book


def encode(s, codes):
    return [codes[ch] for ch in s]


class BitWriter:
    def __init__(self):
        self.bytes = bytearray()
        self.nbits = 0

    def put(self, bit):
        if self.nbits % 8 == 0:
            self.bytes.append(0)
        if bit:
            self.bytes[-1] |= 0x80 >> (self.nbits % 8)
        self.nbits += 1

    def put_bits(self, bits):
        for b in bits:
            self.put(b)


def build_trie(seqs):
    """seqs: iterable of code lists. Returns dict with BFS-ordered data:
    n_nodes, labels (per node id >= 1), terminal (bytearray bitset),
    louds (bytearray bitset), node_seq (node id -> seq, terminals only)."""
    root = {}
    for seq in seqs:
        node = root
        for code in seq:
            node = node.setdefault(code, {})
        node[-1] = True  # terminal marker
    # BFS assign node ids; children in ascending label order
    ids = {id(root): 0}
    order = [root]
    q = deque([root])
    while q:
        node = q.popleft()
        for label in sorted(k for k in node if k != -1):
            child = node[label]
            ids[id(child)] = len(order)
            order.append(child)
            q.append(child)
    n_nodes = len(order)
    labels = [0] * (n_nodes - 1)  # labels[v-1] = edge label into node v
    louds = BitWriter()
    louds.put(1)
    terminal = bytearray((n_nodes + 7) // 8)
    node_seq = {}
    for v, node in enumerate(order):
        if -1 in node:
            terminal[v // 8] |= 0x80 >> (v % 8)
        for label in sorted(k for k in node if k != -1):
            child = node[label]
            louds.put(1)
            labels[ids[id(child)] - 1] = label
        louds.put(0)
    assert louds.nbits == 2 * n_nodes, (louds.nbits, n_nodes)
    return {
        "n_nodes": n_nodes,
        "labels": labels,
        "louds": bytes(louds.bytes),
        "terminal": bytes(terminal),
        "order": order,
        "ids": ids,
    }


def pack_trie(trie, label_bytes, counts=None):
    out = bytearray()
    n_term = sum(bin(b).count("1") for b in trie["terminal"])
    out += TRIE_HDR.pack(trie["n_nodes"], label_bytes, n_term)
    out += trie["louds"]
    if label_bytes == 1:
        out += bytes(trie["labels"])
    else:
        for lab in trie["labels"]:
            out += struct.pack("<H", lab)
    out += trie["terminal"]
    if counts is not None:
        out += bytes(counts)
        samples = []
        first = 0
        for i in range(0, len(counts), SAMPLE_BLOCK):
            samples.append(first)
            first += sum(counts[i:i + SAMPLE_BLOCK])
        for s in samples:
            out += struct.pack("<I", s)
    return bytes(out), n_term


def uleb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: build_bidict2.py words.tsv out.bidf [--conn conn.tsv]")
        return 2
    words_path, out_path = sys.argv[1], sys.argv[2]
    conn = bytearray(256 * 256)
    if "--conn" in sys.argv:
        cpath = sys.argv[sys.argv.index("--conn") + 1]
        with open(cpath, encoding="utf-8") as f:
            for line in f:
                parts = line.split()
                if len(parts) == 3:
                    left, right, cost = int(parts[0]), int(parts[1]), int(parts[2])
                    conn[left * 256 + right] = cost & 0xFF

    by_reading: dict[str, list[tuple[str, int, int]]] = defaultdict(list)
    surfaces: set[str] = set()
    with open(words_path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            reading, surface, pos, cost = line.split("\t")
            by_reading[reading].append((surface, int(pos), int(cost)))
            surfaces.add(surface)

    readings = sorted(by_reading)
    codes, book = build_codebook(readings, surfaces)
    print(f"codebook: {len(book)} chars, "
          f"{sum(len(c.encode()) + 1 for c in book)} bytes")

    # reading trie (labels are codes < 128 -> 1 byte)
    rseqs = [encode(r, codes) for r in readings]
    rtrie = build_trie(rseqs)
    # terminal rank (BFS order) -> reading: DFS the trie tracking node
    # ids, then order the terminal nodes by id.
    id_to_reading = {}

    def dfs(node, prefix):
        if -1 in node:
            id_to_reading[rtrie["ids"][id(node)]] = prefix
        for label in sorted(k for k in node if k != -1):
            dfs(node[label], prefix + book[label])
    dfs(rtrie["order"][0], "")
    rank_to_reading = {}
    rank = 0
    for v in range(rtrie["n_nodes"]):
        if v in id_to_reading:
            rank_to_reading[rank] = id_to_reading[v]
            rank += 1
    assert len(rank_to_reading) == len(readings), (len(rank_to_reading), len(readings))
    assert set(rank_to_reading.values()) == set(readings)

    rcounts = []
    for r in range(len(readings)):
        n = len(by_reading[rank_to_reading[r]])
        if n > 255:
            raise ValueError(f"reading {rank_to_reading[r]!r} has {n} entries (>255)")
        rcounts.append(n)
    rpacked, _ = pack_trie(rtrie, 1, counts=rcounts)

    # surface trie (labels are codes, up to 2 bytes)
    surf_sorted = sorted(surfaces)
    strie = build_trie([encode(s, codes) for s in surf_sorted])
    spacked, n_surf_term = pack_trie(strie, 2)
    assert n_surf_term == len(surfaces)
    # surface id = terminal rank in BFS order; map surface -> id
    surf_id = {}
    rank = 0
    id_to_surf = {}
    def dfs2(node, prefix):
        if -1 in node:
            id_to_surf[strie["ids"][id(node)]] = prefix
        for label in sorted(k for k in node if k != -1):
            dfs2(node[label], prefix + book[label])
    dfs2(strie["order"][0], "")
    for v in range(strie["n_nodes"]):
        if v in id_to_surf:
            surf_id[id_to_surf[v]] = rank
            rank += 1
    assert len(surf_id) == len(surfaces)

    # tokens grouped by reading in BFS terminal order, cost-sorted
    tokens = bytearray()
    for r in range(len(readings)):
        entries = sorted(by_reading[rank_to_reading[r]], key=lambda e: e[2])
        for surface, pos, cost in entries:
            tokens += uleb(surf_id[surface])
            tokens.append(pos)
            tokens.append(cost)

    # assemble
    cb = bytearray()
    cb += struct.pack("<I", len(book))
    for ch in book:
        b = ch.encode("utf-8")
        cb.append(len(b))
        cb += b
    cb_off = HDR_SIZE
    rd_off = cb_off + len(cb)
    sf_off = rd_off + len(rpacked)
    tok_off = sf_off + len(spacked)
    conn_off = tok_off + len(tokens)

    out = bytearray()
    out += HDR.pack(b"BIDF", 2, sum(rcounts), len(readings),
                    len(surfaces), cb_off, rd_off, sf_off, tok_off, conn_off)
    out += b"\0" * (HDR_SIZE - HDR.size)
    out += cb
    out += rpacked
    out += spacked
    out += tokens
    out += conn
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"{out_path}: {len(readings)} readings, {sum(rcounts)} entries, "
          f"{len(surfaces)} surfaces, {len(out)} bytes "
          f"({len(out) / max(sum(rcounts), 1):.1f} B/entry)")
    print(f"  sections: codebook {len(cb)}  reading-trie {len(rpacked)} "
          f" surface-trie {len(spacked)}  tokens {len(tokens)}  conn {len(conn)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
