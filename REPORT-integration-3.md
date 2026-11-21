# 統合フェーズ第2段 — sd-v2.img の QEMU ゲスト実起動・実操作による合格判定

作成: 2026-11-21（段階ごとに追記する）

## 0. 位置づけ・方法・規律

- 対象: `image-work/sd-v2.img`（第1段で発行、ホスト側検証102項目 OK、
  REPORT-integration-2.md が内容の正本）。**原本は書き換えず**、作業用
  コピー `sd-v2-work.img`（md5 `79794e2084662ae0f6861e85b6b229b7`、
  原本と一致を確認済み）を使う。eMMC も作業用コピー `emmc-int.img`
  （pristine `brain-qemu/emmc/emmc.img` の複製）を使う。
- ゲスト環境: QEMU 側プロジェクトの Brain モデル
  （`brain-qemu/qemu/build/qemu-system-arm`、machine `pw-aj2`、
  `-accel tcg,tb-size=64`）。起動は実機の正規経路（§1.5 で確定）:
  ブート ROM が eMMC のブートストリームを読み Stage1 の **EBOOT** が
  起動、EBOOT のファームウェア更新経路が SD 上の **`edsh6exe.bin`
  （nkbin_maker が NK.bin 形式に詰めた U-Boot）** を実行し、U-Boot が
  SD の zImage+DTB で Brainux を起動する。WinCE と brainlilo は
  現行 buildbrain の起動には関与しない（lilo は旧世代方式）。
- 先行実績（QEMU 側）: p22 でチェーンブート到達、p24 でリリース版
  Brainux が rootfs マウント→systemd→シリアルログイン到達、
  p38 でパネル（greeter）ログイン成功。本段はそれを土台に、
  **我々の sd-v2.img** で同じ到達ができるかと、その先の実操作を判定する。
- RAM 規律: VM 全体でゲストは最大2つ。他チャットのゲストが2つ
  埋まっている間は起動せず待機する。ゲスト RSS は各段階で記録する。
  経過: 着手時2つ → 1つ終了で枠が空き第1回起動（後述）→ その後
  他チャットが2つ目を再投入したため、競合を避けて自ゲストを停止し
  再び待機中（本節は随時更新する）。
- 判定は項目ごとに **合格 / 不合格 / ゲストでは判定不可** を明記し、
  証拠（シリアルログ `int3/serial*.log`、JPG 撮影 `int3/shots/`、
  実測値）を対応づける。ホスト側の描画・数値だけで合格にしない。
- 作業物は全て `image-work/int3/` に置く。repo へ commit / push しない。

## 1. 環境の実測（着手時）

- 稼働中ゲスト: 着手時2つ（他チャットの p66/p68 ライン）→ p68 終了後に
  1つとなり、本段のゲストを起動（合計2つ）。
- eMMC 作業コピーの使用前 md5: `612e8a0307436b59852bb39ede358fa8`
  （項目11の照合用、`emmc-int.md5.before`）。
- ディスク空き: 109G（逼迫なし）。

## 1.5 起動経路の確定（一次資料と実測）

当初「WinCE → brainlilo → U-Boot」チェーンを前提にしたが、ユーザーの
指摘と repo の一次資料で誤りと確定し、次の経路に差し替えた（本段の
全判定はこの経路の起動の上で行う）:

- **buildbrain/README.md**「Build and make NK.bin」: nkbin_maker が
  `u-boot.bin` を `nk.bin` に詰める。
- **buildbrain/image/build_image.sh**: `nkbin_maker/bsd-ce` で変換後、
  機種別名に改名する（sh4〜sh7 は `edsh${NUM}exe.bin`、つまり
  PW-SH6 では **`edsh6exe.bin`**）。u-boot.bin 本体は別途
  `lilobin/gen3_6.bin` として残るが、これは SD の起動には使われない。
- **QEMU モデル README**（`qemu/hw/sharp-brain/README.md`）: ブート
  ストリームは Stage0 → IVT → Stage1 = **EBOOT**（Freescale i.MX28
  WinCE BSP ブートローダ、Sharp カスタム、`SHARP ED-AA2 V030 /
  ED-SH6` と名乗る）を 0x40060000 にロードして実行する。EBOOT は
  通常は eMMC から WinCE を読むが、**microSD 上の `EDSH6EXE.BIN`
  （nk.bin ラッパ）を実行するファームウェア更新経路**を持ち、
  実機で U-Boot/Linux が起動するのはこの経路。モデル側の bootrom は
  本物の EBOOT バイナリ（eMMC イメージ内）を実行するため、この
  拾い上げは EBOOT 自身のコードで再現される。
- **実物確認**: sd-v2.img の FAT 領域に `edsh6exe.bin`（499,735 B）、
  zImage（4,842,184 B）、imx28-pwsh6.dtb（24,502 B）が入っている
  ことを mdir で確認（第1段ビルドが正規フローどおり）。
- **ゲスト実測**: 本段の起動で、シリアルに U-Boot の
  「Booting from SD ...」→ zImage/DTB ロード →「Starting kernel ...」
  が記録された（`int3/serial.log`）。p22 の run p22a1 でも同経路が
  実証済み（EBOOT → EDSH6EXE.BIN → U-Boot バナー）。
- **EBOOT 本体の生出力を捕捉（第2回起動 run2、`int3/serial-run2.log`
  冒頭）**: 起動直後からシリアルを接続した run では、本物の EBOOT の
  デバッグ出力そのものが得られた: `LoadBootConfig()` →
  `Boot Config OK!!`、`pDriverGlobalWork->model=[EDSH6]`、充電履歴
  （ChargeStartCount 等）、`DisplayInit()` → `ConfigurePanel` /
  `Display first image` / `InitSharp() pass`、`Batt Detect`、
  `Cold Boot OS`、`Battery Voltage = 3696`、`System ready!`、
  `Preparing for download...`、`Press [ENTER] to download now or
  [SPACE]...`。EBOOT が eMMC の設定を読み、パネルを初期化し、
  ダウンロード（= SD の nk.bin 拾い上げ）待機に入る実機どおりの
  振る舞いで、その後に U-Boot バナー → カーネル起動へ進んだ。
  **欠けたリンクは無い**: bootrom → 本物 EBOOT → SD の
  edsh6exe.bin（U-Boot）→ SD の zImage/DTB → Linux の全段が
  ゲスト内で実コードで再現された。
  - 副次観測: EBOOT の `BeepSound()` で `I2C Timeout ERROR!`
    （モデル側で I2C 先のビープ音デバイスが未再現とみられる。
    ブート継続への影響なし。モデル側のギャップとして記録）。
  - U-Boot バナーの CPU 表示は `Freescale i.MX?? rev0.0 at
    24 MHz`（この表示は PLL 確定前の参照値。カーネル側
    clk_summary の実測は項目3で別途取得する）。
- なお p22 Test B の BrainLILO 経由起動は、旧方式の検証として
  モデル上で動作した記録があるが、現行イメージの正規経路ではない
  ため本段では使わない。

## 2. 判定結果サマリ

（実施しながら埋める）

| # | 項目 | 判定 | 証拠 |
|---|---|---|---|
| 1 | ブート（greeter→セッション） | **不合格（イメージ単体ではセッション開始不能。要手当てで到達は実証）** | §3.4 |
| 2 | 削減カーネルの健全性 | 合格 | §3.4 |
| 3 | クロック実レート | 記録完了（CPU 24 MHz） | §3.4 |
| 4 | バックライト | 制御経路は合格／明るさの目視反映はゲストでは判定不可 | §3.4 |
| 5 | アプリ実操作（両モード） | **不合格**（2件の独立した根本原因。ブラウザとシェルのモード切替は動作） | §3.5 |
| 6 | OSK と IME | ゲストでは判定不可（notes の表示欠陥で所定手順を実施できず） | §3.5 |
| 7 | メディアキーとミュート | ゲストでは判定不可（モデルのキー表にメディアキーが無く押下を再現できない） | §3.5 |
| 8 | プレイヤー再生 | **不合格**（プレイヤーが音声デバイスを開けない。下位経路は部分的動作） | §3.5 |
| 9 | アイドル時メモリ | 合格（デスクトップ 35 MiB 使用） | §3.5 |
| 10 | 書込み中リセット | 合格 | §3.6 |
| 11 | SD/WinCE 併用 | 合格 | §3.7 |
| 12 | ブラウザ | **不合格（出荷状態ではフォント初期化で終了。配置手当てで表示成功）** | §3.5 |

## 3. 実施記録

### 3.1 第1回起動（正規経路、2026-11-21）

- 起動（wall）: カーネル開始 2s、root マウント 6s、systemd 14s、
  シリアル getty の `brain login:` 到達 264s。ゲスト RSS は
  起動時 332,696 KiB → ログイン到達時 514,768 KiB。
  証拠: `int3/serial-boot1.log`、`int3/shots/boot-*.jpg`。
- U-Boot/カーネル: シリアルに「Booting from SD ...」、
  カーネルイメージ 0x49e2c8 = 4,842,184 B（削減 zImage と一致）、
  DTB ロード、「Starting kernel ...」、`Linux version
  6.1.70-g11c11c922fa4-dirty`、Machine model: SHARP Brain PW-SH6、
  cmdline `root=/dev/mmcblk1p2`（この run では SD が mmcblk1 に
  列挙され、フリップなし）。
- グリーター到達: フレームバッファに **braingreeter2** のログイン
  画面（時刻・日付、user カード、パスワード欄、ログイン/再起動/
  電源オフ、キーボードを開く）を撮影（`int3/shots/boot-final.jpg`）。
  → 項目1の「greeter 到達」まで実証。GUI ログイン→デスクトップは
  次回起動で実施（第1回はシリアル側バッテリを優先し、su の
  パスワード相違（root の実パスワードは後述）で詰まったため）。
- シリアルログイン: user/brain で成功（`int3/serial-batt1.log`）。
  オフライン照合（rootfs-v2 の /etc/shadow を crypt 照合）:
  **root のパスワードは 'root'、user は 'brain'**。また
  /etc/sudoers.d/user で user に sudo 全権があることを確認。
  以後の管理コマンドは sudo を使う。
- 項目2の部分実測（同 log）:
  - DRM: `[drm] Initialized brain 1.0.0 for 80030000.lcdif`、
    fb0 = braindrmfb、/sys/class/drm に card0 / card0-DPI-1。
  - MMC: mmcblk1 = SD 3.00 GiB（root）、mmcblk0 = eMMC 8.00 GiB
    （p1〜p4）が列挙。
  - 音声: 起動直後に `mxs-audio-bu26154: Failed to register card:
    -517`（probe 遅延）→ その後 `BU26154 codec found`、
    ALSA device list に `#0: mxs_bu26154`、/proc/asound/cards に
    card0 として登録完了。
  - `systemctl --failed` は **brain-mode.service のみ**
    （Apply the persisted brain gadget mode）。他は失敗なし。
  - lsmod は4行のみ（cfg80211 等。大半は組込み）。
  - 気になる点（要追跡）: systemd の `Failed to find module
    'autofs4'`、cfg80211 の regulatory.db 読込失敗（-2、任意
    ファイル欠落の常套）、journald の ACL 警告（機能影響なし）。
- 項目4の部分実測: /sys/class/backlight/backlight@0 が存在し
  max_brightness=7、brightness=7、actual_brightness=7（読取り）。
  書込み試験は次回起動で実施。
- 項目9の部分実測（シリアルログイン直後・GUI ログイン前）:
  free -m で total 117 / used 36 / free 8 / buff-cache 82 /
  available 80（MiB）。デスクトップアイドル値は次回起動で実測。
- 項目3の部分実測: BogoMIPS 875.72（/proc/cpuinfo）。ただし
  TCG では calibrate_delay がホスト速度で回るため、この値は
  ゲストのクロックレートを反映しない。clk_summary（debugfs、
  要 root）は次回起動で sudo により取得する。
- **項目11の先行証拠**: 第1回起動（ブート〜シリアルセッション〜
  強制終了）を通して eMMC 作業コピーは **1バイトも変化なし**
  （md5 使用前後とも `612e8a0307436b59852bb39ede358fa8`）。
  Brainux 経路が eMMC に書かないことを実証。WinCE の通常起動
  確認は最終段階で実施する。

### 3.2 第2回起動（run2 / run2b、2026-11-21）

- 枠の経過: 第1回終了後、他チャットがゲストを再投入して2枠が
  埋まったため待機。空きが出た時点で run2 を起動（着手前チェックで
  既存1ゲストを確認）。途中、私の誤操作で run2 の駆動スクリプトを
  停止したが、ゲスト自体（detached 起動）は無事で、run2b が
  再接続して継続した。
- ブート: EBOOT 生出力（§1.5）→ U-Boot 2021.07 バナー →
  「Booting from SD ...」→ カーネル → `brain login:` 到達まで
  wall 153 秒（第1回より速いのは並行ゲストが1つだけだったため）。
  ゲスト RSS: 接続時 322,188 KiB → ログイン到達時 461,856 KiB。
  証拠: `int3/serial-run2.log`、`int3/shots/run2-bootdone.jpg`。
- シリアルログイン: user/brain で成功（run2 の記録に READY 応答と
  sudo プライミング成功）。run2 のコマンド駆動はプロンプト同期の
  不具合で2件タイムアウトしたため、駆動方式を drv.py 内蔵 cmd()
  （プロンプト終端待ち）に切り替えた run2b でバッテリを再実施する。
  （実施結果は次節に追記）

### 3.3 run2b（バッテリ再実施の試み — 駆動側の不具合で未達）

- シリアルログインが4回連続で失敗（`int3/serial-run2b.log`）。
  ログの精査で原因は**駆動スクリプト側**と特定: パスワード待ちの
  照合に開始マークを使わなかったため、バッファ内の古い
  `Password:` に即マッチしてパスワードをユーザー名入力中に送り込み、
  以後の入力が1行ずつずれて全て `Login incorrect` になった。
  イメージ側の問題ではない（同じパスワードで run1/run2 は成功）。
- GUI ログインも未達: グリーターでパスワード欄に `brain` の
  5ドットが入ったことは撮影で確認（`run2b-typed.jpg`）したが、
  ログインボタンのタップ後 10〜18 秒でも画面はグリーターのまま
  （`run2b-desk-00.jpg` は typed とピクセル同一）。認証失敗が
  失敗回数ロック（pam_faillock）に触れた可能性を含め、run2c で
  root ログインして auth ログと faillock 状態を確認してから
  再試行する。**この時点では項目1は未達**（greeter 到達まで）。

### 3.4 run2c〜run2h: ログイン不達の切り分けとセッション欠陥の特定

駆動側の不具合を潰しながら切り分けた結果、**イメージ側の統合欠陥**
を1件特定した。経緯と証拠:

- 駆動側の教訓（イメージの問題ではない）: QMP のキーコードは
  小文字 `backspace` が正しく、`BackSpace` では1件も届かない。
  また getty への入力は開始マーク付き照合でないと1行ずれる。
  これらを是正した上で以下を確認した。
- **greeter の認証そのものは正常**: braingreeter2 の認証は PAM では
  なく /etc/shadow への直接 crypt() 照合（rebuild/shell/greeter/
  greeter2_auth.c）。ゲスト内で greeter 自身の検証モード
  `braingreeter2 --check user brain` が **RC=0**、shadow は
  yescrypt（`$y$j9T$…`）。pam_faillock の記録は空（ロックではない）。
- **タップではログインが発火しない**: パスワード欄を空にして
  `brain`（5ドットを撮影で確認）→ ログインボタンを短押し/長押し
  しても無反応。ソース上、認証失敗時は「欄がクリアされ
  『パスワードが違います』が出る」はずが、その兆候が一切無い
  → ボタンの on_click 自体が発火していない。**Enter キーでは
  即座にログイン成功**（greeter が username を出力して終了）。
  タップ座標の写像ずれが疑われ、較正は §3.5 で実施（継続課題）。
- **本丸の欠陥 — braindesk2 が起動できない**: セッションログ
  （`/root/.local/state/brainux/session.log`、スクリプトの HOME が
  root のためここに書かれる）に以下が記録されていた:
  ```
  login: user
  braindesk2: session build failed
  braindesk2 exited 1
  falling back to the X session
  (EE) xf86OpenConsole: Cannot open virtual console 1 (Permission denied)
  xinit: giving up
  ```
  root シェルからセッションスクリプトと同一の引数で braindesk2 を
  手動実行しても `session build failed`（RC=1）で再現。
- **根本原因**: braindesk2 の WM サーバはソケットを
  `$XDG_RUNTIME_DIR/bdwm.sock` に作る（rebuild/shell/src/wm.c
  `bd_wm_default_path()`）。セッションスクリプトは
  `XDG_RUNTIME_DIR=/run/user/1000` を渡すが、**この起動経路では
  logind セッションが存在せず /run/user/1000 が作られない**
  （ゲストで `ls /run/user/1000` → No such file or directory を
  確認）。bind が ENOENT で失敗 → bd_session_new が NULL →
  セッション構築失敗。フォールバックの X も VT1 の権限で失敗する
  ため、**出荷イメージのままではログインしてもデスクトップに
  到達できない**（greeter が再起動してループする）。
- **実証**: root で `/run/user/1000` を作り（user 所有・700）、
  greeter から再ログインすると **braindesk2 が起動しデスクトップ
  表示に成功**（`int3/shots/run2h-desk-04.jpg`: アイコン群、
  マスコット、Android モードの下部バーを確認）。
  /run/user/1000 には bdwm.sock と bdnotify.sock が生成された。
  → 項目1は「greeter 到達まで合格、セッション開始はイメージ側の
  欠陥で不合格（要: セッションスクリプトかサービス側で
  /run/user/<uid> の作成を保証する手当て）」。以降の項目5以降の
  検証は、この回避策を入れた状態（= 欠陥の手当てが入った場合と
  等価な状態）で実施する。
- 項目2（同 run の root バッテリ）: dmesg のエラー走査は
  BU26154 の probe 遅延（-517、後に登録成功）、systemd の
  autofs4 欠落、regulatory.db 欠落（-2）、journald の ACL 警告
  のみで、いずれも機能欠落ではない。/proc/asound/cards に
  mxs_bu26154、/dev/snd に controlC0/pcmC0D0p/pcmC0D1c、
  /sys/class/drm に card0/card0-DPI-1 を確認。失敗ユニットは
  brain-mode.service のみ（他に、私の乱れたシリアルログインの
  残骸である session-c1.scope の failed を1件観測 — 手順由来）。
  → **合格**。
- 項目3: debugfs の clk_summary（69行）で確定:
  **cpu クロック = 24,000,000 Hz（親は cpu_xtal）**、pll0 は
  480 MHz で生成されているが ref_cpu の選択先になっておらず、
  hbus / emi も 24 MHz。OS のメス入れで指摘された「起動経路の
  誰も CPU クロックを設定しない」欠陥が、EBOOT 経由では
  半減どころか **XTAL 直結の 24 MHz** として実測された
  （是正は本段の対象外）。BogoMIPS は 898.66（TCG では
  ホスト速度依存のためクロックの証拠にはならない）。
- 項目4: sysfs 書込み 7→3→0→7 の全てで actual_brightness が
  追随（**制御経路は合格**）。ただし輝度 0 でもスクリーンダンプの
  映像は不変（`run2d-bl0.jpg` でも greeter がそのまま見える）
  → モデルはバックライトをフレームへ反映しないため、
  **明るさ変化の目視確認はゲストでは判定不可**（実機のみ可）。

### 3.5 run3a〜run3j: アプリ・ブラウザ・音声・入力の検証

前提として入力経路の限界を確定した（以降の解釈に必要）:

- **タップはゲストに届かない**: /dev/input/event1（mxs-lradc-ts、
  抵抗膜タッチ）をキャプチャしながら QMP のタップを送っても
  **0 バイト**（キーは event0 = brain-kbd-gpio に届く）。QEMU
  モデルのポインタイベントは LRADC タッチに接続されていない
  （モデル側のギャップ）。greeter のログインボタンがタップで
  反応しなかったのもこれで説明が付く（Enter キーでは成功）。
  画面左下の黒い小片はタップ位置とは無関係の固定描画物だった。
  → 本段の GUI 操作はキーボード中心にならざるを得ない。
- **メディアキーも再現できない**: DTS の keymap-symbol では
  記号+矢印/Space が KEY_VOLUMEUP/DOWN、KEY_BRIGHTNESS*、
  KEY_MUTE だが、QEMU モデルのキー表（hw/sharp-brain/brain-kbd.c）
  にシンボル層のメディアコードのエントリが無く、QMP の
  volumeup 等のキーコードを送っても event0 に **0 バイト**。
  → 項目7は**ゲストでは判定不可**（モデル側の不足。DTS の割当て
  自体と、brainreg の shell.conf に volume=50 / brightness=100 が
  永続化される仕組みの存在は確認済み）。

項目5（アプリ実操作）— **不合格**。アプリ群は起動しても画面に
一切出ない。根本原因は2件、それぞれ独立:

1. **fb 直描きアプリのキャンバス寸法不一致**: calc / clock /
   files / notes / player / tasks は `tk_canvas_new(480, 272)`
   で作ったキャンバスを `tk_fb_flush_damage()` で /dev/fb0 に
   流すが、ツールキットの flush は **キャンバス寸法が fb
   （854×480）と完全一致しないと 0 を返して何も描かない**
   （rebuild/src/tk_fb.c の寸法チェック）。ゲストで実証:
   プロセスは生存し続けるが、braindesk2 を SIGSTOP して
   fb を空けてもアプリの描画は一切現れない。ホスト試験では
   480×272 のメモリ fb を使うため検出されなかった統合欠陥。
2. **recorder / pkg / admin / settings の main が即終了**:
   UI を構築して即座に解放し exit 0 するだけの main になって
   おり、イベントループを持たない（ゲストで bd-settings /
   bd-recorder の RC=0 即終了を確認）。対話アプリとして
   未配線。

- 対照的に**ブラウザ（nsfb-brain）は WM クライアント**として
  正常にウィンドウ表示され（項目12参照）、**シェルのモード
  切替は全体に効く**: brainreg `ui/mode` を winui に上書きして
  再ログインすると、下部バーが WinUI 形タスクバーに変わり
  （`run3j-winui-desk-02.jpg`）、ブラウザのツールバーも
  Edge 形（☆・ホーム・…配置）になった
  （`run3j-ns-winui-b.jpg`）。Android 形は
  `run2h-desk-04.jpg` / `run3i-ns-b.jpg`。→ モードキー正本の
  仕組み自体は合格。アプリ個別の両モード画面は、上記欠陥の
  ため撮影不能。

項目12（ブラウザ）— **出荷状態では不合格、手当てで表示成功**:
nsfb-brain はフォント初期化で終了する（"Unable to initialise
the font system"）。ログでは既定フェイスを
`/usr/share/netsurf/brainui-jp-subset.ttf` に解決しようとして
失敗するが、第1段の統合はフォントを
`/usr/local/share/fonts/libbrainui2/` に配置していた
（**配置パスの不一致**）。3つの TTF を /usr/share/netsurf/ へ
コピーすると起動し、ローカルページ（日本語見出し・英数字・
CSS の赤ブロック）を正しく描画した（`run3i-ns-b.jpg`、
winui は `run3j-ns-winui-b.jpg`）。NetSurf のログに
"ui mode: android" があり、モード読取りも確認。

項目8（プレイヤー再生）— **不合格**（層別に記録）:
- bd-player は ALSA の `"default"` を開く（mk_alsa がデバイス名
  NULL を渡す）が、このイメージには mxs_bu26154 用の default
  PCM 定義が無く `Unknown PCM default` で再生開始不能。
- そもそもデスクトップユーザが音声デバイスを開けない:
  /dev/snd は root:audio 660 で、user の所属グループに audio が
  無く、logind セッションも無いため uaccess の ACL も付かない
  （項目1のセッション統合不足と同根）。
- 下位経路の切り分け: root で `aplay -D hw:0`（ステレオ
  44.1 kHz。モノラルは hw が受付けない）に生成 WAV を流すと、
  ホスト側キャプチャ（int3/audio.wav）に **440 Hz 正弦波が
  フル振幅（max 11999 / 12000、零交差 439 回/秒）** で記録
  された → カーネル SAIF + モデルの出力経路は生きている。
  ただし aplay は途中で `pcm_write: Input/output error` で
  終了した（TCG 環境でのストリーム異常とみられ、モデル/
  タイミング側の嫌疑として記録。実機で再現するかは不明）。

項目9（アイドル時メモリ）— **合格**: デスクトップアイドル
（Android モード、起動後安定時）で free -m: total 117 /
**used 35** / available 81（MiB）。braindesk2 の RSS は
18,300 KiB。主要プロセスは systemd 11.2 MB、ユーザ systemd
10.0 MB、journald 7.3 MB、logind 7.2 MB の順。ブラウザ併用時
でも used 41 MiB（nsfb-brain RSS 26.3 MB）。greeter のみの
状態では used 31〜32 MiB。

項目6（OSK と IME）— **ゲストでは判定不可**: 本来の手順
（bd-notes）は項目5の欠陥1でアプリが表示できず実施不能。
代替としてブラウザの URL 欄に Ctrl-L でフォーカスし
"nihongo" を打鍵したところ、画面最下端に preedit 帯の
上端だけが覗き、romaji がそのまま表示されるのは確認できた
（`run3j-ime-typed.jpg` と拡大 crop）。ただし (a) preedit 帯が
画面下端にほぼ隠れる位置に出る、(b) Space でも変換候補や
変換結果は観測できなかった、(c) OSK はタップ前提で開けない。
→ 変換の成否は本段では確定できない。IME エンジン
（libbrainime + 辞書 brainux3.bid3）は braindesk2 の OSK
バックエンドとして組み込まれていること自体はセッション
コードと起動で確認済み。

### 3.6 項目10: 書込み中リセット（run3k1 / run3k2）

- 仕掛け: デスクトップ稼働中のゲストで、ユーザ権限の2つの
  書込みループを回した: (a) ファイルへの追記ループ
  （`line N` を連続追記）、(b) brainreg 形式 conf の原子書換え
  ループ（tmp に書いて rename、volume を 41/42 で反転）。
  2,978 行まで進んだ時点で **QMP quit で電源断相当の強制終了**。
- 再起動: ブートログに `EXT4-fs (mmcblk1p2): recovery complete`
  → ordered data mode でマウント（ジャーナル再生が正常動作）。
- 検証結果（**合格**）:
  - 追記ファイルは 3,085 行まで記録され、**全行が欠落・重複・
    破損なく連番どおり**（検証スクリプトで bad=0）。
  - 原子書換え conf は `volume = 42` の**完全な値**だけが残り、
    書きかけの混在は無し。書き込み中だった tmp ファイルが
    残骸として残るが、これは原子書換えの設計どおりの挙動。
  - 失敗ユニットは既知の brain-mode.service のみ。再度
    /run/user/1000 を作ってログインし直し、braindesk2 の
    デスクトップまで復帰した（`run3k2-desk-*.jpg`）。

### 3.7 項目11: SD/WinCE 併用（run3l / run3m / run3n）

- **eMMC 無傷**: 本段の全 Linux 実行（通常起動〜強制終了を
  含む）の後も、eMMC 作業コピーの md5 は使用前と完全一致
  （`612e8a0307436b59852bb39ede358fa8`）。Brainux は eMMC に
  1 バイトも書いていない。
- **WinCE 通常起動**: SD を外して起動すると WinCE が立ち上がり、
  初期化確認ダイアログで [N]（いいえ）を選ぶと初期設定
  ウィザード（日付・時刻設定）→ **ホーム画面**（ホームタブ、
  辞書検索、カテゴリ、コンテンツカード群）まで到達した
  （`run3l-wince-05.jpg`、`run3m-home-03.jpg`、
  `run3n-step-01.jpg`）。その後 Enter で一括検索が開くなど
  入力への応答も正常。なお初期設定ウィザードから始まるのは
  この eMMC イメージ自体の初期状態であり、Brainux の影響では
  ない（md5 一致がその証拠）。
  → **合格**。

## 4. 本段で確定した欠陥・知見のまとめ

イメージ側の欠陥（要手当て、いずれもホスト試験では検出され
なかったもの）:

1. **セッションが起動できない**（項目1）: /run/user/<uid> が
   作られないため braindesk2 の WM ソケット bind が失敗し、
   ログインしてもデスクトップに入れない。フォールバックの
   X も VT 権限で失敗する。手当て: セッション開始前に
   ランタイムディレクトリの作成を保証すること。
2. **アプリが画面に出ない**（項目5）: (a) fb 直描きアプリ
   6本はキャンバス 480×272 と fb 854×480 の寸法不一致で
   ツールキットの flush が常時 0 を返す。(b) recorder / pkg /
   admin / settings の main は即終了で対話動作しない。
3. **ブラウザがフォント初期化で終了**（項目12）: 既定
   フェイスの解決先 /usr/share/netsurf/ にフォントが無い
   （実体は /usr/local/share/fonts/libbrainui2/）。
4. **音声の利用者経路が閉じている**（項目8）: プレイヤーは
   ALSA "default" を開くが default PCM の定義が無く、
   デスクトップユーザには /dev/snd の権限（audio グループ /
   セッション ACL）も無い。カーネル+モデルの出力経路自体は
   root の aplay で動作確認済み。
5. **CPU クロックが 24 MHz のまま**（項目3）: PLL 480 MHz は
   生成されるが CPU の親に選ばれていない。OS のメス入れで
   指摘済みの未設定問題の実測確定（本段では是正対象外）。

モデル側（QEMU）のギャップ（実機では起きないもの）:

- QMP のポインタイベントが LRADC タッチ（event1）に届かず、
  タップ操作を再現できない。シンボル層のメディアキー
  （音量/ミュート）もモデルのキー表に無く再現できない
  （項目7が判定不可の理由）。
- バックライトの輝度はフレームに反映されず、明るさの目視
  判定はできない（項目4）。EBOOT のビープ音で I2C タイム
  アウトが出る（ブートへの影響なし）。
- aplay 再生が途中で I/O エラーになった（ストリーム異常。
  TCG/モデル側の嫌疑）。

合格した主要項目: 項目2（削減カーネルの健全性）、項目9
（アイドル 35 MiB）、項目10（書込み中リセット耐性）、
項目11（eMMC 無傷 + WinCE 通常起動）。起動経路は実機どおり
（EBOOT → SD の edsh6exe.bin → U-Boot → カーネル）で全段を
実証した（§1.5）。
