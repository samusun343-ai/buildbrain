# REPORT-apps-dual-mode-notes-tasks — bd-notes / bd-tasks 両モード対応

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


範囲: `apps/notes/`、`apps/tasks/` のみ（+ アイコン素材追加分は §4）。
`shell/`、ライブラリ本体（`../include`・`../src`）、`apps/settings/`、
`apps/files/`、`screenshots/` は編集しない。repo へ commit / push なし。

共通基盤（実証済み、そのまま使用）:
- モード読取り: main() の `tk_theme_set_default(&tk_theme_v2)` を
  `#include "bdappmode.h"` + `bd_app_theme()` に置換（brainreg 正本キー
  `ui/mode` を libbdshell の `bd_theme_from_reg()` が読む）。
  ※ bd-notes の Makefile ターゲットは現行 `$(SHELLIB)` をリンクして
  いないため、bd-notes / test_notes に `$(SHELLIB)` を追加する
  （apps/Makefile の自担当ターゲットのみ変更）。
- コンストラクタは reg を読まず、`tk_theme_default()->style == 2` で
  winui 判定して app 構造体に保持（テストがテーマを直接設定する契約を維持）。
- アイコン: `#include "bdicon.h"`、`bd_icon_mode(winui, material名,
  fluent名, box, tintRGB)`。フォントグリフのラスタライズ
  （glyph_icon）とコード描画（shape_icon_*）は全廃。

## 0. ベースライン（着手前、2026-11-21）

| テスト | 結果 |
|---|---|
| test_notes（dict=brainux2.bidf, BISC=brainux2.bisc） | 77 PASS, 0 FAIL |
| test_tasks | 89 PASS, 0 FAIL |
| test_tasks_ui | 32 PASS, 0 FAIL |

ビルド: `make bd-notes bd-tasks test_notes test_tasks test_tasks_ui` 正常。

## 1. 途中経過

- 2026-11-21: 着手。ベースライン記録（§0）。素材確認:
  material = search / folder / more_vert / arrow_back / push_pin /
  attach_file / add / check_circle / radio_button_unchecked / delete /
  note_add あり。fluent = search / folder / more_vertical / pin /
  attach / add / delete / checkmark あり。fluent の円形2点
  （circle / checkmark_circle）が不足 → §4 の方式で追加中。
- 2026-11-21: fluent circle / checkmark_circle を追加し
  build_icons.py を再実行（186点レンダリング、§4）。
- 2026-11-21: notes 実装完了。test_notes 77 PASS / 0 FAIL
  （ベースライン一致、回帰ゼロ）。WinUI は一時ハーネスで別途実証:
  winui=1、書式バーがエディタ上部（bar y=74..128 < editor y=132）、
  ノート切替がペイン内で完結。

## 2. notes の設計対応

- main(): `bd_app_theme()` に置換。Makefile の bd-notes /
  test_notes に `$(SHELLIB)` を追加（bd_theme_from_reg のリンク）。
- コンストラクタ: `tk_theme_default()->style == 2` を
  `app->winui` に保持。新規アクセサ `notes_app_winui()`。
- グリフ全廃: `glyph_icon`（フォントラスタライズ）、
  `shape_icon_search` / `shape_icon_dots`（コード描画）を削除。
  全アイコンを `bd_icon_mode()` 経由に:
  検索=search/search、フォルダ=folder/folder、
  その他=more_vert/more_vertical、戻る=arrow_back/arrow_left、
  新規=add/add、ピン留め=push_pin/pin、添付=attach_file/attach。
- ピン留め表示: タイトルの「★ 」接頭辞を廃し、ノート一覧の
  行アイコン（push_pin/pin、accent 色）に変更。
- Android モード: 従来の3画面ドリルダウンを維持。一覧ヘッダに
  more_vert オーバーフロー（新規ノート/フォルダ、実アイコン付き
  メニュー）を追加。新規作成は下部 Filled ボタンのまま。
- WinUI モード: 2ペインに再構成。左ペイン=タイトル+件数、
  フォルダ切替コンボボックス（すべてのノート/各フォルダ/最近削除、
  フォルダ画面と同じ写像）、検索ボックス、一覧、新規ボタン。
  右ペイン=フォルダ名ヘッダ（戻るなし、検索/その他アイコン）、
  検索・置換バー、**書式バーをエディタ上部**に配置、エディタ、
  ステータス、OSK。ノート選択は画面遷移せずその場で読込み。
  show_screen は WinUI では可視切替を行わない。
- 書式トグル B/I/U 等の文字は編集コマンドの慣用表記として維持。

## 3. tasks の設計対応

- main(): `bd_app_theme()` に置換。Makefile の bd-tasks /
  test_tasks_ui に `$(SHELLIB)` を追加。
- コンストラクタ: `tk_theme_default()->style == 2` を
  `app->winui` に保持。新規アクセサ `tasks_app_winui()`。
- 完了表示: 行タイトルの「[ ]」「[x]」文字列を廃し、一覧の
  行アイコンに実アイコン化（未完了=radio_button_unchecked/circle、
  完了=check_circle/checkmark_circle、accent 色）。
  優先度 !/!!/!!! と [旗] は従来の表記として維持。
- ボタンの実アイコン化: 追加=add/add、削除=delete/delete、
  完了/未完了=check_circle/checkmark_circle、
  リスト削除（両モード）=delete/delete。
- Android モード（Google Tasks 式）: 単一カラムに再構成。
  サイドバーは畳み、リスト切替は上部のコンボボックス
  （スマートビュー5件＋ユーザーリスト、サイドバーと同じ写像、
  プログラム同期は view_sync でガード）。クイック追加行は
  画面下部（詳細・リスト管理の下）に配置し quick_field を活用。
- WinUI モード（Microsoft To Do 式）: 左にリスト一覧ペイン
  （従来サイドバー＋色スウォッチ＋リスト管理）、右に
  クイック追加＋タスク一覧＋詳細、という形に整えた。
- リスト色スウォッチ（draw_color_discs）は色見本の背景形状で
  アイコンではないため維持（設定アプリの disc と同じ扱い）。

## 4. 追加アイコン

fetch_icons.py の方式で icons.tsv に2行追加し取得、
build_icons.py を再実行（全186点を 96px マスターに再レンダリング）:

| set | name | upstream | source path | license |
|---|---|---|---|---|
| fluent | circle | microsoft/fluentui-system-icons | assets/Circle/SVG/ic_fluent_circle_24_regular.svg | MIT |
| fluent | checkmark_circle | microsoft/fluentui-system-icons | assets/CheckmarkCircle/SVG/ic_fluent_checkmark_circle_24_regular.svg | MIT |

MANIFEST.md にも同2行を追記済み。用途: tasks の未完了/完了の
円アイコンの Fluent 側（Material 側は既存の
radio_button_unchecked / check_circle）。

## 5. 検証（2026-11-21、最終ビルドで実施）

| 項目 | 結果 |
|---|---|
| test_notes | 77 PASS, 0 FAIL（ベースライン一致） |
| test_tasks | 89 PASS, 0 FAIL（同上） |
| test_tasks_ui | 32 PASS, 0 FAIL（同上、Android 分岐で実クリック追加を含む） |
| bd-notes / bd-tasks ヘッドレス起動 | UI built 表示、正常終了 |
| notes WinUI ハーネス（一時） | winui=1、書式バーがエディタ上部、ノート切替がペイン内で完結 → OK |
| tasks WinUI ハーネス（一時） | winui=1、選択→完了トグルがモデル反映 → OK |
| tasks Android ハーネス（一時） | winui=0、quick_add が行に反映 → OK |
| グリフ残存 | notes/tasks ともに glyph_icon / shape_icon 関数は無し（grep 確認） |

- ビルド警告: bdicon.h の未使用ヘルパ警告（他ラインと共通の
  ヘッダ提供関数）と、tasks_app_save_detail の既存
  format-truncation 警告のみ。新規警告なし。
- 編集範囲: notes/bd_notes.c・.h、tasks/bd_tasks.c・.h、
  apps/Makefile（自担当4ターゲットの SHELLIB 追加のみ）、
  assets/icons/{icons.tsv,MANIFEST.md,src/fluent/*.svg,
  fluent/*.png}（追加2点＋全点再レンダリングで既存 PNG も再生成）。
  shell/、ライブラリ本体、settings/、files/、screenshots/ は未編集。
  commit / push なし。
