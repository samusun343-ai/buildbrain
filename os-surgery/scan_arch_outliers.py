#!/usr/bin/env python3
"""List ELF files whose Tag_CPU_arch is not v5TE/v5TEJ (or missing)."""
import os, re, subprocess, sys

ROOT = sys.argv[1]
SKIP = {"proc", "sys", "dev", "run"}
for base, dirs, files in os.walk(ROOT):
    rel = os.path.relpath(base, ROOT)
    if rel.split(os.sep)[0] in SKIP:
        dirs[:] = []
        continue
    for fn in files:
        full = os.path.join(base, fn)
        try:
            with open(full, "rb") as f:
                if f.read(4) != b"\x7fELF":
                    continue
            out = subprocess.run(["readelf", "-A", full],
                                 capture_output=True, text=True, timeout=20).stdout
        except Exception:
            continue
        m = re.search(r"Tag_CPU_arch:\s+(\S+)", out)
        tag = m.group(1) if m else "(none)"
        # machine type: ARM files have Tag_CPU_arch; also print e_machine via -h for non-ARM
        if tag not in ("v5TE", "v5TEJ"):
            hdr = subprocess.run(["readelf", "-h", full],
                                 capture_output=True, text=True, timeout=20).stdout
            mm = re.search(r"Machine:\s+(.+)", hdr)
            print(f"{tag}\t{mm.group(1).strip() if mm else '?'}\t/{os.path.relpath(full, ROOT)}")
