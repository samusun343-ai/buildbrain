# GUI ライブラリ同等化 第8弾（M15）レポート — 残り134セルの格上げ

状態: **このラインの作業は完了**（下記「結果」が最終実数。
未検証事項は「未検証の明記」に列挙）。
開始時点の実数（AUDIT-toolkit.md §2 が正本、自分の再実行で確認済み）:
● 322/456（70.6%）、◐ 84、○ 50、△ 0、✕ 0。
モード別 ●: Android 164/228、WinUI 158/228。
ツールキット 2,910 tests 全 PASS、消費者（アプリ 1,048 /
シェル 683 / OSK 319）回帰ゼロ。

## 作業計画と仕様出典（2026-11-20、Web 一次ソースで取得）

### F ○ の解消方針（優先順位1）

- **#32 Sheet（モーダルトラップ）**: M3 のモーダルシートは
  フォーカスを受けキー（Back）で閉じるのが参照形。
  出典: developer.android.com の ModalBottomSheetProperties
  — `isFocusable` 既定 true（「シートが IME イベントと
  キー押下を受ける」）、`shouldDismissOnBackPress` 既定
  true → onDismissRequest。ModalBottomSheet 解説「ダイアログ
  と同様、アプリの他の機能を全て無効化して前面に出る」。
  WinUI 形（Flyout）の light dismiss は Esc を含む
  （Microsoft Learn、Flyout クラス）。
  → 実装: モーダル表示時に focus_return 保存＋最初の
  フォーカス可能子孫へフォーカス（無ければシート自身）、
  Esc で閉じる。復帰は tk_root_remove_overlay の汎用
  focus_return 機構（M14）に任せる。非モーダル形は現行維持。
- **#31 Drawer（フォーカス不可）**: 項目は両参照とも
  フォーカス可能な項目コントロール。
  出典: Microsoft Learn NavigationView（項目は
  NavigationViewItem コントロール）＋「Focus navigation
  without a mouse」（対話要素はフォーカス移動に対応、
  方向キーは領域内移動）。Android 側は developer.android.com
  の Compose ModalNavigationDrawer 公式サンプルが
  「ドロワーが閉じたらキーボードフォーカスは呼び出し元へ
  戻るべき」と明記。
  → 実装: ドロワーをフォーカス可能化し項目カーソル
  （Up/Down で区切り・無効を飛ばし端で停止、Enter/Space
  で選択、モーダル形は Esc で閉じる）。モーダル表示時は
  フォーカスを中へ移し focus_return で復帰。標準形は
  Tab 到達可能な1ストップ。
- **#4 FAB menu（キー経路なし）**: ポップアップ面のキー
  模型は Menu（#21、F ● で検証済み）と Flyout の
  Esc light-dismiss（Microsoft Learn、Flyout クラス）に
  従う。→ 表示時に最初の項目ボタンへフォーカス、
  Up/Down で項目間移動（循環）、Tab は面内で循環、
  Esc で閉じてフォーカス復帰（remove_floating は
  remove_overlay 経由のため汎用復元が効く）。
- **#26 Snackbar（キー経路なし）**: アクションは実ボタン
  であり Tab 走査に入る。表示でフォーカスは奪わない。
  出典: m3.material.io Snackbar ガイドライン（「They
  shouldn't interrupt the user experience」）＋ Android
  Developers の Snackbar API 参照（「moving the keyboard
  focus indicator to navigate through interactive elements
  in a page is not considered an action」— アクションは
  ページ内の対話要素としてフォーカス移動で到達する前提）。
  → 実装: snack_widget をフォーカス可能化し、アクション
  矩形をその rect に同期。focus_step に「モーダル無し・
  アクション有り・表示中」のとき末尾の Tab ストップとして
  追加。Enter/Space でポインタ経路と同じ発火。Esc は
  付けない（両参照とも定義なし）。
- **#40 DnD（キーボード DnD なし）**: 両参照とも DnD を
  ポインタジェスチャとしてのみ定義し、キー操作の定義が
  存在しない。出典: Microsoft Learn「Drag and drop -
  Windows apps」（"a standard gesture (press-hold-and-pan
  with the finger or press-and-pan with a mouse or a
  stylus)"）、Android Developers「Implement drag and drop
  with views」（"The user starts a drag with a gesture,
  usually by touching or clicking and holding"）。
  キーボード代替は切取/貼付（#41 で ● 検証済み）であり
  別機能。→ F は両モードとも「—」（部品の性質上存在
  しない、M14 の #12 F 訂正と同型）として AUDIT に
  根拠を記録する。分母 456 → 454。

### 観察（未修正・本ラインの範囲外）

- Android Developers の Snackbar API 参照は「アクション
  付きスナックバーはユーザーが他の操作をするまで
  timeout / self-dismiss すべきでない」とするが、本実装は
  duration 指定時にアクション有無によらず自動消去する
  （TK_SNACKBAR_INDEFINITE が永続形）。消費者の契約に
  触れるため本ラインでは変更せず、記録のみとする。
  C セルの格付けは変更しない。

### E/B/D の対象（優先順位2・3）

- E: WinUI #3、#6/#7/#8（ラベル画素）、#19、#28、#33、
  両モード #5、#25、#30、#34。
- B: #4 両（FAB menu 面の状態）、#26 Android、#35
  Android、#34 WinUI、#36 WinUI、#40 WinUI（ドラッグ
  シャドウ画素）、#38 WinUI。
- D: #11 WinUI（indeterminate の位相駆動画素 — 実装は
  prog_phase 駆動のセグメント掃引で画素検証可能）、
  #21 WinUI（出現スライドの画素変位＋退出即時。Android は
  Compose の退出アニメーションが存在し本実装は即時の
  ため ◐ 据え置き見込み）。

## 進捗ログ

- 2026-11-20: ベースライン確認（2,910 PASS）、AUDIT §2 の
  残セル実数え（○ 50 件の内訳一致）、コード調査と
  一次ソース取得を完了。本計画を記録。実装は未着手。
- 2026-11-20: F 実装を完了。Drawer（フォーカス可能化＋
  項目カーソル＋モーダル時のフォーカスイン/復帰）、Sheet
  （モーダル時の focus_return＋内部フォーカス＋Esc）、
  FAB menu（表示時に先頭項目へフォーカス、Up/Down/Tab
  循環、Esc、focus_return 復帰）、Snackbar（アクションを
  Tab ストップ化、Enter/Space 発火、非表示時のフォーカス
  救済、アクション矩形へのリング再描画）。
  test_tk14 第1部 215 checks 全 PASS（両モード）。
- **実在バグ修正（M14 機構の潜在欠陥）**:
  tk_root_remove_overlay が focus_return 復元をモーダル
  解除「前」に行っていたため、tk_focus のモーダルトラップ
  が復元を常時拒否していた。モーダルなオーバーレイ
  （ダイアログ/ドロワー/シート）では M14 のフォーカス
  復帰は一度も発火していなかった（メニュー等の非モーダル
  ポップアップでのみ有効だった）。除去時にモーダルを
  先に解除するよう tk_core.c を修正。tk14 のドロワー/
  シートテストが露出させた。
- 検証過程の自己訂正: Snackbar アクションの押下状態層を
  snack_down_on キーで追加したが、同フラグはボディの
  スワイプ開始アームであり、アクションは DOWN で即発火
  する（押下フレームは参照にも存在しない）ため撤回した。
- 残セル棚卸しの訂正: M14 レポートの散文リストは
  マトリクス正本と一部食い違っていた（#3 E・#19 E は
  両モードとも ● 済み）。以降は AUDIT §2 の表を正とする。

## 結果（2026-11-20、自分の実行で確認）

### セル増減表（モード別、AUDIT §2.3 の機械集計と一致）

| モード | 適用セル | ● | ◐ | ○ | △ | ✕ |
|---|---|---|---|---|---|---|
| Android（前） | 228 | 164 | 42 | 22 | 0 | 0 |
| Android（後） | 227 | **180** | 30 | 17 | 0 | 0 |
| WinUI（前） | 228 | 158 | 42 | 28 | 0 | 0 |
| WinUI（後） | 227 | **181** | 28 | 18 | 0 | 0 |
| 合計（前） | 456 | 322（70.6%） | 84 | 50 | 0 | 0 |
| 合計（後） | 454 | **361（79.5%）** | 58 | 35 | 0 | 0 |

分母の変化は #40 F の「—」化（両モード、根拠は
AUDIT §13.1）のみ。● の増加は Android +16、WinUI +23、
合計 +39。格下げは無し。

### テスト・回帰（全て自分の実行）

- ツールキット `make check`: **3,241 checks 全 PASS**
  （従来 2,910 + test_tk14 331）。test_tk14 は Makefile の
  TESTS に配線した（編集範囲に Makefile が含まれる —
  test_tk13 の先行ラインと同じ配線方法）。
- test_tk14 の ASan/UBSan ビルド: 331 PASS、メモリ
  エラーなし。リークは indirect のみ（ハーネス root_of の
  ラッパツリーを解放しない既存パターン、M14 と同じ性質で
  製品コードのリークではない）。
- 消費者（無修正で再実行）: アプリ 1,048 PASS 行 /
  シェル 683（588 + test_mascot 95）/ OSK 319、
  いずれも exit=0 で M14 ベースラインと完全一致。

### 格上げセルの根拠（仕様出典＋テスト）

全て tests/test_tk14.c のフローで両モード検証（D の
2件は WinUI のみが対象）。

**F（○ → ●、両モード）**
- #31 Drawer: drawer_flow。フォーカス可能化＋項目
  カーソル（Up/Down、区切り・無効スキップ、端で停止）、
  Enter/Space 選択、モーダル形は Esc で閉じフォーカス
  復帰、標準形は Tab 1ストップ。出典は計画節のとおり
  （NavigationView 項目＝フォーカス可能コントロール、
  Compose 公式サンプルの復帰コメント）。
- #32 Sheet: sheet_flow。モーダル形は focus_return
  保存＋内部フォーカス、Esc で閉じて（on_dismiss 発火）
  復帰、Tab は面内にトラップ。非モーダル形はフォーカス
  を奪わず Esc も効かない。出典: ModalBottomSheet
  Properties（isFocusable / shouldDismissOnBackPress
  既定 true）、WinUI Flyout の light dismiss（Esc 含む）。
- #4 FAB menu: fabmenu_flow。表示で先頭項目へ
  フォーカス、Up/Down 循環、Tab 面内循環、Esc で閉じて
  復帰。模型は Menu（#21 F ●）と Flyout の Esc。
- #26 Snackbar: snackbar_flow。アクションが末尾の Tab
  ストップになり Enter/Space でポインタ経路と同一手順で
  発火、表示ではフォーカスを奪わない、非表示でフォーカス
  救済。出典: M3 Snackbar ガイドライン＋ Android
  Developers Snackbar API 参照（計画節）。
- #40 F: ○ → 「—」（両モード）。根拠は AUDIT §13.1。

**E（→ ●）**
- #2 両モード（button_e）: 5バリアントのラベル画素＋
  トグル ON の面変化。LEGACY トグルのアイコン行形は
  下記バグ(3) の修正で初めて画素が動くようになった。
- #5 両モード（split_e）: ラベル画素＋シェブロンの形状
  署名（中央領域の bbox・両上象限の腕）。
- #6/#7/#8 WinUI（toggles_e_winui）: WinUI 面での
  チェック/選択/ON のラベル隣接画素。
- #25 両モード（picker_e）: 年月日・時分の数字グリフを
  両モードで検証（WinUI は ○ から）。
- #28 WinUI（tabs_e_winui）: タブラベルのグリフ。
- #30 両モード（rail_e）: Android はアイコン＋ラベル、
  WinUI はコンパクトペイン形（ラベル無しが仕様どおりで
  あることを画素 0 で確認し、アイコン画素を根拠とした）。
- #33 WinUI（tabs_e_winui の segmented 部）: セグメント
  ラベルのグリフ。
- #34 両モード（carousel_e）: ラベルバンドの白文字
  グリフ（WinUI は ○ から）。

**B（→ ●）**
- #4 両モード（fabmenu_flow の面プローブ）: メニュー面
  ＋影の画素。
- #26 Android（snackbar_flow）: バー面＋アクション
  ラベル＋フォーカスリング。押下フレームはアクションが
  DOWN で即発火するため参照にも存在しない。
- #34 WinUI（carousel_e）: 画像上のラベルバンド
  （スクリム）画素。
- #35 Android（colorpicker_b）: スウォッチ面＋プレビュー
  ＋選択インジケータの移動。
- #36 WinUI（appbar_b_winui）: layer 面（半透明のため
  背景とのブレンド値で照合）＋ divider ＋ scrolled で
  面が変わらないこと。
- #38 両モード（longpress_flow）: 保持中の押下面と
  解放後の復帰（残差はホバー層のみ）。
- #40 両モード（dnd_b）: ドラッグ中のシャドウ面＋
  ラベル画素（WinUI は ○ から）。
- #43 両モード（a11y_b）: a11y 状態文字列が視覚状態と
  一致（checkbox/switch/radio/slider/progress）。
- #44 両モード（thumb_b）: サムネイル描画の勾配画素
  （左→右 R、上下 G の方向まで照合）。

**D（WinUI、◐ → ●）**
- #11（progress D 部）: 位相駆動でセグメント画素が
  実際に動くことを検証。公開仕様は "a repeating
  pattern" のみでキーフレーム定義はない（AUDIT §13.3）。
- #26（snackbar D 部）: ライブクロックで画素不変 =
  出現モーション無しを検証。

### 発見・修正したバグ（3件、詳細は AUDIT §13.2）

1. モーダルオーバーレイで M14 のフォーカス復帰が一度も
   発火していなかった（remove_overlay の順序）。修正済み。
2. 全画面フォーカス矩形へのリング描画（1 の副作用）。
   ルート全体を覆う矩形にはリングを描かないよう修正。
3. アイコン行形の LEGACY トグル（M3/クラシック）に
   checked 面がなく、ON 状態が画素に現れなかった。
   テキスト経路と同じ container 面を実装。

### 残セル一覧と理由（93セル: ◐ 58 + ○ 35）

- **A ◐（Android 27件 / WinUI 24件）**: 記録済み実機
  適合デルタ（187ppi 実寸調整）が理由。新規出典が
  得られなかったため方針どおり据え置き。WinUI #31/#32
  は NavigationView ペイン幾何（320epx ≈ 458px）に
  対しドロワー幅キャップ 360px で一致せず、実装変更は
  しない判断も据え置き。
- **A ○（3件）**: #27 Toast 両モード（公開形状なし）、
  #35 WinUI（ColorPicker の WinUI 寸法の一次ソースなし）。
- **D ○（各モード16件: #1,#2,#3,#13,#14,#15,#16,#18,
  #19,#20,#25,#29,#30,#33,#35,#36）**: 部品ごとに一次
  ソースを再確認したが、「定義なし」と「定義はあるが
  本実装に動きが無く画素検証に届かない」の切り分けが
  個別に必要で、一括格上げはしない（AUDIT §13.3）。
- **D ◐（#21 両モード）**: 理由を精密化して据え置き
  （Android: 退出アニメーション未実装。WinUI: フェード
  成分未再現）。
- **E ◐（Android #9 / WinUI #3,#9,#19）**: #9 は単一形に
  値インジケータ文本が存在しない構造的理由。WinUI #3
  （FAB/アイコンボタン）と #19（ListView 行）は画素
  網羅の残り。
- **C ◐（Android #39）**: アニメーション基盤の C。
  理由の精密な記録が無く、推測での格上げはしない。

### コピー/オーバーヘッド削減

- Snackbar のキー経路は新しいウィジェットを作らず、
  既存のラベルウィジェット（snack_widget）を再利用し
  rect をアクション矩形に同期するだけにした。追加の
  描画パスは作らず、フォーカスリングは既存の
  transient_paint の末尾で1回だけ再描画する。
- focus_step の Tab ストップ追加は既存の収集配列への
  末尾追加のみ（realloc の成長手当て込み）で、通常時の
  走査コストは変わらない。
- Drawer のカーソル同期は表示時と FOCUS_IN 時のみで、
  描画時の追加走査はない。カーソル行の描画はフォーカス
  中のみ。

### キャッシュ/メモリ管理

- 新規のキャッシュ・バッファは導入していない。追加した
  状態はルート/ウィジェット内の既存フィールドと
  focus_step の一時配列のみで、表示・非表示のたびに
  増減しない。ASan/LSan で direct リークなしを確認。

### 実機の現実への対応

- キー操作の追加は抵抗膜1本指の代替経路として意味を
  持つ（タップできない状況・物理キー操作）。寸法は
  変更していない。
- 永続状態は追加していない（フォーカス・カーソルは
  全て揮発状態で、リセットで失われても壊れない）。
- オフライン前提に影響する変更はない。

### 未検証の明記

- QEMU ゲスト/実機での実操作は未実施（統合フェーズの
  合格基準。本ラインの検証はホストの画素・振る舞い
  テストまで）。
- Snackbar の timeout 契約の観察事項（計画節）は
  未修正のまま残る。
- D ○ 32件の個別切り分け、E ◐ の画素網羅の残り、
  A 群の新規出典待ちは次弾以降の課題として残る。
