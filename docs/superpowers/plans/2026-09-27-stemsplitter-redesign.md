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

- [ ] **Step 1: `Source/StemPreview.h`.**
```cpp
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

// Plays one stem file through the plugin's output, on top of the audio passing through.
// Message thread: load/unload/play/pause/seek/queries. Audio thread: addTo(). UI: takePeak().
class StemPreview
{
public:
    StemPreview();
    ~StemPreview();

    void prepare (double sampleRate, int maxBlockSize);   // from prepareToPlay
    void release();                                        // from releaseResources

    bool load (const juce::File& audioFile);   // stops, then loads at position 0; false if unreadable (nothing loaded)
    void unload();
    juce::File getFile() const { return file; }
    void play();
    void pause();
    bool isPlaying() const;
    double getLengthSeconds() const;
    double getPositionFraction() const;        // 0..1
    void setPositionFraction (double);

    void addTo (juce::AudioBuffer<float>& buffer);   // audio thread; returns at once when not playing
    float takePeak();                                // highest preview sample level since the last call

private:
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "LISN preview read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> scratch;
    std::atomic<float> peak { 0.0f };
    juce::File file;
    int maxBlock = 0;
};
```
- [ ] **Step 2: `Source/StemPreview.cpp`.**
  - **Construction:** the constructor calls `formats.registerBasicFormats(); readAhead.startThread();`. The destructor calls `transport.setSource (nullptr); readAhead.stopThread (2000);`.
  - **`prepare`:** `scratch.setSize (2, maxBlockSize); maxBlock = maxBlockSize; transport.prepareToPlay (maxBlockSize, sampleRate);`. **`release`:** `transport.releaseResources()`.
  - **`load`:** `transport.stop(); transport.setSource (nullptr); source.reset();`. Then create a reader with `formats.createReaderFor (f)`, returning false on nullptr. Otherwise `source = std::make_unique<AudioFormatReaderSource> (reader, true); transport.setSource (source.get(), 32768, &readAhead, reader->sampleRate, 2); file = f;`. Capture the reader's sample rate before passing ownership.
  - **Queries:** `play()` is `transport.start()`, `pause()` is `transport.stop()` and `isPlaying()` is `transport.isPlaying()`. Length and position come from `transport.getLengthInSeconds()` and `getCurrentPosition()`; return 0 when the length is 0. **Seek:** `transport.setPosition (fraction * length)`.
  - **`addTo`:**
```cpp
void StemPreview::addTo (juce::AudioBuffer<float>& buffer)
{
    if (! transport.isPlaying()) return;
    const int n = buffer.getNumSamples();
    if (n > scratch.getNumSamples()) return;              // host broke its promised block size; skip rather than allocate
    juce::AudioSourceChannelInfo info (&scratch, 0, n);
    transport.getNextAudioBlock (info);
    const float blockPeak = scratch.getMagnitude (0, n);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.addFrom (ch, 0, scratch, juce::jmin (ch, scratch.getNumChannels() - 1), 0, n);
    if (blockPeak > peak.load (std::memory_order_relaxed)) peak.store (blockPeak, std::memory_order_relaxed);   // benign race with takePeak
}
float StemPreview::takePeak() { return peak.exchange (0.0f); }
```
  - **Lock note:** add `// ponytail: AudioTransportSource briefly locks while the message thread swaps sources in load(); fine for a preview.`
- [ ] **Step 3: Processor.**
  - Add the member `StemPreview preview;`.
  - `prepareToPlay` calls `preview.prepare`, and `releaseResources` calls `preview.release`.
  - `processBlock` becomes `juce::ScopedNoDenormals nd; preview.addTo (buffer);`.
  - `startSplit` calls `preview.unload()` first.
- [ ] **Step 4: `tests/StemPreviewTests.cpp`.** A helper writes a 2 s, 44.1 kHz stereo WAV into a temp file (sine at 440 Hz, amplitude 0.5) with `juce::WavAudioFormat().createWriterFor`. Then:
  1. `prepare (48000, 512)` and `load` succeed, and the length ≈ 2.0 s (±0.01).
  2. **Not playing:** fill the buffer with 0.25, call `addTo`, and every sample must still be exactly 0.25.
  3. **Playing:** `play()`, then 20 blocks. The RMS of blocks 3–20 is > 0.2, `takePeak()` is in 0.45–0.55, and a second `takePeak()` returns 0.
  4. **Position:** after the 20 blocks it's within 0.05 s of 20·512/48000. That checks resampling, because the file is 44.1 kHz and the host 48 kHz.
  5. **Pause:** `pause()` leaves the buffer unchanged and the position stable.
  6. **Seek:** `setPositionFraction (0.5)` gives `getPositionFraction()` within 0.01 of 0.5.
  7. **End of file:** play from 0.98 until it stops (within 200 blocks); then `isPlaying()` is false.
  8. **Bad files:** `load` returns false for a missing file and for a `.txt` file.
- [ ] **Step 5:** Build, run the tests and pluginval. Commit: `feat: stem preview playback mixed into the plugin output`.

---

### Task 4: Pure helpers: peaks, wave geometry, frost

**Files:** Create `Source/Peaks.h/.cpp`, `Source/WaveGeometry.h/.cpp`, `Source/Frost.h/.cpp` and `tests/PeaksTests.cpp`, `tests/WaveGeometryTests.cpp`, `tests/FrostTests.cpp`. Modify `CMakeLists.txt`.

- [ ] **Step 1: `Peaks`.**
```cpp
// Peak level (0..1, loudest channel) of `bars` equal slices of the file.
std::vector<float> readPeaks (juce::AudioFormatReader& reader, int bars);
double lengthSeconds (const juce::AudioFormatReader& reader);   // 0 when sampleRate is 0
```
  - **Slices:** read each slice with `reader.readMaxLevels (start, end - start, ranges, numChannels)`, where slice `i` spans `[len·i/bars, len·(i+1)/bars)`. `ranges` is a `std::vector<juce::Range<float>>` sized to `min(numChannels, 8)`.
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
// Static frosted-glass blur (CSS blur(radius)): shrink the image 4x, run three box blurs horizontally and vertically
// with r = round((sqrt(1 + 4*s*s) - 1) / 2), where s = radius/4, clamping at the edges, then scale back up.
// The result is ARGB, the same size as src, and a SoftwareImageType image.
juce::Image frosted (const juce::Image& src, float radius);
```
  - **Implementation:** do the box blur on `Image::BitmapData` (readWrite) with a running sum per channel over premultiplied ARGB pixels.
  - **Tests:** a uniform 200×120 image stays uniform (±2 per channel). A black image with a 20×20 white square in the middle gives a centre that stays bright (>200) and a pixel 12 px outside the square that is >0 and <255. The output size equals the input size.
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
  - **Scale and image type:** `scale = jlimit (1.0f, 2.0f, g.getInternalContext().getPhysicalPixelScaleFactor())`. Create images with `g.getInternalContext().getPreferredImageTypeForTemporaryImages()`, so window paints get GPU images and snapshots get software ones. Rebuild everything when the scale or `typeid` of that image type changes.
  - **Layer masks:** two `Image::SingleChannel` masks per layer, independent of theme:
    - **shape:** `layerPath (shape, shape.phase, 0, shape.wavelength + 40)` filled in white;
    - **shadow:** `juce::DropShadow (Colours::white, 14, { 12, -4 }).drawForPath (g, path)`. This matches CSS `drop-shadow(12px -4px 14px)`.
    - **Mask bounds:** the window, expanded 40 px, united with that same rect translated by −(direction·(wavelength+40)) and −(normal·push). Render at `scale` using the transform `translated (-bounds.origin).scaled (scale)`.
    - **Drawing:** use `drawImageTransformed (mask, AffineTransform::scale (1/scale).translated (snapped origin), true)` so the current colour fills the mask. Snap the offset to whole physical pixels to avoid resampling.
  - **Panel shadow:** CSS `0 26px 50px -22px rgba(20,8,16,.75)` is drawn as `DropShadow (Colour (20,8,16).withAlpha (0.75f), 25, { 0, 26 })` for the panel rounded rect reduced by 22 px. It's pre-rendered once per scale into an ARGB image.
  - **Frost:** render the full static background (flow 0, pulse 0, no panel) at `scale` into a software ARGB image. Then apply `frosted (image, 18 * scale)` and convert it to the preferred image type. The cache is keyed by (theme id, scale, type) and is only drawn inside the panel clip.
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
