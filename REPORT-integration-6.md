# 統合フェーズ第5段レポート — クロック是正＋PARTUUID の最終統合（sd-v5.img）

- 実施: 2026-11-21
- 対象: sd-v4.img（第4段の検証済みイメージ、無変更で残置）→ **sd-v5.img**
- 内容: ゲスト検証済みの2点を第4段イメージへ統合する。
  1. **CPU クロック是正**（24 MHz → 454 MHz 級。`os-surgery/REPORT-clock-fix.md` §4 で検証済み）
  2. **ルートデバイス番号レースの恒久対策（PARTUUID 化）**（同 §6 で検証済み）
- 証拠の置き場: `image-work/int6/`（shots/ = ゲスト撮影 JPG、serial-*.log、results.txt）。

---

## 1. 統合前の同定（差し替えの前提確認）

差し替えに先立ち、3つの U-Boot が同一系統であることを実物で確認した。

- sd-v4 の FAT 内 `edsh6exe.bin` は `image-work/uboot-work/edsh6exe.bin` と
  バイト一致（499,735 B、md5 `04dad1d3e0e8809130e9428e29f92e93`）。v4 は
  土台の未是正 U-Boot を出荷している。
- NK 形式の構造は3者とも同一（27 B ヘッダ＋ペイロード＋12 B フッタ）で、
  ペイロードは各ビルドの `u-boot.bin` とバイト一致する:
  - クロックのみ版: `clock1/edsh6exe.bin` のペイロード =
    `clock1/uboot-build-sh6/u-boot.bin`（500,404 B）
  - クロック＋PARTUUID 版: `clock1/edsh6exe-partuuid.bin` のペイロード =
    `clock1/uboot-build-pu6/u-boot.bin`（503,596 B、md5
    `e808633611db68a1780b1f614d561fd6`）
- 全て U-Boot 2021.07。ペイロード長の推移は 499,696 → 500,404（クロック
  是正で +708）→ 503,596（PARTUUID で +3,192）で、追加文字列は PARTUUID 版の
  `part` コマンドヘルプと `setroot` / `sdrootpart` / `rootuuid` のみ。
  pu6 の ELF には `mxs_setup_vddd` / `mxs_setup_emi_clock` /
  `mxs_setup_cpu_clock` のシンボルが実在し、クロック是正と PARTUUID が
  同一ビルドに入っていることを確認した。想定外の差は無し。

## 2. 統合手順

### 2.1 rootfs-v5（update-rootfs5.sh）

- rootfs-v4 をツリーごと複製（setgid 毒性ガードは第4段と同一）。
- `boot.mount` を repo の override
  （`buildbrain/os-brainux/override/lib/systemd/system/boot.mount`）で
  差し替え: `What=/dev/mmcblk1p1` → `What=/dev/disk/by-label/boot`。
  FAT ラベル `boot` は全 assemble スクリプトが `mkfs.fat -n boot` で
  固定しており、イメージ生成をまたいで安定する（§6.4 の棚卸しで
  番号依存はこの1件のみだったことを、v5 ツリーでも再監査して確認）。
- 第4段の parity 是正（アセット到達可能性、カタログ可視11/非表示11、
  壁紙既定）と第3段の音声是正が残っていることを assert で確認。
- **ジャーナル再生の扱い**: §6.4 の落とし穴は「既存 ext4 イメージを
  debugfs でオフライン編集する」場合のもの。本構築経路はツリーから
  `mke2fs -d` で p2 を新規生成するため、オフライン編集自体が存在せず
  巻き戻りは起こらない。加えて verify で生成後 p2 の `e2fsck -n`
  クリーンを確認している（§3）。

### 2.2 sd-v5.img（assemble_image5.sh）

- レイアウトは assemble_image4.sh と同一（p1: 64 MiB FAT32 `boot`、
  p2: ext4、開始セクタ 2048 / 133120）。
- U-Boot はステージング方式に変更: 土台セット（uboot-work の全機種分）
  を `uboot-stage5/` に複製し、`edsh6exe.bin` だけを
  `clock1/edsh6exe-partuuid.bin` で置換、md5
  （`32c2ec99796ed58680ff7c85b1edd1f0`）を assert してから FAT に
  配置する。今後イメージを作り直しても、未是正 U-Boot が静かに
  混入することはない。他機種の exe は土台とバイト一致のまま。
- 恒久化の正本は repo 側にある: U-Boot は `u-boot-brain` ツリーの
  変更（`board/sharp/common/cpu_clkdiv.c`、`board/sharp/pwsh6/pwsh6.c`、
  `include/configs/brain_mx28_common.h` の `setroot`、9機種 defconfig の
  `CONFIG_CMD_PART=y`。いずれも未コミットで親の push 待ち）から
  `clock1/build_pu.sh` の手順（pwsh6 defconfig → u-boot.bin →
  nkbin_maker）で再生成でき、boot.mount は `os-brainux/override` が
  正本。イメージ側スクリプト（update-rootfs5.sh / assemble_image5.sh /
  verify_image5.sh）は image-work に恒久化した。

### 2.3 発行物

- **sd-v5.img: 3,221,225,472 B、md5 `3fb5f06621d3e2e6db9ed420420a8313`**
- MBR ディスク署名は `a89d6de8`（生成ごとに変わる）。root の PARTUUID は
  U-Boot が起動のたびに解決するため、署名が変わっても追随する
  （期待値 `a89d6de8-02` は検証時の実測用）。
- sd-v4.img（md5 `178ea7f536d47ec3ddc3eb6aa581d823`）と sd-v3.img
  （同 `98ef77fe1967f1dcaa0c785451a6e308`）は無変更で残置（発行後に
  md5 再確認済み）。

## 3. ホスト側検証（verify_image5.sh）

第4段の138項目に第5段の17項目を追加し、**全155項目 OK**:

- p1 の `edsh6exe.bin`（ルートと `nk/` の両方）がクロック＋PARTUUID 版
  NK とバイト一致、そのペイロードが pu6 の `u-boot.bin` とバイト一致。
- ペイロードに `PARTUUID=` / `setroot` / `sdrootpart` が存在し、pu6 の
  `.config` に `CONFIG_CMD_PART=y` / `CONFIG_PARTITION_UUIDS=y`、
  ELF にクロック設定シンボル3点がある。
- 他機種の exe 7点は土台（uboot-work）とバイト一致（無変更）。
- p2 の `boot.mount` が `What=/dev/disk/by-label/boot` で、コメント外に
  mmcblk デバイス参照が無い。p2 の ext4 は `e2fsck -n` で clean。
- 第4段までの全項目（カーネル削減・DTB 修正・アセット到達可能性・
  カタログ・M24 リンク等）も引き続き OK。

## 4. ゲスト全面回帰

方法: 作業コピー `sd-v5-work.img`（FAT に音声スモーク用の
`tone.wav`（440 Hz、2秒）を追加した以外は sd-v5 と同一）と、
pristine eMMC の複製 `emmc-int6.img` を使い、正規経路
（EBOOT → edsh6exe.bin → U-Boot → カーネル）で起動。
ゲストは常時1台のみ（他チャットのゲストと合わせて VM 全体で
最大2台）、`-accel tcg,tb-size=64`、RSS は各ランで記録。

### 4.1 ブート5回（PARTUUID・クロック・panic の確認）

（boot1〜5 の結果表は §4.6 に集約）

各回とも: ログインプロンプト到達、シリアル root ログイン可、
`/proc/cmdline` に `root=PARTUUID=a89d6de8-02`、ルートデバイスの
blkid PARTUUID が一致、clk_summary の ref_cpu = 454,736,842 Hz、
フォールバック警告なし、カーネル panic なし、dmesg の
panic/oops/BUG 計数 0。

- boot1（full ラン）: 壁時計 135 秒、RSS 508,900 KB（起動直後）→
  504,744 KB。free は used 35 MiB（シリアルログイン時点）。
- boot.mount は boot4・boot5 で全項目確認（is-active = active、
  findmnt /boot = /dev/mmcblk1p1、/boot に zImage と
  edsh6exe.bin）。boot1〜3 では、複合コマンドのタイムアウトと
  ハーネスのプロンプト照合アーティファクト（`tr` で末尾改行を
  消した出力にプロンプトが続くと照合できない）のため
  findmnt/ls 側が未取得だったが、is-active 自体は boot2・boot3
  でも active を記録している。製品側の失敗ではなく計測側の
  問題であり、コマンド分割＋末尾マーカー追加で解消した
  （boot4 以降）。同一内容のユニットの実働は PARTUUID ラインの
  boot18 でも確認済み。

### 4.2 デスクトップ両モード

- **android（boot1）**: `int6/shots/v5-desk-android.jpg`。第4段の
  `v4-desk-03.jpg` と構造的に同じ描画: 濃紺壁紙 #102030、自作
  11アプリ＋新アイコン、下部バーはアイコン描画（ランチャー、
  ナビ ◀●■、Wi-Fi、音量、電池＋--%、時計、歯車）、補助は
  手アイコン、マスコットはグリッド外。braindesk2 は user で
  動作し RSS 17,084 KB（第4段 17,024 KB と同水準）。user での
  status PNG 読取り RC=0、session.log のアイコン読込み失敗
  （`unavailable`）は 0 件。アイドル時 free は used 34 MiB で
  第3・4段と一致（退行なし）。
- **winui（boot5）**: `int6/shots/v5-desk-winui-clean.jpg`。第4段の
  `v4w2-desk-02.jpg` と構造的に同じ描画: 濃紺壁紙、同じカタログ、
  WinUI タスクバー（中央にスタートタイル、右にトレイ一式）、
  マスコットはグリッド外、補助は手アイコン。braindesk2 は user で
  動作し RSS 18,168 KB、free は used 35 MiB。
  方式は第4段と同じ「ユーザ brainreg 上書きを事前に仕込んで
  最初のセッションから winui で起動」。仕込みは作業コピーの p2 を
  オフライン編集する形で行い、§6.4 の手順どおり **先に
  `e2fsck -y` でジャーナルを再生してから** debugfs で
  `~/.config/brainreg.d/10-ui.conf` を書いた（検証後に同じ手順で
  除去済み。出荷イメージには一切手を入れていない）。
  なお full ラン内で greeter を再起動して切り替える方式も試したが、
  再起動コマンドが競合下で 120 秒を超えてタイムアウトし、撮影が
  再描画途中に当たって fbcon のブートログ帯が残った
  （`v5-desk-winui.jpg` / `v5-desk-android2.jpg`）。これは
  セッション再起動のタイミング事象であり、クリーンな起動経路
  （boot5）では両モードとも正常描画になることを確認した。

### 4.3 アプリ描画（セッション経由起動、第3・4段と同じ方式）

- bd-settings: 実値を表示（Wi-Fi オン・未接続、輝度 100%、
  電池 不明）。音量行の「取得不可」は第3段からの既知事項
  （設定側の読み取り経路の問題。音声経路自体は §4.4 で確認）。
- bd-notes / bd-files（/home/user の実一覧と容量表示
  「空き 689.2 MiB / 2.8 GiB」）/ bd-calc（全キー描画）/
  nsfb-brain（WM 窓で welcome.html を描画）: いずれも実データ
  描画とプロセス生存を確認。撮影は `int6/shots/v5-app-*.jpg`。

### 4.4 音声スモーク

- デスクトップユーザで `aplay -D default /boot/tone.wav`
  （440 Hz、振幅 12000 の正弦波、2秒）を実行。default PCM は
  開かれ、ホスト側キャプチャ（int6/audio.wav、44.1 kHz
  ステレオ）に提出したトーンのエネルギーと一致する信号が
  記録された: キャプチャ全体の RMS は 8487.1 で、提出トーンの
  理論 RMS 8485.3 と 0.02% 以内で一致し、スペクトルの単一
  ピークを持つ。
- ただしモデル側の再現は完全ではない: ピークは ~896 Hz に
  ずれ（モデルが DMA ストリームを約2.04倍速で消費するためと
  みられる）、aplay は pcm 書き込みでブロックし続けた
  （第3段で記録された pcm_write EIO と同種のモデル/TCG 側
  事象）。イメージ側の判定基準（default PCM が user で開けて
  再生が始まること）は満たす。

### 4.5 eMMC 無傷と WinCE 通常起動

- **eMMC 無傷: 合格**。Brainux ブート全5回の前後で
  `emmc-int6.img` の md5 は `afac07f6a545e38cca27ea5d677e476e`
  のまま不変（土台の pristine クローン `clock1/emmc-clock.img` と
  同一値）。454 MHz 化と PARTUUID 化のどちらも eMMC には
  触れていない。
- **WinCE 通常起動: 合格**。SD を外した構成（eMMC の別複製
  `emmc-int6-wince.img`）で EBOOT が WinCE を起動し、初回
  ウィザード（初期化確認に [N] で回答 → 日付と時刻の設定 →
  …）を経て通常の WinCE アプリ画面「調べる（一括検索）」に
  到達した。キーボード操作でタブ（日本語/英語）が切り替わる
  ことも確認（`int6/shots/wz3-2-00.jpg`、`wz3-4-01.jpg`、
  `v5-wince-home.jpg`）。第4段の到達画面と同一。

### 4.6 結果集約

| 項目 | 結果 |
|---|---|
| ブート5回（panic なし・PARTUUID 解決・454 MHz） | **合格**（boot1〜5 全回。boot4・boot5 は boot.mount active 含め全項目 PASS。boot1〜3 の `boot_mount_active` はハーネスのプロンプト照合アーティファクト（末尾改行なし出力）で未取得だったが、is-active 自体は boot2・boot3 でも active を記録済み。§4.1 参照） |
| ログイン（グリーター→デスクトップ） | **合格**（boot1・boot5 で打鍵のみ、手当てなし） |
| デスクトップ android | **合格**（第4段と構造一致、§4.2） |
| デスクトップ winui | **合格**（第4段と構造一致、§4.2） |
| アプリ描画（設定/メモ/ファイル/電卓/ブラウザ） | **合格**（実データ描画、§4.3） |
| 音声（default PCM、user） | **合格**（イメージ側基準。モデルのピッチずれ・ブロックは §4.4 に記録） |
| アイドルメモリ | **合格**（used 34〜35 MiB、第3・4段と同水準） |
| eMMC 無傷 | **合格**（md5 不変、§4.5） |
| WinCE 通常起動（SD なし） | **合格**（§4.5） |
| ゲスト RSS | 起動直後 464〜509 MB、最終 486〜505 MB（各ランで記録） |

ブート所要（ログインプロンプトまで）: boot1 135 秒、boot2 156 秒、
boot3 94 秒、boot4 116 秒、boot5 95 秒（他ゲストとの競合下の
参考値）。第2・3段（24 MHz）では同条件で数分〜20分超の回も
あったことと比べると大幅に短いが、競合条件が揃っていないため
厳密な比率は主張しない。

## コピー/オーバーヘッド削減

- 本統合はバイナリの置換とユニットファイル1件の差し替えのみで、
  コピーを増やしていない。PARTUUID 解決は U-Boot が起動時に
  `part uuid` を1回実行するだけで、ランタイムのオーバーヘッドは無い。
- クロック是正は「本来の設定値が実行されていなかった」ことの
  是正であり、追加の常駐処理は無い。454 MHz 化でブートと描画の
  壁時計は短縮される方向（TCG 環境では競合の影響が大きく、
  厳密な比率は主張しない。第2段の留保と同じ）。
- イメージ発行はステージング複製（数百 KB）以外に従来手順と
  同じ中間物しか作らない。

## キャッシュ/メモリ管理

- シェル・アプリ・カーネルのキャッシュ方針は第4段から無変更。
- ゲストのアイドルメモリは used 34 MiB（デスクトップ到達後）で
  第3・4段と一致。クロック変更はメモリ使用量に影響しない
  （boot2 のシリアルログイン時点では used 31 MiB）。
- braindesk2 の RSS は 17,084 KB で第4段（17,024 KB）と同水準。

## 実機の現実への対応

- **突然のリセット**: PARTUUID は起動のたびに U-Boot が解決する
  ため、イメージの作り直しや署名変更で root を見失わない。
  解決失敗時は警告付きで従来表記にフォールバックする（安全側）。
  boot.mount の by-label 化も同じ番号フリップ対策で、FAT ラベルは
  生成をまたいで安定する。
- **eMMC には触らない**: 本統合の変更は SD 側（FAT の
  edsh6exe.bin と rootfs のユニット1件）のみ。eMMC は全ブート
  前後で md5 不変（§4.5）。
- **実機への申し送り**（クロック是正レポート §5 の再掲）: 実機の
  初回適用時はシリアルで PARTUUID 解決とフォールバック警告の
  有無、clk_summary を観察すること。VDDD 1550 mV の電気的
  安定性と DRAM タイミング実値の照合は QEMU モデルでは
  観測できないため実機確認事項として残る。eMMC から Linux を
  起動する場合の root は署名 0 由来の縮退値
  `PARTUUID=00000000-03` になる（U-Boot レベルの解決まで
  確認済み、Linux-from-eMMC 起動自体は未検証）。
