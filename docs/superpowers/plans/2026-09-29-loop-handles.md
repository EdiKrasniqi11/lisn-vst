# Loop Handles, Grid, Zoom and Keys Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the stem player's loop precise. Loop edges become draggable handles that snap to an adaptive beat grid, Alt drags off the grid, and Ctrl+wheel zooms the waveforms. The plugin also takes the keyboard when clicked (Space, L, 1–6, Shift+1–6, Home). Stem icons become colour bars, and the footer goes.

**Architecture:**
- **`StemSplitterEditor`** takes keyboard focus on any click and handles the keys. `keyStateChanged` claims the key-downs of those keys, so FL doesn't also get them.
- **`Peaks`:** a 100 Hz peak envelope per stem (`readEnvelope`) replaces the fixed 104 bars. `barsFromEnvelope` turns any view of it into bars.
- **`WaveformView`** draws a *view* (a range of song fractions) of that envelope and reports the mouse as `WaveMouse` events in song fractions.
- **`StemsScreen`** owns the shared view (zoom and scroll), the grid step, the handle gestures, the tabs, the grid lines and the position strip.

**Tech Stack:** JUCE 8.0.4, CMake, MSVC 2022, C++17. Tests are `juce::UnitTest` suites in `lisn_tests` (run through ctest).

**Spec:** `docs/superpowers/specs/2026-09-29-loop-handles-design.md`. **Mockup:** `docs/design/LoopHandles.mockup.html`. Where the text and the mockup disagree, the mockup wins, and it wins over the older `StemPlayer.mockup.html` too.

**Branch:** `feat/stem-player`, continuing after `1d92e4d`. Don't push.

## Global Constraints

- **UI:**
  - No `ComboBox`, `TextEditor`, `PopupMenu` or `ProgressBar`.
  - **Keyboard focus:** only the editor itself takes focus, when the user clicks anywhere in it. Every child keeps `setWantsKeyboardFocus (false)`, and Buttons also keep `setMouseClickGrabsKeyboardFocus (false)`. This replaces the old rule "nothing takes keyboard focus".
- **Audio and performance:**
  - An idle `processBlock` returns straight away with no allocation, lock or extra work. This plan doesn't touch the audio path.
  - A motion frame averages ≤ 4 ms in the Release bench.
  - Player rules: never call `transport.stop()`; play/pause is an atomic flag; the position is read from an atomic.
- **Code rules:**
  - Non-ASCII UI text is escaped UTF-8, e.g. `juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))` for "–". The shared `dot` string lives in `Screens.cpp`.
  - Deliberate corner-cuts get a `ponytail:` comment naming the ceiling and the upgrade path.
- **Builds:** run from the repo root with CMake from `"C:\Program Files\CMake\bin"`. In Git Bash: `export PATH="/c/Program Files/CMake/bin:$PATH"`.
  - Build (**Debug only** during tasks): `cmake --build build --config Debug`.
  - Tests: `build/lisn_tests_artefacts/Debug/lisn_tests.exe` (prints every suite), then `ctest --test-dir build -C Debug --output-on-failure`.
  - pluginval: `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\LISN StemSplitter.vst3"`.
  - **FL loads the Release build** through a junction (`C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3`). Build Release only when a step says so, and only after checking that FL isn't running: `Get-Process | Where-Object { $_.ProcessName -match 'FL' }`. Never kill FL, and never touch the junction.
  - **Debug VST3 flake:** if the post-build step fails with `juce_vst3_helper … DLL initialization routine failed`, delete `build/StemSplitter_artefacts/Debug/VST3/LISN StemSplitter.vst3/Contents/x86_64-win/LISN StemSplitter.vst3` and rebuild.
- **Commits** end with a blank line and then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Don't push.

## File map

```
Source/PluginEditor.h/.cpp   keyboard: focus on click, keyPressed, keyStateChanged
Source/Icons.h               - stem icons and stemIcon()
Source/Peaks.h/.cpp          readEnvelope + barsFromEnvelope replace readPeaks
Source/StemRow.h/.cpp        colour bar; WaveformView draws a view of the envelope; WaveMouse events
Source/Screens.h/.cpp        StemsScreen: no footer, view/zoom/wheel, position strip, grid step, handles, tabs,
                             grid lines, loop length in beats
tests/UiStateTests.cpp, tests/StemRowTests.cpp, tests/PeaksTests.cpp, tests/Snapshots.cpp   updated
```

Layout constants used below (panel coordinates): the waveforms start at x **213** (rows at 19, waveform at row x 194) and are **376** wide. `Screens.cpp` gets `waveLeft = 213` and `waveWidth = 376` in Task 3.

---

### Task 1: Keyboard: the editor takes focus on click, Space / L / 1–6 / Shift+1–6 / Home

**Files:**
- Modify: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`, `tests/UiStateTests.cpp`

**Interfaces:**
- Consumes: `StemsScreen::onPlayPause`, `onToggleMute (int)`, `onSolo (int)`, `onToggleLoop`, `loopButtonEnabled()`, `numRows()`; `StemPlayer::setPositionFraction`, `isLooping`, `getLoop`.
- Produces: `bool StemSplitterEditor::keyPressed (const juce::KeyPress&)`, `bool keyStateChanged (bool)` and `void mouseDown (const juce::MouseEvent&)`, all public overrides.

- [ ] **Step 1: Failing test.** In `tests/UiStateTests.cpp`, in `beginTest ("no focus, no banned controls")`, replace the line `expect (! c.getWantsKeyboardFocus(), c.getName());` with:

```cpp
                    expect (&c == &ed || ! c.getWantsKeyboardFocus(), c.getName());   // only the editor takes the keyboard
```

  and add `expect (ed.getWantsKeyboardFocus());` right after `ed.tick();` in that loop.

  Then add this test after the `"loop: snapping, button, chip, renders"` block:

```cpp
        beginTest ("keys: Space, L, 1-6, Shift+1-6, Home on the Stems screen; everything else goes to the host");
        {
            StemSplitterProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            StemSplitterEditor ed (proc);
            UiState s;
            s.screen = Screen::Stems;
            s.stemDir = four;
            ed.forceState (s);
            ed.tick();
            auto& screen = ed.stemsScreen();

            expect (ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
            expect (proc.player.isPlaying());
            ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            expect (! proc.player.isPlaying());

            expect (ed.keyPressed (juce::KeyPress ('2')));
            expect (proc.player.isMuted (1));
            expect (ed.keyPressed (juce::KeyPress ('1', juce::ModifierKeys::shiftModifier, '!')));   // Shift+1 solos vocals
            expectEquals ((int) proc.player.audibleMask(), 0b0001);
            expect (ed.keyPressed (juce::KeyPress ('6')));          // there's no sixth stem: ignored, but still ours

            expect (ed.keyPressed (juce::KeyPress ('L')));          // no loop range yet: nothing to toggle
            expect (! proc.player.isLooping());
            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (ed.keyPressed (juce::KeyPress ('l')));
            expect (! proc.player.isLooping());
            ed.keyPressed (juce::KeyPress ('L'));
            expect (proc.player.isLooping());

            proc.player.setPositionFraction (0.9);
            expect (ed.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
            expectWithinAbsoluteError (proc.player.getPositionFraction(), 0.2, 0.01);   // looping: the loop start
            proc.player.setLooping (false);
            ed.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
            expectWithinAbsoluteError (proc.player.getPositionFraction(), 0.0, 0.01);

            expect (! ed.keyPressed (juce::KeyPress ('A')));
            expect (! ed.keyPressed (juce::KeyPress ('S', juce::ModifierKeys::ctrlModifier, 0)));   // FL's Ctrl+S
            expect (! ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0)));

            UiState dropState;
            ed.forceState (dropState);
            ed.tick();
            expect (! ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));   // not on the Stems screen
        }
```

- [ ] **Step 2: Run it and watch it fail.**
  - Run: `cmake --build build --config Debug`.
  - Expected: the build fails, because `keyPressed` isn't overridden in `StemSplitterEditor`. That's the red step. (If it compiles through `Component::keyPressed`, the run fails on the first `expect` instead.)

- [ ] **Step 3: Declare the overrides.** In `Source/PluginEditor.h`, after `void resized() override;`, add:

```cpp
    // The keyboard (the editor takes it on any click; see the constructor). Space, L, 1-6, Shift+1-6 and Home on the Stems
    // screen; every other key returns false, and JUCE passes it on to the host.
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;
```

- [ ] **Step 4: Implement.** In `Source/PluginEditor.cpp`, replace `setWantsKeyboardFocus (false);` in the constructor with:

```cpp
    // Only the editor takes the keyboard, when anything in it is clicked; its children never do. The listener sees the
    // clicks on every child, including buttons, which don't grab focus themselves.
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
```

  and add after `StemSplitterEditor::resized()`:

```cpp
void StemSplitterEditor::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool StemSplitterEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! shown.has_value() || shown->screen != Screen::Stems || mods.isCtrlDown() || mods.isAltDown())
        return false;                                // FL's shortcuts stay FL's
    const auto code = key.getKeyCode();
    auto& p = proc.player;
    if (code == juce::KeyPress::spaceKey)
        stems.onPlayPause();
    else if (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code) == 'L')
    {
        if (stems.loopButtonEnabled()) stems.onToggleLoop();
    }
    else if (code == juce::KeyPress::homeKey)
        p.setPositionFraction (p.isLooping() ? p.getLoop().getStart() : 0.0);
    else if (code >= '1' && code <= '6')
    {
        const int i = code - '1';
        if (i < stems.numRows())
        {
            if (mods.isShiftDown()) stems.onSolo (i);
            else                    stems.onToggleMute (i);
        }
    }
    else
        return false;
    return true;
}

bool StemSplitterEditor::keyStateChanged (bool isKeyDown)
{
    // Windows sends Space, letters and digits as a key-down and then a character. JUCE turns the character into keyPressed()
    // but hands the key-down to the host unless it's used here, so claim the key-downs of the keys keyPressed() takes:
    // otherwise FL would also start its own transport on the same Space. ponytail: while one of these keys is held, any
    // other key-down is claimed too. Upgrade path: remember which key keyPressed() took and claim only that one.
    const auto mods = juce::ModifierKeys::currentModifiers;
    if (! isKeyDown || ! shown.has_value() || shown->screen != Screen::Stems || mods.isCtrlDown() || mods.isAltDown())
        return false;
    for (const int k : { (int) juce::KeyPress::spaceKey, (int) 'L', (int) '1', (int) '2', (int) '3', (int) '4', (int) '5', (int) '6' })
        if (juce::KeyPress::isKeyCurrentlyDown (k))
            return true;
    return false;
}
```

- [ ] **Step 5: Run the tests.**
  - Run: `cmake --build build --config Debug`, then `build/lisn_tests_artefacts/Debug/lisn_tests.exe`.
  - Expected: every suite passes, including `UiState` with the new keys test. Then run `ctest --test-dir build -C Debug --output-on-failure` and expect 100%. `keyStateChanged` reads the physical key state, so no unit test can drive it; Step 7 checks it in FL.

- [ ] **Step 6: Commit.**

```bash
git add Source/PluginEditor.h Source/PluginEditor.cpp tests/UiStateTests.cpp
git commit -m "feat: the plugin takes the keyboard on click: Space plays, L loops, 1-6 mute, Shift+1-6 solo, Home

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Early FL check (controller and user; not the implementer).** The rest of the plan rests on FL passing keys through, so check it now.
  1. Ask the user to close FL.
  2. Confirm with `Get-Process | Where-Object { $_.ProcessName -match 'FL' }` that it's gone.
  3. Build Release: `cmake --build build --config Release`.
  4. Ask the user to open FL, load LISN StemSplitter on a split song, and check:
     - after a click in the plugin, Space plays and pauses **the stems**, and FL's transport doesn't move;
     - 2 mutes drums, Shift+1 solos vocals, L toggles a loop, and Home jumps back;
     - FL shortcuts still work while the plugin is focused (for example F5/F6/F7 to switch windows, or Ctrl+S);
     - after a click on FL's playlist, Space plays **FL** again.

  If focus or Space doesn't work in FL, **stop** and bring it back to the user before Task 2.

---

### Task 2: Colour bars instead of stem icons, and no footer

**Files:**
- Modify: `Source/StemRow.h` (one comment), `Source/StemRow.cpp`, `Source/Icons.h`, `Source/Screens.cpp`, `tests/StemRowTests.cpp`

**Interfaces:**
- Consumes: `Theme::stemColour`, `themeFor`.
- Produces: nothing new. `stemIcon()` and `Icons::vocals/drums/bass/guitar/piano/other` are deleted.

- [ ] **Step 1: Failing test.** In `tests/StemRowTests.cpp`, in `beginTest ("icons")`:
  - remove `Icons::vocals, Icons::drums, Icons::bass, Icons::guitar, Icons::piano, Icons::other` from the icon list, so the list ends with `Icons::speaker, Icons::speakerOff, Icons::loop })`;
  - delete the two `expect (stemIcon (…))` lines.

  After the `"layout"` test block (after `expectEquals (row.barCount(), 104);`), add:

```cpp
        beginTest ("colour bar instead of an icon");
        {
            const auto img = render (row);
            const auto c = themeFor ("dusk").stemColour ("vocals");
            const auto at = img.getPixelAt (12, 31);                        // the 4 x 30 bar at x 10, centred in 62
            expectWithinAbsoluteError ((int) at.getRed(), (int) c.getRed(), 3);
            expectWithinAbsoluteError ((int) at.getGreen(), (int) c.getGreen(), 3);
            expectWithinAbsoluteError ((int) at.getBlue(), (int) c.getBlue(), 3);
            expectEquals ((int) img.getPixelAt (18, 31).getAlpha(), 0);     // the old tinted badge covered x 10-42
        }
```

- [ ] **Step 2: Run it and watch it fail.** Run `cmake --build build --config Debug` then `build/lisn_tests_artefacts/Debug/lisn_tests.exe`. Expected: `StemRow` fails on "colour bar instead of an icon", because the pixel at (18, 31) is the badge tint.

- [ ] **Step 3: Draw the bar.** In `Source/StemRow.cpp`:
  - delete `#include "Icons.h"`, since nothing else in this file uses it;
  - in `StemRow::paint`, replace everything from `// Badge: 32 x 32, …` down to the closing `}` of its `if` block with:

```cpp
    // Marker (LoopHandles.mockup.html): a 4 x 30 bar, radius 2, in the stem colour, dimmed with the name.
    const juce::Rectangle<float> bar ((float) padX, (h - 30.0f) / 2.0f, 4.0f, 30.0f);
    g.setColour (colour.withMultipliedAlpha (a));
    g.fillRoundedRectangle (bar, 2.0f);
```

  - change the comment above the name to `// Name (Bold 14) over the file name (11, cream 0.6), line-height 1.2, 1 px apart, centred as one column, 10 px after the bar.`;
  - change `const auto textX = badge.getRight() + 10.0f, …` to `const auto textX = bar.getRight() + 10.0f, …`, keeping the rest of the line.

  In `Source/StemRow.h`, change the class comment `badge + name, mute light, …` to `colour bar + name, mute light, …`.

- [ ] **Step 4: Delete the stem icons.** In `Source/Icons.h`:
  - delete the six lines `Icons::vocals` … `Icons::other`, and the blank line before them;
  - delete the whole `inline const char* stemIcon (…)` function at the end of the file.

- [ ] **Step 5: Remove the footer.** In `Source/Screens.cpp`:
  - change the comment `// Panel padding 16 18: content (19, 17, 674, 366). Song row 36, divider at 63, rows from 74 (283 tall), footer at 367.` to `// Panel padding 16 18: content (19, 17, 674, 366). Song row 36, divider at 63, rows from 74 (283 tall).`;
  - delete the line `namespace { constexpr float footerY = 367.0f; }`;
  - in `StemsScreen::paint`, delete the whole `if (g.clipRegionIntersects ({ 19, (int) footerY, 674, 16 })) { … }` block (the play glyph and the "Click a light to mute …" text).

  Check that `grep -n "footerY\|Click a light\|stemIcon\|Icons::vocals" Source tests -r` prints nothing.

- [ ] **Step 6: Run the tests.** Build Debug, run `lisn_tests.exe` (every suite passes), then `ctest --test-dir build -C Debug --output-on-failure` (100%).

- [ ] **Step 7: Commit.**

```bash
git add Source/StemRow.h Source/StemRow.cpp Source/Icons.h Source/Screens.cpp tests/StemRowTests.cpp
git commit -m "feat: stem rows show a colour bar instead of an icon; the helper footer is gone

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Peak envelope, a shared view, Ctrl+wheel zoom and scroll, position strip

**Files:**
- Modify: `Source/Peaks.h`, `Source/Peaks.cpp`, `Source/StemRow.h`, `Source/StemRow.cpp`, `Source/Screens.h`, `Source/Screens.cpp`, `tests/PeaksTests.cpp`, `tests/StemRowTests.cpp`, `tests/UiStateTests.cpp`

**Interfaces:**
- Consumes: Task 2's `Screens.cpp`, which has no footer.
- Produces:
  - `std::vector<float> readEnvelope (juce::AudioFormatReader&, double fps, const std::function<bool()>& shouldAbort = {})` and `std::vector<float> barsFromEnvelope (const std::vector<float>&, juce::Range<double> view, int bars)`. `readPeaks` is deleted.
  - `WaveformView::setEnvelope (std::vector<float>)`, `setBars (int)` and `setView (juce::Range<double>)`. `setLevels` is deleted. `onSeek` and `onLoopDrag` now report **song** fractions through the view.
  - `StemRow::setEnvelope (std::vector<float>)` and `setView (juce::Range<double>)`. `setPeaks` is deleted.
  - `StemsScreen::setView (juce::Range<double>)`, `getView()`, `zoom (double atFraction, double factor)` and `mouseWheelMove` (an override). `headX` maps through the view.

- [ ] **Step 1: Failing Peaks test.** In `tests/PeaksTests.cpp`, replace the three tests `"peaks per slice"`, `"no bars"` and `"abort"` (up to, not including, `beginTest ("length");`) with:

```cpp
            beginTest ("envelope and bars");
            const auto env = readEnvelope (*reader, 100.0);
            expectEquals ((int) env.size(), 100);                       // 1 s at 100 frames per second (441 samples each)
            const auto whole = barsFromEnvelope (env, { 0.0, 1.0 }, 4);
            const float want[] = { 0.0f, 0.5f, 0.0f, 1.0f };
            for (size_t i = 0; i < whole.size(); ++i)
                expectWithinAbsoluteError (whole[i], want[i], 0.02f);
            const auto zoomed = barsFromEnvelope (env, { 0.45, 0.55 }, 2);   // the end of the 0.5 sine, then silence
            expectWithinAbsoluteError (zoomed[0], 0.5f, 0.02f);
            expectWithinAbsoluteError (zoomed[1], 0.0f, 0.02f);
            for (auto v : barsFromEnvelope (env, { 0.3, 0.3001 }, 10))       // narrower than a frame: that frame
                expectWithinAbsoluteError (v, 0.5f, 0.02f);
            expectEquals ((int) barsFromEnvelope ({}, { 0.0, 1.0 }, 104).size(), 104);   // no envelope yet: flat bars
            expect (barsFromEnvelope (env, { 0.0, 1.0 }, 0).empty());

            beginTest ("abort");
            int calls = 0;
            const auto aborted = readEnvelope (*reader, 100.0, [&] { return ++calls > 30; });
            expectEquals ((int) aborted.size(), 100);
            expect (aborted[80] == 0.0f && env[80] > 0.9f);             // frames 0-29 read, then stopped
```

- [ ] **Step 2: Failing view tests.**

  **2a.** In `tests/StemRowTests.cpp`, replace the `"wrong peak count"` test with:

```cpp
        beginTest ("any envelope size");
        row.setEnvelope ({ 0.5f });
        row.setEnvelope (std::vector<float> (500, 2.0f));
        row.setEnvelope ({});
        render (row);
```

  **2b.** In the `"played part"` test, change `row.setPeaks (std::vector<float> (104, 1.0f));` to `row.setEnvelope (std::vector<float> (104, 1.0f));`.

  **2c.** Right after the `"played part"` block, add:

```cpp
        beginTest ("the view picks what's drawn, the played part and the seek position");
        {
            std::vector<float> env (1000, 0.0f);
            std::fill (env.begin() + 500, env.end(), 1.0f);           // silent first half, loud second half
            row.setEnvelope (env);
            row.setPosition (0.0);
            row.setView ({ 0.0, 0.5 });
            expectEquals ((int) render (row.waveform()).getPixelAt (37, 8).getAlpha(), 0);            // flat bars only
            row.setView ({ 0.5, 1.0 });
            expectGreaterThan ((int) render (row.waveform()).getPixelAt (37, 8).getAlpha(), 60);      // full bars
            row.setPosition (0.75);                                   // the middle of this view
            const auto img = render (row.waveform());
            expectGreaterThan ((int) img.getPixelAt (37, 25).getAlpha(), 240);                        // played
            expectWithinAbsoluteError ((int) img.getPixelAt (290, 25).getAlpha(), 77, 4);             // not yet
            double seekTo = -1.0;
            row.onSeek = [&] (double f) { seekTo = f; };
            const juce::Point<float> p (94.0f, 20.0f);                // 0.25 of the width
            const auto now = juce::Time::getCurrentTime();
            row.waveform().mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f,
                                                        0.0f, 0.0f, &row.waveform(), &row.waveform(), now, p, now, 1, false));
            expectWithinAbsoluteError (seekTo, 0.625, 1e-9);          // 0.5 + 0.25 of the 0.5-wide view
            row.onSeek = nullptr;
            row.setView ({ 0.0, 1.0 });
            row.setPosition (0.5);
        }
```

  **2d.** In `tests/UiStateTests.cpp`, add after the keys test from Task 1:

```cpp
        beginTest ("zoom keeps the point under the mouse and clamps; the wheel zooms with Ctrl and scrolls when zoomed");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);                   // a 3:12 song, as the player would report it
            expect (screen.getView() == juce::Range<double> (0.0, 1.0));
            screen.zoom (0.5, 0.8);
            expectWithinAbsoluteError (screen.getView().getStart(), 0.1, 1e-9);
            expectWithinAbsoluteError (screen.getView().getLength(), 0.8, 1e-9);
            screen.zoom (0.26, 0.8);                                  // 0.26 sits at 0.2 of the view and stays there
            expectWithinAbsoluteError (screen.getView().getStart(), 0.26 - 0.2 * 0.64, 1e-9);
            expectWithinAbsoluteError (screen.getView().getLength(), 0.64, 1e-9);
            for (int i = 0; i < 100; ++i) screen.zoom (0.3, 0.8);
            expectWithinAbsoluteError (screen.getView().getLength() * 192.0, 2.0, 1e-9);   // 2 s across at most
            for (int i = 0; i < 100; ++i) screen.zoom (0.3, 1.25);
            expect (screen.getView() == juce::Range<double> (0.0, 1.0));

            screen.setView ({ 0.25, 0.75 });                          // everything maps through the view
            expectEquals (screen.playheadArea (0.5).getCentreX(), 213 + 188);
            expectEquals (screen.playheadArea (0.25).getCentreX(), 213);
            screen.setView ({ 0.9, 1.3 });                            // kept inside the song
            expectWithinAbsoluteError (screen.getView().getStart(), 0.6, 1e-9);
            expectWithinAbsoluteError (screen.getView().getEnd(), 1.0, 1e-9);
            screen.setView ({ 0.0, 1.0 });

            auto wheel = [&] (float deltaY, bool ctrl)                // over the middle of the first waveform
            {
                const juce::Point<float> p (213.0f + 188.0f, (float) screen.row (0)->getBounds().getCentreY());
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                          ctrl ? juce::ModifierKeys (juce::ModifierKeys::ctrlModifier) : juce::ModifierKeys(),
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &screen, &screen, now, p, now, 0, false);
                screen.mouseWheelMove (e, { 0.0f, deltaY, false, false, false });
            };
            wheel (-0.14f, false);
            expect (screen.getView() == juce::Range<double> (0.0, 1.0));   // whole song: the plain wheel does nothing
            wheel (0.14f, true);                                      // Ctrl + wheel up zooms in around the mouse
            expectWithinAbsoluteError (screen.getView().getLength(), 0.8, 1e-9);
            expectWithinAbsoluteError (screen.getView().getStart(), 0.1, 1e-9);
            wheel (-0.14f, false);                                    // wheel down: later in the song, 10 % of the view
            expectWithinAbsoluteError (screen.getView().getStart(), 0.18, 1e-9);
            for (int i = 0; i < 20; ++i) wheel (-0.14f, false);
            expectWithinAbsoluteError (screen.getView().getEnd(), 1.0, 1e-9);   // stops at the end of the song
        }
```

- [ ] **Step 3: Run them and watch them fail.** Run `cmake --build build --config Debug`. Expected: the build fails on `readEnvelope`, `setEnvelope`, `setView`, `zoom` and `getView`. That's the red step.

- [ ] **Step 4: The envelope.** In `Source/Peaks.h`, replace the `readPeaks` declaration and its comment with:

```cpp
// Peak level (0..1, loudest channel) per 1/fps seconds of the file (the last frame takes the remainder). Returns early
// (remaining frames 0) when shouldAbort() is true.
std::vector<float> readEnvelope (juce::AudioFormatReader& reader, double fps, const std::function<bool()>& shouldAbort = {});
// `bars` levels across `view` (fractions of the envelope's length): each bar is the loudest frame its slice touches.
// An empty envelope gives `bars` zeros.
std::vector<float> barsFromEnvelope (const std::vector<float>& envelope, juce::Range<double> view, int bars);
```

  In `Source/Peaks.cpp`, add `#include <algorithm>` under `#include "Peaks.h"`, and replace the whole `readPeaks` function with:

```cpp
std::vector<float> readEnvelope (juce::AudioFormatReader& reader, double fps, const std::function<bool()>& shouldAbort)
{
    const auto len = reader.lengthInSamples;
    if (reader.numChannels == 0 || len <= 0 || fps <= 0.0 || reader.sampleRate <= 0.0) return {};
    const auto hop = juce::jmax ((juce::int64) 1, (juce::int64) std::round (reader.sampleRate / fps));
    std::vector<float> env ((size_t) ((len + hop - 1) / hop), 0.0f);

    // readMaxLevels asserts that the channel count is at most what the file has (juce_AudioFormatReader.cpp:218-221).
    std::vector<juce::Range<float>> ranges ((size_t) juce::jmin ((int) reader.numChannels, 8));
    for (size_t i = 0; i < env.size(); ++i)
    {
        if (shouldAbort && shouldAbort()) break;
        const auto start = (juce::int64) i * hop;
        reader.readMaxLevels (start, juce::jmin (hop, len - start), ranges.data(), (int) ranges.size());
        float level = 0.0f;
        for (auto r : ranges) level = juce::jmax (level, std::abs (r.getStart()), std::abs (r.getEnd()));
        env[i] = juce::jmin (1.0f, level);
    }
    return env;
}

std::vector<float> barsFromEnvelope (const std::vector<float>& env, juce::Range<double> view, int bars)
{
    std::vector<float> out ((size_t) juce::jmax (0, bars), 0.0f);
    if (env.empty()) return out;
    const auto n = (int) env.size();
    for (int i = 0; i < bars; ++i)
    {
        const auto a = view.getStart() + view.getLength() * i / bars, b = view.getStart() + view.getLength() * (i + 1) / bars;
        const auto from = juce::jlimit (0, n - 1, (int) std::floor (a * n));
        const auto to = juce::jlimit (from + 1, n, (int) std::ceil (b * n));
        out[(size_t) i] = *std::max_element (env.begin() + from, env.begin() + to);
    }
    return out;
}
```

- [ ] **Step 5: WaveformView draws a view.**

  **5a.** In `Source/StemRow.h`, replace the whole `WaveformView` class with:

```cpp
// The waveform of a stem row: 376 x waveH bars inside a 5 px vertical margin (for the playhead). It draws `view` (song
// fractions) of the stem's 100 Hz peak envelope. Click or drag to seek; positions are song fractions.
class WaveformView : public juce::Component
{
public:
    WaveformView();
    void setEnvelope (std::vector<float> envelope);    // peak levels 0..1, one per 10 ms of the stem
    void setBars (int count);                          // bars across the width: 104, or 96 compact
    void setView (juce::Range<double> songFractions);  // the part of the song drawn; the whole song by default
    void setStemColour (juce::Colour);
    void setFraction (double);                         // the playhead (song fraction); no repaint, the screen repaints the strip
    std::function<void (double)> onSeek;               // song fraction 0..0.999

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (double, double, bool)> onLoopDrag;   // from, to (song fractions), finished

    static constexpr int margin = 5;

private:
    void rebuildBars();
    double fractionAt (float x) const;                 // the song fraction under x, clamped to the view

    std::vector<float> envelope;
    juce::Range<double> view { 0.0, 1.0 };
    int count = 104;
    juce::Path bars;                                   // cached; rebuilt on setEnvelope / setBars / setView / resized
    juce::Colour colour = Theme::cream;
    double fraction = 0.0, downFraction = 0.0;
    bool looping = false;
};
```

  **5b.** In the `StemRow` class in the same header:
  - replace `void setPeaks (std::vector<float> levels);       // bar levels 0..1, padded or cut to barCount()` with:

```cpp
    void setEnvelope (std::vector<float> envelope);  // the stem's 100 Hz peak envelope (levels 0..1)
    void setView (juce::Range<double>);              // the song fractions the waveform shows
```

  - delete the member `std::vector<float> levels;`.

  **5c.** In `Source/StemRow.cpp`, add `#include "Peaks.h"` under `#include "StemRow.h"`. Replace everything from `void WaveformView::setLevels` down to the end of `WaveformView::mouseUp` with:

```cpp
void WaveformView::setEnvelope (std::vector<float> e)
{
    envelope = std::move (e);
    rebuildBars();
    repaint();
}

void WaveformView::setBars (int n)
{
    count = n;
    rebuildBars();
    repaint();
}

void WaveformView::setView (juce::Range<double> v)
{
    if (v == view || v.getLength() <= 0.0) return;
    view = v;
    rebuildBars();
    repaint();
}

void WaveformView::setStemColour (juce::Colour c)
{
    if (c != colour) { colour = c; repaint(); }
}

void WaveformView::setFraction (double f)
{
    fraction = f;
}

void WaveformView::resized()
{
    rebuildBars();
}

void WaveformView::rebuildBars()
{
    bars.clear();
    if (count <= 0)
        return;
    const auto levels = barsFromEnvelope (envelope, view, count);
    const auto h = (float) (getHeight() - 2 * margin), mid = (float) margin + h / 2.0f;
    const auto step = (float) getWidth() / (float) count;
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto half = juce::jmax (0.75f, juce::jlimit (0.0f, 1.0f, levels[i]) * (h / 2.0f - 0.5f));
        bars.addRectangle ((float) i * step, mid - half, step * 0.6f, 2.0f * half);
    }
}

double WaveformView::fractionAt (float x) const
{
    return view.getStart() + juce::jlimit (0.0, 1.0, (double) x / getWidth()) * view.getLength();
}

void WaveformView::paint (juce::Graphics& g)
{
    g.setColour (colour.withAlpha (0.3f));
    g.fillPath (bars);
    // The played part, snapped to whole pixels as the screen repaints per playhead pixel.
    const auto head = juce::jlimit (0, getWidth(), juce::roundToInt ((fraction - view.getStart()) / view.getLength() * getWidth()));
    if (head <= 0)
        return;
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (0, 0, head, getHeight());
    g.setColour (colour);
    g.fillPath (bars);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    looping = false;
    if (getWidth() <= 0) return;
    downFraction = juce::jmin (0.999, fractionAt (e.position.x));
    if (onSeek != nullptr) onSeek (downFraction);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (getWidth() <= 0 || (! looping && e.getDistanceFromDragStart() < 4)) return;
    looping = true;                                  // 4 px or more: this gesture selects a loop instead of seeking
    if (onLoopDrag != nullptr) onLoopDrag (downFraction, fractionAt (e.position.x), false);
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (looping && onLoopDrag != nullptr && getWidth() > 0)
        onLoopDrag (downFraction, fractionAt (e.position.x), true);
    looping = false;
}
```

  **5d.** In the `StemRow` constructor, delete `setPeaks ({});`.

  **5e.** In `StemRow::setCompact`, replace `setPeaks (std::move (levels));` with `wave.setBars (barCount());`.

  **5f.** Replace `StemRow::setPeaks` with:

```cpp
void StemRow::setEnvelope (std::vector<float> e)
{
    wave.setEnvelope (std::move (e));
}

void StemRow::setView (juce::Range<double> v)
{
    wave.setView (v);
}
```

- [ ] **Step 6: StemsScreen view, zoom, wheel and strip.**

  **6a.** In `Source/Screens.h`, in `StemsScreen`'s public part, after `void setAudible (juce::uint32 mask);` add:

```cpp
    // The shared view: the song fractions every row shows (the whole song by default; new stems reset it).
    void setView (juce::Range<double>);             // kept inside 0..1
    juce::Range<double> getView() const { return view; }
    void zoom (double atFraction, double factor);    // factor < 1 zooms in; `atFraction` stays under the mouse; 2 s at most
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;   // Ctrl zooms, plain scrolls
```

  In the private part:
  - add `double fractionAt (float x) const;          // the song fraction at panel x (through the view)` after `int headX (double fraction) const;`;
  - change the `headX` comment to `// panel x of a song fraction (through the view)`;
  - add the members `juce::Range<double> view { 0.0, 1.0 };` and `double position = 0.0;                  // the playhead, as a song fraction` after `Tempo tempo;`.

  **6b.** In `Source/Screens.cpp`, change the comment line to `// Panel padding 16 18: content (19, 17, 674, 366). Song row 36, divider at 63, rows from 74 (283 tall), position strip at 373.` and add under it:

```cpp
namespace
{
    constexpr int waveLeft = 19 + 194, waveWidth = 376;   // the waveforms: rows at x 19, waveform at row x 194
    constexpr float stripY = 373.0f;                      // the position strip (4 tall) while zoomed, in the old footer's slot
    const juce::Rectangle<int> stripArea { waveLeft - 2, 370, waveWidth + 4, 10 };   // with the loop ticks
}
```

  **6c.** In `StemsScreen::setStems`, add `view = { 0.0, 1.0 };` and `position = 0.0;` after `lastHead = -1;`. Then replace the peak job, from `// One job reads every row's peaks …` to the end of the function, with:

```cpp
    // One job reads every row's peak envelope in order; a newer setStems (or the destructor) stops it and drops its result.
    const int gen = generation;
    pool.addJob ([safe = juce::Component::SafePointer<StemsScreen> (this), files, gen]
    {
        const std::function<bool()> shouldExit = [] { return juce::ThreadPoolJob::getCurrentThreadPoolJob()->shouldExit(); };
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::vector<std::vector<float>> envelopes;
        double seconds = 0.0;
        for (const auto& f : files)
        {
            if (shouldExit()) return;
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
            envelopes.push_back (reader != nullptr ? readEnvelope (*reader, 100.0, shouldExit) : std::vector<float>());
            if (reader != nullptr && seconds == 0.0) seconds = lengthSeconds (*reader);
        }
        Tempo found;
        for (const auto& f : files)
            if (f.getFileNameWithoutExtension().equalsIgnoreCase ("drums"))
                if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) })
                    found = detectTempo (*r);
        if (shouldExit()) return;
        juce::MessageManager::callAsync ([safe, gen, envelopes = std::move (envelopes), seconds, found]() mutable
        {
            if (safe == nullptr || safe->generation != gen) return;
            for (int i = 0; i < juce::jmin ((int) envelopes.size(), safe->rows.size()); ++i)
                safe->rows[i]->setEnvelope (std::move (envelopes[(size_t) i]));
            safe->length = seconds;
            safe->peaksReady = true;
            safe->timeText = mmss (0.0) + " / " + mmss (seconds);
            safe->setTempo (found);
            safe->repaint();
        });
    });
```

  **6d.** Replace `StemsScreen::headX` and `StemsScreen::waveSpanArea` with:

```cpp
int StemsScreen::headX (double f) const
{
    return waveLeft + juce::roundToInt ((f - view.getStart()) / view.getLength() * waveWidth);
}

double StemsScreen::fractionAt (float x) const
{
    return view.getStart() + (x - (float) waveLeft) / (double) waveWidth * view.getLength();
}

juce::Rectangle<int> StemsScreen::waveSpanArea() const
{
    const auto span = waveSpan();
    return { waveLeft - 3, span.getStart(), waveWidth + 6, span.getLength() };
}
```

  **6e.** Add after `StemsScreen::setAudible`:

```cpp
void StemsScreen::setView (juce::Range<double> v)
{
    const auto w = juce::jlimit (1e-9, 1.0, v.getLength());
    const auto start = juce::jlimit (0.0, 1.0 - w, v.getStart());
    if (juce::Range<double> (start, start + w) == view) return;
    view = { start, start + w };
    for (auto* r : rows) r->setView (view);
    lastHead = headX (position);
    repaint();
}

void StemsScreen::zoom (double at, double factor)
{
    const auto minWidth = length > 2.0 ? 2.0 / length : 1.0;   // 2 s across at most
    const auto w = juce::jlimit (minWidth, 1.0, view.getLength() * factor);
    const auto rel = (at - view.getStart()) / view.getLength();  // where `at` sits in the view, kept through the zoom
    setView ({ at - rel * w, at - rel * w + w });
}

void StemsScreen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // The rows and waveforms don't handle the wheel, so it reaches the screen. ponytail: one step per wheel event, so a
    // trackpad's many small events move fast. Upgrade path: scale the step by the delta.
    const auto p = e.getEventRelativeTo (this).position;
    const auto span = waveSpan();
    if (rows.isEmpty() || p.x < (float) waveLeft || p.x > (float) (waveLeft + waveWidth)
        || p.y < (float) span.getStart() || p.y > (float) span.getEnd())
        return;
    if (e.mods.isCtrlDown())
    {
        if (wheel.deltaY != 0.0f) zoom (fractionAt (p.x), wheel.deltaY > 0.0f ? 0.8 : 1.25);
    }
    else if (view.getLength() < 1.0)
    {
        const auto d = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;   // down (or a left swipe) = later in the song
        if (d != 0.0f) setView (view + (d < 0.0f ? 0.1 : -0.1) * view.getLength());
    }
}
```

  **6f.** In `StemsScreen::setPlayback`, add `position = f;` after `f = juce::jlimit (0.0, 1.0, f);`.

  **6g.** In `StemsScreen::setLoop`, add `repaint (stripArea);                          // the strip's loop ticks` before `loopButton.setEnabled (…)`.

  **6h.** Replace `StemsScreen::paintOverChildren` with:

```cpp
void StemsScreen::paintOverChildren (juce::Graphics& g)
{
    if (rows.isEmpty()) return;
    const auto span = waveSpan();
    const auto top = (float) span.getStart(), h = (float) span.getLength();
    const int right = waveLeft + waveWidth;
    const auto band = ! dragBand.isEmpty() ? dragBand : loopOn ? loopRange : juce::Range<double>();
    if (! band.isEmpty())
    {
        // Clipped to the view: an edge outside it isn't drawn.
        const auto x0 = headX (band.getStart()), x1 = headX (band.getEnd());
        const auto a = juce::jlimit (waveLeft, right, x0), b = juce::jlimit (waveLeft, right, x1);
        g.setColour (Theme::cream.withAlpha (0.10f));
        g.fillRect ((float) a, top, (float) (b - a), h);
        g.setColour (Theme::cream);
        for (const auto x : { x0, x1 })
            if (x >= waveLeft && x <= right)
                g.fillRect ((float) x - 1.0f, top, 2.0f, h);
    }
    if (lastHead < waveLeft || lastHead > right) return;   // no playhead, or it's outside the view
    g.setColour (Theme::cream);
    g.fillRoundedRectangle ((float) lastHead - 1.0f, top, 2.0f, h, 1.0f);
}
```

  **6i.** At the end of `StemsScreen::paint`, after the divider's `g.fillRect (19.0f, 63.0f, 674.0f, 1.0f);`, add:

```cpp
    // Position strip (only while zoomed): where the view sits in the song, the loop edges in the accent colour.
    if (view.getLength() < 1.0 && g.clipRegionIntersects (stripArea))
    {
        g.setColour (Theme::cream.withAlpha (0.10f));
        g.fillRoundedRectangle ((float) waveLeft, stripY, (float) waveWidth, 4.0f, 2.0f);
        g.setColour (Theme::cream.withAlpha (0.55f));
        g.fillRoundedRectangle ((float) waveLeft + (float) view.getStart() * waveWidth, stripY,
                                juce::jmax (2.0f, (float) view.getLength() * waveWidth), 4.0f, 2.0f);
        if (loopOn)
        {
            g.setColour (theme.accent());
            for (const auto f : { loopRange.getStart(), loopRange.getEnd() })
                g.fillRect ((float) waveLeft + (float) f * waveWidth - 1.0f, stripY - 2.0f, 2.0f, 8.0f);
        }
    }
```

  **6j.** In `StemsScreen::playheadArea` and `StemsScreen::loopArea`, nothing changes: they already go through `headX`.

- [ ] **Step 7: Run the tests.**
  - Build Debug and run `lisn_tests.exe`. Every suite passes: `Peaks` has the new envelope tests, `StemRow` the view test, and `UiState` the zoom test.
  - Then run ctest and expect 100%.
  - Check that `grep -rn "readPeaks\|setPeaks\|setLevels" Source tests` prints nothing.

- [ ] **Step 8: Commit.**

```bash
git add Source/Peaks.h Source/Peaks.cpp Source/StemRow.h Source/StemRow.cpp Source/Screens.h Source/Screens.cpp tests/PeaksTests.cpp tests/StemRowTests.cpp tests/UiStateTests.cpp
git commit -m "feat: 100 Hz peak envelope per stem, one shared view, Ctrl+wheel zoom, wheel scroll and a position strip

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Loop handles, adaptive grid with Alt, grid lines, loop length in beats, a Loop button that makes a loop

**Files:**
- Modify: `Source/StemRow.h`, `Source/StemRow.cpp`, `Source/Screens.h`, `Source/Screens.cpp`, `Source/PluginEditor.cpp`, `tests/StemRowTests.cpp`, `tests/UiStateTests.cpp`

**Interfaces:**
- Consumes: Task 3's `StemsScreen::view`, `headX`, `setView`, `waveLeft`, `waveWidth`, `onSetLoop`, `onSeek`, `loopRange`, `loopOn`, `dragBand` and `tempo`.
- Produces:
  - `enum class WaveMouse { move, down, drag, up, exit };` (in `StemRow.h`).
  - `WaveformView::onMouse` and `StemRow::onWaveMouse`, both `std::function<void (WaveMouse, double songFraction, bool alt)>`. These replace `onSeek` and `onLoopDrag` on both classes.
  - `StemsScreen::waveMouse (WaveMouse, double, bool)`, `double gridStep() const` (seconds; 0 = no grid), `double snap (double fraction, bool free = false) const` and `int hotEdge() const`.
  - `juce::String beatsText (double beats)`, a free function in `Screens.h`.
  - `void StemsScreen::toggleLoop()`: with a loop range it calls `onToggleLoop`; with none it sends `defaultLoop()` to `onSetLoop`.
  - `juce::Range<double> StemsScreen::defaultLoop() const`: 4 bars from the bar under the playhead, or 8 s without a BPM.
  - `loopButtonEnabled()` is deleted, because the Loop button is never disabled now.

- [ ] **Step 1: Failing tests.**

  **1a.** In `tests/StemRowTests.cpp`, add `#include <tuple>` under the includes. In the view test from Task 3, delete everything from `double seekTo = -1.0;` through `row.onSeek = nullptr;`. `onSeek` goes, and the test below covers the mapping.

  **1b.** Replace the whole `"waveform drag selects a loop; a click only seeks"` block with:

```cpp
        beginTest ("the waveform reports the mouse as song fractions through the view, with Alt");
        {
            std::vector<std::tuple<WaveMouse, double, bool>> got;
            row.onWaveMouse = [&] (WaveMouse m, double f, bool alt) { got.push_back ({ m, f, alt }); };
            auto& w = row.waveform();
            auto event = [&] (float x, juce::ModifierKeys mods)
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::Point<float> p (x, 20.0f);
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, &w, &w, now, p, now, 1, false);
            };
            row.setView ({ 0.5, 1.0 });
            w.mouseMove (event (94.0f, {}));
            w.mouseDown (event (94.0f, {}));
            w.mouseDrag (event (500.0f, juce::ModifierKeys (juce::ModifierKeys::altModifier)));   // past the right edge
            w.mouseUp (event (188.0f, {}));
            w.mouseExit (event (188.0f, {}));
            expectEquals ((int) got.size(), 5);
            expect (std::get<0> (got[0]) == WaveMouse::move && std::get<0> (got[1]) == WaveMouse::down
                    && std::get<0> (got[2]) == WaveMouse::drag && std::get<0> (got[3]) == WaveMouse::up
                    && std::get<0> (got[4]) == WaveMouse::exit);
            expectWithinAbsoluteError (std::get<1> (got[1]), 0.625, 1e-9);   // x 94 = 0.25 of the 0.5-wide view
            expectWithinAbsoluteError (std::get<1> (got[2]), 1.0, 1e-9);     // clamped to the view
            expectWithinAbsoluteError (std::get<1> (got[3]), 0.75, 1e-9);
            expect (std::get<2> (got[2]) && ! std::get<2> (got[1]));
            row.setView ({ 0.0, 1.0 });
        }
```

  **1c.** In `tests/UiStateTests.cpp`, in the `"loop: snapping, button, chip, renders"` test, replace the two lines `expectEquals (screen.snap (0.3), 0.0);` and `expectEquals (screen.snap (0.8), 1.0);` with:

```cpp
            // 0.5 s stems at 120 BPM: a quarter beat (0.125 s) is 94 px wide, so edges snap to quarter beats.
            expectWithinAbsoluteError (screen.snap (0.3), 0.25, 1e-9);
            expectWithinAbsoluteError (screen.snap (0.8), 0.75, 1e-9);
```

  In the same test, delete the two lines `expect (! screen.loopButtonEnabled());` and `expect (screen.loopButtonEnabled());`, because the button is never disabled now.

  **1c-2.** In the keys test from Task 1, replace these lines:

```cpp
            expect (ed.keyPressed (juce::KeyPress ('L')));          // no loop range yet: nothing to toggle
            expect (! proc.player.isLooping());
            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (ed.keyPressed (juce::KeyPress ('l')));
            expect (! proc.player.isLooping());
            ed.keyPressed (juce::KeyPress ('L'));
            expect (proc.player.isLooping());
```

  with:

```cpp
            expect (ed.keyPressed (juce::KeyPress ('L')));          // no loop yet: L makes one (here the whole 0.5 s song)
            expect (proc.player.isLooping());
            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (ed.keyPressed (juce::KeyPress ('l')));          // with a loop, L toggles it
            ed.tick();
            expect (! proc.player.isLooping());
            ed.keyPressed (juce::KeyPress ('L'));
            ed.tick();
            expect (proc.player.isLooping());
```

  **1d.** Add these two tests after the zoom test from Task 3:

```cpp
        beginTest ("grid step follows the zoom; Alt and no tempo don't snap; loop length in beats");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);
            screen.setTempo ({ 92.0, 0.2 });
            const double beat = 60.0 / 92.0;
            expectWithinAbsoluteError (screen.gridStep(), 4.0 * beat, 1e-9);   // whole song: a beat is 1.3 px, so bars
            screen.setView ({ 60.0 / 192.0, 100.0 / 192.0 });                   // 40 s across: a beat is 6.1 px
            expectWithinAbsoluteError (screen.gridStep(), beat, 1e-9);
            screen.setView ({ 74.0 / 192.0, 88.0 / 192.0 });                    // 14 s across: a quarter beat is 4.4 px
            expectWithinAbsoluteError (screen.gridStep(), beat / 4.0, 1e-9);
            const double f = 80.0 / 192.0, k = (screen.snap (f) * 192.0 - 0.2) / (beat / 4.0);
            expectWithinAbsoluteError (k, std::round (k), 1e-6);                // on the quarter-beat grid
            expectWithinAbsoluteError (screen.snap (f) * 192.0, 80.0, beat / 8.0 + 1e-9);
            expectEquals (screen.snap (f, true), f);                            // Alt: free
            screen.setTempo ({});
            expectEquals (screen.gridStep(), 0.0);
            expectEquals (screen.snap (f), f);

            expectEquals (beatsText (8.0), juce::String ("8 beats"));
            expectEquals (beatsText (10.26), juce::String ("10.25 beats"));
            expectEquals (beatsText (2.5), juce::String ("2.5 beats"));
            expectEquals (beatsText (1.05), juce::String ("1 beat"));
        }

        beginTest ("handles: hover, trim with snapping and Alt, never crossing; a drag elsewhere makes a new loop");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);
            screen.setTempo ({ 92.0, 0.2 });
            screen.setView ({ 74.0 / 192.0, 88.0 / 192.0 });                    // quarter-beat grid
            const double beat = 60.0 / 92.0, a = (0.2 + 120 * beat) / 192.0, b = (0.2 + 128 * beat) / 192.0;
            screen.setLoop ({ a, b }, true);                                    // 8 beats
            std::vector<std::pair<double, double>> sets;
            screen.onSetLoop = [&] (double from, double to) { sets.push_back ({ from, to }); };
            double seekTo = -1.0;
            screen.onSeek = [&] (double f) { seekTo = f; };
            const double px = (14.0 / 192.0) / 376.0;                           // one pixel, as a song fraction

            screen.waveMouse (WaveMouse::move, b + 3 * px, false);
            expectEquals (screen.hotEdge(), 1);
            screen.waveMouse (WaveMouse::move, b + 9 * px, false);
            expectEquals (screen.hotEdge(), -1);

            // Trim the end two beats in: snapped, sent once on release, and grabbing a handle doesn't seek.
            const double near = b - 2 * beat / 192.0 + 0.05 / 192.0;
            screen.waveMouse (WaveMouse::down, b + 2 * px, false);
            expectEquals (seekTo, -1.0);
            screen.waveMouse (WaveMouse::drag, near, false);
            expect (sets.empty());
            screen.waveMouse (WaveMouse::up, near, false);
            expectEquals ((int) sets.size(), 1);
            expectWithinAbsoluteError (sets[0].first, a, 1e-12);
            expectWithinAbsoluteError (sets[0].second, b - 2 * beat / 192.0, 1e-9);

            // The start can't cross the end: it stops one step (a quarter beat) short.
            screen.waveMouse (WaveMouse::down, a, false);
            screen.waveMouse (WaveMouse::drag, b + 1.0 / 192.0, false);
            screen.waveMouse (WaveMouse::up, b + 1.0 / 192.0, false);
            expectEquals ((int) sets.size(), 2);
            expectWithinAbsoluteError (sets[1].first, b - beat / 4.0 / 192.0, 1e-9);
            expectWithinAbsoluteError (sets[1].second, b, 1e-12);

            // Alt: the end follows the mouse exactly.
            const double freeAt = b - 0.07 / 192.0;
            screen.waveMouse (WaveMouse::down, b, true);
            screen.waveMouse (WaveMouse::drag, freeAt, true);
            screen.waveMouse (WaveMouse::up, freeAt, true);
            expectEquals ((int) sets.size(), 3);
            expectWithinAbsoluteError (sets[2].second, freeAt, 1e-12);

            // Away from the edges: a click seeks, a drag makes a new snapped loop (here an inner one).
            const double mid = (a + b) / 2.0;
            screen.waveMouse (WaveMouse::down, mid, false);
            screen.waveMouse (WaveMouse::up, mid, false);
            expectWithinAbsoluteError (seekTo, mid, 1e-12);
            expectEquals ((int) sets.size(), 3);
            screen.waveMouse (WaveMouse::down, mid, false);
            screen.waveMouse (WaveMouse::drag, mid + 30 * px, false);
            screen.waveMouse (WaveMouse::up, mid + 30 * px, false);
            expectEquals ((int) sets.size(), 4);
            expect (sets[3].first > a && sets[3].second < b);
            const double k = (sets[3].first * 192.0 - 0.2) / (beat / 4.0);
            expectWithinAbsoluteError (k, std::round (k), 1e-6);
        }

        beginTest ("the Loop button (and L) makes a 4-bar loop at the playhead when there's none, else toggles");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 70.0 / 192.0, 192.0);        // the playhead at 1:10 of a 3:12 song
            std::pair<double, double> set { -1.0, -1.0 };
            screen.onSetLoop = [&] (double a, double b) { set = { a, b }; };
            int toggles = 0;
            screen.onToggleLoop = [&] { ++toggles; };

            screen.toggleLoop();                                    // no tempo: 8 s from the playhead
            expectWithinAbsoluteError (set.first * 192.0, 70.0, 1e-9);
            expectWithinAbsoluteError (set.second * 192.0, 78.0, 1e-9);

            screen.setTempo ({ 92.0, 0.2 });
            const double bar = 4.0 * 60.0 / 92.0;
            screen.toggleLoop();                                    // from the bar under the playhead, 4 bars long
            const double k = (set.first * 192.0 - 0.2) / bar;
            expectWithinAbsoluteError (k, std::round (k), 1e-9);
            expect (set.first * 192.0 <= 70.0 && set.first * 192.0 > 70.0 - bar);
            expectWithinAbsoluteError ((set.second - set.first) * 192.0, 4.0 * bar, 1e-9);

            screen.setPlayback (false, 191.0 / 192.0, 192.0);       // it would run past the end: the song's last 4 bars
            screen.toggleLoop();
            expectWithinAbsoluteError (set.second, 1.0, 1e-12);
            expectWithinAbsoluteError ((set.second - set.first) * 192.0, 4.0 * bar, 1e-9);
            expectEquals (toggles, 0);

            screen.setLoop ({ 0.2, 0.3 }, false);                   // with a loop range, it only toggles
            screen.toggleLoop();
            expectEquals (toggles, 1);
        }
```

- [ ] **Step 2: Run them and watch them fail.** Run `cmake --build build --config Debug`. Expected: the build fails on `WaveMouse`, `onWaveMouse`, `waveMouse`, `gridStep`, `hotEdge`, `beatsText` and `toggleLoop`. That's the red step.

- [ ] **Step 3: WaveMouse events.**

  **3a.** In `Source/StemRow.h`, add above the `WaveformView` class:

```cpp
enum class WaveMouse { move, down, drag, up, exit };   // what the mouse did over a waveform
```

  **3b.** In `WaveformView`:
  - change the class comment's last sentence to `It reports the mouse as song fractions (through the view) to onMouse.`;
  - delete `onSeek` and `onLoopDrag`, and replace them with:

```cpp
    std::function<void (WaveMouse, double, bool)> onMouse;   // what happened, the song fraction under the mouse, Alt held
```

  - add `void mouseMove (const juce::MouseEvent&) override;` and `void mouseExit (const juce::MouseEvent&) override;` beside the other mouse overrides;
  - add the private `void send (WaveMouse, const juce::MouseEvent&);`;
  - change the member line `double fraction = 0.0, downFraction = 0.0;` to `double fraction = 0.0;`, and delete `bool looping = false;`.

  **3c.** In `StemRow`, replace the two members `std::function<void (double)> onSeek;` and `std::function<void (double, double, bool)> onLoopDrag;` with:

```cpp
    std::function<void (WaveMouse, double, bool)> onWaveMouse;   // the waveform's mouse, as song fractions
```

  **3d.** In `Source/StemRow.cpp`, replace `WaveformView::mouseDown`, `mouseDrag` and `mouseUp` with:

```cpp
void WaveformView::send (WaveMouse m, const juce::MouseEvent& e)
{
    if (onMouse != nullptr && getWidth() > 0)
        onMouse (m, fractionAt (e.position.x), e.mods.isAltDown());
}

void WaveformView::mouseMove (const juce::MouseEvent& e) { send (WaveMouse::move, e); }
void WaveformView::mouseDown (const juce::MouseEvent& e) { send (WaveMouse::down, e); }
void WaveformView::mouseDrag (const juce::MouseEvent& e) { send (WaveMouse::drag, e); }
void WaveformView::mouseUp (const juce::MouseEvent& e)   { send (WaveMouse::up, e); }
void WaveformView::mouseExit (const juce::MouseEvent& e) { send (WaveMouse::exit, e); }
```

  **3e.** In the `StemRow` constructor, replace the two lines `wave.onSeek = …` and `wave.onLoopDrag = …` with:

```cpp
    wave.onMouse = [this] (WaveMouse m, double f, bool alt) { if (onWaveMouse != nullptr) onWaveMouse (m, f, alt); };
```

- [ ] **Step 4: StemsScreen: grid step, snapping, handles.**

  **4a.** In `Source/Screens.h`, add above `class StemsScreen`:

```cpp
juce::String beatsText (double beats);   // "8 beats", "10.25 beats", "1 beat": rounded to a quarter beat
```

  **4b.** In `StemsScreen`'s public part, replace `double snap (double fraction) const;             // to the nearest beat; unchanged without a tempo` with:

```cpp
    // The grid: the finest of 1/4 beat, 1 beat and 1 bar (4 beats) that is at least 4 px wide at the current zoom, in
    // seconds; 0 without a tempo.
    double gridStep() const;
    double snap (double fraction, bool free = false) const;   // to the nearest grid line; unchanged when free (Alt) or no grid
    // A waveform's mouse (song fractions). Within 6 px of a loop edge, while looping, it hovers and drags that edge (a
    // handle); elsewhere a click seeks and a drag of 4 px or more makes a new loop. Loops go to onSetLoop on release.
    void waveMouse (WaveMouse, double fraction, bool alt);
    int hotEdge() const { return hot; }              // 0 = loop start, 1 = loop end, -1 = none (test hook)
```

  In the private part, add:

```cpp
    int edgeNear (int x) const;                      // the loop edge within 6 px of panel x: 0 / 1, or -1
    juce::Range<double> trimmed (double fraction, bool free) const;   // the loop with the grabbed edge moved there
    void setHot (int edge);                          // the hovered handle: its tab and the resize cursor
```

  and add the members `int hot = -1, grab = -1, downX = 0;`, `double downFraction = 0.0;` and `bool dragging = false;               // a drag away from the handles: a new loop` after `double position = 0.0;`.

  **4c.** In `Source/Screens.cpp`, add after the `stripArea` namespace block:

```cpp
juce::String beatsText (double beats)
{
    const auto q = std::round (beats * 4.0) / 4.0;
    const auto s = juce::String (q, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return s + (juce::exactlyEqual (q, 1.0) ? " beat" : " beats");
}
```

  **4d.** In `StemsScreen::setStems`:
  - add `hot = grab = -1;` and `dragging = false;` after `position = 0.0;`;
  - in the row loop, replace the `row->onSeek = …` line and the whole `row->onLoopDrag = [this] (…) { … };` lambda with:

```cpp
        row->onWaveMouse = [this] (WaveMouse m, double f, bool alt) { waveMouse (m, f, alt); };
```

  **4e.** Replace `StemsScreen::snap` with:

```cpp
double StemsScreen::gridStep() const
{
    if (tempo.bpm <= 0.0 || length <= 0.0) return 0.0;
    const auto beat = 60.0 / tempo.bpm, pxPerSecond = waveWidth / (view.getLength() * length);
    for (const auto step : { beat / 4.0, beat })
        if (step * pxPerSecond >= 4.0) return step;
    return 4.0 * beat;   // ponytail: bars are 4 beats from firstBeat (4/4 only). Upgrade path: a time-signature setting.
}

double StemsScreen::snap (double f, bool free) const
{
    const auto step = gridStep();
    if (free || step <= 0.0) return f;
    const auto t = tempo.firstBeat + std::round ((f * length - tempo.firstBeat) / step) * step;
    return juce::jlimit (0.0, 1.0, t / length);
}

int StemsScreen::edgeNear (int x) const
{
    if (! loopOn || loopRange.isEmpty()) return -1;
    const auto ds = std::abs (x - headX (loopRange.getStart())), de = std::abs (x - headX (loopRange.getEnd()));
    if (juce::jmin (ds, de) > 6) return -1;
    return de <= ds ? 1 : 0;
}

juce::Range<double> StemsScreen::trimmed (double f, bool free) const
{
    const auto step = gridStep();
    const auto minLength = ! free && step > 0.0 ? step / length : (length > 0.0 ? 0.01 / length : 0.0);   // a step, or 10 ms
    const auto e = snap (f, free);
    if (grab == 0) return { juce::jmax (0.0, juce::jmin (e, loopRange.getEnd() - minLength)), loopRange.getEnd() };
    return { loopRange.getStart(), juce::jmin (1.0, juce::jmax (e, loopRange.getStart() + minLength)) };
}

void StemsScreen::setHot (int edge)
{
    if (edge == hot) return;
    hot = edge;
    for (auto* r : rows)
        r->waveform().setMouseCursor (edge >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
    repaint (loopArea (loopRange));
}

void StemsScreen::waveMouse (WaveMouse m, double f, bool free)
{
    const auto x = headX (f);
    switch (m)
    {
        case WaveMouse::move:
            setHot (edgeNear (x));
            break;
        case WaveMouse::exit:
            if (grab < 0) setHot (-1);
            break;
        case WaveMouse::down:
            grab = edgeNear (x);
            downX = x;
            downFraction = f;
            dragging = false;
            if (grab < 0 && onSeek != nullptr) onSeek (juce::jmin (f, 0.999));
            break;
        case WaveMouse::drag:
            if (grab >= 0)
                dragBand = trimmed (f, free);
            else if (dragging || std::abs (x - downX) >= 4)   // 4 px or more: a new loop instead of a seek
            {
                dragging = true;
                dragBand = { snap (juce::jmin (downFraction, f), free), snap (juce::jmax (downFraction, f), free) };
            }
            repaint (waveSpanArea());
            break;
        case WaveMouse::up:
            if ((grab >= 0 || dragging) && ! dragBand.isEmpty() && onSetLoop != nullptr)
                onSetLoop (dragBand.getStart(), dragBand.getEnd());
            dragBand = {};
            grab = -1;
            dragging = false;
            repaint (waveSpanArea());
            break;
    }
}
```

  **4f.** The tabs sit above the band, so widen the repaint areas. Replace `StemsScreen::waveSpanArea` and `StemsScreen::loopArea` with:

```cpp
juce::Rectangle<int> StemsScreen::waveSpanArea() const   // the waveforms plus the tabs above them
{
    const auto span = waveSpan();
    return { waveLeft - 9, span.getStart() - 13, waveWidth + 18, span.getLength() + 13 };
}

juce::Rectangle<int> StemsScreen::loopArea (juce::Range<double> r) const   // the band, its edges and tabs (a hot tab's ring)
{
    const auto span = waveSpan();
    return juce::Rectangle<int>::leftTopRightBottom (headX (r.getStart()) - 9, span.getStart() - 13,
                                                     headX (r.getEnd()) + 9, span.getEnd());
}
```

  **4g.** In `StemsScreen::setLoop`, add `if (! on) setHot (-1);` right after `loopOn = on;`.

  **4h.** Make the Loop button always work.
  - In `Source/Screens.h`, delete `bool loopButtonEnabled() const { return loopButton.isEnabled(); }   // test hook`.
  - After `void setLoop (…);` add:

```cpp
    // The Loop button and the L key: with a loop range, onToggleLoop; with none, onSetLoop (defaultLoop()), which turns
    // looping on.
    void toggleLoop();
    juce::Range<double> defaultLoop() const;         // 4 bars from the bar under the playhead (8 s without a tempo)
```

  - In `Source/Screens.cpp`, in the `StemsScreen` constructor, delete `loopButton.setEnabled (false);` and `loopButton.setAlpha (0.55f);`, and change the Loop button's handler to `loopButton.onClick = [this] { toggleLoop(); };`.
  - In `StemsScreen::setLoop`, delete the two lines `loopButton.setEnabled (! loopRange.isEmpty());` and `loopButton.setAlpha (loopRange.isEmpty() ? 0.55f : 1.0f);`.
  - Add after `StemsScreen::setLoop`:

```cpp
void StemsScreen::toggleLoop()
{
    if (! loopRange.isEmpty())
    {
        if (onToggleLoop != nullptr) onToggleLoop();
        return;
    }
    const auto r = defaultLoop();
    if (! r.isEmpty() && onSetLoop != nullptr) onSetLoop (r.getStart(), r.getEnd());
}

juce::Range<double> StemsScreen::defaultLoop() const
{
    if (length <= 0.0) return {};
    const auto t = position * length;
    auto start = t, len = 8.0;                                   // no tempo: 8 s from the playhead
    if (tempo.bpm > 0.0)
    {
        const auto bar = 4.0 * 60.0 / tempo.bpm;
        start = tempo.firstBeat + std::floor ((t - tempo.firstBeat) / bar) * bar;   // the bar under the playhead
        len = 4.0 * bar;
    }
    if (start + len > length) start = length - len;             // past the end: the song's last 4 bars (or 8 s)
    start = juce::jmax (0.0, start);                             // a shorter song loops whole
    return { start / length, juce::jmin (length, start + len) / length };
}
```

  **4i.** In `Source/PluginEditor.cpp`, in `keyPressed`, replace the `L` branch's body `if (stems.loopButtonEnabled()) stems.onToggleLoop();` with `stems.toggleLoop();`.

- [ ] **Step 5: Draw the grid, the tabs and the loop length.**

  **5a.** Replace `StemsScreen::paintOverChildren` with:

```cpp
void StemsScreen::paintOverChildren (juce::Graphics& g)
{
    if (rows.isEmpty()) return;
    const auto span = waveSpan();
    const auto top = (float) span.getStart(), h = (float) span.getLength();
    const int right = waveLeft + waveWidth;

    // Grid (LoopHandles.mockup.html): a line per beat at cream 0.16, per bar at 0.34, while beats are 8 px or more apart.
    if (tempo.bpm > 0.0 && length > 0.0)
    {
        const auto beat = 60.0 / tempo.bpm;
        if (beat / (view.getLength() * length) * waveWidth >= 8.0)
            for (auto k = (int) std::ceil ((view.getStart() * length - tempo.firstBeat) / beat);; ++k)
            {
                const auto x = headX ((tempo.firstBeat + k * beat) / length);
                if (x > right) break;
                g.setColour (Theme::cream.withAlpha (((k % 4) + 4) % 4 == 0 ? 0.34f : 0.16f));
                g.fillRect ((float) x, top, 1.0f, h);
            }
    }

    const auto band = ! dragBand.isEmpty() ? dragBand : loopOn ? loopRange : juce::Range<double>();
    if (! band.isEmpty())
    {
        // Clipped to the view: an edge outside it isn't drawn, nor is its tab.
        const auto x0 = headX (band.getStart()), x1 = headX (band.getEnd());
        const auto a = juce::jlimit (waveLeft, right, x0), b = juce::jlimit (waveLeft, right, x1);
        g.setColour (Theme::cream.withAlpha (0.10f));
        g.fillRect ((float) a, top, (float) (b - a), h);
        for (int edge = 0; edge < 2; ++edge)
        {
            const auto x = (float) (edge == 0 ? x0 : x1);
            if (x < (float) waveLeft || x > (float) right) continue;
            g.setColour (Theme::cream);
            g.fillRect (x - 1.0f, top, 2.0f, h);
            // The grab tab: 10 x 18, radius 4, top 8 px above the band, two ink grip lines; hovered or dragged it grows to
            // 12 x 22 with a 3 px cream 0.22 ring.
            const bool isHot = edge == hot || edge == grab;
            const auto tab = isHot ? juce::Rectangle<float> (x - 6.0f, top - 10.0f, 12.0f, 22.0f)
                                   : juce::Rectangle<float> (x - 5.0f, top - 8.0f, 10.0f, 18.0f);
            if (isHot)
            {
                g.setColour (Theme::cream.withAlpha (0.22f));
                g.fillRoundedRectangle (tab.expanded (3.0f), 7.0f);
            }
            g.setColour (Theme::cream);
            g.fillRoundedRectangle (tab, 4.0f);
            g.setColour (Theme::ink.withAlpha (0.55f));
            g.fillRect (tab.getCentreX() - 2.0f, tab.getCentreY() - 4.0f, 1.0f, 8.0f);
            g.fillRect (tab.getCentreX() + 1.0f, tab.getCentreY() - 4.0f, 1.0f, 8.0f);
        }
    }

    if (lastHead < waveLeft || lastHead > right) return;   // no playhead, or it's outside the view
    g.setColour (Theme::cream);
    g.fillRoundedRectangle ((float) lastHead - 1.0f, top, 2.0f, h, 1.0f);
}
```

  **5b.** In `StemsScreen::paint`, replace the subtitle's `if (loopOn)` statement (the `subtitle << dot << "loop " …` one and its continuation line) with:

```cpp
        if (loopOn)
        {
            subtitle << dot << "loop " << mmss (loopRange.getStart() * length)
                     << juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) << mmss (loopRange.getEnd() * length);
            if (tempo.bpm > 0.0 && length > 0.0)
                subtitle << dot << beatsText (loopRange.getLength() * length * tempo.bpm / 60.0);
        }
```

- [ ] **Step 6: Run the tests.**
  - Build Debug and run `lisn_tests.exe`. Every suite passes: `StemRow` has the WaveMouse test, and `UiState` the grid, handles and updated snap tests.
  - Then run ctest and expect 100%.
  - Check that `grep -rn "onLoopDrag\|row->onSeek\|wave.onSeek" Source tests` prints nothing.

- [ ] **Step 7: Commit.**

```bash
git add Source/StemRow.h Source/StemRow.cpp Source/Screens.h Source/Screens.cpp Source/PluginEditor.cpp tests/StemRowTests.cpp tests/UiStateTests.cpp
git commit -m "feat: loop edges are handles on an adaptive beat grid (quarter beat when zoomed), Alt drags free, loop length in beats; Loop makes a 4-bar loop when there's none

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Snapshots, bench, full verification, FL click-through

**Files:**
- Modify: `tests/Snapshots.cpp`

**Interfaces:**
- Consumes: `StemsScreen::setView` and `waveMouse`, and `WaveMouse`.

- [ ] **Step 1: New snapshot cases.** In `runSnapshots`, after the `sixMuted` lambda, add:

```cpp
    const auto zoomed = [looping] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        looping (p, ed);
        auto& s = ed.stemsScreen();
        s.setView ({ 0.28, 0.47 });                          // 5.7 s across: beat and bar lines, a quarter-beat grid
        s.waveMouse (WaveMouse::move, p.player.getLoop().getEnd(), false);   // hover the end handle
    };
    const auto sixLooping = [sixMuted] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        sixMuted (p, ed);
        p.player.setLoop (0.30, 0.45);
        ed.tick();
    };
```

  and add these three entries to `shots`, after `stems6-midnight-muted`:

```cpp
        { "stems4-dusk-zoom", "dusk", state (Screen::Stems, false), 1.0f, zoomed },
        { "stems6-midnight-loop", "midnight", state (Screen::Stems, true), 1.0f, sixLooping },
        { "stems4-dusk-zoom@2x", "dusk", state (Screen::Stems, false), 2.0f, zoomed },
```

- [ ] **Step 2: Bench the zoom rebuild.** In `runBench`, before `juce::AudioBuffer<float> buffer (2, 512);`, add:

```cpp
    // A zoom step: every row rebuilds its bars from its envelope (the repaint is async and not timed here).
    {
        const auto z0 = now();
        for (int i = 0; i < 50; ++i)
            stems.setView (i % 2 == 0 ? juce::Range<double> (0.2, 0.4) : juce::Range<double> (0.0, 1.0));
        std::printf ("zoom_rebuild_ms=%.3f\n", (now() - z0) / 50.0);
    }
```

- [ ] **Step 3: Render and read every stems PNG.**
  - Run: `cmake --build build --config Debug`, then `build/lisn_tests_artefacts/Debug/lisn_tests.exe --snapshots snapshots`.
  - Open each of `snapshots/stems4-dusk-mix.png`, `stems4-dusk-zoom.png`, `stems4-dusk-zoom@2x.png`, `stems6-midnight-muted.png`, `stems6-midnight-loop.png` and `stems4-dusk@2x.png` with the Read tool, and compare them with `docs/design/LoopHandles.mockup.html`:
    - colour bars instead of icons, and no footer text;
    - zoomed: beat and bar lines across the rows, both tabs with the end one hot (bigger, ringed), the position strip in the old footer's slot with orange loop ticks, and the subtitle ending in `· N beats`;
    - whole song (`stems6-midnight-loop`): the band and tabs, no grid, no strip;
    - `stems6-midnight-muted` (no loop): the Loop button is off but not dimmed;
    - one playhead; nothing overlaps or clips.
  - Fix any deviation in the source and re-render. List every fix in the report.

- [ ] **Step 4: Full verification (Debug).**
  - Build Debug and run ctest: 100%.
  - Run pluginval `--strictness-level 5` on Debug: SUCCESS.

- [ ] **Step 5: Release bench and pluginval.**
  - Check that FL isn't running: `Get-Process | Where-Object { $_.ProcessName -match 'FL' }`. If it is, stop and report BLOCKED; never kill it.
  - Run `cmake --build build --config Release`.
  - Run `build/lisn_tests_artefacts/Release/lisn_tests.exe --bench` three times. Each run needs `motion_frame_ms` ≤ 4.0, `process_block_us_idle` around 0.005, and no `FAIL` lines. Record `zoom_rebuild_ms`.
  - Run pluginval on `"C:/Program Files/Common Files/VST3/LISN StemSplitter.vst3"`: SUCCESS.
  - Run the Release Standalone (`build/StemSplitter_artefacts/Release/Standalone/LISN StemSplitter.exe`) for 5 s, check that it's still alive, then close it.

- [ ] **Step 6: Commit.**

```bash
git add tests/Snapshots.cpp
git commit -m "test: zoomed-loop and 6-stem loop snapshots, and a zoom rebuild timing in the bench

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: The user's FL click-through (controller and user; not the implementer).** FL loads the Release build through the junction, so no copy and no rescan are needed. Ask the user to check:
  - the look: colour bars, and no footer;
  - Ctrl+wheel zooms around the mouse, the wheel scrolls while zoomed, and the strip shows where the view is;
  - a drag makes a loop, and dragging either tab trims it on the grid (quarter beats when zoomed in, bars when zoomed out);
  - Alt+drag goes off the grid, and an edge can't cross the other;
  - with no loop yet, the Loop button and L make a 4-bar loop at the playhead; after that they toggle it;
  - the keys: Space, L, 1–6, Shift+1–6 and Home, with FL's own shortcuts still working and FL's Space back after clicking FL;
  - Drag loop and a row drag with a loop on still deliver the trimmed loop.
