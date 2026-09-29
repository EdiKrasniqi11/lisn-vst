#include "../Source/StemRow.h"
#include "../Source/Icons.h"

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
                         Icons::speaker, Icons::speakerOff,
                         Icons::vocals, Icons::drums, Icons::bass, Icons::guitar, Icons::piano, Icons::other })
        {
            const auto p = iconPath (d, { 24.0f, 24.0f });
            expect (! p.isEmpty() && juce::Rectangle<float> (-2.0f, -2.0f, 28.0f, 28.0f).contains (p.getBounds()), d);
        }
        expect (stemIcon ("drums") == Icons::drums);
        expect (stemIcon ("kazoo") == Icons::other);

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
        row.setSize (674, 62);
        expectEquals (row.light().getX(), 142);
        expectEquals (row.light().getWidth(), 38);
        expectEquals (row.waveform().getX(), 194);
        expectEquals (row.waveform().getWidth(), 376);
        expectEquals (row.waveform().getHeight(), 40 + 10);
        expectLessOrEqual (row.chipBounds().getRight(), 664);
        expectEquals (row.chipBounds().getHeight(), 34);
        expectEquals (row.barCount(), 104);

        beginTest ("no keyboard focus");
        expect (! row.getWantsKeyboardFocus() && ! row.waveform().getWantsKeyboardFocus() && ! row.light().getWantsKeyboardFocus());
        expect (! row.light().getMouseClickGrabsKeyboardFocus());
        expect (! pill.getWantsKeyboardFocus());
        LisnButton button ("New song", LisnButton::Style::Ghost);
        expect (! button.getWantsKeyboardFocus() && ! button.getMouseClickGrabsKeyboardFocus());

        beginTest ("compact");
        row.setCompact (true);
        expectEquals (row.getHeight(), 42);
        expectEquals (row.waveform().getHeight(), 26 + 10);
        expectEquals (row.barCount(), 96);
        row.setCompact (false);
        expectEquals (row.getHeight(), 62);

        beginTest ("wrong peak count");
        row.setPeaks ({ 0.5f });
        row.setPeaks (std::vector<float> (500, 2.0f));
        row.setPeaks ({});
        render (row);

        beginTest ("played part");
        row.setPeaks (std::vector<float> (104, 1.0f));
        row.setPosition (0.5);
        const auto wave = render (row.waveform());
        const auto alphaAt = [&] (int x, int y) { return (int) wave.getPixelAt (x, y).getAlpha(); };
        // Bar 10 (x 36.2 .. 38.3) is played; bar 80 (x 289.2 .. 291.4) is dim (stem colour at 0.3). No playhead line here.
        expectGreaterThan (alphaAt (37, 25), 240);
        expectWithinAbsoluteError (alphaAt (290, 25), 77, 4);
        expectEquals (alphaAt (188, 1), 0);

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
    }
};

static StemRowTests stemRowTests;
