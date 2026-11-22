# 土台監査 — レイアウト前提とコード品質（2026-11-22）

ユーザーの指摘（「最初から全画面だけの前提で作ってしまった」「実装やコード
品質など、すべてが他に比べて虚弱すぎる」「成熟しているプロジェクトの
考え方を取り入れた方がいい。設計も」）を受けての棚卸し。手法と設計は
記憶ではなく一次資料で調べた（出典は末尾）。数値は全て実測・実数。

## 1. 成熟した設計から学んだこと（調査結果の要約）

### レイアウト
- **Android**: 描画は measure（計測）と layout（配置）の2パス。各ビューは
  親から MeasureSpec（EXACTLY=この寸法で / AT_MOST=上限まで /
  UNSPECIFIED=自由）を受け、制約を守った measured size を返す。親が子の
  配置に責任を持つ。木は invalid region と交差する部分だけ描かれ、親が
  子より先（後ろ）に描かれる。
- **Android WindowInsets**: ステータスバー/ナビバー等のシステム UI は
  プラットフォームが描き、その占有は「insets（上下左右の px）」として
  アプリへ渡る。アプリの内容がシステム UI に隠れないことが枠組み側の
  保証。バーは消えない前提で、アプリは残りの領域に描く。
- **WinUI (XAML)**: Measure/Arrange の2パス。Measure(availableSize) で
  DesiredSize を返し、Arrange(finalRect) で確定。レイアウトの無効化
  （InvalidateMeasure/InvalidateArrange）は非同期に処理され、変更は
  まとめて1回のレイアウト更新になる。
- **LVGL**: CSS の border-box が土台。座標・寸法はスタイルに保持され、
  変更は dirty フラグを立てるだけ。実際の再計算は描画前に
  「無効化フラグが無くなるまで」繰り返す遅延方式。LV_SIZE_CONTENT
  （内容に合わせる）と百分率寸法が寸法の基本語彙。flex/grid レイアウト。
  テストは Unity ベースで、スクリーンショット参照画像比較
  （TEST_ASSERT_EQUAL_SCREENSHOT、ref_imgs/）と gcovr による
  カバレッジレポートを CI で回す。

### 品質担保
- **サニタイザ**: ASan（メモリ誤り）+ UBSan（未定義動作）の併用が
  標準の組。ファジングとテストの両方に常時適用する。
- **ファジング**: libFuzzer はカバレッジ誘導型。パーサ等の入力を受ける
  部品に専用ハーネス（LLVMFuzzerTestOneInput）を用意し、種コーパス
  （正常・切断・境界の実例）から始める。クラッシュは最小化して
  回帰テストに残す。
- **カバレッジ計測**: LVGL は gcovr でレポートを生成し CI に組み込む。
  件数ではなくカバレッジ（行/分岐の到達割合）で網羅を測る。

## 2. レイアウト前提の棚卸し（全画面固定の残存）

寸法リテラル（854/480/800/272/410/436）の出現: 非テストのソースで
21ファイル。分類すると:

| 型 | 件数 | 例 | 判定 |
|---|---|---|---|
| A. アプリの要求寸法が 854×480 直書き | 10アプリ全部 | bd_app_display_open(title, 854, 480, 480, 272, ...) | **契約違反の源**。「最大化したい」という意図を画面寸法で表現している。WM 側は「作業領域以上なら最大化」の推測で拾っている（今回入れた仕組み）ため動くが、意図が寸法に化けている |
| B. パネル定数の正当な使用 | 数件 | shellui の作業領域計算、wm.h のコメント | 正本1箇所に集約すべき（現状も概ね集約） |
| C. コンテンツ設計寸法 | tk_osk.c の 480（グリーター/OSK の設計幅）、notes の 480×800（縦向きプレビュー用キャンバス） | 個別に正当。ただし「なぜその寸法か」のコメントがある物と無い物が混在 |
| D. 最小寸法の宣言だけあって執行なし | 全アプリが min 480×272 を宣言 | **WM に最小寸法の執行が無い**（wm.c に clamp が存在しない）。宣言と執行の分離が未完成 |

その他の構造的な弱さ（レイアウト）:
- **L-1 設定ホームの固定費過多**: ヘッダ 134px 固定＋行高 row_h2=73 が
  高さ 368 の窓で 2 行しか残さない。テーマに「詰めた形」への派生が無く
  （tk_theme_density は拡大方向のみ）、アプリ側も高さに応じた
  切り替えを持たない。→ 症状: 設定の切れ（ユーザー不合格1の実体）。
- **L-2 レイアウト結果の検証手段が無い**: 「子が親の内容領域に収まる」
  「負の寸法にならない」等を機械検査する仕組みが無く、破綻は目視頼み。
- **L-3 塗り責任の暗黙化**: fb 直描きモードの初回フレームは
  「レイアウトが全面を dirty にするはず」という暗黙の前提で全面が
  塗られる。WM モードは full paint で明示保証済み。契約として
  書き分けられていない。→ 症状: 黒い隅・端の温床。
- **L-4 マスコットのレイヤ規則が場当たり**: 可視判定が「ビューが
  デスクトップか」だけで、前面の窓を考慮しない（例外は全画面窓のみ）。
  → 症状: マスコットが窓の内容に被る（ユーザー不合格2の実体）。

## 3. コード品質の監査（型ごとの実数）

対象: src/（ツールキット）、shell/src、apps/common、apps 各モデル。

| 型 | 実数 | 深刻度 | 内容 |
|---|---|---|---|
| Q-1 表明（assert/invariant）の欠如 | **assert 使用 0 件**（全コード） | 高 | レイアウト・描画・WM の前提条件がどこにも表明されていない。破綻しても黙って描画が崩れるだけ |
| Q-2 危険な文字列関数の残存 | strcpy/strcat 14 件 | 高 | パス処理に集中（bdstore.c、files/fmodel.c、notes/nmodel.c）。出力バッファ寸法は呼び出し側の暗黙契約で、境界検査なし |
| Q-3 メモリ確保の失敗未考慮 | malloc 系呼び出し 319 箇所 | 中 | 大半は「失敗したら NULL を返す/使う側で弾く」慣習だが統一されておらず、サンプル確認でも混在。128MB 実機では確保失敗は通常事象 |
| Q-4 エラー返却の不統一 | tk.h の関数 365 件中 void が 247 件 | 中 | 失敗しうる操作（ファイル、確保）でも void が多く、失敗が伝播しない箇所がある |
| Q-5 寸法・色のマジックナンバー | 寸法は §2 のとおり。色は 0 件（テーマ経由が徹底） | 低（色）/中（寸法） | 色の規律は良い。寸法は型 A/D が問題 |
| Q-6 サニタイザ/静的解析/ファジングの未実施 | 0 件（今回が初） | 高 | テスト 3,700 件超は全て通常ビルド。メモリ誤り・UB は未検出のまま |
| Q-7 カバレッジ未計測 | 未計測 | 中 | 「テスト件数」は網羅の証拠にならない（ユーザー指摘どおり） |
| Q-8 層違反 | ほぼ無し | 低 | アプリ→shell は公開ヘッダ wm.h 経由のみ。ツールキット→アプリの逆依存なし。この点は健全 |

## 4. 症状の分類（視覚品質）

ゲスト撮影とホスト再現で確認した症状を型に分類:

| 症状 | 出た画面 | 型 |
|---|---|---|
| 設定で項目が2行しか見えない | 設定ホーム（窓 850×368） | L-1 固定費過多＋密度派生なし |
| 電卓の右列・4つ目のタブが見えない | 電卓（ゲスト撮影） | L-4 マスコットの被り（レイアウト自体はホスト再現で正常を確認済み。被りと「切れ」の見分けがつかなかったこと自体が L-2 の帰結） |
| マスコットが窓の内容に被る | 設定・メモ・電卓 | L-4 レイヤ規則の欠如 |
| 左下の黒いブロック | デスクトップ（v5） | fbcon カーソル（v6 で修正済み。塗り責任ではなくカーネルコンソールの問題と切り分け済み） |
| ブラウザ窓が画面下端を超える | ブラウザ浮動窓 | 窓の復元幾何が作業領域でクランプされていない（WM の配置規則の欠如。L-2 系） |

## 出典（2026-11-22 取得）
- Android Developers「How Android draws views」
  https://developer.android.com/guide/topics/ui/how-android-draws
- Android Developers「About window insets」
  https://developer.android.com/develop/ui/compose/system/insets
- Microsoft Learn「UIElement.Measure」「XAML custom panels overview」
  https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.uielement.measure
  https://docs.microsoft.com/en-us/windows/uwp/design/layout/custom-panels-overview
- LVGL 公式ドキュメント「Positions, sizes, and layouts」/ DeepWiki
  「Layout System」（遅延レイアウト更新の実装説明）
  https://lvgl.io/docs/open/8.0/overview/coords
  https://deepwiki.com/lvgl/lvgl/3.6-layout-system
- LVGL tests README（スクリーンショット比較・gcovr・CI）
  https://github.com/lvgl/lvgl/tree/master/tests （tests/README.md）
- LLVM libFuzzer ドキュメント / サニタイザ統合の実務資料
  https://releases.llvm.org/3.9.0/docs/LibFuzzer.html
- Playwright / Paparazzi・Roborazzi の視覚回帰（ゴールデン画像比較、
  閾値つき差分）の実務
  https://testingbot.com/support/web-automate/playwright/visual-regression-testing
  https://xckevin.github.io/en/blog/android-compose-screenshot-testing-paparazzi/
- レスポンシブ検証のマトリクス設計（ブレークポイント境界・状態軸）
  https://www.frontendcomponent.com/visual-regression-snapshot-strategies/responsive-viewport-testing/

## 追記: リーク計数（G-1 実施時の実測、2026-11-22）

ASan+UBSan でメモリエラー・未定義動作は全スイート 0 件。リークは実運用形
スイート（シェル全12、アプリのモデル/UI の大半、test_session_apps）で 0 に修正済み。
残存はテスト終了時にツリーを解放しない単体テスト分のみ（ライブラリの生成・破棄
サイクル自体は 200 回ループで無欠陥を実証）:
- test_tk5: 83,212 B / 114 件、tk6: 34,406/50、tk7: 44,392/62、tk8: 24,344/34、
  tk9: 307,590/432、tk10: 252,338/356、tk11: 218,302/308、tk12: 272,208/384、
  tk13: 606,797/856、tk14: 232,511/328、tk15: 130,433/184、tk16: 102,078/144、
  tk17: 278,292/398、tk18: 11,342/16（いずれもテスト側の未解放）
- アプリ側: test_tasks_ui 10,368 B/8 件、test_calc_ui 519,917 B/792 件
  （calc_app_new のツリーをテストが解放していない分）、test_alarmd 180 B/9 件。
  他のアプリスイートは全てリークなし。
