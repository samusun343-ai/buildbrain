#!/usr/bin/env python3
"""Convert an Android vector drawable XML (AOSP SystemUI
ic_sysbar_*) to an equivalent SVG. Android pathData uses the
SVG path command set; viewportWidth/Height map to viewBox.
Only the simple single/multi-path filled drawables used by
the sysbar icons are supported - anything else is an error,
not a silent approximation."""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
xml = open(src).read()

m = re.search(r'android:viewportWidth="([0-9.]+)"', xml)
vw = m.group(1) if m else "24"
m = re.search(r'android:viewportHeight="([0-9.]+)"', xml)
vh = m.group(1) if m else "24"

paths = []
for pm in re.finditer(r"<path\b[^>]*>", xml, re.S):
    tag = pm.group(0)
    d = re.search(r'android:pathData="([^"]+)"', tag)
    if not d:
        continue
    fill = 'fill="#ffffff"'
    ft = re.search(r'android:fillType="([^"]+)"', tag)
    if ft and ft.group(1) == "evenOdd":
        fill += ' fill-rule="evenodd"'
    paths.append(f'<path {fill} d="{d.group(1)}"/>')
if not paths:
    sys.exit(f"no paths in {src}")
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="96" '
       f'height="96" viewBox="0 0 {vw} {vh}">'
       + "".join(paths) + "</svg>")
open(dst, "w").write(svg)
print(f"{dst}: {len(paths)} path(s), viewport {vw}x{vh}")
