#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

// Plays one stem file through the plugin's output, on top of the audio passing through.
// Message thread: load/unload/play/pause/seek/queries. Audio thread: addTo(). UI: takePeak()/takeReachedEnd().
// Never calls transport.stop(): it busy-waits for the audio thread (juce_AudioTransportSource.cpp:133-145).
// Play/pause is our own atomic flag, and the position is read from an atomic, never through the transport's lock.
class StemPreview
{
public:
    StemPreview();
    ~StemPreview();

    void prepare (double sampleRate, int maxBlockSize);   // from prepareToPlay
    void release();                                        // from releaseResources

    bool load (const juce::File& audioFile);   // pauses, then loads at position 0; false if unreadable (nothing loaded)
    void unload();
    juce::File getFile() const { return file; }
    void play();                                // no-op without a loaded file; restarts from 0 if the stream had finished
    void pause();                               // never blocks: the audio thread renders one faded-out block, then stops pulling
    bool isPlaying() const { return wanted.load(); }
    double getLengthSeconds() const { return lengthSec; }
    double getPositionFraction() const;         // 0..1, from readPos (no lock)
    void setPositionFraction (double);
    bool takeReachedEnd() { return reachedEnd.exchange (false); }   // true once after the stem played to its end

    void addTo (juce::AudioBuffer<float>& buffer);   // audio thread; idle = one relaxed atomic load, then return
    float takePeak() { return peak.exchange (0.0f); }  // highest preview sample level since the last call

private:
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "LISN preview read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> scratch;
    std::atomic<bool> wanted { false }, prepared { false }, reachedEnd { false };
    std::atomic<juce::int64> readPos { 0 };      // host-rate samples, written by addTo and setPositionFraction
    std::atomic<float> peak { 0.0f };
    bool sounding = false;                       // audio thread only: last block was audible (for the fade-out block)
    double lengthSec = 0.0, hostRate = 44100.0;
    juce::File file;
};
