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

| process | PSS (xmeas6) | PSS (xmeas7) |
|---------|-------------|-------------|
| Xorg (`:0 vt7`) | 34,752 kB | 32,730 kB |
| session dbus-daemon | — | 1,146 kB |
| jwm | not captured | not captured |
| fcitx | not running | not running |

System totals with X up (`free -m`): used 43–44 MB of 112 MB,
MemAvailable ~69 MB. `brain-measure` session PSS total 42,895 kB
(xmeas7) — that total includes systemd/dbus/NetworkManager, so the
X-specific part is Xorg ~33 MB + session dbus ~1 MB.

Before X starts (same image, greeter session stopped): used 31 MB.

## Session startup is slow

JWM's taskbar is visible in the `xsession6.ppm` / `xsession7.ppm`
screendumps, but `ps` taken 2–4 minutes after `startx` still shows
only `xinit` + `Xorg` — the window manager only appears around
3.5–4.5 minutes after Xorg starts on this emulated CPU (with a second
QEMU running concurrently). A measurement taken too early therefore
misses jwm entirely. `fcitx` never appeared in either run:
`/etc/xdg/autostart/` does not exist in the run-5 image, so no input
method is autostarted in the stock X session. A follow-up run with a
~5 minute settle time should capture jwm's PSS (expected small,
1–3 MB); it does not change the conclusion below.

## Comparison: braindesk (measured earlier, same harness)

| stack | PSS |
|-------|-----|
| braindesk greeter (`braindesk-session`) | ~4.9 MB |
| braindesk shell | ~4.6 MB |
| stock Xorg alone | ~33–35 MB |

Xorg alone costs roughly 7x the whole braindesk shell. The stock
X+JWM path is the memory hog of the desktop, not the window manager:
even with jwm's PSS still to be captured, the total will stay
dominated by Xorg. Any future display-path work (Xorg tuning,
Weston/Wayland, or braindesk-direct) should be judged against the
~33 MB Xorg baseline and the ~5 MB braindesk figure.
