#!/usr/bin/env python3
"""Regression test for BIDF v2 (build_bidict2.py).

Reads a v2 file with an independent pure-Python reader and checks it
against the source TSV ground truth:
  1. exact lookup for EVERY reading: candidate multiset equality
     (surface, pos, cost), order-insensitive within a reading
  2. prefix search for every distinct 1- and 2-char prefix of the
     readings: reading-set equality
  3. surface round-trip: decoding every surface id yields exactly
     the surface set (exercises the LOUDS parent walk)
  4. size breakdown + budget verdict (2.3-2.8 MB target, 3.5 MB hard)

Usage: test_bidict2.py words.tsv dict.bidf [--c-tool ./bidict_lookup2]
With --c-tool, also cross-checks the C lookup tool on a sample of
readings/prefixes against this Python reader.
"""
import struct
import subprocess
import sys
from collections import defaultdict

HDR = struct.Struct("<4sIIIIIIIII")
HDR_SIZE = 64
TRIE_HDR = struct.Struct("<III")
SAMPLE_BLOCK = 1024


class BitVec:
    """MSB-first bit array with O(1) rank via word prefix sums."""

    def __init__(self, data: bytes, nbits: int):
        self.nbits = nbits
        self.words = []
        # pack MSB-first bytes into 64-bit words (bit 0 = MSB of byte 0)
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
        # bit p: word p//64, within-word bit (63 - p%64) since word bit 63
        # holds logical bit 0 of the word
        return (self.words[p // 64] >> (63 - (p % 64))) & 1

    def rank1(self, p: int) -> int:
        """#1-bits in [0, p] inclusive; p=-1 -> 0."""
        if p < 0:
            return 0
        w, off = p // 64, p % 64
        mask = (~0 << (63 - off)) & ((1 << 64) - 1)
        return self.prefix[w] + bin(self.words[w] & mask).count("1")

    def rank0_exclusive(self, p: int) -> int:
        """#0-bits in [0, p)."""
        if p <= 0:
            return 0
        return p - self.rank1(p - 1)

    def _select(self, k: int, want_one: bool) -> int:
        """Position of the k-th (1-indexed) bit of the wanted value."""
        # binary search over word prefix sums
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
        """Position of the k-th 0-bit, 0-indexed k (k=0 -> first 0)."""
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

    def children(self, v: int):
        """(child_node_id, label) pairs for node v, in label order."""
        # select0(-1) is conceptually position 0: the leading 1-bit is
        # the root's virtual edge, not a child of the root.
        lo = self.louds.select0(v - 1) if v > 0 else 0
        hi = self.louds.select0(v)
        out = []
        for p in range(lo + 1, hi):
            if self.louds.get(p):
                child = self.louds.rank1(p) - 1
                out.append((child, self.labels[child - 1]))
        return out

    def walk(self, codes):
        v = 0
        for code in codes:
            for child, label in self.children(v):
                if label == code:
                    v = child
                    break
                if label > code:
                    return -1
            else:
                return -1
        return v

    def is_terminal(self, v: int) -> bool:
        return self.terminal.get(v) == 1

    def terminal_rank(self, v: int) -> int:
        return self.terminal.rank1(v - 1)

    def node_of_terminal(self, r: int) -> int:
        return self.terminal.select1(r + 1)

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


class Bidf2:
    def __init__(self, path: str):
        self.data = open(path, "rb").read()
        (magic, ver, n_entries, n_readings, n_surfaces, cb_off, rd_off,
         sf_off, tok_off, conn_off) = HDR.unpack_from(self.data, 0)
        assert magic == b"BIDF" and ver == 2, (magic, ver)
        self.n_entries, self.n_readings, self.n_surfaces = n_entries, n_readings, n_surfaces
        self.cb_off, self.rd_off, self.sf_off = cb_off, rd_off, sf_off
        self.tok_off, self.conn_off = tok_off, conn_off
        p = cb_off
        (nchars,) = struct.unpack_from("<I", self.data, p)
        p += 4
        self.book = []
        for _ in range(nchars):
            ln = self.data[p]
            p += 1
            self.book.append(self.data[p:p + ln].decode("utf-8"))
            p += ln
        self.char_to_code = {ch: i for i, ch in enumerate(self.book)}
        self.reading_trie = Trie(self.data, rd_off, has_counts=True)
        self.surface_trie = Trie(self.data, sf_off, has_counts=False)
        assert self.reading_trie.end == sf_off, (self.reading_trie.end, sf_off)
        assert self.surface_trie.end == tok_off, (self.surface_trie.end, tok_off)

    def encode(self, s: str):
        return [self.char_to_code[ch] for ch in s]

    def decode(self, codes):
        return "".join(self.book[c] for c in codes)

    def token_group(self, rank: int):
        counts = self.reading_trie.counts
        samples = self.reading_trie.samples
        first = samples[rank // SAMPLE_BLOCK] + sum(
            counts[(rank // SAMPLE_BLOCK) * SAMPLE_BLOCK:rank])
        p = self.tok_off
        # skip to the group: walk tokens sequentially is O(n); instead
        # parse from the start of the tokens section each time is too
        # slow for 116k lookups, so cache the parsed token stream.
        return first, counts[rank]

    def _parse_tokens(self):
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

    def candidates(self, reading: str):
        v = self.reading_trie.walk(self.encode(reading))
        if v < 0 or not self.reading_trie.is_terminal(v):
            return []
        rank = self.reading_trie.terminal_rank(v)
        first, count = self.token_group(rank)
        if not hasattr(self, "_toks"):
            self._toks = self._parse_tokens()
        return [self._decode_token(t) for t in self._toks[first:first + count]]

    def _decode_token(self, t):
        sid, pos, cost = t
        return (self.surface_of_id(sid), pos, cost)

    def surface_of_id(self, sid: int) -> str:
        node = self.surface_trie.node_of_terminal(sid)
        return self.decode(self.surface_trie.label_path(node))

    def readings_with_prefix(self, prefix: str):
        v = self.reading_trie.walk(self.encode(prefix))
        if v < 0:
            return []
        out = []
        stack = [(v, list(self.encode(prefix)))]
        while stack:
            node, path = stack.pop()
            if self.reading_trie.is_terminal(node):
                out.append(self.decode(path))
            for child, label in self.reading_trie.children(node):
                stack.append((child, path + [label]))
        return out


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: test_bidict2.py words.tsv dict.bidf [--c-tool ./bidict_lookup2]")
        return 2
    words_path, bidf_path = sys.argv[1], sys.argv[2]
    c_tool = None
    if "--c-tool" in sys.argv:
        c_tool = sys.argv[sys.argv.index("--c-tool") + 1]

    truth = defaultdict(list)
    surfaces = set()
    prefixes = set()
    for line in open(words_path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        r, s, pos, cost = line.split("\t")
        truth[r].append((s, int(pos), int(cost)))
        surfaces.add(s)
        for i in (1, 2):
            if len(r) >= i:
                prefixes.add(r[:i])
    for r in truth:
        truth[r] = sorted(truth[r], key=lambda e: (e[2], e[0], e[1]))

    d = Bidf2(bidf_path)
    print(f"loaded {bidf_path}: {d.n_readings} readings, "
          f"{d.n_entries} entries, {d.n_surfaces} surfaces, "
          f"{len(d.data)} bytes")
    assert d.n_readings == len(truth), (d.n_readings, len(truth))
    assert d.n_surfaces == len(surfaces)

    fails = 0
    # 1. exact lookup for every reading
    for r, want in truth.items():
        got = sorted(d.candidates(r), key=lambda e: (e[2], e[0], e[1]))
        if got != want:
            fails += 1
            if fails <= 5:
                print(f"FAIL exact {r!r}: got {got[:4]} want {want[:4]}")
    print(f"exact lookup: {len(truth) - fails}/{len(truth)} readings OK")

    # 2. prefix search
    pfails = 0
    for pre in sorted(prefixes):
        want = sorted(r for r in truth if r.startswith(pre))
        got = sorted(d.readings_with_prefix(pre))
        if got != want:
            pfails += 1
            if pfails <= 5:
                print(f"FAIL prefix {pre!r}: got {len(got)} want {len(want)}")
    print(f"prefix search: {len(prefixes) - pfails}/{len(prefixes)} prefixes OK")

    # 3. surface round-trip
    decoded = {d.surface_of_id(i) for i in range(d.n_surfaces)}
    sfails = 0 if decoded == surfaces else 1
    print(f"surface round-trip: {'OK' if decoded == surfaces else 'FAIL'} "
          f"({len(decoded)} decoded, {len(surfaces)} expected)")
    if decoded != surfaces:
        print("  missing:", list(surfaces - decoded)[:5])
        print("  extra:", list(decoded - surfaces)[:5])

    # 4. size report
    total = len(d.data)
    budget_ok = total <= 3.5 * 1024 * 1024
    print(f"size: {total} bytes ({total / 1024 / 1024:.2f} MB) "
          f"target 2.3-2.8 MB, hard 3.5 MB -> {'OK' if budget_ok else 'OVER'}")
    print(f"  sections: codebook {d.rd_off - d.cb_off}  "
          f"reading-trie {d.sf_off - d.rd_off}  "
          f"surface-trie {d.tok_off - d.sf_off}  "
          f"tokens {d.conn_off - d.tok_off}  conn {len(d.data) - d.conn_off}")
    print(f"  reading-trie nodes {d.reading_trie.n_nodes}  "
          f"surface-trie nodes {d.surface_trie.n_nodes}")

    # 5. optional C tool cross-check on a sample
    if c_tool:
        import random
        random.seed(42)
        sample = random.sample(sorted(truth), min(60, len(truth)))
        cfails = 0
        for r in sample:
            want = sorted(d.candidates(r), key=lambda e: (e[2], e[0], e[1]))
            out = subprocess.run([c_tool, bidf_path, r], capture_output=True,
                                 text=True)
            got = []
            for line in out.stdout.splitlines():
                s, pos, cost = line.split("\t")
                got.append((s, int(pos), int(cost)))
            got = sorted(got, key=lambda e: (e[2], e[0], e[1]))
            if got != want:
                cfails += 1
                print(f"FAIL C-tool {r!r}: got {got[:3]} want {want[:3]}")
        # prefix sample
        for pre in random.sample(sorted(prefixes), min(10, len(prefixes))):
            want = sorted(d.readings_with_prefix(pre))
            out = subprocess.run([c_tool, bidf_path, "-p", pre],
                                 capture_output=True, text=True)
            got = sorted(line.split(" => ")[0] for line in out.stdout.splitlines())
            if got != want:
                cfails += 1
                print(f"FAIL C-tool prefix {pre!r}: got {len(got)} want {len(want)}")
        print(f"C-tool cross-check: {'OK' if cfails == 0 else f'{cfails} FAILURES'}")

    ok = fails == 0 and pfails == 0 and sfails == 0 and budget_ok
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
