# LISN: volume, speed, opening animation and the desktop look (sub-project 2)

## Context

Sub-project 1 (`2026-10-01-desktop-app-design.md`, merged) shipped LISN, the desktop app: the plugin's Standalone build in a native window, with its own per-user installer. Using it, the owner asked for:
1. a title bar that fits the wave design instead of Windows' white bar;
2. an Export button that saves to a folder, instead of drag chips, in the desktop app;
3. an opening animation where the waves flow in.

The old desktop plan (`2026-09-29-desktop-app.md`) also listed per-stem volume and a speed control.

All of them are in this spec. Volume, speed and the opening animation are **shared**: the VST gets them too, from the same code. The title bar and Export are **desktop only**.

## Decisions (2026-10-01)

- **Approved boards** on the canvas https://claude.ai/artifact/WZAorMYikyGdajfTXVJp2D, copied into `docs/design/desktop/`. Where this text and a board disagree, the board wins.
  - **1A:** the header is the title bar.
  - **2C:** a volume bar under each stem name and a speed strip at the bottom. Its 6-stem and VST variants are approved too.
  - **3A/3B:** the Export sheet, with its saving states.
  - **4A:** the waves flow in from the sides.
- **Speed keeps the key** (time-stretch), from 0.5x to 1.5x, using **Signalsmith Stretch 1.4.0** (MIT) and its **Signalsmith Linear** dependency (MIT). Rejected: SoundTouch (LGPL: a separate DLL plus relinking obligations) and Rubber Band (GPL or paid).
- **The VST gets the same speed strip.** Its dragged mix carries the speed.
- **The intro plays the first time a window opens for a processor instance.** That's every desktop app start, and in a DAW the first open after loading the project. A click skips it, and it's skipped when Windows' animation effects are off.
- **Single stems are always the originals; the mix is what you hear.**
  - Exported or dragged single stems never get volume or speed.
  - "My mix" and "Drag mix" get mutes, volumes, speed and the loop.

## Shared: the engine (`StemPlayer`)

### Volume
- `setVolume (int stem, float 0..1)` / `getVolume (int)`: an atomic per stem.
- In `addTo`, the level each stem fades to is `want && ! muted ? volume : 0`. It uses the existing ramp across one block, so a change is heard within one block, never clicks, and mute keeps its meaning.
- `load()` (a new song, or the 4/6 switch) resets every volume to 1, like the mutes.

### Speed
- `setSpeed (double)`, clamped to 0.5..1.5, and `getSpeed()`: an atomic.
- **At exactly 1.0:** the stretcher is bypassed and `addTo` is the current path, bit for bit.
- **Otherwise, per output block of n samples:**
  - pull `round (n * speed + carry)` samples from the transport;
  - mix them to stereo with the stem levels;
  - `process` them through one stereo stretcher into exactly n output samples.
  One stretcher handles any number of stems.
- **Set-up:** the stretcher is configured in `prepare()` for 2 channels at the host rate (its default preset). `scratch` and the mix buffers are sized there for `ceil (maxBlock * 1.5) + 1` samples. **Nothing allocates on the audio thread.**
- **Engaging and seeks:** the stretcher engages the moment the speed leaves 1.0 (reset, so its first ~60 ms fade in from its pre-roll; engaging mid-play, the direct audio fades out over that first block instead of stepping to silence) and stays engaged, even back at 1.0, until the next `play()` or seek to another position; then 1.0 plays directly again. A seek to another position while stretching resets it, so no old audio smears across the jump; a loop edit that keeps the playhead does not reset it. A loop wrap is a seamless splice in its input, so it needs no reset. Pause fades the stretched output over one block.
- **Position:** the position and playhead stay the transport's input position.
  - `// ponytail:` while stretching, what is heard trails the playhead by the stretcher's latency (about 0.1 s). Upgrade path: shift the reported position back by that latency times the speed.
- **Reset:** `load()` resets the speed to 1.0.
- **The redesign spec's "Must hold" list still binds:**
  - a stopped player's `processBlock` returns after two relaxed loads;
  - `transport.stop()` is never called;
  - play and pause stay atomic flags.

### Rendering
- `render` takes per-stem gains (0 = left out, replacing the audible mask), `startSec` and `endSec`, a speed, `dest`, a progress callback (0..1) and a should-cancel function.
- **At speed 1.0** it sums `gain * stem`, as today.
- **At other speeds** it stretches offline in the library's exact-length mode (`seek` at the start, `flush` at the end). The output is `(endSec - startSec) / speed` long.
- It writes a 24-bit WAV at the stems' rate to `dest.part`, then renames it.
  - A cancel or failure deletes `.part` and returns false.
  - A `dest` that already exists is reused (renders are cached by name).
- **Cache names** (editor `renderFile`) encode:
  - the audible stems' volumes (whole percent);
  - the speed (hundredths);
  - the loop range (hundredths of a second, as now).
  So a changed setting never reuses an old render.
- **Background renders** run on a `juce::Thread` owned by the component that started it (the Export sheet or the VST's stems screen), at low priority. That component cancels and joins the thread in its destructor.

## Shared: the UI

### Rows (`StemRow`)
- **Volume bar** (`VolumeBar`, a new custom component in `Controls`):
  - about 96 x 3 px under the stem name, filled in the stem colour on a `cream@0.16` track, with a 10 px cream thumb;
  - drag to set the volume; double-click resets to 100%;
  - `setWantsKeyboardFocus (false)`, so it never takes focus.
- **Sizes:**
  - **4 stems:** rows 56 px (were 62), waveform 36 px (was 40), mute light 38 px.
  - **6 stems:** rows 36 px (were 42), waveform 24 px (was 26), mute light 30 px, name 13 px.
  - The gap between rows is 8 px for 4 stems and 6 px for 6.
- **Waveform width:**
  - **VST:** 376 px plus the Drag chip, as now.
  - **Desktop:** no chip, so 472 px for 4 stems and 480 px for 6. The bar count scales with the width (about 130 and 123) to keep today's spacing.
  - The stems screen's playhead, loop band, grab tabs, grid and position strip follow the waveform's x and width.

### Speed strip (`SpeedStrip`, a new component in `Screens`)
- A row under a divider at the bottom of the stems panel, 30 px high:
  - the speed icon plus "Speed" in a 118 px label column;
  - a custom slider whose 4 px track fills in the accent colour from the 1x centre to the value, with a 14 px cream thumb;
  - tick labels 0.5x, 0.75x, 1x, 1.25x and 1.5x under it;
  - the value ("1.15x"), "same key", and a small Ghost **Reset** button.
- Behaviour:
  - dragging steps by 0.05 and sticks at 1.0 for a few pixels;
  - a double-click on the slider resets to 1.0;
  - nothing takes focus.
- It calls `player.setSpeed`. The value reads back from the player every tick, so it survives the window closing and reopening.

### Top bar
- **Desktop:** Play, title, Loop, **Export** (a Ghost `LisnButton` with a download icon), New song.
- **VST:** Play, title, Loop, the **Drag mix / Drag loop** chip, New song.
- The subtitle is unchanged.

### Opening animation (`WaveBackground` plus the editor), board 4A
- **Waves:** `WaveBackground::setIntro (float seconds)`. Each of the 5 wave layers starts 820 px (the width plus 60) off-screen, from alternating sides, and eases to its resting offset over 1.25 s, with an ease-out like `cubic-bezier(.22,1,.36,1)`. Each layer starts 0.06 s after the one behind it (board 4A).
- **Panel:** fades in and rises 18 px over 0.7 s from 1.05 s. Its image's alpha is drawn by `WaveBackground`.
- **Header and screen:** the screen sits in the panel and fades and rises with it (board 4A). The header and the info button fade in over 0.6 s from 1.3 s (`setAlpha`). The panel's `scale(.985)` is dropped: scaled frames cost 18-23 ms. The whole intro takes about 1.9 s.
- **Frame rate:** a `juce::VBlankAttachment` drives the intro at the display rate. After the intro it is removed and the editor's 30 Hz timer carries on as now. Intro frames only blit the cached layers at offsets, so the bench's motion-frame budget (4 ms) applies to them too.
- **When it plays:** the processor's `introShown` flag (not saved in the state) is set on the first editor. Later editors for that processor start at the end state.
  - A mouse click during the intro jumps to the end.
  - On Windows, `SystemParametersInfo (SPI_GETCLIENTAREAANIMATION)` false skips it.
- **Instant window:** startup_ms (editor construction plus a full paint, Direct2D warmed) at or under 300 ms. Measured: the restored song's load is about 10 ms and stays in the constructor; the cost was the drag chips' `DraggingHandCursor` (about 150 ms warm), so they use the pointing hand. The cold Direct2D start (0.25 to 1.4 s) belongs to the platform.

### Help sheet
The layout is unchanged: four tips per tab, with new text.
- **Play tab:**
  - "Zoom and scroll" becomes **Change the speed**: "Drag the Speed strip; the key stays the same", [mouse "Speed"] or [mouse "Reset"].
- **Loop tab:**
  - "Finer steps" becomes **Zoom for finer steps**: "Zoom around the mouse; the grid goes down to quarter beats", [key Ctrl] + [mouse Wheel].
- **Stems tab:**
  - "New song" becomes **Set a stem's volume**: "Drag the bar under its name; double-click for 100%", [mouse "Drag bar"].
- **Export tab, VST:** as now, except "Drag the mix" reads "Every stem you hear, with its volume and the speed, as one WAV".
- **Export tab, desktop:**
  - **Export the mix:** "What you hear, as one WAV" [mouse "Export"].
  - **All stems:** "Every stem as a WAV, in one folder" [mouse "Export"].
  - **Just the loop:** "While looping, My mix saves only the loop" [key L] + [mouse "Export"].
  - **Leave stems out:** "Muted stems stay out of My mix" [key 1-6] + [mouse "Export"].

## Desktop only

The editor knows it is the desktop app when `processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone`.

### Title bar (board 1A)
- **Window** (`Source/StandaloneApp.cpp`):
  - `MainWindow` stops using the native title bar: `setUsingNativeTitleBar (false)`, title bar height 0, no border, drop shadow on.
  - On Windows 11 it asks DWM for rounded corners (`DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND` on the peer's HWND).
  - Alt+F4 and the taskbar entry keep working.
- **Header:**
  - it reads **LISN** only;
  - after the pills, a 1 px separator and two 32 px round buttons, Minimise and Close (`cream@0.18` border, `rgba(32,18,27,.55)` fill).
- **Wiring:** the editor gets `onMinimise` and `onClose` callbacks, and the buttons exist only when the app sets them. `StandaloneApp` connects them to `setMinimised (true)` and `systemRequestedQuit()`.
- **Moving:** a drag on empty header space moves the window, through a `ComponentDragger` on the top-level component.

### Rows and Export
- The rows have no Drag chips, and the top bar has Export instead of the mix chip.
- **Export sheet** (`ExportSheet`, boards 3A/3B): shown in the panel in place of the stems screen, like the help sheet. Esc and its x button close it.
  - **Header:** "Export", the subtitle "‹song file› · N stems", and the x button.
  - **Three cards:**
    - **All stems:**
      - **Choose folder...** opens an async folder chooser, starting in the last folder used this session, else Music;
      - the stem WAVs are copied into a new "‹song› stems" folder inside it ("‹song› stems (2)" and so on if it exists).
    - **My mix:**
      - the subtitle lists what is applied, e.g. "Bass muted · Vocals at 85% · 1.25x speed · just the loop 0:58-1:26";
      - **Save mix...** opens an async save chooser ("‹song› - mix.wav", overwrite warning on), then a background render.
    - **One stem:** a pill per stem; a pill opens a save chooser ("‹song› - ‹Stem›.wav"), then a copy.
  - **Card states:**
    - **Working:** "Saving my mix... 62%" with a drawn 6 px progress bar and **Cancel**. The other cards dim to 40% and can't be used.
    - **Done:** a check tile in the accent tint, e.g. "All stems saved", "4 WAVs in Music › Travis Snippet stems", and **Show in folder** (`File::revealToUser`).
    - **Failed:** "Couldn't save: ‹reason›" with **Retry**.
  - No PopupMenu, ProgressBar, ComboBox or TextEditor, per the "Must hold" list.

## VST only

- The header reads **LISN StemSplitter**, with no window buttons.
- Rows keep their Drag chips. A row drag is the stem file, or a render of it over the loop while looping, as now: no volume, no speed.
- **Drag mix / Drag loop** drags what you hear.
  - **At 1.0x:** it renders on drag start, as now (fast, and cached), with volumes applied.
  - **At other speeds:** the stems screen starts a background render of the current mix once mutes, volumes, speed and loop have been unchanged for 0.5 s. Any change cancels it and starts again later.
    - While it runs, the chip reads **"Preparing N%"** with a 2 px cream progress line along its bottom and doesn't drag.
    - When it's done, the chip reads "Drag mix" (or "Drag loop") and drags the cached file.
    - If it fails, it reads "Can't prepare" until a setting changes.

## Failure cases

| Case | What the user sees |
|---|---|
| Export can't write (read-only folder, disk full, file in use) | That card shows "Couldn't save: ‹reason›" and Retry; no `.part` or half file is left |
| Cancel, or closing the window during an export | The render stops, `.part` is deleted, the card goes back to idle |
| The VST's stretched mix can't be prepared | The chip reads "Can't prepare"; dragging is off until a setting changes, which retries |
| Settings change while preparing | The running render is cancelled; a new one starts 0.5 s after the last change |
| Windows animation effects are off | No intro; the window shows at its end state |
| A host block bigger than `prepare`'s size | Handled in chunks, as `addTo` already does |

## Testing

- **Engine:**
  - **Volume:**
    - a rendered mix at 1.0x equals the sum of `gain * stem` (±1e-4);
    - a live volume change is heard within one block;
    - the existing 1.0x player tests (alignment, mute timing, idle bit-exact, loop wrap, reachedEnd once) pass unchanged.
  - **Speed:**
    - renders at 0.75x and 1.25x are `length / speed` long (±1 block);
    - a 440 Hz sine stays at 440 Hz ±1% (zero crossings);
    - live playback at 1.25x advances the position 1.25x as fast, with no NaNs and the RMS within ±1 dB of the input's.
  - **Rendering:**
    - a cancelled render returns false and leaves neither `dest` nor `.part`;
    - different volumes or speeds give different `renderFile` names.
  - **Threads:** the no-stall test (calls return in under 20 ms while an audio thread runs) also calls `setSpeed` and `setVolume`.
  - **Bench:** `--bench` adds `process_block_us_stretch`, which must stay at or under 30% of a 512-sample block at 48 kHz (3.2 ms). `process_block_us_idle` stays about 0.
- **UI:**
  - `VolumeBar` and `SpeedStrip`: value mapping, double-click reset, the 1.0 catch, and the bipolar fill.
  - The window buttons exist only when the callbacks are set, and Export replaces the chips only when `wrapperType` is Standalone. Tests use `AudioProcessor::setTypeOfNextNewPlugin`.
  - `UiStateTests`' focus and banned-control walk covers the new components.
  - The motion frame stays at or under 4 ms (Release bench), intro frames included.
- **Snapshots** (`lisn_tests --snapshots`), both themes, each read by eye against its board:
  - desktop with 4 and 6 stems;
  - VST with 4 stems and the Preparing chip;
  - the Export sheet idle, working, done and failed;
  - one intro frame at 0.6 s;
  - and a check that the intro's last frame matches a no-intro frame pixel for pixel.
- **By the owner:**
  - **FL Studio and Ableton** (FL loads only from Common Files\VST3; copy elevated with FL closed):
    - clicking the plugin still leaves FL's spacebar working;
    - volume and speed are heard live;
    - a Drag mix at 1.25x shows Preparing, then drags a stretched WAV;
    - the intro plays on the first open only.
  - **Desktop, on a real install of `LISN-Setup.exe`:**
    - moving, minimising and closing the window from the header;
    - the intro at every start;
    - all three exports, Cancel, and Show in folder.

## Order of work

Three plans, each built, reviewed and tested before the next. Nothing is released until all three are done.

1. **Engine:** volume, speed, the new `render` and cache names, the Signalsmith dependencies (CMake `FetchContent` at pinned commits, the MIT texts in `installer/THIRD-PARTY-NOTICES.txt`), tests and bench. No visible change.
2. **Shared UI:**
   - `VolumeBar` and `SpeedStrip`;
   - the new row sizes;
   - the intro and the instant window;
   - the help text;
   - the VST's Preparing chip.
   Snapshots, then the FL and Ableton checks.
3. **Desktop:**
   - the frameless window and header buttons;
   - the desktop rows and top bar;
   - the Export sheet;
   - the desktop help tips.
   Snapshots, then the owner's real-install checks.

**Then the release:**
- a version bump and `v*` tag;
- the clean-PC checks carried over from sub-project 1 (decline the UAC prompt; uninstall while LISN is open; a real uninstall);
- publishing;
- a LISN Download button on lisn-site linking `releases/latest/download/LISN-Setup.exe`.

## Docs

- `docs/design/desktop/` holds the approved boards' sources:
  - `TitleBar.dc.html` (1A);
  - `Stems4.dc.html` (2C);
  - `Stems6.dc.html`;
  - `StemsVst.dc.html`;
  - `ExportSheet.dc.html` (3A);
  - `ExportSaving.dc.html` (3B);
  - `Intro.dc.html` (4A).
  They are design-canvas files that render on the canvas linked above, not standalone.
- `docs/superpowers/plans/2026-09-29-desktop-app.md` is deleted. This spec replaces it, as its note said it would.

## Not in this spec

Pitch shift without tempo change, effects, recording, an audio device picker, a resizable or scaled window, a Mac build, code signing, an in-app update check.
