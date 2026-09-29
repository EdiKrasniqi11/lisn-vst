# LISN StemSplitter: stem player (shared playhead, mute/solo, loop, BPM, drag mix)

## Context
Today the Stems screen previews **one stem at a time**, and each row keeps its own position. The user wants it to work like FL's channel rack:
- **one playhead** for the whole song;
- **any mix of stems audible together**, muted and unmuted live;
- **a loop** that snaps to the beat;
- **dragging the audible mix, or just the loop, into FL.**

The engine is shared code, so the future desktop app (`docs/superpowers/plans/2026-09-29-desktop-app.md`, Phases 4–5) reuses it.

The redesign spec's **"Must hold"** list (`2026-09-27-stemsplitter-redesign-design.md`) still binds everything here:
- no ComboBox, TextEditor, PopupMenu or ProgressBar;
- nothing takes keyboard focus;
- an idle `processBlock` does no work;
- a motion frame takes ≤ 4 ms (Release bench).

**Approved mockup:** `docs/design/StemPlayer.mockup.html`, layout A with the white speaker light. Where this text and the mockup disagree, the mockup wins.

## Stems screen

### Song row
The **file icon box becomes a Play/Pause button**: a 36 px cream circle with the existing filled play/pause icons in ink (`CircleButton`, sized 36).

The **subtitle** reads `1:04 / 3:12 · 92 BPM · loop 0:58–1:26`. The BPM part is left out when no tempo was found; the loop part only shows while looping is on.

Right side, 8 px apart, before **New song**:
- **Loop:** a Ghost `LisnButton` with a loop icon. While on, it gets an accent tint (fill `accent@0.22`, border accent). It's disabled (alpha 0.55) until a loop range exists.
- **A dashed drag chip:** **Drag mix**, or **Drag loop** while looping is on. Same style as the row chips.

### Rows
- **Layout** (4 stems: 62 px rows; 6 stems: 42 px):
  - name block 118;
  - **mute light** 38;
  - waveform **376** wide (it takes over the removed per-row time slot: 330 + 14 + 32);
  - Drag chip.
  Gaps stay 14.
- **Mute light:** a neutral ring (fill `cream@0.08`, 1 px `cream@0.22` border) holding an 18 px stroked icon, stroke 2:
  - on: a cream speaker with sound waves, `M11 5 6 9H3v6h3l5 4zM15.5 8.5a5 5 0 0 1 0 7M18.5 5.5a9 9 0 0 1 0 13`;
  - muted: the speaker in `cream@0.5` with an ×, `M11 5 6 9H3v6h3l5 4zM16 9.5l5 5M21 9.5l-5 5`.
  A muted row draws its name block and waveform at alpha 0.38. Stem colour stays in the badge and waveform only.
- **Loop band:** while looping is on, a `cream@0.10` band with 2 px cream edges spans every waveform over the loop range.
- **Footer text:** `Click a light to mute · right-click to solo · drag across the waves to loop` (12 px, cream 0.62, with the existing play glyph).

### Interaction
- **Play/Pause** plays or pauses the whole song. All waveforms share one playhead (the existing played-part colouring and cream playhead line).
- **Waveforms:** mouse-down seeks every stem at once, as today. A drag of 4 px or more then sets the loop range to the dragged span instead of scrubbing. The edges snap to the nearest beat when a BPM is known, and looping switches on. A new drag replaces the range.
- **Loop button:** toggles looping and keeps the range.
- **Mute light:**
  - **click** toggles that stem's mute, live;
  - **right-click** solos it (only that stem on, all others muted);
  - **right-click** on the only audible stem turns every stem back on.
  Solo is not a separate state: it's just a set of mutes, as in FL.
- **End of the song:** playback stops and goes back to 0:00. While looping, the song never ends.
- **New stems** (a new song, or the 4/6 switch) start with every stem on, no loop and position 0.
- **State:** mutes, the loop and the position live in the processor, so closing and reopening the plugin window keeps them. They are not saved in the project.

### Dragging into FL
- **Row Drag chip:** drags that stem's WAV. While looping is on, it drags a render of that stem over the loop range instead.
- **Song-row chip:** drags a render of the **audible** stems (mutes applied), over the loop range while looping, otherwise over the whole song.
- **Renders:**
  - they go to `<stemDir>/renders/` and are kept, because FL references dragged files by path;
  - names are `<song> - vocals+drums (0.58-1.26).wav` (m.ss, no colons), or `<song> - vocals+drums.wav` for the whole song;
  - an existing file with that name is reused.
  Rendering happens when the drag starts. A loop renders in milliseconds, a whole song in under a second.

## Engine: `StemPlayer` (replaces `StemPreview`)
- **Files:** `Source/StemPreview.h/.cpp` become `Source/StemPlayer.h/.cpp`. The processor member `preview` becomes `player`. `processBlock` stays `player.addTo (buffer)`, and `startSplit` still unloads.
- **Rules carried over from `StemPreview`:**
  - never call `transport.stop()`;
  - play/pause is the atomic `wanted` flag;
  - the position is read from an atomic;
  - idle `addTo` is two relaxed atomic loads and a return;
  - one faded block after pause.
- **Lockstep reading:** a private `StemStack : juce::PositionableAudioSource` reads **every** stem's reader at the same position into 2 channels per stem (mono is duplicated). It supports up to 8 stems; any further files are ignored (`ponytail:` comment). It wraps from the loop end to the loop start while looping. Loop bounds and the looping flag are atomics.
- **Buffering:** one `AudioTransportSource` over the stack with `maxNumChannels = 2·N` gives the read-ahead and the sample-rate conversion, as today. Changing the loop re-seeks the transport to the current position, which flushes stale read-ahead audio.
- **Mix on the audio thread:** after the transport, each stem's pair is added to the output with gain 1 or 0. When a mute changes, the gain ramps across that one block (`addFromWithRamp`), so a mute is heard within one block and never clicks. Mutes are atomics written on the message thread.
- **Metering:** the mix peak since the last `takePeak()` drives the beat motion, as before.
- **API (message thread):**
  - loading: `load (juce::Array<juce::File>)` (pauses; everything unmuted, no loop, position 0; false if any file is unreadable), `unload()`, `getFiles()`;
  - transport: `play()`, `pause()`, `isPlaying()`, `getLengthSeconds()`, `get/setPositionFraction()`;
  - mutes: `setMuted (i, bool)`, `isMuted (i)`, `solo (i)` (the FL rule above);
  - loop: `setLoop (double startFraction, double endFraction)`, `setLooping (bool)`, `isLooping()`, `getLoop()`;
  - read-outs: `takeReachedEnd()`, `takePeak()`.
  `// ponytail:` the read-ahead thread can briefly wait on a disk read while playing; idle takes no lock. Upgrade path: decode into memory.
- **Offline render:** `static bool render (const juce::Array<juce::File>& stems, uint32 audibleMask, double startSec, double endSec, const juce::File& dest)`. It reads in 65536-sample chunks, sums the audible stems and writes a 24-bit WAV at the stems' sample rate.

## BPM: `detectTempo`
- **Where:** `Peaks.h/.cpp`, next to `readPeaks`: `struct Tempo { double bpm = 0, firstBeat = 0; }; Tempo detectTempo (juce::AudioFormatReader& drums);`.
- **Method:**
  1. Build a loudness envelope from 10 ms `readMaxLevels` hops.
  2. Take the onset strength: the positive envelope difference.
  3. Autocorrelate over lags covering 70–180 BPM and refine the best lag parabolically.
  4. Find the phase: the offset whose beat positions collect the most onset strength.
  5. If the autocorrelation peak is under 0.2 of the zero-lag energy, return `bpm = 0`.
- **Limits:** `// ponytail:` a steady tempo is assumed, and half or double tempo can be reported. Upgrade path: a ×2/÷2 toggle, or tracking tempo changes.
- **When:** it runs inside the existing `StemsScreen` peak job on the stem named `drums`. No drums stem means no BPM and no snapping.
- **Snapping:** round each loop edge to the nearest `firstBeat + k·60/bpm`.

## Dev workflow: no copy, no rescan
One elevated, one-time step. Remove `C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3`, then create a directory junction with that name pointing at `build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3` (`mklink /J`).

From then on: close FL, build Release, open FL. The path FL remembers never changes, so there is no copy and no rescan. Debug builds don't affect FL. If the build folder is wiped, FL can't load the plugin until the next Release build. Record this in the `fl-studio-plugin-install` memory.

## Testing
- **`StemPlayer`** (evolving `tests/StemPreviewTests.cpp`):
  - two stems with impulses at the same sample stay aligned at 48 kHz;
  - a mute is heard within one block;
  - the solo and un-solo rule;
  - a loop wraps exactly and never reports the end;
  - idle pass-through is bit-exact;
  - the threaded no-stall test (play, pause, load and unload each under 20 ms with a live audio thread; load under 100 ms);
  - the end of the song sets `reachedEnd` once;
  - a render equals the sum of the audible stems over the range (±1e-4), and the same name is reused.
- **`detectTempo`:** the synthetic drums from `tests/Snapshots.cpp` (a hit every 0.5 s) give 120 ± 1 BPM with the first beat within 20 ms of 0. Silence gives `bpm = 0`.
- **UI** (`tests/UiStateTests.cpp`):
  - a light click mutes, a right-click solos and un-solos;
  - a waveform click seeks every stem;
  - a drag sets a snapped loop;
  - the focus and banned-control walk covers the new controls;
  - replace the old per-row play/pause test.
- **Snapshots:** playing with one stem muted and a loop on (Dusk, 4 stems), muted rows in Midnight with 6 stems, and 2×. Read each PNG against the mockup.
- **Final checks:**
  - the Release bench motion frame stays ≤ 4 ms;
  - pluginval `--strictness-level 5` passes in Debug and Release;
  - the user's FL click-through: play/pause, mute, solo, seek, loop drag and toggle, drag mix, drag loop, row drag with a loop, and the 4/6 switch while playing. After each click the spacebar must still toggle FL's transport.

## Not in scope
- per-stem volume and speed;
- syncing to FL's play/stop;
- telling apart half and double BPM;
- saving mutes and the loop in the project;
- background pre-rendering;
- GPU PyTorch (a separate one-off install).
