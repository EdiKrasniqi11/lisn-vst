#include "../Source/UpdateCheck.h"
#include "../Source/Screens.h"
#include "../Source/PluginEditor.h"

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

            // The longest texts stay clear of the pills: the desktop app's "Downloading 99%"; in a plugin "Restart FL Studio
            // to finish" becomes "Restart to finish" after the name, on StemSplitter and on Harmonies alike.
            if (titleBar)
            {
                h.showUpdate ("0.2.5", "Downloading 99%");
                expect (pill->isVisible() && pill->getRight() + 12 <= firstPill->getX(), juce::String (pill->getRight()));
            }
            else
                for (const bool harmonies : { false, true })
                {
                    h.spinTo (harmonies, false);
                    h.showUpdate ("0.2.5", "Restart FL Studio to finish", "Restart to finish");
                    expect (pill->isVisible(), "on Harmonies: " + juce::String ((int) harmonies));
                    expectEquals (pill->getButtonText(), juce::String ("Restart to finish"));
                    expect (pill->getX() > 200, juce::String (pill->getX()));   // after "LISN StemSplitter", never over it
                    expect (pill->getRight() + 12 <= firstPill->getX(), juce::String (pill->getRight()));
                    h.showUpdate ("0.2.5", "Restart Ableton Live to finish", "Restart to finish");
                    expect (pill->getRight() + 12 <= firstPill->getX(), juce::String (pill->getRight()));
                }
            h.spinTo (false, false);

            h.showUpdate ({});
            expect (! pill->isVisible());
        }

        beginTest ("the update panel");
        {
            StemSplitterProcessor proc;
            proc.introShown = true;
            StemSplitterEditor ed (proc);
            UiState error;
            error.screen = Screen::Error;
            ed.forceState (error);
            ed.tick();
            auto& panel = ed.updateScreen();
            expect (! panel.isVisible() && ed.errorScreen().isVisible());
            ed.headerBar().onUpdate();                       // the pill opens it in place of the screen
            expect (panel.isVisible() && ! ed.errorScreen().isVisible());
            ed.headerBar().onUpdate();                       // and closes it
            expect (! panel.isVisible() && ed.errorScreen().isVisible());
            ed.headerBar().onUpdate();
            ed.setHelpOpen (true);                           // help takes the panel's place
            expect (! panel.isVisible() && ed.helpSheet().isVisible());
            ed.setUpdateOpen (true);                         // and the other way round
            expect (panel.isVisible() && ! ed.helpSheet().isVisible());
            panel.onLater();                                 // Later: back to the screen that was there
            expect (! panel.isVisible() && ! ed.helpSheet().isVisible() && ed.errorScreen().isVisible());
        }
        {
            // An install changed the engine: no split starts, from a drop or from the 4/6 switch; the Error screen says why.
            StemSplitterProcessor proc;
            proc.introShown = true;
            StemSplitterEditor ed (proc);
            ed.forceState (UiState());
            ed.tick();
            ed.setEngineChangedForTests();
            expect (! ed.errorScreen().isVisible());
            const auto song = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lisn-gate-test.wav");
            ed.filesDropped ({ song.getFullPathName() }, 0, 0);
            expect (proc.job.getState() != JobState::Running && proc.getLastInput() != song);
            expect (ed.errorScreen().isVisible());
            ed.headerBar().onSixStems (true);
            expect (! proc.isSixStems() && proc.job.getState() != JobState::Running);
            expect (ed.errorScreen().isVisible());
        }
        {
            UpdateScreen u;
            u.setBounds (0, 0, 712, 400);
            ReleaseInfo r;
            r.version = "0.3.1";
            r.notes = { "**Harmonies**: split the lead from the backing vocals", "Faster splits", "Fixes" };
            r.installerUrl = "https://example.com/LISN-Setup.exe";
            r.installerSize = 26 * 1024 * 1024;
            u.setRelease (r, "0.3.0");
            u.setView (UpdateScreen::View::whatsNew, 0.0f, true, false, "FL Studio");
            expect (! u.primaryButton().isEnabled());        // a split is running
            expectEquals (u.primaryButton().getButtonText(), juce::String ("Update now"));
            u.setView (UpdateScreen::View::whatsNew, 0.0f, false, false, "FL Studio");
            expect (u.primaryButton().isEnabled());          // the job ended: it wakes up in place

            juce::Image shot (juce::Image::RGB, 152, 100, true);
            u.setScreenshots ({ shot, shot, shot });
            expectEquals (u.shownScreenshot(), 0);
            const auto p = u.dotArea (1).getCentre();
            const auto now = juce::Time::getCurrentTime();
            u.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         &u, &u, now, p, now, 1, false));
            expectEquals (u.shownScreenshot(), 1);
        }
    }
};


static UpdateCheckTests updateCheckTests;
