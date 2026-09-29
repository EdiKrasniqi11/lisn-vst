#pragma once
#include "StemRow.h"
#include "SeparationJob.h"
#include "Peaks.h"

// The screens of docs/design. Each screen takes the panel bounds (24, 76, 712, 400) and lays out in panel coordinates;
// it paints only its own content over the WaveBackground panel. Nothing here takes keyboard focus.

// Main.dc.html header, bounds (24, 16, 712, 44): "LISN StemSplitter", then [Dusk | Midnight] and [4 stems | 6 stems].
class Header : public juce::Component
{
public:
    Header();
    void setTheme (const Theme&);                  // pill tints and the theme selection
    void setSixStems (bool);                       // selection only, no callback
    void setEnabledSwitches (bool);                // disabled pills are drawn at 0.55
    std::function<void (juce::String)> onTheme;    // "dusk" / "midnight"
    std::function<void (bool)> onSixStems;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GlyphArrangement title;                  // "LISN StemSplitter", shaped once: the header repaints with every motion frame
    SegmentedPill themePill, stemsPill;
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

// StemPlayer.mockup.html panel: song row (Play/Pause, name, time), one StemRow per stem file, footer hint.
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

    int numRows() const { return rows.size(); }
    juce::File fileOf (int row) const;
    bool hasPeaks() const { return peaksReady; }
    StemRow* row (int i) { return rows[i]; }        // test hook; nullptr when out of range
    juce::Rectangle<int> playheadArea (double fraction) const;   // the strip a playhead at `fraction` covers (bench hook)
    int getRepaintCount() const { return repaintCount; }         // setPlayback calls that repainted (test hook)
    void setLoop (juce::Range<double> range, bool on);   // from the player every tick; repaints only on change
    void setTempo (Tempo);                           // the peak job sets it; tests may too
    double snap (double fraction) const;             // to the nearest beat; unchanged without a tempo
    juce::String chipText() const { return dragChip.getText(); }   // test hook
    bool loopButtonEnabled() const { return loopButton.isEnabled(); }   // test hook
    std::function<void (double, double)> onSetLoop;  // a finished waveform drag, snapped
    std::function<void()> onToggleLoop;
    std::function<juce::File()> mixFile;             // the Drag mix / Drag loop file
    std::function<juce::File (int)> stemFile;        // what row i drags

    std::function<void()> onNewSong, onPlayPause;
    std::function<void (int)> onToggleMute, onSolo;
    std::function<void (double)> onSeek;             // fraction 0..0.999

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // the shared playhead across every row
    void resized() override;

private:
    int headX (double fraction) const;               // screen x of the playhead
    juce::Range<int> waveSpan() const;               // top..bottom of the waveforms, row 0 to the last row
    juce::Rectangle<int> waveSpanArea() const;
    juce::Rectangle<int> loopArea (juce::Range<double>) const;   // the band's pixels, padded by the edge width

    Theme theme = themeFor ("dusk");
    juce::File dir;
    juce::String song, timeText { "0:00 / 0:00" };
    double length = 0.0;             // the peak job's length until the player reports one
    bool playing = false, peaksReady = false;
    int lastHead = -1, generation = 0, repaintCount = 0;
    juce::Range<double> loopRange, dragBand;         // the player's loop / the band shown while dragging (empty = none)
    bool loopOn = false;
    Tempo tempo;
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
