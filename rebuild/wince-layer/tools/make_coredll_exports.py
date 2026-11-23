#!/usr/bin/env python3
"""Write the pe-link47 merge file (coredll-exports.json).

pe-link47.py merges <dir of pe-link47>/../static/coredll-exports.json
(ordinal -> name) over its hand-verified seed table. We generate that
file from the QEMU-side census ordmap (exports-census.json), which
was verified byte-identical to the independently derived p47 map.

Usage: make_coredll_exports.py [pe-link47.py path]
"""
import json, os, sys

CENSUS = os.path.expanduser(
    "~/workspace/goals/sharp-brain-full-virtualization/hidden_files/"
    "p45work/static/exports-census.json")
DEFAULT_LINK = os.path.expanduser(
    "~/workspace/brain-tools-git/wince/braincon/pe-link47.py")


def main():
    link = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LINK
    static_dir = os.path.join(os.path.dirname(os.path.abspath(link)),
                              "..", "static")
    os.makedirs(static_dir, exist_ok=True)
    ec = json.load(open(CENSUS))
    ordmap = ec["coredll.dll"]["ordmap"]
    dst = os.path.join(static_dir, "coredll-exports.json")
    with open(dst, "w") as f:
        json.dump(ordmap, f, indent=0, sort_keys=True)
    print(f"wrote {dst}: {len(ordmap)} entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
