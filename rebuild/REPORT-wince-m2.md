# REPORT-wince-m2 — WinCE 互換レイヤー（BrainCE Layer）M2（GWE 最小）

- 対象設計: `DESIGN-wince-layer.md` §5.4（GWE シム）、§9.2 の M2 行、§9.3（検証基盤）。前段: `REPORT-wince-m1.md`。
- 範囲: ウィンドウ生成・管理、メッセージキューと WndProc ディスパッチ、基本 GDI（DC・塗り・テキスト、フォントは libbrainui2 の基盤を再利用）、braindesk WM へのクライアント接続（§8.2 第1段＝分離レイヤー。WM への内蔵化はしていない）。**eMMC には一切触れていない**（詳細は「eMMC への非接触」節）。
- 法的境界（設計 §0）: 再実装したのは公開ドキュメント仕様の WinCE 標準 API のみ。SHARP 独自 API・アプリの中身は再実装していない。
- テストバイナリのビルド依存: pe-link47.py は brain-tools-git **作業ツリーの修正版**（.reloc 詰め物エントリのバグ修正、未コミット。M1 報告書参照）を使用した。`test/build-guitest.sh` のコメントにも明記した。
- 結論: **M2（GWE 最小）の合格条件 5 項目を全て満たした**（証拠は下記。ホスト・ゲスト両環境で GUI テストバイナリが Brainux のウィンドウとして表示され、メッセージループの自己記録が期待値とバイト一致し、実行経路上のスタブ呼び出しは全環境で 0 件、M1 回帰も両環境 PASS）。

## 実装した GWE 関数（件数と根拠）

正本リストは `wince-layer/shim/implemented.txt`。序数は M1 と同じく QEMU 側センサスの ordmap から `tools/gen_shim_table.py` が生成する（生成時に ordmap に無い名前はエラーになるため、全件がこの CE ビルドの coredll に実在することの検証を兼ねる）。

- **累計実装 166 件**（M1 の 34 件＋M2 新規 **132 件**）。
- M2 新規 132 件の根拠（M0 分類表 `wince-layer/m0/m0-classification.json` の機能群との突合）:
  - ウィンドウ／メッセージ群（母数 96）: **65 件**を実装（累計 65/96）。
  - GDI／描画／フォント群（母数 65）: **56 件**を実装（累計 56/65）。
  - 補助: CRT／文字列 +3（lstrcmpW, lstrcmpiW, wsprintfW）、実行／モジュール +2（GetModuleHandleW, GetModuleFileNameW）、分類上レジストリ群の +2（RegisterClassW, RegisterWindowMessageW — 実体は GWE）、その他 +1（PostQuitMessage）。
  - M0 母集団（437 件）の外だが実装したもの 3 件: GetTextMetricsW, OffsetRgn, SelectClipRgn。
- 実装の置き場所: `shim/gwe.c`（ウィンドウ／メッセージ／WM 接続）、`shim/gdi.c`（DC／GDI／描画／テキスト）、共有内部ヘッダ `shim/gwe.h`、モジュール系は `shim/coredll_shim.c` に追加。関数表は M2 から複数翻訳単位にまたがるため、`tools/gen_shim_table.py` が宣言専用ヘッダ `shim_fns.h` と定義専用 `shim_fns_table.h` を生成し、定義は `shim/shim_table_glue.c` が持つ形に改めた。
- 発見事項: **TextOutW はこの CE ビルドの coredll に export が存在しない**（ordmap に無い。実在するのは ExtTextOutW）。CE の SDK ヘッダでは TextOutW は ExtTextOutW へのマクロとして提供されるため、実アプリが名前で import することはない。`ce_TextOutW` の実装自体は gdi.c にあるが束縛対象にはしていない。

### 未実装のまま（スタブ）の GWE 群

呼ばれればスタブが関数名と回数を記録・報告する（隠さない）。M2 最小の範囲外として意図的に残したもの:

- ウィンドウ／メッセージ群の残り 31 件: ダイアログ系（DialogBoxIndirectParamW, CreateDialogIndirectParamW, DefDlgProcW, EndDialog, GetDlgItem, SendDlgItemMessageW, SetDlgItemTextW, GetNextDlgTabItem, IsDialogMessageW, MapDialogRect）、メニュー系（LoadMenuW, InsertMenuW, DeleteMenu, DestroyMenu, GetSubMenu, CheckMenuItem, EnableMenuItem, GetMenuItemInfoW, SetMenuItemInfoW）、スクロールバー系（SetScrollPos, SetScrollRange, SetScrollInfo, GetScrollInfo, ScrollWindowEx）、DeferWindowPos 系 3 件、MessageBoxW、GetKeyState、GetWindowThreadProcessId、keybd_event。
- GDI 群の残り 9 件: リソース読み込み系（LoadBitmapW, LoadIconW, ExtractIconExW, DestroyIcon, AddFontResourceW, RemoveFontResourceW）、EnumFontFamiliesExW、Polygon、SetStretchBltMode。

## アーキテクチャ（設計 §5.4 の実装形）

- **WM 接続**: トップレベル CE ウィンドウ 1 つにつき `bd_wm_client` 接続 1 本（`../shell/src/wmclient.c` をそのままリンク）。CE ウィンドウはサーバ側クローム（タイトルバー 32px 等）を持つ通常の Brainux ウィンドウとして現れる。ソケットパスの解決（`bd_wm_default_path`）はサーバ TU 内にあるため、同一ロジックを gwe.c に置いて wmclient.c を単独リンクした（環境変数 $BD_WM_SOCK → $XDG_RUNTIME_DIR/bdwm.sock → /tmp/bdwm-<uid>.sock の順、サーバ実装と同一）。
- **座標モデル**: WM のコンテンツ領域＝CE ウィンドウのクライアント領域。トップレベル窓のウィンドウ矩形＝クライアント矩形とし、位置はシム側の仮想位置で保持する（GetWindowRect の原点はこの仮想位置。サーバが決める実配置とは一致しない場合がある）。子ウィンドウは親クライアント内の相対矩形。
- **描画モデル**: トップレベル窓ごとにシャドウキャンバス（RGBA8888、0xAARRGGBB）を 1 枚持ち、全ての GDI 描画はそこへ集約する。EndPaint／ReleaseDC／UpdateWindow のタイミングで、損傷矩形だけを WM 共有バッファへコピーして damage リスト付きで commit する。
- **メッセージ**: プロセス内 1 キュー（mutex 保護。CE はスレッド毎キューだが M2 対象は単一 GUI スレッド前提）。WM_PAINT と WM_TIMER は CE と同じ位置（投稿キュー→ペイント→タイマの順）で GetMessageW／PeekMessageW が合成し、事前に積まない。SendMessageW はプロセス内直接呼び出し。入力は bd_wm_client イベントのみが源泉（ポインタは子ツリーへヒットテスト、キーはフォーカス窓へ、確定テキストから WM_CHAR を TranslateMessage で生成）。
- **GDI**: DC・ブラシ・ペン・フォント・ビットマップ・リージョンは固定プール上のオブジェクト。DIBSection はアプリ可視の raw bits（32/24/16bpp BI_RGB）と内部 RGBA8888 面を持ち、読み取り前・描画後に同期する。テキストは libbrainui2 のフォント基盤（同梱フォント、実グリフ advance）で描画する。COLORREF（0x00BBGGRR）と内部ピクセル（0xAARRGGBB）の変換は gdi.c の 1 箇所に集約。
- **ビルド方式の変更**: M1 は静的リンクだったが、M2 は Brainux ユーザーランドと同一方式（armbin2 ラッパー＋Debian armel sysroot、pkg-config は sysroot 向け）に切り替えて動的リンクにした。bd-* アプリ群と同じライブラリ集合でゲスト動作する。ホスト実行は qemu-arm に `-L <sysroot>` を付ける（`tools/run-host.sh` を更新）。

## テストベクタ（ceguitest.exe）

P47App 級の自作 GUI バイナリ（`test/ceguitest.c`、import **36 件**＝GWE 31＋ファイル 5、全件実装済みに束縛されスタブ束縛 0）。P47App 本体のソースは brain-tools 資産に無いため pe-link47 経路で自作した。

- RegisterClassW→CreateWindowExW（400×300）→ShowWindow→UpdateWindow→SetTimer（100ms）→メッセージループ。
- WinMain から PostMessageW（WM_USER+2）と SendMessageW（WM_USER+3、戻り値 0xBEEF を検証）で投稿／直接の両経路を検証。
- WM_PAINT で決定的シーンを描画: 白背景、青帯（0,0）-（400,40）、赤ブロック（10,50）-（110,150）、緑矩形＋黒ペン枠（20,160）-（180,220）、黒テキスト "BrainCE M2"（10,230）、マゼンタ SetPixel（390,290）、メモリ DC＋BitBlt で黄色 50×50 を（300,50）へ転送し GetPixel で自己検証（青・赤・黄の 3 点）、GetDC＋SetPixel でシアン点（395,295）。
- タイマ 8 回で `\CE-TEST\guilog.txt` に決定的ログ（CREATE/USER_POST/USER_SEND/SENDRET/PAINT/TIMER/BITBLT/GETPIXEL/DESTROY/GUILOG）を M1 のファイル API 経由で書く。`\CE-TEST\GHOLD` がある場合は記録後も窓を開いたままポンプを継続する（撮影・RSS 採取用）。

## 検証結果

### ホスト（qemu-arm、設計 §9.3 の回帰手順）

`make host-test` = `tools/run-host.sh`（M1）＋ `tools/run-host-gui.sh`（M2）の 2 本立てにした。

- M1 回帰: **HOST-TEST PASS**（result.txt バイト一致、pattern.bin 一致、stub calls total=0）。
- M2 Run A（実 WM サーバ＋GHOLD）: `tools/wmhost.c` が braindesk WM サーバを同一プロセスで立て、ce-loader を子として起動。**WMHOST PASS（11 passed, 0 failed）**: 窓が Brainux ウィンドウとして出現、コンテンツ 400×300、画素 7 点（青帯・赤・緑・黄・マゼンタ・シアン・白）が **RGBA8888 精度で完全一致**、テキスト黒画素 109 個。guilog（GHOLD 変種）バイト一致。
- M2 Run B（WM 無し・ヘッドレス全ライフサイクル）: 終了 0、guilog（DESTROY=1 変種）バイト一致、**stub calls total=0**、imports bound: 36 total, 0 to stubs。

### ゲスト（sd-v15-ce2、実 Brainux デスクトップ）

ドライバ: `image-work/int18/guest-ce2/run-ce2.py`（結果 `results-ce2.txt`、シリアル `serial-ce2.log`、撮影 `shots/`）。イメージ: `image-work/sd-v15-ce2.img`（組立時 md5 `060b7715869892f8a2f50ebedcc2b831`（2 回読みで安定を確認）、root PARTUUID `8a548b6a-02` は MBR シグネチャ由来でドライバが動的に照合。組立 `assemble_image_ce2.sh`、rootfs は rootfs-ce1 のコピー＋M2 の ce-loader（md5 `12b8d3cc7a6b9c46e3f3dafa4154ff6b`、ゲスト内 md5sum でホストビルドと一致を確認）と ceguitest.exe）。

- **GUEST-TEST PASS**（最終実行の出力 `run-ce2-final.out`、results-ce2.txt 末尾に記録。同ファイルには途中の失敗試行の記録も残してある）。
- M1 回帰（ゲスト内、root 実行）: 終了 0、stub calls total=0、result.txt md5 `e982cd05677bef39b992fd08bfdee4d0`・pattern.bin md5 `efc404fa609a799c7801273de6d65e84` がホスト期待値と一致。
- GUI（デスクトップセッション、user 実行、実 braindesk WM に対して）:
  - GHOLD 実行: アプリ自身が guilog.txt を生成（md5 `0f1a6a48978763e037608f352604bd59`＝期待値と一致）。QMP screendump の画素検証 PASS（青帯アンカー原点 (18,52)、7 点一致、テキスト黒画素 109 個＝ホストと同数）。撮影 `shots/gui-hold.jpg` で「BrainCE M2 Test」窓がサーバ描画クローム付きの通常ウィンドウとして表示され、シーン（青帯・赤・黄・緑＋黒枠・テキスト）が描かれていることを目視でも確認した。
  - FULL 実行（GHOLD 無し）: 終了 0、stub calls total=0、guilog md5 `3fcc93595c04053f2fc7b1a2f6c085cd`＝期待値と一致。
- ゲスト撮影の画素は RGB565 量子化（純色が (0,0,248) 等になる）ため、ゲスト側チェッカは ±8（565 の 1 段）で照合した。888 精度での完全一致はホスト Run A で確認済み。

### スタブ状況

- 実行経路上のスタブ呼び出し: **全環境（ホスト A/B、ゲスト M1・GUI GHOLD・GUI FULL）で 0 件**。束縛時も ceguitest の 36 import は全件実装に束縛（0 to stubs）。
- 未実装関数は import された時点でスタブに束縛され、呼ばれれば名前と回数がローダ終了時に報告される仕組み（M1 から継続）。今回のテストベクタは踏んでいない。

### 資源実測（ゲスト内、合格条件 5）

- GUI アプリ常駐時（GHOLD、窓表示中）の ce-loader プロセス: **VmRSS 3,200 kB、PSS 669 kB**（ゲスト内 /proc 実測。PSS が小さいのは共有ライブラリ分が按分されるため。ローダ＋シム＋アプリ＋シャドウ面＋WM バッファを含むプロセス全体の値）。
- FULL 実行の終了時: VmRSS 5,916 kB / VmHWM 6,860 kB（ローダ自己申告。終了直前のピーク込み）。
- 参考: M1 コンソールテスト終了時 VmRSS 2,808 kB。MemAvailable は GUI 起動前 84,848 kB → GHOLD kill 後 84,072 kB（デスクトップ常駐状態での参考値）。

## コピー／オーバーヘッド削減

- フレーム全体コピーはしない。GDI 描画はシャドウキャンバスへの直接描画で、WM 共有バッファへの転送は **損傷矩形の行単位コピーのみ**（gwe_flush）。commit も同じ損傷リストを渡し、サーバ側の再合成範囲を限定する。
- DIBSection の raw bits 同期は全面変換を避け、読み取り前（raw→面）と描画後（面→raw）の定義点でのみ行う。アプリが触らない DIB は同期コストゼロ。
- BitBlt の SRCCOPY は clip を考慮した行コピー経路、同一面コピーでは一時バッファ経由で重なり安全を確保。その他の ROP は毎画素経路（正確さ優先、対象は小面積の想定）。
- メッセージは値渡しの固定長エントリで、キューはリングバッファ（確保の繰り返しなし）。WM_PAINT／WM_TIMER は積まずに取得時に合成するため、アイドル時の無駄な起床と再描画がない。
- フォントはプロセスで 1 回だけ読み込み（tk_font_install で共有）、グリフ描画は libbrainui2 のキャッシュに乗る。

## キャッシュ／メモリ管理

- ウィンドウ 256／クラス 64／DC 128／GDI オブジェクト 512／タイマ 64／キュー 512／矩形リスト 16 の **固定プール**で上限を保証し、無制限の malloc 増殖をしない。上限超過は NULL＋last error で正直に失敗する。
- シャドウキャンバスはトップレベル窓 1 つにつき 1 枚（窓サイズ分のみ）。WM のダブルバッファはサーバ側の既存機構を使うため、クライアント側で追加の全面バッファは持たない。
- ストックオブジェクト（ブラシ／ペン／フォント）は遅延初期化して共有し、アプリが作るオブジェクトは DeleteObject でプールへ戻る。DIB の raw bits はビットマップ破棄時に解放する。
- テキスト計測は都度変換バッファ（固定長、スタック）で処理し、ヒープ確保を避ける。

## 実機の現実への対応

- **eMMC には一切触らない**: レイヤーは SD 上の Brainux から動き、CE 側ファイルは FS ブリッジ経由で SD 内の fs ルート配下にのみ書く。ゲスト検証でもベースライン emmc-int9.img はアタッチせず、作業コピー emmc-int9-wince.img を snapshot=on で index 0 に置き、前後 md5 不変を確認した（次節）。
- **突然のリセット前提**: レイヤー自身は永続状態を持たない（窓・キュー・GDI 状態は全て揮発）。アプリのファイル書込みは M1 の FS ブリッジの通常ファイル書込みで、書いた分だけが残る。壊れた中間状態を引き継ぐ仕組みはない。
- **オフライン既定・時刻不信**: ネットワーク・時刻に依存する処理は無い。タイマは GetTickCount（単調増加）基準で、日数ベースの有効期限のような仕組みは存在しない。
- **128MB RAM**: 上記の固定プールと損傷限定転送で、GUI アプリ常駐時の増分は PSS 実測 669 kB に収まる（次節の実測参照）。

## eMMC への非接触

- ゲスト構成は M1 と同一: index 0 = 作業コピー `emmc-int9-wince.img`（snapshot=on、書込みは使い捨てオーバーレイへ）、index 1 = sd-v15-ce2.img。復旧済みベースライン `emmc-int9.img`（md5 `612e8a0307436b59852bb39ede358fa8`）は **一切アタッチしていない**。
- ドライバが前後で md5 照合: ベースライン・作業コピーとも **不変**（results-ce2.txt に記録）。

## 検証できなかったこと・未解消項目

- **アプリへの入力配送は未検証**: フェーズ15の確定事実として、ゲストへの QMP ポインタ注入はシェルに届かず、キーボード注入もアプリに届かない。WM プロトコル経由の入力変換（ポインタのヒットテスト、キー→WM_KEYDOWN/WM_CHAR）は実装したが、実アプリで「操作できた」とは主張しない。グリーターへのキー注入（ログイン）だけは届くことを今回も使用した。
- ダイアログ／メニュー／スクロールバー／MessageBoxW は未実装（スタブ節の一覧参照）。実アプリの M3 以降の対象。
- 視覚に反映されない近似: SetWindowRgn は保存のみ（WM フレームは矩形のため形状は出ない）、トップレベル窓のプログラム的リサイズ／非表示は WM フレームへ反映されない（サーバ駆動のリサイズには RESIZE イベントで追従）、破線ペンは実線近似、フォントのイタリックは描画に反映されない、GetWindowRect の原点は仮想位置。
- DrawTextW の '&'  prefix は除去のみ（下線は描かない）。ExtTextOutW の ETO_CLIPPED は DC クリップとの交差で処理する。
- DIB カラーテーブルは対象面が direct color のため Get/Set とも 0 を返す（パレット面は未対応）。
- ゲスト rootfs の /opt が 770 root:root のため、user 実行にはドライバ側で `chmod 755 /opt /opt/braince` が必要だった（rootfs 側の既存属性で、レイヤーの不具合ではない。恒常対応するかは rootfs 側の判断）。

## 検証中に起きたこと（正直な記録）

- 初回のゲスト起動試行は、イメージ組立（約 11 分）の完了前にブートしてしまい、接合途中の p2 で ext4 ジャーナル読み込みエラー→カーネルパニックになった。組立完了をログと md5 の安定で確認してから再実行し、p2 が e2fsck clean であることも確認した。eMMC への影響は無し（上記 md5 照合は全試行を通じて不変）。
- ゲストのフレームバッファが RGB565 のため、初回の画素チェッカ（888 完全一致）は青帯を検出できなかった。実測値（(0,0,248) 等）に基づき ±8 照合へ改めた。888 精度の一致はホスト Run A で別途確認済み。

## 成果物の所在

- 実装: `wince-layer/`（`shim/gwe.c`, `shim/gdi.c`, `shim/gwe.h`, `shim/coredll_shim.c`, `shim/shim_table_glue.c`, `loader/ce-loader.c`, `Makefile`, `tools/gen_shim_table.py`）
- テスト: `wince-layer/test/ceguitest.c`, `test/build-guitest.sh`、ハーネス `wince-layer/tools/wmhost.c`, `tools/run-host-gui.sh`, `tools/run-host.sh`
- ゲスト受け入れ: `image-work/int18/guest-ce2/`（`launch-ce2.py`, `run-ce2.py`, `results-ce2.txt`, `serial-ce2.log`, `shots/gui-hold.jpg` ほか）、イメージ `image-work/sd-v15-ce2.img`、組立 `image-work/assemble_image_ce2.sh`
