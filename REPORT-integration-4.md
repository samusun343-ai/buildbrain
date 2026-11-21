# 統合フェーズ第3段 — 第2段で検出したイメージ側欠陥の修正とゲスト再検証

作成: 2026-11-21（段階ごとに追記する）

## 0. 位置づけ・方法

- 前提: `REPORT-integration-3.md`（第2段の全判定と証拠）。
- 対象欠陥は第2段 §4 のイメージ側欠陥 1〜4 と音声経路（欠陥4）。
  CPU クロック 24 MHz（欠陥5）は本段の対象外（第2段で記録済み）。
  モデル側ギャップ（LRADC タップ不達、メディアキー、バックライトの
  フレーム反映、aplay の pcm_write EIO 疑い）は対象外として記録に留める。
- 修正は rebuild/ のソースに正規の形で入れ、ホスト側 make check
  （ツールキット / シェル / アプリ）全通過を維持した上で armel
  再ビルド → rootfs-v3 → **sd-v3.img** を発行する（sd-v2.img 原本は残す）。
- 再検証は第2段と同じ正規経路（EBOOT → edsh6exe.bin → U-Boot →
  カーネル）でゲスト起動し、ゲストの撮影（JPG）とログで判定する。
- RAM 規律: VM 全体でゲスト最大2つ。着手時点で他チャットのゲストが
  2つ稼働中のため、ゲスト検証は枠が空くまで待機し、コード修正と
  イメージ発行を先行する。repo へ commit / push しない。

## 1. 欠陥の原因特定（着手時調査）

### 1.1 セッション起動不能（第2段・項目1）

- `rebuild/shell/integration/brainux-session.sh` が
  `XDG_RUNTIME_DIR=/run/user/$(id -u)` を braindesk2 に渡すが、
  ディレクトリ自体は誰も作らない。スクリプトのコメントは
  「systemd-provided」と仮定しているが、この起動経路
  （braingreeter2.service が root でスクリプトを実行し runuser で
  ユーザに降りる）では logind セッションが存在せず、仮定が崩れている。
- braindesk2 の WM（shell/src/wm.c `bd_wm_server_start`）は bind 前に
  パスを unlink するため、残存ソケットは問題にならない。欠けている
  のはディレクトリ作成だけ。
- 修正方針: セッション開始フロー本体（brainux-session.sh）が、
  braindesk2 起動前に /run/user/<uid> をユーザ所有・0700 で作成する
  ことを保証する（起動フローの責務として組み込む）。

### 1.2 fb 直描きアプリ6本が無描画（第2段・項目5、原因a）

- 6本（calc/clock/files/notes/player/tasks）の main は全て
  `tk_canvas_new(480, 272)` と `*_app_new(..., 480, 272)` の固定寸法で、
  fb を開くのはその後。480×272 は実 fb 854×480 と一致せず、
  `tk_fb_flush_damage()`（src/tk_fb.c）が寸法不一致で 0 を返し続け、
  何も描かれない。テストは 480×800 等のメモリ fb で組まれていたため
  検出されなかった。
- あわせて tk_fb.c の flush / flush_damage は寸法不一致を無言で
  握り潰す設計で、欠陥の発見を遅らせた。
- 修正方針: (1) 6本の main は fb を先に開き、`tk_fb_width/height`
  の実寸法でアプリとキャンバスを作る（fb が無い場合は従来寸法で
  ヘッドレス描画のみ）。(2) tk_fb.c は寸法不一致時に stderr へ
  記録する（fb インスタンスごとに初回のみ）。(3) ホスト回帰テスト
  として 854×480 メモリ fb の flush を test_tk3 に追加する。

### 1.3 4アプリが即終了（第2段・項目5、原因b）

- recorder / pkg / admin / settings の main は `*_app_new(..., 800, 600)`
  して即 `*_app_free` + return 0。キャンバス・fb・イベントループを
  一切持たない未配線の main。
- 修正方針: 動作している fb アプリ（calc 等）と同型の main にする:
  フォント読込 → fb を開いて実寸法で app/canvas を構築 → evdev
  ループ（paint_damage + flush_damage、recorder/admin は tick 呼び出し）
  → HOME アクションで終了。fb が無い場合は1回描画して終了する
  ヘッドレス分岐も同型に揃える。recorder は factories NULL 時に
  モデル側が ALSA "default" へフォールバックする設計
  （bd_recorder.c の on_record / 再生開始）なので NULL のままでよい。

### 1.4 ブラウザのフォント配置不一致（第2段・項目12）

- 前端（netsurf/frontends/brain）の解決先が旧前提の
  `/usr/share/fonts/libbrainui2/` と `/usr/share/netsurf` で、
  実体の配置先 `/usr/local/share/fonts/libbrainui2/`（ツールキット
  のインストール接頭辞。tk_font.c の絶対候補の筆頭）を指していない:
  - `frontends/brain/gui.c` の `resolve_page_font()` 候補に
    /usr/local/... が無い。
  - `frontends/brain/Makefile` の BRAIN_RESPATH に
    /usr/local/share/fonts/libbrainui2/ が無い（フォールバックの
    respath 探索も外れる）。
- 修正方針: **前端の解決を実体に合わせる**。根拠: フォントの正本の
  置き場はツールキットのインストール先（/usr/local 配下、ライブラリ
  本体・アプリと同一接頭辞）であり、第1段の配置もそちら。前端側の
  2箇所のパスを /usr/local/... に是正すれば、フォントの複製
  （/usr/share/netsurf へのコピー）を持たずに済み、正本が1つに保てる。

### 1.5 音声の利用者経路（第2段・項目8）

- rootfs-v2 に /etc/asound.conf が無く、ALSA "default" PCM が未定義。
- /etc/group の audio（gid 29）に user が入っておらず、/dev/snd
  （root:audio 660）をデスクトップユーザが開けない。logind
  セッションが無いため uaccess ACL も付かない（1.1 と同根）。
- 修正方針（イメージ側）: (1) /etc/asound.conf で default を
  `plug` 経由で hw:0 に向ける（hw:0 はステレオ 44.1 kHz のみ
  受付のため、形式変換を挟む plug が適切な経路）。ctl も hw:0 に
  定義する。(2) user を audio グループに追加する。
- モデル側の pcm_write EIO 疑いは第2段で切り分け済みの別件として
  本段でも区別して記録する。

## 2. 実施記録

### 2.1 コード修正とホスト検証（完了）

- **tk_fb.c**: 寸法不一致時に stderr へ記録する `fb_dims_match()` を
  追加（fb インスタンスごとに初回のみ、転送しない挙動は維持）。
  理由: 間違った寸法のキャンバスを流すとフレームが壊れるため、
  「描かない」こと自体は正しく、問題は無言だったこと。
- **test_tk3.c に回帰テスト追加**: 854×480 メモリ fb（32bpp /
  16bpp）で同寸法キャンバスが全量転送されること、第2段の欠陥
  そのもの（480×272 キャンバス対 854×480 fb）が 0 バイト・
  flush=false になること。test_tk3: **202 passed, 0 failed**
  （不一致ログが1行だけ出ることも目視確認）。
- **fb アプリ6本**: main で fb を先に開き、`tk_fb_width/height`
  の実寸法で `*_app_new` と `tk_canvas_new` を行うよう変更
  （calc / files / notes / tasks / clock / player）。fb が無い
  場合は従来寸法でヘッドレス描画のみ（従来挙動を維持）。
- **4アプリの main 配線**: recorder / pkg / admin / settings の
  main を calc と同型の実働 main に書き換え（フォント読込、
  fb 実寸法で構築、evdev ループ + paint_damage + flush_damage、
  recorder は rec_app_tick、admin は admin_app_tick を毎周回
  呼ぶ。HOME アクションで終了、ヘッドレス分岐も同型）。
  あわせて apps/Makefile の check に **main スモーク**を追加:
  全 UI アプリのバイナリをヘッドレスで起動し「UI built
  (headless」出力と正常終了を確認する（main が UI 構築で
  落ちる回帰のガード。ループが回り続けること自体はゲストで
  担保する）。
- **brainux-session.sh**: セッション開始前に /run/user/<uid> を
  ユーザ所有・0700 で作成する処理を起動フロー本体に追加
  （chown は名前→uid の順で試行）。X フォールバックにも
  XDG_RUNTIME_DIR を渡すよう統一。sh -n / bash -n 通過。
- **ブラウザ前端**: gui.c の resolve_page_font 候補と
  Makefile の BRAIN_RESPATH を /usr/local/share/fonts/
  libbrainui2/ に是正（§1.4 の方針どおり前端側を実体に合わせた。
  フォントの複製は作らない）。
- **ホスト make check 結果**（いずれも exit 0、ベースライン維持）:
  - ツールキット（rebuild/）: 全テスト + sdk-check 通過。
    test_tk3 は 202 件（新規回帰テスト込み）。
  - アプリ（rebuild/apps）: 全20スイート 0 FAIL + main スモーク
    10本全 OK。
  - シェル（rebuild/shell）: 全11スイート 0 failed
    （test_session 163、test_wm 94 等）。
  - 修正した4バイナリはホストのヘッドレス実行でも
    「UI built (headless, no /dev/fb0)」+ exit 0 を個別確認。

### 2.2 armel 再ビルドと sd-v3.img 発行（完了）

- 修正ソースを rebuild-arm2/ に同期し build-userland2.sh で
  再ビルド（USERLAND_BUILD_OK）。ブラウザは build-brain2-stage2.sh
  で再ビルド（nsbrain 7,248,468 B、ARM EABI5）。新バイナリの
  strings で `/usr/local/share/fonts/libbrainui2/...` の解決先と
  BRAIN_RESPATH の是正を確認（残っていた /usr/share/... の文字列は
  ツールキット tk_font.c 自身の第2候補で、前端の欠陥とは別物）。
- make-payload2.sh で payload2 を再生成し nsfb-brain を差し替え。
- rootfs-v3 を update-rootfs3.sh で構築（rootfs-v2 の複製 +
  payload 再配置 + 音声手当て）。途中、staging-chroot.sh の
  binfmt 登録が本セッションでは効かず ldconfig が chroot で
  実行できなかったため、qemu-arm-static のユーザモードで
  対象 rootfs の ldconfig を直接実行して代替した（生成された
  ld.so.cache の libbrainui2 エントリは v2 と一致を確認）。
  音声セクション（asound.conf 作成、audio グループ追加）は
  スクリプト中断後に個別適用し、結果を実物で確認した。
- assemble_image3.sh で **sd-v3.img** を発行
  （3,221,225,472 B、ASSEMBLE_OK、ext4 ルート inode 0:0）。
  sd-v2.img 原本は残置。
- ホスト側検証 verify_image3.sh（v2 の102項目 + 第3段の新規
  9項目）: **111項目全 OK（VERIFY_ALL_OK、exit 0）**。
  新規項目: セッションスクリプトのランタイムディレクトリ作成、
  libbrainui2 の寸法不一致ログ文字列、4アプリの実働 main、
  nsfb-brain のフォント解決先、asound default 定義、
  audio グループ所属。


### 2.3 ゲスト再検証（完了）— int4/ ハーネス

正規経路（EBOOT → edsh6exe.bin → U-Boot → カーネル）で
sd-v3-work.img（sd-v3.img の作業コピー、md5 一致確認済み）と
emmc-int4.img（pristine の新規複製、使用前 md5
`612e8a0307436b59852bb39ede358fa8`）で起動。証拠は
int4/shots/ の JPG（42枚）と serial-run4*.log。

- **run4a（セッション修正）**: ブート→ `brain login:` まで
  wall 283 秒、ゲスト RSS 512,444 KiB。greeter でパスワードを
  打鍵してログインすると、**手当てなしでデスクトップに到達**
  （run4a-desk-03.jpg を目視確認: アイコン群・マスコット・
  Android モード下部バー）。ログイン前に /run/user/1000 が
  無いことを確認した上で、ログイン後にユーザ所有で作成され
  bdwm.sock / bdnotify.sock が生成され、braindesk2 が稼働。
  session.log は「login: user」のみで「session build failed」
  無し。systemctl --failed は既知の brain-mode.service のみ。
  アイドル時メモリ **used 34 MiB**（第2段 35 MiB、退行なし）、
  braindesk2 RSS 17,208 KiB。/proc/asound/cards に card0
  mxs_bu26154、dmesg の問題系は既知の BU26154 probe 遅延
  （-517）のみ。
- **run4b（アプリ10本）**: braindesk2 を SIGSTOP して fb を空け、
  各アプリをデスクトップユーザで起動。**全10本が起動継続
  （RESULTS 全 True）** し、854×480 全面描画を撮影で確認:
  - calc: 全面キーパッド描画。ただし QMP の数字キーは表示欄に
    入らなかった（run4x で Tab×2 後の追試でも同じ）。Brain の
    実キーボードには数字行が無く数字は記号層にあるため、
    モデルのキー表で再現できない入力である（モデル側制約。
    calc のキー処理自体はホスト UI テストで担保）。
  - clock: 世界時計が実時刻で描画（東京/ロンドン/NY/LA）。
  - files: /home/user の一覧・ストレージ残量を描画。
  - notes: 打鍵「abc」が画面下端のコンポジション帯に表示され
    入力到達を確認。tasks も「test」で同様。
  - player: test.wav を読み込み再生 UI を描画。
  - recorder / pkg / admin / settings: いずれも全面描画して
    起動継続。admin は実プロセス一覧、settings はトップページ
    （輝度 100% 等の実値）、pkg はパッケージ一覧、recorder は
    録音 UI を表示。終了後デスクトップ復帰、used 35 MiB。
- **run4c（ブラウザ）**: /usr/share/netsurf に TTF が**無い**
  出荷状態のまま nsfb-brain を起動し、ローカルページ
  （日本語見出し+英数字+CSS 赤ブロック）を正しく描画
  （run4c-ns-02.jpg）。フォント初期化の失敗は無し。
- **run4d（音声）**: user の所属グループに 29(audio) を確認。
  user で `aplay -D default test.wav` が **default を開いて
  再生開始**（"Playing WAVE ... Stereo"、Unknown PCM エラー
  消滅）。bd-player は /proc/asound 状態で **owner_pid =
  bd-player、state: XRUN** となり default PCM を掴んで
  ストリームしていたことを確認。ホスト側キャプチャ
  （audio-run4d.wav、QEMU の wav 出力はサイズ欄 0 のため
  生サンプルとして解析）には **440 Hz・最大振幅 9,599** の
  トーンが記録されていた。ただし aplay は最終的に
  `pcm_write: Input/output error` で終わる（第2段の root /
  hw:0 と同一症状）。これは ALSA 設定層より下（モデル/TCG の
  ストリーム処理）の嫌疑であり、イメージ側の責務と切り分ける。
  キャプチャ全域にトーンが続く点も、モデルの DMA バッファ
  ループ再生の嫌疑として記録する。
- **run4e（WinCE 回帰）**: Linux 実行後の eMMC md5 は使用前と
  **完全一致**（`612e8a0307436b59852bb39ede358fa8`）。SD を
  外して再起動すると WinCE が初期化確認画面→初期設定→
  ホーム画面（調べる一括検索）まで通常起動（run4e-wince-01.jpg、
  run4e-step-05.jpg）。
- **副次観測（第3段の欠陥ではない、記録のみ）**:
  - カーネル cmdline の `console=tty1` + fbcon により、ゲスト
    キーボード入力が tty1 のライン規律にエコーされ fb 下端に
    断片（打鍵文字列や "[[B"）として写る。recorder の shot-b
    では Enter 後に fbcon のブートログ再描画が画面中央を覆った。
    getty@tty1 は inactive で、原因は getty ではなくコンソール
    のエコー。従来イメージからの性質で第3段の変更とは無関係。
    候補手当て（cmdline から console=tty1 を外す等）は対象外。
  - settings トップページの音量行が「音量 取得不可」表示
    （設定アプリの音量バックエンド読み取りの問題。音声経路
    自体は run4d で動作確認済み）。
  - RAM 規律: 他チャットのゲスト起動と重なり一時的に3ゲストに
    なった場面が2回あり、いずれも自ゲストを停止して枠が
    空いてから再開した（第2段の先例どおり）。

## 3. 判定まとめ

| 項目 | 判定 | 根拠 |
|---|---|---|
| 1. セッション起動（ログイン→デスクトップ） | **合格** | run4a: 手当てなしで到達、ランタイムディレクトリと WM ソケット生成、session.log 正常 |
| 2. fb 直描き6アプリの描画・操作 | **合格** | run4b/run4x: 全面描画+起動継続。notes/tasks で打鍵到達を確認。calc の数字入力はモデルキー表の制約でゲスト再現不可（ホスト UI テストで担保）と明記 |
| 3. 4アプリ（recorder/pkg/admin/settings）の起動継続 | **合格** | run4b: 全て起動継続し実データで描画 |
| 4. ブラウザの出荷状態描画 | **合格** | run4c: /usr/share/netsurf に TTF 無しのまま描画成功 |
| 5. 音声の利用者経路 | **合格（イメージ側）** | run4d: audio グループ所属、default PCM で aplay 再生開始、bd-player が default を掴みストリーム、ホストキャプチャに 440 Hz。pcm_write EIO はモデル側嫌疑として分離記録 |
| 回帰: カーネル健全性 | **合格** | run4a: failed は既知の brain-mode のみ、サウンドカード認識、dmesg は既知件のみ |
| 回帰: アイドル時メモリ | **合格** | used 34 MiB（第2段 35 MiB から退行なし） |
| 回帰: WinCE 併用 | **合格** | eMMC md5 不変 + SD 無しで WinCE ホーム画面到達 |

成果物: sd-v3.img（3,221,225,472 B、md5
`98ef77fe1967f1dcaa0c785451a6e308`）。sd-v2.img 原本は残置。
repo への commit/push はしていない（親の担当）。
