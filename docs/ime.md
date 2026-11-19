# Brainux IME — in-house Japanese input (design source)

Direction (user, 2026-11-19): Japanese input is built in-house — a
custom compact dictionary format with wide coverage, a custom
conversion engine, and a custom on-screen keyboard, integrated with
libbrainui. fcitx + anthy is the interim stack only.

Design research (sourced, 34 references):
`~/workspace/research_notes/brainux-ime-design-20261119/report.md`.
Anchors: mozc's published compression takes 1.35 M words from
59.1 MB of text to 13.3 MB (LOUDS tries + token compression);
anthy installs 28 MB of dictionary data for *less* coverage.

## BIDF format (dictionary)

One mmap-able file, shared read-only across processes:

| section | content |
|---|---|
| header | magic `BIDF`, version, counts, section offsets |
| codebook | the custom 1–2 byte character encoding table (~80 KB) |
| reading index | readings sorted; v1 = sorted array + binary search, v2 = LOUDS trie (~0.45 MB at 180 k entries) |
| surface trie | v2 LOUDS over surfaces (~0.55 MB) for reverse lookup |
| tokens | per entry: surface id (varint), POS class (1 B, ≤256 classes), quantised cost (1 B) ≈ 6 B/entry |
| connection | 256×256 POS-class bigram costs, mode+exception compressed (~20–60 KB) |

Budget: **≈2.3–2.8 MB for ~180 k entries** (hard budget 3.5 MB),
engine RSS ≤ 8 MB. Coverage floor: all 2,136 jōyō + 863 jinmeiyō
kanji as single-kanji entries, ~180 k general words.

Build sources (licences checked in the research): mozc
`dictionary_oss` (BSD-3 + IPADIC notices) as the primary source,
gap-filled from NAIST-jdic (BSD-style) and UniDic (BSD arm).
SKK-JISYO is NOT a build source (GPL, and it carries no costs).

## Engine (`libbrainime`)

Dependency-free C library: lattice construction over the reading
with unknown-word nodes (katakana runs, single-kanji fallback),
Viterbi shortest path over word costs + POS-class connection
costs, n-best output, and a learning layer (user dictionary +
per-pair frequency boosts stored outside the read-only BIDF).

Integration:
- libbrainui apps and the greeter link the library in-process.
- Existing apps: a thin **fcitx5 addon** wrapping the same
  library — GTK (IM module), Qt (platform input context), Xlib
  (fcitx5's XIM server) and Wayland all keep working unchanged.
- anthy stays selectable as a fallback until the regression
  suite (mozc-style TSV conversion tests) passes on-device.

## OSK

One libbrainui widget, three hosts: braindesk overlay, greeter,
and an always-on-top X window feeding the fcitx addon.
Layouts: 12-key flick **with toggle multi-tap on the same keys**
(flick is faster — ~70–130 vs ~30–60 chars/min in published
comparisons — but toggle is the zero-learning entry path),
QWERTY romaji, and a 50-on grid. Key short dimension ≥ 56 px at
854×480 (the 48 dp guideline ≈ 6.5 mm on this panel, below the
7–10 mm comfort band).

## Real dictionary build (mozc dictionary_oss, 2026-11-20)

The real dictionary is built from mozc `dictionary_oss`
(BSD-3 + IPADIC notices; provenance and licence texts recorded in
`tools/ime/MOZC-NOTICE.md`, mozc commit
`3cf2d38553835f9241479d3b42c7d408c8f45076`). The `.bidf` binary is
regenerable and is **not** committed; the converter and this
procedure are the source of record.

Procedure:

```
git clone --depth 1 --filter=blob:none --sparse https://github.com/google/mozc
git -C mozc sparse-checkout set src/data/dictionary_oss
python3 tools/ime/convert_mozc.py mozc/src/data/dictionary_oss words.tsv
python3 tools/ime/build_bidict.py words.tsv brainux.bidf
```

`convert_mozc.py` (disk-backed sqlite; runs in a few hundred MB):

- **POS compression**: mozc's 2,672 IPADIC ids → **57 BIDF
  classes** by ordered rules over the id.def POS string (the full
  mapping table is in the script). Kept distinct: noun general /
  proper-person / place / organisation / sahen / numeral /
  suffix / pronoun, verb godan / ichidan / sahen / kahen /
  irregular, adjectives, and the particle subtypes. The class is
  taken from the entry's lid.
- **Cost quantisation**: linear rescale 0..255 with hi = p99 of
  deduplicated costs (11,895 on this snapshot; one step ≈ 47 mozc
  units). Linear, not log: mozc's cost is already a log-domain
  quantity, so linear rescaling preserves the additive spacing
  Viterbi needs.
- **Selection**: mozc's own `dictionary_filter.tsv` applied (15
  entries), non-kana readings dropped (3,486), deduplicated on
  (reading, surface, class) keeping min cost → 1,166,186 entries.
  All **10,390 single-kanji entries force-included** (coverage
  floor), the rest filled by ascending cost (cutoff 5,770) to
  exactly **180,000 entries / 116,292 readings**; max 129
  candidates for one reading. Known bias: cost ranking favours
  frequent loanwords and proper nouns (top class is 人名, 29.5 k).

Measured BIDF v1 (2026-11-20):

| section | bytes |
|---|---|
| header | 64 |
| reading index (116,292 × 12 B) | 1,395,504 |
| tokens (180,000 × 8 B) | 1,440,000 |
| string pool (raw UTF-8) | 3,448,938 |
| connection matrix (zeros, v1) | 65,536 |
| **total** | **6,350,042 (35.3 B/entry)** |

The 2.3–2.8 MB budget above is the **v2** figure (LOUDS tries +
codebook + varint tokens). v1's fixed-width layout costs more —
the raw string pool alone is 3.45 MB — which is exactly what v2
attacks; the token/pool/conn layout does not change. Coverage:
**3,463 distinct single-kanji surfaces** (≥ 2,136 jōyō + 863
jinmeiyō). Verified with `bidict_lookup`: かんじ→漢字・感じ・
幹事, とうきょう→東京 (top, cost 0), せんせい→先生, がくせい→
学生, あい→愛, prefix search over かん…. Ranking is raw mozc
word cost only until connection costs + the Viterbi converter
land (e.g. かんじ's top hit is 感じ, the stem of 感じる, which
mozc prices as very frequent).

## Status

- v1 builder + lookup tool: `tools/ime/` (sorted-index BIDF,
  prefix candidate search verified on a synthetic dictionary).
- Real dictionary built: 180 k-entry BIDF v1 from mozc
  dictionary_oss, 6.35 MB (see above; binary not committed).
- Next: LOUDS v2 (closes the size gap to the 2.3–2.8 MB budget),
  connection matrix from mozc's connection data, the Viterbi
  converter, then the OSK widget.
