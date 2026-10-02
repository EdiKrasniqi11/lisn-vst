#include "../Source/StemPlayer.h"
#include <thread>
#include <algorithm>
#include <cmath>

struct StemPlayerTests : juce::UnitTest
{
    StemPlayerTests() : juce::UnitTest ("StemPlayer") {}

    // A 44.1 kHz, 24-bit stereo WAV of `seconds`, sample value fn (channel, index).
    static void writeWav (const juce::File& f, double seconds, const std::function<float (int, int)>& fn)
    {
        const int n = (int) (seconds * 44100.0);
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, fn (ch, i));
        f.deleteFile();
        auto out = f.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), 44100.0, 2, 24, {}, 0));
        if (w == nullptr) return;
        out.release();   // the writer owns the stream now
        w->writeFromAudioSampleBuffer (b, 0, n);
    }

    static float rms (const juce::AudioBuffer<float>& b, int start, int n, float offset = 0.0f)
    {
        double sum = 0;
        for (int i = start; i < start + n; ++i) sum += juce::square (b.getSample (0, i) - offset);
        return (float) std::sqrt (sum / n);
    }

    static bool allEqual (const juce::AudioBuffer<float>& b, float v)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! juce::exactlyEqual (b.getSample (ch, i), v)) return false;
        return true;
    }

    template <typename Fn> static double msTaken (Fn&& fn)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        fn();
        return juce::Time::getMillisecondCounterHiRes() - t0;
    }

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-player", {});
        expect (dir.createDirectory().wasOk());
        const auto sine = dir.getChildFile ("sine.wav");                 // 2 s, 440 Hz at 0.5 on both channels
        writeWav (sine, 2.0, [] (int, int i) { return (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 44100.0)); });
        const auto left = dir.getChildFile ("left.wav"), right = dir.getChildFile ("right.wav");   // one impulse at sample 10000
        writeWav (left,  1.0, [] (int ch, int i) { return ch == 0 && i == 10000 ? 1.0f : 0.0f; });   // ~10884 at 48 kHz: inside
        writeWav (right, 1.0, [] (int ch, int i) { return ch == 1 && i == 10000 ? 1.0f : 0.0f; });   // the 40 recorded blocks
        const auto steps = dir.getChildFile ("steps.wav");               // 1 s: 0.25 before 0.5 s, 0.75 after
        writeWav (steps, 1.0, [] (int, int i) { return i < 22050 ? 0.25f : 0.75f; });

        StemPlayer p;
        juce::AudioBuffer<float> buf (2, 512);
        auto pull = [&] (float fill)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), fill, buf.getNumSamples());
            p.addTo (buf);
        };

        beginTest ("load");
        p.prepare (48000, 512);
        expect (p.load ({ sine }));
        juce::Thread::sleep (30);
        expectEquals (p.getFiles().size(), 1);
        expectWithinAbsoluteError (p.getLengthSeconds(), 2.0, 0.01);
        expectEquals ((int) p.audibleMask(), 1);

        beginTest ("pass-through while not playing");
        pull (0.25f);
        expect (allEqual (buf, 0.25f));

        beginTest ("playing");
        p.play();
        expect (p.isPlaying());
        for (int i = 0; i < 20; ++i) pull (0.0f);
        expectGreaterThan (rms (buf, 0, 512), 0.2f);
        const float peak = p.takePeak();
        expect (peak > 0.45f && peak < 0.55f, "peak " + juce::String (peak));
        expectEquals (p.takePeak(), 0.0f);
        expectWithinAbsoluteError (p.getPositionFraction() * p.getLengthSeconds(), 20 * 512 / 48000.0, 0.01);

        beginTest ("mute is heard within one block");
        p.setMuted (0, true);
        expect (p.isMuted (0));
        expectEquals ((int) p.audibleMask(), 0);
        pull (0.25f);                                        // the ramp-down block
        const float head = rms (buf, 0, 128, 0.25f), tail = rms (buf, 384, 128, 0.25f);
        expect (head > 0.2f && tail < head / 2, "ramp " + juce::String (head) + " -> " + juce::String (tail));
        pull (0.25f);
        expect (allEqual (buf, 0.25f));
        p.setMuted (0, false);
        pull (0.0f);
        pull (0.0f);
        expectGreaterThan (rms (buf, 0, 512), 0.2f);

        beginTest ("volume is heard within one block");
        expectEquals (p.getVolume (0), 1.0f);
        pull (0.0f);
        const float full = rms (buf, 0, 512);
        p.setVolume (0, 0.5f);
        pull (0.0f);                                         // the ramp block
        pull (0.0f);
        expectWithinAbsoluteError (rms (buf, 0, 512), full * 0.5f, full * 0.1f);
        p.setVolume (0, 2.0f);                               // clamped to 1
        expectEquals (p.getVolume (0), 1.0f);
        pull (0.0f);
        pull (0.0f);
        expectWithinAbsoluteError (rms (buf, 0, 512), full, full * 0.1f);
        expectEquals (p.getVolume (99), 1.0f);               // out of range

        beginTest ("mixGains");
        p.setVolume (0, 0.25f);
        expect (p.mixGains() == std::vector<float> { 0.25f });
        p.setMuted (0, true);
        expect (p.mixGains() == std::vector<float> { 0.0f });
        p.setMuted (0, false);
        p.setVolume (0, 1.0f);

        beginTest ("pause");
        expectLessThan (msTaken ([&] { p.pause(); }), 20.0);
        pull (0.25f);                                        // the fade-out block
        pull (0.25f);
        expect (allEqual (buf, 0.25f));
        const double paused = p.getPositionFraction();
        for (int i = 0; i < 5; ++i) pull (0.25f);
        expectEquals (p.getPositionFraction(), paused);

        beginTest ("solo");
        expect (p.load ({ sine, sine, sine, sine }));
        p.solo (1);
        expectEquals ((int) p.audibleMask(), 0b0010);
        p.solo (1);                                          // the only audible one: everything back on
        expectEquals ((int) p.audibleMask(), 0b1111);
        p.setMuted (2, true);
        p.solo (0);
        expectEquals ((int) p.audibleMask(), 0b0001);
        p.solo (0);
        expectEquals ((int) p.audibleMask(), 0b1111);

        beginTest ("stems stay sample-aligned after resampling");
        expect (p.load ({ left, right }));
        juce::Thread::sleep (30);
        p.play();
        juce::AudioBuffer<float> recorded (2, 40 * 512);
        for (int block = 0; block < 40; ++block)
        {
            pull (0.0f);
            for (int ch = 0; ch < 2; ++ch)
                recorded.copyFrom (ch, block * 512, buf, ch, 0, 512);
            juce::Thread::sleep (1);
        }
        auto argmax = [&] (int ch)
        {
            int best = 0;
            for (int i = 1; i < recorded.getNumSamples(); ++i)
                if (std::abs (recorded.getSample (ch, i)) > std::abs (recorded.getSample (ch, best))) best = i;
            return best;
        };
        expectGreaterThan (std::abs (recorded.getSample (0, argmax (0))), 0.1f);
        expectEquals (argmax (0), argmax (1));
        p.pause();
        pull (0.0f);

        beginTest ("loop wraps exactly and never ends");
        p.prepare (44100, 512);                              // same rate as the file, so sample positions compare directly
        expect (p.load ({ steps }));
        p.setLoop (0.4, 0.6);                                // 17640 .. 26460; jumps into the loop
        expect (p.isLooping());
        expectWithinAbsoluteError (p.getPositionFraction(), 0.4, 0.001);
        juce::Thread::sleep (30);
        p.play();
        std::vector<float> out;
        for (int block = 0; block < 60; ++block)             // 30720 samples: about 3.5 loop passes
        {
            pull (0.0f);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
            juce::Thread::sleep (1);
        }
        std::vector<int> falls;                              // where 0.75 drops back to 0.25: the wrap
        for (size_t i = 1000; i < out.size(); ++i)
            if (out[i - 1] > 0.5f && out[i] < 0.5f) falls.push_back ((int) i);
        expect (falls.size() >= 3, "wraps: " + juce::String ((int) falls.size()));
        if (falls.size() >= 2)
        {
            expectWithinAbsoluteError (falls[0], 4410 + 4410, 8);   // 0.1 s at 0.25, then 0.1 s at 0.75
            expectWithinAbsoluteError (falls[1] - falls[0], 8820, 2);
        }
        expect (p.isPlaying());
        expect (! p.takeReachedEnd());
        p.setLooping (false);
        expect (! p.isLooping());
        expectEquals (p.getLoop().getStart(), 0.4);        // the range is kept
        p.pause();
        pull (0.0f);

        beginTest ("a loop set while the read-ahead is full wraps at the new end");
        p.prepare (44100, 512);
        expect (p.load ({ steps }));
        juce::Thread::sleep (30);
        p.play();
        for (int block = 0; block < 10; ++block) { pull (0.0f); juce::Thread::sleep (1); }
        juce::Thread::sleep (150);                           // the read-ahead fills ~0.74 s past the playhead
        p.setLoop (0.45, 0.55);                              // 19845 .. 24255, inside the buffered range; spans the step at 22050
        juce::Thread::sleep (30);
        std::vector<float> after;
        for (int block = 0; block < 15; ++block)             // 7680 samples: the wrap is due after 2205 + 2205
        {
            pull (0.0f);
            after.insert (after.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
            juce::Thread::sleep (1);
        }
        int firstFall = -1;
        for (size_t i = 1; i < after.size() && firstFall < 0; ++i)
            if (after[i - 1] > 0.5f && after[i] < 0.5f) firstFall = (int) i;
        expectWithinAbsoluteError (firstFall, 2205 + 2205, 8);
        p.pause();
        pull (0.0f);

        beginTest ("a seek before prepare() is kept");
        {
            StemPlayer q;
            expect (q.load ({ steps }));
            q.setPositionFraction (0.75);                    // in the 0.75 half of the file
            q.prepare (44100, 512);
            juce::Thread::sleep (30);
            expectWithinAbsoluteError (q.getPositionFraction(), 0.75, 0.001);
            q.play();
            juce::AudioBuffer<float> b (2, 512);
            for (int i = 0; i < 3; ++i) { b.clear(); q.addTo (b); juce::Thread::sleep (1); }
            expectWithinAbsoluteError (b.getSample (0, 256), 0.75f, 0.01f);
            q.unload();
        }

        beginTest ("zero-length stems are rejected");
        const auto empty = dir.getChildFile ("empty.wav");
        writeWav (empty, 0.0, [] (int, int) { return 0.0f; });
        expect (! p.load ({ empty }));

        beginTest ("end of the song");
        p.prepare (48000, 512);
        expect (p.load ({ sine }));
        p.setPositionFraction (0.98);
        juce::Thread::sleep (50);
        p.play();
        for (int i = 0; i < 400 && p.isPlaying(); ++i) { pull (0.0f); juce::Thread::sleep (1); }
        expect (! p.isPlaying());
        expect (p.takeReachedEnd());
        expect (! p.takeReachedEnd());
        p.play();                                            // after the end, play restarts from 0
        expectEquals (p.getPositionFraction(), 0.0);
        p.pause();
        pull (0.0f);

        beginTest ("bad files and too many stems");
        const auto txt = dir.getChildFile ("notes.txt");
        expect (txt.replaceWithText ("not audio"));
        expect (! p.load ({ sine, dir.getChildFile ("missing.wav") }));
        expect (! p.load ({ txt }));
        expect (! p.load ({}));
        expectEquals (p.getFiles().size(), 0);
        p.play();
        expect (! p.isPlaying());
        juce::Array<juce::File> nine;
        for (int i = 0; i < 9; ++i) nine.add (sine);
        expect (p.load (nine));
        expectEquals (p.getFiles().size(), StemPlayer::maxStems);

        beginTest ("no stall with a live audio thread");
        expect (p.load ({ sine, sine, sine, sine }));
        juce::Thread::sleep (30);
        std::atomic<bool> running { true };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> b (2, 512);
            while (running)
            {
                b.clear();
                p.addTo (b);
                juce::Thread::sleep (5);
            }
        });
        p.play();
        juce::Thread::sleep (50);
        expectLessThan (msTaken ([&] { p.pause(); }), 20.0, "pause");
        expectLessThan (msTaken ([&] { p.play(); }), 20.0, "play");
        expectLessThan (msTaken ([&] { p.setMuted (1, true); }), 20.0, "mute");
        expectLessThan (msTaken ([&] { p.setLoop (0.2, 0.4); }), 20.0, "loop");
        expectLessThan (msTaken ([&] { p.setSpeed (1.2); }), 20.0, "speed");
        expectLessThan (msTaken ([&] { p.setVolume (0, 0.5f); }), 20.0, "volume");
        juce::Thread::sleep (30);                            // a few stretched blocks run on the audio thread
        expectLessThan (msTaken ([&] { expect (p.load ({ sine, sine })); }), 100.0, "load");
        expectLessThan (msTaken ([&] { p.unload(); }), 20.0, "unload");
        running = false;
        audio.join();

        beginTest ("oversized block");
        p.prepare (48000, 256);
        expect (p.load ({ sine }));
        juce::Thread::sleep (30);
        p.play();
        juce::AudioBuffer<float> big (2, 1024);
        big.clear();
        p.addTo (big);
        expectGreaterThan (rms (big, 768, 256), 0.2f);

        beginTest ("load resets the volumes and the speed");
        p.setVolume (0, 0.3f);
        p.setSpeed (1.3);
        expect (p.load ({ sine }));
        expectEquals (p.getVolume (0), 1.0f);
        expectEquals (p.getSpeed(), 1.0);

        beginTest ("speed keeps the key and moves the playhead faster");
        p.prepare (48000, 512);
        expect (p.load ({ sine }));
        juce::Thread::sleep (100);                           // the read-ahead fills (40 stretched blocks read ~25600 samples)
        p.setSpeed (2.0);                                    // clamped
        expectEquals (p.getSpeed(), 1.5);
        p.setSpeed (0.99999999);                             // rounded to hundredths, as renderName names it
        expectEquals (p.getSpeed(), 1.0);
        p.setSpeed (1.234);
        expectEquals (p.getSpeed(), 1.23);
        p.setSpeed (1.25);
        p.play();
        juce::AudioBuffer<float> heard (1, 512 * 40);
        for (int k = 0; k < 40; ++k)
        {
            pull (0.0f);
            heard.copyFrom (0, k * 512, buf, 0, 0, 512);
            if (k % 10 == 9) juce::Thread::sleep (10);       // lets the read-ahead keep up with the faster reads
        }
        expectWithinAbsoluteError (p.getPositionFraction() * p.getLengthSeconds(), 40 * 512 / 48000.0 * 1.25, 0.02);
        {
            const int from = 9600, len = 40 * 512 - from;        // after the stretcher's ~60 ms fade-in
            bool finite = true;
            for (int i = from; i < from + len; ++i) finite = finite && std::isfinite (heard.getSample (0, i));
            expect (finite, "NaN or inf in the stretched output");
            const float level = rms (heard, from, len);
            expect (level > 0.354f * 0.891f && level < 0.354f * 1.122f, "rms " + juce::String (level));   // within 1 dB
            int crossings = 0;
            for (int i = from + 1; i < from + len; ++i)
                crossings += (heard.getSample (0, i - 1) < 0.0f) != (heard.getSample (0, i) < 0.0f);
            expectWithinAbsoluteError (crossings / 2.0 / (len / 48000.0), 440.0, 4.4);
        }

        beginTest ("pause fades the stretched output");
        p.pause();
        pull (0.0f);                                         // the fade-out block
        expectLessThan (rms (buf, 384, 128), rms (buf, 0, 128));
        pull (0.0f);
        expect (allEqual (buf, 0.0f));

        beginTest ("at 1x after a seek the stems play directly again");
        p.setSpeed (1.0);
        p.setPositionFraction (0.0);
        p.play();
        juce::Thread::sleep (30);
        pull (0.0f);
        expectLessThan (rms (buf, 0, 64), rms (buf, 448, 64) / 4,    // a sine from phase 0 already has a smaller head, so a plain "<" cannot tell a ramp from a step
                        "the first block after a resume ramps in");
        pull (0.0f);                                         // stretched, this block would still be in the ~60 ms fade-in
        expectGreaterThan (rms (buf, 0, 512), 0.3f);
        p.pause();
        pull (0.0f);

        beginTest ("engaging the stretcher mid-play fades the direct audio out, not to silence");
        expect (p.load ({ sine }));
        juce::Thread::sleep (100);                           // the read-ahead fills
        p.play();
        for (int k = 0; k < 5; ++k) pull (0.0f);             // 1x: the stems play directly
        p.setSpeed (1.25);
        pull (0.0f);                                         // the engaging block: the stretcher is still in its pre-roll
        expectGreaterThan (rms (buf, 0, 64), 0.2f);

        beginTest ("a loop edit that keeps the playhead does not reset the stretcher");
        for (int k = 0; k < 20; ++k)                         // ~0.2 s: past the stretcher's fade-in
        {
            pull (0.0f);
            if (k % 10 == 9) juce::Thread::sleep (10);
        }
        p.setLoop (0.0, 0.9);                                // the playhead (~0.3 s) is inside, so it stays
        juce::Thread::sleep (30);                            // the read-ahead refills at the re-based range
        pull (0.0f);
        expectGreaterThan (rms (buf, 0, 512), 0.3f);         // a reset drops to the pre-roll; the engage fade-out alone is ~0.2
        p.pause();
        pull (0.0f);

        beginTest ("render");
        const auto a = dir.getChildFile ("a.wav"), b = dir.getChildFile ("b.wav");
        writeWav (a, 1.0, [] (int, int i) { return (float) (0.3 * std::sin (0.04 * i)); });
        writeWav (b, 1.0, [] (int, int i) { return (float) (0.2 * std::sin (0.07 * i)); });
        const auto both = dir.getChildFile ("renders").getChildFile ("both.wav");
        expect (StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.25, 0.75, 1.0, both));
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (both));
            expect (r != nullptr);
            if (r != nullptr)
            {
                expectWithinAbsoluteError ((int) r->lengthInSamples, 22050, 1);
                juce::AudioBuffer<float> got (2, 22050);
                r->read (&got, 0, 22050, 0, true, true);
                float worst = 0.0f;
                for (int i = 0; i < 22050; ++i)
                {
                    const int s = 11025 + i;
                    const float want = (float) (0.3 * std::sin (0.04 * s) + 0.2 * std::sin (0.07 * s));
                    worst = juce::jmax (worst, std::abs (got.getSample (0, i) - want), std::abs (got.getSample (1, i) - want));
                }
                expectLessThan (worst, 1e-4f);
            }
        }
        expect (! StemPlayer::render ({ a, b }, { 0.0f, 0.0f }, 0.0, 1.0, 1.0, dir.getChildFile ("none.wav")));
        expect (! StemPlayer::render ({ a, b }, { 1.0f, 0.0f }, 0.5, 0.5, 1.0, dir.getChildFile ("empty-range.wav")));
        expect (! dir.getChildFile ("none.wav").exists() && ! dir.getChildFile ("empty-range.wav").exists());

        beginTest ("render applies the gains, and rendering again overwrites");
        expect (StemPlayer::render ({ a, b }, { 0.5f, 1.0f }, 0.25, 0.75, 1.0, both));   // callers cache by name, render never does
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (both));
            expect (r != nullptr);
            if (r != nullptr)
            {
                juce::AudioBuffer<float> got (2, 22050);
                r->read (&got, 0, 22050, 0, true, true);
                float worst = 0.0f;
                for (int i = 0; i < 22050; ++i)
                {
                    const int s = 11025 + i;
                    const float want = (float) (0.5 * 0.3 * std::sin (0.04 * s) + 0.2 * std::sin (0.07 * s));
                    worst = juce::jmax (worst, std::abs (got.getSample (0, i) - want), std::abs (got.getSample (1, i) - want));
                }
                expectLessThan (worst, 1e-4f);
            }
        }

        beginTest ("render reports progress and can be cancelled");
        std::vector<float> seen;
        expect (StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.0, 1.0, 1.0, dir.getChildFile ("renders").getChildFile ("progress.wav"),
                                    [&] (float f) { seen.push_back (f); return true; }));
        expect (! seen.empty() && juce::exactlyEqual (seen.back(), 1.0f) && std::is_sorted (seen.begin(), seen.end()));
        const auto cancelled = dir.getChildFile ("renders").getChildFile ("cancelled.wav");
        expect (! StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.0, 1.0, 1.0, cancelled, [] (float) { return false; }));
        expect (! cancelled.exists() && ! cancelled.getSiblingFile ("cancelled.wav.part").exists());

        beginTest ("a render that cannot replace dest leaves no .part");
        const auto held = dir.getChildFile ("renders").getChildFile ("held.wav");
        {
            juce::FileOutputStream hold (held);                  // open in another app: the rename cannot replace it
            expect (hold.openedOk());
            expect (! StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.0, 1.0, 1.0, held));
        }
        expect (! held.getSiblingFile ("held.wav.part").exists());

        beginTest ("renderName");
        const juce::StringArray stemNames { "vocals", "drums" };
        expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 1.0f }, 1.0, {}), juce::String ("Song - vocals+drums.wav"));
        expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 0.85f, 0.0f }, 1.0, {}), juce::String ("Song - vocals 85.wav"));
        expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 1.0f }, 1.25, {}), juce::String ("Song - vocals+drums x1.25.wav"));
        expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 0.0f }, 1.0, { 58.25, 86.5 }),
                      juce::String ("Song - vocals (0.58.25-1.26.50).wav"));
        expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 0.0f, 0.0f }, 1.0, {}), juce::String());
        expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 1.0f }, 1.0, {}, 3), juce::String ("Song - vocals+drums +3st.wav"));
        expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 1.0f }, 1.25, {}, -12), juce::String ("Song - vocals+drums x1.25 -12st.wav"));
        {
            StemPlayer p;
            p.setPitch (20);
            expectEquals (p.getPitch(), 12);
            p.setPitch (-20);
            expectEquals (p.getPitch(), -12);
        }

        beginTest ("render keeps the key at other speeds, at the exact length");
        for (const double s : { 0.75, 1.25 })
        {
            const auto out = dir.getChildFile ("renders").getChildFile ("tone x" + juce::String (s, 2) + ".wav");
            expect (StemPlayer::render ({ sine }, { 1.0f }, 0.0, 2.0, s, out), "render at " + juce::String (s));
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (out));
            expect (r != nullptr);
            if (r == nullptr) continue;
            expectWithinAbsoluteError ((double) r->lengthInSamples, 2.0 * 44100.0 / s, 1.0);
            const int len = (int) r->lengthInSamples;
            juce::AudioBuffer<float> got (2, len);
            r->read (&got, 0, len, 0, true, true);
            const int from = len / 4, n = len / 2;           // the middle, away from the edges
            int crossings = 0;
            for (int i = from + 1; i < from + n; ++i)
                crossings += (got.getSample (0, i - 1) < 0.0f) != (got.getSample (0, i) < 0.0f);
            expectWithinAbsoluteError (crossings / 2.0 / (n / 44100.0), 440.0, 4.4);
            const float level = rms (got, from, n);
            expect (level > 0.354f * 0.891f && level < 0.354f * 1.122f, "rms " + juce::String (level));
        }

        beginTest ("a cancelled stretched render leaves no file");
        const auto cut = dir.getChildFile ("renders").getChildFile ("cut.wav");
        int calls = 0;
        expect (! StemPlayer::render ({ sine }, { 1.0f }, 0.0, 2.0, 1.25, cut, [&] (float) { return ++calls < 2; }));
        expect (! cut.exists() && ! cut.getSiblingFile ("cut.wav.part").exists());

        beginTest ("a tiny range still stretches to the exact length");
        const auto tiny = dir.getChildFile ("renders").getChildFile ("tiny.wav");
        expect (StemPlayer::render ({ sine }, { 1.0f }, 0.5, 0.6, 1.25, tiny));
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (tiny));
            expect (r != nullptr);
            if (r != nullptr)
            {
                expectEquals ((int) r->lengthInSamples, 3528);   // llround (4410 / 1.25)
                juce::AudioBuffer<float> got (2, 3528);
                r->read (&got, 0, 3528, 0, true, true);
                expectGreaterThan (rms (got, 200, 1500), 0.2f);   // stretched audio, not silence
            }
        }

        p.unload();   // releases the files so the temp dir can go
        dir.deleteRecursively();
    }
};

static StemPlayerTests stemPlayerTests;
