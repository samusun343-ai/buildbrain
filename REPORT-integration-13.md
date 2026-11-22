# 統合フェーズ13 報告書（窓・パネル内側の余白の是正）

作業領域: `image-work/int14/`、作業ツリー: `rebuild/`。
commit/push はしていない（親の担当）。

## 1. 成果物

- **sd-v13.img** md5 = `dd0921f38ffcaeb506e91e343df3e7f4`
  （組立ログ `int14/work/assemble13.log`、ASSEMBLE_OK）
- CHECKLIST-v13: `int14/CHECKLIST-v13.md`（第6ラウンド用）
- 盲検第6ラウンド用 無ラベル撮影セット: `int14/blind/`
  48枚（01.jpg〜48.jpg）＋ `blind-key.txt`
  （番号→名前→PPM md5 の対応表）。
  名前つき50枚から、バイト同一の重複2件（settings-android
  ≡ settings-android-page、winui-two-apps ≡ notes-winui）を
  機械排除した。評価者は親が立てる（本ラインは用意まで）。
- 並列比較図: `int14/compare/` 8枚（v12 左 / v13 右:
  settings-winui、files-winui、player-winui、
  browser-selftest、tasks-android、notes-winui、
  recorder-winui、greeter）
- 総点検メモ: `int14/work/audit-spacing14.md`

## 2. ゲスト全面回帰（sd-v13・作業コピーで実施）

ハーネス `int14/guest/run14.py`（boot1/tour/osk/small/
wince）＋ `run14b.py`（browser2、argv URL 方式）。
SD は `sd-v13-work.img`（sd-v13 の複製、md5 同一）、
WinCE 確認は eMMC 作業コピー `emmc-int9-wince.img` のみ。
**実 emmc-int9.img からは直接ブートしていない。**

- boot1: **PASS**（2回実施し両回 PASS）
  - ブート到達（brain login: まで 134s / 89s）
  - PARTUUID: cmdline `root=PARTUUID=f77f6004-02`、
    実行時解決 `/dev/mmcblk1p2`、blkid 一致
    （f77f6004 は sd-v13 のディスクシグネチャ）
  - CPU クロック 454,736,842 Hz（454MHz）✓
  - boot.mount active ✓、フォールバックなし ✓
  - dmesg 異常（panic/oops/BUG）0件 ✓
  - メモリ: Mem available 85MB（アイドル時）
- tour: 全10アプリ起動確認（bd-calc / bd-notes /
  bd-settings / bd-files / bd-tasks / bd-clock /
  bd-recorder / bd-player / bd-pkg / bd-admin、
  いずれも pgrep でプロセス確認）。tour 後の
  Mem available 82MB、QEMU RSS 506,420KB。
- osk / small: 完了（RESULT: done）。browser2: 完了。
- wince: 作業コピーから WinCE が起動し初期化確認画面
  まで到達（`v13-wince-home`、第12相と同状態。入力は
  送っていない）。
- **emmc-int9.img の md5 は回帰前後とも
  `2af360a02f452979b4de84ba3b1845f5` で不変**
  （フェーズ12事故後の値。pristine との差は既知で、
  復旧はユーザーの判断待ちのため触っていない）。
- 名前つき撮影は verify_shots.py で **50/50 全件照合OK、
  不一致0**。run14b のログイン診断ショット
  v13-login-probe のみ不一致（画素はデスクトップ、
  名前は windowed 扱い）のため `int14/rejected/` に
  分離（理由は rejected/README.md）。

途中の事故・対処（隠さず記録する）:
- 組立1回目が rootfs プローブの gid=65534（nobody）を
  検出して POISONED_TREE で停止した。ホストの実空きは
  41GB あり、再試行ではプローブ gid=0 で正常完了した。
  毒ガードは迂回していない。
- サービス再起動で回帰ドライバが落ち QEMU が孤児化した
  ため、孤児を終了して runall を再起動した（boot1 は
  再起動後にも PASS）。
- run14b の `import run13` 残りを修正（run14 へ）。
  tour のキー入力遷移が welcome のままだった撮影は
  run14b の argv 方式で同名上書きし、最終セットでは
  selftest/giftest とも正しいページになっている
  （verify_shots 照合済み）。

## 3. 是正項目ごとの実測（是正前 → 是正後）

実測は全て最終イメージのゲスト撮影 PPM から採取した。
窓の内容端は x=2（枠2px）。

### 項目1: サイドバー付きレイアウトの左余白（C7 起点2）
根本原因は2つ: (a) 各 WinUI サイドバーの pad が
18px 未満、(b) ツールキットの listview 内側オフセット
が 2*unit=16px で、全リストの内容が面端から 16〜17px
しか離れていなかった（起点3の系統的不足）。

- (b) への対処: `src/tk_widgets.c` に
  `LV_PAD_X = 2*unit+2 = 18px` を新設し、行テキスト／
  セクションヘッダ／ドラッグゴーストの描画、希望幅の
  measure、チェックボックス当たり判定セルの4箇所を
  統一。テスト test_tk13/15/17 の旧16px前提アサーション
  を追随更新した。
- 設定 WinUI: pane pad_x 16→18。検索欄の面
  **16px → 18px**（面 x20、PPM 実測）。
- ファイル WinUI: nav pane pad_x 9→11。サイドバー面
  **16px → 18px**、面内アイコン **17px → 18px**
  （アイコン実インク x38、面 x20）。
- プレイヤー WinUI: nav_pane padding 12→18。
  ドロワー面 **12px → 18px**（面 x20）。
- 時計 WinUI 主リスト: 面 x189 から本文 **22px**
  （是正前は 16px 級）。
- 合格済みの Android 側（面 x20 = 18px）は変更なしで
  維持（撮影で再確認）。

### 項目2: ブラウザ同梱ページの余白（C7 起点2）
- selftest.html / giftest.html に
  `body { margin: 20px }` を追加（UA 既定 8px に
  依存していた）。welcome.html は既存 24px。
- ゲスト実測: selftest 本文 **11px → 28px**、
  giftest 本文 **11px → 27px**（内容端 x26 から
  最初の本文インクまで）。どちらも 18px 以上 ✓。
- CHECKLIST-v13 に適用範囲を明文化: 起点2はアプリ
  UI面が対象、第三者の Web 文書の余白は著者制御
  （根拠: Material 3 Spacing はアプリの component /
  UI element / layout への規定、W3C CSS 2.1
  Appendix D の UA 既定 `body { margin: 8px }`、
  いずれも 2026-11-23 取得）。同梱ページは出荷
  コンテンツとして例外なく実測で満たすことも明記。

### 項目3: 入力欄と隣接ボタンの間隔（C2-A 9px）
第5ラウンドの注意フラグ3件＋同型の総点検分を全て
9px 化した（行の spacing 変更）:
- タスク Android hdr（コンボ↔リスト管理）4→9:
  面間 **4px → 11px**（PPM 実測、枠込みスパン。
  コンボ側の枠＋余白を含む）
- タスク WinUI nrow（リスト名欄↔色ボタン）4→9
- メモ 検索行（両テーマ）4→9: WinUI 面間スパン
  **5px → 9px**（PPM 実測）
- ファイル search_row / path_row（検索欄↔検索、
  パス欄↔移動）4→9 ※総点検で発見
- 設定 PSK フォーム（パスワード欄↔接続）8→9 ※同上
- 時計 trow（ラベル欄↔作成）4→9 ※同上
- 変更しなかった行（理由は audit-spacing14.md）:
  ボタンを伴わない欄同士・コンボ＋欄の行
  （clock erow/erow2、tasks drow1/drow2、
  player search_row）。
- CHECKLIST-v13 に「アプリUI内の操作行における
  欄＋ボタンの異種ペアも C2-A の対象」と明文化
  （根拠: Material 3 Spacing は要素種別で間隔の
  扱いを変えない、Microsoft Learn のコントロール
  間隔規定。出典はチェックリスト本文）。
  段階値（9/6〜8/5px）は不変。

### 項目4: 空状態の案内文
- メモ WinUI: 状態文のみ → 2行
  「ノートがありません／新規ノートから作成できます」
  （ゲスト撮影で確認）
- タスク（両テーマ）: 案内なし → 1行の案内ラベル新設
  （Android「タスクがありません。入力欄から追加
  できます」、WinUI「タスクなし。上の欄で追加
  できます」、両方ともゲスト撮影で確認）。
  2行 box 版は 480×272 でリストを 19px に潰し
  matrix の floor24 に抵触したため1行化した。
- レコーダー（両テーマ）: 案内なし → 2行 box
  「録音がありません／「録音」を押すと始まります」
  （WinUI は pkg と同型にスプリット下・主カラムへ
  配置。ゲスト撮影で確認。最近削除ビュー用の文面も
  用意したが、ゲストでは未到達のため未検証と明記）
- ファイル: 状態文のみ → 2行（「このフォルダは
  空です／「その他」メニューから新規フォルダを
  作成できます」）※総点検で発見。ゲスト撮影の
  ホームには Music/ があり空状態は写っていない
  （ホスト matrix で表示を確認）。
- 既に合格済みで変更なし: メモ Android、プレイヤー、
  pkg、電卓履歴。

### 項目5: ログイン画面の時刻の中心ずれ
- 根本原因: advance 幅での中央寄せはグリフの左右
  ベアリング分だけインクがずれる。
- 対処: `tk_text_ink_shift_px/st`（インク bbox 中心
  への補正量を shaping と同じ歩行で算出）を
  tk_font.c に新設し、グリーターの時刻・日付ラベルに
  適用（文字列が変わるたびに再配置）。
- ゲスト実測: 時刻の中心ずれ **−3px → −1.0px**
  （Android グリーター、日付は −0.5px）、
  WinUI グリーターでも **−1.0px**。残り1pxは
  グリフ寸法の偶奇によるもので、これ以上の補正は
  1px 単位の量子化を超える。

### 項目6: C1 残件
- グリーター高さ 52/54px の食い違い: **測定法の差
  として切り分け完了**。コード上の矩形は全て 54px。
  ゲスト PPM 実測（WinUI グリーター、中心列）:
  ログインボタン（枠なし）= 外形 **54px**、
  パスワード欄 = 枠込み外形 **56px**（填充 54 行＋
  上下の枠線が矩形の外側に各1px）。52px は填充のみ
  から枠を差し引いた採取、54px は矩形そのもの、
  56px は枠込み外形であり、同じ対象の3つの採取値
  だった。CHECKLIST-v13 で「C1 の寸法は枠込み
  外形」と明文化した（C2 の枠込み convention と
  統一）。閾値 54px に対してボタン 54px・欄 56px
  で充足するため実装変更はしていない（拡大すると
  電源ボタンの下余白など他基準を壊す）。
- レコーダー名前欄 42px: min_size を control_h に
  固定。ホストプローブ実測で 854×480 / 480×272
  両方とも **54px** を維持（是正前は詳細スクロール
  内で 42px に潰れていた）。ゲスト撮影では録音詳細
  を開いていないため、ゲスト側の確認は未実施と
  明記する。
- リスト行ピッチ 47px: **v13 では再現しない**。
  ゲスト PPM の本文バンド実測: pkg WinUI **54px**、
  タスク WinUI サイドバー **55px**、設定 WinUI
  ナビ **64px**（2行行）。全リスト共通の
  lv_row_h（row_h=54 / row_h2）が効いており、
  47px のピッチは存在しなかった。第5ラウンドの
  47px は面の高さ÷行数などの採取法の差とみられる。
- OSK キー高さ 29〜31px: **構造的限界として未解消**。
  v13 ゲスト実測でも文字キー外形 **30px**。
  tk_osk.c の fit 機構（候補欄 48＋下段バー 56 を
  確保した残りを3行で割る）上、メモ WinUI では
  OSK 全体が窓内容の約205px しか得られず、キー行は
  約33px が上限。54px 化するとアプリ本体が表示
  できなくなるため、無理な拡大はしていない。
  下限 24px は充足。
- スライダーつまみ直径 約26px: 本フェーズの是正
  対象外（第5ラウンドでも下限 24px 充足・推奨
  54px 未達の記録のみ）。未解消として残す。

### 項目7: CHECKLIST-v13 の明文化
`int14/CHECKLIST-v13.md` に v13 変更点として4点を
冒頭に明記（閾値は v11 以来一切変更なし）:
1. C2-A の対象に欄＋ボタンの異種ペア（項目3）
2. C7 起点2と Web 文書の余白の線引き（項目2）
3. C4 グリフサンプリング規則 7: インク画素 100
   未満の細いグリフは最暗／最明の単一画素を芯色
   とし「単一画素・芯色」と記録する。参考実測:
   v13 電卓「＝」は芯色画素 (248,252,248) 対
   填充 (0,100,192) = **5.66:1** で合格域。
   第5ラウンドの 4.25:1（I）は AA 縁画素の採取、
   5.23:1（J）は芯色の採取であり、規則の明文化で
   決着する。
4. C1 の測定注意に「寸法は枠込み外形」を追補
   （項目6）。

## 4. ホスト側検証

各スイートは自分のディレクトリから実行した。
- ツールキット: 全22スイート合格（test_textcore、
  test_tk〜test_tk18、test_font、test_shape、
  test_kbd）
- アプリ: 全スイート合格（test_files(_ui)、
  test_notes、test_tasks(_ui)、test_clock(_ui)、
  test_calc(_ui)、test_recorder(_ui)、test_pkg(_ui)、
  test_settings(_ui)、test_admin(_ui)、test_alarmd。
  test_player / test_player_ui は fixtures 引数つきで
  合格）
- シェル: 全13スイート合格（test_assist、backend、
  desktop、greeter_auth、greeter_font、iconcache、
  mascot、notify、session、session_apps、shell、
  wifi、wm）
- **test_matrix: 1132 passed, 0 failed**。
  ゴールデンは6セルのみ再生成（設定 ×4、メモ WinUI
  ×2）。再生成前に全セルの比率チェックが合格して
  いることと、差分が int14 の意図した視覚変更
  （余白・案内文）に限られることを確認した
  （ゴールデンは FNV ハッシュのみで画像は含まない）。

## 5. コピー／オーバーヘッド削減

- 本フェーズの変更は余白・文言・配置のみで、新規の
  コピー経路やバッファは追加していない。
- グリーターのインク中心補正（tk_text_ink_shift）は
  shaping 結果の歩行のみで、追加のテキスト描画や
  中間バッファを持たない（配置時に1回だけ算出）。
- listview の LV_PAD_X 化は定数の置換であり、
  描画回数・確保量に変化なし。
- 同梱ページの margin 追加は HTML/CSS の静的変更で、
  実行時コストなし。

## 6. キャッシュ／メモリ管理

- 新規キャッシュは追加していない。アイコン
  キャッシュ等の既存機構は無変更。
- ゲスト実測のメモリ: アイドル時 Mem available
  85MB、tour（全アプリ起動一巡）後 82MB で、
  sd-v12 と同水準（128MB 枠に対して余裕を維持）。
- QEMU RSS は約 506MB（ホスト側、tb-size=64 前提）。

## 7. 実機の現実への対応

- 実機は 854×480・約179ppi（1dp≈1.12px、48dp≈54px）
  前提のまま、全ての是正値を px 実測で検証した。
- OSK キー 30px のような構造的限界は、実機の画面
  高と候補欄・バーの確保から来るもので、数値を
  残して未解消とした（項目6）。
- eMMC は無変更（§2 の md5 記録どおり）。WinCE
  確認は作業コピーのみで行った。

## 8. 未解消項目

1. OSK 文字キー高さ 30px（推奨 54px 未達、構造的
   限界。数値は項目6）。
2. スライダーつまみ直径 約26px（下限充足・推奨
   未達。本フェーズ対象外として残置）。
3. レコーダーの最近削除ビューの空状態文面と
   名前欄 54px はホスト側でのみ確認し、ゲスト撮影
   では未検証（録音データのない状態では到達不可）。
4. ファイルの空状態2行文はホスト matrix で確認、
   ゲスト撮影では空フォルダに当たらず未写。
5. プレイヤー WinUI トランスポート行（spacing 8 の
   混成行）の C2-A 適用可否は第12相からの継続
   検討事項で、本フェーズでは変更していない。

## 9. ディスク

- 作業中の空きは 55GB（64% 使用）で逼迫せず、
  クリーンアップは行っていない。
- 生成物: rootfs-v13（1.9GB）、sd-v13-work.img
  （回帰用複製）は次相の基盤として保持した。
