#include "../Source/Theme.h"

struct ThemeTests : juce::UnitTest
{
    ThemeTests() : juce::UnitTest ("Theme") {}

    void runTest() override
    {
        using juce::Colour;
        const auto& dusk = themeFor ("dusk");
        const auto& midnight = themeFor ("midnight");

        beginTest ("lookup");
        expect (midnight.ground == Colour (0xff152238));
        expectEquals (midnight.id, juce::String ("midnight"));
        expect (&themeFor ("nope") == &dusk);
        expect (&themeFor ("") == &dusk);
        expect (dusk.ground == Colour (0xff6F7679));

        beginTest ("stem colours");
        expect (dusk.stemColour ("drums") == Colour (0xffEF7A7F));
        expect (midnight.stemColour ("drums") == Colour (0xff6FD3C6));
        expect (dusk.stemColour ("kazoo") == dusk.other);
        expect (midnight.stemColour ("kazoo") == midnight.other);

        beginTest ("layers");
        expect (dusk.layers[2] == Colour (0xff7B5467));
        expect (midnight.layers[2] == Colour (0xff101A2E));

        beginTest ("fonts");
        juce::SharedResourcePointer<FontSet> fonts;
        const auto display = Fonts::display (22).getTypefacePtr();
        const auto medium  = Fonts::body (14, 500).getTypefacePtr();
        const auto bold    = Fonts::body (12, 700).getTypefacePtr();
        expect (display != nullptr && medium != nullptr && bold != nullptr);
        if (display == nullptr || medium == nullptr || bold == nullptr) return;
        expect (display->getName().contains ("Unbounded"), display->getName());
        expect (medium->getName().contains ("DM Sans"), medium->getName());
        expect (medium->getStyle().contains ("Medium"), medium->getStyle());
        expect (bold->getStyle().contains ("Bold"), bold->getStyle());
    }
};

static ThemeTests themeTests;
