#!/usr/bin/env python3
"""Cross-check the engine's dictionary layer against the original
bidict_lookup2 CLI on a set of readings (exact candidate lists,
including order, POS class and cost must be identical).  This
guards the LOUDS/token decoding the converter is built on.

usage: check_lookup.py <bi-binary> <dict.bidf> [bidict_lookup2-binary]
The reference binary is built from buildbrain's tools/ime if the
path is not given.
"""
import subprocess
import sys
import os

READINGS = ["あ", "かんじ", "とうきょう", "ました", "を", "です",
            "がっこう", "にほんご", "たべる", "あいうえお", "ん",
            "さいたま", "よこはまし", "ゎ", "きょうとふ"]

def run(binary, dictpath, reading):
    out = subprocess.run([binary, dictpath, reading] if "lookup2" in binary
                         else [binary, dictpath, "lookup", reading],
                         capture_output=True, text=True)
    return out.stdout

def main():
    bi, dictpath = sys.argv[1], sys.argv[2]
    ref = sys.argv[3] if len(sys.argv) > 3 else "/tmp/bidict_lookup2"
    if not os.path.exists(ref):
        src = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "..", "..", "buildbrain", "tools",
                           "ime", "bidict_lookup2.c")
        src = os.path.normpath(src)
        subprocess.run(["gcc", "-O2", "-o", ref, src], check=True)
    bad = 0
    for r in READINGS:
        a = run(bi, dictpath, r)
        b = run(ref, dictpath, r)
        if a != b:
            bad += 1
            print(f"MISMATCH {r}:\n  bi: {a!r}\n  ref: {b!r}")
    print(f"lookup equivalence: {len(READINGS)-bad}/{len(READINGS)} identical")
    return 1 if bad else 0

if __name__ == "__main__":
    raise SystemExit(main())
