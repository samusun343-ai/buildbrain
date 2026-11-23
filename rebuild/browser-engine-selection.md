# ブラウザ・エンジン選定と端末特化魔改造計画（bd-web v2）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


- 日付: 2026-11-20
- 前提（ユーザー決定）: 「既存エンジンを使用してもいいが、既存エンジンにも超魔改造のメスを本気で入れる。端末特化で」。**素のエンジンをそのまま入れるのは不可。**
- 対象端末: SHARP Brain PW-SH6（i.MX283 / ARMv5 単コア / RAM 128MB / 480×272 級・抵抗膜タッチ（1本指）/ Linux fbdev。X は動くが重い）
- 目標: 「日常使い＋α、爆速・低メモリで実用的」
- 方法: 候補の比較は web 調査の出典付き（§9）。規模は取り込んだ実ソースの `wc -l` 実測。監査レポート（`~/workspace/goals/brainux-buildbrain-integration/hidden_files/audit-2026-11-20.md` §2.2 / §5）の結論「エンジン採用＋自前フロント 3,000–6,000 行」を引き継ぐ。

---

## 1. 選定結果

### 主選定: **NetSurf 3.11**（`netsurf-all-3.11` 一式、フォークして魔改造）

- 版: NetSurf 3.11（2023-12-28 リリース。公式サイトの現行最新リリース）＋自前ライブラリ群一式
- ライセンス: ブラウザ本体は **GPL-2.0-only**、自前ライブラリ群（LibCSS / LibDOM / Hubbub / libnsfb 等）は **MIT**（各 `COPYING` で確認）。buildbrain 側の配布形態（GPL プロジェクト）と矛盾しない
- 言語: 移植性重視の C（C++ なし）。ARMv5 / 128MB で動くこと自体が設計の出発点（起源は 200MHz StrongARM の RISC OS。「リソース制約のあるハードウェアでよく動く」が公式の自己紹介）
- 取り込み実物: `~/workspace/buildbrain-work/rebuild/browser-engine/netsurf-all-3.11/`（§8 に取得記録とビルド結果）

### 選定理由（要約）

1. **128MB で「グラフィカル＋CSS＋部分的 JS」を満たす唯一の現実解**。32MB 級でも動く設計（3DS 移植は 64MB 機で動作）で、他候補は桁違いに重いか、CSS/JS を捨てている
2. **内部がライブラリに分割されており、メスを入れる場所が部品単位で特定できる**（§3）。エンジンがモノリスでないことが「本気の魔改造」の前提条件
3. **フロントエンド差し替えが正式の設計**（GUI テーブル＋プロッタ API）。framebuffer フロントエンドが同梱され、ツールキットを持たない環境での実績がある
4. Duktape による JS と DOM バインディング生成（nsgenbind）があり、「文書中心サイト＋軽い JS」が視野に入る。Dillo / Links / w3m は JS を構造的に持たない

### 割り切り（黙って捨てない。監査総則4に基づき明示する）

- **SPA（Gmail / X / YouTube / Docs 等）は対象外**。Duktape は ES5 級＋DOM バインディングは部分実装であり、これはエンジンの限界であって設定で直るものではない。「日常使い＋α」は文書系サイト・検索・フォーム・ダウンロード・軽い JS サイトの範囲と定義する
- **CSS Grid は 3.11 の対応範囲に無い**（公式の対応表記は HTML 4.01 / CSS 2.1、3.11 で flex 追加）。Grid 依存サイトは劣化表示になる
- リリース間隔は遅い（3.11 が 2023-12、開発は git と自動ビルドで継続）。フォーク前提なので許容するが、TLS は自前でなく **libcurl＋OpenSSL（システム側）** に寄せ、そちらの更新で追随する構成にする

---

## 2. 候補比較（出典は §9）

| 候補 | (a) 規模と保守状態 | (b) ライセンス | (c) ARMv5/128MB 現実性 | (d) レンダリング/JS/CSS | (e) フロント差し替え・内部改造 |
|---|---|---|---|---|---|
| **NetSurf 3.11** | 自前コード実測 **約46万行**（コア＋fb フロント 252,950 行、ライブラリ群 約20万行、§3）。2002 年開始、最新リリース 3.11（2023-12-28）、開発は git 継続 | 本体 GPL-2.0、ライブラリ MIT | **高い**。起源が 200MHz StrongARM、3DS（64MB）移植が動作。fb フロントはツールキット不要 | 独自レイアウトエンジン。HTML5 パーサ（Hubbub）、DOM（LibDOM）、CSS 2.1＋一部 CSS3＋flex（LibCSS）。JS は Duktape＋生成バインディングで部分対応 | **最良**。ライブラリ分割（Hubbub/LibDOM/LibCSS/libnsfb/nsgenbind 他）、GUI テーブル＋プロッタ API が正式境界、fb フロント同梱 |
| Dillo 3.3.0 | 2023 年に開発再開、3.3.0（2026-04-26）と保守は活発。ただし単一アプリ構成でエンジン/フロントの分離は弱い | GPL-3.0-or-later | 高い（旧式・低速機と組込みが対象） | HTML/XHTML＋CSS 部分対応。**JS は意図的に無し**、右横書き・複合テキスト整形も無し | FLTK 前提。fb 化するには FLTK の fbdev 化が先に必要で、エンジン内部もライブラリ分割されていない。魔改造の単位が粗い |
| WebKitGTK (WebKit) | Source 実測 **約358万 SLOC**（監査 §2.2）。ビルド自体が cc1plus 1 プロセス 1–1.3GB RAM を要求し、ホスト 16–32GB 推奨という報告がある | BSD-3-Clause ＋ LGPL-2.0-or-later | **無い**。マルチプロセス＋JIT＋GPU 合成前提。128MB 単コア ARMv5 では起動すら現実的でない | 最強（現代 Web フル対応） | 埋め込み API はあるが、内部改造は規模的に不可能。落選 |
| Ladybird | C++23＋Rust＋Qt6、pre-alpha（初の公式 alpha は 2026 年目標）。ビルドに 8GB+ RAM・30GB ディスク・モダンな clang が必要とされる | BSD-2-Clause | **無い**。x86-64-v3（AVX2）前提の配布物まである。ARMv5 向けではない | LibWeb/LibJS は現代仕様志向だが未成熟 | プロセス分割設計で組込み向けでない。落選 |
| Links 2 (2.30) | C、保守継続（2.30 / 2024-07-27）。グラフィックモードは framebuffer/SVGAlib 対応 | GPL-2.0-or-later | 高い（非常に高速・軽量） | HTML 4.0 部分対応（表・フレーム）、**CSS 無し、JS は現行版では削除済み**。画像表示はあるが「ページが意図通りに表示されないことが多い」 | 単一バイナリ志向でエンジン/フロント分離なし。テキスト＋簡易グラフィックの割り切りは「日常使い＋α」に届かない。落選（非常用テキストブラウザとしては別途価値あり） |
| w3m 0.5.6 / Lynx 2.9.2 | 共に保守継続（w3m は新フォークが 0.5.6 / 2026-01、Lynx は現役最古参） | w3m: MIT、Lynx: GPLv2 | 高い | **テキスト専用**。表・フレーム変換表示のみ、CSS/JS なし、画像は外部ビューア | 対象外。グラフィカル表示の要求を満たさない。落選 |

### 次点の扱い

- **次点は Dillo**。もし NetSurf の Duktape/DOM が実測で使い物にならないと判明した場合の保険として、比較表と取得先は残す。ただし Dillo に切り替える場合は「JS を捨てる」判断が伴うため、ユーザー承認なしに切り替えない
- WebKitGTK / Ladybird は「将来ハードが変われば」の話であり、本端末では検討対象から完全に外す

---

## 3. 選定エンジンの実構成（実ソースで確認）

### 3.1 規模実測（`netsurf-all-3.11`、`wc -l`、C/H のみ）

| 部品 | 行数 | 役割 |
|---|---|---|
| netsurf コア＋fb フロント＋ヘッダ | 252,950 | レイアウト、コンテンツ処理、デスクトップ機能（履歴・ブックマーク・ダウンロード等）、fb フロント |
| libcss | 74,657 | CSS パース・カスケード・セレクタ・計算値 |
| libdom | 51,516 | DOM Core（IDL からバインディング生成） |
| libhubbub | 19,302 | HTML5 パーサ |
| libutf8proc | 17,266 | Unicode 処理（借り物同梱） |
| libnsfb | 14,608 | framebuffer 抽象（サーフェス＋プロッタ） |
| libparserutils | 8,931 | 文字コード変換等の基盤 |
| libsvgtiny | 4,937 | SVG（tiny プロファイル） |
| libnsgif / libnsbmp | 4,721 / 2,122 | GIF / BMP デコード |
| libnslog | 4,403 | ログ |
| libwapcaplet / libnsutils / libnspsl | 1,183 / 1,064 / 269 | 文字列インターン / 雑多 / Public Suffix |
| nsgenbind | 21,615 | JS-DOM バインディング生成ツール（ホスト側） |
| **合計** | **約 479,000** | （＋ Duktape 本体が JS ハンドラ内に同梱） |

bd-web v1（2,973 行）との比較で言えば、エンジンだけで約 160 倍。これが「日常使い」を名乗るための実在の規模である。

### 3.2 改造境界として使う API

- **GUI テーブル**（`netsurf/include/netsurf/`）: `window.h`（gui_window_table）、`misc.h`（スケジューラ等）、`download.h`、`clipboard.h`、`search.h`、`core_window.h`（履歴・ブックマーク等のツリーウィンドウ用）、`plotters.h`（描画プロッタ）。フロントエンドはこのテーブル群を実装する側であり、コアはフロントを知らない
- **デスクトップ層**（`netsurf/desktop/`）: `browser_window.c`（タブの中身）、`hotlist.c`（ブックマーク）、`global_history.c` / `local_history.c`、`download.c`、`search.c`＋`content/textsearch.c`（ページ内検索）、`textarea.c` / `textinput.c`（テキスト入力）、`knockout.c`（テキスト抽出＝リーダー基盤）、`save_complete.c` / `save_text.c`（ページ保存）、`print.c` / `save_pdf.c`（印刷・PDF＝切除候補、M6）
- **コンテンツ層**（`netsurf/content/`）: `hlcache.c`（高レベルキャッシュ）、`llcache.c`（低レベルキャッシュ＋ディスクストア連携）、`fs_backing_store.c`（ディスクキャッシュ実体）、`fetch.c`＋`fetchers/curl.c`、`handlers/image/`（`image_cache.c` と各デコーダ）、`handlers/html/`（レイアウト `layout.c`、フォーム、対話処理）、`handlers/css/`、`handlers/javascript/duktape/`
- **オプション機構**（`netsurf/utils/nsoption.c`、`netsurf/desktop/options.h`、`frontends/*/options.h`）: 全設定は `NSOPTION_*` マクロで型付き定義され、Choices ファイルに永続化される。brainreg 連携はこの層に橋を架ける（§7.3）

### 3.3 重要な発見: 3.11 の libnsfb に Linux fbdev サーフェスが無い

- `libnsfb/src/surface/` にあるのは `ram.c / sdl.c / x.c / wld.c（Wayland）/ vnc.c` のみ。列挙型 `NSFB_SURFACE_LINUX` は残っているが実装ファイルがリリースに含まれない（ホストビルドでも登録されたのは ram と wld だけだった、§8）
- つまり「fb フロントがあるから fbdev でそのまま動く」は **3.11 では成り立たない**。Brain は X / Wayland / SDL のどれも既定にしない方針なので、**fbdev サーフェスの新規実装（または旧 linux サーフェスの復活）が魔改造の第0号**になる（M0）。サーフェス API 自体は小さい（initialise / finalise / claim / update / input の関数テーブル、`libnsfb/src/surface.h`）ので、これは十分に射程内である

### 3.4 既定値の実測（デスクトップ前提の値がそのまま入っている）

`netsurf/desktop/options.h`、`frontends/framebuffer/options.h` より:

| 設定 | 既定値 | Brain での問題 |
|---|---|---|
| `memory_cache_size` | 12 MB | llcache＋画像キャッシュの総枠。根拠のない固定値で、端末予算と無関係 |
| `disc_cache_size` | **1 GB** | SD 容量と寿命に対して非常識。端末では 32MB 級が上限 |
| `disc_cache_age` | 28 日 | SD では短縮候補 |
| `max_fetchers` | 24 | 単コア ARMv5 では多すぎる（デコード競合とメモリ圧） |
| `max_fetchers_per_host` | 5 | 同上 |
| `max_cached_fetch_handles` | 6 | 調整対象 |
| `curl_fetch_timeout` | 30 秒 | 回線事情で調整対象 |
| `script_timeout` | 10 秒 | 単コアでは長すぎる |
| `enable_javascript` | false | 「＋α」の範囲で有効化を検討（M7 とセット） |
| `fb_depth` | 32 bpp | 480×272 でも帯域の無駄。16bpp（RGB565、プロッタ `libnsfb/src/plot/16bpp.c` あり）に変更 |
| `fb_font_cachesize` | 2,048 グリフ | 日本語はグリフ数が桁違い。予算化と遅延ロードが必須（M4） |
| `fb_osk` | false（OSK ボタン表示） | fbtk 付属 OSK（`frontends/framebuffer/fbtk/osk.c`、199 行の英字キーボード）は使わず、OSK v2 への接続点としてのみ使う（M4） |

加えて `netsurf/desktop/netsurf.c` の `netsurf_init()` が全予算の起点である: `memory_cache_size` → llcache 上限、その **1/4 を画像キャッシュ**（`image_cache_parameters.limit = llcache.limit / 4`）、`disc_cache_size` → バッキングストア上限、帯域前提は `LLCACHE_STORE_MIN_BANDWIDTH 128KB/s / MAX 1MB/s`（同ファイル内定数）、下限は `MINIMUM_MEMORY_CACHE_SIZE 2MB`。**魔改造 M1 はこの関数が主戦場**になる。

---

## 4. 端末メモリ予算（RAM 128MB 全体からの割当て）

ブラウザ単体の上限ではなく、DE 常駐を前提にした全体予算として固定する（数値は初期値であり、第5段の実測で確定させる）。

| 区分 | 予算 | 根拠 |
|---|---|---|
| カーネル＋システム常駐 | 約 30 MB | Brainux 側の実測ベースライン（監査 §1.6、セッション PSS 約 12.8MB はシェル側） |
| braindesk（シェル＋ツールキット v2） | 約 15 MB | 現行シェル PSS 4.6MB からの増加を見込む |
| **ブラウザ プロセス全体（フォアグラウンド時 PSS）** | **最大 56 MB** | 残りを全部ブラウザに回してもこの水準。超えたらキャッシュ→背景タブ内容の順に落とす（M1/M10） |
| うち llcache（メモリキャッシュ＋DOM 等の保持） | 14 MB | `memory_cache_size` の Brain 既定値として設定 |
| うち 画像キャッシュ（デコード済みビットマップ） | 3.5 MB（llcache の 1/4、既存の配分比を維持） | 480×272×16bpp = 0.26MB/画面。約 13 画面分 |
| うち フォントグリフキャッシュ | 2 MB 上限 | 日本語フォントの必要分と上限の両立（M4） |
| うち JS ヒープ（Duktape、ページ当たり） | 4 MB 上限・超過時はそのページの JS を停止 | Duktape のヒープ上限 API で強制 |
| ディスクキャッシュ（SD 上） | 32 MB / 7 日 | §3.4 の 1GB から変更（M3） |
| 同時フェッチ | 全体 6 / ホスト毎 2 | §3.4 から変更（M7） |

---

## 6. 最初の改造 1 件: M1 の前半「予算プロファイル導入」の当たり

改造コードはまだ書かない。対象ファイルと差分の形をここまで特定した。

### 対象ファイル

| ファイル | 触る場所 |
|---|---|
| `netsurf/desktop/options.h` | 新オプション追加: `NSOPTION_INTEGER(brain_budget_mb, 0)`（0 = 無効＝従来動作）、および Brain 既定値の変更: `disc_cache_size` 1GB → 32MB、`disc_cache_age` 28 → 7、`max_fetchers` 24 → 6、`max_fetchers_per_host` 5 → 2、`curl_fetch_timeout` 30 → 20、`script_timeout` 10 → 5 |
| `netsurf/desktop/netsurf.c` | `netsurf_init()` 内、現在 147–176 行付近の予算配分ブロック（`memory_cache_size` → llcache.limit → 画像 1/4 → disc store）。ここに予算プロファイルの導出を差し込む |
| `netsurf/frontends/framebuffer/options.h` | `fb_depth` 既定 32 → 16（M0 とセット。プロッタは `libnsfb/src/plot/16bpp.c` が既存） |

## 7. 自前フロント「bd-web v2」の要求

### 7.1 構成方針

- 新フロントエンド `frontends/brain/` を新設する。土台は framebuffer フロント（`frontends/framebuffer/gui.c` 2,323 行が参照実装）だが、ウィジェットは fbtk ではなく **ツールキット v2** で描く。描画・入力の最下層は M0 の libnsfb「brain」サーフェス（bui_fb / bui_input 接続）
- コア側の既存部品を最大限使う: ブックマーク = `desktop/hotlist.c`、履歴 = `desktop/global_history.c` + `content/urldb.c`、ダウンロード = `desktop/download.c` + GUI ダウンロードテーブル、ページ内検索 = `desktop/search.c` + `content/textsearch.c`、履歴・ブックマークのウィンドウは `core_window.h` の実装パターン（参照: `frontends/framebuffer/corewindow.c`）
- 自前で書くのは「タブ管理・ツールバー・各種ウィンドウのツールキット v2 実装・brainreg 橋渡し・タッチ操作（M5）」に限定する。監査の見積もり（自前フロント 3,000–6,000 行）の範囲はこの構成を前提とする

### 7.2 機能表（参照調査 Must-not-omit 10 件との突き合わせ）

| # | 要求（参照調査 §8） | 実装方式 | 依存 |
|---|---|---|---|
| 1 | タブ: 開く/閉じる/**閉じたタブを開き直す**/切替 | フロント新規（タブ = browser_window 1 個）。閉じたタブのスタック（URL＋スクロール位置）をフロントの状態ファイルに保持 | ツールキット v2 のタブ部品、M10 |
| 2 | アドレス＋検索の統合フィールド（履歴・ブックマークからの補完） | フロント新規＋コアの urldb 補完と `desktop/searchweb.c`（検索プロバイダ） | テキスト編集コア、M4（IME） |
| 3 | 戻る/進む/再読込（長押しで履歴メニュー） | コア `browser_history` / `local_history`（corewindow 実装） | M5（長押し） |
| 4 | ブックマーク: 追加、バー（表示切替）、**フォルダ付きマネージャ** | コア `hotlist.c`（フォルダ対応済み）＋フロントのバーとマネージャウィンドウ | corewindow パターン |
| 5 | 履歴ページ: 日付グループ、検索可能、消去可能 | コア `global_history.c`＋フロントの履歴ウィンドウ。消去は urldb 側の操作として実装 | M8（件数上限と整合） |
| 6 | ダウンロード: 進捗一覧、一時停止/取消、開く、保存先で表示（bd-files で開く） | コア `download.c`＋GUI ダウンロードテーブルをフロントが実装。一覧は corewindow。一時停止は llcache ハンドルの保持/解放で実現可否を検証する | 保存先は brainreg（§7.3） |
| 7 | ページ内検索: 件数表示、次/前 | コア `search.c` + `textsearch.c`（参照実装: `frontends/framebuffer/fb_search.c`） | — |
| 8 | プライベートウィンドウ（履歴・Cookie を残さない、見た目で区別） | **M11 のエンジン側実装**＋フロントの色調・ラベル | M11 |
| 9 | リーダービュー | **M9**（knockout 抽出の再レンダリング） | M9 |
| 10 | ズーム（拡大/縮小/リセット）＋印刷 | ズームはコアのスケール設定をフロントのボタン/ダブルタップ（M5）から操作。**印刷は本端末に印刷経路が無いため「ページ保存（完全保存/テキスト）」への代替を提案し、Must-not-omit からの除外はユーザー承認を取る**（黙って落とさない） | M5、ユーザー判断 |

Must-not-omit 外だが監査 §5.3（第5段合格基準）から必須とするもの: 実サイト集合（文書系、フォーム、ダウンロード、検索）を通すこと、セッション全体 PSS が §4 予算内であること、JS 有効時に軽い JS サイト（フォーム検証程度）が動くこと。

### 7.3 設定（brainreg 連携）

- 橋渡し方式: 起動時に brainreg の `browser.*` キーを読み、nsoption に流し込む。終了時に nsoption の Choices ファイルへ書く代わりに brainreg へ書き戻す。ブラウザ単独の設定ファイルは持たない（設定の一括管理というユーザー要求に合わせる）
- キー一覧（初期）: `browser.budget_mb`（M1）、`browser.disc_cache_mb` / `browser.disc_cache_enabled`（M3）、`browser.javascript_enabled`、`browser.images_enabled`、`browser.user_agent`（M7）、`browser.search_provider`、`browser.homepage`（M8）、`browser.download_dir`、`browser.font_size`、`browser.default_scale`（M5）
- 設定 UI はツールキット v2 上のフロント内ページとして作り、DE の設定アプリからも同じ brainreg キーを編集できるようにする（二重管理にしない）

---

## 8. 取り込みとホストビルドの結果

### 8.1 取得記録

| 項目 | 値 |
|---|---|
| 取得物 | `netsurf-all-3.11.tar.gz`（ブラウザ本体＋全自前ライブラリ＋nsgenbind の一式） |
| 取得元 | `http://download.netsurf-browser.org/netsurf/releases/source-full/netsurf-all-3.11.tar.gz`（公式ダウンロード） |
| 版 | NetSurf **3.11**（2023-12-28） |
| サイズ | 9,080,781 bytes |
| MD5 | `1554bb1093ef582da6b81883e181a3cf` |
| SHA-256 | `4dea880ff3c2f698bfd62c982b259340f9abcd7f67e6c8eb2b32c61f71644b7b` |
| 展開先 | `~/workspace/buildbrain-work/rebuild/browser-engine/netsurf-all-3.11/` |
| ライセンス | 本体 GPL-2.0-only（公式 About および配布条件）、ライブラリ群 MIT（各 `COPYING` で確認） |

### 8.2 ビルド手順と結果

```
# 依存の追加インストール（ホスト Ubuntu 24.04、apt）
apt-get install -y libcurl4-openssl-dev gperf libpsl-dev check
# （既存で存在: gcc 13.3, flex, bison, libpng-dev, libjpeg-dev, libssl-dev,
#   libexpat1-dev, libwebp-dev, libidn2-dev, pkg-config, perl）

cd netsurf-all-3.11
make TARGET=framebuffer -j2     # ログ: ../build-framebuffer.log
```

- **結果: 成功（EXIT=0、約 189 秒、-j2）**。全ライブラリ（libcss / libdom / libhubbub / libnsfb 他）が `inst-framebuffer/` プレフィックスへインストールされ、最終バイナリ `netsurf/nsfb` がリンクされた
- バイナリ: x86-64 ELF、13,431,496 bytes（デバッグ情報付き）／ strip 後 **4,235,816 bytes**。JS（Duktape）有効のフル構成での値
- このビルドで libnsfb に登録されたサーフェスは **ram と wld（Wayland）のみ**。SDL サーフェスは検出されず未登録（SDL 開発環境の検出条件は未確認）。Linux fbdev サーフェスは §3.3 の通り 3.11 に存在しない
- 起動スモークテスト: `NETSURF_RES=<...>/frontends/framebuffer/res ./nsfb -f ram -w 480 -h 272 file:///tmp/smoke.html` → エラーなくメインループに入り、6 秒間（timeout で終了させるまで）動作継続。リソース（CSS / Messages / フォント）の読み込みも同パスで成立
- 未実施: ARMv5 クロスビルド、ゲスト（QEMU）での実ページ表示、実測 PSS。これらは第5段の作業であり、本書はホストでの「ビルド可能」の確認まで

---

## 9. 出典（web 調査、2026-11-20 取得）

- NetSurf 公式 About（GPLv2、C で記述、独自エンジン、fb フロント、200MHz StrongARM 前提）: https://www.netsurf-browser.org/about/
- NetSurf 公式ソース配布（3.11、2023-12-28）: https://www.netsurf-browser.org/downloads/source/
- NetSurf 3.11 リリース（CSS flex 追加、Duktape 2.7.0）: https://download.netsurf-browser.org/netsurf/releases/ChangeLog.txt ／ https://www.mail-archive.com/netsurf-users@netsurf-browser.org/msg08081.html
- NetSurf の構成（Hubbub / LibDOM / LibCSS / Duktape、ライブラリは MIT、本体 GPL-2.0）: https://vectree.io/pdf/c/netsurf ／ https://en.wikipedia.org/wiki/NetSurf
- NetSurf 3DS 移植（64MB 機で動作、fb フロント、32MB 級でも動作する設計）: https://www.gamebrew.org/index.php?title=Netsurf_3DS&curid=36497&diff=203367&oldid=145153
- Dillo（3.3.0 / 2026-04-26、GPL-3.0-or-later、FLTK、JS なし）: https://en.wikipedia.org/wiki/Dillo
- WebKitGTK（ライセンス BSD-3-Clause ＋ LGPL-2.0-or-later、ビルド時のメモリ要求）: https://rpmfind.net/linux/RPM/opensuse/tumbleweed/noarch/WebKitGTK-4.1-lang-2.54.1-4.1.noarch.html ／ https://deepwiki.com/xjordanx/speedrun/8.2-build-time-fixes-and-webkitgtk
- Ladybird（BSD-2-Clause、pre-alpha、ビルド要件 8GB+ RAM / C++23 / Qt6）: https://computingforgeeks.com/install-ladybird-browser/ ／ https://www.ossalternatives.org/ladybird
- Links 2（2.30 / 2024-07-27、GPL-2.0-or-later、HTML 4.0 部分対応、現行版 JS なし、fb グラフィックモード）: https://en.wikipedia.org/wiki/Links_%28web_browser%29
- w3m（0.5.6、MIT、テキスト専用、新フォークで保守継続）: https://en.wikipedia.org/wiki/W3m ／ Lynx（2.9.2、GPLv2、テキスト専用）: http://archive.euph.site/content/wikipedia-en/Lynx_(web_browser)
- 参照調査（Must-not-omit の正本）: `~/workspace/research_notes/reference-desktop-apps-features-20261119-1043/report.md` §8
- 監査レポート: `~/workspace/goals/brainux-buildbrain-integration/hidden_files/audit-2026-11-20.md` §2.2 / §5
