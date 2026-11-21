# REPORT-mediakeys — 記号レイヤのメディアキーを tk_kbd で写像

作成: 2026-11-21（OS 内部 第1弾 §1 の副次的発見への対応）
範囲: `src/tk_kbd.c`、`include/tk_kbd.h`（コメントのみ）、
`tests/test_kbd.c`、`KEYBOARD-FIX.md`（追記）。commit / push なし。

## 1. 前提（一次資料で確定）

pin コミット（11c11c922）の DTS
`buildbrain/linux-brain/arch/arm/boot/dts/imx28-pwsh6.dts` を
直接読み、`keymap-symbol` のメディア割当を確定した
（コミット b0ac9f045、DTS 自身のコメント「Media controls on
the (otherwise unused) symbol-layer arrow keys」付き）。
推測で補った項目は無い。全リストは以下の5件のみ:

| 接点 (row, col) | 物理キー | 記号レイヤのコード | 基本レイヤのコード |
|---|---|---|---|
| (2,3) | Left | KEY_VOLUMEDOWN | KEY_LEFT |
| (4,3) | Right | KEY_VOLUMEUP | KEY_RIGHT |
| (3,3) | Up | KEY_BRIGHTNESSUP | KEY_UP |
| (1,3) | Down | KEY_BRIGHTNESSDOWN | KEY_DOWN |
| (6,3) | Space | KEY_MUTE | KEY_SPACE |

メディアコードは記号キー押下中にドライバが出すコードのみで、
基本レイヤでは一切現れない。従来 tk_kbd はこの5コードを
TK_KBD_KIND_IGNORE で破棄していたため、記号+矢印の音量/輝度
操作は現行スタックで一切効かなかった（REPORT-os-internals-1.md
§1 の記録どおり）。

## 2. 変更内容

### src/tk_kbd.c
- キー定義表で5コードを IGNORE 節から actions 節へ移動し、
  `A()`（ACTION）エントリとして既存の受け口へ接続:
  - KEY_VOLUMEUP → TK_ACTION_VOLUME_UP
  - KEY_VOLUMEDOWN → TK_ACTION_VOLUME_DOWN
  - KEY_MUTE → TK_ACTION_MUTE
  - KEY_BRIGHTNESSUP → TK_ACTION_BRIGHTNESS_UP
  - KEY_BRIGHTNESSDOWN → TK_ACTION_BRIGHTNESS_DOWN
- 受け口（`include/tk.h` の tk_action）は5件全て既存だったため、
  **新設した ACTION は無い**。未対応として残した項目も無い。
- name 文字列を実態に更新（例: `"KEY_VOLUMEUP (Brain:
  symbol+Right key)"`。従来は「no volume key」等の旧状態の記述）。
- ファイル冒頭コメントに記号レイヤのメディア割当を追記。

### include/tk_kbd.h（コメントのみ、API 変更なし）
- 「このハードウェアに無いキー」の列挙からメディアキーを外し、
  記号レイヤに実在する旨の注記を追加。
- 「KEY_POWER is the one ACTION」の記述を実態に修正。

### tests/test_kbd.c
- 表イントロスペクション: 「volumeup は IGNORE」の期待を廃し、
  5コード全ての describe＋種別=ACTION の確認に置換（10件）。
- 旧 group A の「volumeup を押しても無音」の期待を削除
  （写像後はイベントが出るのが正しい）。
- 新規グループ MD（33件）:
  - 5コードそれぞれ: 押下で TK_EV_ACTION が出る、action が
    対応する TK_ACTION_*、離すと無音（押下のみ）。
  - 基本レイヤ非干渉: 同じ物理キーの素コード（KEY_RIGHT /
    LEFT / UP / DOWN / SPACE）は従来どおり矢印/Space の
    フェイスバリューで、ACTION にならない。
  - ACTION の意味論: 長押ししても poll() からリピートは出ず、
    カーネル autorepeat（value=2）も従来どおり破棄される。

### KEYBOARD-FIX.md
- D5 のセルに注記を追加（「発行されない」は基本レイヤ前提で、
  記号レイヤの割当は後に判明・写像済み）。
- §10 として本対応の追記を追加。

## 3. 検証（全て実測）

- `test_kbd`: **179 passed, 0 failed**（ベースライン 139 → +40。
  内訳: 表 +8、group A −1、MD +33）。
- ツールキット全体 `make check`: **3,617 件・全 PASS・回帰ゼロ**
  （ベースライン 3,577 + 40）。内訳は従来どおり
  textcore 118 / tk 83 / tk2 121 / font 22 / test_kbd 179 /
  tk3 191 / tk4 119 / tk5 127 / tk6 205 / tk7 246 / tk8 71 /
  tk9 150 / tk10 128 / tk11 215 / tk12 213 / tk13 770 /
  tk14 335 / tk15 89 / tk16 77 / tk17 148 / tk18 10、
  sdk-check は SDK-OK。

## 4. 「効く」範囲の正確な線引き

- 本対応で裏付けが取れたのは **tk_kbd 層の写像**まで:
  合成 evdev 入力に対し、記号レイヤのメディアコードが正しい
  TK_ACTION_* の TK_EV_ACTION になること（テストで実証）。
- 消費側の現状（コードを読んで確認）:
  - 音量±・輝度±の4件は、シェルの `session_action()`
    （shell/src/session.c）が既に受け口を持ち、
    bd_shell_set_volume / bd_shell_set_brightness（±10）へ
    繋がっている。つまりこの4件は、従来は到達不能だった
    既存の受け手へ今回の写像で初めて届くようになった。
  - **ミュートは消費側がまだ無い**: session_action に
    TK_ACTION_MUTE の分岐が無く、bd_shell にも mute API が
    無い（shellui.c に muted 表示のコメントのみ）。tk_kbd は
    イベントを出すが、現行セッションでは無視される。
    シェル側にミュート切替（音量の保存/復元ポリシー含む）を
    実装するのは本タスク範囲外の別件として残す。
- 実機/ゲストでの実イベント確認（記号+矢印を実際に押して
  evdev コードと画面の音量/輝度変化を見る）は統合フェーズの
  evtest 照合項目であり、本対応では未検証。

## 5. 意味論上の注意（仕様として明記）

- ACTION エントリは KEY_POWER と同じ「押下のみ・リピートなし」
  がこの層の既定意味論のため、記号+Right の長押しで音量が
  連続変化することはない（1押下=1段）。長押し通知自体は
  ACTION にも出る（longpress フラグ付き TK_EV_ACTION。
  機構は種別共通で、power キーで検証済み — メディアコード
  個別の長押しテストは追加していない）。連続調整が欲しければ
  シェル側ポリシーか、ACTION のリピート意味論の変更が別途
  必要で、本対応では変更していない。

## 6. イメージへの反映

- 本対応はソースとテストまで。実機/SD イメージへ反映するには
  統合フェーズ準備ライン側での再ビルド（libbrainui2 の
  ARM クロスビルドとイメージ再組立て）が必要。
