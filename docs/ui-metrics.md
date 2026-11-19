# UI metrics — the implementation source for the skins

All numbers below come from the 2026-11-19 web research (sources in
the research report; recreations and conflicts are flagged). Skins
are implemented FROM THIS TABLE, never from recollection. Panel is
854×480, 1× logical pixels.

## macOS (Sonoma/Sequoia)
- Menu bar 24 pt; menu-extra working area 22 pt
- Dock tile 48 pt default, magnification off; frosted glass; divider before Downloads/Trash
- Traffic lights: 12 px diameter, centres 18/38/58 in a 28 pt title bar; #FF5F57 / #FEBC2E / #28C840
- Window corner radius 10 px; plain title bar 28 pt
- Launchpad: 7 cols × 5 rows, search top-centre, page dots bottom-centre

## Windows 11
- Taskbar 48 px; taskbar icons 24×24; buttons 44 wide; Start pins 32 px
- Start ≈640×720 @100% — does NOT fit 480 px height: use a scaled layout (same structure, compressed)
- Window radius 8 px (controls 4 px); title bar 32 px; caption buttons 46×32; close hover #C42B1C
- Tray L→R: chevron, Wi-Fi/volume/battery cluster, clock+date (2 lines), bell, show-desktop strip
- No official acrylic/Mica hex exists; light taskbar ≈ #F3F3F3 is a third-party approximation

## Windows XP (Luna)
- Taskbar 30 px; title buttons 21×21; menu highlight #316AC5; Tahoma UI font
- Taskbar gradient (recreation): #3168D5→#1941A5; Start green (recreation): #388238→#307443, slanted right edge, "start" bold-italic white with shadow
- Start menu ≈380 wide, height dynamic; header ≈65 px with 48×48 user picture; right-column icons 24×24
- Quick Launch default: IE, Media Player, Show Desktop; task buttons ≈150–160 px
- Clock: time only (date in tooltip); tray is a lighter shade than the taskbar

## Mac OS 9 (Platinum)
- Menu bar 20 px, #DDDDDD, 1 px black bottom line; menus #EEEEEE, hard 2 px shadows
- Square corners; title bar ≥19 px with pinstripes (2 px #FFFFFF/#777777 on #DDDDDD, active only); title bold 12 px centred
- Boxes 13×13: close top-LEFT, zoom top-right, collapse far top-right; scrollbars 15 px
- Desktop icons 32×32 down the right edge, Trash bottom-right
- Highlight conflict in sources: #333399 vs #3366CC (skin currently uses #3366CC)

## Windows 7 (Aero)
- Taskbar 40 px (30 small-icons); icons 32×32 in 52 px cells; Start orb 54×54 protruding ≈7 px above the bar
- Default pins: IE, Explorer, Media Player; running = glass frame; hover wash from icon colour
- Clock two lines at 40 px; Show Desktop strip at the extreme right edge
- Caption height ≈21 px, close ≈46–48 wide (sources conflict); close hover ≈#D9504E (recreation)
- Start menu is content-sized (no fixed dimensions); two columns, search box at the bottom of the left column, user picture 48×48 in a 64×64 frame overlapping the top-right
- Aero tint default "Sky"; no official hex exists

## Derived by generation (not directly sourced)
- Windows 95 / CE / Vista use the classic-shell taskbar height 30 px (XP's sourced value, same shell generation)
- Windows 8 / 10 use the superbar height 40 px (Win7's sourced value, same generation)
