# REPORT — ブラウザ第2フェーズ（残件4項目）

- 日付: 2026-11-20
- 対象: `browser-engine/`（NetSurf 3.11 + frontends/brain）。buildbrain repo へは未統合・push なし。
- 前提: REPORT-browser-m0.md（§5 ディスクキャッシュ欠陥、§6 JS タイムアウト不整合、§8 ARM sysroot）、REPORT-brain-frontend.md（§10 撮影 15〜22）。
- 本レポートの実測は全てホスト x86-64。合格判定は統合フェーズ（SD+QEMU ゲスト）。

## 0. 作業開始時点の現状確認（実測）

- netsurf-all-3.11 の git HEAD は `c1a6634`。frontends/brain は未コミットの作業ツリー変更として存在（`netsurf/nsbrain` バイナリあり、2026-11-20 06:46 ビルド）。
- 撮影 20-article.jpg の生成時刻は 06:49:01、test-site/article.html への meta charset 追加は 06:49:59。**つまり撮影20は charset 追加「前」のフィクスチャで撮られている**。現在の article.html には `<meta charset="utf-8">` があり、ファイル自体は UTF-8（file コマンドで確認）。→ 文字化けが現行フィクスチャ+現行バイナリでも再現するかは未検証だったため、本フェーズで再撮影して切り分ける（§2）。

## 1. ディスクキャッシュ上限強制のペア修正（修正済み・実測あり）

### 1.1 ベースライン再現（修正前）

- 条件: `test-harness/drive_test.py cache`（nsfb 前端、disc_cache_size=1048576=1MiB、JPEG 40 枚・約 3.6MB のページを HTTP（Cache-Control: max-age 付き）で読み、遷移後に 30 秒待つ）。
- 結果: キャッシュディレクトリ総量 **20,392,970 B**（上限の約 19.4 倍）。M0 §5 の実測とバイト一致で再現。
- 内訳: dblk/A と mblk/A が各 8,388,608 B（=16MiB）＋データファイル約 3.6MB。du の実使用量も同値（この fs ではスパース化しない）。
- ログ: `store_evict: removed 1086323 in 12 entries, 1086323 remaining in 0 entries` — 全エントリ削除を主張しながら総量は不変という自己矛盾。

### 1.2 欠陥の機構（`netsurf/content/fs_backing_store.c`、コードを読んで確認）

前任の「ペアで直す」見立ては正しかった。欠陥は 3 件＋付随 1 件:

1. **会計の嘘（中核）**: `invalidate_entry` は要素に HEAP/MMAP フラグがあると ENTRY_FLAGS_INVALID を立てて早期 return し、storage 解放も `total_alloc` 減算もしない。一方 `store_evict` は渡した全エントリの名目サイズを `removed` に加算してログに出す。→ 実削除ゼロでも「削除した」と報告され、総量は永遠に減らない。llcache は store 直後に META のみ release し、DATA の HEAP 参照はオブジェクト破棄まで保持するため、RAM キャッシュに残るオブジェクトのエントリは従来**絶対に追い出せなかった**。
2. **追い出し量の不足**: 1 回の evict で hysteresis（limit/5）分しか消さない設計のため、バースト書込みで上限の数倍まで膨張する。再現条件では 1 回の読込で 3.6MB 書かれた。過去の試行 `7ecaec5`（停止条件を total_alloc <= limit に変更）は、壊れた会計と組み合わさって全エントリを INVALID 化する churn を起こし revert された（`fa6cae6`）— 片方だけ直すと別の不整合が出る、という見立ての実例。
3. **ブロックファイルの事前確保**: `set_block_extents` が開いたブロックファイルを上限と無関係に各 8MiB へ ftruncate（ブロック 8KiB×1024、ファイル数 6、要素 2 種 → 全開で最大 96MiB の予約）。
4. **付随**: `write_entries` は INVALID エントリも直列化し、`read_entries` が次回起動で復活させて total_alloc に再計上する（ゾンビ再計上）。

### 1.3 修正（全て fs_backing_store.c）

1. 新フラグ `ENTRY_ELEM_FLAG_NOSTORE (0x8)`: 「ディスク storage は追い出し済みで会計済み。heap 割当ては有効なまま」。`invalidate_element` は NOSTORE 要素の二重解放・二重会計を避けて早期 return する。ブロック解放時に `blocks_dirty = true` を追加（従来は欠落）。
2. 新ヘルパー `evict_element_storage`: ライブ割当てのある要素についてディスク側だけを解放（ブロック use_map ビット解除＋`elem->block=0`、またはファイル unlink）し、`total_alloc` を減算して NOSTORE を立てる。fetch は heap 側で継続動作する。
3. `store_evict` 書き換え: 目標は limit − hysteresis。**実際に解放されたバイトだけ**を数える。割当てなしエントリは完全削除、割当てありエントリは storage のみ追い出し＋INVALID 標識（後日の `release()` が完全削除を仕上げる）。残件数表示は `hashmap_count` で実数化。
4. `invalidate_entry`: hashmap_remove の前に `entries_dirty = true`（削除の永続化漏れ防止）。
5. `write_entry_iterator`: INVALID エントリを直列化しない（再起動での復活防止）。
6. `set_block_extents`: (a) 事前確保の総量を上限の半分に制限（1 ファイルあたり cap = limit/2/(要素 2×ファイル 6)、ブロック単位で切り捨て、最小 1 ブロック）。(b) use_map の最高使用ブロックまでにファイル実サイズを縮小する（高水位の固定化防止。実データが cap より上にある場合は切らない）。

### 1.4 実測（修正後）

- 1 次実測（修正 1〜5 と 6(a) 適用時）: 総量 20,392,970 B → **1,126,621 B**。evict ログが実態と一致するようになった（例: `removed 271388 in 3 entries, 816394 remaining in 33 entries` — 総量が実際に 1,087,782 → 816,394 へ減少し、目標 838,861=limit−hysteresis へ収束）。内訳: データファイル約 902KB（会計対象は上限内）、mblk/A 180,500 B（使用ブロック高水位）、dblk/A 40,960 B（cap どおり）、制御ファイル約 3KB。
- 6(b)（高水位縮小）適用後の再実測は §1.5 に追記する。

### 1.5 再実測（6(b) 適用後）

- 総量 **1,134,587 B**（1 次実測 1,126,621 B とほぼ同値。実行ごとの残存エントリ数差によるばらつきの範囲）。
- mblk/A は 188,416 B = 23×8,192 とブロック境界に一致し、高使用ブロックまでの縮小 ftruncate が動作していることを確認（1 次実測では 180,500 B と部分書込み端数だった）。ただし最終時点でも約 40 エントリが llcache 側に保持され、その META ブロック（8KiB 粒度）が高水位を決めるため、総量の大幅減にはならない。dblk/A は cap どおり 40,960 B。
- まとめ（上限 1MiB の試験条件）: 修正前 20,392,970 B（上限の約 19.4 倍）→ 修正後 約 1.13MB。**会計対象の格納内容は常時上限以下**（evict ログで total_alloc が上限−hysteresis=838,861 へ収束することを確認）。上限超過分（約 8%）は構造的スラック: META 用 8KiB ブロック粒度（実データ約 200B/件に対し 8KiB 予約）＋ブロックファイル事前確保（cap 内）＋制御ファイル約 3KB。本番の上限 32MiB では同じスラックはエントリ数依存で相対的に小さくなる。META ブロックサイズ自体の変更はディスク形式に触れるため本フェーズでは行わない。

## 2. 記事ページ文字化けの切り分け（完了: 前端・コアの欠陥ではない）

### 2.1 再現と切り分け（実測、撮影ハーネス test-harness/shot_brain、現行バイナリ・現行フィクスチャ）

| 条件 | 結果 |
|---|---|
| 現行 article.html（meta charset=utf-8 あり）を file:// で表示 | **正常**。メタ行「— Sample News 編集部」、日本語段落とも正しく描画（証拠: `shots-brain/23-article.jpg`、本フェーズで撮影） |
| charset 宣言を除いた同一内容を file:// で表示 | **文字化け再現**（撮影20と同一パターン: UTF-8 バイト列が windows-1252 として解釈された形） |
| charset 宣言なし・HTTP 配信で `Content-Type: text/html; charset=utf-8` ヘッダあり | **正常**。コアは HTTP ヘッダの charset を正しく採用する |

### 2.2 結論

- 撮影20の文字化けの原因は、**charset 宣言のないフィクスチャを file:// で開いた際のフォールバック解釈（windows-1252）**であり、frontends/brain にも NetSurf コアのエンコーディング処理にも欠陥はなかった。前任の「フィクスチャの charset 漏れ」という切り分け自体は正しかったが、(a) charset 追加（06:49:59）が撮影20（06:49:01）より後で、(b) 追加後の再撮影による裏取りが無かったため、「直ったはずだが証拠写真は化けたまま」という状態で残っていた。本フェーズで再撮影し、正常表示を `shots-brain/23-article.jpg` として証拠化した（撮影20は旧フィクスチャ時点の記録として残置）。
- framebuffer 前端との比較撮影は省略した。理由: 上記3条件で機構（宣言あり=正常 / HTTP ヘッダ=正常 / 宣言なし file のみ化ける）が完全に特定でき、前端差を疑う余地が消えたため。
- 残る仕様上の注意: charset 宣言が一切ない UTF-8 ページを file:// で開くと化けるのはフォールバックが windows-1252 のため（上流 NetSurf/libdom の既定動作）。実ページは meta か HTTP ヘッダで宣言するのが通常であり、本フェーズではデフォルト解釈を変更する魔改造は行わない（影響範囲が全ページに及ぶため、変更するなら別途判断が要る）。
- 回帰確認: 本フェーズの全コア変更（§1・§3）を入れた最終 nsbrain で同じ記事を再撮影し、正常表示を確認した（`shots-brain/24-article-final.jpg`、PSS 41,206 kB — 予算 56MB 内）。

## 3. JS タイムアウトの統一（修正済み・実測あり）

### 3.1 実態（コード確認、上流 baseline `512e382` でも同一）

- 呼出側 `desktop/browser_window.c:3194` は `js_newheap(nsoption_int(script_timeout), ...)` とオプション値を渡している。js.h の契約は「timeout は経過ウォールクロック秒」。
- しかし duktape 実装 `content/handlers/javascript/duktape/dukky.c` の `js_newheap` は引数を**一切使っておらず**、`dukky_check_timeout` が `#define JS_EXEC_TIMEOUT_MS 10000` をハードコードしていた。つまりオプション `script_timeout`（本ツリーの既定は M0 で 5 秒に変更済み）は無効で、実効は常に 10 秒だった。これは上流 NetSurf 3.11 自体の不整合（pristine baseline でも同じコード）。
- M0 §6 の実測（初版 jshog が 10.46 秒で停止）とも一致する。

### 3.2 修正（dukky.c）

- `struct jsheap` に `uint64_t exec_timeout_ms` を追加。`js_newheap` で引数（秒）×1000 を格納する。
- `dukky_check_timeout` はハードコードを廃し、heap の値を参照する。意味論: **timeout ≤ 0 は時間ベースの割込みなし**（Brainux の heap cap による割込みは従来どおり別途効く）。
- これで前端・本体で分かれていた扱いが「オプション script_timeout（秒）→ 全ヒープ共通の実行タイムアウト」に統一された。既定値は従来どおり 5 秒（options.h、M0 の判断を踏襲）。

### 3.3 実測（修正後 nsfb、フィクスチャ `.work-p2/js-loop.html` = 割当てをほぼしない純粋ビジーループ。heap cap では止まらないため、止めるのは実行タイムアウトだけ）

| 条件 | 結果 |
|---|---|
| script_timeout 未指定（既定 5） | ログ時刻 5.02 秒で `RangeError: execution timeout`、CPU 停止 約 5.5 秒 |
| script_timeout=2 | ログ時刻 2.02 秒で同エラー、CPU 停止 約 2.5 秒 |
| script_timeout=0 | 25 秒の観測窓内ずっと CPU が回り続け、時間ベースでは停止しない（仕様どおり） |

修正前は条件によらず 10 秒固定だった（M0 実測 10.46 秒）。

## 4. ARM sysroot 構成（構築完了・クロスビルド完走を実測）

### 4.1 構成

- `.sysroot-arm/` を新設（x86 用 `.sysroot/` と同じ「.deb 展開のみ」の流儀）。Brainux が Debian 13 (trixie) armel のため、取得元は deb.debian.org の trixie/main/binary-armel。
- `fetch_debs.py`: Packages 索引を取得し、種パッケージ（libc6-dev, linux-libc-dev, zlib1g-dev, libcurl4-openssl-dev, libssl-dev, libpng-dev, libjpeg62-turbo-dev, libfreetype-dev, libexpat1-dev, libpsl-dev, libwebp-dev, libidn2-dev）の Depends を再帰解決して .deb をダウンロード＋`dpkg-deb -x` で展開する。解決結果 **90 パッケージ**（`PACKAGES.txt` に記録）、sysroot 総量 158MB。主要バージョン: libcurl 8.14.1、OpenSSL 3.5.7、libpng 1.6.48、freetype 26.2.20。
- .pc ファイルは x86 .sysroot と同じく prefix を sysroot への絶対パスへ書換え（47 件）。freetype2.pc 等に残っていたハードコードの `-L/usr/lib/...` も書換え、最終的に pkg-config が出す -I/-L が全て sysroot 内を指すことを確認済み。
- merged-usr 対策: Debian の libc.so リンカスクリプトが `/lib/arm-linux-gnueabi/libc.so.6` と `/lib/ld-linux.so.3` を絶対参照するため、sysroot 内に `lib -> usr/lib` シンボリックリンクを 1 本追加。

### 4.2 コンパイラ側の適応（Bootlin gcc 13.3.0 は素のビルドで multiarch を知らない）

`arm-linux-gcc` ラッパー（`build-arm.sh` が生成）で以下を注入する:

- `--sysroot=.sysroot-arm`: ヘッダと libc はツールチェーン同梱の旧 glibc ではなく Debian 側（glibc 2.41、ターゲットと同一）を使う。
- `-B<sysroot>/usr/lib/arm-linux-gnueabi/`: crt1.o/Scrt1.o/crti.o の探索先（LIBRARY_PATH では startfile が見つからないことを実測）。
- `-I<sysroot>/usr/include/arm-linux-gnueabi`: bits/ 等の multiarch ヘッダ。
- `-L` ＋ `-Wl,-rpath-link,<同 lib dir>`: 推移的 DT_NEEDED（libcurl の libnghttp2/libldap 等）の解決。これが無いと「not found」警告＋未定義参照でリンク失敗する（実測）。

### 4.3 ビルド手順の落とし穴（全て実測で遭遇・解決）

1. ホストに flex/bison/gperf が無い（VM 差し替えで消失。x86 ビルドは生成済みパーサのキャッシュで動いていた）。apt は依存関係が壊れており install 不可だったため、`apt-get download`＋展開で `.work-p2/hosttools/` に用意した。Ubuntu の bison は展開先で使う場合 `BISON_PKGDATADIR` と `M4` 環境変数が必須（実測）。
2. netsurf-all のトップ Makefile は HOST をライブラリ群へは渡すが**ブラウザ本体へは渡さない**。さらに前端の Makefile.tools は RISC OS の GCCSDK 以外では CC を設定しないため、本体は既定の `cc`（ホスト）でコンパイルされ、リンク時に ARM ライブラリと混ざって失敗する（build/arm-linux-framebuffer に x86-64 オブジェクトが生成される形で確認）。対策: 本体だけを `CC=arm-linux-gcc CXX=arm-linux-g++` を環境で与えて直接 make する（`build-arm.sh` の stage 2）。
3. 本体の duktape バインディング生成にホスト用 `nsgenbind` が要る（stage 1 が inst-armfb/bin へ入れるので PATH に足す）。
4. pkg-config は `PKG_CONFIG_LIBDIR` で ARM 側だけに制限する（ホストの .pc を誤検出させない）。

再現手順は `.sysroot-arm/build-arm.sh` に一本化した（上記 1〜4 を内蔵）。

### 4.4 結果

- **クロスビルド完走**（この VM、実測）: ライブラリ全コンポーネント＋nsgenbind（ホスト用）＋ブラウザ本体がビルドされ、`netsurf/nsfb` としてリンク成功。成果物: **ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), soft-float ABI, interpreter /lib/ld-linux.so.3**、12,664,004 B（証拠として `.work-p2/nsfb-arm` に保存）。依存ライブラリ全て（curl/png/freetype/expat/psl/webp/ssl/jpeg/z/idn2）へのリンクも個別に確認済み。
- 注意: 上流のビルドは x86 と ARM で同じ出力パス（netsurf/nsfb）を使うため、ARM ビルド後に x86 の nsfb は再リンクで復旧した（復旧後の render シナリオ通過を確認）。build-arm.sh にも警告を明記。
- ARM バイナリの**実行**確認はしていない（この VM に qemu-arm が無い）。実機相当での動作確認は統合フェーズ（SD＋QEMU ゲスト）の担当。

## 5. コピー/オーバーヘッド削減

- §1 の修正はコピーを増やしていない。storage のみの追い出し（evict_element_storage）はブロック use_map のビット解除と会計の減算だけで、データの読み直し・書き直しは発生しない。fetch が継続する場合は従来どおり heap 側の割当てをそのまま使う（追加コピーなし）。
- §1 の 6(b)（ブロックファイル縮小）は 10 秒周期の control maintenance 内の ftruncate のみで、データ転送は伴わない。
- §3 の修正はタイマ照合の定数を heap フィールド参照に変えただけで、割込みコールバックの頻度・経路は従来どおり。
- §4 の sysroot は .deb 展開のみで、ホストへのインストールや変換処理を挟まない。

## 6. キャッシュ/メモリ管理

- ディスクキャッシュ: 上限は従来どおり容量駆動（disc_cache_size、既定 32MiB）。日数ベースの有効期限は使っていない（disc_cache_age は M0 で廃止済みのまま）。本フェーズで上限強制の会計が実態と一致するようになり、追い出しは「実際に解放したバイト」で数える。ブロックファイルの事前確保は上限の半分を総量キャップとし、使用ブロック高水位までの縮小も入れたため、予約が実使用から乖離し続けることはない。
- RAM 側: 本フェーズで RAM キャッシュや JS ヒープ予算（PSS 56MB、brain_js_heap_kb）の変更はない。§3 で script_timeout=0 の場合に時間割込みが無くなるが、heap cap による割込みは残るため、メモリ上限の防衛は維持される。
- 撮影時の PSS: 記事ページ 41,206 kB（§2、最終バイナリ）、cache シナリオ 27,720 kB、render シナリオ 17,176 kB — いずれも予算 56MB 内（ホスト x86-64 実測）。

## 7. 実機の現実への対応

- **突然のリセット**: §1 の修正で、追い出し済みエントリが entries ファイルへ直列化されなくなった（INVALID の除外）ため、リセット後にゾンビエントリが復活して上限会計を汚す経路が消えた。ただし書込み途中リセットでのキャッシュ自己修復そのものは未検証（§8）。
- **オフライン既定・時刻不信**: 本フェーズの変更は時刻ベースの判定を新たに導入していない。§3 のタイムアウトは monotonic 時計（nsu_getmonotonic_ms）基準で、壁時計の狂いの影響を受けない。
- **WinCE 併用・eMMC ノータッチ**: 本フェーズの作業は全てホスト上の browser-engine 内で、eMMC には触れていない。ARM sysroot の取得もネットワークからの .deb 展開のみ。
- **SD 配置**: キャッシュの配置先・既定パスの変更はない（HOME 配下）。

## 8. 未検証の明記

- 本フェーズの実測は全てホスト x86-64（ARM はビルドのみ）。合格判定は統合フェーズ（SD＋QEMU ゲスト）で行う。
- §1: 検証は単一シナリオ（1MiB 上限・JPEG 40 枚・30 秒観測）。本番上限 32MiB での長時間運用、再起動を跨いだキャッシュ再利用（entries 読戻し）の実測はしていない。書込み途中リセット時の自己修復は未検証。
- §2: 切り分けは file:// と自前 HTTP サーバ（charset ヘッダ）で実施。実サイトの多様な宣言パターン（BOM のみ、HTTP ヘッダと meta の矛盾等）は未検証。fb 前端との比較撮影は機構が特定できたため省略した（§2.2 に理由）。
- §3: 検証は純粋ビジーループのフィクスチャのみ。実ページのスクリプト（タイマ・イベント駆動）での停止タイミング、script_timeout の前端 UI からの設定可否は未検証。
- §4: ARM バイナリの実行確認は未実施（qemu-arm がこの VM に無い）。Brainux ゲスト上での起動・描画・PSS は統合フェーズで確認する。build-arm.sh は通しで再実行していない（各 stage は個別に実行して成功を確認済み。再実行は冪等なはずだが未検証と明記する）。
- 項目外: frontends/brain の UI 変更は本フェーズでは行っていない（コア 2 ファイルのみ変更: content/fs_backing_store.c、content/handlers/javascript/duktape/dukky.c）。repo へは push していない（作業ツリーの変更として残置）。
