#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>

// Plays every stem of a song in lockstep through the plugin's output, on top of the audio passing through.
// Message thread: load/unload/play/pause/seek/mutes/loop/queries. Audio thread: addTo(). UI: takePeak()/takeReachedEnd().
// Never calls transport.stop(): it busy-waits for the audio thread (juce_AudioTransportSource.cpp:133-145).
// Play/pause is our own atomic flag; the position is read from an atomic, never through the transport's lock.
class StemPlayer
{
public:
    static constexpr int maxStems = 8;

    StemPlayer();
    ~StemPlayer();

    void prepare (double sampleRate, int maxBlockSize);   // from prepareToPlay
    void release();                                        // from releaseResources

    // Pauses, then loads the stems at position 0, all audible, no loop. False (nothing loaded) if there are none, a file is
    // unreadable or the sample rates differ. Stems past maxStems are ignored.
    bool load (const juce::Array<juce::File>& stems);
    void unload();
    const juce::Array<juce::File>& getFiles() const { return files; }

    void play();                                // no-op without stems; restarts from 0 if the song had finished
    void pause();                               // never blocks: the audio thread renders one faded-out block, then stops pulling
    bool isPlaying() const { return wanted.load(); }
    double getLengthSeconds() const { return lengthSec; }
    double getPositionFraction() const;         // 0..1 in the song (loop-wrapped), from an atomic (no lock)
    void setPositionFraction (double);
    bool takeReachedEnd() { return reachedEnd.exchange (false); }   // true once after the song played to its end

    void setMuted (int stem, bool);
    bool isMuted (int stem) const;
    void solo (int stem);                       // FL: only this stem on; if it already is the only one on, all on
    juce::uint32 audibleMask() const;           // bit i set = stem i audible

    void setLoop (double startFraction, double endFraction);   // sets the range and switches looping on
    void setLooping (bool);                     // keeps the range; turning it on jumps into the loop if outside it
    bool isLooping() const { return looping.load(); }
    juce::Range<double> getLoop() const { return loop; }        // fractions; empty until a loop was set

    void addTo (juce::AudioBuffer<float>& buffer);   // audio thread; idle = two relaxed atomic loads, then return
    float takePeak() { return peak.exchange (0.0f); }  // highest mix level since the last call

    // The stems in audibleMask summed over [startSec, endSec) into a 24-bit stereo WAV at the stems' rate. Reuses dest if it
    // already exists (renders are cached by name). False if nothing is audible, the range is empty or a file is unreadable.
    static bool render (const juce::Array<juce::File>& stems, juce::uint32 audibleMask, double startSec, double endSec,
                        const juce::File& dest);

private:
    static constexpr int readAheadSamples = 32768;
    class Stack;
    juce::int64 songPosition (juce::int64 linear) const;   // read-ahead position -> song position (loop-wrapped)
    bool wraps() const;                                     // looping, and playback started before the loop end
    juce::int64 linearPosition() const;                     // the audio thread's position, in source samples
    void seekSource (juce::int64 songSample, bool loopOn);  // jumps the read-ahead to a fresh range, then publishes the state

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "LISN player read-ahead" };
    std::unique_ptr<Stack> stack;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> scratch, mix;      // 2 * maxStems channels / stereo, sized in prepare()
    juce::Array<juce::File> files;
    std::array<std::atomic<bool>, maxStems> muted {};
    std::array<float, maxStems> gains {};       // audio thread: each stem's gain at the end of the last block
    std::atomic<int> stemCount { 0 };
    std::atomic<bool> wanted { false }, prepared { false }, reachedEnd { false }, looping { false };
    std::atomic<juce::int64> readPos { 0 };     // host-rate linear samples, written by addTo and seekSource
    std::atomic<juce::int64> origin { 0 }, loopStart { 0 }, loopEnd { 0 };   // song positions (source samples)
    std::atomic<juce::int64> base { 0 };        // the linear read position the last seek jumped to
    std::atomic<float> peak { 0.0f };
    bool sounding = false;                      // audio thread only: last block was audible (for the fade-out block)
    juce::int64 lengthSamples = 0;              // source samples
    double lengthSec = 0.0, sourceRate = 44100.0, hostRate = 44100.0;
    juce::Range<double> loop;                   // message thread
};
