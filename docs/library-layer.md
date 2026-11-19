# ライブラリ層の棚卸し (run 5 イメージ実測)

## libLLVM 112MB の扱い

最大のディスク占有は libLLVM.so.19.1 (112MB、Mesa の llvmpipe 用)。
依存の実測:

- libgallium (27MB) が libLLVM を NEEDED で直接リンクしている
- libGLX_indirect / libEGL (mesa) が libgallium をリンク
- つまり LLVM だけを抜くことはできない。抜くなら gallium ごと
  になり、GTK3 等が探査する GL スタック (libglx-mesa0 /
  libegl-mesa0) が壊れる

fbdev 運用では llvmpipe が実行時に使われる場面はほぼ無い
(2D は shadowfb、GL アプリはイメージ内に実質無い) が、
「消す」には次のどちらかが必要:

1. **LLVM 無効のカスタム Mesa ビルド** (softpipe に置換)。
   -112MB の本命だが、Mesa 25 の armel クロスビルド+パッケージ化
   をイメージビルドに組み込む別プロジェクト規模の作業。
2. **現状維持**。RAM への影響はゼロ (使われなければページイン
   されない) で、コストは SD の 112MB のみ。

当面は 2 を採り、1 は独立タスクとして切り分ける。RAM が逼迫する
128MB 機で優先すべきは常駐側 (サービス層とカーネル層) で、
ディスク 112MB は SD 容量の問題に留まる、という判断。

## その他の大きな占有 (参考)

- fonts-noto-cjk 89MB: braindesk は mmap 化で RAM 影響を解消済み。
  ディスク削減はサブセット化フォントの生成が前提 (別タスク)。
- gcc/g++ 一式 約110MB: Brainux の利用者はオンデバイス開発を
  する層を含むため、製品判断として残置 (開発用イメージと
  通常イメージの分離は将来の選択肢)。
- IM の二重化 (fcitx + uim、データだけで約60MB): 統廃合の
  対象。どちらに寄せるかは実測と比較で決める (未着手)。
