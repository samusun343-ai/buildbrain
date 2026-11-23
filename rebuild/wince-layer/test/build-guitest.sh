#!/bin/sh
# Build the M2 GUI test binary ceguitest.exe (WinCE PE, coredll by
# ordinal). Same path as build-test.sh: clang -target armv5te-none-eabi
# + pe-link47.py from the brain-tools-git WORKING TREE, whose copy of
# pe-link47.py carries the uncommitted fix for the .reloc padding
# entry bug (see REPORT-wince-m1.md). pe-link47 merges
# ../static/coredll-exports.json (ordinal -> name for this CE build's
# coredll) over its seed table; tools/make_coredll_exports.py
# generates that file from the census.
set -e
cd "$(dirname "$0")"
PE_LINK=${PE_LINK:-$HOME/workspace/brain-tools-git/wince/braincon/pe-link47.py}
CLANG=${CLANG:-clang}
CFLAGS="-target armv5te-none-eabi -marm -Os -ffreestanding -fno-builtin -fshort-wchar"
# shellcheck disable=SC2086
$CLANG $CFLAGS -c ceguitest.c -o ceguitest.o
python3 "$PE_LINK" ceguitest.o ceguitest.exe
echo "built ceguitest.exe"
