#pragma once
#include "StemRow.h"
#include "SeparationJob.h"

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

// Main.dc.html panel: song row, one StemRow per stem file, footer hint.
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
    void setPlayback (int activeRow, bool playing, double activeFraction, double activeLength);   // activeRow -1: none

    int numRows() const { return rows.size(); }
    int rowOf (const juce::File&) const;           // -1 if no row has that file
    juce::File fileOf (int row) const;
    double positionOf (int row) const;             // each row remembers its own position
    bool hasPeaks() const { return peaksReady; }
    StemRow* row (int i) { return rows[i]; }       // test hook; nullptr when out of range

    std::function<void()> onNewSong;
    std::function<void (int)> onPlayPause;
    std::function<void (int, double)> onSeek;      // row, fraction; the row already shows it

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void seek (int row, double fraction);
    void updateRows();
    double lengthOf (int row) const;

    Theme theme = themeFor ("dusk");
    juce::File dir;
    juce::String song;
    double length = 0.0, activeLength = 0.0;       // first stem (from the peak job) / the preview's
    juce::OwnedArray<StemRow> rows;
    std::vector<double> positions;
    int activeRow = -1, generation = 0;
    bool activePlaying = false, peaksReady = false;
    LisnButton newSong;
    juce::ThreadPool pool { 1 };                   // last member: its jobs stop before the rows go
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
