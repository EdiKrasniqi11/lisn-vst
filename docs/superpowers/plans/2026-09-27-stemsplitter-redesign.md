# StemSplitter Redesign Implementation Plan

> **For agentic workers:** execute task by task. Each task ends with a build, the tests and a commit. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Rebuild the plugin UI as the approved mockup layout "A · Dusk glass":
- a Dusk/Midnight theme switch;
- a 4/6-stem switch;
- real waveforms and stem preview playback;
- beat-reactive waves;
- automatic Python detection;
- no crash-prone controls.

**Spec:** `docs/superpowers/specs/2026-09-27-stemsplitter-redesign-design.md`. Read it first; its "Must hold" list binds every task.
**Pixel reference:** `docs/design/Main.dc.html` (Stems), `Empty.dc.html` (Drop), `Separating.dc.html` (Splitting), `PythonMissing.dc.html` (Error) and `Waves.dc.html` (background JS to port). The CSS `px` values in those files are the layout spec.

**Architecture:**
- **Processor:** owns `SeparationJob` (demucs worker thread plus automatic Python search), `StemPreview` (an `AudioTransportSource` mixed into `processBlock`, with a peak meter) and the saved settings.
- **Editor:** a fixed 760×500 window holding `WaveBackground` (pre-rendered wave layer masks moved per frame, a static frosted panel), a `Header` and one screen component per `UiState::Screen`. A 30 Hz timer maps processor/job state to `UiState`, updates playheads and drives the beat motion.
- **Test app:** `lisn_tests` is a JUCE console app. It runs `juce::UnitTest`s, and `--snapshots` / `--bench` modes render every screen to PNG and time the hot paths.

**Tech stack:** JUCE 8.0.4 (FetchContent), CMake ≥ 3.22, MSVC 2022, C++17.

## Global constraints (from the spec)
- **Window:** fixed at 760×500. VST3 plus Standalone. The Standalone is only a dev convenience.
- **Banned controls:** no `ComboBox`, `TextEditor`, `PopupMenu` or `ProgressBar`, and no component may take keyboard focus: call `setWantsKeyboardFocus (false)` everywhere, plus `setMouseClickGrabsKeyboardFocus (false)` on Buttons.
- **Idle `processBlock`:** with no preview playing it returns immediately, with no allocation, lock or extra work.
- **Motion:** only while a preview plays, or while the waves settle afterwards. At most 30 fps, repainting only `WaveBackground::motionRegion()`. No per-frame path fills, shadows or blurs.
- **Colours and fonts:** exactly the spec's theme table. Fonts come from BinaryData; mockup `font-size` equals `FontOptions::withPointHeight`.
- **Saved state:** `theme`, `model`, `python`, `input` and `stems`. Old projects without `theme` load as `dusk`.
- **Builds:** from the repo root, with CMake at `"C:\Program Files\CMake\bin"` (prepend it to PATH) and the existing VS 17 2022 x64 `build/` directory:
  - `cmake --build build --config Debug`;
  - tests via `ctest --test-dir build -C Debug --output-on-failure`;
  - pluginval via `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\LISN StemSplitter.vst3"`.
- **Commits:** each commit message ends with a blank line and then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Don't push.

## Review corrections (binding; they override the task text where they conflict)
These come from an independent plan review with verification against the JUCE 8.0.4 sources in `build/_deps/juce-src`.

**Global**
- **G1 · Non-ASCII UI text:** every UI literal that isn't plain ASCII uses escaped UTF-8, e.g. `juce::String (juce::CharPointer_UTF8 ("Browse\xe2\x80\xa6"))`, and keep a non-hex character right after each `\x` escape. Put the shared pieces in `Screens.cpp`, e.g. `const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));`. `String (const char*)` treats bytes as ASCII: it asserts and garbles `·` and `…`.
- **G2 · Text measurement:** measure text with `juce::GlyphArrangement::getStringWidth (font, text)`. `Font::getStringWidth(Float)` is deprecated in JUCE 8 and would add C4996 warnings.
- **G3 · Source lists:** new `.cpp` files go inside the single `set(LISN_SOURCES …)` call, which must stay above both `target_sources` calls. New test files go inside `target_sources(lisn_tests PRIVATE …)`. Never use `list(APPEND …)` after the targets are declared.
- **G4 · Letter spacing:** CSS letter-spacing in em maps to `font.withExtraKerningFactor (em * font.getHeightToPointsFactor())`, because JUCE multiplies the tracking by `getHeight()`, not by the em size.
- **G5 · Box heights:** CSS boxes are content-box unless the mockup sets `box-sizing: border-box`, so a 1 px border adds 2 px:
  - pill container: outer 38 px tall, items inset 4 px (1 border + 3 padding);
  - drag chip: outer 34 px tall;
  - error code box: outer 46 px tall;
  - buttons are border-box, so their heights are exactly as given.
- **G6 · Filled icons:** play, pause and the footer play icon are **filled** (`g.fillPath`); every other icon is stroked. The play icon is shifted **+1 px** in x (CSS `margin-left: 2px` inside a centred box). The playhead is `fillRoundedRectangle (fraction * w - 1, -5, 2, h + 10, 1)`.
- **G7 · Multi-line text:** use `g.drawMultiLineText (text, x, firstBaseline, maxWidth, justification, leading)`:
  - `leading = px * lineHeight - font.getHeight()`;
  - `firstBaseline = top + leading / 2 + font.getAscent()`;
  - `height = lines * px * lineHeight`.
  Never use `drawFittedText`, which squeezes the text horizontally.

**Task 1**
- **T1a · Font weight test:** also assert that `Fonts::body (14, 500).getTypefacePtr()->getStyle()` contains "Medium" and that `body (12, 700)` contains "Bold".

**Task 2**
- **T2a · Timeout and PATH quoting:** `findPython` calls `checkPython (candidate, 10000, shouldAbort)`. Unquote PATH entries (`entry.trim().unquoted()`) before the absolute-path check. In the test, add `path2/python.exe`, give it the PATH entry `"<path2>"` (quoted), and expect `[pf311, path1, path2, lad312, lad310, sd39, miniconda3]`.
- **T2b · Bare hints:** a hint with no path separator, such as legacy `"python"`, is **dropped**, because CreateProcess could resolve it to the Microsoft Store stub. So `pythonCandidates ("python", dirs) == pythonCandidates ("", dirs)`. Test 4 checks exactly that.
- **T2c · Race between start() and kill():** add `juce::CriticalSection procLock;` to `SeparationJob`.
  - **run():** `{ const juce::ScopedLock sl (procLock); if (threadShouldExit()) { state = JobState::Cancelled; return; } started = proc.start (cmd, …); }`.
  - **cancel():** `signalThreadShouldExit(); { const juce::ScopedLock sl (procLock); proc.kill(); } stopThread (5000);`.
  This way `kill()` never races `start()`, which resets `activeProcess`.
- **T2d · Settings lock:** the processor gets `juce::CriticalSection settingsLock;`, which guards `theme`, `model`, `pythonPath`, `lastInput`, `stemDir` and `stemDirHasWav`.
  - **Setters:** add `setTheme (String)` and `setPythonPath (String)`. The editor never assigns those fields directly and reads them through getters that return copies under the lock.
  - **Callers:** `startSplit`, `setSixStems`, `syncWithJob` and `setStateInformation` take the lock.
  - **`getStateInformation`:** it is **read-only**. Under the lock it copies the fields, and it substitutes `job.getStemDir()` or `job.getPythonExe()` as `syncWithJob` would, without writing any members.
- **T2e · Cached "has stems" flag:** the processor keeps `bool stemDirHasWav`, recomputed only where `stemDir` is assigned. `uiStateFor` uses that flag and never scans the disk on every tick.
- **T2f · Notify the host:** `setTheme` and `setSixStems` call `updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));` so the DAW marks the project as modified.

**Task 3**
- **T3a · Preview rewrite:** replaced by the rewritten Steps 1–4 above (no `transport.stop()`, the `wanted`/`sounding`/`readPos` atomics, the threaded no-stall test).

**Task 5**
- **T5a · Caches:** replaced by the rewritten Step 2 above (image type chosen with `dynamic_cast`, and shadows blurred with our own blur at σ 14 and 25).
- **T5b · Repainting the motion region:** `Component::repaint` has no `RectangleList` overload, so use `for (auto r : motionRegion()) repaint (r);`. `setMotion` computes the five snapped physical-pixel offsets (`(motionOffset (...) * lastScale).roundToInt()`) and returns early when they equal the last painted offsets.
- **T5c · Shadow test:** add a test that the layer shadow mask 30 px outside the shape edge is still > 10 % of its peak.

**Task 6**
- **T6a · Repaint only on change:** `StemRow::setPlayback` repaints only when `playing`, the playhead pixel `roundToInt (fraction * 330)` or the displayed `m:ss` string changed, and then repaints only the waveform and time rects. The test calls it twice with the same values and asserts no second repaint, using a paint counter in a test subclass or a `getRepaintCount()` debug counter.
- **T6b · Playhead position test:** `setPlayback (false, 0.5, 30)` puts the playhead at waveform x = 165 and the time at "0:15".
- **T6c · Chip height:** the drag chip is 34 tall in the test (G5).
- **T6d · Mockup gaps:**
  - name block: 10 px between the badge and the text; name and file name 1 px apart, line-height 1.2;
  - drag chip: grip-to-text gap 6, dash 3 and gap 3;
  - footer: play icon 14 px filled in cream@0.62, with an 8 px gap before the text.

**Task 7**
- **T7a · Data for every screen:** add `SplittingScreen::setSong (String name, bool six)` and `ErrorScreen::setError (ErrorKind, String text, String pythonExe)`. `show (s)` always calls `drop.setSixStems (s.sixStems); drop.setHighlighted (false); splitting.setSong (s.songName, s.sixStems); error.setError (s.error, s.errorText, s.pythonExe);`.
- **T7b · Rebuilding rows:** every time `stems.setStems` rebuilds the rows, also set `activeRow = -1` and call `proc.preview.unload()`. The screen resets `positions` to the new row count, and every `rows[i]`/`positions[i]` access is bounds-checked.
- **T7c · Peak jobs:** `StemsScreen` keeps `int generation`. `setStems` does `++generation; pool.removeAllJobs (true, 2000);` and queues **one** job that reads every row file in order. The job checks `ThreadPoolJob::getCurrentThreadPoolJob()->shouldExit()` between files and passes that check to `readPeaks` as `shouldAbort`. It captures files, bars and `gen` by value, then posts `callAsync` with a SafePointer. The posted lambda returns unless `safe != nullptr && safe->generation == gen`, and bounds-checks rows. `bool hasPeaks() const` reports when they have arrived.
- **T7d · Positions and seeking:** `positions` is private to StemsScreen. The WaveformView seek handler inside the screen sets `positions[i] = f`, updates `rows[i]->setPlayback (…)` right away (even for a row that isn't playing), then calls `onSeek (i, f)`. The editor's `onSeek` only calls `proc.preview.setPositionFraction (f)` when `i == activeRow`. Add `double positionOf (int row) const`.
- **T7e · Play/pause on the active row:** `onPlayPause (i)`:
  - if `i == activeRow`, toggle `preview.play()` / `pause()` without calling `load()` again;
  - otherwise `load` the file, then `setPositionFraction (stems.positionOf (i))`, `play()` and set `activeRow = i`.
- **T7f · Active row from the preview:** in `tick()`, derive `activeRow` as the index of the row whose `getFile() == proc.preview.getFile()`, or −1 if there is none. That covers reopening the editor and the snapshot hooks. At end of stem, `if (proc.preview.takeReachedEnd())` sets the active row's position to 0 and calls `setPositionFraction (0)`; drop the old `fraction >= 0.999` rule.
- **T7g · Applying the theme:** `applyTheme (const Theme&)` sets every screen, the header, the rows and `waves`, and records `appliedThemeId`. Call it at the end of the constructor, and in `tick()` whenever `proc.getTheme() != appliedThemeId` (for example after the host restores state).
- **T7h · Reading job strings:** `uiStateFor (const StemSplitterProcessor&)` reads `job.getError()` and `getPythonExe()` **only** when the job state is Failed or Done; the worker thread writes them before storing the state.
- **T7i · Stem count from disk:** the compact row mode and the "N stems" count come from the number of stem files on disk (`compact = files.size() > 4`). The model flag only drives the header pill, the Drop chips and the Splitting subtitle.
- **T7j · Header baseline:** draw "LISN" and "StemSplitter" with `g.drawSingleLineText` on a shared baseline at header-local y = 30.25. "StemSplitter" starts at LISN's advance (including tracking) + 10.
- **T7k · Error screen text:**
  - **Python-missing body:** "StemSplitter splits songs with Demucs, a free AI tool that runs on Python. Install Python 3.11 from python.org, then run this once in a terminal:".
  - **Failed variant:** build the lines with `StringArray::fromLines (text.fromFirstOccurrenceOf ("\n", false, false).replaceCharacter ('\r', '\n'))`, remove empty lines and lines containing "%|", and keep the last 6. Draw each line single-line in mono 14 on an 18 px pitch, ellipsized when too wide. The box height is 20 + 18·n, with Copy at the top-right (right − 6 − w, top + 6). Copy copies the full error text.
  - **Demucs-missing command:** while the command is too wide, shorten the middle path components to "…" (keep the drive and the last two components). Copy always copies the full command.
- **T7l · Error screen gaps:** 14 px between the icon circle and the title; 12 px between the code text and Copy. The Copy icon is 14 px, stroke 2, with a 6 px gap before its label.
- **T7m · Drop screen gaps:** 10 px between "or" and Browse; 8 px between "You'll get these stems" and the chips.
- **T7n · Stems top row:**
  - 12 px gap after the icon box;
  - the title and subtitle are 2 px apart with line-height 1.2;
  - the New song plus icon is 16 px, stroke 2, with a 6 px gap before its label;
  - the rows container fills the space, so the footer sits at the bottom (window y 427), rows start at content y + 57, and there's 5 or 6 px of slack.
- **T7o · Focus and banned-control test:** add an editor test that builds `StemSplitterEditor` on each forced screen and walks every descendant. It asserts that none wants keyboard focus, that every `juce::Button` has `getMouseClickGrabsKeyboardFocus() == false`, and that nothing `dynamic_cast`s to ComboBox, TextEditor or ProgressBar. Also assert that the Browse label contains U+2026.
- **T7p · Standalone smoke test** (PowerShell):
  ```powershell
  $p = Start-Process -FilePath '<exe>' -PassThru
  Start-Sleep 5
  if ($p.HasExited) { throw 'standalone exited' }
  Stop-Process -Id $p.Id
  ```
- **T7q · Stale peak results after a switch:** add a test that calls `setStems` with 6 rows, sets `activeRow` 5, calls `setStems` with 4 rows, then runs `tick()` and pumps messages without crashing or going out of bounds.

**Task 8**
- **T8a · Wait for the peaks:** snapshots wait for `stems.hasPeaks()`, polling `runDispatchLoopUntil (50)` for up to 3 s, instead of a fixed 400 ms. `stems4-dusk-playing` works through T7f.
- **T8b · What the bench times:**
  - **`motion_frame_ms`:** bench the **whole editor**, not the bare background. Build `StemSplitterEditor` forced to Stems (4 stems) with vocals loaded and playing. Each frame: `ed.background().setMotion (...)`, then `juce::Graphics g (img)` on a 760×500 `SoftwareImageType` ARGB image, `g.reduceClipRegion (region)` where region is `motionRegion()` plus the active row's waveform and time bounds in editor coordinates, then `ed.paintEntireComponent (g, true)`.
  - **Extra metrics:** also print `motion_frame_ms_2x` (1520×1000 image with `g.addTransform (AffineTransform::scale (2))`) and `background_only_ms`.
  - **Budget:** it applies to `motion_frame_ms`.
  - **`process_block_us_preview`:** time bursts of ≤ 50 blocks with `Thread::sleep (60)` between bursts, outside the timed region. Assert `proc.preview.takePeak() > 0` after each burst, and print both the mean and the max.

**Task 9**
- **T9a · Install into FL's folder:** the controller installs the Release bundle into `C:\Program Files\Common Files\VST3` (an elevated copy, done by the controller rather than an implementer). The user then runs a click-through checklist in FL, recorded in the final report. It covers: both pills, Browse, drop, Cancel, play/pause/seek on each row, switching rows, dragging to the playlist, Copy, Try again, Find python.exe, the 4/6 switch while playing, and closing the window mid-split and while previewing. After each click, the spacebar must still toggle FL's transport.

## File map
```
CMakeLists.txt              fonts binary data, VST3+Standalone, LISN_SOURCES shared by plugin and lisn_tests
Resources/fonts/*.ttf       Unbounded-Bold, DMSans-Regular/Medium/Bold (+ OFL licences)
Source/Theme.h/.cpp         Theme table (dusk, midnight), FontSet + Fonts::display/body/mono
Source/PythonFinder.h/.cpp  candidate list, checkPython, findPython
Source/SeparationJob.h/.cpp ErrorKind, python search in run(), getPythonExe()
Source/StemPreview.h/.cpp   preview playback + peak meter
Source/Peaks.h/.cpp         readPeaks (waveform bars), lengthSeconds
Source/WaveGeometry.h/.cpp  kLayers table, edge/layer paths, motionOffset (port of Waves.dc.html)
Source/Frost.h/.cpp         frosted(): static blur for the glass panel
Source/WaveBackground.h/.cpp background + panel painter with motion
Source/Icons.h              SVG path strings from the mockup + iconPath()
Source/Controls.h/.cpp      SegmentedPill, LisnButton (primary/ghost/chip), CircleButton, dashed-rect helper
Source/StemRow.h/.cpp       one stem row + WaveformView
Source/UiState.h/.cpp       Screen enum, UiState, uiStateFor()
Source/Screens.h/.cpp       Header, DropScreen, SplittingScreen, StemsScreen, ErrorScreen
Source/PluginEditor.h/.cpp  composition, timer, preview control, motion, file choosers, drag-and-drop
Source/PluginProcessor.h/.cpp settings, startSplit, setSixStems, syncWithJob, preview in processBlock
tests/TestMain.cpp          UnitTestRunner main + --snapshots/--bench dispatch
tests/*Tests.cpp            juce::UnitTest suites
tests/Snapshots.cpp         snapshot + bench modes, synthetic stems
```

---

### Task 1: Build setup, fonts, theme table, test app

**Files:** Modify `CMakeLists.txt`, `.gitignore`. Create `Source/Theme.h`, `Source/Theme.cpp`, `tests/TestMain.cpp`, `tests/ThemeTests.cpp`.

**Produces:** `Theme`, `themeFor(id)`, `FontSet`, `Fonts::display/body/mono`, the `lisn_tests` target and the `LISN_SOURCES` list. Later tasks append their `.cpp` files to `LISN_SOURCES` and their test files to the `lisn_tests` sources.

- [ ] **Step 1: Rework `CMakeLists.txt`.**
  1. Keep the JUCE FetchContent block.
  2. Add `juce_add_binary_data(LisnFonts SOURCES Resources/fonts/Unbounded-Bold.ttf Resources/fonts/DMSans-Regular.ttf Resources/fonts/DMSans-Medium.ttf Resources/fonts/DMSans-Bold.ttf)`. The generated names are `BinaryData::UnboundedBold_ttf`, `DMSansRegular_ttf`, `DMSansMedium_ttf` and `DMSansBold_ttf`.
  3. Add `set(LISN_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/SeparationJob.cpp Source/Theme.cpp)`.
  4. Plugin target: keep the current `juce_add_plugin` arguments but use `FORMATS VST3 Standalone`. Add `target_sources(StemSplitter PRIVATE ${LISN_SOURCES})`, keep its definitions, and link `LisnFonts` privately.
  5. Test app: add `juce_add_console_app(lisn_tests PRODUCT_NAME "lisn_tests")`. Give it `target_sources(lisn_tests PRIVATE ${LISN_SOURCES} tests/TestMain.cpp tests/ThemeTests.cpp)` and the definitions `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 "JucePlugin_Name=\"LISN StemSplitter\""`. Link `LisnFonts juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags`.
  6. Tests: keep `progress_test`, and add `add_test(NAME lisn_tests COMMAND lisn_tests)`.
- [ ] **Step 2: `.gitignore`.** Add `.superpowers/`.
- [ ] **Step 3: `Source/Theme.h`.**
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Colours from docs/design (Waves.dc.html / Main.dc.html palettes). Text is always cream; ink is the dark text on cream.
struct Theme
{
    juce::String id;                                    // "dusk" or "midnight"
    juce::Colour ground, shadow, panelTint, pillTint;
    std::array<juce::Colour, 5> layers;                 // back to front
    juce::Colour vocals, drums, bass, guitar, piano, other;

    juce::Colour stemColour (const juce::String& stem) const;   // unknown stem -> other
    juce::Colour accent() const { return vocals; }

    static inline const juce::Colour cream { 0xffFFF4EA };
    static inline const juce::Colour ink   { 0xff2B1822 };
};

const Theme& themeFor (const juce::String& id);         // unknown id -> dusk

// Typefaces loaded once from BinaryData. The editor (and tests) keep a SharedResourcePointer<FontSet>
// member alive so the typefaces aren't reloaded on every call.
struct FontSet
{
    FontSet();
    juce::Typeface::Ptr display, regular, medium, bold;
};

namespace Fonts
{
    juce::Font display (float px);                       // Unbounded Bold
    juce::Font body (float px, int weight = 400);        // DM Sans 400 / 500 / 700
    juce::Font mono (float px);                          // Consolas
}
```
- [ ] **Step 4: `Source/Theme.cpp`.**
  - **Palettes:** two function-local `static const Theme` objects with the spec's exact values. `rgba(r,g,b,a)` maps to `juce::Colour ((uint8) r, (uint8) g, (uint8) b, a)`.
  - **`themeFor`:** returns `midnight` when `id == "midnight"`, otherwise `dusk`.
  - **`FontSet`:** loads the four typefaces with `juce::Typeface::createSystemTypefaceFor (BinaryData::X, (size_t) BinaryData::XSize)`.
  - **`Fonts::*`:** each one reads the typefaces through a local `juce::SharedResourcePointer<FontSet>` and returns `juce::Font (juce::FontOptions (tf).withPointHeight (px))`. The weight picks the typeface: 700+ bold, 500+ medium, otherwise regular.
  - **`mono`:** returns `juce::Font (juce::FontOptions ("Consolas", 0.0f, juce::Font::plain).withPointHeight (px))`.
- [ ] **Step 5: `tests/TestMain.cpp`.**
```cpp
#include <juce_gui_basics/juce_gui_basics.h>

int runSnapshots (const juce::File& outDir);   // tests/Snapshots.cpp (Task 8); until then define stubs here returning 1
int runBench();

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args[0] == "--snapshots") return runSnapshots (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
    if (args[0] == "--bench") return runBench();

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) failures += runner.getResult (i)->failures;
    return failures == 0 ? 0 : 1;
}
```
  Until Task 8, `runSnapshots` and `runBench` are stubs in `TestMain.cpp` that print "not implemented" and return 1. Task 8 moves them out.
- [ ] **Step 6: `tests/ThemeTests.cpp`** (`struct ThemeTests : juce::UnitTest`, registered statically). It checks:
  - `themeFor ("midnight").ground == Colour (0xff152238)`;
  - unknown ids return dusk;
  - `stemColour ("drums")` is correct in both themes, and an unknown stem gives `other`;
  - `layers[2]` is `#7B5467` for dusk and `#101A2E` for midnight;
  - `Fonts::display (22)` and `Fonts::body (14, 500)` have a non-null typeface whose family name contains "Unbounded" / "DM Sans".
- [ ] **Step 7:** Build all targets (`cmake --build build --config Debug`). Then run `build\lisn_tests_artefacts\Debug\lisn_tests.exe` (find the real path with `dir /s /b build\lisn_tests.exe`) and ctest; all must pass. Run pluginval; it must print SUCCESS. Commit: `feat: theme table, embedded fonts, lisn_tests runner, standalone build`.

---

### Task 2: Automatic Python detection and job error kinds

**Files:** Create `Source/PythonFinder.h/.cpp` and `tests/PythonFinderTests.cpp`. Modify `Source/SeparationJob.h/.cpp`, `Source/PluginProcessor.h/.cpp` and `CMakeLists.txt` (sources). Create `tests/ProcessorStateTests.cpp`.

**Produces:**
- `PythonStatus`, `PythonSearchDirs`, `pythonCandidates`, `checkPython`, `findPython`.
- `ErrorKind` with `SeparationJob::getErrorKind()` and `getPythonExe()`.
- On the processor: `theme`, `pythonPath` (a hint; empty means search), `startSplit(File)`, `setSixStems(bool)`, `isSixStems()` and `syncWithJob()`.

- [ ] **Step 1: `Source/PythonFinder.h`.**
```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <functional>

enum class PythonStatus { NotPython, NoDemucs, Ready };

struct PythonSearchDirs
{
    juce::String pathEnv;                                // PATH
    juce::File localAppData, programFiles, systemDrive, userProfile;
    static PythonSearchDirs fromEnvironment();           // skips unset/relative variables
};

// python.exe paths to try, first to last, without duplicates (case-insensitive):
// 1. hint (absolute path if it exists as a file; a bare name such as "python" is kept as-is for the OS to resolve)
// 2. <dir>\python.exe for each absolute PATH entry that exists, skipping any containing "WindowsApps"
// 3. localAppData\Programs\Python\Python3*, programFiles\Python3*, systemDrive\Python3* (each newest first by the number after "Python")
// 4. userProfile\anaconda3\python.exe, userProfile\miniconda3\python.exe
juce::StringArray pythonCandidates (const juce::String& hint, const PythonSearchDirs& dirs);

// Runs: exe -c "import importlib.util,sys;sys.exit(0 if importlib.util.find_spec('demucs') else 3)"
// exit 0 -> Ready, 3 -> NoDemucs; failure to start, any other exit code, timeout or abort -> NotPython (process killed).
PythonStatus checkPython (const juce::String& exe, int timeoutMs, const std::function<bool()>& shouldAbort = {});

struct PythonFound { juce::String exe; PythonStatus status = PythonStatus::NotPython; };

// First Ready candidate; else the first NoDemucs one; else { "", NotPython }. Stops early when shouldAbort() is true.
PythonFound findPython (const juce::String& hint, const PythonSearchDirs& dirs, const std::function<bool()>& shouldAbort = {});
```
- [ ] **Step 2: `Source/PythonFinder.cpp`.**
  - **`checkPython`:** start the process with `juce::ChildProcess::start (StringArray { exe, "-c", "<check>" }, 0)`, then poll `isRunning()` every 50 ms, checking `shouldAbort` and the timeout on each pass.
  - **Versioned folders:** find them with `findChildFiles (findDirectories, false, "Python3*")`. Sort descending by `getFileName().substring (6).getIntValue()`, so Python312 comes before Python311, then Python310, then Python39.
  - **Unset variables:** `fromEnvironment()` reads PATH, LOCALAPPDATA, ProgramFiles, SystemDrive (plus "\\") and the home directory. Never construct a `juce::File` from a relative or empty string, because JUCE asserts on that.
- [ ] **Step 3: `tests/PythonFinderTests.cpp`.**
  1. Build a fake tree under a temp dir, with empty files for every `python.exe`: `path1/python.exe`, `Microsoft/WindowsApps/python.exe`, `lad/Programs/Python/Python310/python.exe`, `lad/Programs/Python/Python312/python.exe`, `pf/Python311/python.exe`, `sd/Python39/python.exe` and `home/miniconda3/python.exe`.
  2. Set `pathEnv` to `path1;<WindowsApps dir>;relative\dir;;"<path1 quoted>"`, and the hint to `pf/Python311/python.exe`.
  3. Expect exactly `[pf311, path1, lad312, lad310, sd39, miniconda3]`: pf311 isn't repeated, and WindowsApps, the relative entry and the duplicate are all skipped.
  4. Test a bare hint: `"python"` stays as the first entry.
  5. `checkPython ("C:\\definitely\\missing.exe", 2000)` returns NotPython. `checkPython ("C:\\Windows\\System32\\where.exe", 5000)` also returns NotPython (non-zero exit).
  6. Only when the environment variable `LISN_INTEGRATION=1` is set: `findPython ("", fromEnvironment()).status == Ready`.
- [ ] **Step 4: `SeparationJob`.**
  - **New API:** add `enum class ErrorKind { None, PythonMissing, DemucsMissing, Failed };`, `std::atomic<ErrorKind> errorKind` with `getErrorKind()`, and `juce::String getPythonExe() const` (valid when Done or Failed). Rename the `python` parameter of `start` to `pythonHint`, and reset `errorKind` and `pythonExe` in `start`.
  - **Python search:** at the top of `run()`, call `findPython (hint, PythonSearchDirs::fromEnvironment(), [this] { return threadShouldExit(); })`.
    - If `threadShouldExit()` → `Cancelled`.
    - `NotPython` → `PythonMissing`.
    - Otherwise set `pythonExe = found.exe`; then `NoDemucs` → `DemucsMissing`, while `Ready` runs demucs with `pythonExe` (the existing code).
  - **Failure kinds:** exit code 9009 → `PythonMissing`, and any other failure → `Failed`, with the error tail as before. Write `errorKind` and `error` before the `state` store.
  - **Messages:** remove the "Set the Python path in settings" wording from error strings, because the UI builds its own text per kind.
- [ ] **Step 5: Processor.**
  - **Members:** `juce::String theme = "dusk";`. Change the `pythonPath` default to `""`.
  - **`startSplit (const juce::File& f)`:** `lastInput = f; stemDir = {}; job.start (f, model, pythonPath);`. Task 3 adds a preview stop.
  - **`setSixStems (bool six)`:** set `model` to `six ? "htdemucs_6s" : "htdemucs"`. If the model changed and `stemDir.isDirectory()` and the job isn't Running and `lastInput.existsAsFile()`, call `startSplit (lastInput)`.
  - **`syncWithJob()`:** if the job is Done and `stemDir` is empty, take the job's stem dir. If the job is Done, or Failed with a kind other than PythonMissing, and `getPythonExe()` isn't empty, set `pythonPath` to it.
  - **State:** `getStateInformation` calls `syncWithJob()`, then writes `theme` as well. `setStateInformation` reads `theme` (default "dusk") and `python` (default "").
- [ ] **Step 6: `tests/ProcessorStateTests.cpp`.**
  - A round trip of theme, model, pythonPath, lastInput and stemDir through get/setStateInformation.
  - Old XML (the version-1 format without `theme`, with `python="python"`) loads as theme `dusk` with pythonPath `python`.
  - `setSixStems (true)` with no input changes the model and leaves the job Idle.
- [ ] **Step 7:** Build, then run the tests with `LISN_INTEGRATION=1` once to confirm the machine's Python (`C:\Python311`) is found. Run pluginval. Commit: `feat: find Python automatically and report missing Python or Demucs`.

---

### Task 3: Stem preview playback in the processor

**Files:** Create `Source/StemPreview.h/.cpp` and `tests/StemPreviewTests.cpp`. Modify `PluginProcessor.h/.cpp` and `CMakeLists.txt`.

**Produces:** `StemPreview` and `processor.preview`. `processBlock` mixes the preview in, and `startSplit` stops it.

- [ ] **Step 1: `Source/StemPreview.h`.** Design constraint: **never call `transport.stop()`**. It busy-waits up to 500 × Sleep(2) until the audio thread renders another block (juce_AudioTransportSource.cpp:133-145), which would freeze FL's UI on every pause or row switch, and forever if the host isn't calling processBlock. Play/pause is our own atomic flag. The UI reads the position from an atomic, never through the transport's callbackLock.
```cpp
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

// Plays one stem file through the plugin's output, on top of the audio passing through.
// Message thread: load/unload/play/pause/seek/queries. Audio thread: addTo(). UI: takePeak()/takeReachedEnd().
class StemPreview
{
public:
    StemPreview();
    ~StemPreview();

    void prepare (double sampleRate, int maxBlockSize);   // from prepareToPlay
    void release();                                        // from releaseResources

    bool load (const juce::File& audioFile);   // pauses, then loads at position 0; false if unreadable (nothing loaded)
    void unload();
    juce::File getFile() const { return file; }
    void play();                                // no-op without a loaded file; restarts from 0 if the stream had finished
    void pause();                               // never blocks: the audio thread renders one faded-out block, then stops pulling
    bool isPlaying() const { return wanted.load(); }
    double getLengthSeconds() const { return lengthSec; }
    double getPositionFraction() const;         // 0..1, from readPos (no lock)
    void setPositionFraction (double);
    bool takeReachedEnd() { return reachedEnd.exchange (false); }   // true once after the stem played to its end

    void addTo (juce::AudioBuffer<float>& buffer);   // audio thread; idle = two relaxed atomic loads, then return
    float takePeak() { return peak.exchange (0.0f); }  // highest preview sample level since the last call

private:
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "LISN preview read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> scratch;
    std::atomic<bool> wanted { false }, prepared { false }, reachedEnd { false };
    std::atomic<juce::int64> readPos { 0 };      // host-rate samples, written by addTo and setPositionFraction
    std::atomic<float> peak { 0.0f };
    bool sounding = false;                       // audio thread only: last block was audible (for the fade-out block)
    double lengthSec = 0.0, hostRate = 44100.0;
    juce::File file;
};
```
- [ ] **Step 2: `Source/StemPreview.cpp`.**
  - **Construction:** the constructor calls `formats.registerBasicFormats(); readAhead.startThread();`. The destructor calls `wanted = false; transport.setSource (nullptr); readAhead.stopThread (2000);`.
  - **`prepare (sr, maxBlock)`:** set `prepared = false; scratch.setSize (2, maxBlock); hostRate = sr;`. Then call `transport.prepareToPlay (maxBlock, sr)` **twice**; the second call sizes the resampler for the ratio the first one set (juce_AudioTransportSource.cpp:233-237). Finally `prepared = true`. **`release()`:** `prepared = false; transport.releaseResources();`.
  - **`load (f)`:**
    1. `wanted = false; transport.setSource (nullptr); source.reset(); file = {}; lengthSec = 0; readPos = 0;`.
    2. Create a reader with `formats.createReaderFor (f)`, returning false on nullptr.
    3. Capture `srcRate = reader->sampleRate` and `lengthSec = reader->lengthInSamples / srcRate`.
    4. `source = std::make_unique<juce::AudioFormatReaderSource> (reader, true); transport.setSource (source.get(), 32768, &readAhead, srcRate, 2); transport.setPosition (0); transport.start(); file = f; return true;`.
    `start()` only sets flags under the lock; we gate audio with `wanted`, so it never makes sound on its own.
  - **`unload()`:** `wanted = false; transport.setSource (nullptr); source.reset(); file = {}; lengthSec = 0; readPos = 0;`.
  - **`play()`:** `if (source == nullptr) return; if (! transport.isPlaying()) { if (transport.hasStreamFinished()) { transport.setPosition (0); readPos = 0; } transport.start(); }`. The transport stops itself only at end of file, and a seek made after the end is kept. Then `wanted = true;`.
  - **`pause()`:** `wanted = false;` and nothing else.
  - **Position:** `getPositionFraction()` returns `lengthSec > 0 ? jlimit (0.0, 1.0, readPos / (lengthSec * hostRate)) : 0.0`. `setPositionFraction (f)` sets `transport.setPosition (f * lengthSec); readPos = (int64) (f * lengthSec * hostRate);`.
  - **`addTo`:**
```cpp
void StemPreview::addTo (juce::AudioBuffer<float>& buffer)
{
    const bool want = wanted.load (std::memory_order_relaxed);
    if ((! want && ! sounding) || ! prepared.load (std::memory_order_relaxed)) return;   // idle: nothing else runs
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    float blockPeak = 0.0f;
    for (int start = 0; start < total; start += scratch.getNumSamples())               // chunks if the host exceeds its block size
    {
        const int n = juce::jmin (scratch.getNumSamples(), total - start);
        juce::AudioSourceChannelInfo info (&scratch, 0, n);
        transport.getNextAudioBlock (info);
        if (! want) scratch.applyGainRamp (0, n, 1.0f, 0.0f);                          // the one faded block after pause()
        blockPeak = juce::jmax (blockPeak, scratch.getMagnitude (0, n));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, start, scratch, juce::jmin (ch, scratch.getNumChannels() - 1), 0, n);
        if (! want) break;
    }
    sounding = want && transport.isPlaying();
    if (want && ! transport.isPlaying()) { wanted = false; reachedEnd = true; }           // played to the end
    readPos.store (transport.getNextReadPosition(), std::memory_order_relaxed);
    if (blockPeak > peak.load (std::memory_order_relaxed)) peak.store (blockPeak, std::memory_order_relaxed);   // benign race with takePeak
}
```
  - **Lock note:** add `// ponytail: while previewing, the audio thread can briefly wait on BufferingAudioSource's lock during a 2048-frame disk read, and load() swaps sources without a fade (a click is possible when switching rows); idle blocks take no lock. Upgrade path: decode the stem into memory on load and play it lock-free.`
- [ ] **Step 3: Processor.**
  - Add the member `StemPreview preview;`.
  - `prepareToPlay` calls `preview.prepare`, and `releaseResources` calls `preview.release`.
  - `processBlock` becomes just `preview.addTo (buffer);`. Put no ScopedNoDenormals here; it lives inside addTo after the idle return.
  - `startSplit` calls `preview.unload()` first.
- [ ] **Step 4: `tests/StemPreviewTests.cpp`.** A helper writes a 2 s, 44.1 kHz stereo WAV into a temp file (sine at 440 Hz, amplitude 0.5) with `juce::WavAudioFormat().createWriterFor`. Call `juce::Thread::sleep (30)` after every `load()` and every `setPositionFraction()` before pulling blocks, so the read-ahead thread fills its buffer. Then:
  1. `prepare (48000, 512)` and `load` succeed, and the length ≈ 2.0 s (±0.01).
  2. **Not playing:** fill the buffer with 0.25, call `addTo`, and every sample must still be exactly 0.25.
  3. **Playing:** `play()`, then 20 blocks. The RMS of blocks 3–20 is > 0.2, `takePeak()` is in 0.45–0.55, and a second `takePeak()` returns 0.
  4. **Position:** after the 20 blocks it's within 0.05 s of 20·512/48000. That checks resampling, because the file is 44.1 kHz and the host 48 kHz.
  5. **Pause:** `pause()` returns in < 20 ms (time it). The next block is the fade-out block (RMS falls across it), and the block after that is unchanged (0.25 fill preserved). The position is then stable over 5 more blocks.
  6. **Seek:** `setPositionFraction (0.5)` gives `getPositionFraction()` within 0.01 of 0.5.
  7. **End of file:** seek to 0.98, sleep 50 ms, then `play()` and pull blocks with `juce::Thread::sleep (1)` between them (cap 400). `isPlaying()` must become false, and `takeReachedEnd()` returns true once, then false.
  8. **Bad files:** `load` returns false for a missing file and for a `.txt` file.
  9. **No stall with a live audio thread:** a `std::thread` calls `addTo` on a 2×512 buffer every 5 ms. Meanwhile the test thread calls `play()`, sleeps 50 ms, and checks that each of `pause()`, `play()`, `load (sameFile)` (< 100 ms, since it includes the prefill) and `unload()` returns within its bound (< 20 ms unless stated). Then stop and join the thread.
  10. **Oversized block:** after `prepare (48000, 256)`, a 1024-sample block while playing is filled completely; the RMS of its last 256 samples is > 0.2.
- [ ] **Step 5:** Build, run the tests and pluginval. Commit: `feat: stem preview playback mixed into the plugin output`.

---

### Task 4: Pure helpers: peaks, wave geometry, frost

**Files:** Create `Source/Peaks.h/.cpp`, `Source/WaveGeometry.h/.cpp`, `Source/Frost.h/.cpp` and `tests/PeaksTests.cpp`, `tests/WaveGeometryTests.cpp`, `tests/FrostTests.cpp`. Modify `CMakeLists.txt`.

- [ ] **Step 1: `Peaks`.**
```cpp
// Peak level (0..1, loudest channel) of `bars` equal slices of the file. Returns early (remaining bars 0) when shouldAbort() is true.
std::vector<float> readPeaks (juce::AudioFormatReader& reader, int bars, const std::function<bool()>& shouldAbort = {});
double lengthSeconds (const juce::AudioFormatReader& reader);   // 0 when sampleRate is 0
```
  - **Slices:** read each slice with `reader.readMaxLevels (start, end - start, ranges.data(), (int) ranges.size())`, where slice `i` spans `[len·i/bars, len·(i+1)/bars)`. `ranges` is a `std::vector<juce::Range<float>>` sized to `min(numChannels, 8)`; the channel count passed must equal `ranges.size()` (juce_AudioFormatReader.cpp:218-221). Return all zeros when `numChannels == 0`, `lengthInSamples <= 0` or `bars <= 0`, and check `shouldAbort` between slices.
  - **Level:** `min(1, max(|range.start|, |range.end|))` over the channels.
  - **Test:** build an in-memory mono 44.1 kHz WAV with `MemoryOutputStream` and `WavAudioFormat`, made of 4 equal segments: silence, a 0.5 sine, silence, a 1.0 sine. `readPeaks (reader, 4)` must be ≈ [0, 0.5, 0, 1] ± 0.02, and `readPeaks (reader, 0)` is empty.
- [ ] **Step 2: `WaveGeometry`** (an exact port of `docs/design/Waves.dc.html`).
```cpp
struct LayerShape { float xTop, xBottom, amp, wavelength, phase, speed, push; };
inline const std::array<LayerShape, 5> kLayers {{      // back to front, from Waves.dc.html
    { 500.0f, 980.0f, 26.0f, 260.0f, 0.3f, 0.6f,  4.0f },
    { 330.0f, 800.0f, 30.0f, 240.0f, 1.7f, 0.8f,  7.0f },
    { 180.0f, 620.0f, 28.0f, 230.0f, 3.1f, 1.0f, 10.0f },
    { -120.0f, 400.0f, 26.0f, 220.0f, 4.4f, 1.2f, 13.0f },
    { -330.0f, 180.0f, 22.0f, 210.0f, 5.2f, 1.4f, 16.0f } }};

namespace WaveGeometry
{
    constexpr float edgeTop = -80.0f, edgeBottom = 580.0f;      // the edge runs from (xTop, edgeTop) to (xBottom, edgeBottom)
    float edgeLength (const LayerShape&);
    juce::Point<float> direction (const LayerShape&);   // unit vector top -> bottom
    juce::Point<float> normal (const LayerShape&);      // (dy, -dx) / len: points out of the filled side
    // Edge point at distance s along the line: P0 + dir*s + normal*(push + amp*sin(2*pi*s/wavelength + phase)).
    juce::Point<float> edgePoint (const LayerShape&, float s, float phase, float push);
    // Filled region left of the edge, like region() in Waves.dc.html. The wavy part is sampled every edgeLength/28
    // for s in [-extendBack - step, len + step], smoothed with the mockup's Catmull-Rom → cubic rule
    // (c1 = b + (c - a)/6, c2 = c - (e - b)/6). It continues straight along the edge for 4000 px at both ends
    // and closes 4000 px out along -normal, so sliding the layer by up to extendBack along the edge leaves no gap.
    juce::Path layerPath (const LayerShape&, float phase, float push, float extendBack = 0.0f);
    // Offset for drawing the pre-rendered layer: direction * fmod(flow*speed, wavelength) + normal * (pulse*push).
    juce::Point<float> motionOffset (const LayerShape&, float flow, float pulse);
}
```
  **Tests:**
  - **(a) Mockup fidelity:** port the mockup `region()` literally into the test as `mockupRegion (shape)` (corners −140,−140 and −140,660, 28 steps). Then `layerPath (shape, shape.phase, 0, 0)` and `mockupRegion` must agree on `contains()` for every point of a 10 px grid over 0..760 × 0..500. Skip grid points within 1.5 px of either path's edge, measured by testing the 4 neighbours at ±1.5 px for disagreement.
  - **(b) Motion equivalence:** for flow ∈ {0, 37, 300, 1234}, pulse ∈ {0, 0.5, 1} and every layer, compare the base path `layerPath (shape, shape.phase, 0, shape.wavelength + 40)` translated by `motionOffset (shape, flow, pulse)` with the direct path `layerPath (shape, shape.phase − 2π·fmod(flow·speed, wavelength)/wavelength, pulse·push, 0)`. They must agree on the same grid, with the same edge tolerance.
  - **(c) Unit vectors:** `direction` and `normal` have length 1 and are perpendicular.
- [ ] **Step 3: `Frost`.**
```cpp
// In-place Gaussian approximation on a SoftwareImageType image (SingleChannel or ARGB, premultiplied):
// three running-sum box blurs horizontally, then three vertically, with r = max(1, round((sqrt(1 + 4*sigma*sigma) - 1) / 2))
// (a box of radius r has variance r(r+1)/3, so three passes give sigma^2 = r(r+1)), clamping at the edges. sigma is in this image's pixels.
void blurImage (juce::Image& image, float sigma);

// Static frosted-glass blur (CSS blur(radius) = Gaussian with sigma = radius px): shrink 4x with high resampling quality,
// blurImage (small, radius / 4), then scale back up with high resampling quality.
// The result is ARGB, the same size as src, and a SoftwareImageType image.
juce::Image frosted (const juce::Image& src, float radius);
```
  - **Implementation:** read and write through `Image::BitmapData (readWrite)`, using `pixelStride` and `lineStride` so the same code serves SingleChannel (1 byte) and ARGB (4 bytes, every channel). `WaveBackground` also uses `blurImage` for its layer and panel shadows.
  - **Tests:**
    - A uniform 200×120 image stays uniform (±2 per channel) with `frosted (img, 18)`.
    - A black image with a 20×20 white square in the middle, through `frosted (img, 4.0f)`: the centre stays bright (>200), and a pixel 12 px outside the square is >0 and <255.
    - The output size equals the input size.
    - **Half-plane, SingleChannel 300×100:** the left half is 255, and `blurImage (mask, 14)` runs at full resolution. The alpha is ≈128 (±12) at the edge column and ≈40 (±10) 14 px outside it, which is one σ.
- [ ] **Step 4:** Build and test. Commit: `feat: peaks, wave geometry port and frost blur`.

---

### Task 5: `WaveBackground` component

**Files:** Create `Source/WaveBackground.h/.cpp` and `tests/WaveBackgroundTests.cpp`. Modify `CMakeLists.txt`.

**Consumes:** `Theme`, `kLayers`, `WaveGeometry`, `frosted`.

```cpp
class WaveBackground : public juce::Component
{
public:
    WaveBackground();                                       // not opaque-intercepting: setInterceptsMouseClicks (false, false)
    void setTheme (const Theme&);                          // repaints; drops only the frost cache
    void setPanel (juce::Rectangle<int> bounds, float cornerRadius, float tintAlpha);
    void setMotion (float flow, float pulse);              // no-op if unchanged; else repaint (motionRegion())
    juce::RectangleList<int> motionRegion() const;         // local bounds minus panel, plus the panel's 4 corner squares (radius+2)
    void paint (juce::Graphics&) override;
};
```
- [ ] **Step 1: The paint order**, which must match `Main.dc.html` and `Waves.dc.html`:
  1. Fill the ground.
  2. For each layer, back to front: draw its shadow mask filled with `theme.shadow`, then its shape mask filled with `theme.layers[i]`. Both go at `cacheOrigin + motionOffset (kLayers[i], flow, pulse)`.
  3. Draw the panel's drop shadow image.
  4. Clip to the panel rounded rect and draw the frost image.
  5. Fill `theme.panelTint.withAlpha (tintAlpha)` inside the rounded rect.
  6. Stroke a 1 px `cream.withAlpha (0.14f)` border along the rounded rect (inset 0.5 px).
- [ ] **Step 2: The caches**, built lazily inside `paint`.
  - **Scale and image type:** `scale = jlimit (1.0f, 2.0f, g.getInternalContext().getPhysicalPixelScaleFactor())`. JUCE 8.0.4 has **no** "preferred image type" query, so choose the type from the context:
```cpp
const bool software = dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr;
const juce::ImageType& type = software ? static_cast<const juce::ImageType&> (softwareType) : nativeType;   // members: juce::SoftwareImageType softwareType; juce::NativeImageType nativeType;
```
    Rebuild everything when `(scale, software)` changes. Window paints **and** `createComponentSnapshot` use the native (Direct2D) branch on Windows. Only images created explicitly with `SoftwareImageType` (the Task 5 test and the Task 8 bench) take the software branch.
  - **No `juce::DropShadow`.** Its radius r gives σ = √(4r/3), 3–4× tighter than the CSS values. Build every soft shadow with Frost's box blur (Task 4 `blurImage`, which handles SingleChannel and ARGB) on a quarter-resolution software image, then upscale to full size with high resampling quality and `type.convert (...)`.
  - **Layer masks:** two `Image::SingleChannel` masks per layer, independent of theme:
    - **shape:** `layerPath (shape, shape.phase, 0, shape.wavelength + 40)` filled in white at full cache resolution;
    - **shadow:** the same path translated by (12, −4), filled into a quarter-resolution software mask, then blurred with **σ = 14** (CSS `drop-shadow(12px -4px 14px)`: the third length is the Gaussian standard deviation). In quarter-resolution pixels that is σ = 14·scale/4.
    - **Mask bounds:** the window, expanded 40 px, united with that same rect translated by −(direction·(wavelength+40)) and −(normal·push). Expand the shadow mask bounds by 3σ (42 px) more. Render at `scale` using the transform `translated (-bounds.origin).scaled (scale)`.
    - **Drawing:** use `drawImageTransformed (mask, AffineTransform::scale (1/scale).translated (snapped origin), true)` so the current colour fills the mask. Snap the offset to whole physical pixels to avoid resampling.
    - **Bench fallback:** if the bench misses its budget, pre-composite shadow + shape per layer into one ARGB image per theme. Source-over is associative, so the result is exact.
  - **Panel shadow:** CSS `0 26px 50px -22px rgba(20,8,16,.75)` is the panel rect reduced by 22 px (the spread shrinks the corner radius too, 22 − 22 = 0) and offset (0, 26), blurred with **σ = 25** (box-shadow blur 50 px = 2σ). Colour it `Colour (20, 8, 16).withAlpha (0.75f)` into an ARGB image once per scale. CSS clips a box-shadow under its own box, so the panel's frost and tint cover the part inside the panel.
  - **Frost:** render the full static background (flow 0, pulse 0, no panel) at `scale` **directly** (fill the layer paths and blur their shadows as above) into a software ARGB image. Don't draw it from the cached masks, which may be GPU images. Then apply `frosted (image, 18 * scale)` and `type.convert (...)`. The cache is keyed by (theme id, scale, software) and is only drawn inside the panel clip.
- [ ] **Step 3: Tests.**
  1. `motionRegion()` contains (10, 10), (380, 40) and (5, 300) and the panel corner (26, 78), but not the panel centre (380, 276).
  2. `setMotion` with the same values twice doesn't crash.
  3. **Painting into a software image:** make a 760×500 ARGB `SoftwareImageType` image with a panel (24, 76, 712, 400) at radius 22 and tint 0.66, and paint the dusk theme through `paintEntireComponent`.
     - Pixel (740, 10) ≈ ground #6F7679 (±6).
     - Pixel (120, 30) ≈ plum #7B5467 (±10).
     - A panel-centre pixel is darker than the ground.
     - With midnight, (740, 10) ≈ #152238.
- [ ] **Step 4:** Build and test. Commit: `feat: wave background with cached layers, frosted panel and motion`.

---

### Task 6: Icons, controls, stem row

**Files:** Create `Source/Icons.h`, `Source/Controls.h/.cpp`, `Source/StemRow.h/.cpp` and `tests/StemRowTests.cpp`. Modify `CMakeLists.txt`.

**Consumes:** `Theme`, `Fonts`, `readPeaks`.

- [ ] **Step 1: `Icons.h`.**
  - **Path data:** copy these SVG `d` strings verbatim from the mockups: `file` (header row), `plus`, `upload`, `alert`, `copy`, `grip`, `play`, `pause`, and the stem icons `vocals`, `drums`, `bass`, `guitar`, `piano` and `other` (Main.dc.html `stemDefs`).
  - **Helper:** `inline juce::Path iconPath (const char* d, juce::Rectangle<float> area)` parses with `juce::Drawable::parseSVGPath` and maps the 24×24 viewBox to `area`.
  - **Stroking:** use `PathStrokeType (width * area.getWidth() / 24, curved, rounded)`.
  - **Lookup:** `inline const char* stemIcon (const juce::String& stem)`, with unknown stems mapping to `other`.
- [ ] **Step 2: `Controls`.** Every control calls `setWantsKeyboardFocus (false)`; Buttons also call `setMouseClickGrabsKeyboardFocus (false)`.
  - **`SegmentedPill (StringArray items)`**
    - **API:** `setSelected (int)`, `std::function<void(int)> onChange`, `setTheme (const Theme&)` and `int preferredWidth() const`.
    - **Container:** a full-radius rounded rect filled with `pillTint`, a 1 px `cream@0.14` border and 3 px padding.
    - **Items:** 30 px tall with a 14 px side padding, DM Sans Bold 13, and 2 px gaps.
    - **States:** the active item is a cream fill with ink text; inactive items are transparent with cream text.
  - **`LisnButton`** with a style enum:
    - `Primary`: cream fill, ink text, radius 10;
    - `Ghost`: fill `cream@0.06`, a 1 px border (`cream@0.22`, or `0.3` for Cancel and the error buttons), cream text, radius 10;
    - `Soft`: fill `cream@0.12`, radius 8 (the Copy button).
    - **Fields:** optional icon (d string, size, stroke width), height, left and right padding, font size (Bold) and `preferredWidth()`.
  - **`CircleButton`:** diameter 38, filled with the given colour, with the play or pause icon (16 px) in ink. Play is shifted +2 px in x.
  - **Helper:** `void drawDashedRoundedRect (Graphics&, Rectangle<float>, float radius, float thickness, float dash, float gap, Colour)`, built from `PathStrokeType::createDashedStroke`.
- [ ] **Step 3: `StemRow`** (Main.dc.html row; the numbers are CSS px).
```cpp
class StemRow : public juce::Component
{
public:
    StemRow (juce::File wav, juce::String stemKey);  // stemKey = lower-case file name without extension
    void setTheme (const Theme&);
    void setCompact (bool sixStems);                 // rows 42 / waveform 26 when true, 62 / 40 when false
    void setPeaks (std::vector<float> levels);       // bar levels 0..1, 104 bars (4 stems) or 96 (6 stems)
    void setPlayback (bool playing, double fraction, double lengthSeconds);
    std::function<void()> onPlayPause;
    std::function<void(double)> onSeek;              // fraction 0..1
    juce::File getFile() const;
    juce::String getStemKey() const;
    int barCount() const;                            // 104 or 96
};
```
  - **Layout** (row width 674, padding 0 10, gap 14, left to right):
    - name block, 118 wide: the badge is 32×32 with radius 10, filled with the stem colour at 0.16, holding the stem icon at 18 px with stroke 2 in the stem colour. The text sits at x+42: name in DM Sans Bold 14, with the file name (`vocals.wav`) below in 11 px at cream 0.6;
    - `CircleButton` of 38: cream fill when idle, stem colour when playing;
    - `WaveformView`, 330 × waveH, a child component with 5 px of vertical margin for the playhead;
    - time, 32 wide, right-aligned, 12 px at cream 0.72, `m:ss`;
    - drag chip, 32 tall with padding 0 12 0 8: the grip icon at 16 px with stroke 3, "Drag" in DM Sans Bold 12 at cream 0.82, a 1 px dashed border at cream 0.3 and radius 10.
  - **Row states:** a playing row gets a radius-14 fill of `cream@0.08`. The name block, time and chip are painted by the row itself, so the row gets their mouse events.
  - **`WaveformView` bars:** step = 330/bars, bar width = step·0.6, and half-height = max(0.75, level·(h/2 − 0.5)), mirrored around the middle. Draw them in stem colour 0.3, then again in the full stem colour clipped to `x < fraction·w`.
  - **`WaveformView` playhead:** when fraction > 0, a 2 px cream bar spanning from y −5 to h+5.
  - **`WaveformView` mouse:** mouseDown and mouseDrag call `onSeek (clamp (x / w, 0, 0.999))`.
  - **Drag out:** on `mouseDrag` in the row, once `getDistanceFromDragStart() > 4`, call `juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this)`, at most once per gesture (reset in `mouseDown`). The row uses the DraggingHand cursor; the play button and waveform use PointingHand.
- [ ] **Step 4: Tests.**
  - In non-compact mode, laid out at 674×62: the play button bounds x = 142, w = 38; the waveform x = 194, w = 330; the chip ends at ≤ 664.
  - `setCompact (true)` makes the row height 42 and the waveform 26 tall (plus margins).
  - `barCount()` is 104 or 96.
  - `setPeaks` of the wrong size is ignored or padded without a crash.
- [ ] **Step 5:** Build and test. Commit: `feat: icons, pill and button controls, stem row with waveform`.

---

### Task 7: UiState, screens, header, editor

**Files:** Create `Source/UiState.h/.cpp`, `Source/Screens.h/.cpp` and `tests/UiStateTests.cpp`. Rewrite `Source/PluginEditor.h/.cpp`. Modify `CMakeLists.txt`.

**Consumes:** everything above.

- [ ] **Step 1: `UiState`.**
```cpp
enum class Screen { Drop, Splitting, Stems, Error };
struct UiState
{
    Screen screen = Screen::Drop;
    double progress = 0.0;                   // Splitting
    ErrorKind error = ErrorKind::None;       // Error
    juce::String errorText, pythonExe;       // Error
    juce::File stemDir;                      // Stems
    juce::String songName;                   // lastInput file name
    bool sixStems = false;
    bool sameScreenAs (const UiState& o) const;   // every field except progress
};
// Pure mapping, testable without a running job:
UiState uiStateFor (JobState, ErrorKind, double progress, const juce::String& errorText, const juce::String& pythonExe,
                    const juce::File& jobStemDir, const juce::File& savedStemDir, const juce::File& lastInput, bool sixStems);
UiState uiStateFor (const StemSplitterProcessor&);   // reads the job + processor fields
```
  - **Mapping:**
    - Running → Splitting.
    - Failed → Error.
    - Done → Stems, with the job's stem dir.
    - Idle or Cancelled → Stems if `savedStemDir` contains a `.wav`, otherwise Drop.
  - **Tests:** every branch, and `sameScreenAs` ignoring progress.
- [ ] **Step 2: `Screens`.** Each screen is transparent (painting only its own content), takes no keyboard focus and has `setTheme`.
  - **`Header`** (Main.dc.html header, bounds 24,16,712,44):
    - "LISN" in `Fonts::display (22)` with kerning 0.08, then 10 px, then "StemSplitter" in `Fonts::body (14, 500)`, aligned on the baseline.
    - At the right: `SegmentedPill {"Dusk","Midnight"}`, an 8 px gap, then `SegmentedPill {"4 stems","6 stems"}`.
    - Callbacks `onTheme (juce::String id)` and `onSixStems (bool)`. `setEnabledSwitches (bool)` disables both pills and sets their alpha to 0.55.
  - **`DropScreen`** (Empty.dc.html; panel tint 0.60):
    - inside the panel, 14 px padding and a 2 px dashed border at cream 0.36 with radius 16 (dash 6, gap 5);
    - a centred column with 14 px gaps: a 64 circle at cream 0.12 with the upload icon (28, stroke 1.8), "Drop a song here" in `display (24)`, then the row "or" (14 px, cream 0.72) with a Primary "Browse…" button (34 tall, padding 16, font 13), then "WAV · MP3 · FLAC · AIFF · OGG" (12 px, cream 0.62, kerning 0.06), then 10 px of margin, "You'll get these stems" (12 px, cream 0.62) and the chips row;
    - chips are 28 tall with padding 0 12 0 10, full radius, a `cream@0.08` fill, an 8 px dot in the stem colour, a 7 px gap and Bold 12. Chips are 6 px apart and follow `setSixStems`;
    - `setHighlighted (bool)` raises the border alpha to 0.7 and adds a `cream@0.04` fill;
    - callback `onBrowse`.
  - **`SplittingScreen`** (Separating.dc.html; tint 0.66): a centred column with 18 px gaps.
    - The song name (Bold 15), then 4 px, then "Splitting into vocals · drums · bass · other" (six names in 6-stem mode; 13 px, cream 0.68).
    - The percentage in `display (40)`.
    - A 440×40 wavy progress line: points x = 4..436 in steps of 2 at y = 20 + 8·sin(2πx/55). The track is stroked 4 px with rounded caps in cream 0.2, and the part with x ≤ 440·progress in `theme.accent()`. The knob is a 14 px cream circle with a 4 px ring of `accent@0.35`, placed on the curve at 440·progress.
    - A Ghost "Cancel" button (36 tall, padding 18, border 0.3).
    - The note text (12 px, line height 1.5, cream 0.6, max width 420, centred).
    - `setProgress (double)` repaints only when the whole percentage changes. Callback `onCancel`.
  - **`StemsScreen`** (Main.dc.html panel; tint 0.66; padding 16 18; gap 10):
    - the top row (36): a 36 icon box with radius 10 at `cream@0.1` holding the file icon (18, stroke 1.8), the song name (Bold 15) with "m:ss · split into N stems" below (12 px, cream 0.64), and a Ghost "New song" button with the plus icon (34 tall, padding 0 14 0 10, border 0.22) → `onNewSong`;
    - a 1 px `cream@0.12` divider;
    - the rows (4 stems: 62 tall, 10 apart; 6 stems: 42 tall, 5 apart), ordered vocals, drums, bass, guitar, piano, other, then unknown stems alphabetically;
    - the footer (16): a play icon (14) and "Play a stem to hear it, then drag it onto any track." (12 px, cream 0.62).
    - **`setStems (dir, sixStems, songName)`:** builds the rows, then computes peaks (`barCount()` bars) and the duration of the first stem on a single-thread `juce::ThreadPool`. Results come back through `juce::MessageManager::callAsync` with a `Component::SafePointer` guard.
    - **Row positions:** kept in `std::vector<double> positions`. Callbacks `onPlayPause (int row)` and `onSeek (int row, double fraction)`, plus `setPlayback (int activeRow, bool playing, double activeFraction, double activeLength)`.
    - **Shutdown:** the destructor calls `pool.removeAllJobs (true, 2000)`.
  - **`ErrorScreen`** (PythonMissing.dc.html; tint 0.70): a 520-wide column with 16 px gaps.
    - Row: a 44 circle filled with the drums colour at 0.18, holding the alert icon (22, stroke 2, drums colour), and the title in `display (20)`.
    - The body (14 px, line height 1.55, cream 0.8).
    - The code box (44 tall, padding 0 6 0 16, radius 12, fill `rgba(12,6,10,.55)`, a 1 px `cream@0.12` border), holding the text in `Fonts::mono (14)` and a Soft "Copy" button with the copy icon (32 tall, padding 0 12 0 10, font 12).
    - Buttons (4 px above, 10 apart): a Primary "Try again" (38 tall, padding 18) and a Ghost "Already installed? Find python.exe…" (38 tall, padding 16, border 0.3).
    - **Per kind:** use the spec's texts. For DemucsMissing the code is `"<pythonExe>" -m pip install demucs`, or `python -m pip install demucs` when pythonExe is empty. For Failed, the title is "The split didn't finish", the body is "Demucs stopped with this message:", the code box grows to show the last 6 lines and the python button is hidden.
    - Callbacks `onRetry` and `onFindPython`. Copy puts the code text on `SystemClipboard`.
- [ ] **Step 3: Editor.**
```cpp
class StemSplitterEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor&);
    ~StemSplitterEditor() override;                  // proc.preview.pause()
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;   // one file, extension wav;mp3;flac;aif;aiff;ogg
    void fileDragEnter (const juce::StringArray&, int, int) override;  // highlight the drop zone on Drop
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;   // proc.startSplit (file)
    // test hooks
    void forceState (const UiState&);                // the timer then uses this instead of uiStateFor(proc)
    void tick();                                     // one timer step (also called by timerCallback)
    WaveBackground& background() { return waves; }
private:
    void timerCallback() override { tick(); }
    juce::SharedResourcePointer<FontSet> fonts;     // first member: keeps the typefaces loaded
    // ...
};
```
  - **Size and stacking:** `setSize (760, 500)`, with the children `waves` (full), `header`, then the four screens. Each screen sits at the panel content rect for its padding. Call `setWantsKeyboardFocus (false)` on the editor. Start the timer with `startTimerHz (30)`.
  - **`tick()` step 1, state:** call `proc.syncWithJob()`, compute `s` (the forced state or `uiStateFor (proc)`), and if it isn't `sameScreenAs` the shown state, call `show (s)`. `show` makes one screen visible, calls `waves.setPanel (panel, 22, tint)` with that screen's tint, sets `header.setEnabledSwitches (s.screen != Screen::Splitting)` and the header selections, stops the preview (`activeRow = -1; proc.preview.unload()`) when leaving Stems, and calls `stems.setStems (...)` when the stem dir or stem count changes.
  - **`tick()` step 2, progress:** on Splitting, call `splitting.setProgress`.
  - **`tick()` step 3, playback:** if `activeRow ≥ 0`, read the preview's position and playing state. If it isn't playing and fraction ≥ 0.999, reset to 0 with `setPositionFraction (0)`. Then store the position and call `stems.setPlayback`.
  - **`tick()` step 4, motion:** measure dt from `juce::Time::getMillisecondCounterHiRes()`, clamped to [0, 0.1].
    - While playing: `peak = proc.preview.takePeak()`, `pulse = peak > pulse ? peak : pulse + (peak − pulse)·min(1, dt·5)`, `flow += dt·(8 + 60·pulse)`, then `waves.setMotion (flow, pulse)`.
    - Else if pulse > 0.01: `pulse *= pow(0.05, dt)`, `flow += dt·8·pulse`, then `setMotion`.
    - Else if pulse ≠ 0: `pulse = 0`, then `setMotion (flow, 0)`.
  - **Row events:**
    - `onPlayPause (i)`: if row i is active and playing, pause it. Otherwise save the old row's position, `proc.preview.load (row file)`; if that succeeds, set the position fraction to `positions[i]`, call `play()` and set `activeRow = i`.
    - `onSeek (i, f)`: set `positions[i] = f`, and if row i is active, `setPositionFraction (f)`.
  - **Header:**
    - `onTheme`: set `proc.theme` and apply `themeFor` to every child and to `waves`.
    - `onSixStems`: call `proc.setSixStems`.
  - **Screen buttons:**
    - Browse and New song open `chooser = std::make_unique<juce::FileChooser> ("Choose a song", {}, "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg")` with `launchAsync (openMode | canSelectFiles, ...)` and call `startSplit` if a file is chosen.
    - Find python.exe uses the pattern `"*.exe"`, sets `proc.pythonPath`, then calls `proc.startSplit (proc.lastInput)`.
    - Retry calls `startSplit (lastInput)`, and Cancel calls `proc.job.cancel()`.
  - **Removals:** delete the old `StemTile`, `modelBox`, `pythonField`, `progressBar` and `status`.
- [ ] **Step 4:** Build, then run the tests and pluginval (Debug). Launch the Standalone exe (`build\StemSplitter_artefacts\Debug\Standalone\LISN StemSplitter.exe`) for 5 s and confirm it's still running: `start ""`, then `timeout 5`, then `tasklist | findstr "LISN"`, then `taskkill`. Commit: `feat: new UI screens, header switches and editor state machine`.

---

### Task 8: Snapshot + bench harness, visual pass

**Files:** Create `tests/Snapshots.cpp` (remove the stubs from `TestMain.cpp`). Modify `CMakeLists.txt`.

- [ ] **Step 1: Synthetic stems.** `makeStems (dir, six)` writes 30 s, 44.1 kHz, 16-bit stereo WAVs:
  - vocals: 300 Hz sine phrases (0.05–0.3, 0.35–0.6, 0.66–0.95 of the length) at 0.6;
  - drums: 60 ms decaying noise bursts every 0.5 s at 0.9, with alternate bursts at 0.6;
  - bass: a 55 Hz sine at 0.55, with a gap at 0.47–0.52;
  - other: noise rising from 0.2 to 0.6;
  - with `six`, also guitar (a 220 Hz sine with a 6 Hz tremolo) and piano (440 Hz notes decaying every 1 s).
- [ ] **Step 2: `runSnapshots (outDir)`.** For each case:
  1. **Setup:** a fresh `StemSplitterProcessor`, `prepareToPlay (48000, 512)`, theme and model set, `lastInput = temp/"Travis Snippet.mp3"`, and `stemDir = stems dir`.
  2. **Editor:** `StemSplitterEditor ed (proc)` with `forceState`, then `MessageManager::getInstance()->runDispatchLoopUntil (400)` so the peaks arrive.
  3. **Render:** `ed.createComponentSnapshot (ed.getLocalBounds(), true, scale)` → `PNGImageFormat` → `<outDir>/<name>.png`, overwriting.

  **Cases:**
  - `drop-dusk`, `drop-midnight-6`;
  - `splitting-dusk` (0.58), `splitting-midnight` (0.58);
  - `stems4-dusk-playing`: vocals loaded, position 0.38, `play()` without an audio callback, then `tick()`;
  - `stems6-midnight`;
  - `error-python-dusk`;
  - `error-demucs-midnight` with pythonExe `C:\Python311\python.exe`;
  - `error-failed-dusk` with a 6-line error text;
  - `motion-dusk`: stems4 paused, then `ed.background().setMotion (140, 0.9)` before rendering;
  - `drop-dusk@2x` and `stems4-dusk@2x` at scale 2.

  Print each path.
- [ ] **Step 3: `runBench()`.** Use software images throughout and print one `name=value` line per metric.
  - `motion_frame_ms`: paint a `WaveBackground` (760×500, dusk, panel set) into a 760×500 ARGB `SoftwareImageType` image clipped to `motionRegion()`. Average 120 frames, with flow growing by 2 per frame and pulse alternating 0.2/0.9, after 5 warm-up frames.
  - `full_paint_ms`: 20 full unclipped paints.
  - `process_block_us_idle`: 20 000 `processBlock` calls with a 2×512 buffer and no preview playing.
  - `process_block_us_preview`: 2000 calls with the synthetic vocals playing.

  Return 1 if `motion_frame_ms > 4.0`, otherwise 0.
- [ ] **Step 4:** Build. Run `lisn_tests --snapshots snapshots` from the repo root (add `snapshots/` to `.gitignore`), then `lisn_tests --bench`. **Open every PNG with the Read tool** and compare it with the mockup numbers in `docs/design/*.dc.html`. Check positions, sizes, colours, fonts, the wave layer shapes, the frosted panel, the playhead, chip text and icons. Fix any deviation, then re-run until the snapshots match and the bench passes. Commit: `test: snapshot and bench harness; visual fixes`.

---

### Task 9: Final verification

- [ ] **Clean build:** Debug and Release build clean (no new warnings in `Source/`). ctest passes, and pluginval `--strictness-level 5` prints SUCCESS for both configs; build Release last so `%USERPROFILE%\VST3` holds Release.
- [ ] **Standalone smoke run:** the Release standalone starts and stays running for 5 s.
- [ ] **Docs:** update `docs/superpowers/specs/2026-09-27-stemsplitter-redesign-design.md` if anything was decided differently. Commit: `chore: final verification for the redesign`.
