# OS 魔改造 第2弾 — 統廃合と入れ替えの「実施」

日付: 2026-11-21 開始
範囲: 第1弾（REPORT-os-surgery-1.md）§5 の実行案のうち残った項目の
実施と、ライブラリ/コンポーネントの入れ替え検討・A/B。
作業は全て os-surgery/staging/ 内で完結させる。image-work/rootfs は
読み取り専用（複製元）。rebuild/ にも書き込まない。
repo へは commit / push しない。

## ユーザー判断（2026-11-21、厳守）

- **開発ツールと ssh は残す**。第1弾 §5.1（開発ツール分離）と
  §5.2（ssh 除去）は**不採用**（本弾では実施しない）。
  理由: Wi-Fi が無いため接続は USB ガジェットのネットワーク
  経由であり、ssh もそれ経由で使う。
- USB ガジェットのネットワーク機能は維持すべき機能として扱う。
  （事実記録: 現行イメージの実装は NCM 関数
  （functions/ncm.usb0）。ユーザーの言う「RNDIS」はこの
  USB 経由ネットワーク機能の意として扱い、本弾の変更で
  この機能を壊さないことを必須条件とする。）
- アーキテクチャ表記は i.MX28 / ARM926EJ-S / ARMv5TE と正確に書く。

## 進捗ログ

- 2026-11-21 着手。第1弾レポートを通読。素材確認:
  - image-work/rootfs: 1.9G（実ファイル量）、dpkg 659 パッケージ。
  - qemu-arm-static はホスト /usr/bin と rootfs 内の両方にあり、
    chroot は image-work/run_chroot_setup.sh の方式
    （binfmt 登録 + proc/sys/devpts マウント + chroot を
    1回の exec 内で完結）を staging 用に適応して使う。
- 2026-11-21 §5.3 の前提調査（rootfs 実物）:
  - dhclient の実使用者は rootfs 内では
    /usr/bin/enable_ethernet_gadget のみ（末尾で引数なしの
    `dhclient` を実行 = 全インターフェース対象）。
    repo override の usr/local/bin/brain-gadget も bind() で
    `dhclient usb0` を呼ぶが、**現行 rootfs には未収録**
    （override 側にのみ存在）。本弾では staging 変更セットに
    brain-gadget の書き換え版を同梱し、イメージ再発行ラインが
    repo override へ適用できる形で残す。
  - rootfs の enable_ethernet_gadget は repo override と同一
    （diff 一致を確認）。
  - /etc/network/interfaces は interfaces.d/* の source のみで、
    interfaces.d は空 → ifupdown 経由の dhclient 呼び出しは無い。
  - dhcpcd は /sbin/dhcpcd（404,132 B）が入り、dhcpcd.service が
    有効。dhcpcd.conf は Debian 既定（全インターフェース対象）。
  - ethernet_gadget.service は [Install] はあるが
    multi-user.target.wants に symlink が無く、現行イメージでは
    既定有効ではない（手動起動される運用）。本弾の範囲外だが
    DHCP 一本化の前提として記録する。
- 2026-11-21 staging 複製を開始（rsync -aH --numeric-ids）。
- 2026-11-21 §1 実施完了: §5.4 タイマー6件 mask + journald
  drop-in、§5.3 DHCP 一本化（isc-dhcp-client purge、
  2,817 kB 解放）を apply-changes.sh 経由で適用し、
  chroot 内で全件検証（§1）。
- 2026-11-21 §2/§3 の実測完了: 全 ELF 2,593 件の NEEDED 走査、
  候補ごとの A/B（apt 連鎖シミュレーション、qemu -cpu arm926
  での既知答テスト、fontTools によるフォント実測）。
  結論は §2・§3。入れ替えの staging 適用は無し（全候補が
  「パッケージ単位では互換を壊す」か「測定で利得なし」。
  利得のある2件は再構築ライン向け候補として数値付きで記録）。

## 0. 測定の前提

- 対象機 PW-SH6: i.MX287（ARM926EJ-S / ARMv5TE）、RAM 128MB。
- 数値は staging 実物での実測（dpkg status、ファイル実在、
  qemu-arm chroot での実走）に基づく。ホストの x86 バイナリでの
  測定を ARM の性能として扱わない。qemu ユーザーモードの
  所要時間は実機性能の代理にはならないため、性能比較は
  「同一 qemu 環境での相対比較」に限定し、その旨を明記する。
- 適用前後の記録は staging に対して行い、再適用可能な
  変更スクリプト + マニフェストとして os-surgery/ に残す。

## 1. 統廃合の実施（第1弾 §5.3 / §5.4）— 実施済み

適用先: os-surgery/staging/rootfs（image-work/rootfs の複製、
rsync -aH --numeric-ids）。適用は全て apply-changes.sh 経由
（再適用可能・冪等）。マニフェストは changeset.json。

### 1.1 適用前後の実測

| 項目 | 適用前 | 適用後 |
|---|---|---|
| パッケージ数 | 659 | **658** |
| Installed-Size 合計 | 1,346.3 MiB | **1,343.6 MiB**（−2.7 MiB） |
| timers.target.wants | 8 件 | 8 件（うち6件を mask で上書き） |
| journald | 全て既定値 | drop-in で容量上限を設定 |

### 1.2 §5.4 サービス・タイマー手術

- mask（/etc/systemd/system/<unit> → /dev/null）:
  apt-daily.timer / apt-daily-upgrade.timer /
  dpkg-db-backup.timer / e2scrub_all.timer / fstrim.timer /
  man-db.timer。
- 残置（第1弾の表どおり）: fake-hwclock-save.timer（時刻永続化）、
  logrotate.timer（ログ肥大の歯止め）。
- 検証（qemu-arm chroot 内 systemctl is-enabled 実測）:
  mask 6件は全て `masked`、残置2件は `enabled`。
- journald: /etc/systemd/journald.conf.d/50-brainux.conf に
  SystemMaxUse=32M / RuntimeMaxUse=16M。日数ベースではなく
  容量ベース（恒常原則どおり）。

### 1.3 §5.3 DHCP 一本化

- 前提作業: dhclient の実使用者は rootfs 内では
  enable_ethernet_gadget のみ（§0 の調査）。
  - staging の /usr/bin/enable_ethernet_gadget:
    末尾の素の `dhclient` → `dhcpcd usb0` に書き換え
    （対象インターフェースを usb0 に限定する意味もある。
    従来の素の dhclient は全インターフェースが対象だった）。
  - repo override の brain-gadget は現行 rootfs 未収録のため、
    書き換え版を override-fixes/usr/local/bin/brain-gadget
    として変更セットに同梱（bind() の `dhclient usb0 &` →
    `dhcpcd usb0 || true`。dhcpcd は常駐デーモンへの指示でも
    単体起動でも自己デーモン化するため & は不要）。
    イメージ再発行ラインが repo override へ適用する。
- 本体: chroot 内 apt-get purge isc-dhcp-client を実行。
  事前シミュレーション（apt-get -s）で削除は当該1件のみ・
  連鎖なしを確認してから実施。apt の実測で **2,817 kB 解放**。
- 検証:
  - dpkg status から isc-dhcp-client が消失（659→658 件）。
  - rootfs 内の dhclient 参照は消滅（usr/bin / usr/local /
    etc/systemd / etc/network / usr/lib/systemd を grep、
    apparmor プロファイルも purge で除去済み）。
  - dhcpcd は 10.1.0 が chroot で実走（--version）。
    dhcpcd.service は enabled のまま。
  - enable_ethernet_gadget は sh -n 構文 OK。
- **USB ガジェットネットワーク（NCM）は無変更**: スクリプトの
  変更は DHCP クライアントの呼び出し1行のみで、gadget の
  構成（configfs、ncm.usb0、UDC バインド）には触れていない。
  ssh.service も enabled のまま（ユーザー判断どおり維持）。
- 実施時の注記: purge 中に man-db トリガが
  `setpriv: failed to execute /usr/bin/mandb: Permission denied`
  を1回出した（統合ラインも観測した既知の VM 環境要因。
  purge 自体は完走し、上の検証は全て通過）。実機での mandb
  動作は未検証（統合フェーズの項目）。

### 1.4 不採用の確認（ユーザー判断）

- §5.1 開発ツール分離（71件 / 327.0 MiB）: **実施しない**。
  staging に gcc 等が残っていることを変更していない事実として
  記録するに留める。
- §5.2 ssh 除去（7件 / 4.0 MiB）: **実施しない**。
  openssh-server は残置し、ssh.service は enabled のまま
  （§1.3 の検証で実測）。

### 1.5 観点（恒常原則）

- コピー/オーバーヘッド削減: DHCP クライアントの二重常駐
  （dhcpcd デーモン + dhclient の都度起動）を一本化。
  タイマー6件の停止で周期 wakeup と不要なプロセス起動を除去。
- キャッシュ/メモリ管理: journald に容量上限（32M/16M）を
  明示し、ログの無制限な肥大を構造的に禁止。タイマー停止は
  apt/man-db 等の周期的なキャッシュ再構築も止める。
- 実機の現実への対応: オフライン既定の端末で apt-daily 系の
  周期処理は実益が無く、SD への周期書込みと突然のリセット時の
  中途半端な書込みの発生源だった。dpkg-db-backup の日次コピーは
  原子的でないため、リセット前提の本機では害が勝つ。
  fake-hwclock-save は時刻永続化の前提として残した。

## 2. ライブラリ/コンポーネントの入れ替え検討（実測と A/B）

### 2.1 方法

- staging の全 ELF 2,593 件を走査（scan_libs.py →
  libscan.json）: NEEDED/SONAME グラフ、パッケージ別の
  実ファイル量（dpkg .list 合算）、Tag_CPU_arch 分布。
- 削除系の A/B は chroot 内 apt-get -s（シミュレーション）で
  連鎖範囲を確定してから判定。実 purge は §1 の DHCP のみ。
- 実行系の A/B は qemu-arm の CPU モデル指定（-cpu arm926 =
  ARM926EJ-S）と既知答テスト（AES/SHA の公開テストベクタ、
  atomic の値検証）で、ホストの x86 測定と混同しない。
- パッケージ実量の上位（実ファイル量）: libllvm19 111.6 MiB、
  fonts-noto-cjk 89.1、gcc-14 55.7（残置・ユーザー判断）、
  git 44.7（同）、vim-runtime 35.8、libicu76 35.6、
  libpresage-data 34.8、libgtk-3-common 29.8、
  g++-14 28.9（残置）、anthy-common 27.3、
  mesa-libgallium 27.0、libz3-4 23.9、libc6 22.1、
  coreutils 19.5。

### 2.2 候補ごとの判定

**(1) Mesa/LLVM（libllvm19 111.6 MiB）— パッケージ削除は不採用、
再構築候補としては最大**
- libLLVM.so.19.1（116,959,732 B）の利用者はシステム全体で
  **mesa-libgallium の1ファイルのみ**（実測）。libgallium の
  利用者は libegl-mesa0 / libgbm1 / libglx-mesa0 の6ファイル。
  libGL.so.1 の利用者は xserver-xorg-core の1ファイルのみで、
  アプリ側の GL 利用はほぼ無い。
- しかし chroot で `apt-get -s purge libllvm19` を実走すると、
  連鎖で **netsurf-fb と libsdl2 / libsdl1.2 まで削除**される
  （合計16パッケージ。SDL が libgl1/libegl1 を必須依存に
  持つため）。既存アプリを壊すため**パッケージ単位の削除は
  不採用**。
- 正しい形は「LLVM 無効（softpipe のみ）の Mesa 再構築」で、
  パッケージ構成と soname を保ったまま libllvm19 だけを
  不要化できる（Mesa では llvmpipe が LLVM の利用者で、
  softpipe は LLVM 不要 — mesa の gallium 構成が裏付け）。
  利得はディスク 111.6 MiB。RAM への効き（Xorg 起動時に
  libgallium/LLVM が実際にマップされるか）はゲスト実測が
  必要で、本弾では**未実測**（統合フェーズの測定項目）。
  → 再構築ライン向け候補 C-1 として記録。

**(2) ICU データ（libicudata.so.76 = 30.4 MiB）— パッケージ削減は
不可、再構築候補**
- libicu76（35.6 MiB）の外部利用者は **libvte-2.91-0 のみ**
  （lxterminal の端末ウィジェット。dpkg 逆依存で実測）。
  データ本体 libicudata 30.4 MiB が全ロケール分を担っている。
- Debian パッケージ単位ではデータを削れない。ICU 公式の
  Data Build Tool（ICU_DATA_FILTER_FILE）でロケールを
  ja+en 等に絞ったデータ再構築が正道（公式ドキュメント
  https://unicode-org.github.io/icu/userguide/icu_data/buildtool.html
  2026-11-21 取得。OpenEmbedded の実例では 3ロケールへの
  絞り込みで libicudata 27MB → 12MB）。
  → 再構築ライン向け候補 C-2 として記録。本弾では未実施。

**(3) フォント（fonts-noto-cjk 89.1 MiB）— 実測の結果、不採用**
- 実物は TTC 4ファイル（Sans Regular 18.6 / Sans Bold 19.1 /
  Serif Regular 25.2 / Serif Bold 26.1 MiB）。Sans は
  10フェイス（Sans 5地域 + Mono 5地域）、Serif は5フェイス。
  全フェイスが同じ 65,535 グリフを共有し、コードポイント数は
  全フェイス同一の 44,810（TTC は地域フェイス間でアウトラインを
  共有する設計。fontTools で実測）。
- 「JP フェイスだけ抜き出す」A/B を実際に抽出して測定:
  JP 全フェイス（Mono 含む）の単体合計は **111.6 MiB で
  TTC 合計より大きい**（共有の喪失）。Mono を捨てて
  プロポーショナル JP のみにしても 79.7 MiB で、
  節約は 9.3 MiB（10%）に留まり、端末用の Mono JP を失う。
- 残る削減はグリフ単位のサブセット化だが、それは辞書端末に
  必要な漢字カバレッジ（JIS X 0213 等）を削る方向であり、
  利得とリスクが見合わない。**不採用**（抽出物は破棄済み）。

**(4) TLS 二重スタック（OpenSSL + GnuTLS、計 15.8 MiB）— 記録のみ**
- libssl.so.3 の利用者18ファイル / libcrypto.so.3 は68ファイル
  （apt、curl、python、openssh 等）。libgnutls.so.30 の
  利用者13ファイル（cups、wget、vte、curl-gnutls 等）。
  libcurl も OpenSSL 版（libcurl4t64）と Gnutls 版
  （libcurl3t64-gnutls）が併存する。
- 利用者が重ならないため、片方への寄せは利用側の再構築が
  前提。パッケージ単位の統廃合は不可能。**現状維持**とし、
  二重化のコスト（GnuTLS 側クラスタ約 8.8 MiB）を記録する。

**(5) zlib — 現状維持**
- libz.so.1 の利用者は 260 ファイル（libc の次に多い基盤）。
  高速化候補の zlib-ng は Debian trixie にパッケージが無い
  （staging 内 apt-cache search で実測確認）。入れ替え不可。

**(6) libc — 現状維持**
- glibc（libc6、22.1 MiB）が唯一の libc で全 ELF の基盤。
  musl / uClibc は Debian の ABI と互換でなく、ディストリ
  ビューション自体の作り替えなしに入れ替えられない。対象外。

**(7) coreutils / busybox — 現状維持**
- coreutils は Essential: yes（dpkg status で実測）のため
  Debian の枠組みでは除去不可。busybox（1.05 MiB）は既に
  併存している（initramfs 側の道具）。重複ではなく役割分担。

**(8) 画像ライブラリ — 重複なし**
- libpng16（利用者13）、libjpeg62-turbo（11）、libwebp7（6）、
  libtiff6 はいずれも単一実装で実利用者あり。入れ替え対象なし。

**(9) 現行 IME スタック（fcitx + anthy + presage = 69.3 MiB）—
staging では残置**
- 内訳: libpresage-data 34.84 + anthy-common 27.31 +
  fcitx-modules 5.34 + fcitx-data 0.83 + libpresage1v5 0.44 +
  fcitx-anthy 0.39 + fcitx-bin 0.16 + anthy 0.04 MiB。
  libpresage1v5 の逆依存は fcitx-modules（実測）。
- これは現行イメージの日本語入力そのものであり、既存機能の
  互換のため staging では触らない。作り直し側の libbrainime
  （辞書 BIDF 7.76 MB）がこの置き換え先であり、比較値として
  69.3 MiB を記録する（実施は作り直しラインの統合時）。

### 2.3 観点（恒常原則）

- コピー/オーバーヘッド削減: 本弾の A/B は「二重スタックの
  実在確認」が主結果。TLS の二重化（15.8 MiB）と GL スタックの
  実質未使用（利用者1〜6ファイルに対して 140 MiB 級の
  クラスタ）を数値で確定した。
- キャッシュ/メモリ管理: ディスク量と RAM 常駐は分けて記録
  した。LLVM/Mesa と ICU データはいずれも「ロードされれば
  ページを食うが、常駐とは限らない」ため、RAM への効きは
  ゲスト実測なしに主張しない（未実測と明記）。
- 実機の現実への対応: 互換を壊す削減（Mesa 連鎖削除）は
  測定で確認した上で不採用にした。フォントは「抜けば減る」
  という想定を実測が否定した（TTC の共有構造）。

## 3. アーキテクチャ最適化の確認（i.MX28 / ARM926EJ-S / ARMv5TE）

### 3.1 前提の訂正: Debian armel の基線は ARMv4T ではない

- Debian armel の基線は **Debian 10 (buster) で ARMv4T から
  ARMv5TE に引き上げ済み**（gcc の Debian バグ #882174、
  2017-12 に修正: buster 以降 with_arm_arch = armv5te。
  #933101 も「buster の最小要件は 5T、4T は stretch が最後」
  と明記。2026-11-21 web で取得）。
- したがって「armel 既定 = armv4t のまま」という前提は
  stretch 以前の話であり、現行 rootfs（trixie）には当てはまらない。

### 3.2 rootfs 実測（Tag_CPU_arch 分布、全 2,593 ELF）

| Tag_CPU_arch | 件数 | 内訳 |
|---|---|---|
| v5TE | 2,491 | Debian パッケージの本体 |
| v5TEJ | 32 | 主に Java 系等 |
| v4 | 34 | 全て klibc（initramfs 用。旧 ISA のコードは新 CPU で動くため問題なし） |
| v6 | 2ファイル | libnettle.so.8（§3.3 で検証） |
| v7 | 3ファイル | libatomic.so.1、libdav1d.so.7（§3.3 で検証） |
| タグなし | 29 | 後述（走査方法の artifacts + qemu-arm-static） |

- つまり現行 rootfs は既に **ARMv5TE 基線でビルドされており、
  ARM926EJ-S の ISA（ARMv5TEJ）と一致**している。基線の
  引き上げによる利得は存在しない。
- タグなし29件の内訳: /usr/bin/awk 等の alternatives 系は
  絶対シンボリックリンクをホスト側で辿った際の読み違い
  （実ファイルの mawk は ARM EABI5 と file で確認済み）。
  ツリー内の実在の x86-64 ファイルは /usr/bin/qemu-arm-static
  のみ（統合ラインが chroot 構築用に置いた道具で、出荷物では
  ない）。

### 3.3 基線超過タグの実走検証（qemu -cpu arm926 = ARM926EJ-S）

タグが CPU より新しい3ライブラリについて、「タグは最上位
バリアントの表示で、実行時は CPU に合った経路が選ばれる」
ことを実走で裏付けた:
- **libatomic.so.1（v7 タグ）**: IFUNC シンボル 68件（実行時
  振り分けの設計）。テストプログラムを toolchain で組み、
  staging の実 libatomic をリンクして -cpu arm926 で実行:
  32bit / 64bit の atomic fetch-add が正しい値を返し正常終了。
- **libnettle.so.8（v6 タグ）**: _arm / _armv6 / _neon の
  バリアント併存（fat ビルド）。AES-128 暗号化、SHA-1、
  SHA-256 を -cpu arm926 で実行し、全て公開テストベクタと
  一致（AES: 66e94bd4…、SHA-1(abc): a9993e36…、
  SHA-256(abc): ba7816bf…）。armv6/neon 経路は v5TE では
  実行されていない。
- **libdav1d.so.7（v7 タグ）**: getauxval を import しており
  実行時 CPU 判定が設計に入っている（利用者は librsvg2 のみ）。
  デコード自体の実走は本弾では**未実施**（AV1 サンプルと
  呼び出し側の用意が必要。タグと import の証拠まで）。
- 結論: 3件とも欠陥ではない。タグだけで「動かない」とは
  言えないことの実証として記録する。

### 3.4 作り直しスタック（rebuild-arm）のビルドフラグ

- libbrainui2.so の Tag_CPU_arch は **v5TEJ**（Tag_CPU_name
  "5TEJ"）。使用 toolchain（armv5-eabi--glibc、Buildroot）の
  既定ターゲットが ARM926EJ-S 級のため、出力は最初から
  実機 ISA に一致している。
- ただし rebuild の Makefile の CFLAGS は
  `-std=c11 -Wall -Wextra -O2 -g` で、**-mcpu / -march の
  明示が無い**（実測）。現在の一致は toolchain 既定への
  依存であり、toolchain を差し替えると silently ずれる。
- 是正案（提案、未実施）: 作り直し側のビルドフラグに
  `-mcpu=arm926ej-s` を明示する。ISA は v5TEJ のままで
  変わらず、効くのは命令スケジューリング（mtune 相当）と
  前提の固定化のみ。性能差の数値は主張しない（未実測）。

### 3.5 カーネル

- dtb-out/.config: CONFIG_CPU_ARM926T=y、
  CONFIG_CPU_32v5=y、CONFIG_ARCH_MXS=y。カーネルは
  最初から実 CPU 指定であり、対象外。

### 3.6 観点（恒常原則）

- コピー/オーバーヘッド削減: 基線引き上げという施策自体が
  前提の誤りだったことを実測で示した。残る無駄は
  「toolchain 既定への暗黙依存」という再現性の問題として
  是正案に落とした。
- キャッシュ/メモリ管理: ISA 基線はメモリ量に直接効かない。
  効くのは §2 のライブラリ選定であり、本節はその前提
  （全バイナリが実機で実行可能か）の検証として位置づける。
- 実機の現実への対応: 「タグが新しい = 実機で落ちる」という
  短絡を避け、実 CPU モデルでの実走（既知答一致）で
  実行可能性を確定させた。dav1d のデコード実走のみ未実施と
  明記する。

## 4. 本弾の到達点と残件

到達点:
- 第1弾 §5 の残項目（§5.3 / §5.4 / journald）は staging に
  適用済みの変更セットとして完成（§1、changeset.json、
  apply-changes.sh、override-fixes/）。
- 入れ替えは全候補を実測で判定し、staging への追加適用は
  ゼロ。再構築ライン向け候補2件（C-1: LLVM 無効 Mesa、
  C-2: ICU データ絞り込み）を数値付きで記録した。
- アーキテクチャは rootfs / 作り直し / カーネルの3層とも
  ARMv5TE(J) で実機と一致していることを実測で確定。
  前提（armv4t 基線）は訂正した。

残件（本弾の範囲外・未検証の明記）:
- C-1 / C-2 の実施可否は再構築ラインとイメージ再発行時の
  判断。RAM への効きはゲスト実測が必要。
- libdav1d のデコード実走（-cpu arm926）は未実施。
- アイドル時メモリ実測（第1弾 §3.4 から継続して未実測）。
  統合フェーズの SD イメージ発行後に実機/ゲストで測る。
- イメージ再発行そのものは別ライン（M21 の前提訂正後）。
  本弾の変更セットはその入力として changeset.json に固定した。
