#!/bin/sh
# render_icons.sh - fetch the shell's status/navigation icon
# sources (established vector sets only - no hand-drawn glyphs)
# and render them to white-on-transparent PNGs at 96x96 with
# librsvg (via ffmpeg). The shell tints the white shape to the
# theme ink/accent/danger colour at load time and area-average
# scales it to the exact display box (tk_image_scale_to_fit),
# so what reaches the panel is a properly anti-aliased vector
# render, never a code-drawn raster glyph.
#
# Sources (fetched 2026-11-21):
#   material/  Google Material Symbols (Apache-2.0),
#              github.com/google/material-design-icons,
#              symbols/web/<name>/materialsymbolsoutlined/
#              <name>_fill1_24px.svg
#   fluent/    Microsoft Fluent UI System Icons (MIT),
#              github.com/microsoft/fluentui-system-icons,
#              assets/<Dir>/SVG/ic_fluent_<name>_24_regular.svg
#   aosp/      AOSP SystemUI navigation vectors (Apache-2.0),
#              frameworks/base packages/SystemUI/res/drawable/
#              ic_sysbar_{back,home,recent}.xml, converted from
#              Android vector XML to SVG by tools/aosp2svg.py
#   fluent/start.png: the Windows logo (public domain, simple
#              geometric shapes), Wikimedia Commons
#              "Windows logo - 2021.svg", recoloured monochrome.
#
# Output: assets/status/<set>/<name>.png (96x96, white shape,
# transparent background) + the kept SVG sources under
# assets/svg-src/<set>/ for provenance.
# Re-running is idempotent; delete assets/status to rebuild.
set -eu
cd "$(dirname "$0")/.."
SRC=assets/svg-src
OUT=assets/status
mkdir -p "$SRC/material" "$SRC/fluent" "$SRC/aosp" \
         "$OUT/material" "$OUT/fluent" "$OUT/aosp"

MAT="https://raw.githubusercontent.com/google/material-design-icons/master/symbols/web"
FLU="https://raw.githubusercontent.com/microsoft/fluentui-system-icons/main/assets"
AOSP="https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/master/packages/SystemUI/res/drawable"

fetch() { # fetch <url> <dest>
    if [ ! -s "$2" ]; then
        echo "GET $1"
        curl -fsSL "$1" -o "$2"
    fi
}

# --- Material Symbols (fill1 = the filled Material style) ---
for n in battery_android_0 battery_android_1 battery_android_2 \
         battery_android_3 battery_android_4 battery_android_5 \
         battery_android_6 battery_android_full battery_android_bolt \
         battery_android_frame_full battery_alert \
         signal_wifi_0_bar network_wifi_1_bar network_wifi_2_bar \
         network_wifi_3_bar network_wifi wifi_off \
         volume_mute volume_down volume_up volume_off \
         brightness_6 account_circle apps pan_tool image settings; do
    fetch "$MAT/$n/materialsymbolsoutlined/${n}_fill1_24px.svg" \
          "$SRC/material/$n.svg"
done

# --- Fluent UI System Icons (24 regular) ---
# The REGULAR weight is used for the whole WinUI tray: one
# consistent stroke style, and its battery family reads
# conventionally (outline + fill growing from the left). The
# FILLED battery family was measured to do the opposite as a
# standalone glyph (solid body with a window that grows with
# the level - battery_0 fully solid), so it is not used.
fluent() { # fluent <Dir URL-encoded> <file base>
    fetch "$FLU/$1/SVG/$2.svg" "$SRC/fluent/${2#ic_fluent_}.svg"
}
for i in 0 1 2 3 4 5 6 7 8 9 10; do
    fluent "Battery%20$i" "ic_fluent_battery_${i}_24_regular"
done
fluent "Battery%20Charge" ic_fluent_battery_charge_24_regular
fluent "Battery%20Warning" ic_fluent_battery_warning_24_regular
for i in 1 2 3 4; do
    fluent "WiFi%20$i" "ic_fluent_wifi_${i}_24_regular"
done
fluent "WiFi%20Off" ic_fluent_wifi_off_24_regular
fluent "WiFi%20Warning" ic_fluent_wifi_warning_24_regular
for i in 0 1 2; do
    fluent "Speaker%20$i" "ic_fluent_speaker_${i}_24_regular"
done
fluent "Speaker%20Off" ic_fluent_speaker_off_24_regular
fluent "Speaker%20Mute" ic_fluent_speaker_mute_24_regular
fluent "Weather%20Sunny" ic_fluent_weather_sunny_24_regular
fluent "Person" ic_fluent_person_24_regular
fluent "Hand%20Right" ic_fluent_hand_right_24_regular
fluent "Image" ic_fluent_image_24_regular
fluent "Settings" ic_fluent_settings_24_regular

# Windows logo for the Start button (public domain mark)
fetch "https://upload.wikimedia.org/wikipedia/commons/8/87/Windows_logo_-_2021.svg" \
      "$SRC/fluent/start.svg" || \
    echo "WARN: windows logo fetch failed (start icon skipped)"

# --- AOSP SystemUI navigation vectors -> SVG ---
for n in back home recent; do
    fetch "$AOSP/ic_sysbar_$n.xml" "$SRC/aosp/nav_$n.xml"
    python3 tools/aosp2svg.py "$SRC/aosp/nav_$n.xml" "$SRC/aosp/nav_$n.svg"
done

# --- normalize to white + render at 96x96 ---
# librsvg rasterizes at the SVG's own width/height, so the
# width/height are rewritten to 96 (viewBox untouched) and all
# fills are forced to white: the shape's alpha is the icon.
render() { # render <svg> <png>
    sed -e 's/fill="#[0-9A-Fa-f]*"/fill="#ffffff"/g' \
        -e 's/fill="#[^"]*"/fill="#ffffff"/g' \
        -e 's/width="[0-9.]*"/width="96"/' \
        -e 's/height="[0-9.]*"/height="96"/' \
        "$1" > /tmp/icon_norm.svg
    # Material Symbols paths carry no fill: default black.
    # Give the root an explicit white fill (paths inherit).
    if ! grep -q 'fill=' /tmp/icon_norm.svg; then
        sed -i 's/<svg /<svg fill="#ffffff" /' /tmp/icon_norm.svg
    fi
    # Sources without width/height (the Windows logo) get an
    # explicit raster size, or librsvg falls back to 100x100.
    if ! grep -q 'width=' /tmp/icon_norm.svg; then
        sed -i 's/<svg /<svg width="96" height="96" /' /tmp/icon_norm.svg
    fi
    ffmpeg -y -v error -i /tmp/icon_norm.svg "$2"
    python3 - "$2" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1])
assert im.size == (96, 96), (sys.argv[1], im.size)
EOF
}
for f in "$SRC"/material/*.svg; do
    render "$f" "$OUT/material/$(basename "${f%.svg}").png"
done
for f in "$SRC"/fluent/*.svg; do
    n=$(basename "${f%.svg}")
    n=${n%_24_regular}
    render "$f" "$OUT/fluent/$n.png"
done
for f in "$SRC"/aosp/nav_*.svg; do
    render "$f" "$OUT/aosp/$(basename "${f%.svg}").png"
done
echo "rendered: $(ls "$OUT"/material/*.png "$OUT"/fluent/*.png "$OUT"/aosp/*.png | wc -l) icons"
