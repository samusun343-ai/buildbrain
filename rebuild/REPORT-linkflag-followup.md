# リンクフラグ追随記録 — HarfBuzz / FreeType（M24 取り込み対応）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


日付: 2026-11-21

## 背景

ライブラリ側でテキスト整形を HarfBuzz + FreeType に切り替えた
（REPORT-toolkit-m24.md）。`libbrainui2.a` は未解決の `hb_*` 15件・
`FT_*` 7件を参照するようになったため、利用側のリンクに
`-lharfbuzz -lfreetype` が必要になった。本作業はリンクフラグのみの
追随で、ソース変更は一切なし。

## 方式

ルート Makefile（M24 で導入済み）の様式に統一した:

```make
TEXT_LIBS := $(shell pkg-config --libs harfbuzz freetype2 2>/dev/null)
ifeq ($(strip $(TEXT_LIBS)),)
TEXT_LIBS = -lharfbuzz -lfreetype
endif
```

pkg-config が使える箇所では harfbuzz / freetype2 の .pc 経由を優先し、
無い環境では `-lharfbuzz -lfreetype` にフォールバックする。
このホストでは pkg-config が `-lharfbuzz -lfreetype` を返した。

## 対象箇所の一覧

### shell/Makefile
- `TKLIB` 定義の直後に `TEXT_LIBS` 定義を追加。
- レシピ行 15 件で `../libbrainui2.a` の直後に `$(TEXT_LIBS)` を追加:
  test_shell / test_iconcache / test_wm / test_assist /
  test_greeter_font / session_client / im_client / test_session /
  test_mascot / test_notify / bench_mascot / wm_sample_raw /
  wm_sample_tk / braingreeter2 / braindesk2。
- libbrainui2 をリンクしない行（test_desktop / test_backend /
  test_wifi / test_greeter_auth 等）は変更なし。

### apps/Makefile
- `IMELIB` 定義の直後に `TEXT_LIBS` 定義を追加。
- レシピ行 22 件で `$(TKLIB)` の直後に `$(TEXT_LIBS)` を追加:
  bd-files / test_files_ui / bd-notes / test_notes / bd-tasks /
  test_tasks_ui / bd-clock / test_clock_ui / bd-alarmd / bd-calc /
  test_calc_ui / bd-recorder / test_recorder_ui / bd-player /
  test_player_ui / bd-pkg / test_pkg_ui / bd-settings / test_settings /
  test_settings_ui / bd-admin / test_admin_ui。
- `$(TKLIB)` をリンクしないモデル単体テスト（test_files 等）は変更なし。

### osk/Makefile
- `TEXT_LIBS` 定義を追加し、`LIBS` に取り込んだ
  （`LIBS = $(ROOT)/libbrainui2.a $(TEXT_LIBS) $(ROOT)/libbrainime/libbrainime.a -lm`）。
  影響先は test_osk / bench_osk の両レシピ。

### browser-engine/netsurf-all-3.11/netsurf/frontends/brain/Makefile
- libbrainui2.a を直接リンクしている（`LDFLAGS += $(REBUILD_ROOT)/libbrainui2.a`）。
- freetype2 は既に pkg-config 経由でリンク済みだったため、不足していた
  harfbuzz のみを同ファイルの既存様式で追加:
  `LDFLAGS += $(shell $(PKG_CONFIG) --libs harfbuzz)`
  （freetype2 の行の直後）。

## 検証結果（2026-11-21、全てクリーンビルドから実施）

- shell: `make check` 全 12 スイート green
  （test_desktop 182 / test_shell 126 / test_iconcache 35 / test_wm 94 /
  test_backend 25 / test_assist 24 / test_greeter_auth 13 /
  test_greeter_font 6 / test_wifi 48 / test_session 163 /
  test_mascot 95 checks / test_notify 46、いずれも 0 failed）
- apps: `make check` 全 20 スイート green
  （test_files 135 / test_files_ui 48 / test_tasks 89 / test_tasks_ui 32 /
  test_notes 77 / test_clock 71 / test_clock_ui 43 / test_calc 136 /
  test_calc_ui 92 / test_recorder 61 / test_recorder_ui 15 /
  test_player 84 / test_player_ui 40 / test_pkg 45 / test_pkg_ui 13 /
  test_settings 87 / test_settings_ui 36 / test_admin 25 /
  test_admin_ui 13 / test_alarmd 93、いずれも 0 FAIL / 0 failed）
- osk: `make check` green（test_osk 319 passed, 0 failed）
- ブラウザ前端: REPORT-brain-frontend.md §7 の再現手順
  （前端サブメイク直接実行、`make TARGET=brain -j2`、PKG_CONFIG_PATH に
  inst-brain と .sysroot、PATH に inst-brain/bin、TMPDIR は
  browser-engine/.tmp-build）でフルビルド完走し、`nsbrain` のリンク成功。
  `ldd` で libharfbuzz.so.0 / libfreetype.so.6 の解決を確認。
  gui.c の urldb_save_cookies 等の implicit-declaration 警告は従来からの
  既存警告で、本変更とは無関係。

## 備考

- repo へ commit / push はしていない。
- 変更はリンクフラグ（と、そのための `TEXT_LIBS` 変数定義）のみ。
  ライブラリ本体（include/・src/）は編集していない。
