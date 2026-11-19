# braindesk デザインシステム

2026-11-20 の方針転換で、OS 再現スキン（macOS / Windows 風）は廃止し、
braindesk は単一の独自デザインに統一する。本書はその正本であり、
数値・色・アイコン・コンポーネントの規格はすべてここで定義する。
個別の実装が本書と食い違う場合は、本書を直すか実装を直すかして
必ず一致させる（食い違いを放置しない）。

設計の目標は、既存 OS の模倣ではなく「一貫性があり、操作しやすく、
アプリが増えても破綻しない独自 DE」である。構成の考え方は
Windows 11 のスタート（中央配置・検索・ピン留め）、macOS の
アプリグリッドと Dock、GNOME / KDE のデザインシステムの一貫性を
参考にするが、見た目の複製はしない。

## 1. デザイントークン

| トークン | 値 | 用途 |
|---|---|---|
| 基本余白単位 | 8px（半単位 4px） | すべての余白・間隔はこの倍数 |
| アイコン表示サイズ | 48×48px | ランチャー / Dock / アプリ一覧 |
| アイコンマスター | 128×128px PNG | `braindesk/assets/app/` に格納 |
| アイコン外形 | 取得セット（Papirus）のデザインに従う | §3 参照。自前描画はしない |
| アプリ名ラベル | 12–13px | アイコン下のラベル |
| パネル角丸 | 12px | シート / カード / 検索欄の標準半径 |
| 検索欄の高さ | 40px | ランチャー上部の検索 |
| アニメーション | 150–200ms | 開閉・切替の標準時間 |
| パネル（タスクバー）高さ | 42px | 現行値。変更時はここを更新 |

## 2. 色

基調は braindesk オリジナルパレット「オーシャン」（`bui_skin.c` の
ビルトイン 0 番）。役割で定義し、画面ごとに勝手な色を増やさない。

| 役割 | 色 | 値 |
|---|---|---|
| bg | アプリ背景 | `#10141C` |
| panel | パネル / タスクバー | `#121721` |
| surface | カード・シート面 | `#1C222E` |
| surface2 | 入れ子面・通常ボタン | `#262E3D` |
| surface3 | 強調面・ホバー | `#323C4E` |
| line | 境界線 | `#3E4A5E` |
| text | 主テキスト | `#EEF2F7` |
| text_dim | 副テキスト | `#A8B2C2` |
| text_faint | 補助テキスト | `#6E798C` |
| accent | 主要アクション・選択・フォーカス | `#2D9CDB` |
| accent2 | 副次アクセント・成功寄りの強調 | `#20C997` |
| danger | 削除・電源など危険操作 | `#E05252` |
| ok | 正常・接続済み | `#51CF66` |
| warn | 注意・警告 | `#FAB005` |
| wall_top / wall_bottom | 壁紙グラデーション | `#0D1B2E` / `#0A2C36` |
| wall_glow | 壁紙の差し色 | `#20C997` |

使い方の規則:

- アクセントは 1 画面で主役 1 色（accent か accent2 のどちらか）に絞る。
- テキストの階層は text / text_dim / text_faint の 3 段まで。
- danger / ok / warn は意味専用にし、装飾に流用しない。
- 面の階層は bg → surface → surface2 → surface3 の順に明るくなる。

## 3. アイコン規格

統一アイコンセットを `braindesk/assets/app/` に置く。旧来の
OS 再現用アイコン（`assets/mac|win11|xp/`）は新デザインでは使わない。

- 取得方針（2026-11-20 ユーザー指示）: アイコンは自前生成しない。
  web 上の統一された高品質なアイコンセットから取得する。
- 採用セット: **Papirus Icon Theme**（カラーのフルアイコンセット。
  外形・色彩・描画密度がセット内で統一されている）。
  - 取得元: https://github.com/PapirusDevelopmentTeam/papirus-icon-theme
  - ライセンス: GPL-3.0。帰属表示は `braindesk/assets/app/NOTICE` に置く。
- 単一セット主義: 原則として Papirus のみから選ぶ。セット外の
  アイコンを混ぜる場合は理由を本書に記録すること（現時点で混在なし）。
- 選定基準: アプリのメタファーが 48px 表示で判別できること。
  似たメタファーを複数タイルで重ねないこと（例: 音楽は音符、
  音量はスピーカー、と競合させない）。
- 変換手順: 上流 SVG をラスタライズ（512×512 → 128×128 へ縮小）
  → 256 色量子化（FASTOCTREE + Floyd–Steinberg、
  `optimize-icons.py` と同じ量子化手順）。テーマ内蔵のパディングを
  保つためトリミングはしない。1 枚あたり約 2.2KB。
- 一般アプリ / システム機能の区別はアイコンの背景色では行わず、
  ランチャー内のグルーピング（§6）で視覚的に区別する。
- 個別の対応は §8 のマニフェスト（取得元パス付き）を正とする。

## 4. コンポーネント規格

- **ボタン**: 角丸 12px。プライマリ = accent 塗り + 白文字、
  通常 = surface2、危険 = danger。押下/フォーカスは accent 枠で示す。
- **カード / シート**: surface 面、角丸 12px、境界は line 1px。
  影は控えめにし、面の明度差で階層を基本とする。
  影の標準値（DY/ぼかし/濃度）は libbrainui の `BD_SHADOW_*` トークンに
  codify 済みで、カードは適用済み。パネルへの適用は今後の課題。
- **検索欄**: 高さ 40px、surface2 背景、角丸 12px。
  ランチャーではグリッドと視覚的に一体のブロックとして置く。
- **パネル（タスクバー）**: panel 色。起動中アプリ、通知、トレイを置く。
  起動中はアイコン下のドット/下線で示す。
- **ランチャー**: 検索（40px）+ ピン留め + 最近使用したアプリ。
  アプリの並びは分類と関連性で決める（関連機能は近接配置）。
- **アプリ一覧**: 全アプリをカテゴリ付きで表示。ページ切替を持ち、
  グリッドは画面全体のバランスで配置する（上部偏在にしない）。
- **Dock**: お気に入りと起動中アプリのみ。全アプリを並べない
  （アプリ一覧との役割重複をなくす）。
- **クイック設定**: 明るさ / 音量 / ネットワーク / 電源 / モード切替を
  1 パネルに集約する。

## 5. 推奨構成

1. デスクトップ: 壁紙とウィンドウ
2. タスクバー: 起動中のアプリ、通知、システムトレイ
3. ランチャー: 検索、ピン留め、最近使用したアプリ
4. アプリ一覧: 全アプリとカテゴリ
5. Dock: お気に入りと起動中アプリ
6. クイック設定: 明るさ、音量、ネットワーク、電源

## 6. アプリ分類

一般アプリとシステム機能は分類し、ランチャー内のグルーピングで
視覚的に区別する（アイコン自体は §3 の単一セットで統一する）。

**一般アプリ**（9）: ファイル / ブラウザ / 音楽 / メモ / 録音 /
時計 / 電卓 / 辞書 / タスク

**システム機能**（8）: 設定 / デバイス / 明るさ / 音量 / ホーム /
パッケージ / 端末 / ゴミ箱（ファイル管理ユーティリティ）

## 7. 改善の優先順位（2026-11-20 評価文書より）

1. アイコンデザインの統一 — 本書 §3 と `assets/app/` で土台を作成済み
2. アプリとシステム機能の分類 — §6
3. Dock とアプリ一覧の役割分担 — §4
4. 余白とグリッドの調整 — §1 トークン適用
5. 検索機能の改善
6. アニメーションの調整（150–200ms）
7. 影、透明度、細かな視覚効果の調整

## 8. アイコンマニフェスト

`braindesk/assets/app/`（→ `/usr/share/braindesk/assets/app/`、
既存の再帰インストールで配布される）。シェル側の参照切替は別作業。

取得元は Papirus Icon Theme リポジトリ内のパス（`Papirus/` 配下）。

| ファイル | アプリ | 分類 | 対応プログラム | 取得元（Papirus） |
|---|---|---|---|---|
| files.png | ファイル | 一般 | bd-files | 64x64/places/folder.svg |
| tasks.png | タスク | 一般 | bd-tasks | 64x64/apps/org.gnome.Todo.svg |
| notes.png | メモ | 一般 | bd-notes | 64x64/apps/org.gnome.Notes.svg |
| clock.png | 時計 | 一般 | bd-clock | 64x64/apps/gnome-clocks.svg |
| calc.png | 電卓 | 一般 | bd-calc | 64x64/apps/accessories-calculator.svg |
| player.png | 音楽 | 一般 | bd-player | 64x64/mimetypes/audio-x-generic.svg |
| recorder.png | 録音 | 一般 | bd-recorder | 64x64/apps/org.gnome.SoundRecorder.svg |
| web.png | ブラウザ | 一般 | bd-web | 64x64/apps/web-browser.svg |
| dict.png | 辞書 | 一般 | （未実装） | 64x64/apps/accessories-dictionary.svg |
| settings.png | 設定 | システム | シェル設定画面 | 64x64/apps/preferences-system.svg |
| devices.png | デバイス | システム | bd-devices | 64x64/devices/cpu.svg |
| pkg.png | パッケージ | システム | bd-pkg | 64x64/apps/system-software-install.svg |
| terminal.png | 端末 | システム | 端末アプリ | 64x64/apps/utilities-terminal.svg |
| home.png | ホーム | システム | シェル | 64x64/places/user-home.svg |
| brightness.png | 明るさ | システム | シェル | 48x48/status/weather-clear.svg |
| volume.png | 音量 | システム | シェル | 32x32/status/audio-volume-high.svg |
| trash.png | ゴミ箱 | システム | bd-files / Dock | 64x64/places/user-trash.svg |

選定メモ:
- player.png: `org.gnome.Music` はスピーカー図柄で音量タイルと
  競合するため、音符の `audio-x-generic` を採用した。
- brightness.png: Papirus に専用の明るさカラーアイコンが無いため、
  太陽の `weather-clear` を採用した（次点はモニタ図柄の
  `preferences-desktop-display`）。

セット実測（2026-11-20 取得）: 17 枚、合計 37,058 バイト（約 36.2KiB）。
取得・変換手順は §3。ファイル名は自前生成版と同一に保ち、
シェル側の参照は変更不要。
