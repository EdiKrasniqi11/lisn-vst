#include "../Source/StemPreview.h"
#include <thread>

struct StemPreviewTests : juce::UnitTest
{
    StemPreviewTests() : juce::UnitTest ("StemPreview") {}

    static void writeSine (const juce::File& f)   // 2 s, 44.1 kHz stereo, 440 Hz at amplitude 0.5
    {
        juce::AudioBuffer<float> b (2, 88200);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto v = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 44100.0));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        auto out = f.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), 44100.0, 2, 24, {}, 0));
        if (w == nullptr) return;
        out.release();   // the writer owns the stream now
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    static float rms (const juce::AudioBuffer<float>& b, int start, int n, float offset = 0.0f)   // channel 0, around offset
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
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-preview", {});
        expect (dir.createDirectory().wasOk());
        const auto wav = dir.getChildFile ("sine.wav");
        writeSine (wav);

        StemPreview p;
        juce::AudioBuffer<float> buf (2, 512);
        auto pull = [&] (float fill)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), fill, buf.getNumSamples());
            p.addTo (buf);
        };

        beginTest ("load");
        p.prepare (48000, 512);
        expect (p.load (wav));
        juce::Thread::sleep (30);
        expect (p.getFile() == wav);
        expectWithinAbsoluteError (p.getLengthSeconds(), 2.0, 0.01);

        beginTest ("pass-through while not playing");
        pull (0.25f);
        expect (allEqual (buf, 0.25f));

        beginTest ("playing");
        p.play();
        expect (p.isPlaying());
        double sumSq = 0;
        for (int block = 1; block <= 20; ++block)
        {
            pull (0.0f);
            if (block >= 3) sumSq += juce::square ((double) rms (buf, 0, 512));
        }
        expectGreaterThan (std::sqrt (sumSq / 18), 0.2);
        const float peak = p.takePeak();
        expect (peak > 0.45f && peak < 0.55f, "peak " + juce::String (peak));
        expectEquals (p.takePeak(), 0.0f);

        beginTest ("position after resampling 44.1 kHz to 48 kHz");
        expectWithinAbsoluteError (p.getPositionFraction() * p.getLengthSeconds(), 20 * 512 / 48000.0, 0.05);

        beginTest ("pause");
        expectLessThan (msTaken ([&] { p.pause(); }), 20.0);
        expect (! p.isPlaying());
        pull (0.25f);                                        // the fade-out block
        const float head = rms (buf, 0, 128, 0.25f), tail = rms (buf, 384, 128, 0.25f);
        expect (head > 0.2f && tail < head / 2, "fade " + juce::String (head) + " -> " + juce::String (tail));
        pull (0.25f);
        expect (allEqual (buf, 0.25f));
        const double paused = p.getPositionFraction();
        for (int i = 0; i < 5; ++i) pull (0.25f);
        expectEquals (p.getPositionFraction(), paused);

        beginTest ("seek");
        p.setPositionFraction (0.5);
        expectWithinAbsoluteError (p.getPositionFraction(), 0.5, 0.01);
        juce::Thread::sleep (30);
        p.play();
        pull (0.0f);                                         // the transport really moved: the audio thread reports ~0.5 too
        expectWithinAbsoluteError (p.getPositionFraction(), 0.5, 0.01);
        expectGreaterThan (rms (buf, 256, 256), 0.2f);
        p.pause();
        pull (0.0f);

        beginTest ("end of file");
        p.setPositionFraction (0.98);
        juce::Thread::sleep (50);
        p.play();
        for (int i = 0; i < 400 && p.isPlaying(); ++i) { pull (0.0f); juce::Thread::sleep (1); }
        expect (! p.isPlaying());
        expect (p.takeReachedEnd());
        expect (! p.takeReachedEnd());
        p.play();                                            // after the end, play restarts from 0
        expect (p.isPlaying());
        expectEquals (p.getPositionFraction(), 0.0);
        p.pause();
        pull (0.0f);

        beginTest ("bad files");
        const auto txt = dir.getChildFile ("notes.txt");
        expect (txt.replaceWithText ("not audio"));
        expect (! p.load (dir.getChildFile ("missing.wav")));
        expect (! p.load (txt));
        expect (p.getFile() == juce::File());
        expectEquals (p.getLengthSeconds(), 0.0);
        p.play();
        expect (! p.isPlaying());

        beginTest ("no stall with a live audio thread");
        expect (p.load (wav));
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
        expectLessThan (msTaken ([&] { expect (p.load (wav)); }), 100.0, "load");
        expectLessThan (msTaken ([&] { p.unload(); }), 20.0, "unload");
        running = false;
        audio.join();

        beginTest ("oversized block");
        p.prepare (48000, 256);
        expect (p.load (wav));
        juce::Thread::sleep (30);
        p.play();
        juce::AudioBuffer<float> big (2, 1024);
        big.clear();
        p.addTo (big);
        expectGreaterThan (rms (big, 768, 256), 0.2f);

        p.unload();   // releases the file so the temp dir can go
        dir.deleteRecursively();
    }
};

static StemPreviewTests stemPreviewTests;
