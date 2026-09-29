#include "Peaks.h"
#include <algorithm>

std::vector<float> readEnvelope (juce::AudioFormatReader& reader, double fps, const std::function<bool()>& shouldAbort)
{
    const auto len = reader.lengthInSamples;
    if (reader.numChannels == 0 || len <= 0 || fps <= 0.0 || reader.sampleRate <= 0.0) return {};
    const auto hop = juce::jmax ((juce::int64) 1, (juce::int64) std::round (reader.sampleRate / fps));
    std::vector<float> env ((size_t) ((len + hop - 1) / hop), 0.0f);

    // readMaxLevels asserts that the channel count is at most what the file has (juce_AudioFormatReader.cpp:218-221).
    std::vector<juce::Range<float>> ranges ((size_t) juce::jmin ((int) reader.numChannels, 8));
    for (size_t i = 0; i < env.size(); ++i)
    {
        if (shouldAbort && shouldAbort()) break;
        const auto start = (juce::int64) i * hop;
        reader.readMaxLevels (start, juce::jmin (hop, len - start), ranges.data(), (int) ranges.size());
        float level = 0.0f;
        for (auto r : ranges) level = juce::jmax (level, std::abs (r.getStart()), std::abs (r.getEnd()));
        env[i] = juce::jmin (1.0f, level);
    }
    return env;
}

std::vector<float> barsFromEnvelope (const std::vector<float>& env, juce::Range<double> view, int bars)
{
    std::vector<float> out ((size_t) juce::jmax (0, bars), 0.0f);
    if (env.empty()) return out;
    const auto n = (int) env.size();
    for (int i = 0; i < bars; ++i)
    {
        const auto a = view.getStart() + view.getLength() * i / bars, b = view.getStart() + view.getLength() * (i + 1) / bars;
        const auto from = juce::jlimit (0, n - 1, (int) std::floor (a * n));
        const auto to = juce::jlimit (from + 1, n, (int) std::ceil (b * n));
        out[(size_t) i] = *std::max_element (env.begin() + from, env.begin() + to);
    }
    return out;
}

double lengthSeconds (const juce::AudioFormatReader& reader)
{
    return reader.sampleRate > 0 ? (double) reader.lengthInSamples / reader.sampleRate : 0.0;
}

Tempo detectTempo (juce::AudioFormatReader& reader)
{
    const double rate = reader.sampleRate;
    if (reader.numChannels == 0 || rate <= 0) return {};
    const auto hop = juce::jmax ((juce::int64) 1, (juce::int64) (rate / 100.0));   // a ~100 Hz loudness envelope
    const double fps = rate / (double) hop;                                          // its exact frame rate
    const int frames = (int) (reader.lengthInSamples / hop);
    if (frames < 400) return {};                             // under 4 s: too short to trust

    std::vector<juce::Range<float>> ranges ((size_t) juce::jmin ((int) reader.numChannels, 8));
    std::vector<float> onset ((size_t) frames, 0.0f);
    float previous = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        reader.readMaxLevels ((juce::int64) i * hop, hop, ranges.data(), (int) ranges.size());
        float level = 0.0f;
        for (auto r : ranges) level = juce::jmax (level, std::abs (r.getStart()), std::abs (r.getEnd()));
        if (i > 0) onset[(size_t) i] = juce::jmax (0.0f, level - previous);   // where it gets louder
        previous = level;
    }

    // Autocorrelation of the mean-removed onsets: a steady beat stands out, a noise floor (whose onsets never go negative,
    // so they correlate at every lag) does not.
    double mean = 0;
    for (auto o : onset) mean += o;
    mean /= frames;
    auto correlate = [&] (int lag)
    {
        double sum = 0;
        for (int i = lag; i < frames; ++i) sum += (onset[(size_t) i] - mean) * (onset[(size_t) (i - lag)] - mean);
        return sum;
    };
    const double energy = correlate (0);
    const int minLag = (int) std::ceil (fps * 60.0 / 180.0), maxLag = (int) std::floor (fps * 60.0 / 70.0);
    int best = minLag;
    double bestAc = correlate (minLag);
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (const auto v = correlate (lag); v > bestAc) { bestAc = v; best = lag; }
    if (energy <= 0 || bestAc < 0.2 * energy) return {};

    // Fine search around the best lag in 0.01-frame steps, scored by the onset a beat grid collects over the whole song:
    // that pins the tempo (a 0.01-frame lag error is ~10 ms after 2 minutes at 90 BPM). Each beat takes its frame plus half
    // of its neighbours, so the grid centres on the onset instead of tying with the frames next to it.
    double bestLag = best, bestScore = -1.0;
    int bestOffset = 0;
    for (int step = -50; step <= 50; ++step)
    {
        const double lag = best + step * 0.01;
        for (int offset = 0; offset < best; ++offset)
        {
            double sum = 0;
            for (double t = offset; t < frames - 2; t += lag)   // rounding can reach frames - 2, so i + 1 stays in range
            {
                const auto i = (size_t) juce::roundToInt (t);
                sum += onset[i] + 0.5f * (onset[i + 1] + (i > 0 ? onset[i - 1] : 0.0f));
            }
            if (sum > bestScore) { bestScore = sum; bestLag = lag; bestOffset = offset; }
        }
    }
    return { 60.0 * fps / bestLag, bestOffset / fps };
}
