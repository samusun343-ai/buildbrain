#!/bin/bash
# apply-changes.sh — OS surgery phase 2 change set, applied to a
# Brainux rootfs copy. Re-runnable (idempotent). The dpkg purge step
# needs the qemu-arm chroot helper next to this script.
#
# usage: apply-changes.sh <rootfs> [--fs-only]
#   --fs-only: skip the chroot dpkg purge (filesystem changes only)
#
# Change set (REPORT-os-surgery-1.md §5.3 / §5.4 + phase-2 §3):
#   1. Mask 6 timers (apt-daily, apt-daily-upgrade, dpkg-db-backup,
#      e2scrub_all, fstrim, man-db). fake-hwclock-save and logrotate
#      stay enabled (phase-1 §5.4 table).
#   2. journald drop-in: SystemMaxUse=32M / RuntimeMaxUse=16M
#      (capacity-based, not day-based — standing project rule).
#   3. DHCP unification: /usr/bin/enable_ethernet_gadget switches
#      from bare `dhclient` to `dhcpcd usb0`; isc-dhcp-client is
#      purged via dpkg inside the chroot.
# NOT included (user decision 2026-11-21): dev-tool separation
# (phase-1 §5.1) and ssh removal (§5.2) are rejected — both stay.
set -eu
RF=$1
FS_ONLY=${2:-}
HERE=$(dirname "$0")

echo "== [1/3] mask timers =="
for u in apt-daily.timer apt-daily-upgrade.timer dpkg-db-backup.timer \
         e2scrub_all.timer fstrim.timer man-db.timer; do
    ln -sf /dev/null "$RF/etc/systemd/system/$u"
    echo "masked: $u"
done

echo "== [2/3] journald drop-in =="
mkdir -p "$RF/etc/systemd/journald.conf.d"
cat > "$RF/etc/systemd/journald.conf.d/50-brainux.conf" <<'EOF'
[Journal]
SystemMaxUse=32M
RuntimeMaxUse=16M
EOF
echo "wrote: etc/systemd/journald.conf.d/50-brainux.conf"

echo "== [3/3] DHCP unification =="
GADGET="$RF/usr/bin/enable_ethernet_gadget"
if grep -qx 'dhclient' "$GADGET"; then
    sed -i 's/^dhclient$/dhcpcd usb0/' "$GADGET"
fi
if grep -q 'dhclient' "$GADGET"; then
    echo "ERROR: dhclient still referenced in enable_ethernet_gadget" >&2
    exit 1
fi
grep -qx 'dhcpcd usb0' "$GADGET" && echo "enable_ethernet_gadget: dhclient -> dhcpcd usb0"

if [ "$FS_ONLY" != "--fs-only" ]; then
    "$HERE/staging-chroot.sh" "$RF" /usr/bin/apt-get purge -y isc-dhcp-client
    echo "purged: isc-dhcp-client (via chroot apt-get)"
else
    echo "skipped dpkg purge (--fs-only)"
fi
echo "APPLY_OK"
