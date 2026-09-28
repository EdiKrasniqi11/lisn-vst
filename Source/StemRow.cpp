#include "StemRow.h"
#include "Icons.h"

namespace
{
    // Main.dc.html row (CSS px): padding 0 10, gap 14; name block 118, button 38, waveform 330, time 32, then the chip.
    constexpr int padX = 10, nameW = 118, gap = 14, buttonSize = 38, waveW = 330, timeW = 32, chipH = 34;   // chip: 32 + 1 px border
    constexpr int buttonX = padX + nameW + gap;         // 142
    constexpr int waveX = buttonX + buttonSize + gap;   // 194
    constexpr int timeX = waveX + waveW + gap;          // 538
    constexpr int chipX = timeX + timeW + gap;          // 584

    juce::Font chipFont() { return Fonts::body (12.0f, 700); }
}

juce::String mmss (double seconds)
{
    const auto s = juce::jmax (0, (int) seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

WaveformView::WaveformView()
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void WaveformView::setLevels (std::vector<float> l)
{
    levels = std::move (l);
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
    if (levels.empty())
        return;
    const auto h = (float) (getHeight() - 2 * margin), mid = (float) margin + h / 2.0f;
    const auto step = (float) getWidth() / (float) levels.size();
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto half = juce::jmax (0.75f, juce::jlimit (0.0f, 1.0f, levels[i]) * (h / 2.0f - 0.5f));
        bars.addRectangle ((float) i * step, mid - half, step * 0.6f, 2.0f * half);
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    g.setColour (colour.withAlpha (0.3f));
    g.fillPath (bars);
    if (fraction <= 0.0)
        return;
    const auto head = juce::roundToInt (fraction * getWidth());   // snapped, as StemRow repaints per playhead pixel
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (0, 0, head, getHeight());
        g.setColour (colour);
        g.fillPath (bars);
    }
    g.setColour (Theme::cream);
    g.fillRoundedRectangle ((float) head - 1.0f, 0.0f, 2.0f, (float) getHeight(), 1.0f);   // spans the bars' -5 .. h + 5
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (onSeek != nullptr && getWidth() > 0)
        onSeek (juce::jlimit (0.0, 0.999, (double) e.position.x / getWidth()));
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    mouseDown (e);
}

StemRow::StemRow (juce::File wav, juce::String key)
    : file (std::move (wav)), stemKey (std::move (key)), name (stemKey.substring (0, 1).toUpperCase() + stemKey.substring (1))
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    button.onClick = [this] { if (onPlayPause != nullptr) onPlayPause(); };
    wave.onSeek = [this] (double f) { if (onSeek != nullptr) onSeek (f); };
    addAndMakeVisible (button);
    addAndMakeVisible (wave);
    setTheme (themeFor ("dusk"));
    setSize (674, 62);
    setPeaks ({});
}

void StemRow::setTheme (const Theme& t)
{
    colour = t.stemColour (stemKey);
    wave.setStemColour (colour);
    button.setFill (playing ? colour : Theme::cream);
    repaint();
}

void StemRow::setCompact (bool sixStems)
{
    compact = sixStems;
    setPeaks (std::move (levels));
    setSize (getWidth(), compact ? 42 : 62);
    resized();   // setSize skips it when the height is unchanged, but the waveform height follows `compact`
}

void StemRow::setPeaks (std::vector<float> l)
{
    levels = std::move (l);
    levels.resize ((size_t) barCount(), 0.0f);
    wave.setLevels (levels);
}

void StemRow::setPlayback (bool isPlaying, double fraction, double lengthSeconds)
{
    fraction = juce::jlimit (0.0, 1.0, fraction);
    wave.setFraction (fraction);
    const auto newHead = juce::roundToInt (fraction * waveW);
    const auto newTime = mmss (fraction * lengthSeconds);
    if (isPlaying == playing && newHead == head && newTime == time)
        return;

    ++repaintCount;
    head = newHead;
    time = newTime;
    if (isPlaying != playing)
    {
        playing = isPlaying;
        button.setPlaying (playing);
        button.setFill (playing ? colour : Theme::cream);
        repaint();   // the row background changes
    }
    else
    {
        wave.repaint();
        repaint (timeBounds());
    }
}

juce::Rectangle<int> StemRow::timeBounds() const
{
    return { timeX, 0, timeW, getHeight() };
}

juce::Rectangle<int> StemRow::chipBounds() const
{
    // border 1 + padding 8, grip 16, gap 6, "Drag", padding 12 + border 1
    const auto w = (int) std::ceil (1 + 8 + 16 + 6 + juce::GlyphArrangement::getStringWidth (chipFont(), "Drag") + 12 + 1);
    return { chipX, (getHeight() - chipH) / 2, w, chipH };
}

void StemRow::resized()
{
    const int h = getHeight(), waveH = compact ? 26 : 40;
    button.setBounds (buttonX, (h - buttonSize) / 2, buttonSize, buttonSize);
    wave.setBounds (waveX, (h - waveH) / 2 - WaveformView::margin, waveW, waveH + 2 * WaveformView::margin);
}

void StemRow::paint (juce::Graphics& g)
{
    const auto h = (float) getHeight();
    if (playing)
    {
        g.setColour (Theme::cream.withAlpha (0.08f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
    }

    // Badge: 32 x 32, radius 10, stem colour at 0.16, icon 18 px with stroke 2.
    const juce::Rectangle<float> badge ((float) padX, (h - 32.0f) / 2.0f, 32.0f, 32.0f);
    if (g.clipRegionIntersects (badge.getSmallestIntegerContainer()))   // the playhead repaints skip the path work
    {
        g.setColour (colour.withAlpha (0.16f));
        g.fillRoundedRectangle (badge, 10.0f);
        g.setColour (colour);
        strokeIcon (g, stemIcon (stemKey), badge.withSizeKeepingCentre (18.0f, 18.0f), 2.0f);
    }

    // Name (Bold 14) over the file name (11, cream 0.6), line-height 1.2, 1 px apart, centred as one column, 10 px after the badge.
    const auto nameH = 14.0f * 1.2f, fileH = 11.0f * 1.2f, top = (h - (nameH + 1.0f + fileH)) / 2.0f;
    const auto textX = badge.getRight() + 10.0f, textW = (float) (padX + nameW) - textX;
    g.setColour (Theme::cream);
    g.setFont (Fonts::body (14.0f, 700));
    g.drawText (name, juce::Rectangle<float> (textX, top, textW, nameH), juce::Justification::centredLeft, true);
    g.setColour (Theme::cream.withAlpha (0.6f));
    g.setFont (Fonts::body (11.0f));
    g.drawText (file.getFileName(), juce::Rectangle<float> (textX, top + nameH + 1.0f, textW, fileH), juce::Justification::centredLeft, true);

    g.setColour (Theme::cream.withAlpha (0.72f));
    g.setFont (Fonts::body (12.0f));
    g.drawText (time, timeBounds().toFloat(), juce::Justification::centredRight, false);

    // Drag chip: 1 px dashed border (dash 3, gap 3) at cream 0.3, radius 10; grip 16 px stroke 3, 6 px, "Drag" Bold 12 at cream 0.82.
    if (! g.clipRegionIntersects ({ chipX, 0, getWidth() - chipX, getHeight() }))
        return;
    const auto chip = chipBounds().toFloat();
    drawDashedRoundedRect (g, chip.reduced (0.5f), 9.5f, 1.0f, 3.0f, 3.0f, Theme::cream.withAlpha (0.3f));
    const juce::Rectangle<float> grip (chip.getX() + 9.0f, chip.getCentreY() - 8.0f, 16.0f, 16.0f);
    g.setColour (Theme::cream.withAlpha (0.82f));
    strokeIcon (g, Icons::grip, grip, 3.0f);
    g.setFont (chipFont());
    g.drawText ("Drag", chip.withLeft (grip.getRight() + 6.0f), juce::Justification::centredLeft, false);
}

void StemRow::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
}

void StemRow::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() <= 4)
        return;
    dragStarted = true;   // once per gesture
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}
