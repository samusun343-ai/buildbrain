# REPORT-toolkit-complete.md — libbrainui2 M7 対応報告

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


対象: `~/workspace/buildbrain-work/rebuild/` の GUI ライブラリ
libbrainui2（include/・src/・tests/ のみを変更。他ラインの領域は
未変更）。repo へは push していない。

## 0. 結論の置き方（過大評価の禁止について）

本報告のファイル名はタスク指定によるもので、「完成」の主張では
ない。ユーザー基準（2026-11-20 17:34）の細分化マトリクス
（コンポーネント × モード × 観点6種、AUDIT-toolkit.md §2）で
数えると、**適用セル 458 中、テスト検証済み（●）は 79 セル
（17.2%）**に留まる。今回の M7 で到達したのは次の範囲である:

- AUDIT §4 の残欠落16項目すべてに**実装と新規テスト**が付いた
  （test_tk6 = 205 件、全緑）。
- 回帰ゼロ: ツールキット 1,117 / アプリ 1,048 / シェル 588 /
  OSK 319（いずれも自分の実行、§4）。
- 両モード（Android / WinUI）× light/dark の4テーマが実在し、
  同一ウィジェットツリーを実行時テーマ切替で描き分けるテストが
  ある。

観点単位（全状態の見た目、アニメーション、キー操作の回り方等）
の検証は大半が ◐（部分検証）/ ○（実装のみ）であり、WinUI 側は
部品別寸法仕様の未取得（△ 32 セル）が残る。次に進めるべきは
この ◐/○/△ を1セルずつ潰す作業であり、本報告はその現状申告
である。

## 1. §4 項目ごとの対応表（実装内容 / Web 出典 / テスト証拠）

出典はすべて 2026-11-20 に実際に取得したもの。取得できなかった
ものは「未確認」と明記し、推測で埋めていない。

| # | §4 項目 | 実装内容 | Web 出典 | テスト証拠（test_tk6、自分の実行） |
|---|---|---|---|---|
| 1 | ダークテーマ | tk_theme_v2_dark（M3 ダーク実値）、tk_theme_winui_dark（WinUI ダーク実値）を新設。全テーマに M7 トークン群を実値で設定 | m3.material.io の color roles（ダーク baseline 値は複数資料で一致を確認）/ WinUI Common_themeresources 系の複製資料（fluent_ui / wpfui） | 4テーマのトークン実値チェック＋同一ツリーの4テーマ描画ピクセル比較 |
| 2 | 検索バー専用部品 | TK_W_SEARCHBAR。候補は Menu オーバーレイ流用（docked SearchView 形）、WinUI では AutoSuggestBox 形。編集は tk_text_edit_key 共有 | m3.material.io Search（バー高 56dp、候補コンテナ高 min 240dp / max 画面 2/3） | 入力→候補表示→選択で cb 発火、閾値・絞り込みの動作チェック |
| 3 | Card 専用種別 | TK_W_CARD（elevated / filled / outlined、clickable、子配置） | m3.material.io Cards（3種別の面トークン、角丸 12dp、padding 16dp） | 3種別の面ピクセル、クリック発火、子配置の配置結果 |
| 4 | ドラッグ＆ドロップ | ルート駆動 DnD（祖先の drag_mime を辿る arm、12px で開始、ゴースト描画、drop_cb 配送）＋ ListView 行ドラッグ（UP で move_row） | developer.android.com Drag and Drop（source/target/shadow/events、長押し開始） | ウィジェット間ドロップ配送、ListView 行順の入れ替わり、クリック抑制 |
| 5 | サムネイル IDCT スケーリング | 自前ベースライン JPEG デコーダ src/tk_jpeg.c（Huffman、4:4:4/4:2:2/4:2:0/4:1:1、グレー、リスタートマーカー、スケール IDCT 1/2・1/4・1/8）。tk_image_load_thumbnail に配線し tk_image_last_decode_peak() を実体化 | JPEG 仕様は ITU T.81 の公知形式として実装（外部ライブラリ依存は追加していない。消費者は -lm のみでリンクする制約のため） | §3 のピーク実測と参照照合（PIL/libjpeg）を test_tk6 に組み込み |
| 6 | 共有クリップボード | tk_clipboard_*（プロセス内単一クリップ）。textcore プロバイダとして TextField/TextView/SearchBar 生成時に接続 | developer.android.com Clipboard（ClipboardManager が ClipData を1つ保持） | 部品間でのコピー→貼り付けの往復 |
| 7 | バリデータ/オートコンプリート | TextField: tf_validator（FOCUS_OUT と明示 validate で実行）、prefix/suffix、tf_max_len＋文字数カウンタ、候補補完（tf_threshold 既定1） | developer.android.com AutoCompleteTextView（threshold、Validator IF）/ m3.material.io Text fields（prefix/suffix、カウンタは supporting text と並置） | バリデータの真偽と is_error 同期、最大長での入力キャップ、カウンタ表示、候補選択 |
| 8 | i18n / アクセシビリティ | tk_i18n（完全一致→言語→デフォルトのフォールバック、複数形 .one/.other、ja は常時 other）。tk_a11y（ノード収集、content description、Android クラス名と UIA 型名の両マッピング、announce） | developer.android.com Localization（デフォルト＋ロケール別リソースのフォールバック）/ Accessibility（AccessibilityNodeInfo、contentDescription、role、live region） | ロケール切替・フォールバック・複数形、a11y ツリーの role/label/state/bounds、スライダー状態文字列 |
| 9 | 非モーダル / full-screen Dialog | tk_dialog_show_modeless（スクリムなし・背後へイベント通過）、tk_dialog_set_fullscreen（角丸0・ヘッダ帯） | m3.material.io Dialogs（basic: 角丸 28dp・幅 280–560dp・padding 24dp。full-screen: 角丸 0・ヘッダ 56dp） | modeless の背後クリック通過、fullscreen の寸法と閉じ方、modal との排他 |
| 10 | TreeView モデル IF 一般化 | tk_tree_model（child_count/child_id/label コールバック）。内部は全ノード展開＋並行配列で model id 変換、展開状態は model id 集合で保持、model_refresh で選択・展開を保持 | Android の Adapter/ListAdapter 的モデル分離（developer.android.com のリスト系ガイド）と M3 リストの振る舞い | モデル経由の構築・展開・選択・データ変更後の refresh 保持 |
| 11 | ボタンへの子配置 | tk_measure/tk_arrange でボタンの子を右寄せ・縦中央に配置（ブラウザのタブ内 × ボタンの形） | 実物 UI 調査（DESIGN-apps-envfit.md 系）に基づく配置要件。数値仕様は部品固有ページ未確認のため △ 扱い（AUDIT §2 #1 E 列） | 子ボタンの配置矩形とクリック配送 |
| 12 | FAB menu / Segmented / Carousel / Drawer・Rail / Bottom sheet | 専用種別 TK_W_SEGMENTED / TK_W_CAROUSEL / TK_W_NAVRAIL / TK_W_DRAWER / TK_W_SHEET。FAB menu は合成（スクリム=全画面 LABEL オーバーレイ） | m3.material.io 各部品ページ（Segmented: 高 40dp・選択時 secondary container＋チェック。Drawer: 幅 360dp・形状 0/16/16/0。Bottom sheets: ハンドル・上 padding 22dp・最大幅 640dp・上マージン 72dp。FAB menu（Expressive）: items 56 高・間隔 4。Rail と Carousel の数値は画像内のみで**未確認**） | 各部品の生成・選択・開閉・ドラッグの主要経路。寸法は取得できた範囲のみ照合 |
| 13 | Slider Range / 円形 determinate | デュアルハンドル（fvalue2、掴み分け、キーでのアクティブハンドル操作）、ドラッグ中の value indicator バブル（56×51px、inverse 色）。TK_W_RING（ドット弧） | m3.material.io Sliders（Range 形、value indicator 44dp 高×48dp 幅・inverse 色、XS トラック 16dp・ハンドル 44dp）/ Progress（circular determinate） | lo/hi の独立操作・クランプ、バブルのピクセル、Ring の sweep ピクセル（0.5 で下端含む） |
| 14 | ColorPicker hex・alpha | hex 欄（6桁=不透明、8桁=alpha 指定）と alpha スライダ行。子順序は既存消費者互換のため [grid, preview, buttons, hex, alpha] を維持 | M3 に専用 ColorPicker 仕様はなく、Android 慣例（#RRGGBB / #RRGGBBAA）に従った旨を明記する | hex 入力→値反映、alpha 行の連動、alpha 無効時の不透明化 |
| 15 | Snackbar スワイプ消去 | 横スワイプ（しきい値超過）で dismiss し on_dismiss 発火。inverse トークンで描画 | m3.material.io Snackbar（色は inverse surface 系。スワイプ消去の明記は specs ページに**なし** — Android 実装慣例として実装した旨を明記） | スワイプ量としきい値、アクション/タップ消去との併存 |
| 16 | inverse トークン | inverse_surface / inverse_on_surface / inverse_primary を4テーマ全てに実値で追加し、Snackbar・Toast・value indicator が使用 | m3.material.io Color roles（inverse 3ロール、surface container 5段ラダー） | トークン実値チェック、Snackbar の面ピクセル |

### 実装中に発見・修正した実在バグ（回帰テストで検出）

AUDIT §4 末尾に記録。要点: tk_root_free の use-after-free
（ASan で確認）、候補メニューの即時クローズ（フォーカス移動の
副作用）、validate 結果と is_error の不整合、a11y スライダー
状態の常時 "0%"、v2_dark.surface の誤値、BottomBar 選択フェイス
のテーマ色焼き込み（実行時切替に追随しない → 動的フェイス化）、
ColorPicker 6桁 hex での alpha 残留。

## 2. 能力マトリクスの最終状態（この報告時点）

正本は AUDIT-toolkit.md §2（44 コンポーネント × 2 モード ×
観点6種）。機械集計の結果:

| モード | 適用セル | ● 検証済み | ◐ 部分検証 | ○ 実装のみ | △ 仕様未確認 | ✕ 未実装 |
|---|---|---|---|---|---|---|
| Android | 229 | 75（32.8%） | 99 | 51 | 3 | 1 |
| WinUI | 229 | 4（1.7%） | 89 | 103 | 32 | 1 |
| 合計 | 458 | 79（17.2%） | 188 | 154 | 35 | 2 |

✕ の2件は両モード共通で Split button の chevron 180° 回転
（canvas に回転プリミティブがないための描画基盤の制約）。
△ の大半は WinUI の部品別寸法（Microsoft Learn の部品別ページ
未取得）と、M3 の rail / app bar / carousel の画像内数値。

## 3. JPEG スケールデコードの実測（自分の実行）

フィクスチャは PIL で生成し、参照デコードは PIL/libjpeg
（全デコード＋box 縮小）。test_tk6 に組み込んだ照合結果:

| 対象 | スケール | 平均誤差 | 最大誤差 |
|---|---|---|---|
| grad320（320×240, 4:2:0, q90） | 1/1 | 0.232 | 3 |
| 同上 | 1/2 | 0.632 | 11 |
| 同上 | 1/4 | 1.587 | 29 |
| 同上 | 1/8 | 4.288 | 48 |
| big1600（1600×1200, q88）→ 200×150 | 自動選択 | 1.136 | 50 |

メモリピーク: big1600 のサムネイル生成で **285,000 bytes**
（従来の stb 全デコード経路 15,480,000 bytes の約 1/54）。
プログレッシブ JPEG はデコーダが対応を申告せず stb 経路に
フォールバックする（照合 mean 0.080）。リスタートマーカー付き
ファイルは通常ファイルと同一の結果。

## 4. 全スイートの実行結果（自分の実行、2026-11-20）

| スイート | 結果 |
|---|---|
| ツールキット `make check` | textcore 118 / tk 83 / tk2 121 / font 22 / kbd 131 / tk3 191 / tk4 119 / tk5 127 / tk6 205 = **1,117 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed**（M6 基準と一致） |
| シェル `make check` | **588 passed, 0 failed**（同上） |
| OSK `make check` | **319 passed, 0 failed**（同上） |

既存 API の互換は、消費者のテストが無修正で全緑であることで
確認した。新規の外部ライブラリ依存は追加していない
（JPEG デコーダは自前実装、リンクは従来どおり -lm のみ）。

## 5. 集計の再現方法

AUDIT §2.3 の数値は、AUDIT-toolkit.md の表の行（`| 数字 |` で
始まる行）の観点セルを機械的に数えたもの。検証済みセルの根拠は
test_tk6.c（205 件）および tk1–tk5 / textcore / kbd の既存
テストで、`make check` の出力が証拠である。

## 6. コピー/オーバーヘッド削減

- JPEG サムネイル: 全デコード（1600×1200×4B = 7.68MB の中間
  バッファ）を廃し、IDCT 段階で縮小する。ピーク 1/54 はその
  実測（§3）。デコード後の縮小は box フィルタ1回のみ。
- BottomBar の動的フェイス: テーマ切替時にアプリが選択を
  再適用する往復をなくし、描画時にトークンを参照するだけに
  した（状態の二重保持も解消）。
- 候補補完: テキストが実際に変わった時だけ候補を再構築する
  （キー入力ごとの全件走査をしない）。
- DnD: ペイロードはドラッグ開始時に1回だけルートへコピーし、
  MOVE 中は座標のみを扱う。ゴースト描画は transient paint で
  damage 範囲に限定。
- TreeView モデル IF: 展開は表示時のみで、モデル id 変換は
  並行配列の索引引き（文字列比較なし）。

## 7. キャッシュ/メモリ管理

- ライブラリ全体のメモリ方針（128MB 予算）は従来どおり:
  グリフキャッシュ等の上限付きキャッシュは既存実装のまま
  変更していない。
- JPEG デコーダの作業メモリは「プレーン総量＋スケール後
  デコード＋最終出力」で上限が決まる形にし、
  tk_image_last_decode_peak() で実測可能にした（無制限な
  中間保持をしないことの検証手段）。
- 共有クリップボードは単一文字列の保持のみ（履歴なし、
  サイズは貼り付け元の文字列長に比例する1件分）。
- i18n カタログは登録文字列への参照保持のみで、ロケール
  切替時の再構築はしない。

## 8. 実機の現実への対応

- 寸法換算: Android 側は 48dp = 56px（187ppi / 160dpi ≈ 1.169）。
  WinUI 側は effective px を ×1.43（≒134 effective DPI 相当）
  で換算し、control_h 56px・Body 14px→20px 等のトークンに
  落とした。調整の記録があるもの: WinUI チェックボックスは
  参照 20epx（5.29mm）→ 実機 28px（3.80mm、800×480 と行高
  56px との整合を優先）、Navigation rail 幅 80dp → 94px、
  Drawer の padding 28dp → 33px、Bottom sheet の上マージン
  72dp → 84px・side 幅上限 421px。
- 抵抗膜1本指: マルチタッチ前提のジェスチャは採用していない。
  DnD の開始は移動量 12px のしきい値（タップとの誤認防止）、
  長押しは従来の 600ms / slop 10px。
- GPU 無し: 影・ぼかしは描画基盤にないため、elevation は
  トーン面（surface container の段差）で表現する近似を採った。
- 突然のリセット: 本ライブラリは永続状態を持たない（テーマ・
  ロケール・クリップボードはプロセス内状態）。永続化は消費側
  の責務であり、今回の変更で新たにファイルへ書く処理は
  追加していない。
- オフライン前提: 実行時にネットワークを要求する処理はない。
  i18n は日数ベースの有効期限等を持たない。

## 9. 物理的差異により等価形を採った項目と根拠

「対象外」として落とした項目はない。等価形・近似を採った
ものは次のとおりで、いずれも AUDIT §2 の該当セルは ● に
していない。

| 項目 | 参照側 | 実装した等価形 | 根拠 |
|---|---|---|---|
| Split button の chevron 回転 | 開くと 180° 回転（M3 Expressive） | 回転なし（✕ として記録） | canvas に回転プリミティブがない（描画基盤の制約。ハード制約ではないので「未実装」と正直に記録） |
| Drawer の standard 形 | レイアウト内に常駐 | オーバーレイ配置（スクリムなし・非モーダル） | 現行ルートのオーバーレイ機構で実現可能な範囲。ヒット順がツリーより上になる差異あり |
| Carousel のアイテム幅 | スクロールに応じ連続変化 | スナップ点で離散切替 | CPU のみ描画での毎フレーム再レイアウト回避 |
| AppBar の scrolled 色 | surfaceContainer 専用トークン | container_low で近似 | 専用トークンの実値を未取得（△） |
| SearchBar の composition | preedit のインライン表示 | コミット済みテキストの後に accent 下線付きで追加表示する簡略形 | IME 連携の現行 textcore 描画範囲 |
| Snackbar の WinUI 形 | InfoBar（severity 付き） | surface 面＋中立色バーの形、severity バリエーションなし | WinUI 側の部品別仕様未取得（△） |
| JPEG 1/8 デコードの色差 | 全デコード＋縮小 | 色差は DC 主体になる | libjpeg のスケールデコードと同種の低域通過特性（実測は §3） |

## 10. 未検証の明記

- AUDIT §2 で ◐/○/△ のセルはすべて未検証または部分検証で
  ある（合計 458 セル中 379 セル）。特に: 各部品のアニメーション
  のタイミング・イージング照合、無効/エラー状態のピクセル網羅、
  フォーカス移動の順序（タブオーダー）の部品横断検証、
  WinUI 部品別寸法の Microsoft Learn 照合。
- M3 タイポグラフィ type scale は 2026-11-20 に再取得しても
  本文が空で未確認。文字サイズの全段照合は未了。
- 実機（PW-SH6 実物）および QEMU ゲスト上での描画確認は
  本ラインの範囲外（ホスト上の単体テストのみ）。ARM での
  性能実測も未実施。
- 当該 API をアプリが実際に使うかは消費側の統合フェーズの
  担当であり、本報告はライブラリ側の存在と単体動作までを
  証拠とする。
