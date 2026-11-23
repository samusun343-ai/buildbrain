#!/bin/sh
# Host acceptance run for M3 (module system + CE volumes + dialogs)
# and the S0 read path, under qemu-arm (user mode):
#   - cemodtest.exe: volume reads (incl. the Japanese path), volume
#     enumeration, overlay write shadowing, ROM DLL hosting of
#     edwgsp.dll, threads/sync, CreateProcessW, dialog manager.
#     modresults.txt must be ALL=PASS and stub calls must be 0
#     (single tolerated exception: process under qemu-user --
#     see the ENOEXEC note in the body).
#   - cectrltest.exe: commctrl.dll cannot map at its native base
#     under qemu-user (measured VA reservation), so on the host it
#     must log commctrl=SKIP-VA; the guest run is authoritative.
# The CE volumes are served from a WORKING COPY of the eMMC image
# (never the pristine image, never the restored baseline), opened
# read-only by the layer; the proof image is hashed before/after.
set -e
cd "$(dirname "$0")/.."
BW=$HOME/workspace/buildbrain-work
QEMU_ARM=${QEMU_ARM:-$BW/hosttools/root/usr/bin/qemu-arm-static}
SYSROOT=$BW/rebuild/browser-engine/.sysroot-arm
SCRATCH=${1:-/tmp/braince-host-m3}
PROOF=${CE_PROOF_IMG:-$BW/image-work/int19/s0/s0-proof.img}
ROM=${CE_ROM_FILE:-$BW/image-work/int19/s0/romA.bin}

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/fs/CE-TEST"
cp test/cetest.exe "$SCRATCH/fs/CE-TEST/child.exe"

sha256sum "$PROOF" | awk '{print $1}' > "$SCRATCH/proof.sha.before"

export CE_VOLUMES="Nand=$PROOF@831525376;Nand2=$PROOF@82845184;Nand3=$PROOF@306191872;Nand4=$PROOF@149948416"

set +e
"$QEMU_ARM" -L "$SYSROOT" ./ce-loader --fs-root "$SCRATCH/fs" \
    --rom "$ROM" ./test/cemodtest.exe >"$SCRATCH/run.log" 2>&1
MODRC=$?
set -e
cat "$SCRATCH/fs/CE-TEST/modresults.txt" || true
# Every check must pass. The single tolerated host exception is
# `process`: under qemu-user the child loader cannot be exec'd
# (the host kernel has no ARM binfmt; "errno=8" ENOEXEC in the
# loader log is the fingerprint). On the ARM guest the same spawn
# is native and must pass there -- the guest run is the
# authority for that one check.
BAD=$(grep "=FAIL" "$SCRATCH/fs/CE-TEST/modresults.txt" \
      | grep -v "^process=FAIL" | grep -v "^ALL=FAIL" || true)
if [ -n "$BAD" ]; then
    echo "HOST-M3 FAIL: cemodtest failures: $BAD"
    cat "$SCRATCH/run.log"; exit 1
fi
if grep -q "^process=FAIL" "$SCRATCH/fs/CE-TEST/modresults.txt"; then
    grep -q "exec of .* failed (errno=8)" "$SCRATCH/run.log" || {
        echo "HOST-M3 FAIL: process failed, but not with the" \
             "documented ENOEXEC artifact"; cat "$SCRATCH/run.log"
        exit 1; }
    echo "NOTE: host process=FAIL is the documented qemu-user" \
         "ENOEXEC artifact; the guest run decides this check."
else
    [ $MODRC -eq 0 ] || {
        echo "HOST-M3 FAIL: cemodtest loader exit != 0"
        cat "$SCRATCH/run.log"; exit 1; }
    echo "ok: cemodtest ALL=PASS"
fi
grep -q "stub calls total=0" "$SCRATCH/run.log" || {
    echo "HOST-M3 FAIL: stub calls != 0"; cat "$SCRATCH/run.log"; exit 1; }
echo "ok: cemodtest checks passed, stub calls 0"

"$QEMU_ARM" -L "$SYSROOT" ./ce-loader --fs-root "$SCRATCH/fs" \
    --rom "$ROM" ./test/cectrltest.exe >"$SCRATCH/run-ctrl.log" 2>&1 || {
    echo "HOST-M3 FAIL: cectrltest loader exit != 0"; cat "$SCRATCH/run-ctrl.log"; exit 1; }
cat "$SCRATCH/fs/CE-TEST/ctrlresults.txt" || true
grep -q "commctrl=SKIP-VA\|ALL=PASS" "$SCRATCH/fs/CE-TEST/ctrlresults.txt" || {
    echo "HOST-M3 FAIL: ctrlresults unexpected"; cat "$SCRATCH/run-ctrl.log"; exit 1; }
echo "ok: cectrltest host outcome as expected (SKIP-VA or PASS)"

sha256sum "$PROOF" | awk '{print $1}' > "$SCRATCH/proof.sha.after"
cmp -s "$SCRATCH/proof.sha.before" "$SCRATCH/proof.sha.after" || {
    echo "HOST-M3 FAIL: proof image hash changed"; exit 1; }
echo "ok: proof image SHA-256 unchanged"
grep "mem at exit" "$SCRATCH/run.log" || true
echo "HOST-M3 PASS (scratch: $SCRATCH)"
