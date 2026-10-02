#include "../Source/Controls.h"
#include "../Source/Screens.h"

struct ControlsTests : juce::UnitTest
{
    ControlsTests() : juce::UnitTest ("Controls") {}

    void runTest() override
    {
        beginTest ("VolumeBar maps x to 0..1 inside the thumb's radius");
        expectEquals (VolumeBar::valueAt (5.0f, 96.0f), 0.0f);
        expectEquals (VolumeBar::valueAt (91.0f, 96.0f), 1.0f);
        expectWithinAbsoluteError (VolumeBar::valueAt (48.0f, 96.0f), 0.5f, 0.001f);
        expectEquals (VolumeBar::valueAt (-20.0f, 96.0f), 0.0f);
        expectEquals (VolumeBar::valueAt (200.0f, 96.0f), 1.0f);

        beginTest ("VolumeBar: setValue is silent, a double-click resets to 1 and reports it");
        VolumeBar bar;
        bar.setSize (96, 12);
        float got = -1.0f;
        bar.onChange = [&] (float v) { got = v; };
        bar.setValue (0.3f);
        expectEquals (got, -1.0f);
        bar.reset();                                 // what a double-click does
        expectEquals (got, 1.0f);
        expectEquals (bar.getValue(), 1.0f);
        expect (! bar.getWantsKeyboardFocus());

        beginTest ("SpeedStrip steps whole BPM (0.05x with no tempo) within 0.5..2");
        expectWithinAbsoluteError (SpeedStrip::stepSpeed (1.0, 1, 92.0), 93.0 / 92.0, 1e-9);
        expectWithinAbsoluteError (SpeedStrip::stepSpeed (1.15, -3, 92.0), 103.0 / 92.0, 1e-9);   // 1.15 x 92 = 105.8 shows 106
        expectEquals (SpeedStrip::stepSpeed (1.0, 2, 0.0), 1.1);
        expectEquals (SpeedStrip::stepSpeed (1.95, 5, 0.0), 2.0);                                  // clamped
        expectEquals (SpeedStrip::stepSpeed (1.0, -200, 92.0), 0.5);

        beginTest ("SpeedStrip: setSpeed is silent, a double-click on a box resets it and nothing takes focus");
        SpeedStrip strip;
        strip.setSize (654, 40);
        double heard = -1.0;
        int heardKey = 99;
        strip.onChange = [&] (double s) { heard = s; };
        strip.onPitch = [&] (int st) { heardKey = st; };
        strip.setSpeed (0.85);
        strip.setPitch (3);
        expectEquals (heard, -1.0);
        expectEquals (heardKey, 99);
        expectEquals (strip.getSpeed(), 0.85);
        expect (! strip.getWantsKeyboardFocus());
        auto dbl = [&] (juce::Point<float> p)
        {
            strip.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                      &strip, &strip, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 2, false));
        };
        dbl ({ 40.0f, 20.0f });
        expectEquals (heard, 1.0);
        dbl ({ 190.0f, 20.0f });
        expectEquals (heardKey, 0);

        beginTest ("a DragChip asks for its file once per gesture, and never while busy");
        DragChip chip;
        chip.setSize (120, 34);
        int asked = 0;
        chip.fileToDrag = [&] { ++asked; return juce::File(); };   // no file: nothing is actually dragged
        auto at = [&] (float x)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, 17.0f }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &chip, &chip, now, { 10.0f, 17.0f }, now, 1, true);
        };
        chip.mouseDown (at (10.0f));
        chip.mouseDrag (at (12.0f));                 // within 4 px
        expectEquals (asked, 0);
        chip.mouseDrag (at (20.0f));
        chip.mouseDrag (at (30.0f));                 // the same gesture
        expectEquals (asked, 1);
        chip.setProgress (0.4f);
        chip.mouseDown (at (10.0f));
        chip.mouseDrag (at (30.0f));
        expectEquals (asked, 1);
    }
};

static ControlsTests controlsTests;
