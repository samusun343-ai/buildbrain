# REPORT-wince-m1 — WinCE 互換レイヤー（BrainCE Layer）M1（ローダ＋最小 coredll）＋ M0（API 分類の確定）

- 対象設計: `DESIGN-wince-layer.md`（方式は §4.3 (a) API 再実装＋上位 DLL 原本ホスト、統合は §8.2 第1段＝分離レイヤー）
- 範囲: §9.1 の M1 と、§6.2／§10-3 が M0 に割り当てた分類作業のみ。S0（原本ストレージ確定）は範囲外で、本報告の期間中 **eMMC には一切触れていない**（詳細は「eMMC への非接触」節）。
- 法的境界（設計 §0）: 再実装したのは Microsoft が公開ドキュメントで仕様を公開している WinCE 標準 API のみ。SHARP 独自 API・ドライバ・アプリの中身は再実装・複製していない。数値は QEMU 側プロジェクトの公開済みセンサス成果物（exports-census.json / callers.json / ordmap）のみから取った（新たなダンプ・解析はしていない）。
- 結論: **M1 の合格条件（設計 §9.1）4 項目を全て満たした**（証拠は下記。ホスト・ゲスト両方でテストバイナリが期待どおりのファイルを生成し、実行経路上のスタブ呼び出しは両環境とも 0 件）。途中でテストバイナリ生成ツール pe-link47.py の再配置バグを 1 件発見し、ツール側を修正した（「発見したバグ」節）。

## M0（分類の確定）

母集団をセンサスから完全再現した: NAND 上でファイル名が大文字小文字まで一致する `AppMain.exe` は 27 本、その coredll import キーは **874 件（名前形式 437／序数のみ 437）** で設計 §6.2 の数字と一致する。

- 序数のみ 437 件は ordmap で **全件解決（未解決 0 件）**。解決先の名前集合は名前形式 437 件と **完全一致**した。すなわち両形式は同一の 437 関数を別モジュールが名前／序数で import した記録であり、アプリ直接呼びの相異なる関数は **437 件**である。
- 分類（自作ルールベース分類器 `wince-layer/m0/classify_m0.py`、ルールはスクリプト冒頭に明記。生成物: `m0-classification.json` / `m0-classification.md`）:

| 機能群 | 件数 |
|---|---:|
| CRT / 文字列 | 119 |
| ウィンドウ / メッセージ | 96 |
| GDI / 描画 / フォント | 65 |
| 実行 / 同期 / モジュール | 33 |
| ファイル / マッピング | 25 |
| レジストリ | 10 |
| メモリ | 8 |
| その他（通知・電源・デバイス・時刻等） | 81 |
| 合計 | 437 |

- 設計 §6.2 の著者分類（106/78/56/32/26/10/8/120、合計 436）との差は境界ルールの違いによる（メニュー／ダイアログ／DeferWindowPos をウィンドウ群へ、矩形ヘルパとアイコンを GDI 群へ、ロケール／変換／マングルド名を CRT 群へ、CloseHandle をファイル群へ寄せた等）。レジストリ（10）とメモリ（8）は完全一致、実行（33 vs 32）とファイル（25 vs 26）は 1 件差。
- 脚注: ファイル名を大文字小文字不問にすると cekanken（`appmain.exe` 小文字）が加わり 28 本・878 件になる。追加分は CreateDCW（序数 909）と CreateFileForMappingW（序数 1167）の 2 関数のみで、分類の結論は変わらない。
- 実装順序への含意: M1 は「ファイル／マッピング」「メモリ」「CRT の最小集合」から実装し（本報告の範囲）、次段 M2 は件数の大きいウィンドウ／メッセージ（96）と GDI（65）＝ GWE が本丸になる。「その他」81 件は内訳が雑多（音声 wave 系約 30、時刻・電源・通知等）で、M2 以降に再細分化する。

## M1（ローダ＋最小 coredll）の実装

作業場所: `wince-layer/`（設計 §5 の構成）。

- **ce-loader**（`loader/ce-loader.c`、`loader/pe.h`）: PE32/ARM の読み込み、セクション配置（希望ベースへ `MAP_FIXED_NOREPLACE` で mmap、取れなければ任意アドレス＋再配置）、HIGHLOW 再配置、import の束縛、TLS コールバック、EXE は WinMain／DLL は DllMain(PROCESS_ATTACH) の呼び出し。通常 PE のみ（XIP は対象外）。遅延ロード import は未対応（警告を出して読み込みは続行）。
- **束縛方式**は設計 §5.1 の第一候補: ローダが序数→実装の表を持ち、IAT を Linux 側実装へ直接束縛する。実行時の名前解決・間接層はない。
- **coredll シム**（`shim/coredll_shim.c`、`shim/ce_types.h`、`shim/fs_bridge.{c,h}`）: 実装 **34 件**（正本リスト `shim/implemented.txt`、序数はセンサス ordmap から `tools/gen_shim_table.py` が生成）。CE の WCHAR は uint16_t で統一し、ホストの wchar_t は ABI 境界で使わない。内部シンボルは全て `ce_*` 接頭辞（libc との interposition 回避）。last error は TLS。
  - ファイル系: CreateFileW(168), ReadFile(170), WriteFile(171), SetFilePointer(173), GetFileSize(172), CloseHandle(553), FlushFileBuffers(175), DeleteFileW(165), CreateDirectoryW(160), GetFileAttributesW(166), FindFirstFileW(167), FindNextFileW(181), FindClose(180)
  - メモリ系: LocalAlloc(33), LocalFree(36), LocalReAlloc(34), VirtualAlloc(524), VirtualFree(525)
  - 時刻・エラー・出力: GetTickCount(535), Sleep(496), GetLastError(516), SetLastError(517), OutputDebugStringW(541)
  - CRT/文字列: memcpy(1044), memset(1047), memmove(1046), memcmp(1043), strlen(1068), strcpy(1066), strcmp(1065), wcslen(63), wcscpy(61), wcscat(58), wcscmp(60)
- **FS ブリッジ v0**（`shim/fs_bridge.c`）: CE 絶対パスを `--fs-root`（または環境変数 `CE_FS_ROOT`）のルート下へ写像するだけのもの。`..` 成分は拒否。第 1 成分（ボリューム名）のディレクトリが事前に存在することが前提（マウント済みボリュームのモデル）。ハンドル表は固定 128 件（ファイル／検索の 2 種）。
- **未実装 API** はスタブプール（`shim/stub_pool.S`、256 スロット）が受け、呼び出しをログ＋計数して 0（失敗）を返す。
- **テストバイナリ**（`test/cetest.c`、`test/build-test.sh`）: 既存の実績経路（clang `-target armv5te-none-eabi` ＋ pe-link47.py）で自作。既知パターン（4096 バイト、`pat[i]=(i*37+11)&0xFF`）の書込み・読み戻し照合＋シム各群の検査を行い、決定的な result.txt（12 行、末尾 `ALL=PASS`）を `\CE-TEST\result.txt` に記録する。`\CE-TEST\HOLD` が存在すると結果記録後に Sleep ループに入る（常駐メモリ採取用）。
- ビルド成果物: ce-loader 541,180 バイト（Bootlin GCC 13.3 で静的リンク、strip 後。ホスト qemu-arm とゲストの libc 版数差を避けるため M1 では静的リンクを選択）、cetest.exe 7,168 バイト（PE32 CE ARM、SizeOfImage 0x6000）。
- cetest が import するのは実装 34 件のうち **27 件**で、その全てが束縛・実行確認できた。残り 7 件（LocalReAlloc, memmove, strcmp, strcpy, strlen, wcscat, wcscmp）は実装済みだが本テストでは未実行（未検証として明記する）。

## 発見したバグ: pe-link47.py の .reloc 詰め物エントリ（ツール側を修正）

ホストテストで「cetest が全チェック PASS し result.txt も生成されるのに、その後ローダが segfault する」現象を追跡し、真因がテストバイナリ生成ツールのバグであると特定した。

- 症状の切り分け（gdb 追跡）: 落点 PC はイメージ内の DLL 名文字列 "COREDLL.dll" のアドレス。WinMain の最初の命令ワード（push）が、希望ベースとの差分 delta を加算された値に化けていた（0xe92d4ff0 + 0x407f1000 = 0x29ac5ff0）。条件付き命令として解釈され push が実行されなかったため、フレームが壊れ、復帰時に文字列アドレスへ跳んでいた。
- 真因: pe-link47.py が .reloc を組む際、1 ページのサイト数が奇数だと「型 0（ABSOLUTE、無操作）の詰め物」のつもりでオフセット 0 のエントリを足すが、パック時に全エントリへ型 3（HIGHLOW）を書き込む実装になっており、**ページ先頭（＝.text 先頭ワード）への実再配置**になっていた。希望ベースにそのまま載る場合は delta=0 で無害なため、これまで表面化しなかった。
- 修正: `~/workspace/brain-tools-git/wince/braincon/pe-link47.py`（作業ツリーで修正済み、**QEMU 側ラインの repo のため未コミット**。差分は詰め物を型 0 でパックする 4 行の変更）。修正後、cetest.exe のサイトは HIGHLOW 61 件＋型 0 の詰め物 1 件となり、ローダ側は「61 applied, 1 skipped」と正しく処理する。
- 申し送り: このバグは pe-link47 で生成した全 PE に潜在する（サイト数が奇数のページがある場合）。希望ベース以外に載せるローダ（本レイヤーを含む）では先頭ワード破壊になるため、QEMU 側ラインでの同修正の取り込み（コミット）を推奨する。

## 合格条件（設計 §9.1）ごとの証拠

### 1. テストバイナリが両環境で期待どおりのファイルを生成する — 合格

- **(i) 開発ホスト（qemu-arm ユーザモード）**: `tools/run-host.sh` が **HOST-TEST PASS**。終了コード 0、result.txt は期待内容とバイト一致、pattern.bin は決定パターンと一致。ログ: `image-work/int17/guest-ce/host-run.log`（再現手順は run-host.sh そのもの）。
- **(ii) Brainux ゲスト**: sd-v15 系にレイヤー一式を載せた新イメージ **sd-v15-ce1.img**（md5 `a0fa1dc66fedbd23d75120dbd5ab9d7f`、root PARTUUID `c332a1e5-02`、rootfs は rootfs-v15 のコピー＋ `/opt/braince/`）で **GUEST-TEST PASS**。ゲスト内で生成された result.txt の md5 `e982cd05677bef39b992fd08bfdee4d0`、pattern.bin の md5 `efc404fa609a799c7801273de6d65e84` が、ホスト側の期待値（＝ホスト実行の生成物と同一）と一致。ゲストの `/proc/cmdline` は `root=PARTUUID=c332a1e5-02` で、ce1 イメージから起動したことを確認。証拠: `image-work/int17/guest-ce/results-ce1.txt`、`run-ce1.out`、`serial-ce1.log`、組立は `assemble_image_ce1.sh`（ログ `int17/work/assemble-ce1.log`）。
- 両環境ともイメージは希望ベース 0x10000 に載らずフォールバック配置（ホスト 0x40801000、ゲスト 0xb6f35000）となり、再配置経路が両方で実地に検証された。

### 2. 実行経路上の未実装スタブ呼び出し 0 件 — 合格

- ホスト・ゲストとも import 27 件は全て実装へ束縛（`imports bound: 27 total, 0 to stubs`）、実行後ログは `stub calls total=0 distinct=0`。

### 3. eMMC に触れていないこと — 下記の構成で担保（「eMMC への非接触」節）

### 4. 資源実測（設計 §5.5 予算との対比） — 合格

ゲストネイティブ実測（QEMU システムモード上の Brainux、シリアルコンソールから採取）:

| 項目 | 実測 | 予算（§5.5） |
|---|---:|---:|
| ローダ＋シム常駐（HOLD 状態、イメージ読み込み済み）VmRSS | **608 kB** | シム常駐「実メモリ数 MB 級」 |
| 同 PSS（smaps_rollup） | **608 kB** | — |
| ローダ終了時 VmRSS / VmHWM（run1） | 608 kB / 608 kB | — |
| アプリ毎の固定費（≒ ローダプロセス全体） | **608 kB** | ≤ 4 MB |
| ゲスト MemAvailable（実行前 → 全実行後） | 83,320 kB → 82,608 kB（差 712 kB、ページキャッシュ含む） | 128 MB 予算内 |
| ce-loader バイナリ（strip 後、静的） | 541,180 バイト | — |
| cetest.exe イメージ（SizeOfImage） | 24 kB | — |

- 注: ホスト qemu-arm（ユーザモード）でローダが読む `/proc/self/status` は qemu プロセス自身を指すため、ホスト側の VmRSS（5.7〜7.0 MB）は qemu 本体の分を含む参考値であり、レイヤーのコストとしてはゲストネイティブの 608 kB を正とする。
- 注: M1 のシムはローダへ静的リンクされており、§5.5 の「共有ライブラリとして一度だけ載る」形態ではない。608 kB はプロセス 1 本分の実測で、複数アプリ同時実行時の共有効果は未測定（形態の移行は未解消項目）。

## eMMC への非接触（構成の明記）

- 復旧済みベースライン **emmc-int9.img はゲストに一切アタッチしていない**。ドライバが実行前後に md5 を取り、`612e8a0307436b59852bb39ede358fa8` で **不変**を確認した。
- pw-aj2 モデルはブートファーム自体が index 0 のイメージ内にあり、index 0 を空にすると起動できないことを実測した（QEMU が "no eMMC image: use -drive if=sd,index=0,file=emmc.img to boot the real firmware" と警告し、シリアル出力が一切出ない）。このため index 0 には、int16 ハーネスがゲストに晒す前提で用意している **作業コピー emmc-int9-wince.img を `snapshot=on` で**アタッチした（書込みは使い捨てオーバーレイに行き、実ファイルへは届かない）。同ファイルも実行前後で md5 `a17320d94cb1e4eebe8c02149a7c9479` のまま **不変**を確認した。
- ゲストのルートは SD（index 1）の第 2 パーティションであり、Brainux 側から eMMC 領域へ書き込む経路はこの構成に存在しない。通常の WinCE 起動が壊れない条件にも抵触しない。

## コピー／オーバーヘッド削減

- 束縛はロード時 1 回だけ: IAT に実装アドレスを直接書くため、API 呼び出しごとの名前解決・PLT・間接層は実行時に存在しない。命令翻訳もない（ARM ネイティブ実行）。
- FS ブリッジはパス文字列の写像のみを行い、データ経路には入らない（ReadFile/WriteFile はホスト fd への直接 read/write で、バッファの余分なコピーはない）。
- シムの序数表は生成コードによる静的配列で、ロード時の参照は表引きのみ。ハンドル表も固定長配列の線形走査で、件数が小さい M1 の規模では割り切った（動的構造を持たない分、断片化もない）。
- スタブは未実装 import 専用のプール（1 スロット 8 バイト）で、実装済み経路のコストにはならない。

## キャッシュ／メモリ管理

- M1 の範囲にキャッシュは存在しない（フォントグリフ・アイコン等のキャッシュは GWE＝M2 以降の設計対象。その際は本プロジェクト共通原則どおりバイト予算＋追い出し方針＋プロセス間共有を必須とする）。
- ローダ＋シムの常駐は実測 608 kB（上表）。内訳はローダ本体の触れたページ＋イメージ 24 kB＋ゲストのヒープ確保分で、§5.5 の固定費予算（≤4 MB）を大きく下回る。
- ハンドル表（128 件）と VirtualAlloc の予約表は固定長で、アプリの要求で際限なく増える構造はない。LocalAlloc 系は malloc 系に写像し、CE 側の確保はそのままホストのページ管理に委ねている（独自アロケータは持たない＝断片化対策を二重に持たない）。
- イメージ配置は mmap ベースで、ファイルのバイト列を二重に保持しない（ヘッダの自前コピーは検証用に保持する分のみ）。

## 実機の現実への対応

- **突然のリセット／電源断**: レイヤー自体は永続状態を持たない（設定ファイル・キャッシュ・索引を持たない）ため、壊れるものがない。FS ブリッジ越しのファイル書込みは SD 上の通常ファイルのセマンティクスに従い、原子性が必要な書換えはアプリ側の作法（tmp→rename）に委ねられる。キャッシュを使い捨て前提にする原則とも矛盾しない（M1 はキャッシュ自体がない）。
- **オフライン既定・時刻不信**: 日数ベースの有効期限の類は一切実装していない。GetTickCount は単調増加の経過時間であり、実時刻の正しさに依存しない。
- **本拠は microSD**: ゲスト配置は rootfs（SD）上の `/opt/braince/` で、FS ルートも SD 上のディレクトリ。eMMC へは上節の構成どおり触れていない。
- **WinCE 互換ストレージの保護**: WinCE 側の通常起動に必要な eMMC 内容には、ベースライン未アタッチ＋作業コピーの snapshot 化により、読み書きいずれの到達経路も作っていない。
- **実機未検証の明記**: 本報告の実測は全て QEMU（ホストのユーザモード＋pw-aj2 システムモード）上のもので、実機 Brain での動作・配置・メモリ量は未検証。

## 未解消項目

1. **XIP 形式は未対応**（設計どおり M1 の対象外）。
2. **GWE（ウィンドウ／GDI）は M2**: M0 で 96＋65 件を確認済み。実装順序の根拠として本報告の分類表を使う。
3. **S0 は別件**: 原本 DLL ホストのストレージ確定（eMMC の WinCE 領域を使う場合の保全条件含む）は本タスクの範囲外で未着手。
4. **シムの形態**: M1 は静的リンク単一バイナリ。§5.5 の共有ライブラリ形態（プロセスを増やしても増えない常駐部分）への移行と、その状態での再実測が残る。
5. **シムの割り切り**（いずれも CE 標準 API の範囲内の後続実装項目）: ディレクトリそのものを開くハンドルは未対応、VirtualAlloc の MEM_DECOMMIT は no-op で成功を返す、LocalAlloc の LMEM_MOVEABLE は FIXED として扱う、遅延ロード import は警告のみで束縛しない。
6. **実装済みだが未実行の 7 件**（LocalReAlloc, memmove, strcmp, strcpy, strlen, wcscat, wcscmp）: テストバイナリの拡張で実行確認する。
7. **希望ベース配置**: 両環境とも 0x10000 は占有済みでフォールバック配置になった。再配置での正常動作は確認済みだが、実機 Brainux 上での配置は未検証。
8. **pe-link47.py の修正が未コミット**: brain-tools-git は QEMU 側ラインの repo のため、本ラインではコミットしていない（作業ツリーに修正は入っている）。取り込みは QEMU 側ラインへの申し送り。
9. **M0 の「その他」81 件**は未細分化（音声・電源・通知・時刻等）。M2 の計画時に分ける。
10. **実機検証**は全項目未実施（QEMU ゲストまで）。

## 成果物の所在

- 実装一式: `wince-layer/`（loader/、shim/、test/、tools/、m0/、Makefile）
- M0 生成物: `wince-layer/m0/m0-classification.md` / `.json`、分類器 `classify_m0.py`
- ホスト受け入れ: `wince-layer/tools/run-host.sh`、ログ `image-work/int17/guest-ce/host-run.log`
- ゲスト受け入れ: `image-work/int17/guest-ce/`（`launch-ce1.py`、`run-ce1.py`、`results-ce1.txt`、`run-ce1.out`、`serial-ce1.log`）、イメージ `image-work/sd-v15-ce1.img`、組立 `image-work/assemble_image_ce1.sh`
