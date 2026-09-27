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
