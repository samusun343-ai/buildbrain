# REPORT-toolkit-m10 — GUI ライブラリ同等化の継続 第3弾（M10）

2026-11-20。対象: `include/`・`src/`・`tests/` と AUDIT のみ。
repo へは push していない。正本マトリクスは AUDIT-toolkit.md §2。

着手時点（M9 到達点、REPORT-toolkit-m9.md が正本）:
● 155/458（33.8%）、◐ 164、○ 139、△ 0、✕ 0。
モード別 ● は Android 86/229、WinUI 69/229。
ツールキット 1,431 tests 全 PASS、消費者（アプリ 1,048 /
シェル 588 / OSK 319）回帰ゼロ。**完成ではない。**
このラインの目的もセルの格上げであり、主戦場は B 観点
（全状態の見た目）のピクセル網羅と WinUI 側 ○ 89 件。

本ラインで据え置き（触らない）: Android Slider の形
（M3 Expressive ピル型ハンドルはユーザー判断待ちの提案として
分離済み）、ListView 行高 Δ1。ProgressBar トラック色と
InfoBar Attention 色は「新たな出典が得られた場合のみ再訪」
の条件付き据え置きだったが、本ラインで出典が得られたため
再訪して仕様値化した（§0、AUDIT §2.4 追記）。

## 0. 着手時に Web で取得した仕様（出典）

- M3 States（https://m3.material.io/foundations/interaction/states/state-layers
  および applying-states、2026-11-20 取得）: 状態層の不透明度は
  Hover +8% / Focus +10% / Pressed +10% / Dragged +16%。
  状態層の色はコンテンツの色（on カラー）。Disabled の内容は
  on-surface の 38%、コンテナは on-surface の 12%
  （md.sys.state.disabled-content-opacity 0.38 /
  disabled-container-opacity 0.12）。Disabled は状態層を継承
  せず、フォーカス・ドラッグ・押下もしない。
  ※ 本機は抵抗膜1本指でホバーが物理的に存在しないため、
  Hover は「ホバー相当の代替」として押下中・フォーカス時の
  状態層で扱う（従来ラインの扱いを継続）。
- WinUI 状態色の実値: Microsoft Learn「Visual Studio theme
  color tokens」（Windows 11 Fluent の既定値を掲載、
  2026-11-20 取得）。M8/M9 で取得不能だった値が揃った:
  ControlFillColorSecondary #80F9F9F9 / #15FFFFFF、
  Tertiary #4DF9F9F9 / #08FFFFFF、Disabled #4DF9F9F9 /
  #0BFFFFFF、TextFillColorDisabled #5C000000 / #5DFFFFFF、
  TextOnAccentFillColorDisabled #B2FFFFFF / #80000000、
  ControlStrongStrokeColorDefault #9C000000 / #9AFFFFFF、
  同 Disabled #37000000 / #28FFFFFF、
  ControlStrongFillColorDefault #72000000 / #8BFFFFFF、
  同 Disabled #51000000 / #3FFFFFFF、AccentFillColor
  Disabled #37000000 / #28FFFFFF、SystemFillColor
  Attention #FF005FB7 / #FF60CDFF、AttentionBackground
  #80F6F6F6 / #08FFFFFF、FocusStrokeInner #FFFFFFFF /
  #B2000000（light / dark）。→ 新トークンとして
  tk_theme に追加（winui / winui_dark のみ実値、他テーマは
  未設定 = 従来ロールへフォールバック）。
  これにより据え置き項目のうち「新たな出典が得られた
  場合のみ再訪」とされていた2件（ProgressBar トラック色 =
  ControlStrongStrokeColorDefault、InfoBar Attention 色）
  は再訪条件を満たし、本ラインで仕様値化した。
- M3 チェックボックス仕様（m3.material.io/components/
  checkbox/specs、2026-11-20 取得）: 容器 18dp・角 2dp、
  状態は Enabled/Disabled/Hovered/Focused/Pressed。
  トークン値表はページから抽出できなかったため、無効形は
  上記 States の枠組み値（内容 38% / 容器 12%）で実装した。

## 0.5 途中経過（実装済みの仕様状態化）

- テーマ: M10 トークン12件を追加（tk.h 末尾、tk_core.c の
  winui / winui_dark に実値）。
- Button: 共有の主面ペインタ accent_button_face を新設し、
  FILLED バリアントと legacy accent 面（テキスト形・
  アイコン行形）を統一。状態形: M3 押下 = on-primary 状態層
  10%、無効 = on-surface 12% 容器 / 38% 内容。WinUI 押下 =
  AccentFillColorTertiary（accent 80%）、無効 =
  AccentFillColorDisabled + TextOnAccentFillColorDisabled。
  ELEVATED / TONAL / OUTLINED / TEXT / 中立 legacy 面も
  モード別状態形に（WinUI: Tertiary/Disabled フィル +
  TextFillColorDisabled、M3: 状態層 10% オーバーレイ、
  OUTLINED 無効枠 = on-surface 12%）。classic は従来形を維持。
- Checkbox / Radio: WinUI の未選択枠を ControlStrongStroke
  に是正（従来は淡い ControlStroke で、ライトでは偶然
  text_dim と近かったスイッチ枠と同種のロール取り違え）。
  無効形を両モードで実装（WinUI: Disabled フィル/強枠/
  TextFillDisabled、M3: on-surface 38%）。
- Switch（WinUI）: OFF 枠 = ControlStrongStroke、ノブ =
  ControlStrongFill、ON ノブ = on-accent 色（ダークで
  surface と食い違っていた）、無効 = Disabled 強ペア +
  チェック時 ControlFillColorDisabled。
- Slider: 無効トラック/つまみを両モードで仕様形に
  （M3: アクティブ 38% / 非アクティブ 12% / つまみ 38%、
  WinUI: ControlFillColorDisabled）。
- Progress: トラックを仕様ロールに是正（M3 =
  surfaceContainerHighest、WinUI = ControlStrongStroke
  ColorDefault。従来は両モードとも surface_alt 近似）。
- TextField: 無効面（WinUI = ControlFillColorDisabled、
  M3 filled = on-surface 12%）。
- Chip: 無効形を両モードで実装（上記 Button と同じ枠組み）。
- Menu 項目: WinUI の無効項目を TextFillColorDisabled に。
- フォーカスリング（WinUI）: 内側ストロークのライト側
  alpha を仕様 #FFFFFFFF に是正（従来 179）。
- Snackbar（InfoBar 形）: INFO の disc/背景を accent 代替
  から SystemFillColorAttention 実値に切替。
- tk7: WinUI ダーク再実行を正式実施（§5）。

## 1. 結果サマリ（セル単位の実数のみ）

| モード | 適用セル | ● 検証済み | ◐ 部分検証 | ○ 実装のみ | △ | ✕ |
|---|---|---|---|---|---|---|
| Android | 229 | 86 → **91**（39.7%） | 93 → 90 | 50 → 48 | 0 | 0 |
| WinUI | 229 | 69 → **76**（33.2%） | 71 → 75 | 89 → 78 | 0 | 0 |
| 合計 | 458 | 155 → **167**（36.5%） | 164 → 165 | 139 → 126 | 0 | 0 |

増減の内訳: ● +12（◐→● が 11、○→● が 1）、◐ は
○→◐ の +11 と ◐→● の −11 で相殺され +1、○ −13。
全て B 観点（全状態の見た目）の移動で、A/C/D/F は不変、
E は WinUI AppBar の +1（○→◐）のみ。機械集計は
AUDIT-toolkit.md §2.3（表の記号の実数えと一致を確認済み）。
**完成ではない。** 残りは ◐ 165 + ○ 126 = 291 セル。

## 2. 格上げセルの根拠（仕様出典 + テスト）

新規テストは tests/test_tk9.c（150 件、全て自分の実行で
PASS）。期待値はテーマトークンからキャンバス自身の
ブレンド算術で算出しており、ライト/ダーク両テーマで
同じチェックがそのまま動く。

### ◐ → ●（11 件、全て B 観点）

- Android Checkbox: 状態集合 {選択, 未選択} × {有効, 無効}
  + フォーカスリングを全てピクセル検証（tk9 B）。
  無効形は M3 States の 38% 枠組み（§0）。
- Android Slider（単一）: 通常形（tk5 で検証済み）に加え、
  無効トラック（アクティブ 38% が非アクティブ 12% の上に
  重なる参照側の描画順そのままの合成値）と無効つまみを
  検証（tk9 D）。
- Android Menu / WinUI Menu: 面・ホバーウォッシュ
  （初期表示で第1項目、ポインタ移動で追随）・無効項目の
  役割色・チェック項目の印を両モードで検証（tk9 H）。
  WinUI の面は CardBackground 役割、无効項目は
  TextFillColorDisabled（§0 の実値）。
- Android TabBar / WinUI TabBar（TabView）: アクティブ/
  非アクティブの形を検証。WinUI はライト+ダーク両方で
  「アクティブ = surface に浮く、非アクティブ = layer」
  を検証（tk9 I）。
- WinUI Checkbox: 未選択の面 + 強い枠（§3-1）、無効時の
  面（選択/未選択とも ControlFillColorDisabled）と
  グリフ（TextFillColorDisabled）を検証（tk9 B）。
  選択+有効形は tk7 で検証済み。
- WinUI Slider: 無効時に両スパンとつまみが
  ControlFillColorDisabled になる形を検証（tk9 D）。
  通常形は tk7 で検証済み。
- WinUI Card: Fluent カードはバリアント非依存の単一形で、
  面（CardBackground 合成値）と枠（ControlStroke 合成値）
  を検証（tk9 L）。
- WinUI ListView: 選択行（subtle 合成 + 左端 accent バー）
  とマーク行（surface_alt）を検証（tk9 G）。
- WinUI Snackbar（InfoBar）: 4 severity 全ての背景と
  ディスクを検証。INFO は M10 で実値化した
  SystemFillColorAttention 系（tk9 K、tk8 も追随済み）。

### ○ → ●（1 件）

- Android Toast: 表示形は単一状態（inverse 面 + inverse
  内容）で、その両方を検証（tk9 K）。

### ○ → ◐（11 件、全て WinUI）

- FAB menu B: 項目面（surface）が表示域に存在することを
  確認（tk9 N）。項目ごとの状態形は未検証のため ◐ 止まり。
- TextView B: 面が surface 役割であることを検証（tk9 M）。
  編集時状態（選択ハイライト等）は未検証。
- Badge（InfoBadge）B: 描画はモード非依存で、件数/ドット/
  非表示の3形を検証（tk9 N）。WinUI 固有の配置は未検証。
- TreeView B: 選択行が「行面への selection オーバーレイ」
  である形を検証（tk9 M）。展開/折りたたみ時の行形は未検証。
- Dialog（modeless）B / Dialog（full-screen）B: ダーク
  テーマでの面（surface）を検証（tk9 L）。ライトは tk8 /
  tk7 の範囲。内容・ボタン状態は各行の範囲外のため ◐。
- Date / Time picker B（両モード）: アクティブセグメント
  （selection 合成値）・非アクティブセグメント
  （surface_alt）・ステッパーの存在を検証（tk9 J）。
  無効形は未検証のため ◐。
- Navigation drawer B: モーダル時のパネル面が「スクリム
  の上に layer 合成」である形をダークで検証（tk9 L）。
- Bottom sheet B: 面が layer 合成である形をダークで
  検証（tk9 L）。
- AppBar B / E: 面（layer 合成）とタイトル描画（text
  役割のピクセル）を検証（tk9 M）。

## 3. 発見・修正したバグ

詳細は AUDIT-toolkit.md §8。要点:

1. WinUI Checkbox / Radio の未選択枠が淡い ControlStroke
   （仕様ロールは ControlStrongStrokeColorDefault）。
   ライトでは text_dim と偶然近く、既存テストでは検出
   できなかった。tk7 のダーク再実行で発覚。
2. WinUI Switch の OFF ノブ・枠も同じロール取り違え
   （ControlStrongFill / ControlStrongStroke が仕様）。
   ON ノブは surface 固定でダーク時に on-accent 色と
   食い違っていた。
3. フォーカスリング内側ストロークの alpha が light で
   179（仕様 #FFFFFFFF = 255）。
4. 無効状態の形が大半の部品で未実装/近似だった
   （text_dim 一括、surface_alt 代用）。§0 の出典に基づき
   Button 全バリアント + legacy 面 / Checkbox / Radio /
   Switch / Slider / Chip / TextField / Menu 項目で
   仕様形に実装。
5. Progress トラックが両モードとも surface_alt 近似
   （仕様ロールに是正、§0）。
6. テスト側の発見: tk7 は WinUI ダークで全体未実行だった
   （M9 残件）。TK7_DARK=1 の読み替え実行を新設し、
   失敗 5 件を分類（ライト前提ピン 3 件 = テスト側修正、
   実装ロール取り違え 2 件 = 上記 2 のスイッチ分）。

なお tk9 作成中に判明した検証上の注意: フォント未 install
では draw_text が何も描かない（tk.h の契約どおり）ため、
グリフピクセル検査は tk_font_load_bundled() +
tk_font_install() が前提（tk7 と同じ作法を tk9 にも適用）。

## 4. 残セルと理由

残り 291 セル（◐ 165 + ○ 126）。本ラインで動かさなかった
主なものと理由:

- Button B（両モード ◐ のまま）: FILLED と legacy 面の
  全状態は検証したが、TONAL / ELEVATED の押下・無効形は
  M10 で実装したもののピクセル未検証。次ラインで tk9 に
  追加すれば ● 化できる見込み（実装と出典は揃っている）。
- Radio B（両モード ◐ のまま）: 未選択+無効のリング形が
  未検証（選択+無効は検証済み。描画分岐は共通）。
- TextField B（両モード ◐ のまま）: エラー形は「danger
  役割のピクセル存在」までで、WinUI のエラー枠・M3 の
  エラー下線の正確な位置ピクセルは未検証。
- Chip B（両モード ◐ のまま）: 押下形（M3 未選択押下 =
  surface_alt 等）が未検証。
- Segmented WinUI B（◐ のまま）: 選択形（accent 面）は
  ライト/ダーク両方で検証したが、未選択セグメントの
  面・区切り線のピクセルは未検証。
- Slider（Range）B（両モード ◐ のまま）: 無効形は
  Android のみ検証。ドラッグ中の value indicator は
  従来テストの範囲。
- WinUI の ○ 78 件: 大半は D（アニメーション）と
  A/F の未検証、および TextView / ColorPicker 等の
  モード内未検証。本ラインは B 優先のため未着手。
- Android の ○ 48 件: 同様に D 観点が中心。
- 据え置き: Android Slider の形（ユーザー判断待ち）、
  ListView 行高 Δ1（AUDIT §2.4 の適応記録どおり）。

## 5. WinUI ダークテーマでの tk7 全体再実行

M9 の残件（tk8 の4テーマ検証で代替されていたもの）を
正式に実施した。

- 仕組み: tests/test_tk7.c に TK7_DARK=1 の読み替えを
  追加。全ての tk_theme_winui 選択が tk_theme_winui_dark
  に置き換わり、ライト前提だったピクセルピンはテーマ
  相対（トークンのブレンド期待値）に書き換えた。
  実行時にマーカー行を出力する。
- 結果（自分の実行）: 通常実行 **246 passed, 0 failed** /
  TK7_DARK=1 実行 **246 passed, 0 failed**。
- 初回ダーク実行の失敗 5 件の分類: (a) スイッチ OFF の
  輪郭・ノブ 2 件 → 実装側のロール取り違え（§3-2）で、
  実装を直し期待値を新トークンの合成値に更新。
  (b) TextField プレースホルダ走査 1 件 → テスト側の
  ライト前提（ダークでは面自体が暗い）で、テーマ相対の
  バンド判定に修正。(c) Dialog / TabBar の面 2 件 →
  tk_theme_winui.surface のリテラルピンで、
  tk_theme_default()->surface に修正。
- 追加で見つかった暗黙のライト前提: Progress トラックの
  ピン（旧近似 surface_alt）は M10 の仕様値化に伴い
  strong_stroke 合成値へ更新。スイッチ ON ノブのピンは
  surface から on_accent（仕様役割）へ更新（ライトでは
  同値、ダークでのみ差が出る）。

## 6. コピー / オーバーヘッド削減

- 主面ペインタ accent_button_face の共有化: FILLED
  バリアントと legacy accent 面（テキスト形・アイコン行形）
  で重複していた状態分岐（押下/無効の面と内容色の決定）を
  1 箇所に統合した。描画呼び出し回数は増えていない
  （同一ピクセルへの二重描きは従来どおり無し）。
- 状態色は全てテーマトークンの参照で、描画時の色計算は
  tk_color の値コピー（4 バイト）のみ。文字列・確保は
  追加していない。
- テスト側の期待値計算（ブレンド）はテストコード内のみで、
  ライブラリの実行時コストは増えていない。

## 7. キャッシュ / メモリ管理

- 新規キャッシュ・静的バッファの追加なし。M10 トークンは
  tk_theme 構造体に 12 色（48 バイト/テーマ）追加した
  だけで、テーマは静的 const（プロセス共有、コピーなし）。
- 未設定トークン（alpha 0）は「従来ロールへフォール
  バック」の意味に統一し、テーマごとの分岐テーブルを
  増やしていない。classic / v2 系テーマはゼロ初期化の
  まま従来動作を維持する。

## 8. 実機の現実への対応

- 状態形の追加は全て描画時の色選択のみで、永続状態・
  ファイル I/O・時刻依存の処理は追加していない
  （リセット耐性・オフライン前提に影響なし）。
- ホバー状態は本機に物理的に存在しないため、従来ライン
  どおり押下・フォーカス・メニューのホバーウォッシュ
  （ポインタ位置による表示）で代替し、実機で再現不能な
  状態は作っていない。
- 追加の割り当てなし（描画パスに malloc を入れていない）
  ため、128MB 予算への影響はテーマ構造体 +48 バイト
  （静的）のみ。

## 9. 未検証の明記

- 本ラインの検証はホスト上のユニットテスト（実フォント描画 +
  ピクセル確認）による。SD イメージ / QEMU ゲスト / 実機での
  確認はしていない（統合フェーズの担当）。

## 10. テスト実行結果（自分の実行で確認）

| スイート | 結果 |
|---|---|
| ツールキット `make check`（12 スイート） | **1,584 passed, 0 failed**（M9 の 1,431 から +153: tk9 新規 150、tk8 +3） |
| test_tk7（通常実行） | 246 passed, 0 failed |
| test_tk7（TK7_DARK=1、winui_dark 全体再実行） | 246 passed, 0 failed |
| test_tk9（新規） | 150 passed, 0 failed |
| アプリ `make check`（無修正） | 1,048 passed, 0 failed（回帰ゼロ） |
| シェル `make check`（無修正） | 683 passed, 0 failed（従来 588 + マスコットラインの 95。回帰ゼロ） |
| OSK `make check`（無修正） | 319 passed, 0 failed（回帰ゼロ） |

変更ファイル: include/tk.h、src/tk_core.c、
src/tk_widgets.c、tests/test_tk7.c、tests/test_tk8.c、
tests/test_tk9.c（新規）、Makefile、AUDIT-toolkit.md、
本レポート。repo へは push していない。
