#pragma once
#include "Theme.h"

// Controls from the mockups. None of them takes keyboard focus, so the host keeps its shortcuts (spacebar etc.).

// The header pill (Main.dc.html): outer 38 tall (1 px border + 3 px padding), items 30 tall with 14 px side padding and 2 px gaps.
class SegmentedPill : public juce::Component
{
public:
    explicit SegmentedPill (juce::StringArray items);
    void setSelected (int index);                // no callback
    int getSelected() const { return selected; }
    void setTheme (const Theme&);
    int preferredWidth() const { return prefWidth; }
    std::function<void (int)> onChange;          // only on a click that changes the selection

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::StringArray items;
    juce::Array<juce::Rectangle<float>> itemRects;   // shaped once in the constructor
    int prefWidth = 0;
    int selected = 0;
    juce::Colour tint = themeFor ("dusk").pillTint;
};

// Rounded text button, optionally with a stroked icon 6 px before the label. Size it with preferredWidth() x height.
class LisnButton : public juce::Button
{
public:
    enum class Style { Primary, Ghost, Soft };   // cream / cream@0.06 with a border / cream@0.12 (Copy)
    LisnButton (const juce::String& text, Style);

    const char* icon = nullptr;                  // SVG d string, or none
    float iconSize = 16.0f, iconStroke = 2.0f;
    int height = 34;
    float padLeft = 16.0f, padRight = 16.0f, fontSize = 13.0f;
    float borderAlpha = 0.22f;                   // Ghost only (0.3 for Cancel and the error buttons)

    int preferredWidth() const;
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    float border() const { return style == Style::Ghost ? 1.0f : 0.0f; }
    Style style;
};

// The 38 px round play/pause button of a stem row, with a filled 16 px icon in ink.
class CircleButton : public juce::Button
{
public:
    CircleButton();
    void setFill (juce::Colour);
    void setPlaying (bool);                      // pause icon while playing, else play
    bool isPlaying() const { return playing; }
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    juce::Colour fill = Theme::cream;
    bool playing = false;
};

// The 38 px mute light of a stem row: a neutral ring with a white speaker, or the speaker with an x when muted.
// Left click toggles, right click solos (FL's channel rack).
class MuteLight : public juce::Button
{
public:
    MuteLight();
    void setMuted (bool);
    bool isMuted() const { return muted; }
    std::function<void()> onToggle, onSolo;
    void clicked (const juce::ModifierKeys&) override;   // public for tests
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    bool muted = false;
};

void drawDashedRoundedRect (juce::Graphics&, juce::Rectangle<float>, float radius, float thickness, float dash, float gap, juce::Colour);
