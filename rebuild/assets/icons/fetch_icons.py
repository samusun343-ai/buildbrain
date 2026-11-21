#!/usr/bin/env python3
"""Fetch the icon SVGs listed in icons.tsv from their upstream
repositories into src/<set>/<name>.svg. Verifies each download
looks like an SVG. Run from assets/icons/."""
import csv
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def url_for(row):
    s, name = row["set"], row["name"]
    if s == "material":
        src = row["file"] or name
        if row["variant"] == "fill0":
            fn = "%s_24px.svg" % src
        else:
            fn = "%s_fill1_24px.svg" % src
        return ("https://raw.githubusercontent.com/google/"
                "material-design-icons/master/symbols/web/%s/"
                "materialsymbolsrounded/%s" % (src, fn))
    if s == "fluent":
        return ("https://raw.githubusercontent.com/microsoft/"
                "fluentui-system-icons/main/assets/%s/SVG/"
                "ic_fluent_%s_24_%s.svg"
                % (row["dir"], row["file"], row["variant"]))
    if s == "papirus":
        return ("https://raw.githubusercontent.com/"
                "PapirusDevelopmentTeam/papirus-icon-theme/master/"
                "Papirus/64x64/%s/%s.svg" % (row["dir"], name))
    raise ValueError(s)


def fallback_url(row):
    """Iconify mirror of the same MIT-licensed Fluent set (the
    upstream repo does not carry every icon under assets/)."""
    if row["set"] == "fluent":
        return ("https://api.iconify.design/fluent/%s-24-%s.svg"
                % (row["file"].replace("_", "-"), row["variant"]))
    return None


def fetch(url):
    req = urllib.request.Request(
        url, headers={"User-Agent": "brainux-icon-fetch"})
    return urllib.request.urlopen(req, timeout=30).read()


def main():
    ok, bad = 0, []
    with open(os.path.join(HERE, "icons.tsv")) as f:
        for row in csv.DictReader(f, delimiter="\t"):
            dst = os.path.join(HERE, "src", row["set"],
                               row["name"] + ".svg")
            if os.path.exists(dst) and os.path.getsize(dst) > 100:
                ok += 1
                continue
            try:
                data = fetch(url_for(row))
                if b"<svg" not in data[:512]:
                    # Papirus ships symlinks: the raw content is
                    # the target's relative path, not the SVG.
                    target = data.decode("utf-8", "replace").strip()
                    if row["set"] == "papirus" and target and \
                            len(target) < 80:
                        import urllib.parse
                        data = fetch(urllib.parse.urljoin(
                            url_for(row), target))
                if b"<svg" not in data[:512] or len(data) < 100:
                    raise ValueError("not an svg (%d bytes)"
                                     % len(data))
                with open(dst, "wb") as out:
                    out.write(data)
                ok += 1
            except Exception as e:  # noqa: BLE001
                fb = fallback_url(row)
                if fb:
                    try:
                        data = fetch(fb)
                        if b"<svg" not in data[:512] or \
                                len(data) < 100:
                            raise ValueError("not an svg")
                        with open(dst, "wb") as out:
                            out.write(data)
                        ok += 1
                        continue
                    except Exception:  # noqa: BLE001
                        pass
                bad.append("%s/%s: %s" % (row["set"], row["name"], e))
    print("fetched ok: %d, failed: %d" % (ok, len(bad)))
    for b in bad:
        print("  FAIL", b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
