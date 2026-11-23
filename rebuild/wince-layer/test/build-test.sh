#!/bin/sh
# Build the M1 test binary cetest.exe (WinCE PE, coredll by ordinal).
# Path proven by brain-tools: clang -target armv5te-none-eabi +
# pe-link47.py. pe-link47 merges ../static/coredll-exports.json
# (ordinal -> name for this CE build's coredll) over its seed table;
# tools/make_coredll_exports.py generates that file from the census.
set -e
cd "$(dirname "$0")"
PE_LINK=${PE_LINK:-$HOME/workspace/brain-tools-git/wince/braincon/pe-link47.py}
CLANG=${CLANG:-clang}
CFLAGS="-target armv5te-none-eabi -marm -Os -ffreestanding -fno-builtin -fshort-wchar"
# shellcheck disable=SC2086
$CLANG $CFLAGS -c cetest.c -o cetest.o
python3 "$PE_LINK" cetest.o cetest.exe
echo "built cetest.exe"
