# DESIGN — frontends/brain（ツールキット v2 上のブラウザ UI）

- 日付: 2026-11-20
- 位置づけ: ブラウザ実装フェーズ M0 の成果物 3（UI 接続方針の設計）。
  エンジン選定と魔改造計画は `browser-engine-selection.md`（以下「選定文書」）が正。
- 状態: **設計のみ**。本フェーズで実 UI として動くのは、既存 framebuffer
  フロントエンドを libnsfb「brain」サーフェス上で動かしたもの（M0 の検証車両）。
  本書の frontends/brain は次フェーズ以降の実装対象であり、動いたとは言わない。

---

## 1. 構成

```
NetSurf コア (desktop/ + content/)          ← GUI テーブル経由でのみ前端と接続
        │  gui_window_table / plotters / misc / download / clipboard / search
frontends/brain                              ← 本書の対象。自前コード 4,000–6,000 行見込み
        │  ツールキット v2 (libbrainui2) のウィジェットで UI を構成
        ├─ 単独起動時: libnsfb "brain" サーフェス → /dev/fb0 直接 (M0 実装済み)
        └─ braindesk2 配下: シェル WM クライアント (Unix ソケット + memfd 共有メモリ、
           ダメージ矩形 COMMIT) として描画
```

### 1.1 表示経路が 2 つある理由と、描画先の統一

- ブラウザは「シェルの 1 アプリ」としても「単独フルスクリーン」としても
  起動できなければならない（グリーター直後やシェル障害時の非常用を含む）。
- どちらの経路でも、コアへの描画は libnsfb のプロッタで 1 枚のピクセル
  バッファへ行う点は共通にする。差分は「バッファの所有者と転送方法」だけ:
  - 単独時: brain サーフェスが fbdev を直接 mmap（M0 実装済み。ゼロコピー）。
  - シェル配下: シェルが memfd で渡す共有バッファを brain サーフェスが
    mmap して描画先にし、`update()` のダメージボックスをシェルの
    クライアントソケットへ COMMIT として送る。**brain サーフェスへの
    追加パラメータ `target=shm` として実装する**（次フェーズ）。
    libnsfb の ram サーフェスは自前 realloc バッファしか持てないため流用しない。
- ツールキット v2 のウィジェット描画（ツールバー等）とページ描画
  （libnsfb プロッタ）は、同じバッファに対する 2 系統の描き手になる。
  ページ領域はツールキット側では「中身を libnsfb に委ねるカスタム
  ウィジェット」として扱い、ツールキットはその矩形を自分では塗らない
  （二重描画と余分なコピーを避ける。ダメージモデルは tk 側の
  tk_fb_flush_damage と矩形単位で一致させる）。

## 2. GUI テーブル → ツールキット v2 の対応

| コア側の要求 | frontends/brain の実装 |
|---|---|
| ウィンドウ生成/破棄 (gui_window_table.create/destroy) | tk ウィンドウ（シェル配下ではシェルクライアント窓、単独時はルート） |
| 再描画要求 (invalidate / redraw) | ページカスタムウィジェットの invalidate。ダメージ矩形をそのまま libnsfb claim/update 区間へ |
| スクロール (set_scroll / get_scroll) | M5 のタッチパンと一体。スクロールバーは tk 部品、ドラッグパンは前端ジェスチャ層 |
| タイトル/ステータス/URL 表示 | ツールバーのラベルとアドレス欄へ反映（セッタ後は明示 invalidate が必須というツールキット側の作法に従う） |
| アドレス入力 | tk TextField + textcore。確定で browser_window_navigate。補完は urldb の補完 API を候補リストへ |
| 戻る/進む/再読込/停止 | tk ボタン。長押しで local_history の core_window 実装を開く（選定文書 §7.2-3） |
| タブ | 前端新規。タブ = browser_window 1 個 + tk タブストリップ。閉じたタブのスタック（URL＋スクロール位置）は前端の状態ファイルに原子的に保持（選定文書 §7.2-1） |
| ブックマーク (hotlist) / 履歴 (global_history) | コア実装をそのまま使い、表示は core_window パターンの tk 実装（ListView/Tree） |
| ダウンロード (gui download table) | コア desktop/download.c + 一覧ウィンドウ。保存先は brainreg `browser.download_dir`、完了後は bd-files で開ける導線 |
| ページ内検索 | desktop/search.c + textsearch.c を tk の検索バーへ（件数表示つき） |
| クリップボード | tk のクリップボード抽象へ接続。パスワード欄の扱いはグリーターで確定した方針（マスク欄にプロバイダを付けない）を踏襲 |
| OSK / IME | §3 |
| スケジュール (misc table) | シェル配下・単独時とも前端のメインループ（tk のイベントループ）に統合。libnsfb input のタイムアウト駆動とは二重にしない |

## 3. IME / OSK（選定文書 M4 の前端側）

- コアのキー入力 API には composition の概念が無いため、選定文書 M4 の
  通り **gui_window テーブルへ任意 IM フックを追加**する（既定 NULL、
  他フロント無影響）:
  - 確定文字列の注入（textinput / textarea が受け取る）
  - preedit の描画は前端オーバーレイ（OSK v2 の composition 表示と
    同じ描画経路。インライン preedit は次々フェーズの判断事項）
- OSK は fbtk 付属 OSK を使わず **OSK v2**（QWERTY 主配置、libbrainime
  接続済み）をそのまま呼ぶ。物理キーボードは tk_kbd のフェイスバリュー
  変換を通し、入力経路（textcore / IME への合流）は OSK と統合するが
  両者は混ぜない（ユーザー確定方針）。
- ブラウザ固有の入力欄（アドレス欄、フォーム）でも、DE の他アプリと
  同じ IME・同じ辞書・同じ OSK になることが要件。

## 4. タッチ（選定文書 M5 の前端側）

- 1 本指前提: ドラッグパン＋慣性（減衰と再描画上限を持ち、再描画は
  ダメージ領域のみ）、ダブルタップで フィット⇔100% トグル、長押しで
  コンテキストメニュー（リンクを開く/画像保存/コピー）。
- タップのヒット拡張（最小 32px 四方）は前端の座標変換層で持つ。
  コアのヒット判定には手を入れない。
- パラメータ（減衰・上限）は QEMU ゲスト/実機で測って決める。推測で
  固定しない。

## 5. 設定（brainreg 橋渡し）

- 起動時に brainreg の `browser.*` を読み nsoption へ流し込む。終了時は
  nsoption の Choices ファイルへ書かず brainreg へ書き戻す（原子的
  書換えは brainreg 側の既存機構を使う）。ブラウザ単独の設定ファイルは
  持たない。
- キー（選定文書 §7.3 を継承）: `browser.budget_mb`（→ `brain_budget_mb`）、
  `browser.disc_cache_mb` / `browser.disc_cache_enabled`、
  `browser.javascript_enabled`、`browser.images_enabled`、
  `browser.user_agent`、`browser.search_provider`、`browser.homepage`、
  `browser.download_dir`、`browser.font_size`、`browser.default_scale`。
- 設定 UI は前端内ページとして作り、DE の設定アプリからも同じキーを
  編集できる（二重管理にしない）。

## 6. 状態の永続化と実機の現実への対応

- 配置は microSD 上の Brainux アプリデータ領域のみ。eMMC には触らない。
- セッション（開いているタブ、閉じたタブのスタック）、履歴、
  ブックマークは全て「tmp へ書く → fsync → rename → ディレクトリ
  fsync」の原子的書換え。突然のリセットで書込み途中のファイルが
  本体を壊さないこと。キャッシュ類は失われても機能が壊れない前提で
  使い捨てにする。
- 期限・追い出しは日数ベースを使わない: ディスクキャッシュは容量
  （`disc_cache_size`、既定 32MB）＋LRU（M3 でコア側を確認済み:
  バッキングストアの追い出しは容量駆動で、`disc_cache_age` は
  コアから参照されない）。履歴は件数上限で追い出す。
- オフライン既定: ホームページは同梱のローカル開始ページ（検索＋
  ブックマーク＋履歴への入口）。ネットワーク失敗時はオフライン
  表示＋再試行導線に落とす（白画面で固まらせない）。

## 7. メモリ予算との接続

- 起動時に `browser.budget_mb`（PW-SH6 の設計値 56MB）を
  `brain_budget_mb` へ渡し、llcache / 画像 / JS ヒープの上限は
  コア側の導出（M1、本フェーズで実装済み）に従う。
- タブのメモリ回収（選定文書 M10）: 背景タブのレンダリング済み
  内容は最大 2 タブまで保持し、残りはディスクストアから復元可能な
  状態にして解放する。戻る/進むの保持も同様に上限を持つ。
  上限数は実測で決める（本書では固定しない）。
- 予算超過時の縮退順序は M1 の方針（画像キャッシュ → llcache →
  背景タブ内容）に従う。PSI 連動の強制回収は M1 後半として残る。

## 8. ファイル構成（見込み）

```
netsurf/frontends/brain/
  Makefile, Makefile.defaults, options.h   (fb と同形式)
  gui.c            メインループ、ウィンドウ管理、GUI テーブル本体
  toolbar.c        ツールバー/アドレス欄/ステータス
  tabs.c           タブ管理、閉じたタブのスタック、セッション保存
  pageview.c       ページ描画カスタムウィジェット、タッチジェスチャ
  corewindows.c    履歴/ブックマーク/ダウンロードの core_window 実装
  search.c         ページ内検索バー
  brainreg.c       brainreg 橋渡し
  ime_bridge.c     IM フックと OSK v2 接続
  res/             開始ページ、アイコン、adblock フィルタ CSS
```

## 9. フェーズ計画

| 段 | 内容 | 合格の物差し |
|---|---|---|
| F1 | 骨格: 1 窓、ツールバー（戻る/進む/再読込/アドレス）、brain サーフェス単独起動 | 実ページ表示・リンク遷移・履歴が tk UI で動く |
| F2 | タブ＋セッション保存/復元、閉じたタブを開き直す | リセット相当の強制終了後にセッションが壊れていない |
| F3 | core_window 群（履歴/ブックマーク/ダウンロード/検索） | 選定文書 §7.2 の 1–7 が前端で完結 |
| F4 | シェル配下モード（target=shm）＋ IME/OSK 接続（M4） | braindesk2 の窓として表示され、日本語入力が他アプリと同一経路 |
| F5 | タッチ仕上げ（M5）、リーダー（M9）、プライベート（M11） | 各選定文書項目の受け入れ条件 |

## 10. 本フェーズ（M0）との関係

- M0 の brain サーフェスは本書の「単独起動時」の描画・入力基盤として
  そのまま使う。fb フロント＋brain サーフェスの組み合わせは検証車両で
  あり、製品 UI ではない（fbtk の見た目・操作系は採用しない）。
- ただし M0 で実証した「ページ表示・リンク遷移・履歴」はコア＋
  サーフェスの性質であり、frontends/brain でも同じコア部品
  （browser_window、local_history、hotlist、download、search）を
  再利用するため、F1 の到達可能性の裏付けにはなる。
