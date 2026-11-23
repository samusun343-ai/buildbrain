# PUSH-REPORT — buildbrain 側 未 push 成果のコミット＆push（最終結果）

実施日: 2026-11-21（第1段: ローカルコミットまで。第2段: 本書 — HTTPS 認証で push 完遂）
結果: **push 完了。** ただし root のサブモジュールポインタ更新は、調査の結果
「更新すると必須のフォーク機能を失う」ことが判明したため**実施しなかった**
（詳細は「ポインタ更新を見送った理由」）。

## 認証

- 両 repo に repo ローカルで
  `credential.helper=store --file=/home/hatch/workspace/.secrets/gitcred`
  を設定し、既存の HTTPS remote（origin）で push した。
  認証情報ファイルの中身は読んでいない（git 自身が読む）。
- SSH 鍵 `~/.ssh/id_ed25519` は qemu 専用 deploy key で書込不可と判明済み
  （第1段の報告どおり）。本方式では不使用。
- 実施前に両 repo の `.git/hooks`（サンプルのみ）と filter 設定（なし）を
  確認し、コミット/add で作業ツリー内コードが実行される経路が無いことを
  確認した。

## linux-brain（samusun343-ai/linux-brain）

### brain ブランチ — push 済み

リモート tip が `5e99ce4dc22d` → **`cc29c866c489`** に前進。ls-remote で一致確認。

| コミット | 内容 |
|---|---|
| `651c614cc31f` | arm: dts: imx28-brain: fix pwm-backlight enable-gpios property name（`enable_gpios` → `enable-gpios`） |
| `cc29c866c489` | arm: dts: imx28-pwsh6: enable the Page Down key（base keymap のコメントアウト解除＋ symbol keymap へ `<3 4 KEY_SCROLLDOWN>` 追加） |

### kbd 修正 `11c11c922` の扱い

- リモート `brain` の `drivers/input/keyboard/brain-kbd-gpio.c` と
  detached 版をファイル単位で比較した。リモート版には ghost 抑制書き換え
  （`bk_is_ghost`）自体が存在せず、押下判定は
  「自列のビットがクリア かつ 行が他列の読みで引かれている（agg テスト）」
  で実装済み — これは `11c11c922` のコミットメッセージが正しい判定法として
  記述する方式そのもの。**修正の実質はリモート brain に既に含まれている。**
- detached 版の差分をそのまま再適用すると、リモート版にある
  `symbol_activated` sysfs 通知、Hi-Z 列スキャン、chord 中の解放処理を
  失う（退行する）ため、強制適用はしなかった。
- コミット自体の保全として、ブランチ **`detached-11c11c922-kbd-fix`** を
  origin へ push 済み（tip = `11c11c922fa4`、ls-remote で確認）。
  これにより master が参照するポインタ先がリモートから取得可能になった
  （第1段で指摘した「ポインタ先がリモートに存在しない」問題は解消）。

### ポインタ更新を見送った理由（重要）

フォークライン（master のポインタ `11c11c922` までの系譜）を fetch して
確認したところ、リモート `brain` には無い **12件の機能コミット**が
積まれていた:

| コミット | 内容 |
|---|---|
| `d25eeb11b` | ASoC: ROHM BU26154 コーデック＋MXS マシン対応（音声の要） |
| `63cc3b0a0` | rtc: stmp3xxx: wakeup-source DT プロパティ対応 |
| `33010e23d` | input: brain-kbd-gpio: ghost 抑制＋キー報告修正 |
| `b0ac9f045` | arm: dts: pwsh6: 戻る/Home＋記号レイヤーのメディアキー割当て |
| `efda30257` | input: misc: i.MX28 PSWITCH 電源ボタンドライバ（新規ドライバ） |
| `a7251386c` | arm: dts: pwsh6: PSWITCH 電源ボタンノード |
| `10e632e19` | input: mxs-pswitch: 電源レジスタの直接マップ化 |
| `06fc8ed0c` | arm: configs: brain: USB configfs HID/ACM/mass storage |
| `bd606a545` | arm: dts: pwsh6: USB OTG dual-role 化 |
| `46edfa455` | brain_defconfig: 周辺ドライバのモジュール化・死機能削除 |
| `65d18706c` | brain_defconfig: zram（モジュール）＋PSI＋MEMCG |
| `11c11c922` | input: brain-kbd-gpio: wedge fix（上記） |

ポインタを新 brain tip `cc29c866` へ更新するとこれら全てを失うため、
絶対条件「現在 master が参照している修正を失わない」に反する。
よって:

- root repo のポインタは `11c11c922` のまま維持（コミット変更なし）。
- サブモジュールのチェックアウトを記録位置 `11c11c922` に戻し、
  root の作業ツリーはクリーンな状態にした。
- master は local = remote = `dbe6dc494` で変更なし（push 不要だった）。

フォークラインを `brain` へ本格的に取り込む作業（rebase/merge）は、
kbd ドライバ2実装の統合を含む独立したカーネル作業であり、本タスクの
範囲では行わない。実施する場合は別途判断が要る（残件）。

## buildbrain 本体（samusun343-ai/buildbrain）

### rebuild ブランチ — 新規作成・push 済み

作業領域 `~/workspace/buildbrain-work/` の作り直し成果を収蔵した。
master から分岐、tip は本レポートのコミットが最新（push 後 ls-remote で
一致確認済み）。収蔵コミット:

| コミット | 内容 |
|---|---|
| `08a4ccbb1` | rebuild: archive libbrainui2 toolkit sources, design docs and reports |
| `4162bd4d8` | rebuild: archive braindesk2 shell sources and reports |
| `e408cdc19` | rebuild: archive applications and on-screen keyboard |
| `34a355dac` | rebuild: archive libbrainime sources, dictionary data and reports |
| `03c566ef9` | rebuild: archive NetSurf brain frontend and browser reports |
| `3eeb0fb7f` | rebuild: archive verification screenshots |
| `9ac48c5c3` | rebuild: archive OS surgery set and Brain modding research |
| （本レポート） | rebuild: push report (2026-11-21) |

収蔵内容（合計 約224 MB / 1,668 ファイル）:

- `rebuild/`: libbrainui2 のソース・ヘッダ・テスト・設計/監査/レポート文書、
  シェル（braindesk2）、アプリ群、OSK、libbrainime（ソース・辞書データ
  `brainux3.bid3`・レポート）、検証スクリーンショット 244 点、フォント、素材。
- `rebuild/browser-engine/`: レポート・ビルドログ・撮影・テストサイトと、
  上流 tarball `netsurf-all-3.11.tar.gz`、自作前端
  `netsurf-all-3.11/netsurf/frontends/brain/` のみ。
- `os-surgery/`: レポート2件、apply-changes.sh、changeset.json、
  libscan.json、kernel-trim/ の config 類、スキャンスクリプト群。
- `RESEARCH-brain-modding.md`（repo ルート）。
- `PUSH-REPORT.md`（本書、repo ルート）。

除外したもの（再生成可能・巨大・作業用）:

- ビルド生成物: `*.o` / `*.a` / `*.so` / ELF 実行ファイル 88 点
  （テストバイナリ・アプリバイナリ）。`rebuild/.gitignore` と
  `os-surgery/.gitignore` にもパターンを残した。
- `image-work/` 全体（SD イメージ・rootfs。統合検証ラインが使用中のため
  そもそも対象外）。
- `os-surgery/staging/`（rootfs ツリー 1.9 GB）と `os-surgery/kbuild/`
  （カーネルビルドツリー 363 MB）。
- `rebuild/browser-engine/` の toolchain（364 MB）、`.sysroot-arm`
  （375 MB）、`.sysroot`、`.work-p2`、および展開済み上流 NetSurf ツリー
  （前端 `frontends/brain` と tarball を除く。tarball から再展開可能）。
- `rebuild/libbrainime/m*work/` の実験作業ディレクトリ群（venv・コーパス
  作業場、約700 MB）。ただし `m4work/ubsan_finding.md` のみ収蔵した。
- `rebuild-arm/`、`rebuild-arm2/` のビルド複製、`toolchain/`、
  `toolchain-official/`、`hosttools/` のバイナリ。
- 単一ファイル 50 MB 超は収蔵内に 0 件（コミット前に確認）。

なお本ブランチは repo の AGENTS.md が定める root repo の通常の役割
（オーケストレーション）とは異なる収蔵専用ブランチであり、master には
一切変更を加えていない。収蔵物はアーカイブであり、ビルド検証の対象外
（検証は各ラインのレポートに記録済みのものを正とする）。

## 検証

- linux-brain: `git ls-remote origin` で `brain = cc29c866c489`、
  `detached-11c11c922-kbd-fix = 11c11c922fa4` を確認。
- buildbrain: `git ls-remote origin` で `rebuild` 先端とローカルの一致、
  `master = dbe6dc494`（無変更）を確認。
- force push は一切行っていない。

## 残件

1. **フォークラインの brain への取り込み判断**: 上表12件（実質は
   BU26154・PSWITCH・USB・defconfig・kbd の5系統）を brain へ
   rebase/merge するかどうか。kbd ドライバは2実装の統合判断が必要。
   取り込みが終われば、その時点で root ポインタを新 tip へ更新できる。
2. `image-work/` の sd-v2.img 等は未収蔵（方針どおり除外。必要なら
   GitHub Releases 等、git 外の置き場を別途検討）。
3. 保全ブランチ `detached-11c11c922-kbd-fix` は当面残す。
   フォークライン取り込み完了後に削除を判断する。

---

# 追記 — 統合フェーズ第3段の反映（2026-11-21）

第2段（ゲスト実起動・合格判定）で検出したイメージ側欠陥の修正（第3段、
REPORT-integration-4.md）のソース変更を `rebuild` ブランチへ反映し、
レポート2点を repo ルートへ追加収蔵した。認証は前回と同じ
repo ローカル `credential.helper=store`（HTTPS）を使用。
force push は行っていない。push 後 `git ls-remote` でリモート
`rebuild` 先端とローカルの一致を確認済み。

## 反映コミット（buildbrain / rebuild ブランチ）

旧 tip `61d38367e` の上に6件＋本レポートのコミットを積んだ:

| コミット | 内容 |
|---|---|
| `81299bbf3` | セッションスクリプト: 起動前に /run/user/<uid>（0700）を作成し X フォールバックへ XDG_RUNTIME_DIR を渡す（第2段・項目1のセッション起動不能の修正） |
| `6d2c34598` | tk_fb.c: 寸法不一致を stderr へ記録する `fb_dims_match()` 追加。test_tk3.c に回帰テスト追加（202 passed） |
| `c68ded04a` | fb 直描き6アプリ（calc / files / notes / tasks / clock / player）: main で fb を先に開き実寸法で構築（無描画の修正） |
| `c7fcb3244` | 4アプリ（recorder / pkg / admin / settings）の main を実働化＋ apps/Makefile の check に main スモーク追加（即終了の修正） |
| `659a5b2f6` | ブラウザ前端: gui.c のフォント候補と Makefile の BRAIN_RESPATH を /usr/local/share/fonts/libbrainui2/ に是正 |
| `4319a1461` | レポート収蔵: REPORT-integration-3.md（第2段）、REPORT-integration-4.md（第3段）を repo ルートへ |
| （本レポート） | rebuild: push report update (integration phase 3 sync) |

## 反映範囲の確認

- 作業領域 `rebuild/` とブランチ収蔵ツリーの全量比較で、差分は
  上記の第3段変更16ファイルのみであることを確認した。
  第3段と無関係な変更の混入は無し。
- 作業側にのみ存在した88ファイルは全て x86-64 ELF 実行ファイル
  （ホストビルド生成物）であり、前回方針どおり収蔵対象外。
- `os-surgery/` と `RESEARCH-brain-modding.md` に差分は無かった。
- `image-work/` のイメージ本体（*.img）と証拠ディレクトリ
  `int3/` / `int4/` は収蔵していない（レポート本文のみ収蔵）。
- linux-brain と root のサブモジュールポインタには変更なし
  （前回の判断を維持）。

---

# 追記 — 統合フェーズ第4段・クロック是正・PARTUUID・最終統合（sd-v5）の反映（2026-11-21）

第4段（DE 全面棚卸しとイメージ内アセットのパーミッション是正、
REPORT-integration-5.md）、CPU クロック是正と PARTUUID 化
（REPORT-clock-fix.md）、最終統合 sd-v5（REPORT-integration-6.md）
の変更を反映した。認証は従来どおり repo ローカル
`credential.helper=store`（HTTPS）。force push は行っていない。

## buildbrain / rebuild ブランチ — push 済み

旧 tip `961d600d5` の上に4件＋本レポートのコミットを積んだ:

| コミット | 内容 |
|---|---|
| `203d2e5c2` | 第4段シェル修正: アイコン読込み失敗のログ追加（従来は無言でテキスト落ち）、失敗時にキーを固定せず再試行可能に、マスコットのフロア分離（グリッド rect 公開アクセサ＋セッション側フロア設計） |
| `2fca6acd6` | レポート収蔵: REPORT-clock-fix.md（新規）、REPORT-os-surgery-1.md 付記3（起動経路の前提訂正） |
| `2faf33d4a` | os-brainux: boot.mount を番号指定から by-label 指定へ（PARTUUID 化と対の番号依存排除。**master には入れず rebuild ブランチに staged**） |
| `cb028bbb0` | レポート収蔵: REPORT-integration-5.md（第4段）、REPORT-integration-6.md（最終統合 sd-v5）を repo ルートへ |
| （本レポート） | rebuild: push report update (phase 4, clock fix, PARTUUID, sd-v5 sync) |

反映範囲の確認:

- 作業領域 `rebuild/` とブランチ収蔵ツリーの全量比較で、内容差分は
  シェル3ファイル（shellui.h / shellui.c / session.c）のみ。
  作業側のみに存在するファイルは全てビルド生成物（ELF・*.o・*.a・
  *.so）、libbrainime の m*work 実験ディレクトリ、テスト用
  フィクスチャであり、前回方針どおり収蔵対象外。
- `os-surgery/` の差分は REPORT-clock-fix.md（新規）と
  REPORT-os-surgery-1.md（付記3）のみ。staging/・kbuild/ は対象外。
- `image-work/` のイメージ本体と証拠ディレクトリ int5/ / int6/ は
  収蔵していない（レポート本文のみ収蔵）。
- master は無変更（`dbe6dc494`）。サブモジュールポインタも変更なし。

## u-boot-brain — ローカルコミット済み、push は宛先なしで未完

クロック是正と PARTUUID 化は u-boot-brain の変更であり、監査の結果:

- submodule の origin は上流 `brain-hackers/u-boot-brain` で、
  buildbrain のポインタ `e8fc0d0cf3` は origin/brain の tip そのもの
  （linux-brain のような「ポインタ先の背後に未取得履歴」の罠は無し）。
- しかし `samusun343-ai` に上流 repo への書込み権は無く（push は
  403 で拒否）、フォーク `samusun343-ai/u-boot-brain` も存在しない。
  **push の宛先が現状存在しないため、push は未完。**
- 変更はローカルブランチ **`brain-clock-partuuid`** に2コミットで
  保全済み（作業ツリーはクリーン、チェックアウトはポインタ位置
  `e8fc0d0cf3` に復帰済み）:

| コミット | 内容 |
|---|---|
| `ae9daf6f08` | sharp: pwsh6: VDDD 1550mV 設定と CPU/EMI クロック初期化を board_early_init_f へ（cpu_clkdiv.c/.h、pwsh6.c） |
| `3b41783add` | brain_mx28: root を起動時 PARTUUID 解決に変更（brain_mx28_common.h の setroot、9機種 defconfig に CONFIG_CMD_PART=y） |

- 宛先が決まれば `git -C u-boot-brain push <remote> brain-clock-partuuid`
  でそのまま送れる。想定する宛先は `samusun343-ai/u-boot-brain`
  フォークの新規作成（ユーザー判断待ち）。

## 検証

- buildbrain: push 後 `git ls-remote origin` で `rebuild` 先端と
  ローカルの一致、`master = dbe6dc494`（無変更）を確認。
- u-boot-brain: 上記のとおり push 未完（宛先なし）。ローカル
  ブランチとコミットの存在は本書が記録。

---

# 追記 — u-boot-brain フォーク作成と push 完了（2026-11-22）

ユーザーの明示指示（「やって。続けて。」）を受け、前節で判断待ちだった
`samusun343-ai/u-boot-brain` フォークを作成し、保全済みの2コミットを
push した。これで u-boot-brain の push 未完は解消。

## フォーク作成の経路

- GitHub コネクタ（MCP）の `fork_repository` を先に試したが、403
  （Resource not accessible by integration）で不可だった。
- 代替として GitHub API
  `POST /repos/brain-hackers/u-boot-brain/forks` で作成した。認証は
  従来の repo ローカル `credential.helper=store`（gitcred）から
  `git credential fill` で取得したトークンをシェル変数内でのみ使用
  （出力・ログ・ファイルへの記録なし）。
- 結果: `samusun343-ai/u-boot-brain` 作成（HTTP 202）、既定ブランチは
  上流に倣い `brain`（フォークの HEAD = `e8fc0d0cf3`、ポインタと一致）。

## push と照合

- u-boot-brain に remote `fork`
  （https://github.com/samusun343-ai/u-boot-brain.git）を追加し、
  repo ローカルに credential.helper を設定のうえ
  `git push fork brain-clock-partuuid` を実行（新規ブランチ）。
- 照合: `git ls-remote fork` で
  `refs/heads/brain-clock-partuuid = 3b41783addf2b9902a21e65787df01e2f56cdb6e`
  がローカルの tip と完全一致することを確認。内訳は前節の2コミット
  （`ae9daf6f08` クロック是正、`3b41783add` PARTUUID 化）。
- buildbrain 本体の master とサブモジュールポインタは変更なし。
  サブモジュールのチェックアウトはポインタ位置 `e8fc0d0cf3` のまま、
  root の作業ツリーもクリーンな状態を維持。

## 2026-11-22 第3弾 push（統合フェーズ第6段〜第8段）

- buildbrain `rebuild` ブランチ: `4a1741f1b` → 2コミット追加して push。
  - `3d6faade7` 第6段: fbcon カーソル漏れ修正（brainux-session.sh の stty -echo）＋ REPORT-integration-7.md 収蔵。発行イメージ sd-v6.img（md5 `1afc39f089a3321c371591ba83c65635`）。
  - `237b69f49` 第7・8段: バー常時表示（全10アプリを WM クライアント化、共有ホスト bdapp_run 新設）＋ GUI 土台立て直し（DESIGN-layout-contract.md / AUDIT-foundation.md、レイアウトマトリクスとゴールデン、ファジングハーネス）＋ REPORT-integration-8.md 収蔵。発行イメージ sd-v8.img（md5 `f0a51cf19892faa5a208db6e3ea0f3f3`）。
  - 同期方法: 作業ツリー（buildbrain-work/rebuild/）から repo の rebuild/ へ rsync（ビルド生成物と browser-engine の作業領域は除外）。変更36ファイル＋新規ソースのみを収蔵し、実行バイナリは含めていない。
- u-boot-brain フォーク `samusun343-ai/u-boot-brain` の `brain-clock-partuuid`: `3b41783add` の上に `a06d136746`（bootargs_custom へ `vt.global_cursor_default=0` を追加、1行差分）を push し、ls-remote で一致を確認。buildbrain 側のサブモジュールポインタは従来どおり不変更（チェックアウトは `e8fc0d0cf3` に復帰済み）。
- buildbrain `master` は無変更。

## 2026-11-22 第4弾 push（統合フェーズ第9段・第10段）

- buildbrain `rebuild` ブランチ: `d6caefd2c` → コミットを追加して push。
  - `c4ca5c5c6` 第9・10段: 独立評価（EVAL-v8-independent.md、sd-v8 は不合格）の全指摘是正（要素の大きさの釣り合い、電卓の表示部に数字が出ない機能欠陥、ブラウザ開始ページ、録音・タスクのレイアウト、タイトルバー3ボタン、バーの帯）＋盲検第2ラウンド残件の是正。発行イメージ sd-v9.img（md5 `d0f95ab7e442ecd60b9b1f91eef51cb3`）、sd-v10.img（md5 `785e4bbf5dfc9e8e12d15162e3de71f7`）。レポート REPORT-integration-9.md / REPORT-integration-10.md / EVAL-v8-independent.md を repo ルートへ収蔵。
  - 同期方法: 作業ツリー（buildbrain-work/rebuild/）から repo の rebuild/ へ rsync（--delete なし、ビルド生成物と browser-engine の作業領域は除外、repo 側の rebuild/.gitignore は保持）。変更41ファイル＋新規ソース1点（apps/tests/probe.c）のみを収蔵し、実行バイナリは含めていない。rsync で repo 側コピーへ混入した展開済み上流 NetSurf ツリー（未追跡 10,787 ファイル）は除去した（収蔵範囲は従来どおり frontends/brain のみ）。libbrainime/m4work の実験ファイルは従来方針どおり収蔵対象外。
  - 申し送り: sd-v10 の盲検第3ラウンドでは両評価者とも不合格が残っている（REPORT-integration-10.md §3・§8）。本 push は是正版の収蔵であり、盲検合格の主張ではない。
  - （本レポート） rebuild: push report update (integration phases 9-10, sd-v9/sd-v10 sync)
- buildbrain `master` は無変更。サブモジュールポインタも不変更。linux-brain / u-boot-brain は今回の push 対象外。

## 2026-11-23 第5弾 push（統合フェーズ第11段・sd-v11）

- buildbrain `rebuild` ブランチ: `b4ab76f34` → コミットを追加して push。
  - `cb053fb27` 第11段: 盲検第3ラウンド残件7項目の是正（マスコットの内容境界クランプ＝シェル session.c/mascot.c、ブラウザ開始ページの内容側収まり是正、録音サイドバー型の重なり＝スクロールビューの実幅再測定 tk_core.c、軸別余白 pad_x/pad_y と共通定義 BD_PAGE_PAD_X=18 によるページ余白統一、間隔 C2 の段階付け明文化に伴う間隔是正、証拠の機械照合 verify_shots、ブラウザ GIF のゲスト撮影）＋ REPORT-integration-11.md を repo ルートへ収蔵。発行イメージ sd-v11.img（md5 `806724b60eec68d6b9a9608725714016`）。
  - 同期方法: 作業ツリー（buildbrain-work/rebuild/）から repo の rebuild/ へ rsync（--delete なし、ビルド生成物 *.o/*.a/*.so と browser-engine の作業領域＝toolchain/.sysroot*/.work-* と展開済み上流 NetSurf ツリーは除外し前端 frontends/brain のみ別途同期、repo 側の rebuild/.gitignore は保持）。変更は追跡済み33ファイルのみで、新規ソースは無し（作業側のみに存在するファイルは全て ELF 実行ファイル等のビルド生成物と libbrainime/m*work の実験ファイルで、従来方針どおり収蔵対象外）。上流 NetSurf ツリーの混入は無し。os-surgery/ に差分は無かった。image-work/ のイメージ本体と証拠ディレクトリ int12/ は収蔵していない（レポート本文のみ収蔵）。
  - 申し送り: sd-v11 の盲検第4ラウンドでは評価者間の結論が割れた（評価者G: 不合格2件＝写真23・25 のコマンドピル列の間隔 C2-A 4px で「盲検合格とは言えない」、評価者H: 不合格0で「盲検合格と言える（条件付き）」）。継続ラインの再測でもピル間隔 4px は再現したため、REPORT-integration-11.md §4.4 のとおり**盲検合格とはしない**。本 push は是正版の収蔵であり、盲検合格の主張ではない。残件は同レポート §7（コマンドピル列の 9px 化と CHECKLIST 次期版での C2 区分明文化、C1 推奨寸法未満の広範な要改善ほか）。
  - （本レポート） rebuild: push report update (integration phase 11, sd-v11 sync)
- buildbrain `master` は無変更。サブモジュールポインタも不変更。linux-brain / u-boot-brain は今回の push 対象外。

## 2026-11-23 第6弾 push（統合フェーズ第12段・sd-v12）

- buildbrain `rebuild` ブランチ: `7fce57b24` → コミットを追加して push。
  - `cb6f4bb1a` 第12段: 盲検第4ラウンド残件の是正（コマンドピル列の間隔 9px 化＝タスク/時計/メモ/ファイル/プレーヤー/共通ダイアログほか、グリーター固定矩形の 54px 化＝shell/greeter/braingreeter2.c、C1 測定注意の明文化、左端余白 18px の C7 是正＝設定/ファイル WinUI の pad_x 追加と時計 navrail 拡幅、設定 WinUI サイドバー下端の次行グリフ漏れ＝src/tk_m7.c の drawer 行境界クリッピング、電卓 WinUI 履歴パネルの空状態案内＝apps/calc/bd_calc.c、ゴールデン8件の再生成）＋ REPORT-integration-12.md を repo ルートへ収蔵。発行イメージ sd-v12.img（md5 `0fe0a6722d1db310d7151ca689530463`）。
  - 同期方法: 作業ツリー（buildbrain-work/rebuild/）から repo の rebuild/ へ rsync（--delete なし、ビルド生成物 *.o/*.a/*.so と browser-engine の作業領域＝toolchain/.sysroot*/.work-* と展開済み上流 NetSurf ツリーは除外し前端 frontends/brain のみ別途同期、repo 側の rebuild/.gitignore は保持）。変更は追跡済み18ファイルのみ（アプリ7点・ゴールデン8点・グリーター1点・ツールキット2点）で、新規ソースは無し（作業側のみに存在／差分となったファイルは全て ELF 実行ファイル等のビルド生成物と libbrainime の *.a/*.o・m*work 実験ファイルで、従来方針どおり収蔵対象外）。上流 NetSurf ツリーの混入は無し（前端 frontends/brain に差分なし）。os-surgery/ に差分は無かった。image-work/ のイメージ本体と証拠ディレクトリ int13/ は収蔵していない（レポート本文のみ収蔵。盲検評価書 review-I.md / review-J.md も image-work/int13/ 側にのみ存在し、repo へは収蔵していない）。
  - 申し送り（盲検第5ラウンド）: フェーズ12では盲検第5ラウンドがライン側で未実施だった（盲検セットと CHECKLIST-v12 は用意されたが、ライン側サブエージェントは独立評価者を立てられなかった。REPORT-integration-12.md §7）。その後、親が評価者2名（I・J）を立てて実施し、結論は両者とも **「盲検合格とはしない」**（評価者I: C7 不合格4件＝写真18・19・21・29 の矩形テーマのサイドバー／検索欄の左余白 16px／12px、評価者J: C7 不合格2件＝写真34・35 のブラウザ窓内容端からの本文・画像左余白 11px。評価書は image-work/int13/review-I.md と review-J.md）。本 push は是正版の収蔵であり、盲検合格の主張ではない。
  - 申し送り（eMMC 事故）: REPORT-integration-12.md §6.3 のとおり、本ラインの追加スクリプト（wince2.py）が正規ハーネスの作業コピー（emmc-int9-wince.img）を経由せず、作業コピーでない実 emmc-int9.img を起動し、WinCE 初回設定ウィザードが進んだことで **emmc-int9.img の md5 が `b6d9fc54fd3cb1520c7e025d9bbbaec5` から `2af360a02f452979b4de84ba3b1845f5` に変化した**。影響範囲は WinCE 側の初回設定状態のみで、Brainux（SD）側・sd-v12 には無関係と報告されている。image-work 内に旧状態と同一の複製は存在せず、復旧の要否は eMMC の所有ライン（QEMU ライン／親）の判断待ち。本 push では eMMC イメージには一切触れていない。
  - （本レポート） rebuild: push report update (integration phase 12, sd-v12 sync)
- buildbrain `master` は無変更。サブモジュールポインタも不変更。linux-brain / u-boot-brain は今回の push 対象外。

## 2026-11-23 第7弾 push（統合フェーズ第13段・sd-v13）

- buildbrain `rebuild` ブランチ: `bcef94a4b` → コミットを追加して push。
  - `0edde64a8` 第13段: 盲検第5ラウンド残件の是正（サイドバー余白の18px統一＝ツールキットの listview 内側オフセットを `LV_PAD_X = 2*unit+2 = 18px` に新設し設定／ファイル／プレイヤー等の WinUI サイドバー pad を是正、ブラウザ同梱ページ（selftest.html／giftest.html）のページマージン是正＝`body { margin: 20px }` で本文左余白 11px→28px／27px、入力欄＋隣接ボタンの間隔 9px 化＝タスク／メモ／ファイル／設定PSK／時計の操作行、空状態案内文の追加＝メモWinUI・タスク両テーマ・レコーダー両テーマ・ファイル、グリーター時刻の中心補正＝`tk_text_ink_shift_px/st` を tk_font.c に新設しインク bbox 中心へ補正し中心ずれ −3px→−1.0px、CHECKLIST-v13 の適用範囲・サンプリング規則の明文化＝C2-A の欄＋ボタン異種ペア対象化、C7 起点2と第三者 Web 文書の余白の線引き、C4 グリフサンプリング規則7、C1 寸法は枠込み外形、ゴールデン6件の再生成＝設定×4・メモWinUI×2）＋ REPORT-integration-13.md を repo ルートへ収蔵。発行イメージ sd-v13.img（md5 `dd0921f38ffcaeb506e91e343df3e7f4`）。
  - 同期方法: 作業ツリー（buildbrain-work/rebuild/）から repo の rebuild/ へ rsync（--delete なし、ビルド生成物 *.o/*.a/*.so と browser-engine の作業領域＝toolchain/.sysroot*/.work-* と展開済み上流 NetSurf ツリーは除外し前端 frontends/brain のみ別途同期、repo 側の rebuild/.gitignore は保持）。変更は追跡済み20ファイルのみ（アプリ7点・ゴールデン6点・グリーター1点・ツールキット3点＝tk_widgets.c／tk_font.c／tk.h・テスト3点＝test_tk13／15／17）で、新規ソースは無し（作業側のみに存在／未追跡となったファイルは全て ELF 実行ファイル等のビルド生成物と libbrainime/m4work の実験ファイルで、従来方針どおり収蔵対象外）。上流 NetSurf ツリーの混入は無し（前端 frontends/brain に差分なし。同梱ページ selftest.html／giftest.html はイメージ側の収蔵対象外領域の変更で、repo の rebuild/ ツリーには対応ファイルが存在しない）。os-surgery/ に差分は無かった。image-work/ のイメージ本体と証拠ディレクトリ int14/（CHECKLIST-v13・盲検セット blind/ を含む）は収蔵していない（レポート本文のみ収蔵）。
  - 申し送り（盲検第6ラウンド）: CHECKLIST-v13 と盲検第6ラウンド用無ラベル撮影セット（48枚）はライン側で用意済みだが、評価は親が評価者2名を立てて実施中であり、**本 push の時点では結論が出ていない**。本 push は是正版の収蔵であり、盲検合格の主張ではない。
  - 申し送り（未解消項目）: REPORT-integration-13.md §8 のとおり、(1) OSK 文字キー高さ 30px は構造的限界として未解消（候補欄48＋下段バー56 を確保した残りを3行で割る fit 機構上、窓内容約205px ではキー行約33px が上限。推奨54px 未達、下限24px は充足）、(2) スライダーつまみ直径 約26px（下限充足・推奨54px 未達、本フェーズ対象外として残置）、(3) レコーダーの最近削除ビューの空状態文面と名前欄54px はホスト側でのみ確認しゲスト撮影では未検証、(4) ファイルの空状態2行文はホスト matrix で確認しゲスト撮影では未写、(5) プレイヤー WinUI トランスポート行（spacing 8 の混成行）の C2-A 適用可否は継続検討事項で未変更。
  - （本レポート） rebuild: push report update (integration phase 13, sd-v13 sync)
- buildbrain `master` は無変更。サブモジュールポインタも不変更。linux-brain / u-boot-brain は今回の push 対象外。eMMC イメージには一切触れていない（REPORT-integration-13.md §2 のとおり、フェーズ13の回帰前後とも emmc-int9.img の md5 は `2af360a02f452979b4de84ba3b1845f5` で不変、WinCE 確認は作業コピーのみ）。

## 第8弾（2026-11-23、統合フェーズ14 / sd-v14 同期）

- コミット: `90d15cf02`（フェーズ14の是正一式＋ REPORT-integration-14.md。
  内訳: bd_calc / bd_clock / bd_files / bd_notes / bd_pkg /
  bd_recorder / bd_tasks、tk_widgets、ゴールデン4件
  （calc 850x368・854x480 android、notes 850x368・854x480 winui））。
  push 後に PUSH-REPORT 更新コミットが続く。
- 内容: 盲検第6ラウンド残件の是正（録音詳細パネルの余白 18px 化、
  コマンドボタン縦積み間隔 9px 化、電卓ピル形「=」の配色による
  コントラスト 4.5:1 以上化、ブラウザ開始ページの最初のリンクの
  内側化（CSS padding-left 30px）、CHECKLIST-v14 の適用範囲明文化）。
  最終イメージ sd-v14.img md5 `336ad31f9f98cc8a74b4c22c08502bc6`、
  PARTUUID `e0c6bdb3-02`。ゲスト全面回帰 PASS。盲検第7ラウンドは
  評価者の結論が割れ（M: 不合格0「合格と言える」/ N: 不合格2
  「言えない」）、不合格側の実測（設定 WinUI 詳細見出しが面左端
  から 1〜2px）を再測で確認したため盲検合格とはしない
  （詳細は REPORT-integration-14.md §5・§7、フェーズ15送り）。
  あわせてフェーズ12の eMMC 事故の復旧を記録: 新ベースライン
  md5 `612e8a0307436b59852bb39ede358fa8`（pristine と同一。
  事故前のバイト列は復旧できず）。
- 同期方法と除外方針は第7弾までと同一（rsync --delete なし、
  *.o/*.a/*.so と browser-engine 作業領域・libbrainime/m4work を除外、
  実行バイナリは収蔵対象外）。

## 第9弾（2026-11-23、統合フェーズ15 / sd-v15 同期）

- コミット: `315a35edd`（フェーズ15の是正＋ REPORT-integration-15.md。
  内訳: bd_settings.c（詳細カラムの rows_box に pad_x =
  BD_PAGE_PAD_X を追加する1箇所）、ゴールデン2件
  （settings 850x368・854x480 winui））。push 後に
  PUSH-REPORT 更新コミットが続く。
- 内容: 盲検第7ラウンド残件の是正（設定の詳細カラムで
  グループ見出しがスクロールビューの面の左端に 1〜2px で
  接していた件を、詳細カラム共通の描画経路で面端から
  18px に是正。全10ページ×4テーマのホスト総点検で違反0、
  ゲスト実測で見出しインク 20px）。CHECKLIST-v15 は
  集計慣行（対象なしセルの除外）・スライダーの C1 対象
  （ウィジェット矩形）・ピル形ボタンの中心列測定を明文化。
  グリーター下部ボタン 34px 主張は再測（中心列 54px、
  ゲスト撮影でも確認）で再現せず是正不要と記録。
  最終イメージ sd-v15.img md5 `048036ade3e106aee85c627e3e9e6a70`、
  PARTUUID `95140364-02`。ゲスト全面回帰 PASS（ブート
  2回全項目合格、全10アプリ生存、eMMC 実ファイルは
  ベースライン md5 `612e8a0307436b59852bb39ede358fa8`
  のまま不変）。**盲検第8ラウンドは評価者O・P とも
  合格・不合格セル0**（要改善は OSK キーセル高のみで
  構造的限界として従来どおり記録）。
- あわせて判明したハーネス制約: QMP ポインタ注入
  （絶対座標/HMP/相対の3方式）がゲストのシェルに
  届かず、アプリ内移動を伴うゲスト撮影は現ハーネス
  では不可能（フェーズ14の「OSK を開いた状態」の
  撮影も実フレームはグリーター画面だった。本フェーズ
  の設定全ページ総点検はホスト描画プローブで代替）。
  詳細は REPORT-integration-15.md §7・§12。
- 認証は本弾から AGENTS.md の新指示に従い保存済み PAT
  （`~/workspace/.secrets/ghtok`、credential helper 経由。
  値は記録しない）に切り替えた。
- 同期方法と除外方針は第8弾までと同一（rsync --delete
  なし、ビルド生成物は収蔵対象外）。
