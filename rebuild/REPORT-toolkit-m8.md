# REPORT-toolkit-m8 — GUI ライブラリ同等化の継続（M8）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


2026-11-20。対象: `include/`・`src/`・`tests/` と AUDIT のみ。
repo へは push していない。正本マトリクスは AUDIT-toolkit.md §2。

このラインの目的は「完成」ではなく、能力マトリクスの検証済み
セル（●）を実測で押し上げること。以下は全て自分の実行で
確認した数字のみ。

## 1. セル増減表（モード別、AUDIT §2 の表から機械集計）

| モード | 時点 | ● | ◐ | ○ | △ | ✕ |
|---|---|---|---|---|---|---|
| Android | M7 | 75（32.8%） | 99 | 51 | 3 | 1 |
| Android | M8 | **76（33.2%）** | 103 | 50 | 0 | 0 |
| WinUI | M7 | 4（1.7%） | 89 | 103 | 32 | 1 |
| WinUI | M8 | **60（26.2%）** | 79 | 89 | 1 | 0 |
| 合計 | M7 | 79（17.2%） | 188 | 154 | 35 | 2 |
| 合計 | M8 | **136（29.7%）** | 182 | 139 | 1 | 0 |

内訳: ● +57（WinUI +56、Android +2、ただし #5 C の格下げ
2件を含むため純増は +59 − 2）。△ −34（検証で解消 27、
仕様取得のみで ○ へ移行 7）。✕ −2（両モードの #5 D）。

格下げも記録する: **#5 Split button の C は両モードとも
●→◐**。理由: アンカー（矢印）の再クリックがトグル閉じに
ならない。ルートの POINTER_DOWN がヒット判定前に
tk_close_all_menus を呼ぶため「解散→クリックで再オープン」
になる（MenuBar と同じ既存挙動）。開く・項目選択・外側
クリック解散・Esc 解散は検証済みだが、この経路が仕様と
違う以上 C を ● とは言わない。修正はルートの解散順序に
触るため本ラインでは行わず、残件として明記する。

## 2. 格上げの根拠（仕様出典＋テスト）

新規テストは tests/test_tk7.c（246 checks、全て
tk_theme_set_default でモードを切り替えた同一ツリーで実行）。

### 2.1 WinUI 仕様の取得（△ 32 の解消の土台）

WinUI の部品別寸法は Microsoft Learn のガイドページではなく
**microsoft/microsoft-ui-xaml リポジトリの themeresources
XAML** が実値の正本だった。取得先:

- https://github.com/microsoft/microsoft-ui-xaml
  （controls/dev/CommonStyles/ の部品別
  *_themeresources.xaml と controls/dev/ 直下の部品フォルダ）

取得した実値（effective px）: Button Padding 11,5,11,6 /
角丸 ControlCornerRadius 4、CheckBox 箱 20・グリフ 12、
RadioButton 外円 20・グリフ 12、ToggleSwitch トラック
40×20・ノブ 12、Slider 高さ 32・つまみ外 18/内 12・
トラック 4、TextBox パディング 10,5,6,6・境界 1（フォーカス
時下辺 2）、MenuFlyout 項目パディング 11,8,11,9・最小高 32、
ContentDialog パディング 24・最小 320×184・最大幅 548、
CommandBar 最小高 64 / compact 48、ListViewItem 最小高 40、
DatePicker/TimePicker フライアウト項目高 40、InfoBar 境界 1、
InfoBadge 最大高 16・値フォント 11、TabView ヘッダ
パディング 8,3,4,3、TreeView 項目最小高 28、SplitButton
Padding 11,6,11,7・主/副 35、ProgressBar 最小高 3・
トラック 1、ProgressRing ストローク 4、ComboBox 項目
パディング 11,5,11,7、ColorPicker 最小 312、NavigationView
項目マージン 4,2、AutoSuggestBox クエリボタン周辺値。
換算は従来どおり ×1.43（187ppi / 96dpi 基準）。

### 2.2 ● への格上げ（WinUI、テスト: test_tk7.c）

- #1 Button A/C/E/F: パディング換算（11epx×2×1.43≒32 =
  テキスト幅 + 4×unit）と measure が一致、角丸 6
  （4×1.43）、フォント 20（14×1.43）。フェイス色の実値、
  ラベルグリフ描画、押下で暗転、無効時不発、Fluent 二重線
  フォーカスリングのピクセル確認、Enter/Space 発火。
- #5 Split button D（両モード）: §3 の回転を参照。
  C は §1 の理由で ◐。
- #6 Checkbox A/C/F: 箱 28（20×1.43）が measure と描画で
  一致（§4 バグ2 の修正込み）。クリック切替＋通知、
  フォーカス＋Space。
- #7 RadioButton A/C/F: 外円 28・ドット 17（12×1.43）を
  ピクセル位置で確認（縁・ドット両端・隙間）。グループ
  排他、フォーカス＋Space。
- #8 Switch A/C/F: トラック 56×28・ノブ 17 が measure と
  描画で一致（§4 バグ2）。OFF/ON のピクセル、アニメーション
  後のノブ着地、Space 切替。
- #9 Slider C/F、#10 Range C/F: クリックで値設定、矢印で
  ステップ移動（Range はアクティブハンドル＋範囲ステップ）。
  A はトラック 6px（4×1.43）が仕様一致だがつまみ 28 対
  換算 26 のデルタで ◐。
- #11 Progress C、#12 Ring C: 値変更でフィル/弧のピクセル
  が動くこと、不確定スイープが時刻で進むこと。
- #13 TextField C/E/F: 入力・BS がバッファに反映、
  placeholder が淡色で描かれる（§4 バグ3 の修正を
  ピクセルで確認: 淡色ピクセル > 10・暗色 0）、Tab で
  フォーカスと入力先が移動。
- #14 TextView C、#15 SearchBar C: モード下での編集成立、
  候補フライアウトの開閉と選択確定。
- #17 Badge E: "99+" が "5" より広い（テキスト依存幅）。
- #19 ListView C/F、#20 TreeView C、#21 Menu C/F、
  #22 Dialog C、#25 Pickers C/F、#26 Snackbar C、
  #27 Toast C、#28 TabBar C/F、#29 BottomBar C、
  #30 NavRail C、#31 Drawer C、#32 Sheet C、
  #33 Segmented C、#34 Carousel C、#35 ColorPicker C、
  #3 FAB C、#4 FAB menu C、#38 長押し C、#39 tk_anim C:
  いずれもモード下で主要な振る舞い経路（クリック/キー/
  解散/期限/値の往復）をテストで確認したもの。
- #41 クリップボード C/E、#42 i18n C/E、#43 a11y C/E:
  モード下でコピー&ペーストの日本語往復（12 バイト一致）、
  ロケール切替で参照文字列が変わること、UIA 型名
  （CheckBox / RadioButton / ProgressBar）の完全一致と
  状態文字列 "checked" の収集。

### 2.3 ● への格上げ（Android）

- #5 D: WinUI と同じ split_flow を tk_theme_v2 で実行。
- #30 Navigation rail A: 幅 80dp を別経路で確認
  （pub.dev の material_design パッケージ
  M3NavigationSizes.railWidth トークンミラー）。実装
  94px = 80 × 187/160 と一致し、measure をテストで固定。

### 2.4 △ の解消（●/◐/○ への移行）

- WinUI A 列 31 件: 仕様取得（§2.1）＋テストで、仕様換算と
  一致するものは ●、記録済みデルタがあるものは ◐、
  テスト未達だが仕様取得済みのもの（#14 TextView、
  #23/#24 Dialog 派生形、#27 Toast、#31 Drawer、
  #32 Sheet、#35 ColorPicker の A）は ○ へ移行した。
  ○ への移行は「仕様は確認したが未検証」の意味であり、
  検証済みではない。
- Android #34 Carousel A △→◐: 公式ガイドライン
  （https://m3.material.io/components/carousel/guidelines）
  で「アイテムに固定幅は無い（レイアウトから決まる）」と
  確認。実装の 360×112 は設計選択としてテストで固定。
- Android #36 AppBar A △→◐: small 64dp（換算 75px）を
  M3 トークン表で確認。実装は control_h 56 で、デルタを
  テストコメントと本報告 §5 に明記して ◐。
- 残る △ は 1 件のみ（§5）。

## 3. ✕ の解消: Split button chevron 180° 回転

仕様: M3 split button ガイドライン
（https://m3.material.io/components/split-button/guidelines）
に「menu button は開閉時に内側へ 180° 回転する」「標準
モーションスキームで回転」とあり、**任意角**の回転が必要と
判断した（90° 単位では中間フレームが描けない）。

実装:
- `tk_canvas_draw_image_rot`（src/tk_draw.c）: 任意角・
  中心回転・逆写像＋最近傍。put/blend の意味論は既存
  blit_image と同一。割り当てなし。
- chevron 駆動（src/tk_m7.c）: tk_anim で 0..1000 →
  0..180°、200ms、tk_ease_in_out_cubic（Switch ノブと
  同じパターン）。アニメーション無効時は同期で最終値。
- 解散フック: tk_menu_close / tk_close_all_menus から
  tk_split_on_menu_closed を呼び、項目選択・外側クリック・
  Esc の全解散経路で chevron が閉位置へ戻る。

テスト（split_flow、両モード）: 閉位置の頂点ピクセル、
開アニメーション中間フレームで閉パターンが消えること、
終了時に上向き頂点、3 解散経路での復帰、モーション無効時
の即時スナップ。回転プリミティブ単体（test_rot）:
0/90/180/270/360° のドット移動が時計回りであること、
半透明画素のブレンド。

## 4. 発見・修正したバグ（全て自分のテストで再現→修正→再検証）

1. **metrics() のテーマ永久キャッシュ**（src/tk_widgets.c）:
   tk_leaf_measure が使う metrics() が初回のデフォルト
   テーマをプロセス寿命で保持し、tk_theme_set_default()
   によるモード切替後に measure が旧テーマで計算され
   続けた。2モード制の根幹の欠陥。実アプリは起動時に
   1 回だけ設定するため顕在化していなかった。キャッシュを
   外して現行デフォルトを読むよう修正。
2. **Checkbox / Radio / Switch の measure と WinUI 描画形の
   不一致**: 描画は 28px 箱 / 56px トラックだが measure は
   20px / 52px 前提で、WinUI モードでラベルが 8px / 4px
   はみ出していた。(style==2 ? 28 : 20) 等、実形に合わせて
   修正。Android 形の measure は従来どおりであることも
   テストで固定。
3. **TextField の placeholder が本文色（fg）で描画**:
   両参照とも淡色役割（M3 on-surface-variant / WinUI
   placeholder fill）のため text_dim に修正。tk7 の
   ピクセル条件（淡色あり・暗色なし）で検証。
4. **Makefile の .o 依存に src/tk_internal.h が無い**:
   構造体変更後に古い .o が混在し segfault する状態を
   実際に踏んだ。%.o / %.pic.o の依存に追加。以後
   tk_internal.h を触る変更はクリーンビルドで確認する。

あわせて WinUI 形状の仕様寄せ（バグではない調整）:
Radio のドット 10px → 17px（グリフ 12epx 換算）、
Switch のノブ 20px → 17px（同）。

## 5. 残った ◐ / ○ / △ と理由

△ は 1 件: **WinUI #2（elevated / トグル）A** —
ToggleButton 固有の themeresources を未取得。推測で
埋めない。あわせて TextControlThemeMinHeight の定義場所は
microsoft-ui-xaml 内で特定できなかった（参照のみ存在。
未確認として記録）。

◐ 止まりの主な理由（デルタ）:
- Slider つまみ径 28px（換算 26px）。Progress バー高 20px
  （WinUI の線形は ~4epx の線が本来形）。Ring は 5px
  ドット列（ストローク 4epx 換算 6px の連続弧ではない）。
- Dialog の padding 16（ContentDialog 24epx 換算 34）、
  幅は内容依存（最小 320epx 換算 458 の規定と違う）。
- Menu の行高 56・テキストインセット 8px/side（項目
  パディング換算 16px/side と違う）。
- ListView 行高 56（最小高換算 57、Δ1）。TreeView 行高は
  タッチフロア 56（項目最小 28epx 換算 40 と違う）。
- Pickers はインライン分割形（本来はフライアウト、
  項目高 40epx）。Badge は高さ 19（最大 16epx 換算 23 の
  範囲内だが寸法規定としては未照合）。
- AppBar 高さ 56（M3 small 換算 75 / CommandBar compact
  換算 69 のどちらとも違う、テーマ control_h の設計判断）。
- Switch の無効時トラック淡色化は未実装（B の欠落）。
- #5 C: アンカー再クリックのトグル閉じ（§1）。
- B 観点は全コンポーネントで ◐ 以下: 無効・ホバー・
  エラー等の全状態のピクセル網羅が未達のため一律に
  ● としていない（M7 の基準を維持）。
- D 観点の大半は ○: 部品固有のアニメーション検証が
  未実施（基盤 tk_anim と Split / Switch / Progress の
  スイープのみ検証済み）。

○ のままの主なもの: WinUI #23/#24 Dialog 派生形、
#31 Drawer、#32 Sheet、#35 ColorPicker の A/B、
#40 Drag & Drop の B/F 等 — 実装はあるがモード下の
テストが無い。

§9「等価形・近似」（REPORT-toolkit-complete.md）は
本ラインでは未着手: Drawer の standard 形（オーバーレイ
実装）、Carousel の離散スナップ、AppBar の scrolled 色、
SearchBar の composition 簡略形、Snackbar の WinUI 形
（severity なし）は全て近似のまま残る。

残セルの全一覧（● と — を除く、機械生成）:

### Android
- #1 Button（基本形・5バリアント）: B◐ D○ F◐
- #2 Button（elevated / トグル）: A◐ B◐ D○ E◐ F○
- #3 IconButton / FAB: A◐ B◐ D○ F○
- #4 FAB menu: A◐ B◐ D○ E◐ F○
- #5 Split button: A◐ B◐ C◐ E◐ F◐
- #6 Checkbox: A◐ B◐ D○ F◐
- #7 RadioButton: A◐ B◐ D○ F◐
- #8 Switch: A◐ B◐ D◐ F◐
- #9 Slider（単一）: A◐ B◐ D○ E◐
- #10 Slider（Range・value indicator）: A◐ B◐ D○ E◐ F◐
- #11 Progress（linear）: B◐ D◐
- #12 Progress（circular determinate = Ring）: A◐ B◐ D○ F○
- #13 TextField: B◐ D○
- #14 TextView（複数行）: A◐ B◐ D○
- #15 SearchBar / SearchView（docked）: A◐ B◐ D○ E◐ F◐
- #16 Chip: A◐ B◐ D○ F○
- #17 Badge: A◐ B◐
- #18 Card（elevated/filled/outlined）: A◐ B◐ D○ E◐ F○
- #19 ListView: A◐ B◐ D○
- #20 TreeView: A◐ B◐ D○ E◐ F◐
- #21 Menu: A◐ B◐ D◐ F◐
- #22 Dialog（basic / modal）: A◐ B◐ D◐ F◐
- #23 Dialog（modeless）: A◐ B◐ D○ E◐ F○
- #24 Dialog（full-screen）: A◐ B◐ D○ E◐ F○
- #25 Date / Time picker: A◐ B○ D○ E◐ F○
- #26 Snackbar: A◐ B◐ D◐ F○
- #27 Toast: A○ B○ D○
- #28 TabBar: A◐ B◐ D○ F◐
- #29 BottomBar / Navigation bar: A◐ D○ F○
- #30 Navigation rail: B◐ D○ E◐ F○
- #31 Navigation drawer: A◐ B◐ D○ E◐ F○
- #32 Bottom sheet: A◐ B◐ D○ E◐ F○
- #33 Segmented button: A◐ B◐ D○ F○
- #34 Carousel: A◐ B◐ D○ E◐ F○
- #35 ColorPicker: A◐ B◐ D○ F◐
- #36 AppBar: A◐ B◐ D○ F○
- #38 長押し: B◐
- #39 アニメーション基盤（tk_anim）: C◐ D◐
- #40 Drag & Drop: B◐ F○
- #43 アクセシビリティ: B◐
- #44 画像サムネイル / JPEG スケールデコード: B◐

### WinUI
- #1 Button: B◐ D○
- #2 Button（elevated / トグル）: A△ B○ C◐ D○ E○ F○
- #3 IconButton / FAB: A◐ B◐ D○ E◐ F○
- #4 FAB menu: A◐ B○ D○ E○ F○
- #5 Split button: A◐ B◐ C◐ E◐ F◐
- #6 Checkbox: B◐ D○ E◐
- #7 RadioButton: B◐ D○ E◐
- #8 Switch（ToggleSwitch）: B◐ D○ E◐
- #9 Slider: A◐ B◐ D○ E◐
- #10 Slider（Range）: A◐ B◐ D○ E○
- #11 Progress（ProgressBar）: A◐ B◐ D◐
- #12 Progress（ProgressRing）: A◐ B◐ D○ F○
- #13 TextField（TextBox）: A◐ B◐ D○
- #14 TextView: A○ B○ D○ E◐ F◐
- #15 SearchBar（AutoSuggestBox）: A◐ B◐ D○ E◐ F○
- #16 Chip: A◐ B◐ D○ E○ F○
- #17 Badge（InfoBadge）: A◐ B○ C◐
- #18 Card: A◐ B◐ C◐ D○ E◐ F○
- #19 ListView（ListView/GridView）: A◐ B◐ D○ E◐
- #20 TreeView: A◐ B○ D○ E○ F○
- #21 Menu（MenuFlyout）: A◐ B◐ D○ E◐
- #22 Dialog（ContentDialog）: A◐ B◐ D○ E◐ F○
- #23 Dialog（modeless）: A○ B○ C◐ D○ E○ F○
- #24 Dialog（full-screen）: A○ B○ C◐ D○ E○ F○
- #25 Date / Time picker: A◐ B○ D○ E○
- #26 Snackbar（InfoBar）: A◐ B◐ D○ E◐ F○
- #27 Toast: A○ B○ D○ E◐
- #28 TabBar（TabView）: A◐ B◐ D○ E◐
- #29 BottomBar（NavigationView 下配置相当）: A◐ B◐ D○ E◐ F○
- #30 Navigation rail（NavigationView 左）: A◐ B◐ D○ E○ F○
- #31 Navigation drawer: A○ B○ D○ E○ F○
- #32 Bottom sheet: A○ B○ D○ E○ F○
- #33 Segmented: A◐ B◐ D○ E◐ F○
- #34 Carousel（FlipView 相当）: A◐ B◐ D○ E○ F○
- #35 ColorPicker: A○ B○ D○ E◐ F○
- #36 AppBar（CommandBar 相当）: A◐ B○ C◐ D○ E○ F○
- #38 長押し: B○
- #39 アニメーション基盤: D◐
- #40 Drag & Drop: B○ C◐ F○
- #43 アクセシビリティ（UIA 型名）: B◐
- #44 画像サムネイル / JPEG: B◐

## 6. テスト結果（自分の実行、クリーンビルド後）

| スイート | 結果 |
|---|---|
| ツールキット `make check` | textcore 118 / tk 83 / tk2 121 / font 22 / kbd 131 / tk3 191 / tk4 119 / tk5 127 / tk6 205 / **tk7 246** = **1,363 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（M7 基準と一致） |
| シェル `make check` | **588 passed, 0 failed**（同上） |
| OSK `make check` | **319 passed, 0 failed**（同上） |

## 7. コピー / オーバーヘッド削減

- 回転プリミティブは割り当てなし・逆写像の単一パス。
  回転するのは 16×16 の chevron のみ、アニメーション中
  （200ms）のフレームだけであり、静止時の描画コストは
  従来の blit と同一経路（split_menu が無いボタンは
  従来どおり）。
- metrics() の修正はポインタ参照 1 回の追加のみ。
  measure はフレーム毎ではなくレイアウト時の処理。
- placeholder 色・measure の修正は算術のみで新規コストなし。
- 新規のキャッシュ・中間バッファは追加していない。

## 8. キャッシュ / メモリ管理

- 新規キャッシュなし。グリフキャッシュ等の既存予算は不変。
- tk_widget に chevron 用の 4 フィールドを追加
  （float + bool + ポインタ 2 = 約 24B/ウィジェット）。
  Split button 以外では未使用だが、構造体共通のため
  全ウィジェットに載る。件数規模（画面あたり数十個）では
  無視できるが、増分として記録する。
- テスト専用のフォント install は製品コードに影響しない。

## 9. 実機の現実への対応

- 永続状態・ファイル書込みの変更なし（SD / eMMC への
  影響なし）。時刻ベースの有効期限等も追加していない。
- chevron アニメーションは既存 tk_anim（壁時計基準、
  ルート tick 駆動）に載せ、アニメーション無効設定時は
  同期で最終値にスナップする — 抵抗膜・単一指・低速 CPU
  の実機で「アニメーション待ちで操作が止まる」形には
  していない。
- 回転は GPU 前提なし（ソフトウェア、最近傍、16×16 のみ）。

## 10. 未検証（明記）

- 実機（ARM / QEMU ゲスト）での実行は未実施。本ラインの
  検証は全てホスト上のテスト。
- WinUI ダークテーマ（tk_theme_winui_dark）では tk7 を
  実行していない（ライトのみ）。トークンは存在するが
  モード×ダークの組合せは未検証。
- WinUI #2 の A（ToggleButton リソース未取得）。
- §9 の近似 5 件（Drawer / Carousel / AppBar / SearchBar /
  Snackbar）は未着手のまま。
- #5 のアンカー再クリック・トグル化は未実施（§1）。
- Switch 無効時のトラック淡色化は未実装。
