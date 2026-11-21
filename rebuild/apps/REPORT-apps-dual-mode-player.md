# REPORT-apps-dual-mode-player — bd-player の両モード対応（Android / WinUI）

作成: 2026-11-21。範囲: `apps/player/bd_player.c`（+ `apps/Makefile`
の player ターゲットのリンク行）、`assets/icons/` へのアイコン追加。
`shell/`、ライブラリ本体（`../include`・`../src`）、`apps/settings/`、
`apps/files/`、`screenshots/` は編集しない。repo へ commit / push なし。

共通基盤（実証済み、そのまま使用）:
- モード読取り: `apps/common/bdappmode.h` の `bd_app_theme()`
  （brainreg 正本キー `ui/mode`、`shell/src/thememode.c` の
  `bd_theme_from_reg()` が唯一の読取り実装）。
- アイコン: `apps/common/bdicon.h` の `bd_icon_mode()` /
  `bd_icon_color()`（素材は `assets/icons/`、MANIFEST.md /
  icons.tsv）。フォントグリフ（glyph_icon）とコード描画
  （shape_icon_*）は全廃する。

## 1. ベースライン（着手前、2026-11-21）

現行テストを player ターゲットのみで実行し記録:

| テスト | 結果 |
|---|---|
| `test_player`（モデル、`player/tests/fixtures`） | 84 passed, 0 failed |
| `test_player_ui`（UI ドライブ、同 fixtures） | 40 passed, 0 failed |
| `bd-player` ビルド | 成功（既存バイナリ up-to-date） |

現行の構造（棚卸し）:
- 3画面ドリルダウン（ライブラリ / 再生中 / キュー、Samsung
  Music 式）。タブバー5セグメント（曲/アルバム/アーティスト/
  ジャンル/プレイリスト）、ミニプレイヤー、再生中トランス
  ポート、キューの上下/削除/クリア。
- アイコンは全て `glyph_icon()`（フォントグリフ: ▶ ⏸ ⏮ ⏭
  ↻ ⇄ ＋ → A × ↑ ↓）と `shape_icon_search/dots()`（コード
  描画）に依存。`main()` は `tk_theme_set_default(&tk_theme_v2)`
  固定で、モードキーを読んでいない。
- WinUI テーマ下では「同じ UI がテーマだけ変わる」状態で、
  構造分岐は無い（既存 UI テストの WinUI 節は生成+描画のみ）。

## 2. 設計

### 2.1 モード読取り
- `main()`: `tk_theme_set_default(&tk_theme_v2)` を
  `#include "bdappmode.h"` + `bd_app_theme()` に置換。
- コンストラクタ（`player_app_new`）は reg を読まず、
  `tk_theme_default()->style == 2` で winui を判定して
  `app->winui` に保持（テスト契約: テストがテーマを差し替えて
  から生成する従来の駆動がそのまま効く）。
- リンク: bd-player / test_player_ui は `bd_theme_from_reg()`
  （libbdshell.a）を引くため、`apps/Makefile` の player
  ターゲット2件に `$(SHELLIB)` を追加（他アプリと同形）。

### 2.2 Android モード（現行構造を Material 形で維持）
- 3画面ドリルダウン、タブ（アイコン付き）、ミニプレイヤー、
  再生中画面は現行どおり。全アイコンを Material Symbols の
  実アイコンへ: search / more_vert / arrow_back、タブは
  music_note / album / group / category / queue_music、
  トランスポートは shuffle / skip_previous / play_arrow・
  pause / skip_next / repeat（repeat_one は 1曲リピート時）、
  コンテキストバーは play_arrow / skip_next / add /
  playlist_add / arrow_forward / edit / delete、キュー操作は
  arrow_upward / arrow_downward / delete。
- 状態連動は維持: 再生/一時停止で play/pause 切替、シャッフル
  トグル、リピート off/all/one のラベル+面（fill）+アイコン
  （repeat / repeat_one）切替。

### 2.3 WinUI モード（Windows Media Player 式に作り替え）
- 単一画面: 左ナビペイン（tk_drawer 標準形、ライブラリ分類
  曲/アルバム/アーティスト/プレイリスト + 区切り + キュー +
  再生中。ジャンルは列挙に無く Android タブ側のみ）、右ペイン（見出し+検索/overflow、一覧、
  コンテキストバー、ステータス）、下部トランスポートバー
  常駐（アートワーク小、タイトル、進捗スクラブ+時刻、
  shuffle / previous / play・pause / next / repeat、音量）。
- 再生中は別画面ドリルにせず、右ペイン内に大きく表示
  （アートワーク+タイトル/アーティスト/アルバム+リピート
  状態）。キューも右ペイン内で一覧+操作バーを表示。
- アイコンは Fluent: play / pause（filled 変種）、next /
  previous（filled）、shuffle、repeat_all / repeat_1、
  music_note_2 / album / people / tag / list、search /
  more_vertical / arrow_left、speaker_2 等。
- ウィジェット再利用: 一覧・キュー・進捗・時刻・音量・
  タイトル等の状態同期対象は両モードで同一ウィジェットを
  配置先だけ変えて共有し、同期コード（sync_play_ui /
  sync_repeat_ui / sync_volume_ui / tick）を一本に保つ。
  ビュー切替は WinUI でも内部タブバー（非表示）を真実として
  駆動し、ナビ選択と双方向に同期する。

### 2.4 アイコン追加（fetch_icons.py 方式）
§3.1 に記録（9点追加）。

## 3. 実施記録

### 3.1 アイコン追加（2026-11-21）
既存セットに無い分類アイコン9点を fetch_icons.py 方式で追加
（icons.tsv へ行追加 → fetch（195件 ok / 0 failed）→
build_icons.py と同方式（384px AA → 96px Lanczos）で追加分
のみラスタライズ → MANIFEST.md へ出典行追加）。全点で
アルファのインク画素があることを確認済み:
- material: album / category / group / playlist_play
- fluent: album / people / tag / list / repeat_1
  （repeat_1 は上流 assets に無く Iconify ミラー経由、
  fetch_icons.py のフォールバック経路）

### 3.2 本体実装（2026-11-21）
- `main()`: `bd_app_theme()` へ置換。Makefile の bd-player /
  test_player_ui に `$(SHELLIB)` を追加（bd_theme_from_reg 用）。
- コンストラクタ: `app->winui = tk_theme_default()->style == 2`
  を保持。アイコンは全て `bd_icon_mode()` 経由の借用ポインタ
  （free しない）に切替、`glyph_icon` / `shape_icon_search` /
  `shape_icon_dots` を全廃。戻るボタンは arrow_back /
  arrow_left の実アイコン化。カバー無し時のプレースホルダは
  灰色地＋ music_note / music_note_2 アイコンの合成。
- Android: 従来構造のまま全アイコン実アイコン化（タブに
  tk_tabbar_set_item_icon、コンテキストバーは playlist_add
  等へ差替）。再生/一時停止・シャッフル・リピートの状態連動
  （face＋アイコン repeat/repeat_one 切替）は維持。
- WinUI: 上位を横分割（左ナビ全高＋右カラム）。ナビは
  tk_drawer 標準形で 曲/アルバム/アーティスト/プレイリスト
  ＋キュー＋再生中（分類は指示の列挙どおり。ジャンルは
  Android タブ側に残置）。右ペインは見出し＋一覧/キュー/
  再生中を切替（show_screen を win_show に写像）。下部
  トランスポートバー常駐（シーク行＝進捗＋時刻＋音量、
  操作行＝アート/タイトル/シャッフル/前/再生/次/リピート）。
  状態ウィジェットは両モード共有で同期経路は一本。
  ビュー切替は非表示タブバーを真実として drawer と双方向同期。
  WinUI のトランスポートタイトルは幅の都合でタイトルのみ
  （アーティストは再生中ペインと一覧行に表示）。

### 3.3 親の目視フィードバック対応（screenshots/v2_81_player_np_winui.jpg）
1. カバーはみ出し: WinUI 再生中ペインを余白16pxのパディング
   付き2カラムとし、カバーは固定サイズ（176px）・非 flex で
   ペイン内に完全収納。Android 再生中 body にも 12px
   パディングを追加（従来は左端ベタ付け）。
2. シーク視認性: 現行ライブラリ（M21）の WinUI レールは
   strong_fill の中灰で描かれることを実レンダで画素確認
   （§4）。音量スライダーと同じ tk_slider で、両テーマとも
   レールが見える。
3. 配置バランス: WinUI 再生中は「カバー（左、余白内）→
   タイトル/アーティスト/アルバム（同一左端で整列）」の
   2カラム＋中央揃えトランスポート行（左右同幅スペーサ、
   アイコンのみの 48/64px クラスタ）に再構成。シーク
   （時刻付き）と音量は常駐バーのシーク行に整列して置く。
   リピート/シャッフルの状態はボタン自体のトグル/face と
   repeat_1 アイコンで示し、WinUI では「リピート: なし」の
   浮いた状態文字行を置かない（repeat_label は非表示で
   更新のみ継続、Android 形式では従来どおり表示）。
   あわせてナビ全高化でドロワ項目のはみ出し、トランス
   ポートバーのタイトル重なり（§3.2 のタイトル短縮）を解消。

## 4. 検証

- テスト回帰（player ターゲットのみビルド）:
  `test_player` 84 passed / 0 failed、`test_player_ui`
  40 passed / 0 failed — ベースライン（§1）と一致、回帰ゼロ。
  `bd-player` 本体もヘッドレス起動確認（UI built, exit 0）。
- 簡易レンダ目視（/tmp の使い捨てハーネスで実アプリを描画、
  800x480 と実機寸法 854x480、両テーマ）:
  - WinUI 再生中: カバーの灰色域は y=123–298（176px）、
    画面左端からナビ＋余白分内側に完全収納（はみ出しなし）。
    タイトル/アーティスト/アルバムは同一左端、トランス
    ポートは中央揃えで全ボタンが右端内に収まる。
  - シークレール画素: WinUI は fill=(0,103,192) /
    rail=(134,134,134) 対 bg=(243,243,243) で明瞭。
    Android（v2）は rail=(230,224,233) の 16px トラックで
    音量スライダーと同じ描画。
  - WinUI ライブラリ/キュー画面も描画確認: ナビ全項目が
    画面内、キュー操作バーの実アイコン、トランスポート
    バー常駐を確認。
- 撮影の最終確認は親ライン（screenshots/）に委ねる。
