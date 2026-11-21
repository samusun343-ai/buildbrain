# OS 魔改造 第1弾 — Brainux 本体へのメス入れ（実測棚卸しと最初の実施）

日付: 2026-11-21 開始
範囲: Brainux OS そのもの（カーネル・ベースシステム・現行イメージの中身）。
作り直しスタック（rebuild/）と統合フェーズ準備（image-work/）には書き込まない。
本レポートは段階ごとに途中経過を書き残す。

## 進捗ログ

- 2026-11-21 着手。素材の所在を確認:
  - カーネル: buildbrain/linux-brain（pin 11c11c922）、`brain_defconfig`、
    ビルド済み成果物は `~/workspace/buildbrain-work/dtb-out/`
    （.config あり、zImage 5,559,104 B、モジュール 92 本。
    統合フェーズ準備ラインの成果物、読み取りのみ参照）。
  - rootfs 素材: `image-work/rootfs/`（dpkg status 642 パッケージ。
    統合ラインが構築中の現行構成イメージ、読み取りのみ参照）。
  - クロス toolchain: `~/workspace/buildbrain-work/toolchain/
    armv5-eabi--glibc--stable-2024.05-1/`。
- 2026-11-21 第1段階完了: §1 カーネル棚卸し（組み込み
  10,767,704 B を built-in.a 実測で内訳化、§1.4 で brainlcd =
  DRM tiny ドライバと確定）、§2 クロック半減は「起動経路の
  欠陥」と結論（SPL を通らない現行経路では誰も CLKCTRL を
  設定しない。外部実測 240MHz の裏付けあり）、§3 ベース
  システム実測（659 パッケージ / 1,346 MiB、ssh 有効を実物で
  確認、アイドルメモリは未実測）、§4 SD 候補洗い出し。
- 2026-11-21 第2段階: §5 統廃合の実行案を実 dpkg データの
  依存閉包計算で裏付け（開発ツール 71 件 / 327.0 MiB、
  ssh 7 件 / 4.0 MiB、DHCP 2.7 MiB、全て依存違反 0）。
  §7 zram A/B 計画を作成。§6 カーネル削減はフラグメントを
  olddefconfig 適用済み（=y 935→880、NFS 一式が =m 化）、
  zImage ビルドで実施前後を比較する。
- 2026-11-21 §6 後継ライン着手: 先行ラインは最終報告なしに
  終了していたが、zImage ビルド（make O=kbuild -j4、
  nohup 起動）は生存しておりコンパイル継続中だった。
  kbuild/.config が config-trimmed.txt とバイト単位で一致
  することを照合済み（cmp 一致）。config 件数は baseline
  =y 935 / =m 97 → trimmed =y 880 / =m 106（=m の増加は
  NFS 一式のモジュール化による）。ビルド完了を待って
  §6 に結果を追記する。
- 2026-11-21 ユーザー判断（§5）: §5.1 開発ツール分離と
  §5.2 ssh 除去は**不採用**（開発ツールも ssh も必要。
  Wi-Fi が無いため接続は USB の RNDIS 経由で、ssh も
  それ経由で使う）。§5.3 DHCP 一本化と §5.4 サービス・
  タイマー手術は有効なまま、第2弾で実施。各節に明記済み
  （実測と経緯の記録は削除していない）。
- 2026-11-21 §6 完遂（後継ライン）: 先行ラインの zImage
  ビルドは生存・完走を確認（kbuild/.config =
  config-trimmed.txt と cmp 一致も照合）。モジュールは
  後継ラインが `make -j2 modules` で追加ビルドし exit 0。
  実測: zImage 5,559,104 → 4,842,184 B（−12.9%）、
  vmlinux text+data −1,444,118 B、モジュール 92 → 101 本
  （NFS モジュール化分）。§6 に記録済み。削減カーネルでの
  起動検証は未実施（統合フェーズ）。

## 0. 測定の前提と一次資料

- 対象機 PW-SH6: i.MX287（ARM926EJ-S）、RAM 128MB、800×480 LCD。
  ハードウェアの実在は DTS（imx28-pwsh6.dts + imx28-brain.dtsi）を正とする。
- カーネル config の実測対象は「実際にビルドされた config」
  （dtb-out/.config）であり、defconfig そのものではない。
- 数値は全て本レポート内で測定方法を明記する。ホスト側の測定と
  実機/ゲストでの測定を混同しない。

## 1. カーネル棚卸し（実測）

### 1.1 ビルド済み config の規模

- dtb-out/.config: 有効 935 項目（=y 935 / =m 97）、無効 2,757 項目。
- 組み込みコード量（dtb-out の全 built-in.a メンバを重複排除し、
  `size` の text+data を合算した実測）: **10,767,704 B**
  （Image 11,467,688 B と整合）。モジュール 92 本は別。
- 上位の内訳（text+data 実測）:

| 領域 | サイズ | 備考 |
|---|---|---|
| mm | 566,136 | コア。対象外 |
| net/ipv4 | 540,056 | コア |
| net/core | 461,502 | コア |
| fs（コア） | 468,692 | |
| fs/ext4 | 449,251 | rootfs。必須 |
| drivers/usb | 446,144 | コア＋ホスト＋ガジェット（ガジェットは brain-mode の機能） |
| kernel/printk | 439,532 | うちログバッファ .data が大きい（§1.3） |
| net/ipv6 | 372,910 | §1.3 の候補（第2段） |
| drivers/mtd | 344,659 | **§1.2 の主候補** |
| lib/zstd | 344,413 | UBIFS 等の利用者と連動（§1.3） |
| fs/nfs | 326,025 | sunrpc 219,409 + lockd 56,195 と合計 **約 600KB** |
| fs/ubifs | 255,463 | **主候補（MTD 依存）** |
| drivers/gpu（DRM） | 457,247 | §1.2 で内訳を判定 |
| fs/jffs2 | 97,200 | **主候補（MTD 依存）** |
| io_uring | 114,023 | 第2段候補 |
| sound/soc | 151,731 | うち SGTL5000 系は不在ハード（§1.2） |

### 1.2 PW-SH6 に存在しないハードウェアのドライバ（DTS を正とした判定）

DTS（imx28-brain.dtsi + imx28-pwsh6.dts）から確定する実在ハード:
SSP0（eMMC 8bit）/ SSP1（microSD 4bit）/ SAIF＋BU26154（i2c0）/
LRADC タッチ / RTC / I2C×2 / PWM / DUART / USB0 OTG / brainlcd /
GPIO マトリクスキーボード。逆に以下のピンは GPIO として
hog されており、対応ペリフェラルは基板に存在しない:
**ENET（Ethernet）、GPMI（NAND）、CAN**。

| 項目 | config 現行 | 判定根拠 |
|---|---|---|
| MTD 一式（MTD core / CFI / DATAFLASH / SST25L / RAW NAND / **GPMI NAND** / SPI NOR / UBI） | 全て =y | NAND・NOR とも基板に無い（GPMI ピンは GPIO hog）。起動・rootfs は MMC 経由のみ。**全て不要** |
| UBIFS / JFFS2 | =y | MTD 前提の FS。デバイスに MTD が無いためマウント不能。**不要** |
| SND_SOC_MXS_SGTL5000 + SND_SOC_SGTL5000 | =y | SGTL5000 は i.MX28 EVK のコーデック。Brain の実在は BU26154（別途 =y で有効）。DT ノード無し。**不要** |
| FEC（i.MX28 Ethernet） | =y | ENET ピンは GPIO hog、DT で fec ノード無効、PHY なし。**不要**（プローブしない） |
| DRM_UDL（USB DisplayLink） / DRM_PANEL_SEIKO_43WVF1G | =y | 対応 DT ノード無し。表示は brainlcd が担う（ドライバ所在を確認中 §1.4）。**不要見込み** |
| tsc2007（I2C タッチ） | =m | Brain のタッチは LRADC。モジュールのため RAM 影響なし。イメージ容量のみ |

### 1.3 メモリを食う組み込み項目・その他

- **NFS クライアント一式 =y（約 600KB）**: rootfs は SD の ext4 で、
  fstab は未設定（base のプレースホルダ）、起動時 NFS マウントは無い。
  機能自体は残し、**=m 化**（必要時にロード、常駐 RAM から外す）が
  互換を壊さない削減。
- **printk ログバッファ**: LOG_BUF_SHIFT=17 → 128KB の .data。
  17→16 で 64KB の RAM 節約。ただし初期ブートログの欠落は
  コンソールが無い本機では痛い。第2段の任意項目とする。
- **IPv6 =y（372,910 B）**: =m 化は systemd / dhcpcd の前提と
  衝突しうる。第2段候補（要検証）として記録のみ。
- **io_uring =y（114,023 B）**: 現行スタックに利用者を確認できず。
  第2段候補（無効化）。systemd は io_uring を必須としない。
- デバッグ系: DEBUG_INFO は無効、FTRACE 系は無効（良好）。
  DEBUG_FS=y は残す（clk_summary によるクロック検証に必須、§2）。
  SLUB_DEBUG=y は起動パラメータで無効化可能な範囲のコスト。
- CMA は無効（LCD の DMA 確保は通常プールから。128MB では正解）。
- HIGHMEM 無効、VMSPLIT_3G、SLUB — いずれも 128MB 構成として妥当。
- カーネル圧縮は gzip。zImage 5,559,104 B。

### 1.4 brainlcd の種別（確定）

- 実在ドライバは **DRM tiny ドライバ**:
  `drivers/gpu/drm/tiny/brain.c`（+ パネル
  `brain-lq050j1ug01.c`）。DTS ノードは
  `lcdif@80030000`、compatible = "sharp,brainlcd"
  （imx28-brain.dtsi:251）。
- したがって **DRM コアは現行の表示経路そのものであり削減対象
  ではない**。§1.2 で切ったのは DRM_UDL と Seiko パネルという
  未使用の周辺ドライバのみで、この判定と矛盾しない。
- fbdev 側に二重の表示経路は無い。

## 2. CPU クロック半減の検証

### 2.1 結論（先に）

**実在する。ただし「カーネルの欠陥」ではなく「起動経路の欠陥」。**
現行の起動経路（WinCE → brainlilo → U-Boot 本体 → カーネル）では、
**CPU クロックを設定する者が誰もいない**。Linux は WinCE が
CLKCTRL に残した値のまま動く。カーネルには cpufreq が無く
（CONFIG_CPU_FREQ 無効）、clk ドライバはツリーを登録して現在値を
読むだけで、起動時にレートを設定しない。

### 2.2 コードでの裏付け（一次資料）

1. **SPL には正しい設定があるが、現行経路では実行されない**:
   u-boot-brain `arch/arm/cpu/arm926ejs/mxs/spl_mem_init.c` の
   `mxs_mem_setup_cpu_and_hbus()` が CPUFRAC=19
   （480×18/19 = 454 MHz）、DIV_CPU=1、HBUS=151MHz を設定する。
   呼び出しは `mxs_mem_init()` ← `spl_boot.c:136`、つまり
   **SPL 内のみ**。
2. **現行イメージの起動経路は SPL を通らない**:
   image-work/assemble_image.sh が示すとおり、p1 の
   `loader/gen3_6.bin` は `u-boot.bin`（U-Boot 本体）で、
   WinCE 上の brainlilo が RAM に載せて直接ジャンプする
   （SD のブートストリーム経由ではない）。SPL のメモリ/クロック
   初期化は丸ごとスキップされ、DRAM もクロックも WinCE の
   設定を引き継ぐ前提の経路。
3. **brainlilo はクロックを触らない**: brainlilo/ のソースに
   CLKCTRL（0x80040000）へのアクセスは無い（grep 実測）。
4. **カーネルも設定しない**: CONFIG_CPU_FREQ は無効。
   drivers/clk/mxs/clk-imx28.c は cpu クロックを mux+divider として
   登録するのみで、ブート時のレート設定は無い。
5. **実測例（外部）**: Brainux（第2世代機）の利用者実測で
   `/sys/kernel/debug/clk/clk_summary` の CPU clock が **240 MHz**、
   BogoMIPS 119.70（≒240MHz 相当）。
   出典: https://blog.unasuke.com/2021/building-ruby-on-brainux/
   （2026-11-21 取得）。240 MHz は 454 MHz の約半分
   （480/2 ないし 454/2=227 の近傍）であり、「半減」の
   観測と整合する。PW-SH6 個体での値は未実測（§2.4 の手順）。

### 2.3 原因箇所の特定

- 直接原因: 起動経路で SPL が担っていた CLKCTRL 初期化
  （CPU frac/div、HBUS、EMI）が、brainlilo 経路では不在。
- 構造原因: カーネル側に「引き継いだクロックを検証・是正する」
  仕組みが無い（cpufreq 無し、clk の初期レート指定無し）。
  そのため最終周波数が「WinCE が最後に残した状態」という
  不確定なものになっている。

### 2.4 是正案（実施は次弾以降、本弾は案と検証手順まで）

| 案 | 内容 | 長所 | 短所・前提 |
|---|---|---|---|
| A. brainlilo で設定 | ジャンプ前に CLKCTRL を SPL と同じ値に設定（MXS 機種のみ） | 最も早い段階で確定。全経路に効く | CE 側ドライバ経由の物理メモリアクセスが必要。VDDD 電圧（454MHz 動作の前提）も SPL の power 初期化とセットで要検討 |
| B. U-Boot 本体で設定 | U-Boot proper の MXS 初期化に CPU/HBUS 設定を追加 | Linux 側だけで完結 | brainlilo が載せるバイナリの初期化順序の精査が必要。電圧の件は同上 |
| C. カーネル早期に設定 | clk-imx28 初期化時、または mach 初期化で frac/div を是正 | イメージ側で完結、経路非依存 | 電圧前提の確認が最も難しい。既に動いているクロックツリーの途中変更は順序に注意 |

- いずれの案でも、**454MHz 動作に必要な VDDD 電圧を誰が保証するか**
  が前提条件。SPL は power 初期化（spl_power_init.c）で電圧側も
  面倒を見ている。WinCE は自らの動作のために電圧を設定しているが、
  それが 454MHz の要求を満たすかは未確認 — 実機での検証項目。
- 検証手順（実機/ゲスト、統合フェーズ）:
  1. 起動直後に `/sys/kernel/debug/clk/clk_summary` で cpu/hbus の
     実レートを記録（現行イメージでのベースライン）。
  2. /proc/cpuinfo の BogoMIPS を併記（240MHz 級なら ≈120、
     454MHz 級なら ≈227 が目安）。
  3. devmem 等で HW_CLKCTRL_FRAC0[CPU] / HW_CLKCTRL_CPU /
     HW_CLKCTRL_CLKSEQ の生値を読み、frac/div/bypass を同定する。

## 3. ベースシステム実測（image-work/rootfs、ホスト側測定）

対象 rootfs は統合ラインが現行構成で構築中のもの（dpkg 実測）。
数値は全て dpkg status の Installed-Size とファイル実在に基づく
ホスト側測定であり、**実機メモリ使用量ではない**。

### 3.1 規模

- インストール済み **659 パッケージ / Installed-Size 合計
  1,346 MiB**（ストック実測 663 件 / 1,388 MiB とほぼ一致。
  差は構築途中の分）。
- 上位: libllvm19 114,281 KiB、fonts-noto-cjk 91,217、
  gcc-14-arm-linux-gnueabi 57,094、git 46,457、vim-runtime 38,426、
  libicu76 36,455、libpresage-data 35,693、libgtk-3-common 30,916、
  g++ 29,561、anthy-common 27,981、mesa-libgallium 27,655、
  cpp 27,380、libperl5.40 24,736、libz3-4 24,451。

### 3.2 起動経路と有効サービス（rootfs の symlink 実測）

- init は systemd（default.target → graphical.target）。
- multi-user.target.wants: boot.mount、brain-mode.service、
  braingreeter2.service、dhcpcd.service、networking.service、
  remote-fs.target、**ssh.service**、wpa_supplicant.service。
- sysinit: fake-hwclock-load、systemd-pstore、systemd-timesyncd。
- timers.target.wants: **apt-daily / apt-daily-upgrade /
  dpkg-db-backup / e2scrub_all / fstrim / logrotate / man-db**
  （＋fake-hwclock-save）。オフライン既定・SD 書込み削減の観点で、
  apt-daily 系・e2scrub・fstrim・man-db・dpkg-db-backup は
  本機では実益が無く、周期的な書込みと wakeup の発生源。
  §5 の実施案で無効化対象とする。
- **sshd が有効**: openssh-server がインストール済み
  （3,357 KiB）で ssh.service が multi-user の wants にある
  （パッケージの maintainer script による既定有効化）。
  os-internals-2 の「ssh メタを外せば server ごと消える」判定は
  この実測でも有効で、削減と同時に不要な待ち受けも消える。
- journald.conf は [Journal] が空 = **全て既定値**。
  ジャーナルの上限・SD への永続化方針が未調整（§5 の対象）。
- fstab は base のプレースホルダのまま（root はカーネル
  コマンドライン由来）。

### 3.3 訂正（先行測定との差異）

- 端末は os-internals-2 の閉包計算では3種（lxterminal / fbterm /
  rxvt-unicode）とされたが、**実 rootfs では lxterminal と
  fbterm の2種のみ**（rxvt-unicode・gnome-terminal・xterm は
  不在。jwm の x-terminal-emulator 代替依存は lxterminal の
  Provides で満たされている）。閉包推定より実物が正。
- DHCP は isc-dhcp-client（2,751 KiB）と dhcpcd-base（561 KiB）が
  **両方インストール済み**で、有効なのは dhcpcd.service。
  os-internals-2 の判定（利用側スクリプトの dhcpcd 化が前提）
  と併せて §5 の対象。

### 3.4 アイドル時メモリ — 未実測

- 本 VM には起動可能な対象イメージが無く（統合ラインが構築中）、
  アイドル時 PSS は**未実測**。ホストの数値で代用しない。
- 測定手順（統合フェーズ、実機または発行済み SD イメージ）:
  1. 起動 → ログイン → アイドル 60 秒で
     `brain-measure stock`（os-brainux/override 同梱）を実行し、
     セッション PSS 合計と MemAvailable を記録。
  2. `systemd-cgtop` / `ps aux --sort=-rss` で常駐サービス分の
     内訳を取得し、§3.2 のサービス一覧と突き合わせる。
  3. カーネル分は /proc/meminfo の MemTotal と
     「dmesg の Memory: ... available」行から算出する
     （カーネル常駐分の実測は §4 の config 削減前後で比較可能）。

## 4. SD 性能 — ボトルネック候補（実測は統合検証フェーズ）

対象は SSP1（microSD、4bit）。ドライバとクロックのコードから
洗い出した候補（いずれも実機での裏取りが前提、断定しない）:

1. **SSP 入力クロックが起動経路の遺産**（CPU クロックと同根）:
   mxs-mmc の set_ios は `mxs_ssp_set_clk_rate()`
   （drivers/clk/mxs/clk-ssp.c）でバス時計を作るが、分母は
   `clk_get_rate(ssp->clk)` = ref_io0 → mux → HW_CLKCTRL_SSP1
   分周、という**現在のハードウェア状態**である。
   clk-imx28.c は SSP クロックのレートを一切設定しない
   （clk_set_rate の呼び出し無し、実測）。つまり SD の実効
   バスクロック上限は WinCE/U-Boot が残した分周状態で決まる。
   カードが 50MHz ハイスピードを申告しても、入力クロックが
   低ければ TIMING 分周では届かない。
   - 裏取り方法（統合フェーズ）: clk_summary の ssp1 レートと
     /sys/kernel/debug/mmc1/ios の clock 実値を読み、
     dd のシーケンシャル読取り実測と突き合わせる。
2. **UHS は使えない構成**: DTS の mmc ノードに vqmmc / UHS 系
   プロパティが無く、1.8V 切替が無いため上限は 50MHz×4bit
   = 25MB/s（理論値）。これはハード構成上の天井であり欠陥では
   ないが、実測がこれを大きく下回るなら候補1が主因となる。
3. **DMA は有効**: ドライバは dma_request_chan("rx-tx") で
   mxs-dma を使用（PIO フォールバック時は大幅に遅くなる）。
   統合フェーズで dmesg の DMA 確保成否を確認する。
4. **カード検出ポーリング**: mxs-mmc は検出タイムアウト
   HZ/2 のポーリング経路を持つ（CD ピンの構成次第）。
   スループットではなくアイドル時 wakeup の論点（PERSPECTIVES
   #24/#29）。実機で /proc/interrupts の増分と合わせて確認。
5. ファイルシステム側: ext4 のマウントオプション（journal の
   commit 間隔等）はイメージ側の調整代として §5 に分離する。
   本弾では未変更。

参考（web）: U-Boot 側 mxsmmc の DMA は「キャッシュ有効時で
なければ性能利得なし・実験的」と注記されている
（https://gitlab.kontron.com/imx/u-boot-imx/-/blob/869833285b48fd51e0319ff2cede2744331a1e10/drivers/mmc/mxsmmc.c
、2026-11-21 取得）。Linux 側ドライバは dmaengine 経由で
常時 DMA を使う設計であり、同列には扱わない。

## 5. 統廃合の実行案（イメージ変更仕様）

削減量は全て実 rootfs の dpkg status + apt extended_states から
依存閉包を計算した実測（os-surgery/purge_closure.py、
`apt purge` + `autoremove` の再現 + 残存パッケージの依存充足を
全件検証）。種リストに対する違反 0 件を確認したものだけを載せる。
これはイメージ容量（SD）の削減であり、RAM 削減ではない
（RAM に効くのは §6 のカーネルと、常駐サービスの削減）。

### 5.1 開発ツール分離 — 71 パッケージ / 327.0 MiB

> **不採用（ユーザー判断 2026-11-21）**: 開発ツールは本機で
> 必要であり、分離しない。以下の実測と実施案は記録として
> 残すが、実施しない。

- 種: gcc/g++/cpp 一式（ネイティブ=arm-linux-gnueabi 名義分含む）、
  build-essential、dpkg-dev、libtool、binutils、
  libstdc++-14-dev、python3-dev / libpython3.13-dev /
  python3-typeshed / python3-numpy / python3-ipython、git。
- 閉包で連鎖削除されるものを含め 71 件・**327.0 MiB**、依存違反 0。
- 検証で判明した注意点:
  - **libz3-4 は開発系ではない**（libllvm19 の依存）。種に入れると
    mesa クラスタを壊すため除外した（充足チェックで検出）。
  - libpython3.13（6.2MB）は埋め込み用共有ライブラリで、
    インタプリタ本体は libpython3.13-minimal 側を使う。
    削除しても python3 は壊れない（dpkg の Depends で確認）。
  - bzip2 / xz-utils / file（合計 約1.7MB）は汎用ツールのため、
    実行時は `apt-mark manual` で残すことを推奨（差引後の
    実質削減は 約325.3 MiB）。
- 実施方法（setup_brainux.sh への変更仕様）: イメージ構築の
  最終段（ly / braindesk の内部ビルド完了後）に purge を実行する
  「ランタイム分離」ステップを追加する。構築時に必要なことは
  os-internals-2 §4 #9 で確認済みで、分離はその前提と矛盾しない。
  開発が要る利用者向けには、同じ種リストを戻す手順
  （apt install の1コマンド）を対で用意する。

### 5.2 ssh メタ除去 — 7 パッケージ / 4.0 MiB + 待ち受け解消

> **不採用（ユーザー判断 2026-11-21）**: ssh は必要であり、
> 除去しない。理由: 本機に Wi-Fi は無く、接続は USB の
> RNDIS 経由であり、ssh もその経由で使う。§3.2 で確認した
> sshd の既定有効化も、この利用形態では必要な待ち受けで
> ある。以下の実測は記録として残すが、実施しない。

- 種: ssh、openssh-server。連鎖で openssh-sftp-server、
  libwrap0、libwtmpdb0、runit-helper、ucf まで **7 件・4.0 MiB**、
  依存違反 0（openssh-client は残る）。
- §3.2 のとおり server は既定で有効化までされている。
  削減量以上に「不要な sshd の待ち受けが消える」ことが本機
  （オフライン既定・辞書端末）では大きい。

### 5.3 DHCP 一本化 — isc-dhcp-client 2.7 MiB（前提作業あり）

> **有効（ユーザー判断 2026-11-21 で確認）**: 本案は採用の
> まま。第2弾で実施する。

- isc-dhcp-client 単体で **2.7 MiB**（共有ライブラリは他と共用の
  ため連鎖は無し）、依存違反 0。
- **前提**: dhclient の実使用者が2スクリプト残っている
  （os-brainux/override の enable_ethernet_gadget と
  brain-gadget が `dhclient usb0` を呼ぶ、os-internals-2 §4 #7）。
  この2箇所を dhcpcd 呼び出しに書き換えるまでは削除しない。
  書き換えは repo 側の変更であり、本弾では仕様として記録する。

### 5.4 サービス・タイマーの手術（互換リスクなし、即実施可能群）

> **有効（ユーザー判断 2026-11-21 で確認）**: 本案は採用の
> まま。第2弾で実施する。

イメージ変更として以下を仕様化する（mask または wants 解除）:

| 対象 | 処置 | 理由 |
|---|---|---|
| apt-daily.timer / apt-daily-upgrade.timer | mask | オフライン既定。周期 wakeup と SD 書込みの発生源 |
| dpkg-db-backup.timer | mask | 日次バックアップは突然のリセット前提と相性が悪い（原子的でないコピーの周期実行） |
| e2scrub_all.timer | mask | SD 上の ext4 に対する定期スクラブは本機の故障モデルに合わない |
| fstrim.timer | mask | 対象は SD/eMMC の discard。本機では手動運用に寄せる |
| man-db.timer | mask | man 再構築の周期実行は不要（man 自体は残す） |
| fake-hwclock-save.timer | **残す** | 時刻の永続化が本機の前提（RTC 検証とセット） |
| logrotate.timer | 残す | ログ肥大の歯止めとして機能 |

- journald: /etc/systemd/journald.conf.d/ に drop-in を追加し
  `SystemMaxUse=32M` / `RuntimeMaxUse=16M` を設定する
  （日数ベースではなく容量ベース — プロジェクトの恒常原則）。
  現行は全て既定値で無調整（§3.2）。

### 5.5 残置・先送りの明記

- 端末: 実 rootfs は lxterminal（X 用）+ fbterm（コンソール用）の
  2種で役割分担があり、削除対象なし（§3.3 の訂正どおり）。
- JWM（489 KiB）: braindesk2 セッションへの切替（統合ラインが
  braingreeter2 を進行中）が済むまで残置。切替後に purge。
  Xorg 一式と libllvm19+mesa クラスタ（約142MB）は、
  「既存 X アプリが新セッションで無変更で動く」ことの証明が
  ゲート（os-internals-2 §4 #1）。LLVM 無効 Mesa への差し替えは
  ディスクのみに効く施策として別弾で測定する。
- vim: 残置（GUI の無い唯一のエディタ。vim-tiny 化は機能変更を
  伴うため利用者判断として分離）。

## 6. カーネル config 削減（第1段）— ビルド検証済み

### 6.1 適用内容（kernel-trim/trim.fragment）

§1.2 / §1.3 の判定に基づく第1段。規則: PW-SH6 に無い
ハードウェア（DTS を正とする）、起動に不要、利用者機能を
削除しない。NFS は削除ではなくモジュール化（機能保存）。

- MTD 一式を無効化: MTD core / CFI / DATAFLASH / SST25L /
  RAW NAND / **GPMI NAND** / SPI NOR / UBI（連鎖分含む）
- MTD 前提 FS を無効化: UBIFS / JFFS2
- SGTL5000 系コーデックを無効化（i.MX28 EVK 用。実在は BU26154）
- FEC（i.MX28 Ethernet）を無効化
- DRM_UDL / DRM_PANEL_SEIKO_43WVF1G を無効化
- NFS クライアントを =y → =m に降格（SUNRPC / LOCKD が連動）

config 件数（olddefconfig 適用後の実測）:
**=y 935 → 880（−55）**、=m 97 → 106（+9、NFS 一式の
モジュール化分）。ビルドツリー kbuild/.config が
config-trimmed.txt とバイト単位で一致することを後継ラインが
照合済み（cmp 一致）。

### 6.2 ビルド通過の証跡

- zImage: 先行ラインが nohup で起動したビルド
  （make O=kbuild、kernel-trim/build.log）が先行ライン終了後も
  継続し、完走した。ログ最終行
  「Kernel: arch/arm/boot/zImage is ready」。エラーなし
  （ログ中の "error" 一致は fdt_strerror.o 等のファイル名のみ）。
- モジュール: 後継ラインが同じツリーで `make -j2 modules`
  を追加実行し exit 0（kernel-trim/build-modules.log）。
- **範囲の明記: これはホスト側のビルド通過まで。削減した
  カーネルでの実機/ゲスト起動は未検証**（統合フェーズで実施）。

### 6.3 実施前後の実測

| 対象 | 削減前 | 削減後 | 差 |
|---|---|---|---|
| zImage | 5,559,104 B | **4,842,184 B** | **−716,920 B（−12.9%）** |
| vmlinux text+data（arm-linux-size 実測） | 11,446,243 B | 10,002,125 B | **−1,444,118 B（−12.6%）** |
| ビルド済みモジュール本数 | 92 本 | 101 本 | +9 本 |
| モジュール総容量 | 5,917,124 B | 7,090,848 B | +1,173,724 B |

- 削減前は dtb-out の実物で再確認した値（zImage は
  dtb-out/arch/arm/boot/zImage、モジュール 92 本も実数）。
  vmlinux の text+data は §1.1 の built-in.a 集計とは
  測定方法が異なるため、同一方法同士の前後比較として読むこと。
- モジュールが増えたのは NFS 降格の実体化
  （fs/nfs/nfs.ko、net/sunrpc/sunrpc.ko、fs/lockd/lockd.ko
  等を確認）。常駐する組み込みコードが約 1.44 MB 減り、
  その一部が「必要時にロードするモジュール」へ移った。
  機能は保存され、RAM 常駐分が減り、SD 容量は増える、
  という設計どおりのトレードオフ。

## 7. zram A/B 測定計画（実施はメモリ実測可能な環境で）

### 7.1 現状（実測）

- CONFIG_ZRAM=m、CONFIG_ZSMALLOC=m、CONFIG_ZRAM_DEF_COMP_LZO_RLE=y
  （既定アルゴリズムは lzo-rle）。CRYPTO_LZ4 は未選択（=m 化には
  追加が必要）。CRYPTO_ZSTD=y（ライブラリとしては存在）。
- 現行イメージに zram を有効化する仕組みは無い（swap 自体が無い。
  fstab はプレースホルダのまま、§3.2）。つまり現行 Brainux は
  128MB を素で使い、溢れたら OOM kill が答えになっている。

### 7.2 なぜ zram が本機の第一候補か

- 本拠が microSD のため、ディスク swap は遅さ以前に書込み寿命を
  削る。zram は SD への書込みゼロで圧力逃がしができる
  （https://www.hvr.biz/articles/topic.php?slug=zram-compressed-swap-vs-a-swap-partition-when-to-use-each
  2026-11-21 取得 — SD/フラッシュ摩耗の観点）。
- カーネル文書の目安: 圧縮率 2:1 を見込むなら zram サイズは
  RAM の2倍を超えて作る意味は薄く、実 RAM 消費は mem_limit で
  頭打ちにできる（https://mustafaerbay.com.tr/en/blog/technology/sikistirilmis-swap-zram-mi-zswap-mi/
  2026-11-21 取得、カーネル文書 zram の要約）。
- 低スペック機では圧縮アルゴリズムは速度優先で lz4 が定番
  （https://github.com/foundobjects/zram-swap/ 2026-11-21 取得。
  Armbian の <4GB 向け既定は サイズ50% / mem_limit 50% /
  swappiness 100: https://docs.armbian.com/config/storage/
  2026-11-21 取得）。ARM926EJ-S 単コアの本機では、zstd の
  圧縮率優位より CPU コストが勝つ可能性が高く、A/B で実測する。

### 7.3 A/B 設計

固定条件: 同一イメージ・同一アプリ集合・同一操作手順
（ログイン → 標準アプリ群を順に起動 → 辞書検索 → ブラウザ相当
の負荷 → 全終了）。メモリ圧力の再現性を取るため、最後に
固定サイズの匿名メモリ確保プログラムで段階的に圧力を上げる。

| 条件 | zram | disksize | mem_limit | アルゴリズム | swappiness |
|---|---|---|---|---|---|
| A（対照） | なし | — | — | — | 60（既定） |
| B | swap | 64MB | 32MB | lzo-rle（現行既定） | 100 |
| C | swap | 64MB | 32MB | lz4 | 100 |
| D | swap | 128MB | 48MB | 優勝アルゴリズム | 100 |

page-cluster は全条件 0 に固定（zram では readahead が無駄、
カーネル vm 文書の推奨系）。

### 7.4 判定指標（全てゲスト内実測）

1. OOM kill 発生回数と、圧力プログラムが確保できた上限
   （A で死ぬ負荷を B/C/D が捌けるか）。
2. `/sys/block/zram0/mm_stat`: orig_data_size / mem_used_total
   の実効圧縮率、huge_pages（圧縮不能ページ）の割合。
3. `/proc/vmstat` の pswpin / pswpout 増分と、操作手順全体の
   所要時間（体感遅延の代理指標）。
4. 手順中の最大 MemAvailable 最小値（/proc/meminfo サンプリング）。
5. 副作用チェック: 圧縮に使う CPU が描画・入力応答を
   目に見えて悪化させていないか（手順所要時間と主観確認の併記）。

採用条件: OOM 回数が A より減り、手順所要時間の悪化が
10% 以内、実効圧縮率 1.8 以上。満たさなければ zram は
入れない（128MB での CPU 税が勝つなら、それも実測の結論）。

## 付記（2026-11-21）: §6 削減の実機安全性監査（DTS × config 照合）

ユーザー懸念（削ったドライバが実機で必要だったのでは。例:
microSD / eMMC が読めなくなる等）を受け、親が実物で照合した。

- **microSD / eMMC: 安全**。削減後 config でも CONFIG_MMC=y /
  MMC_BLOCK=y / MMC_MXS=y（i.MX28 の MMC ホスト）/ EXT4_FS=y /
  VFAT_FS=y が全て残存。Brainux の起動媒体（microSD）と
  eMMC の読み取りはこの経路であり、削減対象に含まれない。
- **音声: 安全**。Brain の実コーデックは ROHM BU26154
  （imx28-brain.dtsi: `bu26154: codec@1a`、sound compatible =
  "sharp,mxs-audio-bu26154"）。削減後も SND_SOC_BU26154=y /
  SND_SOC_MXS_BU26154=y が残存。削った SGTL5000 は Freescale
  評価ボード側のコーデックで、Brain 系 DTS のいずれにも無い。
- **FEC（Ethernet）: 安全**。imx28-brain.dtsi 内の "fec" 一致は
  固定レギュレータ名 "fec-3v3" のみで、MAC ノードの有効化では
  ない。Brain に有線 LAN の実使用は無い。
- **MTD / GPMI NAND / UBIFS / JFFS2: 安全**。Brain 系9機種
  （pwa7200 / pwa7400 / pwsh1〜pwsh7、全て imx28-brain.dtsi を
  共有）の DTS で GPMI NAND を有効化するものは無い。記憶装置は
  eMMC のみで、生フラッシュ用 FS は不使用。
- **表示: 安全**。TINYDRM_BRAIN は DRM_GEM_DMA_HELPER を
  select しており、削った DRM_GEM_SHMEM_HELPER には依存しない。
  DRM_UDL（USB ディスプレイ）と Seiko パネルは Brain 非搭載。

留保: 本監査は静的な照合であり、削減カーネルの起動実証は
統合フェーズでイメージに入れて行う（未実施）。

## 付記2（2026-11-21）: バックライト DTS プロパティ名修正と DTB 検証

### 修正内容

`linux-brain/arch/arm/boot/dts/imx28-brain.dtsi` の
`backlight_display` ノード（pwm-backlight、メイン画面用）で、
バックライト有効化 GPIO のプロパティ名が `enable_gpios`
（アンダースコア）になっていたのを `enable-gpios`
（ハイフン）へ修正した（親が適用、本付記で検証と記録を担当）。
同種の綴りミスの有無は後段で照合する。

### 根拠

- 実践記録: 立命館大学の PW-SH6 実機記録（RESEARCH-brain-modding.md
  §3.6、出典 https://cysec.ise.ritsumei.ac.jp/2026/11/01/brainuxのバックライト制御〜755円の電子辞書、brain-pw-sh6にて/
  取得日 2026-11-21）で、純正と違い蓋を閉じても消灯しない問題の
  原因の一つとして、DTB 内プロパティ名 `enable_gpios` が標準
  ドライバの参照名 `enable-gpios` と一致しない点が報告され、
  DTB 書換えで消灯まで成功したとされている。
- ドライバ側の参照方式: `drivers/video/backlight/pwm_bl.c` は
  `devm_gpiod_get_optional(&pdev->dev, "enable", ...)`（513行目）
  で取得する。gpiod の DT 参照は接続 ID に `-gpios` を付けた
  プロパティ名、すなわち `enable-gpios` を探すため、
  `enable_gpios` では GPIO が見つからず（オプショナル取得の
  ためエラーにもならず）、輝度 0 でも有効化ピンの制御が
  効かない状態になる。これは実践記録の症状と一致する。

### DTB ビルド検証の経過

1. 親の初回試行は `include/config/auto.conf.cmd` で Error 2 と
   なり、`include/generated/autoconf.h` が make に削除された。
2. 原因を再現して特定した: `make syncconfig` が
   `scripts/Kconfig.include:39: C compiler
   '.../xbin/arm-linux-gnueabi-gcc' not found` で失敗する。
   xbin のシンボリックリンク先である Buildroot ツールチェーン
   （`~/workspace/brainux-driver-work/toolchain/`、gcc 13.3.0）が
   ディスク整理で削除されており、Kconfig のコンパイラ確認が
   通らないことが直接原因。syncconfig の生成物である
   autoconf.h が失敗時に make の削除対象になった。
   （検証前の config 関連ファイルは保全済み。）
3. 代替コンパイラの検討: イメージ用 rootfs 内の ARM ネイティブ
   gcc 14.2.0（Debian 14.2.0-19）を qemu-arm-static 経由で使う
   案は不成立だった。理由は2点: (a) qemu-user 8.2.2 が statx の
   パスに sysroot プレフィックスを付けない（strace で確認。
   ゲスト側のファイル存在確認がホスト側ファイルを見て失敗する）、
   (b) ゲスト内 execve がパスを無変換でホストカーネルに渡すため、
   gcc ドライバから cc1 / as への子プロセス実行が成立しない。
4. 採用した方法: 削除された Buildroot ツールチェーンの「答え」
   だけを再現するシム（シェルスクリプト）を用意した。
   バージョン照会には原物の値で答え（gcc バナー =
   .config の CONFIG_CC_VERSION_TEXT と同一文字列、
   GCC_VERSION 130300、binutils 2.41）、実作業は委譲する
   （通常コンパイル → ホスト gcc 13.3.0 ※原物と同一バージョン、
   アセンブラ照会 → qemu 経由の実 ARM GNU as、ARM 固有フラグの
   cc-option は原物の成否どおり成功扱い）。正しさの判定は
   「再生成した auto.conf が原物と一致すること」で行う。
5. 結果: `make syncconfig` が成功し、欠けていた
   `include/generated/autoconf.h` が再生成された。
   **再生成後の auto.conf は原物とバイト単位で完全一致**
   （全プローブが原ツールチェーンと同じ答えを返した証拠）。
   ツリーは壊れた状態ではなく、元の config 状態に復旧した。
   なお auto.conf.cmd の差分は、記録されるツールパス
   （CC / LD / NM / OBJCOPY / AR の照合行）のみで、config の
   内容ではない。ここから原ツールチェーンの実体も判明した:
   Bootlin の armv5-eabi / glibc / stable-2024.05-1
   （`~/workspace/buildbrain-work/toolchain/` にあったもの。
   これも削除済み）。

### DTB ビルド検証結果

- `make imx28-pwsh6.dtb`（ARCH=arm、コンパイラは上記シム、
  DTS プリプロセスはホスト gcc -E）が成功し、
  `os-surgery/kbuild/arch/arm/boot/dts/imx28-pwsh6.dtb`
  （24,502 バイト）が生成された。zImage の再ビルドは行って
  いない（本検証の対象外）。
- 生成 DTB を dtc でデコンパイルして確認した:
  `backlight@0` ノード（compatible = "pwm-backlight"）に
  **`enable-gpios = <0x1a 0x05 0x00>`**（gpio3 の5番、
  GPIO_ACTIVE_HIGH。DTS の指定どおり）が入っている。
  **`enable_gpios` は DTB 全体で0件**。
- kbuild ツリーの最終状態: autoconf.h と、親の失敗時に
  削除されていた rustc_cfg まで再生成済み。壊れた状態では
  残していない。

### 同種の綴りミスの全数照合

`imx28-brain.dtsi` と `imx28-pw*.dts` 全9機種分について、
アンダースコアを含む名前を全数 grep 照合した。

- `*_gpios` 形式のプロパティ名: **修正した1件以外に無し**。
- `keyboard_gpio` の一致はノード名/ラベル
  （compatible = "sharp,brain-kbd-gpio" のノード定義と
  `&keyboard_gpio` 参照）であり、プロパティ名ではない。
  ラベルでのアンダースコアは DT の正規の書き方で、問題なし。
- アンダースコアを含むプロパティ名は他に `dr_mode` と
  `device_type` のみ。どちらも DT の標準綴りそのもの
  （USB の dr_mode、device_type は仕様上アンダースコア）
  であり、正しい。

### 留保

本付記の検証は DTS → DTB のビルドと内容確認まで。
**実機で蓋閉じ・輝度0に連動してバックライトが消灯することの
確認は未実施**であり、イメージ再発行後の統合フェーズで
削減カーネルの起動確認と合わせて行う。
