# キーボード統合 M2 レポート（物理キーボード本格統合＋OSK v2 残件解消）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


対象: (A) Brainux 物理キーボードの本格統合、(B) OSK v2 の残件
（`osk/REPORT.md` §7）解消。repo へは push していない。

## 1. (A) 物理キーボード

### 1.1 キーボード層 `tk_kbd`（新規、`include/tk_kbd.h` 113 行 / `src/tk_kbd.c` 424 行）

evdev バックエンドからキーボードの意味論を分離し、専用層を新設した。
バックエンド（`src/tk_evdev.c`、173 行に再構成）はデバイス読み取りと
抵抗膜タッチ/ホイールだけを担い、キーイベントは全てこの層を通る。

- **キー定義表**: evdev コード → {種別（TEXT/EDIT/MODIFIER/ACTION/IGNORE）、
  tk_key、tk_action、US/JIS 両レイアウトの無修飾/Shift テキスト、
  リピート可否、表示名} の単一表（約120エントリ）。設定 UI・文書向けに
  `tk_kbd_table_at()` / `tk_kbd_describe()` で参照可能。
- **JIS 配列**（Brain 実キーボード、既定）: JIS 句読点キーを正しく写像
  （@キー、^キー、Shift+2=`"`、Shift+7=`'`、Shift+0=無文字、¥キー、ろキー、
  `:`/`;` の JIS 配置等）。US 配列も同じ表から切替可能。英字は
  Caps Lock と Shift の XOR（PC と同じ規則）。
- **Brain 専用キー → `TK_EV_ACTION`**（`tk.h` に新設したイベント種別と
  `tk_action` 列挙）: ホーム、一括検索、戻る、音量±/ミュート、輝度±、
  電源、スリープ、発音。編集キーとして実在するもの（決定=Enter、
  調べる=Esc、国語辞典ボタン=TAB コード、ページ送り=PageUp/Down）は
  編集キーに写像。KEY_HOME はキャレット用 Home ではなくホーム
  アクションに写像した（Brain のキーボードにキャレット Home キーは
  無く、ホームボタンはデバイスのホームであるため。表のコメントに明記）。
  変換/無変換/カナ切替/半角全角は IME 系アクション
  （CONVERT / NO_CONVERT / KANA_TOGGLE / IME_TOGGLE）に写像。
  F1–F12 は `TK_ACTION_FKEY_BASE+n`（Brain に F 行は無いが PC
  キーボード接続時の受け口）。
- **修飾状態**: Shift/Ctrl/Alt（Brainux 結合: シフト=Shift、
  記号キー位置=Ctrl、文字切替=Alt）を層が保持し、全イベントに付与。
- **リピートは単一ソースで二重化不能**: `TK_KBD_REPEAT_SYNTHETIC`
  （既定）ではカーネル autorepeat（value=2）を破棄し、設定された
  delay/interval で `tk_kbd_poll()` が生成する。
  `TK_KBD_REPEAT_KERNEL` では value=2 を素通しし合成は止まる。
  どちらのモードでも重複は構造的に起きない。リピート対象は
  「最後に押された保持中キー」で、離すと前の保持キーに復帰する。
- **長押し**: `longpress_ms`（既定 600ms）保持で `longpress=true` の
  KEY_DOWN を1回だけ追加発火（電源長押しメニュー等の受け口）。
  リピートは継続する。リピート不可のアクションキーにも効く。
- 時計は呼び出し側供給（`now_ms`）で、層は割当・I/O なし
  （生成後の状態は単一 calloc）。合成 evdev イベントで完全に
  テスト可能。

**未検証の明記**: brain-kbd-i2c ドライバが専用キーに実際に発行する
evdev コードは、ドライバソースを本セッションで確認できなかった
（linux-brain サブモジュール未取得、公開情報にもコード表なし）。
表は「発行しうるコード」を広くカバーし、コード修正が1行で済む
表駆動にした。**統合フェーズでゲスト `evtest` による実コード
照合が必要**（特に音量/輝度/発音/一括検索）。

### 1.2 evdev バックエンド

- キー処理を全て tk_kbd へ移行（旧来の US 前提の簡易表は削除）。
- `tk_evdev_poll_at(e, ms, out)` を追加し、実デバイス読み取りと
  tk_kbd の時間駆動出力（リピート/長押し）を単一ストリームに統合。
  `tk_evdev_poll()` は単調時計でこれを呼ぶだけになった。
- **仮想バックエンド** `tk_evdev_new_virtual()` + `tk_evdev_feed_key()`:
  デバイスなしで本番経路そのものに合成 evdev を流せる（テスト用）。
- 設定適用 API: `tk_evdev_kbd_set_config()` / `tk_evdev_kbd_config()`。

### 1.3 キーボード設定（brainreg＋シェル配線）

- 新規 `shell/include/kbdsettings.h`（46 行）/ `shell/src/kbdsettings.c`
  （78 行）: brainreg セクション `keyboard` に
  `layout`（jis/us）、`repeat_source`（synthetic/kernel）、
  `repeat_delay_ms`、`repeat_interval_ms`、`longpress_ms` を定義。
  読込は既定値→ストア上書き→サニタイズ（範囲外は clamp）、
  保存は reg_set 経由（原子書換え）。リピートプリセット
  （遅い 600/80、標準 400/50、速い 250/30）もここに定義。
- シェル配線（`shell/src/shellui.c` / `shellui.h`）: 起動時に
  `bd_kbd_settings_load()` で読み込み、クイック設定パネルに
  キーボード行を追加（「配列: JIS/US」切替ボタン、「リピート:
  遅い/標準/速い」循環ボタン。ラベルが現在値を表示し、変更は
  即座に reg へ永続化）。公開 API `bd_shell_kbd_config()` /
  `bd_shell_set_kbd_config()` を追加。
- **残り**: セッション常駐ループが `bd_shell_kbd_config()` を読んで
  `tk_evdev_kbd_set_config()` へ適用する部分は、セッション統合
  フェーズの embedder 側作業（現行シェルに常駐ループの実体が
  無いため、配線は「UI→reg→config 構造体」までを実装・検証し、
  適用点は API として明示した）。ACTION イベントのシェル側
  ハンドラ（音量→backend 適用等）もシェル側の残件。

### 1.4 入力経路の統一（テストで証明）

`osk/tests/test_osk.c` にグループ **U** を新設: 合成 evdev
（コード+値+時刻）→ tk_kbd → tk_root → OSK という実経路で
「tenki」入力→変換→Space 候補巡回→Enter 確定までを通し、OSK M1 の
K グループ（tk_event 直接投入）と同一の結果になることを確認。
さらに (i) 合成リピートが全スタックを通って文字を繰り返す、
(ii) ACTION キーが preedit を乱さない、(iii) OSK を介さない
通常フォーカス時も同じ物理経路で JIS テキスト（Shift+2=`"`）が
フィールドに入る、まで検証した。

## 3. テスト結果（全て実測、ホスト）

| スイート | 結果 | 備考 |
|---|---|---|
| test_kbd（新規） | **107 PASS / 0 FAIL** | 表・JIS/US・修飾/Caps・編集キー・アクション・リピート（合成/カーネル/二重化なし）・長押し・仮想バックエンド |
| test_textcore | 97 PASS（+13） | 文節 API 追加分 |
| test_tk2 | 121 PASS（+6） | composition 描画ピクセル検証 |
| test_tk / test_font / test_tk3 | 58 / 22 / 74 PASS | 回帰なし（tk3 は並行作業の M3 分） |
| test_osk | **193 PASS（+53）** | U（統一経路）/ V（プレビュー）/ W（文節）新設、G 更新 |
| test_shell | 72 PASS（+15） | キーボード設定: 既定値・切替クリック・reg 永続化・clamp |
| shell 他6スイート | 143+21+51+25+23+13 PASS | 回帰なし |
| libbrainime session_test | PASS | アダプタ変更の影響なし |

ASan/UBSan（別ツリーコピーで実施）: test_kbd / test_osk /
test_textcore / test_font / test_shell / test_desktop はクリーン。
自作の composition 描画テストブロックも単独でクリーン。

## 4. 行数実測

新規: `include/tk_kbd.h` 113、`src/tk_kbd.c` 424、
`tests/test_kbd.c` 399、`shell/include/kbdsettings.h` 46、
`shell/src/kbdsettings.c` 78。
変更: `src/tk_evdev.c` 168→173（全面再構成）、
`src/tk_osk.c` 1548→1703（+155）、`src/tk_widgets.c`（composition
ヘルパ＋両ペインタ＋text_edit_key、約+110）、`src/textcore.c`
808→865（+57）、`include/tk.h`（TK_EV_ACTION/tk_action/イベント
フィールド、約+45）、`include/osk.h` 203→224、`include/textcore.h`
+15、`osk/osk_brainime.c` +6、`shell/src/shellui.c`（約+110）、
`shell/include/shellui.h` +9、テスト追加分（test_osk 約+230、
test_tk2 +54、test_textcore +31、test_shell +40）。

## 5. 未実装・残り（正直な一覧）

- **専用キーの実 evdev コード照合**（§1.1）: 統合フェーズで
  ゲスト `evtest` が必要。表駆動なので修正は1行単位。
- **設定のライブ適用**: シェル→reg→config までは実装・検証済み。
  常駐セッションが evdev へ適用する embedder 配線は統合フェーズ。
- **ACTION のシェル側処理**: 音量/輝度アクションを受けて backend に
  適用するハンドラはシェル側の残件（イベント発火までは検証済み）。
- **IME アクションの消費**: IME_TOGGLE/CONVERT 等を受ける側
  （OSK の表示切替など）は未配線。OSK は現行どおり Space/変換
  キーで動作する。
- **TextView の composition**: キャレット行内のみ描画（折り返しを
  跨ぐ composition は未対応。現行の表示位置づけ自体は従来と同じ）。
- **50音ページ切替の発見性**: ラベルは移動先表示のみ。ページ
  インジケータ（1/2 表示）は未実装。
- **ゲスト/実機検証**: 本 M2 はホスト単体テストまで。SD イメージ＋
  QEMU ゲストでの実キー入力検証は統合フェーズの合格条件に従う。
- 変換品質は libbrainime の水準のまま（本作業は経路と表示の実装）。
