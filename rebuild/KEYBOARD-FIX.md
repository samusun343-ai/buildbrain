# KEYBOARD-FIX — tk_kbd キー定義表の全面修正記録

日付: 2026-11-20
対象: `include/tk_kbd.h`、`src/tk_kbd.c`、`tests/test_kbd.c`（＋波及する最小変更、下記 §7）
正本: `keyboard-research.md`（実機写真・公式ガイド・linux-brain DTS）
追加情報源: Brainux 公式 Wiki（ユーザー指示 2026-11-20 で追加調査）

---

## 1. 結論: 採用した意味論は「Brainux キーマップのフェイスバリュー」

修正前の表は PC JIS 前提で根本的に誤っていた（§4 の D1〜D22）。
修正にあたって3情報源を突き合わせた結果、**借用 evdev コードは
Brainux の流儀どおり「コードの字面どおり」に解釈する**ことを採用した。

| 物理キー | DTS が出すコード | WinCE 純正の意味（公式ガイド） | Brainux Wiki の定義 | **tk_kbd の採用** |
|---|---|---|---|---|
| ホーム | KEY_ESC | ホーム画面 | Esc（キーマップ図の "Esc"） | **TK_KEY_ESCAPE** |
| 国語/漢字 | KEY_TAB | 国語⇔漢字 辞典切替 | Tab | **TK_KEY_TAB** |
| 英和/和英 | KEY_PAGEUP | 英和⇔和英 辞典切替 | PgU | **TK_KEY_PAGE_UP** |
| My辞書 | KEY_PAGEDOWN | My辞書を開く | PgD | **TK_KEY_PAGE_DOWN** |
| 履歴 | KEY_INSERT | 履歴/しおり | Ins | **TK_KEY_INSERT**（新設） |
| 一覧から選ぶ | KEY_DELETE | 辞書一覧 | Del | **TK_KEY_DELETE** |
| ページ送り（上） | KEY_LEFTCTRL | ページ送り | **Ctrl**（Sx5〜Sx7 表: Ctrl=ページアップ） | **TK_MOD_CTRL** |
| 音声 | KEY_LEFTALT | 音声再生 | **Alt**（Sx5〜Sx7 表: Alt=音声） | **TK_MOD_ALT** |
| スペース/変換 | KEY_SPACE | スペース/変換 | Space | TK_KEY_SPACE |
| 決定 | KEY_ENTER | 決定 | Enter | TK_KEY_ENTER |
| 削除 | KEY_BACKSPACE | 削除 | BS | TK_KEY_BACKSPACE |
| シフト | KEY_LEFTSHIFT | 枠囲み副機能（ワンショット） | Shift | TK_MOD_SHIFT（保持式） |
| ー | KEY_MINUS | 長音入力 | `-` / Shift で `_` | **テキスト "-" / "_"** |
| 電源 | （マトリクス外） | 電源 | 「PC の電源ボタンと同じ動作」 | **TK_ACTION_POWER** |

### 採用根拠

1. **Brainux Wiki がプラットフォームの公式仕様である。** Wiki の
   キーマップ図（`keyboard-research/wiki_keymap.png` に保存）は全キーの
   通常/Shift/記号/記号+Shift の4層を定義しており、専用キーは
   Esc/Tab/PgU/PgD/Ins/Del/Ctrl/Alt と字面どおりに働く前提で描かれている。
   特殊キー対応表（Sx5〜Sx7）も「Ctrl=ページアップ（キー）、Alt=音声（キー）」
   と明記する。
2. **字面どおりでなければ Esc/Ctrl/Tab が物理キーから一切出せない。**
   ターミナル・vi・シェル補完など Brainux の中核用途が成立しない。
   実際、実地報告（Qiita、下記出典）も「escは電源の隣のボタン」
   「:は記号+shift+h」と字面どおりの使い方をしている。
3. **DTS の名目コードはこの解釈のために選ばれている。** 記号レイヤの
   GRAVE/EQUAL/BACKSLASH 等は US 配列でその記号になるコードであり、
   Wiki キーマップの割当と完全に一致する（§3）。
4. WinCE の辞書機能（ホーム画面、辞書切替、音声再生、枠囲み副機能）は
   **辞書ファームウェアの意味論であり、evdev 上に独立した信号を持たない**
   （例: 一括検索も 記号+ホーム で同じ KEY_ESC になる）。
   これらを DE で再現したい場合は、tk_kbd ではなくシェル側の
   ポリシー（グローバルショートカット等）として実装すべきもので、
   キー定義表に混ぜると全ての標準的コンシューマ（ターミナル等）が壊れる。

この判断により、当初方針にあった「専用キーのアクション化」
（ホーム/国語辞典/… を TK_EV_ACTION へ）は**採用しなかった**。
代わりに満たした要件: KEY_DELETE の意味は Wiki キーマップの "Del"
（前方削除）であることを3源で確認した上で編集キーとした、
KEY_INSERT は捨てず TK_KEY_INSERT として写像した、
KEY_GRAVE の IME トグル割当（D4 の危険）は除去した。

---

## 2. Brainux Wiki の出典

- **Brainux Wiki「初めての方へ ─ とりあえず動かす」**
  https://wiki.brainux.org/beginners/get-started/
  - §キーボード: 「キートップに記載されたキー以外の文字は、すべて
    『記号』キーと『シフト』キーを組み合わせて入力します」
  - 特殊キー対応表（Sx5 〜 Sx7, H7800 〜 H9100, AAx, AJx, SR3）:
    Shift=シフト、**Ctrl=ページアップ、Alt=音声**、記号=記号
    （旧世代では Alt=文字切り替え（Sx1〜Sx3）、Ctrl=音声（Sx4）等、
    世代で割当が違うことも同表に明記されている。SH6/AJ2 は Sx5〜Sx7 行）
  - §電源を切る: 「電源ボタンには一般的な PC の電源ボタンと同じ
    動作が割り当てられている」
  - 対応機種表: PW-Sx6, H8000, AA2, AJ2 → デバイスツリー名
    imx28-pwsh6（DTS 正本と一致）
- **Wiki キーマップ図** https://wiki.brainux.org/assets/images/keymap.png
  （証拠として `keyboard-research/wiki_keymap.png` に保存。
  「※ Ctrl, Alt は機種ごとにそれっぽい配置のボタンを割り当てる」の注記付き）
- 実地報告（二次情報）: Qiita「Brainでlinux(その1)-はじまり」
  https://qiita.com/aruelu/items/860cd3623b6de600f918
  （SH2/A9200 世代。「escは電源の隣のボタン」「:は記号+shift+h」—
  後者はキーマップ図の 記号+Shift+H=':' と一致）
- 実地報告（二次情報）: 個人ブログ「電子辞書Brainをいじる (1)」
  https://nsb.homeip.net/wp/?p=3003
  （SH3 世代。ESC/TAB/Ctrl/Alt の対応を列挙。記号入力で一部記号が
  連続入力になる不具合の報告あり — SH3 世代のドライバ/キーマップの
  既知の癖とみなし、SH6 の規範とは扱わない。ただし「記号レイヤの
  リピートには実績上の注意点がある」ことは §6 に残す）

---

## 3. 記号レイヤの文字割当（U2 の決着）

keyboard-research.md の未確認事項 U2（記号レイヤの名目コードから
出すべき文字）は、**Wiki キーマップ図で確定**した。US フェイスバリュー
（シフトレベル込み）であり、tk_kbd の表はこのとおり実装した:

| コード（記号+キー） | 素 | +Shift |
|---|---|---|
| KEY_1〜KEY_0（記号+Q〜P） | 1〜0 | ! @ # $ % ^ & * ( ) |
| KEY_GRAVE（記号+D） | ` | ~ |
| KEY_EQUAL（記号+F） | = | + |
| KEY_BACKSLASH（記号+G） | \ | \| |
| KEY_SEMICOLON（記号+H） | ; | : |
| KEY_APOSTROPHE（記号+J） | ' | " |
| KEY_LEFTBRACE（記号+K） | [ | { |
| KEY_RIGHTBRACE（記号+L） | ] | } |
| KEY_COMMA（記号+N） | , | < |
| KEY_DOT（記号+M） | . | > |
| KEY_SLASH（記号+ー） | / | ? |
| KEY_MINUS（ーキー自体） | - | _ |

注: 数字にシフトレベルが存在するのは「記号+Shift+英字」という
3キー同時押しの場合のみで、ドライバは素の KEY_1〜KEY_0 と
KEY_LEFTSHIFT の保持を別々に流すため、ユーザー空間では通常の
シフト適用で再現できる。修正前の JIS 写像（Shift+2=`"` 等）は全廃。

---

## 4. research §6 差分リストへの対処結果

### 重大（D1〜D12）

| # | 対処 |
|---|---|
| D1 | 解消。数字行の PC JIS 定義を削除。数字は記号レイヤの素コード＋Wiki の US シフトレベル（§3） |
| D2 | 解消。KEY_YEN/KEY_RO は実キー定義から削除し、「このハードウェアに無いキー」として KIND_IGNORE で明示（¥・ろ は OSK の担当であることが表自体から読める） |
| D3 | 解消。変換/無変換/半角全角/かな の各 ACTION 定義を削除し KIND_IGNORE で不在を明示。変換はスペース/変換キー（KEY_SPACE→IME 経路）と OSK に寄る |
| D4 | 解消。KEY_GRAVE の IME トグル割当（jis_action 機構ごと）を撤去。素のテキスト `` ` `` / `~` |
| D5 | 解消。発行されないコード（HOME/HOMEPAGE/SEARCH/SOUND/VOLUME*/MUTE/BRIGHTNESS*/MENU/BACK/FORWARD/SLEEP）での待ち受けを全廃。実コード側は §1 のフェイスバリューで定義。VOLUME 等は KIND_IGNORE で不在を明示（※基本レイヤ前提の記述。記号レイヤではメディアコードが実際に発行されることが後に判明し、2026-11-21 に ACTION へ写像済み — §10 追記参照） |
| D6 | **方針転換の上で解消**。KEY_DELETE は「一覧キーだからアクション」ではなく、Wiki キーマップの "Del" ＝前方削除として編集キーに定義（§1 の根拠参照）。誤認ではなく3源確認済みの仕様 |
| D7 | 解消。KEY_INSERT を IGNORE から TK_KEY_INSERT（tk.h に新設）へ |
| D8 | **Wiki 採用で解消**。KEY_LEFTCTRL は Ctrl 修飾キー（ページ送り（上）キー＝Ctrl が Brainux の仕様）。「記号キー位置」の誤コメントは削除。ページ送りとしての意味は持たせない（PgU は英和キー側） |
| D9 | **Wiki 採用で解消**。KEY_LEFTALT は Alt 修飾キー（音声キー＝Alt）。基本/記号レイヤの区別が潰れる問題は、そもそも Brainux では両方 Alt なので消滅 |
| D10 | 解消。KEY_PAGEUP=英和キー→TK_KEY_PAGE_UP、KEY_PAGEDOWN=My辞書キー→TK_KEY_PAGE_DOWN（フェイスバリュー） |
| D11 | **Wiki 採用で解消**。KEY_ESC は TK_KEY_ESCAPE（ホームキーが Esc を出すのが仕様）。「調べる」の命名は削除 |
| D12 | 解消。PC JIS 刻印写像を全廃し、記号レイヤは §3 の Wiki 対応表に置換 |

### 中（D13〜D18）

| # | 対処 |
|---|---|
| D13 | 解消。冒頭コメントを書き直し: 48キー、GPIO マトリクス（keyboard_gpio）、「JIS arrangement」表記を撤去。ドライバが I2C でないこと（buildbrain フォーク側の実態は U4 として残る） |
| D14 | 解消。KEY_TAB は Tab として素通し（フェイスバリュー）。辞書キーとの混線は「そもそも同一キー」という仕様として決着。文脈分離はシェル側ポリシーの領分 |
| D15 | **一部採用・一部不採用**。シフトは evdev の保持式修飾キーとして処理（Brainux の標準キーマップと同じ）。WinCE のワンショット＋枠囲み副機能（クリア/文字小・大/音量）は Brainux キーマップに存在しないため tk_kbd には実装しない（§1 根拠4）。DE が必要とすればシェル側ポリシーで |
| D16 | 解消。KEY_CAPSLOCK は「外部 PC キーボード専用」と明記して残置 |
| D17 | 解消。KEY_END は「外部キーボード専用・内蔵からは来ない」と明記。KEY_HOME も同様に外部専用のキャレットキーとした（旧来の HOME アクション写像は廃止） |
| D18 | **DTS 側課題として記録**（§5）。tk_kbd 側では解決不能 |

### 低（D19〜D22）

| # | 対処 |
|---|---|
| D19 | KEY_SPACE の「スペース/変換」コメントは維持 |
| D20 | 解消。KEY_MINUS の JIS 写像（Shift==）を廃止し Wiki の `-`/`_` に |
| D21 | 解消。「決定=Enter」は維持、「調べる=Esc」は削除（Esc はホームキーのフェイスバリュー） |
| D22 | F1〜F12 は外部キーボード用と明記して残置 |

---

## 5. DTS / ドライバ側の課題（OS 統合フェーズの項目）

tk_kbd（ユーザー空間）では解決できないもの:

1. **戻るキー (0,3)**: **解決済み**（2026-11-21、OS内部フェーズ
   第1弾で pin コミット 11c11c922 の DTS を直接確認）。本節の旧記述は
   古い DTS の状態だった。現行 DTS では (0,3) はコミット b0ac9f045 で
   KEY_BACK が割当済み。tk_kbd 側も KEY_BACK → TK_KEY_BACK の
   EDIT 写像に変更済み（従来の IGNORE を解除、test_kbd で検証）。
   記号レイヤには割当が無い（KEY_HOME と同じ扱い）ため、記号キー
   押下中は無反応 — これはフォーク側の既存判断として維持。
2. **ページ送り（下）(3,4)**: **解決済み**（同上）。pin 時点で
   基本 keymap の最後のコメントアウト接点だった。(3,2) の My辞書が
   KEY_PAGEDOWN を占有するため、別コード **KEY_SCROLLDOWN**（178）を
   基本・記号の両レイヤに割当てた（KEY_PAGEDOWN の両レイヤ複写の
   慣行に倣う）。tk_kbd では KEY_SCROLLDOWN → TK_KEY_PAGE_DOWN に
   写像し、フェイスバリュー（Page Down）を保ちながら My辞書と
   evdev コードで区別できる。DTB のビルドとデコンパイルでの
   セル混入確認まで実施済み（REPORT-os-internals-1.md §1）。
   なお SH1〜SH3 の DTS には Page Down 位置に KEY_PAGEDOWN の
   コメントアウト痕跡があるが、全機種で有効化実績が無く、
   上記の衝突理由により本判断で superseded とする。
3. **無刻印キー候補 (0,6)**: pin 時点で KEY_HOME が割当済み
   （コメントアウトではない）。接点の正体自体は U3 のまま未確認。

## 6. 未確認事項（U1〜U7）の現状

| # | 状態 |
|---|---|
| U1 | **未決のまま**。WinCE 純正の記号割当と linux-brain の symbol keymap の一致は未確認。ただし Brainux の割当自体は Wiki で確定したため、Brainux 側の実装には影響しない |
| U2 | **決着**。Wiki キーマップ図により US フェイスバリュー（シフトレベル込み）で確定（§3） |
| U3 | 未決のまま（無刻印キーの正体）。§5-3 と同件 |
| U4 | **一部決着**（2026-11-21）。buildbrain フォークの pin コミット 11c11c922 で DTS 全接点と brain-kbd-gpio.c の意味論（記号レイヤはフォールバックなし）を直接確認済み（§5）。残るのは実機 evtest での実イベント照合で、統合フェーズの項目 |
| U5 | 未決のまま（他モデルの刻印照合）。本表は SH6/AJ2 前提 |
| U6 | 未決のまま（PW-NA1） |
| U7 | **ほぼ決着**。Wiki「電源ボタンは PC の電源ボタンと同じ動作」により KEY_POWER→TK_ACTION_POWER の採用を正当化。ただし buildbrain フォークで実際に KEY_POWER が来るかは U4 とあわせて統合フェーズで確認 |

## 7. layout 設定の整理と変更ファイル一覧

- `tk_kbd_config.layout`: 内蔵キーボードのキーマップは1つしか
  存在しないため、JIS/US の区別を廃止。enum には
  TK_KBD_LAYOUT_BRAIN(=0) を置き、JIS/US は旧保存値の互換のため
  残すが `tk_kbd_config_sanitize()` が常に BRAIN へ矯正する
  （表は layout で分岐しない）。
- `shell/src/kbdsettings.c` / `shell/include/kbdsettings.h`:
  load は layout を読まない（旧 "jis"/"us" は読み捨て）、
  save は `"brain"` を書く。コメントに理由を明記。
- `shell/src/shellui.c` / `shell/include/shellui.h`（**最小変更**）:
  クイック設定の「配列: JIS/US」トグルボタンを除去
  （存在しない切替を設定画面に残すのは不整合のため）。
  構造体フィールド `kbd_layout_btn`、`on_kbd_layout`、
  ラベル更新の該当分岐も除去。リピートプリセットは不変。
  ※ DE ラインへ: shellui.c への変更はこの1点のみ。
- `shell/tests/test_shell.c`: キーボード設定ブロックを新契約に
  更新（既定 BRAIN、配列トグル不在の確認、保存値 "brain"、
  旧値 "us" が読み捨てられること）。
- `osk/tests/test_osk.c`（**最小変更**、U グループのみ）:
  Shift+2 の期待値を Brainux キーマップの `@` に（旧 JIS `"`）、
  KEY_HOME タップのコメント/チェック名を実態に合わせて更新。
  KEY_ESC で composition が解除される期待はフェイスバリュー
  採用により**元のまま成立**する。
- `include/tk.h`（**追加のみ**）: tk_key に TK_KEY_INSERT を追加
  （履歴キー＝Insert の写像先）。先行して追加した辞書キー用
  TK_ACTION_* は、§1 の判断により不要となったため差し戻し済み。

## 8. テスト結果（2026-11-20、全て実測）

- ツールキット: textcore 97 / tk 58 / tk2 121 / font 22 /
  **test_kbd 131**（旧 107 から書き直し）/ tk3 95 — 全 PASS
- シェル: test_desktop 182 / **test_shell 71** / test_iconcache 21 /
  test_wm 51 / test_backend 25 / test_assist 23 /
  test_greeter_auth 13 — 全 PASS
  （`make tests` が test_session で止まるのは tests/test_session.c
  不在による既存の別ライン WIP の問題で、本修正とは無関係）
- OSK: **test_osk 302** — 全 PASS
- `make sdk-check`: SDK-OK（公開 API のみ）

## 9. 残る注意点

- 記号レイヤのリピート: SH3 世代で「記号が連続入力になる」実地報告
  がある（出典 §2）。tk_kbd の合成リピートは単一ソース設計で
  二重化は起きないが、実機のドライバ側 autorepeat との相互作用は
  統合フェーズの evtest で確認すること（リピート発生源を
  TK_KBD_REPEAT_KERNEL に切り替える判断材料）。
- 本表は PW-SH6 / PW-AJ2（Sx5〜Sx7 世代）前提。旧世代は特殊キーの
  対応が世代で異なる（Wiki の対応表参照）ため、他世代へ広げる際は
  世代別の表が要る。

## 10. 追記（2026-11-21）: 記号レイヤのメディアキーを ACTION へ写像

- D5 の「VOLUME* / MUTE / BRIGHTNESS* は発行されない」は基本
  レイヤだけを見た当時の結論だった。pin コミット（11c11c922）の
  DTS を直接確認した結果、`keymap-symbol` にはメディア割当が
  存在する（コミット b0ac9f045、DTS 自身のコメント付き）:
  (2,3) KEY_VOLUMEDOWN / (4,3) KEY_VOLUMEUP /
  (3,3) KEY_BRIGHTNESSUP / (1,3) KEY_BRIGHTNESSDOWN /
  (6,3) KEY_MUTE。基本レイヤの同じ接点は通常の矢印/Space で、
  メディアコードは記号キー押下中にドライバが出すコードのみ。
  詳細は REPORT-os-internals-1.md §1 と REPORT-mediakeys.md。
- tk_kbd の表で上記5コードを KIND_IGNORE から ACTION へ変更し、
  既存の受け口 TK_ACTION_VOLUME_UP / VOLUME_DOWN / MUTE /
  BRIGHTNESS_UP / BRIGHTNESS_DOWN へ接続した（KEY_POWER と同じ
  押下のみ・リピートなしの ACTION 意味論）。受け口に無い操作は
  無かったため、新設した ACTION は無い。
- test_kbd は 139 → 179 件（表の種別確認10件、MD グループ
  33件を追加し、旧「volumeup は無音」の期待2箇所を置換）。
  ツールキット全体は 3,577 → 3,617 件・全 PASS・回帰ゼロ。
