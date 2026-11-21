# OS 内部フェーズ 第1弾 レポート

日付: 2026-11-21
範囲: 実装2件（DTS キー解消 / マスコット設定行の配線）＋調査2件
（通知・アラーム連携 / ライブラリ統廃合の棚卸し）。
commit / push はしていない。変更箇所は本レポートに全て明記する。

---

## 0. 調査で確定した前提（実装前の実測）

### 0.1 DTS の所在と現行状態

- buildbrain repo（`~/workspace/buildbrain-work/buildbrain/`）の
  カーネルは submodule `linux-brain`
  （`samusun343-ai/linux-brain`、pin = `11c11c922fa4f11448745fc5a1236292346d0f09`）。
  作業ツリーでは未初期化（空）だったため、本弾で
  `git submodule update --init` により pin コミットの内容を展開した
  （作業ツリーへの変更のみ、commit ではない）。
- pin コミットの `arch/arm/boot/dts/imx28-pwsh6.dts` を直接確認した結果:
  - **戻る (0,3) は既に `<0 3 KEY_BACK>` で有効化済み**、
    (0,6) も `<0 6 KEY_HOME>` で有効化済み。
    これはコミット `b0ac9f045`「arm: dts: pwsh6: assign Modoru/Home
    and symbol-layer media keys」によるもので、同コミットが
    pin コミットの祖先であることを `git merge-base --is-ancestor`
    で確認した。keyboard-research.md / KEYBOARD-FIX.md §5 の記述
    （「現行 DTS でコメントアウト」）は、それより古い状態
    （配布イメージの DTB 等）を指していたものとみられる。
  - **ページ送り（下）(3,4) は pin コミットでも
    `/*<3 4 PageDown> */` のまま未割当**。← 本弾の実装対象はこれ。
- 同フォーク他機種の先例（一次資料として同 repo の DTS を確認）:
  - imx28-pwsh7.dts: SH6 と全く同じ状態（(3,2)=KEY_PAGEDOWN、
    (3,4) はコメントアウト）。
  - imx28-pwsh1/pwsh2/pwsh3.dts: Page Down キー位置に
    `/* <… KEY_PAGEDOWN>, */ /* Page Down */` とあり、歴史的には
    KEY_PAGEDOWN が意図されていたが、全機種で有効化されたことはない。
    なお全機種で My辞書は別途 KEY_PAGEDOWN を持つ。
- Brainux Wiki「初めての方へ」（https://wiki.brainux.org/beginners/get-started/、
  2026-11-21 取得）: Sx5〜Sx7 の特殊キー対応表は Shift / Ctrl
  （=ページアップ）/ Alt（=音声）/ 記号 のみで、**ページ送り（下）
  に対する定義は存在しない**。よって割当コードは Wiki からは
  決まらず、KEYBOARD-FIX.md §5-2 のプロジェクト判断
  （KEY_PAGEDOWN は My辞書 (3,2) と衝突するため別コードで検討）
  と Linux 入力コードの意味論から決める。
- ドライバ（`drivers/input/keyboard/brain-kbd-gpio.c`）の意味論を確認:
  - 記号キー押下中は `keymap-symbol` のみが使われ、エントリが無い
    セルは **無言で捨てられる**（基本 keymap へのフォールバックなし）。
    特殊キーは基本・記号の両レイヤに同じコードを書くのが既存の
    慣行（例: (1,4) KEY_LEFTCTRL、(3,2) KEY_PAGEDOWN は両方にある）。
  - 基本 keymap に無いセルは UINT_MAX センチネルで「イベントなし」。

### 0.2 (3,4) への割当コードの決定

**`KEY_SCROLLDOWN`（Linux 入力コード 178）を採用**。根拠:

1. KEY_PAGEDOWN は KEYBOARD-FIX.md §5-2 の明示判断で不採用
   （(3,2) My辞書と同一コードになり、ユーザー空間で2つの物理キーを
   区別できなくなる）。SH1〜SH3 のコメントは歴史的意図の記録だが、
   有効化された実績はなく、KEYBOARD-FIX の判断が優先する。
2. KEY_SCROLLDOWN は「下方向スクロール」を意味する標準の Linux
   入力コードで、ページ送り（下）の機能（1画面分下へ送る）と
   意味が一致し、既存のどの物理キーとも衝突しない。
3. ユーザー空間（tk_kbd）では KEY_SCROLLDOWN → TK_KEY_PAGE_DOWN
   に写像する（§1.2）。これにより既定の振る舞いは「ページ送り
   （下）＝ Page Down」で、かつ My辞書キーとは evdev コードで
   区別可能なままになる。

### 0.3 戻るキー側の残件（tk_kbd）

DTS は KEY_BACK を出すようになっているが、tk_kbd の表はまだ
`I(KEY_BACK, "KEY_BACK (not emitted; the back key has no DTS
assignment)")` と IGNORE のままで、戻るキーはライブラリ層で
捨てられていた。KEYBOARD-FIX.md §5-1 が予告していた
「DTS に割り当てたら tk_kbd の IGNORE 行も見直す」が未実施だった
もの。本弾で tk.h に TK_KEY_BACK を追加（TK_KEY_INSERT 追加の
前例どおり末尾への追加のみ）し、KEY_BACK を写像する。

---

## 1. 実装1: DTS キー解消 — 完了

### 変更内容

- `buildbrain/linux-brain/arch/arm/boot/dts/imx28-pwsh6.dts`
  （submodule、pin コミット 11c11c922 の作業ツリー。commit はしていない）:
  - 基本 keymap: コメントアウト行 `/*<3 4 PageDown> */` を
    `<3 4 KEY_SCROLLDOWN>, /* Page Down */` に置換。
  - keymap-symbol: `<3 4 KEY_SCROLLDOWN>, /* Page Down */` を追加
    （(1,4) KEY_LEFTCTRL の隣。特殊キーを両レイヤに複写する既存慣行に
    倣う。ドライバ brain-kbd-gpio.c:195 は記号レイヤ使用時に
    km_symbol のみを参照し、未登録セルは破棄されるため、片方だけでは
    記号キー押下中に死ぬ）。
  - diff は上記2ハンクのみ（`git diff` で確認。submodule の他の変更なし）。
  - 補足: lb-pin-test チェックアウト（別ブランチ 10e632e19）は同じ
    内容を整形済みの書式で持つが、本作業は pin 実物に対して行った。
    (0,3) KEY_BACK と (0,6) KEY_HOME が割当済みであることも pin 実物で
    再確認した（§0 の結論は不変）。
- rebuild 側 tk_kbd（§0 の判断に基づく）:
  - `include/tk.h`: tk_key 列挙の末尾に TK_KEY_BACK を追加（既存値の
    並びは不変。TK_KEY_INSERT 追加時の前例どおり）。
  - `src/tk_kbd.c`: 表に `E(KEY_BACK, …, TK_KEY_BACK)`、
    `E(KEY_SCROLLDOWN, …, TK_KEY_PAGE_DOWN)`、
    `E(KEY_SCROLLUP, …, TK_KEY_PAGE_UP)` を追加し、従来の
    `I(KEY_BACK, "not emitted…")` 行を削除。KEY_HOME の name と
    ヘッダコメントを実態（(0,6) 接点からも来る）に合わせて修正。
  - `include/tk_kbd.h`: ドキュメントを実態に更新（(0,3) の割当元
    コミット、(3,4) の判断、記号レイヤでの扱い）。
  - `tests/test_kbd.c`: KEY_BACK / KEY_SCROLLDOWN の describe と
    押下→写像のチェックを8件追加。
- `KEYBOARD-FIX.md` §5 と U4 行を実態に合わせて更新（§5-1/2 は
  解決済みに、§5-3 と U4 は pin 実物での確認結果に）。

### ビルド・テスト証拠

- DTB ビルド: kbuild 経路で実施。`brain_defconfig`（buildbrain の
  `make ldefconfig` 相当）→ `make dtbs` で
  `imx28-pwsh6.dtb`（24,502 B）を含む全 DTB の生成に成功。
  環境: クロスコンパイラは Bootlin armv5 ツールチェーン
  （`~/workspace/brainux-driver-work/toolchain.tar.xz` を
  `~/workspace/buildbrain-work/toolchain/` に展開、
  arm-buildroot-linux-gnueabi-gcc 13.3.0）。flex/bison/m4 は
  システムの dpkg 状態が壊れていて apt で入れられなかったため、
  ブラウザフェーズ2の hosttools 展開物
  （`rebuild/browser-engine/.work-p2/hosttools/`）を PATH/M4/
  BISON_PKGDATADIR で流用した。ビルド出力は `O=` で
  `~/workspace/buildbrain-work/dtb-out/` に分離し、submodule
  ツリーには DTS の変更だけが残る状態にした。
- DTB 内容の検証: ビルドした dtc でデコンパイルし、keyboard_gpio
  ノードの keymap / keymap-symbol 両方にセル列
  `0x03 0x04 0xb2`（row 3, col 4, code 178 = KEY_SCROLLDOWN）が
  ちょうど1回ずつ（合計2回）含まれることを確認。0xb2 の出現は
  この2箇所のみ。(0,3) の `0x00 0x03 0x9e`（KEY_BACK=158）も
  基本 keymap に存在。
- tk_kbd テスト: `test_kbd: 139 passed, 0 failed`（従来131 + 新規8）。

### 副次的な発見（未対応・要判断）

- pin コミットの DTS は記号レイヤの矢印キー等にメディアキーを
  割当てている（(2,3) VOLUMEDOWN / (4,3) VOLUMEUP /
  (3,3) BRIGHTNESSUP / (1,3) BRIGHTNESSDOWN / (6,3) MUTE、
  コミット b0ac9f045）。しかし tk_kbd は KEY_VOLUMEUP /
  KEY_VOLUMEDOWN / KEY_MUTE / KEY_BRIGHTNESS* を IGNORE として
  破棄しており、name 文字列も「no volume key」等の旧状態のまま。
  つまり記号+矢印の音量/輝度操作は現行スタックでは一切効かない。
  TK_ACTION_VOLUME_UP 等の受け口は tk 側にあるため写像自体は
  可能だが、「DE で音量キーとして扱うか」は製品判断を含むため
  本弾では変更せず、発見として記録する。

## 2. 実装2: マスコット設定行の配線 — 完了

### 消費側の契約（変更なし・既存）

シェルのセッション（`shell/src/session.c`）は起動時に brainreg
セクション "mascot" を読む: `enabled`（bool、既定 true）、`pack`
（パス文字列。未設定時は環境変数 → ユーザーディレクトリ →
システムディレクトリの `mascot.bdm` へフォールバック）、`size`
（int、既定 208）、`fullscreen`（bool、既定 false）。これまで
この4キーを書く UI が存在せず、設定アプリからの配線が欠けていた。

### 変更内容（設定アプリのみ。UI 側 bd_settings.c は汎用実装のため無変更）

- `apps/settings/setmodel.h`: ページ列挙に SET_PAGE_MASCOT を追加
  （端末情報の前。SET_PAGE_COUNT は 7 → 8）。
- `apps/settings/setmodel.c`: 「マスコット」ページの行を実装。
  - `enabled`: SR_TOGGLE → reg "mascot"/"enabled"。
  - `pack`: SR_CYCLE。マスコットディレクトリ（ユーザー
    `~/.local/share/brainux/mascots` とシステム
    `/usr/share/brainux/mascots`、セッションのフォールバックと
    同じ2箇所）内の `*.bdm` を名前順に列挙して選択肢にし、
    選択でフルパスを reg "mascot"/"pack" へ書く。同名は
    ユーザー側を優先（二重列挙しない）。現在の実効パック
    （reg 値＋セッションと同じフォールバック解決）を
    choice_cur と値表示に反映。reg の値が列挙外のパスなら
    choice_cur=-1 のカスタム値として表示（既存の行契約どおり）。
    パックが1つも無い場合は SR_INFO「パックが見つかりません」。
    テスト用に環境変数 BRAINUX_MASCOT_DIR で列挙先を単一
    ディレクトリに差し替え可能（セッション側の
    BRAINUX_MASCOT_PACK 前例に倣う）。
  - `size`: SR_CYCLE（168 / 208 / 248 / 320 px）→ reg "mascot"/"size"。
  - `fullscreen`: SR_TOGGLE → reg "mascot"/"fullscreen"。
  - `note`: SR_INFO「変更は次回起動時から反映されます」
    （セッションが起動時に読む実装の正直な表示）。
  - 書き込みは全て既存の reg_set 経由。reg 層（`shell/src/reg.c`）が
    tmp 書込み → fsync → rename → ディレクトリ fsync の原子的
    置換を実装済みで、追加作業は不要だった。
- テスト:
  - `apps/settings/tests/test_settings.c`: マスコットグループ21件を
    追加（フィクスチャディレクトリでの列挙・ソート・.txt 除外、
    選択→フルパスの永続化、cycle のラップ、size の永続化と
    ラップ、両トグルの永続化、書き込み後の mascot.conf の存在と
    .tmp 残留なし＝原子的書換えの傍証）。
  - `apps/settings/tests/test_settings_ui.c`: サイドバーの
    ページ数チェックを SET_PAGE_COUNT 連動に更新し、マスコット
    ページの行描画（5行）と UI 経由のトグル書き込みのチェック
    3件を追加。

### テスト証拠

- `test_settings: 51 passed, 0 failed`（従来30 + 新規21）。
- `test_settings_ui: 16 passed, 0 failed`（従来13。ページ数
  チェックの前提更新 + 新規3）。
- 全体回帰（本弾の全変更後に再実行）: ツールキット 3,577 件
  （基線 3,569 + kbd 新規8）、シェル 588 件 + test_mascot 95 checks、
  アプリ 1,083 件（基線 1,059 + 設定の新規24）、OSK 319 件。
  全て 0 failed で回帰ゼロ。

### 残る制約（正直な注記）

- 設定の反映は「次回セッション起動時」。実行中セッションへの
  ライブ反映（マスコットの即時表示/消去・差し替え）はセッション
  側に reg 変更の監視が無いため未対応。やるならセッションに
  reg ファイルの mtime 監視か IPC が必要で、別弾の設計項目。
- パック列挙は行生成のたびに2ディレクトリを読む（scandir）。
  設定アプリの行生成頻度では無視できるコストで、キャッシュを
  持たない分「パックを追加したら再表示で即見える」を優先した。

## 3. 調査1: 通知/アラームのデーモン連携 — 現状棚卸しと設計案（実装なし）

### 3.1 通知の現状

- シェルには通知の「表示側」だけがある: `shell/src/shellui.c` に
  通知シート（時計タップで開くパネル）とメモリ内モデル
  （`bd_shell_notify()`、上限 BD_NOTIFY_MAX 件、文字列リスト）が
  実装済み。しかし **`bd_shell_notify()` の呼出元はテスト
  （test_shell）だけで、実コードの生産者が存在しない**。
- アプリからシェルへ通知を送る IPC は存在しない。rebuild スタックは
  D-Bus を一切使っていない（grep で確認。rootfs には dbus パッケージ
  自体は入っている）。アプリ各所の "notify" は tk のウィジェット内
  コールバック（tk_notify_cb）で、シェル通知とは無関係。
- 通知の永続化・未読管理・履歴はない（シェル再起動で消える）。

### 3.2 アラームの現状

- 時計アプリ（apps/clock）に完全なアラームモデルがある:
  `alarms.txt` へ bdstore 経由で原子的に永続化、wall/monotonic の
  クロスチェック、時刻ジャンプ時の catch-up 発火（一度だけ鳴らす）、
  スヌーズ（monotonic 基準）。モデルは UI 非依存に設計されている。
- しかし評価関数 `ck_poll()` を呼ぶのは **bd-clock 自身のメイン
  ループだけ**（bd_clock.c:223）。bd-clock が起動していないと
  アラームもタイマーも発火しない。
- 常駐デーモンは存在しない: セッションはデーモン群を起動しない、
  RTC wakealarm（/sys/class/rtc/rtc0/wakealarm）の使用なし、
  rootfs 側も cron を無効化済みで systemd timer 方針だが
  アラーム用の unit は無い。
- タスクアプリ（apps/tasks）の期限は「表示・ソート専用。何も
  期限切れにならない」と明記されており、リマインドは意図的に
  持っていない（日数ベース期限の禁止原則とも整合）。

### 3.3 設計案（次弾の実装計画）

方式比較:

| 方式 | 長所 | 短所 |
|---|---|---|
| A. D-Bus（org.freedesktop.Notifications） | 既存 X アプリの通知と互換 | dbus-daemon 常駐のメモリ/CPU、rebuild スタックに新規依存、セッションが fbdev で X とは別系統 |
| B. シェル内蔵の軽量 IPC（Unix ソケット＋行プロトコル） | 既存の WM 独自プロトコル（BD_WM_*）と同型で実装が小さい。通知シートが既にある。依存ゼロ | X アプリの通知は受けられない（将来ブリッジで対応可） |
| C. systemd timer の動的生成 | rootfs が systemd 前提 | 表示・音との連携が別途必要。OnCalendar は wall 前提で時刻不信の原則と要すり合わせ |

推奨: **B を軸**に以下を分離実装する。

1. **通知**: セッションが Unix ソケットで通知を受ける
   （タイトル/本文/緊急度/送信元アプリ名の行プロトコル）。
   シェルが一元管理し、既存の通知シートに表示。履歴は件数上限で
   追い出す（日数期限は使わない）。アプリ側は小さな送信関数
   （libbdshell 側に `bd_notify_send()`）だけを持つ。
2. **アラーム**: 評価を小さな常駐デーモン（bd-alarmd 相当）へ分離し、
   clockmodel の `ck_poll` をそのまま再利用する（モデルが UI 非依存
   なのはこのため）。デーモンは alarms.txt の変更監視＋timerfd の
   イベント駆動で待ち、常時ポーリングしない。発火時は (1) の通知
   IPC でシェルへ通知し、音を鳴らし、bd-clock が起動していれば
   そちらの表示とも二重発火しないよう発火済み ID を共有する。
3. **サスペンド対応**: 発火時刻に RTC wakealarm を設定して
   サスペンドから復帰させる（i.MX28 の RTC で可能かは実機/
   ゲスト検証が前提。不可なら「サスペンド中は鳴らない」を
   仕様として明記する）。
4. **メモリ予算**: デーモンは単機能・依存最小で PSS 数 MB 級を
   目標にし、実測を合格条件にする。

未検証・要判断: RTC wakealarm の可否、X セッション併用時に
X アプリ通知を受ける必要があるか（必要なら A のブリッジを後付け）。

## 4. 調査2: ライブラリ統廃合の棚卸し — 候補一覧と測定計画（削除・置換は未実施）

棚卸しの出典は現行 rootfs の生成スクリプト
（`buildbrain/os-brainux/setup_brainux.sh` の apt リスト＝現行出荷
構成）と rebuild ツリーの実物確認。「重複確認済み」とされてきた
項目も、一次資料で再確認した結果を併記する。

### 4.1 rootfs（現行 Brainux）側の候補

| # | 領域 | 現状（併存） | 備考 |
|---|---|---|---|
| 1 | DE/WM | jwm + Xorg 一式 / 旧 braindesk（repo `braindesk/`）/ rebuild シェル（未統合） | 3世代併存。置換の合格条件は「既存 X アプリが新セッションで無変更で動くこと」 |
| 2 | グリーター | ly（ソースビルド） / braingreeter（rebuild） | セッション決定とセットで切替 |
| 3 | ファイル管理 | pcmanfm / bd-files（rebuild） | 機能差（ネットワーク・ゴミ箱等）を先に一覧化 |
| 4 | 端末 | lxterminal（X）+ fbterm（fb コンソール）。rebuild 側に端末アプリは無い | 「端末3種」は旧監査由来。現行 apt リストで確認できるのはこの2種＋tmux。3種目の混入経路（brain-config パッケージ等）は不透明で**要再確認** |
| 5 | ブラウザ | netsurf-gtk + netsurf-fb（同一エンジンの2フロントエンド）/ rebuild の NetSurf brain フロントエンド | gtk 版は X 互換用に残す判断もありうる。容量と使用実績で測る |
| 6 | IME | fcitx-anthy（fcitx フレームワーク＋anthy） | uim は apt リストに無く `brain-measure` の監視パターンにのみ出現 → 混入経路は**要再確認**（brain-config か依存）。rebuild は libbrainime＋OSK だが、X アプリ向けの fcitx アドオンは未作成で、X 側の入力経路が置換の前提条件 |
| 7 | DHCP | dhcpcd と isc-dhcp-client が両方入っている | 明確な重複。シェルの Wi-Fi 実装がどちらを使うかで一本化先が決まる |
| 8 | SSH | `ssh`（openssh-client を含むメタ）と `openssh-client` の二重指定 | apt リスト上の重複。害は小さいが棚卸し対象 |
| 9 | 開発ツール | build-essential / xorg-dev / libjpeg-dev / libfreetype6-dev / python3-dev / ipython3 / python3-numpy / git / flex / bison / device-tree-compiler 等が実行イメージに入っている | 容量インパクト最大級。ビルド用イメージと実行用イメージの分離が候補（既存アプリ互換には無関係なはずだが、利用実態の確認が先） |
| 10 | メディア再生 | mpg123 + vorbis-tools（CLI）/ bd-player（rebuild） | CLI はスクリプト用途がありうる。併用か置換かは使用実績で判断 |
| 11 | 時刻 | fake-hwclock + systemd-timesyncd | 役割分担あり（RTC 不揮発の代替＋オンライン時の同期）。統合候補ではなく現状維持が本命。測定対象からは外す |
| 12 | エディタ | vim のみ（GUI なし）/ bd-notes（rebuild） | 欠落の補完であって重複ではない |

### 4.2 rebuild 内部

- 共有は概ね健全: brainreg は `shell/src/reg.c` の単一実装を
  libbdshell.a 経由で全アプリがリンク（コピーなし）。GUI は
  libbrainui2.so 単一。アプリ共通部品は apps/common（bdstore/
  bdmime）の単一実装。
- 旧 braindesk（repo `braindesk/` の bui_* ライブラリ＋アプリ群）は
  rebuild と完全な二重実装。置換完了（互換検証通過）が削除の前提。
- apps/admin の sysmodel と shell の backend は責務が近いが、
  現状の機能重複は限定的（電池は backend のみ、sysmodel は
  /proc・mounts 集計）。統合候補として監視に留める。

### 4.3 測定で選ぶ計画（次弾以降）

1. **互換ゲートを先に固定**: 候補ごとに「置換先で既存アプリ/
   ワークフローが無変更で動くか」のチェックリストを作る
   （参照アプリ調査と同じ形式）。ここを通らない候補は測定に進まない。
2. **実測**: イメージ容量（dpkg-query の Installed-Size）、
   常駐 PSS（rootfs 既存の `brain-measure` スクリプトを流用）、
   起動時間、使用実績（オフライン前提のため日数ではなく
   セッション内の起動回数・手動確認ベース）。
3. **順序**: 影響が大きく測定が容易な順 — #9 開発ツールの分離 →
   #7 DHCP 一本化 → #4 端末 → #5 ブラウザ → #6 IME（fcitx アドオン
   作成が前提）→ #1/#2 DE・グリーター（統合フェーズと連動）。
4. 本弾では削除・置換は一切行っていない。上表の「要再確認」
   2件（端末3種目、uim の混入経路）は次弾の棚卸しで
   イメージ実物（dpkg -l）から確定させる。

## 5. コピー/オーバーヘッド削減

- DTS/tk_kbd: 変更はデータ（キー表のエントリ）と DTB のセルのみで、
  実行時パスの追加コストはゼロ。KEY_SCROLLDOWN の追加でドライバの
  スキャン対象が1接点増えるが、これは「キーが存在する」こと自体の
  コストで、無効だった接点の有効化に伴う最小分のみ。
- マスコット設定: 書き込みは reg_set 1回（ファイル1個の原子的
  置換）で、常駐コストなし。パック列挙は行生成時の scandir 2回
  （最大でも数十エントリ）で、ホットパスではない。列挙結果の
  キャッシュは持たない（後述のとおり鮮度優先で、コスト実測上も
  不要と判断）。
- 調査2件は設計・棚卸しのみで実行時コストの変更なし。

## 6. キャッシュ/メモリ管理

- 本弾で新規のキャッシュは導入していない。マスコットのパック
  列挙を都度実行にしたのは、設定アプリの行生成が低頻度で、
  キャッシュの無効化（パック追加/削除）を持つより単純で、
  メモリも使わないため。
- tk_key 列挙への TK_KEY_BACK 追加は末尾追加のみで、既存の
  数値・構造体レイアウトに影響しない（test_kbd 含む全スイートで
  確認済み）。
- DTB は 24,502 B で、追加セル2個分（24 B）の増加のみ。

## 7. 実機の現実への対応

- 原子的書換え: マスコット設定の永続化は既存の reg 層
  （tmp → fsync → rename → ディレクトリ fsync）をそのまま使い、
  テストで「書込み後に .tmp が残らない」ことまで確認した。
  書込み途中のリセットでも旧値か新値のどちらかになる。
- オフライン既定・時刻不信: 本弾の変更に時刻依存の処理はない。
  調査1の設計でも、日数ベースの通知追い出しを採らず件数上限に
  している。調査2でも使用実績の測り方を日数に依存させていない。
- SD 本拠 / eMMC ノータッチ: 設定ファイル（brainreg）は従来どおり
  SD 側のユーザーディレクトリにあり、eMMC への書込みは一切ない。
  DTS 変更はカーネルツリーのソース変更で、実機への反映は
  SD イメージ経由（統合フェーズ）。
- 突然のリセット: DTS/DTB は読み取り専用の成果物で影響なし。

## 8. 未検証

- **実機/ゲストでのキー実イベント**: (3,4) の KEY_SCROLLDOWN と
  (0,3) の KEY_BACK が実機 evtest で実際に発火することは未検証
  （DTB のビルドとセル混入の確認まで）。(0,6) 接点の正体（U3）も
  従来どおり未確認。いずれも統合フェーズの evtest 照合項目。
- **設定変更の実セッション反映**: 設定アプリ→reg→セッション読取り
  の連鎖は、書込み側（設定テスト）と読取り側（既存セッション
  コード＋test_session 102件）で個別に検証済みだが、実セッションを
  起動して「設定で選んだキャラが表示される」ところまでの通し
  検証は統合フェーズ（SD イメージ発行後）。
- **記号+矢印のメディアキー**（§1 の副次的発見）: tk_kbd が
  IGNORE のままで未対応。扱いは要判断。
- 調査2の「要再確認」2件（端末3種目、uim の混入経路）は
  イメージ実物の dpkg 一覧での確定が次弾。
- 本弾の変更は全て作業ツリーのみで、**commit / push はしていない**。
  変更ファイル一覧:
  - buildbrain repo: `linux-brain/arch/arm/boot/dts/imx28-pwsh6.dts`
    （submodule 作業ツリー）
  - rebuild: `include/tk.h`、`include/tk_kbd.h`、`src/tk_kbd.c`、
    `tests/test_kbd.c`、`KEYBOARD-FIX.md`、
    `apps/settings/setmodel.h`、`apps/settings/setmodel.c`、
    `apps/settings/tests/test_settings.c`、
    `apps/settings/tests/test_settings_ui.c`、本レポート
  - 作業用生成物（repo 外）: `~/workspace/buildbrain-work/dtb-out/`
    （.config と DTB 群）、`~/workspace/buildbrain-work/toolchain/`
    （Bootlin ツールチェーン展開物）
