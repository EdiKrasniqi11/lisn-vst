#pragma once
#include "StemRow.h"
#include "SeparationJob.h"
#include "Peaks.h"

// The screens of docs/design. Each screen takes the panel bounds (24, 76, 712, 400) and lays out in panel coordinates;
// it paints only its own content over the WaveBackground panel. Nothing here takes keyboard focus.

// Main.dc.html header, bounds (24, 16, 712, 44): the logo kit's glass symbol, "LISN StemSplitter" (HeaderLogoGlass.dc.html,
// option A), then [Dusk | Midnight] and [4 stems | 6 stems].
// Board 1A's round Minimise / Close buttons: 32 px, cream@0.18 border, rgba(32,18,27,.55) fill.
class WindowButton : public juce::Button
{
public:
    explicit WindowButton (bool closeIcon);
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    bool close;
};

class Header : public juce::Component
{
public:
    Header();
    void setTheme (const Theme&);                  // pill tints and the theme selection
    void setSixStems (bool);                       // selection only, no callback
    void setEnabledSwitches (bool);                // disabled pills are drawn at 0.55
    std::function<void (juce::String)> onTheme;    // "dusk" / "midnight"
    std::function<void (bool)> onSixStems;
    // The desktop app (board 1A): the header is the title bar. It reads "LISN" after the symbol, gets Minimise / Close after
    // the pills, and a drag anywhere else (the symbol and text too) moves the window.
    void makeTitleBar (std::function<void()> minimise, std::function<void()> close);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    // The header repaints with every motion frame, so the symbol is parsed once and drawn from an image made at the screen's
    // pixel scale (remade when the scale or the renderer changes), and the text is shaped once.
    std::unique_ptr<juce::Drawable> symbol;        // lisn-symbol-glass.svg from BinaryData
    juce::Image symbolImage;
    float symbolScale = 0.0f;
    juce::GlyphArrangement title;                  // "LISN StemSplitter"; "LISN" in the title bar
    SegmentedPill themePill, stemsPill;
    WindowButton minimiseButton { false }, closeButton { true };
    bool titleBar = false;
    juce::ComponentDragger dragger;
};

// Empty.dc.html: dashed drop zone, Browse, formats and the stems you'll get.
class DropScreen : public juce::Component
{
public:
    DropScreen();
    void setTheme (const Theme&);
    void setSixStems (bool);                       // the stem chips
    void setHighlighted (bool);                    // a file is dragged over the window
    std::function<void()> onBrowse;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    float columnTop() const;

    Theme theme = themeFor ("dusk");
    bool six = false, highlighted = false;
    LisnButton browse;
};

// Separating.dc.html: song, percentage, wavy progress line, Cancel and the first-run note.
class SplittingScreen : public juce::Component
{
public:
    SplittingScreen();
    void setTheme (const Theme&);
    void setSong (const juce::String& name, bool sixStems);
    void setProgress (double);                     // repaints only when the whole percentage changes
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    float columnTop() const;

    Theme theme = themeFor ("dusk");
    juce::String song, subtitle;
    int percent = 0, noteLines = 1;
    juce::Path track;                              // the 440 x 40 wavy line
    LisnButton cancel;
};

juce::String beatsText (double beats);   // "8 beats", "10.25 beats", "1 beat": rounded to a quarter beat

// The row at the bottom of the stems panel (board 5B): a Tempo box ("106 BPM" over "1.15x of 92", or "1.15x" over "Speed"
// with no tempo) and a Key box ("+0 st" over "Key"), each with up / down chevrons. A vertical drag on a box changes it (up is
// more), a click on a chevron steps it, a double-click or the right-click menu resets it. Tempo steps 1 BPM (0.05x with no
// tempo), 0.5x..2x; Key steps 1 semitone, -12..12. The mix chip sits at the row's right end (StemsScreen lays it out). Never
// takes focus.
class SpeedStrip : public juce::Component
{
public:
    SpeedStrip();
    ~SpeedStrip() override;
    void setTheme (const Theme&);                // the reset menu's fill
    void setSpeed (double);                      // no callback; repaints only on change
    double getSpeed() const { return speed; }
    void setPitch (int);                         // no callback; repaints only on change
    int getPitch() const { return pitch; }
    void setBpm (double songBpm);                // the song's own tempo; 0 = unknown (the box shows the speed instead)
    // The speed for a tempo box value `steps` steps away from `from`: whole BPM when bpm > 0, else 0.05x steps; 0.5..2.
    static double stepSpeed (double from, int steps, double bpm);
    std::function<void (double)> onChange;
    std::function<void (int)> onPitch;
    juce::Rectangle<int> tempoBox() const { return { 0, 1, 140, 38 }; }
    juce::Rectangle<int> keyBox() const   { return { 150, 1, 112, 38 }; }
    enum class Target { none, tempo, key };
    // A right-click on a box opens this at the pointer: one item, "Reset to 92 BPM" ("Reset to 1.00x" with no tempo) or
    // "Reset to +0 st", that does what a double-click does; greyed out when the box is already there. Public for tests.
    juce::PopupMenu resetMenu (Target);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void set (double, bool report);
    void setKey (int, bool report);
    void paintBox (juce::Graphics&, juce::Rectangle<int>, const char* icon, const juce::String& value, const juce::String& caption);
    Target targetAt (juce::Point<int>) const;
    double speed = 1.0, bpm = 0.0, dragFromSpeed = 1.0;
    int pitch = 0, dragFromPitch = 0;
    Target dragging = Target::none;
    bool moved = false;
    std::unique_ptr<juce::LookAndFeel> menuLook;     // the reset menu's look (Screens.cpp)
};

// StemPlayer.mockup.html panel: song row (Play/Pause, name, time), one StemRow per stem file, the loop band with its handles
// over them, and the position strip while zoomed.
class StemsScreen : public juce::Component
{
public:
    StemsScreen();
    ~StemsScreen() override;
    void setTheme (const Theme&);
    // Rebuilds the rows (vocals, drums, bass, guitar, piano, other, then unknown stems A-Z; compact with more than 4)
    // and reads their peaks and the song length on a background thread.
    void setStems (const juce::File& dir, const juce::String& songName);
    juce::File getDir() const { return dir; }
    juce::Array<juce::File> getFiles() const;       // row order = the player's stem order
    void setPlayback (bool playing, double fraction, double lengthSeconds);   // repaints only what changed
    void setVolume (int row, float v);               // the row's volume bar; no callback, repaints only on change
    void setSpeed (double v);                    // the speed strip and the subtitle's BPM; no callback
    void setPitch (int st) { speedStrip.setPitch (st); }
    std::function<void (int)> onPitch;
    SpeedStrip& speedStripControl() { return speedStrip; }   // test hook
    void setAudible (juce::uint32 mask);            // bit i set = row i audible
    // The shared view: the song fractions every row shows (the whole song by default; new stems reset it).
    void setView (juce::Range<double>);             // kept inside 0..1
    juce::Range<double> getView() const { return view; }
    void zoom (double atFraction, double factor);    // factor < 1 zooms in; `atFraction` stays under the mouse; 2 s at most
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;   // Ctrl zooms, plain scrolls

    int numRows() const { return rows.size(); }
    juce::File fileOf (int row) const;
    bool hasPeaks() const { return peaksReady; }
    StemRow* row (int i) { return rows[i]; }        // test hook; nullptr when out of range
    juce::Rectangle<int> playheadArea (double fraction) const;   // the strip a playhead at `fraction` covers (bench hook)
    int getRepaintCount() const { return repaintCount; }         // setPlayback calls that repainted (test hook)
    void setLoop (juce::Range<double> range, bool on);   // from the player every tick; repaints only on change
    // The Loop button and the L key: with a loop range, onToggleLoop; with none, onSetLoop (defaultLoop()), which turns
    // looping on.
    void toggleLoop();
    juce::Range<double> defaultLoop() const;         // 4 bars from the bar under the playhead (8 s without a tempo)
    void setTempo (Tempo);                           // the peak job sets it; tests may too
    // The grid: the finest of 1/4 beat, 1 beat and 1 bar (4 beats) that is at least 4 px wide at the current zoom, in
    // seconds; 0 without a tempo.
    double gridStep() const;
    double snap (double fraction, bool free = false) const;   // to the nearest grid line; unchanged when free (Alt) or no grid
    // A waveform's mouse (song fractions). Within 6 px of a loop edge, while looping, it hovers and drags that edge (a
    // handle); elsewhere a click seeks, a drag scrubs, and a Ctrl+drag of 4 px or more makes a new loop. Loops go to
    // onSetLoop on release.
    void waveMouse (WaveMouse, double fraction, bool alt, bool ctrl = false);
    int waveLeft() const;                            // the waveforms' left edge in this screen's coordinates
    juce::Rectangle<int> stripArea() const;
    int hotEdge() const { return hot; }              // 0 = loop start, 1 = loop end, -1 = none (test hook)
    juce::String chipText() const { return dragChip.getText(); }   // test hook
    // The Drag mix chip's text ("Drag mix", "Preparing 40%" ...) and progress line; its width follows the text.
    void setMixChip (const juce::String& text, std::optional<float> progress);
    std::function<void (double, double)> onSetLoop;  // a finished loop drag, a handle trim or defaultLoop (snapped unless Alt)
    std::function<void()> onToggleLoop;
    std::function<juce::File()> mixFile;             // the Drag mix / Drag loop file
    std::function<juce::File (int)> stemFile;        // what row i drags

    std::function<void()> onNewSong, onPlayPause;
    std::function<void (int)> onToggleMute, onSolo;
    std::function<void (double)> onSpeed;            // the speed strip moved (0.5..1.5)
    std::function<void (int, float)> onVolume;       // a row's volume bar moved
    std::function<void (double)> onSeek;             // fraction 0..0.999

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // over every row: the grid, the loop band and its tabs, the playhead
    void resized() override;
    // The grab tabs reach above the waveforms, where the rows pass the mouse on (StemRow::hitTest): same gesture as waveMouse.
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int headX (double fraction) const;               // panel x of a song fraction (through the view)
    double fractionAt (float x) const;               // the song fraction at panel x (through the view)
    juce::Range<int> waveSpan() const;               // top..bottom of the waveforms, row 0 to the last row
    juce::Rectangle<int> waveSpanArea() const;
    juce::Rectangle<int> loopArea (juce::Range<double>) const;   // the band's pixels, padded for the tabs (9 px sideways, 13 up)
    int edgeNear (int x) const;                      // the loop edge within 6 px of panel x: 0 / 1, or -1
    juce::Range<double> trimmed (double fraction, bool free) const;   // the loop with the grabbed edge moved there
    void setHot (int edge);                          // the hovered handle: its tab and the resize cursor
    void tabMouse (WaveMouse, const juce::MouseEvent&);   // the screen's own mouse over the tabs, as waveMouse

    Theme theme = themeFor ("dusk");
    juce::File dir;
    juce::String song, timeText { "0:00 / 0:00" };
    double length = 0.0;             // the peak job's length until the player reports one
    bool playing = false, peaksReady = false;
    bool hasHead = false;            // setPlayback has run since setStems: lastHead is real (it can be off the panel when zoomed)
    int lastHead = 0, generation = 0, repaintCount = 0;
    juce::Range<double> loopRange, dragBand;         // the player's loop / the band shown while dragging (empty = none)
    bool loopOn = false;
    Tempo tempo;
    juce::Range<double> view { 0.0, 1.0 };
    double position = 0.0;                           // the playhead, as a song fraction
    int hot = -1, grab = -1, downX = 0;              // the hovered / grabbed loop edge (0 start, 1 end, -1 none), the press x
    double downFraction = 0.0;
    bool dragging = false;                           // a drag of 4 px or more: a trim (grab >= 0), a new loop or a scrub
    bool newLoop = false;                            // the press had Ctrl held away from the handles: its drag makes a loop
    bool tabDown = false;                            // the press went to the screen itself, over the waves: it owns the drag
    juce::OwnedArray<StemRow> rows;
    CircleButton playButton;
    LisnButton loopButton { "Loop", LisnButton::Style::Ghost };
    DragChip dragChip;
    SpeedStrip speedStrip;
    LisnButton newSong;
    juce::ThreadPool pool { 1 };                     // last member: its jobs stop before the rows go
};

// PythonMissing.dc.html in three variants: Python missing, Demucs missing, split failed.
class ErrorScreen : public juce::Component
{
public:
    ErrorScreen();
    void setTheme (const Theme&);
    void setError (ErrorKind, const juce::String& text, const juce::String& pythonExe);
    std::function<void()> onRetry, onFindPython;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Theme theme = themeFor ("dusk");
    ErrorKind kind = ErrorKind::PythonMissing;
    juce::String title, body, command, copyText;   // command: the one-line code (Python / Demucs missing)
    juce::StringArray lines;                       // Failed: the last lines of demucs' output
    float top = 0.0f, bodyHeight = 0.0f;
    juce::Rectangle<float> box;
    LisnButton copy, retry, findPython;
};

// Help.mockup.html: "How to use" with [Play | Stems | Loop | Export] tabs of four tips each. The editor shows it in the
// panel in place of the current screen, from the info button in the window's bottom-right corner.
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
