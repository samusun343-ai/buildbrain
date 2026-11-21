# RESEARCH: SHARP Brain 改造に関する web 先行知見の総ざらい

調査日: 2026-11-21
調査範囲: Brainux Wiki、buildbrain / linux-brain 公開情報、個人ブログの実践記録、その他公開情報。
方針: 各発見に出典 URL と取得日を必須とする。見つからなかったテーマは「見つからなかった」と明記し、推測で埋めない。
本ファイルは調査のみの成果物であり、コードやイメージへの変更は含まない。

（§1〜§7 調査完了 — 2026-11-21。§3 以降は継続ラインが追記）

## 1. Brainux Wiki と buildbrain / linux-brain の公式・準公式情報

### 1.1 上流の現行リリースは Debian 13.4 "Trixie" / カーネル 6.1.70（2026-03-25）
- 内容: 利用者ブログが Release 2026-03-25-024518 の変更点を引用している。大きな変更は (a) Debian 13.4 "Trixie" ベース化、(b) **USB Ethernet Gadget を RNDIS から NCM へ切替え**、(c) USB Video Class (UVC) ドライバ有効化。実機 (PW-SH3) の uname は `6.1.70-ga9f534c7f53e`。
- 出典: https://nsb.homeip.net/wp/?p=5751 （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論**。OS メス入れ第2弾で「現行実装の USB ガジェットは NCM」と実測記録済みで、上流の公式な切替えと一致する。ユーザー前提の「RNDIS」は旧世代イメージの話であり、現行は NCM が正。
- 適用先: OS 項目（USB 接続の前提記述の更新）、WinCE 互換レイヤーの接続検討。

### 1.2 Trixie 化に伴う既知の互換性問題（利用者実測）
- 内容: 同ブログの実測。(a) Debian 13 の apt 版 mplayer は ARMv7 以降の命令を含むビルドで **Illegal instruction** になる（自前ビルドで回避）。bullseye では SDL2 アプリが Illegal instruction だった問題は trixie で解消。(b) trixie の libsdl1.2debian は実質 sdl12-compat（SDL2 ラッパー）になり SDL1.2 アプリが異常に遅い。bullseye の libsdl1.2 ソースを trixie 上でビルドすると旧来の速度に戻る。(c) t64 ABI（time_t 64bit 化）で旧バイナリと互換性がなく、混在させると apt の依存が壊れる。
- 出典: https://nsb.homeip.net/wp/?p=5751 （取得日 2026-11-21）
- こちらの現状との関係: **新規**。こちらの OS 第2弾では armel 基線（ARMv5TE）自体は実測で問題なしと結論したが、ディストリ側パッケージの命令セット混入（mplayer の例）は別レイヤーの落とし穴として未点検だった。
- 適用先: OS 統廃合/入れ替えの判定基準、統合フェーズの互換性試験項目（既存アプリの実走確認）。

### 1.3 起動経路の再確認（複数ソースで一致）
- 内容: 純正ファーム（WinCE）から機種名の実行ファイル（例 `edsh6exe.bin`、モデルごとに EDSA1EXE.BIN 等）を起動し、それが U-Boot で、SD の zImage + 機種別 DTB（imx28-pwsh1..7.dtb）を読む。旧来は NK.bin + BrainLILO の経路（Sx1/Sx6 は `NK.bin` + BrainLILO、PW-H1 は Boot4u と DeepWiki の整理）。SD は p1=FAT（起動チェーン）、p2=ext4（rootfs）。
- 出典: https://qiita.com/kurokouji/items/66ddd4884cefbc89d736 、https://deepwiki.com/brain-hackers/buildbrain 、https://vintage2000.org/linux_on_sharp_brain （いずれも取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論**（こちらの実測: WinCE → brainlilo → U-Boot 本体 → カーネル、SPL を通らない）。ファイル名規則（EDSAxEXE.BIN）と DTB の機種別配置はイメージ再発行時の照合材料になる。
- 適用先: 統合フェーズ（イメージ再発行のレイアウト照合）。

### 1.4 情報交換の場は Wiki と Discord、知見は Scrapbox にも蓄積
- 内容: 「Brainuxの今」（IoTLT vol140 発表資料）に「Wiki と Discord で情報交換」と明記。Brain ハッカーズの解析記録は Scrapbox（scrapbox.io/brain-hackers）にもあり、第1世代の回路解析・JTAG 記録等が置かれている。
- 出典: https://files.speakerdeck.com/presentations/7903450e903e4112bb3cb41192ab7879/20261031_IoTLT_vol140_kitazaki_v1.pdf 、https://zenn.dev/tnishinaga/articles/ee0b6cf4f8683d （取得日 2026-11-21）
- こちらの現状との関係: **新規**（体系調査の対象として Scrapbox を含めるべきだった）。
- 適用先: 本調査の継続先リスト。

## 2. CPU クロック問題

### 2.1 実測値が2系統ある（240MHz と実質フルクロック相当）
- 内容: (a) unasuke の実測（2021年、Ruby ビルド記事）: `/sys/kernel/debug/clk/clk_summary` で CPU clock **240MHz**、BogoMIPS **119.70**。(b) nsb.homeip.net の実測（2024-12-23 版イメージ、カーネル 5.4.149、PW-SH3）: lscpu の BogoMIPS **226.09**。ARM926EJ-S の BogoMIPS は概ねクロックの半分程度に出るため、(b) は約 454MHz 級で動いていた計算になる。
- 出典: https://blog.unasuke.com/2021/building-ruby-on-brainux/ 、https://nsb.homeip.net/wp/?p=3048 （取得日 2026-11-21）
- こちらの現状との関係: **こちらの実測と矛盾はしないが、単純ではない**。こちらは「クロックは WinCE が残した値のまま、設定者がいない」と特定済み。(b) の存在は、残される値（＝起動前の WinCE の状態や機種・世代）が条件によってはフルクロック級になり得ることを示唆する。ただし (b) 側は clk_summary の実レート未掲載のため、BogoMIPS からの推算に留まる。
- 適用先: クロック是正の検証手順（統合フェーズで clk_summary 実測を機種・起動条件ごとに取る際の比較対象）。

### 2.2 コミュニティで「修正方法」が公開されているか
- 現時点の検索では、buildbrain / linux-brain の issue や記事で「Brainux の CPU クロックが半分になる問題とその修正」を直接扱った公開情報は**見つからなかった**（2026-11-21 時点、検索ベース）。問題の存在自体を指摘した公開記録も unasuke の実測値がほぼ唯一の手がかり。
- こちらの現状との関係: こちらの是正案（brainlilo / U-Boot / カーネル早期での設定＋電圧保証の確認）はコミュニティに先行事例が無い独自作業になる見込み。
- 適用先: クロック是正項目（外部の答え合わせはできない前提で進める）。

## 3. カーネル設定・ドライバの知見

### 3.1 キーボード実装は世代で2方式ある（Brainux Wiki の対応表）
- 内容: Brainux Wiki「ハードウェア対応状況とロードマップ」の表。キーボードの読み取り方式は世代で異なる: **Sx1〜Sx2 は MPU がキーマトリックスを読み I²C で SoC へ送信**、**Sx3〜Sx7 は SoC が直接キーマトリックスを読む**（3桁世代も直接）。画面開閉検知も同じ経路をたどり、Sx1〜Sx2 では未対応、Sx3〜Sx7 では対応済み。同表の他項目（2021-02-23 時点の記述）: 電源コントローラは未対応（充電状態の表示など一切不可）、cpufreq は無し、eMMC への Linux インストールは研究中。
- 出典: https://wiki.brainux.org/beginners/roadmap/ （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論＋新規の裏付け**。対象機 PW-SH6 は Sx6 なので「SoC 直接読み」世代に属し、こちらのキーボード調査（物理48キー等）の前提と矛盾しない。cpufreq 無しは §2 のクロック問題と同根（制御機構自体が無い）。新規点として、**電池の充電状態を取る公式な経路が上流でも未対応だった**ことが、ステータス表示の状態ソース問題に直結する。
- 適用先: キーボード実装と evtest 照合（統合フェーズ）、シェルのステータスアイコン（電池状態の取得元確認）、機種移植（§6）。

### 3.2 キーボード記号入力の繰り返し入力バグ（利用者実測・長期化）
- 内容: nsb.homeip.net の Trixie 版実機レポート（PW-SH3）。「PW-SH3 本体のキーボードで、記号の入力時に繰り返し入力されてしまうのは相変わらず」とあり、bullseye 世代から続く既知問題であることが分かる。同記事ではタッチパネルの右クリックエミュレーションは設定で有効化できることも確認されている。
- 出典: https://nsb.homeip.net/wp/?p=5751 （取得日 2026-11-21）
- こちらの現状との関係: **新規**。こちらのキーボード未確認事項（U1/U3/U5/U6、統合フェーズで evtest 照合予定）に「記号キーのリピート異常」を追加すべき公開実測がある。機種は SH3（Sx3、直接読み世代の初期）で、SH6 でも同経路なら再現し得る。
- 適用先: 統合フェーズの実キー evtest 照合項目。

### 3.3 タッチパネルは LRADC 抵抗膜（mxs-lradc-ts）、較正は xinput_calibrator 方式
- 内容: Brainux Wiki「タッチパネル」。デバイス名は `mxs-lradc-ts`（i.MX28 の LRADC を使う抵抗膜）。対応表（2021-02-23 時点）では「反応はするが位置がおかしい」状態で、対処は xinput_calibrator（Brainux 同梱）で Calibration 値を取り `/etc/X11/xorg.conf.d/99-calibration.conf` に書く方式。世代によるハードウェア差分は「不明」とされている。
- 出典: https://wiki.brainux.org/tips/touch-panel/ 、https://wiki.brainux.org/beginners/roadmap/ （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論**（抵抗膜・較正必須という前提はこちらの設計と同じ）。上流の較正値は Xorg の evdev Calibration（0〜4095 の生値域）であり、こちらの自前スタックでは較正の持ち方が異なるため、値そのものではなく「個体ごとに較正が要る」という運用知見として参照する。
- 適用先: 統合フェーズのタッチ較正手順、OSK/タッチ検証。

### 3.4 音声: 内蔵スマートアンプは Yamaha 系と Rohm 系の2系統（BU26154 固有の公開情報は見つからず）
- 内容: Brainux Wiki の対応表に「Brain に内蔵されているスマートアンプには大きく分けて Yamaha 系と Rohm 系がある」「録音と再生は 2021年2月23日現在解析中で非対応」とある。以後の公開資料で BU26154（Rohm 系）ドライバ固有の既知問題や対処を述べたものは、今回の検索範囲では**見つからなかった**。
- 出典: https://wiki.brainux.org/beginners/roadmap/ （取得日 2026-11-21）
- こちらの現状との関係: **矛盾なし（公開情報が実物より遅れている）**。こちらの DTS/config 監査では PW-SH6 の実コーデックは BU26154 で、ドライバ（SND_SOC_BU26154）は削減後 config にも残存することを確認済み。つまり公開 Wiki の「非対応」は 2021-02 時点の記述で、その後ツリー側では実装が進んだが公開ドキュメントが追随していない。
- 適用先: 音声検証（player の実 ALSA 出力確認）は公開知見に頼れない前提で、統合フェーズの実機/ゲスト実測で担保する。

### 3.5 表示: 上流の LCD 実装は「DMA を使わない非効率な実装」と明記されている
- 内容: Brainux Wiki の対応表 LCD 項に「一部については実装が完了。ただし、DMA を使用しない非効率な実装になっているため、改善が必要」とある（2021-02-23 時点）。利用者側の実感とも一致し、nsb のゲーム移植記事では「Brain の画面描画の速度が遅すぎて、デフォルトの 50FPS だとタイマー割込み処理が間に合わずキー入力処理が呼ばれない」ため FPS を 20 程度に下げて回避した記録がある。
- 出典: https://wiki.brainux.org/beginners/roadmap/ 、https://nsb.homeip.net/wp/?p=3408 （取得日 2026-11-21）
- こちらの現状との関係: **新規の裏付け**。上流の非効率指摘は fbdev 経路前提の話で、こちらの表示経路（DRM tiny ドライバ）は別実装だが、「描画が遅くフレームレート前提を置くと入力が詰まる」という現象級の知見はこちらのアニメーション設計（§9.5 残像・転送量の監査）と同じ方向を向いている。
- 適用先: GUI ライブラリのアニメーション/転送設計、統合フェーズの表示性能実測。

### 3.6 【SH6 固有】バックライト制御の DT プロパティ名問題（立命館大学の実践記録）
- 内容: PW-SH6 実機での Brainux バックライト制御の解決記録（2026-11-01 公開）。問題は2段: (a) Brainux では純正と違い、蓋を閉じてもバックライトが点灯したままになる。(b) 輝度自体は pwm-backlight ドライバ（`/sys/class/backlight/`）で制御できるが、PW-SH6 の DTB（`/boot/imx28-pwsh6.dtb`）にあるバックライトの有効/無効 GPIO のプロパティ名が `enable_gpios`（アンダースコア）になっており、標準ドライバが参照する `enable-gpios` と異なるため、消灯側の制御が効かない。記事では DTB を書き換えることで段階調整から消灯まで成功している。なお Brainux の `/boot` は読み取り専用マウントで、作業には remount,rw と DTB のバックアップが前提とされている。
- 出典: https://cysec.ise.ritsumei.ac.jp/2026/11/01/brainux%e3%81%ae%e3%83%90%e3%83%83%e3%82%af%e3%83%a9%e3%82%a4%e3%83%88%e5%88%b6%e5%be%a6%ef%bd%9e755%e5%86%86%e3%81%ae%e9%9b%bb%e5%ad%90%e8%be%9e%e6%9b%b8%e3%80%81brain-pw-sh6%e3%81%ab%e3%81%a6/ （取得日 2026-11-21。本文の直接取得に失敗したため検索結果に表示された本文抜粋に基づく）
- こちらの現状との関係: **新規・重要**。こちらの DTS 監査（§6 カーネル削減の安全性確認）では backlight ノードのプロパティ名までは照合していない。対象機そのもの（PW-SH6）の一次実践記録であり、こちらの imx28-pwsh6.dts 側に同名の問題があるか即照合できる。
- 適用先: カーネル/DTS 項目（imx28-pwsh6.dts の backlight ノード照合と必要なら是正）、設定アプリの輝度・消灯項目の実効性確認、蓋開閉連動（§3.1 の開閉検知は Sx3〜Sx7 で対応済みとあるので、検知→消灯の配線が残件になる）。

### 3.7 サスペンド（Suspend to RAM）実装の試みが buildbrain の knowledge 文書にある
- 内容: buildbrain の docs/knowledge に puhitaku の発表資料「Linux suspend on Brain」（2023年、若手の会）がある。読み取れる範囲では、Linux のサスペンド種別（Suspend to Idle / Standby / Suspend to RAM / Hibernation）を Brain で実装する試みで、電源制御を Device Tree 側（regulator / GPIO による MOSFET 制御）で定義する必要があり、ウェイクアップは gpio-keys のイベントを使う構成が検討・実装されている。
- 出典: https://git.pepepper.net/brain-hackers_pepepper-mirror/buildbrain/raw/commit/e28927cf02de82f44c3a5529448c3150b810612c/docs/knowledge/202304_linux_suspend_on_brain.pdf （取得日 2026-11-21。PDF の文字抽出が不完全で、詳細な結論部分は原文で要再確認）
- こちらの現状との関係: **新規**。こちらの OS 項目ではサスペンドはまだ設計対象に入っていない（タイマー手術と journald 上限まで）。実装するなら一次資料がリポジトリ内 knowledge にある。
- 適用先: 省電力項目（サスペンド実装時の出発点）、OS メス入れの将来候補。

### 3.8 SD カード性能の実測値とカーネル config の解説記事は見つからなかった
- Brain 実機での SD カード読み書きベンチマーク（数値）を公開した記録は、今回の検索範囲では**見つからなかった**（i.MX 系一般のフォーラム情報のみ）。また linux-brain の defconfig の設計意図を解説した公開記事も**見つからなかった**（config の実物はリポジトリ内にあるのみ）。
- こちらの現状との関係: こちらの §4（SD 性能）は「ボトルネック候補まで、実測は統合検証」という切り分けで、公開実測との答え合わせはできない。実測は自前で取るしかない。
- 適用先: 統合フェーズの SD 実測（比較対象なしで絶対値を記録する）。

## 4. 性能チューニングの実践記録

### 4.1 swap は「SD 上に 256MB 以上」が公式推奨。zram の Brainux 実践記録は見つからなかった
- 内容: Brainux Wiki「Swap 領域」は、DRAM 128MB では OOM Killer で重要なプロセスが落とされる危険があるとして swap 作成を推奨し、例として **256MB（これ以上を推奨）** の swapfile を rootfs 上に作る手順を示す（2021-02-23 時点のリリースでは swap は標準未設定で、将来のリリースで標準化を検討とある）。利用者実践ではパーティション方式もあり、nsb は 1GB の swap パーティションを設定し、別記事では 2GB 確保を「少し多かった。512MB 程度でよかったかも」と振り返っている。一方、**Brainux で zram を使った実践記録は今回の検索範囲では見つからなかった**（一般の Linux 向け zram 解説のみ）。
- 出典: https://wiki.brainux.org/tips/swap/ 、https://nsb.homeip.net/wp/?p=5751 、https://nsb.homeip.net/wp/?p=3048 （取得日 2026-11-21）
- こちらの現状との関係: **新規**。こちらの OS 第1弾 §7 は zram の A/B 測定計画（採用条件つき）だが、コミュニティの先行実績は SD swap 一択で、zram は前例なし。SD swap はカード書込み（寿命・書込み中リセット時の破損）と引き換えになる点が、こちらの「SD は本拠だが書込みは最小化」という原則と緊張する。
- 適用先: zram A/B 計画（比較対象は SD swapfile 256MB〜1GB が実践相場）、メモリ予算の設計。

### 4.2 上流自身の軽量化の答えは Buildroot rootfs（実験用）
- 内容: buildbrain の README に、Buildroot rootfs は「最も軽量な rootfs（実験用）」を目指すものと明記され、Debian rootfs とは別に `sd_buildroot.img` の生成手順が用意されている。
- 出典: https://github.com/brain-hackers/buildbrain （取得日 2026-11-21）
- こちらの現状との関係: **新規（比較対象の存在）**。こちらの作り直しは Debian ベースを維持したまま統廃合で軽くする路線で、上流の Buildroot 路線とは別の答え。ただし「既存アプリがそのまま動く」要件では Debian 維持が前提になるため、Buildroot は採用候補ではなく軽さの参照値として扱うのが妥当。
- 適用先: OS 統廃合のサイズ比較（参照値）、将来の実験イメージ。

### 4.3 起動高速化の実践記録は見つからなかった。メモリ実測の公開例は1件
- 内容: Brainux の起動時間を systemd-analyze 等で測って公開した記録は、今回の検索範囲では**見つからなかった**。メモリ使用量の公開例としては nsb の実測があり、bullseye ベース＋標準デスクトップ起動後の `free` で Mem total 118,352 KiB（128MB からカーネル予約等を引いた値）、used 42,684 KiB、available 70,476 KiB。
- 出典: https://nsb.homeip.net/wp/?p=3048 （取得日 2026-11-21）
- こちらの現状との関係: こちらの §3.4「アイドル時メモリ未実測（統合フェーズで実測）」に対する唯一の公開比較値。used 約42MB が上流標準構成の水準になる。
- 適用先: 統合フェーズのアイドル時メモリ実測の比較対象、起動時間は自前で基準を作る。

### 4.4 ブラウジング性能の公式見解: グラフィカルブラウザは非現実的、NetSurf を標準追加
- 内容: Brainux Wiki「Web ブラウザ」は、SoC が初代 Raspberry Pi の約半分のクロックで RAM も 128MB しかないため「インターネットブラウジングは非現実的」と明言する（著名人の軽量サイトの表示に実測15秒、Twitter は表示不可能）。そのうえで 2022-05-17 版リリースで **NetSurf を標準追加**し、Midori はプリインストール、テキストブラウザ（Lynx / w3m）はスムーズとしている。
- 出典: https://wiki.brainux.org/tips/web-browser/ （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論**。こちらのブラウザ基盤も NetSurf 3.11 で、上流の選択と一致する。性能の合格ラインは「15秒級が普通の世界」という前提で置くべきだという較正材料になる。
- 適用先: ブラウザ項目の性能基準、統合フェーズの表示実測。

## 5. USB ガジェット（RNDIS / NCM）と ssh 接続

### 5.1 Wiki の手順は RNDIS 前提（configfs で rndis 関数＋dhclient）
- 内容: Brainux Wiki「USB Ethernet Gadget」の手順。USB コントローラの動作モード切替（後述 §5.3）が前提で、有効化は configfs（`/sys/kernel/config/usb_gadget/eth`）に **rndis.rn0** 関数を作り、固定 MAC アドレス（dev_addr `8a:15:8b:44:3a:02` / host_addr `8a:15:8b:44:3a:01`）を書き、`ifconfig usb0 up` の後に **`dhclient`** でアドレスを取る。Brainux 2023-07-29-024604 以降は有効化処理が起動時に自動実行される。Wiki 冒頭には「PC と Brain で双方向に SSH したりできます」とあり、ssh の双方向利用が公式に想定されている。
- 出典: https://wiki.brainux.org/tips/usb-ethernet-gadget/ （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論＋差分1件**。こちらの OS 第2弾で「dhclient の実使用者は gadget 起動スクリプトのみ」と特定し dhcpcd へ一本化したのは、この Wiki 手順の系譜のスクリプトが相手だったと分かる。Wiki 側の記述は今も dhclient のまま（ドキュメントが NCM 化にも dhcpcd 化にも追随していない）。
- 適用先: DHCP 一本化の上流追随確認、接続手順書（利用者向け）の記述。

### 5.2 ホスト側の事情: RNDIS は Windows で手動ドライバ割当て、macOS は実質困難だった
- 内容: 同 Wiki のホスト別手順。Windows では Brain が「ほかのデバイス」に「RNDIS」として列挙され、デバイスマネージャで Microsoft の「リモート NDIS 互換デバイス」ドライバを手動で割り当てる必要がある。macOS では HoRNDIS（kext）を自前ビルドし、SIP（システム整合性保護）を解除する必要があり、現行 macOS ではハードルが高い。Linux ホストは NetworkManager の「Shared to other computers」で共有する。
- 出典: https://wiki.brainux.org/tips/usb-ethernet-gadget/ （取得日 2026-11-21）
- こちらの現状との関係: **新規**。ユーザーが「Wi-Fi が無いので USB 経由で ssh を使う」という利用形態は Wiki の想定どおりだが、RNDIS 時代はホスト側の手間が大きかった。NCM（§1.1 の上流切替え）では Windows / macOS / Linux とも標準ドライバで認識されるため、この手間が消える側に変わっている。
- 適用先: 接続手順書、NCM 前提の確定（§5.4）。

### 5.3 モード切替の正規手段は brain-config（DTB 手編集でも可）
- 内容: USB コントローラは初期状態でホストモード。デバイス（ガジェット）にするには (A) `brain-config`（raspi-config 風ツール）の Interface Options →「USB Switch the role of the USB Host Controller」で host / peripheral を選ぶ方法と、(B) DTB を手編集して `usb@80080000` ノードの `dr_mode = "host"` を `"peripheral"` に書き換える方法がある。ガジェット使用中は USB キーボード・マウス等の周辺機器は併用できない（ホスト機能と排他）。
- 出典: https://wiki.brainux.org/linux/brain-config/ 、https://wiki.brainux.org/tips/usb-ethernet-gadget/ 、https://nsb.homeip.net/wp/?p=3048 （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論**（こちらの第2弾でも USB ガジェットネットワークには DHCP 呼出し1行以外触れていない）。作り直し側の設定アプリからこの切替を扱うなら、正規の作法は brain-config と同じ DT 書換え（＋再起動）である点が設計制約になる。
- 適用先: 設定アプリの項目設計（USB ロール切替を項目化する場合の実装方式）、OS 項目。

### 5.4 NCM 切替の理由の一次資料は見つからなかった（業界動向とは一致）
- 内容: §1.1 のとおり上流は 2026-03 リリースで RNDIS から NCM へ切り替えたが、**切替理由を上流が明記した一次資料は今回の検索範囲では見つからなかった**。業界側の動向としては: 2022 年に Linux コミュニティで「RNDIS は信頼できない USB デバイス相手では本質的に安全でない」としてドライバ無効化が提案され、カーネルの rndis_host.c 自体に「USE OF RNDIS IS STRONGLY DISCOURAGED」と明記されている。Android は Pixel 6 以降 RNDIS をやめて CDC-NCM に移行。カーネルメンテナ（Greg Kroah-Hartman）は legacy な RNDIS ガジェットドライバの削除方針を公言している（2026-10 の LKML）。
- 出典: https://en.wikipedia.org/wiki/RNDIS 、https://lists.openwall.net/linux-kernel/2026/10/30/1181 （取得日 2026-11-21）
- こちらの現状との関係: **矛盾なし**。上流の切替はこの業界動向と方向が一致し、こちらの「現行は NCM が正」という実測結論（第2弾）を補強する。RNDIS に戻す検討は不要と判断できる材料。
- 適用先: USB 接続の前提確定、WinCE 互換レイヤーの接続検討。

### 5.5 給電つき OTG アダプタの相性と給電の癖（利用者実測）
- 内容: nsb の実測。Brainux はデフォルトで USB ホストモードだが Brain から周辺機器への給電はできないため、外部給電つき MicroA OTG アダプタが必須。アダプタの相性（充電可否）には個体差があり、「一度給電を止めると、再開しても Brain に給電されていないように見える（そのうちバッテリ不足で電源が落ちる）」という癖が報告されている。認識が不安定な場合は充電ケーブルの抜き差しで認識することもある。
- 出典: https://nsb.homeip.net/wp/?p=3048 （取得日 2026-11-21）
- こちらの現状との関係: **新規**。実機運用の知見で、こちらの作業には直接の対応物がないが、統合フェーズで USB 周辺機器（キーボード等）を実機検証する際の前提知識になる。
- 適用先: 統合フェーズの実機検証手順（給電条件の記録）。

## 6. 機種差（PW-SH6 と他機種）

### 6.1 世代の骨格: 第1世代 TMPA910 / 第2世代 i.MX28 / 第3世代 i.MX7ULP＋RTOS
- 内容: Brain の世代構成は複数ソースで一致する。(a) 第1世代（2011年頃、3桁世代・G系）: TOSHIBA TMPA910CRAXBG（armv4l）＋ 64MiB DRAM、OS は Windows CE。(b) 第2世代（4桁世代・Sx1〜Sx7）: NXP i.MX28（armv5tej、i.MX283）＋ 128MiB DRAM、OS は Windows CE 6.0。(c) 第3世代（2021年〜、x1 系）: NXP i.MX7ULP（armv7）＋ 128MiB DRAM、OS は Windows CE ではなく μITRON ベースの RTOS とみられる。Brainux の Linux ブートが対応済みなのは第2世代のみ（Wiki 対応表で第1世代・x1 は未対応）。
- 出典: https://wiki.brainux.org/beginners/roadmap/ 、https://git.pepepper.net/brain-hackers_pepepper-mirror/buildbrain/raw/commit/f83787bac4a775f4b49f71ccb32d774ea1622a70/docs/knowledge/202110_brain_boot_sequence.pdf 、http://ja.wikipedia.org/wiki/Brain_(電子辞書) （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論**。対象機 PW-SH6 は第2世代の i.MX28 機であり、こちらの全作業（カーネル、DTS、QEMU モデル）の前提と一致する。第3世代は CE ですらないため、起動経路（CE の追加アプリ → ローダ）自体が存在せず、移植対象にならない理由も明確。
- 適用先: 機種展開の範囲確定、WinCE 互換レイヤーの前提（対象は CE 世代のみ）。

### 6.2 第2世代の中でも一様ではない（移植時の差分リスト）
- 内容: 同じ i.MX28 世代でも機種差がある。(a) キーボード読取り方式: Sx1〜Sx2 は I²C 経由の MPU、Sx3〜Sx7 は SoC 直接（§3.1）。(b) 画面解像度: PW-SH1 は 800×480（発表資料の仕様表）に対し PW-SH6 は 854×480（こちらの一次確認済み）。(c) サブ LCD の有無が機種で異なる（日経 xTECH の開発者記事: 「SoC のほかにも、画面解像度やサブLCDの有無などが機種ごとに異なっている」）。(d) 音のスマートアンプに Yamaha 系 / Rohm 系の2系統（§3.4）。(e) 起動まわりは機種別ファイル: DTB は imx28-pwsh1..7.dtb と機種ごと、CE 側の起動実行ファイル名も機種別（§1.3）。
- 出典: https://wiki.brainux.org/beginners/roadmap/ 、https://xtech.nikkei.com/atcl/nxt/column/18/02170/083100006/ 、https://git.pepepper.net/brain-hackers_pepepper-mirror/buildbrain/raw/commit/e28927cf02de82f44c3a5529448c3150b810612c/docs/knowledge/202304_linux_suspend_on_brain.pdf （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論＋要注記1件**。こちらの統合準備では DTB 全9機種分を同梱済みで (e) は織り込み済み。注記は (b): 画面前提は機種ごとに違うため、854×480 は SH6 の値であって世代共通値ではない（ライブラリの寸法設計を他機種へ広げる際は機種別プロファイルが要る）。
- 適用先: 統合フェーズの機種展開、GUI ライブラリの機種プロファイル、キーボード/音声の機種別検証。

### 6.3 PW-SH6 固有の既知問題はバックライト DT（§3.6 の相互参照）
- SH6 固有として公開記録があるのは §3.6 のバックライト `enable_gpios` プロパティ名問題。それ以外の SH6 固有の不具合報告は今回の検索範囲では**見つからなかった**（利用者ブログの主機は PW-SH3 のものが多い）。
- 適用先: DTS 照合（§3.6 と同じ）。

## 7. その他の落とし穴・ツール・配布物

### 7.1 設定の公式 UI は brain-config（作り直し側の参照点）
- 内容: §5.3 のとおり brain-config は raspi-config をベースにした公式の設定ツールで、rootfs 拡張（Expand Filesystem）と USB ロール切替などを担う。パッケージは packagecloud の brainhackers/brainux リポジトリから apt 配布され、rootfs 構築スクリプト（os-brainux/setup_brainux.sh）が `brain-config` を導入する。同スクリプトではログインマネージャに brain-hackers の Ly フォークを採用し、fake-hwclock の load/save を有効化している（時刻を保持する仕組みをソフトウェアで肩代わりする前提の構成）。
- 出典: https://wiki.brainux.org/linux/brain-config/ 、https://raw.githubusercontent.com/brain-hackers/buildbrain/master/os-brainux/setup_brainux.sh （取得日 2026-11-21）
- こちらの現状との関係: **新規**。設定アプリ作り直しの項目設計では、公式に既にある設定 UI（brain-config）の担当範囲と重複/分担を意識する必要がある。fake-hwclock の採用は、こちらの「時刻不信（日数ベース有効期限の廃止）」方針と同じ現実（RTC を信用できない）への上流の対処で、方針の裏付けになる。
- 適用先: 設定アプリの項目対応表、時刻まわりの設計。

### 7.2 apt の小さな落とし穴: x11-ico-dvd が upgrade で毎回引っかかる
- 内容: nsb の Trixie 版実測で「apt upgrade 実行時に、毎回『x11-ico-dvd』が更新で引っかかってくる」と記録されている。rootfs 構築スクリプトにも x11-apps 群の一部として x11-ico-dvd が含まれている。
- 出典: https://nsb.homeip.net/wp/?p=5751 、https://raw.githubusercontent.com/brain-hackers/buildbrain/master/os-brainux/setup_brainux.sh （取得日 2026-11-21）
- こちらの現状との関係: **新規（軽微）**。こちらのイメージ再発行で同じパッケージ構成を踏むと再現し得る。
- 適用先: 統合フェーズの apt 動作確認。

### 7.3 ビルド側の落とし穴2件（buildbrain README の Known issues と Docker 注意）
- 内容: (a) ホストコンパイラが GCC 10 の場合 `make ubuild` が失敗する。原因は u-boot-brain の dtc 生成ファイルにある `YYLTYPE yylloc;` の二重定義で、コメントアウトが公式の回避策。(b) macOS の Docker ビルドでは、rootfs を APFS のバインドマウントに置くとデバイスファイルや setuid が壊れて「見た目は完全だが起動しない rootfs」になるため、named volume（VM 内 ext4）に置くことが必須と明記されている。
- 出典: https://github.com/brain-hackers/buildbrain （取得日 2026-11-21）
- こちらの現状との関係: **新規**。こちらのビルド環境は Linux VM のため (b) は直接は当たらないが、(a) はホスト GCC の世代に依存する再現性のある罠で、U-Boot を触る作業（クロック是正案 B: U-Boot 本体で設定）で踏み得る。
- 適用先: クロック是正（U-Boot 変更時）、イメージ再発行のビルド手順。

### 7.4 回復・投入ツール: uuu（i.MX リカバリ）と nkbin_maker / boot4u
- 内容: buildbrain の構成要素として、i.MX283 のリカバリモードへ U-Boot を注入する **uuu**（NXP の公式ツール）の利用手順、U-Boot を nk.bin に詰める nkbin_maker、CE 側の追加アプリとして置く boot4u が用意されている。文鎮化時の復旧経路と、起動チェーンの生成ツールが一式そろっている。
- 出典: https://github.com/brain-hackers/buildbrain （取得日 2026-11-21）
- こちらの現状との関係: **既に同じ結論**（起動経路の理解は §1.3 と一致）。新規点は「リカバリモード＋uuu」という実機復旧手段の存在で、DTB 書換え等の危険な操作（§3.6）を行う際の安全網になる。
- 適用先: 統合フェーズの実機作業手順（復旧手段の事前確認）。

### 7.5 Scrapbox の蓄積は主に WinCE 側の作業記録（Python 3 移植など）。Discord の公開アーカイブは見つからなかった
- 内容: Scrapbox（scrapbox.io/brain-hackers）には Brain Hackers の作業記録が蓄積している。代表例は puhitaku の「Python 3 の移植」で、WinCE 向け CPython 3.10 の移植記録。CE では同名の異なる DLL を別々に読み込めないため、異なるビルドの python が同時起動で干渉する問題があり、`.pyd` の命名変更と実行ファイルへのハッシュ埋め込み照合で解決した、といった CE 固有の実践知見が残されている。一方、Discord 上の議論の公開アーカイブ（検索可能な形）は**見つからなかった**（クローズドな場のため、外部からは参照できない）。
- 出典: https://scrapbox.io/brain-hackers/Python_3_%E3%81%AE%E7%A7%BB%E6%A4%8D （取得日 2026-11-21）
- こちらの現状との関係: **新規**。Linux 側の知見は Wiki と GitHub に寄っており、Scrapbox は CE 側（追加アプリ開発）の知見源という住み分け。CE の DLL 同名干渉の話は、WinCE 互換レイヤーや CE 側ツール（brain-tools 系）の設計で同種の問題を踏まないための参照になる。
- 適用先: WinCE 互換レイヤー設計、CE 側作業全般。

### 7.6 OTG ケーブルの動作確認一覧が Wiki にある
- 内容: Brainux Wiki の Tips に「OTG ケーブル」ページがあり、動作可否が確認された給電つき OTG ケーブル/アダプタの一覧が維持されている（§5.5 の相性問題への公式側の回答）。
- 出典: https://wiki.brainux.org/ （トップの Tips 一覧、取得日 2026-11-21）
- こちらの現状との関係: **新規（参照先の確定）**。実機検証で周辺機器を使う際は、まずこの一覧で確認済みアダプタを選ぶのが先行知見に沿う。
- 適用先: 統合フェーズの実機検証の機材選定。
