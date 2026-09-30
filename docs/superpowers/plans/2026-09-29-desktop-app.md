# LISN StemSplitter Desktop: implementation plan

> **Updated 2026-09-30 (installer plan):** the licence is decided: JUCE 8 Starter, closed source (Starter's cap is now $20k, not $50k). The AI engine lives at `C:\ProgramData\LISN\engine`, set up by lisn-vst's installer, and `PythonFinder` checks it first; Phase 6 should reuse it, not build a second engine. VST3 SDK 3.8 is MIT (JUCE 8.0.11+), so the Steinberg licence question is gone.

> **For agentic workers:** run this in order, one phase at a time. Every phase ends with its verification and a commit. Two repos are involved:
> - **lisn-vst:** https://github.com/EdiKrasniqi11/lisn-vst, the plugin and the shared code.
> - **lisn-desktop:** the new repo, the app shell, setup, installer and releases.

> **On approval now (2026-09-29), the only step:** save this file as `lisn-vst/docs/superpowers/plans/2026-09-29-desktop-app.md` and commit it on `master`, so it travels with the code. Build nothing yet; the user runs this later in the new repo.

## Context

LISN StemSplitter is a VST3 that splits a song into stems with Demucs, previews them and lets you drag them into a DAW. Its approved "Dusk glass" UI is built and verified in lisn-vst. The user wants a **public desktop app** from the same code, plus Stem-Player-style features (in the spirit of Kanye's Stem Player, but no Kanye or "Stem Player" branding):
- play stems together and layer them;
- mute, solo and volume per stem;
- loop and speed;
- export one stem, all stems as a .zip, or your own mix.

The VST should be able to get these features later too, so they are **shared code in lisn-vst**, not desktop-only.

**Always newest:** the desktop build fetches lisn-vst `master` from GitHub on every configure. Whenever this plan runs, it uses the latest upstream code. **Never trust this document's description of lisn-vst over the code you fetch.** Phase 0 re-reads it.

## Phase 0: Sync and ground truth (no code)

1. **Upstream must be pushed.** lisn-vst `master` must contain the redesign: commit `a016da1` "chore: final verification for the redesign", or later. On 2026-09-29, GitHub's `master` was still at `ee29ca0`. If `git ls-remote https://github.com/EdiKrasniqi11/lisn-vst master` doesn't include `a016da1` in its history, stop and ask the user to push.
2. **Clone the latest lisn-vst read-only** into a scratch folder. Read:
   - `CMakeLists.txt` (JUCE version, `LISN_SOURCES`, targets);
   - `docs/superpowers/specs/*redesign-design.md`, especially the **"Must hold"** list, which binds all shared code;
   - `docs/design/*.dc.html` (the visual language);
   - `Source/` and `tests/`.
3. **Check this plan's assumptions** against the code, and write the differences into `lisn-desktop/docs/upstream-notes.md`. Adapt the phases below to them:
   - `StemSplitterProcessor` has `job`, `preview`, `startSplit`, `setPythonPath`, `get/setStateInformation` and `settingsLock`;
   - `StemSplitterEditor` is a fixed 760×500 window;
   - the shared sources compile in a non-plugin app (as `lisn_tests` does, with a `JucePlugin_Name` define);
   - `PythonFinder` tries the saved-path hint first.
4. **Licences.** Record the decisions in `lisn-desktop/LICENSES.md`:
   - **JUCE 8 (a blocking decision, before anything is published).** JUCE's licence governs giving the built plugin or app to other people. Private builds are fine under any option. Ask the user to choose one of two paths:
     - **AGPLv3:** distribution (even paid) is allowed only if the complete LISN source is published under AGPLv3. Anyone may then modify it and redistribute it for free. If chosen, add an AGPLv3 `LICENSE` file to both repos.
     - **JUCE licence:** keeps the code closed. The free Starter tier covers individuals and small companies under a yearly revenue limit (about $50k when this plan was written); Indie and Pro are paid tiers above that. The user registers at juce.com.
     Check the current tiers and limits on juce.com at execution time, since this plan's figures may be out of date. Record which path was chosen and when in `LICENSES.md`. Don't set up the website, a public repo or a release (Phase 8) until this is decided. This is not legal advice; for a commercial launch, the user should have the terms reviewed.
   - **Steinberg VST3:** check the VST3 SDK licence and the "VST" logo usage guidelines if the site will show the logo.
   - **Other parts:** Demucs (MIT), PyTorch (BSD-3), CPython (PSF), uv (MIT/Apache-2.0) and the fonts (SIL OFL, texts already in `Resources/fonts`).

## Phase 1: lisn-vst exposes a `lisn_core` build target (upstream, no behaviour change)

**Goal:** another repo can compile the shared code without building the plugin or its tests.

- **`lisn_core` target.** In lisn-vst `CMakeLists.txt`, replace the `LISN_SOURCES` variable with `add_library(lisn_core INTERFACE)`:
  - `target_sources(lisn_core INTERFACE <absolute paths of Source/*.cpp>)`;
  - `target_include_directories(lisn_core INTERFACE Source)`;
  - `target_link_libraries(lisn_core INTERFACE LisnFonts juce::juce_audio_utils)`.
  It's INTERFACE because each consumer compiles the sources with its own JUCE config, exactly like the plugin and `lisn_tests` do today. A static lib would duplicate the JUCE module symbols.
- **Options.** Add `option(LISN_BUILD_PLUGIN … ${PROJECT_IS_TOP_LEVEL})` and `option(LISN_BUILD_TESTS … ${PROJECT_IS_TOP_LEVEL})` around the plugin and test targets.
- **Verify:** from the lisn-vst root, a Debug and a Release build pass, ctest passes and pluginval `--strictness-level 5` prints SUCCESS. The build output is unchanged. Commit and **push** (the desktop app only sees what's pushed).

## Phase 2: lisn-desktop skeleton, the current UI as a real app

**Repo layout:**
```
CMakeLists.txt        fetches lisn-vst master (and through it JUCE), app target
Source/Main.cpp       JUCEApplication + MainWindow
assets/icon.png       1024 px app icon in the LISN wave style
build.ps1             always reconfigures, so every build pulls the newest lisn-vst
docs/upstream-notes.md, LICENSES.md
```

**`CMakeLists.txt`:**
- **Static runtime:** `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")` **before** any target, so users need no VC++ runtime.
- **Fetch lisn-vst:**
  - `set(LISN_BUILD_PLUGIN OFF)`, `set(LISN_BUILD_TESTS OFF)`;
  - `FetchContent_Declare(lisn_vst GIT_REPOSITORY https://github.com/EdiKrasniqi11/lisn-vst.git GIT_TAG ${LISN_VST_REF})`, where `LISN_VST_REF` is a cache variable defaulting to `master`;
  - `FetchContent_MakeAvailable(lisn_vst)`.
  JUCE then comes from lisn-vst's own declaration, so the versions always match.
- **Record the upstream commit:** run `git rev-parse --short HEAD` in `${lisn_vst_SOURCE_DIR}` into `LISN_VST_COMMIT` (a compile definition). `--version` prints it and the release notes include it.
- **App target:**
  - `juce_add_gui_app(LisnStemSplitter PRODUCT_NAME "LISN StemSplitter" COMPANY_NAME LISN ICON_BIG assets/icon.png VERSION <semver>)`;
  - link `lisn_core`, `juce_recommended_config_flags` and `juce_recommended_warning_flags`;
  - defines `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` and `JucePlugin_Name="LISN StemSplitter"`.
- **`build.ps1`:** runs `cmake -S . -B build -DFETCHCONTENT_UPDATES_DISCONNECTED=OFF`, then `cmake --build build --config Release`. A branch `GIT_TAG` only updates on configure, so the script always configures.

**`Main.cpp` (about 150 lines):**
- **Setup:** `AudioDeviceManager::initialiseWithDefaultDevices (0, 2)` (outputs only, so no mic, no feedback and no "input muted" banner). Then create the `StemSplitterProcessor`, an `AudioProcessorPlayer` with `setProcessor`, and `addAudioCallback`.
- **Window:** a `DocumentWindow` with the native title bar, not resizable, holding `processor->createEditor()`.
- **State:** `getStateInformation`/`setStateInformation` saved to a `PropertiesFile` in `%APPDATA%\LISN StemSplitter` (base64), restored at start and saved on quit.
- **Single instance:** `moreThanOneInstanceAllowed() = false`. A file path on the command line (or from `anotherInstanceStarted`) calls `startSplit(file)` and brings the window to the front.
- **Shutdown order:** remove the callback, `setProcessor (nullptr)`, destroy the window (the editor), then the processor.
- **Environment:** set `TORCH_HOME` to `%LOCALAPPDATA%\LISN StemSplitter\engine\models` at startup, before any split, so demucs child processes inherit it (Phase 6).

**Verify:** `build.ps1` builds from a clean clone. The app opens the Drop screen with no banner, splits a song with the dev machine's Python, previews stems on the default output and drags a stem into Explorer. Close and reopen: the theme and the last stems are restored. Run `--version`. Commit.

## Phase 3: Mockups before any UI code (user approval gate)

User rule: HTML design-canvas mockups and approval **before** building UI. DAW trial and error is too slow.
- **Design** in the style of `docs/design/*.dc.html` (same fonts, themes, wave background and frosted panel, still 760×500 so one UI serves the plugin and the app):
  - **Stem player rows:** one shared song position for all stems. Each row gets mute, solo and a volume control, and an audible or silent state; it keeps its waveform and drag chip.
  - **Transport bar:** play/pause all, time, loop on/off (drag across the waveforms to set the loop range), speed 0.5–1.5×.
  - **Export sheet:** an in-UI panel (**no PopupMenu**, which is banned by the spec) with this stem, all stems (.zip) and my mix (.wav), plus a "Drag mix" chip.
  - **Desktop-only screen:** the one-time AI-engine setup (Phase 6), with progress, Cancel and retry.
- **Decide with the user** whether the VST shows the stem player now or later. If later, gate it behind one processor flag the desktop sets.
- Save the approved files to `lisn-vst/docs/design/`. **Do not continue without approval.**

## Phase 4: Stem player engine (upstream in lisn-vst, shared)

> **Done upstream (2026-09-29):** StemPlayer (lockstep stems, mute/solo, loop wrap, render) and the mute/loop/Drag-mix UI already exist; this phase only adds per-stem volume and speed.

Evolve `Source/StemPreview.h/.cpp` into `StemPlayer`. Keep its proven rules:
- never call `transport.stop()`;
- play/pause is an atomic flag;
- the position comes from an atomic;
- idle `addTo` does two relaxed loads and returns.

**Design:**
- **`StemStackSource : PositionableAudioSource`** reads **all** stem readers at the same position and outputs 2·N channels (stereo per stem, mono duplicated). Using one source keeps the stems sample-aligned by construction. Loop range: atomics, wrapping inside `getNextAudioBlock`.
- **Buffering and speed:** one `BufferingAudioSource` (read-ahead thread, 2·N channels), then one `ResamplingAudioSource` with ratio `srcRate·speed/hostRate`. Speed changes the pitch too, like the Stem Player.
- **Mix on the audio thread:** mute, solo and volume are applied **after** the read-ahead buffer. Per-stem gains are `SmoothedValue`s fed from atomics written on the message thread, so a mute takes effect within one block, not after about 0.7 s of buffered audio.
- **Metering:** per-stem peaks (row meters) and a mix peak (beat motion).
- **API (message thread):**
  - loading: `load (Array<File>)`, `unload()`;
  - transport: `play()`, `pause()`, `setPositionFraction()`, `getPositionFraction()`;
  - loop and speed: `setLoop (a, b)`, `clearLoop()`, `setSpeed()`;
  - per-stem controls: `setVolume (i, v)`, `setMute (i, b)`, `setSolo (i, b)`, where solo overrides mute (a truth-table helper);
  - read-outs: `takeReachedEnd()`, `takePeak (i)`, `takeMixPeak()`.
  A loop change re-seeks to the current position to flush stale read-ahead audio.
- **Processor:** replace the `preview` member with a `StemPlayer`. `processBlock` is still one call. `startSplit` unloads.

**Tests** (in `tests/`, same style as `StemPreviewTests.cpp`):
- impulses at the same sample in two stems stay aligned;
- a mute is heard within one block;
- the solo/mute truth table;
- idle pass-through is bit-exact;
- a loop wraps sample-exactly;
- 2× speed halves the duration;
- keep the threaded no-stall test (play/pause/load/unload return in under 20 ms while an audio thread runs);
- the end of the song sets `reachedEnd` once.

**Verify:** ctest and pluginval pass, and `lisn_tests --bench` still has `process_block_us_idle` around 0 and the motion frame ≤ 4 ms (Release). Commit and push.

## Phase 5: Stem player UI and export (upstream, shared; follows the approved mockups)

- **UI:** update `StemRow`, `StemsScreen` and `PluginEditor::tick/show` for the approved design. Reuse `Controls.h` (`SegmentedPill`, `LisnButton`, `CircleButton`, `drawDashedRoundedRect`), `Icons.h`, `Theme`/`Fonts`, the peak-job pattern in `StemsScreen::setStems` and `WaveBackground::setMotion`, now fed from the mix peak.
- **Spec "Must hold":**
  - no ComboBox, TextEditor, PopupMenu or ProgressBar;
  - nothing takes keyboard focus;
  - paint guards with `g.clipRegionIntersects` for any path work.
- **Keyboard API for the desktop:** expose `StemSplitterEditor::togglePlayback()` publicly. The plugin never binds keys, so FL keeps its spacebar.
- **`Source/StemExport.h/.cpp`:**
  - one stem: copy the WAV;
  - all stems: `juce::ZipFile::Builder` into a .zip;
  - my mix: an offline render of the stems with the current gains, loop and speed into a WAV on a background thread, with progress and cancel;
  - "Drag mix": render to a temp WAV, then `performExternalDragDropOfFiles`.
  Save locations use an async `FileChooser` in `saveMode` with an overwrite warning.
- **Tests:**
  - the zip has N entries with the right sizes;
  - a rendered mix equals Σ gain·stem (±1e-4);
  - render cancel works;
  - the existing focus and banned-control walk (`UiStateTests.cpp`) covers the new controls;
  - update the old per-row play/pause test for the shared song position.
- **Verify:** `lisn_tests --snapshots snapshots` for every new state in both themes (read every PNG and compare it with the mockups). The bench's motion frame is ≤ 4 ms in Release. Check pluginval, then the FL click-through (spacebar still toggles FL after each click). Commit and push. The desktop app picks it all up on its next build.

## Phase 6: One-time AI engine setup (desktop only)

Public users won't have Python. The app installs a private engine on first run.
- **Tools:** bundle a pinned `uv.exe` (Astral; verify its SHA-256) in the installer under `<app>\tools\`.
- **Folders:** everything lives in `%LOCALAPPDATA%\LISN StemSplitter\engine`, with `UV_PYTHON_INSTALL_DIR`, `UV_CACHE_DIR` and `TORCH_HOME` pointing inside it so uninstall can remove it all.
- **Steps** (each a `juce::ChildProcess`, killed under a lock on Cancel as in `SeparationJob::cancel`):
  1. `uv python install 3.11`, then `uv venv --python 3.11 engine\venv`.
  2. `uv pip install --python engine\venv\Scripts\python.exe demucs==X torch==Y torchaudio==Z soundfile --index-url https://download.pytorch.org/whl/cpu --extra-index-url https://pypi.org/simple`. Pin X, Y and Z to the versions that work on the dev machine (`C:\Python311\python.exe -m pip show demucs torch torchaudio`), because newer torchaudio releases change how audio is saved.
  3. Prefetch the models: `python -c "from demucs.pretrained import get_model; [get_model(m) for m in ('htdemucs','htdemucs_6s')]"`. Parse tqdm output with lisn-vst's `DemucsProgress.h`.
  4. Verify with lisn-vst's `checkPython (exe, 10000)`, which must return Ready.
- **Screen:** a desktop-repo component in the approved mockup's style, using lisn-vst's `Theme`, `Fonts`, `LisnButton` and `WaveBackground`. It shows the steps (1/4…4/4) with progress, Cancel, retry, and a disk-space check (about 2.5 GB free) before starting.
- **When:** at startup, only if no engine exists **and** `findPython ("", PythonSearchDirs::fromEnvironment())` isn't Ready. Users with a working Python skip it.
- **After:** `processor.setPythonPath (engine python)`. `PythonFinder` tries the hint first.
- **Verify:** rename the dev machine's Python out of PATH (or use a clean VM), run setup, split, then cancel and resume setup midway.

## Phase 7: Installer

- **Inno Setup script `installer/lisn.iss`:**
  - `PrivilegesRequired=lowest`, installing to `{localappdata}\Programs\LISN StemSplitter`;
  - ships the exe, `uv.exe` and the licence texts;
  - adds a Start menu shortcut and an optional desktop icon;
  - adds an optional "Open with" entry for wav/mp3/flac/aif/aiff/ogg (HKCU, without taking over the default apps).
- **Uninstaller:** asks whether to also remove the AI engine and the stem cache (`%APPDATA%\StemSplitter\cache`, from `SeparationJob::stemDirFor`).
- **Verify on a clean Windows machine:** a VirtualBox VM or another PC. This dev machine runs Windows 11 Home, which has no Windows Sandbox. Check: install without admin, first-run setup, split, export all three kinds, then uninstall.

## Phase 8: Signing and release builds (CI)

- **`.github/workflows/build.yml` (windows-latest):**
  - it configures fresh every run, so it always uses the newest lisn-vst;
  - it builds Release and runs lisn-vst's tests (configure a second build dir with `-DLISN_BUILD_TESTS=ON`);
  - it builds the installer.
- **Triggers:**
  - push and PR;
  - a nightly schedule;
  - `repository_dispatch` sent by a small lisn-vst workflow on every push to `master` (needs a PAT secret);
  - tags `v*`, which also sign and publish.
- **Licence gate:** tagging or publishing a release requires the JUCE licence decision recorded in `LICENSES.md` (Phase 0). Without it, stop and ask.
- **Signing:** signtool with an Authenticode certificate, or Azure Trusted Signing if the user is eligible. **This is the user's cost decision; ask.** Unsigned builds trigger SmartScreen warnings.
- **Release:** a GitHub Release with the installer, its SHA-256 and the lisn-vst commit it was built from.
- **Verify:** a test tag produces a signed installer that installs cleanly on the VM.

## Later (not in this plan)

Add these only when asked:
- tempo change that keeps the pitch (SoundTouch is LGPL, so link it dynamically; Rubber Band is GPL or commercial);
- pitch shift;
- effects (filter sweep, echo);
- recording a performance;
- GPU engine (CUDA torch, +2.5 GB);
- update check against the GitHub Releases API;
- macOS build (codesign and notarization);
- a device picker (default output only for now).

## Verification summary

| Phase | Done when |
|---|---|
| 0 | upstream notes written; licences recorded |
| 1 | lisn-vst builds, tests and pluginval unchanged; pushed |
| 2 | app runs the current UI with no banner; state persists; `--version` shows the upstream commit |
| 3 | user approved the mockups |
| 4 | player tests pass; the idle `processBlock` and motion budgets hold |
| 5 | snapshots match the mockups; FL click-through passes; pushed |
| 6 | clean-VM first-run setup, then a split |
| 7 | install, split, export and uninstall on the clean VM |
| 8 | a tagged build produces a signed release |
