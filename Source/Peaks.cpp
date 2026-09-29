#include "Peaks.h"

std::vector<float> readPeaks (juce::AudioFormatReader& reader, int bars, const std::function<bool()>& shouldAbort)
{
    std::vector<float> peaks ((size_t) juce::jmax (0, bars), 0.0f);
    const auto len = reader.lengthInSamples;
    if (reader.numChannels == 0 || len <= 0 || bars <= 0) return peaks;

    // readMaxLevels asserts that the channel count is at most what the file has (juce_AudioFormatReader.cpp:218-221).
    std::vector<juce::Range<float>> ranges ((size_t) juce::jmin ((int) reader.numChannels, 8));
    for (int i = 0; i < bars; ++i)
    {
        if (shouldAbort && shouldAbort()) break;
        const auto start = len * i / bars, end = len * (i + 1) / bars;
        reader.readMaxLevels (start, end - start, ranges.data(), (int) ranges.size());
        float level = 0.0f;
        for (auto r : ranges) level = juce::jmax (level, std::abs (r.getStart()), std::abs (r.getEnd()));
        peaks[(size_t) i] = juce::jmin (1.0f, level);
    }
    return peaks;
}

double lengthSeconds (const juce::AudioFormatReader& reader)
{
    return reader.sampleRate > 0 ? (double) reader.lengthInSamples / reader.sampleRate : 0.0;
}

Tempo detectTempo (juce::AudioFormatReader& reader)
{
    const double rate = reader.sampleRate;
    if (reader.numChannels == 0 || rate <= 0) return {};
    const auto hop = (juce::int64) (rate / 100.0);            // a 100 Hz loudness envelope
    const int frames = (int) (reader.lengthInSamples / juce::jmax ((juce::int64) 1, hop));
    if (frames < 400) return {};                             // under 4 s: too short to trust

    std::vector<juce::Range<float>> ranges ((size_t) juce::jmin ((int) reader.numChannels, 8));
    std::vector<float> env ((size_t) frames), onset ((size_t) frames, 0.0f);
    for (int i = 0; i < frames; ++i)
    {
        reader.readMaxLevels ((juce::int64) i * hop, hop, ranges.data(), (int) ranges.size());
        float level = 0.0f;
        for (auto r : ranges) level = juce::jmax (level, std::abs (r.getStart()), std::abs (r.getEnd()));
        env[(size_t) i] = level;
        if (i > 0) onset[(size_t) i] = juce::jmax (0.0f, level - env[(size_t) i - 1]);   // where it gets louder
    }

    auto correlate = [&] (int lag)
    {
        double sum = 0;
        for (int i = lag; i < frames; ++i) sum += (double) onset[(size_t) i] * onset[(size_t) (i - lag)];
        return sum;
    };
    const double energy = correlate (0);
    const int minLag = 6000 / 180, maxLag = 6000 / 70 + 1;   // frames per beat for 180 .. 70 BPM
    std::vector<double> ac ((size_t) maxLag + 2);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) ac[(size_t) lag] = correlate (lag);
    int best = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (ac[(size_t) lag] > ac[(size_t) best]) best = lag;
    if (energy <= 0 || ac[(size_t) best] < 0.2 * energy) return {};

    // Parabolic refinement of the best lag, then the phase: the offset whose beats collect the most onset.
    const double a = ac[(size_t) best - 1], b = ac[(size_t) best], c = ac[(size_t) best + 1];
    const double bend = a - 2.0 * b + c;
    const double lag = best + (bend != 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / bend) : 0.0);
    int bestOffset = 0;
    double bestSum = -1.0;
    for (int offset = 0; offset < best; ++offset)
    {
        double sum = 0;
        for (double t = offset; t < frames - 2; t += lag)   // rounding can reach frames - 2, so i + 1 stays in range
        {
            const auto i = (size_t) juce::roundToInt (t);
            sum += juce::jmax (onset[i], onset[i + 1], i > 0 ? onset[i - 1] : 0.0f);   // +-1 frame: beats drift off the grid
        }
        if (sum > bestSum) { bestSum = sum; bestOffset = offset; }
    }
    return { 6000.0 / lag, bestOffset / 100.0 };
}
