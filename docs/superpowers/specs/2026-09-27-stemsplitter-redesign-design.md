# LISN StemSplitter: redesign (layout A, Dusk + Midnight)

## Context
The first version works: you drop a song, demucs splits it and the stems appear as tiles you drag out. But:
- the UI looked plain;
- the model dropdown and the Python text field confused the user, and clicking either one crashed FL Studio;
- there was no way to hear a stem before dragging it.

The user approved layout **A · Dusk glass** from the mockup canvas (https://claude.ai/artifact/AtyCfHnYX2eS2c1xbmszx6). **Dusk** stays the default theme and **Midnight** is added as a second theme you can switch to. The waves should flow with the beat, but only if the plugin's performance stays the same.

The approved mockup sources are in `docs/design/*.dc.html` and are **the exact visual spec**: pixel positions, sizes, colours, fonts and the wave-generation JavaScript to port. Where this document and the mockup disagree, the mockup wins, except for the deliberate changes listed under "Differences from the mockup".

## Scope

### Screens (760 × 500 window, not resizable)
Every screen shares the wave background, the header and a frosted panel at (24, 76, 712 × 400) with 22 px corner radius.

1. **Drop** (`docs/design/Empty.dc.html`) is shown with no song loaded. It has a dashed drop zone, a "Browse…" button, the list of formats and "You'll get these stems" chips that follow the 4/6 switch. The dashed border brightens while a file is dragged over the window. Dropping a file anywhere on the window starts a split.
2. **Splitting** (`docs/design/Separating.dc.html`) shows:
   - the song name and "Splitting into vocals · drums · bass · other" (listing six stems in 6-stem mode);
   - the percentage in large type, with a wavy progress line and a knob;
   - a Cancel button, and the note about the first-run model download.
   The header switches are disabled and drawn at 55 % opacity.
3. **Stems** (`docs/design/Main.dc.html`):
   - **Top row:** the file icon, song name, "m:ss · split into N stems" and a "New song" button.
   - **One row per stem:** icon badge, name and file name, a play/pause button, the real waveform (unplayed part dim, played part in the stem colour, playhead), the current time and a "Drag" chip.
   - **Footer:** a hint line.
   - **Row size:** 4 stems use 62 px rows with 10 px gaps and a 40 px waveform; 6 stems use 42 px rows with 5 px gaps and a 26 px waveform.
   - **Stem order:** vocals, drums, bass, guitar, piano, other; unknown stems come last.
4. **Error** (`docs/design/PythonMissing.dc.html`) has three variants:
   - *Python not found:* "Python isn't set up yet", plus the `python -m pip install demucs` code row with Copy, "Try again" and "Already installed? Find python.exe…".
   - *Demucs missing:* "Demucs isn't installed", with body "Python is installed, but Demucs isn't. Run this once in a terminal, then try again:". The code row holds `"<found python>" -m pip install demucs`, followed by the same buttons.
   - *Split failed:* "The split didn't finish". The code box holds the last lines of demucs' output, with Copy and "Try again" but no python button.

A cancelled split returns to Drop.

### Header
- **Left:** "LISN" in Unbounded Bold 22 px with 0.08 em extra spacing, then "StemSplitter" in DM Sans Medium 14 px, aligned on the baseline with a 10 px gap.
- **Right:** two segmented pills 8 px apart, **[Dusk | Midnight]** then **[4 stems | 6 stems]**. They use the style of the mockup's 4/6 pill.
- **Theme switch:** changes the palette at once and is saved with the project.
- **4/6 switch:** changes the model (`htdemucs` / `htdemucs_6s`). On the Stems screen it re-splits the current song with the other model, so the cache usually makes this instant; on Drop it only changes the chips.

### Themes (exact values from the mockup's Waves/Main palettes)
| | Dusk (default) | Midnight |
|---|---|---|
| ground | #6F7679 | #152238 |
| layers, back → front | #D9775F #F4A76F #7B5467 #D95F6B #F08A7C | #2C4A78 #4F86B8 #101A2E #35679C #86B6DD |
| layer shadow | rgba(36,14,28,.42) | rgba(2,6,18,.5) |
| panel tint | rgba(32,18,27,.66) | rgba(8,14,28,.66) |
| pill tint | rgba(32,18,27,.72) | rgba(8,14,28,.72) |
| vocals / drums / bass | #F4A76F #EF7A7F #C9A0BD | #F3B98A #6FD3C6 #A9B4FF |
| guitar / piano / other | #F3CD7F #EFE3D6 #A9B8BC | #F4D58D #E3ECF7 #8DB8DB |

In both themes the text is cream (#FFF4EA) and the ink (dark text on cream buttons) is #2B1822. The accent is the vocals colour; it's used for the progress line.

### Fonts
Unbounded Bold, plus DM Sans Regular, Medium and Bold, are built into the plugin (`Resources/fonts`, SIL OFL). Mockup `font-size: Npx` maps to the JUCE em size (`FontOptions::withPointHeight`). Code uses Consolas.

### Stem preview
- The play button plays that stem through the plugin's own output, added on top of whatever passes through the track. Only one stem plays at a time; starting another pauses the current one.
- Each row remembers its own position. Clicking or dragging on a waveform seeks.
- When a stem reaches the end it stops and its row resets to 0:00.
- Previews resample to the host's sample rate.
- Starting a split, loading a new song or closing the plugin stops the preview.

### Beat motion
The waves move only while a preview is playing, then settle back and stop. Each audio block records the preview's peak level, and the UI turns it into a pulse with a fast attack and a slow release:
- `pulse = peak > pulse ? peak : pulse + (peak − pulse)·min(1, dt·5)`
- `flow += dt·(8 + 60·pulse)`
- after the preview stops, `pulse *= 0.05^dt` until it falls below 0.01.

For each layer:
- **Flow** slides the ripples along the layer's edge by `fmod(flow·speed, wavelength)` px.
- **Beat** pushes the layer outward along the edge normal by `pulse·push`.

The mockup's shape table (xTop, xBottom, amp, wavelength, phase, speed, push) is ported exactly.

### Python handling (no text field)
- **Finding Python:** the plugin searches automatically, in this order:
  1. the saved path (the user's pick or the last Python that worked);
  2. every `python.exe` on PATH, skipping the Microsoft Store `WindowsApps` stub;
  3. `%LOCALAPPDATA%\Programs\Python\Python3*\python.exe`, `%ProgramFiles%\Python3*\python.exe`, `<system drive>\Python3*\python.exe`, `%USERPROFILE%\anaconda3|miniconda3\python.exe`, newest version first.
- **Checking a candidate:** each one runs `-c "import importlib.util,sys;sys.exit(0 if importlib.util.find_spec('demucs') else 3)"`. This doesn't import torch, so it's fast. Exit 0 means ready and exit 3 means Demucs is missing; anything else, or no exit within 10 s, means it isn't a usable Python.
- **Choosing:** the first ready Python wins. If none is ready, the first "Demucs missing" one produces the Demucs-missing error; if none of those exists either, it's the Python-missing error.
- **Where it runs:** the search happens on the split's worker thread and can be cancelled.
- **Remembering:** the Python that worked is saved in the project state.

## Must hold
- **Nothing that crashed FL:**
  - no `ComboBox`, `TextEditor`, `PopupMenu` or `ProgressBar`;
  - no component takes keyboard focus (`setWantsKeyboardFocus(false)`, and Buttons also get `setMouseClickGrabsKeyboardFocus(false)`), so FL's spacebar and other shortcuts keep working;
  - file pickers are async `juce::FileChooser` calls.
- **Audio performance is unchanged:**
  - with no preview playing, `processBlock` returns straight away with no allocation, lock or extra work;
  - with a preview playing, the only additions are the transport read, mixing it in and one `getMagnitude` per block.
- **Drawing performance:**
  - with no motion, nothing redraws;
  - motion runs at no more than 30 fps and repaints only the strips around the panel plus its four corner squares;
  - wave layers are pre-rendered once per scale (shape and shadow masks, coloured at draw time) and only moved per frame, with no per-frame path fills, shadows or blurs;
  - the frosted panel is a static blurred copy;
  - bench budget: one motion frame at 1× on the software renderer averages ≤ 4 ms.
- **State:** `theme`, `model`, `python`, `input` and `stems` are saved and restored, and old projects still load.
- **Stability:** pluginval `--strictness-level 5` passes for Debug and Release.

## Differences from the mockup (deliberate)
- **Frosted panel:** it stays still while the waves move, because blurring every frame costs too much. The mockup already draws it this way.
- **Header:** the theme pill is new. It uses the same style as the 4/6 pill, and the mockup Main board is updated to include it.
- **Panel tint:** each screen keeps the mockup's alpha (Drop 0.60, Error 0.70, others 0.66) in the theme's colour.
- **No saturation boost:** the "saturate(1.15)" on the frosted panel is dropped.

## Testing
- **Unit tests** (`lisn_tests`, a JUCE `UnitTest` runner, run through ctest):
  - theme lookup;
  - wave geometry, including the check that sliding along the edge equals a phase shift;
  - the frost blur;
  - peaks from a WAV built in memory;
  - Python candidate order, using a fake directory tree;
  - the check result for a bogus executable;
  - preview playback and seek, resampling from 44.1 kHz to 48 kHz, peak metering and unchanged pass-through;
  - state round-trip;
  - `uiStateFor` mapping.
- **Snapshots:** `lisn_tests --snapshots <dir>` renders every screen in both themes (at 1×, plus 2× for two of them) to PNG, using synthetic stems.
- **Bench:** `lisn_tests --bench` prints the motion frame time, full paint time and `processBlock` time with the preview idle and playing, and fails if the frame budget is exceeded.
- **pluginval** for Debug and Release, plus a standalone build (`FORMATS VST3 Standalone`) for quick checks without FL.
