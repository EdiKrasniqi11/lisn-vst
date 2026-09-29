#include "../Source/Peaks.h"

struct PeaksTests : juce::UnitTest
{
    PeaksTests() : juce::UnitTest ("Peaks") {}

    // A mono 44.1 kHz WAV in memory: `seconds` long, white noise at `floor`, plus 60 ms decaying noise bursts every 60/bpm
    // seconds from `offset`, alternately 0.9 and 0.6.
    static std::unique_ptr<juce::AudioFormatReader> clicks (juce::MemoryBlock& wav, double bpm, double offset,
                                                            double seconds = 10.0, float floor = 0.0f)
    {
        const int n = (int) (seconds * 44100.0);
        juce::AudioBuffer<float> b (1, n);
        juce::Random background (2), noise (1);
        for (int i = 0; i < n; ++i)
            b.setSample (0, i, floor * (background.nextFloat() * 2.0f - 1.0f));
        if (bpm > 0)
            for (int k = 0;; ++k)
            {
                const int at = (int) std::round ((offset + k * 60.0 / bpm) * 44100.0);
                if (at >= n) break;
                for (int i = 0; i < 2646 && at + i < n; ++i)
                    b.addSample (0, at + i, (k % 2 == 0 ? 0.9f : 0.6f) * std::exp ((float) -i / 882.0f) * (noise.nextFloat() * 2.0f - 1.0f));
            }
        {
            std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (
                new juce::MemoryOutputStream (wav, false), 44100.0, 1, 24, {}, 0));   // the writer owns the stream
            w->writeFromAudioSampleBuffer (b, 0, n);
        }
        return std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (new juce::MemoryInputStream (wav, false), true));
    }

    void runTest() override
    {
        // Mono 44.1 kHz, 4 equal segments: silence, a 0.5 sine, silence, a 1.0 sine (441 Hz, so each cycle hits its peak).
        {
            const int seg = 11025;
            juce::AudioBuffer<float> b (1, 4 * seg);
            b.clear();
            for (int i = 0; i < seg; ++i)
            {
                const auto v = (float) std::sin (juce::MathConstants<double>::twoPi * i / 100.0);
                b.setSample (0, seg + i, 0.5f * v);
                b.setSample (0, 3 * seg + i, v);
            }
            juce::MemoryBlock wav;
            {
                std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (
                    new juce::MemoryOutputStream (wav, false), 44100.0, 1, 24, {}, 0));   // the writer owns the stream
                expect (w != nullptr);
                if (w == nullptr) return;
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            std::unique_ptr<juce::AudioFormatReader> reader (juce::WavAudioFormat().createReaderFor (new juce::MemoryInputStream (wav, false), true));
            expect (reader != nullptr);
            if (reader == nullptr) return;

            beginTest ("envelope and bars");
            const auto env = readEnvelope (*reader, 100.0);
            expectEquals ((int) env.size(), 100);                       // 1 s at 100 frames per second (441 samples each)
            const auto whole = barsFromEnvelope (env, { 0.0, 1.0 }, 4);
            const float want[] = { 0.0f, 0.5f, 0.0f, 1.0f };
            for (size_t i = 0; i < whole.size(); ++i)
                expectWithinAbsoluteError (whole[i], want[i], 0.02f);
            const auto zoomed = barsFromEnvelope (env, { 0.45, 0.55 }, 2);   // the end of the 0.5 sine, then silence
            expectWithinAbsoluteError (zoomed[0], 0.5f, 0.02f);
            expectWithinAbsoluteError (zoomed[1], 0.0f, 0.02f);
            for (auto v : barsFromEnvelope (env, { 0.3, 0.3001 }, 10))       // narrower than a frame: that frame
                expectWithinAbsoluteError (v, 0.5f, 0.02f);
            expectEquals ((int) barsFromEnvelope ({}, { 0.0, 1.0 }, 104).size(), 104);   // no envelope yet: flat bars
            expect (barsFromEnvelope (env, { 0.0, 1.0 }, 0).empty());

            beginTest ("abort");
            int calls = 0;
            const auto aborted = readEnvelope (*reader, 100.0, [&] { return ++calls > 30; });
            expectEquals ((int) aborted.size(), 100);
            expect (aborted[80] == 0.0f && env[80] > 0.9f);             // frames 0-29 read, then stopped

            beginTest ("length");
            expectWithinAbsoluteError (lengthSeconds (*reader), 1.0, 1e-9);
        }

        beginTest ("tempo 120 on the beat");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 120.0, 0.0);
            const auto t = detectTempo (*r);
            expectWithinAbsoluteError (t.bpm, 120.0, 1.0);
            expectWithinAbsoluteError (t.firstBeat, 0.0, 0.02);
        }

        beginTest ("tempo 92 from 0.1 s");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 92.0, 0.1);
            const auto t = detectTempo (*r);
            expectWithinAbsoluteError (t.bpm, 92.0, 1.5);
            expectWithinAbsoluteError (t.firstBeat, 0.1, 0.02);
        }

        beginTest ("no tempo in silence");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 0.0, 0.0);
            expectEquals (detectTempo (*r).bpm, 0.0);
        }

        beginTest ("no tempo in a noise floor");
        for (const float floor : { 0.0005f, 0.01f, 1.0f })
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 0.0, 0.0, 10.0, floor);
            expectEquals (detectTempo (*r).bpm, 0.0, "floor " + juce::String (floor));
        }

        beginTest ("clicks over a noise floor");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 120.0, 0.0, 10.0, 0.02f);
            expectWithinAbsoluteError (detectTempo (*r).bpm, 120.0, 1.0);
        }

        beginTest ("the grid holds to the end of a 2-minute song");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 92.0, 0.1, 120.0);
            const auto t = detectTempo (*r);
            expectWithinAbsoluteError (t.bpm, 92.0, 0.05);
            expectWithinAbsoluteError (t.firstBeat, 0.1, 0.015);
            const int last = (int) ((120.0 - 0.1) * 92.0 / 60.0);   // the last beat in the file
            expectWithinAbsoluteError (t.firstBeat + last * 60.0 / t.bpm, 0.1 + last * 60.0 / 92.0, 0.03);
        }
    }
};

static PeaksTests peaksTests;
