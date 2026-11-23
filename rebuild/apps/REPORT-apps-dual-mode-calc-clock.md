# REPORT-apps-dual-mode-calc-clock — bd-calc / bd-clock 両モード対応

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


作成: 2026-11-21。範囲: `apps/calc/`、`apps/clock/` のみ
（＋本レポート）。`shell/`、ライブラリ本体（`../include`・
`../src`）、`apps/settings/`、`apps/files/`、`screenshots/` は
編集しない。repo へ commit / push なし。

共通基盤（実証済み、そのまま使用）:
- モード読取り: main() の `tk_theme_set_default(&tk_theme_v2);`
  を `#include "bdappmode.h"` ＋ `bd_app_theme();` に置換。
- コンストラクタは reg を読まず `tk_theme_default()->style == 2`
  で winui 判定して保持（テストのテーマ直接設定の契約を維持）。
- アイコン: `#include "bdicon.h"`、`bd_icon_mode(winui,
  "material名", "fluent名", box, tintRGB)`。素材は
  `../assets/icons/`（MANIFEST.md / icons.tsv）。

## 0. ベースライン（着手前、2026-11-21）

自担当ターゲットのみビルドし、現行テストを実行:

| テスト | 結果 |
|---|---|
| test_calc | 136 passed, 0 failed |
| test_calc_ui | 92 passed, 0 failed |
| test_clock | 71 PASS, 0 FAIL |
| test_clock_ui | 43 PASS, 0 FAIL |

注: `/tmp`（512MB tmpfs）が他ラインの作業で満杯のため、
コンパイラ一時ファイル用に `TMPDIR=~/workspace/tmp-calc-clock`
を使ってビルドした（ディスク側は 117GB 空き）。

## 1. 棚卸し（着手前）

### bd-calc
- main() に `tk_theme_set_default(&tk_theme_v2);` ハードコード。
- 画面: 表示部（式フィールド＋結果）＋ tk_tabs（標準/関数/
  プログラマ/変換）＋下部に履歴キャプション＋「履歴」テキスト
  ボタン、履歴全件はダイアログ（シート）。
- backspace 専用キーは無い（AC/C が初回押下でトークン削除を
  兼ねる）。→ 両モードとも backspace 実アイコンの専用キーを
  表示部横に追加し、トークン削除に割り当てる（AC/C は従来
  どおり残し、テスト契約を壊さない）。
- フォントグリフ/コード描画グリフ: 該当箇所は履歴ボタンと
  変換ページの「⇄」スワップボタン（文字グリフ流用）。
  数字・演算子キーの文字は電卓の慣用表記なので維持（タスク
  指示どおり、アイコンではない）。

### bd-clock
- main() に `tk_theme_set_default(&tk_theme_v2);` ハードコード。
- 画面: 現在時刻＋バナー＋ tk_tabs（世界時計/アラーム/
  ストップウォッチ/タイマー）。タブは上部テキストタブのみで
  アイコンなし。
- 追加ボタンはテキスト「追加」（世界時計の都市追加）。
  アラーム追加はエディタ行の「新規」テキストボタン。
- 公開タブ番号（0=世界, 1=アラーム, 2=ストップウォッチ,
  3=タイマー）は現行テストの契約なので両モードとも不変。
  ナビゲーションの「並び」だけモード別にする（§3）。

## 2. bd-calc 実施記録

- main(): `tk_theme_set_default(&tk_theme_v2);` →
  `bd_app_theme();`（bdappmode.h）。リンクに libbdshell が
  必要になったため apps/Makefile の bd-calc ルールに
  `$(SHELLIB)` を追加（bd-clock ルールは既にリンク済み。
  test_calc_ui は main を含まないので変更なし）。
- コンストラクタ: `app->winui = tk_theme_default()->style == 2`
  を一度だけ保持。reg は読まない。
- ページ切替を自前化: tk_tabs を廃し、4ページ（標準/関数/
  プログラマ/変換、公開番号は従来どおり）を縦ボックスに
  重ねて `calc_show_page()` が表示ページだけ visible にする
  単一経路にした。`calc_app_tab/set_tab` はこの経路を使う。
  - Android: 表示部の下にモード選択の tab strip
    （tk_tabbar、4モード同順）。履歴は従来どおり最新1件の
    キャプション＋シート（ダイアログ）だが、開くボタンは
    テキスト「履歴」→ history 実アイコン
    （material history / fluent history）に変更。
  - WinUI: 左上にモードメニュー（≡アイコンボタン:
    material menu / fluent navigation）＋現在モード名の
    タイトル（TK_TEXT_TITLE）。メニューは tk_menu で
    4モード。右側に履歴/メモリペイン常設: 幅は
    `w*240/854`（854幅で240px、下限160・上限 w/2）、
    「履歴」見出し＋履歴リスト（シートと同一ウィジェット、
    activate で式へロード）＋メモリ表示ラベル
    （mc/m+/m- のたびに同期）。履歴リストはモードにより
    シート内（Android）かペイン内（WinUI）かのどちらか
    一方にだけ親を持つ。
- backspace: 両モードで表示部右端に実アイコンの専用キー
  （material backspace / fluent backspace、icon-only）を
  追加。式ページではトークン削除、プログラマページでは
  末尾1文字削除。AC/C は従来どおり（テスト契約維持）。
- 変換ページの「⇄」文字ボタン → 実アイコン
  （material swap_horiz / fluent arrow_swap、§4）。
- 数字・演算子キー（÷ × − 等）は電卓の慣用表記として維持。
  2nd/DEG(Rad チェックボックス)/メモリキーは両モード共通の
  ページ内にあるため機能低下なし。
- アイコン tint はテーマの text 色。アイコンが読めない
  場合は従来どおりテキストボタンにフォールバック
  （bdicon の NULL 許容契約）。

## 3. bd-clock 実施記録

- main(): `tk_theme_set_default(&tk_theme_v2);` →
  `bd_app_theme();`（bdappmode.h）。bd-clock の Makefile
  ルールは元々 `$(SHELLIB)` をリンク済みで変更なし。
- コンストラクタ: `app->winui = tk_theme_default()->style == 2`
  を一度だけ保持。reg は読まない。
- ページ切替を自前化: tk_tabs を廃し、4ページを
  `clock_show_page()` が表示切替する単一経路にした。
  公開番号は従来どおり 0=世界時計, 1=アラーム,
  2=ストップウォッチ, 3=タイマー（現行テストの契約）。
- ナビゲーションの並びは両モードとも参照アプリ順
  （アラーム/時計/タイマー/ストップウォッチ）とし、
  位置→ページの対応表（位置 0,1,2,3 → ページ 1,0,3,2）
  で公開番号へ写像する。
  - Android（Google 時計式）: 下部タブバー
    （tk_bottombar、アイコン＋ラベル、選択は M3 pill
    インジケータ）: material alarm / schedule / timer /
    hourglass_top（タスク指定の対応順どおり:
    アラーム=alarm、時計=schedule、タイマー=timer、
    ストップウォッチ=hourglass_top）。
  - WinUI（Windows 時計式）: 左ナビペイン
    （tk_navrail、アイコン＋ラベルの縦並び）＋右コンテンツ:
    fluent alarm / clock / timer / timer。
    **Fluent に stopwatch アイコンは無い**ため、
    ストップウォッチ行は timer で代替（タスク指示どおり
    ここに記録）。
- 追加ボタンは material add / fluent add の実アイコン＋
  ラベル（ROW 配置）: 世界時計の都市「追加」、アラームの
  「新規」、タイマーの「作成」。
- 実タップ検証で発見した不具合を修正: Android 分岐で
  content カラムに flex が無く、ページ切替後に下部バーが
  画面外（y=495 > 480）へ押し出されていた →
  content に flex 1 を付与（WinUI 分岐は元々あり）。
  修正後は両モードとも実タップで全宛先へ遷移を確認。

## 4. アイコン追加記録

変換スワップ用に2点を fetch_icons.py の方式で追加
（`../assets/icons/`、タスク許可分）:
- material swap_horiz: icons.tsv に行追加 → fetch_icons.py
  で SVG 取得（fetched ok: 199, failed: 0）。
- fluent arrow_swap（ArrowSwap / arrow_swap / regular）:
  同上。Fluent 上流に実在を確認。
- 描画は build_icons.py と同一方式（tools/svg2png で
  384px レンダリング → PIL Lanczos で 96px 化）だが、
  全件再レンダリングを避け新規2点のみ処理。
  MANIFEST.md の表にも2行追加。
- それ以外に必要なアイコン（history / backspace / menu /
  navigation / alarm / schedule / timer / hourglass_top /
  add / clock）は全て既存素材にあり。

## 5. 完了時検証（2026-11-21）

ビルド: 自担当ターゲットのみ
（test_calc / test_calc_ui / test_clock / test_clock_ui /
bd-calc / bd-clock）。警告は bdicon.h の未使用 static 関数
（bd_icon_color 等、共通ヘッダ側の既存パターン）のみ。

現行テスト（回帰ゼロ、ベースラインと同一件数）:

| テスト | ベースライン | 完了時 |
|---|---|---|
| test_calc | 136 passed, 0 failed | 136 passed, 0 failed |
| test_calc_ui | 92 passed, 0 failed | 92 passed, 0 failed |
| test_clock | 71 PASS, 0 FAIL | 71 PASS, 0 FAIL |
| test_clock_ui | 43 PASS, 0 FAIL | 43 PASS, 0 FAIL |

モード別ハーネス（作業領域 `~/workspace/tmp-calc-clock/`、
repo 外。テーマを構築前に直接設定するテスト契約で実施、
854×480）:

| ハーネス | 内容 | 結果 |
|---|---|---|
| winui_calc | WinUI: 2+3=5、履歴ペインのリストから式ロード、関数ページの mc/m+/2nd、プログラマ、変換、描画 | 26 passed, 0 failed |
| android_calc (tk_theme_v2) | 同上（Android 形） | 26 passed, 0 failed |
| winui_clock | WinUI: アラーム作成→発火→解除、ストップウォッチ、タイマー作成→発火、描画 | 22 passed, 0 failed |
| android_clock (tk_theme_v2) | 同上（Android 形） | 22 passed, 0 failed |
| navtap (Android) | 実ポインタイベント: ボトムバーのタップで時計の全宛先遷移、calc の tab strip 遷移、backspace アイコンキーでトークン削除、履歴アイコン | 10 passed, 0 failed |
| navtap (WinUI) | 実ポインタイベント: ナビレールのタップで時計の全宛先遷移、calc の ≡メニューボタン、backspace、履歴アイコン | 9 passed, 0 failed |

グリフ全廃の確認: `grep "⇄|⌫|≡|☰" calc/bd_calc.c
clock/bd_clock.c` → 該当なし（UI 文字列として残るのは
数字・演算子の慣用表記と通常のテキストラベルのみ）。

申し送り:
- bdicon.h を include すると未使用 static 関数の警告が
  出る（共通ヘッダ側の既存パターン、本ラインでは変更
  しない）。
- 撮影（screenshots/）は本ラインの編集禁止範囲のため
  未実施。描画確認は上記ハーネスの paint＋ピクセル変化
  チェックで代替した。
