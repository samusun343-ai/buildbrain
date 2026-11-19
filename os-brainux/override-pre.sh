#!/bin/bash

set -uex -o pipefail

SRC=$1
DST=$2

install -g root -o root -m 0644 $SRC/lib/systemd/system/boot.mount $DST/lib/systemd/system/boot.mount
install -g root -o root -m 0644 $SRC/lib/systemd/system/ethernet_gadget.service $DST/lib/systemd/system/ethernet_gadget.service
install -g root -o root -m 0755 $SRC/usr/bin/enable_ethernet_gadget $DST/usr/bin/enable_ethernet_gadget
# brain-mode.service must be in place BEFORE the chroot setup runs:
# setup_brainux.sh does `systemctl enable brain-mode` inside the chroot,
# and enable fails ("Unit brain-mode.service does not exist") when the
# unit only arrives later via override.sh. That ordering bug failed
# Actions run 8 (44490120842) at `make brainux`. The binary, profiles
# and brain-server.target still ship via override.sh (runtime only).
install -g root -o root -m 0644 $SRC/lib/systemd/system/brain-mode.service $DST/lib/systemd/system/brain-mode.service

# braindesk sources are compiled inside the chroot by setup_brainux.sh
# (same pattern as ly). Copy them from the repo's braindesk/ directory.
install -d -m 0755 $DST/opt/braindesk
cp -a ./braindesk/src ./braindesk/Makefile ./braindesk/third_party ./braindesk/assets ./braindesk/skins $DST/opt/braindesk/

# Kernel modules staged by `make lbuild` (see the root Makefile).
# Shipping them lets peripheral drivers be loadable modules instead
# of permanent built-ins. Absent on builds that skipped lbuild.
if [ -d ./os-brainux/modules-staging/lib/modules ]; then
    install -d -m 0755 $DST/lib/modules
    cp -a ./os-brainux/modules-staging/lib/modules/. $DST/lib/modules/
fi
