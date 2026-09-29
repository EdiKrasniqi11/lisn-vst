#include "Screens.h"
#include "Icons.h"
#include "Peaks.h"

namespace
{
    // Non-ASCII UI text is escaped UTF-8: String (const char*) would read the bytes as ASCII.
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    const char* const note = "The first split downloads the AI model (about 80 MB), so it can sit at 0% for a minute. "
                             "After that it starts right away.";

    constexpr float panelW = 712.0f, centreX = 356.0f;

    const juce::StringArray allStems { "vocals", "drums", "bass", "guitar", "piano", "other" };
    juce::StringArray stemKeys (bool six) { return six ? allStems : juce::StringArray { "vocals", "drums", "bass", "other" }; }

    int stemRank (const juce::File& f)                 // unknown stems sort last
    {
        const auto i = allStems.indexOf (f.getFileNameWithoutExtension().toLowerCase());
        return i < 0 ? allStems.size() : i;
    }

    juce::Font tracked (const juce::Font& f, float em)   // CSS letter-spacing in em; JUCE scales kerning by the height, not the em
    {
        return f.withExtraKerningFactor (em * f.getHeightInPoints() / f.getHeight());
    }

    // One line of text, vertically centred in r like a CSS line box.
    void text (juce::Graphics& g, const juce::Font& f, juce::Colour c, const juce::String& s, juce::Rectangle<float> r,
               juce::Justification j = juce::Justification::centred)
    {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, r, j, true);
    }

    int lineCount (const juce::Font& f, const juce::String& s, float width)
    {
        juce::GlyphArrangement ga;
        ga.addJustifiedText (f, s, 0.0f, 0.0f, width, juce::Justification::left);
        int lines = 0;
        auto last = std::numeric_limits<float>::lowest();
        for (int i = 0; i < ga.getNumGlyphs(); ++i)
            if (ga.getGlyph (i).getBaselineY() > last)
            {
                ++lines;
                last = ga.getGlyph (i).getBaselineY();
            }
        return juce::jmax (1, lines);
    }

    // Text wrapped to area's width with a CSS line-height of lineHeightPx.
    void paragraph (juce::Graphics& g, const juce::Font& f, juce::Colour c, const juce::String& s, juce::Rectangle<float> area,
                    float lineHeightPx, juce::Justification j)
    {
        const auto leading = lineHeightPx - f.getHeight();
        g.setFont (f);
        g.setColour (c);
        g.drawMultiLineText (s, juce::roundToInt (area.getX()), juce::roundToInt (area.getY() + leading / 2.0f + f.getAscent()),
                             juce::roundToInt (area.getWidth()), j, leading);
    }

    juce::String pipFor (const juce::String& exe) { return "\"" + exe + "\" -m pip install demucs"; }

    // pipFor (exe) with the middle folders of exe replaced by "..." until it fits; the drive and the last two parts stay.
    juce::String shortCommand (const juce::String& exe, float maxWidth)
    {
        const auto font = Fonts::mono (14.0f);
        const auto parts = juce::StringArray::fromTokens (exe, "\\", "");
        auto command = pipFor (exe);
        for (int cut = 1; juce::GlyphArrangement::getStringWidth (font, command) > maxWidth && parts.size() - cut >= 3; ++cut)
        {
            auto p = parts;
            p.removeRange (1, cut);
            p.insert (1, ellipsis);
            command = pipFor (p.joinIntoString ("\\"));
        }
        return command;
    }
}

//==============================================================================
Header::Header() : themePill ({ "Dusk", "Midnight" }), stemsPill ({ "4 stems", "6 stems" })
{
    const auto lisn = tracked (Fonts::display (22.0f), 0.08f);
    title.addLineOfText (lisn, "LISN", 0.0f, 30.25f);   // shared baseline
    title.addLineOfText (Fonts::body (14.0f, 500), "StemSplitter", juce::GlyphArrangement::getStringWidth (lisn, "LISN") + 10.0f, 30.25f);

    themePill.onChange = [this] (int i) { if (onTheme != nullptr) onTheme (i == 1 ? "midnight" : "dusk"); };
    stemsPill.onChange = [this] (int i) { if (onSixStems != nullptr) onSixStems (i == 1); };
    addAndMakeVisible (themePill);
    addAndMakeVisible (stemsPill);
}

void Header::setTheme (const Theme& t)
{
    themePill.setTheme (t);
    stemsPill.setTheme (t);
    themePill.setSelected (t.id == "midnight" ? 1 : 0);
}

void Header::setSixStems (bool six)
{
    stemsPill.setSelected (six ? 1 : 0);
}

void Header::setEnabledSwitches (bool on)
{
    for (auto* p : { &themePill, &stemsPill })
    {
        p->setEnabled (on);
        p->setAlpha (on ? 1.0f : 0.55f);
    }
}

void Header::paint (juce::Graphics& g)
{
    g.setColour (Theme::cream);
    title.draw (g);
}

void Header::resized()
{
    const auto w = stemsPill.preferredWidth();
    stemsPill.setBounds (getWidth() - w, 3, w, 38);
    themePill.setBounds (stemsPill.getX() - 8 - themePill.preferredWidth(), 3, themePill.preferredWidth(), 38);
}

//==============================================================================
// Panel padding 14; the 2 px dashed zone is (15, 15, 682, 370) and its content box (17, 17, 678, 366).
DropScreen::DropScreen() : browse (juce::String (juce::CharPointer_UTF8 ("Browse\xe2\x80\xa6")), LisnButton::Style::Primary)
{
    browse.onClick = [this] { if (onBrowse != nullptr) onBrowse(); };   // 34 tall, padding 16, font 13: the defaults
    addAndMakeVisible (browse);
}

void DropScreen::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void DropScreen::setSixStems (bool s)
{
    if (s != six) { six = s; repaint(); }
}

void DropScreen::setHighlighted (bool h)
{
    if (h != highlighted) { highlighted = h; repaint(); }
}

float DropScreen::columnTop() const
{
    const auto total = 64.0f + 14.0f + Fonts::display (24.0f).getHeight() + 14.0f + 34.0f + 14.0f + Fonts::body (12.0f).getHeight()
                     + 14.0f + 10.0f + Fonts::body (12.0f).getHeight() + 8.0f + 28.0f;
    return 17.0f + (366.0f - total) / 2.0f;
}

void DropScreen::resized()
{
    const auto orW = juce::GlyphArrangement::getStringWidth (Fonts::body (14.0f), "or");
    const auto w = browse.preferredWidth();
    browse.setBounds (juce::roundToInt (centreX - (orW + 10.0f + (float) w) / 2.0f + orW + 10.0f),
                      juce::roundToInt (columnTop() + 64.0f + 14.0f + Fonts::display (24.0f).getHeight() + 14.0f), w, browse.height);
}

void DropScreen::paint (juce::Graphics& g)
{
    const juce::Rectangle<float> zone (15.0f, 15.0f, 682.0f, 370.0f);
    if (highlighted)
    {
        g.setColour (Theme::cream.withAlpha (0.04f));
        g.fillRoundedRectangle (zone, 16.0f);
    }
    drawDashedRoundedRect (g, zone.reduced (1.0f), 15.0f, 2.0f, 6.0f, 5.0f, Theme::cream.withAlpha (highlighted ? 0.7f : 0.36f));

    auto y = columnTop();
    g.setColour (Theme::cream.withAlpha (0.12f));
    g.fillEllipse (centreX - 32.0f, y, 64.0f, 64.0f);
    g.setColour (Theme::cream);
    strokeIcon (g, Icons::upload, { centreX - 14.0f, y + 18.0f, 28.0f, 28.0f }, 1.8f);
    y += 64.0f + 14.0f;

    const auto titleFont = Fonts::display (24.0f);
    text (g, titleFont, Theme::cream, "Drop a song here", { 0.0f, y, panelW, titleFont.getHeight() });
    y += titleFont.getHeight() + 14.0f;

    text (g, Fonts::body (14.0f), Theme::cream.withAlpha (0.72f), "or", { 0.0f, y, (float) browse.getX() - 10.0f, 34.0f },
          juce::Justification::centredRight);
    y += 34.0f + 14.0f;

    const auto small = Fonts::body (12.0f);
    text (g, tracked (small, 0.06f), Theme::cream.withAlpha (0.62f), "WAV" + dot + "MP3" + dot + "FLAC" + dot + "AIFF" + dot + "OGG",
          { 0.0f, y, panelW, small.getHeight() });
    y += small.getHeight() + 14.0f + 10.0f;
    text (g, small, Theme::cream.withAlpha (0.62f), "You'll get these stems", { 0.0f, y, panelW, small.getHeight() });
    y += small.getHeight() + 8.0f;

    // Chips: 28 tall, padding 0 12 0 10, an 8 px dot, 7 px, Bold 12; 6 px apart, centred.
    const auto chipFont = Fonts::body (12.0f, 700);
    const auto keys = stemKeys (six);
    juce::StringArray names;
    juce::Array<float> widths;
    auto total = -6.0f;
    for (const auto& k : keys)
    {
        const auto name = k.substring (0, 1).toUpperCase() + k.substring (1);
        names.add (name);
        widths.add (10.0f + 8.0f + 7.0f + juce::GlyphArrangement::getStringWidth (chipFont, name) + 12.0f);
        total += widths.getLast() + 6.0f;
    }
    auto x = centreX - total / 2.0f;
    for (int i = 0; i < keys.size(); ++i)
    {
        g.setColour (Theme::cream.withAlpha (0.08f));
        g.fillRoundedRectangle (x, y, widths[i], 28.0f, 14.0f);
        g.setColour (theme.stemColour (keys[i]));
        g.fillEllipse (x + 10.0f, y + 10.0f, 8.0f, 8.0f);
        text (g, chipFont, Theme::cream, names[i], { x + 25.0f, y, widths[i], 28.0f }, juce::Justification::centredLeft);
        x += widths[i] + 6.0f;
    }
}

//==============================================================================
SplittingScreen::SplittingScreen() : cancel ("Cancel", LisnButton::Style::Ghost)
{
    cancel.height = 36;
    cancel.padLeft = cancel.padRight = 18.0f;
    cancel.borderAlpha = 0.3f;
    cancel.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    addAndMakeVisible (cancel);

    for (int x = 4; x <= 436; x += 2)
    {
        const auto y = 20.0f + 8.0f * std::sin ((float) x / 55.0f * juce::MathConstants<float>::twoPi);
        if (x == 4) track.startNewSubPath ((float) x, y);
        else        track.lineTo ((float) x, y);
    }
    noteLines = lineCount (Fonts::body (12.0f), note, 420.0f);
    setSong ({}, false);
}

void SplittingScreen::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void SplittingScreen::setSong (const juce::String& name, bool six)
{
    song = name;
    subtitle = "Splitting into " + stemKeys (six).joinIntoString (dot);
    repaint();
}

void SplittingScreen::setProgress (double p)
{
    const auto pc = juce::jlimit (0, 100, (int) (p * 100.0 + 1e-6));   // rounded down, but 0.58 is 58, not 57.999...
    if (pc != percent) { percent = pc; repaint(); }
}

// Centred column, 18 px gaps: name / 4 / subtitle, percentage, wave (40), Cancel (36), note (12 px, line-height 1.5).
float SplittingScreen::columnTop() const
{
    const auto total = Fonts::body (15.0f, 700).getHeight() + 4.0f + Fonts::body (13.0f).getHeight() + 18.0f
                     + Fonts::display (40.0f).getHeight() + 18.0f + 40.0f + 18.0f + 36.0f + 18.0f + (float) noteLines * 18.0f;
    return 1.0f + (398.0f - total) / 2.0f;
}

void SplittingScreen::resized()
{
    const auto y = columnTop() + Fonts::body (15.0f, 700).getHeight() + 4.0f + Fonts::body (13.0f).getHeight() + 18.0f
                 + Fonts::display (40.0f).getHeight() + 18.0f + 40.0f + 18.0f;
    const auto w = cancel.preferredWidth();
    cancel.setBounds (juce::roundToInt (centreX - (float) w / 2.0f), juce::roundToInt (y), w, cancel.height);
}

void SplittingScreen::paint (juce::Graphics& g)
{
    const auto nameFont = Fonts::body (15.0f, 700), subFont = Fonts::body (13.0f), pctFont = Fonts::display (40.0f);
    auto y = columnTop();
    text (g, nameFont, Theme::cream, song, { 0.0f, y, panelW, nameFont.getHeight() });
    y += nameFont.getHeight() + 4.0f;
    text (g, subFont, Theme::cream.withAlpha (0.68f), subtitle, { 0.0f, y, panelW, subFont.getHeight() });
    y += subFont.getHeight() + 18.0f;
    text (g, pctFont, Theme::cream, juce::String (percent) + "%", { 0.0f, y, panelW, pctFont.getHeight() });
    y += pctFont.getHeight() + 18.0f;

    // The wavy line: track in cream 0.2, the done part in the accent, then the knob with its 4 px ring.
    const auto left = centreX - 220.0f, done = 440.0f * (float) percent / 100.0f;
    const auto origin = juce::AffineTransform::translation (left, y);
    const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (Theme::cream.withAlpha (0.2f));
    g.strokePath (track, stroke, origin);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (juce::roundToInt (left), juce::roundToInt (y), juce::roundToInt (done), 40);
        g.setColour (theme.accent());
        g.strokePath (track, stroke, origin);
    }
    const juce::Point<float> knob (left + done, y + 20.0f + 8.0f * std::sin (done / 55.0f * juce::MathConstants<float>::twoPi));
    g.setColour (theme.accent().withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (knob));
    g.setColour (Theme::cream);
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (knob));
    y += 40.0f + 18.0f + 36.0f + 18.0f;

    paragraph (g, Fonts::body (12.0f), Theme::cream.withAlpha (0.6f), note, { centreX - 210.0f, y, 420.0f, 0.0f }, 18.0f,
               juce::Justification::centred);
}

//==============================================================================
// Panel padding 16 18: content (19, 17, 674, 366). Song row 36, divider at 63, rows from 74 (4 rows end at 352, 6 at 351),
// position strip at 375.

namespace
{
    constexpr int waveLeft = 19 + 194, waveWidth = 376;   // the waveforms: rows at x 19, waveform at row x 194
    constexpr float stripY = 375.0f;                      // the position strip (4 tall) while zoomed, in the old footer's slot
    const juce::Rectangle<int> stripArea { waveLeft - 2, 372, waveWidth + 4, 10 };   // with the loop ticks
}

juce::String beatsText (double beats)
{
    const auto q = std::round (beats * 4.0) / 4.0;
    const auto s = juce::String (q, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return s + (juce::exactlyEqual (q, 1.0) ? " beat" : " beats");
}

StemsScreen::StemsScreen() : newSong ("New song", LisnButton::Style::Ghost)
{
    newSong.icon = Icons::plus;                    // 16 px, stroke 2; 34 tall, border 0.22, font 13: the defaults
    newSong.padLeft = 10.0f;
    newSong.padRight = 14.0f;
    newSong.onClick = [this] { if (onNewSong != nullptr) onNewSong(); };
    playButton.onClick = [this] { if (onPlayPause != nullptr) onPlayPause(); };
    loopButton.icon = Icons::loop;
    loopButton.padLeft = 10.0f;
    loopButton.padRight = 14.0f;
    loopButton.onClick = [this] { toggleLoop(); };
    dragChip.fileToDrag = [this] { return mixFile != nullptr ? mixFile() : juce::File(); };
    addAndMakeVisible (loopButton);
    addAndMakeVisible (dragChip);
    addAndMakeVisible (newSong);
    addAndMakeVisible (playButton);
}

StemsScreen::~StemsScreen()
{
    pool.removeAllJobs (true, 2000);
}

void StemsScreen::setTheme (const Theme& t)
{
    theme = t;
    loopButton.tint = loopOn ? std::optional<juce::Colour> (theme.accent()) : std::nullopt;
    loopButton.repaint();
    for (auto* r : rows)
        r->setTheme (t);
    repaint();
}

void StemsScreen::setStems (const juce::File& newDir, const juce::String& songName)
{
    ++generation;
    pool.removeAllJobs (true, 2000);
    dir = newDir;
    song = songName;
    length = 0.0;
    playing = peaksReady = false;
    hasHead = false;
    view = { 0.0, 1.0 };
    position = 0.0;
    hot = grab = -1;
    dragging = false;
    loopRange = dragBand = {};
    loopOn = false;
    tempo = {};
    playButton.setPlaying (false);
    rows.clear();

    auto files = SeparationJob::stemsIn (dir);   // A-Z
    std::stable_sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return stemRank (a) < stemRank (b); });
    for (int i = 0; i < files.size(); ++i)
    {
        auto* row = rows.add (new StemRow (files[i], files[i].getFileNameWithoutExtension().toLowerCase()));
        row->setTheme (theme);
        row->setCompact (files.size() > 4);
        row->onToggleMute = [this, i] { if (onToggleMute != nullptr) onToggleMute (i); };
        row->onSolo = [this, i] { if (onSolo != nullptr) onSolo (i); };
        row->onWaveMouse = [this] (WaveMouse m, double f, bool alt) { waveMouse (m, f, alt); };
        row->dragFile = [this, i] { return stemFile != nullptr ? stemFile (i) : fileOf (i); };
        addAndMakeVisible (row);
    }
    resized();
    repaint();

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
}

juce::Array<juce::File> StemsScreen::getFiles() const
{
    juce::Array<juce::File> files;
    for (auto* r : rows) files.add (r->getFile());
    return files;
}

juce::File StemsScreen::fileOf (int row) const
{
    return juce::isPositiveAndBelow (row, rows.size()) ? rows[row]->getFile() : juce::File();
}

juce::Range<int> StemsScreen::waveSpan() const
{
    if (rows.isEmpty()) return {};
    const auto& first = rows.getFirst()->waveform();
    const auto& last = rows.getLast()->waveform();
    return { rows.getFirst()->getY() + first.getY(), rows.getLast()->getY() + last.getBottom() };
}

int StemsScreen::headX (double f) const
{
    return waveLeft + juce::roundToInt ((f - view.getStart()) / view.getLength() * waveWidth);
}

double StemsScreen::fractionAt (float x) const
{
    return view.getStart() + (x - (float) waveLeft) / (double) waveWidth * view.getLength();
}

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

void StemsScreen::setTempo (Tempo t)
{
    tempo = t;
    repaint (19 + 36 + 12, 37, 674 - 36 - 12, 16);   // the subtitle shows the BPM
}

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
    setMouseCursor (edge >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);   // over a tab
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
            // 4 px or more: a trim or a new loop instead of a seek. Less is a click, whatever the mouse jitters (JUCE sends a
            // drag for any move, sub-pixel at 125-150 % DPI). ponytail: a handle dragged to the view's edge stops there, no
            // autoscroll. Upgrade path: scroll the view while a handle is held at an edge.
            if (! dragging && std::abs (x - downX) < 4)
                break;
            dragging = true;
            dragBand = grab >= 0 ? trimmed (f, free)
                                 : juce::Range<double> { snap (juce::jmin (downFraction, f), free), snap (juce::jmax (downFraction, f), free) };
            repaint (waveSpanArea());
            break;
        case WaveMouse::up:
            if (dragging && ! dragBand.isEmpty() && (grab < 0 || dragBand != loopRange) && onSetLoop != nullptr)
                onSetLoop (dragBand.getStart(), dragBand.getEnd());
            dragBand = {};
            grab = -1;
            dragging = false;
            repaint (waveSpanArea());
            break;
    }
}

// The tabs stick out above the waveforms over the rows' margins, which pass the mouse on: run the same gesture here, x
// clamped to the waves like WaveformView's. A press that began here owns its drag and release, wherever they go; elsewhere
// the mouse un-hots.
void StemsScreen::tabMouse (WaveMouse m, const juce::MouseEvent& e)
{
    const auto inside = waveSpanArea().contains (e.getPosition());
    if (m == WaveMouse::down) tabDown = inside;
    const auto ours = m == WaveMouse::drag || m == WaveMouse::up ? tabDown : inside;
    if (m == WaveMouse::up) tabDown = false;
    if (ours)
        waveMouse (m, fractionAt ((float) juce::jlimit (waveLeft, waveLeft + waveWidth, e.x)), e.mods.isAltDown());
    else
        waveMouse (WaveMouse::exit, 0.0, false);
}

void StemsScreen::mouseMove (const juce::MouseEvent& e) { tabMouse (WaveMouse::move, e); }
void StemsScreen::mouseDown (const juce::MouseEvent& e) { tabMouse (WaveMouse::down, e); }
void StemsScreen::mouseDrag (const juce::MouseEvent& e) { tabMouse (WaveMouse::drag, e); }
void StemsScreen::mouseUp (const juce::MouseEvent& e)   { tabMouse (WaveMouse::up, e); }
void StemsScreen::mouseExit (const juce::MouseEvent& e) { tabMouse (WaveMouse::exit, e); }

void StemsScreen::setLoop (juce::Range<double> range, bool on)
{
    if (range == loopRange && on == loopOn) return;
    repaint (loopArea (loopOn ? loopRange : juce::Range<double>()));   // the old band
    loopRange = range;
    loopOn = on;
    if (! on) setHot (-1);
    repaint (loopArea (loopOn ? loopRange : juce::Range<double>()));   // the new band
    repaint (19 + 36 + 12, 37, 674 - 36 - 12, 16);   // the subtitle shows the loop
    repaint (stripArea);                             // the strip's loop ticks
    loopButton.tint = loopOn ? std::optional<juce::Colour> (theme.accent()) : std::nullopt;
    loopButton.repaint();
    dragChip.setText (loopOn ? "Drag loop" : "Drag mix");
    resized();                                       // the chip width follows its text
}

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
        start = juce::jmax (tempo.firstBeat, start);             // before the first beat: the first bar (bars start at firstBeat)
        len = 4.0 * bar;
    }
    if (start + len > length) start = length - len;             // past the end: the song's last 4 bars (or 8 s)
    start = juce::jmax (0.0, start);                             // a shorter song loops whole
    return { start / length, juce::jmin (length, start + len) / length };
}

juce::Rectangle<int> StemsScreen::playheadArea (double f) const
{
    const auto span = waveSpan();
    return { headX (f) - 2, span.getStart(), 4, span.getLength() };
}

void StemsScreen::setPlayback (bool isPlaying, double f, double len)
{
    f = juce::jlimit (0.0, 1.0, f);
    position = f;
    if (len > 0.0) length = len;
    bool changed = false;
    for (auto* r : rows) r->setPosition (f);
    const auto x = headX (f);
    if (! hasHead || x != lastHead)                  // the played colour and the line change only between the two positions
    {
        // Clamped to the waves (the line's 2 px included): a move into the view repaints the whole played part from the
        // panel's edge, and a playhead outside the view repaints nothing.
        const auto span = waveSpan();
        const auto from = hasHead ? juce::jmin (lastHead, x) : x, to = hasHead ? juce::jmax (lastHead, x) : x;
        const auto area = juce::Rectangle<int>::leftTopRightBottom (from - 2, span.getStart(), to + 2, span.getEnd())
                              .getIntersection ({ waveLeft - 2, span.getStart(), waveWidth + 4, span.getLength() });
        lastHead = x;
        hasHead = true;
        if (! area.isEmpty())
        {
            repaint (area);
            changed = true;
        }
    }
    const auto t = mmss (f * length) + " / " + mmss (length);
    if (t != timeText)
    {
        timeText = t;
        repaint (19 + 36 + 12, 37, 674 - 36 - 12, 16);   // the subtitle line only
        changed = true;
    }
    if (isPlaying != playing)
    {
        playing = isPlaying;
        playButton.setPlaying (playing);
        changed = true;
    }
    repaintCount += changed ? 1 : 0;
}

void StemsScreen::setAudible (juce::uint32 mask)
{
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setMuted (((mask >> i) & 1u) == 0);
}

void StemsScreen::setView (juce::Range<double> v)
{
    const auto w = juce::jlimit (1e-9, 1.0, v.getLength());
    const auto start = juce::jlimit (0.0, 1.0 - w, v.getStart());
    // A width within rounding of 1 is the whole song: {1e-16, 1} would show the position strip and scroll at full zoom-out.
    const auto next = w >= 1.0 - 1e-9 ? juce::Range<double> (0.0, 1.0) : juce::Range<double> (start, start + w);
    if (next == view) return;
    view = next;
    for (auto* r : rows) r->setView (view);
    if (hasHead) lastHead = headX (position);
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
            // A playhead-strip repaint away from the tab skips its rounded rectangles (paths).
            if (! g.clipRegionIntersects (tab.expanded (3.0f).getSmallestIntegerContainer())) continue;
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

    if (! hasHead || lastHead < waveLeft || lastHead > right) return;   // no playhead yet, or it's outside the view
    g.setColour (Theme::cream);
    g.fillRoundedRectangle ((float) lastHead - 1.0f, top, 2.0f, h, 1.0f);
}

void StemsScreen::resized()
{
    playButton.setBounds (19, 17, 36, 36);
    const auto w = newSong.preferredWidth();
    newSong.setBounds (19 + 674 - w, 18, w, newSong.height);
    const auto chipW = dragChipWidth (dragChip.getText());
    dragChip.setBounds (newSong.getX() - 8 - chipW, 18, chipW, 34);
    loopButton.setBounds (dragChip.getX() - 8 - loopButton.preferredWidth(), 18, loopButton.preferredWidth(), loopButton.height);
    auto y = 74;
    for (auto* r : rows)
    {
        r->setBounds (19, y, 674, r->getHeight());
        y += r->getHeight() + (rows.size() > 4 ? 5 : 10);
    }
}

void StemsScreen::paint (juce::Graphics& g)
{
    // Playhead repaints only touch the rows: skip the path work outside the clip.
    if (g.clipRegionIntersects ({ 19, 17, 674, 36 }))
    {
        // Song name (Bold 15) over the time (12, cream 0.64): line-height 1.2, 2 px apart, centred in the 36 px row.
        const auto x = 19.0f + 36.0f + 12.0f, w = (float) loopButton.getX() - 12.0f - x, y = 17.0f + (36.0f - 34.4f) / 2.0f;
        text (g, Fonts::body (15.0f, 700), Theme::cream, song, { x, y, w, 18.0f }, juce::Justification::centredLeft);
        auto subtitle = timeText;
        if (tempo.bpm > 0.0) subtitle << dot << juce::roundToInt (tempo.bpm) << " BPM";
        if (loopOn)
        {
            subtitle << dot << "loop " << mmss (loopRange.getStart() * length)
                     << juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) << mmss (loopRange.getEnd() * length);
            if (tempo.bpm > 0.0 && length > 0.0)
                subtitle << dot << beatsText (loopRange.getLength() * length * tempo.bpm / 60.0);
        }
        text (g, Fonts::body (12.0f), Theme::cream.withAlpha (0.64f), subtitle, { x, y + 20.0f, w, 14.4f }, juce::Justification::centredLeft);
    }

    g.setColour (Theme::cream.withAlpha (0.12f));
    g.fillRect (19.0f, 63.0f, 674.0f, 1.0f);

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
}

//==============================================================================
// A 520 px column centred in the panel (x 96), 16 px gaps: icon + title (44), body, code box, 4 px, buttons (38).
ErrorScreen::ErrorScreen()
    : copy ("Copy", LisnButton::Style::Soft), retry ("Try again", LisnButton::Style::Primary),
      findPython (juce::String (juce::CharPointer_UTF8 ("Already installed? Find python.exe\xe2\x80\xa6")), LisnButton::Style::Ghost)
{
    copy.icon = Icons::copy;
    copy.iconSize = 14.0f;
    copy.height = 32;
    copy.padLeft = 10.0f;
    copy.padRight = 12.0f;
    copy.fontSize = 12.0f;
    retry.height = findPython.height = 38;
    retry.padLeft = retry.padRight = 18.0f;
    findPython.borderAlpha = 0.3f;

    copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (copyText); };
    retry.onClick = [this] { if (onRetry != nullptr) onRetry(); };
    findPython.onClick = [this] { if (onFindPython != nullptr) onFindPython(); };
    for (auto* b : { &copy, &retry, &findPython })
        addAndMakeVisible (b);
    setError (ErrorKind::PythonMissing, {}, {});
}

void ErrorScreen::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void ErrorScreen::setError (ErrorKind k, const juce::String& text, const juce::String& pythonExe)
{
    kind = k == ErrorKind::PythonMissing || k == ErrorKind::DemucsMissing ? k : ErrorKind::Failed;
    const juce::String pip ("python -m pip install demucs");
    lines.clear();
    if (kind == ErrorKind::PythonMissing)
    {
        title = "Python isn't set up yet";
        body = "StemSplitter splits songs with Demucs, a free AI tool that runs on Python. "
               "Install Python 3.11 from python.org, then run this once in a terminal:";
        command = copyText = pip;
    }
    else if (kind == ErrorKind::DemucsMissing)
    {
        title = "Demucs isn't installed";
        body = "Python is installed, but Demucs isn't. Run this once in a terminal, then try again:";
        copyText = pythonExe.isEmpty() ? pip : pipFor (pythonExe);
        command = pythonExe.isEmpty() ? pip : shortCommand (pythonExe, 484.0f - (float) copy.preferredWidth());   // box text width
    }
    else
    {
        title = "The split didn't finish";
        body = "Demucs stopped with this message:";
        copyText = text;
        lines = juce::StringArray::fromLines (text.fromFirstOccurrenceOf ("\n", false, false).replaceCharacter ('\r', '\n'));
        lines.removeEmptyStrings();
        for (int i = lines.size(); --i >= 0;)
            if (lines[i].contains ("%|"))        // tqdm progress bars
                lines.remove (i);
        lines.removeRange (0, lines.size() - 6);
        if (lines.isEmpty())
            lines.add (text.upToFirstOccurrenceOf ("\n", false, false));
    }
    resized();
    repaint();
}

void ErrorScreen::resized()
{
    const bool failed = kind == ErrorKind::Failed;
    bodyHeight = (float) lineCount (Fonts::body (14.0f), body, 520.0f) * 14.0f * 1.55f;
    const auto boxH = failed ? 20.0f + 18.0f * (float) lines.size() : 46.0f;
    top = 1.0f + (398.0f - (44.0f + 16.0f + bodyHeight + 16.0f + boxH + 16.0f + 4.0f + 38.0f)) / 2.0f;
    box = { 96.0f, top + 44.0f + 16.0f + bodyHeight + 16.0f, 520.0f, boxH };

    const auto copyW = copy.preferredWidth();   // 1 px border + 6 px padding from the box edge
    copy.setBounds (juce::roundToInt (box.getRight() - 7.0f) - copyW,
                    juce::roundToInt (failed ? box.getY() + 7.0f : box.getCentreY() - 16.0f), copyW, copy.height);
    const auto y = juce::roundToInt (box.getBottom() + 20.0f);
    retry.setBounds (96, y, retry.preferredWidth(), retry.height);
    findPython.setBounds (retry.getRight() + 10, y, findPython.preferredWidth(), findPython.height);
    findPython.setVisible (! failed);
}

void ErrorScreen::paint (juce::Graphics& g)
{
    const juce::Rectangle<float> circle (96.0f, top, 44.0f, 44.0f);
    g.setColour (theme.drums.withAlpha (0.18f));
    g.fillEllipse (circle);
    g.setColour (theme.drums);
    strokeIcon (g, Icons::alert, circle.withSizeKeepingCentre (22.0f, 22.0f), 2.0f);
    text (g, Fonts::display (20.0f), Theme::cream, title, { circle.getRight() + 14.0f, top, 520.0f - 58.0f, 44.0f },
          juce::Justification::centredLeft);
    paragraph (g, Fonts::body (14.0f), Theme::cream.withAlpha (0.8f), body, { 96.0f, top + 60.0f, 520.0f, bodyHeight }, 14.0f * 1.55f,
               juce::Justification::left);

    g.setColour (juce::Colour (0xff0C060A).withAlpha (0.55f));
    g.fillRoundedRectangle (box, 12.0f);
    g.setColour (Theme::cream.withAlpha (0.12f));
    g.drawRoundedRectangle (box.reduced (0.5f), 11.5f, 1.0f);

    const auto x = box.getX() + 17.0f, w = (float) copy.getX() - 12.0f - x;
    if (kind == ErrorKind::Failed)
        for (int i = 0; i < lines.size(); ++i)
            text (g, Fonts::mono (14.0f), Theme::cream, lines[i], { x, box.getY() + 10.0f + 18.0f * (float) i, w, 18.0f },
                  juce::Justification::centredLeft);
    else
        text (g, Fonts::mono (14.0f), Theme::cream, command, { x, box.getY(), w, box.getHeight() }, juce::Justification::centredLeft);
}
