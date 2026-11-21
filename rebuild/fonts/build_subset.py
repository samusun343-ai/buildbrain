#!/usr/bin/env python3
"""Build the bundled Japanese subset font for libbrainui2 (M3).

Source font : Noto Sans CJK JP Regular (face 0 of NotoSansCJK-Regular.ttc)
License     : SIL Open Font License 1.1 (see /usr/share/doc/fonts-noto-cjk/
              copyright on the build host; the OFL text ships next to the
              output as LICENSE.brainui-jp.txt). The OFL Reserved Font
              Name "Noto" is not used for the subset: the file and the
              internal family name are renamed to "BrainUI JP".

Character set:
  * a fixed base set (ASCII, Latin-1 supplement, general punctuation,
    math operators, arrows, enclosed alphanumerics, box drawing,
    geometric shapes, CJK punctuation, hiragana, katakana,
    fullwidth/halfwidth forms)
  * every distinct codepoint used in the IME dictionary surfaces
    (ime-dict/words-mozc.tsv, the source of the shipping BIDF v2/v3
    dictionaries) - this is what decides which kanji make the cut
  * every codepoint in the BIDF v3 supplementary vocabulary
    (libbrainime/m3work/supp.tsv: NEologd + SudachiDict surfaces) -
    added in M4: the M3 build predated the SUPP merge, so kanji
    used only there (U+591B, U+5F0C, ...) were missing
  * every codepoint in the libbrainime regression/eval corpora
    (expected conversion outputs)
  * every non-ASCII codepoint appearing in the UI sources
    (src/, include/, shell/, apps/, osk/) - added in M4: UI strings
    are part of the coverage contract; U+2212 (MINUS SIGN, used by
    the calculator keypad and the assist volume key) was missing
    because no base range covered the math operators block

Coverage is verified against the source font's cmap and reported
per source set, both per-codepoint and per-entry (an entry counts
as covered when every one of its codepoints is in the subset).

Supplements (M4): a handful of charset codepoints do not exist in
Noto Sans CJK JP at all (Latin Extended / Greek symbol variants
used by the OSK symbol pages, superscripts/subscripts used by the
calculator, and the media symbols U+23ED/U+23EE/U+23F8 used by the
player/recorder). Two small fallback fonts are built alongside the
main subset and chained automatically by tk_font_load_bundled():
  * brainui-jp-supplement.ttf - from DejaVu Sans (DejaVu License,
    see fonts/NOTICE.supplement.txt): codepoints DejaVu has and
    Noto Sans CJK lacks
  * brainui-jp-symbols.ttf - from Noto Sans Symbols 2 (SIL OFL 1.1,
    same notice): the remaining codepoints (the media symbols)

Usage: python3 fonts/build_subset.py [--source PATH] [--out PATH]
Re-running reproduces the same subset from the same inputs.
"""
import argparse
import os
import sys

BASE_RANGES = [
    (0x0020, 0x007E),  # ASCII printable
    (0x00A0, 0x00FF),  # Latin-1 supplement (punctuation, x, div, yen)
    (0x2000, 0x206F),  # general punctuation
    (0x2190, 0x21FF),  # arrows
    (0x2200, 0x22FF),  # math operators (minus U+2212, root, inf, ...)
    (0x2460, 0x24FF),  # enclosed alphanumerics (circled numbers etc.)
    (0x2500, 0x257F),  # box drawing
    (0x25A0, 0x25FF),  # geometric shapes
    (0x2600, 0x266F),  # misc symbols (stars, circles, marks)
    (0x3000, 0x303F),  # CJK symbols and punctuation
    (0x3040, 0x309F),  # hiragana
    (0x30A0, 0x30FF),  # katakana
    (0x31F0, 0x31FF),  # katakana phonetic extensions
    (0xFF01, 0xFF9F),  # fullwidth forms + halfwidth kana
    (0xFFFD, 0xFFFD),  # replacement character
]


def base_charset():
    s = set()
    for lo, hi in BASE_RANGES:
        s.update(range(lo, hi + 1))
    return s


def dict_surfaces(path):
    """Yield surface strings from the mozc-derived dictionary TSV."""
    surfaces = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2:
                surfaces.append(parts[1])
    return surfaces


def supp_surfaces(path):
    """Yield surface strings (2nd column) from the BIDF v3 SUPP TSV."""
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2:
                out.append(parts[1])
    return out


def ui_codepoints(root):
    """Every non-ASCII codepoint in the UI sources (.c/.h under
    src/, include/, shell/, apps/, osk/). UI strings are rendered
    with this font, so their characters are part of the contract."""
    cps = set()
    for sub in ("src", "include", "shell", "apps", "osk"):
        d = os.path.join(root, sub)
        for dirpath, _dirs, files in os.walk(d):
            for name in files:
                if not name.endswith((".c", ".h")):
                    continue
                p = os.path.join(dirpath, name)
                try:
                    with open(p, encoding="utf-8", errors="strict") as f:
                        text = f.read()
                except (OSError, UnicodeDecodeError):
                    continue
                cps.update(ord(ch) for ch in text if ord(ch) >= 0x80)
    return cps


def corpus_surfaces(path):
    """Yield expected-output strings (2nd column) from a test TSV."""
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2:
                out.append(parts[1])
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--source",
                    default="/usr/share/fonts/opentype/noto/"
                            "NotoSansCJK-Regular.ttc")
    ap.add_argument("--out", default=os.path.join(here,
                                                  "brainui-jp-subset.ttf"))
    ap.add_argument("--words",
                    default=os.path.expanduser(
                        "~/workspace/buildbrain-work/ime-dict/words-mozc.tsv"))
    args = ap.parse_args()

    from fontTools.ttLib import TTFont
    from fontTools import subset

    charset = base_charset()
    surfaces = dict_surfaces(args.words)
    dict_cps = set()
    for s in surfaces:
        dict_cps.update(ord(ch) for ch in s)
    charset |= dict_cps
    corpus_cps = set()
    for name in ("sentences.tsv", "heldout.tsv"):
        p = os.path.join(root, "libbrainime", "tests", name)
        if os.path.exists(p):
            for s in corpus_surfaces(p):
                corpus_cps.update(ord(ch) for ch in s)
    charset |= corpus_cps
    supp = []
    supp_cps = set()
    supp_path = os.path.join(root, "libbrainime", "m3work", "supp.tsv")
    if os.path.exists(supp_path):
        supp = supp_surfaces(supp_path)
        for s in supp:
            supp_cps.update(ord(ch) for ch in s)
    charset |= supp_cps
    ui_cps = ui_codepoints(root)
    charset |= ui_cps

    src = TTFont(args.source, fontNumber=0)
    src_cmap = src.getBestCmap()
    in_src = {cp for cp in charset if cp in src_cmap}
    missing_src = sorted(cp for cp in charset if cp not in src_cmap)
    print(f"charset: {len(charset)} codepoints "
          f"(base {len(base_charset())}, dict {len(dict_cps)}, "
          f"corpus {len(corpus_cps)}, supp {len(supp_cps)}, "
          f"ui {len(ui_cps)})")
    print(f"source font has {len(in_src)} of them; "
          f"{len(missing_src)} absent from the source itself"
          + (": " + " ".join(f"U+{cp:04X}" for cp in missing_src[:16])
             if missing_src else ""))

    # subset.load_font cannot open a TTC directly: extract face 0
    # (Noto Sans CJK JP) to a temporary single font first.
    import tempfile
    tmp = tempfile.NamedTemporaryFile(suffix=".otf", delete=False)
    tmp.close()
    TTFont(args.source, fontNumber=0).save(tmp.name)
    # hinting=False: stb_truetype runs no bytecode interpreter, so
    # the hinting tables are dead weight. layout_features="":
    # stb_truetype does no OpenType shaping either (the toolkit does
    # its own line breaking/kinsoku), so GSUB/GPOS feature tables
    # would never be read. Dropping both halves the file
    # (2.50 MiB -> 1.22 MiB measured) with identical coverage.
    opts = subset.Options(hinting=False, layout_features="")
    font = subset.load_font(tmp.name, opts)
    pop = subset.Subsetter(options=opts)
    pop.populate(unicodes=sorted(in_src))
    pop.subset(font)

    # OFL: do not ship the subset under the Reserved Font Name.
    name = font["name"]
    for nid, val in ((1, "BrainUI JP"), (2, "Regular"),
                     (3, "BrainUI JP Subset"), (4, "BrainUI JP Subset"),
                     (6, "BrainUIJP-Subset"), (16, "BrainUI JP"),
                     (17, "Regular")):
        name.setName(val, nid, 3, 1, 0x409)
        name.setName(val, nid, 1, 0, 0)
    font.save(args.out)
    os.unlink(tmp.name)

    sub = TTFont(args.out)
    sub_cmap = sub.getBestCmap()
    covered = {cp for cp in dict_cps if cp in sub_cmap}
    entries_ok = sum(1 for s in surfaces
                     if all(ord(ch) in sub_cmap for ch in s))
    src_size = os.path.getsize(args.source)
    out_size = os.path.getsize(args.out)
    print(f"source TTC size : {src_size} bytes (all faces)")
    print(f"subset size     : {out_size} bytes "
          f"({out_size / 1024:.0f} KiB), {len(sub_cmap)} glyphs in cmap")
    print(f"dict codepoints : {len(covered)}/{len(dict_cps)} "
          f"= {100.0 * len(covered) / max(1, len(dict_cps)):.2f}% covered")
    print(f"dict entries    : {entries_ok}/{len(surfaces)} "
          f"= {100.0 * entries_ok / max(1, len(surfaces)):.2f}% fully covered")
    corpus_ok = {cp for cp in corpus_cps if cp in sub_cmap}
    print(f"corpus cps      : {len(corpus_ok)}/{len(corpus_cps)} covered")
    supp_ok = {cp for cp in supp_cps if cp in sub_cmap}
    supp_entries_ok = sum(1 for s in supp
                          if all(ord(ch) in sub_cmap for ch in s))
    print(f"supp codepoints : {len(supp_ok)}/{len(supp_cps)} "
          f"= {100.0 * len(supp_ok) / max(1, len(supp_cps)):.2f}% covered")
    print(f"supp entries    : {supp_entries_ok}/{len(supp)} "
          f"= {100.0 * supp_entries_ok / max(1, len(supp)):.2f}% fully covered")
    ui_ok = {cp for cp in ui_cps if cp in sub_cmap}
    ui_missing = sorted(cp for cp in ui_cps if cp not in sub_cmap)
    print(f"ui codepoints   : {len(ui_ok)}/{len(ui_cps)} "
          f"= {100.0 * len(ui_ok) / max(1, len(ui_cps)):.2f}% covered"
          + (": missing " + " ".join(f"U+{cp:04X}" for cp in ui_missing[:32])
             if ui_missing else ""))
    for cp in (0x2212, 0x591B, 0x5F0C):
        print(f"regression U+{cp:04X}: "
              f"{'present' if cp in sub_cmap else 'MISSING'}")

    # ---- supplements: charset codepoints the main source lacks ----
    outdir = os.path.dirname(os.path.abspath(args.out))

    def build_supplement(src_path, cps, family, out_name):
        if not cps:
            print(f"{out_name}: no codepoints needed")
            return set()
        opts = subset.Options(hinting=False, layout_features="")
        sfont = subset.load_font(src_path, opts)
        pop = subset.Subsetter(options=opts)
        pop.populate(unicodes=sorted(cps))
        pop.subset(sfont)
        name = sfont["name"]
        for nid, val in ((1, family), (2, "Regular"), (3, family),
                         (4, family), (6, family.replace(" ", "")),
                         (16, family), (17, "Regular")):
            name.setName(val, nid, 3, 1, 0x409)
            name.setName(val, nid, 1, 0, 0)
        out_path = os.path.join(outdir, out_name)
        sfont.save(out_path)
        got = set(TTFont(out_path).getBestCmap())
        print(f"{out_name}: {len(got)} glyphs, "
              f"{os.path.getsize(out_path)} bytes (from {src_path})")
        return got

    dejavu_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    symbols_path = "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf"
    chain = set(sub_cmap)
    if os.path.exists(dejavu_path):
        dv_cmap = TTFont(dejavu_path).getBestCmap()
        need = {cp for cp in charset
                if cp not in src_cmap and cp in dv_cmap}
        chain |= build_supplement(dejavu_path, need,
                                  "BrainUI JP Supplement",
                                  "brainui-jp-supplement.ttf")
    if os.path.exists(symbols_path):
        sy_cmap = TTFont(symbols_path).getBestCmap()
        need = {cp for cp in charset
                if cp not in src_cmap and cp not in chain
                and cp in sy_cmap}
        chain |= build_supplement(symbols_path, need,
                                  "BrainUI JP Symbols",
                                  "brainui-jp-symbols.ttf")
    ui_chain_ok = {cp for cp in ui_cps if cp in chain}
    ui_chain_missing = sorted(cp for cp in ui_cps if cp not in chain)
    print(f"ui codepoints (full chain): {len(ui_chain_ok)}/{len(ui_cps)} "
          f"= {100.0 * len(ui_chain_ok) / max(1, len(ui_cps)):.2f}% covered"
          + (": missing " + " ".join(f"U+{cp:04X}" for cp in ui_chain_missing[:32])
             if ui_chain_missing else ""))
    dict_chain_ok = {cp for cp in dict_cps if cp in chain}
    print(f"dict codepoints (full chain): {len(dict_chain_ok)}/{len(dict_cps)} "
          f"= {100.0 * len(dict_chain_ok) / max(1, len(dict_cps)):.2f}% covered")
    return 0


if __name__ == "__main__":
    sys.exit(main())
