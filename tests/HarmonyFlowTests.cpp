#include "FakeHarmony.h"
#include "../Source/HarmonyFlow.h"
#include "../Source/PluginProcessor.h"

using namespace FakeHarmony;

struct HarmonyFlowTests : juce::UnitTest
{
    HarmonyFlowTests() : juce::UnitTest ("Harmony flow") {}

    static void waitUntilIdle (HarmonyFlow& flow)   // the flow's timer needs the message loop
    {
        for (int i = 0; i < 100 && (flow.getStep() == HarmonyFlow::Step::vocals || flow.getStep() == HarmonyFlow::Step::harmonies); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    void runTest() override
    {
        beginTest ("a cached song runs only the harmony pass");
        {
            CachedSong s;
            HarmonyFlow flow;
            flow.harmonyJobForTests().testCommand = okPass (s);
            flow.start (s.file, "best", {});
            waitUntilIdle (flow);
            expect (flow.getStep() == HarmonyFlow::Step::done, flow.getError());
            expect (flow.getStemDir() == s.stems);
            expect (HarmonyJob::hasResult (flow.getStemDir()));
            expectEquals (HarmonyJob::qualityOf (flow.getStemDir()), juce::String ("best"));
            expectEquals (flow.getProgress(), 1.0f);
        }

        beginTest ("an existing result is kept, or redone at another quality");
        {
            CachedSong s;
            HarmonyFlow flow;
            flow.harmonyJobForTests().testCommand = okPass (s);
            flow.start (s.file, "best", {});
            waitUntilIdle (flow);
            expectEquals (HarmonyJob::qualityOf (s.stems), juce::String ("best"));

            flow.start (s.file, "quick", {});
            expect (flow.getStep() == HarmonyFlow::Step::done);   // at once, no waiting
            expectEquals (HarmonyJob::qualityOf (s.stems), juce::String ("best"));

            flow.start (s.file, "quick", {}, false);
            expect (flow.getStep() == HarmonyFlow::Step::harmonies);
            waitUntilIdle (flow);
            expect (flow.getStep() == HarmonyFlow::Step::done, flow.getError());
            expectEquals (HarmonyJob::qualityOf (s.stems), juce::String ("quick"));
        }

        beginTest ("cancel during the harmony pass leaves nothing half-done");
        {
            CachedSong s;
            HarmonyFlow flow;
            flow.harmonyJobForTests().testCommand = fakeCommand (s.root.getChildFile ("slow"),
                "@echo off\r\nping -n 4 127.0.0.1 >nul\r\n", "#!/bin/sh\nexec sleep 3\n");
            flow.start (s.file, "quick", {});
            juce::Thread::sleep (300);
            expect (flow.getStep() == HarmonyFlow::Step::harmonies);
            flow.cancel();
            expect (flow.getStep() == HarmonyFlow::Step::idle);
            expect (! s.stems.getChildFile ("harmony").exists() && ! s.stems.getChildFile ("harmony.part").exists());
            expectEquals (SeparationJob::stemsIn (s.stems).size(), 4);
            juce::Thread::sleep (3500);   // the orphaned ping/sleep lets go of the folder
        }

        beginTest ("a failing harmony pass reports failed with its words");
        {
            CachedSong s;
            HarmonyFlow flow;
            flow.harmonyJobForTests().testCommand = fakeCommand (s.root.getChildFile ("bad"),
                "@echo off\r\necho NEEDS SETUP: no model\r\nexit /b 3\r\n", "#!/bin/sh\necho NEEDS SETUP: no model\nexit 3\n");
            flow.start (s.file, "quick", {});
            waitUntilIdle (flow);
            expect (flow.getStep() == HarmonyFlow::Step::failed);
            expect (flow.getError().contains ("NEEDS SETUP"), flow.getError());
            expect (flow.getErrorKind() == ErrorKind::Failed);
        }
    }
};

static HarmonyFlowTests harmonyFlowTests;
