# LISN logo: usage guide

**Idea:** one sound enters the disc, opens into its parts (vocals, drums, bass, other) and leaves as one again.
LISN works inside the sound. The mark isn't tied to stem splitting, so it can cover every future LISN product.

## Files

| Use | File |
|---|---|
| Primary lockup | `lisn-horizontal.svg` |
| Stacked lockup (square spaces, splash screens) | `lisn-stacked.svg` |
| Symbol alone | `lisn-symbol.svg` |
| On dark backgrounds | `*-reversed.svg` |
| The app's headers and the website | `lisn-symbol-glass.svg`, followed by "LISN" as live Unbounded Bold text (`lisn-horizontal-glass.svg` is the same lockup with outlined lettering, for images) |
| One colour (print, engraving, fax, embroidery) | `lisn-symbol-mono.svg`, `lisn-horizontal-mono.svg`, `one-colour/` |
| Small sizes (16–24 px) | `lisn-symbol-small.svg` (3 strands, heavier wave) |
| App icon | `lisn-symbol.svg` with nothing behind it, and `lisn-symbol-small.svg` at 16 and 24 px: the PNGs and `lisn.ico` in `app/`. On dark backgrounds the ink disc and tails fade and the strands carry it; the owner chose that over a tile on 2026-10-05. |
| Web | `web/` (favicon.ico, favicon.svg, PWA icons, `head-snippet.html`) |
| Wordmark alone | `lisn-wordmark.svg` |

`build.py` regenerates every master. Everything is a filled outline: no strokes, text, clips or effects.

## Where the app, installers and README get the logo

They point at the files in this kit, so the kit stays the single source. Change a file here and rebuild; don't copy it into the code. The build reads these files, so `docs/design/logo/kit` must stay in git.

| Where | File | Set in |
|---|---|---|
| LISN.exe and the VST3: taskbar, Alt-Tab, Start menu, Explorer, Open with, the plugin folder's icon | `app/lisn-app-icon-16.png` and `app/lisn-app-icon-1024.png` | `ICON_SMALL` and `ICON_BIG` in `CMakeLists.txt`. JUCE makes the exe's .ico when CMake configures, each size from the smallest image at least that big: 16 px from the small cut, 32, 48 and 256 px from the 1024. Re-run `cmake -S . -B build -A x64` after changing them. |
| Setup and the uninstaller, both installers | `app/lisn.ico` | `SetupIconFile` in `installer/lisn-desktop.iss` and `installer/lisn-vst.iss` |
| Installer wizard, top right corner | `app/lisn-app-icon-256.png` | `WizardSmallImageFile` in both .iss files (PNG needs Inno Setup 6.5.2 or later) |
| Add/Remove Programs | Desktop app: LISN.exe's icon. VST3: the uninstaller's icon, which is `app/lisn.ico` | `UninstallDisplayIcon` in both .iss files |
| In-app header: the desktop app's title bar and the VST3's header | `lisn-symbol-glass.svg` | `juce_add_binary_data(LisnAssets ...)` in `CMakeLists.txt` builds it in as `BinaryData::lisnsymbolglass_svg`. `Header` in `Source/Screens.cpp` draws it at 32 px, then "LISN" in Unbounded Bold 22 px. Rebuild after changing it. |
| README | `lisn-horizontal.svg`, and `lisn-horizontal-reversed.svg` in GitHub's dark mode | `<picture>` at the top of `README.md` |
| Website header, hero mock, title-bar mock and footer (the lisn-site repo) | A copy of `lisn-symbol-glass.svg`, before the "LISN" text | `assets/lisn-symbol-glass.svg` in lisn-site, used by `index.html` (class `brand-symbol`). Update the copy when the kit changes. |
| Website (the lisn-site repo) | Copies of `web/favicon.ico` and `web/apple-touch-icon.png`, and `web/favicon.svg` with a dark-mode style that switches to the reversed colours | `assets/` in lisn-site. It's a separate repo, so these are copies: update them when the kit changes. |

In-app header and website header: the glass symbol (`lisn-symbol-glass.svg`) followed by "LISN" as live Unbounded Bold text, 0.08 em letter spacing: option A in `docs/design/desktop/HeaderLogo.dc.html`. The owner chose it on 2026-10-03 after rejecting the custom lowercase "lisn" wordmark. That wordmark has been removed from the kit.

## Colour

| Name | HEX | RGB | CMYK (approx.) |
|---|---|---|---|
| Ink | #2B1822 | 43 24 34 | 0 44 21 83 |
| Cream | #FFF4EA | 255 244 234 | 0 4 8 0 |
| Plum (reversed disc) | #5E3A4D | 94 58 77 | 0 38 18 63 |
| Drums | #EF7A7F | 239 122 127 | 0 49 47 6 |
| Vocals | #F4A76F | 244 167 111 | 0 32 55 4 |
| Bass | #C9A0BD | 201 160 189 | 0 20 6 21 |
| Other | #A9B8BC | 169 184 188 | 10 2 0 26 |

The CMYK values are conversions. Get a printer's proof (or match Pantone) before the first print run.
The strand order is fixed, top to bottom: drums, vocals, bass, other (the app's Dusk stem colours). Future products may recolour the strands but keep the disc and wave.

## Backgrounds

- Light (white, cream, pale photos): the full-colour mark.
- Dark (ink, black, dark photos): the reversed version, with a plum disc and cream wave and wordmark.
- Coloured dark backgrounds that change (the app's Dusk and Midnight headers, the website): the glass version. Its disc is cream at 20 % opacity, so it takes on the background's colour while the strands keep the stem colours.
- Busy or mid-tone backgrounds: one-colour ink or white, or place the mark on a cream tile.

## Clear space and minimum size

- **Clear space:** keep a margin of half the disc's radius on every side. Nothing else goes inside it.
- **Minimum size:** use the symbol at 24 px and above, and switch to `lisn-symbol-small.svg` from 16 to 24 px. The horizontal lockup needs at least 96 px of width.

## Type

The wordmark is "LISN" in **Unbounded Bold**, capitals, letter-spaced 0.08 em: the same lettering as the app's header. In the kit's files it is outlined (build.py holds the four letters), so it renders the same everywhere. In the app and on the website it is live text in that font.
Companion fonts, both OFL and already in `Resources/fonts`: **Unbounded Bold** for headlines and **DM Sans** for text.

## Don't

- Don't stretch, tilt or rotate it.
- Don't distort the disc: it stays a perfect circle.
- Don't reorder, add or remove strands. The 3-strand small cut is only for small sizes.
- Don't put the full-colour mark on a dark background. Use the reversed version.
- Don't add shadows, glows, gradients or outlines.
- Don't separate the wave's tails from the disc.
- Don't set LISN in any other font, in lowercase, or without the 0.08 em letter spacing.

## Open items

- Trademark: get a professional search done (EUIPO / USPTO / WIPO, plus a reverse image search) before you register or print it.
- The strand curves are built from sampled points: about 340 anchors, where hand-drawn logos have around 50. They render smoothly, but a designer could redraw them with fewer points in Illustrator or Figma if exact print masters (AI, PDF, EPS) are needed.
