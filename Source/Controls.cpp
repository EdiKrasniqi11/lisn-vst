#include "Controls.h"
#include "Icons.h"

namespace
{
    constexpr float pillInset = 4.0f;            // 1 px border + 3 px padding
    constexpr float pillItemHeight = 30.0f, pillItemPad = 14.0f, pillGap = 2.0f;
    constexpr float iconGap = 6.0f;

    juce::Font pillFont() { return Fonts::body (13.0f, 700); }
}

SegmentedPill::SegmentedPill (juce::StringArray itemTexts) : items (std::move (itemTexts))
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedPill::setSelected (int index)
{
    if (index != selected && juce::isPositiveAndBelow (index, items.size()))
    {
        selected = index;
        repaint();
    }
}

void SegmentedPill::setTheme (const Theme& t)
{
    tint = t.pillTint;
    repaint();
}

juce::Rectangle<float> SegmentedPill::itemBounds (int index) const
{
    auto x = pillInset;
    for (int i = 0; i < index; ++i)
        x += juce::GlyphArrangement::getStringWidth (pillFont(), items[i]) + 2.0f * pillItemPad + pillGap;
    return { x, pillInset, juce::GlyphArrangement::getStringWidth (pillFont(), items[index]) + 2.0f * pillItemPad, pillItemHeight };
}

int SegmentedPill::preferredWidth() const
{
    return items.isEmpty() ? (int) (2 * pillInset) : (int) std::ceil (itemBounds (items.size() - 1).getRight() + pillInset);
}

void SegmentedPill::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (tint);
    g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
    g.setColour (Theme::cream.withAlpha (0.14f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() / 2.0f - 0.5f, 1.0f);

    g.setFont (pillFont());
    for (int i = 0; i < items.size(); ++i)
    {
        const auto item = itemBounds (i);
        if (i == selected)
        {
            g.setColour (Theme::cream);
            g.fillRoundedRectangle (item, item.getHeight() / 2.0f);
        }
        g.setColour (i == selected ? Theme::ink : Theme::cream);
        g.drawText (items[i], item, juce::Justification::centred, false);
    }
}

void SegmentedPill::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! getLocalBounds().contains (e.getPosition()))   // JUCE still sends mouse events to disabled components
        return;
    for (int i = 0; i < items.size(); ++i)
        if (i != selected && itemBounds (i).contains (e.position))
        {
            setSelected (i);
            if (onChange) onChange (i);
            return;
        }
}

LisnButton::LisnButton (const juce::String& text, Style s) : juce::Button (text), style (s)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int LisnButton::preferredWidth() const
{
    const auto textW = juce::GlyphArrangement::getStringWidth (Fonts::body (fontSize, 700), getButtonText());
    return (int) std::ceil (2.0f * border() + padLeft + (icon != nullptr ? iconSize + iconGap : 0.0f) + textW + padRight);
}

void LisnButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto r = getLocalBounds().toFloat();
    const auto radius = style == Style::Soft ? 8.0f : 10.0f;
    g.setColour (style == Style::Primary ? Theme::cream : Theme::cream.withAlpha (style == Style::Ghost ? 0.06f : 0.12f));
    g.fillRoundedRectangle (r, radius);
    if (style == Style::Ghost)
    {
        g.setColour (Theme::cream.withAlpha (borderAlpha));
        g.drawRoundedRectangle (r.reduced (0.5f), radius - 0.5f, 1.0f);
    }

    g.setColour (style == Style::Primary ? Theme::ink : Theme::cream);
    auto x = border() + padLeft;
    if (icon != nullptr)
    {
        strokeIcon (g, icon, { x, (r.getHeight() - iconSize) / 2.0f, iconSize, iconSize }, iconStroke);
        x += iconSize + iconGap;
    }
    g.setFont (Fonts::body (fontSize, 700));
    g.drawText (getButtonText(), r.withLeft (x), juce::Justification::centredLeft, false);
}

CircleButton::CircleButton() : juce::Button ({})
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void CircleButton::setFill (juce::Colour c)
{
    if (c != fill) { fill = c; repaint(); }
}

void CircleButton::setPlaying (bool p)
{
    if (p != playing) { playing = p; repaint(); }
}

void CircleButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (fill);
    g.fillEllipse (r);
    // CSS: a 16 px icon centred; play has margin-left 2px, which moves the centred box by +1.
    const auto icon = juce::Rectangle<float> (16.0f, 16.0f).withCentre (r.getCentre()).translated (playing ? 0.0f : 1.0f, 0.0f);
    g.setColour (Theme::ink);
    g.fillPath (iconPath (playing ? Icons::pause : Icons::play, icon));
}

void drawDashedRoundedRect (juce::Graphics& g, juce::Rectangle<float> r, float radius, float thickness, float dash, float gap, juce::Colour c)
{
    juce::Path outline, dashed;
    outline.addRoundedRectangle (r, radius);
    const float lengths[] = { dash, gap };
    juce::PathStrokeType (thickness).createDashedStroke (dashed, outline, lengths, 2);
    g.setColour (c);
    g.fillPath (dashed);
}
