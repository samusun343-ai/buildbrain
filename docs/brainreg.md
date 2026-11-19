# brainreg — 中央設定ストア設計 (レジストリ/環境変数的な設定管理)

設定がファイルごとに散らばっている (スキン、パネル、モード、アプリ個別)
状態をやめ、Windows のレジストリ + 環境変数のように「1か所で見て、
1か所で変える」仕組みにする。既存アプリは従来ファイルも読み続けるので
壊れない (移行は段階的に)。

## ストア

- 実体はテキストファイル。人間が読めて、git でも差分が見える形にする。
  - システム: `/etc/brainreg.d/*.conf`
  - ユーザ: `~/.config/brainreg.d/*.conf` (同キーはユーザ側が勝つ)
- 形式は階層キー + key=value:

```ini
[desktop]
skin = ocean
panel_height = 52
assist = true

[network]
wifi = on

[audio]
volume = 0.45
key_sound = false
```

- 優先順位: 環境変数 `BRAINREG_<SECTION>_<KEY>` (大文字化) > ユーザ > システム > 既定値。
  環境変数で一時上書きできるのが「環境変数的」な部分。

## 道具

- `brainctl`: CLI。get / set / list / unset / export。
  `brainctl export` は `BRAINREG_DESKTOP_SKIN=ocean` のような
  シェル変数形式で出すので、セッション開始時に読み込めば
  アプリから getenv() でも引ける。
- `libbrainreg` (小さな C ライブラリ): braindesk 本体とアプリが使う読み書き API。
  読みは mmap せず単純パース (設定ファイルは数 KB)。
- `bd-regedit`: GUI エディタ。左にセクションのツリー、右にキー一覧、
  値の編集は型付き (真偽はトグル、数値はスライダ/入力、文字列は OSK)。
  変更は即保存され、対象アプリは再起動か SIGHUP で反映。

## 移行計画 (互換を壊さない順序)

1. brainctl + ストア + libbrainreg を入れる。既存の設定ファイル
   (skin / panel / brain-mode の状態) を初回起動時に取り込む移行コードを置く。
2. braindesk 本体 (shell/greeter) の読み先を brainreg に寄せ、
   旧ファイルへは互換のため当面ミラー書きする。
3. アプリ群を順に移行。bd-pkg のような管理系は brainreg を正とする。
4. X セッション側のアプリ (JWM 等) は `brainctl export` を
   Xsession で評価して環境変数として渡す。

## やらないこと

- バイナリ形式のストア (壊れたら終わり、編集もできない)。
- デーモン常駐 (設定読み書きに常駐プロセスは要らない。変更通知が
  要る場面だけ inotify で足りる)。
