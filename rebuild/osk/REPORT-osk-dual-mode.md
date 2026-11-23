# REPORT — OSK のモード追随確認（Android / WinUI）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


- 日付: 2026-11-21
- 対象: `osk/`（本体ウィジェットはライブラリ内 `src/tk_osk.c` = tk_osk、編集禁止。直せるのは osk/ 配下のみ）。
- 前提の検証: 「OSK 本体はツールキットのウィジェットで、モードはテーマ経由のはず」を実コードで確認する。

## 段階1: 前提の実コード確認（確認済み）

- `src/tk_osk.c` の描画入口 `tk_osk_paint(w,c,t)` はテーマ `t` を引数で受け取り、`tk_osk_measure` 以外の色は全てテーマ経由:
  - 背景 `t->bg`、候補バー背景 `t->surface_alt`、キー面 `t->surface`、押下/シフト時 `t->selection`、候補チップ選択時 `t->accent` / `t->on_accent`、文字 `t->text` / `t->text_dim`、枠 `t->border`、フリックプレビュー面 `t->surface`＋角丸 `t->radius`。
  - つまり **OSK 側（osk/）にテーマ選択コードは無く、モードはプロセスの `tk_theme_default()` で決まる**。osk/tests/test_osk.c のフィクスチャもテーマを設定せず `tk_theme_default()` を使用（既定 = classic light のはず。要確認は段階2）。
- ハードコード疑い（段階2で描画比較により裏取りする）:
  1. **キー面の角丸が `6` 固定**（paint_cells: `tk_canvas_fill_round_rect(c, face, 6, ...)`、コメントも「radius 6」明記）。テーマ値ではない: Android (tk_theme_v2) の radius は 12、WinUI は 6。→ WinUI では偶然一致するが Android ではテーマより角が立つはず。
  2. キー面に `tk_canvas_stroke_rect`（直角の枠線）を併用 — 角丸面に四角い枠で、テーマの角丸と不整合になり得る。
  3. 候補チップ / 記号カテゴリチップは `cell->r.h / 2`（完全な pill）固定。テーマ非依存だが両モードとも pill とは限らない（WinUI の radius_button は 6）。
  4. 寸法定数 `OSK_CAND_H 48` / `OSK_BAR_H 56` / 行高 56 等は全て固定（テーマの control_h / row_h は両テーマとも 54 で近いが連動はしていない）。
  5. プレビューの角丸だけは `t->radius` を使用（テーマ追随する箇所としない箇所が混在）。

## 段階2: 両テーマ描画比較（854×480、実施済み）

### 2.0 レンダラと場所の注記
- `/tmp` は 512MB tmpfs が 100% 満杯（無関係な `/tmp/guicmp` 約400MB 等）で 2MB の書込みも ENOSPC になったため、レンダラは同じ揮発スクラッチである `/var/tmp/osk-dual/` に置いた（render_osk.c / バイナリ / PPM・PNG）。タスク指定の「/tmp に作る」からの逸脱はこの事情による。証拠 JPG は `osk/dual-shots/` に保存した。
- レンダラは test_osk のフィクスチャ（ui_new_w）相当: tk_fixed に TextField (0,0,854,44) と tk_osk をボトムシート配置 (0,208,854,272 = 候補48＋QWERTY 3×56＋バー56)、`tk_root_layout(854,480)` → `tk_root_paint(root, canvas, theme)`。テーマはプロセス既定を差し替えて v2 / winui（＋参考に v2_dark / winui_dark）。読み状態（"あい" を fallback バックエンドへ入力し候補バーを populated にした状態）も各テーマで描画。
- なお既存 test_osk.c は `tk_theme_set_default` を**一度も呼ばない**（grep 実測）: 既存テストの描画確認は製品2モードのどちらでも行われていなかった。本比較がその穴を埋める。

### 2.1 色: 完全にテーマ追随（数値実測）
プローブ（idle、キー 'q' セル (0,256,85,56)、候補バー右端 (848,232)、隙間 (2,100)）:

| 対象 | tk_theme_v2 | tk_theme_winui | 判定 |
|---|---|---|---|
| 隙間ピクセル | #FEF7FF = theme.bg | #F3F3F3 = theme.bg | 一致 |
| 候補バー地 | #E6E0E9 = surface_alt | #F9F9F9 = surface_alt | 一致 |
| キー面中央 | #FFFFFF = surface | #FFFFFF = surface | 一致 |
| 選択中候補チップ | #6750A4 = accent | #0067C0 = accent | 一致 |
| ダーク2種も同様 | bg/surface_alt/surface 全て各テーマ値と一致 | 同左 | 一致 |

文字サイズもテーマ連動: font_px 16 (v2) / 19 (winui) がキー・候補の計測に効き、候補チップ幅が 56 → 62px に変わることを実測。候補チップの選択色・下線色（TextField の composition 下線）もテーマ accent で変わる。証拠: `dual-shots/osk-v2.jpg` / `osk-winui.jpg` / `osk-v2-reading.jpg` / `osk-winui-reading.jpg`。

### 2.2 形: キー角丸はテーマ非連動（ハードコード 6、数値で確定）
- キー面の左上弧の開始位置を face 行ごとに走査（face = セル inset 2px）: dy=1 で dx=2、dy=3 で dx=1、dy=5 で dx=1 — **v2 と winui で完全一致**。これは半径≈6 の幾何であり、v2 のテーマ半径 12 なら dy=1 で dx≈6、dy=3 で dx≈4 になるはずで、明確に不整合。
- 原因は段階1の疑いどおり `src/tk_osk.c:paint_cells` の `tk_canvas_fill_round_rect(c, face, 6, ...)` 固定値。WinUI のテーマ半径も 6 のため **WinUI では偶然正しく、Android モードでだけテーマより角が立つ**（写真でも両モードのキーが同じ角張った形に見える）。
- 併せてキー面に `tk_canvas_stroke_rect`（直角枠）を重ねるため、仮に fill 半径をテーマ化しても角は四角い枠線に支配される。写真のキーが両モードとも「四角い箱」に見える主因はこの2点セット。

### 2.3 候補バー: 色は追随、チップ形は pill 固定
- 候補チップ・記号カテゴリチップとも `cell->r.h / 2` の完全 pill 固定（paint_cells）。対角走査: 両テーマとも d=6 で地→チップ面に遷移し pill 幾何と一致。WinUI（Fluent）のチップ様式は半径 6 の角型であり、**WinUI モードでも pill のまま**（色だけ Fluent accent に変わる）。
- バー高さ OSK_CAND_H=48、チップ高 40 (48-8) も固定。テーマ row_h/control_h は両テーマとも 54 で、バー寸法はテーマ連動していない（ただし両モード共通の固定であり、モード間の不整合ではない）。
