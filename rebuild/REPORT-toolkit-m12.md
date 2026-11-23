# REPORT-toolkit-m12 — GUI ライブラリ同等化の継続 第5弾（M12）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


2026-11-20。対象: `include/`・`src/`・`tests/` と AUDIT のみ。
repo へは push していない。正本マトリクスは AUDIT-toolkit.md §2。

着手時点（M11 到達点、REPORT-toolkit-m11.md が正本）:
● 190/458（41.5%）、◐ 144、○ 124、△ 0、✕ 0。
モード別 ● は Android 102/229、WinUI 88/229。
ツールキット 1,703 tests 全 PASS、消費者（アプリ 1,048 /
シェル 683 / OSK 319）回帰ゼロ。**完成ではない。**
残りは 268 セル。本ラインの目的もセルの格上げのみ。

本ラインで据え置き（触らない）: Android Slider の形
（M3 Expressive ピル型ハンドルはユーザー判断待ちの提案として
分離済み）、ListView 行高 Δ1（AUDIT §2.4 の適応記録どおり）。

## 0.5 途中経過（着手時）

### 着手時の優先順位（タスク指定）

1. D 観点（アニメーション）の網羅 — ○ の大半を占める最大の
   残塊。D 列の現状（AUDIT §2 の実数え）: 両モード合計
   72 セル中 ● 2（Split chevron のみ、両モード）、◐ 8、
   ○ 62。
2. WinUI 側の A/E/F 観点と ColorPicker 等のモード内未検証セル。
3. FAB / Split button / Navigation 系の B ◐。
4. WinUI ダークでの tk10 全体再実行（M11 で未実施）。
5. 実在バグは発見次第修正し AUDIT に記録。

### 着手時のアニメーション実装の棚卸し（実読）

tk_anim（src/tk_anim.c）: 整数1値の補間エンジン。壁時計基準
（tk_root_tick の now_ms）、イージングは linear /
ease_out_cubic / ease_in_out_cubic の3種。アニメーション
無効化（reduced motion）時は最終値を同期配送する。
配線済みの部品は次の5経路のみ:

- Switch ノブ: 150ms、ease_out_cubic（tk_widgets.c）。
- Progress 不定（linear sweep / circular spinner）:
  位相が 1200ms 周期で自己再起動（tk_widgets.c）。
- Split button chevron: 200ms 回転（tk_m7.c）。
  D は両モード ● 済み（tk7）。
- Dialog 出現: +12px から 160ms で上昇（tk_overlay_slide_in、
  TK_ANIM_DURATION_MS=160、ease_out_cubic）。
- Menu 出現: −6px から 160ms で落下（同上）。

未配線（出現・状態変化が即時）: Drawer、Bottom sheet、
Snackbar / Toast、TabBar インジケータ、FAB menu、
Carousel のスナップ settle、Checkbox / Radio の状態変化、
Slider のプログラム的値変更、他。

### 仕様取得（Web・一次ソース、2026-11-20 本セッション）

- M3 モーショントークン（m3.material.io/styles/motion/
  easing-and-duration/tokens-specs）: duration は short1-4 =
  50/100/150/200ms、medium1-4 = 250/300/350/400ms、long1-4 =
  450/500/550/600ms、extra-long1-4 = 700-1000ms。明文で
  「Selection controls have a short duration of 200ms with
  Standard easing」。イージング実値: Standard =
  cubic-bezier(0.2, 0, 0, 1)、Standard decelerate = (0,0,0,1)、
  Standard accelerate = (0.3, 0, 1, 1)、Emphasized
  decelerate = (0.05, 0.7, 0.1, 1)、Emphasized accelerate =
  (0.3, 0, 0.8, 0.15)。
- WinUI（Microsoft Learn「Timing and easing」/「Motion in
  Windows」）: ControlFastAnimationDuration = 167ms、
  ControlNormalAnimationDuration = 250ms、
  ControlFasterAnimationDuration = 83ms。Direct Entrance の
  イージングは cubic-bezier(0,0,0,1)（= M3 Standard
  decelerate と同一曲線）、フェードは Linear 83ms。
- M3 の部品別スペックページ（progress-indicators /
  navigation-drawer / dialogs の /specs）にはモーションの
  数値はテキストとして載っていない（トークンモジュールは
  JS 駆動）。よって部品別の対応付けは上記トークン体系への
  マッピングとして行い、その旨を AUDIT に記録する（部品別
  仕様値の取得を主張しない）。

### モード別アニメーション対応表（M12 の配線値）

| 対象 | M3 モード | WinUI モード |
|---|---|---|
| Switch ノブ | 200ms / Standard (0.2,0,0,1) | 167ms / (0,0,0,1) |
| Dialog・Menu 出現（+ Dialog 退出） | 200ms / Standard decelerate | 167ms / (0,0,0,1) |
| Drawer・Sheet 出現/退出 | 300ms / Emphasized decelerate | 250ms / (0,0,0,1) |
| Snackbar 出現（M3 形のみ） | 200ms / Standard decelerate | 出現モーションなし（InfoBar） |
| Split chevron | 200ms（M8 のまま、両モード ● 済み） | 同左 |
| Progress 不定 sweep | 1200ms 線形位相（M6 のまま） | 同左 |

### 実装の途中経過（第1段）

- tk_anim に cubic-bezier 評価を追加し、tk_ease_standard /
  tk_ease_standard_decelerate / tk_ease_emphasized_
  decelerate を新設（tk.h に公開）。モード別ペアの決定は
  tk_anim_mode_pair（tk_anim.c）に集約。
- スライド機構を一般化: 従来は面の rect だけが動き、
  ダイアログの子は最終位置に残る不整合があったため、
  サブツリー全体が面と一緒に移動するよう修正。横軸
  （サイドシート）と slide-out（退出→形状復元→完了通知）
  を追加。
- Switch: 150ms ease_out_cubic → モード別ペアに変更。
- Dialog: tk_dialog_close に退出スライドを追加。ただし
  退出が発動するのは「表示中にルートの時計が進んだ」
  （実セッションループが回った証拠がある）場合のみで、
  ヘッドレス/一発用途は従来どおり同期クローズ。
- Drawer: drawer_off によるパネルスライド（出現・退出）
  ＋スクリムの進捗連動フェードを追加。Sheet: slide 機構で
  出現・退出（ドラッグ却下も同じ経路に合流）。
- Snackbar: M3 形のみ出現スライド（200ms）を追加。
  WinUI 形（InfoBar）は出現モーションなしを維持。
- この時点で既存スイートは tk8 の1件（Snackbar を tick
  済みルートで表示し即座に最終位置のピクセルを期待する
  検証）を除き全 PASS。その1件は前提側の誤り（出現
  スライドで意図的に動く）として、出現を走り切ってから
  探る形に tk8 側を是正し、全 1,703 件 PASS に戻した。
  出現スライド自体の検証は tk11 で行う。

### 実装の途中経過（第2段: 優先順位4 = tk10 ダーク再実行）

- test_tk10 に TK7_DARK と同方式の TK10_DARK=1 機構を新設
  （tk10_map で winui→winui_dark に読み替え。描画も期待値も
  テーマ相対）。初回は **116/119**（失敗3件）。切り分け:
  - FAIL 853（Slider Range 無効・中央）= **テスト期待値の
    誤り**。中央はつまみ間の選択区間で、実装は無効トラック
    df の上に無効フィルを重ねた blend(control_fill_dis, df)
    を描く（実測: light 245・dark 50、ともにその合成値。
    旧期待 df は light 244 と偶然 tol 3 内、dark は 41 vs
    50 で露呈）。直下の lo つまみ検証が同じ合成式で両テーマ
    通過していることとも一致。期待値を合成式に是正。
  - FAIL 895/896（Toast WinUI）= **実装側の実在バグ**。
    WinUI トーストは文書・テストが主張する inverse
    ペアリングを使っておらず surface/text で描画していた。
    light では surface {255,255,255} = inverse_on_surface、
    グリフ色 {27} ≈ inverse_surface {32} と数値的に一致
    するため偶然通過し、dark で乖離が露呈（M10 の tk7
    ダーク再実行が暴いたロール取り違えと同型）。実装を
    inverse ペア（inv_bg/inv_fg、角6・枠は維持）に修正し、
    テストのグリフ期待値もトークンからのブレンド算術に
    是正（旧形は ink の alpha=255 前提で dark の alpha=228
    を数えられなかった）。
- 是正後: **通常実行 119/119、TK10_DARK=1 でも 119/119**。
  優先順位4は完了。バグの正式記録は §3 / AUDIT §10 へ。

### 実装の途中経過（第3段: tk11 完成と最終結果）

- test_tk11.c（D 観点専用 215 件）を新規作成し
  Makefile に配線。全節 PASS（エンジン契約、bezier
  独立参照照合、Switch / Dialog / Menu / Drawer /
  Sheet / Progress / Snackbar の両モード検証）。
- 優先順位2の一部として test_tk10 に §M（WinUI
  TextView A、Dialog modeless/full-screen A、トグル
  E/F、9 件）を追加。通常・TK10_DARK とも全 PASS。
- 最終結果と残セルは §1〜§9 に集約した。

## 1. 結果サマリ（セル単位の実数のみ）

| モード | 適用セル | ● 検証済み | ◐ 部分検証 | ○ 実装のみ | △ | ✕ |
|---|---|---|---|---|---|---|
| Android | 229 | 102 → **108**（47.2%） | 79 → 75 | 48 → 46 | 0 | 0 |
| WinUI | 229 | 88 → **93**（40.6%） | 65 → 71 | 76 → 65 | 0 | 0 |
| 合計 | 458 | 190 → **201**（41.5% → **43.9%**） | 144 → 146 | 124 → 111 | 0 | 0 |

（AUDIT §2 の表からの機械集計と一致。● +11、◐ +2、
○ −13。）**完成ではない。** 残りは ◐ 146 + ○ 111 =
257 セル。

テスト総数: ツールキット 1,703 → **1,927**
（新規 test_tk11 = 215、test_tk10 増分 +9）。
消費者回帰ゼロ（アプリ 1,048 / シェル 683 / OSK 319、
いずれも自分の実行で確認）。

## 2. 格上げセルの根拠（仕様出典 + テスト）

仕様出典は全て §0.5 の一次ソース（M3 モーション
トークン、Microsoft Learn の WinUI アニメーション
トークン）。検証は新規 test_tk11.c（215 件）が担い、
期待値はエンジンの整数式
`from + (int)((to - from) * ease(t) + 0.5f)` と、
ライブラリの Newton ソルバとは独立に書いた密サンプル
参照 bezier から導出した（実装同士のなれ合いにしない）。

### ● への格上げ（11 セル、全て D 観点）

| セル | 検証内容（tk11 の節） |
|---|---|
| #39 基盤 D（両モード） | §A: 線形補間の正確な配送値、壁時計基準（tick 回数を変えても同一絶対時刻で同値）、完了値の正確さ、done は1回だけ、キャンセル後の無配送、中断→現在値からの再開（エンジン式どおり）、reduced-motion の同期配送。§B: 3種のトークン曲線が独立参照と全11点で一致、端点の正確さ、単調性 |
| #8 Switch D（両モード） | §C: ノブ中心が 1/4・1/2・終了時点でエンジン式どおり（モード別ペア: M3 200ms Standard / WinUI 167ms (0,0,0,1)）、途中反転が配送済み値から再開（スナップなし）、reduced-motion で即時確定 |
| #22 Dialog D（両モード） | §D: 出現 +12→0（1/4 時点の式一致、タイトルグリフが面と一緒に移動 = サブツリー移動の実測）、退出スライドと除去タイミング、時計が進んでいない場合の同期クローズ（live-loop ゲート）、reduced-motion の同期クローズ |
| #31 Drawer D（両モード） | §F: パネル可視幅が中間・定常で式どおり、スクリム alpha が進捗比例（中間時点でブレンド実測）、退出と除去タイミング |
| #32 Sheet D（両モード） | §G: bottom / end 両エッジの出現・退出、on_dismiss が退出完了時に1回だけ、スクリムタップが同じアニメーション付きクローズを通る |
| #26 Snackbar D（Android のみ） | §I: 出現スライド（span→0、200ms Standard decelerate）が 1/4・1/2・終了で式どおり |

### ◐ への格上げ（WinUI の ○→◐ 7 セル）

- #21 Menu D（WinUI）○→◐: 出現 −6→0 を式で検証
  （tk11 §E、両モード）。退出は即時（アニメーション
  なし）が仕様相当の振る舞いとして残るため ● にしない。
  Android 側は元々 ◐ で、今回の検証で根拠が精密化した
  （セルの移動なし）。
- #26 InfoBar D（WinUI）○→◐: 出現モーションが無い
  ことを、出現窓の前後でキャンバス全体が画素一致する
  ことで検証（tk11 §I）。「無いこと」の検証のみのため
  ● にしない。
- #14 TextView A（WinUI）○→◐: 測定寸法がテーマ
  トークン式（最長行 + 2*unit、下限 120 / 行数 ×
  line_height + 8）と WinUI トークンで一致（tk10 §M）。
- #23 / #24 Dialog A（WinUI）○→◐: modeless が
  modal（A は ●）と同一ジオメトリ、full-screen が
  ルート全体を占めることを実測（tk10 §M）。
- #2 トグル E / F（WinUI）○→◐: ラベルが text ロール
  で描かれること、フォーカス + Space / Enter で
  checked が反転すること（tk10 §M）。アイコン併記形と
  フォーカス巡回順は未検証のため ◐ 止まり。

### 据え置き（検証したが格上げしない）

- #11 Progress D（両モード）◐ のまま: 自前 sweep の
  位相位置・ラップ（空になる）・再入・周期性・
  reduced-motion 静止を tk11 §H で精密検証したが、
  M3 不定形アニメーションのキーフレーム（区間の伸縮）
  への忠実度は未検証であり、当方の sweep は定幅移動
  という設計差があるため ● にしない。
- #12 Ring / Spinner D（両モード）○ のまま: 位相
  エンジンは #11 と共有するが、回転自体の画素検証を
  していない。
- #27 Toast D（両モード）○ のまま: フェードは
  未配線（出現・消退は即時）。

## 3. 発見・修正したバグ

AUDIT §10 に正式記録。要点:

1. **WinUI トーストが inverse ペアリングを使って
   いなかった**（実装バグ）。light では surface/text
   が inverse ペアと数値一致するため M11 のテストが
   偶然通過し、M12 の TK10_DARK 全体再実行で露呈。
   実装を inverse ペアに修正。
2. **Slider Range 無効形テストの期待値誤り**
   （テスト側）。中央 = 選択区間の合成色が正しく、
   旧期待は light で偶然通過していた。是正済み。
3. **スライドが面の rect だけを動かしていた**
   （実装バグ、着手時棚卸しで発見）。子が最終位置に
   残る不整合を、サブツリー全体の移動に修正。
4. **tk8 の Snackbar 前提 1 件**（テスト側）: 出現
   スライド配線と矛盾する前提を是正。

このほか、M12 の実装過程で切り分けた「失敗に見えた
正常挙動」: Switch 反転直後のノブ移動量は WinUI の
decelerate 曲線が前倒し型のため大きく、テスト前提を
エンジン式ベースに書き換えた（実装は仕様どおり）。

## 4. 残セルと理由

- **D 観点の残り**: 未配線のままの部品 — Toast
  フェード、TabBar インジケータのスライド、FAB menu
  の出現、Carousel の settle、Checkbox / Radio の
  状態グリフ遷移、Slider のプログラム的値変更の
  アニメーション、Dialog modeless / full-screen の
  出現（基本形と機構は共有するが未検証）。これらは
  「配線 + 仕様確認 + テスト」が1セットで残作業。
- **WinUI 側の残り ○**: #3/#4 の E・F、#10 の E、
  #16 の E・F、#20 の E・F、#23/#24 の B 以外、
  #25 の E、#30/#31/#32 の E・F、#34 の E・F、
  #35 ColorPicker の A・B・F、#36 の F 等。
  ColorPicker は themeresources の部品別 XAML 取得
  から要る。
- **B 観点の ◐ 残り**: FAB / Split / Navigation 系の
  状態ピクセル網羅（優先順位3）は本ラインでは
  未着手（D 網羅と ダーク再実行を優先した）。
- **Android 側の残り ○**: #2 の F、#3/#4 の F、
  #9/#10 の D（プログラム的値変更、未配線）、
  #23/#24 の D、#27 の A・D、#35 の周辺等。

## 5. コピー / オーバーヘッド削減

- bezier 評価は固定回数反復（Newton 8回上限 +
  二分ブラケット）で割り当てゼロ。アニメーションの
  毎フレーム処理は整数式1回と damage 帯の追加のみ。
- スライドの damage は「前フレーム位置と現在位置の
  差分帯」に限定（全面再描画にしない）。スライド
  完了時の land は正確な1回移動のみ。
- スライド機構の一般化で、面と子の不整合を直すために
  毎フレームの再レイアウトは行わない（rect の再帰
  シフトのみ、レイアウト再計算なし）。
- テスト側の参照 bezier（密サンプル 20001 点）は
  テスト専用コードで、ライブラリには入っていない。

## 6. キャッシュ / メモリ管理

- アニメーションは tk_anim 1件 = 固定小構造体
  （calloc 1回、完了/キャンセルで回収）。同時実行数
  は表示中オーバーレイ数に比例する上限で、無制限な
  蓄積はしない。
- 新規の永続キャッシュは追加していない。モード別
  ペア（継続時間・イージング）の決定はテーマ参照の
  みで、状態を持たない（M8 で問題になった metrics
  永久キャッシュのような構造は作っていない）。

## 7. 実機の現実への対応

- **突然のリセット**: アニメーション状態は全て
  メモリ内の一時状態で、永続化しない。リセットで
  失われても UI が初期状態から再構築されるだけで
  壊れない（キャッシュ使い捨て前提と同型）。
- **低速 CPU / 粗いメインループ**: エンジンは壁時計
  基準のため、50ms ポーリングでもアニメーションの
  総時間は伸びず中間フレームが減るだけ（tk11 §A で
  「tick 回数に依存しない」ことを検証済み）。
- **reduced-motion**: 設定でアニメーションを切ると
  全経路が同期確定に落ちる（tk11 でエンジン・Switch・
  Dialog・Progress の4経路を検証）。実機の抵抗膜
  1本指操作でアニメーション待ちが操作をブロック
  しないための必須経路。
- **オフライン・時刻不信**: アニメーションは単調な
  相対時刻（tick の now_ms）だけで動き、暦時刻・
  有効期限の概念を使わない。

## 8. 未検証の明記

- 本ラインの検証はホスト上のユニットテストのみ。
  SD イメージ / QEMU ゲスト / 実機でのアニメーション
  実測（実フレームレート、実 CPU 負荷）は未実施
  （統合フェーズ）。
- WinUI ダークでの tk11 全体再実行は未実施（tk11 は
  両テーマとも light 系トークンで検証。ダークでの
  検証は tk10 の TK10_DARK と tk7 の TK7_DARK が担い、
  tk11 のダーク版は今後の課題として残す）。
- M3 部品別スペックページにモーション数値が無い
  部品の対応付けはトークン体系へのマッピングであり、
  部品別仕様値そのものの取得は主張しない（§0.5）。
- 優先順位3（FAB / Split / Navigation の B ◐）は
  未着手。優先順位2も §M の5セル分のみで、残りは
  §4 のとおり残っている。

## 9. テスト実行結果（自分の実行で確認）

| スイート | 結果 |
|---|---|
| ツールキット `make check` | **1,927 passed, 0 failed**（内訳: textcore 118 / tk 83 / tk2 121 / font 22 / kbd 131 / tk3 191 / tk4 119 / tk5 127 / tk6 205 / tk7 246 / tk8 71 / tk9 150 / tk10 128 / tk11 215） |
| tk10（TK10_DARK=1） | **128 passed, 0 failed** |
| tk7（TK7_DARK=1） | **246 passed, 0 failed** |
| アプリ `make check` | **1,048 passed, 0 failed** |
| シェル `make check` | **683 passed, 0 failed** |
| OSK `make check` | **319 passed, 0 failed** |
