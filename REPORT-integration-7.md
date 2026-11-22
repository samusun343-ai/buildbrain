# 統合フェーズ第6段レポート — fbcon コンソールカーソルの漏れ修正（sd-v6）

- 日付: 2026-11-21〜22
- 起点: ユーザー指摘（2026-11-21）。第5段のゲスト撮影（設定アプリ）を見て「左下の黒いのは何？」→ 実体は tty1 のフレームバッファコンソール（fbcon）のテキストカーソルと同定 → ユーザーが「コンソールのカーソル漏れを直しておいて」を選択。
- 成果物: **sd-v6.img**（3,221,225,472 B、md5 `1afc39f089a3321c371591ba83c65635`）= sd-v5 ＋ 本修正。sd-v5.img は無変更（md5 `3fb5f06621d3e2e6db9ed420420a8313` を再確認）。
- 証拠: `int7/`（撮影 shots/、シリアルログ、results.txt、ジャーナル抽出 journal/・journal5/、音声キャプチャ audio-v6-capture.wav）。

## 1. 問題の切り分け（実証済み）

撮影 (x=0, y=462) の黒ブロックは 6×11 px。cmdline は `console=ttyAMA0`（シリアル）＋ `console=tty1` ＋ `fbcon=font:ProFont6x11` で、フォント 6×11 の最終行（42行目×11=462）のカーソルセルと位置・寸法が完全一致した。点滅するため写らないコマもある（消灯相）。

漏れは2経路ある:
1. **カーソル描画**: fbcon が VT カーソルを GUI のフレームバッファ上に描き続ける。
2. **キーエコー**: `braingreeter2.service` は TTYPath=/dev/tty1・TTYReset=yes で、systemd が tty 既定（ECHO オン）を復元する。グリーターもシェルもキー入力は evdev のみで tty1 の stdin を読む者はいないが、tty1 のライン discipline が全打鍵を fbcon 下端へエコーし続ける（第2・3段の「キーエコーが画面下端に写る」と同根）。

## 2. 方式の選択

| 候補 | 判定 | 理由 |
|---|---|---|
| cmdline `vt.global_cursor_default=0` | **採用**（カーソル側） | VT カーソル自体を全 VT で描かなくなる。恒久的で、イメージ側の1パラメータ追加のみ。 |
| sysfs `cursor_blink` で点滅停止 | 却下 | 点滅を止めるだけでカーソル（黒ブロック）が残る。ユーザー指摘の「黒いもの」自体が消えない。 |
| `console=tty1` を外す | 却下 | エコーは止まらない（エコーはライン discipline の性質で、console= の有無と無関係）。パネル上のブートログ表示も失う。シリアル（ttyAMA0）は元々別にあり、デバッグ手段はそちらで保全される。 |
| セッション開始時 `stty -echo`（tty1） | **採用**（エコー側） | systemd の TTYReset の後に効く。サービス停止時に systemd が tty を再リセットするためセッション限定で、後続のコンソールログイン（getty は自前で termios を設定）へ漏れない。 |

## 3. 変更箇所

1. **U-Boot** `include/configs/brain_mx28_common.h`: `bootargs_custom` に `vt.global_cursor_default=0` を追加（1行）。クロック是正＋PARTUUID の系統（ブランチ `brain-clock-partuuid` tip `3b41783add`）の上にのみ適用。ビルド成果物: NK 形式 `clock1/edsh6exe-v6.bin` = 503,655 B、md5 `97f0f05fc8f5bf58dacdf96849e2201f`。編集済みヘッダの保存コピーは `clock1/brain_mx28_common.h.v6`（コミット時の材料）。
   - 罠と対処: 初回ビルドは u-boot-brain のチェックアウトがポインタ `e8fc0d0cf3`（pristine）に復帰していたため系統違い（クロック/PARTUUID なし）を掴んだ。strings 検査で検出し、ブランチ tip にチェックアウトして再適用・再ビルドした。以後 `clock1/build_pu6.sh` はクロックシンボル3点＋PARTUUID＋setroot を assert する。作業後サブモジュールは detached `e8fc0d0cf3`・クリーンに復帰済み。
2. **セッションスクリプト** `rebuild/shell/integration/brainux-session.sh`（正本）: ログ redirect 直後・グリーター起動前に `[ -t 0]` ガード付き `stty -echo` を追加。
3. **イメージ構築**: `update-rootfs6.sh`（rootfs-v6 構築＋第3・4・5段 assert）、`assemble_image6.sh`（U-Boot ステージング＋md5 assert）、`verify_image6.sh`。
4. **ホスト検証: 全158項目 OK**（第5段155＋第6段新規: NK バイト同定、payload 内 `vt.global_cursor_default=0`、p2 の brainux-session に `stty -echo` かつ rebuild 正本と一致、等）。

## 4. ゲスト検証（QEMU -M pw-aj2、-accel tcg,tb-size=64、証拠 int7/）

### 4.1 本修正の効果 — ピクセル検査（決定的証拠）

下端帯 y=455..479 の純黒ピクセル数を領域 A（x<12、カーソル位置）/ B（x<200、エコー範囲）/ C（全幅）で集計（`int7/analyze7.py`）。

- **sd-v6: 全39フレームで A=B=0**。内訳: full ラン24フレーム（グリーター、パスワード打鍵後、デスクトップ、打鍵後、設定/メモ/ファイル/電卓 pre＋撮影、ブラウザ）、WinUI 7フレーム（C=69 はタスクバーの時計・アイコンの黒グリフで漏れ位置 A/B は 0）、diag デスクトップ1、アプリラン7（設定/メモ/ファイル/電卓/ブラウザ、全て C まで 0）。
- 対照（sd-v5 ベースライン）: グリーター/デスクトップ/設定/WinUI で A=54（カーソルブロック）、メモ 211（ブロック＋エコー "abc"）、電卓 103（エコー "78"）、ファイル 66。
- **tty1 の termios 実証**: デスクトップセッション稼働中にシリアルから `stty -a -F /dev/tty1` を取得し `-echo` を含むことを確認。

### 4.2 回帰

- **ブート**: Brainux 7回（boot1/diag/boot2/boot3/winui/audio/apps）。panic 0 回。シリアル検査した全ブートで PARTUUID 実行時解決（v6 の MBR 署名は再生成で `360e9e24` に変わり、ゲスト cmdline は `root=PARTUUID=360e9e24-02` と実行時解決されたものを確認）、cmdline に `vt.global_cursor_default=0`、CPU **454,736,842 Hz** 維持。boot2 は全項目 PASS（壁時計 157 秒、RSS 472,616 KB）。QEMU RSS は各ランで記録（460〜501 MB、tb-size=64 適用）。
- **両モードデスクトップ**: Android（diag ラン、int6 と構造一致を目視確認）、WinUI（winui ラン PASS、braindesk2 RSS 17,076 KB）。マスコットはグリッド外に配置。
- **主要アプリ描画**（apps ラン）: bd-settings / bd-notes / bd-files / bd-calc / nsfb-brain が全て起動・描画（設定画面はユーザーが指摘した当の画面で、左下ブロックなしを目視確認）。
- **アイドルメモリ**: used 35 MiB（v5 の 34〜35 MiB と同等）。
- **音声スモーク**（audio ラン）: boot.mount active・/boot/tone.wav から aplay 再生開始。ホストキャプチャ解析: **RMS 8484.5（提出トーン理論値 8485.3 と 0.01% 以内で一致）**、単一トーンのピーク ~448 Hz（440 Hz 提出、モデルの消費レート揺らぎで近傍ビンに分散）。第5段と同基準で合格。
- **eMMC 無傷**: Brainux 用コピーは全ラン後も md5 `afac07f6a545e38cca27ea5d677e476e` 不変。WinCE 用コピーは WinCE 自身の初回起動書込み（レジストリ等）で変化するのが正常。
- **WinCE 通常起動**: SD なしブートでウィザードを完走し通常ホーム（一括検索）画面に到達（`int7/shots/v6-wince-home.jpg`）。

### 4.3 一過性事象の切り分け（正直な記録）

1. **boot.mount が inactive になるブートがある（boot1・boot3、観測5回中2回）**。切り分け済み: 作業イメージの永続ジャーナルに失敗シグネチャが残っていた — `dev-disk-by\x2dlabel-boot.device: Job .../start timed out` → `Dependency failed for boot.mount`。つまり by-label デバイスの出現が systemd のデバイスジョブ・タイムアウト（既定90秒級）に間に合わない競合で、TCG の vCPU が他ゲストと競合して飢えたブートでのみ起きる。diag/boot2/audio では active・mounted を実証。**この経路は v6 の変更対象外**（ユニットファイルは v5 からバイト同一、カーネル・udev も同一）で、by-label 化（第5段 PARTUUID 対応）由来の既存競合と判断する。実機ではデバイス出現は1秒未満で、実害はゲスト検証環境に限られる見込み。硬化（デバイスジョブのタイムアウト延長ドロップイン等）は本段の範囲外として判断待ちに挙げる。
2. **full ランの GUI ログイン停滞**: パスワード欄に打鍵は届いた（マスクドット5個）が Enter が配送されず、認証試行自体が起きなかった。同一手順は int5/int6 で反復成功し、本段でも diag/winui/apps の3ランで GUI ログイン成功。3ゲスト競合下の入力配送フレークと分類する（機構は未証明）。

## 5. コピー/オーバーヘッド削減

- 修正は cmdline 1パラメータ＋セッションスクリプト2行のみ。ランタイムの常駐物・定期処理は一切追加していない（カーソル消去はカーネルの既定値変更、エコー抑止は termios フラグ1つ）。
- イメージ構築は v5 の正規経路（update-rootfs→assemble）を継承し、U-Boot ステージングに md5 assert を組み込んで再現性を担保。

## 6. キャッシュ/メモリ管理

- 本修正はメモリ使用量に影響しない（アイドル used 35 MiB で v5 と同等を実測）。新規キャッシュなし。

## 7. 実機の現実への対応

- **突然のリセット**: どちらの修正も永続状態を持たない（cmdline は U-Boot 環境由来で毎ブート再適用、stty はセッション開始ごとに再実行）。電源断で壊れる状態はない。
- **オフライン・時刻不信**: 期限・時刻に依存する要素なし。
- **eMMC ノータッチ**: Brainux から eMMC へ書いていないことを md5 で実証（§4.2）。本拠は microSD のまま。
- **実機での初回確認手段**: シリアルコンソール（ttyAMA0）は無変更で保全。パネル上のブートログ（console=tty1）も残した（カーソルだけが消える）。
- 実機申し送り: 実機でも左下ブロックが消えることの目視確認（ゲストではモデル経由の描画で実証済みだが、実機 fb での最終確認は実機待ち）。

## 8. 申し送り

- commit/push は親の担当。本段のコミット材料: (a) u-boot-brain は `clock1/brain_mx28_common.h.v6` の差分（`brain-clock-partuuid` ブランチの上に載せる）、(b) rebuild ブランチは `rebuild/shell/integration/brainux-session.sh` の変更＋本レポート＋image-work のスクリプト群。
- 判断待ち: boot.mount デバイスタイムアウト競合の硬化（§4.3-1）。発生は TCG 競合環境に限られる見込みだが、ゲスト検証で /boot 前提の確認（音声等）が確率的に落ちるため、検証基盤の安定性のためにも小さいドロップイン追加を検討する価値がある。
