# REPORT-toolkit-m13 — GUI ライブラリ同等化の継続 第6弾（M13）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


2026-11-20。対象: `include/`・`src/`・`tests/` と AUDIT のみ。
repo へは push していない。正本マトリクスは AUDIT-toolkit.md §2。

着手時点（M12 到達点、REPORT-toolkit-m12.md が正本）:
● 201/458（43.9%）、◐ 146、○ 111、△ 0、✕ 0。
モード別 ● は Android 108/229、WinUI 93/229。
ツールキット 1,927 tests 全 PASS（本セッション着手時に
再実行して一致確認）、消費者（アプリ 1,048 / シェル 683 /
OSK 319）回帰ゼロ。**完成ではない。**
残りは ◐ 146 + ○ 111 = 257 セル。本ラインの目的も
セルの格上げのみ。

本ラインで据え置き（触らない）: Android Slider の形
（M3 Expressive ピル型ハンドルはユーザー判断待ちの提案として
分離済み。ただしプログラム的値変更アニメーションは形ではなく
動きの話なので対象に含む）、ListView 行高 Δ1（AUDIT §2.4 の
適応記録どおり、新たな出典が得られた場合のみ再訪）。

## 0.5 途中経過（着手時）

### 着手時の優先順位（タスク指定）

1. FAB / Split button / Navigation 系の B ◐（M12 で未着手
   だった優先3）: 残りの状態ピクセルを仕様と照合して ● 化。
2. 未配線のまま残る D 観点の実装＋検証: Toast フェード、
   TabBar インジケータ移動、FAB menu の展開、Carousel
   settle、Checkbox / Radio のグリフ遷移、Slider の
   プログラム的値変更アニメーション。
3. Progress D / Ring D / Toast D の残件: Progress は
   M3 キーフレーム忠実度、Ring は回転の画素検証、Toast は
   フェード配線後の検証。
4. tk11 のダーク全体再実行（M12 で未実施）。
5. その他 ◐/○ の残セル（A/E/F 観点、WinUI 側のモード内
   未検証）を順に検証。実在バグは発見次第修正し AUDIT に
   記録。根拠のない格上げはしない。

### 着手時の対象セルの棚卸し（AUDIT §2 の実数え）

- 優先1の B ◐: #3 IconButton/FAB（両モード）、#4 FAB menu
  （両）、#5 Split button（両）、#30 Navigation rail（両）、
  #31 Navigation drawer（両）、#32 Bottom sheet（両）。
  ほかに #29 BottomBar B（WinUI ◐）、#36 AppBar B
  （WinUI ◐）、#17 Badge B（両 ◐）、#20 TreeView B（両 ◐）、
  #19 ListView B（Android ◐）。
- 優先2の D ○: #27 Toast（両）、#28 TabBar（両）、
  #4 FAB menu（両）、#34 Carousel（両）、#6 Checkbox（両）、
  #7 Radio（両）、#9/#10 Slider（両）、#23/#24 Dialog
  modeless/full-screen（両、機構は基本形と共有・未検証）。
- 優先3: #11 Progress D（両 ◐、M3 キーフレーム忠実度が
  未検証）、#12 Ring D（両 ○、回転の画素検証なし）。

### 途中経過 1（実装フェーズ完了時点）

優先1（B）と優先2・3（D）の実装を一通り終え、既存スイート
が再び全緑になった（ツールキット 1,927 件全 PASS、内訳は
着手時と一致）。この時点ではまだ格上げは確定していない
（新規検証テスト test_tk12 と TK11_DARK が未実施）。

実装した内容:

- **テーマ**: 新トークン on_container（onPrimaryContainer:
  light #21005D / dark #EADDFF）と on_selection
  （onSecondaryContainer: light #1D192B / dark #E8DEF8）を
  追加。**実在バグ修正: v2_dark の selection トークンが
  primaryContainer の値（#4F378B）の複写になっており**、
  M3 ダークの secondaryContainer #4A4458 に是正した
  （ListView のダーク選択色もこの是正で仕様ロールに
  揃う）。
- **NavRail（#30）**: M3 指標ピルの色ロールを
  primaryContainer → secondaryContainer に是正
  （NavigationRailItemDefaults: indicatorColor =
  secondaryContainer）。押下アイテムの状態層（10%）が
  paint に無かったので追加（選択時は onSecondaryContainer
  ロール、非選択時は onSurface）。無効時はラベルを
  onSurface 38% に（指標色に無効バリアントは無いので
  指標は不変）。あわせて DOWN/UP で invalidate が無く
  押下形が描画されなかった問題も修正。
- **NavDrawer（#31）**: 選択ピルを secondaryContainer に
  是正（NavigationDrawerItemDefaults）。選択ラベルを
  onSecondaryContainer（新トークン、未設定テーマでは
  onSurface へフォールバック）に。無効項目のラベルが
  非選択と同色だったのを onSurface 38% に是正。
- **BottomBar（#29、家族一貫性）**: 指標ピルも同じ
  ロール誤りをしていたため btn_face_indicator フラグを
  新設して secondaryContainer に是正（トーナル全面形は
  primaryContainer のまま維持）。
- **Bottom sheet（#32）**: ドラッグハンドルの色を
  不透明な outline 色から onSurfaceVariant 40% に是正
  （M3 bottom sheet 仕様のハンドルロール）。寸法
  38×5px ≈ 32×4dp は従来どおり一致。
- **FAB（#3）**: 面ロールを filled ボタンの primary から
  FAB 仕様の primaryContainer + onPrimaryContainer に
  是正（btn_fab フラグ新設）。角半径が円形（size/2）
  だったのを M3 の角丸正方形（16dp ≈ 19px、small
  12dp ≈ 14px、large 28dp ≈ 33px）に是正。押下は
  コンテンツ色 10% の状態層。M3 に FAB の無効形の
  定義は無いため、無効形は filled ボタンの枠組み
  （容器 12% / 内容 38%）を対応付けとして採用（記録）。
  WinUI 形は従来の accent 面を維持（モード対応付け）。
- **Split button（#5）**: 内側角を全周ピル形から
  4dp ≈ 5px に是正。tk_canvas_fill_round_rect4
  （角ごとの半径）を新設し、FILLED 面の描画でのみ
  非対称角を適用（btn_split_side / btn_split_inner）。
  セグメント間ギャップ 2px は従来どおり一致。
- **Toast フェード（#27 D）**: タイムライン駆動で配線
  （表示期間の先頭 = フェードイン、末尾 = フェードアウト、
  明示 hide は現在 alpha からフェード。実クロックが無い
  / 表示と同一 tick の hide は従来どおり即時）。WinUI は
  Microsoft Learn のフェード値（Linear 83ms）。M3 側は
  AOSP の toast_enter/toast_exit 実値を本ラウンドでも
  取得できなかったため、M3 トークンスケールへの対応付け
  （short3 = 150ms、Standard decelerate）として記録し、
  AOSP 実値の取得は主張しない。
- **TabBar インジケータ（#28 D）**: 選択変更時に指標の
  左端をモード小ペア（200/167ms）でスライド。選択自体は
  即時（従来契約不変）。実クロック無しではスナップ。
- **Carousel settle（#34 D）**: ドラッグ終了・タップ・
  プログラム的 index 変更でオフセットがスナップ点へ
  大ペア（300/250ms）で settle するようにした。ドラッグ
  開始で settle をキャンセル。
- **Checkbox / Radio グリフ遷移（#6/#7 D）**: Switch の
  型で glyph_pos を配線（選択ペア: M3 200ms Standard /
  WinUI 167ms (0,0,0,1)）。M3 チェックボックスは容器の
  accent アルファ + マークのスケール、WinUI は面と
  グリフのアルファ、Radio はドットのスケールで遷移し、
  静止時は従来とピクセル同一になるよう描画順を調整。
  **実在バグ修正: チェックボックスのイベントハンドラが
  tk_checkbox_set_checked を経由せずフィールドを直接
  反転しており**、セッター側の配線だけでは遷移が一切
  動かなかった。イベント経路をセッター経由に是正。
- **Slider プログラム的値変更（#9/#10 D）**: 論理値と
  コールバックは即時（契約不変）、表示値（sld_disp /
  sld_disp2）だけをモード小ペアでアニメーション。
  ポインタ / キーボード / ColorPicker 内部同期は直接
  セッターに振り分け（ドラッグ中の追随遅延なし）。
- **Progress（#11 D）**: M3 不定形を MDC キーフレーム
  そのものに置換（material-web の _linear-progress.scss
  を再取得して区間イージングの対応まで確定: 周期
  2000ms、2本バー、基準左端 -145.167% / -54.8889%、
  区間ごとの cubic-bezier）。確定形の値遷移も同ソースの
  250ms cubic-bezier(0.4, 0, 0.6, 1) で配線（M3 のみ。
  WinUI に値遷移は無い）。tk_bezier_y を内部公開して
  キーフレーム評価に使用。
- **FAB menu（#4 D）**: 出現はオーバーレイ slide-in
  （自身の高さ分だけ下から上昇、小ペア）、退出は
  ダイアログと同じゲート付き slide-out（実クロックが
  進んだ場合のみ。headless / 同一 tick の hide は従来
  どおり同期削除）。

既存テストの前提是正（いずれも仕様駆動の挙動変更に
伴うもの。是正しないと新仕様の検証にならない）:

- test_tk5 test_toast: フェードイン完了後に全不透明の
  画素チェックを行うよう tick を追加（1件）。
- test_tk6 test_bottombar_pill: 指標ピルの期待色を
  container → selection に是正（ロール是正に追随）。
- test_tk11 progress_flow: M3 側は旧 sweep 公式の前提を
  キーフレーム前提（周期 2000ms、phase 0 で空、wrap）に
  書き分け。WinUI 側は従来どおり。精密な幾何照合は
  test_tk12 で独立参照実装と突き合わせる。
- test_tk7 WinUI Checkbox: 是正不要だった（イベント経路
  のバグ修正で従来前提のまま合格）。

残作業: test_tk12（新規検証: B 状態ピクセル + D の
開始/中間/終了値 + Progress キーフレーム独立照合 +
Dialog modeless/full-screen の D + Spinner 回転画素 +
TK11_DARK 機構）、消費者3スイートの再実行、AUDIT §2 の
更新と §11 の記録、本レポートの完成。

## 1. 結果サマリ（セル単位の実数のみ）

| モード | ● 検証済み | ◐ 部分検証 | ○ 実装のみ | △ | ✕ |
|---|---|---|---|---|---|
| Android（229） | 108 → **127**（55.5%） | 75 → 66 | 46 → 36 | 0 | 0 |
| WinUI（229） | 93 → **111**（48.5%） | 71 → 62 | 65 → 56 | 0 | 0 |
| 合計（458） | 201 → **238**（52.0%） | 146 → 128 | 111 → 92 | 0 | 0 |

- ● の増加は +37（Android +19、WinUI +18）。内訳は §2。
- テスト総数: ツールキット 1,927 → **2,140**（新規
  test_tk12 = 213 checks）。消費者3スイートは無修正で
  回帰ゼロ（アプリ 1,048 / シェル 683 / OSK 319、
  いずれも自分の実行で確認）。
- ダーク全体再実行: TK7_DARK 246/246、TK10_DARK
  128/128、**TK11_DARK 215/215（M13 で機構を新設し
  正式実施。優先4の完了）**。
- 本レポートは「完成」を主張しない。残セルは §4。

## 2. 格上げセルと根拠（仕様出典＋テスト）

テストは全て tests/test_tk12.c（213 checks、全て
自分の実行で PASS）。検証は開始/中間/終了の画素値で
行い、中間値はライブラリと独立な参照実装（密パラ
メトリックスキャンによる bezier 評価）で照合した。

### 2.1 B 観点（優先1: FAB / Split / Navigation 系）

| セル | 仕様出典 | 検証内容 |
|---|---|---|
| #3 IconButton/FAB B（両モード） | M3 FAB specs（m3.material.io）: 既定 container = primaryContainer、content = onPrimaryContainer、サイズ級 40/56/80/96dp、extended 高さ 56dp・角 16dp | 静止面・前景色、サイズ級別角半径の画素判別（角の内側/外側サンプル）、押下状態層、無効枠組み。WinUI は accent 面のモード対応付けを検証 |
| #5 Split button B（両モード） | M3E SplitButton トークン（pub.dev split_button_m3e トークン表＋Compose SplitButtonDefaults）: セグメント間 2dp、内側角 XS/S/M = 4dp | ギャップ 2px の画素確認、内側角 5px の非対称角（新設 tk_canvas_fill_round_rect4）、押下、コールバック発火。WinUI は均一角のまま（記録済み） |
| #17 Badge B（両モード） | M3 Badge / WinUI InfoBadge の dot・count 形 | スタンドアロン dot/count の画素、TabBar アンカー付き dot の位置と色 |
| #23/#24 Dialog B（両モード） | M12 取得済みの面ロール（M3 basic = surfaceContainerHigh、full-screen = surface、WinUI = surface） | modeless / full-screen × 両モードの面色を tk11 の dialog_face と同方式で照合。M3 full-screen の面が surface であることを実測で確定（当初前提の container_high から訂正） |
| #29 BottomBar B（WinUI） | NavigationView 下配置相当の選択表現 | accent バー＋選択状態の画素。Android 側は M12 で ● 済み、家族一貫性のため指標ピルのロール是正（下記バグ）も実施 |
| #30 Navigation rail B（両モード） | NavigationRailItemDefaults（developer.android.com）: indicator = secondaryContainer、selected label = onSurface、disabled = unselected × 0.38 | 指標ピル色、選択/非選択の押下状態層（onSecondaryContainer@10% / onSurface@10%）、無効ラベルの画素帯カウント |
| #31 Navigation drawer B（両モード） | M3 Navigation drawer の active indicator = secondaryContainer、active label = onSecondaryContainer、無効 = onSurface 38%。WinUI 無効 = TextFillColorDisabled | 選択ピル色、選択ラベルの画素帯、無効項目ラベル（M3: 38% ブレンド、WinUI: 半透明トークンのパネル上ブレンド） |
| #32 Bottom sheet B（両モード） | M3 Bottom sheet: container = surfaceContainerLow、ドラッグハンドル 32×4dp = onSurfaceVariant 40% alpha、scrim 32% | ハンドル色（M3: text_dim a=102 の実画素、WinUI: border トークンのブレンド）、容器色 |

### 2.2 D 観点（優先2・3: 未配線アニメーション）

全て tk_anim のモード別ペア（M3 / WinUI）で駆動。
ペアの出典は M12 取得済み（M3 モーショントークン、
Microsoft Learn の WinUI アニメーション値）。

| セル | 実装 | 検証内容 |
|---|---|---|
| #4 FAB menu D（両モード） | 出現 = スライドイン（小ペア 200/167ms）、退出 = ダイアログ退出と同じゲート付きスライドアウト | 出現時に自身の高さ分ずれた位置から着地すること、退出が同一 tick では即時・実クロック下ではスライドになること |
| #6 Checkbox D / #7 Radio D（両モード） | グリフ表示率 glyph_pos を選択ペア（200/167ms）で遷移。Checkbox はグリフ alpha とマークのスケール、Radio はドット径のスケール | 中間 tick での alpha 実測値と参照イージングの照合、Radio 2 個のドット画素数の増減 |
| #9 Slider D（両モード、**opt-in**、§3 参照） | 表示値 sld_disp を小ペアで遷移。論理値とコールバックは即時（契約不変）。ポインタ/キーボードは常時直接 | 表示 fill の端点プローブで開始 (0.2) / 中間 (≈0.65、参照 decelerate 照合) / 終了 (0.8)、ポインタセットの即時性 |
| #11 Progress D（Android のみ） | 不定形を MDC キーフレーム2本バーへ置換（周期 2000ms）。確定形は 250ms cubic-bezier(0.4, 0, 0.6, 1) の遷移（**opt-in**） | material-web の _linear-progress.scss（@material/web 2.3.0 を本セッションで再取得）にあるキーフレーム表・区間イージングを、独立参照表＋独立 bezier で 7 位相（0.125〜0.875）照合。位相 0.125 では両バーとも画面外（画素数 0）、2000ms で wrap |
| #12 Ring D（Android のみ） | 既存の位相エンジン（変更なし） | 不定形円形（Spinner）の回転を位相 0 と 250ms で 45°/315° プローブの反転で画素検証。確定形リング自体に仕様上の動きは無い（AUDIT §11 に明記）。WinUI は未検証のため ○ 据え置き |
| #23/#24 Dialog D（両モード） | 基本形と共有の dialog_show_impl（スライドイン 12px、小ペア）。実装変更なし、検証のみ | modeless / full-screen × 両モードで +12px 開始・中間・着地を tk11 の dialog_flow と同方式で照合 |
| #27 Toast D（両モード） | タイムライン駆動のフェード（transients_tick 内）。WinUI = Linear 83ms（Microsoft Learn、M12 取得）。M3 側は short3 150ms + Standard decelerate への対応付け（**AOSP の toast_enter/toast_exit 実値は複数回検索しても取得できず、取得したとは主張しない**） | サンプル点の alpha 0 → 中間 → 255、期限末尾のフェードアウト、明示 hide の現在 alpha からのフェード、headless（実クロック無し）では即時表示の従来契約 |
| #28 TabBar D（両モード） | インジケータ表示位置 tab_ind_x を小ペアでスライド。選択自体は即時 | 指標左端の開始/中間/終了を参照イージングで照合 |
| #34 Carousel D（両モード） | settle を大ペア（M3 300ms Emphasized decelerate / WinUI 250ms）へ。set_index・タップ・ドラッグ終了の全経路、DOWN でキャンセル | 項目レイアウトの独立シミュレーションと画素カウントの照合で開始/中間/終了を確認 |

## 3. 発見・修正したバグ

1. **v2_dark の selection トークンが primaryContainer
   値の複写**（実在バグ）: {79,55,139} は primaryContainer
   dark の値で、M3 dark の secondaryContainer は
   #4A4458 = {74,68,88}。是正によりダーク時の選択色
   （ListView 等）が仕様ロールに一致した。
2. **NavRail の押下形が描画され得なかった**（実在
   バグ）: navrail_paint に押下状態層が無く、
   navrail_event の DOWN/UP に invalidate も無かった。
   状態層の追加と invalidate 配線で修正。
3. **Checkbox のイベントハンドラがセッターを経由
   しなかった**（実在バグ）: ポインタ/キーの両経路が
   w->checked を直接反転しており、セッター側に
   グリフ遷移を配線しても一切動かなかった。セッター
   経由に是正。
4. **WinUI トーストが inverse ペアを使っていなかった**
   （M12 ダーク再実行で露呈、本セッションで修正を
   確認）: toast の paint がテーマの inverse ロールを
   参照するよう是正済み。
5. **スライダー/プログレスのプログラム的アニメー
   ション常時 ON が消費者の契約を破った**（M13 の
   実装が生んだ回帰）: シェルの test_session が
   決定的に 3 件失敗した。原因は、バックエンド同期
   （bd_shell_poll_backend）が音量/輝度スライダーへ
   値をセットするたびに 200ms 分の再描画尾が発生し、
   シェルの「静止時は転送ゼロ」契約が破れたこと
   （ダメージ矩形 (476,248) 252x96 = クイック設定の
   スライダーをバックトレースで特定）。プレイヤーの
   シークバー、レコーダーのレベルメーターも同じ
   反映系であり、アニメーションは実害になる。
   対策: **opt-in 化**（tk_slider_set_animate_values /
   tk_progress_set_animate_values、既定 off = 従来の
   スナップ）。M5 のスクロールシフト opt-in と同じ
   判断。opt-in 有効時のアニメーション自体は仕様
   どおりで、tk12 で検証済み。test_session は
   102/102 に回復し、消費者3スイートは無修正で
   回帰ゼロに戻った。
6. テーマに on_container（onPrimaryContainer）/
   on_selection（onSecondaryContainer）トークンを
   追加（末尾追加、未設定テーマは alpha 0 → 従来
   ロールへフォールバックする M9/M10 方式）。

既存テストの前提是正（仕様駆動の挙動変更に伴う、
3件）: test_tk5 のトースト画素確認をフェードイン
完了後に移動、test_tk6 の BottomBar 指標ピル期待色を
secondaryContainer へ、test_tk11 の progress_flow を
モード分岐化（M3 = キーフレーム前提、WinUI = 従来の
sweep 公式のまま）。

## 4. 残セル一覧と理由

合計 ◐ 128 + ○ 92 = 220 セルが未検証/部分検証の
まま残る。主な塊:

- **D 観点の残り**: #1/#2 Button D、#13〜#16、
  #18〜#21、#25、#33、#35、#36 の D は両モードとも
  ○（出現・状態遷移のアニメーションが未配線または
  未検証）。#10 Slider Range D は ○（アニメーション
  実装は単一形と共有だが Range 形でのテスト未実施）。
  #11 WinUI D は ◐（MDC キーフレームは M3 専用で、
  WinUI 側の根拠は M12 の sweep 検証のみ）。
  #12 WinUI D は ○（回転検証が M3 モードのみ）。
  #26 WinUI Snackbar D は ◐、#29 Android D は ○。
- **A 観点の ◐ 群**: 実機適合の記録済みデルタ
  （タッチフロア等）を持つ部品が多く、仕様換算値との
  完全一致を主張できないため ◐ のまま。新たな出典が
  得られた場合のみ再訪する（ListView 行高 Δ1 は
  AUDIT §2.4 の適応記録として据え置き確定）。
- **E/F 観点の ◐/○ 群**: #2 の E/F、#4 の E、
  #25 の E、#30/#31 の E、#35 の WinUI B/E 等、
  テキスト・アイコンとキー操作の網羅検証が未完。
- **#19 ListView B（Android ◐）/ #20 TreeView B /
  #36 AppBar B（WinUI ◐）**: 状態ピクセルの網羅が
  未完のため据え置き。
- 優先5（その他 ◐/○ の順次検証）は本セッションでは
  §2 の範囲までで、A/E/F の網羅には届いていない。
  これは未実施であり、実施したとは書かない。

## 5. コピー/オーバーヘッド削減

- 本セッションの変更は全て「状態が変わった時だけ」
  の範囲に閉じている: アニメーションは tk_anim の
  既存エンジンに乗り、静止時は追加の描画・タイマーを
  一切発生させない（トーストのフェードもタイム
  ライン駆動で、表示期間中のみ tick 対象）。
- スライダー/プログレスの opt-in 判断はこの原則の
  直接の適用である: 反映系の値セットに 200/250ms の
  再描画尾を付けると、静止時転送ゼロを前提に設計
  されたシェルの転送予算を恒常的に破る。既定では
  従来どおり1回の invalidate のみ。
- 追加した描画経路は tk_canvas_fill_round_rect4
  （非対称角）の1つだけで、既存の fill_round_rect_gen
  に角ごとの半径を渡す形にし、描画コードの重複を
  増やしていない。グリフ遷移は静止時に従来と
  ピクセル同一の描画順（fills → stroke → mark）を
  維持し、遷移中のみ alpha/スケールが入る。

## 6. キャッシュ/メモリ管理

- 新規のキャッシュ・バッファは追加していない。
  アニメーション状態は既存の tk_widget 内フィールド
  （glyph_pos、sld_disp 系、tab_ind_x、prog_disp）と
  ルートのトースト状態のみで、ヒープ確保は tk_anim
  の既存ハンドル管理に集約される。
- テーマトークンの追加は tk_theme 末尾への2色追加
  （8バイト）で、テーマは静的定義のため実行時コスト
  は無い。未設定テーマのフォールバックにより、旧
  テーマ定義を持つ消費者コードは無変更で動作する。

## 7. 実機の現実への対応

- **突然のリセット**: 本セッションは永続状態を
  追加していない（アニメーション表示値は全て揮発で、
  論理値は従来どおり即時確定する）。フェード中の
  トーストや遷移中のグリフが電源断で失われても
  論理状態は壊れない。
- **オフライン/時刻不信**: アニメーションは全て
  tk_root_tick の相対クロック駆動で、絶対時刻や
  日数ベースの有効期限は導入していない。トーストの
  表示期限も従来どおり tick 相対である。
- **microSD / eMMC**: 本セッションはストレージに
  一切触れていない。テスト用の一時ファイルは /tmp
  の使い捨てデバッグプログラムのみで、成果物には
  含めない（デバッグパッチは全て復元済みで、
  src/tk_core.c と shell/src/session.c に残存が
  無いことを確認した）。

## 8. 未検証の明記

- Toast の M3 側フェード値（150ms + Standard
  decelerate）は AOSP 実値への対応付けではなく、
  M3 モーショントークンへの対応付けである。AOSP の
  toast_enter/toast_exit は複数回の Web 検索で取得
  できなかった。
- FAB の無効形は M3 に公式定義が無い。12%/38% の
  枠組み（他の部品と共通の無効表現）で実装・検証
  したが、「仕様どおり」とは主張しない。
- WinUI の FAB/Split は Fluent に直接対応物が無い
  ためのモード対応付けであり、検証はその対応付けの
  範囲に限る。
- 実機（800×480・抵抗膜）での実測、QEMU ゲストでの
  実操作は本セッションの範囲外（統合フェーズの
  担当）。ホスト上の画素検証のみで合格を主張しない。
- test_session の失敗切り分けで使ったデバッグ計装
  （session.c / tk_core.c への一時パッチ）は全て
  復元済み。最終的な回帰確認はクリーンビルドで
  実施した。
