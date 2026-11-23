# REPORT — ブラウザ UI の両モード対応（Android / WinUI）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


- 日付: 2026-11-21
- 対象: `browser-engine/netsurf-all-3.11/netsurf/frontends/brain/`（toolbar.c / gui.c / gui.h / brainreg.c / Makefile / icons）。repo へ commit / push なし。
- 正本キー: brainreg `ui/mode`（"android" 既定 / "winui"）＋ `ui/dark`（shell/include/thememode.h が正本定義）。
- アイコン方針: 確立したベクターセットから AA ラスタライズしたものだけを使う（`assets/icons/`、MANIFEST.md）。コード描画・文字流用は不可。

## 段階1: 現状確認（実コードで確認済み）

### 1.1 テーマ決定（変更前）
- `gui.c:main()` が `tk_theme_set_default(&tk_theme_v2);` と**無条件に Android 形ライト固定**。`ui/mode` / `ui/dark` は一切読んでいなかった（ブラウザだけがモード追随の輪の外）。
- ツールバー等の色・形は全て `tk_theme_default()` 経由（toolbar.c の `th->surface` / `th->surface_alt` / `th->container`、ボタン角丸は tk_widgets 側が `theme->radius_button`、テキストフィールドは `theme->field_filled` を参照）。→ **テーマを差し替えるだけで色・角丸・寸法の大半は追随する**構造だった。追随していなかったのは (a) テーマ選択そのもの、(b) ツールバーのボタン構成、(c) アイコンセット。

### 1.2 アイコン決定（変更前）
- `icons.h`: `test-harness/make_icons.py` が Material Icons（filled, 24px grid）を cairosvg で 4倍レンダリング→28px LANCZOS 縮小し、色 #49454F（= tk_theme_v2 の text_dim と同値）に着色して `uint32_t[784]` 配列化した生成物。収録は back / forward / reload / stop / home / menu / plus / star の8点。home と star は生成済みだが**ツールバーでは未使用**だった（home はメニュー内項目、star は未使用）。
- toolbar.c の使用箇所: back / forward / reload（+読込中は stop に差替）/ plus（新規タブ）/ menu の5点のみ。切替機構なし。

### 1.3 ツールバー構成（変更前）
- 56px（48px ターゲット＋上下4px padding、間隔8px）: [戻る][進む][再読込] [URLフィールド flex] [＋][メニュー]。
- ※タスク文の「Android = 戻る/進む/再読込/ホーム/アドレス/メニュー」は概略で、実物はホームボタンではなく「＋（新規タブ）」があり、ホームはメニュー内。Android モードは現行形を**そのまま維持**する（回帰防止を優先し、この差異は本レポートに明記）。

### 1.4 brainreg API 確認
- 同梱 `brainreg.c` は shell の `reg.h` API を使用（Makefile が `shell/src/reg.c` を同梱コンパイル）。`reg_get(section,key)` / `reg_get_int` / `reg_get_bool` が使え、現状は section "browser" のみ参照。
- `ui/mode` は **同じ API で `reg_get("ui","mode")` として読める**（env 上書き `BRAINREG_UI_MODE` も reg.c の規約で効く）。正本の読み手は shell の `thememode.c:bd_theme_from_reg()`（mode=winui → tk_theme_winui[_dark]、他 → tk_theme_v2[_dark]、dark は `reg_get_bool("ui","dark",false)`）。apps は `apps/common/bdappmode.h` 経由でこの1実装を共有し、WinUI 判定は `theme->style == 2` で行う規約。
- ブラウザ前端の Makefile は shell/src から wmclient.c / reg.c / wm.c を同梱コンパイルする流儀。→ **thememode.c も同方式で同梱すれば、正本の読み手を複製せず共有できる**。

## 段階2: 実装（完了）

変更は全て `frontends/brain/` 内＋生成スクリプト。エンジン本体（NetSurf コア）・shell/・ライブラリ本体には触れていない（shell/src/thememode.c は**読み手として同梱コンパイル**しただけで shell 側ファイルは無編集）。

### 2.1 モード読取り
- `frontends/brain/Makefile`: 同梱コンパイルする shell ソースに `shell/src/thememode.c` を追加（reg.c / wm.c と同じ流儀）。
- `gui.c:main()`: `tk_theme_set_default(&tk_theme_v2)` 固定を廃し、ウィジェット構築前に `bd_theme_from_reg()`（正本の読み手: `reg_get("ui","mode")` ＋ `reg_get_bool("ui","dark")`）でテーマを選択。`brain_fe.winui`（theme->style==2、apps の bd_app_winui と同じ判定）/ `brain_fe.dark` を gui.h の前端状態に追加し、起動ログに `ui mode: winui (dark)` 形式で出す。
- `brainreg.c:apply()`: section "ui" が同じ reg API で読めることの実地確認として、起動時に `reg_get("ui","mode")` / `reg_get_bool("ui","dark")` を読みログに残す処理を追加（テーマ選択そのものは thememode に一元化し、複製しない）。
- 切替タイミングは正本契約どおり**次回起動時**（ライブ切替なし、シェル/apps と同じ）。

### 2.2 Fluent アイコン生成
- `test-harness/make_icons_winui.py`（新規）: make_icons.py と同じ生成方式（4倍レンダリング→28px LANCZOS 縮小→着色→uint32 配列化）。レンダラだけ cairosvg（本ホストに未導入）ではなく、アイコンパイプライン既存の `assets/icons/tools/svg2png`（librsvg+cairo、build_icons.py が全出荷アイコンに使うもの）を使用。
- 生成物 `frontends/brain/icons_winui.h`（別ヘッダ方式）: back←arrow_left / forward←arrow_right / reload←arrow_clockwise / stop←dismiss / home←home / menu←more_horizontal / plus←add / star←star（全て assets/icons/src/fluent/、MANIFEST 記載の Fluent regular）。生成時に全8点の coverage 非ゼロを確認し、プレビュー（test-harness/icons-winui-preview.png）で8グリフ全ての形状を目視確認済み（←/→/↻/×/ホーム/…/＋/☆）。
- 着色: 生成時の焼き込みは Fluent 原色 #212121。ただしツールバー側で**実テーマの text_dim に再着色**する（下記）ため、ライト/ダーク両テーマで同じ配列を使う。

### 2.3 ツールバー切替（toolbar.c）
- アイコン選択ヘルパー `icon_default/icon_for/icon_tint`: モードでセット（Material/Fluent）を選び、alpha（＝形状）を保持したまま RGB をテーマの text_dim に置換する（bdicon の tint と同じ処理。tk_button_set_icon がコピーするためスロットバッファ共有は安全）。Android ライトでは text_dim=#49454F で従来の焼き込み色と一致＝**従来ピクセル不変**。ダークでは明色になり視認性を保つ（段階3の撮影で確認）。
- 構成:
  - Android: 従来どおり [戻る][進む][再読込] [URL flex] [＋][メニュー(縦…)]、間隔8px。変更なし。
  - WinUI（Edge 式）: [戻る][進む][更新] を左に間隔4px、URL 中央 flex、右に [お気に入り star][ホーム][メニュー(横…)]。新規タブはメニュー内（Edge の＋はタブ帯側という整理。本クロームのタブ帯は2タブ時のみ表示のためツールバーには置かない）。fav/home ボタン用に gui.h へ `fav_btn` / `home_btn` を追加（Android では NULL、newtab_btn は WinUI で NULL。他ファイルはこの3ポインタを参照していないことを grep で確認済み）。
- お気に入りボタン: 現在ページのブックマークをトグル（メニューの同項目と同じ hotlist 操作）。ブックマーク済みの間は star をテーマ accent 色で表示（Fluent 確立セットに filled star が無いため、代替グリフでなく色で状態を示す）。
- 読込中の reload→stop 差替もモードのセットから選ぶ（Material close / Fluent dismiss）。
- 色・寸法の様式寄せはテーマトークン経由で効く範囲を使った: WinUI ではボタン角丸 6（Android は pill 999）、URL 欄は outline 型（Android は filled pill）、候補バー/タブ帯の surface_alt、accent 等が tk_theme_winui の値になる。ツールバー高は両モードとも 56px（48px ターゲット＋上下4px）で不変 — WinUI テーマの appbar_h は 65 だが、これはシェルのバー寸法であり、ブラウザのタッチ床（48px）と従来寸法の継続を優先した（申し送り参照）。

## 段階3: ビルド検証と実描画（完了、ホスト x86-64 実測）

### 3.1 ビルド
- 手順: REPORT-brain-frontend.md §7 の再現メモに従い、トップの build-stamp を経由せず前端サブメイクを直接実行（stamp は前提なしで再ビルドしないため）: `netsurf/` で `make TARGET=brain -j2`（PKG_CONFIG_PATH に inst-brain と .sysroot、PATH に inst-brain/bin、TMPDIR は browser-engine/.tmp-build へ — /tmp 満杯への従来対処。使用後 .tmp-build は削除済み）。
- 結果: **変更した全翻訳単位がコンパイル成功し、`nsbrain` のリンクまで完走**（thememode.c の同梱コンパイル、brainreg.c、gui.c、toolbar.c を含む）。toolbar.c / brainreg.c / thememode.c に警告なし。gui.c に urldb_save_cookies 等の implicit-declaration 警告があるが、これは本変更と無関係の既存警告（gui_quit/main の従来コード）。
- つまり「フルビルドが重い場合」の縮小検証ではなく、**前端フルビルドで検証済み**。

### 3.2 実描画（test-harness/shot_brain、実 nsbrain を WM クライアントとして撮影）
モード指定は reg の env 層 `BRAINREG_UI_MODE` / `BRAINREG_UI_DARK`（reg.c の正規の上書き経路）で与えた。証拠: `shots-brain/25-dual-android.png` / `26-dual-winui.png` / `27-dual-winui-dark.png` / `28-dual-android-dark.png`（+同名 .jpg）。PSS は各約 34MB（予算 56MB 内）。

| 条件 | ツールバーの実描画 |
|---|---|
| android（既定） | Material 太線グリフ [←][→][↻]、filled pill 型 URL 欄、[＋][縦…] — **変更前と同形** |
| winui | Fluent 細線グリフ [←][→][↻]、outline 角型 URL 欄（半径小）、右に [☆][ホーム][横…]、間隔が詰まる — Edge 式に切替 |
| winui + dark | ダーク帯に明色 Fluent グリフ（再着色が効き視認可）、URL 欄もダーク |
| android + dark | ダーク帯に明色 Material グリフ、pill URL 欄はダーク容器色 |

- 撮影で分かる範囲外の注意: 写真の WM タイトルバー（「ブラウザ」帯）は撮影ハーネス内 WM サーバ側の描画で、ブラウザ前端のテーマとは独立（サーバは自前テーマ）。前端の追随範囲はクライアント領域内であり、そちらは4条件とも正しく切り替わった。
