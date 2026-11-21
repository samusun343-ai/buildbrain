# AUDIT-toolkit.md — libbrainui2 監査と作り直し（M6 監査 / M7 細分化マトリクス）

日付: 2026-11-20。対象: `include/`、`src/`、`tests/`（libbrainui2）。
方法: 過去レポート（M1〜M5）の主張は一切信用せず、「ヘッダに宣言があるか」
「実装が存在するか」「テストプログラムで実際に動くか」の3点で1件ずつ検証した。
能力マトリクスの評価軸と寸法の根拠は、全て今回 Web で確認した Material 3 /
Android 公式仕様に基づく（各項目に出典を付す）。記憶・過去 DESIGN の記述を
根拠にした評価は行っていない。

凡例: 真 = 実装があり自分の実行したテストで動作確認 / 部分 = 実在するが
主張どおりでない・未使用・未テスト / 虚偽 = 実態が存在しない / 未検証 = 確認できていない。

---

## 1. 主張の真偽表（フェーズ1: 過去レポートの検証）

### 1.1 テスト総数の主張

| 主張 | 証拠（自分の実行） | 判定 |
|---|---|---|
| ツールキット 764 tests PASS（M5 報告） | `make check` 自前実行: textcore 97 / tk 83 / tk2 121 / font 22 / kbd 131 / tk3 191 / tk4 119 = 764、0 failed | 真 |
| アプリ 1,048 tests PASS | apps `make check` 自前実行: 19 バイナリ合計 1,048、0 failed、exit=0 | 真 |
| シェル 588 / OSK 319 PASS | shell `make check` exit=0（588 集計）、osk `make check`: test_osk 319 passed, 0 failed、exit=0 | 真 |
| 公開 API は全て実装されている | 公開ヘッダ4点（tk.h / textcore.h / osk.h / tk_kbd.h）の宣言 392 関数を `nm` で照合: 全て libbrainui2.a に定義あり | 真（ただし下記注記） |
| （注記）392 関数のうち 119 関数はツールキット内テストから未呼出 | grep 照合。多は OSK 領分・バックエンド・消費者側テストで担保されるが、ツールキット単体では未検証のものを含む | 部分 |

### 1.2 REPORT-toolkit-m5.md の主張

| 主張 | 証拠 | 判定 |
|---|---|---|
| スクロールフレーム転送 731,200B → 24,350B（−96.7%） | bench_paint 自前実行: 731,200B → 24,550B（−96.6%）。バイト数は再現。24,350 との僅差あり | 真（数値に僅差） |
| 時計更新の damage 転送 −92.5% | bench_damage 自前実行で再現。ただし時間系の数値はホスト競合で分散が大きく、精密な再現性はない（桁は一致） | 真（時間値は参考値） |
| 描画カリング実装 | tk_core.c のサブツリー skip、ListView 行カリングを実読。60行ツリーで1ラベル再描画 0.067ms を実測 | 真 |
| アニメーション基盤 tk_anim 新設 | src/tk_anim.c 193行が実在。Switch ノブ / Menu 出現 / Dialog 出現への配線、解放時 cancel、シェル側ステップ連動を実読で確認 | 真 |
| セッタ自動 invalidate 全面化 | setter 的関数 83 件を機械抽出して本体確認。例外: コールバック登録、クリップボードプロバイダ、レイアウトパラメータ、textcore 側 | 真（例外明記） |
| ファイル選択ダイアログ | tk_widgets.c に実 opendir/readdir、dirs-first、拡張子フィルタ（大小無視）、256件上限。test_tk4 が実 /tmp 木で PASS。ただし tk_filedialog_refresh 単体はテスト未呼出 | 真（1関数は未テスト） |
| 日付/時刻ピッカー | グレゴリオ暦うるう年規則を実装、test_tk4 に 2024-02-29 等のテストあり PASS | 真 |
| ListView チェック行/見出し行 | 実装＋test_tk4 PASS | 真 |
| tc_find / tc_find_next（textcore 検索） | 実装＋test_tk4 PASS | 真 |
| **textcore の置換（replace）** | **tc_replace はコードベース全体に存在しなかった**（grep 皆無）。「検索・置換」として不足 | **虚偽（欠落）→ M6 で実装** |
| 画像サムネイル API | tk_image_scale_to_fit は実物で test_tk4 に4件のテストあり。しかし tk_image_load_thumbnail / tk_imageview_load_thumbnail は**全スイートにテストがなく、apps/shell/osk のどこからも使われていない**。load_thumbnail は全デコード後に縮小する方式でメモリピークは下がらない（M5 レポート自身が IDCT スケーリングデコードを「未処置」と記載）。bd-files は今も自前ヘッダスニッフで代替 | **部分（実務上は未解消）** |
| 密度トークン | tk_theme_density 実在、test_tk4 PASS | 真 |

### 1.3 DESIGN-toolkit-v2.md の主張

| 主張 | 証拠 | 判定 |
|---|---|---|
| §3 ウィジェット表: Slider は「連続値（float 0..1／範囲指定）」 | **範囲指定 API（slider_set_min/max/step 等）は全体 grep で皆無**。表の記載だけが存在した | **虚偽 → M6 で実装し実態に合わせた** |
| §9 自認: 非モーダル Dialog 未実装 | 実読で一致（今も未実装、§4 に残欠落として記載） | 正直な自認 |
| §9 自認: Grid セルスパンなし | 監査時点では一致 → M6 で実装 | 正直な自認（解消済み） |
| §9 自認: TreeView モデル IF 未一般化 | 今も未一般化 | 正直な自認 |
| §9 自認: ドラッグ＆ドロップなし、i18n なし、アクセシビリティなし | 全て今も該当（§4 参照） | 正直な自認 |
| M3 で TextView ドラッグ選択・ComboBox 編集可能・SpinButton 直接入力を追加 | test_tk3 にテストあり PASS | 真 |

### 1.4 利用側からの不足との突き合わせ

| 出典 | 不足 | 監査時点の状態 | M6 後 |
|---|---|---|---|
| apps wave1 §5 | textcore 検索は find のみ、replace なし | 一致（欠落） | 実装済み |
| apps wave1 §5 | サムネイル API が実務で使えない | 一致（§1.2） | 未解消（§4） |
| apps wave2 §8 | tk_button_set_label 不在 | 後に追加済み（実在を確認） | — |
| apps wave2 §8 | 行ドラッグ並べ替え不在 | move_row モデル API は追加済み、ドラッグ UI はなし | 未解消（§4） |
| ブラウザ §6-1 | tk_button は子ウィジェットをコンテナ配置できない（タブ内 × が溢れた） | 一致（欠落） | 未解消（§4） |
| ブラウザ §6-2 | オーバーレイだけを再描画する公開 API がない | 一致（欠落） | tk_root_paint_overlays 実装済み |
| ブラウザ §6-3 | ボタンのテキスト整列 API がない | 一致（欠落） | tk_button_set_text_align 実装済み |
| DESIGN-apps-envfit §6-1 | filled/tonal/outlined/text が第一級 API でない（accent 描画はアイコンを描かない、tonal は裏技） | 一致 | tk_button_set_variant 実装済み（FILLED でアイコン描画をテストで確認） |
| DESIGN-apps-envfit §6-2 | 横並び「アイコン＋ラベル」のボタン形式が無い | 一致 | TK_BTN_ICON_ROW 実装済み |
| DESIGN-apps-envfit §6-3 | FAB 用の汎用オーバーレイ配置が無い | 一致 | tk_root_add_floating + tk_fab_new 実装済み |
| DESIGN-apps-envfit §6-4 | インジケータ付きタブの定型が無い | 一致 | TK_W_TABBAR 実装済み |
| DESIGN-apps-envfit §6-5 | 下部バーの定型が無い | 一致 | tk_bottombar_* 実装済み |
| DESIGN-apps-envfit §6-6 | メニューがテキスト項目のみ | 一致 | tk_menu_set_item_icon 実装済み |

---

## 2. 能力マトリクス（細分化版、M7 で作り替え）

粒度: コンポーネント × モード（Android / WinUI）× 観点6種。
観点: **A** 寸法・余白 / **B** 全状態の見た目（通常・押下・フォーカス・
無効・エラー等）/ **C** 振る舞い / **D** アニメーション /
**E** テキスト・アイコンの扱い / **F** キー操作・フォーカスの回り方。

状態コード（自分の実行で確認した範囲のみを申告する）:
- **●** テストで検証済み（このセッションで実行したテストに該当チェックがある）
- **◐** 部分検証（存在と主要経路のみ。状態網羅・ピクセル全数等は未検証）
- **○** 実装済み・未検証
- **△** 仕様未確認（公式ページで数値・文言を取得できていない。推測で埋めない）
- **✕** 未実装
- **—** その観点が存在しない（分母から除外）

**読み方（過大評価の禁止）**: ● は「その観点についてテストが存在し緑だった」
ことだけを意味し、参照実装との完全一致を意味しない。現状の大半は ◐/○ で
あり、これは「ごく初期段階」というユーザー評価（2026-11-20 17:34）と
整合する。集計は本節末尾。

### 2.1 Android モード（Material 3 / tk_theme_v2 系）

| # | コンポーネント | A | B | C | D | E | F |
|---|---|---|---|---|---|---|---|
| 1 | Button（基本形・5バリアント） | ● | ● | ● | ● | ● | ● |
| 2 | Button（elevated / トグル） | ● | ● | ● | ● | ● | ● |
| 3 | IconButton / FAB | ● | ● | ● | ● | ● | ● |
| 4 | FAB menu | ● | ● | ● | ● | ● | ● |
| 5 | Split button | ● | ● | ● | ● | ● | ● |
| 6 | Checkbox | ● | ● | ● | ● | ● | ● |
| 7 | RadioButton | ● | ● | ● | ● | ● | ● |
| 8 | Switch | ● | ● | ● | ● | ● | ● |
| 9 | Slider（単一） | ● | ● | ● | ● | ● | ● |
| 10 | Slider（Range・value indicator） | ● | ● | ● | ● | ● | ● |
| 11 | Progress（linear） | ● | ● | ● | ● | — | — |
| 12 | Progress（circular determinate = Ring） | ● | ● | ● | ● | — | — |
| 13 | TextField | ● | ● | ● | ○ | ● | ● |
| 14 | TextView（複数行） | ◐ | ● | ● | ● | ● | ● |
| 15 | SearchBar / SearchView（docked） | ● | ● | ● | ◐ | ● | ● |
| 16 | Chip | ● | ● | ● | ● | ● | ● |
| 17 | Badge | ● | ● | ● | — | ● | — |
| 18 | Card（elevated/filled/outlined） | ● | ● | ● | ● | ● | ● |
| 19 | ListView | ● | ● | ● | ● | ● | ● |
| 20 | TreeView | ◐ | ● | ● | ○ | ● | ● |
| 21 | Menu | ● | ● | ● | ● | ● | ● |
| 22 | Dialog（basic / modal） | ● | ● | ● | ● | ● | ● |
| 23 | Dialog（modeless） | ● | ● | ● | ● | ● | ● |
| 24 | Dialog（full-screen） | ● | ● | ● | ● | ● | ● |
| 25 | Date / Time picker | ◐ | ● | ● | ○ | ● | ● |
| 26 | Snackbar | ● | ● | ● | ● | ● | ● |
| 27 | Toast | ● | ● | ● | ● | ● | — |
| 28 | TabBar | ● | ● | ● | ● | ● | ● |
| 29 | BottomBar / Navigation bar | ● | ● | ● | ● | ● | ● |
| 30 | Navigation rail | ● | ● | ● | ● | ● | ● |
| 31 | Navigation drawer | ● | ● | ● | ● | ● | ● |
| 32 | Bottom sheet | ● | ● | ● | ● | ● | ● |
| 33 | Segmented button | ● | ● | ● | ● | ● | ● |
| 34 | Carousel | ● | ● | ● | ● | ● | ● |
| 35 | ColorPicker | ◐ | ● | ● | ○ | ● | ● |
| 36 | AppBar | ● | ● | ● | ● | ● | ● |
| 37 | テーマ / トークン（light・dark） | ● | ● | ● | — | — | — |
| 38 | 長押し | — | ● | ● | — | — | — |
| 39 | アニメーション基盤（tk_anim） | — | — | ● | ● | — | — |
| 40 | Drag & Drop | — | ● | ● | — | — | — |
| 41 | 共有クリップボード | — | — | ● | — | ● | — |
| 42 | i18n | — | — | ● | — | ● | — |
| 43 | アクセシビリティ | — | ● | ● | — | ● | — |
| 44 | 画像サムネイル / JPEG スケールデコード | — | ● | ● | — | — | — |

### 2.2 WinUI モード（Fluent / tk_theme_winui 系）

寸法の扱い: WinUI 参照寸法（effective px、96dpi 前提）を実機
に合わせ換算したトークンで実装（M8 当時は 187ppi・800×480
前提で ×1.43。前提訂正 §20 により現行は 854×480・約179.2ppi、
×1.344）。
M8（2026-11-20）で部品別寸法を microsoft/microsoft-ui-xaml
リポジトリの themeresources XAML（部品別ファイル）から取得し、
tk7（246 checks）でモード切替した同一ツリーの寸法・状態・
振る舞いを検証した。A 列は仕様換算値と一致するものは ●、
タッチフロア等の記録済みデルタがあるものは ◐ とする。

| # | コンポーネント | A | B | C | D | E | F |
|---|---|---|---|---|---|---|---|
| 1 | Button | ● | ● | ● | ● | ● | ● |
| 2 | Button（elevated / トグル） | ● | ● | ● | ● | ● | ● |
| 3 | IconButton / FAB | ◐ | ● | ● | ● | ● | ● |
| 4 | FAB menu | ◐ | ● | ● | ● | ● | ● |
| 5 | Split button | ● | ● | ● | ● | ● | ● |
| 6 | Checkbox | ● | ● | ● | ● | ● | ● |
| 7 | RadioButton | ● | ● | ● | ● | ● | ● |
| 8 | Switch（ToggleSwitch） | ● | ● | ● | ● | ● | ● |
| 9 | Slider | ● | ● | ● | ● | ● | ● |
| 10 | Slider（Range） | ● | ● | ● | ● | ● | ● |
| 11 | Progress（ProgressBar） | ● | ● | ● | ● | — | — |
| 12 | Progress（ProgressRing） | ● | ● | ● | ● | — | — |
| 13 | TextField（TextBox） | ● | ● | ● | ● | ● | ● |
| 14 | TextView | ◐ | ● | ● | ● | ● | ● |
| 15 | SearchBar（AutoSuggestBox） | ● | ● | ● | ◐ | ● | ● |
| 16 | Chip | ◐ | ● | ● | ◐ | ● | ● |
| 17 | Badge（InfoBadge） | ◐ | ● | ● | — | ● | — |
| 18 | Card | ◐ | ● | ● | ○ | ● | ● |
| 19 | ListView（ListView/GridView） | ● | ● | ● | ○ | ● | ● |
| 20 | TreeView | ◐ | ● | ● | ● | ● | ● |
| 21 | Menu（MenuFlyout） | ● | ● | ● | ● | ● | ● |
| 22 | Dialog（ContentDialog） | ● | ● | ● | ● | ● | ● |
| 23 | Dialog（modeless） | ● | ● | ● | ● | ● | ● |
| 24 | Dialog（full-screen） | ● | ● | ● | ● | ● | ● |
| 25 | Date / Time picker | ◐ | ● | ● | ○ | ● | ● |
| 26 | Snackbar（InfoBar） | ● | ● | ● | ● | ● | ● |
| 27 | Toast | ○ | ● | ● | ● | ● | — |
| 28 | TabBar（TabView） | ◐ | ● | ● | ● | ● | ● |
| 29 | BottomBar（NavigationView 下配置相当） | ◐ | ● | ● | ● | ● | ● |
| 30 | Navigation rail（NavigationView 左） | ◐ | ● | ● | ● | ● | ● |
| 31 | Navigation drawer | ◐ | ● | ● | ● | ● | ● |
| 32 | Bottom sheet | ◐ | ● | ● | ● | ● | ● |
| 33 | Segmented | ◐ | ● | ● | ◐ | ● | ● |
| 34 | Carousel（FlipView 相当） | ◐ | ● | ● | ● | ● | ● |
| 35 | ColorPicker | ○ | ● | ● | ● | ● | ● |
| 36 | AppBar（CommandBar 相当） | ● | ● | ● | ● | ● | ● |
| 37 | テーマ / トークン（light・dark） | ● | ● | ● | — | — | — |
| 38 | 長押し | — | ● | ● | — | — | — |
| 39 | アニメーション基盤 | — | — | ● | ● | — | — |
| 40 | Drag & Drop | — | ● | ● | — | — | — |
| 41 | 共有クリップボード | — | — | ● | — | ● | — |
| 42 | i18n | — | — | ● | — | ● | — |
| 43 | アクセシビリティ（UIA 型名） | — | ● | ● | — | ● | — |
| 44 | 画像サムネイル / JPEG | — | ● | ● | — | — | — |

### 2.3 集計（このファイルの表から機械集計）

集計値は表の記号を数えたもので、印象値ではない（集計コマンドと
結果は REPORT-toolkit-complete.md §5 に記録）。

| モード | 適用セル | ● 検証済み | ◐ 部分検証 | ○ 実装のみ | △ 仕様未確認 | ✕ 未実装 |
|---|---|---|---|---|---|---|
| Android | 227 | 218（96.0%） | 5 | 4 | 0 | 0 |
| WinUI | 227 | 204（89.9%） | 18 | 5 | 0 | 0 |
| 合計 | 454 | **422（93.0%）** | 23 | 9 | 0 | 0 |

M23 時点の集計（§23 参照）。M21 時点（M22 は分類不動）は
● 411（90.5%）・◐ 34・○ 9。M23 の増減は (a) 11件の A セル ◐ → ● の
11件のみ: ユーザーが 2026-11-21 に (a) 全件を現状で
認定した（#29 の選択表示のみ差し戻し → 是正完了後に
認定へ合流）。分母 454 は不変。

M21 時点の集計（§19 参照）。M20 時点は ● 410（90.3%）・
◐ 35・○ 9。M21 の増減は Android #12 A の ◐ → ● の
1件のみ（ring_arc のスタンプ中心の丸めで、◐ の唯一の
根拠だったインク帯 +1px が解消し、公称値でピンした）。
M20 の増減は #21 Menu の D 両モード
◐ → ● の2件のみ（基盤へのオフスクリーン捕獲 + scale
blit の導入で、§16.5 が「基盤側にオフスクリーン面が
入る場合はこの限りでない」としていた条件を満たした）。

M14 時点の集計（表から機械集計）。適用セルが 458 → 456 に
減ったのは訂正1件: #12 Progress（circular）の F は
表示専用ウィジェットにキー操作が存在せず、#11・#17 と
同じく「—」が正しい（M14 §12 参照）。M13 時点は
● 238（52.0%）・◐ 128・○ 92、M12 時点は ● 201（43.9%）・
◐ 146・○ 111、M11 時点は ● 190（41.5%）・
◐ 144・○ 124、M10 時点は ● 167（36.5%）・
◐ 165・○ 126、M9 時点は ● 155（33.8%）・◐ 164・○ 139、
M8 時点は ● 136（29.7%）・◐ 182・○ 139・△ 1、M7 時点は
● 79（17.2%）・◐ 188・○ 154・△ 35・✕ 2 だった。
M15 時点の集計は ● 361（79.5%）・◐ 58・○ 35（適用
454）。適用セルが 456 → 454 に減ったのは訂正1件:
#40 Drag & Drop の F は両参照ともキー操作による
ドラッグを定義しておらず「—」が正しい（§13 参照）。
M16 時点の集計は ● 375（82.6%）・◐ 53・○ 26（適用
454、分母の変更なし）: D の実装解消（#29/#30 両
モード）と不変性検証（#13 WinUI、#14・#36 両モード）、
E の網羅（#9 両モード、WinUI #3・#19）、C の両テーマ
対称検証（Android #39）による +14 件。詳細は §14 と
REPORT-toolkit-m16.md に置く。
M17 時点の集計は ● 387（85.2%）・◐ 53・○ 14（適用
454、分母の変更なし）: D の実装解消（Android
#1/#2/#3 ripple、#16 チェック出現 + ripple、#18
カード ripple、#33 チェック出現、WinUI #1/#2/#3
ブラシ遷移）と不変性検証（Android #19、WinUI
#20・#35）による +12 件。#21 D はフェード実装後も
◐（scale 成分の再現不能、§15.2）。A は分類監査を
実施し (a) 文書化された適応 7件 / (b) 正当化できない
逸脱 0件 / (c) 逸脱の記録なし・寸法テスト未網羅
44件 / ○ 3件は仕様不在・形の相違で据え置き（§15.4）。
詳細は §15 と REPORT-toolkit-m17.md に置く。
M18 時点の集計は ● 408（89.9%）・◐ 35・○ 11（適用
454、分母の変更なし）: A (c) 44件の寸法テスト完遂
（tk17、148 checks）による ● +21件（Android
#2/#4/#5/#15/#18/#23/#24/#26/#28/#32/#33/#34 と
#27 の ○ 解消、WinUI #2/#5/#10/#12/#13/#15/#23/
#24）、(a) への移動 4件（Android #6/#7/#9/#10）、
D #15 両モードの ○ → ◐（候補ドロップダウンの入場
フェードがメニュー経路で実装済みだったことを確認・
テスト化）。仕様駆動の寸法修正 6件（F1〜F6）と
Split の実在バグ修正 1件を含む。詳細は §16 と
REPORT-toolkit-m18.md に置く。本書は「完成」を
主張しない。
M19 時点の集計は ● 408（89.9%）・◐ 37・○ 9（適用
454、分母・● の変更なし）: 残り46セルの全件再点検で、
未実施だった検証を2件実施した（WinUI #16/#33 の D、
追随先テンプレートの遷移を部品固有経路で検証し
○ → ◐。第一者部品が無いため ● にはしない）。
あわせて sdk-check（make check の対象外だった）が
M7 の寸法変更以降ドリフトしていたことを発見し、
サンプル側の前提を修正して make check に組み込んだ
（§17）。詳細は §17 と REPORT-toolkit-m19.md に置く。
本書は「完成」を主張しない。
格上げ根拠は REPORT-toolkit-m15.md に置く。M14 の格上げ根拠は REPORT-toolkit-m14.md に
置く。M13 の格上げ根拠は REPORT-toolkit-m13.md に
置く。M12 の格上げ根拠は REPORT-toolkit-m12.md に
置く。M11 の格上げ根拠は REPORT-toolkit-m11.md に置く。
M10 の格上げ根拠は REPORT-toolkit-m10.md に置く。
M9 の格上げ根拠は REPORT-toolkit-m9.md に置く。M8 で
格下げした #5 Split button の C（アンカー再クリックが
トグル閉じにならない既存挙動）は M9 で根本修正し、
両モードとも ● に戻した。残り △ 1件（WinUI ToggleButton
の A）は一次ソース取得により解消し ◐ とした（専用寸法
テストは未実施のため ● にはしない）。

### 2.4 仕様未確認（△）の内訳 — 推測で埋めていない項目

- M3 タイポグラフィ type scale: 公式ページ
  （https://m3.material.io/styles/typography/type-scale-tokens）を
  2026-11-20 に再取得したが本文が空（タイトル行のみ）。
  文字サイズの根拠は DESIGN-apps-envfit.md の実測と caption 下限
  12sp≈14px に依拠しており、type scale 全段の照合は未了。
  **M18 で照合を実施し解消**: androidx/material3 の
  TypeScaleTokens（一次ソース）全15段を取得し、ツール
  キットの5スタイルと突き合わせた（結果は §16.3）。
  未了だったのは Web ページ経由の取得で、トークン
  ソース自体は取得可能だった。以後この項目は閉じる。
- Navigation rail / App bars の M3 寸法数値: M8 で別経路から
  確認済み（rail 幅 80dp は M3 トークンのミラー、top app bar
  small 64dp / medium 112dp / large 152dp は M3 トークン表を
  載せた複数ソースで一致）。△ は解消し、rail A は ●。
  AppBar A のデルタ（実装 56px）は M9 で解消: テーマトークン
  appbar_h（M3 系 75px / WinUI 系 69px）を新設し両モード ●。
  あわせて scrolled 色を surfaceContainerLow から
  surfaceContainer（container_mid トークン新設）に是正し
  Android B も ●（light/dark 両方でピクセル検証）。
- WinUI ToggleButton の固有リソース: M9 で一次ソース取得
  （microsoft-ui-xaml の ToggleButton_themeresources.xaml）。
  Style の Padding は ButtonPadding（Button と同一リソース）
  で、寸法は Button と同一と確定。△ は解消し A は ◐
  （Button と共有の寸法経路で部分検証。トグル専用の寸法
  テストは未実施）。
- ListView 行高の Δ1（56 対 57、M8 で記録）: M9 で扱いを
  確定。57 は 40epx × 1.43 の素の換算で、×1.40 の着地は
  Switch トラック 40epx → 56px（M8 で ● 判定）と同一の
  丸め帯域内にある。実機のタッチフロア（行高 ≥56px）とも
  一致するため、56px は換算誤差内の適応として記録し、
  テーマ値は変更しない。セルは ◐ のまま（ピクセル全数
  検証が未了のため格上げはしない）。
- Carousel のアイテム幅: 公式ガイドラインで「固定幅は無い
  （レイアウトから決まる）」と確認済み。△ は解消。
  M9 でアイテム幅をスクロール量に対して連続化し（仕様の
  "items change size as they move" の形）、離散スナップの
  近似は解消した。Android B は ●、A/C は従来どおり。
- WinUI の部品別寸法: M8 で microsoft-ui-xaml リポジトリの
  themeresources XAML から部品別に取得済み。残る △ は
  WinUI #2（elevated / トグル）の A のみ —
  ToggleButton 固有リソースを未取得のため。
  あわせて TextControlThemeMinHeight の定義場所は同リポジトリ
  内で特定できなかった（TextBox.xaml は参照のみ。OS 側の
  リソース定義と推定されるが未確認 — 推測で埋めない）。

- ProgressBar トラック色 / InfoBar Attention 色（M9 で
  「値が取得できない近似」として記録）: M10 で新たな出典
  （Microsoft Learn の Fluent テーマカラートークン表、
  2026-11-20 取得）により両方とも実値を取得し、仕様値化
  した。ProgressBar トラック = ControlStrongStroke
  ColorDefault（#9C000000 / #9AFFFFFF）、InfoBar の
  Informational = SystemFillColorAttention
  （#FF005FB7 / #FF60CDFF）+ AttentionBackground
  （#80F6F6F6 / #08FFFFFF）。据え置き項目の「新たな出典が
  得られた場合のみ再訪」の条件を満たす再訪であり、
  テーマトークン（strong_stroke / sys_attn / sys_attn_bg）
  として実装し tk7 / tk8 でピクセル検証した。あわせて
  同表から ControlFill Secondary/Tertiary/Disabled、
  TextFillColorDisabled、ControlStrongFill、AccentFill
  Disabled 等の状態値を取得し、§8 の状態形実装の根拠とした。
- M3 の状態層・無効不透明度: M10 で公式 States ページを
  取得し直し（Hover 8% / Focus 10% / Pressed 10% /
  Dragged 16%、無効は内容 38% / 容器 12%）、押下・無効の
  状態形をこの枠組みで実装・検証した（§8）。本機は抵抗膜
  1本指でホバーが物理的に無いため、Hover は押下・フォーカス
  時の状態層で代替する（従来ラインの扱いを継続）。

## 3. 寸法の根拠（実機適合）

**前提訂正（2026-11-21、§20 / §22）**: 本節の前提（5型
800×480・187ppi）は誤りだった。実機は 854×480・表示域
121×68mm・約 **179.2ppi**、1px = 0.1417mm。現行の換算は
1dp = 1.12px → **48dp = 53.8px ≒ 54px（7.62mm）** で、
タッチフロア・control_h・row_h は M22 で 54px に、
キャプション下限は 13px に再導出済み。以下は M6 時点の
導出記録であり、数値部の正本は §20 / §22。

実機（M6 時点の前提）: 5型 800×480 → 対角 933px / 5in ≈ 187ppi、1px = 0.136mm。

- タッチターゲット最小 48×48dp、間隔 ≥8dp
  （https://support.google.com/accessibility/android/answer/7101858 、
  https://m3.material.io/foundations/designing/structure 、
  developer.android.com の Compose API defaults で確認）。
  換算（M6 時点）: 1dp = 187/160 ≈ 1.169px → 48dp = 56px（7.62mm）。
  本機で 48px は 6.5mm しかなく、ガイドラインの物理寸法を満たさない。
- 対応（M6）: 製品テーマ tk_theme_v2 の control_h を 48 → **56**、
  font_caption を 13 → **14**（キャプション下限 12sp ≈ 14px）に変更。
  classic テーマ（tk_theme_light）は互換のため不変。
  検証: test_tk5 の test_theme_v2_fit、test_tk4 の density 期待値を
  新ベース（56×5/4=70）に更新して全スイート PASS。
  （現行値は M22 で control_h・row_h 54px、キャプション下限
  13px に再導出済み、§22。）
  DESIGN-apps-envfit §3 の寸法規程（M23 再導出: 一次タップ対象
  ≥54px、ヘッダ 54px、行高 70/54px、本文下限 16px）とも一致する。
- 個別寸法: バッジ高 19px（仕様 16dp。新換算 17.9px に対し
  +1 の残差、(a-8)）、チップ高は M3 形 37px（仕様 32dp。
  新換算 35.8px に対し +1 の残差、(a-7)）/ WinUI・classic 形
  40px（共有形、§19）、FAB 既定 65px（仕様 56dp。新換算
  62.7px に対し +2 の残差、(a-1)、§19）、ボトムバー総高は
  M22 実測で M3 73px / WinUI 77px（envfit §3 の帯域内、(a-10)）。

タイポグラフィの type scale ページ（https://m3.material.io/styles/typography/type-scale-tokens）
は取得時に本文が空で**未確認**。文字サイズの根拠は envfit §3 と
上記キャプション下限のみに依っている。

---

## 4. M6 残欠落への M7 対応状況（2026-11-20）

M6 時点の §4 リスト16項目は、全項目に実装と新規テスト（test_tk6、
205 件）が付いた。ただし「API が存在し主要経路のテストが緑」であり、
§2 の細分化マトリクスが示すとおり観点単位では ◐/○ が大半である。
「解消」はこの意味に限る（完全同等の主張ではない）。

1. **ダークテーマ** → tk_theme_v2_dark / tk_theme_winui_dark を新設
   （M3 ダーク実値、WinUI ダーク実値）。4テーマで同一ツリーを描画する
   テストあり。→ §2 #37 は ●、ただし各部品のダーク時ピクセルは未網羅。
2. **検索バー専用部品** → TK_W_SEARCHBAR（候補ドロップダウン付き、
   WinUI では AutoSuggestBox 形）。composition 描画は簡略形。
3. **Card 専用種別** → TK_W_CARD（elevated/filled/outlined、
   clickable、子配置可）。
4. **ドラッグ＆ドロップ** → ルート駆動 DnD（drag_mime/drop_cb、
   ゴースト描画）＋ ListView 行ドラッグ並べ替え。
5. **サムネイルのメモリピーク** → 自前ベースライン JPEG デコーダ
   （src/tk_jpeg.c、スケール IDCT 1/2・1/4・1/8）。1600×1200 →
   200×150 でピーク 285,000B（従来 stb 経路 15,480,000B の約 1/54）。
   参照（PIL/libjpeg）との画素誤差はスケールで増える（§6 参照）。
6. **共有クリップボード** → tk_clipboard_*（プロセス内単一クリップ、
   textcore プロバイダとして全テキスト部品に接続）。
7. **バリデータ/オートコンプリート** → TextField に validator、
   prefix/suffix、文字数カウンタ＋最大長、候補補完（threshold 式）。
8. **i18n/アクセシビリティ** → tk_i18n（ロケールフォールバック、
   複数形）、tk_a11y（ノード収集、Android クラス名/UIA 型名の
   両マッピング、live region 相当 announce）。
9. **非モーダル/full-screen Dialog** → tk_dialog_show_modeless、
   tk_dialog_set_fullscreen。
10. **TreeView モデル IF 一般化** → tk_tree_model（child_count /
    child_id / label のコールバック IF、展開状態はモデル id で保持、
    model_refresh で再読込）。
11. **ボタンへの子配置** → Button が子ウィジェットを右寄せ・縦中央に
    配置（タブ内 × ボタンの形が可能）。
12. **FAB menu / Segmented / Carousel / Drawer・Rail / Bottom sheet**
    → 全て専用種別として実装（FAB menu と Split は合成）。
    留保: Split の chevron 180° 回転は canvas に回転プリミティブが
    無く未実装（§2 #5 の D = ✕）。Drawer の standard 形はオーバーレイ
    実装のためヒット順がツリーより上になる。
13. **Slider Range / 円形 determinate** → デュアルハンドル＋
    value indicator バブル（inverse 色）、TK_W_RING。
14. **ColorPicker hex・alpha** → hex 入力欄（6桁=不透明、8桁=alpha
    指定）と alpha スライダ行。
15. **Snackbar スワイプ消去** → 横スワイプで dismiss（しきい値
    超過で cb1 発火）。
16. **inverse トークン** → inverse_surface / inverse_on_surface /
    inverse_primary を全テーマに実値で追加（Snackbar/Toast/
    value indicator が使用）。

新たに判明し M7 で修正した実在バグ（回帰テストで発見）:
- tk_root_free の use-after-free（root 内蔵 damage store を top /
  overlays が借用。解放順で UAF、ASan で確認し修正）。
- 候補メニューが開いた直後に閉じる（tk_menu_popup 内のフォーカス
  移動が owner へ FOCUS_OUT を配送し open_sub を閉じていた）。
- tk_textfield_validate の結果が is_error に反映されない不整合。
- a11y のスライダー状態が常に "0%"（非 range 時の span=0）。
- tk_theme_v2_dark.surface の誤値（containerLow の値が入っていた）。
- BottomBar の選択フェイスがデフォルトテーマの色を焼き込んでいた
  （実行時テーマ切替に追随しない）。描画テーマの container を
  参照する動的フェイス（btn_face_container）に変更。
- ColorPicker の 6桁 hex 入力で前値の alpha が残る問題
  （6桁=不透明に統一）。

---

## 5. M6 の実装とテスト証拠（フェーズ2）

新規・変更ファイル: include/tk.h、include/textcore.h、src/tk_core.c、
src/tk_widgets.c、src/tk_internal.h、src/textcore.c、tests/test_tk5.c（新規）、
tests/test_textcore.c（+21件）、tests/test_tk4.c（density 期待値1行を
テーマ変更に追随）、Makefile（test_tk5 配線）。

自分の実行による最終結果（2026-11-20）:

| スイート | 結果 |
|---|---|
| ツールキット `make check` | textcore 118 / tk 83 / tk2 121 / font 22 / kbd 131 / tk3 191 / tk4 119 / **tk5 127** = **912 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（監査前の基準と一致、回帰ゼロ） |
| シェル `make check` | **588 passed, 0 failed**（同上） |
| OSK `make check` | **319 passed, 0 failed**（同上） |

test_tk5 の内訳: ボタンバリアント（ピクセルで塗り確認、FILLED でアイコンが
描かれること）、ROW 配置、テキスト整列（中央 = 従来描画とピクセル一致を
含む）、FAB（寸法・配置・タップ）、スライダー範囲（量子化・キー・ドラッグ
スナップ）、不定プログレス/スピナー（時間経過で描画が動くこと）、チップ
（filter 切替・input close のゾーン判定）、バッジ（0 で不可視・99+）、
タブバー（等幅・選択・インジケータ位置・バッジ）、メニューアイコン
（幅と描画）、テキストフィールド supporting/error（measure 加算・danger
描画）、TextView 行タップ、ListView 複数選択、グリッドスパン
（「0」キー二重幅の配置）、長押し（600ms 発火・クリック抑制・slop
キャンセル）、トースト（表示・期限消去）、スナックバー（アクション発火・
本体タップ消去・indefinite）、tk_root_paint_overlays、ボトムバー
（選択の tonal 塗り・バッジ・高さ）、カラーピッカー（スウォッチ選択→
OK で値が届くこと）、v2 テーマの実寸トークン。

### M6 の挙動変更（互換性メモ）

- グリッド: 不可視の子はセルを占有しなくなった（旧実装は不可視でも配置）。
  配置はカーソル式フロー（穴の後方埋めなし）。全スイート回帰ゼロで確認。
- ボタン描画: 既定（LEGACY バリアント、整列 center）の描画は従来と
  ピクセル一致（test_tk5 で検証）。
- tk_theme_v2: control_h 48→56、font_caption 13→14（§3 の根拠）。
  製品側のレイアウトはこの分だけ縦に伸びる。

---

## 6. M7 の実装とテスト証拠

新規・変更ファイル: include/tk.h（M7 API 群）、src/tk_m7.c（新規:
新ウィジェット9種・DnD・クリップボード・i18n・a11y・候補補完）、
src/tk_jpeg.c（新規: ベースライン JPEG スケールデコード）、
src/tk_widgets.c（TextField/Slider/Button/Menu/ListView/Dialog/
ColorPicker/TreeView の M7 拡張、BottomBar 動的フェイス）、
src/tk_core.c（テーマ4種、tk_root_free の UAF 修正）、
src/tk_internal.h、src/tk_image.c（サムネイル経路配線、
tk_image_last_decode_peak の実体化）、tests/test_tk6.c（新規
205 件）、tests/fixtures/（JPEG フィクスチャ6種＋PIL 参照 RGB）、
Makefile（tk_m7.c / tk_jpeg.c / test_tk6 配線）。

自分の実行による結果（2026-11-20）:

| スイート | 結果 |
|---|---|
| ツールキット `make check` | textcore 118 / tk 83 / tk2 121 / font 22 / kbd 131 / tk3 191 / tk4 119 / tk5 127 / **tk6 205** = **1,117 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（M6 基準と一致、回帰ゼロ） |
| シェル `make check` | **588 passed, 0 failed**（同上） |
| OSK `make check` | **319 passed, 0 failed**（同上） |

JPEG デコーダの参照照合（PIL/libjpeg、全デコード＋box 縮小が参照）:
grad320 scale1 mean 0.232 / max 3、1/2: 0.632 / 11、1/4: 1.587 / 29、
1/8: 4.288 / 48（単位: 8bit 画素値）。1/8 は色差が DC 主体になる
低域通過特性で、libjpeg のスケールデコードと同種の性質。
プログレッシブ JPEG は probe が false を返し stb 経路にフォール
バックする（mean 0.080 で照合済み）。

詳細な対応表・出典・残課題は REPORT-toolkit-complete.md を参照。

## 7. M8 の実装とテスト証拠（2026-11-20）

変更ファイル: src/tk_draw.c（回転プリミティブ
tk_canvas_draw_image_rot 追加）、include/tk.h（同宣言）、
src/tk_internal.h（tk_widget に chev_pos / chev_anim /
split_menu / split_arrow）、src/tk_m7.c（chevron 駆動）、
src/tk_widgets.c（measure 修正・placeholder 色・menu 解散
フック）、src/tk_core.c（tk_close_all_menus フック）、
Makefile（test_tk7 配線、.o 依存に tk_internal.h 追加）、
tests/test_tk7.c（新規 246 件）。

M8 で発見・修正した実在バグ:

1. **metrics() のテーマ永久キャッシュ**（src/tk_widgets.c）:
   初回の tk_theme_default() をプロセス寿命で保持し、
   tk_theme_set_default() によるモード切替が全 leaf measure に
   効かなくなっていた。2モード制の根幹に関わる欠陥。キャッシュ
   を外し現行デフォルトを読むよう修正。
2. **Checkbox / Radio / Switch の measure が WinUI 描画形と
   不一致**: 描画は 28px 箱 / 56px トラック、measure は
   20px / 52px 前提で、WinUI モードでラベルが 8px / 4px
   はみ出していた。style==2 の実形に合わせて修正。
3. **TextField の placeholder が本文色で描画**されていた:
   両参照（M3 on-surface-variant / WinUI placeholder fill）
   とも淡色役割のため text_dim に修正。
4. ビルド衛生: Makefile の .o 依存に src/tk_internal.h が無く、
   構造体変更後に古い .o が混在しうる状態だった。依存を追加。

自分の実行による結果（2026-11-20、クリーンビルド）:

| スイート | 結果 |
|---|---|
| ツールキット `make check` | 従来 1,117 ＋ **tk7 246** = **1,363 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（回帰ゼロ） |
| シェル `make check` | **588 passed, 0 failed**（同上） |
| OSK `make check` | **319 passed, 0 failed**（同上） |

詳細は REPORT-toolkit-m8.md を参照。

## 8. M10 の実装とテスト証拠（2026-11-20）

変更ファイル: include/tk.h（tk_theme に M10 状態トークン
12件追加）、src/tk_core.c（winui / winui_dark への実値設定、
フォーカスリング内側 alpha 是正、InfoBar Attention 実値化）、
src/tk_widgets.c（状態形の実装: Button 全バリアントと
legacy 面、Checkbox / Radio、Switch、Slider、Progress、
TextField、Chip、Menu 項目）、tests/test_tk9.c（新規
150 件、状態ピクセルの両モード網羅）、tests/test_tk7.c
（WinUI ダーク全体再実行の仕組み TK7_DARK=1 と期待値の
テーマ相対化、スイッチ期待値の新トークン化）、
tests/test_tk8.c（無効スイッチ / InfoBar の新仕様への追随
+3 件）、Makefile（test_tk9 配線）。

M10 で発見・修正した実在バグ / 仕様ロール取り違え:

1. **WinUI Checkbox / Radio の未選択枠が淡い ControlStroke
   だった**: 仕様ロールは ControlStrongStrokeColorDefault
   （themeresources でチェックボックス・ラジオの境界は
   強いストローク側）。ライトでは text_dim と偶然近い値で
   覆い隠れていた。strong_stroke / strong_stroke_dis に是正。
2. **WinUI Switch の OFF ノブ・枠のロール取り違え**:
   ノブは ControlStrongFillColorDefault、枠は
   ControlStrongStrokeColorDefault が仕様。従来は両方とも
   text_dim で、ライトでのみ偶然一致していた。あわせて
   ON ノブが surface 固定で、ダークでは on-accent 色
   （黒）と食い違っていたのを是正。
3. **フォーカスリング内側ストロークの alpha が仕様と
   不一致**: WinUI FocusStrokeInner は light #FFFFFFFF /
   dark #B2000000。実装は light 側が 179 だった。是正。
4. **無効状態の形が大半の部品で未実装 / 近似だった**:
   M3 は「内容 = on-surface 38%、容器 = 12%」、WinUI は
   部品別の Disabled トークン群が仕様。従来は text_dim
   一括や surface_alt 代用だったものを、Button（全バリアント
   + legacy 面）/ Checkbox / Radio / Switch / Slider /
   Chip / TextField / Menu 項目で仕様形に実装した。
5. **Progress トラックが両モードとも surface_alt 近似**:
   M3 = surfaceContainerHighest、WinUI =
   ControlStrongStrokeColorDefault に是正（§2.4 参照）。
6. **tk7 が WinUI ダークテーマで全体未実行だった**:
   TK7_DARK=1 で全 246 件を winui_dark に読み替えて実行
   する仕組みを入れ、246 件全 PASS を確認（M9 の残件を
   解消）。読み替えで見つかった失敗 5 件は、ライト前提の
   テストピン 3 件と上記 1・2 の実装ロール取り違え 2 件
   （＝スイッチ）だった。

自分の実行による結果（2026-11-20）:

| スイート | 結果 |
|---|---|
| ツールキット `make check` | 従来 1,431 ＋ tk8 +3 ＋ **tk9 150** = **1,584 passed, 0 failed** |
| tk7（TK7_DARK=1、winui_dark 全体再実行） | **246 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（回帰ゼロ） |
| シェル `make check` | **683 passed, 0 failed**（588 + マスコット 95、同上） |
| OSK `make check` | **319 passed, 0 failed**（同上） |

詳細は REPORT-toolkit-m10.md を参照。

## 9. M11 の実装とテスト証拠（2026-11-20）

変更ファイル: src/tk_widgets.c（WinUI トグル checked 面の
共有ペインタ新設、Chip の WinUI 状態形、M3 トグルの
バリアント別選択形）、src/tk_core.c（モーダル Dialog の
scrim と damage 範囲）、tests/test_tk10.c（新規 119 件）、
tests/test_tk9.c（WinUI chip の期待値を是正形に更新）、
Makefile（test_tk10 配線）、AUDIT-toolkit.md（本節と §2）。

M11 で発見・修正した実在バグ / 仕様ロール取り違え:

1. **WinUI トグルの checked 無効形が仕様ロール取り違え**:
   M10 は ToggleButton themeresources の値として
   「ControlFillColorDisabled + TextFillColorDisabled」を
   実装したが、一次ソース（2026-11-20 再取得）では
   CheckedDisabled = AccentFillColorDisabled、
   ForegroundCheckedDisabled =
   TextOnAccentFillColorDisabled（前者は未選択時の無効
   ペア）。checked 押下形（AccentFillColorTertiary）も
   未実装だった。共有ペインタ winui_toggle_checked_face
   で是正。
2. **WinUI の LEGACY トグル checked 形に到達経路が無い**:
   テキスト形の分岐が `style != 2` 条件で、WinUI では
   checked でも中立面のままだった。同じ共有ペインタを
   テキスト形・アイコン行形にも接続。
3. **モーダル Dialog に scrim が無かった**（両モード）:
   テーマの smoke トークン（M3 系は 32% scrim、WinUI は
   SmokeFillColorDefault）を使い、モーダルシートと同じ
   ルート描画経路で追加。表示・除去時の damage もルート
   全体に広げた。
4. **M3 トグルのバリアント別選択形が大半未実装**:
   FILLED は常時 primary 面（仕様の未選択形は
   surfaceContainer + on-surface-variant）、ELEVATED は
   選択時も低コンテナ（仕様は primary 面）、OUTLINED /
   TEXT は container ウォッシュ（仕様は inverse 対）。
   buttons スペック（2026-11-20 取得）に基づき仕様化。
5. **Chip の WinUI 押下形が無かった**: 未選択押下 =
   ControlFillColorTertiary、選択押下 =
   AccentFillColorTertiary、選択無効 = 上記 1 と同じ
   accent ペアに是正（チップの WinUI 形は ToggleButton
   形という M10 の定義に従う）。

自分の実行による結果（2026-11-20）:

| スイート | 結果 |
|---|---|
| ツールキット `make check` | 従来 1,584 ＋ **tk10 119** = **1,703 passed, 0 failed** |
| tk7（TK7_DARK=1、winui_dark 全体再実行） | **246 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（回帰ゼロ） |
| シェル `make check` | **683 passed, 0 failed**（回帰ゼロ） |
| OSK `make check` | **319 passed, 0 failed**（回帰ゼロ） |

詳細は REPORT-toolkit-m11.md を参照。

## 10. M12 の実装とテスト証拠（2026-11-20）

変更ファイル: src/tk_anim.c（cubic-bezier 評価、
tk_ease_standard / tk_ease_standard_decelerate /
tk_ease_emphasized_decelerate、tk_anim_mode_pair、
スライド機構の一般化: サブツリー全体の移動・横軸・
slide-out）、src/tk_widgets.c（Switch ノブのモード別
ペア化）、src/tk_core.c（Dialog 退出スライド、Snackbar
出現スライド、WinUI トーストの inverse ペア是正）、
src/tk_m7.c（Drawer / Sheet の出現・退出スライド、
スクリム進捗連動）、include/tk.h・src/tk_internal.h
（宣言と状態フィールド）、tests/test_tk11.c（新規
215 件、D 観点専用）、tests/test_tk10.c（TK10_DARK
機構、Slider 期待値是正、Toast 期待値是正、§M WinUI
残件 9 件）、tests/test_tk8.c（Snackbar 出現前提の
是正 1 件）、Makefile（test_tk11 配線）、
AUDIT-toolkit.md（本節と §2）。

アニメーションの配線値（仕様出典は REPORT-toolkit-m12
§0.5）: Switch = M3 200ms Standard / WinUI 167ms
(0,0,0,1)。Dialog・Menu = 小ペア（200/167ms、
(0,0,0,1)）。Drawer・Sheet = 大ペア（M3 300ms
Emphasized decelerate / WinUI 250ms (0,0,0,1)）。
Snackbar 出現 = M3 形のみ 200ms Standard decelerate、
WinUI 形（InfoBar）は出現モーションなし。部品別
スペックページにモーション数値が無い部品については、
トークン体系への対応付けとして記録し、部品別仕様値の
取得は主張しない。

M12 で発見・修正した実在バグ / テスト前提の誤り:

1. **WinUI トーストが inverse ペアリングを使っていな
   かった**（実装バグ）: 文書・テストが主張する
   inverseSurface / inverseOnSurface 対ではなく
   surface / text で描画していた。light テーマでは
   surface {255,255,255} = inverseOnSurface、グリフ色
   {27} ≈ inverseSurface {32} と数値的に一致するため
   M11 のテストが偶然通過し、TK10_DARK=1 の全体再実行
   で乖離が露呈した（M10 の tk7 ダーク再実行が暴いた
   ロール取り違えと同型）。実装を inverse ペアに修正
   （角 6・枠は維持）。テストのグリフ期待値も、ink の
   alpha を織り込んだブレンド算術に是正（旧形は
   alpha=255 前提で dark の alpha=228 を数えられな
   かった）。
2. **Slider Range 無効形テストの期待値誤り**（テスト
   側）: 中央ピクセルはつまみ間の選択区間であり、実装
   は無効トラックの上に無効フィルを重ねた合成を描く。
   旧期待（トラック単体）は light で合成値と 1 単位差
   のため偶然通過し、dark で露呈。期待値を合成式に
   是正（実装は両テーマとも合成値どおりで正しい）。
3. **スライドが面の rect だけを動かしていた**（実装
   バグ、M12 着手時の棚卸しで発見）: ダイアログの子は
   最終位置に残り、面だけが移動する不整合があった。
   サブツリー全体が一緒に移動するよう修正し、tk11 で
   タイトルグリフの追従を実測検証した。
4. **Snackbar 出現スライド追加に伴う tk8 前提是正**:
   tk8 の1件は「tick 済みルートで表示し即座に最終位置
   を探る」前提で、出現スライド（仕様配線）と矛盾する
   ため、出現を走り切ってから探る形に是正。出現自体は
   tk11 §I で検証。

格上げの内訳（§2 の機械集計と一致）: ● +11
（Android: #8 Switch D、#22 Dialog D、#26 Snackbar D、
#31 Drawer D、#32 Sheet D、#39 基盤 D。WinUI: #8 D、
#22 D、#31 D、#32 D、#39 D）。○→◐ は WinUI の
7 セル（#21 Menu D = 出現のみ検証・退出は即時、
#26 InfoBar D = 出現モーションが無いことの画素一致
検証、#14 TextView A、#23/#24 Dialog A、#2 トグル
E/F）。◐ から ● へ抜けた 5 件と差し引き、◐ は
144→146、○ は 124→111。
据え置き: #11 Progress D は両モード ◐ のまま
（自前 sweep のタイミングは tk11 で精密検証したが、
M3 不定形のキーフレーム忠実度は未検証）、#12 Ring D
は ○ のまま（位相エンジン共有のみで回転自体の画素
検証なし）、#27 Toast D は ○ のまま（フェード未配線）、
TabBar インジケータ / FAB menu / Carousel settle /
Checkbox・Radio のグリフ遷移は未配線で D は ○ のまま。

自分の実行による結果（2026-11-20）:

| スイート | 結果 |
|---|---|
| ツールキット `make check` | 従来 1,703 ＋ **tk11 215** ＋ tk10 増分 9 = **1,927 passed, 0 failed** |
| tk10（TK10_DARK=1、winui_dark 全体再実行） | **128 passed, 0 failed**（M12 で新設・正式実施） |
| tk7（TK7_DARK=1） | **246 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（回帰ゼロ） |
| シェル `make check` | **683 passed, 0 failed**（回帰ゼロ） |
| OSK `make check` | **319 passed, 0 failed**（回帰ゼロ） |

詳細は REPORT-toolkit-m12.md を参照。

## 11. M13 の実装とテスト証拠（2026-11-20）

M12 で未着手だった優先3（FAB / Split / Navigation 系の
B ◐）と未配線の D 観点を処理した。検証は新規スイート
test_tk12（213 checks、両モード＋ダーク機構なしの
ライト実行。ダークは tk11 で別途実施）による。
格上げの根拠（仕様出典＋テスト）の詳細は
REPORT-toolkit-m13.md に置く。

B の是正（実装変更を伴うもの）:

- #30 Navigation rail / #31 Navigation drawer: 選択指標・
  選択ピルの色が primaryContainer のままだったのを
  secondaryContainer（テーマの selection ロール）へ是正。
  選択ラベルは onSecondaryContainer（新トークン
  on_selection、未設定テーマは従来ロールへフォール
  バック）。NavRail は押下状態層が無く、イベント側に
  invalidate 自体が無かった（押下形が描画され得ない
  実在バグ）。BottomBar（#29）の指標ピルも同ロールへ
  是正（トーナル全面形は primaryContainer のまま）。
- #3 FAB: 面が accent のままだったのを M3 既定の
  primaryContainer / onPrimaryContainer へ是正し、角半径
  をサイズ級別（small 14 / regular 19 / large 33 px）に。
  無効形は M3 に定義が無いため 12%/38% の枠組みで記録。
  IconButton の押下形も状態層の形へ是正。
- #5 Split button: 内側角半径 4dp ≈ 5px を非対称角の
  描画（tk_canvas_fill_round_rect4 新設）で実装。従来は
  両セグメントとも全面角だった。
- #32 Bottom sheet: ドラッグハンドル色を border から
  onSurfaceVariant 40% へ是正（M3 側のみ。WinUI は
  border トークンのまま検証）。
- テーマ実在バグ: v2_dark の selection トークンが
  primaryContainer の値 {79,55,139} の複写になっていた
  （M3 dark の secondaryContainer は #4A4458）。是正に
  よりダーク時の ListView 選択色も仕様ロールに揃った。

D の配線（全て tk_anim のモード別ペアで駆動し、開始/
中間/終了を画素で検証）:

- #27 Toast フェード（タイムライン駆動。WinUI は
  Microsoft Learn のフェード Linear 83ms、M3 側は
  short3 150ms＋Standard decelerate への対応付けとして
  記録し、AOSP 実値の取得は主張しない）。
- #28 TabBar インジケータ移動（小ペア 200/167ms）。
- #34 Carousel settle（大ペア 300 Emphasized
  decelerate / 250ms）。
- #6/#7 Checkbox・Radio グリフ遷移（選択ペア）。
  実在バグ: チェックボックスのイベントハンドラが
  セッターを経由せず checked を直接反転しており、
  セッター側だけ配線しても遷移が一切動かなかった。
- #4 FAB menu: 出現はスライドイン（小ペア）、退出は
  ゲート付きスライドアウトを追加。
- #11 Progress（Android のみ ●）: 不定形を MDC の
  キーフレーム2本バーへ置換（周期 2000ms、区間
  イージング・キーフレーム表は material-web の
  _linear-progress.scss が出典）。テストは独立参照
  （密パラメトリックスキャンの bezier 評価＋独自の
  キーフレーム表）で 7 位相を照合。確定形の遷移
  （250ms, cubic-bezier(0.4, 0, 0.6, 1)）も検証。
- #12 D（Android）: 検証した動きは不定形（Spinner）の
  回転で、位相 0 と 250ms で 45°/315° プローブが反転
  することを画素で確認した。確定形リング自体には
  仕様上の動きは無い。WinUI の #12 D は未検証のまま
  ○（テストは M3 モードのみ）。
- #23/#24 Dialog modeless / full-screen: 基本形と共有の
  スライドイン（12px、小ペア）と面色を両モードで検証
  （full-screen の M3 面は surface、modeless は
  surfaceContainerHigh）。

据え置き・条件付きの記録:

- #9/#10 Slider D と #11 の確定遷移は **opt-in**
  （tk_slider_set_animate_values /
  tk_progress_set_animate_values、既定 off = スナップ）。
  常時 ON の形で実装したところ、消費者のシェル
  test_session が決定的に失敗した: バックエンド同期が
  値変更のたびに 200ms の再描画尾を引き、「静止時は
  転送ゼロ」の契約が破れた。反映系の消費者（シェルの
  音量/輝度、プレイヤーのシーク、レコーダーのレベル
  メーター）ではアニメーション自体が実害のため、M5 の
  スクロールシフトと同じ opt-in 判断とした。#9 D は
  opt-in 有効時の開始/中間/終了で ●、#10（Range）の
  D はテスト未実施のため ○ のまま据え置く。
- WinUI #11 D は ◐ のまま（MDC キーフレームは M3
  専用。WinUI 側は M12 の sweep 検証のみ）。
- tk11 のダーク全体再実行（優先4）: TK11_DARK 機構を
  新設し **215/215 PASS**。M12 で格上げした D セル群が
  ダークテーマでも成立することを確認した。

格上げの内訳（§2 の機械集計と一致）: ● +37
（Android +19: #3 B、#4 D、#5 B、#6 D、#7 D、#9 D、
#11 D、#12 D、#17 B、#23 B/D、#24 B/D、#27 D、
#28 D、#30 B、#31 B、#32 B、#34 D。WinUI +18:
#3 B、#4 D、#5 B、#6 D、#7 D、#9 D、#17 B、
#23 B/D、#24 B/D、#27 B、#28 D、#29 B、#30 B、
#31 B、#32 B、#34 D）。合計 ● 201→238（52.0%）、
◐ 146→128、○ 111→92、△ 0、✕ 0。

自分の実行による結果（2026-11-20）:

| スイート | 結果 |
|---|---|
| ツールキット `make check` | 従来 1,927 ＋ **tk12 213** = **2,140 passed, 0 failed** |
| tk11（TK11_DARK=1、winui_dark 全体再実行） | **215 passed, 0 failed**（M13 で新設・正式実施） |
| tk10（TK10_DARK=1） | **128 passed, 0 failed** |
| tk7（TK7_DARK=1） | **246 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（回帰ゼロ） |
| シェル `make check` | **683 passed, 0 failed**（回帰ゼロ。途中、スライダー常時アニメーション化による test_session の決定的失敗を検出し、opt-in 化で解消） |
| OSK `make check` | **319 passed, 0 failed**（回帰ゼロ） |

既存テストの前提是正（仕様駆動の挙動変更に伴う）:
test_tk5 のトースト（フェードイン完了後に画素確認）、
test_tk6 の BottomBar 指標ピル色（secondaryContainer
是正に追随）、test_tk11 の progress_flow（M3 側を
キーフレーム前提に書き換え、WinUI 側は従来公式のまま）。

詳細は REPORT-toolkit-m13.md を参照。

## 12. M14 の実装とテスト証拠（2026-11-20）

M14 は A/E/F 観点の網羅、M13 の積み残し2件（#10 Range の
D、#12 の D WinUI 側）、◐ の順次解消を担当した。新規
テスト test_tk13.c（770 checks）を追加し、自分の実行で
ツールキット 2,910 checks 全 PASS、消費者3スイート
（アプリ 1,048 / シェル 683 / OSK 319）無修正で全 PASS
= 回帰ゼロを確認した。格上げの内訳は ● +84
（◐→● 46、○→● 38）、訂正2件（#12 F の両モードを
○ → —: 表示専用ウィジェットにキー操作の観点は存在
しない。分母 458 → 456）、A の部分格上げ2件
（#31/#32 の WinUI A を ○ → ◐: モード間で配置矩形が
一致することと 360dp キャップ・下端アンカーを検証。
モーダル形の幾何は未検証のため ● にはしない）。
観点別の ● 増分は F +41、E +26、D +3、B +7、C +7。
詳細と根拠は REPORT-toolkit-m14.md。

新規実装3件: (1) Radio の矢印キー（Microsoft Learn の
"Selection follows focus": 平矢印はフォーカス移動＋
選択追随、Ctrl+矢印はフォーカスのみ、グループ端で
止まる）。(2) Slider の全キー契約（M3 sliders
accessibility: 矢印1目盛、Home/End は端値。PageUp/
PageDown は矢印ステップの10倍。デュアルは掴んだ
ハンドル基準）。(3) Segmented 複数選択のキー操作と
Carousel の左右キー（従来はポインタのみだった）。

実在バグ4件を発見し修正した: (1) メニュー/ダイアログを
閉じるとフォーカスが NULL に落ち、後続のキー入力が
全て死ぬ（SearchBar の候補選択後に顕在化）。表示時の
フォーカスを保存し、オーバーレイ除去時に tk_focus
経由で復元するよう修正。(2) ダイアログの初期フォーカス
がダイアログ自身になっていた（自コメント「中身の最初の
フォーカス可能要素」と不一致）。Tab サイクルでも
中身があるモーダルはダイアログ自身をストップから
外した。(3) 共有エディタキー（text_edit_key）の
Shift+矢印が単語移動になっており、両プラットフォーム
の標準（Shift+矢印 = 1文字選択、単語は Ctrl、
Ctrl+Shift = 単語選択）と食い違っていた。TextField /
TextView / SearchBar の全編集面に効く共有経路を修正。
(4) FAB メニューの scrim が show/hide のたびに
リークしていた（auto_free の解放は呼び出し側の契約
だが tk_fabmenu_hide が忘れていた。ASan/LSan で
発見し、hide 側で解放するよう修正）。

## 13. M15 記録（残セル格上げ: F の実装解消・E/B 網羅・D 再確認）

実施内容と格上げ根拠の詳細は REPORT-toolkit-m15.md。
新規テストは tests/test_tk14.c（331 checks、両モード）。
ツールキット合計は 3,241 checks（2,910 + 331）。

### 13.1 #40 Drag & Drop の F を「—」に訂正（分母 456 → 454）

一次ソースで両参照ともドラッグ＆ドロップをポインタ
ジェスチャとしてのみ定義し、キー操作によるドラッグの
定義が存在しないことを確認した:
- Microsoft Learn「Drag and drop - Windows apps」:
  "a direct manipulation gesture"（press, hold, pan）
  として定義され、キーボード経路の記述なし。
- Android Developers「Implement drag and drop with
  views」: 長押しで始まるジェスチャ前提のみ。
キーボード代替（切取/貼付による移動）は #41 共有
クリップボードの C/E で ● 検証済みであり、到達可能性
自体は担保されている。#12 F の訂正（M14）と同じ
「部品の性質上存在しない」場合に当たるため両モード
とも「—」とし、適用セルは 456 → 454。

### 13.2 実在バグの修正（3件）

(1) **モーダルオーバーレイでフォーカス復帰が一度も
発火していなかった**（tk_core.c）: tk_root_remove_
overlay が focus_return の復元をモーダル解除より前に
実行していたため、tk_focus のモーダルトラップが復元を
常時拒否していた。M14 のフォーカス復帰機構は非モーダル
なオーバーレイ（メニュー等）でのみ有効で、ダイアログ/
ドロワー/シートでは無効だった。除去時に modal を先に
クリアするよう順序を修正。tk14 のドロワー/シートの
テストが露出させた。
(2) **全画面フォーカス矩形へのリング描画**（tk_core.c、
(1) の副作用の手当て）: モーダルドロワーがフォーカスを
持つと全画面オーバーレイ矩形にフォーカスリングが
描かれた。focused の矩形がルート全体を覆う場合は
リングを描かない（全画面ダイアログにも適用。シートの
パネル矩形はルートより小さいのでリングは残る）。
(3) **アイコン行形の LEGACY トグルに checked 面が
なかった**（tk_widgets.c）: テキスト経路は M7 から
container 面を持つが、アイコン行経路は WinUI 分岐
（M11）しか checked を見ておらず、M3/クラシックでは
アイコン付きトグルの ON 状態が画素に一切現れなかった
（実測 diff = 0）。テキスト経路と同じ container 面を
アイコン行にも実装。

### 13.3 D ○ 群の一次ソース再確認（据え置きの根拠）

部品ごとに再確認し、「参照自体に既定形のアニメーション
定義が無い」ものは ○ のまま据え置いた（○ は「実装
のみ・未検証」であり、無アニメーションが同等である
ことの主張にはしない。アニメーションの有無自体を
画素で検証できたものだけを格上げした）:
- #11 WinUI D → ●: Microsoft Learn の progress
  controls 記述は indeterminate を "a repeating
  pattern" とだけ定義し、キーフレーム・周期の公開
  仕様はない。位相駆動でセグメント画素が実際に動く
  ことを tk14 で検証した（仕様が与える性質の範囲での
  同等）。
- #26 WinUI D → ●: 出現モーションが無いことを画素
  不変で検証（InfoBar に出現アニメーションの定義
  なし）。
- #21 D は両モード ◐ 据え置き（理由を精密化）:
  Android は Compose の ModalNavigationDrawer 等に
  退出アニメーションが存在し本実装は即時クローズ。
  WinUI は出現スライドは tk11 で検証済みだが参照の
  フェード成分を再現していない。
- 残りの D ○（各モード #1,#2,#3,#13,#14,#15,#16,
  #18,#19,#20,#25,#29,#30,#33,#35,#36）: 既定形に
  仕様定義アニメーションの一次ソースが見つからな
  かったものと、定義はあるが本実装に動きが無く画素
  検証に届かないものが混在する。個別の切り分けは
  次弾の課題とし、一括の格上げはしない。

### 13.4 観察事項（未修正）

Android Developers の Snackbar 契約ではアクション付き
Snackbar は timeout で消えるべきでないが、本実装は
duration 指定時にアクション有無によらず自動消去する
（TK_SNACKBAR_INDEFINITE が永続形）。消費者の契約に
触れるため M15 では変更せず記録のみとする。#26 C の
格付けは変えない。

## 14. M16 記録（Snackbar 契約是正・D 群の部品別切り分け・E/C 小塊の解消）

格上げの根拠は REPORT-toolkit-m16.md と tests/test_tk15.c
（新規 89 checks）。ツールキット合計は 3,334 checks
（tk14 の Snackbar 検証書き換えで +4、tk15 で +89）。
分母の変更はない（454 のまま）。格下げはなし。

### 14.1 Snackbar timeout 契約の是正（§13.4 の解消）

一次ソース（2026-11-20 取得）: Android Developers
「Snackbar | API reference」— "Snackbars with an action
should not timeout or self-dismiss until the user
performs another action"（フォーカス移動は action に
数えない）。Microsoft Learn「InfoBar - Windows apps」—
既定ではユーザーが閉じるまで残る。実装: アクション付き
バーは duration 指定によらず自動消去しない
（snackbar_show_impl が snack_forever 化、tk.h の契約
コメントも更新）。アクション無しの timed 形は従来どおり
（tk7 で検証済みの抽象として維持）。tk14 の旧契約固定
ブロックを新契約の検証に書き換えた。#26 の格付けは
変更なし（C は他の振る舞いで ● 済み。本是正で記録上の
逸脱が解消した）。

### 14.2 実在バグの修正（1件）

Slider の sld_grabbed（ドラッグ/ステップ中フラグ）が
Range（dual）形の経路でしかセットされておらず、単一形
ではポインタドラッグでもキーステップでも一切立たな
かった。値インジケータの表示条件がこのフラグのため、
単一形では（14.4 の移植以前から）ドラッグ状態の追跡
自体が存在しなかった。tk15 の新規 E テストが検出。
修正: POINTER_DOWN とキーステップの両経路で両形とも
セットし、クリアは既存の POINTER_UP / FOCUS_OUT 経路
（両形共通で既に存在）に委ねる（Range 形の確立済み
振る舞いと対称）。

### 14.3 D ○ 群の部品別切り分け（各モード16件）

● 化（実装 + 検証）:
- #29 BottomBar / #30 Navigation rail（両モード）:
  選択インジケータの遷移を実装。共通基盤
  tk_ind_anim_to が表示位置 ind_pos（item 単位）を
  tk_anim_selection_pair（M3 200ms Standard /
  WinUI 167ms Direct Entrance）で駆動し、描画は
  モード別（M3 形 = クロスフェード — Compose の
  NavigationBarItem/NavigationRailItem インジケータは
  フェード、WinUI 形 = スライド）。ライブクロック
  無しではスナップ（Switch/glyph の確立パターン）。
  BottomBar はインジケータ描画をセルからバー側
  （tk_paint 内・子より前）へ移設し、静止時は従来と
  同一画素（tk12/tk13 の B 検証が無修正で通過）。
  tk15 で中間状態（スライド通過・フェード通過）と
  終了フレームの静止画一致を両モードで検証。

● 化（不変性検証。参照の既定形に時間駆動アニメーション
の定義が無く、ライブクロック下の画素不変を tk15 で
検証。M15 の #26 WinUI D の前例）:
- #14 TextView（両モード）、#36 AppBar（両モード）、
  #13 TextField WinUI（TextBox にラベルアニメーション
  は存在しない）。

○ 維持（参照に定義があり本実装に無い）:
- Android #1/#2/#3: ripple 未実装（src/・include/・
  文書のどこにも ripple が存在しないことを grep で
  確認）。Android #16: フィルタチップのチェック出現
  + ripple。Android #18: クリック可能カードの ripple。
  Android #19: M3 既定リストにアイテムアニメーション
  は無く LazyColumn の animateItem は opt-in API
  （既定形の定義とは扱わない）。WinUI #19: Microsoft
  Learn の ItemContainerTransitions API 頁で既定
  テンプレートに AddDelete/Content/Reorder/Entrance
  の定義が実在することを確認（実装なし）。#25 両
  モード: Picker の遷移が参照に実在。Android #33:
  チェックアイコン出現アニメーション。

○ 維持（遷移対象の形・部品が参照に無い）:
- Android #13: 本実装のプレースホルダ形には M3 の
  フローティングラベル遷移の対象が無い。WinUI #16:
  公式 Chip コントロールなし。WinUI #18: WinUI に
  Card コントロールなし。Android #20: M3 にツリー
  部品なし。Android #35: M3 に ColorPicker 部品なし。
  WinUI #33: 公式 Segmented コントロールなし。

○ 維持（一次ソースで確定できなかった）:
- WinUI #1/#2/#3: themeresources のテンプレート本文
  取得に至らず、状態遷移アニメーションの有無を肯定も
  否定もできない。推測で動かさない。WinUI #20:
  TreeViewItem のトランジション有無が未確認。
  WinUI #35: ColorPicker テンプレートのトランジション
  有無が未確認。#15 両モード: §13.3 の理由（ビュー
  出現・候補リスト遷移の未再現）のまま。

### 14.4 E ◐ / C ◐ の解消

- #9 Slider E（両モード）→ ●: 単一形に値インジケータ
  （ドラッグ/ステップ中のみ、Range 形と同一の
  48x44dp 逆色バブル + 値文本）を移植し
  slider_value_bubble に共通化。tk15 で分数形
  （"50%"）とレンジ形（"25"）の面画素 + 文本グリフを
  両モードで検証。前提として 14.2 のバグ修正が必要
  だった。
- WinUI #3 E → ●: tk15 で FILLED アイコンボタン /
  アイコン+ラベルボタン / FAB のアイコン画素と
  ラベルグリフを WinUI 形で検証。
- WinUI #19 E → ●: tk13（アイコン/主/副ラベル）に
  tk15（ヘッダ文本 — WinUI の text_dim は alpha 158
  のため行背景とのコントラストで計数、チェック印、
  選択行ラベル。light/dark 両テーマ）を加え、行が
  描く要素クラスを網羅した。◐ の残りの精密な記録は
  元々無く、要素クラスの網羅を根拠とする。
- Android #39 C → ●: tk11 のエンジン契約検証は
  プロセス既定テーマ（Android light）でのみ走って
  おり、WinUI C は部品フロー経由だった非対称を、
  tk15 で契約自体（線形配送の正確さ・done 1回・
  キャンセル・壁時計等価性）を両テーマで対称に
  検証して解消。あわせてモードペア選択（Switch の
  ノブ着地が M3 200ms / WinUI 167ms）も両モードで
  検証。WinUI C の ● は直接の裏付けも得た。

### 14.5 据え置き（M16 で動かさなかったもの）

- #21 D ◐（両モード）: 理由は §13.3 の精密化どおり。
  退出アニメーションは検討のうえ見送った: メニューの
  除去は同期的で、M15 で直したフォーカス復帰の順序と
  メニューテスト群の「即時消去」前提に構造変更が
  必要であり、今弾の比例を超える。見送りは判断として
  記録する（未検討ではない）。
- A ○ 3件（#27 両モード、#35 WinUI）: 新規出典を
  得られず据え置き（Toast の公開形状仕様なし、
  ColorPicker の themeresources 本文未取得）。
- A ◐ 群（Android 27件 / WinUI 24件）: 新規出典が
  無いため方針どおり据え置き。
- 結果として、残る非 ● セルは A（◐ 51 + ○ 3）と
  D（◐ 2 + ○ 23）のみ。C / E / F は両モード全 ●。

## 15. M17 記録（ripple / ブラシ遷移の実装・#21 フェード・A 分類監査）

一次ソースは全て 2026-11-20 に取得: M3 ripple は
androidx material-ripple の RippleAnimation.kt と
material3 Ripple.kt、WinUI ボタン系は
microsoft-ui-xaml（main ブランチ）の Button /
ToggleButton / AppBarButton _themeresources.xaml
（前弾までの取得失敗はブランチ名 dev の誤りが原因）、
ColorPicker.xaml テンプレート全文と
ColorPicker_themeresources.xaml、TreeViewItem.xaml、
M3 Menu.kt / Chip.kt。取得物は /tmp/tr/ に置いた。
検証は新規 test_tk16（77 checks）。ツールキット総数
3,334 → 3,411、全 PASS。消費者（アプリ 1,048 /
シェル 683 / OSK 319）回帰ゼロ。ASan/UBSan は
tk13〜tk16 でエラーなし・直接リーク 0（実行は
rebuild ディレクトリから行うこと — フォント探索が
cwd 相対のため、別ディレクトリから走らせると字形
カウント系が落ちる。M17 で踏んだ既知の落とし穴）。

### 15.1 D ● 化（実装 + 検証、12件）

- Android #1/#2/#3（ripple）: 参照実装どおりに実装。
  押下点から円が拡大（半径 0.3×最大寸法 → 対角半分
  + 10dp = 実機換算 12px、225ms FastOutSlowIn =
  cubic-bezier(0.4, 0, 0.2, 1)）、中心は押下点から
  部品中心へ 225ms リニア、alpha は 75ms リニアで
  押下状態層 10% へ、解放で 150ms リニアの
  フェードアウト、早期解放時は alpha を最大にして
  拡大を完走してからフェード。形状でクリップ
  （bounded）。描画プリミティブ
  tk_canvas_fill_circle_rrect4（円×隅別角丸の行
  スパン交差、スプリットボタンの非対称角丸対応）を
  tk_draw.c に新設。適用はボタン全形の M3 状態層
  サイト（accent / FAB / バリアント各形 / アイコン
  のみ / レガシー2形）。拡大完了 = 均一状態層と
  画素一致し、無クロックでは最終形に同期スナップ
  するため、静止画の B 検証は無修正で通過した
  （チップの1件のみ §15.5 のとおり前提を更新）。
  tk16 で拡大中（遠端未到達）・ settled 形・解放
  フェードの中間値・早期解放の完走を画素検証。
- WinUI #1/#2/#3（ブラシ遷移）: themeresources の
  確定事実は「状態変化は離散キーフレーム + 背景
  ブラシのみ BrushTransition 83ms」。tk_face_brush
  （表示色を 83ms リニアで目標色へブレンド、前景・
  枠は即時のまま）を新設し、accent 面・トグル選択
  面・バリアント各形（Elevated / Tonal / Outlined /
  Text の押下填充を含む）・レガシー面（本文形・
  アイコン行形・アイコンのみの subtle 層）・チップ
  の ToggleButton 形に適用。tk16 で FILLED・トグル
  選択面・アイコンのみの3経路について、開始点の
  無跳躍・中間値・ settled 形・復帰を画素検証。
- Android #16: フィルタチップのチェック出現を
  グリフ遷移機構（選択ペア）でフェード + スロット
  のクリップリビールに変更（M3 Chip.kt の
  AnimatedVisibility = expand + fade の形）。
  あわせてチップの状態層を ripple 円に変更。
  tk16 で出現・消失の中間状態と settled 一致を検証。
- Android #18: カードに押下経路自体が無かった
  （tk_m7_event に CARD ケースなし、cb1 は
  ポインタから発火しなかった）ため、card_event を
  新設して押下追跡 + cb1 発火を成立させ、その上に
  ripple 円を描画。子が消費したイベントはカードに
  届かない（ディスパッチチェーン）ため子ボタンと
  競合しない。tk16 で発火・不発火（外し解放・
  コールバックなしカードの不活性）と settled 形を
  検証。WinUI #18 の D は §15.3 のとおり ○ 据え置き
  （参照コントロール不在）。
- Android #33: Segmented のチェック出現を
  seg_check_anim（選択ペア、セグメント単位）で
  フェード + リビールに変更。イベント・キー経路の
  直接フィールド反転をセッタ経由に配線変更した
  （チェックボックスで M13 に直したのと同じ迂回
  欠陥）。単一アニメーションのみ同時実行で、旧
  セグメントのチェックは即時消去（参照の同時
  クロスフェードとは異なる簡略化として記録）。
  tk16 で中間状態（両 settled 形との差分）と
  settled 一致を検証。
- Android #19（不変性検証）: §14.3 の前提どおり
  M3 既定リストにアイテムアニメーションは無く、
  LazyColumn の animateItem は opt-in API で既定形
  の定義ではない。ライブクロック下の画素不変を
  tk16 で検証し、#14/#36 と同じ証拠種別で ● 化。
- WinUI #20（不変性検証）: TreeViewItem.xaml に
  状態遷移は無く、唯一のアニメーションは
  ReorderedPlaceholder の FadeOutThemeAnimation
  （複数選択ドラッグ並べ替えのプレースホルダ、
  本実装に無い経路）。通常経路の画素不変を tk16
  で検証し ● 化。並べ替え経路を持つ場合は再訪が
  必要。
- WinUI #35（不変性検証）: ColorPicker.xaml 全文に
  Storyboard / VisualTransition / BrushTransition /
  ThemeTransition は 0 件。通常経路の画素不変を
  tk16 で検証し ● 化。

### 15.2 #21 D ◐ 据え置き（理由の精密化）

M3 メニューの出現/退出は scale 0.8↔1.0 + alpha
0↔1（Menu.kt、MotionScheme の FastSpatial /
FastEffects）。M17 で alpha 成分は実装した:
paint_alpha によるフェード（出現・退出とも）。
退出は除去をフェード完了まで遅延させ、フォーカス
復帰は除去（tk_root_remove_overlay）の中で行う —
ダイアログ退出（tk_dialog_close / slide_out）で
M14 が確立した順序と同一構造であり、§14.5 が
懸念した「フォーカス復帰順序との構造干渉」は
この形では発生しない（M15 のモーダル復帰未発火は
除去側の順序バグであり、遅延除去はその修正と
同方向）。tk_close_all_menus は退出中メニューを
即時完了させ、退出中メニューは入力を飲む。
tk16 で出現フェード・退出中の開存続とフォーカス
保持・入力遮断・完了時のフォーカス復帰・無クロック
時の同期閉じを両モードで検証した。
残る未達は scale 成分: 本ツールキットの即時描画
パイプラインにはオフスクリーンの面が無く、面を
アンカー中心に 0.8↔1.0 へ拡縮する描画経路が存在
しない。再現には面のオフスクリーン化（描画全体の
構造変更）が必要で、今弾の比例を超えるため ◐ を
維持する。フェード時間は M3 メニュートークンの
ms 実値を取得できていないため、ツールキット既存
の小表面ペア（M3 150ms / WinUI 167ms）を流用して
おり、仕様値とは主張しない。なお WinUI
MenuFlyout の遷移（EntranceThemeTransition 系）に
ついては themeresources に確定値を得ていない。

### 15.3 D ○ 据え置き（11件、理由の現行化）

- Android #13: 本実装のプレースホルダ形には M3 の
  フローティングラベル遷移の対象が無い（形の相違）。
- #15 両モード: §13.3 の理由（ビュー出現・候補
  リスト遷移の未再現）のまま未着手。
- Android #20 / #35: 参照に部品自体が無い（M3 に
  ツリー / カラーピッカー部品なし）。不変性は
  「同等」の証拠にならないため ○ のまま。
- WinUI #16 / #18 / #33: 参照に公式コントロールが
  無い（Chip / Card / Segmented）。本実装の形は
  実在コントロールのテンプレートに追随させてある
  （#16 は ToggleButton のブラシ遷移、#33 は選択
  グリフ遷移、#18 はクリック挙動のみで遷移なし）
  が、部品自体の同等を主張する参照が無いため
  ○ のまま。
- WinUI #19: 既定 ItemContainerTransitions の
  実在は確認済みだが、既定スタイルの実値
  （オフセット量・継続時間）を一次ソースで確定
  できなかった（ListViewItem_themeresources.xaml
  に該当記述なし、関連パスは 404）。推測値での
  実装はしない。実装自体も仮想化リストへの大規模
  手術になる。
- #25 両モード: Picker の遷移が参照に実在し、
  未実装のまま。

### 15.4 A セル分類監査（◐ 51 + ○ 3 = 54件）

分類は AUDIT §2.4 / §3 と各弾 REPORT の寸法記録を
全件照合して行った（54部品の再計測ではない）。
全 A ◐ セルは §3 の全体規則（control_h 56px =
48dp の 187ppi 忠実換算、caption 下限 14px、
タッチフロア、WinUI ×1.43 換算）の上に乗っており、
この全体規則自体が出典付きで文書化されている。

- (a) 文書化された実機適合（部品固有のデルタが
  記録されている）: **7件**。Android #3（FAB 既定
  64px、§3）、Android #8（Switch トラック
  40epx→56px の丸め帯域、§2.4）、Android #16
  （チップ高 40px、換算 37.4px をタッチのため
  切上げ、§3）、Android #17（バッジ 19px、換算
  18.7px の丸め、§3）、Android #19（行高 56px、
  換算 57px との Δ1、丸め帯域 + タッチフロア、
  §2.4）、Android #29（ボトムバー総高 75px、
  envfit §3 の 64〜76px 帯、§3）、WinUI #19
  （行高 Δ1、§2.4 と同一記録）。これらは適応を
  「同等」と認めるかがユーザーの判断事項のため、
  ● には格上げしない。
- (b) 正当化できない逸脱: **0件**。記録にある
  デルタは全て根拠付きで、根拠のない逸脱は監査
  範囲で特定されなかった。したがって仕様値への
  修正で ● 化するセルは本弾では発生しなかった。
- (c) 逸脱の記録なし・寸法テスト未網羅: **44件**
  （Android 21 / WinUI 23）。◐ の理由は寸法の
  逸脱ではなく、部品別寸法テストの網羅不足。
  内訳の注意: WinUI #2 は §2.4 に「ToggleButton は
  Button と寸法共有・専用テスト未実施」と明記
  された (c) の典型。Android #14 / WinUI #13 は
  タイポグラフィ type scale の一次照合が未了
  （§2.4）という留保付き。Android #34 は仕様側に
  固定寸法自体が無い（レイアウト由来、§2.4）。
  いずれも ● には格上げしない（検証の完遂が
  条件）。
- ○ 3件は分類外として据え置き: #27 両モードは
  Toast の公開形状仕様自体が存在せず照合不能
  （逸脱ではない）。WinUI #35 は本弾で
  themeresources / テンプレートを取得したが、
  テンプレートはスペクトラム形であり本実装の
  パレットグリッド形と形自体が異なるため、対応
  する寸法値が存在しない（形の相違）。

### 15.5 既存テストの前提更新（2件）と実在バグ

前提更新（仕様形への追随であり、検証の緩和では
ない。経緯は各テスト内のコメントに明記）:
- tk10 test_chip_pressed: チップ押下形は旧近似形
  （未選択 = surface_alt ウォッシュ、選択 = 素の
  container）を固定していた。M3 の状態層の仕様形
  （内容色 @10% を静止面へ）に書き換えた。
- tk14 longpress_flow: 解放直後に押下面が消える
  前提を、退出遷移（ripple 150ms / ブラシ 83ms）
  完了後の比較に変更。あわせて比較基準を DOWN
  フレームから保持フレームへ変更した（ブラシ
  遷移では DOWN 直後のフレームは静止面のままで、
  押下面を測らないため）。

実在バグ（M17 の作業で発見・修正）:
- カードにポインタの押下経路が無く、cb1 が
  一度も発火しなかった（§15.1 Android #18）。
  card_event の新設で修正。
- Segmented のイベント・キー経路がチェック遷移を
  迂回していた（直接フィールド反転）。セッタ
  経由に配線変更して修正。
- 欠陥ではないが記録: tk_anim_cancel は done
  コールバックを呼ばないため、アニメーションの
  ユーザーデータにヒープ確保を使うとキャンセル時
  にリークする。M17 の新規アニメーションは全て
  ユーザーデータ = ウィジェット自身として確保を
  避けた（seg_check_anim の初版はこの形で書き、
  途中で改めた）。

## 16. M18: A (c) 44件の寸法テスト完遂と残り D / ○ の処理

実施: tests/test_tk17.c（148 checks、新規）を新設し、
(c) 44件全てに部品別の寸法テストを書いた。仕様値は
全て一次ソースから取得した（取得日はいずれも
2026-11-20）: androidx/material3 のトークン群
（compose/material3/material3/src/commonMain/kotlin/
androidx/compose/material3/tokens/ の Kotlin ソース）、
microsoft-ui-xaml の themeresources XAML（CommonStyles
の CheckBox / RadioButton / Slider / TextBox /
DatePicker / TimePicker、TreeView.xaml、InfoBadge /
TabView / NavigationView の各 themeresources）、
Microsoft Learn の Windows type ramp、AOSP
platform/frameworks/base の core/res（Toast の
レイアウト・dimens・アニメーション）。結果は
● +21件（内訳は §2.3 の M18 注記）、(a) 移動 4件、
◐ の理由精密化 20件、D の ○ → ◐ 2件。ツールキット
3,559 tests（3,411 + tk17 148）全 PASS、消費者
3スイート（アプリ 1,048 / シェル 683 / OSK 319）
無修正で全 PASS、回帰ゼロ。

### 16.1 仕様駆動の寸法修正（F1〜F6）

いずれも「仕様値が一次ソースで確定し、実装がそれと
異なる」正当化できない逸脱として修正し、tk17 で
ピンを張った。消費者の前提更新は不要だった
（tk12 の Toast ピンについては §16.7）。

- F1 SearchBar（Android）: 高さ 56 → 65px。
  SearchBarTokens の ContainerHeight 56dp の換算値。
  WinUI 形（AutoSuggestBox = TextBox フロア形）は
  56px のまま。
- F2 TabBar（Android、アイコン+ラベル形）: 高さ
  72 → 75px。PrimaryNavigationTabTokens の
  icon+label container 64dp の換算値。テキストのみ
  形は 56px（48dp ターゲット）のまま。
- F3 Segmented（Android）: 高さ 48 → 47px。
  OutlinedSegmentedButtonTokens の 40dp の換算値。
  WinUI 形（connected ToggleButtons）は一次ソースに
  対応する高さが無く 48px を維持しピンした。
- F4 Dialog full-screen（Android）: ヘッダ帯
  56 → 65px。M3 全画面ダイアログのヘッダ 56dp の
  換算値。measure / arrange / paint の3箇所を
  tk_dialog_title_h() に一元化した（basic 形は
  全モード control_h のまま）。
- F5 Toast（Android）: §16.6 参照（形状とフェード
  の両方を AOSP 実値へ）。
- F6 Bottom sheet ドラッグハンドル: 幅 38 → 37px。
  SheetBottomTokens の 32dp の換算値（高さ 4dp →
  5px は従来どおり）。side 形も同じく 37px。

### 16.2 (a) への移動（4件、(a) は計11件に）

いずれも実測で寸法を確定し tk17 でピンした上で、
逸脱の根拠をここに文書化する。● にはしない
（ユーザーの判断事項）。

- Android #6 Checkbox / #7 RadioButton: グリフは
  dp 数値スケールで描かれる（チェックボックス箱
  18px、ラジオ箱 18px + ドット 8px @ +5）。換算値
  （18dp → 21px、ドット 8dp → 9px）とは異なるが、
  これは px タイポグラフィ（sp 数値をそのまま px
  として使う §3 の型スケール）と内部比例が一貫する
  実装姿勢であり、Switch の (a) 前例（トラック
  52×32 を dp 数値のまま使用）と同型。行高・
  タッチターゲットは全体規則（56px）で満たす。
- Android #9 Slider（単一）/ #10 Slider（Range）:
  トラック 16px・つまみ直径 28px は UI-STYLE-v2 /
  UI-REDESIGN 2.2 に文書化されたツールキット自身の
  デザイントークン。現行 M3 トークン（Expressive:
  トラック 16dp → 19px、ハンドル 4×44dp）とも
  クラシック M3（トラック 4dp → 5px、つまみ
  20dp → 23px）とも異なる。値バブルは換算どおり
  （48×44dp）。Slider の形自体は M3 Expressive
  ピル型ハンドル化の提案がユーザー判断待ちで
  分離されており、本移動はその判断を先取りしない。
  WinUI 側は #9 が ●（トラック 4epx → 6px、
  つまみ 26px）、#10 は #9 と同一形状 + デュアル
  つまみ位置のピンで ● とした（モードで扱いが
  異なるのは、WinUI 側にこの種の設計トークン
  逸脱が無いため）。

### 16.3 タイポグラフィ照合（§2.4 の未了項目を閉じる）

M3 TypeScaleTokens（一次ソース、全15段）:
BodyLarge 16/24、BodyMedium 14/20、BodySmall
12/16、LabelLarge 14/20、LabelMedium 12/16、
LabelSmall 11/16、TitleLarge 22/28、TitleMedium
16/24、TitleSmall 14/20、HeadlineSmall 24/32、
HeadlineMedium 28/36、DisplaySmall 36/44（sp、
size/lineHeight）。ツールキットの5スタイル
（v2 テーマ）: BODY 16 = BodyLarge の size と一致。
DISPLAY 28 = HeadlineMedium の size と一致。
CAPTION 14 = LabelLarge / BodyMedium の size と
一致。HEADING 16 = TitleMedium の size と一致
（ウェイトは通常）。TITLE 20 はランプに無い段
（最近傍は TitleLarge 22）で、envfit §3 の実機
タイトル寸法として選ばれた値。行高は全スタイル
とも同梱フォントのメトリクス由来（BODY の行
ステップ 21px に対しトークン 24sp）。

WinUI type ramp（Microsoft Learn、取得済み）:
Caption 12/16、Body 14/20、BodyLarge 18/24、
Subtitle 20/28、Title 28/36、TitleLarge 40/52、
Display 68/92（epx）。ツールキットの WinUI
テーマは size を全段 ×1.43 換算で写している
（BODY 20 = 14×1.43、DISPLAY 40 = Title 28×
1.43、TITLE 29 = Subtitle 20×1.43、HEADING 26
= BodyLarge 18×1.43、CAPTION 17 = Caption 12×
1.43、全て一致を実測）。行高はフォントメトリクス
由来（BODY の行ステップ 26px に対しランプ換算
29px）。

この照合により、WinUI #13 TextBox の A は ● と
した（高さフロア 56px・角 6px・本文フォントが
ランプ換算値と一致。Android #13 の ● と同じ
判定基準）。一方 #14 TextView は両モードとも
◐ のまま: サイズ写像は上記のとおりだが、行高が
フォントメトリクス由来で参照の行高トークンと
一致しない（Android は TITLE 段の不在も理由）。
TextView の寸法式（行数 × 行高 + 8、幅は単体
パディング）自体は tk17 でピンした。

### 16.4 ◐ のまま理由を精密化したセル（20件）

寸法は全て tk17 で実測ピン済み。◐ の理由は
「寸法テスト未網羅」ではなく下記の各理由に置き
換わる。

- Android #12 Progress（circular）: ストロークの
  公称値は仕様換算どおり（4dp → 5px、コード値）。
  ただし ring_arc はストローク幅のブラシを弧に
  沿って打つため、交点のインク帯は公称 +1px
  （実測 6px）になるラスタライズ特性がある。
  既定ボックス 48px は現行トークンの Size 40dp
  → 47px と Δ1、クラシック仕様 48dp → 56px とも
  異なる（出典側の値自体が世代で異なる）。3点の
  記録をもって ◐ とする。
  **M20 更新（2026-11-21、§18.2）**: 既定ボックスの
  Δ は理由が崩れたため是正した（モード別に解決し
  M3 = 47px、tk17 でピン）。残る ◐ の根拠はインク帯
  +1px のラスタライズ特性のみ。
- Android #14 TextView: §16.3。
- Android #20 TreeView: 行高は ListView と同じ
  行規則（56px）でピン。M3 にツリー部品自体が
  無く、インデント 18px/depth に対応する参照値が
  存在しない。
- Android #25 Date/Time picker: フィールド高は
  TextField 規則（56px）でピン。M3 のモーダル形
  （DatePickerModal 360×568dp 等）はツールキット
  に存在せず、フィールド+ステッパ形が実装形。
  モーダル形の不在は寸法の問題ではないが、部品の
  形が参照と異なるため ◐ とする。
- Android #35 ColorPicker: M3 にカラーピッカの
  仕様自体が無い。実装はダイアログ+スウォッチ+
  スライダの合成形で、自然寸法（392×532）を
  ツールキット形としてピンした。
- WinUI #3 IconButton/FAB: WinUI に FAB が無い。
  実装は Android 形と共通（64×64、Android #3 の
  (a) 記録が根拠）。
- WinUI #4 FAB menu: WinUI に対応部品が無い。
  寸法は Android 形と共通の構成（項目 = トーン
  ボタン 56px、間隔 4dp → 5px、箱パディング
  8px）でピンした。
- WinUI #14 TextView: §16.3。
- WinUI #16 Chip: WinUI に第一者の Chip コント
  ロールが無い。高さ 40px は Android 形と共通
  （Android #16 の (a) 記録と同一ウィジェット）。
- WinUI #17 InfoBadge: 参照の高さは範囲指定
  （MinHeight 4epx / MaxHeight 16epx → 6〜23px、
  値フォント 11epx）で単一値が無い。実装の
  19px は範囲内にあることを確認しピンした
  （Android 形と共通のバッジ）。
- WinUI #18 Card: WinUI に第一者の Card コント
  ロールが無い。パディング 19px（ツールキット
  規則）と角 6px（ControlCornerRadius）はピン
  した。角を OverlayCornerRadius（11px）に
  寄せる根拠は一次ソースに無いため変更しない。
- WinUI #20 TreeView: TreeViewItemContentHeight
  20epx → 29px（TreeView.xaml で確定）は最小
  コンテンツ高であり、実装の行高 56px（行規則）
  はそれを上回る。行高自体の参照値は無い。
- WinUI #25 Date/Time picker: フィールド高は
  ピン（56px）。ただし DatePicker の
  ThemeMinWidth 296epx → 423px が themeresources
  に実在し、実装のコンテンツ駆動幅（168px）は
  それを満たさない。ツールキットのピッカは
  フライアウト形ではなく小型ステッパフィールド
  として設計された形であり、幅フロアの強制は
  その形と両立しないため、逸脱を数値で記録して
  ◐ とする。
- WinUI #28 TabView: TabViewItemMinHeight
  32epx → 46px / MinWidth 100epx → 143px が
  実在する。実装のタブ帯は両モード共通の部品で、
  高さはコントロールフロア 56px、項目幅は
  テキスト駆動（最小 64px）。文書タブ形への
  切り替えは共通ウィジェットの契約を壊すため
  行わず、数値を記録して ◐ とする。
- WinUI #29 BottomBar: NavigationView の下配置は
  標準形でなく直接の参照寸法が無い。高さはセル
  内容駆動（実測 78px、WinUI の型ランプ由来）を
  ピンした。なお Android #29 の (a) 記録にある
  75px は別内容での実測で、高さ自体は内容駆動
  （tk17 の標準セル構成では 73px）であることを
  本弾の実測で確認した。envfit の 64〜76px 帯の
  中にある。
- WinUI #30 NavRail: 実装は両モード共通の M3
  レール形（幅 94px = 80dp、セル 64px）。
  NavigationView の compact pane（48epx →
  69px）は採用していない。B 観点の証拠が既に
  この形で ● のため、形は変更しない。
- WinUI #31 Drawer: パネル上限は両モード共通の
  360dp → 421px（実測でキャップを確認）。
  NavigationView の expanded pane 長は採用して
  いない（#30 と同じ扱い）。
- WinUI #32 Bottom sheet: WinUI にボトムシートが
  無い。実装形は Android と共通で、角は WinUI の
  radius_overlay（11px = OverlayCornerRadius）、
  ハンドルは共通寸法（37×5）をピンした。
- WinUI #33 Segmented: WinUI に第一者の
  Segmented コントロールが無い（実装形は
  connected ToggleButtons）。高さ 48px を
  ツールキット形としてピンした。
- WinUI #34 Carousel: FlipView は1アイテム全面
  形で、実装のマルチブラウズ形（ヒーロー 248px
  + 小アイテム 64px + 間隔 9px）とはレイアウト
  戦略が異なる。Android 側は M3 カルーセルの
  マルチブラウズ戦略に属するため ● としたが、
  WinUI 側は形の相違として ◐ とする。

### 16.5 D の処理

- #15 両モード ○ → ◐: §15.3 の「候補リスト出現は
  メニューと別経路」という記録は誤りだった。
  tk_m7_suggest_refresh は tk_menu_popup を経由し、
  候補リストは M17 でフェード入場を得たメニュー
  オーバーレイそのものである（コードで確認）。
  tk17 で検索バー経由の入場フェード（実時計で
  開く → フェード中と静止画の差分）を両モードで
  テスト化した。残る未再現は M3 SearchView の
  展開形（バーから面へ広がる遷移）で、ツール
  キットに SearchView 形自体が無い。よって ◐。
- #21 両モード ◐ の終端理由（確定記録）: 残る
  未再現は scale 0.8↔1.0 成分のみ。描画側の
  アフィン近似（座標を中心へ寄せて描く）は、
  グリフが再ラスタライズされず引き伸ばされる
  点で、面そのものを変換する参照の見た目と
  等価にならない。オフスクリーン面を持つ設計
  （メニュー面を一旦バッファへ描いて拡縮する）
  は、即時描画・ダメージベース転送の基盤契約
  （静止時転送ゼロ、M12 以来の性能前提）と
  両立しない。したがって scale 成分は現行基盤
  では再現不能と確定し、フェード成分のみで
  ◐ を維持する。基盤側にオフスクリーン面が
  入る場合はこの限りでない。
- WinUI #19 D ○ の終端理由（補強）: 本弾で
  ListViewItem.xaml（themeresources、63,908B）を
  全文走査し、ThemeTransition 要素が 0 件である
  ことを実測した。既定 ItemContainerTransitions
  は XAML テーマ層には存在せず、フレームワークの
  コード側既定値であり、一次ソース（XAML /
  Microsoft Learn）からは数値を取得できない。
  推測で実装しない方針（§15.3）を維持し、○ を
  終端とする。
- 残りの D ○ は §15.3 の理由を再確認し据え置く:
  Android #13（フローティングラベル遷移の対象
  形が無い）、Android #20 / WinUI #16 / #18 /
  #33（参照部品が無く遷移が定義できない）、
  Android #35（同左）、#25 両モード（ピッカの
  ポップアップ形自体が無く、出現遷移の対象が
  無い）。

### 16.6 ○ 3件の再検討

- Android #27 Toast A ○ → ●: 「公開形状仕様が
  存在しない」は M17 時点の記録だが、本弾で
  AOSP フレームワークの一次ソースを取得した
  （platform/frameworks/base core/res:
  layout/transient_notification.xml = wrap_content・
  マージン 16dp・パディング 16dp・テキスト縦
  パディング 12dp・maxLines 2、values/dimens.xml
  = toast_width 300dp / toast_y_offset 48dp /
  toast_text_size 14sp）。M3 モードの Toast を
  この形状へ修正した（F5）: テキストは 14sp =
  caption スタイル、幅 = テキスト幅 + 19px×2
  （上限 351px）、高さ = 行高 + 14px×2、下端
  オフセット 56px。tk17 で形状をピンした。
  あわせて D の留保も解消した: フェードは
  anim/toast_enter.xml / toast_exit.xml の実値
  （config_longAnimTime = 500ms、入場
  decelerate_quad / 退出 accelerate_quad）。
  退出は「残り割合に対する decelerate_quad」と
  数学的に同一形になるため、単一の easing で
  両曲線を正確に再現する。M13 の「M3 トークン
  への対応付け（150ms）」は AOSP 実値への置換
  により解消し、tk12 のピンを仕様駆動で更新
  した。D セルは ● のまま（根拠が近似から
  一次ソースへ変わった）。
- WinUI #27 Toast A ○ のまま（終端）: Windows の
  トーストは OS の通知面であり、アプリ内コント
  ロールとしての形状仕様は存在しない。実装形
  （本体テキスト幅 + 32px、高さ = 行高 + 16、
  オフセット 24px）はツールキット形として
  tk17 でピンした。
- WinUI #35 ColorPicker A ○ のまま: テンプレート
  （ColorPicker_full.xaml）の形（スペクトラム+
  スライダ）と実装のパレットグリッド形の相違は
  本弾でも変わらない。自然寸法は §16.4 の
  Android #35 と同じくピンしたが、対応する
  参照寸法が存在しないため ○ を維持する。

### 16.7 実在バグと前提更新

実在バグ（M18 の作業で発見・修正）:
- Split button の矢印セグメントが全高でなかった:
  アイコン専用ボタンの希望高は 24px で、水平
  ボックスが上端揃えのため、56px のバーの上部に
  24×24 の面が張り付き、シェブロンも当たり判定も
  全高でなかった。tk_splitbutton_new で
  TK_ALIGN_STRETCH を設定し両セグメントを全高に
  修正。tk17 の split テスト（子 rect の高さ・
  間隔・合算幅）が回帰ピンになる。#5 の A ● は
  この修正後の寸法に対するもの。

前提更新（仕様形への追随であり、検証の緩和では
ない）:
- tk12 toast_flow: M3 モードのフェード前提を
  150ms / Standard decelerate から AOSP 実値
  （500ms / decelerate_quad、退出は同値の裏返し）
  へ、アンカー画素を AOSP 形状の中心（y=401）へ
  更新。WinUI 側の前提（83ms Linear、y=436）は
  不変。

## 17. M19 記録（残り46セルの最終点検と総括）

実施: 残り46セル（A ◐ 31・A ○ 2・D ◐ 4・D ○ 9）を
全件再点検した。格上げは2件（いずれも未実施だった
検証の実施による ○ → ◐）。新規 tests/test_tk18.c
（10 checks）を追加し Makefile の TESTS / check に
配線。ツールキット 3,569 tests（3,559 + tk18 10）
全 PASS、ダーク再実行（TK7/TK10/TK11_DARK）全 PASS、
消費者3スイート（アプリ 1,048 / シェル 683 /
OSK 319）無修正で全 PASS、回帰ゼロ。tk18 の
ASan/UBSan はメモリエラー・UB なし、直接リーク 0
（間接リークはテストハーネス root_of のラッパ
ツリー由来で tk14/tk16 と同形）。

### 17.1 格上げ（2件）: WinUI #16 / #33 の D ○ → ◐

§15.3 はこの2件を「部品自体の同等を主張する参照が
無いため ○」とした。しかし ○ の定義は「実装済み・
未検証」であり、この2件の遷移は追随先テンプレート
の振る舞いとして実装済みで、部品固有経路の検証だけ
が未実施だった。M19 で tk18 により検証した:
- WinUI #16 Chip: ToggleButton 形の背景ブラシ遷移
  （83ms）がチップ自身の描画経路（tk_face_brush、
  未選択 = ControlFill → 選択 = accent）で、変化
  時点の無跳躍・中間値が両 settled 形と異なること・
  settled 形が無クロック参照と一致すること・復帰
  側も同じく遷移することを画素検証した。
- WinUI #33 Segmented: 選択チェックのグリフ遷移が
  WinUI の選択ペア（167ms / decelerate）で、出現
  中間状態が両 settled 形と異なり、完了フレームが
  settled 参照と画素一致することを検証した
  （tk16 は Android ペアのみ検証していた）。
第一者の WinUI Chip / Segmented が存在せず部品
自体の同等は主張できないため、上限は ◐ とする。
WinUI #18（Card）は遷移自体が実装に無く（クリック
挙動のみ）、検証すべき遷移が存在しないため
○ のまま（§15.3 の扱いを維持）。

### 17.2 再調査で終端理由を補強したセル

- WinUI #19 D（ItemContainerTransitions）:
  Microsoft Learn の ItemsControl.ItemContainer
  Transitions 記述を 2026-11-21 に再取得した。
  既定コレクションの構成（AddDeleteThemeTransition /
  ContentThemeTransition / ReorderThemeTransition /
  EntranceThemeTransition、IsStaggeringEnabled=
  "False"）が文書化されているのは **GridView の
  既定テンプレートについてのみ** で、数値
  （オフセット量・継続時間）は Learn にも
  microsoft-ui-xaml の XAML 層にも無い（M18 で
  ListViewItem.xaml 全文に ThemeTransition 0件を
  実測済み）。数値は OS 側 generic.xaml /
  フレームワークコード側にあり、一次ソースとして
  取得できない。推測で実装しない方針を維持し、
  ○ を終端とする。
- #15 両モード D ◐: 残る未再現は M3 の展開形
  SearchView（バーから面へ広がる遷移）。用語の
  明確化: ツールキットの「SearchView form」
  （tk.h の記載）はバー直下にドックする候補
  コンテナであり、その入場フェードは M18 で
  検証済み。存在しないのは全画面/モーダルな
  展開形そのもので、その形が無い以上、展開遷移の
  実装対象が無い。◐ を維持する。

### 17.3 全件再点検の判定（残り44セル、変更なし）

(a) 実機適合 11件はユーザーの判断待ちのため
対象外（§15.4 / §16.2）。以下は理由区分ごとの
再確認結果で、いずれも「まだ打てる手は無い」と
判定した。
- 参照部品が無く寸法・遷移の対応値が存在しない:
  Android #20 A/D、Android #35 A/D、WinUI #3 A、
  WinUI #4 A、WinUI #16 A、WinUI #18 A、WinUI #32 A、
  WinUI #33 A（寸法は全て tk17 でツールキット形
  としてピン済み）。
- 形の相違（参照形が実装形と異なる）: Android
  #25 A/D（モーダル/ポップアップ形の不在）、
  Android #13 D（フローティングラベル形の不在）、
  WinUI #25 D、WinUI #34 A（レイアウト戦略の相違）、
  WinUI #35 A（スペクトラム形との相違）。
- 数値で記録済みの逸脱（形と両立しないため
  強制しない）: WinUI #25 A（DatePicker
  ThemeMinWidth 423px に対し実装はコンテンツ
  駆動 168px。実装は3フィールド+ステッパの
  合成形で、単一フィールド+フライアウトの
  WinUI 形とは形自体が異なる）、WinUI #28 A
  （TabViewItem MinWidth 143px に対し実装は
  テキスト駆動・最小 64px。高さは 56px で
  MinHeight 46px を満たす）。
- 参照が範囲・最小値のみで単一値が無い: WinUI
  #17 A（高さ範囲 6〜23px 内に実装 19px）、
  WinUI #20 A（最小コンテンツ高 29px を実装の
  行高 56px が満たす。行高自体の参照値は無い）。
- 行高がフォントメトリクス由来: Android #14 A /
  WinUI #14 A（サイズ写像は §16.3 で照合済み。
  行高トークンへの強制は全テキストレイアウトに
  波及する設計変更であり、本点検の範囲では
  行わない）。
- 出典側の世代不整合 + ラスタライズ特性:
  Android #12 A（§16.4 の3点記録を再確認）。
- 共通ウィジェットの形を維持する判断: WinUI
  #29 A（直接の参照寸法なし、内容駆動 78px を
  ピン済み）、WinUI #30 A / #31 A（NavigationView
  の pane 寸法を採用しない形。B はこの形で ●）。
- D の終端: #21 両モード ◐（scale 成分は
  §16.5 の確定記録どおり現行基盤で再現不能、
  フェード成分は実装・検証済み）、WinUI #27 A ○
  （OS 通知面でアプリ内形状仕様が存在しない）。

### 17.4 sdk-check のドリフト発見と修正（実在の不整合）

総括作業の一環で `make sdk-check`（公開ヘッダ +
共有ライブラリのみでサンプルアプリをビルド・実行
する SDK 検証。make check の対象外だった）を
実行したところ、3件の FAIL が出た。切り分けの
結果、ライブラリの欠陥ではなく、サンプル側の
前提が M7 の寸法変更（control_h 48 → 56px、
187ppi 実寸化）以降ドリフトしていたものと特定
した（数値はデバッグ複製で実測）:
- Switch のダメージ転送量: 行 rect 464×56 に
  標準の 2px インフレートを加えた 468×60 =
  56,160B が転送され、272px 高のステージでは
  全体の 21.5% となり、サンプルの「< 1/5」
  前提を割った。ライブラリの挙動（転送は
  トグルした行のみ、1 rect）は正しい。
- ダイアログが開かない: 同じ高さ不足で最後の
  行（ボタン）が root の外（y=260..316、中心
  288 > 272）に落ち、クリックが当たらなかった。
  ヒットテストが root 境界を尊重する挙動は
  正しい。
修正: サンプルのステージ高を 272 → 360 に
変更した（examples/sdk_settings.c、理由と数値を
コメントに明記）。全スタックがステージ内に
収まり、Switch 行は全体の 16.2% に戻り、
ダイアログフローも通る。再発防止として
sdk-check を Makefile の check ターゲットに
組み込んだ（M19 で配線）。修正後の make check
は sdk_settings: SDK-OK を含み全 PASS。
なお、この不整合は M7 以降の各弾が「消費者
回帰ゼロ」をアプリ/シェル/OSK の3スイートで
確認していた範囲の外にあった。ライブラリ本体の
変更は本件では行っていない。

## 18. M20 記録（終端理由付き35件の再監査・#21 基盤実装・(a) 判断材料）

実施（2026-11-21）: M19 で終端とした35件（A ◐ 20・
A ○ 2・D ◐ 6・D ○ 7）を、理由の根拠となった一次
ソースをその場で再取得して再監査した。取得経路:
microsoft-ui-xaml / androidx の現行 main を GitHub
経由で取得、Microsoft Learn / developer.android.com
の現行ページを取得（いずれも 2026-11-21）。
結果: **● 化 2件**（#21 D 両モード、§18.1）、
**理由の一部が崩れ修正 1件**（Android #12 A、
§18.2。◐ のまま理由を縮小）、**理由存続 32件**
（§18.3 に再取得出典を記録）。(a) 11件の分類は
動かしていない（判断材料は §18.4）。Slider の
形状も変更していない（判断材料は §18.5）。
ツールキット make check = **3,646 tests 全 PASS +
sdk_settings: SDK-OK**（ベースライン 3,617 +
tk17 正味 +29。内訳: tk17 148 → 177、tk16 は
前提更新のみで 77 のまま）。

### 18.1 #21 D 両モード ● 化（基盤へのオフスクリーン捕獲 + scale blit）

§16.5 の確定記録は「オフスクリーン面を持つ設計は
即時描画・ダメージベース転送の基盤契約と両立
しない。基盤側にオフスクリーン面が入る場合は
この限りでない」としていた。M20 で両立する形を
実測のうえ導入し、この条件を満たした:

- 方式（overlay_paint_scaled、src/tk_core.c）:
  背景退避 → オーバーレイのサブツリーを実
  キャンバスへ通常どおり描画 → 合成済み矩形を
  捕獲 → 背景復元 → 新規公開 API
  tk_canvas_blit_scaled（src/tk_draw.c、双線形・
  クリップ尊重・ダメージ記録）でアンカー原点
  へスケール転写する。キャンバス全面に原点
  オフセットを持ち込む方式は tk_draw 全面に
  波及するため不採用とした。
- 契約との両立（実測）: 静止時（paint_scale =
  1000）は従来経路と**全画面バイト同一**（tk17 が
  無クロック参照レンダリングとの一致をピン）。
  追加転送はゼロ（転写フットプリント ⊆ 既存
  ダメージ矩形）。追加メモリはルート所有の
  スクラッチ2面で、最大のスケール対象矩形の
  サイズで再利用し、上限はルート面サイズ。
- 参照形の再取得（2026-11-21）: material3
  Menu.kt（androidx main）で入場がフェード +
  scale 0.8→1.0（アンカー原点）であることを
  再確認。時間は fluentui-android の Menu.kt
  定数（InTransitionDuration 120ms /
  OutTransitionDuration 75ms）で裏取りし、
  menu_trans_pair を新設して M3 を 120/75ms に
  是正した（従来は小表面ペア 200ms の流用）。
  スライドも同じペアに同期。
- **WinUI 形の訂正**: Microsoft Learn の再取得で、
  PopupThemeTransition はオフセット（平行移動）
  のみ、PopInThemeAnimation は「opacity +
  translation」と確定した。scale は M3 形のみに
  ゲートし（menu_scale_form）、WinUI の入場は
  フェード 167ms + スライド −6px とした。これが
  参照形と一致することが WinUI ● の根拠である
  （「WinUI も scale 同型」という従来の理解は
  実記録より緩く、本弾で訂正する）。
- 既知の過渡特性: フェード中は捕獲矩形に背後の
  背景も含まれ一緒にスケールされる（入場
  ≤167ms の間のみ。静止時は正確）。コード内
  コメントに明記済み。
- 検証: test_tk17 に test_blit_scaled（恒等・
  2倍拡大・2倍縮小・クリップ + ダメージ）と
  menu_scale_flow（モード別: M3 は中間帯走査で
  原点成長・単調拡大・退場時縮小、WinUI は
  フェード中間状態 + スライド未着地、両モード
  共通で静止時の参照レンダリングとの全画面
  バイト一致 + 残留ゼロ）を追加。test_tk16 の
  退場中間 tick は M3 退場 75ms 化に追随して
  1375 → 1340 に調整（前提更新であり検証の
  緩和ではない）。

### 18.2 Android #12 A: 理由の一部崩壊と修正

再取得（2026-11-21）: ProgressIndicator.kt
（androidx main）で CircularIndicatorDiameter =
CircularProgressIndicatorTokens.Size を確認し、
Size = 40dp は同トークンへの整合を明記した
第三者実装のコミットで裏取りした（一次ソースを
引く二次ソースである点は記録する）。§16.4 の
3点記録のうち「既定ボックス 48px は現行トークン
換算 47px と Δ1」は、実装側で是正可能な逸脱と
判定し修正した: tk_ring_new の size_px を未設定
（0）とし、measure でテーマ別に解決する
（M3 = 47px、WinUI = 48px 据え置き —
ProgressRing にサイズトークンは無く、Segmented
の measure に同型の前例がある）。tk17 のピンを
更新。残る ◐ の根拠は ring_arc のインク帯 +1px
（公称 5px に対し交点で実測 6px）のみで、これは
整数グリッド上の円弧ストロークの量子化特性
（参照の AA レンダリングにもフリンジがある）
として存続する。スピナー（ドット弧の独自形、
40px）は記録された ◐ 根拠の対象外のため
変更していない。

**M21 訂正（2026-11-21、§19）**: 上記の残根拠
（インク帯 +1px）は解消した。ring_arc のスタンプ
中心を切り捨てから丸めに変えたところ、交点の
インク帯は両モードとも公称どおり（M3 5px /
WinUI 6px）に一致した（実測、tk17 でピン）。
根拠が消滅したため #12 A は ● に格上げする。
切り捨ては負側で中心方向へ最大 1px 寄る偏りが
あり、それが帯の非対称な +1px として出ていた、
というのが実測と整合する説明である。

### 18.3 終端理由の再監査 — 理由存続 32件（再取得出典つき）

取得日は全て 2026-11-21。

- **参照部品の不在（再確認）**: Android #20 A/D・
  #35 A/D — androidx.compose.material3 パッケージ
  概要（developer.android.com）の Components 表を
  全行確認し、TreeView / ColorPicker の項目が
  無い。WinUI #3 A・#4 A（FAB なし）、#16 A
  （Chip なし）、#18 A（Card なし — SettingsCard
  は CommunityToolkit で第一者ではない）、
  #32 A（Bottom sheet なし）— Microsoft Learn
  「Windows Controls and patterns」の現行
  コントロール一覧に該当項目が無い。
- **WinUI #33 A/D の理由精密化**: SelectorBar は
  第一者コントロールとして存在するが、Learn の
  定義は「少数の異なるデータ集合 / ビューを
  切り替える選択子」で単一選択専用であり、
  連結アウトライン形も複数選択も持たない。
  M3 Segmented（連結形・単一/複数選択）とは別
  コントロールであり、「第一者の Segmented が
  無い」という理由はこの注記付きで存続する。
- **形の相違（再確認）**: Android #25 A/D —
  DatePicker / DatePickerDialog はモーダル形
  として実在する（パッケージ概要で確認）。
  実装はフィールド + ステッパ形。WinUI #25 D —
  フライアウト形との相違。WinUI #34 A —
  FlipView は「一度に1アイテムを表示する」形
  （Learn 一覧）。実装はマルチブラウズ形。
  WinUI #35 A — ColorPicker は「選択可能な
  カラースペクトラムを表示する」（Learn 一覧）。
  実装はパレットグリッド形。Android #13 D —
  M3 TextField のフローティングラベル形は参照側
  に実在するが、実装の TextField は固定ラベル形で
  遷移の対象形が無い。参照の不在ではなく実装形の
  相違として存続する（形の追加は部品の再設計で
  あり、本再監査の範囲では扱わない）。
- **数値逸脱（値を再取得して確認）**: WinUI
  #25 A — DatePickerThemeMinWidth = 296epx
  （controls/dev/CommonStyles/
  DatePicker_themeresources.xaml、現行 main）
  → 423px。実装はコンテンツ駆動 168px で、
  フライアウト形でない実装形と両立しない。
  WinUI #28 A — TabViewItemMinHeight = 32epx /
  MinWidth = 100epx（TabView_themeresources
  .xaml、現行 main）→ 46px / 143px。実装は
  高さ 56px（コントロールフロア、MinHeight は
  満たす）・幅はテキスト駆動（最小 64px）。
- **範囲・最小値のみ（再取得して確認）**:
  WinUI #17 A — InfoBadgeMinHeight 4 /
  MaxHeight 16 / ValueFontSize 11epx
  （InfoBadge_themeresources.xaml を全文取得）
  → 高さ範囲 6〜23px、実装 19px は範囲内。
  WinUI #20 A — TreeViewItemContentHeight =
  20epx（TreeView_themeresources.xaml）→
  29px は最小コンテンツ高で、実装の行高 56px
  が満たす。行高自体の参照値は無い。
- **行高がフォントメトリクス由来**: #14 両
  モード。M3 TypeScaleTokens（androidx main、
  v0_103）全15段を再取得し §16.3 の値と完全
  一致（BodyLarge 16/24、BodyMedium 14/20、
  BodySmall 12/16、LabelLarge 14/20、
  LabelMedium 12/16、LabelSmall 11/16、
  TitleLarge 22/28、TitleMedium 16/24、
  TitleSmall 14/20、HeadlineSmall 24/32、
  HeadlineMedium 28/36、DisplaySmall 36/44）。
  WinUI type ramp は Learn「XAML theme
  resources」で再確認（サイズ Caption 12 /
  Body 14 / Body Large 18 / Subtitle 20 /
  Title 28 / Title Large 40 / Display 68、
  行高 16 / 20 / 24 / 28 / 36 / 52 / 92）。
  サイズ写像は両モードとも一致するが、行高は
  同梱フォントのメトリクス由来（Android BODY
  行ステップ 21px 対トークン 24sp、WinUI
  26px 対ランプ換算 29px）で存続する。
- **共通ウィジェットの形を維持**: WinUI #29 A
  — NavigationView に標準の下配置形は無く、
  高さは内容駆動（実測 78px）をピン済み。
  WinUI #30 A / #31 A — NavigationView.idl の
  既定値 CompactPaneLength 48.0 / OpenPane
  Length 320.0 を再取得で確認した。pane 寸法を
  採用しない形は B 観点がこの形で ● のため
  維持する。
- **D の終端（再確認）**: #15 両モード ◐ —
  M3 SearchBar / DockedSearchBar は実在する
  （パッケージ概要）。存在しないのは全画面
  展開形そのもので、展開遷移の実装対象が無い
  （§17.2 の用語明確化どおり）。WinUI #16 D /
  #33 D ◐ — 代理コントロールの遷移追随は
  tk18 で検証済みで、第一者部品が無いため
  上限は ◐。WinUI #18 D ○ — 遷移自体が実装に
  無い（クリック挙動のみ）。WinUI #19 D ○ —
  ListViewItem.xaml に対し ThemeTransition /
  ItemContainerTransitions のコード検索が現行
  main で 0 件であり、M18 の全文走査（0件）を
  再裏取りした。数値はフレームワークコード側に
  あり一次ソースとして取得できない。WinUI
  #27 A ○ — トーストは Windows.UI.Notifications
  の通知プラットフォームが描画する OS 面で
  （Learn の ToastNotificationManager 記述）、
  WinUI コントロール一覧にも Toast は無い。
  アプリ内コントロールとしての形状仕様は
  存在しない。

### 18.4 (a) 11件の判断材料（分類は動かさない）

対象11件のウィジェットを両テーマで描画した撮影:
`screenshots/m20_fit_android.jpg` /
`screenshots/m20_fit_winui.jpg`（生成:
tests/shot_m20.c、実ウィジェットの描画）。
各項目の「両モードでの見た目」はこの撮影と
下記のモード間差異を指す。換算は §3 の全体規則
（Android 1dp = 1.16875px、WinUI 1epx = 1.43px、
タッチフロア 56px）。
（注記: この換算前提は前提訂正 §20 で置き換えられた。
現行の換算は 1dp = 1.12px / WinUI ×1.344 / フロア 54px で、
(a) 残差の最新値は §22。本節は M20 時点の判断材料の
記録として保持する。）

#### (a-1) Android #3 FAB — 既定 64px

- 参照仕様の値: M3 FAB コンテナ 56dp → 換算 65px。
- こちらの値: 既定 64px（Δ −1px）。
- 実機適合の根拠: §3 に既定値として文書化。
  48dp タッチ最小（56px）を十分上回る。
- 両モードでの見た目: 同一ウィジェット。
  WinUI 側は FAB 自体が第一者に無いため #3 A は
  ◐（別理由）。撮影では WinUI 側はアクセント
  塗りの角丸正方形として描画される。

#### (a-2) Android #6 Checkbox — グリフ 18px

- 参照仕様の値: CheckboxTokens コンテナ 18dp
  → 換算 21px。
- こちらの値: 箱 18px（dp 数値をそのまま px 化）。
- 実機適合の根拠: §16.2 — px タイポグラフィ
  （sp 数値をそのまま px として使う §3 の型
  スケール）と内部比例が一貫する実装姿勢。
  行高・タッチターゲットは全体規則（56px）で
  満たす。
- 両モードでの見た目: WinUI #6 は ●
  （themeresources 換算と一致する別形）。
  (a) は Android モードのみ。

#### (a-3) Android #7 RadioButton — グリフ 18px + ドット 8px

- 参照仕様の値: グリフ 18dp → 換算 21px、
  ドット 8dp → 換算 9px（§16.2 の記録値）。
- こちらの値: 箱 18px + ドット 8px（+5 位置）、
  dp 数値スケール。
- 実機適合の根拠: (a-2) と同じ（§16.2）。
- 両モードでの見た目: WinUI #7 は ●。(a) は
  Android モードのみ。

#### (a-4) Android #8 Switch — トラック 52×32

- 参照仕様の値: M3 スイッチトラック 52×32dp
  → 換算 61×37px。
- こちらの値: トラック 52×32px（dp 数値のまま）。
- 実機適合の根拠: §16.2 — (a-2) と同型の
  dp 数値スケール姿勢で、Switch はその前例。
  操作高は行の 56px フロアで確保する。
- 両モードでの見た目: WinUI #8（ToggleSwitch）
  は ●（40epx 系の換算が丸め帯域内で一致）。
  (a) は Android モードのみ。

#### (a-5) Android #9 Slider（単一） — トラック 16px / つまみ 28px

- 参照仕様の値: 現行 M3（Expressive）トラック
  16dp → 19px、ハンドル 4×44dp → 5×51px の
  ピル形。クラシック M3 はトラック 4dp → 5px、
  つまみ 20dp → 23px。
- こちらの値: トラック 16px、つまみ直径 28px。
  UI-STYLE-v2 / UI-REDESIGN 2.2 に文書化された
  ツールキット自身のデザイントークン（§16.2）。
- 実機適合の根拠: 抵抗膜1本指では 23px の
  つまみより 28px が掴みやすく、トラック 16px
  は視認性を確保する。ただしこれは参照形からの
  意図的な逸脱であり、同等と認めるかは判断事項。
- 両モードでの見た目: WinUI #9 は ●
  （トラック 4epx → 6px、つまみ 26px で
  themeresources 適合）。ピル形との並列撮影は
  §18.5。

#### (a-6) Android #10 Slider（Range） — #9 と同形

- 参照仕様の値 / こちらの値: (a-5) と同一
  （デュアルつまみも同じトークン）。値バブルは
  換算どおり（48×44dp → 56×51px）で、この部分
  に逸脱は無い（§16.2）。
- 実機適合の根拠: (a-5) と同じ。
- 両モードでの見た目: WinUI #10 は ●。

#### (a-7) Android #16 Chip — 高さ 40px

- 参照仕様の値: M3 チップコンテナ 32dp →
  換算 37px。
- こちらの値: 40px。
- 実機適合の根拠: §3 — 換算 37.4px を実寸側
  （タッチ側）へ切り上げた。抵抗膜での誤タップ
  を避ける方向の適合。
- 両モードでの見た目: 同一ウィジェット。
  WinUI #16 A は第一者 Chip が無いため ◐
  （別理由）で、高さ 40px は共通。

#### (a-8) Android #17 Badge — 高さ 19px

- 参照仕様の値: M3 バッジ 16dp → 換算 18.7px。
- こちらの値: 19px（Δ +0.3px の丸め）。
- 実機適合の根拠: §3 — 換算値の切り上げ丸め。
  実質的な差は 1px 未満。
- 両モードでの見た目: 同一ウィジェット。
  WinUI #17 A は InfoBadge の範囲指定（§18.3）
  のため ◐（別理由）で、実装 19px はその範囲
  （6〜23px）内でもある。

#### (a-9) Android #19 ListView — 行高 56px

- 参照仕様の値: 行高換算 57px（§2.4）。
- こちらの値: 56px（Δ −1px）。
- 実機適合の根拠: §2.4 — 57px は素換算で、
  ×1.40 の着地（56px）は Switch トラック
  40epx → 56px と同一の丸め帯域内にある。
  実機のタッチフロア（行高 ≥ 56px）とも一致。
- 両モードでの見た目: 行高は両モード共通
  56px。WinUI 側は (a-11) として同じ記録に
  乗る。

#### (a-10) Android #29 BottomBar — 総高 75px

- 参照仕様の値: M3 NavigationBar コンテナ
  80dp → 換算 94px。
- こちらの値: 内容駆動で総高 75px（tk17 の
  標準セル構成では 73px。§16.4 の実測）。
- 実機適合の根拠: §3 — envfit §3 の実機寸法帯
  （64〜76px）の内側。480px 高の画面で 94px の
  バーは画面高の約20%を占めるため、実機帯を
  優先した。
- 両モードでの見た目: 同一ウィジェット。
  WinUI #29 A は NavigationView の下配置に
  標準形が無いため ◐（別理由、§18.3）で、
  高さの扱いは共通。

#### (a-11) WinUI #19 ListView — 行高 56px

- 参照仕様の値: 40epx × 1.43 = 57px（素換算）。
- こちらの値: 56px（Δ −1px）。
- 実機適合の根拠: (a-9) と同一記録（§2.4 の
  丸め帯域 + タッチフロア）。
- 両モードでの見た目: Android 側は (a-9)。
  両モードで行高 56px が共通。

### 18.5 Slider ピル型ハンドルの判断材料（形状は変更しない）

現行形（実ウィジェット）とピル型（提案形の
キャンバスモック — ウィジェット未実装、記録済み
トークン トラック 16dp → 19px / ハンドル
4×44dp → 5×51px から描画）の並列撮影:
`screenshots/m20_slider_android.jpg` /
`screenshots/m20_slider_winui.jpg`
（生成: tests/shot_m20.c）。

- 寸法対照: 現行 = トラック 16px・つまみ直径
  28px（円形）。ピル型 = トラック 19px・
  ハンドル 5×51px（縦ピル、トラック内に 6px の
  ギャップを空けて配置）。
- 状態別: 値 0 / 50 / 100、押下（現行形は押下中
  に値バブル「50%」を表示する — 撮影で
  pressed 行の上に見えるバブルがそれ。モックは
  押下形の寸法指定が無いため静止形と同寸で
  描画）、無効（両形とも減衰色）。
- モード間: WinUI 側の現行形は themeresources
  適合で ● のため、ピル形は WinUI への提案では
  なく Android 側提案の参考表示として同じ撮影に
  含めた（WinUI 色で描画）。
- 本節は判断材料であり、形状の変更は行って
  いない。(a-5) / (a-6) の扱いとあわせて
  ユーザーの判断を待つ。

### 18.6 M20 判断材料撮影の異常と是正（2026-11-21）

§18.4 の撮影（m20_fit_*）へのユーザー指摘を
起点に切り分けた。3点の異常の内訳は、ライブラリ
描画バグ2件（＋同型1件）と撮影側の問題だった。
詳細は REPORT-toolkit-m20-fix.md。

- **Android #6 チェック済みマーク（ライブラリ）**:
  M3 のチェック済みマークが 10×10 の塗り正方形
  で、チェック形ではなかった。チェック済みの箱が
  「空の枠」に読め、未チェックと誤読された（撮影
  プログラムの状態は両モードとも checked=true で
  揃っていた）。WinUI 側は元々チェック形で正しい。
  → 共通ヘルパー paint_check_glyph（チップのグリフ
  と同じ 2×2 ブロック2ストローク、glyph_pos で
  中心から拡大）を新設し、Checkbox の有効/無効
  checked 両経路と、同じ 18px ボックス言語を
  共有する ListView チェック可能行に適用。箱の
  輪郭も fill（r3）に沿う stroke_round_rect に
  是正した。
- **Android #7 ラジオの輪（ライブラリ）**: M3 の
  輪が円 fill（r9）の上に stroke_rect（四角）で
  描かれ、角丸四角に読めた。無効経路は元々
  stroke_round_rect で正しかった。→ 有効経路も
  stroke_round_rect r9 に是正。
- **● の根拠の置き換え（正直な記録）**: Android
  #6 B / #7 B / #19 B の従来の ● は、tk12 / tk13
  の「存在のみ」サンプル（中心ピクセルの色・
  塗りの有無）に依拠し、グリフの形状と輪の形状を
  固定していなかった。今回の欠陥はその盲点に
  入っていた。是正後は tk17 の test_glyph_shapes
  （25件: マークの両腕・隅の空き・画素数帯、
  輪の隅クリア＋辺中点、ListView 箱、WinUI 側の
  正形ガード）が形状を固定し、● はこの新根拠に
  置き換わる。マトリクスの記号と集計（● 410）は
  変更しない。既存ピンの前提更新2件（tk13 /
  tk15 の ListView 箱の塗りサンプル (bx+1, by+1)
  → (bx+2, by+2)）は、四角ストローク時代の偶然の
  ピクセルを指していたための移動で、検証する
  性質（アクセント塗り）は維持している。
- **WinUI #29 ボトムバー（ライブラリの位置バグ
  ではない）**: 実測で、アクセントバーは選択
  セルの左端・垂直中央（3×24px）に設計どおり
  描かれており、tk7 が同位置をピンしている
  （NavigationView 形）。「ハイライトが無い」
  ように見えた主因は (i) subtle 塗りが
  SubtleFillColorSecondary 相当（alpha 9）で
  白地ではほぼ不可視、(ii) 撮影の合成アイコンが
  純白で、アイコンは素通し blit のため淡い面の
  上で不可視だった。(ii) は撮影側の問題として
  グリフをテーマのテキスト色に変更した。#29 の
  セルは変更しない（バー左端の縦線という形
  そのものへの異論は設計判断として別途）。
- **撮影の是正**: 状態を両モードで明示的に
  揃え（チェック 有/無・ラジオ 選択/非選択の
  2×2配置）、参照ラベルをモード別にした
  （WinUI 側に Material の dp 参照値がそのまま
  印字されていた）。撮り直した2枚は目視確認
  済み（チェック形・円形・状態一致・ラベル
  重なり無し）。
- テスト総数: 3,646 → **3,671**（tk17 +25）、
  全 PASS、回帰ゼロ（上記のピン前提更新2件を
  除く）。
- 未対応の観測: メニューのチェック可能項目の
  マークも 10×10 のアクセント正方形（メニュー
  描画）。今回の指摘範囲外のため未変更。
  チェック形に寄せるかは別判断とする。

## 19. M21 記録（復旧・細部調整・実コード突き合わせ）

経緯: 先行ラインが最終報告なしに終了し、4ファイルに
途中状態の変更が残った。復旧ラインが棚卸しと green 化
を行った（詳細は REPORT-toolkit-m21.md）。本節はその
確定事項の記録であり、実コード突き合わせの結果は
§19.2 以降に追記する。

### 19.1 復旧と (a) 値の確定（2026-11-21）

ユーザーの確定基準: 既定は「一致させる」。例外は
Brain 実機への適合に具体的な必要性を数値・実測で
示せる箇所だけ。(a) の分類（判断待ち）は動かさない。

- **値を参照換算に一致させた (a)**: (a-1) FAB 既定
  64 → 65px（56dp 換算）。(a-2) Checkbox の M3 箱
  18 → 21px（18dp 換算）、ラベル位置 23 + unit。
  ListView チェック行の箱も同値に追随。(a-3) Radio
  の M3 リング 21px・ドット 9px（§16.2 の記録値
  18dp / 8dp の換算。トークン実値の再検証は
  §19.2 で行う）。(a-4) Switch の M3 トラック
  61×37px・つまみ 28px（52×32dp / 24dp 換算）。
  (a-7) Chip は Android 形のみ 40 → 37px
  （32dp 換算）: WinUI / classic は第一者 Chip が
  無く、tk7 がピンする共有形 40px を維持する
  （モードで値を分けた。根拠は本項）。
  いずれもタッチ適合は行高・操作領域（56px フロア）
  で満たされ、グリフ寸法自体に実機適合の必要性は
  示されなかったため、例外ではなく一致として処理した。
- **戻した変更**: Slider トラック厚トークン
  16 → 19px（無印・無記録だった）。(a-5) の Slider
  形は §16.2 が「ピル型ハンドル化のユーザー判断を
  先取りしない」と分離した項目であり、トラック
  だけの変更は Expressive 形でもクラシック形でも
  ない中途の形になるため、M20-fix 時点の 16px に
  戻した。Slider 形の判断待ちは従来どおり。
- **メニューのチェック印**: 10×10 のアクセント
  塗り正方形を、項目の前景色によるチェックグリフ
  （paint_check_glyph、16px スロット）に変更した
  （§18.6 で同種と記録されていた残件）。あわせて
  test_tk9 の旧ピンの訂正: M3 側の「アクセント
  画素」ピンは、印ではなくメニューのフォーカス
  リングの画素を数えて通過していた（実測で確認）。
  新ピンは前景色のストロークを両モードで数える。
- **#12 A の格上げ**: §18.2 の M21 訂正を参照
  （インク帯の解消）。● 410 → 411。
- テスト総数: **3,671 件、全 PASS、SDK-OK**
  （M20-fix ベースラインと同数、回帰ゼロ）。
  ピンの前提更新は上記の値変更に伴うもののみ。

### 19.2 細部調整（実測監査・撮影・形の言語）

詳細は REPORT-toolkit-m21.md §6。確定事項のみ:

- **WinUI スライダーの不可視（実欠陥、修正済み）**:
  レールが container 塗り（#F9F9F9）で背景に
  溶けていた。レール = strong_fill、disabled の
  両スパンとつまみ = strong_fill_dis に変更し、
  つまみにアクセント内ドット（12epx、静止
  スケール 0.86 = 14px）を追加した。実測:
  レール #f9f9f9 → #868686、disabled 両スパン
  #a5a5a5（均一）。根拠は SliderTrackFill の
  系統（UWP = BaseMediumLow、Fluent = strong 系、
  ProgressBar トラック = ControlStrongStroke
  の一次抽出）。WinUI 3 での最終ブラシ別名は
  未確定と明記する。ドットの状態スケール
  （hover 1.167 / pressed 0.71）は未実装が残差。
- **トラックの二重ブレンド（修正済み）**: α色の
  トラック全幅塗り＋ active スパンの重ね塗りで
  中央に段差が出ていた。非アクティブ側は active
  スパンの外側だけを描く（接合部はつまみの下）。
  M3 disabled の active スパンも単層 38% に是正。
- **メニュー印の色（WinUI、テンプレート準拠に
  修正）**: 静止時は二次テキスト色
  （MenuFlyoutSubItemChevron 相当）、ホバー行で
  項目前景色に切替わる形にした。M3 は
  コンテンツ色のまま。実レンダリング色でピンを
  固定（test_tk9）。
- **M3 メニューの角丸（実欠陥、修正済み）**:
  ダイアログ用 overlay 半径 28px の流用を、
  extraSmall 4dp = 5px に是正（fill と stroke の
  角の食い違いも解消）。形スケールの照合では
  他部品は換算どおり（WinUI は Control 4epx→6 /
  Overlay 8epx→11 で全整合、M3 はボタン full /
  カード 14 / ダイアログ 33 / FAB 19 / チップ 9）。
- **WinUI Switch のラベル間隔（実欠陥、修正
  済み）**: テンプレートの列間隔 12epx = 17px に
  対し汎用の 8px を使っていた。measure / paint
  とも 17px にし、実測インク左 +64 → +73
  （換算位置 72.8px と一致）。
- **文字位置の監査（欠陥なし）**: ラベル差し替え
  2描画差分で実測。全コンポーネントが「行
  ボックスを中央に置く」形で統一され、インクの
  残差（descender 無し +0.5〜+1.5px、有り
  +2.5〜+3.0px）は同梱フォントのメトリクス
  由来で、参照側と同じ種類の振る舞い。
- **値バブル**: 配置は参照どおり（つまみの上に
  浮く 48×44dp 換算）。撮影側の帯域不足で上の
  行に重なって見えた分は撮影を直した（pressed
  行 116px + 下揃え、モックのハンドルの
  クランプ、ラベル現行値化）。4枚を目視確認
  済み（REPORT §6.4）。
- テスト総数: **3,680 件、全 PASS、SDK-OK**
  （§19.1 の 3,671 + 新規ピン 9、旧挙動ピンの
  更新は根拠コメント付き）。

### 19.3 実コード突き合わせ

WinUI 側は一次抽出が完了し、上記の修正に
反映済み（出典: microsoft-ui-xaml `main`、
controls/dev/ 配下、2026-11-21 取得。期間
トークン Normal 250ms / Fast 167ms / Faster
83ms は Microsoft Learn 公式
https://learn.microsoft.com/en-us/windows/apps/design/motion/timing-and-easing
で確定）。Android 側も一次抽出が完了した
（出典: androidx/androidx `androidx-main`、
compose/material3 の tokens/ 120ファイル全件と
components/ 約56ファイル、2026-11-21 取得。
詳細は REPORT-toolkit-m21.md §5.3）。確定した
記録訂正:
- **Radio（§16.2 の記録を置き換える）**:
  「18dp 箱 + 8dp ドット」は現行 material3
  Compose に存在しない。現行正は IconSize
  20dp（外径）・線幅 2dp・ドット公称 12dp
  （塗り実径 約10dp）。(a-3) の参照値は
  これに差し替わる（分類は不動）。
- **Checkbox**: トークン 18dp だが既定フラグ
  OFF の実効は 20dp 箱 + 2dp パディング。
  (a-2) の参照は実効 20dp を正とする。
- **Slider**: 現行ブランチにクラシック 4dp
  トラックは無い（Expressive の 16dp のみ）。
  値バブルは Slider でなく Label の管轄。
- **ListItem**: 1行 56dp が一次ソースの正で、
  (a-9) の参照根拠は §2.4 の統一値から
  56dp（§20 の新換算で 62.7px）に置き換わる。
- Switch のつまみは状態可変（16 / 24 / 28dp）
  で当方の固定径は未実装の差異。状態層
  Pressed 0.10 は当方実装と一致。モーションは
  参照が spring 既定で当方の ms 補間と方式が
  異なる。TreeView / ColorPicker / Toast は
  material3 に実体なし。

### 19.4 換算方式の根拠（画面比ではなく物理サイズ）

（注記: 本節の数値は 800×480・187ppi 前提で書かれて
おり、前提訂正（§20、854×480・約179ppi）により
数値部は置き換えられた。方式の結論は不変で、
新前提での試算は REPORT §8.2 が持つ。）

ユーザーからの問い「参照側の想定画面サイズと Brain の
比率で計算したら悪影響が出るか、現行のやり方のほうが
いいのか」に、一次ソースと試算で答える（2026-11-21
取得）。

**参照側の単位定義（どちらも画面サイズ比ではない）**:

- Android の dp: 公式は「UI の見た目の大きさを保存する
  ための単位」と定義し、換算式は `px = dp × (dpi/160)`
  （1dp = 160dpi 画面の 1px = 1/160 インチの物理寸法）。
  画面サイズは定義に一切現れない。出典:
  https://developer.android.com/training/multiscreen/screendensities
- WinUI の epx: 「画面密度に依らない仮想単位」で、OS が
  視距離と画面密度からスケール係数（100〜400% の
  プラトー）を選び「知覚される大きさ」を正規化する。
  同じ 1080p でもモニタは 1080 epx 以上、TV は 540 epx
  になる — つまり epx は物理解像度からも一意に決まら
  ない。設計対象は「物理画面」ではなく「ウィンドウの
  epx 幅」であることも明記されている。出典:
  https://learn.microsoft.com/en-us/windows/apps/design/layout/screen-sizes-and-breakpoints-for-responsive-design

**現行換算はこの物理サイズ保存に沿っている**:

- dp: 1dp = 187/160 = 1.16875px（実機 187ppi）。
  48dp = 56.1px = 7.62mm — ガイドラインの物理寸法
  そのものが実機に再現される（§3）。
  （前提訂正後の現行値: 1dp = 1.12px、48dp = 53.8px、§20。）
- epx: ×1.43（着地の丸め帯域 ×1.40 を含む、§2.2 /
  §18.4。前提訂正後は錨方式で ×1.344 に再導出、§20）。
  これは「Windows がこのクラスの携帯画面に
  選ぶであろう知覚スケール」を、両モードの物理寸法が
  実機上で一致する値として固定したもの: Switch
  トラック 40epx → 56〜57px = 7.6〜7.7mm（Android の
  48dp = 7.62mm と同帯）、本文 14epx → 20px =
  2.72mm（M3 本文 16dp → 18.7px = 2.54mm と同帯）。
  対して 96dpi の素の物理換算 ×1.948 では
  ContentDialog の MaxWidth 548epx → 1,067px が画面幅
  800px を超え、MinWidth 320epx → 623px（画面の 78%）
  になる。epx 側はスケール選択自体が適合点であり、
  素の換算は実機で破綻する。

**画面比方式の試算（悪影響）**:

前提の問題: 参照側に単一の「想定画面サイズ」が存在
しない（Android は電話〜TV〜デスクトップ、WinUI は
任意のウィンドウ）。自然な参照を2通り取って試算する
と、係数自体が定まらないうえ、どちらも破綻する:

- WinUI デスクトップ参照 1920×1080 @100% →
  係数 800/1920 = 0.417。Switch トラック 40epx →
  16.7px = 2.3mm（指の最小約 7.6mm の 30%）、
  最小操作高 32epx → 13.3px = 1.8mm、本文 14epx →
  5.8px = 0.79mm で判読不能。本機は抵抗膜 1 本指で
  指向精度が静電容量式より低いため、タッチ下限割れの
  実害は電話より大きい。
- Android FHD 電話参照 1080px 幅（420dpi）→ 係数
  800/1080 = 0.741 を物理 px に適用すると、48dp の
  タッチ対象 = 126 物理 px → 93px = 12.7mm と
  ガイドラインの物理寸法を 67% 上回り、高さ 480px の
  画面では行が収まらなくなる（逆向きの破綻）。

**結論**: 換算は物理サイズ基準（現行方式）が正しい。
両プラットフォームの単位は元々「画面比」ではなく
「物理／知覚サイズの保存」のために設計されており、
現行換算はその不変量（タッチ ≥ 約 7.6mm、本文の
可読寸法）を実機で再現する。小さい画面で破綻する
箇所（ダイアログ幅等）だけ (a) の実機適合で受ける、
という現行の切り分けを維持する。

## 20. 前提訂正: パネルは 854×480・約179ppi（2026-11-21）

従来前提（800×480・187ppi・5型）は誤りだった。
本節が §2.2 / §3 / §18.4 / §19.4 の数値部の前提を
置き換える（結論の構造は不変）。詳細は
REPORT-toolkit-m21.md §8。

- **実機の確定値**: 854×480、表示域 121×68mm
  （DTS 値。公式仕様の 121.1×68.0mm と整合）、
  約179.2ppi（横 179.1 / 縦 179.3）、
  1px = 0.1417mm。
- **fb 解像度の3点照合**: (1) DTS
  imx28-pwsh6.dts の &brainlcd: lcd-width 854 /
  lcd-height 480 / width-mm 121 / height-mm 68。
  (2) ドライバ linux-brain/drivers/gpu/drm/tiny/
  brain.c（compatible "sharp,brainlcd"）の
  get_modes が DT 値を 854×480・121×68mm の
  PREFERRED モードとして登録する。
  (3) 運用実績: RUN19 の QMP screendump が
  854x480、braindesk が bu_display_open(854, 480)。
- **換算係数の再導出**: Android 1dp = 179.2/160
  = **1.12px**、48dp = 53.8px → **タッチフロア
  54px**（物理 7.62mm は不変）。WinUI は旧 ×1.43
  の導出記録が残っていないため、錨方式で再導出:
  40epx を 48dp の物理寸法に同寸化 → **×1.344**
  （別法の比保存では 1.370、着地帯 54〜55px で
  一致）。最終決定は親の段階判断。
- **再計算の要点**（全表は REPORT §8.3）:
  (a) 11件は分類不動のまま残差が変わる
  （FAB +2、Checkbox +1、Switch +3/+1、
  Chip +1、Badge +1、ListView 行 +2、
  Range バブル +2/+2 等）。WinUI 型スケールは
  全段 1〜2px 縮小（20→19、17→16、26→24、
  29→27、40→38）、appbar_h 69→65。
  M21 のスライダードット 14px は新換算でも
  14px で不変。
- **幅 800 前提の棚卸し**（修正は親の段階判断後、
  REPORT §8.4）: 実害あり = src/tk_evdev.c の
  screen_w 既定 800（較正セッタは存在）、
  src/tk_core.c のルート幅フォールバック 800、
  shell/src/wm.c の workarea/screen 既定
  800×448 / 800×480、OSK の設計幅 800。
  代表性の問題 = tests/shot_m20.c の撮影
  キャンバス 800×480（(a) 判断材料は 854 での
  撮り直しが必要）、bench_paint の基準面積。
- **色数の確定**: 公式仕様に色数の記載は無い。
  両 OS が駆動する表示経路は **RGB565 =
  65,536 色**（パネル原生形式として検証記録
  あり）。パネル素子自体の階調は公表なし・
  未確認。

## 21. 実機特性監査（2026-11-21、詳細は REPORT §9）

- **RGB565 の潰れ（全5テーマ機械検証）**:
  テキスト対背景は全テーマで潰れなし。
  **WinUI dark の面ラダーが実質2段に潰れる**
  （surface / card_bg / subtle / control_fill 等
  が 565 値 0x2965 に collapse、一段下の群が
  0x2945）— カードと subtle 塗りは境界線でしか
  見えない。直す場合は Fluent 逐語値からの
  意図的な逸脱になるため (a) 同種の判断事項と
  して親の段階判断に委ね、本弾では未変更。
  WinUI light の状態塗り collapse は 24bit 時点
  で 1〜2/255 しか離れていない参照固有の性質で
  調整対象外。M3 light の inverse_on_surface /
  container_mid、M3 dark の desktop_bg / smoke
  の collapse は文脈が分離しており記録のみ。
- **バンディング**: ライブラリにグラデーション
  描画は無く、影は単段 α。ディザリング不要と
  判断。写真側の階調は画像パイプラインの
  観察事項。
- **パネル**: コントローラは Ilitek ILI9805
  （ドライバ出典）。視野角・コントラスト・
  ガンマ・応答速度の公表データシートは
  2026-11-21 の調査で発見できず**未確認** —
  統合フェーズの実機確認項目。数値は埋めない。
- **タッチ**: i.MX28 LRADC 4線式抵抗膜、
  DTS で平均化設定あり（ave-ctrl 4 等）。
  ツールキット側は線形写像のみで追加の平滑化
  なし。ドラッグ可否は現行寸法で不足なしと
  判断し、実機ノイズ実測は統合フェーズ。
  ホバー代替（押下/フォーカス層）は従来扱いを
  再確認。
- **アニメーション**: 期間は参照どおりで変更
  なし。大きな移動を伴う遷移の実機での
  読みやすさは統合フェーズ確認項目。

## 22. M22 前提訂正の適用（2026-11-21、詳細は REPORT-toolkit-m22）

§20 の新前提（854×480・約179.2ppi）を全面適用した。
分類（●/◐/○）は一切変更していない（ユーザーの
判断待ち）。変わったのは px 値と残差数値のみ。

- **タッチフロア 54px**: 48dp = 53.8px（1.12px/dp）。
  tk_theme_v2 / v2_dark / winui / winui_dark の
  control_h・row_h を 56 → 54。classic は歴史値の
  まま。派生寸法（ダイアログ題帯、ピッカー行、
  InfoBar 65/54、トースト、タブバー行、FAB
  メニュー項目、カラーピッカー高 −14 等）は
  実測で再ピン（test_tk17 / tk5 / tk9 / tk11 /
  tk13）。row_h2 は v2 系 70（行＋本文行）、
  WinUI 73 を維持。
- **型スケール**: M3 系は sp=px の姿勢を維持し、
  換算由来だった caption 下限のみ 14 → 13px
  （12sp × 1.12 = 13.4）。WinUI 系は錨 ×1.344 で
  font_px 20→19、caption 17→16、heading 26→24、
  title 29→27、display 40→38（§8.3 の表どおり）。
- **既定寸法の 854 化**: tk_core のルート幅
  フォールバック、tk_evdev の画面寸法既定、
  shell/src/wm.c の workarea/screen と wm.h の
  文書、greeter / braindesk2 の設計幅、osk.h の
  設計幅注記。撮影ハーネス（tests/shot_m20.c）も
  854 化し m22_ 4枚を再撮影（screenshots/）。
- **(a) 残差の最新化**（分類不動）:
  a-9 ListView 行: 表示 56 → 54px。参照は §5.3 で
  一次ソース 56dp = 62.7px に置換済みのため残差は
  −8.7px（行高はフロア由来、部品固有高さの採否は
  段階判断のまま）。a-11 WinUI 行: 54px = 参照
  40epx の新換算 53.8 と一致（残差 0）。
  a-10 BottomBar: M3 表示 75 → 73px（セル構成の
  実測）、参照 80dp = 89.6px、残差 −16.6px。
  WinUI は 78 → 77px。a-8 Badge: 高さ 19 不変、
  幅は caption 由来で 19 → 18px（参照 16dp =
  17.9 → 18、幅残差 0）。a-1〜a-7 の表示寸法は
  不変（FAB 65、箱 21、ドット 9、トラック
  61×37、バブル 56×51、チップ 37）。ただし
  a-2/a-3 のグリフ形状は下記 6(a) で是正。
  Slider（a-5 系）: WinUI つまみ Ø26 → 24px
  （18epx × 1.344 = 24.2、新換算に一致）。
- **WinUI dark 面ラダーの是正**（§21 の collapse）:
  白オーバーレイ α を調整 — selection 15→18、
  container 15→18、container_highest 40→42、
  subtle 40→46、card_bg 40→46、control_fill
  15→17、control_fill2 13→24、border 15→25。
  §9.1 と同じプローブ（bg へ合成し
  round-to-nearest で 565 化）で、意味を担う段が
  0x2104 / 0x2925 / 0x2945 / 0x2965 / 0x3186 /
  0x31a6 / 0x39a7 / 0x39c7 / 0x39e7 の別ステップ
  に分離することを確認し、test_tk5 に固定した。
  24bit 側の逸脱は α +2〜+11（最大 +6/255 ≈
  2.4%）で、参照の段順序は保存している。これは
  §21 で「(a) 同種の判断事項」として保留された件
  への M22 実施であり、分類判断とは別に、表示
  経路 RGB565 の実測に基づく実機適合として記録
  する。
- **6(a) チェックグリフ**: material3 の比率座標
  （相対 (0.15,0.55) / (0.45,0.85) / (0.85,0.25)）
  と当方グリフを照合し、長い腕の上端が左に寄って
  いた不一致を是正（パスを比率座標どおりに再定義、
  21px 箱・28px 箱の両形態）。プローブ再測定で
  腕・頂点の画素を test_tk17 に固定。
- **6(b) メニュー項目高**: 当方は control_h =
  54px。参照の従来型項目高 48dp（material3
  Menu.kt の MenuListItemContainerHeight）の
  新換算 53.8 → 54px と一致。WinUI テンプレート
  に固定項目高は無く（Padding 8+9epx + テキスト
  行）、共有 control_h 行は構造的差異として記録。
- **6(c) WinUI スライダードット状態スケール**:
  実装した。基準ドット 12epx × 1.344 = 16.1px に
  テンプレートの状態倍率（rest 0.86 / hover
  1.167 / pressed 0.71、M21 §5.1 抽出）を適用し
  14 / 19 / 11px。test_tk17 に状態別の実測ピン。
- **7. Switch 状態可変つまみ径**: 実装した。
  M3 形態: 未選択 18px / 選択 27px / 押下 31px
  （16 / 24 / 28dp × 1.12）。押下径のため
  Switch にポインタ押下中の pressed 処理を追加
  （従来は UP で切替のみ）。スライドアニメーション
  と複合する径は knob_pos で補間。WinUI 形態は
  参照どおり固定 22px のまま。test_tk17 に
  3状態の実測ピン。

## 23. M23 記録 — (a) 認定と BottomBar 選択表示の是正（2026-11-21、詳細は REPORT-toolkit-m23）

### 23.1 (a) 11件の認定

- ユーザーは 2026-11-21、提示した (a) 残差一覧に対し
  「気になるのは下のバーの選択表示だけ」と回答した。
  すなわち (a-1)〜(a-11) の寸法残差は **#29 BottomBar の
  選択表示の見た目を除き、全て現状で認定**された。
  (a-10) の高さ 73px（参照 89.6px に対する実機寸法帯の
  適合）自体は認定範囲に含まれる — 指摘は高さではなく
  選択表示の形である。
- この認定に合わせ、(a) 11件に対応する A セル 11件を
  ◐ → ● に変更した（Android #3/#6/#7/#8/#9/#10/#16/
  #17/#19/#29、WinUI #19）。集計は §2.3（● 422・93.0%）。
  #29 の A セルは下記 23.2 の是正完了をもって認定に
  合流させた（是正前は認定済みにしていない）。

### 23.2 #29 BottomBar 選択表示の是正

- **ユーザーの指摘**: バー本体は角の丸い四角なのに、
  選択時の見た目が区間幅いっぱいの細長い楕円で、
  アイコンとラベルの両方を包んでおり、本物の Android
  と感じが違う。
- **原因**: インジケーター形式の既定が mode 0（セル全面の
  トーナル面）のままで、参照形である mode 1（ピル）を
  生産側のどこも選んでいなかった（呼出元はテストのみ）。
  ライブラリの撮影（m22_fit）でも全面楕円になっていた。
- **参照の裏取り**: androidx material3 の NavigationBar
  実装（M21 取得ソース）では、ピルはアイコンのみを包み
  アイコン中心に置かれ、ラベルはピルの外（下）に置かれる。
  寸法はベースライン M3 仕様図で 64×32dp（アイコン 24dp
  + 横 20dp / 縦 4dp の余白、角は CornerFull）。現行
  androidx 開発ブランチの実測コードは M3 Expressive の
  56dp 幅に移行途上だが、本バーの実装前提はベースライン
  形（Tall コンテナ）であり、64dp を正とした（56dp は
  将来の移行候補として記録に留める）。
- **是正**: (1) 既定形式を mode 1（ナビゲーション
  インジケーター）に変更。mode 0 は明示選択の旧形として
  残置。(2) ピル寸法を固定 64×36px からアイコン由来に
  変更（nav_pill_rect: 幅 = アイコン幅 + 45px、 高さ =
  アイコン高 + 9px、上端はアイコンの 4px 上、半径 =
  高さ/2。参照 24dp アイコン = 27px では 72×36px =
  64×32dp 換算と一致）。バー描画とボタン描画の両経路に
  適用（レール/ドロワーのピルも同形状に揃う）。
- **検証**: test_tk6 に既定形のピンと形状ピン（24px
  アイコンでピル 69×33px、ピル下端とラベルの間は
  選択色でないこと）を追加、test_tk5 は旧形を明示選択に
  変更。make check 3,712件・0 failed。854 で再撮影
  （screenshots/m23_fit_android.jpg / m23_fit_winui.jpg、
  before は m22_fit_*）し、ピルがアイコンのみを包み
  ラベルが外に出ていることを目視確認した。
- **バー本体の扱い**: ライブラリの BottomBar は全面バー
  （浮動ではない）で、参照の NavigationBar も全面が
  標準形 — この点は一致しており変更なし。撮影ハーネスは
  バーを素の背景に置くため浮いて見えることがあるが、
  実シェルのパネルはシェル側の別実装（shell/）であり、
  本是正の対象外 — 同じ参照形状への追随はシェルラインへ
  申し送る。
- **残る微差**: ピル下端からラベルまでの間隔は共有の
  アイコンセル配置由来で 3px（参照 4dp = 4.5px）。
  デスクトップアイコンと共有する配置のため本弾では
  変更していない。

### 23.3 ライブラリ記録済み残件の解消（M23 同時実施）

- DESIGN-apps-envfit.md §3 を新前提（854×480・179.2ppi・
  1dp = 1.12px・フロア 54px）で再導出し、旧値との差分表を
  追加した。
- tk_font_load_bundled の探索順を是正: 環境変数 →
  実行ファイル相対 → ライブラリ相対（dladdr）→
  インストール先 → cwd 相対は最後の保険。test_font に
  cwd 非依存のピンを追加した。
- libbrainui2.so のシンボル可視性をバージョンマップ
  （libbrainui2.map）で制御: 動的エクスポート 801 →
  公開 606件（公開ヘッダ宣言 601 + テーマデータ 5）と
  完全一致。内部 195件（stb 113 + 内部関数 82）は
  非公開化。利用側は全て静的リンクのため無影響、
  sdk-check（公開ヘッダ + .so のみ）も通過。
- ライブラリ系ドキュメントとコメントの旧前提
  （800×480 / 187ppi）記述を一掃した（歴史的記録は
  その旨を明記して保持）。詳細は REPORT-toolkit-m23。
