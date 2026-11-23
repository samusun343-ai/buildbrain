# マスコット キャラクター追加レポート（2026-11-20）

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


ユーザー要求「ほかにも色々キャラをやって。ゼノブレイドとかのはないの？」への対応。
調査は全て Web で実在を確認した（記憶で断言していない）。変換・表示は実行で裏付けが
取れたものだけを「できた」と書く。**キャラデータは全てローカル検証専用で repo 非同梱**
（パック置き場: `~/mascot-src/packs/`、Live2D 作業場: `~/l2d-work/`）。

## 結論サマリ

- **ゼノブレイド系: マスコット用 2D データ（Shimeji / Live2D ランタイム形式）の公開配布は
  確認できなかった**（詳細は §1）。実在するのは 3D モデル（XPS 等）のみで、マスコット化は
  3D→2D 焼き込みパイプライン経由が唯一の路。
- **新規に .bdm 化できたのは 10 パック**: Shimeji 7 件（ピカチュウ / レム / サンズ /
  サスケ / まどか / ほむら / 「Gura」名義だが中身は別キャラ）＋ Live2D 公式サンプル 3 件
  （Mao / Wanko / 初音ミク miku_sample_t04）。
- **公式ミクの Live2D ランタイム形式は実在した**。前回調査の結論「公式ミクは Editor
  ソースのみ」は Cubism SDK サンプルについては誤りだった（§3、SHARED.md にも訂正済み）。
- 全パックを実セッション（shot_mascot）で表示撮影済み。test_mascot 回帰ゼロ
  （58 checks, 0 failures）。
- **未解決の表示欠陥 1 件**: Mao と初音ミクのパックが現行 C ランタイムで表示すると
  崩れる（§4）。切り分けの結論は **ランタイム側の再構成バグ**（3 パックともデータ
  自体は参照デコーダで全数検証合格、Wanko は正常表示）。修正はランタイム担当ラインの
  仕事で、本タスクでは未着手。

## §1 ゼノブレイド系の調査（入手不可、理由と根拠）

2026-11-20 に以下の場所を実地で調べた。

| 場所 | 方法 | 結果 |
|---|---|---|
| shimeji.org | API 検索（Xenoblade / Pyra / Mythra / Nia / Homura / Rex / Pneuma / KOS-MOS / Fiora / Melia / Mio / Eunie / Brighid / Morag / Poppi / Shulk） | 該当なし。「Pyra」のヒットは無関係な OC、「Homura」は暁美ほむら（まどか☆マギカ）、「Rex」はヴェロキラプトル等、「Mio」は別作品キャラ、「Nia」は部分一致の誤検出のみ |
| shimejis.xyz ディレクトリ | 全カタログ（約 3MB の SSR HTML）を文字列検索 | ゼノブレイド系ゼロ。「Pyra」は Pyramid Head のみ、「Melia」は Amelia Watson |
| DeviantArt | サイト内検索 "pyra shimeji" | 結果なし |
| GameBanana | shimeji 周辺の検索 | スマブラ MOD のみで shimeji パックなし |
| Live2D（公式・ファン製） | Web 検索（Pyra Live2D model / moc3 等） | ランタイム形式モデルの配布は確認できず |

実在が確認できたゼノブレイド系の配布物は **3D モデルのみ**:

- **Xelandis（DeviantArt）** が XB1（Fiora / Melia / Sharla）、XB2（Brighid / KOS-MOS /
  Morag / Mythra / Nia / Pandoria / Pneuma / Pyra / Rex）、XB3（Eunie / Mio / Sena）の
  モデルを配布しているが、形式は **XPS / XNALara 用 3D ポージングモデル**
  （MediaFire / Google Drive フォルダ配布）。Shimeji でも Live2D でもない。
- 他は Sketchfab の Blender 3D モデル、STL プリント用、ゲーム MOD のみ。
- 公式 Live2D モデルは存在しない（任天堂 / モノリスソフトからの配布なし）。

→ ゼノブレイド系のマスコット化は、ユーザー指示の **3D→2D 変換ライン**（ホスト側で
3D モデル＋骨格アニメーションをサンプリングして各側の独自形式へ取り込む）でのみ可能。
この結論は共有ノート `~/workspace/mascot-shared/SHARED.md` にも記録済み（WinCE 側と共有）。

## §2 Shimeji 新規 7 パック（形式 v1、サイズ段 168/208/248、全て --verify 通過）

入手は全て shimeji.org。取得 ZIP の SHA-256 がサイト掲載の blobSha256 と一致することを
ダウンロード時に確認済み。撮影は実セッションで idle + action（歩行系クリップ）の 2 枚。

| キャラ | 入手元（shimeji.org アップロード） | 作者 | パック容量 | クリップ数 | 撮影 |
|---|---|---|---|---|---|
| ピカチュウ | id `cmkn5xy8z000ix6nthkl11z04`、ZIP sha256 `cb8e6dcb…a81567` | headbutt-of-love | 19,375,057 B | 89 | v2_34 / v2_35 |
| レム（Re:ゼロ） | id `cml2bn0s60u02i9lpkcc3gcg0`、ZIP sha256 `46d2f291…e97074` | Mell | 8,832,946 B | 78 | v2_36 / v2_37 |
| サンズ（Undertale） | id `cmm3lrul3c3b9lp4e4ml8xn8a`、ZIP sha256 `6fd42987…2d89` | （掲載ページ参照） | 22,013,055 B | 90 | v2_38 / v2_39 |
| うちはサスケ（NARUTO） | 「Naruto」検索ヒット id `cmkn63u9i000qx6ntifxgn4ld` の中身がサスケ、ZIP sha256 `bd84e760…7997` | tomato-x-ramen | 12,091,093 B | 89 | v2_40 / v2_41 |
| 鹿目まどか | まどか☆マギカ一式パック id `cmkudfknb00wtlb45ggmocdn0`（ZIP 32,461,633 B、sha256 `8cfa9b89…7fb2f`）の "Madoka ver.2" | digitalromance | 20,149,221 B | 89 | v2_44 / v2_45 |
| 暁美ほむら | 同一式パックの "Homura ver.2"（元キャンバス 256×192、109 フレーム） | digitalromance | 25,114,863 B | 95 | v2_46 / v2_47 |
| 「Gura」名義パック（中身は別キャラ） | id `cmlxrigf56m9alp4ebfre5u2g`、ZIP sha256 `c12a6f89…b0d2b0` | laxuyo | 17,313,457 B | 89 | v2_42 / v2_43 |

撮影（screenshots/v2/、JPG/PNG）:
![ピカチュウ idle](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_34_mascot_pikachu_idle.jpg)
![レム idle](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_36_mascot_rem_idle.jpg)
![サンズ idle](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_38_mascot_sans_idle.jpg)
![サスケ idle](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_40_mascot_sasuke_idle.jpg)
![まどか idle](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_44_mascot_madoka_idle.jpg)
![ほむら action](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_47_mascot_homura_action.jpg)

### 同定と正直な注記

- ピカチュウ / レム / サンズ / サスケ / まどか / ほむらは撮影と元スプライトの目視で
  本人であることを確認した。
- **「Gura」パックは名義と中身が一致しない**: shimeji.org では Gawr Gura 名義で配布
  されているが、実スプライトは黒髪・赤目の少年風キャラで、がうる・ぐらではない
  （撮影 v2_42 が証拠）。パックとしては正常に動作するが、「ぐらができた」とは言えない。
  同梱データの同定はできなかった。
- 「Naruto」名義のパックも同様に中身はサスケだった。**shimeji.org のタイトルは
  中身を保証しない** — 必ずスプライトで同定すること（SHARED.md に勘所として記録済み）。
- まどか☆マギカ一式パックには他に Charlotte / Kyoko（ver.1・2）/ Mami / QB /
  Sayaka（ver.1・2）と各キャラの ver.1 が同梱されており、同じ手順で変換可能（今回は
  ver.2 の 2 人のみ変換）。

### 変換の勘所（今回実地で確認、SHARED.md にも記録）

- 日本語パックは conf 名が `動作.xml` / `行動.xml` のため、ステージングで
  actions.xml / behaviors.xml へリネームコピーが必要（中身の日本語スキーマは
  コンバータが正規化対応）。
- フレーム名が `shime*.png` でないパック（Rem は `Rem1.png` 形式）は、発見用の
  `shime1.png` コピーを 1 枚追加すれば変換できる（ポーズ参照は XML の名前で解決）。

## §3 Live2D 公式サンプル 3 モデル（形式 v2、サイズ段 248/320）

### 入手経路（miku_sample_t04 の訂正を含む）

- WinCE 側 p56 の作業領域に Live2D プレイヤー repo の clone があり、その
  `resources/` に Cubism SDK サンプルモデル集が同梱されていた:
  `~/workspace/goals/sharp-brain-full-virtualization/hidden_files/p56work/data/l2drepo/resources/`
  （clone 元は GitHub の my-live2d-qt 系 repo。手元 clone の remote は
  `https://github.com/linghualive/my-live2d-qt.git`、WinCE 側の記録では
  lsk-china/my-live2d-qt — 同一プロジェクトのフォーク/改名と思われる）。
- **miku_sample_t04 は Live2D 公式 Cubism SDK サンプルの初音ミクで、ランタイム形式
  一式（.moc3 / .model3.json / physics、テクスチャ 2048、モーション Idle×3 / Tap×2 /
  Flick×2 / FlickUp×1）が実在する**。前回 Brainux 側調査の「公式ミクは Editor ソース
  のみでランタイム形式なし」という結論は、このサンプルについては誤りだった。
- 同じ resources に **Mao**（魔法使い風の少女、Idle×2 / TapBody×6、表情 8 種）と
  **Wanko**（犬、Idle×3 / TapBody×2）も同梱。3 モデルとも `~/l2d-work/models/` へ
  複写して焼き込みした（bake_live2d.py、live2d-py 1.0.0 を `~/l2d-venv` に再構築 —
  前回の venv は /tmp 掃除で消失していた）。

### 変換結果（全て --verify 合格: 最悪チャンネル偏差 8 = tol 以内）

| キャラ | 焼き込みクリップ（元フレーム数） | 保持ポーズ | パック容量 | 撮影（320px・tap） | 表示 |
|---|---|---|---|---|---|
| Mao（魔法使い風の少女、公式サンプル） | Stand / Idle×2 / Tap×2（759） | 744（ドロップ 56） | 36,727,864 B | v2_48 / v2_49（248px は v2_54 / v2_55） | **✕ グリッチ（§4）** |
| Wanko（お椀に入った白い子犬、公式サンプル） | Stand / Idle×3 / Tap×2（940） | 756（ドロップ 184） | 36,977,678 B | v2_50 / v2_51 | ○ 正常 |
| 初音ミク（miku_sample_t04、等身・KEI 版） | Stand / Idle×2 / Tap×2（445） | 436（ドロップ 9） | 28,830,436 B | v2_52 / v2_53 | **✕ グリッチ（§4）** |

- 焼き込みフレーム自体は 3 モデルとも目視で無欠陥を確認した（Mao の杖の発光エフェクト、
  ミクの等身全身、Wanko のお椀ごと全身まで綺麗にレンダリングされている）。
- パックデータは 3 件とも参照デコーダの検証に合格しており、**データとしては完成**。
  ただし Mao と初音ミクは現行 C ランタイムでの表示が崩れる（§4）。Wanko は正常表示。
- ミクの平均偏差（blue 0.85〜0.88）が他より高いのは髪の細かいグラデーションが多い
  ためで、tol 8 以内には収まっている。

## §4 Mao の表示グリッチ（未解決、切り分け完了: ランタイム側の欠陥）

Mao パックを実セッションで表示すると、帽子と杖の周囲に斜めのストリーク状アーティ
ファクトが出る（320px: v2_48 / v2_49、248px: v2_54 / v2_55。248px ではキャラの
断片が誤った位置に二重表示される症状も出た）。

![Mao 320px idle（グリッチあり）](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_48_mascot_mao_idle.jpg)

切り分けの証拠:

1. **焼き込みフレームは無欠陥**: `~/l2d-work/mao_frames/` の元フレーム（Stand 先頭、
   Tap2 中盤）を目視し、完全にクリーンな描画を確認した。
2. **パックデータは正しい**: frames_to_bdm.py の `--verify`（参照デコーダで全ポーズを
   再構成し元フレームと比較）が合格 — Mao は 248 / 320 両サイズとも最悪チャンネル
   偏差 8（= tol 8 以内）、平均偏差は blue で 0.38〜0.40。Wanko（756 ポーズ）と
   初音ミク（436 ポーズ）のパックも同様に全数検証合格済みで、崩れるミクのパックも
   データとしては正しいことが確定している。
3. **対照**: 同じランタイムで Hiyori の v2 パックは正常表示できている（前回の
   v2_31〜v2_33）。Mao パックは両サイズとも崩れる。

→ 結論: **C ランタイム（shell/src/mascot.c）の再構成が、Mao パックのデータに対して
参照デコーダと食い違う**。**本タスク（既存ツールでのキャラ追加）の範囲では未修正**で、
ランタイム側の担当ラインで直す対象。パック自体は検証合格の正しいデータなので、
ランタイム修正後に再変換なしで表示できるはず。

引き金の切り分け（追加実測）:

- **Wanko パックは正常表示**（v2_50 / v2_51、アーティファクトなし）。Wanko のアトラスは
  320 で 8,790,016 B と **Mao（4,878,336 B）より大きい**ため、「アトラスが大きいと
  崩れる」という単純な説は否定された。
- **初音ミク（miku_sample_t04）パックも崩れた**（v2_52 / v2_53）。症状は Mao より
  顕著で、画像が斜めの帯状にずれるシアー（帯ごとに横方向へ変位）パターン。
  つまり欠陥は Mao 固有ではなく、v2 パックの一部が踏む一般的な条件がある。
- 正常/異常の対応（全て同じランタイム・同じ撮影手順）: Hiyori ○ / Wanko ○ /
  Mao ✕ / 初音ミク ✕。崩れる 2 件はどちらも「全身が画面の上下いっぱいに広がり、
  細長い構造（ツインテール、杖）が斜めに走る」等身キャラで、正常な Wanko は
  中央下部に固まったコンパクトな絵柄。ただし Hiyori も等身キャラで正常なため、
  絵柄だけでは説明がつかない。ここから先の特定（どのポーズ種別・どのラン構造で
  C 側再構成が参照デコーダとずれるか）はランタイム担当ラインの仕事とする。

![HDミク idle（シアー状グリッチあり）](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_52_mascot_miku_hd_idle.jpg)
![Wanko idle（正常表示）](sandbox://workspace/buildbrain-work/rebuild/screenshots/v2/v2_50_mascot_wanko_idle.jpg)

## コピー/オーバーヘッド削減

- 変換方式は従来どおり「変換時に全てを焼き込み、実行時は適用のみ」。ランタイム側の
  変更はゼロで、新パックは既存の mmap ゼロコピー読み込み・ダメージ矩形描画のまま動く。
- パック容量の実測: Shimeji v1 は 8.8〜25.1 MB/キャラ（元 PNG の総量とクリップ数に
  比例）。Live2D v2 は 28.8〜37.0 MB/パックで、モーションの長さと変化量で決まる
  （Mao 36.7 MB は 169 フレーム級のアイドル 2 本＋195 フレームのタップ、Wanko
  37.0 MB は 940 フレームという総量、ミク 28.8 MB はドロップ僅か 9 という
  「常に髪が動く」変化量の多さが主因）。

## 実機の現実への対応

- **容量**: 全 10 パック合計は **227,425,670 B（約 217 MiB）**（Shimeji 7 件
  124,889,692 B ＋ Live2D 3 件 102,535,978 B）。Brainux の本拠である microSD 側に
  置く前提では現実的な範囲だが、「全キャラ常時同梱」は無駄が大きい。使うキャラだけを
  SD に置く運用が前提（ランタイムは mmap で開くため、置くだけでは RAM を食わない）。
  Live2D パックが 29〜37 MB と大きい主因はモーションの長さ（Wanko は 940 フレーム中
  756 保持）で、クリップを絞れば比例して小さくなる。
- **RAM**: パックは mmap 読み込みで、表示中フレームの展開分のみが実メモリに乗る
  （既存ランタイムの設計どおり）。パック容量そのものは RAM 予算に影響しない。
- **電源断・リセット**: パックは読み取り専用データで、破損しても再コピーで回復する。
  永続状態を持たないため原子的書換えの対象外。
- **ARM 実測は未実施**: 今回の撮影はホスト上の実セッション（製品コードの描画）であり、
  QEMU ゲスト / 実機での表示確認は統合フェーズの残件（従来パックと同じ扱い）。

## ライセンス・配布上の注意

- Shimeji パックはファンメイドの配布物、Live2D 3 モデルは Live2D 公式サンプルの
  規約に従うデータ。いずれも **ローカル検証専用で repo には一切同梱していない**
  （パック・元データは `~/mascot-src/` と `~/l2d-work/` の repo 外にのみ存在）。
- キャラ自体の権利は各権利者に帰属する。SD イメージへの同梱可否は別途判断が必要で、
  本レポートは「技術的に変換・表示できること」の検証に限る。

## テスト

- `shell/test_mascot`: **58 checks, 0 failures**（ベースラインと同一、回帰ゼロ）。
- 各 Shimeji パック: convert_shimeji.py の `--verify` 通過。
- 各 Live2D パック: frames_to_bdm.py の `--verify` 結果は §3 に記載。
