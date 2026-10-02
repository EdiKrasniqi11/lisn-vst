#include "../Source/RenderJob.h"

struct RenderJobTests : juce::UnitTest
{
    RenderJobTests() : juce::UnitTest ("RenderJob") {}

    // A 44.1 kHz, 24-bit stereo WAV of `seconds`, sample value fn (channel, index) (as in StemPlayerTests).
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

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-renderjob", {});
        expect (dir.createDirectory().wasOk());
        const auto a = dir.getChildFile ("a.wav"), b = dir.getChildFile ("b.wav");
        writeWav (a, 3.0, [] (int, int i) { return (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 44100.0)); });
        writeWav (b, 3.0, [] (int, int i) { return (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / 44100.0)); });

        beginTest ("a RenderJob renders on its thread and reports done");
        RenderJob job;
        job.start ({ { a, b }, { 1.0f, 1.0f }, 0.0, 3.0, 1.25, dir.getChildFile ("mix x1.25.wav") });
        for (int i = 0; i < 400 && job.getState() == RenderJob::State::running; ++i) juce::Thread::sleep (10);
        expect (job.getState() == RenderJob::State::done);
        expectEquals (job.getProgress(), 1.0f);
        expect (dir.getChildFile ("mix x1.25.wav").existsAsFile());

        beginTest ("cancel stops it, leaves no file and goes back to idle");
        job.start ({ { a, b }, { 1.0f, 1.0f }, 0.0, 3.0, 0.75, dir.getChildFile ("mix x0.75.wav") });
        job.cancel();
        expect (job.getState() == RenderJob::State::idle);
        expect (! dir.getChildFile ("mix x0.75.wav").exists() && ! dir.getChildFile ("mix x0.75.wav.part").exists());

        beginTest ("a bad stem fails");
        job.start ({ { dir.getChildFile ("missing.wav") }, { 1.0f }, 0.0, 1.0, 1.25, dir.getChildFile ("bad.wav") });
        for (int i = 0; i < 200 && job.getState() == RenderJob::State::running; ++i) juce::Thread::sleep (10);
        expect (job.getState() == RenderJob::State::failed);

        job.cancel();
        dir.deleteRecursively();
    }
};

static RenderJobTests renderJobTests;
