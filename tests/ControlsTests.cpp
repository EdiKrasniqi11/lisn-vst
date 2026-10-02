#include "../Source/Controls.h"

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
    }
};

static ControlsTests controlsTests;
