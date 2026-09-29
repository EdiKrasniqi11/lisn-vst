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
            expectEquals (screen.chipText(), juce::String ("Drag mix"));

            screen.setTempo ({ 120.0, 0.0 });                // a beat every 0.5 s (the stems are 0.5 s long)
            // 0.5 s stems at 120 BPM: a quarter beat (0.125 s) is 94 px wide, so edges snap to quarter beats.
            expectWithinAbsoluteError (screen.snap (0.3), 0.25, 1e-9);
            expectWithinAbsoluteError (screen.snap (0.8), 0.75, 1e-9);
            screen.setTempo ({});
            expectEquals (screen.snap (0.3), 0.3);           // no tempo: no snapping

            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (proc.player.isLooping());
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

            expect (ed.keyPressed (juce::KeyPress ('L')));          // no loop yet: L makes one (here the whole 0.5 s song)
            expect (proc.player.isLooping());
            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (ed.keyPressed (juce::KeyPress ('l')));          // with a loop, L toggles it
            ed.tick();
            expect (! proc.player.isLooping());
            ed.keyPressed (juce::KeyPress ('L'));
            ed.tick();
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

        beginTest ("zoom keeps the point under the mouse and clamps; the wheel zooms with Ctrl and scrolls when zoomed");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);                   // a 3:12 song, as the player would report it
            expect (screen.getView() == juce::Range<double> (0.0, 1.0));
            screen.zoom (0.5, 0.8);
            expectWithinAbsoluteError (screen.getView().getStart(), 0.1, 1e-9);
            expectWithinAbsoluteError (screen.getView().getLength(), 0.8, 1e-9);
            screen.zoom (0.26, 0.8);                                  // 0.26 sits at 0.2 of the view and stays there
            expectWithinAbsoluteError (screen.getView().getStart(), 0.26 - 0.2 * 0.64, 1e-9);
            expectWithinAbsoluteError (screen.getView().getLength(), 0.64, 1e-9);
            for (int i = 0; i < 100; ++i) screen.zoom (0.3, 0.8);
            expectWithinAbsoluteError (screen.getView().getLength() * 192.0, 2.0, 1e-9);   // 2 s across at most
            for (int i = 0; i < 100; ++i) screen.zoom (0.3, 1.25);
            expect (screen.getView() == juce::Range<double> (0.0, 1.0));

            screen.setView ({ 0.25, 0.75 });                          // everything maps through the view
            expectEquals (screen.playheadArea (0.5).getCentreX(), 213 + 188);
            expectEquals (screen.playheadArea (0.25).getCentreX(), 213);
            screen.setView ({ 0.9, 1.3 });                            // kept inside the song
            expectWithinAbsoluteError (screen.getView().getStart(), 0.6, 1e-9);
            expectWithinAbsoluteError (screen.getView().getEnd(), 1.0, 1e-9);
            screen.setView ({ 0.0, 1.0 });

            auto wheel = [&] (float deltaY, bool ctrl)                // over the middle of the first waveform
            {
                const juce::Point<float> p (213.0f + 188.0f, (float) screen.row (0)->getBounds().getCentreY());
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                          ctrl ? juce::ModifierKeys (juce::ModifierKeys::ctrlModifier) : juce::ModifierKeys(),
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &screen, &screen, now, p, now, 0, false);
                screen.mouseWheelMove (e, { 0.0f, deltaY, false, false, false });
            };
            wheel (-0.14f, false);
            expect (screen.getView() == juce::Range<double> (0.0, 1.0));   // whole song: the plain wheel does nothing
            wheel (0.14f, true);                                      // Ctrl + wheel up zooms in around the mouse
            expectWithinAbsoluteError (screen.getView().getLength(), 0.8, 1e-9);
            expectWithinAbsoluteError (screen.getView().getStart(), 0.1, 1e-9);
            wheel (-0.14f, false);                                    // wheel down: later in the song, 10 % of the view
            expectWithinAbsoluteError (screen.getView().getStart(), 0.18, 1e-9);
            for (int i = 0; i < 20; ++i) wheel (-0.14f, false);
            expectWithinAbsoluteError (screen.getView().getEnd(), 1.0, 1e-9);   // stops at the end of the song
        }

        beginTest ("zooming fully out lands on exactly the whole song, from any anchor");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);
            for (int k = 0; k <= 1000; k += 9)                        // rounding used to leave {1e-16, 1} for some anchors
            {
                const auto at = k / 1000.0;
                screen.setView ({ 0.1, 0.9 });
                screen.zoom (at, 1.25);                               // 0.8 wide x 1.25: the whole song
                expect (screen.getView() == juce::Range<double> (0.0, 1.0), "anchor " + juce::String (at));
            }
        }

        beginTest ("grid step follows the zoom; Alt and no tempo don't snap; loop length in beats");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);
            screen.setTempo ({ 92.0, 0.2 });
            const double beat = 60.0 / 92.0;
            expectWithinAbsoluteError (screen.gridStep(), 4.0 * beat, 1e-9);   // whole song: a beat is 1.3 px, so bars
            screen.setView ({ 60.0 / 192.0, 100.0 / 192.0 });                   // 40 s across: a beat is 6.1 px
            expectWithinAbsoluteError (screen.gridStep(), beat, 1e-9);
            screen.setView ({ 74.0 / 192.0, 88.0 / 192.0 });                    // 14 s across: a quarter beat is 4.4 px
            expectWithinAbsoluteError (screen.gridStep(), beat / 4.0, 1e-9);
            const double f = 80.0 / 192.0, k = (screen.snap (f) * 192.0 - 0.2) / (beat / 4.0);
            expectWithinAbsoluteError (k, std::round (k), 1e-6);                // on the quarter-beat grid
            expectWithinAbsoluteError (screen.snap (f) * 192.0, 80.0, beat / 8.0 + 1e-9);
            expectEquals (screen.snap (f, true), f);                            // Alt: free
            screen.setTempo ({});
            expectEquals (screen.gridStep(), 0.0);
            expectEquals (screen.snap (f), f);

            expectEquals (beatsText (8.0), juce::String ("8 beats"));
            expectEquals (beatsText (10.26), juce::String ("10.25 beats"));
            expectEquals (beatsText (2.5), juce::String ("2.5 beats"));
            expectEquals (beatsText (1.05), juce::String ("1 beat"));
        }

        beginTest ("handles: hover, trim with snapping and Alt, never crossing; a drag elsewhere scrubs, a Ctrl+drag makes a new loop");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);
            screen.setTempo ({ 92.0, 0.2 });
            screen.setView ({ 74.0 / 192.0, 88.0 / 192.0 });                    // quarter-beat grid
            const double beat = 60.0 / 92.0, a = (0.2 + 120 * beat) / 192.0, b = (0.2 + 128 * beat) / 192.0;
            screen.setLoop ({ a, b }, true);                                    // 8 beats
            std::vector<std::pair<double, double>> sets;
            screen.onSetLoop = [&] (double from, double to) { sets.push_back ({ from, to }); };
            double seekTo = -1.0;
            screen.onSeek = [&] (double f) { seekTo = f; };
            const double px = (14.0 / 192.0) / 376.0;                           // one pixel, as a song fraction

            screen.waveMouse (WaveMouse::move, b + 3 * px, false);
            expectEquals (screen.hotEdge(), 1);
            screen.waveMouse (WaveMouse::move, b + 9 * px, false);
            expectEquals (screen.hotEdge(), -1);

            // Trim the end two beats in: snapped, sent once on release, and grabbing a handle doesn't seek.
            const double near = b - 2 * beat / 192.0 + 0.05 / 192.0;
            screen.waveMouse (WaveMouse::down, b + 2 * px, false);
            expectEquals (seekTo, -1.0);
            screen.waveMouse (WaveMouse::drag, near, false);
            expect (sets.empty());
            screen.waveMouse (WaveMouse::up, near, false);
            expectEquals ((int) sets.size(), 1);
            expectWithinAbsoluteError (sets[0].first, a, 1e-12);
            expectWithinAbsoluteError (sets[0].second, b - 2 * beat / 192.0, 1e-9);

            // The start can't cross the end: it stops one step (a quarter beat) short.
            screen.waveMouse (WaveMouse::down, a, false);
            screen.waveMouse (WaveMouse::drag, b + 1.0 / 192.0, false);
            screen.waveMouse (WaveMouse::up, b + 1.0 / 192.0, false);
            expectEquals ((int) sets.size(), 2);
            expectWithinAbsoluteError (sets[1].first, b - beat / 4.0 / 192.0, 1e-9);
            expectWithinAbsoluteError (sets[1].second, b, 1e-12);

            // Alt: the end follows the mouse exactly (8 px here; the grid line beside it is a quarter beat, 4.4 px, further).
            const double freeAt = b - 0.3 / 192.0;
            screen.waveMouse (WaveMouse::down, b, true);
            screen.waveMouse (WaveMouse::drag, freeAt, true);
            screen.waveMouse (WaveMouse::up, freeAt, true);
            expectEquals ((int) sets.size(), 3);
            expectWithinAbsoluteError (sets[2].second, freeAt, 1e-12);

            // A click on a handle that jitters by a pixel or two changes nothing: no loop is sent, so the player isn't re-seeked.
            screen.waveMouse (WaveMouse::down, b, false);
            screen.waveMouse (WaveMouse::drag, b - 1.5 * px, false);
            screen.waveMouse (WaveMouse::drag, b + 2.0 * px, false);
            screen.waveMouse (WaveMouse::up, b + 2.0 * px, false);
            expectEquals ((int) sets.size(), 3);
            expectEquals (seekTo, -1.0);

            // Away from the edges: a click seeks, a drag scrubs, a Ctrl+drag makes a new snapped loop (here an inner one).
            const double mid = (a + b) / 2.0;
            screen.waveMouse (WaveMouse::down, mid, false);
            screen.waveMouse (WaveMouse::up, mid, false);
            expectWithinAbsoluteError (seekTo, mid, 1e-12);
            expectEquals ((int) sets.size(), 3);
            screen.waveMouse (WaveMouse::down, mid, false);
            screen.waveMouse (WaveMouse::drag, mid + 30 * px, false);
            expectWithinAbsoluteError (seekTo, mid + 30 * px, 1e-12);
            screen.waveMouse (WaveMouse::up, mid + 30 * px, false);
            expectEquals ((int) sets.size(), 3);
            screen.waveMouse (WaveMouse::down, mid, false, true);
            screen.waveMouse (WaveMouse::drag, mid + 30 * px, false, true);
            screen.waveMouse (WaveMouse::up, mid + 30 * px, false, true);
            expectEquals ((int) sets.size(), 4);
            expect (sets[3].first > a && sets[3].second < b);
            const double k = (sets[3].first * 192.0 - 0.2) / (beat / 4.0);
            expectWithinAbsoluteError (k, std::round (k), 1e-6);
        }

        beginTest ("a grab tab's top half is a handle: the rows let the mouse through there and the screen runs the gesture");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);
            screen.setTempo ({ 92.0, 0.2 });
            screen.setView ({ 74.0 / 192.0, 88.0 / 192.0 });
            const double beat = 60.0 / 92.0, a = (0.2 + 120 * beat) / 192.0, b = (0.2 + 128 * beat) / 192.0;
            screen.setLoop ({ a, b }, true);
            std::vector<std::pair<double, double>> sets;
            screen.onSetLoop = [&] (double from, double to) { sets.push_back ({ from, to }); };
            auto set = [&] (size_t i) { return i < sets.size() ? sets[i] : std::pair<double, double> { -1.0, -1.0 }; };
            double seekTo = -1.0;
            screen.onSeek = [&] (double f) { seekTo = f; };

            auto& row = *screen.row (0);
            const int wx = row.waveform().getX();
            const int wr = row.waveform().getRight();
            expect (! row.hitTest (wx + 100, 2));                              // above the waveform (its 5 px margin lies within the tab)
            expect (! row.hitTest (wx + 100, row.getHeight() - 2));            // below it
            expect (! row.hitTest (wx - 5, 2) && ! row.hitTest (wx - 9, 2));   // 5-9 px left of waveform, above it
            expect (! row.hitTest (wr + 4, 2) && ! row.hitTest (wr + 8, 2));   // 4-8 px right of waveform, above it
            expect (row.hitTest (wx + 100, row.getHeight() / 2));              // the waveform itself
            expect (row.hitTest (40, 2) && row.hitTest (row.chipBounds().getCentreX(), row.getHeight() / 2));   // name, chip: still the row's

            auto at = [&] (int x, int y)
            {
                const juce::Point<float> p ((float) x, (float) y);
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(),
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &screen, &screen, now, p, now, 0, false);
            };
            auto xAt = [&] (double f)
            {
                return 213 + juce::roundToInt ((f - screen.getView().getStart()) / screen.getView().getLength() * 376.0);
            };
            const int tabY = 76, xa = xAt (a), xb = xAt (b);                   // y 76 is row 0's top margin, inside the tab (72..90)

            screen.mouseMove (at (xb + 2, tabY));
            expectEquals (screen.hotEdge(), 1);
            screen.mouseMove (at (xb + 30, tabY));
            expectEquals (screen.hotEdge(), -1);
            screen.mouseMove (at (xb + 2, tabY));
            screen.mouseMove (at (xb + 2, 20));                                // up in the song row: not a handle
            expectEquals (screen.hotEdge(), -1);
            screen.mouseMove (at (xb + 2, tabY));
            screen.mouseExit (at (xb + 2, tabY));
            expectEquals (screen.hotEdge(), -1);

            screen.mouseMove (at (xb + 2, tabY));                              // trim the end two beats in from the tab
            screen.mouseDown (at (xb, tabY));
            expectEquals (seekTo, -1.0);
            screen.mouseDrag (at (xb - 35, tabY));
            expect (sets.empty());
            screen.mouseUp (at (xb - 35, 300));                                // released away from the tab: still this gesture
            expectEquals ((int) sets.size(), 1);
            expectWithinAbsoluteError (set (0).first, a, 1e-12);
            expectWithinAbsoluteError (set (0).second, b - 2 * beat / 192.0, 1e-9);

            screen.mouseDown (at (xa, tabY));                                  // the start, dragged far left of the waves: x is clamped
            screen.mouseDrag (at (5, 300));                                    // ... and the drag goes on outside the tab area
            screen.mouseUp (at (5, 300));
            expectEquals ((int) sets.size(), 2);
            expectWithinAbsoluteError (set (1).first * 192.0, 74.0, beat / 8.0 + 1e-9);   // the view's start (snapped)

            screen.mouseDown (at (xa + 60, 20));                               // a press elsewhere isn't ours, nor is its drag
            screen.mouseDrag (at (xa + 120, tabY));
            screen.mouseUp (at (xa + 120, tabY));
            expectEquals ((int) sets.size(), 2);
            expectEquals (seekTo, -1.0);
        }

        beginTest ("new stems reset the zoom");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 0.0, 192.0);
            screen.zoom (0.5, 0.5);
            expect (screen.getView() != juce::Range<double> (0.0, 1.0));
            screen.setStems (four, "Song.mp3");
            expect (screen.getView() == juce::Range<double> (0.0, 1.0));
        }

        beginTest ("the Loop button (and L) makes a 4-bar loop at the playhead when there's none, else toggles");
        {
            StemsScreen screen;
            screen.setStems (four, "Song.mp3");
            screen.setPlayback (false, 70.0 / 192.0, 192.0);        // the playhead at 1:10 of a 3:12 song
            std::pair<double, double> set { -1.0, -1.0 };
            screen.onSetLoop = [&] (double a, double b) { set = { a, b }; };
            int toggles = 0;
            screen.onToggleLoop = [&] { ++toggles; };

            screen.toggleLoop();                                    // no tempo: 8 s from the playhead
            expectWithinAbsoluteError (set.first * 192.0, 70.0, 1e-9);
            expectWithinAbsoluteError (set.second * 192.0, 78.0, 1e-9);

            screen.setTempo ({ 92.0, 0.2 });
            const double bar = 4.0 * 60.0 / 92.0;
            screen.toggleLoop();                                    // from the bar under the playhead, 4 bars long
            const double k = (set.first * 192.0 - 0.2) / bar;
            expectWithinAbsoluteError (k, std::round (k), 1e-9);
            expect (set.first * 192.0 <= 70.0 && set.first * 192.0 > 70.0 - bar);
            expectWithinAbsoluteError ((set.second - set.first) * 192.0, 4.0 * bar, 1e-9);

            screen.setPlayback (false, 191.0 / 192.0, 192.0);       // it would run past the end: the song's last 4 bars
            screen.toggleLoop();
            expectWithinAbsoluteError (set.second, 1.0, 1e-12);
            expectWithinAbsoluteError ((set.second - set.first) * 192.0, 4.0 * bar, 1e-9);
            expectEquals (toggles, 0);

            screen.setPlayback (false, 0.0, 192.0);                 // before the first beat: the first bar, not 0:00
            screen.toggleLoop();
            expectWithinAbsoluteError (set.first * 192.0, 0.2, 1e-9);
            expectWithinAbsoluteError ((set.second - set.first) * 192.0, 4.0 * bar, 1e-9);

            screen.setLoop ({ 0.2, 0.3 }, false);                   // with a loop range, it only toggles
            screen.toggleLoop();
            expectEquals (toggles, 1);
        }

        beginTest ("a seek from an off-screen playhead repaints the whole played part; an off-screen playhead repaints nothing");
        {
            StemsScreen screen;
            screen.setSize (712, 400);
            screen.setVisible (true);
            screen.setBufferedToImage (true);                         // keeps what was painted: only repaint() refreshes it
            screen.setStems (four, "Song.mp3");
            screen.row (0)->setEnvelope (std::vector<float> (1000, 1.0f));   // full-height bars
            screen.setPlayback (false, 0.0, 192.0);
            screen.setView ({ 0.385, 0.458 });                        // the playhead at 0 is far left of it
            auto snapshot = [&]
            {
                juce::Image img (juce::Image::ARGB, 712, 400, true, juce::SoftwareImageType());
                juce::Graphics g (img);
                screen.getCachedComponentImage()->paint (g);
                return img;
            };
            snapshot();                                               // everything is valid now
            const auto count = screen.getRepaintCount();
            screen.setPlayback (false, 0.002, 192.0);                 // still off-screen, same time text
            expectEquals (screen.getRepaintCount(), count);
            screen.setPlayback (false, 0.42, 192.0);                  // a seek into the view: the head lands at x 393
            expectGreaterThan ((int) snapshot().getPixelAt (250, 105).getAlpha(), 240);   // a bar of row 0 left of it: played
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
