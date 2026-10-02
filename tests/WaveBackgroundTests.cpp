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
        // The panel centre is frosted layer 2 (plum) under the tint; without the frost it would be the ground under it.
        const auto frostedPlum = juce::Colour (0xff7B5467).overlaidWith (themeFor ("dusk").panelTint.withAlpha (0.66f));
        expect (matches (centre, frostedPlum.getARGB(), 6), centre.toDisplayString (false) + " vs " + frostedPlum.toDisplayString (false));
        // Over layer 3 (#D95F6B) the frost carries that layer's colour.
        expectGreaterThan (std::abs (dusk.getPixelAt (100, 276).getRed() - centre.getRed()), 20);

        beginTest ("paint midnight");
        const auto midnight = render (bg, "midnight").getPixelAt (740, 10);
        expect (matches (midnight, 0xff152238, 6), midnight.toDisplayString (false));

        beginTest ("panel tint change rebuilds the panel image");
        const auto tinted = render (bg, "midnight").getPixelAt (380, 276);
        bg.setPanel ({ 24, 76, 712, 400 }, 22.0f, 0.0f);
        const auto clearImage = render (bg, "midnight");
        const auto clear = clearImage.getPixelAt (380, 276);
        expect (tinted != clear, tinted.toDisplayString (false) + " vs " + clear.toDisplayString (false));
        bg.setPanel ({ 24, 76, 712, 400 }, 22.0f, 0.66f);

        beginTest ("the frost is blurred");
        // 8 px inside layer 2's edge at a crest (sin = 1) the lighter layer 1 bleeds in; a sharp copy equals the centre.
        const auto& s2 = kLayers[2];
        const auto crest = s2.wavelength * (4.5f * juce::MathConstants<float>::pi - s2.phase) / juce::MathConstants<float>::twoPi;
        const auto inside = (WaveGeometry::edgePoint (s2, crest, s2.phase, 0.0f) - WaveGeometry::normal (s2) * 8.0f).roundToInt();
        const auto nearEdge = clearImage.getPixelAt (inside.x, inside.y);
        expectGreaterThan ((int) nearEdge.getBlue() - (int) clear.getBlue(), 15, nearEdge.toDisplayString (false) + " vs " + clear.toDisplayString (false));

        beginTest ("paint with motion");
        const auto rest = render (bg, "dusk");
        bg.setMotion (200.0f, 1.0f);
        const auto moving = render (bg, "dusk");
        bg.setMotion (0.0f, 0.0f);
        // Layers still cover the window: a reversed offset moves layers 0 and 1 past their bottom-right mask margin,
        // and masks without the -direction extension leave the top left bare.
        for (auto p : { juce::Point<int> (759, 499), juce::Point<int> (60, 0) })
        {
            const auto c = moving.getPixelAt (p.x, p.y);
            expect (! matches (c, 0xff6F7679, 10), p.toString() + ": " + c.toDisplayString (false));
        }
        bool rowMoved = false;
        for (int x = 0; x < rest.getWidth(); ++x)
            rowMoved = rowMoved || rest.getPixelAt (x, 40) != moving.getPixelAt (x, 40);
        expect (rowMoved, "row y = 40 is the same as at rest");

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

        // Board 4A (Intro.dc.html): translateX(-/+820px) on a 760 px stage, 1.25 s, delays 0.06 s apart.
        beginTest ("intro: layers start off-screen (width + 60) from alternating sides and land at rest");
        expectEquals (WaveBackground::introOffset (0, 0.0f, 760.0f).x, -820.0f);
        expectEquals (WaveBackground::introOffset (1, 0.0f, 760.0f).x, 820.0f);
        expectEquals (WaveBackground::introOffset (2, 0.0f, 760.0f).x, -820.0f);
        expectEquals (WaveBackground::introOffset (4, 0.24f, 760.0f).x, -820.0f);   // layer 4 has not started yet
        for (int i = 0; i < 5; ++i)
        {
            expectEquals (WaveBackground::introOffset (i, 1.25f + 0.06f * (float) i, 760.0f).x, 0.0f);
            expectEquals (WaveBackground::introOffset (i, 0.3f, 760.0f).y, 0.0f);
        }
        expect (std::abs (WaveBackground::introOffset (0, 0.6f, 760.0f).x) < 0.2f * 820.0f);   // ease-out: mostly in by half time

        beginTest ("intro: the panel fades from 1.05 s over 0.7 s");
        expectEquals (WaveBackground::panelIntro (1.0f), 0.0f);
        expectEquals (WaveBackground::panelIntro (1.75f), 1.0f);
        expect (WaveBackground::panelIntro (1.4f) > 0.6f && WaveBackground::panelIntro (1.4f) < 1.0f);
        expectEquals (WaveBackground::panelDrop (1.05f), 18);
        expectEquals (WaveBackground::panelDrop (1.75f), 0);

        beginTest ("intro: CSS cubic-bezier easing");
        expectEquals (WaveBackground::cubicBezier (0.25f, 0.1f, 0.25f, 1.0f, 0.0f), 0.0f);
        expectEquals (WaveBackground::cubicBezier (0.25f, 0.1f, 0.25f, 1.0f, 1.0f), 1.0f);
        expectWithinAbsoluteError (WaveBackground::cubicBezier (0.25f, 0.1f, 0.25f, 1.0f, 0.5f), 0.8024f, 0.002f);   // CSS `ease`
    }
};

static WaveBackgroundTests waveBackgroundTests;
