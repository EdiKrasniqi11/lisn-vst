# Harmonies as its own product — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Harmonies a product of its own inside LISN: reached by the name reel (or a choir icon after a split), with its own drop screen, Quick | Best switch and two-step progress, running in the processor so it survives the plugin window closing.

**Architecture:** A new `HarmonyFlow` (owned by the processor, polled by its own message-thread timer) chains a 4-stem `SeparationJob` (step 1, cache-shared with StemSplitter) and the existing `HarmonyJob` (step 2). The processor saves the product, quality and Harmonies' song. `UiState` gains three Harmonies screens through a pure mapping function; the editor shows StemSplitter's screens on the left page and Harmonies' on the right, reusing the existing reel spin and panel slide between them.

**Tech Stack:** JUCE 8 / C++17, CMake, `lisn_tests` (juce::UnitTest) + `--snapshots`, GitHub Actions (`windows` + `macos` jobs).

**Spec:** `docs/superpowers/specs/2026-10-19-harmonies-product-design.md`

## Global Constraints

- Branch `feature/mac-release` in C:\Projects\lisn-vst. Verify on CI with `gh workflow run build.yml --ref feature/mac-release` (pushes alone don't trigger CI on this branch); both `windows` and `macos` jobs must end green. Never push master, never tags or releases.
- StemSplitter must not change for its users apart from the choir icon: its screens, its 4/6 setting, its saved model and song are never changed by Harmonies.
- Harmonies always uses the 4-stem model `htdemucs` for step 1, whatever StemSplitter's 4/6 says.
- Copy (verbatim): drop screen "Drop a song here" / "LISN finds the vocals, then splits the lead from the harmonies." / "Browse…" / "You'll get" with chips "Lead" and "Harmonies"; offer button "Split the harmonies of <song>" (<song> = the file name without extension); progress steps "Finding the vocals", "Splitting the harmonies", and during Best's download "Downloading the Best quality model"; switch "Quick" | "Best quality"; choir icon tooltip "Harmonies"; Harmonies screen button "+ New song" (icon `Icons::plus`, text "New song", like the stems screen's).
- Best not yet downloaded: the drop screen shows "Best quality downloads its 900 MB model the first time." under the copy when the switch is on Best; the switch's Best side has the tooltip "900 MB download first".
- Colours: Lead = `theme.vocals`, Harmonies = `theme.guitar` (as the Harmonies screen uses today).
- The choir icon is mockup A (three singers), as an `Icons.h` path: `M12 4.4a2.6 2.6 0 1 1 0 5.2a2.6 2.6 0 1 1 0-5.2M5.5 7.3a2.2 2.2 0 1 1 0 4.4a2.2 2.2 0 1 1 0-4.4M18.5 7.3a2.2 2.2 0 1 1 0 4.4a2.2 2.2 0 1 1 0-4.4M7.5 20c0-3 2-5.2 4.5-5.2S16.5 17 16.5 20M1.8 20c0-2.6 1.6-4.4 3.7-4.4 1 0 1.9.4 2.6 1.1M22.2 20c0-2.6-1.6-4.4-3.7-4.4-1 0-1.9.4-2.6 1.1` (stroked).
- Saved state keys (processor): `product` ("stems" | "harmonies", default "stems"), `harmonyQuality` ("quick" | "best", default "quick"), `harmonyInput` (absolute path or empty).
- Remove the old entry: `StemsScreen::harmonyMenu/showHarmonyMenu/HarmonyChoice/onHarmonyChoice/setHarmonyButton` text states and its `menuLook`, and the editor's `harmonyJob`, `harmonyClicked`, `harmonyChosen`, `updateHarmony`.

## Review Focus

1. **The plugin window closes during step 1 or 2** → the flow keeps going (step 2 still starts after step 1) and a reopened window shows the progress or the result. Pinned in Task 1 (the flow advances with no editor alive).
2. **A project saved in Harmonies is reopened after its harmony result was deleted from the cache** → Harmonies' drop screen, no crash. Pinned in Task 2 (mapping) and Task 1 (state restore).
3. **The user clicks the name or drops a file while a StemSplitter split or a Harmonies step runs** → nothing switches or starts. Pinned in Task 4 (editor test: name not clickable, `isInterestedInFileDrag` false while running).
4. **Cancel during Best's download or step 2** → back to Harmonies' drop screen, no half-written `harmony/` or model left, stems from step 1 kept. Pinned in Task 1 (cancel during a slow fake step 2).
5. **The same song is opened from StemSplitter and Harmonies alternately** → each product shows that song without re-splitting (cache hits). Pinned in Task 4 (switching with a cached 4-stem dir starts no Python).

---

### Task 1: `HarmonyFlow` in the processor, and saved state

**Files:**
- Create: `Source/HarmonyFlow.h`, `Source/HarmonyFlow.cpp` (add to `LISN_SOURCES` in `CMakeLists.txt`)
- Modify: `Source/PluginProcessor.h/.cpp`
- Test: `tests/HarmonyFlowTests.cpp` (add to `lisn_tests` in `CMakeLists.txt`), `tests/ProcessorStateTests.cpp`

**Interfaces:**
- Consumes: `SeparationJob` (`start (input, model, pythonHint)`, cache hit = `Done` at once, `stemDirFor`, `getStemDir`, `getErrorKind`, `getError`, `getPythonExe`), `HarmonyJob` (`start (stemDir, hint, quality)`, `hasResult`, `qualityOf`, `isDownloading`, `testCommand`).
- Produces:
  - `class HarmonyFlow : private juce::Timer` with `enum class Step { idle, vocals, harmonies, done, failed };`
  - `void start (const juce::File& song, const juce::String& quality, const juce::String& pythonHint, bool keepExistingResult = true);` — step 1 = `SeparationJob` with model `"htdemucs"`; step 2 skipped when `keepExistingResult && HarmonyJob::hasResult (stemDir)`, or when `qualityOf (stemDir) == quality`.
  - `void cancel();` (stops the running step, back to `idle`; stems kept), `Step getStep() const`, `float getProgress() const` (current step, 0..1), `bool isDownloading() const`, `juce::File getSong() const`, `juce::File getStemDir() const`, `ErrorKind getErrorKind() const`, `juce::String getError() const`, `juce::String getPythonExe() const`, `HarmonyJob& harmonyJobForTests()`.
  - Processor: `HarmonyFlow harmonyFlow;` `juce::String getProduct() const; void setProduct (const juce::String&);` `juce::String getHarmonyQuality() const; void setHarmonyQuality (const juce::String&);` `juce::File getHarmonyInput() const;` `void startHarmonies (const juce::File& song, bool keepExistingResult = true);` (sets `harmonyInput`, adopts the Python step 1 found, like `syncWithJob`). All settings go through `settingsLock` and `notifyHost` on change, like the theme.

- [ ] **Step 1: Write the failing tests** (`HarmonyFlowTests`):
  - `"a cached song runs only the harmony pass"`: temp song file; create `SeparationJob::stemDirFor (song, "htdemucs")` with four short WAVs (delete it at the end); `flow.harmonyJobForTests().testCommand` = the fake script from `tests/HarmonyTests.cpp` (reuse its helper: `fakeCommand`); `flow.start (song, "best", {})`; pump the message loop until not running (≤ 10 s); expect `Step::done`, `HarmonyJob::hasResult (flow.getStemDir())`, `qualityOf == "best"`. No editor exists in this test.
  - `"an existing result is kept, or redone at another quality"`: with that result, `start (song, "quick", {})` → `done` at once with quality still `"best"`; `start (song, "quick", {}, false)` → runs step 2 → quality `"quick"`.
  - `"cancel during the harmony pass leaves nothing half-done"`: slow fake (`sleep 3` / `ping`); `start`, wait 300 ms, `cancel()` → `Step::idle`, no `harmony.part`, no new `harmony/`, the stem dir still has its four WAVs.
  - `"a failing harmony pass reports failed with its words"`: fake exits 3 printing `NEEDS SETUP: no model` → `Step::failed`, `getError()` contains it.
  - `ProcessorStateTests`: `"product, quality and Harmonies' song are saved"` — set product "harmonies", quality "best", `startHarmonies (song)` (cached + fake) → save → new processor → restore → the three values back; a restored `harmonyInput` whose cache was deleted restores without crash.
- [ ] **Step 2:** Push and run CI; the new tests fail (no `HarmonyFlow`).
- [ ] **Step 3:** Implement `HarmonyFlow` (timer at 100 ms only while running) and the processor members and state keys.
- [ ] **Step 4:** CI green on both jobs.
- [ ] **Step 5:** Commit `feat: Harmonies' flow lives in the processor`.

### Task 2: Harmonies' screens in `UiState`

**Files:**
- Modify: `Source/UiState.h/.cpp`
- Test: `tests/UiStateTests.cpp`

**Interfaces:**
- Consumes: Task 1's `HarmonyFlow::Step`, processor getters.
- Produces:
  - `enum class Screen { Drop, Splitting, Stems, Error, HarmonyDrop, HarmonySplitting, Harmonies };`
  - New `UiState` fields: `bool harmoniesProduct = false; int harmonyStep = 0;` (1 = finding the vocals, 2 = splitting the harmonies) `bool downloading = false; juce::String offerSong;` (the StemSplitter song offered on HarmonyDrop, file name without extension, empty = none) `juce::File harmonyStemDir; juce::String harmonyQuality;`
  - Pure: `UiState harmoniesStateFor (HarmonyFlow::Step, float progress, bool downloading, ErrorKind, const juce::String& errorText, const juce::String& pythonExe, const juce::File& harmonyInput, const juce::File& harmonyStemDir, bool hasResult, const juce::File& stemSplitterSong, bool stemSplitterSongHasStems, const juce::String& quality);`
  - `uiStateFor (const StemSplitterProcessor&)` returns `harmoniesStateFor (...)` when `getProduct() == "harmonies"`, else today's mapping unchanged. `sameScreenAs` compares the new fields too (except progress).

- [ ] **Step 1: Write the failing tests** (`UiStateTests`, new cases): running `vocals` → `HarmonySplitting`, `harmonyStep 1`; running `harmonies` + downloading → step 2, `downloading`; `failed` → `Error` with the kind/text; `done` or idle with a result → `Harmonies` with `harmonyStemDir`; idle, no result, StemSplitter song with stems and no result → `HarmonyDrop`, `offerSong == "Liberian Girl"` for `Liberian Girl.mp3`; idle, nothing → `HarmonyDrop`, `offerSong` empty; idle with a `harmonyInput` whose result was deleted → `HarmonyDrop`; the StemSplitter mapping's existing tests unchanged.
- [ ] **Step 2:** CI: new tests fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** CI green.
- [ ] **Step 5:** Commit `feat: Harmonies' screens in the UI state`.

### Task 3: The new and changed components

**Files:**
- Modify: `Source/Icons.h` (`choir`), `Source/Screens.h/.cpp`, `Source/Controls.h/.cpp` if `SegmentedPill` needs relabelling or a per-segment tooltip
- Test: `tests/HarmonyTests.cpp` (component cases), `tests/Snapshots.cpp`

**Interfaces:**
- Produces:
  - `StemsScreen`: the harmony button becomes icon-only (`Icons::choir`, square like the mockup, tooltip "Harmonies"), same place left of Loop, same visibility rule (4-stem splits only); keeps `std::function<void()> onHarmony`; the menu API listed in Global Constraints is removed.
  - `class HarmonyDropScreen : public juce::Component` — `setTheme (const Theme&)`, `setOffer (const juce::String& songName)` (empty hides the offer button), `setBestNote (bool show)`, `setHighlighted (bool)`, `std::function<void()> onBrowse, onOffer;` Layout and type follow `DropScreen` (choir icon in the circle where Drop has its upload icon; chips like Drop's stem chips; the offer is a Primary `LisnButton` above the heading).
  - `class HarmonySplittingScreen : public juce::Component` — `setTheme`, `setSong (const juce::String& name)`, `setStep (int step, float progress, bool downloading)` (repaints only on a whole-percent or step change), `std::function<void()> onCancel;` Two rows: a circle (✓ when done, else the number; gold when current), the label, a progress bar in `theme.guitar`, the percentage; Cancel below. Type and spacing follow `SplittingScreen`.
  - `HarmonyScreen`: `LisnButton newSong` ("New song", `Icons::plus`, Ghost) left of "All stems"; `std::function<void()> onNewSong;`
  - `Header`: `void setProduct (bool harmonies);` — the second pill shows "Quick" | "Best quality" in Harmonies ("4 stems" | "6 stems" otherwise); `void setHarmonyQuality (bool best);` `std::function<void (bool best)> onHarmonyQuality;`; the Best segment's tooltip "900 MB download first" (set by the editor only while Best isn't downloaded: `void setBestTooltip (bool show)`).
  - The editor owns one `juce::TooltipWindow` (Task 4 adds it) so tooltips show.

- [ ] **Step 1: Write the failing tests**: `HarmonyDropScreen` offer button hidden with `setOffer ({})`, visible with text "Split the harmonies of Liberian Girl" after `setOffer ("Liberian Girl")`, `onOffer` fires on click; `HarmonySplittingScreen` `setStep (2, 0.42f, false)` → its percentage label "42%" and step-2 label "Splitting the harmonies"; `setStep (2, 0.1f, true)` → "Downloading the Best quality model"; `Header::setProduct (true)` → second pill labels "Quick", "Best quality"; `setProduct (false)` → "4 stems", "6 stems"; `StemsScreen` harmony button has no text, tooltip "Harmonies". Snapshots: `harmony-drop-dusk`, `harmony-drop-offer-midnight`, `harmony-splitting-dusk` (step 2, 42%), `harmony-downloading-midnight`, `stems4-dusk-choir` (the icon), `harmony-dusk` gains the New song button.
- [ ] **Step 2:** CI: new tests fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** CI green; download `mac-snapshots` and look at every new shot (layout, text fits, colours) — fix anything off.
- [ ] **Step 5:** Commit `feat: Harmonies' drop and progress screens, the choir icon and the Quick | Best switch`.

### Task 4: Wire it in the editor

**Files:**
- Modify: `Source/PluginEditor.h/.cpp`
- Test: `tests/HarmonyTests.cpp` (editor cases, with `forceState` / a real processor as the snapshots do), `tests/Snapshots.cpp`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: the behaviour below; the old harmony members listed in Global Constraints removed.

Behaviour to implement (from the spec):
- **Pages:** the panel holds StemSplitter's screens (Drop, Splitting, Stems, Error) on the left page and Harmonies' (HarmonyDrop, HarmonySplitting, Harmonies, and Error when the product is Harmonies) on the right; the existing slide (`slideTo`) moves between pages; `show()` picks the screen within the product's page.
- **Name reel:** clickable whenever no StemSplitter split and no Harmonies step is running. A click toggles the product: `proc.setProduct (...)`, `header.spinTo (harmonies, true)`, then slide on `onLanded`. Going to Harmonies with StemSplitter's song having a harmony result → `proc.startHarmonies (thatSong)` (opens it). Going to StemSplitter while Harmonies shows song X ≠ StemSplitter's song → `proc.startSplit (X)` (a cache hit when it was split at StemSplitter's 4/6 before; otherwise it splits). The header's pill follows the product (`setProduct`).
- **Choir icon:** `proc.setProduct ("harmonies")`, `proc.startHarmonies (stems' song)` (keeps an existing result), spin + slide.
- **Offer button:** `proc.startHarmonies (StemSplitter's song)`. **Browse / drops / New song** while in Harmonies → `proc.startHarmonies (file)`; in StemSplitter → `startSplit` as today. `isInterestedInFileDrag` returns false while any job runs.
- **Quick | Best:** `proc.setHarmonyQuality (...)`; when the Harmonies result screen is up and its quality differs → `proc.startHarmonies (song, false)`. The Best tooltip and the drop screen's note show while `HarmonyJob::findModel ("best", python) == juce::File()`.
- **Progress / Cancel / Retry:** HarmonySplitting shows `harmonyFlow` step and progress; Cancel → `proc.harmonyFlow.cancel()`; the Error screen's Retry runs `startHarmonies (harmonyInput, true)` in Harmonies, `startSplit (lastInput)` in StemSplitter.
- **Player:** holds Lead + Harmonies only on the Harmonies screen (today's `setHarmonyView` behaviour), StemSplitter's stems on Stems, nothing otherwise.
- **While running:** `header.setEnabledSwitches (false)` and the name not clickable.
- **Opening:** a reopened editor shows the saved product's page at once (no spin, no slide) and the reel at that product.
- One `juce::TooltipWindow` in the editor.

- [ ] **Step 1: Write the failing tests** (editor cases): with a real processor and a cached song (Task 1's helper): choir click → product "harmonies", reel shows Harmonies after the spin, `harmonyFlow` started on that song; name click on a Harmonies screen of song X while StemSplitter has song Y → product "stems", StemSplitter's song becomes X with no Python run (cache hit); while `harmonyFlow` runs: `header` name not clickable and `isInterestedInFileDrag ({ "x.wav" })` false; changing Quick→Best on a quick result starts a redo; a new editor on a processor saved in Harmonies shows the Harmonies page at once. Snapshots: `harmonies-reopened-dusk` (editor opened in Harmonies with a result).
- [ ] **Step 2:** CI: new tests fail.
- [ ] **Step 3:** Implement; remove the old members.
- [ ] **Step 4:** CI green on both jobs, the Mac input checks (click/drag) still pass; look at the new snapshots.
- [ ] **Step 5:** Commit `feat: Harmonies is its own product: reel, choir icon, drop, progress`.

---

**After this plan:** the Mac tester build (the Mac plan's pending steps: the `mac-test-1` pre-release and `lisn.digital/mac/` going live, each with the owner's OK) uses this branch.
