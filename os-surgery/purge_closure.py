#!/usr/bin/env python3
"""Compute purge impact from a real rootfs's dpkg status + apt
extended_states.

Mirrors `apt-get purge <seeds>` followed by `apt-get autoremove
--purge`:
- seeds are removed unconditionally;
- afterwards, auto-installed packages (Auto-Installed: 1 in
  extended_states) whose reverse dependencies are all gone are
  removed, iteratively;
- manually installed, Essential, and Priority: required packages
  are never cascade-removed;
- finally, satisfiability of every remaining package's
  Depends/Pre-Depends is verified and violations are reported
  (a non-empty violation list means the seed set is unsafe).

Conservative simplification: a dependency "a | b" counts as a
reverse dependency on both a and b, so cascades may stop earlier
than real apt. Reported sizes are therefore lower bounds.

Usage: purge_closure.py <status> <extended_states> <seed,seed,...>
"""
import re, sys, json

STATUS, EXTENDED, SEEDS = sys.argv[1], sys.argv[2], sys.argv[3].split(",")

def parse_blocks(path):
    blocks, cur = {}, None
    field = None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.strip() == "":
            cur = None; field = None; continue
        if line[0] in " \t":
            if cur is not None and field:
                cur[field] += " " + line.strip()
            continue
        k, _, v = line.partition(":")
        field = k.strip()
        if field == "Package":
            cur = {"Package": v.strip()}
            blocks[v.strip()] = cur
        elif cur is not None:
            cur[field] = v.strip()
    return blocks

pkgs = {p: d for p, d in parse_blocks(STATUS).items()
        if d.get("Status") == "install ok installed"}
ext = parse_blocks(EXTENDED)
auto = {p for p, d in ext.items() if d.get("Auto-Installed") == "1"}

def dep_groups(depstr):
    groups = []
    for part in depstr.split(","):
        alts = [re.split(r"[\s(:]", a.strip())[0]
                for a in part.split("|") if a.strip()]
        alts = [a for a in alts if a]
        if alts:
            groups.append(alts)
    return groups

provides = {}
for p, d in pkgs.items():
    for prov in (d.get("Provides") or "").split(","):
        name = re.split(r"[\s(:]", prov.strip())[0]
        if name:
            provides.setdefault(name, set()).add(p)

revdeps = {p: set() for p in pkgs}
deps_of = {}
for p, d in pkgs.items():
    groups = []
    for f in ("Depends", "Pre-Depends"):
        if d.get(f):
            groups += dep_groups(d[f])
    deps_of[p] = groups
    for g in groups:
        for alt in g:
            targets = ({alt} & set(pkgs)) | provides.get(alt, set())
            for t in targets:
                if t != p:
                    revdeps[t].add(p)

removed = set(s for s in SEEDS if s in pkgs)
changed = True
while changed:
    changed = False
    for p in pkgs:
        if p in removed or p not in auto:
            continue
        d = pkgs[p]
        if d.get("Essential") == "yes" or d.get("Priority") == "required":
            continue
        if revdeps[p] and revdeps[p] <= removed:
            removed.add(p); changed = True

violations = []
for p, groups in deps_of.items():
    if p in removed:
        continue
    for g in groups:
        sat = any((a in pkgs and a not in removed) or
                  any(q in pkgs and q not in removed
                      for q in provides.get(a, set()))
                  for a in g)
        if not sat:
            violations.append(f"{p} needs {' | '.join(g)}")

total_kib = sum(int(pkgs[p].get("Installed-Size", 0)) for p in removed)
print(json.dumps({
    "seeds_present": [s for s in SEEDS if s in pkgs],
    "seeds_missing": [s for s in SEEDS if s not in pkgs],
    "removed_count": len(removed),
    "removed_MiB": round(total_kib / 1024, 1),
    "violations": violations,
    "removed": sorted(removed),
}, ensure_ascii=False, indent=1))
