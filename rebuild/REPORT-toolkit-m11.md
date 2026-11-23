# REPORT-toolkit-m11 — GUI ライブラリ同等化の継続 第4弾（M11）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


2026-11-20。対象: `include/`・`src/`・`tests/` と AUDIT のみ。
repo へは push していない。正本マトリクスは AUDIT-toolkit.md §2。

着手時点（M10 到達点、REPORT-toolkit-m10.md が正本）:
● 167/458（36.5%）、◐ 165、○ 126、△ 0、✕ 0。
モード別 ● は Android 91/229、WinUI 76/229。
ツールキット 1,584 tests 全 PASS、消費者（アプリ 1,048 /
シェル 683 / OSK 319）回帰ゼロ。**完成ではない。**
残りは 291 セル。本ラインの目的もセルの格上げのみ。

本ラインで据え置き（触らない）: Android Slider の形
（M3 Expressive ピル型ハンドルはユーザー判断待ちの提案として
分離済み）、ListView 行高 Δ1（AUDIT §2.4 の適応記録どおり）。

## 0. 着手時に Web で取得した仕様（出典）

- M3 Buttons スペック（https://m3.material.io/components/
  buttons/specs、2026-11-20 取得）: 色役割の確定 —
  Elevated container = Surface container low / 内容 = Primary、
  Tonal container = Secondary container / 内容 =
  On secondary container、Filled = Primary / On primary。
  状態は一般の States 枠組み（M10 で取得済み: Pressed 層 10%、
  無効は容器 12% / 内容 38%）に従う。
- M3 Radio button スペック（同 radio-button/specs）:
  色役割は Primary（選択）と On surface variant（未選択
  リング）。状態集合は Enabled/Hover/Focus/Pressed/Disabled
  で、無効形は States 枠組み（内容 38%）。
- M3 Text fields スペック（同 text-fields/specs）:
  filled / outlined 双方にエラー状態があり、エラー色は
  Error 役割（本実装の danger トークン）。filled のエラーは
  下部の active indicator（下線）が Error、outlined は
  アウトラインが Error。
- M3 Chips スペック（同 chips/specs）: Filter chip の選択時
  container = Secondary container、内容 = On secondary
  container。状態集合に Pressed があり States 枠組みに従う。
- WinUI ToggleButton themeresources（microsoft-ui-xaml
  `controls/dev/CommonStyles/ToggleButton_themeresources.xaml`、
  2026-11-20 取得）: Default テーマ辞書の実値 —
  Background = ControlFillColorDefault、Pressed =
  ControlFillColorTertiary、Disabled =
  ControlFillColorDisabled、Checked =
  AccentFillColorDefault、**CheckedPressed =
  AccentFillColorTertiary、CheckedDisabled =
  AccentFillColorDisabled、ForegroundCheckedDisabled =
  TextOnAccentFillColorDisabled**。

## 0.5 途中経過

### 着手時点で判明した実在バグ（§3 で詳述、修正済みかは §3 参照）

1. **WinUI トグルの checked 無効形が仕様ロール取り違え**
   （button_variant_face）: M10 は「ControlFillColorDisabled +
   TextFillColorDisabled（ToggleButton themeresources）」と
   記録して実装したが、上記一次ソースでは CheckedDisabled =
   AccentFillColorDisabled、ForegroundCheckedDisabled =
   TextOnAccentFillColorDisabled。checked 押下形も未実装
   （rest の accent のまま。正しくは AccentFillColorTertiary）。
2. **WinUI の LEGACY トグル checked 形が描画経路に無い**:
   テキスト形の分岐条件が `style != 2` で、WinUI では checked
   でも中立面（ControlFillColorDefault）のままだった。
   Fluent の checked = accent 面に到達する経路が無かった。
3. **モーダル Dialog に scrim が無い**（両モード）:
   M3 モーダルは scrim（on-surface 32% 相当）、WinUI
   ContentDialog は Smoke の背後層が仕様。テーマには両モード
   とも smoke トークン実在（v2 系 {0,0,0,82} ≒ 32%、
   WinUI {0,0,0,77} = SmokeFillColorDefault #4D000000）し、
   モーダルシートとドロワーは既に同じトークンで scrim を
   描いているのに、ダイアログだけ欠けていた。

### 実装修正（途中経過）

- バグ1・2 を修正: `winui_toggle_checked_face` を新設し、
  variant 経路・LEGACY テキスト形・アイコン行形で共有。
  checked 押下 = accent 80%、checked 無効 = accent_dis +
  on_accent_dis。LEGACY テキスト形でも WinUI checked が
  accent 面に到達するようになった。
- Chip の WinUI 形も同じ一次ソースに合わせて修正:
  未選択押下 = control_fill3、選択押下 = accent 80%、
  選択無効 = accent_dis + on_accent_dis。tk9 F の WinUI
  chip 期待値（M10 が未選択ペアをピンしていた箇所）を
  是正後の形に更新し、tk9 は 150 件全 PASS を維持。
- バグ3 を修正: overlay_paint でモーダル Dialog にも
  smoke の scrim を描画（モーダルシートと同じ経路）。
  あわせて表示時はルート全体を damage に入れ、除去時も
  モーダル Dialog の場合はルート全体を再描画対象にした。
- M3 トグルの各バリアント形を仕様化（従来は TONAL の
  selection 切替のみで、FILLED は常時 primary 面、
  ELEVATED は選択時も低コンテナ、OUTLINED/TEXT は
  container ウォッシュだった）: FILLED 未選択 =
  surfaceContainer（container_mid）+ on-surface-variant、
  FILLED/ELEVATED 選択 = primary 面、OUTLINED/TEXT
  選択 = inverse 対。いずれも押下層 10%・無効 12%/38% の
  枠組み付き。

## 1. 結果サマリ（セル単位の実数のみ）

| モード | 適用セル | ● 検証済み | ◐ 部分検証 | ○ 実装のみ | △ | ✕ |
|---|---|---|---|---|---|---|
| Android | 229 | 91 → **102**（44.5%） | 90 → 79 | 48 | 0 | 0 |
| WinUI | 229 | 76 → **88**（38.4%） | 75 → 65 | 78 → 76 | 0 | 0 |
| 合計 | 458 | 167 → **190**（41.5%） | 165 → 144 | 126 → 124 | 0 | 0 |

増減の内訳: ● +23（◐→● が 21、○→● が 2）、全て B 観点
（全状態の見た目）の移動で、A/C/D/E/F は不変。機械集計は
AUDIT-toolkit.md §2.3（表の記号の実数えと一致を確認済み）。
**完成ではない。** 残りは ◐ 144 + ○ 124 = 268 セル。

## 2. 格上げセルの根拠（仕様出典 + テスト）

新規テストは tests/test_tk10.c（119 件、全て自分の実行で
PASS）。期待値はテーマトークンからキャンバスのブレンド
算術で算出しており、半透明トークン（WinUI の text /
text_dim / selection 等）は面との合成値で照合する。

### ◐ → ●（21 件、全て B 観点）

優先度1（REPORT-m10 §4 の「実装済み・ピクセル未検証」）:
- Button（基本形）B（両モード）: FILLED / OUTLINED / TEXT
  は tk9 で検証済みだったものに、tk10 A で TONAL と
  ELEVATED の通常・押下・無効の全形、OUTLINED の無効枠、
  TEXT の押下層を追加し、5 バリアントの状態集合が揃った。
  M3 の根拠は buttons スペックの色役割（§0）と States
  枠組み、WinUI は ControlFill / AccentFill の状態ペア。
- RadioButton B（両モード）: tk10 C で未選択+無効の
  リング形を追加（M3 = surface 円盤 + on-surface 38% の
  リング、WinUI = ControlFillColorDisabled +
  ControlStrongStrokeColorDisabled）。選択+無効は tk9 B
  で検証済み。
- TextField B（両モード）: tk10 D でエラー形を正確な
  ピクセル位置で検証。M3 filled は下線の行・左右端
  （容器高の半分の内側）・2px の高さをキャンバスから
  実測して照合し、WinUI は danger ストロークの上辺・
  下辺の行位置と、エラー無し時の control stroke との
  対比まで確認した。placeholder / 無効 / フォーカスは
  tk9 E で検証済み。
- Chip B（両モード）: tk10 E で押下形を追加（M3 未選択
  押下 = surface_alt ウォッシュ、選択押下 = container
  維持、WinUI 未選択押下 = ControlFillColorTertiary、
  選択押下 = AccentFillColorTertiary）。無効形は tk9 F
  （WinUI 選択無効は M11 是正形で期待値を更新）。

その他の B 観点:
- Button（elevated / トグル）B（Android）: tk10 B で
  LEGACY / FILLED / ELEVATED / TONAL / OUTLINED の
  トグル選択・未選択形を検証（M11 で仕様化した形）。
- Slider（Range）B（両モード）: WinUI は tk10 I で無効時
  の両スパン+つまみ（ControlFillColorDisabled）を、
  Android は無効つまみ（38% 形）を追加。Android の
  スパンは tk9 D で検証済み。
- Progress（linear）B（両モード）: tk10 F で determinate
  の 0% / 50% / 100% とトラック役割（M3 =
  surfaceContainerHighest、WinUI = ControlStrongStroke
  ColorDefault）を検証。
- TextView B（両モード）: tk10 L で選択ハイライト
  （selection ウォッシュが選択範囲の下に敷かれ、グリフが
  その上に乗る）を検証。WinUI の面は tk9 M で検証済み、
  Android の面はウォッシュの合成値が surface 前提で
  一致することから確認。
- SearchBar B（両モード）: tk10 K で面（M3 =
  container_high のピル、WinUI = control fill）+
  placeholder の dim 役割 + 入力時のテキスト役割と
  クリアアフォーダンスを検証。
- Card B（Android）: tk10 H で elevated（container_low
  + 影）/ filled（container_highest）/ outlined
  （surface + border_line 枠）の3形を検証。WinUI は
  tk9 L で ● 化済み。
- Dialog（basic / modal）B（両モード）: tk10 L で
  M11 実装の scrim（smoke トークンの合成値、モーダルの
  時のみ・modeless では出ないことまで対比）、面
  （M3 = container_high、WinUI = surface）、タイトル
  グリフ、タイトル下の区切り線を検証。
- Segmented B（WinUI）: tk10 G で未選択セグメントの
  無地（ページ色透過）、未選択同士の間の区切り線
  （border_line）、容器の外枠（control stroke）を追加。
  選択形は tk9 I で検証済み。

### ○ → ●（2 件、全て B 観点）

- Button（elevated / トグル）B（WinUI）: トグルの全形
  （未選択 = 標準ボタンの状態ペア、選択 = accent 面 /
  AccentFillColorTertiary / accent 無効ペア）を LEGACY
  形とバリアント形の両経路で検証（tk10 B、M11 修正分）。
- Toast B（WinUI）: tk10 J で inverse 対（面と内容）を
  検証。Android は tk9 K で ● 化済み。

## 3. 発見・修正したバグ

AUDIT-toolkit.md §9 に記録。要点は §0.5 の 3 件に加え:

4. **M3 トグルのバリアント別選択形が大半未実装**:
   FILLED は常時 primary 面（仕様の未選択形は
   surfaceContainer + on-surface-variant 内容）、
   ELEVATED は選択時も低コンテナ（仕様は primary 面）、
   OUTLINED / TEXT は container ウォッシュ（仕様は
   inverse 対）。buttons スペックに基づき仕様化した。
5. **Chip の WinUI 押下形が無かった**: 未選択・選択とも
   rest 形のままだった。ToggleButton リソースに合わせて
   Tertiary ペアを実装。

tk10 初回実行の失敗 8 件は全てテスト側の前提誤り
（トグルの UP でクリックが成立し状態が戻る、ラベル
グリフ上のプローブ、半透明トークンのブレンド未考慮、
TextView のアンチエイリアス縁ピクセル）で、プローブ
実測で切り分けて是正した。実装側の想定外動作は上記
5 件以外に見つかっていない。

## 4. 残セルと理由

残り 268 セル（◐ 144 + ○ 124）。主な残りと理由:

- **D 観点（アニメーション）が最大の塊**（○ の大半）:
  本ラインは B 優先のため未着手。tk_anim 駆動の中間
  フレームをピクセルで追う検証が要る。
- WinUI の ○ 76 件: 上記 D に加え、#2 の A/E/F、
  #14 TextView の A、#23 / #24 Dialog（modeless /
  full-screen）の A・E・F、#27 Toast の A、#35
  ColorPicker の A・B 等のモード内未検証。
- Android の ○ 48 件: 同様に D が中心。
- ◐ の残り: FAB / IconButton B（両モード）、Split
  button B（両モード）、Switch B は ● だがその周辺
  （#3・#4・#5 の E/F）、Navigation rail / drawer /
  Bottom sheet の B（Android は面のみ未検証 — WinUI
  ダークでは tk9 L で検証済み）、Dialog（modeless /
  full-screen）B、Date / Time picker の無効形、
  Carousel の B 以外、ColorPicker B、長押し B 等。
- SearchBar B の ● は「描画される状態」（空・入力済み）
  の範囲。無効形は実装に存在しない（無効でも同じ面を
  描く）ため、状態として数えていない。
- 据え置き: Android Slider の形（ユーザー判断待ち）、
  ListView 行高 Δ1（AUDIT §2.4 の適応記録どおり）。

## 5. コピー / オーバーヘッド削減

- WinUI トグルの checked 面は共有ペインタ
  winui_toggle_checked_face に一本化し、variant 経路・
  LEGACY テキスト形・アイコン行形で重複していた状態
  分岐を排除した。描画呼び出し回数は増えていない。
- Dialog の scrim はモーダル時のみ全画面 fill 1 回。
  modeless / メニュー等の他のオーバーレイには描いて
  いない。damage はモーダル表示・除去の時だけルート
  全体に広げた（モーダルシートの既存挙動と同じ形）。
- 状態色は全てテーマトークンの参照で、描画時の追加
  確保・文字列処理はない。

## 6. キャッシュ / メモリ管理

- 新規キャッシュ・静的バッファ・トークンの追加なし。
  M11 の変更は描画分岐と scrim の fill のみで、
  tk_theme 構造体のサイズは不変。
- テスト側の期待値計算（ブレンド）はテストコード内の
  みで、ライブラリの実行時コストは増えていない。

## 7. 実機の現実への対応

- 変更は全て描画時の色・面の選択と scrim の有無のみで、
  永続状態・ファイル I/O・時刻依存の処理は追加して
  いない（リセット耐性・オフライン前提に影響なし）。
- scrim はモーダル表示中だけの一時描画で、閉じれば
  ルート全体の再描画で消える（残留状態なし）。
- 追加の割り当てなし（描画パスに malloc を入れていない）
  ため、128MB 予算への影響はない。

## 8. 未検証の明記

- 本ラインの検証はホスト上のユニットテスト（実フォント
  描画 + ピクセル確認）による。SD イメージ / QEMU
  ゲスト / 実機での確認はしていない（統合フェーズの
  担当）。
- WinUI ダークテーマでの tk10 全体再実行は未実施
  （tk10 の期待値はトークン相対で、ライト/ダーク同一
  チェックがそのまま動く設計だが、ダークでの実行確認
  自体はしていない。tk7 の TK7_DARK=1 は本ラインでも
  246/246 PASS を再確認済み）。

## 9. テスト実行結果（自分の実行で確認）

| スイート | 結果 |
|---|---|
| ツールキット `make check`（13 スイート） | **1,703 passed, 0 failed**（M10 の 1,584 から +119: tk10 新規） |
| test_tk10（新規） | 119 passed, 0 failed |
| test_tk7（TK7_DARK=1、winui_dark 全体再実行） | 246 passed, 0 failed |
| アプリ `make check`（無修正） | 1,048 passed, 0 failed（回帰ゼロ） |
| シェル `make check`（無修正） | 683 passed, 0 failed（回帰ゼロ） |
| OSK `make check`（無修正） | 319 passed, 0 failed（回帰ゼロ） |

変更ファイル: src/tk_widgets.c、src/tk_core.c、
tests/test_tk9.c（期待値1箇所の是正）、tests/test_tk10.c
（新規）、Makefile、AUDIT-toolkit.md、本レポート。
repo へは push していない。
