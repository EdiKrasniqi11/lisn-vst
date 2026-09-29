#pragma once
#include "StemRow.h"
#include "SeparationJob.h"
#include "Peaks.h"

// The screens of docs/design. Each screen takes the panel bounds (24, 76, 712, 400) and lays out in panel coordinates;
// it paints only its own content over the WaveBackground panel. Nothing here takes keyboard focus.

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
    int hotEdge() const { return hot; }              // 0 = loop start, 1 = loop end, -1 = none (test hook)
    juce::String chipText() const { return dragChip.getText(); }   // test hook
    std::function<void (double, double)> onSetLoop;  // a finished loop drag, a handle trim or defaultLoop (snapped unless Alt)
    std::function<void()> onToggleLoop;
    std::function<juce::File()> mixFile;             // the Drag mix / Drag loop file
    std::function<juce::File (int)> stemFile;        // what row i drags

    std::function<void()> onNewSong, onPlayPause;
    std::function<void (int)> onToggleMute, onSolo;
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
