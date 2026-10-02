#include "../Source/StemRow.h"
#include "../Source/Icons.h"
#include <tuple>

struct StemRowTests : juce::UnitTest
{
    StemRowTests() : juce::UnitTest ("StemRow") {}

    static juce::Image render (juce::Component& c)
    {
        juce::Image img (juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (img);
        c.paintEntireComponent (g, true);
        return img;
    }

    static void click (juce::Component& c, float x, float y)
    {
        const juce::Point<float> p (x, y);
        const auto now = juce::Time::getCurrentTime();
        c.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &c, &c, now, p, now, 1, false));
    }

    void runTest() override
    {
        beginTest ("icons");
        expect (iconPath (Icons::plus, { 10.0f, 10.0f, 48.0f, 48.0f }).getBounds() == juce::Rectangle<float> (20.0f, 20.0f, 28.0f, 28.0f));
        for (auto* d : { Icons::file, Icons::plus, Icons::upload, Icons::alert, Icons::copy, Icons::grip, Icons::play, Icons::pause,
                         Icons::speaker, Icons::speakerOff, Icons::loop })
        {
            const auto p = iconPath (d, { 24.0f, 24.0f });
            expect (! p.isEmpty() && juce::Rectangle<float> (-2.0f, -2.0f, 28.0f, 28.0f).contains (p.getBounds()), d);
        }

        beginTest ("pill");
        SegmentedPill pill ({ "4 stems", "6 stems" });
        pill.setSize (pill.preferredWidth(), 38);
        int changed = -1;
        pill.onChange = [&] (int i) { changed = i; };
        expectGreaterThan (pill.preferredWidth(), 2 * (4 + 28 + 40));
        click (pill, (float) pill.getWidth() - 10.0f, 19.0f);
        expectEquals (changed, 1);
        expectEquals (pill.getSelected(), 1);
        changed = -1;
        click (pill, (float) pill.getWidth() - 10.0f, 19.0f);   // already selected
        pill.setEnabled (false);
        click (pill, 10.0f, 19.0f);
        expectEquals (changed, -1);
        expectEquals (pill.getSelected(), 1);

        beginTest ("layout");
        StemRow row (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vocals.wav"), "vocals");
        row.setSize (674, 56);
        expectEquals (row.light().getX(), 142);
        expectEquals (row.light().getWidth(), 38);
        expectEquals (row.waveform().getX(), 194);
        expectEquals (row.waveform().getWidth(), 376);
        expectEquals (row.waveform().getHeight(), 36 + 10);
        expectEquals (row.getHeight(), 56);
        expectEquals (row.volumeBar().getWidth(), 96);
        expectEquals (row.volumeBar().getHeight(), 12);
        expectEquals (row.volumeBar().getX(), 24);
        expect (row.volumeBar().getBottom() <= row.getHeight() && row.volumeBar().getY() > 20);
        expectLessOrEqual (row.chipBounds().getRight(), 664);
        expectEquals (row.chipBounds().getHeight(), 34);
        expectEquals (row.barCount(), 104);

        beginTest ("colour bar instead of an icon");
        {
            const auto img = render (row);
            const auto c = themeFor ("dusk").stemColour ("vocals");
            const auto at = img.getPixelAt (12, 28);                        // the 4 x 30 bar at x 10, centred in 56
            expectWithinAbsoluteError ((int) at.getRed(), (int) c.getRed(), 3);
            expectWithinAbsoluteError ((int) at.getGreen(), (int) c.getGreen(), 3);
            expectWithinAbsoluteError ((int) at.getBlue(), (int) c.getBlue(), 3);
            expectEquals ((int) img.getPixelAt (18, 28).getAlpha(), 0);     // the old tinted badge covered x 10-42
        }

        beginTest ("no keyboard focus");
        expect (! row.getWantsKeyboardFocus() && ! row.waveform().getWantsKeyboardFocus() && ! row.light().getWantsKeyboardFocus());
        expect (! row.light().getMouseClickGrabsKeyboardFocus());
        expect (! pill.getWantsKeyboardFocus());
        LisnButton button ("New song", LisnButton::Style::Ghost);
        expect (! button.getWantsKeyboardFocus() && ! button.getMouseClickGrabsKeyboardFocus());

        beginTest ("compact");
        row.setCompact (true);
        expectEquals (row.getHeight(), 36);
        expectEquals (row.light().getWidth(), 30);
        expectEquals (row.waveform().getX(), 186);
        expectEquals (row.waveform().getHeight(), 24 + 10);
        expectEquals (row.barCount(), 96);
        row.setCompact (false);
        expectEquals (row.getHeight(), 56);
        expectEquals (row.waveform().getX(), 194);

        beginTest ("volume bar: setVolume is silent, the mouse reports");
        {
            float got = -1.0f;
            row.onVolume = [&] (float v) { got = v; };
            row.setVolume (0.4f);
            expectEquals (got, -1.0f);
            expectEquals (row.volumeBar().getValue(), 0.4f);
            row.volumeBar().reset();
            expectEquals (got, 1.0f);
        }

        beginTest ("any envelope size");
        row.setEnvelope ({ 0.5f });
        row.setEnvelope (std::vector<float> (500, 2.0f));
        row.setEnvelope ({});
        render (row);

        beginTest ("played part");
        row.setEnvelope (std::vector<float> (104, 1.0f));
        row.setPosition (0.5);
        const auto wave = render (row.waveform());
        const auto alphaAt = [&] (int x, int y) { return (int) wave.getPixelAt (x, y).getAlpha(); };
        // Bar 10 (x 36.2 .. 38.3) is played; bar 80 (x 289.2 .. 291.4) is dim (stem colour at 0.3). No playhead line here.
        expectGreaterThan (alphaAt (37, 25), 240);
        expectWithinAbsoluteError (alphaAt (290, 25), 77, 4);
        expectEquals (alphaAt (188, 1), 0);

        beginTest ("the view picks what's drawn, the played part and the seek position");
        {
            std::vector<float> env (1000, 0.0f);
            std::fill (env.begin() + 500, env.end(), 1.0f);           // silent first half, loud second half
            row.setEnvelope (env);
            row.setPosition (0.0);
            row.setView ({ 0.0, 0.5 });
            expectEquals ((int) render (row.waveform()).getPixelAt (37, 8).getAlpha(), 0);            // flat bars only
            row.setView ({ 0.5, 1.0 });
            expectGreaterThan ((int) render (row.waveform()).getPixelAt (37, 8).getAlpha(), 60);      // full bars
            row.setPosition (0.75);                                   // the middle of this view
            const auto img = render (row.waveform());
            expectGreaterThan ((int) img.getPixelAt (37, 25).getAlpha(), 240);                        // played
            expectWithinAbsoluteError ((int) img.getPixelAt (290, 25).getAlpha(), 77, 4);             // not yet
            row.setView ({ 0.0, 1.0 });
            row.setPosition (0.5);
        }

        beginTest ("mute light");
        int toggles = 0, solos = 0;
        row.onToggleMute = [&] { ++toggles; };
        row.onSolo = [&] { ++solos; };
        row.light().clicked (juce::ModifierKeys());
        row.light().clicked (juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));
        expectEquals (toggles, 1);
        expectEquals (solos, 1);
        row.setMuted (true);
        expect (row.light().isMuted());
        expectWithinAbsoluteError (row.waveform().getAlpha(), 0.38f, 0.005f);   // Component alpha is stored in 1/255 steps
        row.setMuted (false);
        expectEquals (row.waveform().getAlpha(), 1.0f);
        render (row);

        beginTest ("the waveform reports the mouse as song fractions through the view, with Alt and Ctrl");
        {
            std::vector<std::tuple<WaveMouse, double, bool, bool>> got;
            row.onWaveMouse = [&] (WaveMouse m, double f, bool alt, bool ctrl) { got.push_back ({ m, f, alt, ctrl }); };
            auto& w = row.waveform();
            auto event = [&] (float x, juce::ModifierKeys mods)
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::Point<float> p (x, 20.0f);
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, &w, &w, now, p, now, 1, false);
            };
            row.setView ({ 0.5, 1.0 });
            w.mouseMove (event (94.0f, {}));
            w.mouseDown (event (94.0f, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
            w.mouseDrag (event (500.0f, juce::ModifierKeys (juce::ModifierKeys::altModifier)));   // past the right edge
            w.mouseUp (event (188.0f, {}));
            w.mouseExit (event (188.0f, {}));
            expectEquals ((int) got.size(), 5);
            expect (std::get<0> (got[0]) == WaveMouse::move && std::get<0> (got[1]) == WaveMouse::down
                    && std::get<0> (got[2]) == WaveMouse::drag && std::get<0> (got[3]) == WaveMouse::up
                    && std::get<0> (got[4]) == WaveMouse::exit);
            expectWithinAbsoluteError (std::get<1> (got[1]), 0.625, 1e-9);   // x 94 = 0.25 of the 0.5-wide view
            expectWithinAbsoluteError (std::get<1> (got[2]), 1.0, 1e-9);     // clamped to the view
            expectWithinAbsoluteError (std::get<1> (got[3]), 0.75, 1e-9);
            expect (std::get<2> (got[2]) && ! std::get<2> (got[1]));
            expect (std::get<3> (got[1]) && ! std::get<3> (got[2]));
            row.setView ({ 0.0, 1.0 });
        }
    }
};

static StemRowTests stemRowTests;
