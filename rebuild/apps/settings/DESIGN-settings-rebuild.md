# DESIGN-settings-rebuild — 設定アプリ全面作り直し設計

ユーザー判断（2026-11-21）: 現行の設定アプリは見た目・構成とも
「ちゃっちい」。既存 UI は破棄し、Android と Windows の実際の
設定画面を調査したうえで作り直す。あわせて項目の網羅性そのもの
も設計対象とする（「設定項目も少なすぎでは？」2026-11-21）。
捨てるのは UI であって機能ではない。見せかけの項目は作らない。

作成: 2026-11-21。本書が調査結果と設計の正本。
実施記録は REPORT-settings-rebuild.md。

---

## 1. 調査結果（出典つき、全て 2026-11-21 取得）

### 1.1 Android 側

**(a) ホーム画面のカテゴリ構成 — AOSP 実物**
`packages/apps/Settings/res/xml/top_level_settings.xml`（main ブランチ、
googlesource から実ファイル取得）。トップレベル項目は order 順に:

ネットワークとインターネット / コミュニティ（Communal）/ 接続済み
デバイス / アプリ / 通知 / モード（優先モード）/ バッテリー /
ストレージ / サウンドとバイブレーション / ディスプレイ /
壁紙とスタイル / ユーザー補助 / セーフティセンター / セキュリティ /
プライバシー / 位置情報 / 安全と緊急 / パスワードとアカウント /
システム / デバイスについて / ヒントとサポート

各項目は Preference（アイコン＋タイトル＋概要の2行構成、
概要はコントローラが動的に供給: 例 バッテリー残量、接続先 SSID）。
タップでサブページへドリルダウンする。
出典: https://android.googlesource.com/platform/packages/apps/Settings/+/refs/heads/main/res/xml/top_level_settings.xml

**(b) 情報設計の枠組み — AOSP 公式ドキュメント**
ページは DashboardFragment（ホスト）＋ PreferenceController 群で
構成され、項目はページ間を移動可能なプラグイン単位。
出典: https://source.android.com/docs/core/settings/info-architecture

**(c) Preference 行の形 — AndroidX 公式ドキュメント＋実装レイアウト**
- 基本行 = タイトル＋概要（summary）の2行。SwitchPreference は
  右端にスイッチ、SeekBarPreference はタイトル＋概要の下に
  シークバーを全幅で置く。
  出典: https://developer.android.com/develop/ui/views/components/settings
- 実装レイアウト `preference.xml`（androidx-main 実ファイル取得）:
  行の最小高 = listPreferredItemHeight、アイコンは最大 48dp、
  テキスト塊は開始側 15dp / 終端側 6dp のマージン、上下 6dp。
  seekbar 版はテキスト開始マージン 16dp。
  出典: https://github.com/androidx/androidx/blob/androidx-main/preference/preference/src/main/res/layout/preference.xml
        同 preference_widget_seekbar.xml / preference_widget_switch_compat.xml

**(d) サブページのグルーピング — AOSP 実物**
`display_settings.xml`: ページ内は PreferenceCategory で
グループ化される（例: 明るさ / ロック画面 / 外観 / 色）。
出典: https://android.googlesource.com/platform/packages/apps/Settings/+/refs/heads/main/res/xml/display_settings.xml

**(e) Material 3 Expressive（Android 16 QPR1、2025-09 Pixel 配信）**
ホーム画面はカテゴリごとに色付きアイコン、関連項目を角丸カードに
グループ化、遷移行にはシェブロン、見出しは強め。分類体系自体は
(a) から不変。
出典: https://geekchamp.com/android-16-qpr1-brings-a-colorful-settings-redesign-to-supported-pixels/
      https://mefmobile.org/android-16-qpr1-brings-a-colorful-settings-redesign-to-pixel/

**(f) 検索**
ホーム最上部に検索バー。検索は全ページの全項目を対象とし、
結果は「項目名＋所属ページ名」の2行で表示、タップで該当ページの
該当項目へ移動する（Android の設定検索の実挙動。(b) の IA と
トップレベル検索 UI の組み合わせ）。

### 1.2 Windows 側

**(a) カテゴリ構成 — Windows 11 設定アプリ実物**
左サイドバー常駐＋上部パンくず。カテゴリ順:
ホーム / システム / Bluetooth とデバイス / ネットワークと
インターネット / 個人用設定 / アプリ / アカウント / 時刻と言語 /
ゲーム / ユーザー補助 / プライバシーとセキュリティ /
Windows Update。
システム配下: ディスプレイ / サウンド / 通知 / 集中モード /
電源とバッテリー / ストレージ / マルチタスク / ライセンス認証 /
トラブルシューティング / 回復 / バージョン情報。
個人用設定配下: 背景 / 色 / テーマ / ロック画面 / スタート /
タスクバー / テキスト入力。
出典（Microsoft のカテゴリ構成の記述として複数資料で一致）:
https://support.microsoft.com/en-us/windows/ （Settings の案内）
https://www.minitool.com/news/windows-11-settings-how-to-open-use-it.html
https://www.osimulator.com/blog/windows-11-settings-app-explained.html

**(b) 設定ページの作法 — Microsoft Learn 公式ガイドライン**
- 関連設定はセクションヘッダ（BodyStrong）の下にグループ化。
- 項目は SettingsCard / SettingsExpander（Windows Community
  Toolkit）で作る: ヘッダ＋説明＋ヘッダアイコン＋右端に操作
  コントロール（ToggleSwitch / ComboBox / Button）。
- 2値にはトグルスイッチ、5個までの排他選択にはラジオ、
  選択肢集合にはコンボボックス。変更は即時反映（確認ボタンなし）。
- バージョン情報は最下部に SettingsExpander で置く。
- 内容は単一カラム、スクロール可。
出典: https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings

**(c) SettingsCard の実寸法 — CommunityToolkit 実装 XAML 取得**
SettingsCard.xaml（main ブランチ実ファイル）:
- SettingsCardMinHeight = 68、Padding = 16,16,16,16
- HeaderIconMargin = 2,0,20,0（アイコンと本文の間 20）
- CornerRadius = ControlCornerRadius（WinUI 既定 4）
- SettingsExpander の子カード: Padding 58,8,44,8
  （ヘッダ本文の開始位置 58 に揃える）、MinHeight 52
出典: https://github.com/CommunityToolkit/Windows/blob/main/components/SettingsControls/src/SettingsCard/SettingsCard.xaml
      同 SettingsExpander/SettingsExpander.xaml

### 1.3 端末の確定値（AUDIT-toolkit.md §20 / REPORT-toolkit-m21.md §8）

- パネル 854×480、表示域 121×68mm、約179ppi、1px = 0.1417mm
- Android 1dp = 1.12px、48dp = 53.8px → タッチフロア 54px
- WinUI 換算は錨方式 ×1.344（40epx を 48dp の物理寸法に同寸化）
- 表示経路は RGB565 = 65,536 色
- 寸法の最終決定はツールキットのテーマトークンに従う
  （control_h / row_h / row_h2 / radius_card 等。本アプリ側で
  独自の px 定数を新設しない）

---

## 2. 現行設定アプリの機能棚卸し（setmodel.c 実コードで確認）

捨てるのは UI のみ。以下は全て実在し、作り直し後も欠落は不合格。

| ページ | 行（id / 種別） | 制御実体 |
|---|---|---|
| キーボード | preset / cycle | bd_kbd_settings（brainreg keyboard） |
| | delay / slider 100–1000ms | 同上 |
| | interval / slider 10–200ms | 同上 |
| | longpress / slider 0–1500ms | 同上 |
| | layout / info「Brain 固定配列」 | 固定（選択肢は存在しない、§3） |
| 画面 | brightness / slider 0–100 | bd_backend（QS と同一オブジェクト） |
| サウンド | volume / slider 0–100 | bd_backend |
| Wi-Fi | wifi_on / toggle | reg shell/wifi ＋ bd_wifi 再接続・切断 |
| | state / ssid / ip / info | bd_wifi_get_status |
| | scan / action、ap* / action（PSK 展開つき）、disconnect / action | bd_wifi |
| 電源 | battery / info | bd_backend_battery |
| | sleep / cycle {なし,1,5,10,30分} | reg power/sleep_timeout_min |
| 日付と時刻 | offset / slider −720–+840（30分刻み） | reg datetime/local_offset_min |
| | utc / local / info、note / info | time() |
| マスコット | enabled / toggle、pack / cycle、size / cycle {168,208,248,320}、fullscreen / toggle、note / info | reg mascot/*、パック列挙はセッションと同一規則 |
| 端末情報 | os / machine / rootfs / info、mem / swap / info、fs* / info | sy_sys（sysmodel） |

UI 側の現行形（破棄対象）: 見出し＋検索ピル＋左ページリスト
（テキストのみ）＋ページ名＋全行を1枚カードに詰めたスクロール。
行内編集（スライダー/チップ/スイッチを行に置く）自体は
2026-11-20 のユーザー判断で確定した操作モデルであり、これは
引き継ぐ（§5）。

## 3. スタック全体の制御面棚卸し（項目対応表の材料）

brainreg の全キー（shell/src・apps・osk・libbrainime を grep で
全数確認、2026-11-21）、バックエンド API、関連モジュールの
設定面を洗い出した結果:

| 制御面 | 実体 | 現行設定アプリでの扱い |
|---|---|---|
| keyboard/repeat_source（synthetic/kernel） | kbdsettings が読み書き、tk_kbd_config.repeat_mode | **未掲載 → 追加** |
| shell/muted（ミュート） | shellui の bd_shell_set_muted: reg ＋ backend へ実効音量 0/復帰を適用。音量変更時は自動解除（unmute-on-adjust） | **未掲載 → 追加** |
| shell/wallpaper（色 #RRGGBB または画像パス） | shellui load_settings が解釈（'/' 始まり=画像、それ以外=色） | **未掲載 → 追加**（色プリセット＋既定に戻す。画像は §4 で不採用理由を記録） |
| shell/panel_position（top/それ以外=bottom） | shellui load_settings | **未掲載 → 追加** |
| shell/brightness, shell/volume | QS と backend の永続側。設定アプリは backend 経由で同一値を扱う（現行どおり） | 掲載済み相当 |
| ストレージ容量（マウント別） | sy_mounts（sysmodel、端末情報で一部表示） | **独立ページ化 → 追加** |
| assist（1本指アシスト） | shell/src/assist.c。常駐オーバーレイで、永続設定面を持たない（reg キーなし） | 対象なし → 不採用（§4） |
| 通知 | shell/src/notify.c。通知モデルはあるが永続設定面（DND 等）を持たない | 対象なし → 不採用（§4） |
| IME 学習 | libbrainime: bi_engine_enable_learning は埋め込み側がパスを渡して有効化。ユーザー切替面なし | 対象なし → 不採用（§4） |
| OSK | osk/ に設定面なし（WM 連携で表示制御） | 対象なし → 不採用（§4） |
| RNDIS / USB 接続 | rebuild ツリー内に制御面なし（カーネル/OS 統合側の領域） | 対象なし → 不採用（§4） |
| フォント / 表示サイズ | ツールキットに density API はあるが永続化先（reg キー）が存在しない | 対象なし → 不採用（§4、申し送り） |
| テーマ（モード/ダーク） | 現行は各プロセス main が tk_theme_v2 を固定（キーなし） | **ユーザー指示で実項目化**（§6、キーはシェル側確定待ち） |
| keyboard/layout | kbdsettings.h 明記: 実在するキーマップは1つだけ。選択肢の提示自体が禁止されている | info 表示のみ（現行どおり） |
| Wi-Fi 保存済みネットワーク管理 | bd_wifi API に列挙・削除の面なし（scan/status/connect/disconnect/reconnect のみ） | 対象なし → 不採用（§4） |
| 時刻の自動設定（NTP 等） | 制御面なし。offset のみが実在 | offset のみ（現行どおり） |

## 4. 項目対応表（採用 / 不採用の全件）

参照列: A = Android 設定、W = Windows 設定。
判定基準: 制御対象が実在するものだけを UI に出す。

| # | 項目 | 参照 | 端末側の対応 | 判定 |
|---|---|---|---|---|
| 1 | Wi-Fi オン/オフ・状態・接続先・IP・スキャン・接続・切断 | A: ネットワークとインターネット / W: ネットワークとインターネット | bd_wifi ＋ reg shell/wifi | 採用（現行維持） |
| 2 | 画面の明るさ | A: ディスプレイ＞明るさ / W: システム＞ディスプレイ | bd_backend 輝度 | 採用（現行維持） |
| 3 | 音量 | A: サウンド / W: システム＞サウンド | bd_backend 音量 | 採用（現行維持） |
| 4 | ミュート | A: サウンド（着信音量とは別に実在） / W: サウンドのミュート | reg shell/muted ＋ backend 適用（シェルと同一方式） | **採用（新規）** |
| 5 | 電池残量・状態 | A: バッテリー / W: 電源とバッテリー | bd_backend_battery | 採用（現行維持） |
| 6 | 画面タイムアウト（スリープ） | A: ディスプレイ＞画面タイムアウト / W: 電源＞画面とスリープ | reg power/sleep_timeout_min | 採用（現行維持） |
| 7 | ストレージ（マウント別容量） | A: ストレージ / W: システム＞ストレージ | sy_mounts | **採用（新規ページ）** |
| 8 | 壁紙（デスクトップの色） | A: 壁紙とスタイル / W: 個人用設定＞背景 | reg shell/wallpaper（色形式） | **採用（新規）** 色プリセット＋既定 |
| 9 | 壁紙（画像ファイル指定） | 同上 | 同キー（パス形式）は実在するが、ファイル選択 UI の検証範囲が本作り直しを超える | 不採用（理由: 選択 UI は別途。キーのパス形式自体はシェルが解釈可能で、既存値が画像パスの場合は「画像」として正直に表示し、上書きしない） |
| 10 | パネル（タスクバー）の位置 | W: 個人用設定＞タスクバー | reg shell/panel_position | **採用（新規）** 上/下 |
| 11 | 表示モード（Android / Windows） | A/W の見た目そのものの切替 | brainreg のモードキー（§6） | **採用（新規、ユーザー指示）** |
| 12 | マスコット表示・パック・サイズ・全画面 | 端末固有（参照なし） | reg mascot/* | 採用（現行維持） |
| 13 | キーリピート: プリセット・開始・間隔 | W: 時刻と言語＞入力 / 端末固有 | bd_kbd_settings | 採用（現行維持） |
| 14 | キーリピート方式（ソフト/カーネル） | 端末固有 | keyboard/repeat_source | **採用（新規）** |
| 15 | 長押し判定時間 | 端末固有（1本指・抵抗膜の実用項目） | keyboard/longpress_ms | 採用（現行維持） |
| 16 | キー配列の切替 | A/W には実在 | 実在するキーマップは1つのみ（kbdsettings.h が提示を禁止） | 不採用（理由: 選択肢が存在しない。info 行で「Brain 固定配列」と表示） |
| 17 | 日付と時刻: UTC オフセット | A: システム＞日付と時刻 / W: 時刻と言語 | reg datetime/local_offset_min | 採用（現行維持） |
| 18 | 時刻の自動設定・タイムゾーン名選択 | 同上 | 制御面なし（offset が唯一の実在） | 不採用（理由: 対象なし。offset 行が実質のタイムゾーン設定） |
| 19 | 端末情報（OS・機種・rootfs・メモリ・スワップ） | A: デバイスについて / W: システム＞バージョン情報 | sy_sys | 採用（現行維持） |
| 20 | 通知の設定（DND・アプリ別） | A: 通知 / W: システム＞通知 | 通知モデルに永続設定面なし | 不採用（理由: 対象なし） |
| 21 | 1本指アシストのオン/オフ | 端末固有 | assist は常駐 UI で設定面を持たない | 不採用（理由: 対象なし。常時有効が現行設計） |
| 22 | 文字入力（IME 学習のオン/オフ、ユーザー辞書） | A: システム＞キーボード | 学習は埋め込み側固定、切替面なし | 不採用（理由: 対象なし） |
| 23 | OSK の詳細（キー高さ等） | A: 画面キーボード | OSK に設定面なし | 不採用（理由: 対象なし） |
| 24 | RNDIS / USB 接続 | W: ネットワーク（イーサネット相当） | rebuild 内に制御面なし | 不採用（理由: 対象なし。OS 統合フェーズの領域） |
| 25 | フォントサイズ・表示スケール | A: ディスプレイ＞表示サイズとテキスト / W: ディスプレイ＞拡大縮小 | 永続化先の reg キーが存在しない | 不採用（理由: 対象なし。ツールキット density API はあるため、シェル側で永続化が決まれば追加可能 → 申し送り） |
| 26 | ダークテーマ切替 | A: ディスプレイ＞ダークテーマ / W: 個人用設定＞色 | モードキーと同様、現行は固定で永続化先なし | 不採用（理由: §6 のモード切替に含めない。シェル側のテーマ決定方式の確定待ちに合流 → 申し送り） |
| 27 | Bluetooth・位置情報・プライバシー・ アカウント・更新 | A/W の主要カテゴリ | 対応ハードウェア/サービスが端末に存在しない | 不採用（理由: 対象なし） |
| 28 | アプリ管理（インストール済みアプリ等） | A: アプリ / W: アプリ | bd-pkg が専用アプリとして実在（設定の責務外） | 不採用（理由: 専用アプリあり。設定に重複させない） |
| 29 | 保存済み Wi-Fi ネットワークの管理 | A: ネットワーク＞保存済み | bd_wifi に列挙面なし | 不採用（理由: 対象なし） |

## 5. 情報設計（最終形）

### 5.1 ページ一覧（10ページ、両モード共通の実体）

enum は既存値を維持し末尾に追加する（shot_apps 等の既存利用者の
ため）: KEYBOARD / DISPLAY / SOUND / WIFI / POWER / DATETIME /
MASCOT / ABOUT（既存）＋ PERSONAL（個人用設定）/ STORAGE
（ストレージ）（新規）。

| ページ | 行（グループ別） |
|---|---|
| Wi-Fi | [接続] wifi_on(toggle), state, ssid, ip / [ネットワーク] scan, ap*, disconnect |
| 画面 | [明るさ] brightness(slider) |
| サウンド | [音量] volume(slider), mute(toggle) ※新規 |
| 電源 | [電池] battery / [スリープ] sleep(cycle) |
| ストレージ ※新規 | [容量] fs*(info、マウント別) |
| 個人用設定 ※新規 | [表示] ui_mode(cycle) ※§6 / [デスクトップ] wallpaper(cycle), panel(cycle) |
| マスコット | [表示] enabled(toggle), pack(cycle), size(cycle), fullscreen(toggle), note |
| キーボード | [キーリピート] preset(cycle), repeat_source(cycle) ※新規, delay(slider), interval(slider) / [長押し] longpress(slider) / layout(info) はキーリピート group 末尾 |
| 日付と時刻 | [時刻] offset(slider), utc, local, note |
| 端末情報 | [端末] os, machine, rootfs / [メモリ] mem, swap / [ストレージ] fs* |

### 5.2 Android モードの構成（テーマ style 1 / 0）

AOSP ホーム＋ドリルダウン（§1.1 (a)(c)(d)(f)）:

- 起動直後はホーム: 見出し「設定」＋検索バー（tk_searchbar、
  M3 検索バーの実形）、その下にカテゴリ一覧。
- カテゴリ一覧は ListView の2行行: 左に色付き円アイコン
  （カテゴリ色＋白グリフ、§5.4）、タイトル＋概要（現在値の
  要約: 例 Wi-Fi は「オン・接続先 SSID」、画面は「輝度 80%」）。
  並びは AOSP の順序思想（ネットワーク→デバイス系→
  パーソナライズ→入力系→システム→端末情報）:
  Wi-Fi / 画面 / サウンド / 電源 / ストレージ / 個人用設定 /
  マスコット / キーボード / 日付と時刻 / 端末情報。
- カテゴリをタップ → サブページ: 上部に戻るボタン＋ページ名、
  本文はグループ見出し（キャプション体・淡色）＋グループの
  カード（角丸はテーマの radius_card）に行を積む（§1.1 (d) の
  PreferenceCategory ＋ Expressive のカード化）。
- 行の形（§1.1 (c)）:
  - info: タイトル＋値（概要体）の2行
  - toggle: 左にタイトル＋値、右端にスイッチ
  - slider: タイトル＋値を1行に置き、スライダーを下に全幅
    （SeekBarPreference の形）
  - cycle: タイトル＋現在値の下にセグメントボタン全幅
    （選択肢は最大8件・短文のため行内セグメントが成立する。
    AOSP の ListPreference はダイアログだが、本端末では
    2026-11-20 確定の「行内で完結する編集」モデルを優先する
    — 乖離として明記）
  - action: 左にタイトル＋値、右端にボタン。Wi-Fi の AP 行は
    従来どおり PSK フォームを行の下に展開する
- 検索: 入力中はカテゴリ一覧が検索結果に変わる。全ページの
  全行を対象にラベル/値で照合し、「項目名＋ページ名」の2行で
  表示。タップで該当ページへ移動し、その行を選択状態にする
  （§1.1 (f)）。

### 5.3 WinUI モードの構成（テーマ style 2）

Windows 11 設定アプリの形（§1.2）:

- 左ナビ常駐（tk_drawer の標準パネル形 = NavigationView 展開
  パネル相当）: 最上部に検索ボックス（AutoSuggestBox 形は
  テーマ側が描く）、項目はアイコン＋ラベル、選択中は
  テーマの選択面＋左端アクセントバー（ツールキットの
  NavigationView 描画）。
  並びは Windows の分類順: 画面 / サウンド / 電源 / ストレージ
  （システム）→ Wi-Fi（ネットワーク）→ 個人用設定 /
  マスコット（個人用設定）→ キーボード / 日付と時刻
  （時刻と言語・デバイス）→ 端末情報（システム＞バージョン情報）。
  ナビ幅は 256px とする（乖離の記録: 実物は 320epx =
  換算 430px で、854px 幅では本文が SettingsCard の最小幅を
  割り込むため、画面幅に対する比率を優先した実機適合）。
- 本文上部: パンくず「設定 › ページ名」（キャプション体・
  淡色）＋ページ名（タイトル体）。
- 本文: セクション見出し（HEADING 体 = BodyStrong 相当）の
  下に、**行ごとに1枚のカード**（tk_card。WinUI テーマでは
  CardBackground＋カードストローク＋半径4で描かれる =
  SettingsCard の形、§1.2 (c)）。カード間隔は 4px
  （実物のカード間隔に倣う）。
  - info: 左にタイトル、右端に値を淡色で（Windows の
    バージョン情報ページの形）。note 行は説明文として
    タイトルの下に置く
  - toggle: 左にタイトル＋値（説明位置）、右端にスイッチ
  - slider: カード内にタイトル＋値の行、その下にスライダー
    全幅（Windows の明るさカードの実形）
  - cycle: 右端にコンボボックス（§1.2 (b) の指定どおり）
  - action: 右端にボタン
- 検索: ナビ上の検索で本文が検索結果リストに変わる
  （Windows の設定検索と同じく本文側に結果を出す）。
  結果の行形式・遷移は Android モードと共通。
- 端末情報ページは最下部グループ扱い（§1.2 (b) の About の
  作法に倣い、ナビ最下段に置く）。

### 5.4 アイコン

同梱 Papirus セット（assets/app）はアプリ用17枚のみで、
カテゴリ用（Wi-Fi/キーボード/時計等）は存在しない
（DESIGN-apps-envfit.md §134 の実測どおり）。
そのためカテゴリ/行アイコンは、シェルの QS グリフと同じ流儀で
**単色グリフをコードからラスタライズ**して使う（外部素材を
増やさない、RAM はアイコン1枚 44×44×4B ≒ 7.7KB を構築時のみ）:
- Android モード: カテゴリ色の円＋白グリフ（Expressive の
  色付きアイコン、§1.1 (e)）。ホーム行のみ（サブページ行は
  AOSP の実形どおりアイコンなし）
- WinUI モード: 無地にテーマ文字色のグリフ（Fluent の線
  アイコン相当）。ナビ項目とカードのヘッダアイコンに使う
グリフ形状は各カテゴリの意味から自作する（wifi=弧、
画面=輝度、サウンド=スピーカー、電源=電池、ストレージ=円柱、
個人用設定=人、 マスコット=星、キーボード=キー配列、
日時=時計、端末情報=i、ミュート=スピーカー×、壁紙=画像、
パネル=ドック、モード=画面切替）。

### 5.5 寸法

- 行・コントロールの高さ、角丸、余白は全てテーマトークン
  （row_h2 / control_h / radius_card / unit）に従う。
  タッチフロア 54px（§1.3）はトークン側の責務。
- ページ余白: Android は unit×2、WinUI 本文は実物の
  ページマージン（24epx 換算 ≒ 32px を上限に、854 幅では
  unit×2=16px を採用 — 実機適合として記録）。
- SettingsCard 実寸（MinHeight 68epx → 換算 91px）は
  トークンの row 系が吸収する範囲とし、本アプリでは
  カードの最小高を control_h 以上に保つ。

## 6. 表示モード切替（ユーザー指示 2026-11-21）

- 個人用設定ページに「表示モード」cycle 行を置く:
  選択肢 = Android / Windows。
- 書き込み先は brainreg のモード設定キー。**キーの正本と
  反映タイミング（即時 / 再ログイン時）はシェル側の方式
  確定待ち**（シェル担当ラインが現行の決定方法を調査中）。
  現行スタックでは各プロセスの main がテーマを固定しており
  永続キーは存在しない（2026-11-21 実コードで確認）。
- 本実装では書き込みを setmodel の1関数に隔離し、暫定キー
  として `shell/ui_mode`（値 "android" / "winui"、未設定時の
  既定は現行製品既定に合わせて "android"）を使う。
  シェル側の確定結果が来たら、この1箇所を差し替える。
- 切り替えはシェルと全アプリが追随する前提の設計であり、
  設定アプリ単独の見た目変更にはしない。追随の実装は
  シェル側の担当（申し送り、REPORT に記録）。

## 7. 実装・テスト計画

- setmodel: 既存ページ/行/id/書込先は不変。追加は
  (1) サウンドへ mute 行、(2) キーボードへ repeat_source 行、
  (3) PERSONAL ページ（ui_mode / wallpaper / panel）、
  (4) STORAGE ページ（fs 行）。mute はシェルと同一方式
  （reg shell/muted ＋ backend へ実効音量を適用、音量変更で
  自動解除）。wallpaper は色プリセット＋既定（reg_unset）。
  既存値が画像パスの場合は cycle の現在値に「画像」と表示し
  choice_cur=-1（カスタム）として誤上書きを防ぐ。
- bd_settings: UI を全面書き換え。公開 API（bd_settings.h）は
  維持し、page_list / search_field の指す実体が変わることを
  ヘッダ注記に明記する。settings_app_page() は Android
  モードのホームで -1（SETTINGS_PAGE_HOME）を返す。
- テスト: test_settings.c は既存チェック不変＋新規行の
  モデルテストを追加。test_settings_ui.c は新しい操作フロー
  （ホーム→ドリルダウン、検索ジャンプ、両モード構築）に
  合わせて駆動手順を改訂する（チェックの趣旨は維持）。
- 撮影: screenshots ハーネス（shot_apps settings）は編集せず
  そのまま使う。ハーネスが 800 幅のままなら、その旨を
  レポートに明記する。ホーム画面と WinUI モードの撮影は
  ハーネスに無いため、テスト側の描画ダンプで補う。
