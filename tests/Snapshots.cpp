#include "../Source/PluginEditor.h"
#include <cstdio>
#include <map>

// lisn_tests --snapshots <dir>: every screen rendered to PNG from synthetic stems; fails unless the intro's last frame
// equals the plain frame.
// lisn_tests --bench: motion frame, full paint, intro frame and processBlock timings; fails when a motion or an intro frame
// averages over 4 ms.

namespace
{
    double sine (double hz, double t) { return std::sin (juce::MathConstants<double>::twoPi * hz * t); }

    // A 30 s, 44.1 kHz, 16-bit stereo stem; sample (seconds, fraction of the length, noise source).
    void writeStem (const juce::File& f, const std::function<double (double, double, juce::Random&)>& sample)
    {
        constexpr int rate = 44100, n = rate * 30;
        juce::AudioBuffer<float> b (2, n);
        juce::Random noise (1);
        for (int i = 0; i < n; ++i)
        {
            const auto v = (float) sample ((double) i / rate, (double) i / n, noise);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        f.deleteFile();
        auto out = f.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), rate, 2, 16, {}, 0));
        if (w == nullptr) return;
        out.release();   // the writer owns the stream now
        w->writeFromAudioSampleBuffer (b, 0, n);
    }

    juce::File makeStems (const juce::File& dir, bool six)
    {
        dir.createDirectory();
        const auto signed1 = [] (juce::Random& r) { return r.nextDouble() * 2.0 - 1.0; };
        writeStem (dir.getChildFile ("vocals.wav"), [] (double t, double f, juce::Random&)
        {
            const bool on = (f > 0.05 && f < 0.3) || (f > 0.35 && f < 0.6) || (f > 0.66 && f < 0.95);
            return on ? 0.6 * sine (300.0, t) : 0.0;
        });
        writeStem (dir.getChildFile ("drums.wav"), [&] (double t, double, juce::Random& r)
        {
            const auto hit = (int) (t / 0.5);
            const auto since = t - hit * 0.5;
            return since < 0.06 ? (hit % 2 == 0 ? 0.9 : 0.6) * std::exp (-since / 0.02) * signed1 (r) : 0.0;
        });
        writeStem (dir.getChildFile ("bass.wav"), [] (double t, double f, juce::Random&)
        {
            return f > 0.47 && f < 0.52 ? 0.0 : 0.55 * sine (55.0, t);
        });
        writeStem (dir.getChildFile ("other.wav"), [&] (double, double f, juce::Random& r) { return (0.2 + 0.4 * f) * signed1 (r); });
        if (six)
        {
            writeStem (dir.getChildFile ("guitar.wav"), [] (double t, double, juce::Random&)
            {
                return 0.5 * sine (220.0, t) * (0.6 + 0.4 * sine (6.0, t));
            });
            writeStem (dir.getChildFile ("piano.wav"), [] (double t, double, juce::Random&)
            {
                return 0.6 * std::exp (-std::fmod (t, 1.0) / 0.3) * sine (440.0, t);
            });
        }
        return dir;
    }

    juce::File tempDir() { return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lisn-snapshots"); }

    void waitForPeaks (StemSplitterEditor& ed)
    {
        for (int i = 0; i < 60 && ! ed.stemsScreen().hasPeaks(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    double now() { return juce::Time::getMillisecondCounterHiRes(); }
}

int runSnapshots (const juce::File& outDir)
{
    outDir.createDirectory();
    const auto four = makeStems (tempDir().getChildFile ("four"), false);
    const auto six = makeStems (tempDir().getChildFile ("six"), true);

    auto state = [&] (Screen screen, bool sixStems)
    {
        UiState s;
        s.screen = screen;
        s.sixStems = sixStems;
        s.songName = "Travis Snippet.mp3";
        if (screen == Screen::Stems)
            s.stemDir = sixStems ? six : four;
        return s;
    };
    auto withProgress = [] (UiState s, double p) { s.progress = p; return s; };
    auto withError = [] (UiState s, ErrorKind k, const juce::String& text, const juce::String& exe)
    {
        s.error = k;
        s.errorText = text;
        s.pythonExe = exe;
        return s;
    };

    using Setup = std::function<void (StemSplitterProcessor&, StemSplitterEditor&)>;
    struct Shot { juce::String name, theme; UiState state; float scale = 1.0f; Setup before; };
    const auto playing = [] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        p.player.setPositionFraction (0.38);
        p.player.setMuted (2, true);                 // bass muted
        p.player.play();                             // no audio callback: the position stays put
        ed.tick();
        ed.background().setMotion (0.0f, 0.0f);      // tick() advanced the flow by wall-clock time; keep snapshots repeatable
    };
    const auto looping = [playing] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        playing (p, ed);
        p.player.setLoop (0.30, 0.45);
        p.player.setPositionFraction (0.38);
        ed.tick();
        ed.background().setMotion (0.0f, 0.0f);
    };
    const auto sixMuted = [] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        p.player.setMuted (1, true);
        p.player.setMuted (4, true);
        p.player.setPositionFraction (0.6);
        ed.tick();
    };
    const auto zoomed = [looping] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        looping (p, ed);
        auto& s = ed.stemsScreen();
        s.setView ({ 0.28, 0.47 });                          // 5.7 s across: beat and bar lines, a quarter-beat grid
        s.waveMouse (WaveMouse::move, p.player.getLoop().getEnd(), false);   // hover the end handle
    };
    const auto sixLooping = [sixMuted] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        sixMuted (p, ed);
        p.player.setLoop (0.30, 0.45);
        ed.tick();
    };
    const auto volumes = [] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        const float v[] = { 0.85f, 1.0f, 0.6f, 0.7f, 0.5f, 0.9f };
        for (int i = 0; i < 6; ++i) p.player.setVolume (i, v[i]);
        p.player.setMuted (2, true);
        p.player.setSpeed (1.15);
        p.player.setPositionFraction (0.4);
        ed.tick();
    };
    const auto volumes6 = [volumes] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        volumes (p, ed);
        p.player.setMuted (2, false);
        p.player.setMuted (4, true);
        p.player.setLoop (0.30, 0.45);
        ed.tick();
    };
    const auto slow = [volumes6] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        volumes6 (p, ed);
        p.player.setSpeed (0.85);
        ed.tick();
    };
    const auto preparing = [volumes] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        volumes (p, ed);
        ed.stemsScreen().setMixChip ("Preparing 40%", 0.4f);   // the job's progress, frozen (StemsVst's busy chip)
    };
    // The reset menu, opened by a right-click at p in the speed strip (the real PopupMenu, a child of the editor).
    const auto rightClick = [] (StemSplitterEditor& ed, juce::Point<float> p)
    {
        auto& strip = ed.stemsScreen().speedStripControl();
        const auto now = juce::Time::getCurrentTime();
        strip.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
                                           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &strip, &strip, now, p, now, 1, false));
    };
    const auto tempoMenu = [volumes, rightClick] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        volumes (p, ed);                                     // 1.15x
        rightClick (ed, { 60.0f, 20.0f });
        for (auto* c : ed.getChildren())
            if (c->getName() == "menu")
                c->keyPressed (juce::KeyPress (juce::KeyPress::downKey));   // the item hovered
    };
    const auto keyMenu = [volumes6, rightClick] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        volumes6 (p, ed);                                    // +0 st: the item is greyed out
        rightClick (ed, { 200.0f, 24.0f });
    };
    const auto moving = [] (StemSplitterProcessor&, StemSplitterEditor& ed) { ed.background().setMotion (140.0f, 0.9f); };
    const auto introAt = [] (float seconds)
    {
        return Setup ([seconds] (StemSplitterProcessor&, StemSplitterEditor& ed) { ed.setIntroTime (seconds); });
    };
    const auto helpTab = [] (int tab)
    {
        return Setup ([tab] (StemSplitterProcessor&, StemSplitterEditor& ed) { ed.setHelpOpen (true); ed.helpSheet().setTab (tab); });
    };
    const juce::String failedText = "demucs failed:\nTraceback (most recent call last):\n  File \"demucs/separate.py\", line 180, in main\n"
                                    "    100%|##########| 5.85/5.85 [00:14<00:00]\n  File \"demucs/apply.py\", line 214, in apply_model\n"
                                    "    out = model(mix)\nRuntimeError: CUDA out of memory. Tried to allocate 1.20 GiB (GPU 0; 4.00 GiB total capacity)\n"
                                    "Could not separate the song.";
    const auto titleBar = [] (StemSplitterProcessor&, StemSplitterEditor& ed) { ed.makeTitleBar ([] {}, [] {}); };   // the desktop app

    const std::vector<Shot> shots {
        { "drop-dusk", "dusk", state (Screen::Drop, false) },
        { "drop-midnight-6", "midnight", state (Screen::Drop, true) },
        { "drop-midnight@2x", "midnight", state (Screen::Drop, false), 2.0f },
        { "titlebar-dusk", "dusk", state (Screen::Drop, false), 1.0f, titleBar },
        { "titlebar-midnight", "midnight", state (Screen::Drop, false), 1.0f, titleBar },
        { "titlebar-dusk@1.25x", "dusk", state (Screen::Drop, false), 1.25f, titleBar },
        { "titlebar-dusk@2x", "dusk", state (Screen::Drop, false), 2.0f, titleBar },
        { "titlebar-midnight@2x", "midnight", state (Screen::Drop, false), 2.0f, titleBar },
        { "splitting-dusk", "dusk", withProgress (state (Screen::Splitting, false), 0.58) },
        { "splitting-midnight", "midnight", withProgress (state (Screen::Splitting, true), 0.58) },
        { "stems4-dusk-mix", "dusk", state (Screen::Stems, false), 1.0f, looping },
        { "stems6-midnight-muted", "midnight", state (Screen::Stems, true), 1.0f, sixMuted },
        { "stems4-dusk-zoom", "dusk", state (Screen::Stems, false), 1.0f, zoomed },
        { "stems6-midnight-loop", "midnight", state (Screen::Stems, true), 1.0f, sixLooping },
        { "stems4-dusk-volume", "dusk", state (Screen::Stems, false), 1.0f, volumes },
        { "stems4-dusk-preparing", "dusk", state (Screen::Stems, false), 1.0f, preparing },
        { "stems6-midnight-volume", "midnight", state (Screen::Stems, true), 1.0f, volumes6 },
        { "stems6-midnight-speed", "midnight", state (Screen::Stems, true), 1.0f, slow },
        { "stems4-dusk-zoom@2x", "dusk", state (Screen::Stems, false), 2.0f, zoomed },
        { "error-python-dusk", "dusk", withError (state (Screen::Error, false), ErrorKind::PythonMissing, {}, {}) },
        { "error-demucs-midnight", "midnight", withError (state (Screen::Error, false), ErrorKind::DemucsMissing, {}, "C:\\Python311\\python.exe") },
        { "error-failed-dusk", "dusk", withError (state (Screen::Error, false), ErrorKind::Failed, failedText, {}) },
        { "motion-dusk", "dusk", state (Screen::Stems, false), 1.0f, moving },
        { "drop-dusk@2x", "dusk", state (Screen::Drop, false), 2.0f },
        { "stems4-dusk@2x", "dusk", state (Screen::Stems, false), 2.0f, looping },
        { "help-play-dusk", "dusk", state (Screen::Stems, false), 1.0f, helpTab (0) },
        { "help-stems-midnight", "midnight", state (Screen::Drop, false), 1.0f, helpTab (1) },
        { "help-loop-dusk", "dusk", state (Screen::Drop, false), 1.0f, helpTab (2) },
        { "help-export-dusk@2x", "dusk", state (Screen::Drop, false), 2.0f, helpTab (3) },
        { "intro-dusk-0.6", "dusk", state (Screen::Drop, false), 1.0f, introAt (0.6f) },
        { "intro-dusk-end", "dusk", state (Screen::Drop, false), 1.0f, introAt (1.9f) },
        { "reset-menu-dusk", "dusk", state (Screen::Stems, false), 1.0f, tempoMenu },
        { "reset-menu-midnight", "midnight", state (Screen::Stems, true), 1.0f, keyMenu },
    };

    int failures = 0;
    std::map<juce::String, juce::Image> images;
    for (const auto& shot : shots)
    {
        StemSplitterProcessor proc;
        proc.introShown = true;                      // the shots are at rest unless their Setup sets an intro time
        proc.prepareToPlay (48000.0, 512);
        proc.setTheme (shot.theme);
        proc.setSixStems (shot.state.sixStems);
        StemSplitterEditor ed (proc);
        ed.forceState (shot.state);
        ed.tick();
        if (shot.state.screen == Screen::Stems)
            waitForPeaks (ed);
        if (shot.before != nullptr)
            shot.before (proc, ed);

        const auto image = ed.createComponentSnapshot (ed.getLocalBounds(), true, shot.scale);
        images[shot.name] = image;
        const auto file = outDir.getChildFile (shot.name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out))
            std::puts (file.getFullPathName().toRawUTF8());
        else
            ++failures;
        proc.player.unload();
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);   // deletes the reset menus (closed with their editors)

    // The intro's last frame is the plain frame, pixel for pixel.
    const auto& end = images["intro-dusk-end"];
    const auto& rest = images["drop-dusk"];
    bool same = end.getBounds() == rest.getBounds();
    for (int y = 0; same && y < end.getHeight(); ++y)
        for (int x = 0; same && x < end.getWidth(); ++x)
            if (end.getPixelAt (x, y) != rest.getPixelAt (x, y))
            {
                std::printf ("FAIL: intro-dusk-end differs from drop-dusk at (%d, %d): %s vs %s\n", x, y,
                             end.getPixelAt (x, y).toDisplayString (true).toRawUTF8(), rest.getPixelAt (x, y).toDisplayString (true).toRawUTF8());
                same = false;
            }
    if (same)
        std::puts ("intro-dusk-end equals drop-dusk pixel for pixel");
    else
        ++failures;
    return failures == 0 ? 0 : 1;
}

int runBench()
{
    const auto four = makeStems (tempDir().getChildFile ("four"), false);
    // The first image of a process starts Direct2D/GDI+ (0.25 to 1.4 s). A host or the window's peer pays that anyway, so warm it first:
    // startup_ms is then our own work.
    juce::Graphics (juce::Image (juce::Image::ARGB, 8, 8, true)).fillAll (juce::Colours::red);
    double startupMs = 0.0;
    // Startup: the editor's construction plus one full paint, for a processor whose state restores a Stems screen.
    {
        auto restored = [&] (StemSplitterProcessor& p)
        {
            juce::ValueTree v ("StemSplitter");
            v.setProperty ("input", four.getChildFile ("Travis Snippet.mp3").getFullPathName(), nullptr);
            v.setProperty ("stems", four.getFullPathName(), nullptr);
            juce::MemoryBlock mb;
            if (auto xml = v.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, mb);
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            p.introShown = true;
            p.prepareToPlay (48000.0, 512);
        };
        juce::Image img (juce::Image::ARGB, 760, 500, true, juce::SoftwareImageType());
        auto startup = [&] (bool paint, bool preload)
        {
            StemSplitterProcessor p;
            restored (p);
            if (preload)                                 // diagnostic: the load happens outside the timed part
                p.player.load ({ four.getChildFile ("vocals.wav"), four.getChildFile ("drums.wav"),
                                four.getChildFile ("bass.wav"), four.getChildFile ("other.wav") });
            const auto t0 = now();
            StemSplitterEditor e (p);
            if (paint)
            {
                juce::Graphics g (img);
                e.paintEntireComponent (g, true);
            }
            return now() - t0;
        };
        startupMs = startup (true, false);
        std::printf ("startup_ms=%.1f\nstartup_construct_only_ms=%.1f\nstartup_construct_no_load_ms=%.1f\n",
                     startupMs, startup (false, false), startup (false, true));
        if (startupMs > 300.0)
            std::printf ("FAIL: startup_ms %.1f is over the 300 ms budget\n", startupMs);
    }

    StemSplitterProcessor proc;
    proc.introShown = true;                          // the intro frames are timed on their own below
    proc.prepareToPlay (48000.0, 512);
    StemSplitterEditor ed (proc);
    UiState s;
    s.screen = Screen::Stems;
    s.stemDir = four;
    s.songName = "Travis Snippet.mp3";
    ed.forceState (s);
    ed.tick();
    waitForPeaks (ed);

    // A motion frame as the app paints it: the strips around the panel plus the playing row's waveform and time.
    auto& stems = ed.stemsScreen();
    proc.player.play();
    ed.tick();
    auto& waves = ed.background();
    const auto motion = waves.motionRegion();
    auto frameRegion = motion;                       // plus the strip a moving playhead repaints across all rows
    frameRegion.add (ed.getLocalArea (&stems, stems.playheadArea (0.38).expanded (1, 0)));

    auto averageMs = [&] (juce::Component& c, const juce::RectangleList<int>& region, int scale, int frames, bool moving)
    {
        juce::Image img (juce::Image::ARGB, 760 * scale, 500 * scale, true, juce::SoftwareImageType());
        float flow = 0.0f;
        double total = 0.0;
        for (int i = 0; i < frames + 5; ++i)             // 5 warm-up frames build the caches
        {
            if (moving)
                waves.setMotion (flow += 2.0f, i % 2 == 0 ? 0.2f : 0.9f);
            const auto t0 = now();
            {
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale ((float) scale));
                if (! region.isEmpty())
                    g.reduceClipRegion (region);
                c.paintEntireComponent (g, true);
            }
            if (i >= 5)
                total += now() - t0;
        }
        return total / frames;
    };

    const auto motionMs = averageMs (ed, frameRegion, 1, 120, true);
    std::printf ("motion_frame_ms=%.3f\n", motionMs);
    std::printf ("motion_frame_ms_2x=%.3f\n", averageMs (ed, frameRegion, 2, 120, true));
    std::printf ("background_only_ms=%.3f\n", averageMs (waves, motion, 1, 120, true));
    std::printf ("full_paint_ms=%.3f\n", averageMs (ed, {}, 1, 20, false));

    // Intro frames: 60 full paints over the whole intro (the panel and screen fade from 1.05 s), after 5 warm-ups. Every
    // intro frame is a full window, so the budget is on the renderer the window paints with (Direct2D); a software full
    // paint is over 4 ms even at rest (full_paint_ms), so the software figure is printed without a budget.
    const auto introFrames = [&] (const juce::ImageType& type, double& worst)
    {
        juce::Image img (juce::Image::ARGB, 760, 500, true, type);
        double total = 0.0;
        worst = 0.0;
        for (int i = -5; i < 60; ++i)
        {
            ed.setIntroTime (1.9f * (float) juce::jmax (0, i) / 60.0f);
            const auto t0 = now();
            {
                juce::Graphics g (img);
                ed.paintEntireComponent (g, true);
            }
            const auto ms = now() - t0;
            if (i >= 0)
            {
                total += ms;
                worst = juce::jmax (worst, ms);
            }
        }
        ed.setIntroTime ({});
        return total / 60.0;
    };
    double introWorst = 0.0, introSoftwareWorst = 0.0;
    const auto introMs = introFrames (juce::NativeImageType(), introWorst);
    const auto introSoftwareMs = introFrames (juce::SoftwareImageType(), introSoftwareWorst);
    std::printf ("intro_frame_ms=%.3f\nintro_frame_ms_max=%.3f\n", introMs, introWorst);
    std::printf ("intro_frame_ms_software=%.3f\nintro_frame_ms_software_max=%.3f\n", introSoftwareMs, introSoftwareWorst);

    // A zoom step: every row rebuilds its bars from its envelope (the repaint is async and not timed here).
    {
        const auto z0 = now();
        for (int i = 0; i < 50; ++i)
            stems.setView (i % 2 == 0 ? juce::Range<double> (0.2, 0.4) : juce::Range<double> (0.0, 1.0));
        std::printf ("zoom_rebuild_ms=%.3f\n", (now() - z0) / 50.0);
    }

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    proc.player.pause();
    proc.processBlock (buffer, midi);                    // the one faded block after pause()
    auto t0 = now();
    for (int i = 0; i < 20000; ++i)
        proc.processBlock (buffer, midi);
    std::printf ("process_block_us_idle=%.4f\n", (now() - t0) * 1000.0 / 20000.0);

    // "other" is never silent, so every burst must meter a peak. Bursts of 50 blocks let the read-ahead refill between them.
    for (int i = 0; i < stems.numRows(); ++i)
        proc.player.setMuted (i, i != 3);            // "other" only: it is never silent, so every burst must meter a peak
    proc.player.setPositionFraction (0.0);
    proc.player.play();
    juce::Thread::sleep (60);
    double total = 0.0, worst = 0.0;
    bool metered = true;
    for (int burst = 0; burst < 40; ++burst)
    {
        for (int i = 0; i < 50; ++i)
        {
            buffer.clear();
            t0 = now();
            proc.processBlock (buffer, midi);
            const auto us = (now() - t0) * 1000.0;
            total += us;
            worst = juce::jmax (worst, us);
        }
        metered = metered && proc.player.takePeak() > 0.0f;
        juce::Thread::sleep (60);
    }
    // The same bursts through the time-stretcher (1.25x, keeping the key): 30% of a 512-sample block at 48 kHz is 3200 us.
    proc.player.setSpeed (1.25);
    proc.player.setPositionFraction (0.0);
    proc.player.play();
    juce::Thread::sleep (60);
    double stretchTotal = 0.0, stretchWorst = 0.0;
    for (int burst = 0; burst < 20; ++burst)
    {
        for (int i = 0; i < 50; ++i)
        {
            buffer.clear();
            t0 = now();
            proc.processBlock (buffer, midi);
            const auto us = (now() - t0) * 1000.0;
            stretchTotal += us;
            stretchWorst = juce::jmax (stretchWorst, us);
        }
        juce::Thread::sleep (60);
    }
    const double stretchUs = stretchTotal / 1000.0;
    proc.player.unload();
    std::printf ("process_block_us_playing=%.3f\nprocess_block_us_playing_max=%.3f\n", total / 2000.0, worst);
    std::printf ("process_block_us_stretch=%.3f\nprocess_block_us_stretch_max=%.3f\n", stretchUs, stretchWorst);   // no budget on the max
    if (stretchUs > 3200.0)
        std::puts ("FAIL: process_block_us_stretch is over 30% of a 512-sample block at 48 kHz");
    if (! metered)
        std::puts ("FAIL: a playing burst metered no peak");
    if (motionMs > 4.0)
        std::puts ("FAIL: motion_frame_ms is over the 4 ms budget");
    if (introMs > 4.0)
        std::puts ("FAIL: intro_frame_ms is over the 4 ms budget");
    return metered && startupMs <= 300.0 && motionMs <= 4.0 && introMs <= 4.0 && stretchUs <= 3200.0 ? 0 : 1;
}
