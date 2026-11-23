# UI-STYLE-v2 — One UI / Material You / ChromeOS 寄りデザイン言語

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


位置づけ: ユーザー指示（2026-11-20）「One UI や Google（Pixel）や
Chromebook（ChromeOS）寄りの UI のほうがいい」を受けた方向転換文書。
汎用 HIG ベースで作った UI-REDESIGN.md のトークン体系は**方向として
不採用**とし、本書のトークン（`tk_theme_v2`）に置き換える。
数値は全て Web の一次・準一次ソースで確認した（§1）。記憶で決めた
値は使っていない。本書の実値はパイロット実装（シェル3画面＋設定
1画面）で検証し、全アプリ展開はユーザーの画像判断の後に別途行う。

対象ハード: 854×480（約179ppi、M22 前提訂正）、fbdev、GPU なし、タッチは1本指のみ、
RAM 128MB。GPU 前提の表現（ブラー、半透明の重ね合わせ）は使わず、
従来どおり「不透明面＋1px境界＋オフセット影」で代替する（§6）。

## 1. 調査ソース

### Samsung One UI
- Samsung 公式の One UI 紹介（公式サイト引用経由）: 「 decluttered
  screen」「最も関連するコンテンツを画面下半分に置き、片手操作を
  自然で快適に」— https://mspoweruser.com/samsungs-one-ui-update-brings-a-minimal-focused-interface-to-android/
- One UI の3原則 "Focus on the task at hand / Interact naturally /
  Be comfortable to view"、上部=視認領域（大きなタイトル）、
  下半分=操作領域という分離（Samsung デザインプログラムの解説）:
  https://karenwong.design/samsung-mobile.html
- One UI 8.5 の方向: 設定は角丸拡大＋余白増、検索バーは片手到達性
  のため画面下部へ移動、クイックパネルはピル型トグルで全面
  カスタマイズ可能、アプリはピル型アイコン/ボトムバーへ:
  https://www.androidauthority.com/one-ui-8-5-hands-on-3601412/
  https://samsung.gadgethacks.com/news/samsung-one-ui-85-leaked-major-galaxy-s25-redesign/
- One UI 7: クイック設定と通知の分離、角丸＋半透明レイヤー、
  フラットで角丸のアイコン: https://www.cashify.in/samsung-one-ui-7-update-eligible-devices-features-more

### Google Pixel / Material You（Material 3）
- M3 形状スケール（公式トークン）: none 0 / extra-small 4 /
  small 8 / medium 12 / large 16 / large-increased 20 /
  extra-large 28 / extra-large-increased 32 / extra-extra-large 48 /
  full 9999（ピル）。ボタン→full、カード→medium 12、FAB→large 16、
  ダイアログ/シート→extra-large 28、検索バー→full:
  https://github.com/lk153/vc-class/blob/HEAD/.claude/skills/material-3-skill/SKILL.md
  https://github.com/wollydev24/astra/blob/HEAD/guidelines.md
  https://superdesign.dev/blog/material-design-system
- M3 ベースライン色（seed #6750A4 のライトスキーム全ロール）:
  primary #6750A4 / primaryContainer #EADDFF / secondaryContainer
  #E8DEF8 / surface #FEF7FF / surfaceContainerLowest #FFFFFF /
  surfaceContainerLow #F7F2FA / surfaceContainer #F3EDF7 /
  surfaceContainerHigh #ECE6F0 / surfaceContainerHighest #E6E0E9 /
  onSurface #1D1B20 / onSurfaceVariant #49454F / outline #79747E /
  outlineVariant #CAC4D0 / error #B3261E:
  https://camouflage-dev.srctool.com/guide/skins/material-skin
  https://www.designref.ai/design-systems/material-design/colors
- M3 コンポーネント寸法: スイッチ 52×32（トラック full 角丸、
  ハンドル 未選択16/選択24）、検索バー 高さ56・surfaceContainerHigh・
  full 角丸、リスト行 56、タッチターゲット 48、ボタン 高さ40・full、
  スライダー（2024年版）トラック16＋ハンドルバ
...[truncated 13571 chars]ー、FAB 56・角丸16:
  https://www.appetiteui.com/docs/material-3
  https://github.com/gauravrajkagwaniya/native_adaptive_ui/blob/HEAD/doc/design-specs.md
  https://camouflage-dev.srctool.com/guide/skins/material-skin
- トーナル階層: ページ=surface、ナビバー=surfaceContainer、
  シート=surfaceContainerHigh、入力欄=surfaceContainerHighest:
  https://atlas.aarondesign.rocks/design-systems/material-3/

### ChromeOS
- Shelf の構成: 左に Launcher ボタン、ピン留め＋実行中アプリ、
  右端に時計＋Wi-Fi＋電池のステータス領域（ピル型バブル）、
  クリックで Quick Settings:
  http://chromeos.guide/ChromeOS-Guide/Using-ChromeOS/ChromeOS-Desktop/
  https://www.orduh.com/chrome-os-70-stable-features-redesigned-table-ui-floating-gboard/
- ChromeOS 117 で Material You 化: Quick Settings は大きな角丸
  タイル＋太いスライダー、パネル類はカード状の分離、角丸の拡大:
  https://umatechnology.org/chromeos-117-begins-roll-out-with-material-you-on-board
  https://www.gizchina.com/google-2/chromeos-in-material-you-looks-more-and-more-like-windows-11
- Launcher: 検索ボックス＋おすすめ/最近＋アプリグリッドが一体:
  http://chromeos.guide/ChromeOS-Guide/Using-ChromeOS/ChromeOS-Desktop/

## 2. 3スタイルの要点

### One UI（Samsung）
- 3原則: Focus on the task at hand / Interact naturally /
  Be comfortable to view。
- 画面の縦分離: 上部は「視認領域」（大きなタイトルと情報）、
  下半分は「操作領域」（ボタン、タブ、主要アクション）。
- 設定はグループ化されたカード＋大きな余白。検索バーは
  片手到達性のため下部へ（8.5）。
- クイックパネルはピル型トグルの集合。アイコンはフラットで
  角丸。全体に「呼吸する」余白を取り、線と装飾を減らす。

### Material You / Material 3（Google Pixel）
- 形状はトークン: full（ピル）＝ボタン/チップ/検索バー、
  medium 12＝カード、large 16＝FAB、extra-large 28＝シート。
  角丸は「対象が大きいほど大きい」。
- 色はロール制のトーナル階層（§1 のベースライン表）。面の
  階層は影ではなく色の段差で作る。選択状態は
  secondaryContainer、強調面は primaryContainer。
- スイッチ 52×32、スライダーは太いトラック（16）＋ハンドル、
  リスト行 56、最小タッチターゲット 48。

### ChromeOS（Chromebook）
- Shelf: 下部の帯。左=Launcher、中央=ピン留め＋実行中、
  右=ステータス（時計/Wi-Fi/電池）がひとつのピルに集約され、
  そこから Quick Settings を開く。
- Launcher: 検索ピル＋最近使ったもの＋全アプリグリッド。
- Quick Settings: 大きなピル型タイル＋太いスライダー
  （117 の Material You 化でこの形に統一された）。
- ウィンドウやパネルは大きな角丸のカードとして浮く。

## 3. 共通言語（抽出）

3者に共通し、本機へ採用する言語は次の6点:

1. **ピル形状が操作の合図**: ボタン、チップ、検索バー、
   トグルタイルは full 角丸。角丸の大きさが要素の格を表す。
2. **カード型グルーピング**: 関連項目は線で区切らず、
   角丸カード（面）の上に集める。面と背景はトーンの段差で分離。
3. **トーナルな面階層**: 背景 → コンテナ → 高コンテナ →
   最高コンテナ の段差で奥行きを作る（影は補助）。
4. **検索はピル**: ランチャーも設定も、検索は「カプセル型の
   入力欄」が第一要素。
5. **下部到達性**: 主要操作とシステム面（Shelf、QS、
   ランチャー）は画面下部から開き、下部に集める（One UI の
   操作領域、ChromeOS の Shelf の一致点）。
6. **余白と大きな見出し**: 見出しは大きく（表示 28）、
   余白は 4 の倍数のスケールで広めに取る。

## 4. Brainux 新トークン（tk_theme_v2 実値）

色は M3 ベースライン（seed #6750A4）ライトスキームのロールを
そのまま採用した。紫系トーンが Material You の基準色であり、
壁紙連動の動的色付け（Pixel/ChromeOS の機構）を行う場合も
このロール表が生成先になる。

| tk_theme フィールド | v2 値 | M3 ロール / 根拠 |
|---|---|---|
| bg | #FEF7FF | surface |
| surface | #FFFFFF | surfaceContainerLowest（カード面） |
| surface_alt | #E6E0E9 | surfaceContainerHighest（入力欄・スライダー不活性・スイッチ OFF） |
| desktop_bg | #ECE6F0 | surfaceContainerHigh（無地壁紙の既定） |
| text | #1D1B20 | onSurface |
| text_dim | #49454F | onSurfaceVariant |
| accent | #6750A4 | primary |
| on_accent | #FFFFFF | onPrimary |
| container | #EADDFF | primaryContainer（QS タイル ON 面） |
| selection | #E8DEF8 | secondaryContainer（選択行・選択チップ） |
| border | #79747E | outline |
| border_line | #CAC4D0 | outlineVariant |
| focus | #6750A4 | primary |
| danger | #B3261E | error |

| 寸法フィールド | v1（現行） | v2 | 根拠 |
|---|---|---|---|
| unit | 8 | 8 | M3 の 4dp グリッド（8 が既定ステップ） |
| radius（小コントロール/カード） | 4 | 12 | M3 medium（カード） |
| radius_overlay（浮動面） | 8 | 28 | M3 extra-large（シート） |
| radius_button | （なし→radius） | 999 = full | M3 ボタン/チップは full |
| radius_card（グループカード/QSタイル） | （なし） | 24 | M3 large〜extra-large の間、One UI カードの強い角丸 |
| radius_list（リスト面） | （なし→四角） | 24 | グループカードと同格 |
| control_h | 48 | 48 | M3 最小タッチターゲット 48 |
| row_h / row_h2 | 48 / 64 | 56 / 72 | M3 リスト行 56、2行 72 |
| font_display / title / heading / body / caption | 28/20/16/16/13 | 同左 | One UI の大きな見出し＋M3 タイプスケールの範囲内 |
| slider_track_h | （なし→4） | 16 | M3 2024 スライダー / ChromeOS の太いスライダー |
| field_filled | （なし→枠線） | 1 | M3 充填フィールド＋検索バー full |
| panel_margin | （なし→0） | 8 | 浮動 Shelf の画面端マージン |
| style | 0 | 1 | v2 判定フラグ（実装用） |

### スイッチ / スライダー（寸法は既存実装が既に M3 と一致）
- スイッチ: 52×32 トラック（full）、ハンドル 24（ON 時）。
  既存 tk_widgets の実装寸法が M3 公式と一致するため変更なし。
  色だけがテーマ追随（ON=#6750A4、OFF=surfaceContainerHighest）。
- スライダー: トラック 16（v2）、不活性側 surface_alt、
  活性側 accent、ハンドルは白丸 28＋accent 中心点
  （ChromeOS QS の丸ハンドル寄り。M3 Expressive のバーハンドルは
  本機の既存ヒット判定と合わず採用しない）。

### 面と影の代替定義（GPU なし）
- ブラー/半透明は使わない。浮動面は不透明 surface、
  境界は outlineVariant 1px の角丸ストローク（新設
  tk_canvas_stroke_round_rect）、影は (4,4) オフセット・
  黒 25% の角丸シルエット1回だけ（従来の backdrop 定義を継承）。
- 面の階層は色で作る: bg（#FEF7FF）の上に surface（白）の
  カード、カードの上に surface_alt（#E6E0E9）の部品、
  状態面に container / selection。

## 5. 対応表（3スタイル → 本機の部品）

| 要素 | One UI | Material You / Pixel | ChromeOS | Brainux v2 での形 |
|---|---|---|---|---|
| 下部バー | （下部操作領域の思想） | ナビバーのトーン面 | Shelf | 浮動 Shelf: 画面端 8px マージン、角丸 28 のピル状バー、左 Launcher グリッドボタン＋アイコンタスク、右にトレイ＋太字時計 |
| ランチャー | ドロワ＋検索下部 | 検索バー full | 検索＋最近＋グリッド | 浮動パネル（角丸 28）最上段に検索ピル（高さ 48）、ピン留めグリッド、最近リスト（角丸カード）、カテゴリチップはピル、フッター |
| クイック設定 | ピル型トグル群 | QS タイル（primaryContainer） | ピルタイル＋太いスライダー | Wi-Fi は full 角丸タイル（ON 時 container 面）、輝度/音量はトラック 16 のスライダー、ボタンはピル |
| 設定アプリ | 大見出し＋グループカード＋検索下部寄り | 2ペイン＋行 56/72 | 左ナビ＋右ページ見出し | 上部に表示 28 の「設定」大見出し、検索ピル、ページ一覧と設定行は角丸 24 のグループカード、右ペイン先頭にページ見出し |
| ボタン | 角丸大 | full | 角丸大 | full（ピル）。面は surface、枠は角丸ストローク |
| チップ | ピル | full | ピル | full。選択時は selection 面 |
| 入力欄/検索 | 角丸 | 充填＋検索は full | 検索ピル | 充填（surface_alt）＋ full 角丸、枠線なし |
| リスト | グループカード内 | 行 56/72、選択は secondaryContainer | カード | 角丸 24 の面、選択は selection のピル型インセット |
| スイッチ | ピル | 52×32 | ピル | 既存実装のまま（M3 寸法一致） |
| アイコンセル | フラット角丸アイコン | 角丸 | 角丸 | 押下ウォッシュは radius 12 |

## 6. 本機制約への適応

- **GPU なし / fbdev**: ブラー・半透明合成は使わない（§4 の
  代替定義）。角丸ストロークは行単位描画で、追加コストは
  浮動面の輪郭だけに限定する。
- **1本指タッチ**: 最小ターゲット 48 を維持。ピル化で
  見た目の当たり判定と実際の rect が一致するようにする
  （角の欠けは半径分のみで、行・ボタン rect は変えない）。
- **128MB**: 新規の画像・フォントは増やさない。グリフ
  キャッシュ増分は v1 再設計時実測（+164KiB）から不変。
  テーマは静的データのみ（追加ヒープなし）。
- **時刻不信・オフライン**: 見た目だけの変更で、永続化や
  期限ロジックには触れない。
- **eMMC ノータッチ / SD 本拠**: 変更は rebuild/ 内のコードと
  テーマだけ。配置先の変更なし。

## 7. パイロット範囲（本書に基づく今回の実装）

- ツールキット: `tk_theme_v2` 新設＋既定テーマ差し替え API
  （`tk_theme_set_default`、製品既定は従来テーマのまま）、
  角丸ストローク原語、ボタン/入力欄/スライダー/リストの
  テーマ駆動化（v1 テーマでは従来描画と一致）。
- シェル: デスクトップ（浮動 Shelf）、ランチャー（検索ピル＋
  ピルチップ）、クイック設定（ピルタイル＋太いスライダー＋
  ピルボタン）。
- 設定アプリ: 大見出し、検索ピル、グループカード化、
  ページ見出し。
- 撮影: screenshots ハーネスに BD_SHOT_V2=1 を追加し、
  screenshots/v2/ に4画面を出す。

未展開（ユーザーの方向判断の後）: 他アプリ全般、WM 枠、
グリーター、OSK、アシスト、通知シート、ダークテーマ、
壁紙連動の動的色付け、設定検索バーの下部配置（One UI 8.5 式。
本機は物理キー併用でフォーカス移動が要るため要検討）。

## 7.5 精錬フェーズの決定（2026-11-20、REPORT-ui-v2-refine.md）

パイロットへのユーザー評価（レイアウトが荒い）を受けた精錬で
確定した差分。本節が §5 の記述に優先する。

- **設定の検索は全幅ピル**: サイドバーから出し、大見出しの
  直下に全幅で置く（One UI の viewing/interaction 分離＋
  ChromeOS 設定の形）。ページ見出しは右ペイン先頭に単独で
  置き、両ペインは同じ y から始める。間隔は全て 8、外周 16。
- **チップ**: 高さ 40、間隔 8/8（ピッチ 48）、未選択も
  surface_alt の面を持つ。タッチターゲット 40 は 48 規定の
  例外として明記（48 化すると3行になり中段が潰れる）。
- **スライダーハンドル**: 白丸には 1px 輪郭リング必須
  （白い面の上では中心点しか見えず「小さな点」に化ける）。
- **QS スライダー行のアイコン**: テーマファイルではなく
  シンボリックグリフ（単色の太陽/スピーカー）を使う。
  Papirus の brightness ファイルは多色円盤で小サイズでは
  読めないため不採用。
- **ランチャーのフッター**: アバター円盤＋名前のユーザーチップ
  形（素のラベルを浮かせない）。ホーム節の下の残量には
  全アプリグリッドを流し込み、空白の void を残さない。
- **アイコンセルのラベル**: セル幅で省略描画（…）する。
  固定幅セルで長い名前を中央描画すると両側にはみ出す。
- **アシスト FAB（v2）**: Shelf の角グリッドにドック
  （右マージン 8、下 8）。色はテーマ accent 追随。

## 8. 実装メモ（コード対応）

- テーマ追加フィールド（tk_theme 末尾に追加、既存初期化子に
  影響なし）: container / radius_button / radius_card /
  radius_list / slider_track_h / field_filled / panel_margin /
  style。tk_theme_light では radius_button=-1（従来動作）、
  他は従来値を入れて v1 の描画を不変に保つ。
- v2 判定は `tk_theme_default()->style == 1`。製品コードの
  分岐はこの1点に集約する。
- 角丸ストロークは半径 8 以下では従来の四角ストロークを
  使う（v1 のピクセルを変えないため）。
