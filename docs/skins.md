# braindesk スキン定義ファイル (.skin)

braindesk のスキンはビルトイン 17 種のほかに、ユーザーがテキスト
ファイルで追加できる。形式は brain-modes / brainreg と同じ
`key=value` 形式で、拡張子は `.skin`。起動時に読み込まれ、
ビルトインの後ろにそのままの名前で設定アプリのスキン一覧へ並ぶ。

## 置き場所と優先順位

探索順 (先に見つかったものが優先。同名ファイルは後の場所のものが
無視される):

1. 環境変数 `BRAINDESK_SKINS_DIR` が指すディレクトリ
2. `~/.config/braindesk/skins/` (ユーザー定義)
3. `/usr/share/braindesk/skins/` (工場出荷サンプル)

各ディレクトリ内ではファイル名の辞書順に読み込む。読み込みは
プロセス起動時に一度だけ行うので、ファイルを追加・変更したら
braindesk (シェル/グリーター/アプリ) の再起動が必要。

壊れたファイル (読めない、認識できるキーが一つもない) は無視して
続行する。未知のキーも無視する。`#` と `;` で始まる行はコメント。

## 形式

```ini
# コメント
name=マイ・サンセット
base=Brainux
layout=bd
chrome=modern
radius=12
panel_h=44
accent=#FF7A59
```

| キー | 値 | 説明 |
|---|---|---|
| `name` | 文字列 (63 バイトまで) | 一覧に出る名前。省略時はファイル名 (拡張子なし) |
| `base` | ビルトインの名前または番号 | 継承元。省略時は `layout` の代表ビルトイン (bd→Brainux、mac→macOS、classic→クラシック Mac、win11→Windows 11、win→Windows 10) |
| `layout` | `bd` / `mac` / `classic` / `win11` / `win` (数値 0–4 でも可) | バー/ドック/ウィンドウ配置の系統 |
| `chrome` | `modern` / `bevel` / `luna` / `aero` / `metro` / `dock` (数値 0–5 でも可) | ウィンドウ枠の描画様式 |
| `radius` | 0–32 | 角丸の半径 (px) |
| `panel_h` | 0–80 | バーの既定の高さ (px)。0 はシェルの既定に従う |
| 色キー | `#RRGGBB` | 下表参照 |

色キー (すべて任意。指定しない色は `base` から継承する):

`bg`, `surface`, `surface2`, `surface3`, `line`, `text`,
`text_dim`, `text_faint`, `accent`, `accent2`, `panel`,
`wall_top`, `wall_bottom`, `wall_glow`

- `bg`〜`line`: 画面と面の階層色、`text` 系: 文字色
- `accent` / `accent2`: 強調色
- `panel`: パネル/タイトルバーの地色
- `wall_top` / `wall_bottom` / `wall_glow`: 壁紙グラデーション

不正な色値 (6 桁の 16 進数でない) はそのキーだけ無視される。

## 選択の保存

選んだスキンは従来どおり brainreg の `desktop.skin` (インデックス)
に保存される。ファイル定義スキンのインデックスはビルトインの
総数 (17) 以降。スキンファイルを削除すると、そのインデックスを
指していた保存値は範囲外として先頭のスキンに戻る。

## サンプル

`braindesk/skins/sample-sunset-custom.skin` が同梱され、イメージ
では `/usr/share/braindesk/skins/` に入る (配線:
`os-brainux/override-pre.sh` が `braindesk/skins` を chroot へ
コピーし、`os-brainux/setup_brainux.sh` がインストールする)。
自分のスキンを作るときはこれを `~/.config/braindesk/skins/`
へコピーして書き換えるのが早い。

## 実装

- ローダ: `braindesk/src/bui_skin.c` の `bd_skin_load_files()`
  (冪等。`bd_skin_load()` / `bd_skin_get()` / `bd_skin_count()`
  から自動で呼ばれる)
- 件数・取得: `bd_skin_count()` / `bd_skin_at(idx)`。
  互換のため `BD_SKINS` / `BD_NSKINS` も総件数を指し続ける
