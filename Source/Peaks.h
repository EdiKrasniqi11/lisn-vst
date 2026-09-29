#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <vector>

// Peak level (0..1, loudest channel) of `bars` equal slices of the file. Returns early (remaining bars 0) when shouldAbort() is true.
std::vector<float> readPeaks (juce::AudioFormatReader& reader, int bars, const std::function<bool()>& shouldAbort = {});
double lengthSeconds (const juce::AudioFormatReader& reader);   // 0 when sampleRate is 0

// Tempo of a percussive stem: bpm in 70..180 and the time of the first beat (seconds). bpm 0 = no steady beat found.
// ponytail: assumes a steady tempo and can report half or double tempo. Upgrade path: a x2 / /2 toggle, or tempo tracking.
struct Tempo { double bpm = 0.0, firstBeat = 0.0; };
Tempo detectTempo (juce::AudioFormatReader& reader);
