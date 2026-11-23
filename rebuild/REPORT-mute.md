# REPORT-mute — ミュートの消費側をシェルに実装

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


作成: 2026-11-21（REPORT-mediakeys.md の後継。tk_kbd が発行する
TK_ACTION_MUTE の消費側が存在しない件への対応）
範囲: `shell/include/shellui.h`、`shell/src/shellui.c`、
`shell/src/session.c`、`shell/tests/test_shell.c`、
`shell/tests/test_session.c`。commit / push なし。

## 0. 途中経過（設計確定時点）

前提確認（コードを直接読んで確定）:
- 音量モデルは `bd_shell.volume`（0..100、reg `shell/volume` に
  永続化）が唯一の状態で、`bd_shell_set_volume()` が全ての
  変更経路（セッションのアクションキー、QS スライダ、assist
  オーバーレイの音量ボタン）の合流点。backend へは
  `bd_backend_volume_set()` 経由で ALSA Master の整数値を書く
  （backend.c はミュートスイッチを使っていない）。
- 表示面: トレイアイコン（`glyph_volume`、level 0 で × の
  ミュート形になる）、QS のスライダ＋% ラベル、assist の
  ステータス行。OSD（操作時の一時表示）は存在しない。
- `bd_shell_poll_backend()` はハードウェア音量をモデルへ
  読み戻す。ミュート中に実効 0 をハードウェアへ書くと、
  読み戻しが保存レベルを 0 で潰す相互作用がある。

設計（意味論）:
- ミュートは音量レベルとは独立した bool 状態として保持し、
  reg `shell/muted` に永続化する（wifi と同じ "true"/"false"
  形式）。設定レベル（`shell/volume`）はミュートで書き換えない。
- 実効音量 = muted ? 0 : volume。backend（ALSA）へ書くのは
  常に実効音量。既存 backend にミュートスイッチ経路が無い
  ため、実効 0 の書込みがこのスタックでの消音手段になる。
- **ミュート中の音量変更はミュートを解除する**
  （unmute-on-adjust）。根拠: (1) ハードウェア音量キーの
  一般的な実装（Android / Windows / GNOME とも、ミュート中の
  音量操作で解除される）がこの規則。(2) 本シェルでは全ての
  音量変更が `bd_shell_set_volume()` に合流するため、「現在と
  異なるレベルへの明示的な変更がミュートを解除する」という
  1規則を合流点に置けば、キー・スライダ・assist の全経路で
  一貫する。同値の設定（生成時のラベル初期化呼び出し）は
  解除しない。
- poll の読み戻しはミュート中は音量フィールドをスキップする
  （ミュート中はハードウェア音量の所有者はシェルであり、
  読み戻した 0 は「設定レベル」の真実ではないため）。
- 表示: トレイアイコンは実効レベルで描く（ミュート中は既存の
  × グリフになり、常時見えるフィードバックになる）。QS の
  スライダ＋% ラベルと assist 行は設定レベルを表示する
  （解除時に戻る値であり、ユーザが調整している値そのもの）。
  OSD は新設しない。

## 1. 実装

### shell/include/shellui.h
- `bd_shell_set_muted()` / `bd_shell_muted()` /
  `bd_shell_effective_volume()` を追加（意味論は宣言コメントに
  明記）。`bd_shell_poll_backend()` のコメントにミュート中の
  音量スキップを追記。

### shell/src/shellui.c
- 状態に `bool muted` を追加。生成時に reg `shell/muted`
  （既定 false）を `shell/volume` と並べて読み込む。
- `bd_shell_set_muted()`: 状態を reg へ永続化し、トレイ
  アイコンを実効レベルで再描画し、backend へ実効音量
  （ミュート時 0 / 解除時 保存レベル）を書く。同値の再設定は
  no-op（backend 書込みも発生しない）。
- `bd_shell_set_volume()`: 合流点に unmute-on-adjust を実装
  （異なるレベルへの変更でのみミュート解除＋reg へ記録）。
  トレイと backend へは実効音量を渡すよう変更。QS ラベル/
  スライダと assist 行は従来どおり設定レベルを表示。
- トレイ描画を `volume_icon_update()` に集約（実効レベルで
  描く）。生成時の初期グリフも実効レベルで描く。
- `bd_shell_poll_backend()`: ミュート中は音量フィールドの
  読み戻しをスキップ（輝度・電池は従来どおり）。

### shell/src/session.c
- `session_action()` に `TK_ACTION_MUTE` 分岐を追加
  （`bd_shell_set_muted(sh, !bd_shell_muted(sh))`、消費したら
  true）。記号+Space → KEY_MUTE → TK_ACTION_MUTE →
  session_action → bd_shell という全経路が繋がった。

## 2. 画面フィードバックの確認

- 専用 OSD は本シェルに存在しないため新設していない。
- 常時表示のフィードバックはトレイアイコン: 実効レベルで
  描画されるため、ミュート中は既存の × グリフ
  （`glyph_volume` の level 0 形）になる。これはコード経路
  として確認したもので、ピクセル単位のテストは追加して
  いない（アイコンのピクセルバッファはシェル内部 static で
  テストから観測できないため。正直な線引きとして明記）。
- QS を開いた状態ではスライダと % ラベルが設定レベルを
  表示し続ける（解除で戻る値が見える形）。

## 3. 検証（全て実測）

- test_shell: **99 passed, 0 failed**（変更前 71 → +28）。
  新規ブロック（独立した5番目のシェル＋mock backend）:
  ミュートで設定レベル保持・実効 0・backend 0・reg の
  `muted=true` / `volume` 不変、ミュート中の poll で設定
  レベルが潰れない、同値設定ではミュート維持、レベル変更で
  解除＋backend へ新レベル適用、解除で保存レベルが backend
  へ復元、冗長な set_muted が no-op（backend 書込み回数で
  確認）、シェル再生成を跨いだミュートの永続化（生成時の
  同値 set_volume で解除されないこと含む）。
- test_session: **151 passed, 0 failed**（変更前 138 →
  +13）。TK_ACTION_MUTE の直接注入でセッション経由の
  トグル到達、往復でのレベル保持、ミュート中の
  TK_ACTION_VOLUME_UP で解除＋一段上がることまで確認。
- シェル全体 `make check`: 全スイート 0 failed
  （desktop 182 / shell 99 / iconcache 35 / wm 90 /
  backend 25 / assist 24 / greeter_auth 13 / greeter_font 6 /
  wifi 40 / session 151 / notify 46、合計 711）＋
  test_mascot **95 checks, 0 failures**。
  なお合計が従来ベースライン 655 + 41 にならないのは、
  本タスク着手時点の実測で test_session が既に 138 件
  （ベースライン記録の 123 から他ラインの作業で +15）に
  なっていたため。差分は本タスクの +41（28+13）と一致する。
- ツールキット `make check`: **3,617 件・全 PASS・SDK-OK**
  （ベースライン 3,617 と一致、回帰ゼロ）。

## 4. 「効く」範囲の正確な線引き

- 裏付けが取れた範囲: シェルのモデル・永続化・backend
  （mock）への実効音量適用と、セッションのアクション経路
  での TK_ACTION_MUTE 到達。tk_kbd 側の写像は
  REPORT-mediakeys.md で検証済み。
- 未検証（統合フェーズの evtest 項目）: 実機/ゲストで
  記号+Space を実際に押したときの evdev コードと画面変化、
  実 ALSA（BU26154）で Master 0% 書込みが実際の消音に
  なるか。実 backend は Master の整数値を書くのみで、
  ハードウェア側に独立したミュートスイッチがある場合でも
  本実装はそれを使わない（実効 0 書込みで消音する設計）。

## 5. イメージへの反映

- 本対応はソースとテストまで。実機/SD イメージへ反映する
  には、統合フェーズ準備ライン側での再ビルド（シェルの
  ARM クロスビルドとイメージ再組立て）が必要。
