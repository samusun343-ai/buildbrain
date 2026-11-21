# REPORT-apps-dual-mode-rec-pkg-admin — bd-recorder / bd-pkg / bd-admin の両モード対応

作成: 2026-11-21。範囲: `apps/recorder/`、`apps/pkg/`、`apps/admin/` の
UI（`bd_recorder.c` / `bd_pkg.c` / `bd_admin.c` と各ヘッダの注記）、
`apps/Makefile` の当該3ターゲットのリンク行、アイコン追加分
（`assets/icons/`）。`shell/`、ライブラリ本体（`../include`・`../src`）、
`apps/settings/`、`apps/files/`、`screenshots/` は編集しない。
repo へ commit / push なし。

共通基盤（実証済み、そのまま使用）:
- モード読取り: 各 `main()` の `tk_theme_set_default(&tk_theme_v2);` を
  `#include "bdappmode.h"` ＋ `bd_app_theme();` に置換
  （brainreg 正本キー `ui/mode`、読取りは `bd_theme_from_reg()`）。
  コンストラクタは reg を読まず `tk_theme_default()->style == 2` で
  winui 判定して `winui` フラグに保持（テスト契約を壊さない）。
- アイコン: `#include "bdicon.h"`、
  `bd_icon_mode(winui, "material名", "fluent名", box, tintRGB)`。
  フォントグリフ流用・コード描画グリフは全廃。
- リンク是正: bd-recorder / bd-pkg / bd-admin の製品バイナリは
  従来 libbdshell.a をリンクしておらず、`bd_app_theme()` が呼ぶ
  `bd_theme_from_reg()` が未解決になるため、`apps/Makefile` の
  当該3アプリ＋各 UI テストのリンク行へ `$(SHELLIB)` を追加した
  （settings 等の既対応アプリと同じ形）。

## 0. ベースライン（着手前、2026-11-21 実測）

対象3アプリの現行テストを再ビルドして実行:

| テスト | 結果 |
|---|---|
| test_recorder | 61 passed, 0 failed |
| test_recorder_ui | 15 passed, 0 failed |
| test_pkg | 45 passed, 0 failed |
| test_pkg_ui | 13 passed, 0 failed |
| test_admin | 25 passed, 0 failed |
| test_admin_ui | 13 passed, 0 failed |
| 合計 | 172 passed, 0 failed |

## 1. アイコン追加（fetch_icons.py 方式）

不足分として Fluent 2件を上流 microsoft/fluentui-system-icons から
追加（`icons.tsv` へ行追加 → `fetch_icons.py` → `build_icons.py` で
96px PNG 化 → `MANIFEST.md` へ行追加）:

| set | name | 用途 |
|---|---|---|
| fluent | pulse | admin プロセスタブ（material monitoring の対） |
| fluent | gauge | admin メモリタブ（material memory の対。Fluent に memory アイコンは上流に存在しないため性能系の gauge を採用） |

その他の使用アイコンは既存素材で充足:
fiber_manual_record/record、play_arrow/play、pause、stop、delete、
restore_from_trash/arrow_undo、delete_forever/delete、star、
drive_file_rename_outline/rename、content_cut/cut、mic、
download/arrow_download、refresh/arrow_clockwise、apps、
inventory_2/library、download_done/arrow_download（pkg 更新タブ）、
close/dismiss、storage、article/document_text、search は不使用。

## 2. recorder（bd-recorder）

- モード読取り: `main()` を `bd_app_theme()` へ置換。コンストラクタは
  `bd_app_winui(tk_theme_default())`（style==2）で `app->winui` を保持。
- 構造分岐:
  - Android（Google レコーダー式）: 単一カラム — タブ（録音一覧/
    最近削除）＋検索、一覧（flex）、詳細（波形・再生・速度・名前変更・
    お気に入り・削除系・トリム）、レベル/状態行、最下部中央の大きな
    録音ボタン（40px アイコン、CELL 配置、FILLED、最小 84×84、
    左右に一時停止/停止）。
  - WinUI（Windows ボイスレコーダー式）: 左ペイン（タブ＋検索＋
    一覧）、右に詳細/波形/再生、下部に録音コントロールバー
    （録音/一時停止/停止＋レベル＋状態）。
- アイコン実アイコン化（bd_icon_mode、24px、テーマ文字色ティント、
  録音のみ danger 色）: 録音 fiber_manual_record/record（filled）、
  再生 play_arrow/play、一時停止 pause、停止 stop、削除 delete、
  復元 restore_from_trash/arrow_undo、完全削除 delete_forever/delete、
  お気に入り star（琥珀ティント）、名前変更
  drive_file_rename_outline/rename、トリム content_cut/cut、
  タブ mic / delete。アイコン付きボタンは TONAL（録音は FILLED）で
  面を保持し、アイコン＋ラベルの ROW 配置（Android 録音のみ CELL）。
- グリフ全廃: ボタンの ▶/●/⏸/■/★、状態表示の ●/⏸、詳細の ★ を
  すべて除去し、状態は文言（録音中/一時停止中/お気に入り）で表示。
  -15秒/+15秒 は数値ラベルのテキストボタンとして維持（アイコン
  代用に当たらない通常の文言）。
- テスト: test_recorder 61/0、test_recorder_ui 15/0（回帰ゼロ）。
  追加で使い捨てハーネス（/tmp/check_winui_rec.c）により
  tk_theme_winui と tk_theme_v2 の両方で生成・録音・停止・再生・
  EOF・アイコン有無（tk_button_has_icon）を検証し全 PASS。

## 3. pkg（bd-pkg）

- モード読取り: `main()` を `bd_app_theme()` へ置換。コンストラクタは
  style==2 で `app->winui` を保持。
- 構造分岐:
  - Android（Play ストアのアプリ管理式）: 単一カラム — 上部タブ
    （インストール済み/カタログ/更新＋更新件数＋再読込）、検索、
    一覧（flex）、詳細、操作行、ログ。タブは現行の3ビュー
    （インストール済み/カタログ/更新）をそのまま上部タブとした。
  - WinUI（Microsoft Store / 設定のアプリ一覧式）: 左ナビペイン
    （3ビューを縦積み、最小幅 208px、更新件数ラベル）、右メイン
    カラムにコマンド行（検索＋再読込）、一覧＋詳細（左右）、
    操作行、ログ。
- アイコン実アイコン化（アイコン＋ラベルの形、ROW 配置）:
  インストール download/arrow_download（FILLED）、全て更新
  refresh/arrow_clockwise、削除 delete/delete、再読込
  refresh/arrow_clockwise、タブは apps/apps、
  inventory_2/library、download_done/arrow_download。
- テスト: test_pkg 45/0、test_pkg_ui 13/0（回帰ゼロ）。
  追加で使い捨てハーネス（/tmp/check_winui_pkg.c）により
  tk_theme_winui / tk_theme_v2 の両方でカタログ表示・
  インストール・ドライラン削除・実削除とアイコン有無を検証し
  全 PASS（更新適用フローは test_pkg_ui が既定テーマで担保）。

## 4. admin（bd-admin）

- モード読取り: `main()` を `bd_app_theme()` へ置換。コンストラクタは
  style==2 で `app->winui` を保持。
- 構造分岐（現行タブ構造＝プロセス/ストレージ/メモリ/ログは両モード
  で同一ウィジェットとして維持）:
  - Android（端末情報/開発者向け画面式）: 単一カラム、上部タブ
    切替を Material 形（アイコン＋ラベル、TONAL）に。
  - WinUI（タスクマネージャー式）: 左ナビペイン（タブを縦積み、
    最小幅 208px、その下に更新）＋右コンテンツ。プロセスページは
    並び替え行（PID/名前/RSS/CPU）が一覧直上の列ヘッダとして
    整列する形を両モードで維持。
- アイコン実アイコン化: 更新 refresh/arrow_clockwise、終了・
  強制終了 close/dismiss、タブは monitoring/pulse（プロセス）、
  storage/storage、memory/gauge（メモリ。§1 の追加アイコン）、
  article/document_text（ログ）、ログ読み込みも
  article/document_text。並び替えボタンは列ヘッダの文言として
  テキストのまま（アイコン代用ではない）。
- テスト: test_admin 25/0、test_admin_ui 13/0（回帰ゼロ）。
  追加で使い捨てハーネス（/tmp/check_winui_admin.c）により
  tk_theme_winui / tk_theme_v2 の両方でプロセス終了（実子
  プロセスへの SIGTERM）、並び替え、全タブ遷移、ログ読込/
  フィルタとアイコン有無を検証し全 PASS。

## 5. 完了時テスト（2026-11-21 実測）

| テスト | 着手前 | 完了時 |
|---|---|---|
| test_recorder | 61/0 | 61 passed, 0 failed |
| test_recorder_ui | 15/0 | 15 passed, 0 failed |
| test_pkg | 45/0 | 45 passed, 0 failed |
| test_pkg_ui | 13/0 | 13 passed, 0 failed |
| test_admin | 25/0 | 25 passed, 0 failed |
| test_admin_ui | 13/0 | 13 passed, 0 failed |
| 合計 | 172/0 | 172 passed, 0 failed（回帰ゼロ） |

- `make all`（全アプリ＋全テストのビルド）成功、`make check`
  （全アプリのテスト一式）も全通過（他アプリへの影響なし）。
- 製品バイナリ bd-recorder / bd-pkg / bd-admin は起動スモーク
  （生成→破棄）で終了コード 0。`bd_app_theme()` 経由の
  libbdshell リンクも解決済み（§0 のリンク是正）。
- グリフ残存チェック: 対象3ファイルに ● ▶ ⏸ ■ ★ 等の
  フォントグリフ流用は残っていないことを grep で確認。
  `tk_theme_set_default` の直書きも残っていない。
- 変更ファイル: recorder/bd_recorder.c・.h、pkg/bd_pkg.c・.h、
  admin/bd_admin.c・.h、apps/Makefile（3アプリのリンク行のみ）、
  assets/icons/{icons.tsv, MANIFEST.md, src/fluent/{pulse,gauge}.svg,
  fluent/{pulse,gauge}.png}（追加分。build_icons.py は全アイコンを
  同一ソースから再描画するが内容は同一素材の再生成）。
  shell/、ライブラリ本体、settings/、files/、screenshots/ は
  未編集。commit / push なし。
