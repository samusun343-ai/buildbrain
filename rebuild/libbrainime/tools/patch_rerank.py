#!/usr/bin/env python3
"""Write trained reranker weights into a BIDF v3 file's RNK
section, in place, and refresh the section CRC in the directory.

usage: patch_rerank.py <file.bid3> <weights.bin>
(weights.bin = BI_RERANK_NFEAT float32 values, as written by
tools/train_rerank.c)
"""
import struct
import sys
import zlib

RNK_ID = 7
FEAT_VER = 3


def main():
    path, wpath = sys.argv[1], sys.argv[2]
    weights = open(wpath, "rb").read()
    if len(weights) % 4:
        raise SystemExit("weights file size not a multiple of 4")
    nfeat = len(weights) // 4
    data = bytearray(open(path, "rb").read())
    if data[:4] != b"BID3":
        raise SystemExit("not a BID3 file")
    nsec = struct.unpack_from("<I", data, 8)[0]
    for i in range(nsec):
        eoff = 16 + 16 * i
        sid, off, ln = struct.unpack_from("<III", data, eoff)
        if sid != RNK_ID:
            continue
        struct.pack_into("<II", data, off, FEAT_VER, nfeat)
        data[off + 8:off + 8 + len(weights)] = weights
        crc = zlib.crc32(bytes(data[off:off + ln])) & 0xFFFFFFFF
        struct.pack_into("<I", data, eoff + 12, crc)
        open(path, "wb").write(bytes(data))
        print(f"patched RNK: {nfeat} weights, section crc {crc:08x}")
        return
    raise SystemExit("no RNK section found")


if __name__ == "__main__":
    main()
