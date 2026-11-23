#!/bin/sh
# Host acceptance run for M1: ce-loader + cetest.exe under
# qemu-arm (user mode). Verifies, per DESIGN section 9.1:
#   - exit status 0
#   - CE-TEST/result.txt byte-identical to the expected content
#   - CE-TEST/pattern.bin matches the deterministic pattern
#   - loader log reports "stub calls total=0"
#   - no HOLD file leftovers affect the run (fresh scratch dir)
set -e
cd "$(dirname "$0")/.."
QEMU_ARM=${QEMU_ARM:-$HOME/workspace/buildbrain-work/hosttools/root/usr/bin/qemu-arm-static}
# Since M2 the loader is dynamically linked (libbrainui2 + WM
# client), so qemu-arm needs the userland sysroot as its -L prefix.
SYSROOT=$HOME/workspace/buildbrain-work/rebuild/browser-engine/.sysroot-arm
SCRATCH=${1:-/tmp/braince-host-test}
rm -rf "$SCRATCH"
# FS bridge v0 maps CE absolute paths under the fs root; the first
# component is a volume directory that must exist (mounted volume).
mkdir -p "$SCRATCH/fs/CE-TEST"

"$QEMU_ARM" -L "$SYSROOT" ./ce-loader --fs-root "$SCRATCH/fs" \
    ./test/cetest.exe >"$SCRATCH/run.log" 2>&1 || {
    echo "HOST-TEST FAIL: loader exit != 0"; cat "$SCRATCH/run.log"; exit 1; }

cat > "$SCRATCH/expected.txt" <<'EOF'
braince-m1 cetest v1
wcs=OK
localalloc=OK
virtualalloc=OK
tick=OK
filewrite=OK
filesize=OK
fileread=OK
errorpath=OK
mkdir=OK
findfile=OK
delete=OK
ALL=PASS
EOF

fail=0
cmp -s "$SCRATCH/expected.txt" "$SCRATCH/fs/CE-TEST/result.txt" || {
    echo "HOST-TEST FAIL: result.txt mismatch"; fail=1;
    echo "--- got:"; cat "$SCRATCH/fs/CE-TEST/result.txt" || true; }
grep -q "stub calls total=0" "$SCRATCH/run.log" || {
    echo "HOST-TEST FAIL: stub calls != 0"; fail=1; }
python3 - "$SCRATCH/fs/CE-TEST/pattern.bin" <<'EOF' || fail=1
import sys
data = open(sys.argv[1], "rb").read()
exp = bytes(((i * 37 + 11) & 0xFF) for i in range(4096))
if data != exp:
    print("HOST-TEST FAIL: pattern.bin mismatch", len(data))
    sys.exit(1)
EOF
grep "mem at exit" "$SCRATCH/run.log" || true
if [ "$fail" = 0 ]; then
    echo "HOST-TEST PASS (scratch: $SCRATCH)"
else
    echo "--- run.log:"; cat "$SCRATCH/run.log"
    exit 1
fi
