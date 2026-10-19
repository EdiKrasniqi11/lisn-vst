#include "Screens.h"
#include "BinaryData.h"
#include "Icons.h"
#include "Peaks.h"

namespace
{
    // HeaderLogoGlass.dc.html, option A: lisn-symbol-glass.svg at 32 px, then "LISN" 10 px after it. The disc is centred in
    // the symbol's box and the box on the capitals: the baseline (30.25) minus half of Unbounded's 0.75 em cap height is y 22.
    const juce::Rectangle<float> symbolArea { 0.0f, 6.0f, 32.0f, 32.0f };
    const float titleX = symbolArea.getRight() + 10.0f;

    // Non-ASCII UI text is escaped UTF-8: String (const char*) would read the bytes as ASCII.
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    const juce::String times (juce::CharPointer_UTF8 ("\xc3\x97"));
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

    // The "You'll get" chips: 28 tall, padding 0 12 0 10, an 8 px dot, 7 px, Bold 12; 6 px apart, centred.
    void chipRow (juce::Graphics& g, float y, const juce::StringArray& names, const juce::Array<juce::Colour>& dots)
    {
        const auto chipFont = Fonts::body (12.0f, 700);
        juce::Array<float> widths;
        auto total = -6.0f;
        for (const auto& name : names)
        {
            widths.add (10.0f + 8.0f + 7.0f + juce::GlyphArrangement::getStringWidth (chipFont, name) + 12.0f);
            total += widths.getLast() + 6.0f;
        }
        auto x = centreX - total / 2.0f;
        for (int i = 0; i < names.size(); ++i)
        {
            g.setColour (Theme::cream.withAlpha (0.08f));
            g.fillRoundedRectangle (x, y, widths[i], 28.0f, 14.0f);
            g.setColour (dots[i]);
            g.fillEllipse (x + 10.0f, y + 10.0f, 8.0f, 8.0f);
            text (g, chipFont, Theme::cream, names[i], { x + 25.0f, y, widths[i], 28.0f }, juce::Justification::centredLeft);
            x += widths[i] + 6.0f;
        }
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
Header::Header()
    : symbol (juce::Drawable::createFromImageData (BinaryData::lisnsymbolglass_svg, (size_t) BinaryData::lisnsymbolglass_svgSize)),
      themePill ({ "Dusk", "Midnight" }), stemsPill ({ "4 stems", "6 stems" })
{
    jassert (symbol != nullptr);
    title.addLineOfText (tracked (Fonts::display (22.0f), 0.08f), "LISN", titleX, 30.25f);   // the reel shares its baseline
    makeReel (themeFor ("dusk"));

    themePill.onChange = [this] (int i) { if (onTheme != nullptr) onTheme (i == 1 ? "midnight" : "dusk"); };
    stemsPill.onChange = [this] (int i)
    {
        if (harmoniesPill) { bestSelected = i == 1; if (onHarmonyQuality != nullptr) onHarmonyQuality (bestSelected); }
        else               { sixSelected = i == 1;  if (onSixStems != nullptr) onSixStems (sixSelected); }
    };
    addAndMakeVisible (themePill);
    addAndMakeVisible (stemsPill);
    update.onClick = [this] { if (onUpdate != nullptr) onUpdate(); };
    addChildComponent (update);
}

void Header::showUpdate (const juce::String& version, const juce::String& label)
{
    update.setButtonText (label.isNotEmpty() ? label : "Update " + version);
    updateVersion = version;
    placeUpdate();
}

void Header::placeUpdate()
{
    // The title bar has no room for the name and the pill, nor the plugin's header for a long pill ("Restart FL Studio to
    // finish"): the pill takes the name's place (after "LISN") while it rests on StemSplitter, and steps aside while the
    // reel spins to or shows Harmonies.
    const auto reelBusy = spinning || reelTarget > 0.0 || reelPos != 0.0;
    pillOverName = titleBar || nameArea().getRight() - 4 + 16 + update.preferredWidth() + 12 > themePill.getX();
    update.setVisible (updateVersion.isNotEmpty() && ! (pillOverName && reelBusy));
    const auto x = pillOverName ? juce::roundToInt (title.getBoundingBox (0, -1, true).getRight()) : nameArea().getRight() - 4;
    update.setBounds (x + 16, 9, update.preferredWidth(), 26);   // 16 px after the title, centred on the capitals (y 22)
    repaint (nameArea().expanded (14, 0));
}

namespace
{
    float nameX()                                  // the reel's left edge: 10 px after "LISN"
    {
        return titleX + juce::GlyphArrangement::getStringWidth (tracked (Fonts::display (22.0f), 0.08f), "LISN") + 10.0f;
    }
    constexpr float reelRow = 22.0f;               // px between the reel's words

    double easeOutBack (double t)                  // off the mark fast, a hair past the end (about 0.6 %), back
    {
        constexpr double c1 = 0.45, c3 = c1 + 1.0;
        const auto u = t - 1.0;
        return 1.0 + c3 * u * u * u + c1 * u * u;
    }
}

void Header::makeReel (const Theme& t)
{
    const auto font = Fonts::body (14.0f, 500);
    const auto x = nameX();
    const auto word = [&] (const juce::String& s, juce::Colour c)
    {
        Word w { {}, c };
        w.glyphs.addLineOfText (font, s, x, 0.0f);
        return w;
    };
    reel.clear();
    reel.push_back (word ("StemSplitter", Theme::cream));
    for (int k = 0; k < 3; ++k)
    {
        reel.push_back (word ("Vocals", t.vocals));
        reel.push_back (word ("Drums", t.drums));
        reel.push_back (word ("Bass", t.bass));
        reel.push_back (word ("Other", t.other));
        reel.push_back (word ("Lead", t.vocals));
        reel.push_back (word ("StemSplitter", Theme::cream));
    }
    reel.push_back (word ("Harmonies", t.guitar));
    if (reelTarget > 0.0) reelTarget = (double) reel.size() - 1.0;
}

juce::Rectangle<int> Header::nameArea() const
{
    const auto w = juce::GlyphArrangement::getStringWidth (Fonts::body (14.0f, 500), "StemSplitter");   // the widest word
    return { (int) nameX() - 4, 6, juce::roundToInt (w) + 8, 32 };
}

void Header::spinTo (bool harmonies, bool animate)
{
    const auto target = harmonies ? (double) reel.size() - 1.0 : 0.0;
    if (! animate)
    {
        spinning = false;
        reelFrames.reset();
        reelPos = reelTarget = target;
        landedAt = 0.0;
        placeUpdate();
        return;
    }
    if (target == reelTarget && (spinning || reelPos == target))
        return;                                      // there, or on its way
    reelTarget = target;
    reelFrom = reelPos;
    landedAt = 0.0;
    reelStart = juce::Time::getMillisecondCounterHiRes();
    reelMs = harmonies ? 1900.0 : 1300.0;
    spinning = true;
    placeUpdate();
    if (reelFrames == nullptr)
        reelFrames = std::make_unique<juce::VBlankAttachment> (this, [this] { reelFrame(); });
}

void Header::reelFrame()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (spinning)
    {
        const auto t = juce::jlimit (0.0, 1.0, (now - reelStart) / reelMs);
        reelPos = reelFrom + (reelTarget - reelFrom) * easeOutBack (t);
        if (t >= 1.0)
        {
            reelPos = reelTarget;
            spinning = false;
            landedAt = reelTarget > 0.0 ? now : 0.0;
            placeUpdate();
            if (onLanded != nullptr)
                onLanded();
        }
    }
    if (! spinning && (landedAt == 0.0 || now - landedAt > 1200.0))
    {
        landedAt = 0.0;
        // Not from inside its own callback: drop the attachment on the next message.
        juce::MessageManager::callAsync ([safe = SafePointer<Header> (this)]
        {
            if (safe != nullptr && ! safe->spinning && safe->landedAt == 0.0)
                safe->reelFrames.reset();
        });
    }
    repaint (nameArea().expanded (14, 0));
}

void Header::setReelPosition (double index)
{
    spinning = false;
    reelFrames.reset();
    reelPos = juce::jlimit (0.0, (double) reel.size() - 1.0, index);
    reelTarget = std::round (reelPos);
    placeUpdate();
    repaint();
}

void Header::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (nameClickable && nameArea().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                                           : juce::MouseCursor::NormalCursor);
}

void Header::mouseUp (const juce::MouseEvent& e)
{
    if (nameClickable && ! e.mouseWasDraggedSinceMouseDown() && nameArea().contains (e.getPosition()) && onName != nullptr)
        onName();
}

void Header::setTheme (const Theme& t)
{
    makeReel (t);
    repaint();
    themePill.setTheme (t);
    stemsPill.setTheme (t);
    update.setAccent (t.accent());
    themePill.setSelected (t.id == "midnight" ? 1 : 0);
}

void Header::setSixStems (bool six)
{
    sixSelected = six;
    refreshStemsPill();
}

void Header::setProduct (bool harmonies)
{
    harmoniesPill = harmonies;
    refreshStemsPill();
}

void Header::setHarmonyQuality (bool best)
{
    bestSelected = best;
    refreshStemsPill();
}

void Header::setBestTooltip (bool show)
{
    bestTip = show;
    refreshStemsPill();
}

void Header::refreshStemsPill()
{
    stemsPill.setItems (harmoniesPill ? juce::StringArray { "Quick", "Best quality" } : juce::StringArray { "4 stems", "6 stems" });
    stemsPill.setSelected ((harmoniesPill ? bestSelected : sixSelected) ? 1 : 0);
    stemsPill.setItemTooltip (1, harmoniesPill && bestTip ? "900 MB download first" : "");
    if (stemsPill.getWidth() != stemsPill.preferredWidth())
        resized();                                     // "Best quality" is wider than "6 stems"
}

void Header::setEnabledSwitches (bool on)
{
    for (auto* p : { &themePill, &stemsPill })
    {
        p->setEnabled (on);
        p->setAlpha (on ? 1.0f : 0.55f);
    }
}

void Header::makeTitleBar (std::function<void()> minimise, std::function<void()> close)
{
    titleBar = true;
    nativeButtons = ! minimise && ! close;          // the Mac: macOS draws the buttons, LISN adds none
    if (! nativeButtons)
    {
        minimiseButton.onClick = std::move (minimise);
        closeButton.onClick = std::move (close);
        addAndMakeVisible (minimiseButton);
        addAndMakeVisible (closeButton);
    }
    resized();
    repaint();
}

void Header::paint (juce::Graphics& g)
{
    auto& context = g.getInternalContext();
    const auto scale = context.getPhysicalPixelScaleFactor();
    const auto type = context.getPreferredImageTypeForTemporaryImages();
    if (symbolImage.isNull() || scale != symbolScale || symbolImage.getPixelData()->createType()->getTypeID() != type->getTypeID())
    {
        // The image starts at this component's origin, so at a whole-pixel position its pixels land on the screen's.
        symbolScale = scale;
        symbolImage = juce::Image (juce::Image::ARGB, (int) std::ceil (symbolArea.getRight() * scale),
                                   (int) std::ceil (symbolArea.getBottom() * scale), true, *type);
        juce::Graphics sg (symbolImage);
        sg.addTransform (juce::AffineTransform::scale (scale));
        if (symbol != nullptr)
            symbol->drawWithin (sg, symbolArea, juce::RectanglePlacement::centred, 1.0f);
    }
    g.drawImageTransformed (symbolImage, juce::AffineTransform::scale (1.0f / scale));

    g.setColour (Theme::cream);
    title.draw (g);
    if (! (pillOverName && update.isVisible()))      // the update pill sits where the name goes
    {
        // The reel: the words around reelPos, faded by their distance from the line. Mid-spin each word leaves two faint
        // copies along its path (motion blur); the landed "Harmonies" glows for 1.2 s.
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (nameArea());
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto blur = spinning ? (float) (6.0 * std::sin (juce::MathConstants<double>::pi
                                                              * juce::jlimit (0.0, 1.0, (now - reelStart) / reelMs))) : 0.0f;
        const auto last = (int) reel.size() - 1;
        for (int i = juce::jmax (0, (int) std::floor (reelPos) - 1); i <= juce::jmin (last, (int) std::ceil (reelPos) + 1); ++i)
        {
            const auto d = (float) (i - reelPos);
            const auto alpha = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (d));   // gone one row away: nothing peeks at rest
            if (alpha <= 0.0f) continue;
            const auto& w = reel[(size_t) i];
            const auto at = juce::AffineTransform::translation (0.0f, 30.25f + d * reelRow);
            for (const auto off : { -blur, blur })
                if (blur > 0.5f)
                {
                    g.setColour (w.colour.withMultipliedAlpha (alpha * 0.22f));
                    w.glyphs.draw (g, at.translated (0.0f, off));
                }
            if (i == last && landedAt > 0.0)
            {
                const auto glow = (float) std::sin (juce::MathConstants<double>::pi * juce::jlimit (0.0, 1.0, (now - landedAt) / 1200.0));
                juce::Path p;
                w.glyphs.createPath (p);
                p.applyTransform (at);
                juce::DropShadow (w.colour.withAlpha (0.9f * glow), 12, {}).drawForPath (g, p);
            }
            g.setColour (w.colour.withMultipliedAlpha (alpha));
            w.glyphs.draw (g, at);
        }
    }
    if (titleBar && ! nativeButtons)   // the 1 px separator between the pills and the window buttons
    {
        g.setColour (Theme::cream.withAlpha (0.24f));
        g.fillRect (minimiseButton.getX() - 11, 11, 1, 22);
    }
}

void Header::resized()
{
    auto right = getWidth();
    if (titleBar && ! nativeButtons)   // board 1A: pills, 8 + 2 px, separator, 2 + 8 px, Minimise, 8 px, Close
    {
        closeButton.setBounds (right - 32, 6, 32, 32);
        minimiseButton.setBounds (closeButton.getX() - 8 - 32, 6, 32, 32);
        right = minimiseButton.getX() - 21;
    }
    const auto w = stemsPill.preferredWidth();
    stemsPill.setBounds (right - w, 3, w, 38);
    themePill.setBounds (stemsPill.getX() - 8 - themePill.preferredWidth(), 3, themePill.preferredWidth(), 38);
    // The update pill: 16 px after the title, centred on the header like the capitals (y 22); clear of the theme pill
    placeUpdate();
}

void Header::mouseDown (const juce::MouseEvent& e)
{
    if (auto* top = getTopLevelComponent(); titleBar && top != nullptr)
        dragger.startDraggingComponent (top, e.getEventRelativeTo (top));
}

void Header::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* top = getTopLevelComponent(); titleBar && top != nullptr)
        dragger.dragComponent (top, e.getEventRelativeTo (top), nullptr);
}

WindowButton::WindowButton (bool closeIcon) : juce::Button (closeIcon ? "Close" : "Minimise"), close (closeIcon)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void WindowButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (32, 18, 27).withAlpha (down ? 0.85f : over ? 0.72f : 0.55f));
    g.fillEllipse (r);
    g.setColour (Theme::cream.withAlpha (over ? 0.36f : 0.18f));
    g.drawEllipse (r, 1.0f);

    juce::Path icon;   // the board's 14 px icons on a 24 grid: "M6 12h12" and "M7 7l10 10M17 7 7 17"
    if (close) { icon.startNewSubPath (7, 7); icon.lineTo (17, 17); icon.startNewSubPath (17, 7); icon.lineTo (7, 17); }
    else       { icon.startNewSubPath (6, 12); icon.lineTo (18, 12); }
    const auto s = 14.0f / 24.0f;
    icon.applyTransform (juce::AffineTransform::scale (s).translated (r.getCentreX() - 7.0f, r.getCentreY() - 7.0f));
    g.setColour (Theme::cream);
    g.strokePath (icon, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

    const auto keys = stemKeys (six);
    juce::StringArray names;
    juce::Array<juce::Colour> dots;
    for (const auto& k : keys)
    {
        names.add (k.substring (0, 1).toUpperCase() + k.substring (1));
        dots.add (theme.stemColour (k));
    }
    chipRow (g, y, names, dots);
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
HarmonyDropScreen::HarmonyDropScreen()
    : offer ({}, LisnButton::Style::Primary), browse (juce::String (juce::CharPointer_UTF8 ("Browse\xe2\x80\xa6")), LisnButton::Style::Primary)
{
    offer.onClick = [this] { if (onOffer != nullptr) onOffer(); };
    browse.onClick = [this] { if (onBrowse != nullptr) onBrowse(); };
    addChildComponent (offer);
    addAndMakeVisible (browse);
}

void HarmonyDropScreen::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void HarmonyDropScreen::setOffer (const juce::String& songName)
{
    auto name = songName;
    const auto label = [] (const juce::String& n) { return "Split the harmonies of " + n; };
    offer.setButtonText (label (name));
    while (name.length() > 1 && offer.preferredWidth() > 600)   // a long file name is cut, with an ellipsis, to fit the zone
    {
        name = name.dropLastCharacters (1);
        offer.setButtonText (label (name.trimEnd() + ellipsis));
    }
    offer.setVisible (songName.isNotEmpty());
    resized();
    repaint();
}

void HarmonyDropScreen::setBestNote (bool show)
{
    if (show == bestNote) return;
    bestNote = show;
    resized();
    repaint();
}

void HarmonyDropScreen::setHighlighted (bool h)
{
    if (h != highlighted) { highlighted = h; repaint(); }
}

// Centred column like DropScreen's: circle 64, [offer 34], heading, copy, [note], Browse, "You'll get", chips.
float HarmonyDropScreen::columnTop() const
{
    const auto total = 64.0f + 14.0f + offerHeight() + Fonts::display (24.0f).getHeight() + 10.0f + Fonts::body (14.0f).getHeight()
                     + noteHeight() + 14.0f + 34.0f + 14.0f + Fonts::body (12.0f).getHeight() + 8.0f + 28.0f;
    return 17.0f + (366.0f - total) / 2.0f;
}

void HarmonyDropScreen::resized()
{
    const auto top = columnTop() + 64.0f + 14.0f;
    const auto ow = offer.preferredWidth();
    offer.setBounds (juce::roundToInt (centreX - (float) ow / 2.0f), juce::roundToInt (top), ow, offer.height);
    const auto bw = browse.preferredWidth();
    browse.setBounds (juce::roundToInt (centreX - (float) bw / 2.0f),
                      juce::roundToInt (top + offerHeight() + Fonts::display (24.0f).getHeight() + 10.0f + Fonts::body (14.0f).getHeight()
                                        + noteHeight() + 14.0f), bw, browse.height);
}

void HarmonyDropScreen::paint (juce::Graphics& g)
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
    strokeIcon (g, Icons::choir, { centreX - 16.0f, y + 16.0f, 32.0f, 32.0f }, 1.8f);
    y += 64.0f + 14.0f + offerHeight();

    const auto titleFont = Fonts::display (24.0f);
    text (g, titleFont, Theme::cream, "Drop a song here", { 0.0f, y, panelW, titleFont.getHeight() });
    y += titleFont.getHeight() + 10.0f;

    const auto body = Fonts::body (14.0f), small = Fonts::body (12.0f);
    text (g, body, Theme::cream.withAlpha (0.72f), "LISN finds the vocals, then splits the lead from the harmonies.",
          { 0.0f, y, panelW, body.getHeight() });
    y += body.getHeight();
    if (bestNote)
    {
        text (g, small, Theme::cream.withAlpha (0.68f), "Best quality downloads its 900 MB model the first time.",
              { 0.0f, y + 6.0f, panelW, small.getHeight() });
        y += noteHeight();
    }
    y += 14.0f + 34.0f + 14.0f;

    text (g, small, Theme::cream.withAlpha (0.62f), "You'll get", { 0.0f, y, panelW, small.getHeight() });
    y += small.getHeight() + 8.0f;
    chipRow (g, y, { "Lead", "Harmonies" }, { theme.vocals, theme.guitar });
}

//==============================================================================
namespace
{
    constexpr float stepsW = 580.0f, stepRow = 40.0f, stepGap = 10.0f;
}

HarmonySplittingScreen::HarmonySplittingScreen() : cancel ("Cancel", LisnButton::Style::Ghost)
{
    cancel.height = 36;
    cancel.padLeft = cancel.padRight = 18.0f;
    cancel.borderAlpha = 0.3f;
    cancel.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    addAndMakeVisible (cancel);
}

void HarmonySplittingScreen::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void HarmonySplittingScreen::setSong (const juce::String& name)
{
    song = name;
    repaint();
}

void HarmonySplittingScreen::setStep (int s, float progress, bool isDownloading)
{
    s = juce::jlimit (1, 2, s);
    const auto pc = juce::jlimit (0, 100, (int) (progress * 100.0f + 1e-4f));
    if (s == step && pc == percent && isDownloading == downloading) return;
    step = s;
    percent = pc;
    downloading = isDownloading;
    repaint();
}

juce::String HarmonySplittingScreen::stepLabel (int s) const
{
    if (s == 1) return "Finding the vocals";
    return downloading && step == 2 ? "Downloading the Best quality model" : "Splitting the harmonies";
}

// Centred column like SplittingScreen's: the song (Bold 15), two 40 px rows 10 apart, Cancel (36), 18 / 24 px between.
float HarmonySplittingScreen::columnTop() const
{
    const auto total = Fonts::body (15.0f, 700).getHeight() + 18.0f + 2.0f * stepRow + stepGap + 24.0f + 36.0f;
    return 1.0f + (398.0f - total) / 2.0f;
}

void HarmonySplittingScreen::resized()
{
    const auto y = columnTop() + Fonts::body (15.0f, 700).getHeight() + 18.0f + 2.0f * stepRow + stepGap + 24.0f;
    const auto w = cancel.preferredWidth();
    cancel.setBounds (juce::roundToInt (centreX - (float) w / 2.0f), juce::roundToInt (y), w, cancel.height);
}

void HarmonySplittingScreen::paint (juce::Graphics& g)
{
    const auto nameFont = Fonts::body (15.0f, 700);
    auto y = columnTop();
    text (g, nameFont, Theme::cream, song, { 0.0f, y, panelW, nameFont.getHeight() });
    y += nameFont.getHeight() + 18.0f;

    // Each row: a 24 px circle (the step's number, a tick once done; gold from the current step on), the label, a 6 px bar in
    // Harmonies' colour and, on the current step, its percentage.
    const auto left = centreX - stepsW / 2.0f;
    for (int i = 1; i <= 2; ++i)
    {
        const auto done = i < step, current = i == step;
        const auto circle = juce::Rectangle<float> (left, y + (stepRow - 24.0f) / 2.0f, 24.0f, 24.0f);
        g.setColour (done || current ? theme.guitar : Theme::cream.withAlpha (0.12f));
        g.fillEllipse (circle);
        if (done)
        {
            g.setColour (Theme::ink);
            strokeIcon (g, "M7 12.5l3.5 3.5 6.5-7", circle, 2.2f);
        }
        else
            text (g, Fonts::body (12.0f, 700), current ? Theme::ink : Theme::cream.withAlpha (0.7f), juce::String (i), circle);

        text (g, Fonts::body (14.0f, current ? 700 : 500), Theme::cream.withAlpha (current ? 1.0f : done ? 0.9f : 0.5f), stepLabel (i),
              { left + 36.0f, y, 262.0f, stepRow }, juce::Justification::centredLeft);

        const juce::Rectangle<float> bar (left + 310.0f, y + (stepRow - 6.0f) / 2.0f, stepsW - 310.0f - 58.0f, 6.0f);
        g.setColour (Theme::cream.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, 3.0f);
        const auto filled = done ? 1.0f : current ? (float) percent / 100.0f : 0.0f;
        if (filled > 0.0f)
        {
            g.setColour (theme.guitar);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * filled)), 3.0f);
        }
        if (current)
            text (g, Fonts::body (13.0f, 700), Theme::cream, percentText(), { bar.getRight() + 8.0f, y, 50.0f, stepRow },
                  juce::Justification::centredLeft);
        y += stepRow + stepGap;
    }
}

//==============================================================================
// Panel padding 16 18: content (19, 17, 674, 366). Song row 36, divider at 63, rows from 74 (4 rows end at 74+4*56+3*8=322, 6 at 74+6*36+5*6=320),
// zoom position strip at 326, divider at 332, speed strip at 343.

namespace
{
    constexpr int waveWidth = 376;                        // the waveforms; their left edge is StemsScreen::waveLeft()
    constexpr float stripY = 326.0f;                      // the position strip (4 tall) while zoomed, between the last row and the divider

    // The speed strip's reset menu (board 5B): the theme's panel tint at 0.94 (PopupMenu::backgroundColourId), radius 10, a
    // 1 px cream@0.16 border and 4 px padding around one 32 px item: 10 px, the 14 px reset icon, 9 px, Medium 13, 28 px (the
    // mockup's menu is 176 wide for "Reset to 120 BPM"). Hovered, the item is cream@0.10 (radius 7); disabled, its icon and
    // text are at 0.38. No shadow: the menu is a child of the plugin window and could only draw one inside its own bounds.
    constexpr int menuBorder = 5;

    struct ResetMenuLook : juce::LookAndFeel_V4
    {
        explicit ResetMenuLook (const char* d = Icons::reset) : icon (d) {}
        const char* icon;

        int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return menuBorder; }
        void drawResizableFrame (juce::Graphics&, int, int, const juce::BorderSize<int>&) override {}   // PopupMenu's frame for a menu in a parent

        void getIdealPopupMenuItemSizeWithOptions (const juce::String& s, bool, int, int& w, int& h, const juce::PopupMenu::Options&) override
        {
            w = (int) std::ceil (10.0f + 14.0f + 9.0f + juce::GlyphArrangement::getStringWidth (Fonts::body (13.0f, 500), s) + 28.0f);
            h = 32;
        }

        void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int w, int h, const juce::PopupMenu::Options&) override
        {
            const auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);
            g.setColour (findColour (juce::PopupMenu::backgroundColourId));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (Theme::cream.withAlpha (0.16f));
            g.drawRoundedRectangle (r, 10.0f, 1.0f);
        }

        // The item spans the menu's width (PopupMenu insets items by the border only vertically), so inset it here.
        void drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted,
                                           const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&) override
        {
            const auto r = area.toFloat().reduced ((float) menuBorder, 0.0f);
            if (highlighted)                         // never for a disabled item
            {
                g.setColour (Theme::cream.withAlpha (0.1f));
                g.fillRoundedRectangle (r, 7.0f);
            }
            const auto c = item.isTicked ? findColour (juce::PopupMenu::highlightedTextColourId)   // the current choice: accent
                                         : Theme::cream.withAlpha (item.isEnabled ? 1.0f : 0.38f);
            g.setColour (c);
            strokeIcon (g, icon, { r.getX() + 10.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f }, 2.2f);
            text (g, Fonts::body (13.0f, 500), c, item.text, r.withLeft (r.getX() + 33.0f), juce::Justification::centredLeft);
        }
    };
}

//==============================================================================
SpeedStrip::SpeedStrip() : menuLook (std::make_unique<ResetMenuLook>())
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setTheme (themeFor ("dusk"));
}

SpeedStrip::~SpeedStrip()
{
    juce::PopupMenu::dismissAllActiveMenus();        // an open reset menu draws with menuLook; its window goes later, asynchronously
}

void SpeedStrip::setTheme (const Theme& t)
{
    menuLook->setColour (juce::PopupMenu::backgroundColourId, t.panelTint.withAlpha (0.94f));
}

juce::PopupMenu SpeedStrip::resetMenu (Target t)
{
    const auto tempo = t == Target::tempo;
    const auto label = ! tempo ? juce::String ("Reset to +0 st")
                     : bpm > 0.0 ? "Reset to " + juce::String (juce::roundToInt (bpm)) + " BPM"
                     : "Reset to 1.00" + times;
    juce::PopupMenu menu;
    menu.setLookAndFeel (menuLook.get());
    menu.addItem (label, tempo ? ! juce::exactlyEqual (speed, 1.0) : pitch != 0, false, [safe = SafePointer<SpeedStrip> (this), tempo]
    {
        if (safe == nullptr) return;
        if (tempo) safe->set (1.0, true);
        else       safe->setKey (0, true);
    });
    return menu;
}

void SpeedStrip::setSpeed (double v)
{
    set (juce::jlimit (0.5, 2.0, v), false);
}

void SpeedStrip::setPitch (int st)
{
    setKey (juce::jlimit (-12, 12, st), false);
}

void SpeedStrip::setBpm (double b)
{
    if (juce::exactlyEqual (b, bpm)) return;
    bpm = b;
    repaint();
}

double SpeedStrip::stepSpeed (double from, int steps, double bpm)
{
    if (bpm > 0.0)
        return juce::jlimit (0.5, 2.0, (double) (juce::roundToInt (bpm * from) + steps) / bpm);
    return juce::jlimit (0.5, 2.0, std::round (from * 20.0 + steps) / 20.0);
}

void SpeedStrip::setKey (int st, bool report)
{
    if (st == pitch) return;
    pitch = st;
    repaint();
    if (report && onPitch != nullptr) onPitch (st);
}

void SpeedStrip::set (double v, bool report)
{
    if (juce::exactlyEqual (v, speed)) return;
    speed = v;
    repaint();
    if (report && onChange != nullptr) onChange (v);
}

void SpeedStrip::paintBox (juce::Graphics& g, juce::Rectangle<int> box, const char* icon, const juce::String& value, const juce::String& caption)
{
    if (! g.clipRegionIntersects (box)) return;
    const auto r = box.toFloat().reduced (0.5f);
    g.setColour (Theme::cream.withAlpha (0.06f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::cream.withAlpha (0.22f));
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    g.setColour (Theme::cream);
    strokeIcon (g, icon, { r.getX() + 10.0f, r.getCentreY() - 8.0f, 16.0f, 16.0f }, 2.0f);
    const auto tx = r.getX() + 34.0f, tw = r.getRight() - 28.0f - tx;
    text (g, Fonts::body (14.0f, 700), Theme::cream, value, { tx, r.getCentreY() - 13.0f, tw, 15.0f }, juce::Justification::centredLeft);
    text (g, Fonts::body (10.0f), Theme::cream.withAlpha (0.55f), caption, { tx, r.getCentreY() + 3.0f, tw, 11.0f }, juce::Justification::centredLeft);

    // The chevron column: a 1 px divider, then up over down.
    const auto cx = r.getRight() - 14.0f;
    g.setColour (Theme::cream.withAlpha (0.14f));
    g.fillRect (r.getRight() - 27.0f, r.getY() + 6.0f, 1.0f, r.getHeight() - 12.0f);
    g.setColour (Theme::cream.withAlpha (0.75f));
    strokeIcon (g, "M7 14l5-5 5 5", { cx - 7.0f, r.getCentreY() - 15.0f, 14.0f, 14.0f }, 2.0f);
    strokeIcon (g, "M7 10l5 5 5-5", { cx - 7.0f, r.getCentreY() + 1.0f, 14.0f, 14.0f }, 2.0f);
}

void SpeedStrip::paint (juce::Graphics& g)
{
    const auto speedText = juce::String (speed, 2) + times;
    if (bpm > 0.0)
        paintBox (g, tempoBox(), Icons::speed, juce::String (juce::roundToInt (bpm * speed)) + " BPM",
                  speedText + " of " + juce::String (juce::roundToInt (bpm)));
    else
        paintBox (g, tempoBox(), Icons::speed, speedText, "Speed");
    paintBox (g, keyBox(), Icons::note, (pitch >= 0 ? "+" : "") + juce::String (pitch) + " st", "Key");
}

SpeedStrip::Target SpeedStrip::targetAt (juce::Point<int> p) const
{
    return tempoBox().contains (p) ? Target::tempo : keyBox().contains (p) ? Target::key : Target::none;
}

void SpeedStrip::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (targetAt (e.getPosition()) != Target::none ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void SpeedStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())                        // a right-click never drags or steps
    {
        dragging = Target::none;
        const auto t = targetAt (e.getPosition());
        if (t == Target::none) return;
        // The menu's top-left at the pointer, inside the window. JUCE opens a menu above a point low in its parent, so the
        // target runs from the window's top (inside the menu's border) down to the pointer: the menu goes under it.
        auto* top = getTopLevelComponent();
        const auto p = top->getLocalPoint (this, e.getPosition());
        const auto target = juce::Rectangle<int>::leftTopRightBottom (p.x, menuBorder, p.x + 1, p.y);
        resetMenu (t).showMenuAsync (juce::PopupMenu::Options().withParentComponent (top).withTargetScreenArea (top->localAreaToGlobal (target)));
        return;
    }
    dragging = targetAt (e.getPosition());
    dragFromSpeed = speed;
    dragFromPitch = pitch;
    moved = false;
}

void SpeedStrip::mouseDrag (const juce::MouseEvent& e)
{
    const auto steps = -e.getDistanceFromDragStartY() / 4;   // 4 px a step, up is more
    if (steps != 0) moved = true;
    if (dragging == Target::tempo) set (stepSpeed (dragFromSpeed, steps, bpm), true);
    if (dragging == Target::key)   setKey (juce::jlimit (-12, 12, dragFromPitch + steps), true);
}

void SpeedStrip::mouseUp (const juce::MouseEvent& e)
{
    // A click (no drag) on a box's chevron column steps it: the upper half up, the lower half down.
    const auto box = dragging == Target::tempo ? tempoBox() : keyBox();
    if (dragging != Target::none && ! moved && e.getNumberOfClicks() == 1 && e.x >= box.getRight() - 27)
    {
        const int step = e.y < box.getCentreY() ? 1 : -1;
        if (dragging == Target::tempo) set (stepSpeed (speed, step, bpm), true);
        else                           setKey (juce::jlimit (-12, 12, pitch + step), true);
    }
    dragging = Target::none;
}

void SpeedStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = targetAt (e.getPosition());
    if (t == Target::tempo && e.x < tempoBox().getRight() - 27) set (1.0, true);
    if (t == Target::key && e.x < keyBox().getRight() - 27)     setKey (0, true);
}

void StemsScreen::setSpeed (double v)
{
    speedStrip.setSpeed (v);
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
    harmonyButton.icon = Icons::choir;              // icon only, 34 x 34 (setBounds): the border and 7 px put its 18 px in the middle
    harmonyButton.iconSize = 18.0f;
    harmonyButton.iconStroke = 1.8f;
    harmonyButton.padLeft = 7.0f;
    harmonyButton.setTooltip ("Harmonies");
    harmonyButton.onClick = [this] { if (onHarmony != nullptr) onHarmony(); };
    addChildComponent (harmonyButton);
    dragChip.fileToDrag = [this] { return mixFile != nullptr ? mixFile() : juce::File(); };
    addAndMakeVisible (loopButton);
    addAndMakeVisible (newSong);
    addAndMakeVisible (playButton);
    addAndMakeVisible (speedStrip);
    addAndMakeVisible (dragChip);                    // after the strip: the chip sits inside its bounds
    speedStrip.onChange = [this] (double s) { if (onSpeed != nullptr) onSpeed (s); };
    speedStrip.onPitch = [this] (int st) { if (onPitch != nullptr) onPitch (st); };
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
    speedStrip.setTheme (t);
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
    speedStrip.setBpm (0.0);
    playButton.setPlaying (false);
    rows.clear();

    auto files = SeparationJob::stemsIn (dir);   // A-Z
    // ponytail: 4-stem splits only; 6 stems + 2 don't fit the panel. A taller panel or a scroll if 6-stem mode wants it.
    harmonyFits = files.size() == 4 && dir.getChildFile ("vocals.wav").existsAsFile();
    harmonyButton.setVisible (harmonyFits && songAvailable);
    std::stable_sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return stemRank (a) < stemRank (b); });
    for (int i = 0; i < files.size(); ++i)
    {
        auto* row = rows.add (new StemRow (files[i], files[i].getFileNameWithoutExtension().toLowerCase()));
        row->setTheme (theme);
        row->setCompact (files.size() > 4);
        row->onToggleMute = [this, i] { if (onToggleMute != nullptr) onToggleMute (i); };
        row->onSolo = [this, i] { if (onSolo != nullptr) onSolo (i); };
        row->onVolume = [this, i] (float v) { if (onVolume != nullptr) onVolume (i, v); };
        row->onWaveMouse = [this] (WaveMouse m, double f, bool alt, bool ctrl) { waveMouse (m, f, alt, ctrl); };
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

int StemsScreen::waveLeft() const   // rows sit at x 19; the waveform is at row x 194 (186 in 6-stem rows)
{
    return 19 + (rows.isEmpty() ? 194 : rows.getFirst()->waveform().getX());
}

juce::Rectangle<int> StemsScreen::stripArea() const   // with the loop ticks
{
    return { waveLeft() - 2, 323, waveWidth + 4, 10 };
}

int StemsScreen::headX (double f) const
{
    return waveLeft() + juce::roundToInt ((f - view.getStart()) / view.getLength() * waveWidth);
}

double StemsScreen::fractionAt (float x) const
{
    return view.getStart() + (x - (float) waveLeft()) / (double) waveWidth * view.getLength();
}

juce::Rectangle<int> StemsScreen::waveSpanArea() const   // the waveforms plus the tabs above them
{
    const auto span = waveSpan();
    return { waveLeft() - 9, span.getStart() - 13, waveWidth + 18, span.getLength() + 13 };
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
    speedStrip.setBpm (t.bpm);                       // the Tempo box shows it
    repaint (19 + 36 + 12, 37, 674 - 36 - 12, 16);   // the subtitle's loop beats
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

void StemsScreen::waveMouse (WaveMouse m, double f, bool free, bool ctrl)
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
            newLoop = grab < 0 && ctrl;
            if (grab < 0 && onSeek != nullptr) onSeek (juce::jmin (f, 0.999));
            break;
        case WaveMouse::drag:
            // 4 px or more: a trim, a new loop or a scrub. Less is a click, whatever the mouse jitters (JUCE sends a drag for
            // any move, sub-pixel at 125-150 % DPI). ponytail: a handle dragged to the view's edge stops there, no
            // autoscroll. Upgrade path: scroll the view while a handle is held at an edge.
            if (! dragging && std::abs (x - downX) < 4)
                break;
            dragging = true;
            if (grab < 0 && ! newLoop)
            {
                if (onSeek != nullptr) onSeek (juce::jmin (f, 0.999));
                break;
            }
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
        waveMouse (m, fractionAt ((float) juce::jlimit (waveLeft(), waveLeft() + waveWidth, e.x)), e.mods.isAltDown(),
                   e.mods.isCtrlDown());
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
    repaint (stripArea());                             // the strip's loop ticks
    loopButton.tint = loopOn ? std::optional<juce::Colour> (theme.accent()) : std::nullopt;
    loopButton.repaint();
    setMixChip (loopOn ? "Drag loop" : "Drag mix", std::nullopt);
}

void StemsScreen::setMixChip (const juce::String& text, std::optional<float> progress)
{
    dragChip.setProgress (progress);
    if (text == dragChip.getText()) return;
    dragChip.setText (text);
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
                              .getIntersection ({ waveLeft() - 2, span.getStart(), waveWidth + 4, span.getLength() });
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

void StemsScreen::setVolume (int i, float v)
{
    if (juce::isPositiveAndBelow (i, rows.size())) rows[i]->setVolume (v);
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
    if (rows.isEmpty() || p.x < (float) waveLeft() || p.x > (float) (waveLeft() + waveWidth)
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
    const int right = waveLeft() + waveWidth;

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
        const auto a = juce::jlimit (waveLeft(), right, x0), b = juce::jlimit (waveLeft(), right, x1);
        g.setColour (Theme::cream.withAlpha (0.10f));
        g.fillRect ((float) a, top, (float) (b - a), h);
        for (int edge = 0; edge < 2; ++edge)
        {
            const auto x = (float) (edge == 0 ? x0 : x1);
            if (x < (float) waveLeft() || x > (float) right) continue;
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

    if (! hasHead || lastHead < waveLeft() || lastHead > right) return;   // no playhead yet, or it's outside the view
    g.setColour (Theme::cream);
    g.fillRoundedRectangle ((float) lastHead - 1.0f, top, 2.0f, h, 1.0f);
}

void StemsScreen::resized()
{
    playButton.setBounds (19, 17, 36, 36);
    const auto w = newSong.preferredWidth();
    newSong.setBounds (19 + 674 - w, 18, w, newSong.height);
    // While preparing, the chip keeps its widest width so the changing percent doesn't nudge the Loop button.
    const auto chipW = dragChipWidth (dragChip.isPreparing() ? juce::String ("Preparing 100%") : dragChip.getText());
    loopButton.setBounds (newSong.getX() - 8 - loopButton.preferredWidth(), 18, loopButton.preferredWidth(), loopButton.height);
    harmonyButton.setBounds (loopButton.getX() - 8 - 34, 18, 34, 34);
    speedStrip.setBounds (19 + 10, 339, 674 - 20, 40);                               // board 5B: Tempo and Key boxes...
    dragChip.setBounds (19 + 674 - 10 - chipW, 339 + 2, chipW, 36);                  // ...and the mix chip at the right end
    auto y = 74;
    for (auto* r : rows)
    {
        r->setBounds (19, y, 674, r->getHeight());
        y += r->getHeight() + (rows.size() > 4 ? 6 : 8);
    }
}

void StemsScreen::setSongAvailable (bool on)
{
    songAvailable = on;
    if (harmonyButton.isVisible() == (harmonyFits && on)) return;
    harmonyButton.setVisible (harmonyFits && on);
    repaint (0, 0, getWidth(), 60);                  // the title's width follows the icon
}

void StemsScreen::paint (juce::Graphics& g)
{
    // Playhead repaints only touch the rows: skip the path work outside the clip.
    if (g.clipRegionIntersects ({ 19, 17, 674, 36 }))
    {
        // Song name (Bold 15) over the time (12, cream 0.64): line-height 1.2, 2 px apart, centred in the 36 px row.
        const auto x = 19.0f + 36.0f + 12.0f, w = (float) (harmonyButton.isVisible() ? harmonyButton : loopButton).getX() - 12.0f - x, y = 17.0f + (36.0f - 34.4f) / 2.0f;
        text (g, Fonts::body (15.0f, 700), Theme::cream, song, { x, y, w, 18.0f }, juce::Justification::centredLeft);
        auto subtitle = timeText;
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
    if (g.clipRegionIntersects ({ 19, 332, 674, 1 })) g.fillRect (19.0f, 332.0f, 674.0f, 1.0f);

    // Position strip (only while zoomed): where the view sits in the song, the loop edges in the accent colour.
    if (view.getLength() < 1.0 && g.clipRegionIntersects (stripArea()))
    {
        g.setColour (Theme::cream.withAlpha (0.10f));
        g.fillRoundedRectangle ((float) waveLeft(), stripY, (float) waveWidth, 4.0f, 2.0f);
        g.setColour (Theme::cream.withAlpha (0.55f));
        g.fillRoundedRectangle ((float) waveLeft() + (float) view.getStart() * waveWidth, stripY,
                                juce::jmax (2.0f, (float) view.getLength() * waveWidth), 4.0f, 2.0f);
        if (loopOn)
        {
            g.setColour (theme.accent());
            for (const auto f : { loopRange.getStart(), loopRange.getEnd() })
                g.fillRect ((float) waveLeft() + (float) f * waveWidth - 1.0f, stripY - 2.0f, 2.0f, 8.0f);
        }
    }
}

//==============================================================================
// A 520 px column centred in the panel (x 96), 16 px gaps: icon + title (44), body, code box, 4 px, buttons (38).
ErrorScreen::ErrorScreen()
    : copy ("Copy", LisnButton::Style::Soft), retry ("Try again", LisnButton::Style::Primary),
     #if JUCE_MAC
      findPython (juce::String (juce::CharPointer_UTF8 ("Already installed? Find python3\xe2\x80\xa6")), LisnButton::Style::Ghost)
     #else
      findPython (juce::String (juce::CharPointer_UTF8 ("Already installed? Find python.exe\xe2\x80\xa6")), LisnButton::Style::Ghost)
     #endif
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
    restart = false;
    const juce::String pip ("python -m pip install demucs");
    lines.clear();
   #if JUCE_MAC
    if (kind != ErrorKind::Failed)   // the pkg sets the engine up: nothing to type in a Terminal, just run it again
    {
        title = "The AI engine isn't set up yet";
        body = "LISN's AI engine isn't installed. Run LISN-Mac.pkg again (it needs the internet for a few minutes). "
               "If it keeps failing, the installer's log is here:";
        command = copyText = "/Library/Application Support/LISN/engine/setup.log";
    }
    else
   #endif
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

void ErrorScreen::setRestart (const juce::String& host)
{
    setError (ErrorKind::Failed, {}, {});
    restart = true;
    title = "Restart " + host + " to finish the update";
    body = "The update changed LISN's AI engine. Splits and Harmonies work again once " + host + " restarts.";
    lines.clear();
    resized();
    repaint();
}

void ErrorScreen::resized()
{
    const bool failed = kind == ErrorKind::Failed;
    bodyHeight = (float) lineCount (Fonts::body (14.0f), body, 520.0f) * 14.0f * 1.55f;
    const auto boxH = failed ? 20.0f + 18.0f * (float) lines.size() : 46.0f;
    top = 1.0f + (398.0f - (44.0f + 16.0f + bodyHeight + 16.0f + boxH + 16.0f + 4.0f + 38.0f)) / 2.0f;
    if (restart)
        top = 1.0f + (398.0f - (44.0f + 16.0f + bodyHeight)) / 2.0f;
    box = { 96.0f, top + 44.0f + 16.0f + bodyHeight + 16.0f, 520.0f, boxH };

    const auto copyW = copy.preferredWidth();   // 1 px border + 6 px padding from the box edge
    copy.setBounds (juce::roundToInt (box.getRight() - 7.0f) - copyW,
                    juce::roundToInt (failed ? box.getY() + 7.0f : box.getCentreY() - 16.0f), copyW, copy.height);
    const auto y = juce::roundToInt (box.getBottom() + 20.0f);
    retry.setBounds (96, y, retry.preferredWidth(), retry.height);
    findPython.setBounds (retry.getRight() + 10, y, findPython.preferredWidth(), findPython.height);
    findPython.setVisible (! failed);
    copy.setVisible (! restart);
    retry.setVisible (! restart);
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
    if (restart)
        return;

    g.setColour (juce::Colour (0xff0C060A).withAlpha (0.55f));
    g.fillRoundedRectangle (box, 12.0f);
    g.setColour (Theme::cream.withAlpha (0.12f));
    g.drawRoundedRectangle (box.reduced (0.5f), 11.5f, 1.0f);

    const auto x = box.getX() + 17.0f, w = (float) copy.getX() - 12.0f - x;
   #if JUCE_MAC
    const auto monoSize = 12.5f;   // the log path is long
   #else
    const auto monoSize = 14.0f;
   #endif
    if (kind == ErrorKind::Failed)
        for (int i = 0; i < lines.size(); ++i)
            text (g, Fonts::mono (14.0f), Theme::cream, lines[i], { x, box.getY() + 10.0f + 18.0f * (float) i, w, 18.0f },
                  juce::Justification::centredLeft);
    else
        text (g, Fonts::mono (monoSize), Theme::cream, command, { x, box.getY(), w, box.getHeight() }, juce::Justification::centredLeft);
}

//==============================================================================
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
    const char* const oneToSix = "1\xe2\x80\x93" "6";   // "1-6" with an en dash

    const std::vector<std::vector<Tip>> helpTips {
        { { Icons::play, "Play or pause", "Every stem plays together, in sync", { key ("Space") } },
          { Icons::jump, "Jump and scrub", "Click the waves to jump there, drag to scrub", { mouse ("Click"), mouse ("Drag") } },
          { Icons::home, "Back to the start", "Back to 0:00, or to the loop's start while looping", { key ("Home") } },
          { Icons::speed, "Tempo and key", "Drag either box up or down; double-click it to reset", { mouse ("Tempo"), orWord, mouse ("Key") } } },
        { { Icons::file, "Split a song", "Drop it on the panel or click Browse. 6 stems adds guitar and piano", { mouse ("Drop") } },
          { Icons::speakerOff, "Mute a stem", "Click its light; stems 1 to 6 go top to bottom", { mouse ("Light"), orWord, key (oneToSix) } },
          { Icons::speaker, "Solo a stem", "Only that stem plays; do it again to hear them all",
            { mouse ("Right-click"), orWord, key ("Shift"), plusWord, key (oneToSix) } },
          { Icons::volume, "Set a stem's volume", "Drag the bar under its name; double-click for 100%", { mouse ("Drag bar") } } },
        { { Icons::loop, "Make a loop", "Ctrl+drag across the waves; the edges snap to the beat", { key ("Ctrl"), plusWord, mouse ("Drag") } },
          { Icons::trim, "Trim it", "Drag either handle; hold Alt to go off the grid",
            { mouse ("Handle"), orWord, key ("Alt"), plusWord, mouse ("Drag") } },
          { Icons::toggle, "Loop on or off", "With no loop yet, you get 4 bars at the playhead", { key ("L"), orWord, mouse ("Loop") } },
          { Icons::zoom, "Zoom for finer steps", "Zoom around the mouse; the grid goes down to quarter beats", { key ("Ctrl"), plusWord, mouse ("Wheel") } } },
        { { Icons::grip, "Drag a stem", "Drag its row onto any track in FL", { mouse ("Drag row") } },
          { Icons::layers, "Drag the mix", "Every stem you hear, with its volume and the speed, as one WAV", { mouse ("Drag mix") } },
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

    // The tab's four tips share y 74-383, 8 px apart.
    const auto tipH = (383.0f - 74.0f - 3.0f * 8.0f) / 4.0f;
    const auto& tips = helpTips[(size_t) getTab()];
    for (size_t i = 0; i < tips.size(); ++i)
        paintTip (g, tips[i], { 19.0f, 74.0f + (float) i * (tipH + 8.0f), 674.0f, tipH });
}

//==============================================================================
void Fader::setValue (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (juce::exactlyEqual (v, value)) return;
    value = v;
    repaint();
}

void Fader::drag (const juce::MouseEvent& e)
{
    const auto top = 8.0f, bottom = (float) getHeight() - 8.0f;
    setValue ((bottom - e.position.y) / (bottom - top));
    if (onChange != nullptr) onChange (value);
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    setValue (1.0f);
    if (onChange != nullptr) onChange (value);
}

void Fader::paint (juce::Graphics& g)
{
    const auto cx = (float) getWidth() / 2.0f, top = 8.0f, bottom = (float) getHeight() - 8.0f;
    const auto y = bottom - value * (bottom - top);
    g.setColour (Theme::cream.withAlpha (0.14f));
    g.fillRoundedRectangle (cx - 2.0f, top, 4.0f, bottom - top, 2.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (cx - 2.0f, y, 4.0f, bottom - y, 2.0f);
    g.setColour (Theme::cream);
    g.fillEllipse (cx - 8.0f, y - 8.0f, 16.0f, 16.0f);
}

//==============================================================================
HarmonyScreen::HarmonyScreen()
{
    playButton.onClick = [this] { if (onPlayPause != nullptr) onPlayPause(); };
    back.icon = Icons::back;
    back.padLeft = 10.0f;
    back.padRight = 14.0f;
    back.onClick = [this] { if (onBack != nullptr) onBack(); };
    newSong.icon = Icons::plus;                    // as the stems screen's New song
    newSong.padLeft = 10.0f;
    newSong.padRight = 14.0f;
    newSong.onClick = [this] { if (onNewSong != nullptr) onNewSong(); };
    both.setText ("Drag both");
    both.fileToDrag = [this] { return mixFile != nullptr ? mixFile() : juce::File(); };
    for (int i = 0; i < 2; ++i)
    {
        auto& l = lanes[i];
        for (auto* b : { &l.solo, &l.mute })
        {
            b->height = 26;
            b->corner = 13.0f;
            b->fontSize = 12.0f;
            b->padLeft = b->padRight = 10.0f;
            addAndMakeVisible (b);
        }
        l.solo.onClick = [this, i] { if (onSolo != nullptr) onSolo (i); };
        l.mute.onClick = [this, i] { if (onToggleMute != nullptr) onToggleMute (i); };
        l.wave.setBars (60);
        l.wave.onMouse = [this] (WaveMouse m, double f, bool, bool)
        {
            if ((m == WaveMouse::down || m == WaveMouse::drag) && onSeek != nullptr)
                onSeek (juce::jlimit (0.0, 0.999, f));
        };
        l.drag.setText ("Drag");
        l.drag.fileToDrag = [this, i] { return files[i]; };
        l.fader.onChange = [this, i] (float v)
        {
            repaint (lanes[i].fader.getX() - 20, 78, 68, 22);   // its level above it
            if (onVolume != nullptr) onVolume (i, v);
        };
        addAndMakeVisible (l.wave);
        addAndMakeVisible (l.drag);
        addAndMakeVisible (l.fader);
    }
    addAndMakeVisible (playButton);
    addAndMakeVisible (back);
    addAndMakeVisible (newSong);
    addAndMakeVisible (both);
    setTheme (theme);
}

HarmonyScreen::~HarmonyScreen()
{
    pool.removeAllJobs (true, 2000);
}

void HarmonyScreen::setTheme (const Theme& t)
{
    theme = t;
    playButton.setFill (Theme::cream);
    for (int i = 0; i < 2; ++i)
    {
        lanes[i].wave.setStemColour (laneColour (i));
        lanes[i].fader.setColour (laneColour (i));
        if (lanes[i].solo.tint.has_value())
            lanes[i].solo.tint = laneColour (i);
    }
    repaint();
}

void HarmonyScreen::setSong (const juce::File& stemDir, const juce::String& songName, const juce::String& q)
{
    ++generation;
    pool.removeAllJobs (true, 2000);
    const auto dir = stemDir.getChildFile ("harmony");
    files = { dir.getChildFile ("lead.wav"), dir.getChildFile ("harmonies.wav") };
    song = songName;
    quality = q;
    peaksReady = false;
    length = 0.0;
    for (auto& l : lanes)
        l.wave.setEnvelope ({});
    repaint();

    const int gen = generation;
    pool.addJob ([safe = juce::Component::SafePointer<HarmonyScreen> (this), f = files, gen]
    {
        const std::function<bool()> shouldExit = [] { return juce::ThreadPoolJob::getCurrentThreadPoolJob()->shouldExit(); };
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::vector<std::vector<float>> envelopes;
        double seconds = 0.0;
        for (const auto& file : f)
        {
            if (shouldExit()) return;
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            envelopes.push_back (reader != nullptr ? readEnvelope (*reader, 100.0, shouldExit) : std::vector<float>());
            if (reader != nullptr && seconds == 0.0) seconds = lengthSeconds (*reader);
        }
        if (shouldExit()) return;
        juce::MessageManager::callAsync ([safe, gen, envelopes = std::move (envelopes), seconds]() mutable
        {
            if (safe == nullptr || safe->generation != gen) return;
            for (size_t i = 0; i < 2 && i < envelopes.size(); ++i)
                safe->lanes[i].wave.setEnvelope (std::move (envelopes[i]));
            if (safe->length == 0.0) safe->length = seconds;
            safe->peaksReady = true;
            safe->repaint();
        });
    });
}

void HarmonyScreen::setPlayback (bool isPlaying, double f, double lengthSeconds)
{
    if (isPlaying != playing)
    {
        playing = isPlaying;
        playButton.setPlaying (playing);
    }
    if (lengthSeconds > 0.0) length = lengthSeconds;
    fraction = f;
    const auto t = mmss (f * length) + " / " + mmss (length);
    if (t != timeText)
    {
        timeText = t;
        repaint (67, 17, 400, 36);
    }
    const auto& w = lanes[0].wave;
    const auto head = w.getX() + juce::roundToInt (fraction * w.getWidth());
    if (head != lastHead)
    {
        const auto top = laneArea (0).getY(), h = laneArea (1).getBottom() - top;
        if (lastHead >= 0) repaint (lastHead - 2, top, 4, h);
        repaint (head - 2, top, 4, h);
        lastHead = head;
    }
}

void HarmonyScreen::setVolume (int lane, float v)
{
    if (! juce::isPositiveAndBelow (lane, 2) || juce::exactlyEqual (lanes[lane].fader.getValue(), juce::jlimit (0.0f, 1.0f, v)))
        return;
    lanes[lane].fader.setValue (v);
    repaint (lanes[lane].fader.getX() - 20, 78, 68, 22);
}

void HarmonyScreen::setAudible (juce::uint32 mask)
{
    for (int i = 0; i < 2; ++i)
    {
        auto& l = lanes[i];
        const auto heard = ((mask >> i) & 1u) != 0;
        const auto soloed = mask == (1u << i);
        if (heard == l.audible && l.solo.tint.has_value() == soloed)
            continue;
        l.audible = heard;
        l.wave.setAlpha (heard ? 1.0f : 0.35f);
        l.solo.tint = soloed ? std::optional<juce::Colour> (laneColour (i)) : std::nullopt;
        l.mute.tint = heard ? std::nullopt : std::optional<juce::Colour> (Theme::cream);
        l.solo.repaint();
        l.mute.repaint();
        repaint (laneArea (i));
    }
}

void HarmonyScreen::resized()
{
    playButton.setBounds (19, 17, 36, 36);
    const auto bw = back.preferredWidth();
    back.setBounds (19 + 674 - bw, 18, bw, back.height);
    const auto nw = newSong.preferredWidth();
    newSong.setBounds (back.getX() - 8 - nw, 18, nw, newSong.height);
    for (int i = 0; i < 2; ++i)
    {
        auto& l = lanes[i];
        const auto r = laneArea (i);
        l.solo.setBounds (r.getX() + 16, r.getY() + 74, l.solo.preferredWidth(), l.solo.height);
        l.mute.setBounds (l.solo.getRight() + 6, r.getY() + 74, l.mute.preferredWidth(), l.mute.height);
        l.drag.setBounds (r.getRight() - 12 - 64, r.getCentreY() - 17, 64, 34);
        l.wave.setBounds (r.getX() + 134, r.getY() + 12, l.drag.getX() - 14 - (r.getX() + 134), 92);
        l.fader.setBounds (557 + 34 + i * 68 - 14, 102, 28, 180);
    }
    const auto dw = dragChipWidth ("Drag both");
    both.setBounds (19 + 674 - dw, 338, dw, 34);
    lastHead = -1;
}

void HarmonyScreen::paint (juce::Graphics& g)
{
    // Its own room: a plum (Midnight: navy) gradient over the whole panel.
    const auto dusk = theme.id != "midnight";
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (dusk ? 0xff45283A : 0xff20324F), 0.0f, 0.0f,
                                             juce::Colour (dusk ? 0xff2E1C27 : 0xff121B2E), b.getWidth() * 0.7f, b.getHeight(), false));
    g.fillRoundedRectangle (b, 22.0f);

    // The song name (Bold 15) over the time and where it came from (12, cream 0.64), as on the stems screen.
    const auto x = 67.0f, w = (float) newSong.getX() - 12.0f - x, y = 17.0f + (36.0f - 34.4f) / 2.0f;
    text (g, Fonts::body (15.0f, 700), Theme::cream, song, { x, y, w, 18.0f }, juce::Justification::centredLeft);
    text (g, Fonts::body (12.0f), Theme::cream.withAlpha (0.64f),
          timeText + dot + "split from Vocals, " + (quality == "best" ? "best quality" : "quick"),
          { x, y + 20.0f, w, 14.4f }, juce::Justification::centredLeft);

    const char* const names[] = { "Lead", "Harmonies" };
    const char* const hints[] = { "the main voice", "every voice behind it" };
    for (int i = 0; i < 2; ++i)
    {
        const auto r = laneArea (i).toFloat();
        g.setColour (Theme::cream.withAlpha (0.05f));
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (Theme::cream.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
        const auto a = lanes[i].audible ? 1.0f : 0.45f;
        text (g, Fonts::body (21.0f, 700), laneColour (i).withMultipliedAlpha (a), names[i],
              { r.getX() + 16.0f, r.getY() + 16.0f, 118.0f, 26.0f }, juce::Justification::centredLeft);
        text (g, Fonts::body (12.0f), Theme::cream.withAlpha (0.64f * a), hints[i],
              { r.getX() + 16.0f, r.getY() + 44.0f, 118.0f, 16.0f }, juce::Justification::centredLeft);
    }

    // The fader column: a darker well, each level (17 bold, in its colour) above its fader and its name below.
    g.setColour (juce::Colour (0x4d140a10));
    g.fillRoundedRectangle (557.0f, 70.0f, 136.0f, 244.0f, 16.0f);
    for (int i = 0; i < 2; ++i)
    {
        const auto cx = (float) lanes[i].fader.getBounds().getCentreX();
        text (g, Fonts::body (17.0f, 700), laneColour (i), juce::String (juce::roundToInt (lanes[i].fader.getValue() * 100.0f)),
              { cx - 34.0f, 78.0f, 68.0f, 22.0f }, juce::Justification::centred);
        text (g, Fonts::body (12.0f, 700), Theme::cream, names[i], { cx - 34.0f, 286.0f, 68.0f, 18.0f }, juce::Justification::centred);
    }

    text (g, Fonts::body (13.0f), Theme::cream.withAlpha (0.72f),
          "Pull a fader down to hear more of the other voice. Drag a lane onto a track in FL.",
          { 19.0f, 336.0f, (float) both.getX() - 31.0f, 38.0f }, juce::Justification::centredLeft);
}

void HarmonyScreen::paintOverChildren (juce::Graphics& g)
{
    g.setColour (Theme::cream);
    for (const auto& l : lanes)
    {
        const auto x = (float) l.wave.getX() + (float) (fraction * l.wave.getWidth());
        g.fillRoundedRectangle (x - 1.0f, (float) l.wave.getY() + 2.0f, 2.0f, (float) l.wave.getHeight() - 4.0f, 1.0f);
    }
}

//==============================================================================
namespace
{
    constexpr float inset = 24.0f, columnX = inset + 365.0f + inset, columnW = panelW - inset - columnX;   // 413, 275
    constexpr float bodyY = 92.0f, buttonY = 400.0f - inset - 34.0f;

    // "**Harmonies**: split ..." with the ** runs in Bold.
    juce::TextLayout noteLayout (const juce::String& bullet, float width)
    {
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        auto rest = bullet;
        bool bold = false;
        while (rest.isNotEmpty())
        {
            const auto run = rest.upToFirstOccurrenceOf ("**", false, false);
            if (run.isNotEmpty())
                a.append (run, bold ? Fonts::body (14.0f, 700) : Fonts::body (14.0f), bold ? Theme::cream : Theme::cream.withAlpha (0.82f));
            rest = rest.fromFirstOccurrenceOf ("**", false, false);
            bold = ! bold;
        }
        juce::TextLayout t;
        t.createLayout (a, width);
        return t;
    }
}

UpdateScreen::UpdateScreen()
    : symbol (juce::Drawable::createFromImageData (BinaryData::lisnsymbolglass_svg, (size_t) BinaryData::lisnsymbolglass_svgSize)),
      later ("Later", LisnButton::Style::Ghost), primary ("Update now", LisnButton::Style::Primary),
      cancel ("Cancel", LisnButton::Style::Ghost), website ("Download from the website", LisnButton::Style::Ghost)
{
    cancel.borderAlpha = website.borderAlpha = 0.3f;
    later.onClick = [this] { if (onLater != nullptr) onLater(); };
    cancel.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    website.onClick = [this] { if (onWebsite != nullptr) onWebsite(); };
    primary.onClick = [this]
    {
        const auto call = [] (const std::function<void()>& f) { if (f != nullptr) f(); };
        switch (view)
        {
            case View::whatsNew:       call (onUpdate); break;
            case View::failedDownload:
            case View::failedInstall:  call (onRetry); break;
            default:                   call (onInstall); break;   // ready, inUse, engine
        }
    };
    for (auto* b : { &later, &primary, &cancel, &website })
        addChildComponent (b);
    more.setButtonText (ellipsis + "and more");
    more.setFont (Fonts::body (14.0f), false, juce::Justification::centredLeft);
    more.setWantsKeyboardFocus (false);
    more.setMouseClickGrabsKeyboardFocus (false);
    addChildComponent (more);
    setTheme (theme);
    setBufferedToImage (true);                       // the waves move behind it every motion frame
    refresh();
}

void UpdateScreen::setTheme (const Theme& t)
{
    theme = t;
    more.setColour (juce::HyperlinkButton::textColourId, t.guitar);
    repaint();
}

void UpdateScreen::setRelease (const ReleaseInfo& r, const juce::String& currentVersion)
{
    release = r;
    current = currentVersion;
    more.setURL (juce::URL (r.pageUrl));
    notes.clear();
    for (const auto& n : r.notes)
        notes.push_back (noteLayout (n, columnW - 16.0f));
    refresh();
}

void UpdateScreen::setScreenshots (juce::Array<juce::Image> images)
{
    shots = std::move (images);
    shot = 0;
    resized();
    repaint();
}

void UpdateScreen::setView (View v, float p, bool busyJob, bool app, const juce::String& hostName)
{
    p = juce::jlimit (0.0f, 1.0f, p);
    if (v == view && juce::exactlyEqual (p, progress) && busyJob == busy && app == standalone && hostName == host)
        return;
    view = v;
    progress = p;
    busy = busyJob;
    standalone = app;
    host = hostName;
    refresh();
}

void UpdateScreen::refresh()
{
    const auto v = view;
    const auto app = standalone;
    const auto next = "LISN " + release.version + " starts the next time you open " + host + ".";
    juce::String label ("Install");
    switch (v)
    {
        case View::whatsNew:       message = {}; label = "Update now"; break;
        case View::downloading:    message = juce::String (juce::roundToInt (progress * 100.0f)) + "%" + dot + "keep making music, it downloads in the background"; break;
        case View::ready:
        case View::installing:
           #if JUCE_MAC
            message = app ? juce::String ("Apple's Installer opens and asks for your Mac password, then LISN closes.")
                          : "Apple's Installer opens and asks for your Mac password. " + next;
           #else
            message = app ? "LISN closes, the installer finishes in a minute, then LISN opens again on " + release.version + "."
                          : "Windows asks for permission, then the installer runs while " + host + " stays open. " + next;
            if (app) label = "Install and restart";
           #endif
            if (v == View::installing) label = "Installing" + ellipsis;
            break;
        case View::installed:      message = "Installed. " + next; break;
        case View::waiting:        message = "Finish in the Installer. " + next; break;
        case View::failedDownload: message = "The download didn't finish."; label = "Try again"; break;
        case View::failedInstall:  message = "The update didn't install."; label = "Try again"; break;
        case View::inUse:          message = "Close " + host + ", then click Install"; break;
        case View::engine:         message = "Installed, but the AI engine could not be set up. Check your internet connection and click Install again."; break;
    }
    primary.setButtonText (label);
    primary.setEnabled (! (v == View::whatsNew && busy) && v != View::installing);
    later.setButtonText (v == View::installed || v == View::waiting ? "Close" : "Later");
    resized();
    repaint();
}

juce::Rectangle<float> UpdateScreen::shotArea() const
{
    const juce::Rectangle<float> area (inset, inset, columnX - 2.0f * inset, 400.0f - 2.0f * inset - (shots.size() > 1 ? 24.0f : 0.0f));
    const auto img = shots[shot];
    if (! img.isValid())
        return area;
    return juce::RectanglePlacement (juce::RectanglePlacement::centred).appliedTo (img.getBounds().toFloat(), area);
}

juce::Rectangle<float> UpdateScreen::dotArea (int i) const
{
    const auto r = shotArea();
    const auto x = r.getCentreX() + ((float) i - (float) (shots.size() - 1) / 2.0f) * 14.0f;
    return juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ x, r.getBottom() + 14.0f });
}

void UpdateScreen::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < shots.size() && shots.size() > 1; ++i)
        if (i != shot && dotArea (i).contains (e.position))
        {
            shot = i;
            repaint();
        }
}

void UpdateScreen::resized()
{
    const bool failed = view == View::failedDownload || view == View::failedInstall;
    auto x = juce::roundToInt (panelW - inset);
    const auto place = [&x] (LisnButton& b, bool show)
    {
        b.setVisible (show);
        if (! show) return;
        x -= b.preferredWidth();
        b.setBounds (x, juce::roundToInt (buttonY), b.preferredWidth(), b.height);
        x -= 8;
    };
    place (primary, view != View::downloading && view != View::installed && view != View::waiting);
    place (cancel, view == View::downloading);
    place (later, true);

    // The notes, 10 px apart, as far as they fit above the buttons; "...and more" under them.
    auto notesBottom = bodyY;
    for (const auto& n : notes)
    {
        if (notesBottom + n.getHeight() > buttonY - 40.0f) break;   // ponytail: notes that don't fit are cut; the link stays
        notesBottom += n.getHeight() + 10.0f;
    }
    more.setVisible (view == View::whatsNew && release.moreNotes);
    more.setBounds (juce::roundToInt (columnX + 16.0f), juce::roundToInt (notesBottom - 4.0f),
                    juce::roundToInt (juce::GlyphArrangement::getStringWidth (Fonts::body (14.0f), more.getButtonText()) + 4.0f), 22);

    const auto lines = lineCount (Fonts::body (14.0f), message, columnW);
    website.setVisible (failed);
    website.setBounds (juce::roundToInt (columnX), juce::roundToInt (bodyY + (float) lines * 21.0f + 16.0f), website.preferredWidth(), website.height);
}

void UpdateScreen::paint (juce::Graphics& g)
{
    // Left: the screenshot, rounded 10 px with a hairline, its dots under it; none: the LISN symbol on the panel.
    const auto r = shotArea();
    if (const auto img = shots[shot]; img.isValid())
    {
        juce::Path clip;
        clip.addRoundedRectangle (r, 10.0f);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (clip);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, r, juce::RectanglePlacement::stretchToFit);
        }
        g.setColour (Theme::cream.withAlpha (0.1f));
        g.drawRoundedRectangle (r.reduced (0.5f), 9.5f, 1.0f);
        if (shots.size() > 1)
            for (int i = 0; i < shots.size(); ++i)
            {
                g.setColour (i == shot ? Theme::cream : Theme::cream.withAlpha (0.3f));
                g.fillEllipse (dotArea (i).withSizeKeepingCentre (6.0f, 6.0f));
            }
    }
    else if (symbol != nullptr)
        symbol->drawWithin (g, r.withSizeKeepingCentre (120.0f, 120.0f), juce::RectanglePlacement::centred, 1.0f);

    // Right: "LISN 0.3.1" (Unbounded, as the screen titles), "26 MB · you have 0.3.0".
    text (g, Fonts::display (22.0f), Theme::cream, "LISN " + release.version, { columnX, inset, columnW, 30.0f }, juce::Justification::centredLeft);
    auto sub = "you have " + current;
    if (release.installerSize > 0)
        sub = juce::String (juce::jmax (1, juce::roundToInt ((double) release.installerSize / (1024.0 * 1024.0)))) + " MB" + dot + sub;
    text (g, Fonts::body (13.0f), Theme::cream.withAlpha (0.64f), sub, { columnX, inset + 34.0f, columnW, 18.0f }, juce::Justification::centredLeft);

    if (view == View::whatsNew)
    {
        auto y = bodyY;
        for (const auto& n : notes)
        {
            if (y + n.getHeight() > buttonY - 40.0f) break;
            const auto firstLine = n.getNumLines() > 0 ? n.getLine (0).getLineBoundsY().getLength() : n.getHeight();
            text (g, Fonts::body (14.0f, 700), theme.guitar, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")),
                  { columnX + 2.0f, y, 10.0f, firstLine }, juce::Justification::centredLeft);
            n.draw (g, { columnX + 16.0f, y, columnW - 16.0f, n.getHeight() });
            y += n.getHeight() + 10.0f;
        }
        if (busy)
            text (g, Fonts::body (12.0f), Theme::cream.withAlpha (0.6f), "Finish the current split first",
                  { columnX, buttonY - 26.0f, columnW, 18.0f }, juce::Justification::centredRight);
        return;
    }
    auto y = bodyY;
    if (view == View::downloading)   // a gold bar over the caption
    {
        const juce::Rectangle<float> bar (columnX, y + 2.0f, columnW, 6.0f);
        g.setColour (Theme::cream.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (theme.guitar);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, columnW * progress)), 3.0f);
        y += 22.0f;
    }
    const auto lines = lineCount (Fonts::body (14.0f), message, columnW);
    paragraph (g, Fonts::body (14.0f), Theme::cream.withAlpha (view == View::downloading ? 0.72f : 0.88f), message,
               { columnX, y, columnW, (float) lines * 21.0f }, 21.0f, juce::Justification::left);
}
