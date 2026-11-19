# Optimization charter (buildbrain / Brainux)

Standing rule from the project owner (2026-11-19):

> Interface compatibility is sacred; implementation is free.

Existing applications must run unchanged, and any change must
make behavior measurably better. Under those two conditions,
every layer may be optimized or replaced — including Xorg
itself, the kernel's internals and memory management, and the
libraries. Neither "everything custom" nor "everything
upstream": decide per layer, by measurement (brain-measure,
boot-time and latency checks on the real image).

## Interfaces that stay stable

- Linux syscall ABI, ELF/armel ABI of shipped libraries
- evdev / fbdev / ALSA / DRM-less display ioctls the drivers expose
- The session display protocol (X11 or Wayland, once chosen)
- D-Bus system services apps rely on (logind, NetworkManager)
- File formats and on-disk layout users touch (SD partitions)

## Layers explicitly in scope

- Kernel: config trim to what the hardware + system actually
  uses; memory management (reclaim, page-cache behaviour,
  zram/zswap decided by measurement, not assumption); the
  known CPU-clock-halving defect on some boot paths; SD I/O
  scheduling; build flags.
- Drivers: backlight, audio (BU26154), keyboard matrix,
  PSWITCH, suspend/resume paths.
- Display server: whichever is chosen, its fbdev/shadow and
  damage paths are fair game for optimization or replacement.
- Libraries: optimized ARMv5 string/memory routines, size/LTO
  builds of hot libraries, fontconfig caches — same ABI.
- Userspace: systemd unit trim, service consolidation
  (one IM, one terminal), duplicate removal from the rootfs
  inventory.
- braindesk itself: renderer, glyph cache, damage-only
  redraws, zero-copy where the hardware allows.

## Method

1. Inventory and measure the stock system first.
2. Change one layer at a time; keep the measurement that
   justified it with the commit.
3. If a change does not measure better, revert it — taste is
   not evidence.
