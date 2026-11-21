# OS 内部フェーズ 第2弾 レポート

日付: 2026-11-21
範囲: 実装3件（通知 IPC / アラーム常駐評価 / マスコット設定の通し確認）
＋測定1件（ライブラリ統廃合の測定パス）。commit / push はしていない。
前提: REPORT-os-internals-1.md §3（設計案）・§4（棚卸しと測定計画）。

ベースライン（着手前に再実行して確認）: ツールキット 3,577 件、
シェル 588 件 + test_mascot 95 checks、アプリ 1,083 件、OSK 319 件。
全て 0 failed。

## 進捗ログ

- 2026-11-21 着手。ベースライン再実行で全スイート一致を確認。
- 2026-11-21 §4（統廃合の測定）を完了し本レポートに記録（削除・置換は
  行っていない）。続いて実装1（通知 IPC）に着手。
- 2026-11-21 実装1〜3を完了（§1〜§3）。全体回帰を再実行し、
  全スイート 0 failed で確定（下記）。

## 最終回帰（全変更後に再実行）

| スイート | ベースライン | 本弾後 | 差分 |
|---|---|---|---|
| ツールキット（make check） | 3,577 | 3,577（+ SDK-OK） | 0 |
| シェル（make check） | 588 + test_mascot 95 | **655** + test_mascot 95 | test_session +21（§3）、test_notify +46（§1） |
| アプリ（make check） | 1,083 | **1,176** | test_alarmd +93（§2）。test_clock 71 / test_clock_ui 43 は無変更で通過（clockmodel 追加の影響なし） |
| OSK（make check） | 319 | 319 | 0 |

全て 0 failed、回帰ゼロ。

## 1. 実装1: 通知 IPC — 完了

第1弾 §3.3 の推奨（方式 B: シェル内蔵の軽量 IPC）を実装した。

### 構成

- プロトコル（`shell/include/notify.h` / `shell/src/notify.c`）:
  WM プロトコルと同型の固定長ヘッダ（magic "BDNT" / type / len）
  ＋固定レイアウト構造体のペイロード。POST（app 32B / title 128B /
  body 512B / urgency）、CANCEL（id）、POSTED（割当 id の ack）。
  テキスト解析もエスケープも無く、コピーは「ソケットバッファ→
  モデル」の1回だけ。
- サーバはセッションが保持し、`bd_session_step()` が WM の直後に
  ノンブロッキングでポーリングする（`bd_session_config.notify_path`
  で経路指定可、既定は WM と同じ規則の bdnotify.sock）。開始失敗は
  非致命（セッションは通知なしで継続）。
- クライアントは **接続ごとに1メッセージ**（`bd_notify_send()` /
  `bd_notify_cancel()` を libbdshell に収録）。アプリ側に常駐状態を
  持たないため、アプリのクラッシュでサーバが詰まることは無く、
  シェル不在は -1 で返る。
- シェルモデル（shellui）の拡張: 通知エントリが id / 送信元アプリ名 /
  緊急度を持つようになった。従来の `bd_shell_notify()`（満杯で失敗）
  は互換のため維持し、IPC 経由の構造化投稿は **満杯時に最古を
  追い出す**（第1弾 §3.3 の「件数上限で追い出す」）。表示文字列は
  title、body がある場合は "title - body"。アプリ名はメタデータ
  として保持・照会可能（シート表示への組み込みは未実施、下記）。

### テスト（shell/tests/test_notify.c、新規46件・全通過）

- 本番クライアント（子プロセス）→ソケット→サーバ→モデル→
  id/app/緊急度/表示文字列まで一致する通し確認。
- 複数クライアント同時接続（3接続を1回の poll で全適用）、
  投稿順の保持、上限での最古追い出し。
- id 指定キャンセル、未知 id のキャンセル（正常系として処理され
  エラーにならない）。
- 異常系: 不正 magic、過大な len、途中で切断された部分メッセージ
  （いずれも接続単位で切り捨て、エラーカウンタ+1、シェルは生存し
  後続の正常投稿が通る）。シェル不在時のクライアント -1。
- セッション経由の通し: 実セッションの step で投稿がモデルに届く
  ことを子プロセスから確認。

### 正直な注記

- 通知シートの表示は従来どおり表示文字列のみで、アプリ名/緊急度の
  視覚的な区別はまだ無い（値はモデルまで届いている）。
- 通知の永続化は無い（シェル再起動で消える。第1弾の棚卸しどおりで、
  本弾の範囲外）。

## 2. 実装2: アラーム常駐評価（bd-alarmd） — 完了

### 方式の選択（第1弾 §3.3 の設計判断に沿う）

**シェル内蔵スレッドではなく独立デーモンを選択**した。理由:
(1) シェル（braindesk2）は描画・入力・WM を担い、再起動や異常終了の
影響範囲が大きい。評価専用デーモンは単機能で、落ちてもシェルを
巻き込まず、逆も無い。(2) デーモンは実測 PSS 823 kB（ホスト
x86-64 ビルドでの参考値、§6）と小さく、常駐コストが読みやすい。
(3) ck_store はプロセスごとに状態を持つ設計で、評価者を1プロセスに
固定する方が二重評価を構造的に避けられる。

### 構成

- コア（`apps/alarmd/alarmd.c`）: ck_store を所有し `ck_poll()` を
  そのまま評価に使う（モデルの再実装なし）。時刻は注入、出力は
  シンク（notify / ring）経由で、本体はソケットも時計も持たない。
  発火イベントは1回だけ通知される。通知の宛先は本番 main が
  §1 の `bd_notify_send()` に接続する（シェル不在時は失敗として
  数えるだけで評価は継続）。
- clockmodel への追加は純粋な照会2件のみ: `ck_next_alarm_epoch()`
  （次回発火の wall 時刻）と `ck_next_snooze_until()`（最直近の
  スヌーズ期限）。既存の評価セマンティクスは無変更。
- 本体（`apps/alarmd/bd_alarmd.c`）: データルートとローカル
  オフセット（brainreg "datetime"、既定 540）は bd-clock と同一
  解決。単一インスタンスを flock で保証。ループはイベント駆動:
  timerfd（次回発火/タイマー/スヌーズまで。ただし上限30秒）＋
  クロックデータディレクトリへの inotify。外部編集（bd-clock の
  保存）は mtime スナップショットとの比較で自書き込みと区別し、
  store をリロードする（リロードで実行時状態はリセットされ、
  編集ウィンドウ内の発火は catch-up されない — alarmd.h に明記
  した割り切り）。SIGTERM/SIGINT で正常終了する。
- 音について: リングはシンクの `ring` フックまでで、本番 main は
  ログ出力のみ。**現行スタックにはアラーム音の再生経路自体が
  無い**（bd-clock もバナー表示のみ）ため、鳴るとは主張しない。
  フックが将来の再生実装の差し込み口。
- RTC wakealarm: `apps/alarmd/alarmd_wake.h/.c` にインターフェース
  のみ用意（環境変数 BD_ALARMD_WAKEALARM=1 で有効化し、次回発火
  時刻を「0 クリア→epoch 書込み」の手順で設定）。**実機未検証**。
  この VM には /sys/class/rtc 自体が無く、書込み列の検証は注入
  ライタでの単体テストまで。サスペンド中の発火は「動く」とは
  主張しない。
- bd-clock との二重発火について: 出力（通知・リング）はデーモン
  のみが持ち、bd-clock は通知も音も出さない実装のため、両方が
  開いていても出力は二重にならない。bd-clock のバナーは自前
  store の表示であり出力ではない、という整理を alarmd.h に明記。

### テスト（apps/alarmd/tests/test_alarmd.c、新規93件・全通過）

- bd-clock を介さずデーモンの poll だけでアラームが発火し、
  通知タイトル/本文/緊急度と ring が正しく出る。
- 前方時刻ジャンプでの catch-up 発火（1回だけ、catch_up フラグ）。
- 一度きりアラームの自動無効化が永続化され、デーモン再起動後に
  再発火しない。
- **デーモン停止中に期限切れしたタイマー**が、次回起動の初回
  poll で発火する（永続化された wall 終端からの再アーム）。
- **書込み途中リセットの模擬**: alarms.txt の2行目途中での
  切断＋ `.tmp.<pid>` 残骸を置いた状態から起動し、警告カウント
  と残骸の掃除（bdstore の sweep）を確認した上で、無事な行の
  アラームが発火する。
- スヌーズ: デーモンの store 経由でスヌーズを設定し、期限の
  sleep 計算への反映と再発火を確認。
- sleep 計算（上限キャップ、タイマーの残量、空ストア）、
  外部編集のリロード取り込み、通知チャネル断時の失敗カウント
  （評価は継続）、ck_next_alarm_epoch の曜日マスク/無効化/
  「発火時刻ちょうどでは翌日」の境界。
- wakealarm の書込み列（clear→set、失敗の伝播）は注入ライタで検証。

### 実バイナリのスモーク（ホスト実行、実測）

- 期限切れタイマーを仕込んだルートで起動 → 起動直後に発火して
  ring ログを出力。再起動後は RINGING 状態が永続化されている
  ため再発火しない。
- 2重起動は flock で拒否（rc=1）。SIGTERM で rc=0 の正常終了。
- 常駐 PSS は約 823 kB（smaps_rollup、ホスト x86-64 参考値）。

## 3. 実装3: マスコット設定の通し確認 — 完了

第1弾で配線した「設定アプリ → brainreg "mascot" → セッション
起動時読取り」を、`shell/tests/test_session.c` に21件追加して
固定した（102 → 123件、全通過）。

- 書き込みは設定アプリ（setmodel.c）と全く同じ reg_set 呼び出し
  （enabled/fullscreen は "true"/"false"、size は10進文字列、
  pack はフルパス）。
- 検証用の合成パックは2つのサイズレコード（160 / 240）を持つ
  v1 .bdm で、セッションの「要求高さに最も近いレコードを選ぶ」
  仕様により、設定値 168 → 160、248 → 240 と、書いた値が
  実際に効くところまで観測できる。
- 確認内容: enabled=false でマスコット無し、enabled=true＋pack
  で生成、size の値で選択レコードが変わる、存在しない pack
  パスでは機能オフ（クラッシュなし）。
- fullscreen はセッションに公開ゲッターが無い（生成時に可視性
  ポリシーへ取り込まれる）ため、セッションが読むのと同じキーの
  設定側ラウンドトリップと、フラグ設定下でのマスコット生成まで
  を確認範囲とし、それ以上は主張しない。
- ライブ反映は対象外（設定ページ自身が「次回起動時から反映」と
  表示する仕様どおり）。

## 4. ライブラリ統廃合の測定パス（削除・置換は未実施）

### 4.0 方法と一次資料

- 容量は Debian trixie **armel** main の Packages インデックス
  （deb.debian.org から取得。対象機 ARM926 は armel が正）の
  Installed-Size を用い、setup_brainux.sh の明示 apt リストを種に
  **Depends 閉包**を計算した（Recommends は 90-norecommend で無効。
  brain-config のトランザクションのみ --install-recommends のため
  その Depends/Recommends を種に加えた）。代替依存は apt と同じく
  先頭候補で解決。閉包は 643 パッケージ / Installed-Size 合計
  約 1,322 MiB。
- 「排他的容量」は「全種の閉包 − 当該領域の種を除いた閉包」で、
  その領域を外したときに実際に消える量。
- brain-config のメタデータは packagecloud の一次インデックス
  （dists/any/main/binary-armhf/Packages.gz、arch all のため armel
  でも同一）から取得した。
- **PSS は本弾では未実測**。この VM には対象 rootfs が無く、実測は
  実機/ゲストイメージ上で `brain-measure`（os-brainux/override 同梱）
  を置換前後に回す手順のみを提示する。ホストビルドの rebuild
  バイナリ実サイズは参考値として記す（braindesk2 2,702,352 B、
  bd-files 2,338,520 B、bd-clock 2,195,968 B。いずれもホスト
  アーキテクチャ・デバッグ情報込みで、目標値ではない）。

### 4.1 「要再確認」2件の決着（一次資料で確定）

1. **端末3種目 = rxvt-unicode**。jwm の Depends が
   `rxvt-unicode | gnome-terminal | konsole | x-terminal-emulator`
   で、先頭候補の rxvt-unicode（3,124 kB）が依存で入る。明示
   インストールの lxterminal（X 用）+ fbterm（fb コンソール）と
   合わせて、現行イメージの端末は **3種が実在**する。
2. **uim は現行イメージに存在しない**。apt リストに無く、
   brain-config の Depends（whiptail, parted, lua5.1, alsa-utils,
   psmisc, initramfs-tools）/ Recommends（triggerhappy, iw）にも
   無く、Depends 閉包にも現れない。`uim*` の文字列は
   brain-measure のプロセス名パターン（比較対象スタック向けの
   監視名）にのみ存在する。旧監査の「fcitx＋uim 重複」は現行
   構成では不成立で、IME の重複は無い（fcitx-anthy のみ）。

### 4.2 領域ごとの測定と判定案

| # | 領域 | 実測（Installed-Size） | 互換ゲート（依存者） | 判定案 |
|---|---|---|---|---|
| 1 | DE/WM（jwm+Xorg） | jwm 直接 489kB、jwm 排他 4.21MiB（rxvt-unicode 3.1MB を含む）。Xorg スタックは全 X アプリと共有され排他化できない | 既存 X アプリが新セッションで無変更で動くこと（未証明） | **残置**（統合フェーズで X 共存を証明してから） |
| 2 | グリーター（ly / braingreeter） | 両者ともソースビルドで dpkg 対象外 | セッション決定とセット。setup のコメントどおり ly が現行既定 | **要移行**（セッション切替の判断と同時に） |
| 3 | ファイル管理（pcmanfm） | 直接 1.55MiB、排他 7.40MiB（libfm 系） | bd-files にネットワーク/ゴミ箱等の機能差（第1弾 §4） | **要移行**（機能差の一覧化と解消が先） |
| 4 | 端末（lxterminal/fbterm/rxvt-unicode） | 直接合計 1.70MiB（tmux 込み）。lxterminal+fbterm 排他 37.59MiB の大半は libvte 経由の libicu76（36.5MB） | rebuild に端末アプリが無い。fbterm は X 無しコンソールの受け皿。rxvt-unicode は jwm 依存で jwm と運命共同体 | **残置**（rebuild 端末ができるまで）。rxvt-unicode は #1 と同時に消える |
| 5 | ブラウザ（netsurf-gtk/fb） | 直接 gtk 4.93MB / fb 3.77MB。gtk 排他は自パッケージのみ 4.82MiB（GTK3 は pcmanfm/lxterminal と共有） | gtk は X 用、fb は非 X 用の受け皿。rebuild の brain フロントエンドが後継 | **要移行**（fb 版から rebuild FE へ。gtk は X が残る間は残置） |
| 6 | IME（fcitx-anthy） | fcitx-anthy 直接 459kB、排他 23 パッケージ 77.0MiB | X アプリの入力は fcitx 経由のみ。libbrainime の fcitx アドオンが無くゲート不成立（第1弾 §4 のとおり） | **残置**（アドオン作成が前提条件） |
| 7 | DHCP（dhcpcd / isc-dhcp-client） | dhcpcd 直接 38kB（本体は dhcpcd-base）。isc 排他 7.76MiB だが iproute2（4.08MB）は brain-gadget / nmcli シムが `ip` を使うため残す → 実効削減は isc 本体＋libtirpc 系の約 3MiB | **dhclient の実使用者が存在**: override の enable_ethernet_gadget と brain-gadget が `dhclient usb0` を呼ぶ。dhcpcd サービス自体は有効化済み | **要移行**（上記2スクリプトを dhcpcd 呼び出しへ書き換えてから isc-dhcp-client を削除） |
| 8 | SSH（ssh メタ + openssh-client） | openssh-client 直接 4.93MB。`ssh` メタ排他 9 パッケージ 4.07MiB（openssh-server 3.36MB を含む） | `ssh` メタの Depends が openssh-server を引き込む。sshd は setup の有効化リストに無く、常駐させる運用も repo 内に無い | **削除可能**（リストから `ssh` を外し openssh-client のみ残す。約 4.07MiB 削減＋不要なサーバの除去） |
| 9 | 開発ツール一式 | 直接合計 93.4MiB、**排他 185 パッケージ 413.5MiB**（最大） | 実行時依存は repo 内に確認できず（numpy/ipython の利用者なし、git は ly の clone 用）。ただし setup 自身がイメージ構築中に ly と braindesk を **内部でビルド**するため構築時には必要 | **要移行**（ランタイムイメージから分離: 構築ステージで使い、最終イメージでは purge する構成へ） |
| 10 | メディア CLI（mpg123 / vorbis-tools） | 直接合計 2.77MiB、排他 6.64MiB | **旧 braindesk の bd-player が mpg123 / ogg123 を execlp で呼ぶ**（braindesk/src/apps/bd-player.c） | **残置**（旧 braindesk の置換完了まで） |
| 11 | 時刻（fake-hwclock / timesyncd） | 第1弾の判断どおり測定対象外 | 役割分担あり | 対象外（現状維持） |
| 12 | エディタ（vim） | 直接 3.49MiB、排他 43.28MiB（vim-runtime 38.4MB が大半） | GUI の無い唯一のエディタ。bd-notes はテキストファイル編集の代替ではない | **残置**（重複ではない） |

補足: rebuild 内部の共有は第1弾の結論どおり健全（brainreg は
libbdshell.a 経由の単一実装）。旧 braindesk（repo `braindesk/`）は
rebuild と完全な二重実装だが、その削除は #1/#3/#10 の置換完了が
前提で、本表の各行に内包される。

## 5. コピー/オーバーヘッド削減

- 通知 IPC: 固定長ヘッダ＋固定レイアウト構造体で、パース・
  エスケープ・文字列分割が無い。データコピーはクライアントの
  送信バッファ組み立て1回、サーバの受信→モデル取り込み1回のみ。
  接続はメッセージ単位で、常駐コネクション管理を持たない。
- アラーム: 評価は ck_poll の再利用で新規のスケジュール計算を
  増やしていない（次回時刻の照会は既存の fire epoch 計算の
  公開化）。デーモンの待機は timerfd＋inotify のイベント駆動で、
  ポーリング周期は持たない（30秒の上限は安全網）。
- マスコット通し確認はテストのみで実行時コストの変更なし。

## 6. キャッシュ/メモリ管理

- 新規キャッシュは導入していない。通知リストは従来どおり
  BD_NOTIFY_MAX=4 件の上限付きで、構造化投稿は最古追い出しの
  ため肥大化しない。
- bd-alarmd の常駐メモリは実測 PSS 約 823 kB（ホスト x86-64
  ビルド、smaps_rollup。目標アーキテクチャでの再測は統合
  フェーズ）。128MB 予算に対する比率は 1% 未満。
- デーモンは store を1つだけ所有し、リロード時も旧 store を
  閉じてから開く（同時保持なし）。

## 7. 実機の現実への対応

- 原子的書換え: 本弾は永続化形式を新設していない。アラームの
  状態は既存の bdstore（tmp→fsync→rename→dir fsync）のまま。
  §2 のテストで「書込み途中リセット相当」の壊れたファイル
  から起動できることを確認した。通知はメモリ内のみ（失われて
  も機能が壊れない使い捨て情報のため）。
- オフライン既定・時刻不信: 通知の追い出しは件数ベースのみ。
  アラーム評価は clockmodel の wall/monotonic クロスチェック
  （ジャンプ検出と catch-up）をそのまま使う。デーモンの
  sleep 上限30秒は時刻ではなくイベント駆動の安全網。
- SD 本拠 / eMMC ノータッチ: デーモンのデータは bd-clock と
  同じ SD 側ユーザーディレクトリ。ソケットは
  XDG_RUNTIME_DIR / /tmp（tmpfs）。eMMC へのアクセスは無い。
- 突然のリセット: デーモンは再起動後に永続化状態から再アーム
  する（期限切れタイマーの即時発火、発火済み一度きりアラームの
  無効化の保持をテストで確認）。

## 8. 未検証・残件

- **RTC wakealarm の実機可否**（§2）: インターフェースのみ。
  i.MX28 RTC でサスペンド復帰できるかは実機/ゲスト検証が前提。
- **bd-alarmd のセッション組み込み**: デーモン単体では動作するが、
  セッション起動時に誰が bd-alarmd を起動するか（セッションの
  子プロセス管理 / systemd ユニット）は統合フェーズの配線項目。
  現時点では「セッションから自動ではまだ起動しない」。
- 通知シートでのアプリ名/緊急度の表示区別（値はモデルまで
  届いている）。
- 統廃合（§4）は測定と判定案まで。削除・置換の実施は次弾以降
  （#8 の `ssh` メタ除去が最も小さい一手）。
- §4 の PSS は対象イメージでの brain-measure 実測が未実施
  （手順のみ提示）。
- 本弾の変更は全て作業ツリーのみで、**commit / push はしていない**。

## 変更範囲（他ラインとの衝突回避）

- 本弾が触るファイル: shell/include/notify.h（新規）、shell/src/notify.c
  （新規）、shell/include/shellui.h・shell/src/shellui.c（通知節のみ）、
  shell/include/session.h・shell/src/session.c（通知サーバ配線のみ）、
  shell/Makefile、shell/tests/test_notify.c（新規）、
  shell/tests/test_session.c（マスコット通しテストの追加のみ）、
  apps/alarmd/（新規ディレクトリ一式）、apps/Makefile、
  apps/clock/clockmodel.h・clockmodel.c（次回発火時刻の公開ヘルパ追加のみ）。
- 触らないもの: shell/mascot/ 配下、shell/src/mascot.c、
  shell/tests/test_mascot.c、shell/include/mascot.h
  （並行するマスコット再焼き込みラインの領域）。
