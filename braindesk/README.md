# braindesk — a from-scratch desktop for Brainux

> **このファイルの位置づけ**: 本ファイルは、作業の過程で積み上げ最中の観測・解析・実装の記録であり、確立した知識ではない。
> 文中の「確定」「実証」等の断定表現は、記録時点での記録者の判断を示すにすぎず、内容が正確である保証はない（誤りを含む可能性がある）。
> 参照する場合は、前提として扱わず、一次資料と照合して独立に検証すること。


braindesk is the desktop environment, greeter (login screen) and
settings app for Brainux on SHARP Brain, written from scratch
together with its own UI toolkit, **libbrainui**.

braindesk is not a purist from-scratch stack, and it is not a
reskin of the stock one either. The rule is per-layer: abolish
what is wasteful, adopt what is already good (protocols and
libraries), and build our own where the generic option costs
this machine too much. Concretely:

- The greeter owns the framebuffer directly before any session
  exists (the same position ly occupies today) — no display
  server is needed, or wanted, at that stage.
- Inside the session, existing applications must keep running:
  a desktop that only runs its own apps is a dead end for
  development and for users. The session therefore keeps a
  standard display path (X11 or Wayland — decided by measuring
  both on this hardware, see below), and braindesk's shell,
  panel and settings live there as first-class programs.
- libbrainui is the toolkit for braindesk's own programs and
  draws into an abstract canvas, so the same apps run on the
  greeter's fbdev backend and on the session backend without
  being rewritten for either.
- Where an existing library is simply better (pixman for
  compositing, libwayland, fontconfig), braindesk uses it
  instead of reinventing it.

Measured on the QEMU Brainux image (2026-11-19, brain-measure):
the shell runs at 4.6 MB PSS total (docs/on-device-qemu.png is a
framebuffer capture from that run). The first build measured
22.2 MB — the font had been read fully into anonymous memory;
mmap'ing the TTC instead brought it to 4.6 MB. Stock-system
baseline for comparison: NetworkManager alone is 13.0 MB PSS,
systemd(1) 4.5 MB, ly 0.5 MB.

Open decision, to be settled by measurement on the real image
(idle PSS, start-up time, input latency): Xorg+fbdev with a
braindesk window manager vs a small Wayland compositor. The
greeter and libbrainui are unaffected by that choice.

- **libbrainui** (src/bui.c): one renderer for everything —
  software canvas with damage tracking in mind, glyph-cached
  TrueType text (stb_truetype), drawn vector icons (zero image
  assets), a single theme. On device it blits to the LCD
  framebuffer; there is no display server at all.
- **braingreeter**: the login screen (docs/login.png). Real
  authentication against /etc/shadow via crypt() — Debian's
  libxcrypt covers the yescrypt ($y$) hashes Brainux uses.
  The greeter picks the first regular user automatically,
  reports poweroff/reboot requests through its exit code, and
  hands the session user to the shell via
  /run/braindesk/session-user. Like ly, it is built inside the
  Brainux chroot during image creation, where the target's own
  headers and libraries are available.
- **braindesk shell**: panel/taskbar/tray + launcher
  (docs/desktop.png). Familiar desktop grammar, nothing exotic.
- **brainsettings**: the settings app (docs/settings.png),
  replacing brain-config; GUI-first, with a CLI twin for every
  setting.

Apps draw into shared-memory surfaces that the shell composites;
input comes straight from evdev (keyboard matrix, touch, PSWITCH
power button). Existing X applications are not the target —
braindesk apps are native libbrainui programs, and the SDK is
the toolkit itself plus templates.

Status: toolkit core + scenes render today; the shell binary
(src/shell.c) runs an evdev event loop over an fbdev backend
(RGB565 conversion, page flipping when available) and applies
the brightness slider to /sys/class/backlight for real.
Interaction is covered by src/test_events.c (synthetic taps and
drags: launcher, settings, slider, toggles, close). A static ARM
build is ~1.0 MiB (`make arm`). Host build: `make`, then
`./demo`, `./test_events`, `./shell --shot`.
Next: run it on the device image in QEMU and on hardware, wire
volume to ALSA, add the greeter binary, then real apps.
