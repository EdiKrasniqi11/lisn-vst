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

        beginTest ("the cache rule");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lisn-update-test");
            dir.deleteRecursively();
            dir.createDirectory();
            juce::PropertiesFile::Options options;
            options.filenameSuffix = ".settings";
            juce::PropertiesFile seen (dir.getChildFile ("seen.settings"), options);
            const juce::int64 now = 10'000'000'000;
            const auto hour = juce::int64 (3600 * 1000);
            const auto stored = juce::JSON::parse (R"({"tag_name":"v0.9.0"})");
            const auto fresh = juce::JSON::parse (R"({"tag_name":"v1.0.0"})");
            int calls = 0;
            const auto fetchFresh = [&] { ++calls; return fresh; };
            const auto fetchNothing = [&] { ++calls; return juce::var(); };

            seen.setValue ("release", juce::JSON::toString (stored, true));
            seen.setValue ("checked", juce::String (now - hour));
            expectEquals (UpdateCheck::releaseFor (seen, now, fetchFresh)["tag_name"].toString(), juce::String ("v0.9.0"));
            expectEquals (calls, 0);                                         // fresh: no request

            seen.setValue ("checked", juce::String (now - 25 * hour));
            expectEquals (UpdateCheck::releaseFor (seen, now, fetchFresh)["tag_name"].toString(), juce::String ("v1.0.0"));
            expectEquals (calls, 1);                                         // stale: one request, kept
            expect (seen.getValue ("release").contains ("v1.0.0"));
            expectEquals (seen.getValue ("checked"), juce::String (now));
            expectEquals (seen.getValue ("latest"), juce::String ("v1.0.0"));   // for 0.2.5 processes

            seen.setValue ("checked", juce::String (now - 25 * hour));
            expectEquals (UpdateCheck::releaseFor (seen, now, fetchNothing)["tag_name"].toString(), juce::String ("v1.0.0"));
            expectEquals (calls, 2);                                         // offline: the stored one stays
            expectEquals (seen.getValue ("checked"), juce::String (now - 25 * hour));

            seen.setValue ("checked", juce::String (now - 25 * hour));       // GitHub's error body: not a release
            const auto before = seen.getValue ("release");
            expectEquals (UpdateCheck::releaseFor (seen, now, [] { return juce::JSON::parse (R"({"message":"API rate limit exceeded"})"); })["tag_name"].toString(), juce::String ("v1.0.0"));
            expectEquals (seen.getValue ("release"), before);
            expectEquals (seen.getValue ("checked"), juce::String (now - 25 * hour));

            seen.removeValue ("release");                                    // a 0.2.5 cache: only the tag
            seen.setValue ("latest", "v0.9.0");
            seen.setValue ("checked", juce::String (now - hour));
            expectEquals (UpdateCheck::releaseFor (seen, now, fetchFresh)["tag_name"].toString(), juce::String ("v1.0.0"));
            expectEquals (calls, 3);
            dir.deleteRecursively();
        }

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
