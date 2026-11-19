# braindesk — a from-scratch desktop for Brainux

braindesk is the desktop environment, greeter (login screen) and
settings app for Brainux on SHARP Brain, written from scratch
together with its own UI toolkit, **libbrainui**.

It deliberately does not use X11, GTK, Qt, or any existing desktop:
on a 128 MiB, single-core ARMv5 machine with an 854x480 panel,
a general-purpose stack spends the hardware budget on itself.
braindesk is designed only for this machine:

- **libbrainui** (src/bui.c): one renderer for everything —
  software canvas with damage tracking in mind, glyph-cached
  TrueType text (stb_truetype), drawn vector icons (zero image
  assets), a single theme. On device it blits to the LCD
  framebuffer; there is no display server at all.
- **braingreeter**: the login screen (docs/login.png).
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
