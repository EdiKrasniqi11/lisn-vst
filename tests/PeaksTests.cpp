#include "../Source/Peaks.h"

struct PeaksTests : juce::UnitTest
{
    PeaksTests() : juce::UnitTest ("Peaks") {}

    void runTest() override
    {
        // Mono 44.1 kHz, 4 equal segments: silence, a 0.5 sine, silence, a 1.0 sine (441 Hz, so each cycle hits its peak).
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

        beginTest ("peaks per slice");
        const auto peaks = readPeaks (*reader, 4);
        expectEquals ((int) peaks.size(), 4);
        const float want[] = { 0.0f, 0.5f, 0.0f, 1.0f };
        for (size_t i = 0; i < peaks.size(); ++i)
            expectWithinAbsoluteError (peaks[i], want[i], 0.02f);

        beginTest ("no bars");
        expect (readPeaks (*reader, 0).empty());

        beginTest ("abort");
        int calls = 0;
        const auto aborted = readPeaks (*reader, 4, [&] { return ++calls > 1; });
        expectEquals ((int) aborted.size(), 4);
        expect (aborted[1] == 0.0f && aborted[3] == 0.0f);   // bar 0 read, then stopped

        beginTest ("length");
        expectWithinAbsoluteError (lengthSeconds (*reader), 1.0, 1e-9);
    }
};

static PeaksTests peaksTests;
