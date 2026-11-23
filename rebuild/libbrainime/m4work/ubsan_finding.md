# UBSan 指摘 (engine.c:1892) の判定と修正 — REPORT-m4.md へ転記する記録

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


- 指摘: UBSan ビルドの OSK テストで `src/engine.c:1892`「nonnull 属性の引数への NULL 渡し」1件（テスト自体は全 PASS）。
- 再現: libbrainime 単体を `-fsanitize=undefined` でビルドし session_test を v3 辞書で実行
  → `src/engine.c:1892:2: runtime error: null pointer passed as argument 1, which is declared to never be null` を同一行で再現。
- 該当箇所: `session_set_bounds()` の `memcpy(s->bounds, b, nb * sizeof(int))`。
- 実際に NULL が流れる経路（実在する）: `bi_session_new` は calloc で `bounds=NULL, cap_bounds=0`。
  境界のないセッションで `bi_session_commit_first` を呼ぶと、残りの境界が 0 件 (m=0) のため
  `session_set_bounds(s, nb, 0)` が呼ばれ、realloc も走らず `memcpy(NULL, b, 0)` になる。
  OSK の通常フロー（リサイズなしで文節確定）がそのまま該当する。
- 実害判定: **現行環境では実害なし**。コピー長 0 のため何も読み書きされず、glibc の memcpy は
  そのまま返る（テスト全 PASS と整合）。ただし nonnull 属性付き関数への NULL 引数は C 標準上
  未定義動作であり、最適化や環境が変われば保証はない。「属性の付け間違い」ではなく
  「実際に NULL が流れうる」側の問題。
- ついでに見つけた同所の潜在バグ: realloc 失敗時に旧コードは `bounds=NULL` のまま
  `cap_bounds` だけ増やし、`nb>0` の memcpy で実クラッシュしうる経路があった（今回の指摘とは別）。
- 修正: (1) `nb>0` のときだけ memcpy する。(2) realloc 失敗時は旧 bounds/cap/n_bounds を
  保持して早期 return する。
- 修正後検証: 同じ UBSan ビルドで session_test を実行し runtime error 0 件・全 PASS を確認。
