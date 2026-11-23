#!/bin/sh
# Host acceptance run for M2 (GWE minimal): ce-loader + ceguitest.exe
# under qemu-arm (user mode), in two runs.
#
# Run A (pixels): tools/wmhost.c runs a real braindesk WM server in
# process; the CE app appears as a normal Brainux window (GHOLD mode:
# the app writes its log at timer 8 and stays open). wmhost composites
# the server surface and checks the deterministic paint scene pixel
# by pixel, plus the 400x300 content size.
#
# Run B (lifecycle): no WM server (the shim runs headless on its
# shadow canvas). The app must run the full lifecycle and exit 0:
# guilog.txt must equal the expected DESTROY=1 content byte for byte
# and the loader log must report "stub calls total=0".
set -e
cd "$(dirname "$0")/.."
BW=$HOME/workspace/buildbrain-work
QEMU_ARM=${QEMU_ARM:-$BW/hosttools/root/usr/bin/qemu-arm-static}
SYSROOT=$BW/rebuild/browser-engine/.sysroot-arm
SCRATCH=${1:-/tmp/braince-host-gui}
WMHOST_BIN=$SCRATCH/wmhost
export BRAINUI_FONT_PATH=$BW/rebuild/fonts/brainui-jp-subset.ttf

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/fs/CE-TEST"

# --- build the host harness (host gcc + host libbrainui2) ---
gcc -I../include -I../shell/include -D_GNU_SOURCE \
    -D_POSIX_C_SOURCE=200809L -O2 -o "$WMHOST_BIN" \
    tools/wmhost.c ../shell/src/wm.c ../libbrainui2.a \
    $(pkg-config --libs harfbuzz freetype2 2>/dev/null || echo "-lharfbuzz -lfreetype") \
    -lm -ldl

# --- Run A: GHOLD + real WM server, pixel checks ---
touch "$SCRATCH/fs/CE-TEST/GHOLD"
"$WMHOST_BIN" "$SCRATCH/bdwm.sock" "$SCRATCH/fs/CE-TEST/guilog.txt" \
    "$QEMU_ARM" -L "$SYSROOT" ./ce-loader \
    --fs-root "$SCRATCH/fs" ./test/ceguitest.exe \
    >"$SCRATCH/runA.log" 2>&1 || {
    echo "HOST-GUI FAIL: run A (wmhost)"; cat "$SCRATCH/runA.log"; exit 1; }
cat "$SCRATCH/runA.log"

cat > "$SCRATCH/expected-ghold.txt" <<'EOF'
braince-m2 ceguitest v1
CREATE=1
USER_POST=1
USER_SEND=1
SENDRET=OK
PAINT=OK
TIMER=8
BITBLT=OK
GETPIXEL=OK
DESTROY=0
GUILOG=PASS
EOF
cmp -s "$SCRATCH/expected-ghold.txt" "$SCRATCH/fs/CE-TEST/guilog.txt" || {
    echo "HOST-GUI FAIL: run A guilog mismatch"
    echo "--- got:"; cat "$SCRATCH/fs/CE-TEST/guilog.txt"; exit 1; }
echo "ok: run A guilog (GHOLD variant) byte-identical"

# --- Run B: full lifecycle, headless, exit + stub report ---
rm -f "$SCRATCH/fs/CE-TEST/GHOLD" "$SCRATCH/fs/CE-TEST/guilog.txt"
"$QEMU_ARM" -L "$SYSROOT" ./ce-loader --fs-root "$SCRATCH/fs" \
    ./test/ceguitest.exe >"$SCRATCH/runB.log" 2>&1 || {
    echo "HOST-GUI FAIL: run B loader exit != 0"; cat "$SCRATCH/runB.log";
    exit 1; }

cat > "$SCRATCH/expected-full.txt" <<'EOF'
braince-m2 ceguitest v1
CREATE=1
USER_POST=1
USER_SEND=1
SENDRET=OK
PAINT=OK
TIMER=8
BITBLT=OK
GETPIXEL=OK
DESTROY=1
GUILOG=PASS
EOF
fail=0
cmp -s "$SCRATCH/expected-full.txt" "$SCRATCH/fs/CE-TEST/guilog.txt" || {
    echo "HOST-GUI FAIL: run B guilog mismatch"; fail=1;
    echo "--- got:"; cat "$SCRATCH/fs/CE-TEST/guilog.txt" || true; }
grep -q "stub calls total=0" "$SCRATCH/runB.log" || {
    echo "HOST-GUI FAIL: run B stub calls != 0"; fail=1; }
grep "mem at exit" "$SCRATCH/runB.log" || true
if [ "$fail" = 0 ]; then
    echo "HOST-GUI PASS (scratch: $SCRATCH)"
else
    echo "--- runB.log:"; cat "$SCRATCH/runB.log"
    exit 1
fi
