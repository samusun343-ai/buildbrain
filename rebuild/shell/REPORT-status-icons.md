# REPORT-status-icons — ステータスアイコン全面見直し＋シェルのモード追随＋Android 3ボタン ナビ

作成: 2026-11-21。範囲: `shell/` 配下（src / include / tests / greeter /
braindesk2.c）。ライブラリ本体（`../include`・`../src`）は別ライン作業中のため
編集していない。repo へ commit / push なし。

担当範囲（ユーザー指示 2026-11-21、3件）:
1. ステータス系アイコン（電池・音量・Wi-Fi 等）の全面見直し
2. シェル（ホーム画面）の WinUI モード対応とモード決定キーの正本化
3. Android モードの3ボタン ナビゲーション（タブレット/Chromebook 形、機能込み）

## 0. 途中経過

- 2026-11-21: 棚卸し・状態ソース確認・一次ソース調査・モードキー契約の確定まで
  記録（§1〜§4）。実装は §5 以降に段階記録する。
- 2026-11-21: 全件完了。ステータスアイコン（状態経路＋ベクター素材
  レンダリング方式、§5・§8）、シェルの WinUI モード（§6）、Android
  3ボタン ナビ（§7）を実装し、全テスト 0 failed（§9.1）と状態別
  撮影11枚の目視確認（§9.3）で検証した。撮影中に wifi.c の実バグ
  （ローカルソケットパスの衝突）を発見・修正した（§9.2）。

## 1. 棚卸し: コードで手描きしているグリフ的全数

全て `shell/src/shellui.c` の 40x40 キャンバス（中央 24px にグリフ）に
プログラム描画。色は `GLYPH_INK`（RGB 24,24,28）と `GLYPH_DIM`（138,138,146）の
ハードコードだった（テーマ非追随 — §5 で是正）。

| # | グリフ | 場所 | 旧来の描き方 | 状態連動 |
|---|--------|------|--------------|----------|
| 1 | glyph_wifi | トレイ、QS タイル | 弧3本＋点。引数は bool（無線 on/off の reg 値）だけ | 接続状態・強度は**無反映**。未接続でも「接続中」と同じ顔 |
| 2 | glyph_volume | トレイ、QS 行 | スピーカー＋弧（level>0 で1本、>50 で2本）、0 で × | トレイは実効レベル連動。**QS 行は `glyph_volume(qs_vol_px, 60)` 固定で生成時のまま一切更新されない**（欠陥） |
| 3 | glyph_battery | トレイ | 枠＋充填（残量比例）、≤20% で赤、充電中は右上に「＋」バッジ | 残量・充電は連動するが、充電印が「＋」で一般的でない。％は**別ウィジェットのラベル**（`batt_pct_label`）で表示し、充電時はラベル側にも「+」を付けていた |
| 4 | glyph_brightness | QS 行 | 太陽（中心 disc＋8本の光線） | 静的でよい（Fluent Brightness / Material の太陽形と整合）。色のみ是正対象 |
| 5 | glyph_person | ランチャーフッター | トーン disc＋頭と肩のシルエット | 静的（アバター既定形として一般的）。色はテーマ参照済み |
| 6 | glyph_grid | ランチャーボタン | 3x3 セル、中央はアクセント | 静的。アプリ一覧の印として一般的 |
| 7 | placeholder_icon | デスク/棚/歯車のフォールバック | 窓ピクトグラムのタイル | 静的。汎用プレースホルダとして許容 |
| 8 | fab_glyph（assist.c） | 1本指アシストのフローティングボタン | アクセントの輪＋中心点 | 静的。アシスト系の慣用形 |

グリーター（greeter/braingreeter2.c）に手描きグリフは無い（ボタンは文字のみ）。
アプリ側（apps/）は棚卸しのみ: 同パターンの手描きグリフ関数は検出されなかった
（アイコンは Papirus 系ファイル＋キャッシュ経由）。設定アプリは別ラインが
作り直し中のため変更しない。

## 2. 状態ソース（実コードで確認）

- 電池: `bd_backend_battery()` → `bd_battery_info{present, capacity 0..100
  （-1=不明）, charging, status 文字列}`（sysfs power_supply）。シェルは
  `bd_shell_set_battery(pct, charging)` / `bd_shell_poll_backend()` で受ける。
- 音量: `bd_shell.volume`（0..100、reg `shell/volume`）と独立 bool
  `bd_shell.muted`（reg `shell/muted`、REPORT-mute.md の意味論: 実効音量 =
  muted ? 0 : volume、ミュート中の音量変更は解除）。
- Wi-Fi 接続: `bd_wifi_get_status()` → `bd_wifi_status{connected, ssid, bssid,
  state, ip}`。**強度は STATUS に含まれない**。スキャン結果
  （`bd_wifi_ap.signal_dbm`）には AP 毎の dBm があるが、接続中 AP の現在強度を
  引く経路は無かった → §5 で wpa_supplicant の `SIGNAL_POLL` を追加する
  （無い情報を推測で描かないための実測経路）。
- 時計: 文字列で受ける（`bd_shell_set_clock`）。アイコンではない。

## 3. 調査（一次ソース、取得日 2026-11-21）

### Android（AOSP SystemUI）
- 電池: `BatteryMeterView` はアイコン＋％テキストの**複合ビュー**（LinearLayout
  に ImageView と TextView）。形は `BatteryMeterDrawable`/
  `AccessorizedBatteryDrawable` が描く: 枠＋残量比例の充填、充電中は
  **稲妻（bolt）パス**を重ねる（残量 30% 未満では bolt を不透明にする
  BOLT_LEVEL_THRESHOLD=0.3 の規定あり）。
  出典: https://android.googlesource.com/platform/frameworks/base/+/5a5f77afb0af20b7dd2f08cc22bb9348548a900a/packages/SystemUI/src/com/android/systemui/battery/BatteryMeterView.java
- Wi-Fi: ステータスバーは `ic_wifi_signal_0`〜`ic_wifi_signal_4` の
  **5段階**（扇の充填が強度で変わる、0 が最弱・4 が最強）。
  出典: https://source.android.com/docs/automotive/hmi/status_bar?hl=en&authuser=6
- 音量: Material の慣用形はスピーカー＋波（volume_up=波2本、volume_down=波1本、
  volume_mute/volume_off=波なし/×）。トレイの段階表現はこの形に従う。

### Windows（Microsoft 公式）
- Segoe Fluent Icons の実在グリフとコードポイント（Microsoft Learn）:
  Wifi = U+E701、Wifi1/2/3 = U+E872/E873/E874（扇の段階）、
  Battery0〜Battery10 = U+E850〜U+E85A（**10段階**の充填）、
  BatteryCharging0〜9（稲妻つき充電形）、Volume = U+E767、
  Mute = U+E74F、VolumeDisabled = U+EA85、Brightness = U+E706。
  出典: https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
- タスクバー/スタート/QS の構成（Microsoft Support）: タスクバーは既定で
  **中央揃え**（Start・検索・タスクビュー・アプリが中央、システムトレイが右端）。
  トレイの Wi-Fi/スピーカー/電池クラスタのクリックで**クイック設定**が開く。
  時計のクリックで通知センター。スタートは上部に検索、その下に
  「ピン留め」グリッド、「おすすめ」、最下段にアカウント（左）と電源（右）。
  出典: https://support.microsoft.com/en-au/windows/experience/personalization/customize-the-taskbar-in-windows

### Android タブレット / Chromebook のナビゲーション
- Pixel Tablet の3ボタンモード: 下部タスクバーが常駐し、3ボタンは
  **Back（左・三角）— Home（中央・丸）— Recents（右・四角）** の順。
  アプリ内でもバーは消えない。出典:
  https://9to5google.com/2023/06/30/constant-taskbar-pixel-fold-tablet-android/ 、
  Google 公式（既定レイアウト Back—Home—Recents、入替可）:
  https://support.google.com/accessibility/thread/456165485/how-to-customize-your-android-3-button-navigation-bar?hl=ru
- Android 12L: タブレット向けタスクバーにナビを統合（タスクバーは inset として
  アプリへ報告される）。出典:
  https://developer.android.com/about/versions/12/12L/summary?authuser=217
- ChromeOS タブレットモードのボタンナビ（Google 公式）: シェルフ下部に
  ボタンが出る。**Back と Home（ランチャー）は左下、「すべてのウィンドウを表示」
  （Overview）は右下**。Chromium の ash 側 pref 名にも
  "shelf navigation buttons (home, back and overview button)" とある。
  出典: https://support.google.com/chromebook/answer/9739838?hl=en 、
  https://chromium.googlesource.com/chromium/src/+/refs/tags/110.0.5466.0/ash/constants/ash_pref_names.cc

## 4. モード決定の正本（キー契約）— 本ラインで確定

現状確認（実コード）: モード切替の永続設定は**存在しなかった**。全製品
エントリポイント（braindesk2、braingreeter2、各アプリ main）が
`tk_theme_set_default(&tk_theme_v2)` をハードコードし、WinUI テーマ
（`tk_theme_winui` / `tk_theme_winui_dark`、style=2）はテストと撮影でのみ
選択されていた。シェルの `style_v2()` は style==1 のみを見るため、WinUI
テーマ下ではシェルが classic 幾何に落ちる混在状態になる（§6 で是正）。

確定する契約（設定アプリ担当ラインはこのキーへ書く側）:

| 項目 | 値 |
|------|-----|
| 名前空間 | `ui`（brainreg） |
| キー | `mode` |
| 値 | `android`（既定）/ `winui` |
| 意味 | android → `tk_theme_v2` 系、winui → `tk_theme_winui` 系 |
| 補助キー | `ui/dark` = `true`/`false`（既定 false）。true で各モードの dark 変種（`tk_theme_v2_dark` / `tk_theme_winui_dark`） |
| 反映タイミング | **次回ログイン（セッション開始）時**。各プロセスは起動時に一度だけテーマを確定する（`tk_theme_set_default` はプロセス全体の既定であり、ウィジェット木は構築時にテーマを参照するため、実行中の即時切替は行わない）。セッション（braindesk2）とグリーター（braingreeter2）は起動時にこのキーを読む |
| 読む側 | シェルは本ラインで実装（`bd_theme_from_reg()`、`shell/src/thememode.c`。braindesk2 と braingreeter2 が起動時に呼ぶ）。アプリ群は GUI ライブラリ完成後の別ラインで同じキーを読む（ユーザー指示の順序） |

## 5. 実装: ステータスアイコン（段階記録）

### 5.1 状態経路の新設（第1段階）

- Wi-Fi 強度: `bd_wifi_signal_dbm()` を新設（wpa_supplicant の
  SIGNAL_POLL の RSSI を返す。未接続・取得失敗は -1）。
  `shell/include/wifi.h` / `shell/src/wifi.c`。テスト用 fake に
  SIGNAL_POLL 応答（既定 -42、`FAKE_WPA_RSSI` で上書き可）と
  `FAKE_WPA_STATE=DISCONNECTED` を追加。
- シェル側: `bd_shell_poll_wifi()` が STATUS poll の周期に相乗りして
  SIGNAL_POLL を取得し `bd_shell.wifi_signal_dbm`（0=未知）に保持。
  アクセサ `bd_shell_wifi_signal_dbm()` を公開。
- QS 音量アイコンの固定表示バグ（§1）を是正: トレイと QS 行の両方を
  `volume_icon_update()` で実効レベルから更新する構造に統一。
- 電池の一体化: 別ラベル `batt_pct_label` を廃止し、電池ボタン自体を
  アイコン＋％ラベルの複合要素化（`TK_BTN_ICON_ROW` でアイコン左＋
  ％右。縦積みにならないよう明示指定）。充電の「+」表記は廃止。

### 5.2 描画方式の転換（第2段階、§8 の方針厳格化を受けて）

第1段階ではツールキットの描画プリミティブで形を作ったが、ユーザー
指示「コードで描いたラスターグリフは不可」を受けて全廃し、§8 の
ベクター素材レンダリング方式に置き換えた。現在の実装は §8 が正。

## 6. 実装: シェルの WinUI モード（段階記録）

- `shell/include/thememode.h` / `shell/src/thememode.c`:
  `bd_theme_from_reg()` が §4 のキー契約（`ui/mode`、`ui/dark`）を
  読んで4テーマから選択。不明値は android にフォールバック。
  braindesk2 / braingreeter2 のハードコードを置換（グリーターは
  libbdshell をリンクしないため Makefile のリンク行に
  thememode.c + reg.c を最小追加。test_greeter_font のリンク行にも
  同じ欠落があり併せて是正）。
- shellui に `style_winui()`（style==2）を追加し、WinUI 幾何を実装:
  (a) 棚はスペーサー2つで Start＋タスク群を中央揃え（タスクバー
  既定）、(b) ランチャーボタンは Windows ロゴ（§8）、(c) ランチャーは
  中央配置・カテゴリチップ行は非表示（スタートに相当する構成）、
  (d) 通知シートは右端アンカー（通知センター形）、(e) QS タイルの
  角丸と container 面は WinUI テーマ値に従う。
- Android モード側の現行形は不変（style_v2 の分岐は従来どおり）。

## 7. 実装: Android 3ボタン ナビ（段階記録）

- 配置: 棚バーのタスク群とトレイの間（タブレット/Chromebook 側の
  形。§3 の調査どおり Back=左三角、Home=丸、Recents=四角。アイコン
  自体は AOSP SystemUI の実ベクターを使用、§8）。表示は Android
  モード（style==1）のみ。WinUI モードには混ざらない。
- 経路: `bd_shell_nav(action)`（shellui）→ nav sink（session.c が
  実装）。Back はシェルのパネルが先に消費（recents＞QS＞通知
  シート＞ランチャーの順。Android の dismiss 順）。何も開いて
  いなければ sink 経由で `bd_wm_server_key()` によりフォーカス中
  ウィンドウへ TK_KEY_BACK の down/up を配送（アプリ側の対応は
  申し送り、§10）。
- Home: 全パネルを閉じてデスクトップ表示＋sink 経由で全ウィンドウ
  を最小化（`bd_wm_server_set_state(BD_WIN_MINIMIZED)`）。
- アプリスイッチャー: `bd_shell_win_ops`（list/activate/close/
  thumbnail のウィンドウブリッジ）を新設し、session.c が WM
  サーバで実装。パネルは新しい順（z-order の逆）に最大6枚の
  カード（サムネイル 184x104＋タイトル＋×）。サムネイルは WM に
  新設した `bd_wm_server_thumbnail()` がコミット済みフレームを
  nearest 縮小する（開いた時だけの意図的コピー）。
- カードの × は即時再構築せず `recents_dirty` フラグで次回
  レイアウトへ遅延（クリック配送中の自己解放＝use-after-free の
  回避。テストで実際に踏んで是正した）。

## 8. アイコン素材と描画方式（ユーザー指示 2026-11-21 厳格化への対応）

方針: コード描画グリフは全廃。確立したベクターセットから
179ppi 実表示で荒れない解像度で AA レンダリングし、モード内の
様式（線の太さ・光学サイズ 24px ボックス）を統一する。

### 8.1 パイプライン

- `shell/tools/render_icons.sh`: 素材 SVG を取得し、白形状に
  正規化（fill を白へ。実行時にテーマ色で着色するため）して
  librsvg（ffmpeg 経由）で **96x96 PNG** にレンダリング。
  成果物は `shell/assets/status/{material,fluent,aosp}/*.png`、
  素材 SVG は `shell/assets/svg-src/` に出典として保持。
  AOSP の Android ベクター XML は `shell/tools/aosp2svg.py` で
  SVG 化（pathData は SVG パスと同文法）。
- 実行時: `bd_icon_pixels()`（shellui.c、assist.c からも使用）が
  PNG を読み、テーマ色（ink/dim/danger/accent）に着色し、
  `tk_image_load_thumbnail` の面積平均縮小で表示ボックス
  （原則 24px）ちょうどに縮小する。ボタンの blit はニアレスト
  のため、縮小を必ずローダ側で行うのが品質の要点。
- アイコン探索パス: 環境変数 `BD_SHELL_ASSETS`、`assets`、
  `../assets`、`shell/assets`、`/usr/share/braindesk/assets`。
  見つからない場合はアイコン無し表示に落とす（描画グリフへの
  フォールバックはしない）。
- 状態連動アイコンは (set, 名前, サイズ, 色) をキーに再読込を
  抑制（シェル構造体にキー保持）。poll 周期で無駄な再デコードは
  しない。

### 8.2 アイコンごとの素材対応表

Android モード = Google Material Symbols（Apache-2.0、
`symbols/web/<name>/materialsymbolsoutlined/<name>_fill1_24px.svg`）:

| 用途 | 素材 | 状態対応 |
|------|------|----------|
| 電池 | battery_android_0..6, battery_android_full | 15%刻み7段階＋満充電。≦20% は danger 着色 |
| 電池（充電中） | battery_android_bolt | 稲妻は形状内の切り抜き |
| 電池（不明） | battery_android_frame_full | dim 着色＋ラベル「--%」 |
| Wi-Fi | network_wifi_1_bar / 2_bar / 3_bar / network_wifi | 強度4段階（dBm 閾値 ≧-60/≧-70/≧-80） |
| Wi-Fi（未接続） | signal_wifi_0_bar | dim 着色（空の扇） |
| Wi-Fi（off） | wifi_off | dim 着色 |
| 音量 | volume_off / volume_down / volume_up | ミュート（斜線入り）/ ≦50 / >50 |
| 明るさ | brightness_6 | QS 行 |
| アカウント | account_circle | ランチャーフッター（32px） |
| ランチャー | apps | 3x3 ドット |
| 設定 | settings | 棚バーの設定ボタン |
| 補助 FAB | pan_tool | accent 着色（28px） |
| プレースホルダ | image | dim 着色 |

WinUI モード = Microsoft Fluent UI System Icons **regular**
（MIT、`assets/<Dir>/SVG/ic_fluent_<name>_24_regular.svg`）:

| 用途 | 素材 | 状態対応 |
|------|------|----------|
| 電池 | battery_0..10 | 10%刻み11段階。≦20% は danger 着色 |
| 電池（充電中） | battery_charge | 枠＋稲妻 |
| 電池（不明） | battery_0 | dim 着色＋「--%」 |
| Wi-Fi | wifi_1 / wifi_2 / wifi_3 / wifi_4 | **番号は上から数える**（wifi_1=満扇、wifi_4=点のみ。下記 8.3） |
| Wi-Fi（未接続） | wifi_warning | dim 着色 |
| Wi-Fi（off） | wifi_off | dim 着色 |
| 音量 | speaker_mute / speaker_1 / speaker_2 | ミュート（×印、Windows トレイと同形）/ ≦50 / >50 |
| 明るさ | weather_sunny | QS 行 |
| アカウント | person | ランチャーフッター（32px） |
| スタート | start（Windows ロゴ、パブリックドメイン、Wikimedia Commons「Windows logo - 2021.svg」を単色化） | accent 着色 |
| 設定 | settings | 棚バーの設定ボタン |
| 補助 FAB | hand_right | accent 着色（28px） |
| プレースホルダ | image | dim 着色 |

ナビ3ボタン = AOSP SystemUI 実ベクター（Apache-2.0、
`packages/SystemUI/res/drawable/ic_sysbar_{back,home,recent}.xml`）。
3つとも塗りシルエットが原典どおり（輪郭線ではない）。

### 8.3 実測で確定した素材側の注意（記憶ではなくレンダリング実測）

- **Fluent の filled 電池は採用しなかった**: filled 版は「実体＋
  窓がレベルとともに広がる」マスク的構造で、単体グリフとして
  読むと充填が逆転する（battery_0 が全面塗り、battery_10 が
  最も空）。中行の不透明ピクセル実測とパス解析で確認。regular
  版は通常の読み（左から充填）だったため、WinUI のトレイ全体を
  regular で統一した（線様式の統一も兼ねる）。
- **Fluent の Wi-Fi 番号は逆**: wifi_1 が満扇、wifi_4 が点のみ
  （帯域別不透明ピクセル数で実測）。写像はレンダリング結果に
  基づいて逆順にした。
- 棚バーの設定ボタンは従来アイコンキャッシュの多色アプリアイコン
  （歯車＋電源風の緑アイコン）を流用しており、単色シンボル群の
  中で浮き、意味も判読できなかった（親経由のユーザー指摘）。
  モードセットの settings（単色歯車）に差し替え済み。アプリ
  グリッド内の設定アプリアイコンは「アプリ自身のアイコン」と
  して正しい文脈なので変更していない。
- 充電状態の表示形（実機制約）: Brainux Wiki のハードウェア
  対応表（2021-02 時点）では電源コントローラ未対応で「充電
  状態の表示など一切不可」と明記されている（出典:
  `~/workspace/buildbrain-work/RESEARCH-brain-modding.md` §3）。
  本実装はバックエンドが charging=true を返した時だけ稲妻を
  出し、取れなければ通常の段階アイコン（稲妻なし）になる。
  捏造はしない。容量自体が取れない場合は空枠＋「--%」。
  現行カーネルで改善しているかは統合フェーズの実機確認項目。

### 8.4 棚バーの選択表示（申し送りへの対応）

ユーザー認定（2026-11-21）: 選択インジケーターは「アイコンだけを
包むピル、ラベルは外」が原則。シェル側の棚バーでは、タスク
ボタンがアイコンのみ（ラベルなし）で、アクティブ表示はその
ボタン面（surface_alt）のみ＝構造的に原則と一致している。
ナビ3ボタンに選択状態はない。確定寸法（64x32dp ピル）は
ライブラリ側ラインの是正完了報告に合わせて後日突き合わせる
（§10）。

## 9. 検証と撮影

### 9.1 テスト（`make check`、全スイート 0 failed）

| スイート | 件数 | 備考 |
|----------|------|------|
| test_desktop | 182 | |
| test_shell | 126 | +27: 電池の一体ラベル、Wi-Fi 統合（fake 介して接続＋-42dBm、未接続＋信号なし）、ナビのパネル消費順、fake ブリッジでの recents 構築・活性化・終了 |
| test_iconcache | 35 | |
| test_wm | 94 | +4: `bd_wm_server_thumbnail`（実クライアントのコミット済みフレームの色・未知 id の拒否） |
| test_backend | 25 | |
| test_assist | 24 | |
| test_greeter_auth | 13 | |
| test_greeter_font | 6 | リンク行の thememode/reg 欠落を是正 |
| test_wifi | 48 | +5（SIGNAL_POLL 含む）＋2クライアント共存の回帰（下記 9.2） |
| test_session | 163 | +12: 実クライアント相手のナビ実配送（Back が TK_KEY_BACK として届く、Home で最小化、Recents の開閉と Back 消費） |
| test_mascot | 95 | |
| test_notify | 46 | |

### 9.2 副産物: wifi.c の実バグ修正

撮影ハーネスが supplicant を2つ（強/弱）使ったことで露見:
`bd_wifi` のローカルソケットパスが `bdwifi-<pid>` で同一
プロセス内の全クライアントで衝突し、片方の bind が他方の
応答を奪っていた（実機はクライアント1個のため未発現）。
オブジェクトごとの連番を付けて一意化（`bdwifi-<pid>-<n>`）。
修正前: 2クライアント目以降の STATUS がタイムアウトし
「未接続」に化けた（ハーネス所要 37 秒）。修正後: 両クライアント
共存を再現プログラムと test_wifi の回帰テストで確認、ハーネス
所要 0.5 秒。

### 9.3 撮影（`shell/shots/`、幅 800 のまま — 854 化は M22 の担当）

撮影ハーネスは `shell/tests/shot_status.c`（独立ハーネス、
screenshots/Makefile は未編集）。状態を変えた11枚＋トレイ拡大
（x3 ニアレスト、実ピクセル確認用）を生成し、全て目視確認した:

- st_a_home_low: 電池 12%（赤・ほぼ空）＋ミュート（斜線）＋Wi-Fi 満扇
- st_a_home_charge: 電池 72% 充電中（稲妻の切り抜き）＋弱 Wi-Fi（1段）
- st_a_home_full: 電池 100%＋音量最大＋Wi-Fi 未接続（空の扇）
- st_a_home_off: Wi-Fi off（斜線、dim）＋電池 45%
- st_a_qs: QS タイルに SSID「TestNet」＋満扇、明るさ/音量行アイコン連動
- st_a_recents: カード2枚（新しい順、フォーカス強調、サムネイル、×）
- st_a_launcher / st_w_launcher: 両モードのランチャー（WinUI は
  中央配置・スタート構成・チップなし）
- st_w_home: Fluent 満扇＋speaker_1＋電池 55%（6段ブロック）＋単色設定ギア
- st_w_home_charge_mute: 充電（枠＋稲妻）＋ミュート（×）＋最弱 Wi-Fi（点）
- st_w_qs: WinUI の QS（右端アンカー）

### 9.4 恒常原則への対応

- コピー/オーバーヘッド: アイコン再読込はキー変化時のみ。
  SIGNAL_POLL は既存 poll に相乗り。サムネイル生成はスイッチャーを
  開いた時だけ（毎フレームではない）。
- キャッシュ/メモリ: アイコンは 96px PNG のデコード＋24px 化が
  状態変化時のみ。恒常保持はボタン内コピー（24x24x4=2.3KB/個）
  のみ。新規の無制限キャッシュなし。
- 実機の現実: モードキーは brainreg（原子的書換えの既存基盤）。
  アイコン素材は SD 側アセット（eMMC ノータッチ）。日数ベース
  期限の要素なし。

## 10. 申し送り

- アプリ群のモード追随（`ui/mode` を読む）は GUI ライブラリ完成後の別ライン。
- アプリ側の「戻る」対応（TK_KEY_BACK を受けて画面遷移する）はアプリ両モード
  対応ラインの範囲。本ラインは経路（シェル→WM→フォーカス中ウィンドウへ
  TK_KEY_BACK 配送）までを実装した。
- アイコンアセットの配置: 統合フェーズのイメージでは
  `shell/assets/status/` を `/usr/share/braindesk/assets/status/` へ
  収めるか、`BD_SHELL_ASSETS` を設定すること（未配置だとアイコン
  無し表示になる。描画フォールバックは意図的に無い）。
- 選択インジケーターの確定寸法（64x32dp ピル）はライブラリ側
  ラインの是正完了報告が出たら棚バー側と突き合わせる。
- 実機確認項目（統合フェーズ）: 現行カーネルで充電状態
  （/sys/class/power_supply の status）が取れるか。取れなければ
  稲妻なし表示が実機の正しい形になる（§8.3）。
- WM ウィンドウはシェル UI の上に合成されるため、スイッチャーも
  ランチャー/QS と同じ重なり条件になる（本作業で導入した制約
  ではない）。
