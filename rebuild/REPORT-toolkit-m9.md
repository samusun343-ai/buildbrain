# REPORT-toolkit-m9 — GUI ライブラリ同等化の継続 第2弾（M9）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


2026-11-20。対象: `include/`・`src/`・`tests/` と AUDIT のみ。
repo へは push していない。正本マトリクスは AUDIT-toolkit.md §2。

着手時点（M8 到達点、REPORT-toolkit-m8.md が正本）:
● 136/458（29.7%）、◐ 182、○ 139、△ 1、✕ 0。
モード別 ● は Android 76/229、WinUI 60/229。
ツールキット 1,363 tests 全 PASS、消費者（アプリ 1,048 /
シェル 588 / OSK 319）回帰ゼロ。**完成ではない。**
このラインの目的はセルの格上げと記録済みデルタの解消。

## 0. 着手時に Web で取得した仕様（出典）

- WinUI ToggleButton（△ 1件の対象）:
  microsoft/microsoft-ui-xaml
  `controls/dev/CommonStyles/ToggleButton_themeresources.xaml`
  （2026-11-20 取得）。ToggleButtonBorderThemeThickness = 1、
  Background = ControlFillColorDefault 系、
  BackgroundChecked = AccentFillColorDefault、
  ForegroundChecked = TextOnAccentFillColorPrimary、
  Style の Padding = {StaticResource ButtonPadding}
  （= Button と同一リソース。M8 取得の Button 実値
  Padding 11,5,11,6 と同じ）。→ ToggleButton の寸法は
  Button と同一であることが一次ソースで確認できた。
- WinUI InfoBar: 同リポジトリ
  `controls/dev/InfoBar/InfoBar_themeresources.xaml`。
  severity 背景 = SystemFillColor{Attention / Caution /
  Success / Critical}Background、アイコン背景 =
  SystemFillColor{...}（ソリッド）、アイコン前景 =
  TextFillColorInverse、境界 = CardStrokeColorDefault・1、
  InfoBarMinHeight 48、アイコン 16、コンテンツ左パディング
  16、アイコンマージン右 14、クローズボタン 38・グリフ 16。
- WinUI ProgressBar: 同 `controls/dev/ProgressBar/
  ProgressBar_themeresources.xaml`。ProgressBarMinHeight 3、
  ProgressBarTrackHeight 1、CornerRadius 1.5、
  Foreground = AccentFillColorDefault、Background =
  ControlStrongStrokeColorDefault、境界 0。
- WinUI ProgressRing: 同 `controls/dev/ProgressRing/
  ProgressRing_themeresources.xaml`。
  ProgressRingStrokeThickness 4、Foreground =
  AccentFillColorDefault、Background =
  ControlFillColorTransparent（トラックリング無し）。
- M3 Dialog（https://m3.material.io/components/dialogs/specs、
  2026-11-20 取得）: 基本形は全辺パディング 24dp、
  幅 Min 280dp / Max 560dp、角丸 28dp、容器色
  surfaceContainerHigh、divider 1dp、タイトル→本文 16dp。
  フルスクリーン形はヘッダ高 56dp、パディング 24dp。
- M3 Navigation drawer
  （https://m3.material.io/components/navigation-drawer/specs）:
  容器幅 360dp、モーダル形の角丸 0,16,16,0dp、左右パディング
  28dp、アクティブインジケータ高 56dp・幅 336dp・
  パディング 12dp。standard 形はレイアウト内常駐（モーダルと
  同一寸法表、角丸なし）。
- M3 Carousel（https://m3.material.io/components/carousel/specs）:
  「Items change size as they move through the carousel
  layout」— アイテム幅はスクロールに伴い連続変化する。
  small アイテム幅 40–56dp（dynamic）、要素間 8dp、
  アイテム角丸 28dp、hero レイアウト = large + small。
- M3 Slider 現行仕様（material-components-android の
  Slider.md / m3 sliders specs、検索経由で 2026-11-20 確認）:
  Expressive 形はトラック高 16dp（XS 既定）、ハンドル
  44dp 高 × 4dp 幅、value indicator 容器 48×44dp。
- M3 surfaceContainer のベースライン値: tk_core.c の M7
  コメントに記録済みのラダー（light: Low #F7F2FA /
  Container #F3EDF7 / High #ECE6F0 / Highest #E6E0E9、
  dark: Low #1D1B20 / Container #211F26 / High #2B2930 /
  Highest #36343B）のうち Container 段が未収録だった。
  AppBar の scrolled 色はこの Container 段が仕様値。

取得できなかったもの（推測で埋めていない）:
- SystemFillColorAttention / AttentionBackground の実値
  （microsoft-ui-xaml リポジトリ内に定義が無く、OS 側
  リソース。GitHub コード検索は要ログイン、ミラー候補も
  404）。→ InfoBar の Informational severity はアイコン
  背景にテーマの accent を充てる近似とし、該当セルは
  ● にしない。
- ControlStrongStrokeColorDefault の実値（同上）。
  → ProgressBar のトラック色は従来ロールを維持し、
  高さ（MinHeight 由来）のデルタのみ解消する。

## 1. 結果サマリ（セル単位の実数のみ）

| モード | ● | ◐ | ○ | △ | ✕ |
|---|---|---|---|---|---|
| M8 到達点 | 136 | 182 | 139 | 1 | 0 |
| Android（M9 後） | 86（+10） | 93（−10） | 50（±0） | 0 | 0 |
| WinUI（M9 後） | 69（+9） | 71（−8） | 89（±0） | 0（−1） | 0 |
| **合計（M9 後）** | **155（33.8%）** | 164 | 139 | 0 | 0 |

- 格上げ 20 セル（● 化 19、△→◐ 1）。格下げはなし。
- AUDIT-toolkit.md §2 の表を更新し、機械集計で上表と一致を
  確認した（集計は表の記号の実数え）。
- テスト: ツールキット 1,431 件全 PASS（M8 の 1,363 + 新規
  test_tk8 の 68）。消費者3スイートは無修正で アプリ 1,048 /
  シェル 588 / OSK 319 全 PASS（基準値と完全一致、回帰ゼロ）。
- **完成ではない。** ◐ 164 + ○ 139 = 303 セルが未検証・
  部分検証のまま残る（§6）。

## 2. 記録済みデルタの処理内訳

### 2.1 仕様値に直して ● 化したもの（未追随だったもの）

| デルタ | 仕様値（出典 §0） | 実装 | 検証 |
|---|---|---|---|
| Dialog padding 16 | M3 24dp = 28px / WinUI 24epx = 34px | テーマ style から measure 時に同期（tk_core.c の dialog_theme_pad）。アプリ側の手設定は不要になった | tk8: 子オフセット 28 / 34 を実測 |
| Dialog 最小・最大幅（未実装だった） | M3 min 280dp = 327 / max 560dp = 655、WinUI min 320epx = 458 / min 高 184epx = 263 / max 548epx = 784 | layout_overlay にモード別クランプを追加 | tk8: 表示 rect 327 / 458×263 を実測 |
| Slider つまみ 28（WinUI） | 外径 18epx = 26px | slider_thumb の径を style2 で 26 に（他モードは 28 を維持） | tk8: アウトライン間隔 26 をピクセル実測 |
| AppBar 高さ 56 | M3 small 64dp = 75px / WinUI 48epx = 69px | テーマトークン appbar_h を新設し measure が参照（0 のテーマは control_h にフォールバック） | tk8 + tk7 更新: 75 / 69 を実測。tk6/tk7 の旧ピニング（56 / 20）は仕様値に更新 |
| Progress 高さ 20（一律） | M3 track 4dp = 5px / WinUI MinHeight 3epx = 4px | measure をモード別に（classic は 20 を維持） | tk8 / tk7 更新: 5 / 4 を実測 |
| Menu 内容インセット 8（一律） | M3 12dp = 14px / WinUI 11epx = 16px | menu_inset() を measure と paint の両方に適用 | tk8: ラベル相対幅 28 / 32 を実測 |
| Ring ストローク 5px ドット列 | M3 4dp = 5px 連続 / WinUI 4epx = 6px・トラック無し | 連続弧化（§3 と同じ作業で実施）＋ WinUI はトラック円を描かない | tk8: 36 点サンプリング全点 accent、WinUI 0% で無描画 |

### 2.2 実機適応として根拠を明記し、据え置いたもの

- **Android Slider の形**（A は ◐ のまま）: 現行 v2 形
  （16px トラック + 28px 丸つまみ）は UI-STYLE-v2 の
  One UI 系デザイン言語としての適応。M3 の現行仕様
  （Expressive、§0）はピル型ハンドル（4dp 幅 × 44dp 高）
  で、全面変換はデザイン言語の判断を含む大改修のため
  本ラインでは行わず、次ラインへの提案として残す。
- **ListView 行高 Δ1（56 対 57）**: 57 は 40epx × 1.43 の
  素の換算で、×1.40 の着地は Switch トラック 40epx →
  56px（M8 で ●）と同一の丸め帯域内。タッチフロア
  （行高 ≥56px）とも一致する。AUDIT §2.4 に記録し
  テーマ値は変更しない。セルは ◐ のまま。
- **ProgressBar トラック色**（B は ◐ のまま）:
  ControlStrongStrokeColorDefault の実値は OS 側
  リソースで取得不能（§0）。従来ロール（surface_alt）を
  維持し、高さのデルタのみ解消した。
- **InfoBar の Informational 色**（B は ◐ のまま）:
  SystemFillColorAttention 系の実値が取得不能のため、
  アイコンディスクは accent で代替（コード内コメントと
  本節に明記）。背景は CardBackground ロール。

### 2.3 △ 1件の解消

- WinUI ToggleButton の A: §0 の一次ソースで寸法が
  Button と同一（ButtonPadding 共有）と確定。△ → ◐。
  ● にしない理由: トグル専用の寸法テストは未実施で、
  Button と共有の寸法経路による部分検証に留まるため。

## 3. §9 近似 5 件の処理（REPORT-toolkit-complete.md §9）

| 近似 | 処理 | 検証 |
|---|---|---|
| Drawer standard がオーバーレイ実装 | `tk_drawer_attach(parent, w)` を新設: 通常子としてレイアウト内常駐（幅は内容量 ≤421px、高さは親に追随）、M3 standard 形として角丸なし・スクリムなし・選択で閉じない | tk8: 配置・全高・角ピクセル・選択後も残存 |
| Carousel の離散スナップ | アイテム幅をスクロール量で連続補間（スロット中心で f=1、1ステップで f=0、幅 = 64 + 184f、位置は補間幅の累積）。静止時の形状は従来と一致 | tk8: 半ステップドラッグ中の境界位置をピクセル実測（離散形の 221 ではなく約 130） |
| AppBar scrolled 色（container_low 近似） | surfaceContainer そのもの（新トークン container_mid、値は M3 ベースラインラダーの中段 #F3EDF7 / dark #211F26）に是正 | tk8: light / dark 両方でピクセル一致 |
| SearchBar composition（末尾追加の簡略形） | TextField と共有の正式 preedit 描画（tk_draw_composition を内部公開化）でキャレット位置に prefix / composition / suffix を描画。セグメント下線・候補選択ウォッシュも同一形 | tk8: キャレット位置での accent 下線をピクセル確認（無変換時は 0） |
| Snackbar の WinUI 形（severity なし） | `tk_snackbar_show_sev` を新設し InfoBar 形に: severity 背景（sys_ok/warn/err_bg ロール）、ソリッドな severity アイコンディスク + 逆色グリフ、高さ 48epx = 69、テキストオフセット 46epx = 66。M3 形は不変（severity の概念なし） | tk8: SUCCESS の背景・ディスク、INFO のディスクをピクセル確認。高さ 69 / M3 形 56 の不変も確認 |

## 4. 格上げセルの根拠（仕様出典 + テスト）

全て tests/test_tk8.c（68 checks、本セッションで実行し
全 PASS）+ 既存スイートの更新分が根拠。出典は §0。

Android（+10）:
- #5 Split C ◐→●: §5 のバグ修正 + トグル往復テスト
- #8 Switch B ◐→●: 無効時淡色化を実装（ON/OFF 両位置で
  トラック = container_highest をピクセル確認）
- #12 Ring B ◐→●: 連続弧（36 点サンプリング）+ 従来の
  四分円確認（tk6/tk7）
- #15 SearchBar E ◐→●: §3 の composition 正式化
- #21 Menu A ◐→●: §2.1 のインセット実測（28 = 2×14）
- #22 Dialog A ◐→●: §2.1 の padding + 最小幅実測
- #31 Drawer A ◐→●: §3 の standard 形（レイアウト内配置）
- #34 Carousel B ◐→●: §3 の連続サイズ（ドラッグ中形状）
- #36 AppBar A ◐→● / B ◐→●: 高さ 75 実測、scrolled 色の
  役割一致（light/dark）

WinUI（+9、△→◐ 1）:
- #2 ToggleButton A △→◐: §2.3
- #5 Split C ◐→●: §5（モード共通のルート処理）
- #8 Switch B ◐→●: 無効時に accent が消え ControlFill に
  なることをピクセル確認
- #9 Slider A ◐→●: つまみ 26px 実測（M8 で ● を阻んでいた
  唯一のデルタ）
- #11 ProgressBar A ◐→●: 高さ 4px 実測
- #12 ProgressRing B ◐→●: トラック無し + ストローク 6px
  （0% で無描画、四分円は tk7）
- #21 Menu A ◐→●: インセット実測（32 = 2×16）
- #22 ContentDialog A ◐→●: padding 34 + 最小 458×263 実測
- #26 InfoBar A ◐→●: 高さ 69 + アイコンインセット実測
- #36 AppBar A ◐→●: 高さ 69 実測

## 5. 発見・修正したバグ

1. **アンカー再クリックでメニューが閉じない**（M8 で
   #5 C 格下げの原因として記録済み）: ルートの
   POINTER_DOWN 解散が先行し、アンカー側のトグル処理が
   再オープンしていた。修正は3経路:
   (a) Split: 解散前にヒットテストし、開いている split
   メニューのアンカーへの押下は解散後に飲み込む。
   (b) MenuBar: 解散したメニューをルートに記録し
   （down_dismissed_menu）、UP での再オープンを抑止。
   (c) ComboBox: ドロップダウンは開くたびに作り直される
   ため所有者（owner_ref）で記録し combo_open で抑止。
   同じ機構の欠陥が (b)(c) にもあることを本ラインで
   確認し、あわせて修正した。tk8 で3経路とも
   開→閉→開の往復を検証。
   なお auto_free メニューのアドレスは解放後に記録が
   残らないよう、記録対象から除外した（所有者記録で
   カバー）。
2. 実在バグは上記以外に新規発見なし。tk8 初回実行の
   失敗 8 件は全てテスト側の前提誤り（プローブ対象が
   ノブ位置と重なる、フォントメトリクス差の混入、
   半透明トークンのブレンド未考慮等）で、実測プローブで
   切り分けてテストを是正した。実装側の想定外動作は
   その過程では見つからなかった。

## 6. 残セルと理由（主なもの）

- ◐ 164 セルの大半: B 観点（全状態のピクセル網羅）が
  主因のまま。状態ごとに描画を確認する作業は部品単位で
  継続が必要（本ラインでは Switch / Ring / AppBar /
  Snackbar の一部のみ前進）。
- ○ 139 セル: WinUI 側に集中（89）。実装はあるが
  モード内での検証が未実施のもの（例: WinUI Drawer の
  standard 形は attach 機構がモード共通で実装済みだが
  WinUI モードでの実行確認はしていないため ○ のまま）。
- D 観点（アニメーション）は両モードとも ○/◐ が大半:
  tk_anim の実時間挙動の検証が薄い。
- Android Slider A: §2.2 のとおりデザイン言語の判断
  待ち（M3 Expressive のピル型ハンドルへの変換可否）。
- WinUI Snackbar B: severity 4 種のうちピクセル確認は
  SUCCESS / INFO の 2 種。WARNING / ERROR は同一コード
  パスの別トークンで未確認のため ◐。
- ProgressBar B（両モード）: §2.2 のトラック色ロール
  近似が残る。
- SearchBar E（WinUI）: composition 描画はモード共通
  コードだが、検証は Android モードでのみ実施のため ◐。

## 7. コピー / オーバーヘッド削減

- 新規の描画・レイアウト処理は全て既存の damage 駆動
  パイプラインの内側で、追加の全画面再描画や中間
  バッファは導入していない。
- Carousel の連続化は paint とヒットテストで共有する
  幅計算（car_f / car_w）を置き換えただけ（追加の
  状態保持なし、1アイテムあたり数演算）。
- Ring の連続弧は 5px ドット列（約 120 描画）を
  ストローク径ディスクの密ステップ描画（約 480 描画、
  96px 級リングの場合）に置換した。1回の paint あたり
  数百ピクセル規模の増加で、リングは静止画部品のため
  フレーム転送量への影響はない（値が変わる時だけ
  damage が出る）。
- Snackbar の severity は root の int 1 個の追加のみ。
  InfoBar 形の描画も既存 transient_paint 内の分岐追加。
- テーマトークンの追加（container_mid / appbar_h）は
  構造体末尾への追加で、既存テーマ定義の designated
  initializer は無変更（未設定テーマは 0 = フォール
  バックで従来動作）。

## 8. キャッシュ / メモリ管理

- 本ラインでキャッシュは追加していない。テーマ追加
  フィールドは静的 const テーマに属し、実行時確保なし。
- tk_root への追加は down_dismissed_menu / owner の
  ポインタ 2 個のみ（所有権なし、押下ごとにクリア）。
- 辞書・フォント等の予算には一切触れていない。

## 9. 実機の現実への対応

- 寸法は全て実機スケール（800×480・187ppi・抵抗膜
  1 本指）換算で実装: タッチフロア（スライダー高 56、
  リスト行 56 等）は従来どおり維持し、仕様値がフロアを
  下回る箇所（Progress 高さ等）は視覚要素のみ縮小し
  ヒット領域は変えていない。
- 永続状態・ファイル I/O は本ラインの対象外（ライブラリ
  の描画・寸法・振る舞いのみ）。リセット耐性に関わる
  変更はない。
- オフライン前提: 仕様取得は全て本セッションの Web
  取得で完結し、実行時にネットワークを要する要素は
  追加していない。

## 10. 未検証の明記

- 本ラインの検証は全てホスト上のユニットテスト
  （実フォント描画 + ピクセル確認）による。SD イメージ /
  QEMU ゲスト / 実機での確認はしていない（統合フェーズ
  の担当）。
- WinUI ダークテーマでの tk7 実行: tk8 の4テーマ同一
  ツリー検証（v2 / v2_dark / winui / winui_dark で
  AppBar・Progress の寸法と描画有無を確認）で代替した。
  tk7 全体を winui_dark 既定で再実行する形は未実施。
- §6 に挙げた残セルは全て未検証・部分検証のまま。
  「完成」「ほぼ完成」の主張はしない。

## 11. テスト実行結果（自分の実行で確認）

```
ツールキット (make check):
  textcore: 118 passed, 0 failed
  tk: 83 / tk2: 121 / font: 22 / test_kbd: 131
  tk3: 191 / tk4: 119 / tk5: 127 / tk6: 205
  tk7: 246 / tk8: 68（新規）
  合計 1,431 passed, 0 failed
消費者（無修正）:
  apps: 1,048 全 PASS / shell: 588 全 PASS / osk: 319 全 PASS
```

変更ファイル: include/tk.h、src/tk_core.c、
src/tk_widgets.c、src/tk_m7.c、src/tk_internal.h、
tests/test_tk8.c（新規）、tests/test_tk7.c（ピニング値
2 件と注記を仕様値に更新）、Makefile（test_tk8 配線）、
AUDIT-toolkit.md（§2 表・集計・§2.4）。
