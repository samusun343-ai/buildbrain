# REPORT-apps-dual-mode — アプリ群の両モード対応

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


作成: 2026-11-21。範囲: `apps/`（files / notes / calc / player /
tasks / clock / recorder / pkg / admin / settings のアイコン）、
`browser-engine/netsurf-all-3.11/netsurf/frontends/brain/`、`osk/`。
`shell/` と ライブラリ本体（`../include`・`../src`）は編集しない。
repo へ commit / push なし。

前提: libbrainui2 は M22 で完成（854×480・約179ppi、タッチフロア
54px、型スケール、WinUI dark の 565 潰れ是正、make check 3,701件
全通過を親が再確認）。ユーザー指示の順序「GUI ライブラリ完成後」
を満たして着手する。

## 0. 途中経過

- 2026-11-21 完了: 全アプリ（files/notes/calc/player/tasks/clock/recorder/pkg/admin）＋ブラウザ＋OSK 確認＋設定（キー差し替えとアイコン置換）の両モード対応を実施。apps の make check は 20スイート 1,235件・0 failed（本ライン再実行）。両モード撮影（854×480）を実施し本ラインが目視確認した。不具合として見つけた3件（files のパンくず重なり、notes WinUI のペイン圧迫、player 再生中の親フィードバック3点）は全て修正・再撮影確認済み。残件は §7 の申し送りのみ（ライブラリ側 OSK 角丸、ブラウザのキー/実機未検証、アイコン大サイズマスター）。
- 進め方: 基盤（bdappmode/bdicon/素材パイプライン）を本ラインで作り実証 → 設定と files を本ラインで実施 → 残りを子ライン5本に委任し、各子ラインのレポート（apps/REPORT-apps-dual-mode-*.md、browser-engine/REPORT-browser-dual-mode.md、osk/REPORT-osk-dual-mode.md）を取り込んだ。

## 1. モード決定の正本（シェルラインで確定済み、本ラインは読む側）

`shell/REPORT-status-icons.md` §4 の契約が正本:

| 項目 | 値 |
|---|---|
| 名前空間 / キー | brainreg `ui` / `mode` |
| 値 | `android`（既定）/ `winui` |
| 補助キー | `ui/dark` = `true`/`false`（既定 false） |
| 反映 | 次回プロセス起動時（各プロセスは起動時に1回だけ読む） |
| 読取り実装 | `bd_theme_from_reg()`（shell/src/thememode.c、libbdshell.a に収録、全アプリがリンク済み） |

- アプリ側の読取りは本ラインで `apps/common/bdappmode.h` に
  一元化する（中身は `bd_theme_from_reg()` の呼出し＋
  `tk_theme_set_default()` ＋モード判定。キー文字列を持つのは
  thememode.c の1箇所だけ）。
- **整合の是正**: 設定アプリは暫定キー `shell/ui_mode` へ書いて
  いた（apps/settings/DESIGN-settings-rebuild.md §6 の隔離設計
  どおり）。正本が `ui/mode` に確定したため、本ラインで
  setmodel の書込み側も正本キーへ差し替える（隔離された1関数＋
  テストの追随）。これで「設定で切り替え → 全プロセスが追随」が
  初めて閉じる。

## 2. 現状棚卸し（判定済み）

全アプリの main() は `tk_theme_set_default(&tk_theme_v2)` をハードコードしており、コンストラクタはテーマを設定しない（撮影/テストハーネスが構築前にテーマを強制する方式）。したがって「モード追随」の実体は (a) main の読取り差し替え、(b) コンストラクタの style 分岐、の2点に集約される。reg 既定が android のため、既存テストはテーマ直接設定のままで回帰しないことを全ラインで確認した。

| アプリ | 変更前 | 判定 |
|---|---|---|
| files | ドリルダウン＋下部コンテキストバー、WinUI はテーマ色のみ | 色替え止まり → Explorer 形に再構成（§4） |
| notes | Samsung Notes 式ドリルダウン3画面、グリフアイコン | 色替え止まり → WinUI は2ペイン化（子ライン） |
| calc | 単一画面キーパッド | 履歴ペインの有無で作り分け（子ライン） |
| player | Samsung Music 式3画面、グリフ＋コード描画アイコン | 色替え止まり → WinUI は Media Player 形（子ライン） |
| tasks | サイドバー持ち単一画面 | Android は畳んで単一カラム / WinUI は To Do 形（子ライン） |
| clock | タブ持ち | Android は下部タブバー / WinUI は左ナビ（子ライン） |
| recorder | 一覧＋録音ボタン | WinUI は左一覧ペイン形（子ライン） |
| pkg | タブ＋一覧 | WinUI は左ナビ/コマンドバー形（子ライン） |
| admin | タブ切替 | WinUI はタスクマネージャー式左ナビ（子ライン） |
| browser | Material アイコンのツールバー固定 | モード読取り＋Edge 式バー（子ライン） |
| osk | tk_osk（ライブラリ内） | テーマ追随の確認のみ（子ライン） |
| settings | 作り直し済み（2形態あり） | モードキーを正本へ差し替え＋カテゴリアイコン実アイコン化（§4） |

アイコンの棚卸し: files/notes/player は共通して `glyph_icon()`（フォントグリフのラスタライズ）＋ `shape_icon_*()`（コード描画）でアイコンを作っていた = ユーザー指摘どおりの方式で、全廃して bdicon 経由の実アイコンに置換した。tasks/clock/recorder/pkg/admin はグリフアイコンのパターンは無くテキストボタン主体（該当箇所のみ実アイコン化）。ブラウザの icons.h は Material 公式 SVG 由来の埋め込み配列で実在セット由来だった（WinUI 用に Fluent を追加生成する方針で子ラインが実施）。

## 3. アイコン

### 3.1 パイプラインの実証（2026-11-21）

- ユーザー指摘: コード描画のラスターグリフと文字・記号流用は
  不可。確立したベクターセットから AA つきで描くこと。
- ホスト側ラスタライザとして librsvg（.so は導入済み、ヘッダ
  無しのため必要プロトタイプを自前宣言）＋ cairo の小ツールを
  実証: Material の back.svg を 48px で描画し、AA の効いた
  RGBA PNG が得られることを確認した。
- 運用形: ビルド時（ホスト）に SVG → PNG を必要サイズで
  生成し、`assets/icons/` に成果物 PNG と出典 manifest を置く。
  実機側は PNG を読むだけ（SVG パーサを実機に持ち込まない、
  RAM 128MB の恒常原則）。

（素材の選定・manifest・様式統一の記録はここに追記する）

## 4. アプリ別実施記録

### settings（本ライン実施）
- モードキーを正本へ差し替え: setmodel.c の書込先を暫定 `shell/ui_mode` から brainreg **`ui`/`mode`** へ（シェルライン確定の契約、shell/REPORT-status-icons.md §4）。テスト5箇所の参照も追随。test_settings 87 / test_settings_ui 36、全通過。
- main() を bd_app_theme() へ。
- カテゴリアイコン置換（追加指示）: 旧 ras/draw_glyph のコード描画ラスタライザを全廃。ホームのカテゴリ行は bdicon の実アイコン（material/fluent のモード対、44px の円盤スウォッチはテーマ色＋白グリフ合成に変更し、グリフ自体は実アイコン化）。WinUI の行アイコン・パンくずアイコンも fluent 実アイコン。拡大証拠は §5。
- 残件: REPORT-settings-rebuild.md へのキー差し替え付記（未実施、次回設定ライン作業時）。

### files（本ライン実施）
- main() を bd_app_theme() へ。コンストラクタは `bd_app_winui()` で分岐。
- アイコン全廃置換: 行アイコン6種は Papirus フルカラー（folder / application-x-generic / image-x-generic / audio-x-generic / video-x-generic / text-x-generic）。ヘッダ（戻る/検索/その他）とコンテキストバー8セル（開く/コピー/名前変更/削除/復元/完全削除/貼付/その他）は material/fluent のモード対（bd_icon_mode、テーマ色ティント）。場所メニュー7項目も Papirus の場所別アイコン（user-home / folder-download / folder-documents / folder-pictures / folder-music / drive-removable-media / user-trash）に個別化（従来は全部フォルダ絵）。
- WinUI 形（Explorer 構成）: 左に場所ナビペイン（208px、Papirus アイコン付き7項目、選択で places_go）、右カラムにコマンドバー（戻る/進む/上へ＋パンくずセグメントボタン＋検索/その他）、その下に検索行・パス行・コンテキストバー（選択時コマンド行の位置）、一覧、ステータス行。パンくずは refresh のたびに cur から再構築し、タップでその階層へ navigate。Android 形は従来構造を維持（ヘッダ先頭=戻るの UI テスト契約も維持）。
- テスト: test_files 135 / test_files_ui 45、変更前と同数で全通過（回帰ゼロ）。bd-files ビルド通過。
- 撮影: §5（v2_72/v2_73 を 854 で再撮影）。

### notes / tasks（子ライン完了）
- 詳細は apps/REPORT-apps-dual-mode-notes-tasks.md。両 main を bd_app_theme() 化、グリフ全廃（bd_icon_mode）。notes: Android は3画面ドリルダウン維持、WinUI は2ペイン（左=フォルダ切替＋検索＋一覧、右=書式バー付きエディタ）。tasks: 行チェックを実アイコン化、Android は単一カラム（上部コンボでリスト切替）、WinUI は To Do 形（左リストペイン＋右詳細）。fluent circle / checkmark_circle を素材に追加（全186点再レンダ）。
- テスト: test_notes 77 / test_tasks 89 / test_tasks_ui 32、ベースライン同数で全通過。一時ハーネスで両モードの分岐を実証済み。
- 申し送りだった notes WinUI 左ペイン幅は、本ラインの撮影で実不具合を確認し修正した: 横ボックスは非flex子に希望幅を与える仕様で、左ペインの希望幅（最長行由来 約748px）がエディタを 84px まで圧迫していた（プローブで矩形実測）。両ペインを flex 化し設計比 264:590 ≒ 4:9 の重み付けにした（左 256px / 右 約576px で再撮影確認、v2_76）。test_notes 77 は維持。

### calc / clock（子ライン完了）
- 詳細は apps/REPORT-apps-dual-mode-calc-clock.md。両 main を bd_app_theme() 化（bd-calc は Makefile に $(SHELLIB) 追加）。
- calc: tk_tabs を廃し単一の切替経路に統一。Android = 表示部下にモードストリップ＋履歴シート（履歴ボタンは history 実アイコン）。WinUI = ≡メニュー＋モード名タイトル、右に履歴/メモリペイン常設。backspace 専用キーを両モードに追加（実アイコン）、変換の ⇄ 文字ボタンは swap アイコン化。素材追加: material swap_horiz / fluent arrow_swap。
- clock: ナビを参照アプリ順（アラーム/時計/タイマー/ストップウォッチ）に並べ替え（公開ページ番号は不変、写像表で対応）。Android = 下部タブバー（実アイコン＋ラベル）、WinUI = 左ナビレール＋右コンテンツ（Fluent に stopwatch が無いため timer で代替、記録済み）。実タップ検証で発見した「Android で下部バーが画面外に押し出される」レイアウト不具合を修正済み。
- テスト: test_calc 136 / test_calc_ui 92 / test_clock 71 / test_clock_ui 43、ベースライン同数で全通過。モード別ハーネス（854×480）: calc 26+26、clock 22+22、実ポインタイベント検証も Android 10 / WinUI 9 で全通過。
- 撮影は screenshots/ が同ラインの編集禁止範囲だったため未実施 → 本ラインの最終撮影で撮る。

### player（子ライン完了＋親フィードバック対応を本ラインで撮影検証）
- 詳細は apps/REPORT-apps-dual-mode-player.md。main を bd_app_theme() 化、グリフ全廃。Android = 3画面ドリルダウン維持で全アイコン化。WinUI = Media Player 形（左ナビ全高＋右ペイン切替＋下部トランスポートバー常駐）。素材9点追加（material album/category/group/playlist_play、fluent album/people/tag/list/repeat_1）。
- テスト: test_player 84 / test_player_ui 40、ベースライン同数で全通過。
- **親目視フィードバック3点（v2_81 再生中 WinUI）の解消を、本ラインが 854 で再撮影して目視確認した**（§5）: (1) カバーは余白16px・固定176px・非flexの2カラムで画面内に完全収納。(2) シークトラックはレール画素 (134,134,134) 対背景 (243,243,243) で視認可能（音量と同一の tk_slider 描画）。(3) 配置はカバー左＋情報右（タイトル/アーティスト/アルバム同一左端）、トランスポート中央揃え、リピート状態はボタンのトグル表現にし「リピート: なし」の浮いた文字行を WinUI では廃止。
- 併せて直したもの: ナビ全高化（ドロワはみ出し）、バー内タイトル重なり（WinUI はタイトルのみ表示）。
- 既知の小点: カバー無し時のプレースホルダ（灰色地＋音符）は 96px マスターを約176pxへ拡大するため輪郭がやや粗い。実カバー（アルバムアート）には影響なし。恒久対策は §7。

### recorder / pkg / admin（子ライン完了）
- 詳細は apps/REPORT-apps-dual-mode-rec-pkg-admin.md。3アプリとも main を bd_app_theme() 化、アイコンは bd_icon_mode の実アイコン化（アイコン＋ラベル配置）。製品バイナリ3本が libbdshell.a 未リンクで bd_theme_from_reg が未解決になる不整合を発見し、Makefile のリンク行に $(SHELLIB) を追加して是正（notes ラインも同種の是正を実施）。
- recorder: Android = Google レコーダー式（下部中央の大きな録音ボタン、40px 実アイコン）、WinUI = ボイスレコーダー式（左一覧ペイン＋右波形/再生＋下部録音バー）。状態グリフ（▶●⏸■★）は全廃し文言表示に。
- pkg: Android = Play ストア式（上部タブ）、WinUI = 左ナビペイン 208px＋コマンド行＋一覧/詳細。
- admin: Android = 現行タブ維持、WinUI = タスクマネージャー式（左ナビ＋列ヘッダ付きプロセス表）。
- 素材追加: fluent pulse / gauge（admin 用、gauge は Fluent に memory が無いための代替選定を記録）。
- テスト: 6スイート計172件、ベースライン同数で全通過。両モード実駆動の使い捨てハーネス検証も全 PASS（録音→再生、インストール→削除、実プロセスへの SIGTERM 終了まで）。

### browser / osk（子ライン完了）
- 詳細は browser-engine/REPORT-browser-dual-mode.md と osk/REPORT-osk-dual-mode.md。
- ブラウザ: gui.c が起動時に `bd_theme_from_reg()` でモード選択（thememode.c を前端に同梱コンパイル、複製なし）。ツールバーは Android=従来 Material 形そのまま、WinUI=Edge 式（戻る/進む/更新を左、URL 中央、star/ホーム/横…メニューを右、新規タブはメニュー内）。Fluent アイコンは test-harness/make_icons_winui.py で icons_winui.h を新規生成（既存パイプラインと同方式、全点プレビュー目視確認済み）。前端フルビルド（make TARGET=brain、nsbrain リンク）通過、4条件（両モード×明暗）の撮影を目視確認済み。未検証: ARM 実機描画、brainreg ファイル層経由の指定、fav ボタンのタップ往復。
- OSK: 色・文字サイズ・候補計測はテーマ完全追随を数値で確認。**形だけ非追随**: キー角丸がライブラリ src/tk_osk.c で 6px 固定＋直角 stroke、候補チップは pill 固定。これはライブラリ側の問題として §7 に申し送り（修正案付き）。osk/ 側に描画コードは無く、直せるものは無かった。

## 5. 撮影・検証

撮影ハーネスの是正（本ライン）: screenshots/src/shot_apps.c と shot_apps_envfit.c のキャンバスを 800×480 → **854×480**（実パネル）へ全箇所変更（27+16箇所）。envfit に settings の両モード撮影（v2_82–84）を追加。convert.py は raw ヘッダの寸法を読むため無変更で 854 に対応。

撮影一覧（全て 854×480、JPG/PNG は screenshots/ 直下）:
- v2_72 files Android（選択＋コンテキストバー）/ v2_73 files WinUI（ナビペイン＋パンくず）— 本ライン目視確認済み。深いパスでパンくずが検索ボタンへ重なる不具合を発見し、5個超は中間を「…」へ畳む Explorer 方式で修正・再撮影確認済み。
- v2_74/75 notes Android（一覧/エディタ）/ v2_76 notes WinUI（2ペイン）— ペイン圧迫の不具合を修正後に再撮影し目視確認済み（§4）。
- v2_77–79 player Android（ライブラリ/再生中/キュー）/ v2_80/81 player WinUI — v2_81 は親フィードバック3点の解消を目視確認済み（§4）。v2_78（Android 再生中）もカバー収納・シーク視認性・配置を確認済み。
- v2_82 settings Android ホーム / v2_83 settings WinUI 初期表示（画面ページ）/ v2_84 settings WinUI 画面ページ（輝度スライダー編集中）— 目視確認済み。カテゴリアイコンは実アイコンの輪郭が AA で描かれている。
- 拡大証拠: v2_85（設定カテゴリアイコン列 ×4）、v2_86（files コンテキストバーのアイコン列拡大）。どちらも実セットのグリフ形が AA つきで読めることを目視確認済み。
- shot_apps 側（Android 形）: v2_10 notes / v2_12 calc / v2_21 tasks / v2_22 clock / v2_24 recorder / v2_25 pkg / v2_26 admin を 854 で再撮影。calc は backspace 実アイコン＋履歴アイコンを確認。
- calc/clock の WinUI 形と recorder/pkg/admin の WinUI 形は、各子ラインが自前ハーネスで実駆動検証済み（レポート参照）。envfit への追加撮影は次回ハーネス拡張時。

テスト総計（最終、本ラインが再実行）: apps の make check **20スイート 1,235件・0 failed**（内訳は §4 の各行。test_files_ui は戻るキー検証3件を追加し 45→48）。ブラウザ前端はフルビルド通過＋4条件撮影（子ライン）。ライブラリ make check は本ラインの範囲外（無変更）。

## 6. 恒常原則への対応

- コピー/オーバーヘッド: アイコンは起動時に必要分だけ読み、
  プロセス内で共有する（アイコン単位で1コピー）。
- キャッシュ/メモリ: アイコンキャッシュはバイト予算を設け、
  予算超過時は載せない（無制限に溜めない）。
- 実機の現実: モードキーは brainreg（原子的書換え基盤）に乗り、
  未設定・不正値でも android 既定に落ちる（thememode の契約）。
