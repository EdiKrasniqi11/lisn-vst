# LISN StemSplitter: help sheet (info button, tabbed "How to use")

**Mockup:** `docs/design/Help.mockup.html` (A: the button; B–E: the four tabs). The mockup wins over this text.

## What it is

- A small **info button** in the window's bottom-right corner, under the panel. The user asked for it to be low-key rather than a highlighted feature, so it's not in the header.
  - It's a 16 px ring (1 px cream 0.35) around an "i" (cream 0.55), in a 24 px hit area at (716, 476). The ring is centred under the panel's right edge, in the 24 px bottom margin.
  - It looks the same on every screen and is never disabled.
  - When hovered, or lit while the sheet is open, the ring goes to cream 0.8 and the "i" to full cream.
- A **help sheet** that the editor shows inside the panel in place of the current screen.
  - A click on the info button opens it. The info button again, or the sheet's × button, closes it.
  - The screen underneath comes back exactly as it was.
  - Playback, the keys and file drops keep working while the sheet is open.
  - If the screen changes while it's open (for example a split finishes), the sheet stays open over the new screen.
  - Closing it doesn't reset the tab.

## The sheet (panel coordinates, 712 × 400)

- **Top row** (y 17, 36 tall, like the stems screen):
  - At x 19: "How to use" (Bold 15, cream) over "Click the plugin first so its keys work." (12, cream 0.64).
  - A `SegmentedPill` [Play | Stems | Loop | Export] (38 tall, y 16) ends 12 px before the × button.
  - The × button is a Ghost `LisnButton`, 34 × 34 at (659, 18), with a 16 px x icon.
- **Divider:** at y 63, x 19–693, cream 0.12, the same as the stems screen.
- **Four tips** share y 74–383 with 8 px gaps (71.25 px each), x 19–693.
  - **Card:** cream 0.05 with a 1 px cream 0.08 border, radius 14.
  - **Icon tile:** 40 px, cream 0.1, radius 12, at x+15. It holds a 20 px stroked icon (stroke 1.9).
  - **Text:** at the tile's right + 14, the title (Bold 14) over one line (12.5, cream 0.66).
  - **Right side:** the how-to parts, right-aligned 17 px from the card's right edge, 6 px apart, 26 px tall:
    - **key:** a solid keycap. Cream 0.1 fill, 1 px cream 0.4 border, radius 7, a 2 px cream 0.12 bottom edge, Bold 12 cream, 9 px padding.
    - **mouse:** a dashed cap (dash 3, gap 3, cream 0.36, radius 7) with a 13 px mouse icon, then Bold 12 in cream 0.86. The padding is 7 before the icon, 5 after it and 9 at the end.
    - **word** ("or", "+"): 12 px, cream 0.5.

## The tips

| Tab | Icon | Title | Line | How |
|---|---|---|---|---|
| Play | play | Play or pause | Every stem plays together, in sync | key Space |
| | jump | Jump and scrub | Click the waves to jump there, drag to scrub | mouse Click, mouse Drag |
| | home | Back to the start | Back to 0:00, or to the loop's start while looping | key Home |
| | zoom | Zoom and scroll | Zoom around the mouse; the wheel alone scrolls | key Ctrl + mouse Wheel |
| Stems | file | Split a song | Drop it on the panel or click Browse. 6 stems adds guitar and piano | mouse Drop |
| | speakerOff | Mute a stem | Click its light; stems 1 to 6 go top to bottom | mouse Light or key 1–6 |
| | speaker | Solo a stem | Only that stem plays; do it again to hear them all | mouse Right-click or key Shift + key 1–6 |
| | plus | New song | Start over with another song | mouse New song |
| Loop | loop | Make a loop | Ctrl+drag across the waves; the edges snap to the beat | key Ctrl + mouse Drag |
| | trim | Trim it | Drag either handle; hold Alt to go off the grid | mouse Handle or key Alt + mouse Drag |
| | toggle | Loop on or off | With no loop yet, you get 4 bars at the playhead | key L or mouse Loop |
| | grid | Finer steps | Zoomed in, the grid is quarter beats; zoomed out, bars | key Ctrl + mouse Wheel |
| Export | grip | Drag a stem | Drag its row onto any track in FL | mouse Drag row |
| | layers | Drag the mix | Every stem you hear, as one WAV | mouse Drag mix |
| | loop | Just the loop | While looping, every drag gives only the loop | key L + mouse Drag |
| | speakerOff | Leave stems out | Muted stems stay out of the mix | key 1–6 + mouse Drag mix |

## Not included

- Esc to close, closing with a click outside the sheet, a remembered tab across window reopenings, and per-screen help.

## Testing

- A `UiStateTests` check covers the toggle:
  - the info button opens the sheet, hides the screen and lights the button;
  - a screen change keeps the sheet open;
  - the × and the info button close it, and the screen comes back.
- The existing focus test covers the new buttons.
- Snapshots: the four tabs (one over the stems screen, one at 2x), checked against the mockup.
- The user's FL click-through.
