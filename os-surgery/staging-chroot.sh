#!/bin/bash
# staging-chroot.sh — run a command inside the staging rootfs via
# qemu-arm chroot. Mounts in this environment are per-exec-call, so
# binfmt registration, mounts, chroot and unmount all happen inside
# this single invocation (pattern adapted from
# image-work/run_chroot_setup.sh).
#
# usage: staging-chroot.sh <rootfs> <command> [args...]
set -u
RF=$1; shift

mount -t binfmt_misc none /proc/sys/fs/binfmt_misc 2>/dev/null || true
if [ ! -e /proc/sys/fs/binfmt_misc/qemu-arm ]; then
    printf '%b' ':qemu-arm:M::\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-arm-static:F' > /proc/sys/fs/binfmt_misc/register
fi

mount -t proc none "$RF/proc"
mount --rbind /sys "$RF/sys"
mount -t devpts none "$RF/dev/pts" -o ptmxmode=666 || true
# Bind host /dev over the copied tree's /dev so device nodes
# (null, urandom, ...) exist for maintainer scripts.
mount --rbind /dev "$RF/dev" || true

env -i PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin HOME=/root TERM=xterm chroot "$RF" "$@"
rc=$?

umount "$RF/dev/pts" 2>/dev/null || true
umount -l "$RF/dev" 2>/dev/null || true
umount -l "$RF/sys" 2>/dev/null || true
umount "$RF/proc" 2>/dev/null || true
exit $rc
