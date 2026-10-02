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

        beginTest ("SpeedStrip maps x to 0.5..1.5 in 0.05 steps and sticks at 1");
        expectEquals (SpeedStrip::speedAt (100.0f, 100.0f, 500.0f), 0.5);
        expectEquals (SpeedStrip::speedAt (500.0f, 100.0f, 500.0f), 1.5);
        expectEquals (SpeedStrip::speedAt (303.0f, 100.0f, 500.0f), 1.0);   // within 4 px of the centre (300)
        expectEquals (SpeedStrip::speedAt (400.0f, 100.0f, 500.0f), 1.25);
        expectEquals (SpeedStrip::speedAt (362.0f, 100.0f, 500.0f), 1.15);  // 1.155 rounds to the 0.05 step 1.15
        expectEquals (SpeedStrip::speedAt (0.0f, 100.0f, 500.0f), 0.5);     // clamped

        beginTest ("SpeedStrip: setSpeed is silent, Reset reports 1 and nothing takes focus");
        SpeedStrip strip;
        strip.setSize (674, 30);
        double heard = -1.0;
        strip.onChange = [&] (double s) { heard = s; };
        strip.setSpeed (0.85);
        expectEquals (heard, -1.0);
        expectEquals (strip.getSpeed(), 0.85);
        expect (! strip.getWantsKeyboardFocus());
        strip.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 300.0f, 10.0f }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                  &strip, &strip, juce::Time::getCurrentTime(), { 300.0f, 10.0f }, juce::Time::getCurrentTime(), 2, false));
        expectEquals (heard, 1.0);
        expectEquals (strip.getSpeed(), 1.0);

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
