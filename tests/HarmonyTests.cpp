#include "FakeHarmony.h"
#include "../Source/HarmonyJob.h"
#include "../Source/Screens.h"
#include "../Source/Theme.h"
#include <juce_cryptography/juce_cryptography.h>

using namespace FakeHarmony;

struct HarmonyTests : juce::UnitTest
{
    HarmonyTests() : juce::UnitTest ("Harmony Split") {}

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

        beginTest ("the choir icon is on 4-stem splits only, has no text and says Harmonies");
        {
            StemsScreen s;
            s.setBounds (0, 0, 712, 398);
            s.setStems (four, "Song");
            expect (s.hasHarmonyButton());
            expectEquals (s.numRows(), 4);                   // Lead and Harmonies live on their own screen
            expectEquals (s.harmonyText(), juce::String());
            expectEquals (s.harmonyControl().getTooltip(), juce::String ("Harmonies"));
            expectEquals (s.harmonyControl().getWidth(), s.harmonyControl().getHeight());   // square
            int clicks = 0;
            s.onHarmony = [&] { ++clicks; };
            s.harmonyControl().onClick();
            expectEquals (clicks, 1);
            s.setStems (six, "Song");
            expect (! s.hasHarmonyButton());
        }

        beginTest ("Harmonies' drop screen: the offer shows the song, Browse and the offer call back");
        {
            HarmonyDropScreen d;
            d.setBounds (0, 0, 712, 398);
            d.setOffer ({});
            expect (! d.offerButton().isVisible());
            d.setOffer ("Liberian Girl");
            expect (d.offerButton().isVisible());
            expectEquals (d.offerButton().getButtonText(), juce::String ("Split the harmonies of Liberian Girl"));
            expect (d.offerButton().getRight() <= 712);
            int offered = 0, browsed = 0;
            d.onOffer = [&] { ++offered; };
            d.onBrowse = [&] { ++browsed; };
            d.offerButton().onClick();
            d.browseButton().onClick();
            expectEquals (offered, 1);
            expectEquals (browsed, 1);
            d.setOffer (juce::String::repeatedString ("A very long song name ", 10));    // elided, never wider than the zone
            expect (d.offerButton().getX() >= 20 && d.offerButton().getRight() <= 692);
            d.setOffer ({});
            expect (! d.offerButton().isVisible());
        }

        beginTest ("Harmonies' progress screen: the step's words and its percentage");
        {
            HarmonySplittingScreen p;
            p.setBounds (0, 0, 712, 398);
            p.setSong ("Liberian Girl");
            p.setStep (1, 0.5f, false);
            expectEquals (p.stepLabel (1), juce::String ("Finding the vocals"));
            expectEquals (p.percentText(), juce::String ("50%"));
            p.setStep (2, 0.42f, false);
            expectEquals (p.stepLabel (2), juce::String ("Splitting the harmonies"));
            expectEquals (p.percentText(), juce::String ("42%"));
            p.setStep (2, 0.1f, true);
            expectEquals (p.stepLabel (2), juce::String ("Downloading the Best quality model"));
            expectEquals (p.percentText(), juce::String ("10%"));
            int cancelled = 0;
            p.onCancel = [&] { ++cancelled; };
            p.cancelButton().onClick();
            expectEquals (cancelled, 1);
        }

        beginTest ("the header's second pill: 4 stems | 6 stems, or Quick | Best quality in Harmonies");
        {
            Header hd;
            hd.setBounds (24, 16, 712, 44);
            expectEquals (hd.stemsPillControl().getItems().joinIntoString ("|"), juce::String ("4 stems|6 stems"));
            hd.setSixStems (true);
            hd.setProduct (true);
            expectEquals (hd.stemsPillControl().getItems().joinIntoString ("|"), juce::String ("Quick|Best quality"));
            expectEquals (hd.stemsPillControl().getSelected(), 0);        // the quality, not the 6 stems
            hd.setHarmonyQuality (true);
            expectEquals (hd.stemsPillControl().getSelected(), 1);
            bool best = false;
            hd.onHarmonyQuality = [&] (bool b) { best = b; };
            hd.stemsPillControl().onChange (0);
            expect (! best);
            hd.setBestTooltip (true);
            expectEquals (hd.stemsPillControl().tooltipFor (1), juce::String ("900 MB download first"));
            expectEquals (hd.stemsPillControl().tooltipFor (0), juce::String());
            hd.setBestTooltip (false);
            expectEquals (hd.stemsPillControl().tooltipFor (1), juce::String());
            hd.setProduct (false);
            expectEquals (hd.stemsPillControl().getItems().joinIntoString ("|"), juce::String ("4 stems|6 stems"));
            expectEquals (hd.stemsPillControl().getSelected(), 1);        // the 6 stems are back
            bool sixPicked = false;
            hd.onSixStems = [&] (bool b) { sixPicked = b; };
            hd.stemsPillControl().onChange (0);
            expect (! sixPicked);
            hd.setBestTooltip (true);                        // not in StemSplitter
            expectEquals (hd.stemsPillControl().tooltipFor (1), juce::String());
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
            int newSongs = 0;
            h.onNewSong = [&] { ++newSongs; };
            expectEquals (h.newSongButton().getButtonText(), juce::String ("New song"));
            expect (h.newSongButton().getRight() < h.backButton().getX());   // left of "All stems"
            h.newSongButton().onClick();
            expectEquals (newSongs, 1);
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
