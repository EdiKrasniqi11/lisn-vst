#include "Theme.h"
#include "BinaryData.h"

namespace
{
    juce::Colour rgba (int r, int g, int b, float a) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, a); }
}

juce::Colour Theme::stemColour (const juce::String& stem) const
{
    if (stem == "vocals") return vocals;
    if (stem == "drums")  return drums;
    if (stem == "bass")   return bass;
    if (stem == "guitar") return guitar;
    if (stem == "piano")  return piano;
    return other;
}

const Theme& themeFor (const juce::String& id)
{
    using C = juce::Colour;
    static const Theme dusk { "dusk", C (0xff6F7679), rgba (36, 14, 28, 0.42f), rgba (32, 18, 27, 0.66f), rgba (32, 18, 27, 0.72f),
                              { C (0xffD9775F), C (0xffF4A76F), C (0xff7B5467), C (0xffD95F6B), C (0xffF08A7C) },
                              C (0xffF4A76F), C (0xffEF7A7F), C (0xffC9A0BD), C (0xffF3CD7F), C (0xffEFE3D6), C (0xffA9B8BC) };
    static const Theme midnight { "midnight", C (0xff152238), rgba (2, 6, 18, 0.5f), rgba (8, 14, 28, 0.66f), rgba (8, 14, 28, 0.72f),
                                  { C (0xff2C4A78), C (0xff4F86B8), C (0xff101A2E), C (0xff35679C), C (0xff86B6DD) },
                                  C (0xffF3B98A), C (0xff6FD3C6), C (0xffA9B4FF), C (0xffF4D58D), C (0xffE3ECF7), C (0xff8DB8DB) };
    return id == "midnight" ? midnight : dusk;
}

FontSet::FontSet()
    : display (juce::Typeface::createSystemTypefaceFor (BinaryData::UnboundedBold_ttf, (size_t) BinaryData::UnboundedBold_ttfSize)),
      regular (juce::Typeface::createSystemTypefaceFor (BinaryData::DMSansRegular_ttf, (size_t) BinaryData::DMSansRegular_ttfSize)),
      medium  (juce::Typeface::createSystemTypefaceFor (BinaryData::DMSansMedium_ttf,  (size_t) BinaryData::DMSansMedium_ttfSize)),
      bold    (juce::Typeface::createSystemTypefaceFor (BinaryData::DMSansBold_ttf,    (size_t) BinaryData::DMSansBold_ttfSize))
{
}

namespace Fonts
{
    juce::Font display (float px)
    {
        juce::SharedResourcePointer<FontSet> fonts;
        return juce::Font (juce::FontOptions (fonts->display).withPointHeight (px));
    }

    juce::Font body (float px, int weight)
    {
        juce::SharedResourcePointer<FontSet> fonts;
        const auto& tf = weight >= 700 ? fonts->bold : weight >= 500 ? fonts->medium : fonts->regular;
        return juce::Font (juce::FontOptions (tf).withPointHeight (px));
    }

    juce::Font mono (float px)
    {
        return juce::Font (juce::FontOptions ("Consolas", 0.0f, juce::Font::plain).withPointHeight (px));
    }
}
