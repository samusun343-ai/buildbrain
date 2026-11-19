#!/bin/bash

set -uex -o pipefail

SRC=$1
DST=$2

install -g root -o root -m 0644 $SRC/lib/systemd/system/boot.mount $DST/lib/systemd/system/boot.mount
install -g root -o root -m 0644 $SRC/lib/systemd/system/ethernet_gadget.service $DST/lib/systemd/system/ethernet_gadget.service
install -g root -o root -m 0755 $SRC/usr/bin/enable_ethernet_gadget $DST/usr/bin/enable_ethernet_gadget

# braindesk sources are compiled inside the chroot by setup_brainux.sh
# (same pattern as ly). Copy them from the repo's braindesk/ directory.
install -d -m 0755 $DST/opt/braindesk
cp -a ./braindesk/src ./braindesk/Makefile ./braindesk/third_party $DST/opt/braindesk/
