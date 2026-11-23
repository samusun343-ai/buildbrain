# CPU クロック是正 — EBOOT→nk.bin(U-Boot) 経路での再分析・設計・実装・検証

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


- 位置づけ: OS のメス入れ第1弾（REPORT-os-surgery-1.md §2）の是正実施編。
  第1弾の分析は旧経路（WinCE → brainlilo → U-Boot）前提だったため、
  現行の正規経路（EBOOT が SD の nk.bin を直接起動）で全面やり直す。
- 対象機: PW-SH6（i.MX28 / ARM926EJ-S / ARMv5TE）。
- ベースライン実測（統合フェーズ第2段、ゲスト）: CPU = 24 MHz
  （親は cpu_xtal）、pll0 = 480 MHz は生成されるが ref_cpu の選択先に
  なっていない、hbus / emi も 24 MHz。
- 作業日: 2026-11-21。repo への commit / push はしない（本レポートと
  u-boot-brain 作業ツリー内の変更のみ）。

## 1. 経路の実態（コードで確定）

### 1.1 nk.bin の中身と EBOOT のジャンプ先

- nkbin_maker/bsd-ce.c は NK.BIN 形式（マジック "B000FF\n"）で
  単一レコードを書く: ロード先 = 0xA0200000（物理 0x40200000、
  = pwsh6_defconfig の CONFIG_SYS_TEXT_BASE）、長さ = u-boot.bin の
  サイズ、チェックサム、続いて u-boot.bin の生バイト列、末尾に
  長さ 0 の終了レコード（開始アドレス欄がエントリポイント）。
- つまり EBOOT は **u-boot.bin（U-Boot 本体のみ）を 0x40200000 に
  置いてその先頭へジャンプ**する。SPL バイナリ
  （spl/u-boot-spl.bin）は u-boot.sb（ブートストリーム）専用で、
  nk.bin には含まれない（ルート Makefile の `nk.bin` ターゲットが
  bsd-ce に渡すのは u-boot-brain/u-boot.bin）。

### 1.2 SPL が担っていた初期化は丸ごと実行されない

SPL 内でのみ呼ばれる初期化（spl_boot.c:134-136）:

- `mxs_power_init()`（spl_power_init.c）: VDDD を 1550 mV
  （ブラウンアウト 1400 mV）に、VDDIO を 3300 mV に設定する等の
  電源初期化一式。
- `mxs_mem_init()`（spl_mem_init.c）: 順に
  1. `mxs_mem_init_clock()`: EMI — FRAC0[EMI]=21
     （ref_emi = 480×18/21 = 411 MHz）、HW_CLKCTRL_EMI に
     DIV_EMI=2 / DIV_XTAL=1（→ 205 MHz）、BYPASS_EMI 解除。
  2. VDDA 設定、DRAM コントローラ初期化。
  3. `mxs_mem_setup_cpu_and_hbus()`: FRAC0[CPU]=19
     （ref_cpu = 480×18/19 = 454 MHz、ゲート解除）、BYPASS_CPU を
     セット → HBUS DIV=3（454/3 = 151 MHz）→ DIV_CPU=1 →
     BYPASS_CPU 解除。

EBOOT 経路ではこの全てが不在。DRAM 初期化は EBOOT 自身が行う
（NK イメージを DRAM に載せるため）。

### 1.3 U-Boot 本体側にクロック/電源設定はほぼ無い

- arch_cpu_init（arch/arm/cpu/arm926ejs/mxs/mxs.c）: GPMI の
  バイパスと GPIO/DMA 初期化のみ。CPU クロックには触らない。
- board_early_init_f（board/sharp/pwsh6/pwsh6.c）: IOCLK0/1 =
  480 MHz、SSP0/1 = 96 MHz を設定し、上流 PR #27
  （commit 2f6a6700 "sharp: fix CPU clock div"）で追加された
  `mxs_set_divcpu(1)`（HW_CLKCTRL_CPU の DIV_CPU=1）を呼ぶ。
- しかし **FRAC0[CPU] の設定、BYPASS_CPU の解除、HBUS 分周、
  EMI、VDDD は本体側のどこにも無い**。DIV_CPU=1 でも、親が
  XTAL バイパスのままでは 24 MHz ÷ 1 = 24 MHz にしかならない。
  これがゲスト実測（cpu 24 MHz / 親 cpu_xtal、hbus = cpu の分周で
  24 MHz、emi は XTAL バイパスで 24 MHz）と正確に一致する。
- カーネル側（clk-imx28.c）はツリーを登録するだけでブート時に
  レートを設定せず、cpufreq も無い。したがって是正はカーネル
  起動前に完了している必要があり、置く層は U-Boot 本体が正解。

## 2. 是正設計

### 2.1 設定値の根拠（一次資料）

NXP 公式データシート IMX28CEC Rev. 4（10/2018）§3.1.2.1
「Recommended Operating Conditions for Specific Clock Targets」:

- Table 14（推奨動作状態）最速行: **VDDD = 1.550 V、
  ブラウンアウト = 1.450 V、CPUCLK = 454.73 MHz
  （CPUFRC=19、DIV_CPU=1）、AHBCLK = 151.57 MHz（HBUS_DIV=3）、
  EMICLK = 205.71 MHz（DIV_EMI=2、EMIFRAC=21）**、対応 DRAM は
  DDR2 / mDDR。
- Table 15（CPU）: 454.73 MHz の最大周波数には VDDD 1.550 V /
  ブラウンアウト 1.450 V が必要（FRAC は 18–35 の範囲）。
- Table 17（EMI）: EMICLK 205.71 MHz も **VDDD 1.550 V /
  ブラウンアウト 1.450 V** が条件（1.450 V 以下では最大
  196.36 MHz）。よって電圧確定は EMI 切替よりも前に置く
  （SPL の実行順＝電源初期化→メモリ初期化とも一致）。
- Table 8: VDDD 推奨範囲 1.35–1.55 V（周波数依存、最小値には
  75 mV のガードバンドを含む）。絶対最大定格は 1.575 V。
- SPL の値（FRAC 19 / HBUS 3 / EMI 21÷2 / VDDD 1550 mV）は
  この表と一致する。相違はブラウンアウトのみ（SPL は 1400 mV、
  データシート推奨は 1450 mV）→ 本設計はデータシート側を採る。

出典: https://www.mouser.com/datasheet/2/302/IMX28CEC-3138281.pdf
（NXP IMX28CEC Rev. 4、2026-11-21 取得）。

### 2.2 置く層と手順

置く層: **U-Boot 本体のボード初期化**（board/sharp/common に共通
ヘルパを置き、PW-SH6 の board_early_init_f から呼ぶ）。理由:

- カーネルはレートを設定しない（§1.3）ため、是正はカーネル起動前に
  完了している必要がある。EBOOT には手を入れられない（eMMC 側・
  SHARP 純正）。残る層は U-Boot 本体のみ。
- 既存の `mxs_set_divcpu(1)` も同じ場所（board_early_init_f）にあり、
  本是正はその自然な拡張になる。他の MX28 機種は同じヘルパを
  1行で採用できるが、本タスクでは検証可能な PW-SH6 のみに配線し、
  他機種は申し送りとする。

手順（SPL の実績シーケンスを移植。電圧→クロックの順）:

1. PLL0 の POWER をセット（冪等。SPL の mxs_power_clock2pll と同じ
   保険）＋ 100 µs 待ち。※ SSP/IO クロックが現に動いていることから
   PLL0 は既に給電されているはずだが、前提にしない。
2. **VDDD をデータシートの動作点へ**: SPL の mxs_power_set_vddx を
   VDDD レール分だけ移植し、目標 1550 mV / ブラウンアウト
   1450 mV（IMX28CEC Table 14/15）に寄せる。現在値が何であれ
   SPL が毎回行うのと同じ正規化であり、100 mV 刻みのランプと
   DC_OK 待ちを含む。**クロックを上げる前に電圧を確定させる**
   のが順序の要。
3. **EMI**（SPL の mxs_mem_init_clock を移植）: FRAC0[EMI] を
   ゲートして 21 に設定し直し、HW_CLKCTRL_EMI = DIV_EMI 2 /
   DIV_XTAL 1、BYPASS_EMI 解除 → 205.71 MHz。
   ※ DRAM を初期化したのが EBOOT である点が SPL と違う。
   EBOOT の DRAM レジスタ値が SPL の標準テーブル（205 MHz 級の
   DDR2 初期化）と一致するかをゲストで実測してから実施可否を
   最終判断する（§4 のベースライン計測）。一致しない場合は
   EMI を本タスクから外し、申し送りに回す。
4. **CPU + HBUS**（SPL の mxs_mem_setup_cpu_and_hbus を移植）:
   FRAC0[CPU]=19（ゲート解除を兼ねる）→ BYPASS_CPU をセット
   （XTAL で走らせたまま分周を変える）→ HBUS DIV=3 →
   10 ms → DIV_CPU=1 → BYPASS_CPU 解除 → 15 ms。
   遅延は DIGCTL の µs カウンタ（SPL の early_delay と同一原理、
   CPU クロックに依存しない）で取る。
- CPU/HBUS の切替は DRAM クロック（clk_emi）に触れないため、
  DRAM 上で実行中の本体コードからでも SPL と同じ安全性で
  実施できる（バイパス中は XTAL 駆動、分周器はグリッチフリー
  前提のカウンタ式）。EMI 切替だけが DRAM クロック自体を
  触るため、上記の実測ゲートを設ける。

## 3. 実装

変更箇所（u-boot-brain 作業ツリー内、commit/push はしない）:

- `board/sharp/common/cpu_clkdiv.c`（拡張）:
  - `sharp_delay_us()`: DIGCTL の µs カウンタによる遅延
    （SPL の early_delay と同一原理）。
  - `sharp_vddd_powered_by_linreg()`: SPL の
    mxs_get_vddd_power_source_off() の移植。
  - `sharp_set_vddd()`: SPL の mxs_power_set_vddx() を VDDD
    レール分だけ移植（25 mV 刻み、最低 800 mV、100 mV 刻み
    ランプ＋DC_OK 待ち、ブラウンアウトオフセットは最後に設定）。
    SPL との意図的な相違は1点のみ: DC_OK 待ちに 100 ms の
    上限を付けた（SPL は無限待ち。ブート経路がステータスビット
    1つでハングする作りは避ける。実機でも DC_OK は通常即時に
    立つため実害はない）。
  - `mxs_setup_vddd()`: PLL0 POWER セット＋ `sharp_set_vddd(1550,
    1450)`。
  - `mxs_setup_cpu_clock()`: SPL の mxs_mem_setup_cpu_and_hbus()
    の移植（FRAC0[CPU]=19 → BYPASS_CPU セット → HBUS DIV=3 →
    10 ms → DIV_CPU=1 → BYPASS_CPU 解除 → 15 ms）。
  - `mxs_setup_emi_clock()`: SPL の mxs_mem_init_clock() の移植
    （FRAC0[EMI]=21 → HW_CLKCTRL_EMI に DIV_EMI=2/DIV_XTAL=1 →
    BYPASS_EMI 解除）。
- `board/sharp/common/cpu_clkdiv.h`: 上記3関数の宣言を追加。
- `board/sharp/pwsh6/pwsh6.c`: board_early_init_f の冒頭で
  `mxs_setup_vddd()` → `mxs_setup_emi_clock()` →
  `mxs_setup_cpu_clock()` を呼ぶよう変更（従来の
  `mxs_set_divcpu(1)` は置換。他機種の board ファイルは不変更で、
  従来どおり mxs_set_divcpu(1) のまま＝本タスクの配線は PW-SH6
  のみ）。

ビルド（image-work/build_uboot.sh の流儀、Bootlin
armv5-eabi glibc 2024.05-1、-j2）:

- PW-SH6: `pwsh6_defconfig` → u-boot.bin 500,360 B（ベースライン
  499,735 B 比 +625 B）。nk.bin（edsh6exe.bin）500,399 B を生成。
  シンボル表に mxs_setup_cpu_clock / mxs_setup_emi_clock の
  実在を確認。
- クロスチェック: PW-SH1 も同一ツリーでビルド通過
  （u-boot.bin 484,684 B）。共通ファイル変更が他機種のビルドを
  壊していないことを確認。

## 4. ゲスト検証

### 4.1 検証対象と方法

- 検証イメージ: `image-work/clock1/sd-clockfix.img`。sd-v3.img を
  土台に、FAT 側の `edsh6exe.bin` のみを新しい nk.bin に差し替えた
  もので、それ以外は sd-v3 と同一。sd-v3.img 原本と sd-v4.img
  （統合フェーズ第4段使用中）には触っていない。
- 差し替えた nk.bin の同定: `clock1/edsh6exe.bin`（500,443 B、
  md5 `086a7cb08fe1bb66bdf8f46f5aedd86f`）。NK レコードの
  ペイロードが `clock1/uboot-build-sh6/u-boot.bin`（500,404 B、
  md5 `039beee201b264880acc9407fc557046`）とバイト一致すること、
  ELF のシンボル表に `mxs_setup_vddd` / `mxs_setup_emi_clock` /
  `mxs_setup_cpu_clock` が実在することを確認した。EBOOT の
  ダウンロード記録でも ImageLength = 0x7A2B4 = 500,404 B を確認。
  ※ §3 の数値（u-boot.bin 500,360 B / nk.bin 500,399 B）は初回
  ビルドのもので、検証に使ったのは再ビルド（build3）の
  500,404 / 500,443 B である。
- 経路: QEMU `pw-aj2`、`-accel tcg,tb-size=64` で正規経路
  （EBOOT → edsh6exe.bin → U-Boot → カーネル）を起動。シリアル
  ログは `clock1/serial-fix*.log`、駆動は `clock1/run_measure.py`
  と `clock1/run_desktop*.py`、撮影は `clock1/shots/`。
- 記録上の注意: 前ラインが用意した `sd-clock.img` は FAT 内の
  edsh6exe.bin が旧版（499,735 B）のままで、起動したまま残されて
  いたゲストもベースライン版だった。そのため前ライン時点の
  「ゲスト検証」は実際には始まっておらず、本節が初の実測である。
  当該ゲストは終了し、イメージは上記のとおり組み直した。

### 4.2 クロック実測（是正後）

fix2 の Linux 起動後に root で取得した clk_summary:

| クロック | 是正前（第2段実測） | 是正後 |
|---|---|---|
| cpu | 24,000,000 Hz（親 cpu_xtal） | **454,736,842 Hz**（親 cpu_pll） |
| ref_cpu | （選択されず） | 454,736,842 Hz = 480M×18/19 |
| hbus | 24,000,000 Hz | **151,578,948 Hz**（cpu ÷3） |
| emi | 24,000,000 Hz | **205,714,286 Hz**（親 emi_pll） |
| ref_emi | （選択されず） | 411,428,571 Hz = 480M×18/21 |
| pll0 | 480,000,000 Hz（生成のみ） | 480,000,000 Hz（親として選択） |

- U-Boot の起動表示でも `CPU: Freescale i.MX?? rev0.0 at 454 MHz`
  を確認（fix1 / fix2 / fix5）。
- U-Boot プロンプトでのレジスタ読み戻し（fix3 / fix4、2回とも
  同一値）:
  - HW_CLKCTRL_CPU（0x80040050）= `0x00010001` → DIV_CPU = 1。
  - HW_CLKCTRL_HBUS（0x80040060）= `0x00000003` → DIV = 3。
  - HW_CLKCTRL_EMI（0x800400F0）= `0x00000102` → DIV_EMI = 2、
    DIV_XTAL = 1。
  - HW_POWER_VDDDCTRL（0x80044040）= `0x0032041e` → TRG = 30
    （= 800 + 30×25 = **1550 mV**）、BO_OFFSET = 4
    （= 1550 − 4×25 = **1450 mV**）。設計どおりの動作点が
    プログラムされている。
- Linux 側からのレジスタ確認はできなかった: このカーネルでは
  /dev/mem の当該領域の読みが 0 バイトで返る（dd の records が
  0+0 になることを fix5 で切り分け。run_measure のレジスタ
  ダンプが全て空だった理由はこれ）。regulator_summary にも
  MXS の VDDD レールは現れない（カーネルはこの電源ブロックを
  regulator として公開していない）。レジスタレベルの証拠は
  上記の U-Boot 読み戻しで代替した。

### 4.3 ブートと安定性

- **fix2（通常ブート）**: ログイン到達、シリアル root ログイン可。
  新クロックのままストレスを通過（SD の p2 から 32 MiB 読み出し
  完走、/bin/bash の md5sum×3 が一致）。dmesg に panic / oops /
  BUG なし。QEMU の RSS は最終 505,264 KB。
- **fix5（通常ブート）**: シリアルログインまで壁時計 101 秒
  （他ゲストと競合する環境下の参考値）。GUI ログインで
  デスクトップ到達（braindesk2 稼働、/run/user/1000 と
  bdnotify.sock の生成を確認）。アプリのスモークとして bd-calc
  を第3段の方法で起動し、生存と描画を確認
  （`shots/fix5-app-bd-calc-b.jpg`）。デスクトップアイドル時の
  メモリは used 34 MiB で、第3段（是正前）の 34 MiB と同等
  （退行なし）。braindesk2 の RSS は 18.3 MB。
- **fix4（U-Boot プロンプトで止めてから再開したブート）**:
  グリーター画面まで到達した（`shots/fix4-boot.jpg`）が、
  シリアル getty のバナーが出ずシリアルログインができなかった。
  同一イメージの通常ブート（fix2 / fix5）では出ているため、
  プロンプト割り込みと CR 送信を伴う駆動側経路に起因する
  観測上の異常として記録する（製品側の欠陥とは判定しない）。
- ブート所要の壁時計はホスト競合（常時 2〜3 ゲストが共有
  1.5 CPU を取り合う）の影響が大きく、是正前後の厳密な比率は
  本検証では主張しない。BogoMIPS も是正前 898.66 に対し
  是正後 858.52〜915.86 と TCG では再現性がなく、クロックの
  証拠には使えない（第2段の留保どおり）。レートの証拠は
  §4.2 の clk_summary とレジスタ読み戻しである。

### 4.4 ルートデバイス番号レース（新規発見）

- fix1 と fix3 でカーネルパニック
  （`VFS: Unable to mount root fs on unknown-block(179,10)` /
  `(179,2)`）。原因はクロック設定の破損ではなく、2つの
  mxs-mmc ホストの登録順が入れ替わり、SD が mmcblk0 になった
  回では `root=/dev/mmcblk1p2` が eMMC 側のパーティションを
  指したため。両カードのパーティション列挙自体は正常で、
  メモリ破損の兆候はない。
- 発生率は是正後のブート 5 回中 2 回。24 MHz のベースライン
  では第2・3段の多数回のブートで未発生だったレースが、
  高速化によって顕在化した。
- cmdline の出所は U-Boot の `include/configs/brain_mx28_common.h`
  の `sdroot=/dev/mmcblk1p2 rw rootwait`。恒久対策
  （`root=PARTUUID=` 化）はイメージ/U-Boot 側の変更になるため
  本タスクの範囲外とし、§5 に申し送る。

### 4.5 EMI ゲートの扱い

- §2.2 手順3 のゲート（EBOOT の DRAM レジスタ値と SPL
  テーブルの照合）は、ゲストでは実施できなかった。QEMU
  モデルの DRAM コントローラレジスタ（0x800E0000）は
  U-Boot プロンプトの `md.l` でも読み戻しがほぼ全て 0 で、
  EBOOT が書いた値を観測できないためである。
- 照合相手の特定にも訂正がある: PW-SH6 の SPL 実効テーブルは
  汎用の `mxs_dram_vals` ではなく、`board/sharp/pwsh6/iomux.c`
  の `lpddr_dram_vals`（`mxs_adjust_memory_params` が全 190
  エントリを置換する。汎用表とは 49 エントリ相違し、タイミング
  レジスタを含む）。前ラインが `clock1/spl_dram_vals.txt` に
  抽出していたのは汎用表の方だったため、`lpddr_dram_vals.txt`
  を別途抽出して clock1/ に置いた。
- EMI 切替の安全性についてゲストで取れた証拠: EMI =
  205,714,286 Hz の状態で、カーネルブート、両 MMC の列挙と
  読み出し、32 MiB 読み出し＋md5 ストレス、デスクトップと
  アプリの動作が全て正常だった。DRAM タイミング実値の照合は
  実機での確認事項として §5 に残す。

### 4.6 eMMC 無傷

- 全ラン（fix1〜fix5）の前後で `emmc-clock.img` の md5 は
  `afac07f6a545e38cca27ea5d677e476e` で不変だった。本是正は
  SD 側の nk.bin のみの変更で、EBOOT（eMMC）には手を入れて
  いないことと整合する。

## 6. 追記（2026-11-21）: ルートデバイス番号レースの恒久対策（PARTUUID 化）

§5 の申し送り 3（§4.4 のレース対策）を実施した記録。作業は
`image-work/clock1/` で、土台は `sd-clockfix.img`（クロック是正
入り）。`sd-v3.img` / `sd-v4.img` 原本は無変更。repo への
commit/push はしていない。

### 6.1 方式: PARTUUID はハードコードせず U-Boot が実行時に解決する

- root 指定の出所は `u-boot-brain/include/configs/brain_mx28_common.h`
  の `sdroot=/dev/mmcblk1p2` / `emmcroot=/dev/mmcblk0p3`
  （`CONFIG_ENV_IS_NOWHERE` のため環境はコンパイル時のみ。
  FAT に uEnv.txt 等はない）。
- MBR ディスク署名はイメージ生成ごとに変わることを実測した
  （v2=`a398c999`、v3=`c8503c8b`、v4=`dc4ef97b`）。したがって
  PARTUUID の固定値は使えない。
- 採用方式: `selectsd` / `selectemmc` が対象パーティション番号
  （`sdrootpart=2` / `emmcrootpart=3`）だけを持ち、新規スクリプト
  `setroot` が `part uuid mmc ${dev}:${rootpart} rootuuid` で
  起動のたびに PARTUUID を解決して `root=PARTUUID=<uuid> rw
  rootwait` を組み立てる。解決に失敗した場合は警告を表示して
  従来の `/dev/mmcblkNpM` 表記にフォールバックする（安全側）。
  MBR の PARTUUID 形式（`%08x-%02x`）は U-Boot の part_dos.c と
  カーネルで一致することをソースで確認した。
- `part` コマンドは全機種 defconfig で無効だったため、共通
  ヘッダを使う 9 機種（pwa7200、pwa7400、pwsh1〜7）の defconfig
  に `CONFIG_CMD_PART=y` を追加した（`CONFIG_PARTITION_UUIDS=y`
  は自動で有効になる）。

### 6.2 変更ファイル（repo 側・未コミット）

- `u-boot-brain/include/configs/brain_mx28_common.h`:
  `setroot` スクリプト追加、`selectsd` / `selectemmc` の書き換え、
  理由コメント。`sdroot` / `emmcroot` はフォールバック値として残置。
- `u-boot-brain/configs/` の defconfig 9 件: `CONFIG_CMD_PART=y`。
- `os-brainux/override/lib/systemd/system/boot.mount`:
  `What=/dev/mmcblk1p1` → `/dev/disk/by-label/boot`（§6.4）。

ビルドは PW-SH6 で実施し、PW-SH1 でもクロスビルド通過を確認。
U-Boot プロンプトでの実地確認: preboot の `selectsd` で
`root=PARTUUID=c8503c8b-02`（テストイメージの署名）に解決され、
`run selectemmc` → `PARTUUID=00000000-03`、`run selectsd` で
復帰、フォールバック警告なし。

### 6.3 検証（正規経路ブート）

テストイメージ `sd-partuuid.img` で 10 回ブートし、全回とも
root マウント成功＋シリアルログイン到達を確認した
（boot1〜3、5〜11 が確定合格。各回 `root=PARTUUID=c8503c8b-02`、
ルート FS の blkid PARTUUID 一致、clk_summary の ref_cpu =
454,736,842 Hz でクロック是正も維持、フォールバック警告なし、
panic なし）。

- boot4 は root マウント成功（`Mounted root ... 179:10`）の後、
  後段ユーザースペース（cfg80211 ロード直後）で 1,500 秒
  タイムアウトした。panic ではなく、他ラインのゲストと CPU を
  奪い合う環境での停滞と判断し、判定不能として代替ブート
  （boot11）で 10 回確定に揃えた。boot.mount 確認専用の
  boot16 でも同型のタイムアウトが 1 回出た（boot17/18 は
  93〜94 秒で正常完了）。
- 本シリーズでは番号フリップ自体は発生しなかった（root は
  全回 mmcblk1p2）。PARTUUID は番号に依存しないため、フリップ
  時も設計上安全である。
- eMMC イメージ `emmc-clock.img` の md5 は全検証の前後で
  `afac07f6a545e38cca27ea5d677e476e` のまま不変だった。

### 6.4 番号依存の棚卸しと boot.mount の by-label 化

- rootfs / os-brainux を棚卸しし、番号依存は `boot.mount` の
  `What=/dev/mmcblk1p1` のみだった（診断ツール brain-part-report
  は列挙のみで変更不要、swap/zram の番号依存なし）。
- FAT ラベル `boot` は全 `assemble_image*.sh` が `mkfs.fat -n
  boot` で固定しており、FAT UUID や MBR 署名と違ってイメージ
  生成をまたいで安定する。よって by-label が正解。
- **落とし穴（重要）**: 土台イメージの rootfs（ext4）は
  `needs_recovery`（未再生ジャーナル、sequence 0xda）を保持して
  いた。debugfs で inode を直接編集しても、ゲストの初回マウント
  時のジャーナル再生で inode（サイズ等）が旧状態に巻き戻る
  （データブロックの編集だけが残り、boot.mount が 130 バイトに
  切り詰められて systemd 257 に `What= setting is missing` と
  判定された。ホスト側の debugfs 読みでは正常に見えるため
  気づきにくい）。対策は「ホストで `e2fsck -y` によりジャーナル
  を再生してから編集する」。この手順で作り直した p2 で、
  ゲスト側 md5 一致・`boot.mount` active・`/boot` に FAT の
  内容（zImage、edsh6exe.bin 等）が見えることを確認した
  （boot18）。**今後この系統のイメージをオフライン編集する
  際は、必ず先にジャーナル再生すること。**
- なお boot.mount は従来（番号指定の頃）もこのジャーナル問題
  とは無関係に動作実績の確認がないユニットだった。今回の
  by-label 化＋ジャーナル対策で初めて実働を確認した。

### 6.5 注意: eMMC 向け PARTUUID は縮退値

eMMC の MBR 署名は 0 のため、eMMC から Linux を起動する場合の
root は `PARTUUID=00000000-03` になる。2 ディスク構成では
一意だが縮退値である。今回確認したのは U-Boot レベルでの
解決までで、Linux-from-eMMC の起動自体は未検証。

### 6.6 成果物（最終イメージ統合用）

すべて `~/workspace/buildbrain-work/image-work/clock1/` 配下。

- テストイメージ: `sd-partuuid.img`
  （md5 `11a56e67dc637db0723984b690737605`。= sd-clockfix.img
  ＋ PARTUUID 版 edsh6exe.bin ＋ ジャーナル再生済み p2）
- NK 形式 U-Boot: `edsh6exe-partuuid.bin`
  （md5 `32c2ec99796ed58680ff7c85b1edd1f0`）
- U-Boot 本体: `uboot-build-pu6/u-boot.bin`
  （md5 `e808633611db68a1780b1f614d561fd6`、PW-SH6 用。
  PW-SH1 用は `uboot-build-pu1/u-boot.bin`）
- 修正済み rootfs パーティション単体: `p2-fix.img`
  （md5 `e945f6a245acea4745719798c0273a3d`。ジャーナル再生＋
  boot.mount by-label 化済み。旧 `p2-pu.img` はジャーナル未再生
  のため廃棄した）
- ハーネスと記録: `launch_pu.py`、`run_pu.py`、
  `run_pu_extra.py`、`run_pu_diag13.py`〜`run_pu_diag15.py`、
  `run_pu_boot16.py`〜`run_pu_boot18.py`、`build_pu.sh`、
  結果 `pu-results.txt`、シリアルログ `serial-pu*.log`、
  スクリーンショット `shots/`

実機への申し送り: §5 の 3 は QEMU 上では本追記で完了。実機の
初回適用時はシリアルで PARTUUID 解決とフォールバック警告の
有無を観察すること。
