# REPORT — ブラウザ実装フェーズ M0（NetSurf 3.11 魔改造・第1弾）

- 日付: 2026-11-20
- 対象: `browser-engine/netsurf-all-3.11/`（git 管理、後述）。buildbrain repo へは未統合・push なし。
- 位置づけ: 選定文書 `browser-engine-selection.md` の魔改造リスト M0〜M11 のうち **M0（brain サーフェス）＋予算組み込み（M1〜M3 の核）** を実装し、ホストで実証した段階。**「完成」ではない。** 動く UI は framebuffer フロントエンド＋brain サーフェス（検証車両）であり、ツールキット v2 上の製品 UI（frontends/brain）は設計のみ（`DESIGN-brain-frontend.md`）。
- 合格判定について: 本レポートの実測は全てホスト x86-64 の参考値であり、合格証拠ではない。合格は統合フェーズの SD イメージ＋QEMU ゲスト実測で判定する。

## 1. 差分管理

`netsurf-all-3.11/` 内に git を初期化し、変更を追える形にした。

| commit | 内容 |
|---|---|
| `512e382` | baseline: 受け取り時点のツリー（tarball md5 `1554bb1093ef582da6b81883e181a3cf`。先行ホストビルドの生成物を含む） |
| `589f302` | M0 brain サーフェス＋予算プロファイル（M1〜M3 核） |
| `0e7162d` | M3: fb ターゲットで fs バッキングストア有効化 |
| `7ecaec5` | M3 試行: 追い出しを上限収束型に変更 |
| `fa6cae6` | 上記を revert（理由は §5 の会計欠陥。ペアで直す必要があるため）＋生成物の追跡解除 |

ソース差分（baseline→HEAD、生成物除く）: **11 ファイル、+1,258 / −23 行**。中核は新規 `libnsfb/src/surface/brain.c`（959 行）。

## 2. 実装内容

### 2.1 M0: libnsfb「brain」サーフェス（新規）

- 登録名 `"brain"`、型 `NSFB_SURFACE_LINUX`。パラメータは `fbdev=...;input=...` 形式で `nsfb_set_parameters()` 経由（fb フロントの既存オプション `fb_device` / `fb_input_devpath` を `frontends/framebuffer/framebuffer.c` で配線）。
- **描画**: デバイス形式が RGB565 / XRGB8888 / XBGR8888 / RGB888 の場合、fbdev を直接 mmap して描画する（update は no-op ＝コピーゼロ）。それ以外のレイアウトはシャドウバッファ＋ダメージボックス変換にフォールバック。
- **ホスト検証モード**: fbdev パスが通常ファイルの場合、要求ジオメトリで ftruncate＋MAP_SHARED mmap するフェイクデバイスとして動作（テストハーネスがそのままピクセルを読める）。
- **入力**: evdev。ABS（EVIOCGABS でスケーリング）/ MT / REL（ホイールは PageUp/Down へ変換）/ BTN_TOUCH・BTN_LEFT 等を MOVE_ABSOLUTE＋マウスボタンイベント化し、pending キュー経由で配送。キーボードは Linux コード→nsfb keysym 表（US フェイスバリュー。Brain 実機キーの借用コード解釈はプラットフォーム仕様どおりシェル側の領分であり、ここでは変換しない）。`input` 未指定時は /dev/input/event0..31 を自動スキャン。タイムアウト時は CONTROL_TIMEOUT を返す（x.c と同じ意味論）。
- 意図的に未実装: KDSETMODE（VT グラフィックスモード切替）。実機ではシェル（braindesk2）がコンソールを所有する前提のため保留。単独起動モードでの要否は統合フェーズで判断する。

### 2.2 予算の組み込み（M1〜M3 核）

設定は NetSurf のオプション機構に一元化し、Brain 固有の3キーを新設した（`netsurf/desktop/options.h`、`desktop/netsurf.c`）。

| 項目 | 変更前（上流既定） | 変更後 |
|---|---|---|
| `disc_cache_size` | 1 GiB | **32 MiB**（容量ベース LRU。§5 の欠陥あり） |
| `disc_cache_age` | 28（日） | **0**（コアはこの値を一切参照せず追い出しは容量駆動のみであることを grep で確認。日数ベース有効期限はユーザー原則により廃止） |
| `max_fetchers` | 24 | 6 |
| `max_fetchers_per_host` | 5 | 2 |
| `max_cached_fetch_handles` | 6 | 4 |
| `curl_fetch_timeout` | 30s | 20s |
| `script_timeout` | 10s | 5s（ただし §6 の不整合あり） |
| `fb_depth`（fb 前端） | 32 | **16**（デバイスネイティブ RGB565 に一致させ変換を消す） |

新オプション（0=無効）:
- `brain_budget_mb`: ブラウザ総予算。`netsurf_init` で llcache 基礎 = 予算/4、`brain_max_image_pixels` 未設定なら (基礎/4×2)/4 px、`brain_js_heap_kb` 未設定なら 4096 を導出し、NSLOG INFO に出す（実測ログ: 予算56MB → `llcache base 14680064, max image 1835008 pixels, JS heap 4096KB`、llcache 実効上限 11,010,048 B、画像キャッシュ上限 3,670,016 B / hysteresis 734,003 B — 選定文書 §4 の予算表どおりに導出されることを確認）。

### 2.3 画像上限（M2 第1段）

- `frontends/framebuffer/bitmap.c`: `bitmap_create` で width×height > `brain_max_image_pixels` を拒否（WARNING ログ）。全デコーダ共通のチョークポイントであり、寸法爆弾（小さなファイルで巨大ビットマップを要求）もここで止まる。
- `content/handlers/image/jpeg.c`: 上限超過 JPEG は DCT スケーリング（1/2・1/4・1/8）で上限内に落としてからデコード（INFO ログ）。**表示サイズデコードは JPEG のみ**。他形式は現状「拒否」が上限の振る舞い（§6）。

### 2.4 JS ヒープ上限（ページ単位）

- `content/handlers/javascript/duktape/dukky.c`: Duktape の alloc/realloc/free フックにサイズヘッダ union を付け、全アロケーションを正確に会計。jsheap 単位で heap_used / heap_limit を持ち、上限は `brain_js_heap_kb` から設定。上限超過時はアロケーション失敗を返し（realloc 失敗時は旧ブロック保持）、heap_over ラッチ＋WARNING ログ、`dukky_check_timeout` でも割り込む。プロセスは殺さない。

### 2.5 M3: ディスクキャッシュの有効化（fb ターゲット）

上流の fb フロントではディスクキャッシュが**そもそも存在しなかった**（`NETSURF_FS_BACKING_STORE` 既定 NO＋fb の GUI テーブルが llcache テーブル未登録＋`netsurf_init(NULL)` でストアパスなし）。以下を変更:
- `frontends/framebuffer/Makefile.defaults`: `NETSURF_FS_BACKING_STORE := YES`
- `frontends/framebuffer/gui.c`: `.llcache = filesystem_llcache_table` を登録。ストアパスの既定を `$HOME/.netsurf/cache` に（`disc_cache_path` 明示時はそちら優先）。実機では HOME が microSD の Brainux 領域にある前提で、配置原則（SD 配置・eMMC ノータッチ）を満たす。

## 3. 実測（ホスト x86-64、テストハーネス `browser-engine/test-harness/drive_test.py`）

テストサイトは `browser-engine/test-site/`（ローカル）。入力は FIFO 経由の evdev 注入、画面はフェイク fb ファイルのピクセルで検証。スクリーンショットは `browser-engine/shots/` に保全。

| シナリオ | 結果 | RSS |
|---|---|---|
| render（page1、予算なし） | brain サーフェス経由で描画。背景 #204060=41,981px、h1 #c03028=18,584px、リンク #28a028=39,526px。ツールバー・本文・ステータス "Done (0.1s)" を目視確認 | 17,172 KB（再現時 17,236 KB） |
| nav | リンク重心タップ→page2 遷移（背景 #f0e040=82,963px）。Back ボタンタップ→page1 復帰（page2 背景=0px）。**リンク遷移と履歴が evdev 注入で動作** | — |
| images（予算56MB） | 予算導出ログ確認。大 JPEG 2400×1800 は「exceeds cap 1835008 pixels; decoding at 1/2 scale」で表示 | 22,000 KB（再現時 21,932 KB） |
| png（予算56MB） | small.png 100×60 が正確に 6,000px 表示。big.png 2000×2000 は「Refusing oversized bitmap」で拒否（マゼンタ化なし） | 17,340 KB（再現時 17,416 KB） |
| js | Duktape が DOM を書き換え "JS RAN OK" を表示 | — |
| jshog（4KB 一意文字列を保持し続けるスクリプト） | **3.9 秒で「JS heap cap reached (4194304 bytes), failing allocations」**。RSS は 16,836→20,984 KB で凍結、プロセス生存、ページは "HOG STOPPED: Error"＋"After script." を描画し "Done (3.9s)" で完了。**上限がプロセスを殺さずに機能することを実証** | 20,984 KB（上限到達後凍結） |
| cache | §5 参照（上限強制に欠陥。上流問題として切り分け済み） | — |

注: 初版 jshog は同一文字列の繰り返しで Duktape のインターンにより保持メモリが増えず、既存の実行タイムアウト（§6）で停止した＝テスト側の欠陥として記録し、一意文字列版に差し替えた。

## 4. コピー/オーバーヘッド削減

- brain サーフェスはデバイス形式が一致すれば fbdev 直接 mmap で**中間バッファと転送コピーをゼロ**にした（update は no-op）。不一致時のみシャドウ＋ダメージ矩形変換に落ちる。
- `fb_depth` 既定を 32→16 にし、RGB565 デバイスに対する毎フレームの色変換を排除。
- フェッチャ並列度・タイムアウトを端末向けに下げ、無駄な同時接続と待ちを削減（§2.2）。
- JS ヒープ会計はサイズヘッダ1ワード/アロケーションのみで、追加の走査やコピーは入れていない。
- 画像は上限超過時にデコード前に落とす（JPEG は DCT スケーリングでデコード自体の仕事量も削減）。「落としてから処理する」を「処理してから捨てる」にしない。

## 5. キャッシュ/メモリ管理の方針と実測

方針: 全キャッシュにバイト予算を持たせ、上限は設定一元管理（§2.2）。追い出しは容量ベースのみ（日数ベースは廃止）。ディスク配置は microSD の Brainux 領域。

実測で裏付けられたもの:
- llcache（RAM）: 予算からの導出値がログどおりに効いている（§2.2）。画像キャッシュは llcache の 1/4 で連動。
- JS ヒープ: ページ単位 4MB で実際に止まる（§3 jshog）。RSS が上限到達後に凍結することを確認。

**実測で見つけた上流欠陥（M3 の残件、証拠付き）**: fs バッキングストアの上限強制が NetSurf 3.11 では機能しない。
- 再現条件: `disc_cache_size=1048576`（1MB）、Cache-Control: max-age 付き HTTP で約3.6MB の画像40枚を読み、遷移してオブジェクトを解放、persist を待つ。なお persist 候補の条件は「fetch 完了・使用者ゼロ・残存ライフタイム > minimum_lifetime」で、**Cache-Control を送らない素の http.server では何も書かれない**（テストサーバ `test-harness/cache_server.py` で付与して切り分けた）。
- 欠陥1（会計）: `store_evict` が全41エントリを無効化しても `state->total_alloc` が 3,622,269 のまま減らない（ログ: `removed 3622269 in 41 entries, 3622269 remaining in 0 entries`）。原因箇所: `content/fs_backing_store.c` の `invalidate_entry` が、要素に HEAP/MMAP フラグがある場合に total_alloc を減らさず早期 return する（「allocation release 待ち」だが、release 側でも会計が戻らない）。
- 欠陥2（追い出し量）: 上流の `store_evict` は1回の呼び出しで hysteresis（上限/5）分しか消さないため、バースト書込みでは上限の数倍に膨れたまま残る。収束型への変更（`7ecaec5`）は欠陥1と組み合わさると全消し→再 persist のチャーンを起こすため revert した（`fa6cae6`）。**欠陥1と2はペアで直すこと**が次フェーズの必須作業。
- 欠陥3（事前確保）: ブロックファイル `dblk/A`・`mblk/A` が上限と無関係に各 8MiB へ ftruncate される（`1 << (log2_block_size + BLOCK_ENTRY_COUNT)`）。1MB 上限の実測でキャッシュディレクトリ総量は **20,392,970 B** になった（うち16MiB がこの2ファイル）。実機（容量の小さい SD）では上限に応じたスケーリングが必要。
- 結論: 現段階で「ディスクキャッシュは上限どおり」とは言えない。RAM 側（llcache/画像/JS）の予算は実証済み、ディスク側は未達として引き継ぐ。

## 6. 未実装・既知の問題（正直なリスト）

- **frontends/brain（ツールキット v2 UI）は設計のみ**（`DESIGN-brain-frontend.md`）。動く UI は fb フロント＋brain サーフェス。アドレスバー/タブ/履歴の製品 UI、シェル WM クライアント化（brain サーフェスへの `target=shm` 追加が前提）は次フェーズ。
- M1 の PSI 連動強制回収フック: 未実装（予算導出と上限のみ）。
- M2 の表示サイズデコード: JPEG のみ。他形式は上限で拒否（表示されない）のが現状の振る舞い。
- M4（IME/OSK 接続の IM フック）、M5（1本指タッチジェスチャ）、M9（リーダービュー）、M10（タブのメモリ回収）、M11（プライベートモード）: 未着手。
- JS 実行タイムアウトの不整合: `dukky_check_timeout` 内の 10 秒ハードコードが残り、`script_timeout` オプション（既定を5に変更）と一致しない（初版 jshog が 10.46 秒で止まった実測あり）。次フェーズでオプション参照に直す。
- brain サーフェスの KDSETMODE 未対応（§2.1）、`fb_input_glob` オプション未展開（`fb_input_devpath` のみ配線）。
- ディスクキャッシュ上限の欠陥3件（§5）。
- 履歴・ブックマークの原子的永続化: fb フロント既存の urldb 保存に依存しており、Brainux 向けの原子化（tmp→fsync→rename）は frontends/brain 側の作業として未実施。

## 7. 実機の現実への対応

- **突然のリセット**: RAM キャッシュと JS ヒープは使い捨て前提で問題なし。ディスクキャッシュは fs バッキングストアの control/entries 書込みに依存し、書込み中リセットでの自己修復は未検証（統合フェーズの試験項目）。履歴等の原子化は §6 のとおり未実施。
- **オフライン既定**: ネットワーク前提の機能は追加していない。キャッシュの有効性判断は日数ではなく残存ライフタイム（HTTP 意味論）と容量で行う。時刻が信用できない環境では max-age の絶対時刻評価がずれる可能性があり、統合フェーズで実機時刻条件の確認が要る。
- **配置**: キャッシュ既定パスは HOME 配下（microSD の Brainux 領域）。eMMC には一切触れていない。WinCE 併用への影響なし。
- **RAM 128MB**: 予算プロファイルの総量 56MB は選定文書 §4 の上限どおり。ただしホスト RSS 実測（17〜22MB）は x86-64 の参考値であり、ARM/実機での検証は統合フェーズ。

## 8. ARM クロスビルド

- ツールチェーン: Bootlin armv5-eabi glibc stable-2024.05-1（gcc 13.3.0）を `browser-engine/toolchain/` に展開。生成物は ELF 32-bit ARM EABI5 ソフトフロートで、Brainux（Debian 13 armel / gnueabi）と一致。
- **libnsfb の ARM クロスビルドは成功**: `src_surface_brain.o` が ARM EABI5 で生成され、brain_* 関数と `_nsfb_register_surface` 参照を含むことを確認。落とし穴: CC/AR の明示が必須、ホスト pkg-config が wld を誤検出するため `NSFB_*_AVAILABLE=no` の強制が必要。
- **NetSurf 本体の ARM ビルドはブロッカーあり（未実施）**: ARM 用 libcurl / OpenSSL / libpng / libjpeg / freetype / expat / libpsl の sysroot がワークスペース内に存在しない（buildbrain の buildroot は過去の掃除で消滅）。未試行の候補経路: Brainux が Debian 13 armel であるため、Debian armel の dev パッケージを取得・展開して sysroot を組む方法。統合フェーズで判断する。

## 9. 環境メモ（再発防止）

- VM 差し替えで apt の開発パッケージ（bison、flex、gperf、libcurl4-openssl-dev、libpsl-dev、check、libpng/libjpeg/libssl/libexpat/libwebp/libidn2 の dev 系）が消えていた。選定文書 §8.2 の手順で再インストールして復旧済み。VM 差し替えのたびに同じ復旧が要る。
- ビルド手順: `cd netsurf-all-3.11 && rm -f inst-framebuffer/build-stamp && make TARGET=framebuffer -j2`（スタンプ駆動。ログは `../build-brain-m0.log`）。所要はホストで約10分（並行 QEMU 作業と CPU を共有する環境では変動）。

## 10. 次フェーズへの引き継ぎ

1. ディスクキャッシュ欠陥の修正（§5 の1+2をペアで。3のブロックファイル・スケーリングも同時に）。修正後は本レポート §5 の再現手順で上限収束を再測定すること。
2. frontends/brain の実装（DESIGN-brain-frontend.md の F1〜F5）。前提として brain サーフェスへの `target=shm` 追加と、M4 の IM フック（コア側、既定 NULL）。
3. JS 実行タイムアウトを `script_timeout` オプション参照に統一。
4. 他形式（PNG 等）の表示サイズデコード、または最低限「上限超過でもプレースホルダ表示」への改善。
5. ARM sysroot の調達方法を決め、本体クロスビルドを通す（§8）。
6. 統合フェーズ: SD イメージ＋QEMU ゲストで RSS 実測、書込み中リセット試験、実キー/タッチでの操作確認。
