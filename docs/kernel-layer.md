# カーネル層の棚卸しと最適化

## 前提の発見: イメージにモジュールが無かった

Brainux の rootfs には `/lib/modules` が存在せず、カーネルは
実質モノリシックだった。そのため周辺ドライバは全部 `=y`
(組み込み) にせざるを得ず、挿すことのないドングルのドライバ
まで 128MB の RAM に常駐していた。defconfig にあった `=m`
(TSC2007、DS1307 RTC、SPI_GPIO、FSCACHE 等) は、モジュールが
イメージに無いので実際には読み込まれず死んでいた。

## 実施した変更

### イメージ側 (buildbrain)
- `make lbuild` が `modules_install` でモジュールを
  `os-brainux/modules-staging` にステージング (strip 済み、
  build/source リンクは除去)
- `override-pre.sh` が rootfs の `/lib/modules` へ取り込む
- CI に `kmod` (depmod) を追加

### カーネル側 (linux-brain `46edfa455`)
モジュール化 (=y → =m、udev が挿入時に自動ロード):
- 無線スタック CFG80211/MAC80211 と USB Wi-Fi 全般
  (ath9k/htc、ath6kl、ar5523、ath10k-usb、mt76 系、rt2x00、
  rtl8187、rtl8xxxu、rtw88、wilc1000)
- USB 有線 (usbnet 系、rtl8150/8152、smsc、rndis_host)、
  SPI 有線 (enc28j60)
- USB カメラ (UVC)、USB オーディオ、USB シリアル系

除去 (このハードで動作し得ないもの):
- WCN36XX (Qualcomm SoC 用、i.MX28 では無関係)
- ZSWAP + Z3FOLD (スワップデバイスが無い構成では死にコード)
- BLK_DEV_INTEGRITY (DIF 対応ストレージが無い)
- USB_SERIAL_CONSOLE (組み込み前提のデバッグ機能)

追加: BLK_DEV_LOOP (USB 共有イメージの整形など、ディスク
イメージをデバイス上で扱うため)

据え置きの判断:
- SLUB_DEBUG は EXPERT 無しでは promptless で切れないため
  今回は見送り (カーネル全体に EXPERT を入れる副作用の方が大きい)
- カーネル圧縮は GZIP のまま (XZ との差は SD 読み込み速度と
  展開速度で相殺、ブートチェーンとの相性も現行が実績あり)

## 実測 (フルビルド、同一ツリー before/after)

| 項目 | before | after | 差 |
|---|---|---|---|
| zImage | 7,126,448 B | 5,494,464 B | -22.9% |
| vmlinux 常駐 (text+data+bss) | 15,015,110 B | 11,576,155 B | -3.28 MiB |
| モジュール | 7 個 / 0.2 MB | 90 個 / 6.3 MB (strip 後) | 挿入時のみロード |

modules.dep で ath9k_htc → mac80211 → cfg80211 の依存連鎖が
解決できること、従来死んでいた tsc2007.ko / rtc-ds1307.ko が
出荷対象になったことを確認済み。

## 次の候補
- 起動時のメモリ確保 (CMA、予約領域) の棚卸し
- 不要な built-in の第二弾 (他機種と共用のドライバは残す前提で、
  サイズの大きい順に再評価)
- イメージでの実測: 起動直後の free / slabinfo を run 5 と比較
