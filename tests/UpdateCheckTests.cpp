#include "../Source/UpdateCheck.h"
#include "../Source/Screens.h"

struct UpdateCheckTests : juce::UnitTest
{
    UpdateCheckTests() : juce::UnitTest ("UpdateCheck") {}

    void runTest() override
    {
        beginTest ("newer versions");
        expect (isNewerVersion ("v0.2.5", "0.2.4"));
        expect (isNewerVersion ("v0.3.0", "0.2.9"));
        expect (isNewerVersion ("v1.0", "0.9.9"));
        expect (isNewerVersion ("0.2.10", "0.2.9"));        // numbers, not text
        expect (isNewerVersion ("v0.2.4.1", "0.2.4"));      // a missing part is 0

        beginTest ("not newer");
        expect (! isNewerVersion ("v0.2.4", "0.2.4"));
        expect (! isNewerVersion ("v0.2.3", "0.2.4"));
        expect (! isNewerVersion ("v0.2", "0.2.0"));
        expect (! isNewerVersion ("", "0.2.4"));
        expect (! isNewerVersion ("v0.3.0-beta", "0.2.4"));  // only plain releases
        expect (! isNewerVersion ("latest", "0.2.4"));
        expect (! isNewerVersion ("v0..3", "0.2.4"));

        beginTest ("the header's pill");
        for (const bool titleBar : { false, true })
        {
            Header h;
            if (titleBar) h.makeTitleBar ([] {}, [] {});
            h.setBounds (24, 16, 712, 44);
            UpdatePill* pill = nullptr;
            SegmentedPill* firstPill = nullptr;
            for (auto* c : h.getChildren())
            {
                if (auto* p = dynamic_cast<UpdatePill*> (c)) pill = p;
                if (auto* p = dynamic_cast<SegmentedPill*> (c); p != nullptr && firstPill == nullptr) firstPill = p;
            }
            expect (pill != nullptr && firstPill != nullptr && ! pill->isVisible());
            if (pill == nullptr || firstPill == nullptr) continue;

            h.showUpdate ("0.2.5");
            expect (pill->isVisible());
            expectEquals (pill->getButtonText(), juce::String ("Update 0.2.5"));
            expectEquals (pill->getHeight(), 26);
            expectEquals (pill->getY(), 9);
            expect (pill->getX() > (titleBar ? 100 : 200), juce::String (pill->getX()));      // after "LISN" / "LISN StemSplitter"
            expect (pill->getRight() + 24 < firstPill->getX(), juce::String (pill->getRight()));   // clear of the theme pill

            int clicks = 0;
            h.onUpdate = [&] { ++clicks; };
            pill->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            expectEquals (clicks, 1);

            h.showUpdate ({});
            expect (! pill->isVisible());
        }
    }
};

static UpdateCheckTests updateCheckTests;
