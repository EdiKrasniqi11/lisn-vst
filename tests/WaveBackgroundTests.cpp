#include "../Source/WaveBackground.h"

struct WaveBackgroundTests : juce::UnitTest
{
    WaveBackgroundTests() : juce::UnitTest ("WaveBackground") {}

    static bool matches (juce::Colour c, juce::uint32 argb, int tolerance)
    {
        const juce::Colour e (argb);
        return std::abs (c.getRed() - e.getRed()) <= tolerance && std::abs (c.getGreen() - e.getGreen()) <= tolerance
            && std::abs (c.getBlue() - e.getBlue()) <= tolerance;
    }

    static juce::Image render (WaveBackground& bg, const juce::String& themeId)
    {
        bg.setTheme (themeFor (themeId));
        juce::Image img (juce::Image::ARGB, 760, 500, true, juce::SoftwareImageType());
        juce::Graphics g (img);
        bg.paintEntireComponent (g, true);
        return img;
    }

    static int alphaAt (const WaveBackground::Mask& m, juce::Point<float> p)   // p in window px, scale 1
    {
        const auto px = p.roundToInt() - m.origin;
        return m.image.getPixelAt (px.x, px.y).getAlpha();
    }

    void runTest() override
    {
        WaveBackground bg;
        bg.setBounds (0, 0, 760, 500);
        bg.setPanel ({ 24, 76, 712, 400 }, 22.0f, 0.66f);

        beginTest ("motion region");
        const auto region = bg.motionRegion();
        expect (region.containsPoint (juce::Point<int> (10, 10)));
        expect (region.containsPoint (juce::Point<int> (380, 40)));
        expect (region.containsPoint (juce::Point<int> (5, 300)));
        expect (region.containsPoint (juce::Point<int> (26, 78)));      // panel corner
        expect (! region.containsPoint (juce::Point<int> (380, 276)));  // panel centre

        beginTest ("same motion twice");
        bg.setMotion (37.0f, 0.5f);
        bg.setMotion (37.0f, 0.5f);
        bg.setMotion (0.0f, 0.0f);

        beginTest ("paint dusk");
        const auto dusk = render (bg, "dusk");
        const auto ground = dusk.getPixelAt (740, 10), plum = dusk.getPixelAt (120, 30), centre = dusk.getPixelAt (380, 276);
        expect (matches (ground, 0xff6F7679, 6), ground.toDisplayString (false));
        expect (matches (plum, 0xff7B5467, 10), plum.toDisplayString (false));
        expect (centre.getPerceivedBrightness() < ground.getPerceivedBrightness(), centre.toDisplayString (false));

        beginTest ("paint midnight");
        const auto midnight = render (bg, "midnight").getPixelAt (740, 10);
        expect (matches (midnight, 0xff152238, 6), midnight.toDisplayString (false));

        beginTest ("layer shadow reach");
        // 30 px outside layer 2's edge at a trough (sin = -1): outside the shape, but sigma 14 still reaches it.
        const auto& s = kLayers[2];
        const auto trough = s.wavelength * (1.5f * juce::MathConstants<float>::pi - s.phase) / juce::MathConstants<float>::twoPi + s.wavelength;
        const auto out = WaveGeometry::edgePoint (s, trough, s.phase, 0.0f) + WaveGeometry::normal (s) * 30.0f;
        const juce::Rectangle<float> window (760.0f, 500.0f);
        const auto shadow = WaveBackground::shadowMask (s, window, 1.0f);
        const auto peak = alphaAt (shadow, { 20.0f, 250.0f });
        expectEquals (alphaAt (WaveBackground::shapeMask (s, window, 1.0f), out), 0);
        expectGreaterThan (peak, 250);
        expectGreaterThan (alphaAt (shadow, out) * 10, peak, "alpha " + juce::String (alphaAt (shadow, out)));
    }
};

static WaveBackgroundTests waveBackgroundTests;
