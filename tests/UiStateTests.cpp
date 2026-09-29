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
                expect (ed.getWantsKeyboardFocus());
                bool browseHasEllipsis = false;
                walk (ed, [&] (juce::Component& c)
                {
                    expect (&c == &ed || ! c.getWantsKeyboardFocus(), c.getName());   // only the editor takes the keyboard
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

        beginTest ("loop: snapping, button, chip, renders");
        {
            StemSplitterProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            StemSplitterEditor ed (proc);
            UiState s;
            s.screen = Screen::Stems;
            s.stemDir = four;
            s.songName = "Song.mp3";
            ed.forceState (s);
            ed.tick();
            auto& screen = ed.stemsScreen();
            expect (! screen.loopButtonEnabled());
            expectEquals (screen.chipText(), juce::String ("Drag mix"));

            screen.setTempo ({ 120.0, 0.0 });                // 0.5 s stems: beats every 0.5 s = the whole song
            expectEquals (screen.snap (0.3), 0.0);
            expectEquals (screen.snap (0.8), 1.0);
            screen.setTempo ({});
            expectEquals (screen.snap (0.3), 0.3);           // no tempo: no snapping

            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (proc.player.isLooping());
            expect (screen.loopButtonEnabled());
            expectEquals (screen.chipText(), juce::String ("Drag loop"));
            screen.onToggleLoop();
            ed.tick();
            expect (! proc.player.isLooping());
            expectEquals (screen.chipText(), juce::String ("Drag mix"));

            proc.player.setMuted (2, true);
            const auto mix = screen.mixFile();
            expect (mix.existsAsFile(), mix.getFullPathName());
            expect (mix.getFileName().contains ("vocals+drums+other"), mix.getFileName());
            expect (mix.isAChildOf (four.getChildFile ("renders")));
            expect (screen.stemFile (1) == screen.fileOf (1));    // no loop: the stem file itself
            screen.onToggleLoop();
            const auto drumsLoop = screen.stemFile (1);
            expect (drumsLoop.existsAsFile() && drumsLoop.getFileName().contains ("drums ("), drumsLoop.getFileName());
            screen.onSetLoop (0.4, 0.8);                          // a beat away: same whole seconds, so it must not reuse the file
            ed.tick();
            expect (screen.stemFile (1) != drumsLoop, screen.stemFile (1).getFileName());
            four.getChildFile ("renders").deleteRecursively();
        }

        beginTest ("keys: Space, L, 1-6, Shift+1-6, Home on the Stems screen; everything else goes to the host");
        {
            StemSplitterProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            StemSplitterEditor ed (proc);
            UiState s;
            s.screen = Screen::Stems;
            s.stemDir = four;
            ed.forceState (s);
            ed.tick();
            auto& screen = ed.stemsScreen();

            expect (ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
            expect (proc.player.isPlaying());
            ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            expect (! proc.player.isPlaying());

            expect (ed.keyPressed (juce::KeyPress ('2')));
            expect (proc.player.isMuted (1));
            expect (ed.keyPressed (juce::KeyPress ('1', juce::ModifierKeys::shiftModifier, '!')));   // Shift+1 solos vocals
            expectEquals ((int) proc.player.audibleMask(), 0b0001);
            expect (ed.keyPressed (juce::KeyPress ('6')));          // there's no sixth stem: ignored, but still ours

            expect (ed.keyPressed (juce::KeyPress ('L')));          // no loop range yet: nothing to toggle
            expect (! proc.player.isLooping());
            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (ed.keyPressed (juce::KeyPress ('l')));
            expect (! proc.player.isLooping());
            ed.keyPressed (juce::KeyPress ('L'));
            expect (proc.player.isLooping());

            proc.player.setPositionFraction (0.9);
            expect (ed.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
            expectWithinAbsoluteError (proc.player.getPositionFraction(), 0.2, 0.01);   // looping: the loop start
            proc.player.setLooping (false);
            ed.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
            expectWithinAbsoluteError (proc.player.getPositionFraction(), 0.0, 0.01);

            expect (! ed.keyPressed (juce::KeyPress ('A')));
            expect (! ed.keyPressed (juce::KeyPress ('S', juce::ModifierKeys::ctrlModifier, 0)));   // FL's Ctrl+S
            expect (! ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0)));

            UiState dropState;
            ed.forceState (dropState);
            ed.tick();
            expect (! ed.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));   // not on the Stems screen
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
