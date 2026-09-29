#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <vector>

// Peak level (0..1, loudest channel) per 1/fps seconds of the file (the last frame takes the remainder). Returns early
// (remaining frames 0) when shouldAbort() is true.
std::vector<float> readEnvelope (juce::AudioFormatReader& reader, double fps, const std::function<bool()>& shouldAbort = {});
// `bars` levels across `view` (fractions of the envelope's length): each bar is the loudest frame its slice touches.
// An empty envelope gives `bars` zeros.
std::vector<float> barsFromEnvelope (const std::vector<float>& envelope, juce::Range<double> view, int bars);
double lengthSeconds (const juce::AudioFormatReader& reader);   // 0 when sampleRate is 0

// Tempo of a percussive stem: bpm in 70..180 and the time of the first beat (seconds). bpm 0 = no steady beat found.
// ponytail: assumes a steady tempo and can report half or double tempo. Upgrade path: a x2 / /2 toggle, or tempo tracking.
struct Tempo { double bpm = 0.0, firstBeat = 0.0; };
Tempo detectTempo (juce::AudioFormatReader& reader);
