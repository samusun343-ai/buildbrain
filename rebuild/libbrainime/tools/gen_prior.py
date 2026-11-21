#!/usr/bin/env python3
"""M7: surface prior table from the M6 65k-sentence corpus.

Unigram (reading, surface) counts are obtained by marginalising
m6work/bigram_counts_m6.tsv over its "current" side: in that file
every token occurrence of the guarded 65k corpus appears exactly
once as a bigram's current element (sentence-initial tokens via
the (<BOS>,<BOS>) pseudo-pair), so the marginal IS the exact
unigram count of the same corpus that produced the M6 BIGR
section -- same sentences, same leakage guards, no re-tokenising.

For each dictionary reading r with enough corpus evidence, the
prior redistributes word cost WITHIN the reading's candidate set:

    p^(s|r) = (c(r,s) + alpha) / (sum_{s' in dict(r)} c(r,s')
                                   + alpha * |dict(r)|)
    delta_units(s) = -lam * (ln p^(s|r) - sum_s p^ * ln p^)
    delta_bytes(s) = clamp(round(delta_units / 16), +-cap)

The p^-weighted centring keeps each reading's expected total cost
unchanged: mozc's cost byte keeps governing BETWEEN readings,
the prior only expresses the corpus' WITHIN-reading preference
among surfaces (the M5/M6 "surface choice" failure class, 68% of
held-out misses).  Readings whose dictionary-surface corpus mass
is below --cmin get no adjustment (evidence too thin).

Output: prior.tsv lines  reading \\t surface \\t delta_bytes
(non-zero only), consumed by build_bidict3.py --prior.
"""
import argparse, math, os, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from gen_sidecar import Bidf2  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bigram", default=os.path.join(
        ROOT, "m6work", "bigram_counts_m6.tsv"))
    ap.add_argument("--bidf", default=os.path.expanduser(
        "~/workspace/buildbrain-work/ime-dict/brainux2.bidf"))
    ap.add_argument("--lam", type=float, required=True,
                    help="engine cost units per nat")
    ap.add_argument("--gamma", type=float, default=1.0,
                    help="damping for the cross-script-class term "
                    "(1.0 = plain prior; 0 = prior never moves "
                    "the kanji-vs-kana class balance, it only "
                    "ranks surfaces WITHIN a script class).  The "
                    "corpus (Tatoeba) has a colloquial style bias "
                    "toward kana forms (watashi over watakushi-kanji "
                    "etc.); the class term is where that bias lives, "
                    "the within-class term is genuine usage "
                    "frequency among same-register variants.")
    ap.add_argument("--attested-min", type=int, default=0,
                    help="if >0, only surfaces with corpus count "
                    ">= this under the SAME reading join the prior "
                    "competition; other surfaces get delta 0.  "
                    "Guards against reading-key mismatches between "
                    "the corpus teacher (Sudachi) and the "
                    "dictionary (mozc): e.g. Sudachi reads the "
                    "pronoun kanji as WATAKUSHI while the "
                    "dictionary registers it under WATASHI, so its "
                    "same-reading count is 0 and the alpha floor "
                    "would maximally demote the standard form.  "
                    "Absence under a reading is not dispreference "
                    "when the two resources assign readings "
                    "differently.")
    ap.add_argument("--anchor", action="store_true",
                    help="champion-anchored mode: the most frequent "
                    "surface of each script class keeps delta 0 and "
                    "every other surface only receives a DEMOTION "
                    "(-lam * (ln P(s|class) - ln P(champion|class)) "
                    ">= 0).  The prior then never promotes anything "
                    "over mozc's own class champions: cross-class "
                    "outcomes stay mozc's, and the corpus can only "
                    "veto variants it dislikes within a register. "
                    "(The plain/gamma modes also BONUS the kana "
                    "champion of pronouns etc., which transfers "
                    "Tatoeba's colloquial style into the word cost "
                    "and flipped watakushi-kanji -> watashi-kana on "
                    "the hand-written corpora.)")
    ap.add_argument("--alpha", type=float, default=1.0)
    ap.add_argument("--cmin", type=int, default=30,
                    help="min corpus mass over dict surfaces")
    ap.add_argument("--cap", type=int, default=48,
                    help="max |delta| in cost bytes")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    # ---- unigram counts by marginalising the bigram table ----
    uni = defaultdict(int)
    total = 0
    for line in open(args.bigram, encoding="utf-8"):
        pr, ps, cr, cs, n = line.rstrip("\n").split("\t")
        if cr in ("<BOS>", "<EOS>"):
            continue
        uni[(cr, cs)] += int(n)
        total += int(n)
    print(f"unigram: {len(uni)} distinct (r,s), {total} tokens "
          f"from {args.bigram}")

    # ---- dictionary tokens grouped by reading -----------------
    bidf = Bidf2(args.bidf)
    by_reading = defaultdict(list)   # reading -> [(surface, costb)]
    ntok = 0
    for reading, surface, pos, costb in bidf.tokens_in_order():
        by_reading[reading].append((surface, costb))
        ntok += 1
    print(f"dict: {ntok} tokens, {len(by_reading)} readings")

    # ---- prior deltas ------------------------------------------
    deltas = {}          # (reading, surface) -> delta bytes
    n_read_adj = 0
    # pooled diagnostic: does mozc's byte already encode ln p^?
    # regress (byte*16) on -ln p^ over adjusted groups.
    reg_x, reg_y = [], []
    for r, toks in by_reading.items():
        surfaces = sorted({s for s, _ in toks})
        if args.attested_min > 0:
            surfaces = [s for s in surfaces
                        if uni.get((r, s), 0) >= args.attested_min]
        if len(surfaces) < 2:
            continue
        mass = sum(uni.get((r, s), 0) for s in surfaces)
        if mass < args.cmin:
            continue
        denom = mass + args.alpha * len(surfaces)
        phat = {s: (uni.get((r, s), 0) + args.alpha) / denom
                for s in surfaces}

        def script_class(s):
            return "K" if any(0x3400 <= ord(c) <= 0x9FFF or
                               0xF900 <= ord(c) <= 0xFAFF or
                               c in "々〆" for c in s) else "k"

        cls_mass = {}
        for s in surfaces:
            cls_mass[script_class(s)] = \
                cls_mass.get(script_class(s), 0.0) + phat[s]
        centre_class = sum(p * math.log(p) for p in cls_mass.values())
        centre_within = {}
        for cl in cls_mass:
            mem = [s for s in surfaces if script_class(s) == cl]
            centre_within[cl] = sum(
                (phat[s] / cls_mass[cl]) *
                math.log(phat[s] / cls_mass[cl]) for s in mem)
        n_read_adj += 1
        # representative byte per surface (min over POS variants)
        byte_of = {}
        for s, b in toks:
            if s not in byte_of or b < byte_of[s]:
                byte_of[s] = b
        champ = {}
        for cl in cls_mass:
            champ[cl] = max((s for s in surfaces
                             if script_class(s) == cl),
                            key=lambda s: phat[s])
        for s in surfaces:
            cl = script_class(s)
            if args.anchor:
                du = -args.lam * (math.log(phat[s] / cls_mass[cl])
                                  - math.log(phat[champ[cl]]
                                             / cls_mass[cl]))
            else:
                within = math.log(phat[s] / cls_mass[cl]) \
                    - centre_within[cl]
                across = math.log(cls_mass[cl]) - centre_class
                du = -args.lam * (within + args.gamma * across)
            db = max(-args.cap, min(args.cap, round(du / 16)))
            if db != 0:
                deltas[(r, s)] = db
            reg_x.append(-math.log(phat[s]))
            reg_y.append(byte_of[s] * 16)
    n_tok_adj = 0
    for r, toks in by_reading.items():
        for s, _ in toks:
            if (r, s) in deltas:
                n_tok_adj += 1
    if reg_x:
        mx = sum(reg_x) / len(reg_x)
        my = sum(reg_y) / len(reg_y)
        sxy = sum((x - mx) * (y - my) for x, y in zip(reg_x, reg_y))
        sxx = sum((x - mx) ** 2 for x in reg_x)
        slope = sxy / sxx if sxx else 0.0
        print(f"diagnostic: mozc byte*16 vs -ln p^ pooled slope "
              f"= {slope:.1f} units/nat over {len(reg_x)} surfaces "
              f"(>0: mozc already encodes part of the preference; "
              f"lam={args.lam} adds on top)")
    print(f"prior: {n_read_adj} readings adjusted, "
          f"{len(deltas)} (r,s) deltas, {n_tok_adj} tokens affected")
    with open(args.out, "w", encoding="utf-8") as f:
        for (r, s), db in sorted(deltas.items()):
            f.write(f"{r}\t{s}\t{db}\n")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
