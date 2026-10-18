#include "../Source/HarmonyJob.h"
#include "../Source/Screens.h"
#include "../Source/Theme.h"

struct HarmonyTests : juce::UnitTest
{
    HarmonyTests() : juce::UnitTest ("Harmony Split") {}

    static void writeWav (const juce::File& f)   // 0.2 s of a quiet 440 Hz tone, 44.1 kHz stereo
    {
        juce::AudioBuffer<float> b (2, 8820);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, 0.2f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));
        f.getParentDirectory().createDirectory();
        auto out = f.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
        if (w == nullptr) return;
        out.release();
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    static juce::StringArray rowNames (StemsScreen& s)
    {
        juce::StringArray names;
        for (const auto& f : s.getFiles()) names.add (f.getFileNameWithoutExtension());
        return names;
    }

    static juce::StringArray menuItems (const juce::PopupMenu& m)   // "text", or "text [ticked]"
    {
        juce::StringArray items;
        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            items.add (it.getItem().text + (it.getItem().isTicked ? " [ticked]" : ""));
        return items;
    }

    // A HarmonyJob running `script` (a .cmd file: %1 the .part dir, %2 the quality) in place of Python, until it leaves
    // running or 10 s pass.
    HarmonyJob::State runFake (HarmonyJob& job, const juce::File& dir, const juce::String& script, const juce::String& quality = "quick")
    {
        const auto cmd = dir.getSiblingFile ("fake.cmd");
        cmd.replaceWithText (script);
        job.testCommand = { "cmd.exe", "/c", cmd.getFullPathName() };
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

        beginTest ("Lead keeps the vocal colour, Harmonies take guitar's");
        for (auto id : { "dusk", "midnight" })
        {
            const auto& t = themeFor (id);
            expect (t.stemColour ("lead") == t.stemColour ("vocals"));
            expect (t.stemColour ("harmonies") == t.stemColour ("guitar"));
        }

        beginTest ("the button is on 4-stem splits only, and the rows go under Vocals");
        {
            StemsScreen s;
            s.setBounds (0, 0, 712, 398);
            s.setStems (four, "Song", true);       // no harmony result yet: 4 rows
            expect (s.hasHarmonyButton());
            expect (! s.showsHarmony(), "no rows without a result");
            expectEquals (s.numRows(), 4);

            writeWav (four.getChildFile ("harmony/lead.wav"));
            writeWav (four.getChildFile ("harmony/harmonies.wav"));
            expect (HarmonyJob::hasResult (four));
            s.setStems (four, "Song", true);
            expect (s.showsHarmony());
            expectEquals (rowNames (s).joinIntoString (" "), juce::String ("vocals lead harmonies drums bass other"));
            expectEquals (s.row (1)->getHeight(), s.row (5)->getHeight());   // all compact
            expect (s.row (5)->getBottom() <= 339, "the rows end above the speed strip");

            s.setStems (four, "Song", false);       // hidden again
            expect (! s.showsHarmony());
            expectEquals (s.numRows(), 4);

            s.setStems (six, "Song", true);
            expect (! s.hasHarmonyButton() && ! s.showsHarmony());
            expectEquals (s.numRows(), 6);

            s.setHarmonyButton ("Splitting 42%");
            expectEquals (s.harmonyText(), juce::String ("Splitting 42%"));

            beginTest ("the menu: Quick and Best quality, the result's ticked, then Hide / Show");
            s.setStems (four, "Song", true);
            expectEquals (menuItems (s.harmonyMenu ({})).joinIntoString ("|"), juce::String ("Quick split|Best quality"));
            expectEquals (menuItems (s.harmonyMenu ("best")).joinIntoString ("|"),
                          juce::String ("Quick split|Best quality [ticked]|Hide Lead and Harmonies"));
        }
        four.getChildFile ("harmony").deleteRecursively();

        beginTest ("a pass moves a finished result into harmony/, with progress");
        {
            HarmonyJob job;
            const auto vocals = four.getChildFile ("vocals.wav").getFullPathName();
            expectEquals (HarmonyJob::qualityOf (four), juce::String());
            const auto state = runFake (job, four, "@echo off\r\necho 37%%^|\r\ncopy /y \"" + vocals + "\" \"%~1\\lead.wav\" >nul\r\n"
                                                    "copy /y \"" + vocals + "\" \"%~1\\harmonies.wav\" >nul\r\n"
                                                    "echo %~2> \"%~1\\quality.txt\"\r\n", "best");
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
            const auto state = runFake (job, four, "@echo off\r\necho NEEDS SETUP: no model\r\nexit /b 3\r\n");
            expect (state == HarmonyJob::State::failed);
            expect (job.getError().contains ("NEEDS SETUP"), job.getError());
            expect (! four.getChildFile ("harmony").exists() && ! four.getChildFile ("harmony.part").exists());
        }

        beginTest ("cancel kills the pass, leaves nothing and goes back to idle");
        {
            HarmonyJob job;
            const auto cmd = root.getChildFile ("fake.cmd");
            cmd.replaceWithText ("@echo off\r\nping -n 4 127.0.0.1 >nul\r\n");
            job.testCommand = { "cmd.exe", "/c", cmd.getFullPathName() };
            job.start (four, {});
            juce::Thread::sleep (300);
            expect (job.getState() == HarmonyJob::State::running);
            job.cancel();
            expect (job.getState() == HarmonyJob::State::idle);
            expect (! four.getChildFile ("harmony").exists() && ! four.getChildFile ("harmony.part").exists());
        }

        juce::Thread::sleep (3500);   // the orphaned ping lets go of the folder
        root.deleteRecursively();
    }
};

static HarmonyTests harmonyTests;
