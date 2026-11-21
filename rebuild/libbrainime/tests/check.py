#!/usr/bin/env python3
"""Run the sentence regression corpus through the bi CLI and
report top-1 exact matches, plus whether the expected string
appears in the n-best list or can be assembled from the segment
candidates (candidate recall).  Failures are printed verbatim —
classification into dict-gap / cost-model / segmentation is a
human step recorded in REPORT-m1.md, not automated here.

usage: check.py <bi-binary> <dict.bidf> [sentences.tsv]
"""
import subprocess
import sys
import os

def convert(bi, dictpath, reading):
    out = subprocess.run([bi, dictpath, "convert", reading],
                         capture_output=True, text=True)
    best = None
    nbest = []
    cands_ok = False
    seg_surfaces = []
    seg_cands = []
    for line in out.stdout.splitlines():
        if line.startswith("best: "):
            best = line[6:]
        elif " nbest" in line and ": " in line:
            nbest.append(line.split(": ", 1)[1].rsplit(" (cost", 1)[0])
        elif line.startswith("  [") and " cands:" in line:
            seg_surfaces.append(line.split(" -> ", 1)[1]
                                .split(" (pos", 1)[0])
            seg_cands.append(line.split(" cands:", 1)[1].split())
    return best, nbest, seg_surfaces, seg_cands

def main():
    bi, dictpath = sys.argv[1], sys.argv[2]
    tsv = sys.argv[3] if len(sys.argv) > 3 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "sentences.tsv")
    rows = []
    for line in open(tsv, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        rows.append(line.split("\t"))
    npass = nbest_hit = cand_hit = 0
    fails = []
    for reading, expected, *cat in rows:
        best, nbest, seg_surfaces, seg_cands = convert(bi, dictpath,
                                                       reading)
        if best == expected:
            npass += 1
            continue
        in_nbest = expected in nbest
        # candidate recall: expected can be formed by picking one
        # candidate per segment (same segmentation)
        in_cands = False
        if seg_cands and len(seg_cands) == len(seg_surfaces):
            # try to segment the expected string by candidate lists
            pos = 0
            ok = True
            for cands in seg_cands:
                matched = None
                for cand in cands:
                    if expected.startswith(cand, pos):
                        if matched is None or len(cand) > len(matched):
                            matched = cand
                if matched is None:
                    ok = False
                    break
                pos += len(matched)
            in_cands = ok and pos == len(expected)
        if in_nbest:
            nbest_hit += 1
        if in_cands:
            cand_hit += 1
        fails.append((reading, expected, best, in_nbest, in_cands,
                      cat[0] if cat else ""))
    total = len(rows)
    print(f"top-1 exact: {npass}/{total} = {npass/total:.1%}")
    print(f"of the {total-npass} failures: expected in n-best: "
          f"{nbest_hit}, assemblable from segment candidates: {cand_hit}")
    print()
    for reading, expected, best, nb, cd, cat in fails:
        print(f"FAIL [{cat}] {reading}")
        print(f"  expected: {expected}")
        print(f"  got:      {best}")
        print(f"  n-best contains expected: {nb}; "
              f"candidate-assemblable: {cd}")
    return 0 if npass == total else 1

if __name__ == "__main__":
    raise SystemExit(main())
