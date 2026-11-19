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

## Status

- v1 builder + lookup tool: `tools/ime/` (sorted-index BIDF,
  prefix candidate search verified on a synthetic dictionary).
- Next: fetch mozc dictionary_oss, build the real ~180 k BIDF,
  LOUDS v2, the Viterbi converter, then the OSK widget.
