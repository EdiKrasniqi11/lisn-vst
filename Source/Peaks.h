#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <vector>

// Peak level (0..1, loudest channel) of `bars` equal slices of the file. Returns early (remaining bars 0) when shouldAbort() is true.
std::vector<float> readPeaks (juce::AudioFormatReader& reader, int bars, const std::function<bool()>& shouldAbort = {});
double lengthSeconds (const juce::AudioFormatReader& reader);   // 0 when sampleRate is 0
