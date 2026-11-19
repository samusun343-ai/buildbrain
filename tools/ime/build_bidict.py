#!/usr/bin/env python3
"""Build a BIDF v1 dictionary from a TSV word list.

Input TSV columns: reading <TAB> surface <TAB> pos_class(0-255) <TAB> cost(0-255)
Lines starting with # are comments. Output: a single mmap-able file:

  header   64 bytes: magic BIDF, version=1, entry_count,
           index_off, tokens_off, pool_off, conn_off (256*256 int8)
  index    per distinct reading: pool_off(reading) u32, first_token u32,
           token_count u16, pad u16   (sorted by reading; binary search)
  tokens   per entry: pool_off(surface) u32, pos u8, cost u8, pad u16
           (grouped by reading, sorted by cost within the group)
  pool     NUL-terminated UTF-8 strings (readings and surfaces, deduped)
  conn     256*256 int8 POS-class connection costs (zeros in v1 builds
           unless a --conn TSV of 'left right cost' lines is given)

v1 uses a sorted index; v2 will replace it with LOUDS tries
(see docs/ime.md). The on-disk token/pool/conn layout is final.
"""
import struct
import sys
from collections import defaultdict

HDR = struct.Struct("<4sIIIIIII")  # magic, ver, token_count, idx, tok, pool, conn, reading_count
IDX = struct.Struct("<IIHH")
TOK = struct.Struct("<IBBH")


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: build_bidict.py words.tsv out.bidf [--conn conn.tsv]")
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
    with open(words_path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            reading, surface, pos, cost = line.split("\t")
            by_reading[reading].append((surface, int(pos), int(cost)))

    pool = bytearray(b"\0")
    pool_off: dict[str, int] = {"": 0}

    def intern(s: str) -> int:
        if s not in pool_off:
            pool_off[s] = len(pool)
            pool.extend(s.encode("utf-8") + b"\0")
        return pool_off[s]

    readings = sorted(by_reading)
    index = []
    tokens = []
    for reading in readings:
        entries = sorted(by_reading[reading], key=lambda e: e[2])
        first = len(tokens)
        for surface, pos, cost in entries:
            tokens.append((intern(surface), pos, cost))
        index.append((intern(reading), first, len(entries)))

    idx_off = HDR.size
    tok_off = idx_off + IDX.size * len(index)
    pool_off_abs = tok_off + TOK.size * len(tokens)
    # pool offsets in index/tokens are relative to the pool start; the
    # builder stored them pool-relative already, so just record sections.
    conn_off = pool_off_abs + len(pool)

    out = bytearray()
    out += HDR.pack(b"BIDF", 1, len(tokens), idx_off, tok_off, pool_off_abs, conn_off, len(index))
    for roff, first, count in index:
        out += IDX.pack(roff, first, count, 0)
    for soff, pos, cost in tokens:
        out += TOK.pack(soff, pos, cost, 0)
    out += pool
    out += conn
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"{out_path}: {len(readings)} readings, {len(tokens)} entries, "
          f"{len(out)} bytes ({len(out) / max(len(tokens),1):.1f} B/entry)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
