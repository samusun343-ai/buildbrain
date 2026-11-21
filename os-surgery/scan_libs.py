#!/usr/bin/env python3
"""Scan a rootfs: ELF inventory, NEEDED/SONAME graph, CPU arch tags,
per-package real disk usage. Host-side, read-only."""
import os, re, subprocess, sys, json
from collections import defaultdict

ROOT = sys.argv[1]
OUT = sys.argv[2]

# path -> package map from dpkg info lists
path2pkg = {}
info = os.path.join(ROOT, "var/lib/dpkg/info")
for name in os.listdir(info):
    if name.endswith(".list"):
        pkg = name[:-5].split(":")[0]
        with open(os.path.join(info, name), errors="replace") as f:
            for line in f:
                p = line.strip()
                if p:
                    path2pkg[p] = pkg

pkg_size = defaultdict(int)
for p, pkg in path2pkg.items():
    full = os.path.join(ROOT, p.lstrip("/"))
    try:
        st = os.lstat(full)
        pkg_size[pkg] += st.st_size
    except OSError:
        pass

SKIP = {"proc", "sys", "dev", "run"}
elf_files = []
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
        except OSError:
            continue
        elf_files.append(full)

soname_of = {}          # file path -> soname
needed_by = {}          # file path -> [sonames]
arch_of = {}            # file path -> Tag_CPU_arch string
consumers = defaultdict(set)  # soname -> set(file paths)
for full in elf_files:
    try:
        out = subprocess.run(["readelf", "-d", "-A", full],
                             capture_output=True, text=True, timeout=20).stdout
    except Exception:
        continue
    rel = "/" + os.path.relpath(full, ROOT)
    for line in out.splitlines():
        m = re.search(r"\(SONAME\)\s+.*\[(.+)\]", line)
        if m:
            soname_of[rel] = m.group(1)
        m = re.search(r"\(NEEDED\)\s+.*\[(.+)\]", line)
        if m:
            needed_by.setdefault(rel, []).append(m.group(1))
            consumers[m.group(1)].add(rel)
        m = re.search(r"Tag_CPU_arch:\s+(\S+)", line)
        if m and rel not in arch_of:
            arch_of[rel] = m.group(1)

soname_file = {}
for rel, so in soname_of.items():
    soname_file[so] = rel

result = {
    "elf_count": len(elf_files),
    "pkg_real_bytes": dict(pkg_size),
    "arch_distribution": {},
    "libs": {},
}
from collections import Counter
result["arch_distribution"] = dict(Counter(arch_of.values()))
noarch = [p for p in ("/" + os.path.relpath(f, ROOT) for f in elf_files) if p not in arch_of]
result["elf_without_arch_tag"] = len(noarch)

for so, cons in sorted(consumers.items(), key=lambda kv: -len(kv[1])):
    libfile = soname_file.get(so)
    entry = {
        "consumer_files": len(cons),
        "consumer_pkgs": sorted({path2pkg.get(c, "?") for c in cons}),
        "lib_file": libfile,
        "lib_pkg": path2pkg.get(libfile) if libfile else None,
        "lib_bytes": None,
    }
    if libfile:
        try:
            entry["lib_bytes"] = os.path.getsize(os.path.join(ROOT, libfile.lstrip("/")))
        except OSError:
            pass
    result["libs"][so] = entry

with open(OUT, "w") as f:
    json.dump(result, f, indent=1, ensure_ascii=False)
print(f"ELF files: {len(elf_files)}")
print("arch distribution:", result["arch_distribution"])
print(f"sonames with consumers: {len(result['libs'])}")
