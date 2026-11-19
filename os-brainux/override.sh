#!/bin/bash

set -uex -o pipefail

SRC=$1
DST=$2

install -g root -o root -m 0644 $SRC/etc/motd $DST/etc/motd
install -g root -o root -m 0440 $SRC/etc/sudoers $DST/etc/sudoers

install -g root -o root -m 0644 $SRC/etc/X11/xorg.conf $DST/etc/X11/xorg.conf
install -g root -o root -m 0644 $SRC/etc/X11/Xsession.d/96calibrate $DST/etc/X11/Xsession.d/96calibrate

install -g root -o root -m 0644 -D $SRC/etc/xdg/weston/weston.ini $DST/etc/xdg/weston/weston.ini

install -g 1000 -o 1000 -m 0644 $SRC/home/user/.xprofile $DST/home/user/.xprofile
install -d -o 1000 -g 1000 -m 0755 $DST/home/user/.config/fcitx
install -g 1000 -o 1000 -m 0644 $SRC/home/user/.config/fcitx/profile $DST/home/user/.config/fcitx/profile
install -d -o 1000 -g 1000 -m 0755 $DST/home/user/lxterminal
install -g 1000 -o 1000 -m 0644 $SRC/home/user/lxterminal/lxterminal.conf $DST/home/user/lxterminal/lxterminal.conf
install -g root -o root -m 0644 -D $SRC/etc/jwm/system.jwmrc $DST/etc/jwm/system.jwmrc

# braindesk (from-scratch desktop) session files
install -g root -o root -m 0755 $SRC/usr/local/bin/brain-pin-hunt $DST/usr/local/bin/brain-pin-hunt
install -g root -o root -m 0755 $SRC/usr/local/bin/braindesk-session $DST/usr/local/bin/braindesk-session
install -g root -o root -m 0644 $SRC/lib/systemd/system/braindesk-session.service $DST/lib/systemd/system/braindesk-session.service
install -g root -o root -m 0644 -D $SRC/etc/systemd/logind.conf.d/braindesk.conf $DST/etc/systemd/logind.conf.d/braindesk.conf
install -g root -o root -m 0755 $SRC/usr/local/bin/brain-measure $DST/usr/local/bin/brain-measure
# gadget modes (brain-mode / brain-gadget)
install -g root -o root -m 0755 $SRC/usr/local/bin/brain-mode $DST/usr/local/bin/brain-mode
install -g root -o root -m 0755 $SRC/usr/local/bin/brain-gadget $DST/usr/local/bin/brain-gadget
install -g root -o root -m 0644 $SRC/lib/systemd/system/brain-mode.service $DST/lib/systemd/system/brain-mode.service
install -g root -o root -m 0644 $SRC/lib/systemd/system/brain-server.target $DST/lib/systemd/system/brain-server.target
for mode in normal deck server share; do
    install -g root -o root -m 0644 -D $SRC/usr/share/brain-modes/$mode.conf $DST/usr/share/brain-modes/$mode.conf
done
