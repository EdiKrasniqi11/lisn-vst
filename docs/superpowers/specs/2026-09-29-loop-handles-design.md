# LISN StemSplitter: loop handles, beat grid, zoom and keys

Follows `2026-09-29-stem-player-design.md` on the same branch (`feat/stem-player`, not merged yet). It adds:
- **loop handles:** after a drag creates a loop, its two edges can be dragged to trim it;
- **an adaptive beat grid** that the edges snap to (down to ¼ beat), with **Alt** to go off the grid;
- **Ctrl+wheel zoom** on the waveforms, so a ¼ beat is wide enough to hit;
- **keyboard control** (Space, L, 1–6, Home) while the plugin has focus;
- two look changes: **colour bars instead of stem icons**, and **no footer**.

**Mockup** (approved; wins over this text and over the older `StemPlayer.mockup.html` where they differ): `docs/design/LoopHandles.mockup.html`.

## Why zoom is needed

The waveform is 376 px wide. On a 3-minute song at 92 BPM one pixel is about 0.8 of a beat, so a ¼-beat grid can't be hit at whole-song scale. Zoom gives the handles real precision.

## Look

- **Stem marker:** the 32 px tinted badge with a line icon is replaced by a **4 × 30 px bar, radius 2, in the stem colour**, at the badge's x, vertically centred. The name column starts 10 px after the bar. Muted rows dim it with the name (alpha 0.38). The stem icons (`Icons::vocals`, `drums`, `bass`, `guitar`, `piano`, `other`) and `stemIcon()` are deleted.
- **No footer:** the helper line (`Click a light to mute · …`) and its play glyph go. Nothing else moves.
- **Loop band** (while looping): as today (`cream@0.10`, 2 px cream edges across every waveform), plus a **grab tab on each edge**: 10 × 18 px, radius 4, cream, two 1 px ink@0.55 grip lines, centred on the edge, its top 8 px above the band. The hovered tab grows to 12 × 22 px with a 3 px `cream@0.22` ring, and the mouse cursor becomes `LeftRightResizeCursor`.
- **Grid lines** (only while beats are at least 8 px apart): a 1 px line per beat at `cream@0.16` and per bar at `cream@0.34`, across the same span as the loop band, drawn over the waves and under the band.
- **Position strip** (only while zoomed): in the old footer's slot, a 4 px track, radius 2, `cream@0.10`, spanning the waveform x range; a `cream@0.55` thumb over the visible part of the song; and a 2 × 8 px accent tick at each loop edge while looping. It doesn't respond to the mouse.
- **Subtitle:** while looping and a BPM is known, it adds the loop length in beats: `1:20 / 3:12 · 92 BPM · loop 1:18–1:23 · 8 beats`. The length is rounded to 0.25 and printed without trailing zeros (`8 beats`, `10.25 beats`, `1 beat`).

## View (zoom and scroll)

- **One view for all rows:** a range of song fractions, `[v0, v1]`, owned by `StemsScreen`. The whole song is `[0, 1]`. New stems (a new song or the 4/6 switch) reset it. It isn't kept when the window closes.
- **Ctrl+wheel** over any waveform zooms around the cursor: each notch multiplies the view width by 0.8 (in) or 1.25 (out). The width is clamped between **2 seconds** (or the whole song, if shorter) and the whole song, and the view stays inside `[0, 1]`.
- **The plain wheel** (and a trackpad's horizontal swipe) scrolls a zoomed view by 10 % of its width per notch. At whole-song zoom it does nothing.
- **Everything maps through the view:** the bars, the played colour, the playhead, the loop band, the grid, clicks and drags. A playhead or loop edge outside the view isn't drawn. The view doesn't follow the playhead.
- **Waveform detail:** the peak job computes a **100 Hz peak envelope** per stem (the max level per 10 ms) instead of 104 bars. Each row builds its visible bars from the envelope: the max over each bar's slice of the view. There are the same 104 (or 96 compact) bars at any zoom. This is about 19 k floats per stem for a 3-minute song. At the 2-second limit each bar still covers about 2 envelope frames.

## Grid and snapping

- **The step** is the finest of **¼ beat, 1 beat and 1 bar (4 beats)** that is at least **4 px** wide at the current zoom. If even a bar is narrower, the step is a bar.
- **Bars count 4 beats from `firstBeat`.** This is a `ponytail:` corner-cut that assumes 4/4. A time-signature setting is the upgrade path.
- **Snapping** rounds an edge to the nearest `firstBeat + k·step`. With no BPM there is no grid and no snapping, as today.
- **Alt** held during a drag turns snapping off for that edge: it follows the mouse exactly. The modifier comes from the mouse event, so it needs no keyboard focus.

## Mouse on the waveforms

- **Click** (under 4 px of movement) seeks, as today.
- **Hovering** within 6 px of a loop edge, while looping is on, shows the resize cursor and the hot tab.
- **Dragging a handle** moves only that edge, snapped (or free with Alt). It stops one step short of the other edge; with Alt the minimum loop is 10 ms. The band follows the mouse, and the new range goes to the player on release (`onSetLoop`). Setting the loop re-seeks, so it isn't done live.
- **Dragging anywhere else** (4 px or more) creates a new loop, as today, with edges snapped to the current step. It replaces the old loop and turns looping on.
- **Moving the whole loop** by dragging its middle is not included. A drag inside the band makes a new loop.

## Keyboard

- **Focus:**
  - The editor takes keyboard focus when you click anywhere in it. A mouse listener on the editor catches every click, including clicks on buttons and rows, and calls `grabKeyboardFocus()`.
  - Child components still never take focus themselves.
  - Clicking FL gives focus back to FL. This replaces the old rule "nothing takes keyboard focus".
- **Keys the editor handles** (it consumes them):

  | Key | Action |
  |---|---|
  | Space | play/pause the stems |
  | L | loop on/off (only when a loop range exists) |
  | 1 to 6 | mute/unmute stem N (a stem that doesn't exist is ignored) |
  | Shift+1 to 6 | solo stem N, like right-clicking its light |
  | Home | jump to the start, or to the loop start while looping |

- **Every other key returns false.** JUCE's Windows peer then posts it to the host window (`juce_Windowing_windows.cpp`, `forwardMessageToParent`), so FL shortcuts still work while the plugin has focus.
- **Claiming the key-down too:**
  - Windows delivers Space, letters and digits as a key-down followed by a character.
  - JUCE turns only the character into `keyPressed`. It forwards the key-down to the host unless `keyStateChanged` uses it.
  - So `keyStateChanged (true)` returns true while one of Space, L or 1–6 is down, and Ctrl or Alt isn't. Otherwise FL would also start its own transport on the same Space.
  - This is a `ponytail:` corner-cut: while one of those keys is held, any other key-down is claimed as well.

## Risks, checked early in FL

- **Focus:** FL has to let a VST3 editor take keyboard focus, and has to act on the keys JUCE forwards. The first plan task ships only the focus change and Space, and asks the user to check them in FL before the rest is built.
- **Ctrl+wheel and Alt:** FL may catch Ctrl+wheel or Alt itself. Alt is only read as a modifier during a drag.

## Testing

- **Unit tests:**
  - **Step choice:** at a given zoom and BPM the step is ¼ beat, 1 beat or 1 bar; there is no snapping without a BPM; Alt gives the raw position.
  - **Handles:** the hit zone is 6 px; an edge can't cross the other; a handle drag sends one `onSetLoop` on release, and a drag in the middle creates a new loop.
  - **View:** zooming keeps the fraction under the cursor still, and the clamps work (2 s minimum, never past the whole song, stays inside 0..1). Scrolling works only while zoomed. `headX` and the seek fraction map through the view.
  - **Envelope bars:** the visible bars are the max over their slice. A 4-segment test file (silence, 0.5, silence, 1.0) reads back per segment at whole-song zoom, and a zoomed view picks the right segment.
  - **Beats text:** `8 beats`, `10.25 beats`, `1 beat`, and nothing without a BPM.
  - **Keys:** each key does what the table says. An unhandled key (for example `A`) returns false.
- **Snapshots:** a zoomed loop with the hovered handle, the grid and the strip (Dusk, 4 stems); the whole song with handles (Midnight, 6 stems); and 2×. Read each PNG against the mockup.
- **Bench:** the Release motion frame stays ≤ 4 ms, and a zoom step (rebuilding every row's bars) is measured too.
- **FL click-through:**
  - Space in the plugin plays the stems, and Space after clicking FL plays FL.
  - L, 1–6, Shift+1–6 and Home work.
  - Ctrl+wheel zooms and the wheel scrolls.
  - Handle drags snap, and Alt drags don't.
  - Drag loop still delivers the trimmed loop.

## Not included

Moving the whole loop by dragging, a draggable position strip, following the playhead, time signatures other than 4/4, keeping the zoom across window reopenings, and a focus indicator.
