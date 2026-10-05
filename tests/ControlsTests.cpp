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

        beginTest ("SpeedStrip: the right-click menu resets a box like a double-click, greyed out when it is already reset");
        auto item = [&] (SpeedStrip::Target t)
        {
            const auto menu = strip.resetMenu (t);
            juce::PopupMenu::MenuItemIterator it (menu);
            expect (it.next());
            return it.getItem();
        };
        expectEquals (item (SpeedStrip::Target::tempo).text, juce::String (juce::CharPointer_UTF8 ("Reset to 1.00\xc3\x97")));   // no tempo
        expectEquals (item (SpeedStrip::Target::key).text, juce::String ("Reset to +0 st"));
        expect (! item (SpeedStrip::Target::tempo).isEnabled);
        expect (! item (SpeedStrip::Target::key).isEnabled);
        strip.setSpeed (0.85);
        strip.setPitch (-2);
        strip.setBpm (92.4);
        const auto tempoReset = item (SpeedStrip::Target::tempo), keyReset = item (SpeedStrip::Target::key);
        expectEquals (tempoReset.text, juce::String ("Reset to 92 BPM"));
        expect (tempoReset.isEnabled);
        expect (keyReset.isEnabled);
        heard = -1.0;
        heardKey = 99;
        tempoReset.action();                         // what choosing it does
        keyReset.action();
        expectEquals (strip.getSpeed(), 1.0);
        expectEquals (heard, 1.0);
        expectEquals (strip.getPitch(), 0);
        expectEquals (heardKey, 0);

        beginTest ("SpeedStrip: a right-click on a box opens the menu and never drags or steps; a left click still steps");
        strip.setSpeed (0.85);
        strip.setPitch (3);
        heard = -1.0;
        heardKey = 99;
        auto gesture = [&] (juce::ModifierKeys::Flags button, juce::Point<float> from, juce::Point<float> to)
        {
            auto at = [&] (juce::Point<float> p)
            {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (button), 0.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, &strip, &strip, now, from, now, 1, p != from);
            };
            strip.mouseDown (at (from));
            strip.mouseDrag (at (to));
            strip.mouseUp (at (to));
        };
        const auto right = juce::ModifierKeys::rightButtonModifier;
        gesture (right, { 40.0f, 20.0f }, { 40.0f, 0.0f });       // a drag up on Tempo
        expect (juce::PopupMenu::dismissAllActiveMenus());          // it opened the menu
        gesture (right, { 130.0f, 10.0f }, { 130.0f, 10.0f });     // Tempo's up chevron
        gesture (right, { 250.0f, 30.0f }, { 250.0f, 26.0f });     // Key's down chevron, then a drag up
        juce::PopupMenu::dismissAllActiveMenus();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);   // deletes the dismissed menus
        expectEquals (strip.getSpeed(), 0.85);
        expectEquals (strip.getPitch(), 3);
        expectEquals (heard, -1.0);
        expectEquals (heardKey, 99);
        gesture (right, { 400.0f, 20.0f }, { 400.0f, 20.0f });     // outside the boxes: nothing
        expect (! juce::PopupMenu::dismissAllActiveMenus());
        gesture (juce::ModifierKeys::leftButtonModifier, { 250.0f, 30.0f }, { 250.0f, 30.0f });
        expectEquals (strip.getPitch(), 2);

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
