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
    // The texts and font never change, so shape them once: the pill repaints on every motion frame.
    auto x = pillInset;
    for (const auto& item : items)
    {
        const auto w = juce::GlyphArrangement::getStringWidth (pillFont(), item) + 2.0f * pillItemPad;
        itemRects.add ({ x, pillInset, w, pillItemHeight });
        x += w + pillGap;
    }
    prefWidth = items.isEmpty() ? (int) (2 * pillInset) : (int) std::ceil (itemRects.getLast().getRight() + pillInset);

    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setBufferedToImage (true);   // the header repaints with every motion frame; the pill itself rarely changes
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
        const auto item = itemRects[i];
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
        if (i != selected && itemRects[i].contains (e.position))
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
    const auto radius = style == Style::Soft ? 8.0f : corner;
    g.setColour (tint.has_value() ? tint->withAlpha (0.22f)
                                  : style == Style::Primary ? Theme::cream : Theme::cream.withAlpha (style == Style::Ghost ? 0.06f : 0.12f));
    g.fillRoundedRectangle (r, radius);
    if (style == Style::Ghost)
    {
        g.setColour (tint.has_value() ? *tint : Theme::cream.withAlpha (borderAlpha));
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

namespace { juce::Font chipFont() { return Fonts::body (12.0f, 700); } }

int dragChipWidth (const juce::String& text)
{
    return (int) std::ceil (1 + 8 + 16 + 6 + juce::GlyphArrangement::getStringWidth (chipFont(), text) + 12 + 1);
}

void drawDragChip (juce::Graphics& g, juce::Rectangle<float> chip, const juce::String& text)
{
    drawDashedRoundedRect (g, chip.reduced (0.5f), 9.5f, 1.0f, 3.0f, 3.0f, Theme::cream.withAlpha (0.3f));
    const juce::Rectangle<float> grip (chip.getX() + 9.0f, chip.getCentreY() - 8.0f, 16.0f, 16.0f);
    g.setColour (Theme::cream.withAlpha (0.82f));
    strokeIcon (g, Icons::grip, grip, 3.0f);
    g.setFont (chipFont());
    g.drawText (text, chip.withLeft (grip.getRight() + 6.0f), juce::Justification::centredLeft, false);
}

DragChip::DragChip()
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragChip::setText (const juce::String& t)
{
    if (t != text) { text = t; repaint(); }
}

void DragChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() <= 4 || fileToDrag == nullptr)
        return;
    dragStarted = true;   // once per gesture
    const auto f = fileToDrag();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
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

InfoButton::InfoButton() : juce::Button ("Help")
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setBufferedToImage (true);   // it sits in the waves' motion region (the bottom margin): repaint only on hover and toggle
}

void InfoButton::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    // Help.mockup.html: a 16 px ring (cream 0.35) around an "i" (cream 0.55), centred in the 24 px hit area. Hovered, or lit
    // while the sheet is open, the ring goes to 0.8 and the "i" to full cream.
    const auto bright = isMouseOver || getToggleState();
    const auto ring = getLocalBounds().toFloat().withSizeKeepingCentre (16.0f, 16.0f);
    g.setColour (Theme::cream.withAlpha (bright ? 0.8f : 0.35f));
    g.drawEllipse (ring.reduced (0.5f), 1.0f);

    // The "i": M12 11v6 at stroke 2.4 and a 2.8 dot at (12, 7.25), in a 24 px viewBox drawn at 12 px.
    const auto icon = ring.withSizeKeepingCentre (12.0f, 12.0f);
    g.setColour (bright ? Theme::cream : Theme::cream.withAlpha (0.55f));
    strokeIcon (g, "M12 11v6", icon, 2.4f);
    g.fillEllipse (juce::Rectangle<float> (1.4f, 1.4f).withCentre ({ icon.getX() + 6.0f, icon.getY() + 3.625f }));
}

MuteLight::MuteLight() : juce::Button ({})
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MuteLight::setMuted (bool m)
{
    if (m != muted) { muted = m; repaint(); }
}

void MuteLight::clicked (const juce::ModifierKeys& mods)
{
    const auto& callback = mods.isPopupMenu() ? onSolo : onToggle;   // Button::onClick would fire for both buttons
    if (callback != nullptr) callback();
}

void MuteLight::paintButton (juce::Graphics& g, bool, bool)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::cream.withAlpha (0.08f));
    g.fillEllipse (r);
    g.setColour (Theme::cream.withAlpha (0.22f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    g.setColour (muted ? Theme::cream.withAlpha (0.5f) : Theme::cream);
    const auto icon = r.getWidth() < 38.0f ? 15.0f : 18.0f;   // board 2C: 18 in the 38 light, 15 in the 30 one
    strokeIcon (g, muted ? Icons::speakerOff : Icons::speaker, r.withSizeKeepingCentre (icon, icon), 2.0f);
}

VolumeBar::VolumeBar()
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void VolumeBar::setColour (juce::Colour c)
{
    if (c != colour) { colour = c; repaint(); }
}

void VolumeBar::setValue (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (juce::exactlyEqual (v, value)) return;
    value = v;
    repaint();
}

float VolumeBar::valueAt (float x, float width)
{
    return juce::jlimit (0.0f, 1.0f, (x - 5.0f) / juce::jmax (1.0f, width - 10.0f));
}

void VolumeBar::reset()
{
    value = 1.0f;
    repaint();
    if (onChange != nullptr) onChange (1.0f);
}

void VolumeBar::setFromMouse (const juce::MouseEvent& e)
{
    value = valueAt (e.position.x, (float) getWidth());
    repaint();
    if (onChange != nullptr) onChange (value);
}

void VolumeBar::mouseDown (const juce::MouseEvent& e)        { setFromMouse (e); }
void VolumeBar::mouseDrag (const juce::MouseEvent& e)        { setFromMouse (e); }
void VolumeBar::mouseDoubleClick (const juce::MouseEvent&)   { reset(); }

void VolumeBar::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth(), cy = (float) getHeight() / 2.0f, endX = 5.0f + value * (w - 10.0f);
    g.setColour (Theme::cream.withAlpha (0.16f));
    g.fillRoundedRectangle (0.0f, cy - 1.5f, w, 3.0f, 1.5f);   // the full width; only the thumb travel is inset
    g.setColour (colour);
    g.fillRoundedRectangle (0.0f, cy - 1.5f, endX, 3.0f, 1.5f);
    g.setColour (juce::Colour (0x73000000));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ endX, cy + 1.0f }));
    g.setColour (Theme::cream);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ endX, cy }));
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
