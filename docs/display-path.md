# Display-path memory baseline

Measured 2026-11-19 in QEMU (PW-SH6 model) on the run-5 image
(`sd-run5.zip`, master `edf520c`, kernel 6.1.70-g10e632e195da),
with `brain-measure` run as root. Method: boot via the `bdtest.py`
harness, stop `ly` + `braindesk-session` from the serial shell to free
the framebuffer, then `startx -- vt7` as user `user`.

## What blocked the first attempts

- `startx` as the normal user died with
  `(EE) parse_vt_settings: Cannot open /dev/tty0 (Permission denied)`.
  Fix used for the measurements below:
  `sudo chmod 666 /dev/tty0 /dev/tty7 /dev/fb0` before `startx`.
  (Running `startx` itself under sudo was not needed.)
- On an image with `braindesk-session` enabled, the braingreeter holds
  `/dev/fb0` with `KD_GRAPHICS`, so `startx` cannot connect. Stop both
  `ly` and `braindesk-session` first.
- `brain-measure` run as the normal user reports PSS 0 (smaps needs
  root). Always run it via sudo.

## Stock X + JWM (run-5 image)

| process | PSS (xmeas6) | PSS (xmeas7) | PSS (xmeas8) | PSS (xmeas9) | PSS (xmeas10) |
|---------|-------------|-------------|-------------|-------------|--------------|
| Xorg (`:0 vt7`) | 34,752 kB | 32,730 kB | 33,420 kB | 29,385 kB | 30,976 kB |
| jwm (`x-window-manager`) | not captured | not captured | not captured | not captured | 15,466 kB |
| session dbus-daemon | — | 1,146 kB | 1,000 kB | 1,007 kB | 1,002 kB |
| ssh-agent | — | — | — | — | 850 kB |
| fcitx | not running | not running | not running | not running | not running |

System totals with X up (`free -m`): used 43–44 MB of 112 MB,
MemAvailable ~68–69 MB. `brain-measure` session PSS total 41,275 kB
(xmeas10, old binary without the jwm fix — see below); adding jwm's
15,466 kB gives ~56,741 kB for the Xorg + jwm + base-services total.

Before X starts (same image, greeter session stopped): used 31 MB.

jwm's PSS breakdown (xmeas10, `/proc/384/smaps_rollup`):
Pss_Anon 2,444 kB, Pss_File 13,022 kB, VmRSS 19,116 kB. Most of it is
file-backed: the X client libraries and fontconfig caches that jwm —
the only X client in this session — maps alone. Its anonymous cost is
only ~2.4 MB. Extra X clients would share those file pages, so jwm's
marginal cost inside a full desktop is lower than 15.5 MB; but a bare
stock X session pays the full amount.

## The jwm detection trap (xmeas6–xmeas9)

In xmeas6–xmeas9 jwm never appeared in `ps | grep jwm` or in
`brain-measure`, even after up to ~11 minutes of polling — yet every
screendump showed jwm's taskbar with a live clock. The cause was the
process name, not the startup speed: the session starts the WM via
`/usr/bin/x-window-manager` (Debian alternatives symlink to jwm), so
the kernel comm is `x-window-manager` and the cmdline contains no
`jwm` substring. Both the `ps | grep jwm` polls and
`brain-measure`'s comm list missed it. In xmeas10, polling for
`x-window-manager` found jwm (PID 384) immediately after the 180 s
settle: it had started at 15:24, one minute after Xorg (15:23). The
earlier "session startup is slow, jwm appears after 3.5–11 minutes"
reading was a measurement artifact; the real X session comes up in
about a minute after `startx` on this emulated CPU (with two other
QEMU instances competing).

The session chain itself was confirmed from `~/.xsession-errors`
(xmeas9): `xinput_calibrator` runs via `96calibrate` but completes
non-interactively for the `mxs-lradc-ts` touch device (calibration
data 0, 4095, 0, 4095) and does not block the chain. jwm logs one
warning on startup: `JWM: warning: /etc/jwm/system.jwmrc[6]:
invalid include: /etc/jwm/debian-menu` — the `menu` package is not
installed, so no Debian menu is generated. Cosmetic; candidate
follow-up is to drop the include from `system.jwmrc` in os-brainux
or install `menu`.

`fcitx` never appeared in any run: `/etc/xdg/autostart/` does not
exist in the run-5 image, so no input method is autostarted in the
stock X session.

Fix shipped with this doc update: `brain-measure` now also matches
the `x-window-manager` / `x-session-manager` comms, so future runs
capture the WM. (The run-5 image still carries the old binary, which
is why xmeas10's `brain-measure` total above excludes jwm.)

## Comparison: braindesk (measured earlier, same harness)

| stack | PSS |
|-------|-----|
| braindesk greeter (`braindesk-session`) | ~4.9 MB |
| braindesk shell | ~4.6 MB |
| stock Xorg alone | ~29–35 MB |
| stock Xorg + jwm | ~45–50 MB |

Xorg + jwm together cost roughly 10x the whole braindesk shell. The
stock X+JWM path is the memory hog of the desktop — and the WM is a
bigger share of it than expected (jwm ~15.5 MB PSS, mostly shared
libraries and font caches it alone maps as the only X client). Any
future display-path work (Xorg tuning, Weston/Wayland, or
braindesk-direct) should be judged against the ~31 MB Xorg baseline
and the ~5 MB braindesk figure.
