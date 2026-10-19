#include "FakeHarmony.h"
#include "../Source/HarmonyJob.h"
#include "../Source/PluginEditor.h"
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

    static void waitForFlow (StemSplitterProcessor& p)   // the flow's timer needs the message loop
    {
        using Step = HarmonyFlow::Step;
        for (int i = 0; i < 100 && (p.harmonyFlow.getStep() == Step::vocals || p.harmonyFlow.getStep() == Step::harmonies); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    // The editor wired to the processor: the reel, the choir icon, the switch, Retry and a reopened window.
    void editorTests()
    {
        using Step = HarmonyFlow::Step;

        beginTest ("the editor: the choir icon spins to Harmonies and starts that song's harmonies");
        {
            CachedSong s;
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = okPass (s);
            proc.startSplit (s.file);                        // a cache hit: StemSplitter shows its stems
            StemSplitterEditor ed (proc);
            expect (ed.stemsScreen().isVisible() && ed.stemsScreen().hasHarmonyButton());
            ed.stemsScreen().harmonyControl().onClick();
            expectEquals (proc.getProduct(), juce::String ("harmonies"));
            expect (ed.headerBar().showsHarmonies());
            expect (proc.harmonyFlow.getSong() == s.file && proc.harmonyFlow.getStep() != Step::idle);
            waitForFlow (proc);
            expect (proc.harmonyFlow.getStep() == Step::done, proc.harmonyFlow.getError());
            expect (proc.getLastInput() == s.file && ! proc.isSixStems());   // StemSplitter's song and 4/6 untouched
        }

        beginTest ("the editor: the name to Harmonies follows StemSplitter's song");
        {
            CachedSong x, y;
            writeResult (x.stems, "best");
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.startHarmonies (x.file);                    // Harmonies' own song, with a result
            proc.startSplit (y.file);                        // StemSplitter's, split, without harmonies
            StemSplitterEditor ed (proc);
            ed.headerBar().onName();
            expectEquals (proc.getProduct(), juce::String ("harmonies"));
            auto st = uiStateFor (proc);
            expect (st.screen == Screen::HarmonyDrop && st.offerSong == "song", "the drop screen offers the song on screen");
            ed.tick();
            ed.headerBar().onName();                         // back: the drop screen has no song, StemSplitter keeps its own
            expectEquals (proc.getProduct(), juce::String ("stems"));
            expect (proc.getLastInput() == y.file);
            ed.tick();
            writeResult (y.stems, "quick");
            ed.headerBar().onName();                         // y has harmonies now: they open
            st = uiStateFor (proc);
            expect (st.screen == Screen::Harmonies && st.harmonyStemDir == y.stems);
        }

        beginTest ("the editor: the name back to StemSplitter takes Harmonies' song along, from the cache");
        {
            CachedSong x, y;
            writeResult (x.stems, "quick");
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.startSplit (y.file);
            proc.setProduct ("harmonies");
            proc.startHarmonies (x.file);                    // a result: done at once
            StemSplitterEditor ed (proc);
            expect (ed.harmonyScreen().isVisible() && ed.headerBar().isNameClickable());
            ed.headerBar().onName();
            expectEquals (proc.getProduct(), juce::String ("stems"));
            expect (! ed.headerBar().showsHarmonies());
            expect (proc.getLastInput() == x.file);
            expect (proc.job.getState() == JobState::Done, "a cache hit: no Python ran");
        }

        beginTest ("the editor: while Harmonies works, the name, the switches and file drops wait");
        {
            CachedSong s;
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = fakeCommand (s.root.getChildFile ("slow"),
                "@echo off\r\nping -n 4 127.0.0.1 >nul\r\n", "#!/bin/sh\nexec sleep 3\n");
            proc.setProduct ("harmonies");
            StemSplitterEditor ed (proc);
            const juce::StringArray drag { s.root.getChildFile ("x.wav").getFullPathName() };
            expect (ed.headerBar().isNameClickable() && ed.isInterestedInFileDrag (drag));
            proc.startHarmonies (s.file);
            ed.tick();
            expect (proc.harmonyFlow.getStep() == Step::harmonies);
            expect (! ed.headerBar().isNameClickable());
            expect (! ed.isInterestedInFileDrag (drag));
            expect (! ed.headerBar().stemsPillControl().isEnabled());
            proc.harmonyFlow.cancel();
            ed.tick();
            expect (ed.headerBar().isNameClickable() && ed.isInterestedInFileDrag (drag));
            juce::Thread::sleep (3500);                      // the orphaned ping/sleep lets go of the folder
        }

        beginTest ("the editor: nothing restarts or starts beside a running Harmonies step");
        {
            CachedSong s, other;
            const auto starts = s.root.getChildFile ("starts.txt");   // one line per pass started
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = fakeCommand (s.root.getChildFile ("counted"),
                "@echo off\r\necho x>>\"" + starts.getFullPathName() + "\"\r\nping -n 4 127.0.0.1 >nul\r\n",
                "#!/bin/sh\necho x >> \"" + starts.getFullPathName() + "\"\nexec sleep 3\n");
            proc.startSplit (s.file);
            StemSplitterEditor ed (proc);
            ed.stemsScreen().harmonyControl().onClick();
            juce::Thread::sleep (300);
            ed.stemsScreen().harmonyControl().onClick();     // a double click
            juce::Thread::sleep (300);
            expect (proc.harmonyFlow.getStep() == Step::harmonies && proc.harmonyFlow.getSong() == s.file);
            expectEquals (starts.loadFileAsString().retainCharacters ("x").length(), 1, "one pass, not restarted");

            proc.setProduct ("stems");                       // a song chosen for StemSplitter (New song, Browse, a drop) waits too
            ed.filesDropped ({ other.file.getFullPathName() }, 0, 0);
            expect (proc.getLastInput() == s.file && proc.job.getState() == JobState::Done, "no split started");
            ed.headerBar().onName();
            expectEquals (proc.getProduct(), juce::String ("stems"), "the name waits");
            proc.harmonyFlow.cancel();
            juce::Thread::sleep (3500);                      // the orphaned ping/sleep lets go of the folder
        }

        beginTest ("the editor: a quality click on a result whose song is gone keeps the result");
        {
            CachedSong s;
            writeResult (s.stems, "quick");
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.setProduct ("harmonies");
            proc.startHarmonies (s.file);
            StemSplitterEditor ed (proc);
            s.file.deleteFile();
            ed.headerBar().onHarmonyQuality (true);
            expect (proc.getHarmonyInput() == s.file && proc.harmonyFlow.getStep() == Step::done);
            expectEquals (proc.getHarmonyQuality(), juce::String ("quick"), "the switch keeps the result's quality");
            expect (uiStateFor (proc).screen == Screen::Harmonies);
        }

        const auto slow = [] (const CachedSong& s)
        {
            return fakeCommand (s.root.getChildFile ("slow"), "@echo off\r\nping -n 4 127.0.0.1 >nul\r\n", "#!/bin/sh\nexec sleep 3\n");
        };
        const auto intercepts = [] (juce::Component& c)
        {
            bool self = false, children = false;
            c.getInterceptsMouseClicks (self, children);
            return self && children;
        };

        beginTest ("the editor: Retry after a failed redo runs the pass again");
        {
            CachedSong s;
            writeResult (s.stems, "quick");
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.setProduct ("harmonies");
            proc.startHarmonies (s.file);                    // the Quick result opens
            StemSplitterEditor ed (proc);
            proc.harmonyFlow.harmonyJobForTests().testCommand = fakeCommand (s.root.getChildFile ("bad"),
                "@echo off\r\necho NEEDS SETUP: no model\r\nexit /b 3\r\n", "#!/bin/sh\necho NEEDS SETUP: no model\nexit 3\n");
            ed.headerBar().onHarmonyQuality (true);          // the Best redo fails
            waitForFlow (proc);
            expect (proc.harmonyFlow.getStep() == Step::failed);
            ed.tick();
            expect (ed.errorScreen().isVisible());
            proc.harmonyFlow.harmonyJobForTests().testCommand = slow (s);
            ed.errorScreen().onRetry();
            expect (proc.harmonyFlow.getStep() == Step::harmonies, "the pass runs again, not the old Quick result at once");
            proc.harmonyFlow.cancel();
            juce::Thread::sleep (3500);                      // the orphaned ping/sleep lets go of the folder
        }

        beginTest ("the editor: no offer and no choir icon when StemSplitter's song file is gone");
        {
            CachedSong s;
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.startSplit (s.file);
            proc.syncWithJob();
            s.file.deleteFile();
            StemSplitterEditor ed (proc);
            expect (ed.stemsScreen().isVisible() && ! ed.stemsScreen().hasHarmonyButton());
            proc.setProduct ("harmonies");
            const auto st = uiStateFor (proc);
            expect (st.screen == Screen::HarmonyDrop && st.offerSong.isEmpty());
        }

        beginTest ("the editor: an opened result sets the quality the switch and the next run use");
        {
            CachedSong s;
            writeResult (s.stems, "quick");
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.setHarmonyQuality ("best");
            proc.setProduct ("harmonies");
            proc.startHarmonies (s.file);
            proc.harmonyFlow.harmonyJobForTests().testCommand = okPass (s);
            StemSplitterEditor ed (proc);
            expectEquals (proc.getHarmonyQuality(), juce::String ("quick"));
            expectEquals (ed.headerBar().stemsPillControl().getSelected(), 0);
            ed.headerBar().onHarmonyQuality (true);          // Best now redoes it
            expect (proc.harmonyFlow.getStep() == Step::harmonies);
            waitForFlow (proc);
            expectEquals (HarmonyJob::qualityOf (s.stems), juce::String ("best"));
        }

        beginTest ("the editor: a start from the page being left takes the product along");
        {
            CachedSong s;
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = okPass (s);
            proc.startSplit (s.file);
            proc.setProduct ("harmonies");
            StemSplitterEditor ed (proc);
            proc.setProduct ("stems");                       // switched back before a late click (or a chooser) answered
            ed.tick();
            ed.harmonyDropScreen().offerButton().onClick();
            expectEquals (proc.getProduct(), juce::String ("harmonies"));
            expect (proc.harmonyFlow.getSong() == s.file);
            ed.tick();
            expect (ed.headerBar().showsHarmonies() && ! ed.headerBar().isSpinning(), "the reel and the page follow at once");
            waitForFlow (proc);
        }

        beginTest ("the editor: the page being left takes no clicks until the slide settles");
        {
            StemSplitterProcessor proc;
            proc.introShown = true;
            StemSplitterEditor ed (proc);
            expect (intercepts (ed.stemsScreen()) && ! intercepts (ed.harmonyScreen()));
            ed.headerBar().onName();                         // to Harmonies: the reel spins, the panel hasn't moved
            ed.tick();
            expect (! intercepts (ed.stemsScreen()) && ! intercepts (ed.harmonyScreen()));
            ed.setSlideForTests (0.5f);
            expect (! intercepts (ed.stemsScreen()) && ! intercepts (ed.harmonyScreen()));
            ed.setSlideForTests (1.0f);
            expect (! intercepts (ed.stemsScreen()) && intercepts (ed.harmonyScreen()));
        }

        beginTest ("the editor: reopened mid-flow, it shows the progress at once");
        {
            CachedSong s;
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = slow (s);
            proc.setProduct ("harmonies");
            {
                StemSplitterEditor first (proc);
                proc.startHarmonies (s.file);
                first.tick();
            }                                                // the window closes during step 2
            expect (proc.harmonyFlow.getStep() == Step::harmonies, "the flow keeps going");
            StemSplitterEditor ed (proc);
            expect (ed.harmonySplittingScreen().isVisible() && ed.harmonySplittingScreen().getX() == 0);
            expect (ed.headerBar().showsHarmonies() && ! ed.headerBar().isSpinning());
            expectEquals (uiStateFor (proc).harmonyStep, 2);
            expect (proc.harmonyFlow.getStep() == Step::harmonies);
            proc.harmonyFlow.cancel();
            juce::Thread::sleep (3500);
        }

        beginTest ("the editor: Cancel goes back to the drop screen, or to the result a redo would have replaced");
        {
            CachedSong a, b;
            writeResult (b.stems, "quick");
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = slow (a);
            proc.setProduct ("harmonies");
            proc.startHarmonies (a.file);
            StemSplitterEditor ed (proc);
            expect (ed.harmonySplittingScreen().isVisible());
            ed.harmonySplittingScreen().cancelButton().onClick();
            expect (proc.harmonyFlow.getStep() == Step::idle);
            ed.tick();
            expect (ed.harmonyDropScreen().isVisible());

            proc.startHarmonies (b.file);                    // b's Quick result opens; Best redoes it, Cancel keeps it
            ed.tick();
            ed.headerBar().onHarmonyQuality (true);
            ed.tick();
            expect (ed.harmonySplittingScreen().isVisible());
            ed.harmonySplittingScreen().cancelButton().onClick();
            expect (proc.harmonyFlow.getStep() == Step::idle);
            ed.tick();
            expect (ed.harmonyScreen().isVisible());
            expectEquals (uiStateFor (proc).harmonyQuality, juce::String ("quick"));
            juce::Thread::sleep (3500);
        }

        beginTest ("the editor: Best on a Quick result redoes it");
        {
            CachedSong s;
            writeResult (s.stems, "quick");
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = okPass (s);
            proc.setProduct ("harmonies");
            proc.startHarmonies (s.file);
            StemSplitterEditor ed (proc);
            expect (ed.harmonyScreen().isVisible());
            ed.headerBar().onHarmonyQuality (true);
            expectEquals (proc.getHarmonyQuality(), juce::String ("best"));
            expect (proc.harmonyFlow.getStep() == Step::harmonies);
            waitForFlow (proc);
            expect (proc.harmonyFlow.getStep() == Step::done, proc.harmonyFlow.getError());
            expectEquals (HarmonyJob::qualityOf (s.stems), juce::String ("best"));
        }

        beginTest ("the editor: Retry on a song that's gone shows Harmonies' drop screen");
        {
            CachedSong s;
            StemSplitterProcessor proc;
            proc.introShown = true;
            proc.harmonyFlow.harmonyJobForTests().testCommand = fakeCommand (s.root.getChildFile ("bad"),
                "@echo off\r\necho NEEDS SETUP: no model\r\nexit /b 3\r\n", "#!/bin/sh\necho NEEDS SETUP: no model\nexit 3\n");
            proc.setProduct ("harmonies");
            proc.startHarmonies (s.file);
            waitForFlow (proc);
            StemSplitterEditor ed (proc);
            expect (ed.errorScreen().isVisible());
            s.file.deleteFile();
            ed.errorScreen().onRetry();
            expect (proc.harmonyFlow.getStep() == Step::idle, "no flow on a missing file");
            expect (uiStateFor (proc).screen == Screen::HarmonyDrop);
        }

        beginTest ("the editor: reopened in Harmonies, it is there at once");
        {
            CachedSong s;
            writeResult (s.stems, "best");
            juce::MemoryBlock saved;
            {
                StemSplitterProcessor a;
                a.setProduct ("harmonies");
                a.startHarmonies (s.file);
                a.getStateInformation (saved);
            }
            StemSplitterProcessor b;
            b.introShown = true;
            b.setStateInformation (saved.getData(), (int) saved.getSize());
            StemSplitterEditor ed (b);
            expect (ed.headerBar().showsHarmonies() && ! ed.headerBar().isSpinning());
            expect (ed.harmonyScreen().isVisible() && ! ed.stemsScreen().isVisible());
            expectEquals (ed.harmonyScreen().getX(), 0);
        }
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
        editorTests();
    }
};

static HarmonyTests harmonyTests;
