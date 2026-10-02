#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <vector>

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
    void setVolume (int stem, float level);     // 0..1 (clamped), heard within one block; load() resets every stem to 1
    float getVolume (int stem) const;           // 1 for a stem out of range
    std::vector<float> mixGains() const;        // per loaded stem: its volume, or 0 while muted (what a mix renders)

    void setLoop (double startFraction, double endFraction);   // sets the range and switches looping on
    void setLooping (bool);                     // keeps the range; turning it on jumps into the loop if outside it
    bool isLooping() const { return looping.load(); }
    juce::Range<double> getLoop() const { return loop; }        // fractions; empty until a loop was set

    void addTo (juce::AudioBuffer<float>& buffer);   // audio thread; idle = two relaxed atomic loads, then return
    float takePeak() { return peak.exchange (0.0f); }  // highest mix level since the last call

    // Playback speed 0.5..1.5 (clamped) that keeps the key. At exactly 1 the stems play directly; otherwise every block
    // goes through a time-stretcher, which stays engaged until the next play() or seek. load() resets it to 1.
    void setSpeed (double);
    double getSpeed() const { return speed.load(); }

    // Every stem with a gain above 0, scaled by it, summed over [startSec, endSec) into a 24-bit stereo WAV at the stems'
    // rate. Always writes, replacing dest; callers cache by name (the editor's renderFile does). onProgress gets 0..1 after
    // each chunk; returning false cancels. False, with no dest.part left, if no gain is above 0, the range
    // is empty, a file is unreadable, a write fails, dest cannot be replaced (open in another app) or it was cancelled. A speed other than 1 (0.5..1.5) time-stretches it, keeping the key, to (endSec - startSec) / speed long.
    static bool render (const juce::Array<juce::File>& stems, const std::vector<float>& gains, double startSec, double endSec,
                        double speed, const juce::File& dest, const std::function<bool (float)>& onProgress = {});

    // A render's cached file name: "<song> - <stem>[ <volume %>]+..."; " x<speed>" when not 1; " (m.ss.cc-m.ss.cc)" for a
    // loop (empty loopSeconds = the whole song). Empty when no gain is above 0.
    static juce::String renderName (const juce::String& songFile, const juce::StringArray& stemNames,
                                    const std::vector<float>& gains, double speed, juce::Range<double> loopSeconds);

private:
    static constexpr int readAheadSamples = 32768;
    class Stack;
    struct Stretch;                             // the time-stretcher (StemPlayer.cpp), configured in prepare()
    juce::int64 songPosition (juce::int64 linear) const;   // read-ahead position -> song position (loop-wrapped)
    bool wraps() const;                                     // looping, and playback started before the loop end
    juce::int64 linearPosition() const;                     // the audio thread's position, in source samples
    void seekSource (juce::int64 songSample, bool loopOn);  // jumps the read-ahead to a fresh range, then publishes the state

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "LISN player read-ahead" };
    std::unique_ptr<Stack> stack;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> scratch, mix, mixIn;   // 2 * maxStems channels for 1.5x a block / stereo, a block / stereo, 1.5x
    std::unique_ptr<Stretch> stretch;
    std::atomic<double> speed { 1.0 };
    std::atomic<bool> restretch { false };          // play() and seeks: the next block decides the path again
    bool stretching = false;                        // audio thread: blocks go through the stretcher
    double carry = 0.0;                             // audio thread: the part of an input sample owed to the next block
    float outGain = 1.0f;                           // audio thread: the stretched output's level at the end of the last block
    juce::Array<juce::File> files;
    std::array<std::atomic<bool>, maxStems> muted {};
    std::array<std::atomic<float>, maxStems> volumes;   // 0..1 per stem; 1 after construction and load()
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
