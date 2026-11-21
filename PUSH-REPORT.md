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
