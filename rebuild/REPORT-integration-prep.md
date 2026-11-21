# 統合フェーズ準備 — ARM クロスビルドと SD イメージ組立て

作成: 2026-11-21（作業の区切りごとに追記する）

## 0. 本書の位置づけ・合格範囲

- 目的: ホスト（x86-64）で検証してきた rebuild スタックを実機アーキテクチャ
  （ARMv5TE / armel, Debian trixie）でビルドし、起動可能な SD イメージとして
  組み立て、**ホスト側で構造を機械検証する**ところまで。
- **ゲストでの実起動検証は本弾の範囲外**（次段: QEMU 側との連携）。
  本書で「起動できる」とは主張しない。主張するのは
  (1) ARM バイナリ群が揃い `file` で ELF 32-bit ARM EABI5 を確認したこと、
  (2) イメージのパーティション構成・ファイル配置・DTB 内容・起動スクリプトの
  参照整合性が既存仕様（buildbrain repo の `image/build_image.sh`）どおりで
  あることを機械的に確認したこと、まで。
- repo へは commit / push しない。eMMC には一切触れない。

## 1. 棚卸し（2026-11-21 時点）

### 1.1 rebuild コンポーネントと ARM ビルド要否

| コンポーネント | 成果物 | 外部リンク依存 | ARM ビルド |
|---|---|---|---|
| ツールキット (rebuild/) | libbrainui2.a / libbrainui2.so.1.0 | -lm のみ | 対象 |
| libbrainime | libbrainime.a（+ 辞書データ brainux3.bid3） | なし（純 C） | 対象（.a のみ。tools はホスト用） |
| シェル (shell/) | libbdshell.a, braindesk2, braingreeter2 | -lm, -lcrypt（greeter 認証） | 対象 |
| OSK (osk/) | 単体バイナリなし（ウィジェットは libbrainui2 内、変換アダプタ osk_brainime.c は braindesk2 へリンク） | — | 対象外（braindesk2 に含まれる） |
| アプリ群 (apps/) | bd-files bd-notes bd-tasks bd-clock bd-calc bd-recorder bd-player bd-pkg bd-settings bd-admin bd-alarmd（11本） | -lm, -ldl, **-lasound**（recorder/player） | 対象 |
| ブラウザ (browser-engine/) | nsfb-arm | 済（phase2 で ELF ARM EABI5 を確認済み、`.work-p2/nsfb-arm`） | 済 |
| フォント (fonts/) | Noto サブセット（データ） | — | 配置のみ |
| マスコットパック | .bdm パック（データ。再焼き込みラインで再生成済み） | — | 配置のみ |

- 全 Makefile は `CC ?= gcc` / `AR ?= ar` 形式で、コマンドラインで
  `CC=` / `AR=` を与えればクロスビルド可能（CFLAGS/CPPFLAGS は各 Makefile が
  `+=` で組み立てるため、コマンドライン上書きはしない。sysroot 系フラグは
  ブラウザ phase2 と同じくコンパイララッパ側に寄せる）。
- ホスト側オブジェクトと混ざらないよう、ARM ビルドは staging コピー
  （`~/workspace/buildbrain-work/rebuild-arm/`）で実施し、原木はホスト検証用に
  保持する。

### 1.2 既存資産の状態

- ツールチェーン: Bootlin armv5-eabi glibc（`~/workspace/buildbrain-work/toolchain/`、
  browser-engine 側にも同一物）。`buildbrain/xbin/` に arm-linux-gnueabi-* ラッパあり。
- ARM sysroot: `browser-engine/.sysroot-arm/`（Debian trixie armel の .deb 展開のみ、
  90 パッケージ）。ただし **libasound は含まれていない**（PACKAGES.txt に無し）
  → bd-player / bd-recorder 用に追加取得が必要。
- カーネル: buildbrain/linux-brain submodule は初期化済み（pin 11c11c922）。
  zImage は未ビルド。DTB は OS内部 第1弾で `~/workspace/buildbrain-work/dtb-out/`
  （kbuild O= ツリー、brain_defconfig）にビルド済み（imx28-pwsh6.dtb 24,502 B、
  キー割当て修正込み）。
- submodule: boot4u / brainlilo / buildroot / nkbin_maker / u-boot-brain は
  未初期化だった。本弾で u-boot-brain / brainlilo / nkbin_maker を初期化する
  （`image/build_image.sh` が必要とするため）。
- 既存イメージ作成手順: repo の `image/build_image.sh ROOTFS IMG_NAME SIZE_M`。
  レイアウト: p1 = 先頭 2048 セクタから 64MiB FAT32 (type b, label "boot")、
  p2 = 残り全部 ext4 (type 83, label "rootfs")。p1 内容: brainux_version,
  zImage, imx28-pw*.dtb, nk/*.bin と FAT ルートへの *.bin（EBOOT チェーンロード用）、
  アプリ/Launch Linux/（brainlilo + exeopener）、loader/*.bin。
  rootfs は `make brainux`（debootstrap --foreign + qemu-arm-static chroot で
  setup_brainux.sh + override 適用）が正。
- ホストツール: debootstrap / mkfs.ext4 / sudo は有り。**sfdisk, kpartx,
  mkfs.fat, qemu-arm-static は無し** → .deb 展開で用意する（phase2 の流儀）。
  本 VM のシェルは root で動作する。

### 1.3 セッション/グリーターの既存配線（repo override と rebuild integration）

- repo override 側: `braindesk-session.service`（旧 braindesk 用）が
  `/usr/local/bin/braindesk-session`（旧 greeter → 旧 shell のループ）を回す。
  setup_brainux.sh は **ly を有効化したまま**で、braindesk-session は
  「未有効化」とコメントされている。
- rebuild 側: `shell/integration/` に v2 用の資産が既にある:
  - `brainux-session.sh`: braingreeter2（成功時に stdout へユーザ名）→
    runuser で braindesk2 → 異常時は startx（X セッション）へフォールバック。
  - `braingreeter2.service`: tty1 で brainux-session を回す unit。
    ly / getty@tty1 と Conflicts。**未有効化**で、ゲートは
    (a) ツールキットのパスワードマスク、(b) 統合フェーズでの X 互換 PSS 測定。
  - `setup_brainux.sh.patch`: chroot 内で rebuild をビルドして install する差分案。
- ゲート (a) は解消済み（ツールキットにパスワードマスク実装済み、グリーター v2 は
  ● マスク＋コピー/切取/貼付無効化を検証済み）。ゲート (b) はゲスト測定項目で
  本弾では未実施 → 本弾のイメージでは braingreeter2 を既定にしつつ、
  brainux-session.sh の X フォールバックは残す（安全網を外さない）。

## 2. セッション配線（bd-alarmd 常駐化＋既定セッション）

### 2.1 bd-alarmd のセッション起動配線 — 実装完了（ホスト検証済み）

方式: **bd_session が子プロセスとして所有する**（OS内部 第2弾レポートの残件
「セッションの子プロセス管理 / systemd ユニット」のうち前者を選択）。
理由: デーモンはセッションの環境（HOME / XDG_RUNTIME_DIR /
BRAINUX_CLOCK_DIR）で状態解決し、通知もセッションのソケットへ出すため、
寿命をセッションに一致させるのが最も素直で、systemd 側の追加配線なしに
グリーター→セッションの既存起動経路へそのまま乗る。

- `shell/include/session.h`: `bd_session_config.alarmd_path` を追加。
  NULL = 既定 `/usr/local/bin/bd-alarmd`、"" = 無効、存在しない/実行不可は
  **非致命**（通知サーバと同じ扱い）。`bd_session_alarmd_pid()` で状態を観測可能。
- `shell/src/session.c`: 生成時に fork/exec（引数なし、環境は継承）、
  step ごとに WNOHANG で回収（ゾンビ化防止）、free 時に SIGTERM →
  最大約1秒の猶予 → SIGKILL で確実に回収。**自動再起動はしない**
  （落ちた場合は pid が 0 になり、黙って死なない形にした。デーモン自体が
  flock で単一インスタンス保証を持つため二重起動の心配はない）。
- `shell/braindesk2.c`: 環境変数 `BD_ALARMD_PATH` で上書き可能にした
  （未設定 = 既定パス、空 = 無効）。
- テスト: `test_session` に15件追加し **138/138 通過**（従来 123）。
  内訳: 無効（""）で起動しない、欠落バイナリでもセッションが成立する、
  既定パス不在のホストで helper なしになる、即終了する helper（/bin/true）が
  step で回収され pid 0 に戻る、常駐 helper が生成時に起動し free で
  SIGTERM を受けて終了し回収済み（kill(pid,0) が ESRCH）になることまで確認。

### 2.2 グリーター電源コードの配線欠陥 — 発見と修正

- 発見: braingreeter2 の契約は「電源操作は終了コードで返す: **4 = 電源オフ、
  5 = 再起動**」（braingreeter2.c の GREETER_EXIT_POWEROFF/REBOOT）だが、
  `shell/integration/brainux-session.sh` は非0終了を全て失敗として扱い
  exit 1 → systemd がグリーターを再起動していた。つまり**グリーターで
  電源オフを押しても電源は切れず、グリーターに戻る**欠陥があった。
- 修正: brainux-session.sh で終了コードを分岐（0 = ログイン継続、
  4 = `systemctl poweroff`、5 = `systemctl reboot`、その他 = 従来どおり
  exit 1 で systemd 再起動）。ヘッダコメントにも契約を明記。
- 検証: systemctl / runuser / getent / id / startx と /usr/local/bin の
  greeter・desk をスタブ化し、5ケースを実走して確認した
  （4→poweroff のみ、5→reboot のみ、0+desk正常→runuser で braindesk2 起動し
  フォールバックなし、0+desk異常終了→startx フォールバック実行、
  1→何も呼ばず rc=1）。スタブは検証後に撤去済み。

### 2.3 既定セッションの決定

- 本弾のイメージでは **braingreeter2.service を有効化し ly を無効化**して
  v2 セッション（braingreeter2 → braindesk2）を既定にする。
- 根拠: ゲート (a) パスワードマスクは解消済み（§1.3）。ゲート (b) X 互換の
  PSS 測定はゲスト実測項目で本弾では未実施のため、安全網として
  brainux-session.sh の **startx フォールバックは残す**（braindesk2 が
  起動できない/異常終了した場合に旧 X セッションへ落ちる）。
  したがって「既定」は v2 でありながら、到達可能性は旧来経路でも担保される。
- rootfs への反映方法は §5 に記す。

## 3. ARM クロスビルド — 完了（file で形式確認済み）

- staging コピー `~/workspace/buildbrain-work/rebuild-arm/`（98MB）で実施。
  tar 展開時にホスト所有権の復元がこの環境の idmap と競合したため
  `--no-same-owner` で展開し、混入していたホスト ELF / .a を全削除してから
  ビルドした（クリーンな状態から再現）。
- sysroot: `browser-engine/.sysroot-arm/` に **libasound2-dev を追加取得**
  （fetch_debs.py の SEEDS に追加して再実行、PACKAGES.txt 更新済み）。
- ツールチェーン: browser-engine/.work-p2/armbin/ の既存ラッパを再利用
  （Bootlin armv5 GCC 13.3 + Debian trixie armel sysroot）。全 Makefile が
  `CC?=` / `AR?=` 形式のためコマンドライン指定でクロス化し、
  CFLAGS/CPPFLAGS は上書きしていない。
- 成果物は全て `file` で **ELF 32-bit LSB ARM EABI5** を確認:
  libbrainui2.a / libbrainui2.so.1.0、libbrainime.a、libbdshell.a、
  braindesk2、braingreeter2、アプリ11本（bd-files bd-notes bd-tasks
  bd-clock bd-calc bd-recorder bd-player bd-pkg bd-settings bd-admin
  bd-alarmd）。ブラウザ nsfb-arm は phase2 で作成済み
  （`browser-engine/.work-p2/nsfb-arm`、12,664,004 B）。
- 動的依存は最小: 全て libc/libm のみ + braingreeter2 は libcrypt.so.1、
  bd-player / bd-recorder は libasound.so.2（いずれも trixie armel 標準）。

## 4. カーネル / ブートチェーン — 完了

- カーネル: buildbrain/linux-brain（pin 11c11c922、作業ツリーの DTS 修正込み）
  を Bootlin ツールチェーンでビルド（`ARCH=arm CROSS_COMPILE=arm-linux-`
  brain_defconfig、O=dtb-out）。**zImage 5,559,104 B、モジュール 92 本
  （.ko）、DTB 全9機種**（imx28-pwa7200 / pwa7400 / pwsh1〜7）を生成。
  repo の lbuild は in-tree ビルドで zImage の置き場が違うが、成果物は
  等価（配置時に dtb-out からコピーする）。
  - 途中の失敗2件はいずれも環境要因で、切り分け済み: (1) 初回の起動は
    セッション喪失で中断（DTB は生成済みだった）。(2) 再開時に
    ARCH=arm を付け忘れ x86 の defconfig を探しに行き、対話的
    oldconfig プロンプトで停止 → brain_defconfig を入れ直して解決。
  - モジュールは repo の作法どおり modules_install で
    os-brainux/modules-staging へ staging し、rootfs の /lib/modules へ
    コピーする（override-pre.sh の modules 節と等価な最終状態）。
- ブートチェーン: build_image.sh の前半をそのまま再現するスクリプト
  （`image-work/build_uboot.sh`）で9機種分の U-Boot をビルドし、
  nkbin_maker/bsd-ce で nk.bin 化: **edna3exe.bin、edsa1〜3exe.bin、
  edsh4〜7exe.bin（8本）＋ lilobin 9本**（gen2 / gen2_7400 /
  gen3_1〜7、各 342〜500KB）。
  - 差分: CROSS_COMPILE は repo の xbin シンボリックリンク（リンク先の
    brainux-driver-work ツールチェーンが掃除で消えており使用不可）の
    代わりに Bootlin ツールチェーンの絶対パス prefix を使用。
    生成物の性質（arm-linux-gnueabi、GCC 13.3）は同じ。
  - ホスト側の不足ツール（bison / flex / bc / m4）は .deb 展開で
    hosttools に追加した（bison は BISON_PKGDATADIR と M4 の指定が
    必要）。
- brainlilo: cegcc（arm-mingw32ce-gcc 9.3.0、repo の Dockerfile 記載の
  GitHub リリース）でビルド済み: BrainLILO.dll / BrainLILO.exe /
  BrainLILODrv.dll。

## 5. rootfs 構築

方式: repo の `make brainux` の流れ（debootstrap --foreign → qemu-arm-static
chroot で setup_brainux.sh + override）を踏襲し、setup は §1.3 の適応版
`setup_rebuild.sh`（ly 節と旧 braindesk 節を削除し、rebuild インストール節を
追加したもの。それ以外は原文どおり）を用いる。実行は
`image-work/run_rootfs_all.sh`（冪等・再開可能、dpkg の半端な構成は
`dpkg --configure -a` で回復してから setup に入る）。

環境要因による逸脱（いずれも §8 に詳述）:
- rootfs の置き場と chown 問題: 真因は setgid 継承だった（§8）。
  最終形は btrfs 上に rootfs ディレクトリを `chmod g-s` して構築する
  方式（§5.1）。
- apt の取得はホスト先行取得＋lists 先行配置でオフライン化した
  （guest ネットワークは proxy トークン失効で信頼できない）。
  .deb キャッシュは bind で rootfs の /var/cache/apt/archives に
  繋ぎ、組立て前に中身を空にする（出荷ツリーに .deb を残さない。
  原 setup は clean しないため、これは容量対策の逸脱として明記する）。
- override-pre.sh は原文のまま使う（braindesk ソースのコピーを含む。
  本イメージでは未使用だが原フローとの差を最小化するため残す）。
  モジュールは override-pre の時点では staging が無いため、
  カーネル完成後に同じ内容（modules-staging/lib/modules を
  /lib/modules へ）を後段でコピーする。

### 5.1 結果 — 完了（ROOTFS_ALL_RC=0）

- 最終形: rootfs は **btrfs 上**（`image-work/rootfs`、1.9GB）に構築した。
  chown 問題の真因は §8 のとおり setgid 継承であり、rootfs ディレクトリに
  `chmod g-s` することで決定的に解消した（子ファイルは全て実 id 所有）。
  ツリー内の overflow 所有は rootfs ルート自体（組立て時に debugfs で
  修正）と /opt/braindesk のコピー 68 件のみで、後者は finish 段階で
  --no-preserve=ownership でコピーし直して解消した。
- 実行は再開可能な runner で通算4回に分けて完走した（サービス再起動で
  2回、gpgkey 取得失敗で1回中断し、その都度 resume）。setup の
  非冪等性への対処は2点のみ setup_rebuild.sh に入れた（いずれも
  「再開可能性のための逸脱」として明記）:
  (1) 第2段階の呼び出しを `/debootstrap` が存在する時だけに条件化
  （完了後に作業ディレクトリが消えるため、無条件だと再開できない）。
  (2) packagecloud 鍵は curl 取得をやめ、ホストで同じ URL から取得した
  本物の鍵が事前配置されていることを `test -s` で検証する形に変更
  （guest の curl は proxy に接続リセットされ2回失敗したため）。
- 完走の証跡（最終ランのログ）: locales 導入、curl/gnupg セット導入、
  packagecloud の apt update、大規模 install（約500パッケージ、
  全てキャッシュから）、brain-config 導入、rebuild スタックの
  install（バイナリ15本・lib・フォント・辞書・マスコット・
  .desktop・アイコン）、**`systemctl enable braingreeter2` の
  symlink 作成**、ユーザ user（パスワード brain、sudoers、input/video
  グループ）作成、boot.mount / brain-mode 有効化、サービス整理、
  最終 sources.list 書換えまで全て実行され rc=0。
- 副次的な観察: man-db トリガで `setpriv: failed to execute
  /usr/bin/mandb: Permission denied` が1回出た（qemu 環境での
  setpriv 制限とみられ、トリガ自体は継続し install は完走した。
  実機での mandb 動作は未検証）。

## 6. SD イメージ組立てとホスト側検証

組立て: `image-work/assemble_image.sh`。build_image.sh との差分は
充填方法のみ（本 VM に loop デバイスが無いため）:
- パーティションテーブル: 同一の sfdisk スクリプト
  （p1: start=2048, size=131072, type=b / p2: start=133120, type=83、
  全体 3072MiB）。
- p1: 64MiB の単体ファイルに mkfs.fat -n boot -F32 -I し、mtools で
  配置してからセクタ 2048 へ dd で埋め込む。配置物は build_image.sh
  と同一（brainux_version=repo HEAD の sha、zImage、全9機種の DTB、
  nk/*.bin と FAT ルートへの *.bin、アプリ/Launch Linux/ 一式、
  loader/*.bin）。mtools の日本語ディレクトリ名（アプリ）は
  LANG=C.UTF-8 で正しく作成できることを事前テストで確認済み。
- p2: mke2fs -t ext4 -L rootfs -d <rootfs> で単体ファイルを作り
  セクタ 133120 へ埋め込む。

検証: `image-work/verify_image.sh`（読み取り専用）:
sfdisk -l の表形式での配置確認、p1 を切り出して mdir -s -b
（長名パスの再帰リスト）で全配置物を照合、p2 を切り出して debugfs で
/usr/local/bin の全バイナリ・braingreeter2 の有効化 symlink・
辞書・マスコット・libbrainui2・/lib/modules の存在を確認し、
braindesk2 を dump して file で ARM EABI5 を再確認、
brainux-session の参照（greeter/desk）も照合する。DTB は
imx28-pwsh6.dtb をデコンパイルし、キー割当てセル
（row3, col4, KEY_SCROLLDOWN=0xb2）が keymap と keymap-symbol の
両方に1回ずつあることを数える（事前テストで確認済み）。

### 6.1 結果 — 組立て完了・検証 64/64 OK

- 成果物: **`~/workspace/buildbrain-work/image-work/sd.img`**
  （3,221,225,472 B = 3072MiB）。カーネルモジュールは
  modules_install で staging し rootfs の /lib/modules/6.1.70-* へ
  コピー済み（finish ログに記録）。
- 検証（`verify_image.sh`、修正版）: **64 項目全て OK、rc=0**。
  内訳: パーティション表（p1 start=2048/131072セクタ/type b、
  p2 start=133120/type 83）、p1 の全配置物（brainux_version =
  repo HEAD sha 一致、zImage、全9 DTB、ed*.bin 8本 ×2箇所、
  loader 9本、Launch Linux 一式6点、アプリ ディレクトリの
  長名も正しく格納）、p2 の /usr/local/bin 全15本、
  braingreeter2 有効化 symlink、IME 辞書、マスコットパック、
  libbrainui2.so.1.0、カーネルモジュール、イメージ内 braindesk2 の
  file 形式（ELF 32-bit LSB ARM EABI5）、brainux-session の
  参照整合性、ext4 ルート inode の所有者 0:0、DTB のキーセル ×2。
- 検証の初回は 28 件 FAIL を出したが、全て検証スクリプト側の
  欠陥だった（debugfs の `ls -la` が当該バージョンで不正オプション、
  mdir の整形リストが 8.3 表示・ロケール依存で照合不能、LANG 未設定で
  日本語名が `___` に化けた）。実物を mdir -s -b（長名パスの素リスト）と
  debugfs ls -l で直接確認して全配置物の存在を確定した上で、
  スクリプトを直して再実行し 64/64 OK を得た。教訓として、
  「FAIL はまず検証系を疑う」を実践した形になった。
- **ゲスト実起動は未検証**（本弾の合格範囲外、次段）。

## 7. 全体回帰 — 回帰ゼロ（2026-11-21 に本担当が再実行して確認）

| スイート | 結果 | ベースライン | 差分 |
|---|---|---|---|
| ツールキット (make check) | 3,577 passed, 0 failed | 3,577 | なし |
| シェル (make check) | 670 passed + test_mascot 95 checks, 0 failed | 655 + 95 | **+15 は意図した増加**（test_session 123→138、§2.1 の alarmd 配線テスト） |
| アプリ (make check) | 1,176 passed, 0 failed | 1,176 | なし |
| OSK (make check) | 319 passed, 0 failed | 319 | なし |

- クロスビルドは staging コピーで実施したため原木のビルド系への影響は
  なし。原木側の変更は §2 の session.c/h・braindesk2.c・test_session.c・
  brainux-session.sh のみで、その影響は test_session の +15 件（全通過）
  として切り分け済み。

## 8. 未検証・残件

- ゲスト実起動（次段）。
- **本 VM の loop デバイス不在**: /dev/loop-control が無く losetup が
  使えないため、build_image.sh の kpartx 経路はそのままでは使えない。
  イメージ組立てはレイアウト（パーティション配置・内容）を
  build_image.sh に一致させ、充填方法のみ代替する（FAT は mkfs.fat +
  mtools 系、ext4 は mke2fs -d、dd でオフセット配置、sfdisk で
  テーブル作成）。差分は §6 に明記する。
- **chown 問題の真因は setgid 継承だった**（実測で確定。当初「exec
  セッションごとに idmap の flavour が違う」と誤認し、tmpfs 方式や
  良セッション抽選に時間を費やした — その経緯も含めて正直に記す）:
  ホームツリー全体が mode 2770（setgid、group 65534）であり、その下で
  作成したファイルは group 65534（未マップ overflow）を継承し、
  以後どのプロセスからも chown できなくなる。これが dpkg postinst を
  壊した（base-files の /var/mail chown が EPERM で失敗し debootstrap
  第2段階が停止）。「良セッション」に見えたのは、setgid でない
  ディレクトリ（debootstrap が作った rootfs/tmp、/tmp 等）で作った
  ファイルが 0:0 になっていただけだった。決定的な切り分け:
  同一セッション内で setgid ディレクトリ配下の新規ファイルは
  0:65534 で chown 不可、`chmod g-s` したディレクトリ配下では
  0:0 で chown 可。**対策: rootfs ディレクトリを事前に chmod g-s
  する**（中のファイルは作成者の egid=0 か tar/dpkg の明示所有になり
  全て chown 可能）。rootfs ルート inode 自体の group 65534 は
  組立て時に debugfs で 0 へ修正する。mmap_min_addr は読み取り専用で
  0 にできない（Makefile の前提と違う点。qemu 8.2 のフォールバックで
  実害は出なかった）。
- **curl シム案は不採用**: packagecloud 鍵の取得失敗への対策として
  /usr/local/bin/curl にシムを置く案を試したが、シムとステージした
  鍵ファイルが stage 2 実行区間で消失し（消失の最終的な機序は
  特定できていない）、setup 側で事前配置済み鍵を検証する形（§5.1）
  に置き換えた。シムは出荷ツリーに残っていない（finish で除去を
  確認する手順も残してある）。
- **本 VM の egress proxy と guest ネットワーク**（実測で判明）:
  chroot 内（qemu）の apt/curl は不安定で使えない。exec セッションの
  proxy トークンは短命で、長時間の chroot 実行中に失効すると全ての
  取得が Connection failed / Ign になり、setup が locales の install で
  停止した（1回目の tmpfs 実行はここで終了）。ホスト側の取得は高速・
  安定（無認証の `hatch-egress-proxy:3128` でも可、ホスト apt の
  80hatch-egress-proxy 設定がそれ）。対策: **必要 .deb を全てホストで
  先行取得**する。guest と同じ base status（debootstrap 直後の dpkg
  status）＋完全な lists でホスト apt シミュレーション
  （`image-work/apt-sim/`）を回し、`--print-uris` の閉包 523 件
  （325MB）を aptcache へ取得（失敗0）。sim の検証済み lists を
  rootfs へ先行配置し、chroot の proxy 環境変数は無認証 proxy に
  固定、apt には proxy 設定とリトライ設定を conf で入れる。
  packagecloud の index は apt の https メソッドだと proxy で
  詰まるため curl で直接取得して lists に配置した。
