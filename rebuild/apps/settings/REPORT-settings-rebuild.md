# REPORT-settings-rebuild — 設定アプリ全面作り直し 実施記録

設計の正本: DESIGN-settings-rebuild.md。
本レポートは段階ごとに追記する（最終報告の喪失対策）。

## 0. 指示と前提

- ユーザー判断（2026-11-21）: 現行の設定アプリは見た目・構成
  とも「ちゃっちい」。既存 UI は捨て、Android / Windows の
  実際の設定画面を調べたうえで作り直す。
- 追加指示（同日）: 項目の網羅性そのものを設計対象にする
  （現行はカテゴリ・項目が少なすぎる）。見せかけの項目は
  作らない。実在するのに欠けている項目は追加する。
- 追加指示（同日）: 表示モード（Android / Windows）の切替を
  設定アプリから行えるようにする。キーの正本と反映
  タイミングはシェル側の方式確定に合わせる。
- 制約: 作業範囲は apps/settings/ と撮影・テストの最小範囲。
  ライブラリ本体（include/・src/）と screenshots/Makefile は
  別ライン作業中のため編集しない。

## 1. 第1段階: 調査（完了）

DESIGN §1 に出典 URL・取得日（2026-11-21）つきで記録。要点:

- Android: AOSP top_level_settings.xml の実ファイルを取得し
  トップレベル21項目の順序を確認。androidx preference の
  実レイアウト（行の最小高、アイコン最大48dp、テキスト
  マージン）を取得。display_settings.xml の
  PreferenceCategory グルーピングを確認。Material 3
  Expressive（QPR1）の色付きアイコン＋カード化を確認。
- Windows: Microsoft Learn の設定ガイドライン
  （SettingsCard/Expander、セクション見出し、即時反映、
  About の置き方）を取得。CommunityToolkit の
  SettingsCard.xaml 実寸（MinHeight 68、Padding 16、
  アイコン Margin 2,0,20,0、Expander 子 Padding 58,8,44,8）
  を取得。Windows 11 設定のカテゴリ一覧（11＋ホーム）と
  左ナビ＋パンくずの構成を確認。
- 端末値: 854×480・約179ppi・1dp=1.12px・フロア54px・
  RGB565（AUDIT §20/§21）。

## 2. 棚卸し（完了）

- 現行機能: DESIGN §2 の表（8ページ・全行・制御実体を
  setmodel.c 実コードで確認）。UI は捨てるが機能は全て残す。
- スタック全体: DESIGN §3 の表（brainreg 全キーを grep で
  全数確認）。現行未掲載で実在する制御面として
  repeat_source / ミュート / 壁紙（色）/ パネル位置 /
  ストレージ独立ページ の5件を特定。逆に設定面を持たない
  もの（assist、通知、IME 学習、OSK、RNDIS、フォント、
  キー配列切替、保存済みネットワーク管理）は不採用として
  理由つきで DESIGN §4 項目対応表に記録（全29件）。

## 3. 第2段階: 設計（完了）

DESIGN §5〜§7。確定事項:

- ページは10（既存8＋個人用設定＋ストレージ）。
  enum は既存値を維持し末尾追加（既存利用者の互換）。
- Android モード = AOSP ホーム（検索＋2行カテゴリ一覧、
  色付き円アイコン）＋ドリルダウン。サブページは
  グループ見出し＋カード、行内編集（SeekBarPreference 形）。
- WinUI モード = 左ナビ常駐（NavigationView 形）＋
  パンくず＋行ごとの SettingsCard 形カード。cycle は
  右端コンボボックス。
- 検索は両モードとも全項目対象、結果は「項目名＋ページ名」、
  タップで該当行へジャンプ。
- 表示モード切替: 個人用設定に cycle 行。書き込みは
  setmodel の1関数に隔離し暫定キー shell/ui_mode を使用。
  **キーの正本と反映タイミングはシェル側の方式確定待ち**
  （DESIGN §6）。追随実装はシェル側へ申し送り。

## 4. 第3段階: 実装（完了）

### 4.1 モデル（setmodel.c / setmodel.h）

既存の全ページ・全行・id・書込先は不変。追加のみ:

- enum に SET_PAGE_PERSONAL / SET_PAGE_STORAGE を末尾追加
  （既存値は不変、shot_apps 互換を維持）。
- サウンドに mute 行（SR_TOGGLE）: reg shell/muted ＋
  backend へ実効音量を適用し、シェルの bd_shell_set_muted /
  bd_shell_set_volume と同一方式（音量変更で自動解除、
  音量は shell/volume へ永続化）。ミュート ON の瞬間は
  ライブ音量を shell/volume に揃えてから 0 に落とす
  （新規端末でキーが無い場合の復帰先を正すため。下記 5 の
  UI テストで実際に踏んで修正した）。
- キーボードに repeat_source 行（SR_CYCLE:
  ソフトウェア/カーネル）: bd_kbd_settings 経由で
  keyboard/repeat_source を読み書き。
- PERSONAL ページ: ui_mode（Android/Windows、暫定キー
  shell/ui_mode、書込は personal_apply の1箇所に隔離）、
  wallpaper（既定=reg_unset ＋色プリセット6件。既存値が
  画像パスの場合は choice_cur=-1 のカスタム値として
  「画像（パス）」と表示し誤上書きしない）、
  panel（下/上、shell/panel_position）。
- STORAGE ページ: sy_mounts のマウント別 info 行
  （合計/空き/使用率）。

### 4.2 UI（bd_settings.c 全面書き換え、bd_settings.h 注記更新）

- モードで構造ごと切り替える（色替えではない）:
  - Android（style 0/1）: 見出し＋検索バー＋カテゴリ一覧
    （色付き円アイコン＋タイトル＋ライブ概要の2行
    ListView）→ ドリルダウン。サブページはアプリバー
    （戻る＋ページ名）＋グループ見出し＋カード群。
    cycle は行内セグメント、slider はラベル下全幅、
    toggle は右端スイッチ、action は右端ボタン、PSK は
    AP 行の下に展開（従来の行内編集モデルを継承）。
  - WinUI（style 2）: 左ナビ常駐（tk_drawer 標準パネル＝
    NavigationView 形、上部に検索）＋パンくず＋ページ名
    ＋行ごとに SettingsCard 形カード（ヘッダアイコン＋
    タイトル＋説明、右端に操作。cycle はコンボボックス、
    info は右端に値）。
- 検索は両モードとも全ページ全行が対象。結果は
  「項目名＋ページ名」の2行、選択で該当ページの該当行へ
  ジャンプ（選択状態つき）。
- アイコンはコードからラスタライズ（14種のグリフ）。
  Android はカテゴリ色の円＋白グリフ、WinUI はテーマ
  文字色の単色グリフ。同梱 Papirus にカテゴリ用が無い
  ことの実測は DESIGN §5.4。
- 公開 API（bd_settings.h）は維持。page_list /
  search_field の指す実体が変わったことをヘッダ注記に
  明記。settings_app_page() は Android ホームで
  SETTINGS_PAGE_HOME (-1) を返す。
- 実装中に踏んだ実バグ: tk_card_new は tk_box_new と違い
  cross_align が非ストレッチ既定のため、カード内の行が
  左に潰れた → カード生成時に tk_box_set_align
  (TK_ALIGN_STRETCH) を指定して解消（ライブラリ側は
  無変更。撮影で発見し撮影で解消を確認）。

## 5. 検証

- モデルテスト test_settings: **87 passed, 0 failed**
  （従来51件は全て維持＋新規36件: ミュート一式、
  repeat_source、ui_mode、wallpaper（画像パスの正直な
  表示を含む）、panel、storage）。
- UI テスト test_settings_ui: **36 passed, 0 failed**
  （ホーム→ドリルダウン、行内スライダー、プリセット、
  repeat_source、ミュート往復、ui_mode 往復、検索
  ジャンプ（両モード）、ナビ選択追随、WinUI カード
  スライダー）。駆動手順は新フローに改訂、チェックの
  趣旨は維持（DESIGN-apps-envfit.md の前例と同種）。
- アプリ群全体 `make -C apps check`: **全20テスト
  0 failed**（files/notes/tasks/clock/calc/recorder/
  player/pkg/settings/admin/alarmd、bd-settings 本体
  ビルド含む）。回帰なし。
- 撮影（全て目視確認済み）:
  - 既存ハーネス shot_apps settings（無改変）で
    v2_11/15/16/17/18/19/20 を再撮影。**ハーネスは
    800×480 のまま**（854 化は M22 ラインの担当範囲の
    ため、こちらの撮影は 800 幅であることを明記する）。
    キーボード頁（プリセット＋リピート方式のセグメント）、
    Wi-Fi 頁（トグル＋情報行＋グループ）を確認。
  - 854×480 の自前ダンプ（UI テストの
    BD_SETTINGS_SHOT_DIR 経由、apps/settings/shots/ に
    JPG を収蔵）: Android ホーム（色付きアイコン＋
    ライブ概要）、Android 画面頁、Android 個人用設定
    （表示モードのセグメント）、WinUI 画面頁
    （ナビ＋パンくず＋SettingsCard）、WinUI 個人用設定
    （コンボボックスつきカード）、WinUI 検索結果。
- 未検証: 実機/QEMU ゲストでの実操作（統合フェーズ）、
  WinUI ダークテーマでの撮影（テーマは4種あるが撮影は
  light のみ）、コンボボックスのドロップダウン展開状態
  の撮影（選択→書込は activate 経路で検証済み、展開
  メニュー自体の見た目はツールキットの監査範囲）。

## 6. 申し送り・未検証

- 表示モード: キーの正本確定と、シェル＋全アプリの追随
  実装はシェル担当ライン側。設定側の差し替え箇所は
  setmodel.c の SET_UI_MODE_* 定数と personal_apply の
  ui_mode 分岐のみ。反映タイミングもシェル側の方式
  確定待ち（DESIGN §6）。
- フォント/表示スケール: ツールキット density API の
  永続化先が決まれば設定項目化できる（DESIGN §4 #25）。
- ダークテーマ切替: テーマ決定方式の確定待ちに合流
  （DESIGN §4 #26）。
- 壁紙の画像指定: キーのパス形式はシェルが解釈可能で
  既存値は保持・表示されるが、ファイル選択 UI は本
  作り直しの範囲外（DESIGN §4 #9）。

---

## 付記（2026-11-21、両モード対応ライン）

- モード切替行（ui_mode）の書込先キーを、暫定の `shell`/`ui_mode` から正本の brainreg **`ui`/`mode`**（"android"/"winui"）へ差し替えた（シェルライン確定の契約: shell/REPORT-status-icons.md §4、読取りは libbdshell の `bd_theme_from_reg()`）。反映は次回プロセス起動時。setmodel.c の SET_UI_MODE_SECTION/KEY とテスト参照5箇所を追随済み（test_settings 87 / test_settings_ui 36 全通過）。
- カテゴリアイコンを、コード描画のラスタライザ（ras/draw_glyph）から、確立したベクターセット（Material Symbols / Fluent UI System Icons）由来の実アイコンへ置換した（ユーザー指摘「見た目が荒い」への対応、両モード対応ラインで実施）。詳細は apps/REPORT-apps-dual-mode.md §4。
