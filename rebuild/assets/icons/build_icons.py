#!/usr/bin/env python3
"""Rasterise src/<set>/*.svg to <set>/<name>.png at 96x96.

Each SVG is rendered by librsvg (tools/svg2png) at 384x384 with
full anti-aliasing, then downscaled to 96x96 with PIL's Lanczos
filter - rendering straight at display size would alias, and the
stored 96px master is later area-averaged by the toolkit to the
exact box an app asks for (24/28/44px), which keeps edges smooth
at the panel's 179ppi. Run from assets/icons/."""
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BIG = 384
OUT = 96


def ensure_tool():
    tool = os.path.join(HERE, "tools", "svg2png")
    src = tool + ".c"
    if os.path.exists(tool) and \
            os.path.getmtime(tool) > os.path.getmtime(src):
        return tool
    cmd = ["gcc", "-O2", "-o", tool, src,
           "-I/usr/include/cairo", "-I/usr/include/glib-2.0",
           "-I/usr/lib/x86_64-linux-gnu/glib-2.0/include",
           "-lcairo", "/lib/x86_64-linux-gnu/librsvg-2.so.2",
           "-lgobject-2.0", "-lglib-2.0"]
    subprocess.run(cmd, check=True)
    return tool


def main():
    tool = ensure_tool()
    n = 0
    for s in ("material", "fluent", "papirus"):
        srcdir = os.path.join(HERE, "src", s)
        outdir = os.path.join(HERE, s)
        os.makedirs(outdir, exist_ok=True)
        for fn in sorted(os.listdir(srcdir)):
            if not fn.endswith(".svg"):
                continue
            name = fn[:-4]
            big = os.path.join("/tmp", "iconbig_%s_%s.png" % (s, name))
            subprocess.run([tool, os.path.join(srcdir, fn), big,
                            str(BIG)], check=True)
            im = Image.open(big).convert("RGBA")
            im = im.resize((OUT, OUT), Image.LANCZOS)
            im.save(os.path.join(outdir, name + ".png"))
            os.unlink(big)
            n += 1
    print("rendered %d icons at %dx%d" % (n, OUT, OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
