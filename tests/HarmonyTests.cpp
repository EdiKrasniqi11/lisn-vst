#include "FakeHarmony.h"
#include "../Source/HarmonyJob.h"
#include "../Source/Screens.h"
#include "../Source/Theme.h"
#include <juce_cryptography/juce_cryptography.h>

using namespace FakeHarmony;

struct HarmonyTests : juce::UnitTest
{
    HarmonyTests() : juce::UnitTest ("Harmony Split") {}

    static juce::StringArray menuItems (const juce::PopupMenu& m)   // "text", or "text [ticked]"
    {
        juce::StringArray items;
        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            items.add (it.getItem().text + (it.getItem().isTicked ? " [ticked]" : ""));
        return items;
    }

    // A HarmonyJob running a fake pass until it leaves running or 10 s pass.
    HarmonyJob::State runFake (HarmonyJob& job, const juce::File& dir, const juce::String& windowsScript, const juce::String& shScript,
                               const juce::String& quality = "quick")
    {
        job.testCommand = fakeCommand (dir.getSiblingFile ("fake"), windowsScript, shScript);
        job.start (dir, {}, quality);
        for (int i = 0; i < 1000 && job.getState() == HarmonyJob::State::running; ++i) juce::Thread::sleep (10);
        return job.getState();
    }

    void runTest() override
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-harmony", {});
        const auto four = root.getChildFile ("htdemucs"), six = root.getChildFile ("htdemucs_6s");
        for (auto stem : { "vocals", "drums", "bass", "other" })
            writeWav (four.getChildFile (juce::String (stem) + ".wav"));
        for (auto stem : { "vocals", "drums", "bass", "guitar", "piano", "other" })
            writeWav (six.getChildFile (juce::String (stem) + ".wav"));

        beginTest ("the button is on 4-stem splits only; its menu ticks the result's quality and opens it");
        {
            StemsScreen s;
            s.setBounds (0, 0, 712, 398);
            s.setStems (four, "Song");
            expect (s.hasHarmonyButton());
            expectEquals (s.numRows(), 4);                   // Lead and Harmonies live on their own screen
            s.setStems (six, "Song");
            expect (! s.hasHarmonyButton());

            s.setHarmonyButton ("Splitting 42%");
            expectEquals (s.harmonyText(), juce::String ("Splitting 42%"));
            expectEquals (menuItems (s.harmonyMenu ({}, false)).joinIntoString ("|"),
                          juce::String ("Quick split|Best quality, 900 MB download first"));
            expectEquals (menuItems (s.harmonyMenu ("best", true)).joinIntoString ("|"),
                          juce::String ("Quick split|Best quality [ticked]|Open Harmonies"));
        }

        beginTest ("the Harmonies screen: Lead and Harmonies files, faders both ways");
        {
            HarmonyScreen h;
            h.setBounds (0, 0, 712, 398);
            h.setSong (four, "Song", "best");
            expectEquals (h.getFiles()[0].getFullPathName(), four.getChildFile ("harmony/lead.wav").getFullPathName());
            expectEquals (h.getFiles()[1].getFileName(), juce::String ("harmonies.wav"));
            float heard = -1.0f;
            int lane = -1;
            h.onVolume = [&] (int i, float v) { lane = i; heard = v; };
            h.fader (1).onChange (0.4f);
            expectEquals (lane, 1);
            expectEquals (heard, 0.4f);
            h.setVolume (0, 0.25f);
            expectEquals (h.fader (0).getValue(), 0.25f);
            h.setAudible (1u);                               // Lead soloed: Harmonies dims
            h.setPlayback (true, 0.5, 60.0);
        }

        beginTest ("a model download keeps the file only when its hash matches, and leaves nothing when stopped");
        {
            const auto src = root.getChildFile ("model.bin"), dest = root.getChildFile ("models/model.bin");
            juce::MemoryBlock data;
            for (int i = 0; i < 300000; ++i) data.append (&i, 1);   // a few chunks' worth
            src.replaceWithData (data.getData(), data.getSize());
            const auto sha = juce::SHA256 (src).toHexString();
            const auto none = [&] { return ! dest.exists() && ! dest.getSiblingFile ("model.bin.part").exists(); };

            float last = 0.0f;
            expect (HarmonyJob::download (juce::URL (src), sha, dest, [&] (float f) { last = f; return true; }));
            expect (dest.existsAsFile() && dest.getSize() == src.getSize());
            expectEquals (last, 1.0f);
            dest.deleteFile();

            expect (! HarmonyJob::download (juce::URL (src), juce::String::repeatedString ("0", 64), dest, [] (float) { return true; }));
            expect (none(), "a wrong hash keeps nothing");
            expect (! HarmonyJob::download (juce::URL (src), sha, dest, [] (float) { return false; }));
            expect (none(), "a stop keeps nothing");
            expect (! HarmonyJob::download (juce::URL (root.getChildFile ("missing.bin")), sha, dest, [] (float) { return true; }));
            expect (none());
        }

       #if JUCE_MAC
        beginTest ("the Best quality model folder is in Application Support");
        expect (HarmonyJob::userModelDir().getFullPathName().endsWith ("Library/Application Support/LISN/models/harmony"),
                HarmonyJob::userModelDir().getFullPathName());

        beginTest ("Mac: the Quick model is found beside the venv's python3");
        {
            const auto venv = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-harmony-venv", {});
            const auto model = venv.getChildFile ("models/harmony/UVR_MDXNET_KARA_2.onnx");
            expect (model.create().wasOk());
            expectEquals (HarmonyJob::findModel ("quick", venv.getChildFile ("bin/python3").getFullPathName()).getFullPathName(), model.getFullPathName());
            venv.deleteRecursively();
        }
       #endif

        beginTest ("the header's name: StemSplitter or Harmonies");
        {
            Header hd;
            hd.setBounds (24, 16, 712, 44);
            expect (! hd.showsHarmonies() && ! hd.isSpinning());
            hd.spinTo (true, false);
            expect (hd.showsHarmonies() && ! hd.isSpinning());
            hd.spinTo (false, true);
            expect (! hd.showsHarmonies() && hd.isSpinning());
            hd.spinTo (false, false);
            expect (! hd.isSpinning());
        }

        beginTest ("a pass moves a finished result into harmony/, with progress");
        {
            HarmonyJob job;
            const auto vocals = four.getChildFile ("vocals.wav").getFullPathName();
            expectEquals (HarmonyJob::qualityOf (four), juce::String());
            const auto state = runFake (job, four,
                "@echo off\r\necho 37%%^|\r\ncopy /y \"" + vocals + "\" \"%~1\\lead.wav\" >nul\r\n"
                "copy /y \"" + vocals + "\" \"%~1\\harmonies.wav\" >nul\r\n"
                "echo %~2> \"%~1\\quality.txt\"\r\n",
                "#!/bin/sh\necho '37%|'\ncp \"" + vocals + "\" \"$1/lead.wav\"\n"
                "cp \"" + vocals + "\" \"$1/harmonies.wav\"\n"
                "echo \"$2\" > \"$1/quality.txt\"\n", "best");
            expect (state == HarmonyJob::State::done, job.getError());
            expect (HarmonyJob::hasResult (four));
            expectEquals (HarmonyJob::qualityOf (four), juce::String ("best"));
            expect (! four.getChildFile ("harmony.part").exists());
            expectEquals (job.getProgress(), 1.0f);
        }
        four.getChildFile ("harmony").deleteRecursively();

        beginTest ("a failed pass leaves nothing and keeps the script's words");
        {
            HarmonyJob job;
            const auto state = runFake (job, four, "@echo off\r\necho NEEDS SETUP: no model\r\nexit /b 3\r\n",
                                    "#!/bin/sh\necho NEEDS SETUP: no model\nexit 3\n");
            expect (state == HarmonyJob::State::failed);
            expect (job.getError().contains ("NEEDS SETUP"), job.getError());
            expect (! four.getChildFile ("harmony").exists() && ! four.getChildFile ("harmony.part").exists());
        }

        beginTest ("cancel kills the pass, leaves nothing and goes back to idle");
        {
            HarmonyJob job;
            job.testCommand = fakeCommand (root.getChildFile ("fake"),
                                           "@echo off\r\nping -n 4 127.0.0.1 >nul\r\n", "#!/bin/sh\nexec sleep 3\n");
            job.start (four, {});
            juce::Thread::sleep (300);
            expect (job.getState() == HarmonyJob::State::running);
            job.cancel();
            expect (job.getState() == HarmonyJob::State::idle);
            expect (! four.getChildFile ("harmony").exists() && ! four.getChildFile ("harmony.part").exists());
        }

        juce::Thread::sleep (3500);   // the orphaned ping/sleep lets go of the folder
        root.deleteRecursively();
    }
};

static HarmonyTests harmonyTests;
