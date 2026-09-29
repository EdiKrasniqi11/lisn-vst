#include "StemRow.h"

namespace
{
    // StemPlayer.mockup.html row (CSS px): padding 0 10, gap 14; name block 118, light 38, waveform 376 (the old 330 plus the
    // removed time slot), then the chip.
    constexpr int padX = 10, nameW = 118, gap = 14, lightSize = 38, waveW = 376, chipH = 34;   // chip: 32 + 1 px border
    constexpr int lightX = padX + nameW + gap;          // 142
    constexpr int waveX = lightX + lightSize + gap;     // 194
    constexpr int chipX = waveX + waveW + gap;          // 584
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
    const auto head = juce::roundToInt (fraction * getWidth());   // snapped, as the screen repaints per playhead pixel
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (0, 0, head, getHeight());
        g.setColour (colour);
        g.fillPath (bars);
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    looping = false;
    if (getWidth() <= 0) return;
    downFraction = juce::jlimit (0.0, 0.999, (double) e.position.x / getWidth());
    if (onSeek != nullptr) onSeek (downFraction);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (getWidth() <= 0 || (! looping && e.getDistanceFromDragStart() < 4)) return;
    looping = true;                                  // 4 px or more: this gesture selects a loop instead of seeking
    if (onLoopDrag != nullptr) onLoopDrag (downFraction, juce::jlimit (0.0, 1.0, (double) e.position.x / getWidth()), false);
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (looping && onLoopDrag != nullptr && getWidth() > 0)
        onLoopDrag (downFraction, juce::jlimit (0.0, 1.0, (double) e.position.x / getWidth()), true);
    looping = false;
}

StemRow::StemRow (juce::File wav, juce::String key)
    : file (std::move (wav)), stemKey (std::move (key)), name (stemKey.substring (0, 1).toUpperCase() + stemKey.substring (1))
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    lightButton.onToggle = [this] { if (onToggleMute != nullptr) onToggleMute(); };
    lightButton.onSolo = [this] { if (onSolo != nullptr) onSolo(); };
    wave.onSeek = [this] (double f) { if (onSeek != nullptr) onSeek (f); };
    wave.onLoopDrag = [this] (double a, double b, bool done) { if (onLoopDrag != nullptr) onLoopDrag (a, b, done); };
    addAndMakeVisible (lightButton);
    addAndMakeVisible (wave);
    setTheme (themeFor ("dusk"));
    setSize (674, 62);
    setPeaks ({});
}

void StemRow::setTheme (const Theme& t)
{
    colour = t.stemColour (stemKey);
    wave.setStemColour (colour);
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

void StemRow::setMuted (bool m)
{
    if (m == muted) return;
    muted = m;
    lightButton.setMuted (m);
    wave.setAlpha (m ? 0.38f : 1.0f);
    repaint();
}

void StemRow::setPosition (double fraction)
{
    wave.setFraction (juce::jlimit (0.0, 1.0, fraction));
}

juce::Rectangle<int> StemRow::chipBounds() const
{
    return { chipX, (getHeight() - chipH) / 2, dragChipWidth ("Drag"), chipH };
}

void StemRow::resized()
{
    const int h = getHeight(), waveH = compact ? 26 : 40;
    lightButton.setBounds (lightX, (h - lightSize) / 2, lightSize, lightSize);
    wave.setBounds (waveX, (h - waveH) / 2 - WaveformView::margin, waveW, waveH + 2 * WaveformView::margin);
}

void StemRow::paint (juce::Graphics& g)
{
    const auto h = (float) getHeight();
    const float a = muted ? 0.38f : 1.0f;            // a muted row dims its name block (the waveform dims via its alpha)

    // Marker (LoopHandles.mockup.html): a 4 x 30 bar, radius 2, in the stem colour, dimmed with the name.
    const juce::Rectangle<float> bar ((float) padX, (h - 30.0f) / 2.0f, 4.0f, 30.0f);
    g.setColour (colour.withMultipliedAlpha (a));
    g.fillRoundedRectangle (bar, 2.0f);

    // Name (Bold 14) over the file name (11, cream 0.6), line-height 1.2, 1 px apart, centred as one column, 10 px after the bar.
    const auto nameH = 14.0f * 1.2f, fileH = 11.0f * 1.2f, top = (h - (nameH + 1.0f + fileH)) / 2.0f;
    const auto textX = bar.getRight() + 10.0f, textW = (float) (padX + nameW) - textX;
    g.setColour (Theme::cream.withAlpha (a));
    g.setFont (Fonts::body (14.0f, 700));
    g.drawText (name, juce::Rectangle<float> (textX, top, textW, nameH), juce::Justification::centredLeft, true);
    g.setColour (Theme::cream.withAlpha (0.6f * a));
    g.setFont (Fonts::body (11.0f));
    g.drawText (file.getFileName(), juce::Rectangle<float> (textX, top + nameH + 1.0f, textW, fileH), juce::Justification::centredLeft, true);

    if (g.clipRegionIntersects ({ chipX, 0, getWidth() - chipX, getHeight() }))
        drawDragChip (g, chipBounds().toFloat(), "Drag");
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
    const auto f = dragFile != nullptr ? dragFile() : file;
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}
