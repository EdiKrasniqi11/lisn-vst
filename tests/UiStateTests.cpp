#include "../Source/PluginEditor.h"

struct UiStateTests : juce::UnitTest
{
    UiStateTests() : juce::UnitTest ("UiState") {}

    void runTest() override
    {
        const juce::File jobDir ("C:\\cache\\job"), savedDir ("C:\\cache\\saved"), input ("C:\\music\\song.mp3");
        auto map = [&] (JobState s, ErrorKind k = ErrorKind::None, const juce::File& saved = {})
        {
            return uiStateFor (s, k, 0.4, "boom", "C:\\Py\\python.exe", jobDir, saved, input, true);
        };

        beginTest ("mapping");
        {
            const auto running = map (JobState::Running);
            expect (running.screen == Screen::Splitting && running.progress == 0.4 && running.songName == "song.mp3" && running.sixStems);

            const auto failed = map (JobState::Failed, ErrorKind::DemucsMissing);
            expect (failed.screen == Screen::Error && failed.error == ErrorKind::DemucsMissing);
            expect (failed.errorText == "boom" && failed.pythonExe == "C:\\Py\\python.exe");
            expect (map (JobState::Failed).error == ErrorKind::Failed);

            const auto done = map (JobState::Done);
            expect (done.screen == Screen::Stems && done.stemDir == jobDir);

            for (auto s : { JobState::Idle, JobState::Cancelled })
            {
                expect (map (s).screen == Screen::Drop);
                const auto withStems = map (s, ErrorKind::None, savedDir);
                expect (withStems.screen == Screen::Stems && withStems.stemDir == savedDir);
            }
        }

        beginTest ("sameScreenAs ignores progress");
        {
            auto a = map (JobState::Running), b = a;
            b.progress = 0.9;
            expect (a.sameScreenAs (b));
            b.sixStems = false;
            expect (! a.sameScreenAs (b));
        }

        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-ui", {});
        const auto four = makeStems (root.getChildFile ("four"), { "other", "bass", "vocals", "drums" });
        const auto six = makeStems (root.getChildFile ("six"), { "piano", "other", "bass", "vocals", "guitar", "drums" });

        beginTest ("no focus, no banned controls");
        {
            UiState drop, splitting, stems4, stems6, python, demucs, failed;
            splitting.screen = Screen::Splitting;
            stems4.screen = stems6.screen = Screen::Stems;
            stems4.stemDir = four;
            stems6.stemDir = six;
            python.screen = demucs.screen = failed.screen = Screen::Error;
            python.error = ErrorKind::PythonMissing;
            demucs.error = ErrorKind::DemucsMissing;
            demucs.pythonExe = "C:\\Users\\someone\\AppData\\Local\\Programs\\Python\\Python311\\python.exe";
            failed.error = ErrorKind::Failed;
            failed.errorText = "demucs failed:\nline 1\n 12%|###\r 40%|#####\nline 2\n\nRuntimeError: boom";

            for (const auto& s : { drop, splitting, stems4, stems6, python, demucs, failed })
            {
                StemSplitterProcessor proc;
                StemSplitterEditor ed (proc);
                ed.forceState (s);
                ed.tick();
                bool browseHasEllipsis = false;
                walk (ed, [&] (juce::Component& c)
                {
                    expect (! c.getWantsKeyboardFocus(), c.getName());
                    if (auto* b = dynamic_cast<juce::Button*> (&c))
                    {
                        expect (! b->getMouseClickGrabsKeyboardFocus(), b->getButtonText());
                        if (b->getButtonText().startsWith ("Browse"))
                            browseHasEllipsis = b->getButtonText().containsChar ((juce::juce_wchar) 0x2026);
                    }
                    expect (dynamic_cast<juce::ComboBox*> (&c) == nullptr && dynamic_cast<juce::TextEditor*> (&c) == nullptr
                            && dynamic_cast<juce::ProgressBar*> (&c) == nullptr);
                });
                if (s.screen == Screen::Drop)
                    expect (browseHasEllipsis);
            }
        }

        beginTest ("stem player: order, play/pause, mutes, solo, repaint only on change");
        {
            StemSplitterProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            StemSplitterEditor ed (proc);
            UiState s;
            s.screen = Screen::Stems;
            s.stemDir = six;
            ed.forceState (s);
            ed.tick();
            auto& screen = ed.stemsScreen();
            juce::StringArray order;
            for (int i = 0; i < screen.numRows(); ++i)
                order.add (screen.fileOf (i).getFileNameWithoutExtension());
            expectEquals (order.joinIntoString (","), juce::String ("vocals,drums,bass,guitar,piano,other"));
            expect (proc.player.getFiles() == screen.getFiles());
            proc.player.unload();                        // what startSplit() does before a cached re-split of the same song
            ed.tick();
            expect (proc.player.getFiles() == screen.getFiles());

            screen.onPlayPause();
            expect (proc.player.isPlaying());
            screen.onPlayPause();
            expect (! proc.player.isPlaying());

            screen.onToggleMute (1);
            expect (proc.player.isMuted (1));
            ed.tick();
            expect (screen.row (1)->light().isMuted() && ! screen.row (0)->light().isMuted());
            screen.onSolo (0);
            expectEquals ((int) proc.player.audibleMask(), 0b000001);
            screen.onSolo (0);
            expectEquals ((int) proc.player.audibleMask(), 0b111111);

            screen.setPlayback (false, 0.25, 30.0);
            const auto count = screen.getRepaintCount();
            screen.setPlayback (false, 0.25, 30.0);
            screen.setPlayback (false, 0.2501, 30.0);    // same pixel, same time text
            expectEquals (screen.getRepaintCount(), count);
            screen.setPlayback (false, 0.5, 30.0);
            expectEquals (screen.getRepaintCount(), count + 1);
        }

        beginTest ("switching 6 to 4 stems drops the old rows, player files and peaks");
        {
            StemSplitterProcessor proc;
            StemSplitterEditor ed (proc);
            UiState s;
            s.screen = Screen::Stems;
            s.stemDir = six;
            ed.forceState (s);
            ed.tick();
            auto& screen = ed.stemsScreen();
            expect (screen.numRows() == 6);
            proc.player.play();
            ed.tick();
            expectEquals (proc.player.getFiles().size(), 6);

            s.stemDir = four;
            ed.forceState (s);
            ed.tick();
            expect (screen.numRows() == 4 && proc.player.getFiles() == screen.getFiles() && ! proc.player.isPlaying());
            for (int i = 0; i < 60 && ! screen.hasPeaks(); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            ed.tick();
            expect (screen.hasPeaks() && screen.numRows() == 4);
        }

        root.deleteRecursively();
    }

    template <typename Fn>
    static void walk (juce::Component& c, Fn&& fn)
    {
        fn (c);
        for (auto* child : c.getChildren())
            walk (*child, fn);
    }

    static juce::File makeStems (const juce::File& dir, std::initializer_list<const char*> names)   // 0.5 s stereo sines
    {
        dir.createDirectory();
        juce::AudioBuffer<float> b (2, 22050);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, (float) (0.5 * std::sin (0.06 * i)));
        for (auto* n : names)
        {
            auto out = dir.getChildFile (juce::String (n) + ".wav").createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
            if (w == nullptr) continue;
            out.release();
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        return dir;
    }
};

static UiStateTests uiStateTests;
