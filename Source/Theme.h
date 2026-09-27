#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Colours from docs/design (Waves.dc.html / Main.dc.html palettes). Text is always cream; ink is the dark text on cream.
struct Theme
{
    juce::String id;                                    // "dusk" or "midnight"
    juce::Colour ground, shadow, panelTint, pillTint;
    std::array<juce::Colour, 5> layers;                 // back to front
    juce::Colour vocals, drums, bass, guitar, piano, other;

    juce::Colour stemColour (const juce::String& stem) const;   // unknown stem -> other
    juce::Colour accent() const { return vocals; }

    static inline const juce::Colour cream { 0xffFFF4EA };
    static inline const juce::Colour ink   { 0xff2B1822 };
};

const Theme& themeFor (const juce::String& id);         // unknown id -> dusk

// Typefaces loaded from BinaryData once per JUCE lifetime (freed at shutdownJuce_GUI). Each
// createSystemTypefaceFor call registers a DirectWrite loader that is never released, so never reload.
struct FontSet : juce::DeletedAtShutdown
{
    FontSet();
    ~FontSet() override { clearSingletonInstance(); }
    juce::Typeface::Ptr display, regular, medium, bold;       // null if a font failed to load
    JUCE_DECLARE_SINGLETON (FontSet, false)
};

namespace Fonts
{
    juce::Font display (float px);                       // Unbounded Bold
    juce::Font body (float px, int weight = 400);        // DM Sans 400 / 500 / 700
    juce::Font mono (float px);                          // Consolas
}
