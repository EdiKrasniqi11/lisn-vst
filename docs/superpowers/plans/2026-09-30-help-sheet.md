# Help Sheet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An info button in the header opens a tabbed "How to use" sheet (Play, Stems, Loop, Export; four tips each) inside the panel, in place of the current screen.

**Architecture:**
- **`InfoButton`** (Controls) is the round "i" button in the header.
- **`Header`** gets the button, `onInfo` and `setInfoOn`.
- **`HelpSheet`** (Screens) paints the title row, the tab pill, the × button, the divider and the tips from a static table.
- **`StemSplitterEditor`** owns the sheet and a `helpOpen` flag. `showScreen()` shows either the current screen or the sheet.

**Tech Stack:** JUCE 8.0.4, CMake, MSVC 2022, C++17. Tests are `juce::UnitTest` suites in `lisn_tests` (run through ctest).

**Spec:** `docs/superpowers/specs/2026-09-30-help-sheet-design.md`. **Mockup:** `docs/design/Help.mockup.html`. Where the text and the mockup disagree, the mockup wins.

**Branch:** `feat/stem-player`, continuing after the spec commit. Don't push.

## Global Constraints

- **UI:**
  - No `ComboBox`, `TextEditor`, `PopupMenu` or `ProgressBar`.
  - **Keyboard focus:** only the editor itself takes focus, when the user clicks anywhere in it. Every child keeps `setWantsKeyboardFocus (false)`, and Buttons also keep `setMouseClickGrabsKeyboardFocus (false)`. The existing test "no focus, no banned controls" checks this.
- **Performance:** a motion frame averages ≤ 4 ms in the Release bench. The sheet is buffered to an image, so the moving waves behind it don't repaint it.
- **Code rules:**
  - Non-ASCII UI text is escaped UTF-8, e.g. `"1\xe2\x80\x93" "6"` for "1–6", read through `juce::CharPointer_UTF8`.
  - Deliberate corner-cuts get a `ponytail:` comment naming the ceiling and the upgrade path.
- **Builds:** run from the repo root with CMake from `"C:\Program Files\CMake\bin"`. In Git Bash: `export PATH="/c/Program Files/CMake/bin:$PATH"`.
  - Build (**Debug only**): `cmake --build build --config Debug`.
  - Tests: `build/lisn_tests_artefacts/Debug/lisn_tests.exe` (exit 0 means pass; failures print the test name), then `ctest --test-dir build -C Debug --output-on-failure`.
  - **Never build Release.** FL Studio loads the Release build through a junction, and the controller builds it with FL closed.
  - **Debug VST3 flake:** if the post-build step fails with `juce_vst3_helper … DLL initialization routine failed`, delete `build/StemSplitter_artefacts/Debug/VST3/LISN StemSplitter.vst3/Contents/x86_64-win/LISN StemSplitter.vst3` and rebuild.
- **Line endings:** the C++ files are CRLF. Edit them with the Edit tool, never with `sed -i` (it strips the CRs).
- **Commits** end with a blank line and then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Use that exact trailer, not your own model name. Don't push.

## File map

| File | Change |
|---|---|
| `Source/Icons.h` | Task 1: `close`. Task 2: `jump`, `home`, `zoom`, `trim`, `toggle`, `grid`, `layers`, `mouse` |
| `Source/Controls.h/.cpp` | Task 1: `InfoButton` |
| `Source/Screens.h/.cpp` | Task 1: the `Header` info button and a `HelpSheet` shell. Task 2: the tips table and `paintTip` |
| `Source/PluginEditor.h/.cpp` | Task 1: `help`, `helpOpen`, `setHelpOpen`, `helpSheet()`, `showScreen()` |
| `tests/UiStateTests.cpp` | Task 1: the toggle test |
| `tests/Snapshots.cpp` | Task 2: four help shots |

---

### Task 1: The info button opens and closes the help sheet

**Files:**
- Modify: `Source/Icons.h`, `Source/Controls.h`, `Source/Controls.cpp`, `Source/Screens.h`, `Source/Screens.cpp`, `Source/PluginEditor.h`, `Source/PluginEditor.cpp`
- Test: `tests/UiStateTests.cpp`

**Interfaces:**
- Produces:
  - `class InfoButton : public juce::Button` with `setTheme (const Theme&)`. Its toggle state means lit.
  - `Header::onInfo`, `Header::setInfoOn (bool)`.
  - `class HelpSheet` with `setTheme`, `setTab (int)`, `getTab()`, `onClose`, and a private `SegmentedPill tabs` and `LisnButton close`.
  - `StemSplitterEditor::setHelpOpen (bool)`, `StemSplitterEditor::helpSheet()`.
  - Task 2 fills `HelpSheet::paint` with the tips.

- [ ] **Step 1: Write the failing test.** In `tests/UiStateTests.cpp`, add this test at the end of `runTest()`, after the last `beginTest` block ("switching 6 to 4 stems…") closes:

```cpp
        beginTest ("the info button opens the help sheet in place of the screen; the x or the info button closes it");
        {
            StemSplitterProcessor proc;
            StemSplitterEditor ed (proc);
            UiState s;                                               // the Drop screen
            ed.forceState (s);
            ed.tick();
            InfoButton* info = nullptr;
            DropScreen* dropScreen = nullptr;
            SplittingScreen* splittingScreen = nullptr;
            walk (ed, [&] (juce::Component& c)
            {
                if (auto* b = dynamic_cast<InfoButton*> (&c)) info = b;
                if (auto* d = dynamic_cast<DropScreen*> (&c)) dropScreen = d;
                if (auto* sp = dynamic_cast<SplittingScreen*> (&c)) splittingScreen = sp;
            });
            expect (info != nullptr && dropScreen != nullptr && splittingScreen != nullptr);
            auto& help = ed.helpSheet();
            expect (! help.isVisible() && dropScreen->isVisible() && ! info->getToggleState());

            info->onClick();
            expect (help.isVisible() && ! dropScreen->isVisible() && info->getToggleState());
            ed.tick();                                               // the timer doesn't bring the screen back
            expect (help.isVisible() && ! dropScreen->isVisible());

            s.screen = Screen::Splitting;                            // a screen change keeps the sheet open
            ed.forceState (s);
            ed.tick();
            expect (help.isVisible() && ! splittingScreen->isVisible());

            help.setTab (2);
            LisnButton* close = nullptr;
            walk (help, [&] (juce::Component& c) { if (auto* b = dynamic_cast<LisnButton*> (&c)) close = b; });
            expect (close != nullptr);
            close->onClick();
            expect (! help.isVisible() && splittingScreen->isVisible() && ! info->getToggleState());

            info->onClick();
            expect (help.isVisible() && help.getTab() == 2);        // closing keeps the tab
            info->onClick();
            expect (! help.isVisible() && splittingScreen->isVisible());
        }
```

- [ ] **Step 2: Build to see it fail.** Run `cmake --build build --config Debug`. Expected: a compile error, because `InfoButton` and `helpSheet` don't exist yet.

- [ ] **Step 3: Add the close icon.** In `Source/Icons.h`, inside `namespace Icons`, after the `speakerOff` line, add:

```cpp
    inline constexpr const char* close  = "M6 6l12 12M18 6L6 18";                                       // Help.mockup.html, stroked
```

- [ ] **Step 4: Add `InfoButton`.** In `Source/Controls.h`, after the `CircleButton` class, add:

```cpp
// The header's info button (Help.mockup.html): a 38 px circle in the pill style with an "i". Its toggle state is lit
// (cream, ink "i") while the help sheet is open; the editor sets it.
class InfoButton : public juce::Button
{
public:
    InfoButton();
    void setTheme (const Theme&);
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    juce::Colour tint = themeFor ("dusk").pillTint;
};
```

In `Source/Controls.cpp`, after `CircleButton::paintButton`, add:

```cpp
InfoButton::InfoButton() : juce::Button ("Help")
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void InfoButton::setTheme (const Theme& t)
{
    tint = t.pillTint;
    repaint();
}

void InfoButton::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    const auto r = getLocalBounds().toFloat();
    const auto lit = getToggleState();
    g.setColour (lit ? Theme::cream : tint);
    g.fillEllipse (r);
    if (isMouseOver && ! lit)
    {
        g.setColour (Theme::cream.withAlpha (0.08f));   // hover: a faint wash, and the border goes up to 0.3
        g.fillEllipse (r);
    }
    g.setColour (Theme::cream.withAlpha (lit ? 1.0f : isMouseOver ? 0.3f : 0.14f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);

    // The mockup's "i": M12 11v6 at stroke 2.2 and a 2.8 dot at (12, 7.25), in a 24 px viewBox drawn at 18 px.
    const auto icon = r.withSizeKeepingCentre (18.0f, 18.0f);
    g.setColour (lit ? Theme::ink : Theme::cream);
    strokeIcon (g, "M12 11v6", icon, 2.2f);
    g.fillEllipse (juce::Rectangle<float> (2.1f, 2.1f).withCentre ({ icon.getX() + 9.0f, icon.getY() + 5.44f }));
}
```

- [ ] **Step 5: Put the button in the header.** In `Source/Screens.h`, replace the `Header` comment and class with:

```cpp
// Main.dc.html header, bounds (24, 16, 712, 44): "LISN StemSplitter", then [Dusk | Midnight], [4 stems | 6 stems] and the
// info button (Help.mockup.html).
class Header : public juce::Component
{
public:
    Header();
    void setTheme (const Theme&);                  // pill tints and the theme selection
    void setSixStems (bool);                       // selection only, no callback
    void setEnabledSwitches (bool);                // disabled pills are drawn at 0.55; the info button stays enabled
    void setInfoOn (bool);                         // the info button lit while the help sheet is open; no callback
    std::function<void (juce::String)> onTheme;    // "dusk" / "midnight"
    std::function<void (bool)> onSixStems;
    std::function<void()> onInfo;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GlyphArrangement title;                  // "LISN StemSplitter", shaped once: the header repaints with every motion frame
    SegmentedPill themePill, stemsPill;
    InfoButton info;
};
```

In `Source/Screens.cpp`, in `Header::Header()`, after `addAndMakeVisible (stemsPill);`, add:

```cpp
    info.onClick = [this] { if (onInfo != nullptr) onInfo(); };
    addAndMakeVisible (info);
```

In `Header::setTheme`, after `stemsPill.setTheme (t);`, add `info.setTheme (t);`. After `Header::setEnabledSwitches`, add:

```cpp
void Header::setInfoOn (bool on)
{
    info.setToggleState (on, juce::dontSendNotification);
}
```

Replace `Header::resized` with:

```cpp
void Header::resized()
{
    info.setBounds (getWidth() - 38, 3, 38, 38);
    const auto w = stemsPill.preferredWidth();
    stemsPill.setBounds (info.getX() - 8 - w, 3, w, 38);
    themePill.setBounds (stemsPill.getX() - 8 - themePill.preferredWidth(), 3, themePill.preferredWidth(), 38);
}
```

- [ ] **Step 6: Add the `HelpSheet` shell.** In `Source/Screens.h`, after the `ErrorScreen` class, add:

```cpp
// Help.mockup.html: "How to use" with [Play | Stems | Loop | Export] tabs of four tips each. The editor shows it in the
// panel in place of the current screen, from the header's info button.
class HelpSheet : public juce::Component
{
public:
    HelpSheet();
    void setTheme (const Theme&);                    // the tab pill's tint
    void setTab (int);                               // 0 Play, 1 Stems, 2 Loop, 3 Export; no callback
    int getTab() const { return tabs.getSelected(); }
    std::function<void()> onClose;                   // the x button

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SegmentedPill tabs;
    LisnButton close;
};
```

At the end of `Source/Screens.cpp`, add:

```cpp
//==============================================================================
HelpSheet::HelpSheet() : tabs ({ "Play", "Stems", "Loop", "Export" }), close ({}, LisnButton::Style::Ghost)
{
    tabs.onChange = [this] (int) { repaint(); };
    close.icon = Icons::close;
    close.padLeft = 8.0f;                            // the 16 px x centred in 34: border 1 + 8 + 16 + 8 + 1
    close.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (tabs);
    addAndMakeVisible (close);
    setBufferedToImage (true);                       // the waves move behind it every motion frame; the sheet only changes with the tab
}

void HelpSheet::setTheme (const Theme& t)
{
    tabs.setTheme (t);
}

void HelpSheet::setTab (int i)
{
    tabs.setSelected (i);
    repaint();
}

void HelpSheet::resized()
{
    close.setBounds (659, 18, 34, 34);
    tabs.setBounds (close.getX() - 12 - tabs.preferredWidth(), 16, tabs.preferredWidth(), 38);
}

void HelpSheet::paint (juce::Graphics& g)
{
    // "How to use" (Bold 15) over the hint (12, cream 0.64), placed like the stems screen's song name and time.
    const auto y = 17.0f + (36.0f - 34.4f) / 2.0f, w = (float) tabs.getX() - 12.0f - 19.0f;
    text (g, Fonts::body (15.0f, 700), Theme::cream, "How to use", { 19.0f, y, w, 18.0f }, juce::Justification::centredLeft);
    text (g, Fonts::body (12.0f), Theme::cream.withAlpha (0.64f), "Click the plugin first so its keys work.",
          { 19.0f, y + 20.0f, w, 14.4f }, juce::Justification::centredLeft);
    g.setColour (Theme::cream.withAlpha (0.12f));
    g.fillRect (19.0f, 63.0f, 674.0f, 1.0f);
}
```

- [ ] **Step 7: Wire it into the editor.** In `Source/PluginEditor.h`, after the `mouseDown` declaration, add:

```cpp
    void setHelpOpen (bool);                         // the help sheet in the panel in place of the screen, the info button lit
```

In the test hooks, after `StemsScreen& stemsScreen() { return stems; }`, add:

```cpp
    HelpSheet& helpSheet() { return help; }
```

In the private section, after `void show (const UiState&);`, add:

```cpp
    void showScreen();                               // the shown screen, or the help sheet in its place
```

After `ErrorScreen error;`, add:

```cpp
    HelpSheet help;
    bool helpOpen = false;
```

In `Source/PluginEditor.cpp`:
- In the constructor, change the `addChildComponent` loop's list to `{ &drop, &splitting, &stems, &error, &help }`. After `header.onSixStems = …;`, add:

```cpp
    header.onInfo = [this] { setHelpOpen (! helpOpen); };
    help.onClose = [this] { setHelpOpen (false); };
```

- In `resized()`, change the loop's list to `{ &drop, &splitting, &stems, &error, &help }` as well.
- In `show()`, replace the four lines `drop.setVisible (…);` … `error.setVisible (…);` with `showScreen();`. It goes right after `shown = s;`.
- In `applyTheme`, after `error.setTheme (t);`, add `help.setTheme (t);`.
- After `StemSplitterEditor::show`, add:

```cpp
void StemSplitterEditor::setHelpOpen (bool open)
{
    helpOpen = open;
    showScreen();
}

void StemSplitterEditor::showScreen()
{
    const auto is = [this] (Screen x) { return ! helpOpen && shown.has_value() && shown->screen == x; };
    drop.setVisible (is (Screen::Drop));
    splitting.setVisible (is (Screen::Splitting));
    stems.setVisible (is (Screen::Stems));
    error.setVisible (is (Screen::Error));
    help.setVisible (helpOpen);
    header.setInfoOn (helpOpen);
}
```

- [ ] **Step 8: Build and run the tests.** Run `cmake --build build --config Debug`, then `build/lisn_tests_artefacts/Debug/lisn_tests.exe`. Expected: exit 0. The new test passes, and so does "no focus, no banned controls", which now also walks the info button and the sheet's pill and × button. Then run `ctest --test-dir build -C Debug --output-on-failure`. Expected: 100 % passed.

- [ ] **Step 9: Commit.**

```bash
git add Source/Icons.h Source/Controls.h Source/Controls.cpp Source/Screens.h Source/Screens.cpp Source/PluginEditor.h Source/PluginEditor.cpp tests/UiStateTests.cpp
git commit -m "feat: an info button in the header opens a help sheet in place of the screen"
```

End the message with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

---

### Task 2: The tips

**Files:**
- Modify: `Source/Icons.h`, `Source/Screens.cpp`
- Test: `tests/Snapshots.cpp`

**Interfaces:**
- Consumes (from Task 1):
  - `HelpSheet::paint` already draws the title row and the divider;
  - `HelpSheet::getTab()`;
  - `StemSplitterEditor::setHelpOpen (bool)`, `StemSplitterEditor::helpSheet()`.
- Consumes (existing, in `Screens.cpp`): the file-local `text (g, font, colour, string, rect, justification = centred)` helper, `drawDashedRoundedRect` (Controls.h), and `strokeIcon` (Icons.h).

- [ ] **Step 1: Add the snapshots.** In `tests/Snapshots.cpp`, in `runSnapshots`, after the `moving` lambda, add:

```cpp
    const auto helpTab = [] (int tab)
    {
        return Setup ([tab] (StemSplitterProcessor&, StemSplitterEditor& ed) { ed.setHelpOpen (true); ed.helpSheet().setTab (tab); });
    };
```

At the end of the `shots` list, after the `"stems4-dusk@2x"` entry, add:

```cpp
        { "help-play-dusk", "dusk", state (Screen::Stems, false), 1.0f, helpTab (0) },
        { "help-stems-midnight", "midnight", state (Screen::Drop, false), 1.0f, helpTab (1) },
        { "help-loop-dusk", "dusk", state (Screen::Drop, false), 1.0f, helpTab (2) },
        { "help-export-dusk@2x", "dusk", state (Screen::Drop, false), 2.0f, helpTab (3) },
```

- [ ] **Step 2: Render them before the tips exist.** Build Debug, then run `build/lisn_tests_artefacts/Debug/lisn_tests.exe --snapshots snapshots`. Expected: exit 0, and `snapshots/help-*.png` show the title row and the divider over an empty panel.

- [ ] **Step 3: Add the icons.** In `Source/Icons.h`, after the `close` line, add:

```cpp
    inline constexpr const char* jump   = "M4 12h11M11 7l5 5-5 5M20 5v14";                               // Help.mockup.html, stroked
    inline constexpr const char* home   = "M6 5v14M19 5.5v13L9 12z";
    inline constexpr const char* zoom   = "M11 4a7 7 0 1 0 0 14a7 7 0 1 0 0-14M20 20l-4-4M11 8v6M8 11h6";
    inline constexpr const char* trim   = "M7 4v16M17 4v16M4 8h3M17 16h3";
    inline constexpr const char* toggle = "M8 7h8a5 5 0 0 1 0 10H8A5 5 0 0 1 8 7zM16 10a2 2 0 1 0 0 4a2 2 0 1 0 0-4";
    inline constexpr const char* grid   = "M4 4v16M9 4v16M14 4v16M19 4v16";
    inline constexpr const char* layers = "M12 3l9 5-9 5-9-5zM3 13l9 5 9-5";
    inline constexpr const char* mouse  = "M12 3a6 6 0 0 0-6 6v6a6 6 0 0 0 12 0V9a6 6 0 0 0-6-6zM12 7v3";
```

- [ ] **Step 4: Add the tips table and `paintTip`.** In `Source/Screens.cpp`, right before the `HelpSheet::HelpSheet()` constructor (after its `//====` line), add:

```cpp
namespace
{
    // Help.mockup.html's tips, [tab][tip]. A way to do a tip is a key (a solid keycap), a mouse action or a button (a dashed
    // cap with a mouse), or a joining word.
    enum class Part { key, mouse, word };
    struct How { Part part; const char* text; };
    struct Tip { const char* icon; const char* title; const char* line; std::vector<How> how; };

    const How orWord { Part::word, "or" }, plusWord { Part::word, "+" };
    How key (const char* t)   { return { Part::key, t }; }
    How mouse (const char* t) { return { Part::mouse, t }; }
    const char* const oneToSix = "1\xe2\x80\x93" "6";   // "1–6"

    const std::vector<std::vector<Tip>> helpTips {
        { { Icons::play, "Play or pause", "Every stem plays together, in sync", { key ("Space") } },
          { Icons::jump, "Jump and scrub", "Click the waves to jump there, drag to scrub", { mouse ("Click"), mouse ("Drag") } },
          { Icons::home, "Back to the start", "Back to 0:00, or to the loop's start while looping", { key ("Home") } },
          { Icons::zoom, "Zoom and scroll", "Zoom around the mouse; the wheel alone scrolls", { key ("Ctrl"), plusWord, mouse ("Wheel") } } },
        { { Icons::file, "Split a song", "Drop it on the panel or click Browse. 6 stems adds guitar and piano", { mouse ("Drop") } },
          { Icons::speakerOff, "Mute a stem", "Click its light; stems 1 to 6 go top to bottom", { mouse ("Light"), orWord, key (oneToSix) } },
          { Icons::speaker, "Solo a stem", "Only that stem plays; do it again to hear them all",
            { mouse ("Right-click"), orWord, key ("Shift"), plusWord, key (oneToSix) } },
          { Icons::plus, "New song", "Start over with another song", { mouse ("New song") } } },
        { { Icons::loop, "Make a loop", "Ctrl+drag across the waves; the edges snap to the beat", { key ("Ctrl"), plusWord, mouse ("Drag") } },
          { Icons::trim, "Trim it", "Drag either handle; hold Alt to go off the grid",
            { mouse ("Handle"), orWord, key ("Alt"), plusWord, mouse ("Drag") } },
          { Icons::toggle, "Loop on or off", "With no loop yet, you get 4 bars at the playhead", { key ("L"), orWord, mouse ("Loop") } },
          { Icons::grid, "Finer steps", "Zoomed in, the grid is quarter beats; zoomed out, bars", { key ("Ctrl"), plusWord, mouse ("Wheel") } } },
        { { Icons::grip, "Drag a stem", "Drag its row onto any track in FL", { mouse ("Drag row") } },
          { Icons::layers, "Drag the mix", "Every stem you hear, as one WAV", { mouse ("Drag mix") } },
          { Icons::loop, "Just the loop", "While looping, every drag gives only the loop", { key ("L"), plusWord, mouse ("Drag") } },
          { Icons::speakerOff, "Leave stems out", "Muted stems stay out of the mix", { key (oneToSix), plusWord, mouse ("Drag mix") } } } };

    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    juce::Font howFont (const How& h) { return Fonts::body (12.0f, h.part == Part::word ? 400 : 700); }

    // A cap's width: a key is 1 + 9 + text + 9 + 1; a mouse cap is 1 + 7 + icon 13 + 5 + text + 9 + 1; a word is its text.
    float howWidth (const How& h)
    {
        const auto w = juce::GlyphArrangement::getStringWidth (howFont (h), utf8 (h.text));
        return w + (h.part == Part::key ? 20.0f : h.part == Part::mouse ? 36.0f : 0.0f);
    }

    // Help.mockup.html .tip:
    // - card: cream 0.05, a 1 px cream 0.08 border, radius 14;
    // - icon tile: 40 px (cream 0.1, radius 12), 15 px in, holding a 20 px icon at stroke 1.9;
    // - text: the title (Bold 14) over the line (12.5, cream 0.66), 14 px after the tile;
    // - the how-to caps: 26 tall, 6 px apart, ending 17 px before the right edge.
    void paintTip (juce::Graphics& g, const Tip& tip, juce::Rectangle<float> r)
    {
        g.setColour (Theme::cream.withAlpha (0.05f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (Theme::cream.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 13.5f, 1.0f);

        const juce::Rectangle<float> tile (r.getX() + 15.0f, r.getCentreY() - 20.0f, 40.0f, 40.0f);
        g.setColour (Theme::cream.withAlpha (0.1f));
        g.fillRoundedRectangle (tile, 12.0f);
        g.setColour (Theme::cream);
        strokeIcon (g, tip.icon, tile.withSizeKeepingCentre (20.0f, 20.0f), 1.9f);

        auto x = r.getRight() - 17.0f - 6.0f * (float) (tip.how.size() - 1);
        for (const auto& h : tip.how)
            x -= howWidth (h);
        const auto textX = tile.getRight() + 14.0f, textW = x - 14.0f - textX;
        text (g, Fonts::body (14.0f, 700), Theme::cream, utf8 (tip.title), { textX, r.getCentreY() - 18.5f, textW, 18.0f },
              juce::Justification::centredLeft);
        text (g, Fonts::body (12.5f), Theme::cream.withAlpha (0.66f), utf8 (tip.line), { textX, r.getCentreY() + 2.5f, textW, 16.0f },
              juce::Justification::centredLeft);

        for (const auto& h : tip.how)
        {
            const juce::Rectangle<float> cap (x, r.getCentreY() - 13.0f, howWidth (h), 26.0f);
            x = cap.getRight() + 6.0f;
            if (h.part == Part::word)
            {
                text (g, howFont (h), Theme::cream.withAlpha (0.5f), utf8 (h.text), cap);
                continue;
            }
            if (h.part == Part::mouse)
            {
                drawDashedRoundedRect (g, cap.reduced (0.5f), 6.5f, 1.0f, 3.0f, 3.0f, Theme::cream.withAlpha (0.36f));
                g.setColour (Theme::cream.withAlpha (0.86f));
                strokeIcon (g, Icons::mouse, { cap.getX() + 8.0f, cap.getCentreY() - 6.5f, 13.0f, 13.0f }, 2.0f);
                text (g, howFont (h), Theme::cream.withAlpha (0.86f), utf8 (h.text), cap.withTrimmedLeft (26.0f),
                      juce::Justification::centredLeft);
                continue;
            }
            {
                // A keycap's 2 px bottom edge, drawn outside the cap only (a CSS box-shadow), so the translucent fill stays even.
                juce::Graphics::ScopedSaveState keep (g);
                juce::Path outside;
                outside.addRectangle (cap.expanded (0.0f, 3.0f));
                outside.addRoundedRectangle (cap, 7.0f);
                outside.setUsingNonZeroWinding (false);
                g.reduceClipRegion (outside);
                g.setColour (Theme::cream.withAlpha (0.12f));
                g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 7.0f);
            }
            g.setColour (Theme::cream.withAlpha (0.1f));
            g.fillRoundedRectangle (cap, 7.0f);
            g.setColour (Theme::cream.withAlpha (0.4f));
            g.drawRoundedRectangle (cap.reduced (0.5f), 6.5f, 1.0f);
            text (g, howFont (h), Theme::cream, utf8 (h.text), cap);
        }
    }
}
```

- [ ] **Step 5: Paint the tab's tips.** At the end of `HelpSheet::paint`, after the divider's `fillRect`, add:

```cpp
    // The tab's four tips share y 74-383, 8 px apart.
    const auto tipH = (383.0f - 74.0f - 3.0f * 8.0f) / 4.0f;
    const auto& tips = helpTips[(size_t) getTab()];
    for (size_t i = 0; i < tips.size(); ++i)
        paintTip (g, tips[i], { 19.0f, 74.0f + (float) i * (tipH + 8.0f), 674.0f, tipH });
```

- [ ] **Step 6: Build, test, and render.**
  1. Run `cmake --build build --config Debug`, `build/lisn_tests_artefacts/Debug/lisn_tests.exe` (exit 0) and `ctest --test-dir build -C Debug --output-on-failure` (100 % passed).
  2. Run `build/lisn_tests_artefacts/Debug/lisn_tests.exe --snapshots snapshots`.
  3. Open `snapshots/help-play-dusk.png`, `help-stems-midnight.png`, `help-loop-dusk.png` and `help-export-dusk@2x.png` with the Read tool. Compare each with the matching tab in `docs/design/Help.mockup.html` (B–E):
     - four full-height cards;
     - the icon tiles;
     - titles and lines that aren't clipped;
     - keycaps with a bottom edge, and dashed mouse caps with the mouse glyph;
     - no overlap between the text and the caps;
     - the header's info button lit.

     Report what differs.

- [ ] **Step 7: Commit.**

```bash
git add Source/Icons.h Source/Screens.cpp tests/Snapshots.cpp
git commit -m "feat: the help sheet's four tabs of tips, with keycaps and mouse caps"
```

End the message with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
