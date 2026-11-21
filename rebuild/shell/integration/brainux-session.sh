#!/bin/sh
# brainux-session - session entry for the shell v2 desktop.
#
# Installed as /usr/local/bin/brainux-session and run by
# braingreeter2.service on tty1 (as root). Flow:
#   1. braingreeter2 shows the login UI on /dev/fb0 and, on
#      success, prints the username on stdout and exits 0.
#      Power actions leave through the exit code (the greeter
#      itself stays unprivileged): 4 = power off, 5 = reboot.
#      This script owns carrying them out.
#   2. The shell v2 session (braindesk2) is started as that user.
#   3. If the v2 session cannot start or exits abnormally, fall
#      back to the classic X session (ly's session path) so the
#      device always reaches a usable UI. This fallback stays
#      until the X-compatibility PSS measurement (integration
#      phase) decides the session layout for good.
#
# State and logs live only in the user's home on the microSD
# rootfs (Brainux area). Nothing here writes to the WinCE eMMC.
set -u

LOG_DIR="${HOME:-/root}/.local/state/brainux"
mkdir -p "$LOG_DIR" 2>/dev/null || LOG_DIR=/tmp
LOG="$LOG_DIR/session.log"
exec >>"$LOG" 2>&1
echo "=== brainux-session $(date 2>/dev/null || echo unknown-time) ==="

# 1. Greeter (root, owns tty1/fb0 until login succeeds).
USER_NAME="$(/usr/local/bin/braingreeter2)"
grc=$?
case $grc in
    0)
        ;;
    4)
        echo "greeter requested power off"
        exec systemctl poweroff
        ;;
    5)
        echo "greeter requested reboot"
        exec systemctl reboot
        ;;
    *)
        echo "greeter exited $grc; restarting via systemd"
        exit 1
        ;;
esac
if [ -z "$USER_NAME" ]; then
    echo "greeter returned no user; aborting"
    exit 1
fi
echo "login: $USER_NAME"

# Resolve the user's home (all session state lives there, on SD).
USER_HOME="$(getent passwd "$USER_NAME" | cut -d: -f6)"
[ -n "$USER_HOME" ] || USER_HOME="/home/$USER_NAME"

# The per-user runtime directory is this script's responsibility:
# this launch path (service running as root, runuser to drop
# privileges) has no logind session, so nothing else creates
# /run/user/<uid> - and without it the shell's WM socket bind
# fails and the session cannot start at all (integration phase-2
# defect). Create it here, owned by the user, mode 0700, before
# anything runs as the user. /run is a tmpfs, so this runs on
# every boot; stale sockets inside are unlinked by the WM itself
# before bind.
USER_UID="$(id -u "$USER_NAME")"
RUNTIME_DIR="/run/user/$USER_UID"
if [ ! -d "$RUNTIME_DIR" ]; then
    mkdir -p "$RUNTIME_DIR" || {
        echo "cannot create runtime dir $RUNTIME_DIR; aborting"
        exit 1
    }
fi
chown "$USER_NAME" "$RUNTIME_DIR" 2>/dev/null || \
    chown "$USER_UID" "$RUNTIME_DIR"
chmod 700 "$RUNTIME_DIR"

# 2. Shell v2 session as the user. BRAINREG_* default to the
#    user's home (SD); XDG_RUNTIME_DIR is the directory above.
if [ -x /usr/local/bin/braindesk2 ]; then
    runuser -u "$USER_NAME" -- env \
        HOME="$USER_HOME" \
        XDG_RUNTIME_DIR="$RUNTIME_DIR" \
        /usr/local/bin/braindesk2
    rc=$?
    echo "braindesk2 exited $rc"
    [ $rc -eq 0 ] && exit 0
    echo "falling back to the X session"
fi

# 3. Fallback: classic X session (startx runs the user's
#    .xinitrc / the system xsession with jwm). Kept while the
#    X-compatibility measurement is pending.
exec runuser -u "$USER_NAME" -- env HOME="$USER_HOME" \
    XDG_RUNTIME_DIR="$RUNTIME_DIR" startx
