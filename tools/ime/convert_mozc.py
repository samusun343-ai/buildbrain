#!/usr/bin/env python3
"""Convert mozc dictionary_oss into the TSV input of build_bidict.py.

Usage:
    convert_mozc.py <dictionary_oss_dir> <out.tsv> [--target N] [--db PATH]

Input (from https://github.com/google/mozc, src/data/dictionary_oss;
licences recorded in MOZC-NOTICE.md next to this script):
    dictionary00.txt … dictionary09.txt
        one entry per line:  reading <TAB> lid <TAB> rid <TAB> cost <TAB> surface
        (reading is hiragana; cost is mozc's log-domain word cost)
    id.def
        "<lid> POS,sub1,sub2,sub3,conj_type,conj_form,base" per line
    dictionary_filter.tsv
        key/value regex pairs mozc itself removes; applied here too.

Output TSV columns (build_bidict.py input):
    reading <TAB> surface <TAB> pos_class(0-255) <TAB> cost(0-255)

Processing notes
----------------
POS classes: mozc has 2,672 IPADIC part-of-speech ids — far too many
for BIDF's 1-byte class and its 256x256 connection matrix. Each id's
POS string is compressed by the ordered rule table in classify()
below (~50 classes). The split keeps what a Viterbi converter needs
to tell apart: noun subtypes (general / proper-person / place /
organisation / sahen / numeral / suffix / pronoun …), verb
conjugation families (godan / ichidan / sahen / kahen / irregular),
adjectives, and the particle subtypes (case / binding / adverbial /
conjunctive / final / …). The token's class comes from the entry's
*lid* (left id); rid is dropped in v1 (lid == rid for most entries,
and the single class is the left-context class the connection
matrix will key on).

Cost quantisation: mozc's cost is already a log-domain quantity
(it is derived from -log(corpus probability), scaled), so the
additive spacing Viterbi depends on is preserved by a LINEAR rescale
into 0..255 — taking log(cost) again would double-compress and
destroy the relative spacing of common words. Bounds: lo = 0 (mozc
costs are >= 0); hi = the 99th percentile of all deduplicated
entries, so the rarest 1% of outliers saturate at 255 instead of
compressing the resolution of the common range. The scale depends
only on the source dictionary, not on --target, so a word's
quantised cost is stable across builds. Measured on the 2026-11
mozc snapshot: hi = 11895, i.e. one step ~= 47 mozc cost units.

Selection (~--target entries, default 180000):
  1. Drop dictionary_filter.tsv matches and entries whose reading
     is not kana-only (unusable as kana-kanji input).
  2. Normalise readings katakana -> hiragana (mozc readings are
     already hiragana except a handful; normalising keeps lookup
     uniform).
  3. Deduplicate on (reading, surface, pos_class), keeping the
     minimum cost.
  4. Force-include every single-kanji entry (surface = one CJK
     ideograph): the design's coverage floor requires all jōyō +
     jinmeiyō kanji as single-kanji entries regardless of cost.
  5. Fill the remaining slots with the lowest-cost entries.
     mozc cost tracks corpus frequency, so this keeps the common
     vocabulary; the known bias is towards frequent loanwords and
     proper nouns, accepted for v1.
All heavy lifting is done in a disk-backed sqlite database so the
converter runs in a few hundred MB of RAM. The temp DB is removed
unless --db names a path to keep.
"""
import argparse
import os
import re
import sqlite3
import sys
from collections import Counter

# ---------------------------------------------------------------- POS classes
# Class ids (BIDF, 0-255). Keep in sync with the table in docs/ime.md.
CLS_UNKNOWN = 0
CLASS_NAMES = {
    0: "その他/未分類",
    1: "名詞-一般", 2: "名詞-固有名詞-人名", 3: "名詞-固有名詞-地域",
    4: "名詞-固有名詞-組織", 5: "名詞-固有名詞-その他",
    6: "名詞-サ変接続", 7: "名詞-形容動詞語幹", 8: "名詞-数",
    9: "名詞-接尾-一般", 10: "名詞-接尾-助数詞", 11: "名詞-接尾-人名地域",
    12: "名詞-代名詞", 13: "名詞-副詞可能", 14: "名詞-非自立",
    15: "名詞-接続詞的", 16: "名詞-その他",
    20: "動詞-五段", 21: "動詞-一段", 22: "動詞-サ変", 23: "動詞-カ変",
    24: "動詞-ラ変/特殊", 25: "動詞-非自立", 26: "動詞-接尾", 27: "動詞-その他",
    30: "形容詞-自立", 31: "形容詞-非自立", 32: "形容詞-接尾",
    40: "副詞-一般", 41: "副詞-助詞類接続", 42: "連体詞", 43: "接続詞",
    44: "感動詞", 45: "フィラー", 46: "その他-間投",
    50: "接頭詞-名詞接続", 51: "接頭詞-動詞接続", 52: "接頭詞-形容詞接続",
    53: "接頭詞-数接続", 54: "接頭詞-その他",
    60: "助詞-格助詞", 61: "助詞-係助詞", 62: "助詞-副助詞",
    63: "助詞-接続助詞", 64: "助詞-終助詞", 65: "助詞-副詞化",
    66: "助詞-連体化", 67: "助詞-並立助詞", 68: "助詞-特殊",
    69: "助詞-副助詞/並立/終助詞", 70: "助詞-その他", 71: "助動詞",
    80: "記号-一般", 81: "記号-アルファベット", 82: "記号-括弧開",
    83: "記号-括弧閉", 84: "記号-句点", 85: "記号-読点", 86: "記号-空白",
    87: "記号-その他",
}


def classify(pos: str) -> int:
    """Map one id.def POS string to a BIDF class. Ordered rules,
    first match wins. Fields: major, sub1, sub2, sub3, conj_type,
    conj_form, base (comma-separated, '*' = unspecified)."""
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
            return 9  # 一般/サ変/形容動詞語幹/副詞化/特殊
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
            # 五段・ラ行特殊 conjugates like a godan verb but its
            # imperative/euphony differ; group with the irregulars.
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
    return 0  # BOS/EOS and anything unforeseen


# ------------------------------------------------------------------ readings
def _kana_ok(ch: str) -> bool:
    o = ord(ch)
    return (0x3041 <= o <= 0x3096 or 0x30A1 <= o <= 0x30F6
            or o in (0x309D, 0x309E, 0x30FC))  # ゝ ゞ ー


def normalize_reading(r: str) -> str:
    """Katakana -> hiragana (ァ-ヶ shift by -0x60); ー stays."""
    return "".join(chr(ord(c) - 0x60) if 0x30A1 <= ord(c) <= 0x30F6 else c
                   for c in r)


def is_single_kanji(s: str) -> bool:
    if len(s) != 1:
        return False
    o = ord(s)
    return 0x4E00 <= o <= 0x9FFF or 0x3400 <= o <= 0x4DBF


# ---------------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mozc_dir", help="mozc src/data/dictionary_oss directory")
    ap.add_argument("out", help="output TSV for build_bidict.py")
    ap.add_argument("--target", type=int, default=180000,
                    help="entry budget including forced single-kanji (default 180000)")
    ap.add_argument("--db", default=None,
                    help="temp sqlite path (default: <out>.db, removed afterwards)")
    args = ap.parse_args()

    d = args.mozc_dir
    # --- id.def: lid -> class
    lid_class: dict[int, int] = {}
    unmapped: Counter = Counter()
    with open(os.path.join(d, "id.def"), encoding="utf-8") as fp:
        for line in fp:
            lid_s, pos = line.rstrip("\n").split(" ", 1)
            cls = classify(pos)
            if cls == 0 and not pos.startswith(("BOS/EOS", "その他")):
                unmapped[pos] += 1
            lid_class[int(lid_s)] = cls
    print(f"id.def: {len(lid_class)} ids -> {len(set(lid_class.values()))} classes"
          f"; unmapped POS strings: {sum(unmapped.values())}")

    # --- dictionary_filter.tsv: mozc's own removal list
    filters = []
    fpath = os.path.join(d, "dictionary_filter.tsv")
    if os.path.exists(fpath):
        with open(fpath, encoding="utf-8") as fp:
            for line in fp:
                line = line.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                k, v = line.split("\t")
                filters.append((re.compile(k), re.compile(v)))
    print(f"dictionary_filter: {len(filters)} patterns")

    db_path = args.db or (args.out + ".db")
    if os.path.exists(db_path):
        os.unlink(db_path)
    db = sqlite3.connect(db_path)
    db.execute("PRAGMA journal_mode=OFF")
    db.execute("PRAGMA synchronous=OFF")
    db.execute("CREATE TABLE posmap_t(lid INTEGER PRIMARY KEY, cls INTEGER)")
    db.executemany("INSERT INTO posmap_t VALUES (?,?)", lid_class.items())
    db.execute("CREATE TABLE raw(reading TEXT, surface TEXT, lid INTEGER,"
               " cost INTEGER, kanji INTEGER)")

    n_raw = n_filtered = n_non_kana = n_bad = 0
    batch = []
    import glob
    for fn in sorted(glob.glob(os.path.join(d, "dictionary[0-9][0-9].txt"))):
        with open(fn, encoding="utf-8") as fp:
            for line in fp:
                n_raw += 1
                parts = line.rstrip("\n").split("\t")
                if len(parts) < 5:
                    n_bad += 1
                    continue
                reading, lid_s, _rid, cost_s, surface = parts[:5]
                if any(krx.fullmatch(reading) and vrx.fullmatch(surface)
                       for krx, vrx in filters):
                    n_filtered += 1
                    continue
                if not reading or not all(_kana_ok(c) for c in reading):
                    n_non_kana += 1
                    continue
                try:
                    lid, cost = int(lid_s), int(cost_s)
                except ValueError:
                    n_bad += 1
                    continue
                batch.append((normalize_reading(reading), surface, lid,
                              cost, 1 if is_single_kanji(surface) else 0))
                if len(batch) >= 20000:
                    db.executemany("INSERT INTO raw VALUES (?,?,?,?,?)", batch)
                    batch.clear()
    if batch:
        db.executemany("INSERT INTO raw VALUES (?,?,?,?,?)", batch)
    db.commit()
    n_nolid = db.execute(
        "SELECT COUNT(*) FROM raw WHERE lid NOT IN (SELECT lid FROM posmap_t)"
    ).fetchone()[0]
    print(f"raw lines {n_raw}: filtered {n_filtered}, non-kana reading "
          f"{n_non_kana}, malformed {n_bad}, unknown lid {n_nolid}")

    db.execute(
        "CREATE TABLE dedup AS SELECT r.reading AS reading, r.surface AS surface,"
        " p.cls AS cls, MIN(r.cost) AS cost, MAX(r.kanji) AS kanji"
        " FROM raw r JOIN posmap_t p ON p.lid = r.lid"
        " GROUP BY r.reading, r.surface, p.cls")
    db.commit()
    n_dedup = db.execute("SELECT COUNT(*) FROM dedup").fetchone()[0]
    n_kanji = db.execute("SELECT COUNT(*) FROM dedup WHERE kanji=1").fetchone()[0]
    print(f"deduped entries: {n_dedup} (single-kanji: {n_kanji})")

    # quantisation bound: 99th percentile of deduped costs
    hi = db.execute("SELECT cost FROM dedup ORDER BY cost LIMIT 1 OFFSET ?",
                    (int(n_dedup * 0.99),)).fetchone()[0]
    print(f"cost quantisation: linear 0..{hi} -> 0..255 (hi = p99 of deduped)")

    # selection: all single-kanji + cheapest fill up to --target
    db.execute("CREATE TABLE sel AS SELECT * FROM dedup WHERE kanji=1")
    room = max(args.target - n_kanji, 0)
    db.execute("INSERT INTO sel SELECT * FROM dedup WHERE kanji=0"
               " ORDER BY cost ASC LIMIT ?", (room,))
    db.commit()
    n_sel = db.execute("SELECT COUNT(*) FROM sel").fetchone()[0]
    n_read = db.execute("SELECT COUNT(DISTINCT reading) FROM sel").fetchone()[0]
    max_cand = db.execute(
        "SELECT MAX(c) FROM (SELECT COUNT(*) c FROM sel GROUP BY reading)"
    ).fetchone()[0]
    cutoff = db.execute("SELECT MAX(cost) FROM sel WHERE kanji=0").fetchone()[0]

    def q(cost: int) -> int:
        return min(255, round(cost * 255 / hi)) if hi else 0

    hist = Counter()
    with open(args.out, "w", encoding="utf-8") as fp:
        fp.write("# reading\tsurface\tpos_class\tcost  "
                 "(from mozc dictionary_oss via convert_mozc.py)\n")
        for reading, surface, cls, cost in db.execute(
                "SELECT reading, surface, cls, cost FROM sel"
                " ORDER BY reading, cost, surface"):
            hist[cls] += 1
            fp.write(f"{reading}\t{surface}\t{cls}\t{q(cost)}\n")
    db.close()
    if not args.db:
        os.unlink(db_path)

    print(f"selected: {n_sel} entries, {n_read} readings, "
          f"max candidates/reading {max_cand}, fill cost cutoff {cutoff}")
    print("top classes:", ", ".join(
        f"{c}:{CLASS_NAMES.get(c, '?')}={n}" for c, n in hist.most_common(8)))
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
