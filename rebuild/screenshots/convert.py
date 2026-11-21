#!/usr/bin/env python3
"""Convert screenshots/raw/*.raw (u32 w, u32 h, RGB triplets)
into PNG + JPG pairs next to this script."""
import glob
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

def main() -> int:
    raws = sorted(glob.glob(os.path.join(HERE, "raw", "*.raw")))
    if not raws:
        print("no raw files found", file=sys.stderr)
        return 1
    for raw in raws:
        with open(raw, "rb") as f:
            data = f.read()
        w, h = struct.unpack_from("<II", data, 0)
        img = Image.frombytes("RGB", (w, h), data[8:8 + w * h * 3])
        name = os.path.splitext(os.path.basename(raw))[0]
        png = os.path.join(HERE, name + ".png")
        jpg = os.path.join(HERE, name + ".jpg")
        img.save(png)
        img.save(jpg, quality=92)
        print(f"{name}: {w}x{h} -> {os.path.basename(png)}, "
              f"{os.path.basename(jpg)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
