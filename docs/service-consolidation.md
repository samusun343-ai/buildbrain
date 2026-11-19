# サービス層の統廃合 (第1弾: ネットワークと常駐デーモン)

run 5 イメージの実測 (INVENTORY) に基づく統廃合。方針は
「界面互換を保ったまま中身を軽くする」。既存アプリの呼び出し方は
変えない。

## ネットワーク: NetworkManager → dhcpcd + wpa_supplicant

- 実測で NetworkManager が単独 PSS 約13MB (RAM 128MB の1割)。
  代替は dhcpcd (DHCP、gadget の usb0・有線・Wi-Fi 全部) +
  wpa_supplicant (dhcpcd のフックがインタフェース毎に起動)。
  合計 3〜4MB 級の見込み (次回イメージで brain-measure 実測)。
- **互換**: `nmcli` は /usr/local/bin/nmcli のシムが引き受ける。
  braindesk の設定画面が使う4コマンド (接続中 SSID、AP 一覧、
  radio on/off、connect) はそのまま動く (スタブで出力検証済み)。
  D-Bus の NM API を使うアプリはイメージ内に存在しないことを確認済み。
- ついでに直ったこと: 旧イメージは Install-Recommends=0 のため
  NM の推奨パッケージ wpasupplicant が入っておらず、**Wi-Fi は
  そもそも動かなかった**。wpasupplicant を明示パッケージにした。

## 常駐デーモンの整理 (無効化のみ、パッケージは残置)

| サービス | 判定 | 根拠 |
|---|---|---|
| avahi-daemon | 既定 off | mDNS はサーバ用途。brain-server 系プロファイルが要時に起動 |
| cron | 既定 off | ジョブ (logrotate/man-db/dpkg-backup/apt) は全て systemd timer が既にある |
| e2scrub_reap | 既定 off | 自前の timer がある。SD/eMMC での常駐スクラブ監視は不要 |
| triggerhappy | 既定 off | triggers.d が空 = 何も処理していない。電源キーは logind/braindesk の担当 |

## 次回イメージでの検証項目

- 起動後の常駐 PSS 合計と NetworkManager 分の消失 (brain-measure)
- dhcpcd で usb0 (gadget NCM) が DHCP を引けること (QEMU では
  gadget 自体がモデルの制約で未検証。実機または対応モデルで)
- 設定画面のネットワーク表示・接続がシム経由で動くこと
- 無効化したサービスの機能が timer 側で回っていること
  (logrotate.timer 等)
