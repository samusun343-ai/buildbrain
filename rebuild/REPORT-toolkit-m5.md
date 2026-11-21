# REPORT — ツールキット本格強化（M5、2026-11-20）

対象: `rebuild/` の libbrainui2（include/・src/・tests/）＋シェルの
代表遷移への配線（shell/src/session.c、shell/braindesk2.c）。
公開 API の互換は維持（追加のみ）。repo へは push していない。

## 1. 不足リスト（監査結果、優先順）と処置

出典: 大手（GTK/LVGL/Material）部品表との差分（DESIGN §9）、
アプリ群レポート（apps/REPORT-apps-wave1.md §5、wave2 §8）、
コード実読による性能監査。

| # | 不足 | 処置 |
|---|------|------|
| 1 | スクロール転送が −7.7% しか効かない（M3 実測の弱点） | シフト高速パスを実装（§2）。−96.7% |
| 2 | 描画ウォークにカリングなし（画面外ウィジェットも全走査） | tk_paint にサブツリーカリング、ListView に行カリング |
| 3 | グリフ描画がピクセル毎に clip 判定＋1×1 damage | グリフ単位の一括ブレンド（算術同一） |
| 4 | アニメーション基盤が皆無（開閉がカクッと切替わる） | tk_anim.c 新設＋シェル配線（§3） |
| 5 | セッタ自動 invalidate が一部のみ（§13 の原則が未全面化） | 旧セッタ全域へ展開（変化時のみ） |
| 6 | 画像の縮小デコード/サムネイル API なし | tk_image_scale_to_fit / load_thumbnail / imageview_load_thumbnail |
| 7 | ファイル選択ダイアログなし | tk_filedialog_*（Dialog 合成） |
| 8 | ListView 行が文字列のみ（チェック行・見出し行なし） | チェック行＋見出し行を追加 |
| 9 | 日付/時刻ピッカーなし | TK_W_DATEPICKER / TK_W_TIMEPICKER |
| 10 | textcore に検索 API なし（notes が全⽂スナップショットで回避） | tc_find / tc_find_next |
| 11 | 窮屈さを一括で直す基盤なし | 密度トークン tk_theme_density |
| 12 | 角丸/ピル多用の塗りコスト | fill_round_rect を行直接書込み化、fb flush を bpp 別特化 |

未処置（次フェーズ候補）: WM プロトコルへのシフト通知（クライアント
アプリの ScrollView 高速化にはプロトコル拡張が前提）、JPEG の
IDCT スケーリングデコード（現状は全デコード後の縮小）、
TextView の行単位シフト（ListView 的行構造を持たないため未適用）。

## 2. 性能（実測、800×480・16bpp、同一ベンチソースで旧/新を対向）

計測: tests/bench_paint.c を pristine ライブラリ（作業前スナップ
ショット）と新ライブラリの双方にリンクして比較。ホスト競合下で
時間はぶれるため、転送バイト数を決定指標とした。

| 項目 | 旧 | 新 | 備考 |
|------|----|----|------|
| スクロールフレーム転送（blit on） | 731,200 B | **24,350 B（−96.7%）** | dy=13〜40px の連続スクロール |
| スクロールフレーム時間（blit on） | 4.9〜9.7 ms | 0.22 ms | 参考値（ホスト競合あり） |
| 60 行ツリー中の 1 ラベル再描画 | 3.15〜3.56 ms | 0.06〜0.10 ms | カリング効果 |
| リスト画面フル描画 | 6.1〜6.8 ms | 3.3〜3.5 ms | グリフ一括化＋round_rect |
| M3 bench_damage（blit off 既定経路） | スクロール 240,960 B 等 | **全項目同一** | 既定経路は無回帰 |

シフト高速パスの設計要点:
- root 単位 opt-in（`tk_root_set_scroll_blit`、既定 off）。WM の
  COMMIT はダメージ矩形のみでシフトを表現できないため、コンポ
  ジタ経由のクライアントで有効化すると保持領域が古くなる。
  直接 fb のシェルセッション（bd_session が fb 保有時）が opt-in
  済みで、ランチャーの検索結果リストが実効対象。
- 発動条件は保守的: 描画済み状態が既知（全描画 or 前回シフトで
  確定）、内容 dirty でない、損傷矩形が SV 全域を包含、
  |dy| < ビューポート高。内容変更・子孫の invalidate・対象
  イベントは dirty 化して全再描画へフォールバックする。
- 再描画域は露出帯＋スクロールバーのサム移動域。旧サムの
  ピクセルは内容シフト量だけずれて残る（ゴースト）ため、その
  域も再描画域に含める（テストで画素不一致として検出・修正済み）。
- 正しさ: test_tk4 が「シフト経路 vs 全再描画」で全ピクセル
  memcmp 一致を検証（下スクロール/連続/上戻し/内容変更後の
  フォールバックまで）。

## 3. アニメーション基盤（src/tk_anim.c）

- 整数 1 値＋イージング（linear / ease-out cubic /
  ease-in-out cubic）の時間ベース補間。`tk_root_tick` 駆動。
- 標準 duration 160 ms（デザイントークン 150–200 ms）。
  フレーム予算: 補間は壁時計基準なので、メインループが粗く
  ても「中間フレームが減るだけ」で duration は伸びない。
  braindesk2 は `tk_root_anim_active` の間だけステップを
  50 ms → 16 ms に短縮（実機では転送帯域が律速し自動間引き）。
- 無効化: `tk_root_set_animations_enabled(root, false)` で
  `tk_anim_start` が最終値を同期配送（設定での無効化要求に対応）。
- 適用箇所: Switch ノブのスライド（150 ms、論理状態は即時
  切替）、Dialog 出現（+12 px から ease-out）、Menu 出現
  （−6 px）。UI-REDESIGN の制約（浮動面の出現のみ、フェード
  なし、全画面遷移なし）に従う。QS/ランチャー/OSK はいずれも
  Menu/Dialog 系の浮動面として表示されるため同じ経路で動く。
- ウィジェット解放時は root 経由で cancel（UAF 防止を
  test_tk4 の範囲外だが free パスに配線済み）。

## 4. API 追加一覧（全て追加のみ、既存シグネチャ不変）

- アニメ: tk_anim_start / tk_anim_cancel / tk_root_anim_active /
  tk_root_set_animations_enabled / tk_ease_* / TK_ANIM_DURATION_MS
- スクロール: tk_root_set_scroll_blit / tk_root_scroll_blit
- 検索: tc_find / tc_find_next（textcore）
- 画像: tk_image_scale_to_fit / tk_image_load_thumbnail /
  tk_imageview_load_thumbnail
- ファイル選択: tk_filedialog_new / show / set_on_result / dir /
  refresh、tk_filedialog_cb
- ピッカー: tk_datepicker_* / tk_timepicker_*、
  TK_W_DATEPICKER / TK_W_TIMEPICKER
- ListView: tk_listview_set_checkable / row_checked /
  set_row_checked / last_checked / set_on_check / set_header /
  is_header
- テーマ: tk_theme_density
- 挙動変更（互換範囲内）: 旧セッタ群が「変化したときだけ」
  自動 invalidate するようになった。従来は手動 invalidate が
  必須だったため、既存アプリは無変更で正しく再描画される
  （アプリ側の D テスト「setter-only change で描画される」が
  全アプリで PASS）。

## 5. テスト結果（全スイート、同一手法で再実行）

| スイート | 結果 | 基準 |
|----------|------|------|
| ツールキット（make check、7 本） | **764 PASS / 0 FAIL** | 641（+tk4 新規 119、tk3 は他ライン増分 +4） |
| シェル（make check、10 本） | **588 PASS / 0 FAIL** | 588（配線変更後も同一） |
| アプリ（make check、19 本） | **1,048 PASS / 0 FAIL** | 1,048 |
| OSK | **319 PASS / 0 FAIL** | 319 |

新規テスト tests/test_tk4.c（119 件）: シフト画素等価、
カリング等価、アニメ（値遷移・端点・cancel・無効時同期配送）、
Switch ノブ、ダイアログスライド、セッタ全面化（変化時のみ
damage が立つ/立たないの双方）、tc_find（ギャップ跨ぎ含む）、
画像スケール（面積平均・α 重み・拡大なし・縦横比）、
ピッカー（2024-02-29、月末クランプ、時刻ラップ）、
ListView 複合行（チェックトグル/見出しスキップ/move_row 追随）、
ファイルダイアログ（実 /tmp 木で遷移・選択・フィルタ）、密度。

## 6. 環境メモ

- 作業中、/tmp（512MB tmpfs）が他ラインの /tmp/shimeji-src
  （434MB、マスコット調査用）で枯渇しコンパイルが失敗した。
  自分のデバッグ生成物のみ削除し、以後のビルドは TMPDIR を
  ホーム側へ逃がして完走。shimeji-src は他ラインの作業物の
  ため未処置（親エージェント判断待ち）。
- braindesk2 --check: 正常（first frame 768,000 B、quiet 0 B）。
