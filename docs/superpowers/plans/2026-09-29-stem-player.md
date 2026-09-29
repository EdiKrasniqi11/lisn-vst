# Stem Player Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the one-stem-at-a-time preview into an FL-style stem player: one shared playhead, mute/solo lights, a beat-snapped loop and "Drag mix / Drag loop" renders, plus a no-rescan FL dev setup.

**Architecture:**
- **`StemPlayer`** replaces `StemPreview`. A private `Stack` source reads every stem at the same position (2 channels per stem) and wraps inside the loop. It sits behind one `AudioTransportSource`, which gives the read-ahead and the sample-rate conversion. Mutes are gain ramps applied *after* the read-ahead buffer.
- **`detectTempo`** in `Peaks.cpp` finds the BPM from the drums stem inside the existing background peak job.
- **The UI changes** live in `StemRow`, `StemsScreen` and `StemSplitterEditor`.

**Tech Stack:** JUCE 8.0.4, CMake, MSVC 2022, C++17. Tests are `juce::UnitTest` suites in `lisn_tests` (run through ctest).

**Spec:** `docs/superpowers/specs/2026-09-29-stem-player-design.md`. **Mockup:** `docs/design/StemPlayer.mockup.html`; where the text and the mockup disagree, the mockup wins.

## Global Constraints

- **Redesign spec's "Must hold" list** (`2026-09-27-stemsplitter-redesign-design.md`):
  - no `ComboBox`, `TextEditor`, `PopupMenu` or `ProgressBar`;
  - nothing takes keyboard focus: `setWantsKeyboardFocus (false)`, and Buttons also get `setMouseClickGrabsKeyboardFocus (false)`;
  - an idle `processBlock` returns straight away with no allocation, lock or extra work;
  - a motion frame averages ≤ 4 ms in the Release bench.
- **Player rules:** never call `transport.stop()`; play/pause is an atomic flag; the position is read from an atomic.
- **Non-ASCII UI text** is escaped UTF-8: `juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))` for "–". The shared `dot` string already lives in `Screens.cpp`.
- **Builds:** run from the repo root with CMake from `"C:\Program Files\CMake\bin"` (prepend it to PATH):
  - build: `cmake --build build --config Debug`;
  - tests: `ctest --test-dir build -C Debug --output-on-failure`;
  - single suite: `build\lisn_tests_artefacts\Debug\lisn_tests.exe`, which prints every suite;
  - pluginval: `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\LISN StemSplitter.vst3"`.
- **Commits** end with a blank line and then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Don't push.
- **Deliberate corner-cuts** get a `ponytail:` comment naming the ceiling and the upgrade path.

## File map

```
Source/StemPlayer.h/.cpp     NEW  lockstep multi-stem playback, mutes/solo, loop wrap, offline render
Source/StemPreview.h/.cpp    DELETED in Task 5 (StemPlayer covers it)
Source/Peaks.h/.cpp          + Tempo detectTempo (reader)
Source/Icons.h               + speaker, speakerOff, loop
Source/Controls.h/.cpp       + MuteLight, DragChip, drawDragChip, LisnButton::tint
Source/StemRow.h/.cpp        mute light instead of play button, shared playhead, no per-row time, loop drag
Source/Screens.h/.cpp        StemsScreen: Play/Pause in the song row, shared playhead + loop band, Loop button, Drag mix chip, BPM
Source/PluginProcessor.h/.cpp  StemPreview preview  ->  StemPlayer player
Source/PluginEditor.h/.cpp   wiring: play/pause, mutes, seek, loop, renders
tests/StemPlayerTests.cpp    NEW (replaces tests/StemPreviewTests.cpp)
tests/PeaksTests.cpp         + tempo tests
tests/StemRowTests.cpp, tests/UiStateTests.cpp, tests/Snapshots.cpp   updated
```

---

### Task 1: No-copy, no-rescan FL setup (one-time, elevated)

**Files:** none in the repo. Updates the memory file `C:\Users\edikr\.claude\projects\C--Projects-lisn-vst\memory\fl-studio-plugin-install.md`.

- [ ] **Step 1: FL must be closed.** Run `Get-Process | Where-Object { $_.ProcessName -match '^FL' }` in PowerShell. It must print nothing; otherwise ask the user to close FL.
- [ ] **Step 2: A Release build must exist.** `Test-Path "C:\Projects\lisn-vst\build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3\Contents\x86_64-win\LISN StemSplitter.vst3"` must print `True`. If not, run `cmake --build build --config Release` first.
- [ ] **Step 3: Replace the copied bundle with a junction.** This triggers one UAC prompt for the user; tell them before running it.

```powershell
$dst = 'C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3'
$src = 'C:\Projects\lisn-vst\build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3'
$cmd = "rmdir /s /q `"$dst`" & mklink /J `"$dst`" `"$src`""
$p = Start-Process cmd -ArgumentList "/c $cmd" -Verb RunAs -Wait -PassThru -WindowStyle Hidden
"exit=$($p.ExitCode)"
```

- [ ] **Step 4: Verify.**

```powershell
$dst = 'C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3'
(Get-Item $dst).LinkType   # expected: Junction
(Get-Item $dst).Target     # expected: ...\build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3
& 'C:\tools\pluginval\pluginval.exe' --strictness-level 5 --validate $dst | Select-Object -Last 1   # expected: SUCCESS
```

- [ ] **Step 5: Update the memory file.** Replace its "How to apply" paragraph with:

```markdown
**How to apply:** Since 2026-09-29, `C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3` is a directory junction to `build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3`. Workflow: user closes FL, build Release, user reopens FL; no copy, no UAC, no rescan (FL's saved path never changes). Debug builds don't reach FL. To remove the junction use `rmdir` WITHOUT `/s` (with `/s` it deletes the build output). A rename of the plugin/bundle still needs an FL rescan. Related: [[mockups-before-ui-builds]].
```

No commit: nothing in the repo changed.

---

### Task 2: `StemPlayer` engine (lockstep, mutes, solo, loop)

**Files:**
- Create: `Source/StemPlayer.h`, `Source/StemPlayer.cpp`, `tests/StemPlayerTests.cpp`
- Modify: `CMakeLists.txt`: add `Source/StemPlayer.cpp` inside `set(LISN_SOURCES …)` and `tests/StemPlayerTests.cpp` inside `target_sources(lisn_tests PRIVATE …)`

**Interfaces:**
- Consumes: nothing new. `StemPreview` stays untouched until Task 5.
- Produces (used by Tasks 3, 5 and 6), the `StemPlayer` public API exactly as declared in Step 1: `load (const juce::Array<juce::File>&)`, `unload()`, `getFiles()`, `play()`, `pause()`, `isPlaying()`, `getLengthSeconds()`, `getPositionFraction()`, `setPositionFraction (double)`, `takeReachedEnd()`, `setMuted (int, bool)`, `isMuted (int)`, `solo (int)`, `audibleMask()`, `setLoop (double, double)`, `setLooping (bool)`, `isLooping()`, `getLoop()`, `addTo (juce::AudioBuffer<float>&)`, `takePeak()`, `prepare (double, int)`, `release()`.

- [ ] **Step 1: Write `Source/StemPlayer.h`.**

```cpp
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

private:
    class Stack;
    juce::int64 songPosition (juce::int64 linear) const;   // read-ahead position -> song position (loop-wrapped)
    bool wraps() const;                                     // looping, and playback started before the loop end
    juce::int64 linearPosition() const;                     // the audio thread's position, in source samples
    void seekSource (juce::int64 sourceSample);             // sets the wrap origin, then moves the transport

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
    std::atomic<juce::int64> origin { 0 }, loopStart { 0 }, loopEnd { 0 };   // source samples
    std::atomic<float> peak { 0.0f };
    bool sounding = false;                      // audio thread only: last block was audible (for the fade-out block)
    juce::int64 lengthSamples = 0;              // source samples
    double lengthSec = 0.0, sourceRate = 44100.0, hostRate = 44100.0;
    juce::Range<double> loop;                   // message thread
};
```

- [ ] **Step 2: Write the failing tests `tests/StemPlayerTests.cpp`.**

```cpp
#include "../Source/StemPlayer.h"
#include <thread>

struct StemPlayerTests : juce::UnitTest
{
    StemPlayerTests() : juce::UnitTest ("StemPlayer") {}

    // A 44.1 kHz, 24-bit stereo WAV of `seconds`, sample value fn (channel, index).
    static void writeWav (const juce::File& f, double seconds, const std::function<float (int, int)>& fn)
    {
        const int n = (int) (seconds * 44100.0);
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, fn (ch, i));
        f.deleteFile();
        auto out = f.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), 44100.0, 2, 24, {}, 0));
        if (w == nullptr) return;
        out.release();   // the writer owns the stream now
        w->writeFromAudioSampleBuffer (b, 0, n);
    }

    static float rms (const juce::AudioBuffer<float>& b, int start, int n, float offset = 0.0f)
    {
        double sum = 0;
        for (int i = start; i < start + n; ++i) sum += juce::square (b.getSample (0, i) - offset);
        return (float) std::sqrt (sum / n);
    }

    static bool allEqual (const juce::AudioBuffer<float>& b, float v)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! juce::exactlyEqual (b.getSample (ch, i), v)) return false;
        return true;
    }

    template <typename Fn> static double msTaken (Fn&& fn)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        fn();
        return juce::Time::getMillisecondCounterHiRes() - t0;
    }

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-player", {});
        expect (dir.createDirectory().wasOk());
        const auto sine = dir.getChildFile ("sine.wav");                 // 2 s, 440 Hz at 0.5 on both channels
        writeWav (sine, 2.0, [] (int, int i) { return (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 44100.0)); });
        const auto left = dir.getChildFile ("left.wav"), right = dir.getChildFile ("right.wav");   // one impulse at sample 10000
        writeWav (left,  1.0, [] (int ch, int i) { return ch == 0 && i == 10000 ? 1.0f : 0.0f; });   // ~10884 at 48 kHz: inside
        writeWav (right, 1.0, [] (int ch, int i) { return ch == 1 && i == 10000 ? 1.0f : 0.0f; });   // the 40 recorded blocks
        const auto steps = dir.getChildFile ("steps.wav");               // 1 s: 0.25 before 0.5 s, 0.75 after
        writeWav (steps, 1.0, [] (int, int i) { return i < 22050 ? 0.25f : 0.75f; });

        StemPlayer p;
        juce::AudioBuffer<float> buf (2, 512);
        auto pull = [&] (float fill)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), fill, buf.getNumSamples());
            p.addTo (buf);
        };

        beginTest ("load");
        p.prepare (48000, 512);
        expect (p.load ({ sine }));
        juce::Thread::sleep (30);
        expectEquals (p.getFiles().size(), 1);
        expectWithinAbsoluteError (p.getLengthSeconds(), 2.0, 0.01);
        expectEquals ((int) p.audibleMask(), 1);

        beginTest ("pass-through while not playing");
        pull (0.25f);
        expect (allEqual (buf, 0.25f));

        beginTest ("playing");
        p.play();
        expect (p.isPlaying());
        for (int i = 0; i < 20; ++i) pull (0.0f);
        expectGreaterThan (rms (buf, 0, 512), 0.2f);
        const float peak = p.takePeak();
        expect (peak > 0.45f && peak < 0.55f, "peak " + juce::String (peak));
        expectEquals (p.takePeak(), 0.0f);
        expectWithinAbsoluteError (p.getPositionFraction() * p.getLengthSeconds(), 20 * 512 / 48000.0, 0.01);

        beginTest ("mute is heard within one block");
        p.setMuted (0, true);
        expect (p.isMuted (0));
        expectEquals ((int) p.audibleMask(), 0);
        pull (0.25f);                                        // the ramp-down block
        const float head = rms (buf, 0, 128, 0.25f), tail = rms (buf, 384, 128, 0.25f);
        expect (head > 0.2f && tail < head / 2, "ramp " + juce::String (head) + " -> " + juce::String (tail));
        pull (0.25f);
        expect (allEqual (buf, 0.25f));
        p.setMuted (0, false);
        pull (0.0f);
        pull (0.0f);
        expectGreaterThan (rms (buf, 0, 512), 0.2f);

        beginTest ("pause");
        expectLessThan (msTaken ([&] { p.pause(); }), 20.0);
        pull (0.25f);                                        // the fade-out block
        pull (0.25f);
        expect (allEqual (buf, 0.25f));
        const double paused = p.getPositionFraction();
        for (int i = 0; i < 5; ++i) pull (0.25f);
        expectEquals (p.getPositionFraction(), paused);

        beginTest ("solo");
        expect (p.load ({ sine, sine, sine, sine }));
        p.solo (1);
        expectEquals ((int) p.audibleMask(), 0b0010);
        p.solo (1);                                          // the only audible one: everything back on
        expectEquals ((int) p.audibleMask(), 0b1111);
        p.setMuted (2, true);
        p.solo (0);
        expectEquals ((int) p.audibleMask(), 0b0001);
        p.solo (0);
        expectEquals ((int) p.audibleMask(), 0b1111);

        beginTest ("stems stay sample-aligned after resampling");
        expect (p.load ({ left, right }));
        juce::Thread::sleep (30);
        p.play();
        juce::AudioBuffer<float> recorded (2, 40 * 512);
        for (int block = 0; block < 40; ++block)
        {
            pull (0.0f);
            for (int ch = 0; ch < 2; ++ch)
                recorded.copyFrom (ch, block * 512, buf, ch, 0, 512);
            juce::Thread::sleep (1);
        }
        auto argmax = [&] (int ch)
        {
            int best = 0;
            for (int i = 1; i < recorded.getNumSamples(); ++i)
                if (std::abs (recorded.getSample (ch, i)) > std::abs (recorded.getSample (ch, best))) best = i;
            return best;
        };
        expectGreaterThan (std::abs (recorded.getSample (0, argmax (0))), 0.1f);
        expectEquals (argmax (0), argmax (1));
        p.pause();
        pull (0.0f);

        beginTest ("loop wraps exactly and never ends");
        p.prepare (44100, 512);                              // same rate as the file, so sample positions compare directly
        expect (p.load ({ steps }));
        p.setLoop (0.4, 0.6);                                // 17640 .. 26460; jumps into the loop
        expect (p.isLooping());
        expectWithinAbsoluteError (p.getPositionFraction(), 0.4, 0.001);
        juce::Thread::sleep (30);
        p.play();
        std::vector<float> out;
        for (int block = 0; block < 60; ++block)             // 30720 samples: about 3.5 loop passes
        {
            pull (0.0f);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
            juce::Thread::sleep (1);
        }
        std::vector<int> falls;                              // where 0.75 drops back to 0.25: the wrap
        for (size_t i = 1000; i < out.size(); ++i)
            if (out[i - 1] > 0.5f && out[i] < 0.5f) falls.push_back ((int) i);
        expect (falls.size() >= 3, "wraps: " + juce::String ((int) falls.size()));
        if (falls.size() >= 2)
        {
            expectWithinAbsoluteError (falls[0], 2205 + 4410, 8);   // 0.1 s at 0.25, then 0.1 s at 0.75
            expectWithinAbsoluteError (falls[1] - falls[0], 8820, 2);
        }
        expect (p.isPlaying());
        expect (! p.takeReachedEnd());
        p.setLooping (false);
        expect (! p.isLooping());
        expectEquals (p.getLoop().getStart(), 0.4);        // the range is kept
        p.pause();
        pull (0.0f);

        beginTest ("end of the song");
        p.prepare (48000, 512);
        expect (p.load ({ sine }));
        p.setPositionFraction (0.98);
        juce::Thread::sleep (50);
        p.play();
        for (int i = 0; i < 400 && p.isPlaying(); ++i) { pull (0.0f); juce::Thread::sleep (1); }
        expect (! p.isPlaying());
        expect (p.takeReachedEnd());
        expect (! p.takeReachedEnd());
        p.play();                                            // after the end, play restarts from 0
        expectEquals (p.getPositionFraction(), 0.0);
        p.pause();
        pull (0.0f);

        beginTest ("bad files and too many stems");
        const auto txt = dir.getChildFile ("notes.txt");
        expect (txt.replaceWithText ("not audio"));
        expect (! p.load ({ sine, dir.getChildFile ("missing.wav") }));
        expect (! p.load ({ txt }));
        expect (! p.load ({}));
        expectEquals (p.getFiles().size(), 0);
        p.play();
        expect (! p.isPlaying());
        juce::Array<juce::File> nine;
        for (int i = 0; i < 9; ++i) nine.add (sine);
        expect (p.load (nine));
        expectEquals (p.getFiles().size(), StemPlayer::maxStems);

        beginTest ("no stall with a live audio thread");
        expect (p.load ({ sine, sine, sine, sine }));
        juce::Thread::sleep (30);
        std::atomic<bool> running { true };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> b (2, 512);
            while (running)
            {
                b.clear();
                p.addTo (b);
                juce::Thread::sleep (5);
            }
        });
        p.play();
        juce::Thread::sleep (50);
        expectLessThan (msTaken ([&] { p.pause(); }), 20.0, "pause");
        expectLessThan (msTaken ([&] { p.play(); }), 20.0, "play");
        expectLessThan (msTaken ([&] { p.setMuted (1, true); }), 20.0, "mute");
        expectLessThan (msTaken ([&] { p.setLoop (0.2, 0.4); }), 20.0, "loop");
        expectLessThan (msTaken ([&] { expect (p.load ({ sine, sine })); }), 100.0, "load");
        expectLessThan (msTaken ([&] { p.unload(); }), 20.0, "unload");
        running = false;
        audio.join();

        beginTest ("oversized block");
        p.prepare (48000, 256);
        expect (p.load ({ sine }));
        juce::Thread::sleep (30);
        p.play();
        juce::AudioBuffer<float> big (2, 1024);
        big.clear();
        p.addTo (big);
        expectGreaterThan (rms (big, 768, 256), 0.2f);

        p.unload();   // releases the files so the temp dir can go
        dir.deleteRecursively();
    }
};

static StemPlayerTests stemPlayerTests;
```

- [ ] **Step 3: Add both files to `CMakeLists.txt` and build to see it fail.** Put `Source/StemPlayer.cpp` in `LISN_SOURCES` and `tests/StemPlayerTests.cpp` in the `lisn_tests` sources. Create an empty `Source/StemPlayer.cpp` containing only `#include "StemPlayer.h"`.
Run: `cmake --build build --config Debug --target lisn_tests`
Expected: link errors (unresolved `StemPlayer::StemPlayer`, `StemPlayer::load`, …).

- [ ] **Step 4: Implement `Source/StemPlayer.cpp`.**

```cpp
#include "StemPlayer.h"

// ponytail: while playing, the read-ahead thread can briefly wait on a disk read and the audio thread on the buffer's lock;
// idle blocks take no lock. Upgrade path: decode the stems into memory on load and play them lock-free.

namespace
{
    // Stereo from any reader: mono is copied to both sides; beyond the file's end the reader returns zeros.
    void readStereo (juce::AudioFormatReader& r, float* l, float* rgt, juce::int64 pos, int n)
    {
        float* dest[] = { l, rgt };
        r.read (dest, juce::jmin (2, (int) r.numChannels), pos, n);
        if (r.numChannels == 1)
            juce::FloatVectorOperations::copy (rgt, l, n);
    }
}

// Every stem read at the same position into channels 2i / 2i+1. The read-ahead buffer counts positions linearly, so this
// source reports linear positions and maps them into the song itself (songPosition), wrapping inside the loop.
class StemPlayer::Stack : public juce::PositionableAudioSource
{
public:
    Stack (const StemPlayer& p, juce::OwnedArray<juce::AudioFormatReader>& r) : owner (p)
    {
        readers.swapWith (r);
        for (auto* reader : readers)
            length = juce::jmax (length, reader->lengthInSamples);
    }

    juce::int64 songLength() const { return length; }
    int numStems() const { return readers.size(); }

    void prepareToPlay (int, double) override {}
    void releaseResources() override {}
    void setNextReadPosition (juce::int64 p) override { pos = p; }
    juce::int64 getNextReadPosition() const override { return pos; }
    // While the loop can wrap, the stream must not end: the read-ahead buffer and the transport stop at this length.
    juce::int64 getTotalLength() const override { return owner.wraps() ? std::numeric_limits<juce::int64>::max() / 4 : length; }
    bool isLooping() const override { return false; }   // true would make the buffer wrap at the song length itself

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();
        for (int done = 0; done < info.numSamples;)
        {
            const auto linear = pos.load();
            const auto song = owner.songPosition (linear);
            const auto limit = owner.wraps() ? owner.loopEnd.load() : length;   // read up to the wrap or the song end
            const int n = (int) juce::jmin ((juce::int64) (info.numSamples - done), juce::jmax ((juce::int64) 0, limit - song));
            if (n == 0)
                break;                                   // past the end of the song: the rest stays silent
            for (int i = 0; i < readers.size() && 2 * i + 1 < info.buffer->getNumChannels(); ++i)
                readStereo (*readers[i], info.buffer->getWritePointer (2 * i, info.startSample + done),
                            info.buffer->getWritePointer (2 * i + 1, info.startSample + done), song, n);
            pos = linear + n;
            done += n;
        }
    }

private:
    const StemPlayer& owner;
    juce::OwnedArray<juce::AudioFormatReader> readers;
    juce::int64 length = 0;
    std::atomic<juce::int64> pos { 0 };
};

StemPlayer::StemPlayer()
{
    formats.registerBasicFormats();
    readAhead.startThread();
}

StemPlayer::~StemPlayer()
{
    wanted = false;
    transport.setSource (nullptr);
    readAhead.stopThread (2000);
}

void StemPlayer::prepare (double sampleRate, int maxBlockSize)
{
    prepared = false;
    scratch.setSize (2 * maxStems, juce::jmax (1, maxBlockSize));   // a 0-sample scratch would make addTo's chunk loop spin
    mix.setSize (2, juce::jmax (1, maxBlockSize));
    hostRate = sampleRate;
    transport.prepareToPlay (maxBlockSize, sampleRate);
    transport.prepareToPlay (maxBlockSize, sampleRate);   // again: sizes the resampler for the ratio the first call set
    prepared = true;
}

void StemPlayer::release()
{
    prepared = false;
    transport.releaseResources();
}

bool StemPlayer::load (const juce::Array<juce::File>& stems)
{
    unload();
    juce::OwnedArray<juce::AudioFormatReader> readers;
    juce::Array<juce::File> loaded;
    for (const auto& f : stems)
    {
        if (readers.size() == maxStems)
            break;                                   // ponytail: stems past 8 are ignored; demucs makes 4 or 6
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || (! readers.isEmpty() && ! juce::exactlyEqual (r->sampleRate, readers[0]->sampleRate)))
            return false;
        readers.add (r.release());
        loaded.add (f);
    }
    if (readers.isEmpty())
        return false;

    sourceRate = readers[0]->sampleRate;
    const int count = readers.size();
    stack = std::make_unique<Stack> (*this, readers);
    lengthSamples = stack->songLength();
    lengthSec = (double) lengthSamples / sourceRate;
    transport.setSource (stack.get(), 32768, &readAhead, sourceRate, 2 * count);
    files = loaded;
    stemCount = count;
    seekSource (0);
    transport.start();   // only sets flags under the lock; audio is gated by `wanted`
    return true;
}

void StemPlayer::unload()
{
    wanted = false;
    stemCount = 0;
    transport.setSource (nullptr);
    stack.reset();
    files.clear();
    for (auto& m : muted) m = false;
    looping = false;
    loop = {};
    loopStart = loopEnd = origin = 0;
    lengthSamples = 0;
    lengthSec = 0;
    readPos = 0;
    reachedEnd = false;   // an end not yet taken belonged to the previous song
}

void StemPlayer::play()
{
    if (stack == nullptr) return;
    if (! transport.isPlaying())   // the transport stops itself only at the end; a seek made after that is kept
    {
        if (transport.hasStreamFinished()) seekSource (0);
        transport.start();
    }
    wanted = true;
}

void StemPlayer::pause()
{
    wanted = false;
}

juce::int64 StemPlayer::songPosition (juce::int64 linear) const
{
    const auto s = loopStart.load(), e = loopEnd.load();
    if (! looping.load() || s >= e || origin.load() >= e || linear < e)
        return linear;
    return s + (linear - e) % (e - s);
}

bool StemPlayer::wraps() const
{
    return looping.load() && loopStart.load() < loopEnd.load() && origin.load() < loopEnd.load();
}

juce::int64 StemPlayer::linearPosition() const
{
    return hostRate > 0 ? (juce::int64) ((double) readPos.load() * sourceRate / hostRate) : 0;
}

double StemPlayer::getPositionFraction() const
{
    return lengthSamples > 0 ? juce::jlimit (0.0, 1.0, (double) songPosition (linearPosition()) / (double) lengthSamples) : 0.0;
}

void StemPlayer::seekSource (juce::int64 sourceSample)
{
    origin = sourceSample;
    transport.setPosition ((double) sourceSample / sourceRate);   // also flushes the read-ahead
    readPos = (juce::int64) ((double) sourceSample * hostRate / sourceRate);
}

void StemPlayer::setPositionFraction (double f)
{
    seekSource ((juce::int64) (juce::jlimit (0.0, 1.0, f) * (double) lengthSamples));
}

void StemPlayer::setMuted (int stem, bool m)
{
    if (juce::isPositiveAndBelow (stem, maxStems))
        muted[(size_t) stem] = m;
}

bool StemPlayer::isMuted (int stem) const
{
    return juce::isPositiveAndBelow (stem, maxStems) && muted[(size_t) stem].load();
}

juce::uint32 StemPlayer::audibleMask() const
{
    juce::uint32 mask = 0;
    for (int i = 0; i < files.size(); ++i)
        if (! muted[(size_t) i].load())
            mask |= 1u << i;
    return mask;
}

void StemPlayer::solo (int stem)
{
    if (! juce::isPositiveAndBelow (stem, files.size())) return;
    const bool alone = audibleMask() == (1u << stem);
    for (int i = 0; i < files.size(); ++i)
        muted[(size_t) i] = ! alone && i != stem;
}

void StemPlayer::setLoop (double a, double b)
{
    loop = { juce::jlimit (0.0, 1.0, juce::jmin (a, b)), juce::jlimit (0.0, 1.0, juce::jmax (a, b)) };
    loopStart = (juce::int64) (loop.getStart() * (double) lengthSamples);
    loopEnd = (juce::int64) (loop.getEnd() * (double) lengthSamples);
    setLooping (true);
}

void StemPlayer::setLooping (bool on)
{
    const auto song = songPosition (linearPosition());
    looping = on && ! loop.isEmpty();
    // Turning a loop on jumps into it; every change re-seeks, which drops read-ahead audio from the old setting.
    seekSource (looping.load() && (song < loopStart.load() || song >= loopEnd.load()) ? loopStart.load() : song);
}

void StemPlayer::addTo (juce::AudioBuffer<float>& buffer)
{
    const bool want = wanted.load (std::memory_order_relaxed);
    if ((! want && ! sounding) || ! prepared.load (std::memory_order_relaxed)) return;   // idle: nothing else runs
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples(), stems = stemCount.load (std::memory_order_relaxed);
    float blockPeak = 0.0f;
    for (int start = 0; start < total; start += scratch.getNumSamples())               // chunks if the host exceeds its block size
    {
        const int n = juce::jmin (scratch.getNumSamples(), total - start);
        juce::AudioSourceChannelInfo info (&scratch, 0, n);
        transport.getNextAudioBlock (info);
        mix.clear (0, n);
        for (int i = 0; i < stems; ++i)
        {
            // A mute (or the one faded block after pause()) ramps across this block: heard at once, never a click.
            const float to = want && ! muted[(size_t) i].load (std::memory_order_relaxed) ? 1.0f : 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                mix.addFromWithRamp (ch, 0, scratch.getReadPointer (2 * i + ch), n, gains[(size_t) i], to);
            gains[(size_t) i] = to;
        }
        blockPeak = juce::jmax (blockPeak, mix.getMagnitude (0, n));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, start, mix, juce::jmin (ch, 1), 0, n);
        if (! want) break;
    }
    sounding = want && transport.isPlaying();
    if (want && ! transport.isPlaying() && wanted.exchange (false)) reachedEnd = true;    // played to the end
    readPos.store (transport.getNextReadPosition(), std::memory_order_relaxed);
    if (blockPeak > peak.load (std::memory_order_relaxed)) peak.store (blockPeak, std::memory_order_relaxed);   // benign race with takePeak
}
```

- [ ] **Step 5: Run the tests.**
Run: `cmake --build build --config Debug --target lisn_tests`, then `build\lisn_tests_artefacts\Debug\lisn_tests.exe`
Expected: every `StemPlayer / …` test completes with no failures, and the other suites still pass.

- [ ] **Step 6: Commit.**

```bash
git add Source/StemPlayer.h Source/StemPlayer.cpp tests/StemPlayerTests.cpp CMakeLists.txt
git commit -m "feat: StemPlayer plays all stems in lockstep with mutes, solo and a wrapping loop

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Offline render (Drag mix / Drag loop files)

**Files:**
- Modify: `Source/StemPlayer.h`, `Source/StemPlayer.cpp`, `tests/StemPlayerTests.cpp`

**Interfaces:**
- Consumes: `readStereo` in the anonymous namespace of `StemPlayer.cpp` (Task 2).
- Produces: `static bool StemPlayer::render (const juce::Array<juce::File>& stems, juce::uint32 audibleMask, double startSec, double endSec, const juce::File& dest)`, used by the editor in Task 6.

- [ ] **Step 1: Declare it** in the `public:` section of `StemPlayer.h`, after `takePeak`:

```cpp
    // The stems in audibleMask summed over [startSec, endSec) into a 24-bit stereo WAV at the stems' rate. Reuses dest if it
    // already exists (renders are cached by name). False if nothing is audible, the range is empty or a file is unreadable.
    static bool render (const juce::Array<juce::File>& stems, juce::uint32 audibleMask, double startSec, double endSec,
                        const juce::File& dest);
```

- [ ] **Step 2: Write the failing test.** Add it at the end of `StemPlayerTests::runTest()`, before `p.unload();`:

```cpp
        beginTest ("render");
        const auto a = dir.getChildFile ("a.wav"), b = dir.getChildFile ("b.wav");
        writeWav (a, 1.0, [] (int, int i) { return (float) (0.3 * std::sin (0.04 * i)); });
        writeWav (b, 1.0, [] (int, int i) { return (float) (0.2 * std::sin (0.07 * i)); });
        const auto both = dir.getChildFile ("renders").getChildFile ("both.wav");
        expect (StemPlayer::render ({ a, b }, 0b11, 0.25, 0.75, both));
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (both));
            expect (r != nullptr);
            if (r != nullptr)
            {
                expectWithinAbsoluteError ((int) r->lengthInSamples, 22050, 1);
                juce::AudioBuffer<float> got (2, 22050);
                r->read (&got, 0, 22050, 0, true, true);
                float worst = 0.0f;
                for (int i = 0; i < 22050; ++i)
                {
                    const int s = 11025 + i;
                    const float want = (float) (0.3 * std::sin (0.04 * s) + 0.2 * std::sin (0.07 * s));
                    worst = juce::jmax (worst, std::abs (got.getSample (0, i) - want), std::abs (got.getSample (1, i) - want));
                }
                expectLessThan (worst, 1e-4f);
            }
        }
        const auto modified = both.getLastModificationTime();
        juce::Thread::sleep (20);
        expect (StemPlayer::render ({ a, b }, 0b11, 0.25, 0.75, both));   // cached by name: not rewritten
        expect (both.getLastModificationTime() == modified);
        expect (! StemPlayer::render ({ a, b }, 0, 0.0, 1.0, dir.getChildFile ("none.wav")));
        expect (! StemPlayer::render ({ a, b }, 0b01, 0.5, 0.5, dir.getChildFile ("empty.wav")));
        expect (! dir.getChildFile ("none.wav").exists() && ! dir.getChildFile ("empty.wav").exists());
```

- [ ] **Step 3: Build to see it fail.**
Run: `cmake --build build --config Debug --target lisn_tests`
Expected: an unresolved external symbol for `StemPlayer::render`.

- [ ] **Step 4: Implement it** at the end of `StemPlayer.cpp`:

```cpp
bool StemPlayer::render (const juce::Array<juce::File>& stems, juce::uint32 audibleMask, double startSec, double endSec,
                         const juce::File& dest)
{
    if (dest.existsAsFile()) return true;            // renders are cached by name (FL keeps pointing at them)
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::OwnedArray<juce::AudioFormatReader> readers;
    for (int i = 0; i < stems.size() && i < 32; ++i)
        if ((audibleMask >> i) & 1u)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (stems[i]));
            if (r == nullptr) return false;
            readers.add (r.release());
        }
    if (readers.isEmpty()) return false;

    const double rate = readers[0]->sampleRate;
    const auto first = (juce::int64) (startSec * rate), last = (juce::int64) (endSec * rate);
    if (last <= first) return false;

    // Written next to dest, then renamed: a failed render never leaves a half file that FL could reference.
    dest.getParentDirectory().createDirectory();
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
    part.deleteFile();
    {
        auto out = part.createOutputStream();
        if (out == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), rate, 2, 24, {}, 0));
        if (w == nullptr) return false;
        out.release();   // the writer owns the stream now
        constexpr int chunk = 65536;
        juce::AudioBuffer<float> sum (2, chunk), one (2, chunk);
        for (auto pos = first; pos < last; pos += chunk)
        {
            const int n = (int) juce::jmin ((juce::int64) chunk, last - pos);
            sum.clear();
            for (auto* r : readers)
            {
                readStereo (*r, one.getWritePointer (0), one.getWritePointer (1), pos, n);
                for (int ch = 0; ch < 2; ++ch)
                    sum.addFrom (ch, 0, one, ch, 0, n);
            }
            if (! w->writeFromAudioSampleBuffer (sum, 0, n)) return false;
        }
    }
    return part.moveFileTo (dest);
}
```

- [ ] **Step 5: Run the tests.**
Run: `cmake --build build --config Debug --target lisn_tests`, then `build\lisn_tests_artefacts\Debug\lisn_tests.exe`
Expected: `StemPlayer / render` passes, and so does everything else.

- [ ] **Step 6: Commit.**

```bash
git add Source/StemPlayer.h Source/StemPlayer.cpp tests/StemPlayerTests.cpp
git commit -m "feat: render the audible stems over a range to a cached WAV

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `detectTempo` (BPM from the drums stem)

**Files:**
- Modify: `Source/Peaks.h`, `Source/Peaks.cpp`, `tests/PeaksTests.cpp`

**Interfaces:**
- Produces: `struct Tempo { double bpm = 0.0, firstBeat = 0.0; };` and `Tempo detectTempo (juce::AudioFormatReader&)`. Used by `StemsScreen` in Task 6. `bpm == 0` means no steady beat was found.

- [ ] **Step 1: Declare it** at the end of `Source/Peaks.h`:

```cpp
// Tempo of a percussive stem: bpm in 70..180 and the time of the first beat (seconds). bpm 0 = no steady beat found.
// ponytail: assumes a steady tempo and can report half or double tempo. Upgrade path: a x2 / /2 toggle, or tempo tracking.
struct Tempo { double bpm = 0.0, firstBeat = 0.0; };
Tempo detectTempo (juce::AudioFormatReader& reader);
```

- [ ] **Step 2: Write the failing tests.** Add this helper as a static member of `PeaksTests`:

```cpp
    // 10 s mono 44.1 kHz WAV in memory: 60 ms decaying noise bursts every 60/bpm seconds from `offset`, alternately 0.9 and 0.6.
    static std::unique_ptr<juce::AudioFormatReader> clicks (juce::MemoryBlock& wav, double bpm, double offset)
    {
        const int n = 441000;
        juce::AudioBuffer<float> b (1, n);
        b.clear();
        juce::Random noise (1);
        if (bpm > 0)
            for (int k = 0;; ++k)
            {
                const int at = (int) std::round ((offset + k * 60.0 / bpm) * 44100.0);
                if (at >= n) break;
                for (int i = 0; i < 2646 && at + i < n; ++i)
                    b.setSample (0, at + i, (k % 2 == 0 ? 0.9f : 0.6f) * std::exp (-i / 882.0f) * (noise.nextFloat() * 2.0f - 1.0f));
            }
        {
            std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (
                new juce::MemoryOutputStream (wav, false), 44100.0, 1, 24, {}, 0));   // the writer owns the stream
            w->writeFromAudioSampleBuffer (b, 0, n);
        }
        return std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (new juce::MemoryInputStream (wav, false), true));
    }
```

and these tests at the end of `runTest()`:

```cpp
        beginTest ("tempo 120 on the beat");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 120.0, 0.0);
            const auto t = detectTempo (*r);
            expectWithinAbsoluteError (t.bpm, 120.0, 1.0);
            expectWithinAbsoluteError (t.firstBeat, 0.0, 0.02);
        }

        beginTest ("tempo 92 from 0.1 s");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 92.0, 0.1);
            const auto t = detectTempo (*r);
            expectWithinAbsoluteError (t.bpm, 92.0, 1.5);
            expectWithinAbsoluteError (t.firstBeat, 0.1, 0.02);
        }

        beginTest ("no tempo in silence");
        {
            juce::MemoryBlock wav;
            auto r = clicks (wav, 0.0, 0.0);
            expectEquals (detectTempo (*r).bpm, 0.0);
        }
```

- [ ] **Step 3: Build to see it fail.**
Run: `cmake --build build --config Debug --target lisn_tests`
Expected: an unresolved external symbol for `detectTempo`.

- [ ] **Step 4: Implement it** at the end of `Source/Peaks.cpp`:

```cpp
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
```

- [ ] **Step 5: Run the tests.**
Run: `cmake --build build --config Debug --target lisn_tests`, then `build\lisn_tests_artefacts\Debug\lisn_tests.exe`
Expected: `Peaks / tempo …` all pass. If the 92 BPM case misses by more than 1.5 BPM, check the parabolic sign first: with `a > c`, the true peak lies toward `best - 1`, so the correction must be negative.

- [ ] **Step 6: Commit.**

```bash
git add Source/Peaks.h Source/Peaks.cpp tests/PeaksTests.cpp
git commit -m "feat: detect the tempo and first beat of the drums stem

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Wire the player in: Play/Pause, mute lights, shared playhead (retire `StemPreview`)

**Files:**
- Modify: `Source/Icons.h`, `Source/Controls.h/.cpp`, `Source/StemRow.h/.cpp`, `Source/Screens.h/.cpp`, `Source/PluginProcessor.h/.cpp`, `Source/PluginEditor.h/.cpp`, `CMakeLists.txt`, `tests/StemRowTests.cpp`, `tests/UiStateTests.cpp`, `tests/Snapshots.cpp`
- Delete: `Source/StemPreview.h`, `Source/StemPreview.cpp`, `tests/StemPreviewTests.cpp`

**Interfaces:**
- Consumes: the `StemPlayer` API (Task 2).
- Produces for Task 6:
  - `MuteLight`;
  - `StemRow::setPosition (double)`, `setMuted (bool)`, `onToggleMute`, `onSolo`, `onSeek`, `dragFile`;
  - `StemsScreen::getFiles()`, `setPlayback (bool, double, double)`, `setAudible (juce::uint32)`, `onPlayPause`, `onToggleMute (int)`, `onSolo (int)`, `onSeek (double)`, `playheadArea (double)`, `getRepaintCount()`;
  - the processor member `StemPlayer player`.

- [ ] **Step 1: Icons.** Add these to `namespace Icons` in `Source/Icons.h`:

```cpp
    inline constexpr const char* speaker    = "M11 5 6 9H3v6h3l5 4zM15.5 8.5a5 5 0 0 1 0 7M18.5 5.5a9 9 0 0 1 0 13";   // stroked
    inline constexpr const char* speakerOff = "M11 5 6 9H3v6h3l5 4zM16 9.5l5 5M21 9.5l-5 5";                          // stroked
```

- [ ] **Step 2: Write the failing `StemRow` tests.** In `tests/StemRowTests.cpp`:
  - add `Icons::speaker, Icons::speakerOff` to the icon list in the "icons" test;
  - replace every `beginTest` block from "layout" to the end of `runTest()` with:

```cpp
        beginTest ("layout");
        StemRow row (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vocals.wav"), "vocals");
        row.setSize (674, 62);
        expectEquals (row.light().getX(), 142);
        expectEquals (row.light().getWidth(), 38);
        expectEquals (row.waveform().getX(), 194);
        expectEquals (row.waveform().getWidth(), 376);
        expectEquals (row.waveform().getHeight(), 40 + 10);
        expectLessOrEqual (row.chipBounds().getRight(), 664);
        expectEquals (row.chipBounds().getHeight(), 34);
        expectEquals (row.barCount(), 104);

        beginTest ("no keyboard focus");
        expect (! row.getWantsKeyboardFocus() && ! row.waveform().getWantsKeyboardFocus() && ! row.light().getWantsKeyboardFocus());
        expect (! row.light().getMouseClickGrabsKeyboardFocus());
        expect (! pill.getWantsKeyboardFocus());
        LisnButton button ("New song", LisnButton::Style::Ghost);
        expect (! button.getWantsKeyboardFocus() && ! button.getMouseClickGrabsKeyboardFocus());

        beginTest ("compact");
        row.setCompact (true);
        expectEquals (row.getHeight(), 42);
        expectEquals (row.waveform().getHeight(), 26 + 10);
        expectEquals (row.barCount(), 96);
        row.setCompact (false);
        expectEquals (row.getHeight(), 62);

        beginTest ("wrong peak count");
        row.setPeaks ({ 0.5f });
        row.setPeaks (std::vector<float> (500, 2.0f));
        row.setPeaks ({});
        render (row);

        beginTest ("played part");
        row.setPeaks (std::vector<float> (104, 1.0f));
        row.setPosition (0.5);
        const auto wave = render (row.waveform());
        const auto alphaAt = [&] (int x, int y) { return (int) wave.getPixelAt (x, y).getAlpha(); };
        // Bar 10 (x 36.2 .. 38.3) is played; bar 80 (x 289.2 .. 291.4) is dim (stem colour at 0.3). No playhead line here.
        expectGreaterThan (alphaAt (37, 25), 240);
        expectWithinAbsoluteError (alphaAt (290, 25), 77, 4);
        expectEquals (alphaAt (188, 1), 0);

        beginTest ("mute light");
        int toggles = 0, solos = 0;
        row.onToggleMute = [&] { ++toggles; };
        row.onSolo = [&] { ++solos; };
        row.light().clicked (juce::ModifierKeys());
        row.light().clicked (juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));
        expectEquals (toggles, 1);
        expectEquals (solos, 1);
        row.setMuted (true);
        expect (row.light().isMuted());
        expectEquals (row.waveform().getAlpha(), 0.38f);
        row.setMuted (false);
        expectEquals (row.waveform().getAlpha(), 1.0f);
        render (row);
```

- [ ] **Step 3: Add `MuteLight` to `Source/Controls.h`,** after `CircleButton`:

```cpp
// The 38 px mute light of a stem row: a neutral ring with a white speaker, or the speaker with an x when muted.
// Left click toggles, right click solos (FL's channel rack).
class MuteLight : public juce::Button
{
public:
    MuteLight();
    void setMuted (bool);
    bool isMuted() const { return muted; }
    std::function<void()> onToggle, onSolo;
    void clicked (const juce::ModifierKeys&) override;   // public for tests
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    bool muted = false;
};
```

and to `Source/Controls.cpp`:

```cpp
MuteLight::MuteLight() : juce::Button ({})
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MuteLight::setMuted (bool m)
{
    if (m != muted) { muted = m; repaint(); }
}

void MuteLight::clicked (const juce::ModifierKeys& mods)
{
    const auto& callback = mods.isPopupMenu() ? onSolo : onToggle;   // Button::onClick would fire for both buttons
    if (callback != nullptr) callback();
}

void MuteLight::paintButton (juce::Graphics& g, bool, bool)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::cream.withAlpha (0.08f));
    g.fillEllipse (r);
    g.setColour (Theme::cream.withAlpha (0.22f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    g.setColour (muted ? Theme::cream.withAlpha (0.5f) : Theme::cream);
    strokeIcon (g, muted ? Icons::speakerOff : Icons::speaker, r.withSizeKeepingCentre (18.0f, 18.0f), 2.0f);
}
```

- [ ] **Step 4: Rewrite the `StemRow` declaration** in `Source/StemRow.h`. Leave `mmss` and the `WaveformView` class as they are, except that `WaveformView::setFraction`'s comment becomes `// no repaint; the screen repaints the strip that changed`. Replace the `StemRow` class with:

```cpp
// One stem row of the stem player (docs/design/StemPlayer.mockup.html): badge + name, mute light, waveform, drag chip.
// Drag from the row (outside the light and the waveform) to drop dragFile() onto a DAW track.
class StemRow : public juce::Component
{
public:
    StemRow (juce::File wav, juce::String stemKey);  // stemKey = lower-case file name without extension
    void setTheme (const Theme&);
    void setCompact (bool sixStems);                 // rows 42 / waveform 26 when true, 62 / 40 when false (sets the height)
    void setPeaks (std::vector<float> levels);       // bar levels 0..1, padded or cut to barCount()
    void setMuted (bool);                            // dims the name and waveform, swaps the light's icon
    void setPosition (double fraction);              // the shared playhead; no repaint (the screen repaints the strip)
    std::function<void()> onToggleMute, onSolo;
    std::function<void (double)> onSeek;             // fraction 0..0.999
    std::function<juce::File()> dragFile;            // what a drag drops; unset = the stem file
    juce::File getFile() const { return file; }
    juce::String getStemKey() const { return stemKey; }
    int barCount() const { return compact ? 96 : 104; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // test hooks
    MuteLight& light() { return lightButton; }
    WaveformView& waveform() { return wave; }
    juce::Rectangle<int> chipBounds() const;

private:
    juce::File file;
    juce::String stemKey, name;
    juce::Colour colour;
    bool compact = false, muted = false, dragStarted = false;
    std::vector<float> levels;
    MuteLight lightButton;
    WaveformView wave;
};
```

- [ ] **Step 5: Update `Source/StemRow.cpp`.**
  1. Replace the layout constants block with:

```cpp
    // StemPlayer.mockup.html row (CSS px): padding 0 10, gap 14; name block 118, light 38, waveform 376 (the old 330 plus the
    // removed time slot), then the chip.
    constexpr int padX = 10, nameW = 118, gap = 14, lightSize = 38, waveW = 376, chipH = 34;   // chip: 32 + 1 px border
    constexpr int lightX = padX + nameW + gap;          // 142
    constexpr int waveX = lightX + lightSize + gap;     // 194
    constexpr int chipX = waveX + waveW + gap;          // 584
```

  2. In `WaveformView::paint`, delete the last two lines (the cream playhead `fillRoundedRectangle`). The playhead is now drawn once across all rows by `StemsScreen`. Change the `head` comment to `// snapped, as the screen repaints per playhead pixel`.
  3. Replace `WaveformView::mouseDrag`'s body with nothing (`{}`). Dragging no longer scrubs; Task 6 turns it into loop selection.
  4. Replace the `StemRow` constructor, `setTheme`, `setPlayback`, `timeBounds`, `resized` and `paint` with:

```cpp
StemRow::StemRow (juce::File wav, juce::String key)
    : file (std::move (wav)), stemKey (std::move (key)), name (stemKey.substring (0, 1).toUpperCase() + stemKey.substring (1))
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    lightButton.onToggle = [this] { if (onToggleMute != nullptr) onToggleMute(); };
    lightButton.onSolo = [this] { if (onSolo != nullptr) onSolo(); };
    wave.onSeek = [this] (double f) { if (onSeek != nullptr) onSeek (f); };
    addAndMakeVisible (lightButton);
    addAndMakeVisible (wave);
    setTheme (themeFor ("dusk"));
    setSize (674, 62);
    setPeaks ({});
}

void StemRow::setTheme (const Theme& t)
{
    colour = t.stemColour (stemKey);
    wave.setStemColour (colour);
    repaint();
}

void StemRow::setMuted (bool m)
{
    if (m == muted) return;
    muted = m;
    lightButton.setMuted (m);
    wave.setAlpha (m ? 0.38f : 1.0f);
    repaint();
}

void StemRow::setPosition (double fraction)
{
    wave.setFraction (juce::jlimit (0.0, 1.0, fraction));
}

juce::Rectangle<int> StemRow::chipBounds() const
{
    // border 1 + padding 8, grip 16, gap 6, "Drag", padding 12 + border 1
    const auto w = (int) std::ceil (1 + 8 + 16 + 6 + juce::GlyphArrangement::getStringWidth (chipFont(), "Drag") + 12 + 1);
    return { chipX, (getHeight() - chipH) / 2, w, chipH };
}

void StemRow::resized()
{
    const int h = getHeight(), waveH = compact ? 26 : 40;
    lightButton.setBounds (lightX, (h - lightSize) / 2, lightSize, lightSize);
    wave.setBounds (waveX, (h - waveH) / 2 - WaveformView::margin, waveW, waveH + 2 * WaveformView::margin);
}

void StemRow::paint (juce::Graphics& g)
{
    const auto h = (float) getHeight();
    const float a = muted ? 0.38f : 1.0f;            // a muted row dims its name block (the waveform dims via its alpha)

    // Badge: 32 x 32, radius 10, stem colour at 0.16, icon 18 px with stroke 2.
    const juce::Rectangle<float> badge ((float) padX, (h - 32.0f) / 2.0f, 32.0f, 32.0f);
    if (g.clipRegionIntersects (badge.getSmallestIntegerContainer()))   // playhead repaints skip the path work
    {
        g.setColour (colour.withAlpha (0.16f * a));
        g.fillRoundedRectangle (badge, 10.0f);
        g.setColour (colour.withMultipliedAlpha (a));
        strokeIcon (g, stemIcon (stemKey), badge.withSizeKeepingCentre (18.0f, 18.0f), 2.0f);
    }

    // Name (Bold 14) over the file name (11, cream 0.6), line-height 1.2, 1 px apart, centred as one column, 10 px after the badge.
    const auto nameH = 14.0f * 1.2f, fileH = 11.0f * 1.2f, top = (h - (nameH + 1.0f + fileH)) / 2.0f;
    const auto textX = badge.getRight() + 10.0f, textW = (float) (padX + nameW) - textX;
    g.setColour (Theme::cream.withAlpha (a));
    g.setFont (Fonts::body (14.0f, 700));
    g.drawText (name, juce::Rectangle<float> (textX, top, textW, nameH), juce::Justification::centredLeft, true);
    g.setColour (Theme::cream.withAlpha (0.6f * a));
    g.setFont (Fonts::body (11.0f));
    g.drawText (file.getFileName(), juce::Rectangle<float> (textX, top + nameH + 1.0f, textW, fileH), juce::Justification::centredLeft, true);

    // Drag chip: 1 px dashed border (dash 3, gap 3) at cream 0.3, radius 10; grip 16 px stroke 3, 6 px, "Drag" Bold 12 at cream 0.82.
    if (! g.clipRegionIntersects ({ chipX, 0, getWidth() - chipX, getHeight() }))
        return;
    const auto chip = chipBounds().toFloat();
    drawDashedRoundedRect (g, chip.reduced (0.5f), 9.5f, 1.0f, 3.0f, 3.0f, Theme::cream.withAlpha (0.3f));
    const juce::Rectangle<float> grip (chip.getX() + 9.0f, chip.getCentreY() - 8.0f, 16.0f, 16.0f);
    g.setColour (Theme::cream.withAlpha (0.82f));
    strokeIcon (g, Icons::grip, grip, 3.0f);
    g.setFont (chipFont());
    g.drawText ("Drag", chip.withLeft (grip.getRight() + 6.0f), juce::Justification::centredLeft, false);
}
```

  5. In `StemRow::mouseDrag`, replace the last line with:

```cpp
    const auto f = dragFile != nullptr ? dragFile() : file;
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
```

  6. Delete the now-unused `playing`, `head`, `repaintCount`, `time` and `button` members (Step 4 already dropped them from the header). Delete `timeX` and `timeW`.

- [ ] **Step 6: Update the `StemsScreen` declaration** in `Source/Screens.h`. Replace the whole class with:

```cpp
// StemPlayer.mockup.html panel: song row (Play/Pause, name, time), one StemRow per stem file, footer hint.
class StemsScreen : public juce::Component
{
public:
    StemsScreen();
    ~StemsScreen() override;
    void setTheme (const Theme&);
    // Rebuilds the rows (vocals, drums, bass, guitar, piano, other, then unknown stems A-Z; compact with more than 4)
    // and reads their peaks and the song length on a background thread.
    void setStems (const juce::File& dir, const juce::String& songName);
    juce::File getDir() const { return dir; }
    juce::Array<juce::File> getFiles() const;       // row order = the player's stem order
    void setPlayback (bool playing, double fraction, double lengthSeconds);   // repaints only what changed
    void setAudible (juce::uint32 mask);            // bit i set = row i audible

    int numRows() const { return rows.size(); }
    juce::File fileOf (int row) const;
    bool hasPeaks() const { return peaksReady; }
    StemRow* row (int i) { return rows[i]; }        // test hook; nullptr when out of range
    juce::Rectangle<int> playheadArea (double fraction) const;   // the strip a playhead at `fraction` covers (bench hook)
    int getRepaintCount() const { return repaintCount; }         // setPlayback calls that repainted (test hook)

    std::function<void()> onNewSong, onPlayPause;
    std::function<void (int)> onToggleMute, onSolo;
    std::function<void (double)> onSeek;             // fraction 0..0.999

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // the shared playhead across every row
    void resized() override;

private:
    int headX (double fraction) const;               // screen x of the playhead
    juce::Range<int> waveSpan() const;               // top..bottom of the waveforms, row 0 to the last row

    Theme theme = themeFor ("dusk");
    juce::File dir;
    juce::String song, timeText { "0:00 / 0:00" };
    double length = 0.0, fraction = 0.0;             // the peak job's length until the player reports one
    bool playing = false, peaksReady = false;
    int lastHead = -1, generation = 0, repaintCount = 0;
    juce::OwnedArray<StemRow> rows;
    CircleButton playButton;
    LisnButton newSong;
    juce::ThreadPool pool { 1 };                     // last member: its jobs stop before the rows go
};
```

- [ ] **Step 7: Update `StemsScreen` in `Source/Screens.cpp`.**
  1. Replace the constructor:

```cpp
StemsScreen::StemsScreen() : newSong ("New song", LisnButton::Style::Ghost)
{
    newSong.icon = Icons::plus;                    // 16 px, stroke 2; 34 tall, border 0.22, font 13: the defaults
    newSong.padLeft = 10.0f;
    newSong.padRight = 14.0f;
    newSong.onClick = [this] { if (onNewSong != nullptr) onNewSong(); };
    playButton.onClick = [this] { if (onPlayPause != nullptr) onPlayPause(); };
    addAndMakeVisible (newSong);
    addAndMakeVisible (playButton);
}
```

  2. In `setStems`:
     - replace `length = activeLength = 0.0; activeRow = -1; activePlaying = peaksReady = false;` with `length = fraction = 0.0; playing = peaksReady = false; lastHead = -1; playButton.setPlaying (false);`;
     - replace the two row callback lines with the block below;
     - delete `positions.assign (...)`;
     - in the `callAsync` lambda, replace `safe->updateRows();` with `safe->timeText = mmss (0.0) + " / " + mmss (seconds);`.

```cpp
        row->onToggleMute = [this, i] { if (onToggleMute != nullptr) onToggleMute (i); };
        row->onSolo = [this, i] { if (onSolo != nullptr) onSolo (i); };
        row->onSeek = [this] (double f) { if (onSeek != nullptr) onSeek (f); };
```

  3. Delete `setPlayback (int, …)`, `updateRows`, `lengthOf`, `seek`, `rowOf` and `positionOf`. Add:

```cpp
juce::Array<juce::File> StemsScreen::getFiles() const
{
    juce::Array<juce::File> files;
    for (auto* r : rows) files.add (r->getFile());
    return files;
}

juce::Range<int> StemsScreen::waveSpan() const
{
    if (rows.isEmpty()) return {};
    const auto& first = rows.getFirst()->waveform();
    const auto& last = rows.getLast()->waveform();
    return { rows.getFirst()->getY() + first.getY(), rows.getLast()->getY() + last.getBottom() };
}

int StemsScreen::headX (double f) const
{
    return 19 + 194 + juce::roundToInt (f * 376.0);   // rows at x 19, waveform at row x 194, 376 wide
}

juce::Rectangle<int> StemsScreen::playheadArea (double f) const
{
    const auto span = waveSpan();
    return { headX (f) - 2, span.getStart(), 4, span.getLength() };
}

void StemsScreen::setPlayback (bool isPlaying, double f, double len)
{
    f = juce::jlimit (0.0, 1.0, f);
    if (len > 0.0) length = len;
    bool changed = false;
    for (auto* r : rows) r->setPosition (f);
    const auto x = headX (f);
    if (x != lastHead)                               // the played colour and the line change only between the two positions
    {
        const auto span = waveSpan();
        const auto from = lastHead < 0 ? x : juce::jmin (lastHead, x), to = lastHead < 0 ? x : juce::jmax (lastHead, x);
        repaint (juce::Rectangle<int>::leftTopRightBottom (from - 2, span.getStart(), to + 2, span.getEnd()));
        lastHead = x;
        changed = true;
    }
    const auto t = mmss (f * length) + " / " + mmss (length);
    if (t != timeText)
    {
        timeText = t;
        repaint (19 + 36 + 12, 37, 674 - 36 - 12, 16);   // the subtitle line only
        changed = true;
    }
    if (isPlaying != playing)
    {
        playing = isPlaying;
        playButton.setPlaying (playing);
        changed = true;
    }
    fraction = f;
    repaintCount += changed ? 1 : 0;
}

void StemsScreen::setAudible (juce::uint32 mask)
{
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setMuted (((mask >> i) & 1u) == 0);
}

void StemsScreen::paintOverChildren (juce::Graphics& g)
{
    if (rows.isEmpty() || lastHead < 0) return;
    const auto span = waveSpan();
    g.setColour (Theme::cream);
    g.fillRoundedRectangle ((float) lastHead - 1.0f, (float) span.getStart(), 2.0f, (float) span.getLength(), 1.0f);
}
```

  4. In `resized()`, add `playButton.setBounds (19, 17, 36, 36);` as the first line.
  5. In `paint()`, replace the song-row block (from `const juce::Rectangle<float> iconBox` to the `summary` `text (...)` call) with the code below, and change the footer text to `"Click a light to mute" + dot + "right-click to solo"`.

```cpp
        // Song name (Bold 15) over the time (12, cream 0.64): line-height 1.2, 2 px apart, centred in the 36 px row.
        const auto x = 19.0f + 36.0f + 12.0f, w = (float) newSong.getX() - 12.0f - x, y = 17.0f + (36.0f - 34.4f) / 2.0f;
        text (g, Fonts::body (15.0f, 700), Theme::cream, song, { x, y, w, 18.0f }, juce::Justification::centredLeft);
        text (g, Fonts::body (12.0f), Theme::cream.withAlpha (0.64f), timeText, { x, y + 20.0f, w, 14.4f }, juce::Justification::centredLeft);
```

- [ ] **Step 8: Processor.** In `Source/PluginProcessor.h`:
  - replace `#include "StemPreview.h"` with `#include "StemPlayer.h"` and `StemPreview preview;` with `StemPlayer player;`;
  - in `prepareToPlay`, `releaseResources` and `processBlock`, use `player.prepare (…)`, `player.release()` and `player.addTo (buffer)`, with the `processBlock` comment `// pass-through plus the stem player`.

  In `Source/PluginProcessor.cpp`, `startSplit` calls `player.unload();`.

- [ ] **Step 9: Editor.** In `Source/PluginEditor.h`, delete `void playPause (int row);` and change the destructor comment to `// pauses the player`. In `Source/PluginEditor.cpp`:
  1. Replace the `stems.onPlayPause` and `stems.onSeek` lambdas in the constructor with:

```cpp
    stems.onPlayPause = [this]
    {
        if (proc.player.isPlaying()) proc.player.pause();
        else                         proc.player.play();
    };
    stems.onToggleMute = [this] (int i) { proc.player.setMuted (i, ! proc.player.isMuted (i)); };
    stems.onSolo = [this] (int i) { proc.player.solo (i); };
    stems.onSeek = [this] (double f) { proc.player.setPositionFraction (f); };
```

  2. The destructor body becomes `proc.player.pause();`.
  3. In `tick()`, replace everything from `auto& preview = proc.preview;` down to the motion comment with the block below. In the motion code, replace `preview.` with `player.`.

```cpp
    auto& player = proc.player;
    if (player.takeReachedEnd())                     // the song played to its end: back to 0:00
        player.setPositionFraction (0.0);
    stems.setPlayback (player.isPlaying(), player.getPositionFraction(), player.getLengthSeconds());
    stems.setAudible (player.audibleMask());
```

  4. In `show()`, replace the final `if/else` block with:

```cpp
    if (s.screen != Screen::Stems)
        proc.player.unload();
    else if (s.stemDir != stems.getDir())
    {
        stems.setStems (s.stemDir, s.songName);
        if (proc.player.getFiles() != stems.getFiles())   // reopening the window keeps the mix, the position and the loop
            proc.player.load (stems.getFiles());
    }
```

  5. Delete `StemSplitterEditor::playPause`.

- [ ] **Step 10: Retire `StemPreview`.**
  - `git rm Source/StemPreview.h Source/StemPreview.cpp tests/StemPreviewTests.cpp`;
  - remove `Source/StemPreview.cpp` from `LISN_SOURCES` and `tests/StemPreviewTests.cpp` from the `lisn_tests` sources in `CMakeLists.txt`.

- [ ] **Step 11: Update `tests/UiStateTests.cpp`.** Replace the whole "stem rows: order, play/pause, switching rows" block with the block below. In "switching 6 to 4 stems…":
  - replace `screen.onPlayPause (5); ed.tick(); expect (proc.preview.getFile() == screen.fileOf (5));` with `proc.player.play(); ed.tick(); expectEquals (proc.player.getFiles().size(), 6);`;
  - replace `proc.preview.getFile() == juce::File()` with `proc.player.getFiles() == screen.getFiles() && ! proc.player.isPlaying()`.

```cpp
        beginTest ("stem player: order, play/pause, mutes, solo, repaint only on change");
        {
            StemSplitterProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            StemSplitterEditor ed (proc);
            UiState s;
            s.screen = Screen::Stems;
            s.stemDir = six;
            ed.forceState (s);
            ed.tick();
            auto& screen = ed.stemsScreen();
            juce::StringArray order;
            for (int i = 0; i < screen.numRows(); ++i)
                order.add (screen.fileOf (i).getFileNameWithoutExtension());
            expectEquals (order.joinIntoString (","), juce::String ("vocals,drums,bass,guitar,piano,other"));
            expect (proc.player.getFiles() == screen.getFiles());

            screen.onPlayPause();
            expect (proc.player.isPlaying());
            screen.onPlayPause();
            expect (! proc.player.isPlaying());

            screen.onToggleMute (1);
            expect (proc.player.isMuted (1));
            ed.tick();
            expect (screen.row (1)->light().isMuted() && ! screen.row (0)->light().isMuted());
            screen.onSolo (0);
            expectEquals ((int) proc.player.audibleMask(), 0b000001);
            screen.onSolo (0);
            expectEquals ((int) proc.player.audibleMask(), 0b111111);

            screen.setPlayback (false, 0.25, 30.0);
            const auto count = screen.getRepaintCount();
            screen.setPlayback (false, 0.25, 30.0);
            screen.setPlayback (false, 0.2501, 30.0);    // same pixel, same time text
            expectEquals (screen.getRepaintCount(), count);
            screen.setPlayback (false, 0.5, 30.0);
            expectEquals (screen.getRepaintCount(), count + 1);
        }
```

- [ ] **Step 12: Update `tests/Snapshots.cpp`.**
  1. The `playing` lambda becomes the block below. Rename the shot `stems4-dusk-playing` to `stems4-dusk-mix`.

```cpp
    const auto playing = [] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        p.player.setPositionFraction (0.38);
        p.player.setMuted (2, true);                 // bass muted
        p.player.play();                             // no audio callback: the position stays put
        ed.tick();
        ed.background().setMotion (0.0f, 0.0f);      // tick() advanced the flow by wall-clock time; keep snapshots repeatable
    };
```

  2. In the shot loop, replace `proc.preview.unload();` with `proc.player.unload();`.
  3. In `runBench`:
     - replace `proc.preview.load (stems.fileOf (0)); proc.preview.play();` with `proc.player.play();`;
     - replace the row region lines (`auto& row = …` to `frameRegion.add (ed.getLocalArea (&row, row.timeBounds()));`) with:

```cpp
    const auto motion = waves.motionRegion();
    auto frameRegion = motion;                       // plus the strip a moving playhead repaints across all rows
    frameRegion.add (ed.getLocalArea (&stems, stems.playheadArea (0.38).expanded (1, 0)));
```

     - replace `proc.preview.pause();` with `proc.player.pause();`;
     - replace the `proc.preview.load (stems.fileOf (3)); proc.preview.play();` pair with the block below;
     - replace the remaining `proc.preview.takePeak()` and `proc.preview.unload()` with `proc.player.…`;
     - rename the output keys `process_block_us_preview*` to `process_block_us_playing*`.

```cpp
    for (int i = 0; i < stems.numRows(); ++i)
        proc.player.setMuted (i, i != 3);            // "other" only: it is never silent, so every burst must meter a peak
    proc.player.setPositionFraction (0.0);
    proc.player.play();
```

- [ ] **Step 13: Build and run everything.**
Run: `cmake --build build --config Debug`, then `ctest --test-dir build -C Debug --output-on-failure`
Expected: 100% pass, and no warnings from `Source/` or `tests/` in the build output.

- [ ] **Step 14: Smoke-test the Standalone.** In PowerShell:
`$p = Start-Process -FilePath (Get-ChildItem build\StemSplitter_artefacts\Debug\Standalone\*.exe)[0].FullName -PassThru; Start-Sleep 5; if ($p.HasExited) { throw 'standalone exited' }; Stop-Process -Id $p.Id`
Expected: no error.

- [ ] **Step 15: Commit.**

```bash
git add -A Source tests CMakeLists.txt
git commit -m "feat: stem player UI with one Play/Pause, mute lights with solo and a shared playhead

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Loop, BPM snapping, Drag mix / Drag loop

**Files:**
- Modify: `Source/Icons.h`, `Source/Controls.h/.cpp`, `Source/StemRow.h/.cpp`, `Source/Screens.h/.cpp`, `Source/PluginEditor.h/.cpp`, `tests/StemRowTests.cpp`, `tests/UiStateTests.cpp`

**Interfaces:**
- Consumes:
  - `StemPlayer::setLoop`, `setLooping`, `isLooping`, `getLoop`, `audibleMask` and `render` (Tasks 2–3);
  - `detectTempo` and `Tempo` (Task 4);
  - `StemRow::dragFile` and `StemsScreen::waveSpan` (Task 5).
- Produces:
  - `WaveformView::onLoopDrag (double a, double b, bool done)`, forwarded through `StemRow::onLoopDrag`;
  - `StemsScreen::setLoop (juce::Range<double>, bool)`, `setTempo (Tempo)`, `snap (double)`, `chipText()`, `loopButtonEnabled()`;
  - the callbacks `onSetLoop (double, double)`, `onToggleLoop()`, `mixFile()` and `stemFile (int)`;
  - `DragChip`, `drawDragChip` and `LisnButton::tint`.

- [ ] **Step 1: Loop icon.** Add to `namespace Icons` in `Source/Icons.h`:

```cpp
    inline constexpr const char* loop = "M17 2l4 4-4 4M3 11v-1a4 4 0 0 1 4-4h14M7 22l-4-4 4-4M21 13v1a4 4 0 0 1-4 4H3";   // stroked
```

  Add `Icons::loop` to the icon list in the "icons" test in `tests/StemRowTests.cpp`.

- [ ] **Step 2: Controls.** In `Source/Controls.h`:
  - add `#include <optional>` at the top;
  - add to `LisnButton`'s public fields: `std::optional<juce::Colour> tint;   // Ghost "on" state: fill tint@0.22, border tint (the Loop button)`;
  - add after `drawDashedRoundedRect`:

```cpp
// The dashed "Drag" chip of the mockups: 1 px dashed border (dash 3, gap 3) at cream 0.3, radius 10, grip 16 px stroke 3,
// 6 px, text Bold 12 at cream 0.82. chipWidth gives the width for `text` (padding 0 12 0 8 plus the border).
void drawDragChip (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& text);
int dragChipWidth (const juce::String& text);

// A standalone drag chip: dragging it drops fileToDrag() (when it exists) onto a DAW track.
class DragChip : public juce::Component
{
public:
    DragChip();
    void setText (const juce::String&);          // repaints; size it with dragChipWidth (text) x 34
    juce::String getText() const { return text; }
    std::function<juce::File()> fileToDrag;
    void paint (juce::Graphics& g) override { drawDragChip (g, getLocalBounds().toFloat(), text); }
    void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::String text { "Drag mix" };
    bool dragStarted = false;
};
```

  In `Source/Controls.cpp`:
  - in `LisnButton::paintButton`, replace the fill colour line and the Ghost border block with the code below;
  - append the chip code after it.

```cpp
    g.setColour (tint.has_value() ? tint->withAlpha (0.22f)
                                  : style == Style::Primary ? Theme::cream : Theme::cream.withAlpha (style == Style::Ghost ? 0.06f : 0.12f));
    g.fillRoundedRectangle (r, radius);
    if (style == Style::Ghost)
    {
        g.setColour (tint.has_value() ? *tint : Theme::cream.withAlpha (borderAlpha));
        g.drawRoundedRectangle (r.reduced (0.5f), radius - 0.5f, 1.0f);
    }
```

```cpp
namespace { juce::Font chipFont() { return Fonts::body (12.0f, 700); } }

int dragChipWidth (const juce::String& text)
{
    return (int) std::ceil (1 + 8 + 16 + 6 + juce::GlyphArrangement::getStringWidth (chipFont(), text) + 12 + 1);
}

void drawDragChip (juce::Graphics& g, juce::Rectangle<float> chip, const juce::String& text)
{
    drawDashedRoundedRect (g, chip.reduced (0.5f), 9.5f, 1.0f, 3.0f, 3.0f, Theme::cream.withAlpha (0.3f));
    const juce::Rectangle<float> grip (chip.getX() + 9.0f, chip.getCentreY() - 8.0f, 16.0f, 16.0f);
    g.setColour (Theme::cream.withAlpha (0.82f));
    strokeIcon (g, Icons::grip, grip, 3.0f);
    g.setFont (chipFont());
    g.drawText (text, chip.withLeft (grip.getRight() + 6.0f), juce::Justification::centredLeft, false);
}

DragChip::DragChip()
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragChip::setText (const juce::String& t)
{
    if (t != text) { text = t; repaint(); }
}

void DragChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() <= 4 || fileToDrag == nullptr)
        return;
    dragStarted = true;   // once per gesture
    const auto f = fileToDrag();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}
```

  In `Source/StemRow.cpp`:
  - delete the local `chipFont()`;
  - `chipBounds()` returns `{ chipX, (getHeight() - chipH) / 2, dragChipWidth ("Drag"), chipH }`;
  - `paint()`'s chip section (after the `clipRegionIntersects` guard) becomes `drawDragChip (g, chipBounds().toFloat(), "Drag");`.

- [ ] **Step 3: Write the failing tests.**
  - **`tests/StemRowTests.cpp`:** add this test at the end of `runTest()`:

```cpp
        beginTest ("waveform drag selects a loop; a click only seeks");
        {
            double seekTo = -1.0, a = -1.0, b = -1.0;
            bool done = false;
            row.onSeek = [&] (double f) { seekTo = f; };
            row.onLoopDrag = [&] (double from, double to, bool finished) { a = from; b = to; done = finished; };
            auto& w = row.waveform();
            auto event = [&] (float x, juce::Point<float> down)
            {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, 20.0f }, {}, 0.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, &w, &w, now, down, now, 1, false);
            };
            const juce::Point<float> down (94.0f, 20.0f);    // 0.25 of 376
            w.mouseDown (event (94.0f, down));
            expectWithinAbsoluteError (seekTo, 0.25, 0.001);
            w.mouseUp (event (95.0f, down));
            expectEquals (a, -1.0);                          // under 4 px: no loop
            w.mouseDown (event (94.0f, down));
            w.mouseDrag (event (188.0f, down));
            expect (! done);
            expectWithinAbsoluteError (b, 0.5, 0.001);
            w.mouseUp (event (188.0f, down));
            expect (done);
            expectWithinAbsoluteError (a, 0.25, 0.001);
            expectWithinAbsoluteError (b, 0.5, 0.001);
        }
```

  - **`tests/UiStateTests.cpp`:** add this block after the stem-player block:

```cpp
        beginTest ("loop: snapping, button, chip, renders");
        {
            StemSplitterProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            StemSplitterEditor ed (proc);
            UiState s;
            s.screen = Screen::Stems;
            s.stemDir = four;
            s.songName = "Song.mp3";
            ed.forceState (s);
            ed.tick();
            auto& screen = ed.stemsScreen();
            expect (! screen.loopButtonEnabled());
            expectEquals (screen.chipText(), juce::String ("Drag mix"));

            screen.setTempo ({ 120.0, 0.0 });                // 0.5 s stems: beats every 0.5 s = the whole song
            expectEquals (screen.snap (0.3), 0.0);
            expectEquals (screen.snap (0.8), 1.0);
            screen.setTempo ({});
            expectEquals (screen.snap (0.3), 0.3);           // no tempo: no snapping

            screen.onSetLoop (0.2, 0.6);
            ed.tick();
            expect (proc.player.isLooping());
            expect (screen.loopButtonEnabled());
            expectEquals (screen.chipText(), juce::String ("Drag loop"));
            screen.onToggleLoop();
            ed.tick();
            expect (! proc.player.isLooping());
            expectEquals (screen.chipText(), juce::String ("Drag mix"));

            proc.player.setMuted (2, true);
            const auto mix = screen.mixFile();
            expect (mix.existsAsFile(), mix.getFullPathName());
            expect (mix.getFileName().contains ("vocals+drums+other"), mix.getFileName());
            expect (mix.isAChildOf (four.getChildFile ("renders")));
            expect (screen.stemFile (1) == screen.fileOf (1));    // no loop: the stem file itself
            screen.onToggleLoop();
            const auto drumsLoop = screen.stemFile (1);
            expect (drumsLoop.existsAsFile() && drumsLoop.getFileName().contains ("drums ("), drumsLoop.getFileName());
            four.getChildFile ("renders").deleteRecursively();
        }
```

  Run: `cmake --build build --config Debug --target lisn_tests`
  Expected: compile errors (the missing `onLoopDrag`, `setTempo`, `snap`, `chipText`, `loopButtonEnabled`, `onSetLoop`, `onToggleLoop`, `mixFile` and `stemFile`).

- [ ] **Step 4: Waveform loop drag.**
  - **`Source/StemRow.h`:**
    - `WaveformView` gets the public member `std::function<void (double, double, bool)> onLoopDrag;   // from, to (fractions), finished`, the override `void mouseUp (const juce::MouseEvent&) override;`, and the private members `double downFraction = 0.0; bool looping = false;`;
    - `StemRow` gets the public member `std::function<void (double, double, bool)> onLoopDrag;`.
  - **`Source/StemRow.cpp`:** replace `WaveformView::mouseDown` and `mouseDrag` with the code below, and add to the `StemRow` constructor: `wave.onLoopDrag = [this] (double a, double b, bool done) { if (onLoopDrag != nullptr) onLoopDrag (a, b, done); };`.

```cpp
void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    looping = false;
    if (getWidth() <= 0) return;
    downFraction = juce::jlimit (0.0, 0.999, (double) e.position.x / getWidth());
    if (onSeek != nullptr) onSeek (downFraction);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (getWidth() <= 0 || (! looping && e.getDistanceFromDragStart() < 4)) return;
    looping = true;                                  // 4 px or more: this gesture selects a loop instead of seeking
    if (onLoopDrag != nullptr) onLoopDrag (downFraction, juce::jlimit (0.0, 1.0, (double) e.position.x / getWidth()), false);
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (looping && onLoopDrag != nullptr && getWidth() > 0)
        onLoopDrag (downFraction, juce::jlimit (0.0, 1.0, (double) e.position.x / getWidth()), true);
    looping = false;
}
```

- [ ] **Step 5: `StemsScreen` loop, BPM and chip.**
  - **`Source/Screens.h`:**
    - add `#include "Peaks.h"`;
    - add to `StemsScreen`'s public section:

```cpp
    void setLoop (juce::Range<double> range, bool on);   // from the player every tick; repaints only on change
    void setTempo (Tempo);                           // the peak job sets it; tests may too
    double snap (double fraction) const;             // to the nearest beat; unchanged without a tempo
    juce::String chipText() const { return dragChip.getText(); }   // test hook
    bool loopButtonEnabled() const { return loopButton.isEnabled(); }   // test hook
    std::function<void (double, double)> onSetLoop;  // a finished waveform drag, snapped
    std::function<void()> onToggleLoop;
    std::function<juce::File()> mixFile;             // the Drag mix / Drag loop file
    std::function<juce::File (int)> stemFile;        // what row i drags
```

    - and to its private section:

```cpp
    juce::Rectangle<int> loopArea (juce::Range<double>) const;   // the band's pixels, padded by the edge width
    juce::Range<double> loopRange, dragBand;         // the player's loop / the band shown while dragging (empty = none)
    bool loopOn = false;
    Tempo tempo;
    LisnButton loopButton { "Loop", LisnButton::Style::Ghost };
    DragChip dragChip;
```

    Keep `newSong` and `pool` last, and declare `loopButton` and `dragChip` before `newSong`.
  - **`Source/Screens.cpp`, constructor:** add

```cpp
    loopButton.icon = Icons::loop;
    loopButton.padLeft = 10.0f;
    loopButton.padRight = 14.0f;
    loopButton.setEnabled (false);
    loopButton.setAlpha (0.55f);
    loopButton.onClick = [this] { if (onToggleLoop != nullptr) onToggleLoop(); };
    dragChip.fileToDrag = [this] { return mixFile != nullptr ? mixFile() : juce::File(); };
    addAndMakeVisible (loopButton);
    addAndMakeVisible (dragChip);
```

  - **`setStems`:**
    - reset `loopRange = dragBand = {}; loopOn = false; tempo = {};`;
    - per row add the callbacks below;
    - in the peak job, after the peaks loop, add the drums-tempo code below, and capture `found` into the `callAsync` lambda (`[safe, gen, peaks = std::move (peaks), seconds, found]`), where it calls `safe->setTempo (found);`.

```cpp
        row->onLoopDrag = [this] (double a, double b, bool done)
        {
            dragBand = { snap (juce::jmin (a, b)), snap (juce::jmax (a, b)) };
            repaint (waveSpanArea());
            if (done)
            {
                if (! dragBand.isEmpty() && onSetLoop != nullptr) onSetLoop (dragBand.getStart(), dragBand.getEnd());
                dragBand = {};
            }
        };
        row->dragFile = [this, i] { return stemFile != nullptr ? stemFile (i) : fileOf (i); };
```

```cpp
        Tempo found;
        for (const auto& f : files)
            if (f.getFileNameWithoutExtension().equalsIgnoreCase ("drums"))
                if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) })
                    found = detectTempo (*r);
```

  - **New `StemsScreen` members.** Also add the declaration `juce::Rectangle<int> waveSpanArea() const;` to the private section.

```cpp
juce::Rectangle<int> StemsScreen::waveSpanArea() const
{
    const auto span = waveSpan();
    return { 19 + 194 - 3, span.getStart(), 376 + 6, span.getLength() };
}

juce::Rectangle<int> StemsScreen::loopArea (juce::Range<double> r) const
{
    const auto span = waveSpan();
    return juce::Rectangle<int>::leftTopRightBottom (headX (r.getStart()) - 2, span.getStart(), headX (r.getEnd()) + 2, span.getEnd());
}

void StemsScreen::setTempo (Tempo t)
{
    tempo = t;
    repaint (19 + 36 + 12, 37, 674 - 36 - 12, 16);   // the subtitle shows the BPM
}

double StemsScreen::snap (double f) const
{
    if (tempo.bpm <= 0.0 || length <= 0.0) return f;
    const double beat = 60.0 / tempo.bpm;
    const double t = tempo.firstBeat + std::round ((f * length - tempo.firstBeat) / beat) * beat;
    return juce::jlimit (0.0, 1.0, t / length);
}

void StemsScreen::setLoop (juce::Range<double> range, bool on)
{
    if (range == loopRange && on == loopOn) return;
    repaint (loopArea (loopOn ? loopRange : juce::Range<double>()));   // the old band
    loopRange = range;
    loopOn = on;
    repaint (loopArea (loopOn ? loopRange : juce::Range<double>()));   // the new band
    repaint (19 + 36 + 12, 37, 674 - 36 - 12, 16);   // the subtitle shows the loop
    loopButton.setEnabled (! loopRange.isEmpty());
    loopButton.setAlpha (loopRange.isEmpty() ? 0.55f : 1.0f);
    loopButton.tint = loopOn ? std::optional<juce::Colour> (theme.accent()) : std::nullopt;
    loopButton.repaint();
    dragChip.setText (loopOn ? "Drag loop" : "Drag mix");
    resized();                                       // the chip width follows its text
}
```

  - **`setTheme`:** add `loopButton.tint = loopOn ? std::optional<juce::Colour> (theme.accent()) : std::nullopt; loopButton.repaint();`.
  - **`resized()`:** after placing `newSong`, add

```cpp
    const auto chipW = dragChipWidth (dragChip.getText());
    dragChip.setBounds (newSong.getX() - 8 - chipW, 18, chipW, 34);
    loopButton.setBounds (dragChip.getX() - 8 - loopButton.preferredWidth(), 18, loopButton.preferredWidth(), loopButton.height);
```

  - **`paint()`:**
    - the song-name width becomes `w = (float) loopButton.getX() - 12.0f - x`;
    - the subtitle text is the `subtitle` string below, drawn with the `text (…)` call that currently draws `timeText`;
    - the footer text becomes `"Click a light to mute" + dot + "right-click to solo" + dot + "drag across the waves to loop"`.

```cpp
        auto subtitle = timeText;
        if (tempo.bpm > 0.0) subtitle << dot << juce::roundToInt (tempo.bpm) << " BPM";
        if (loopOn)
            subtitle << dot << "loop " << mmss (loopRange.getStart() * length)
                     << juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) << mmss (loopRange.getEnd() * length);
```

  - **`paintOverChildren`:** draw the band before the playhead, and remove the `lastHead < 0` early return (keep the `rows.isEmpty()` one; draw the playhead only when `lastHead >= 0`):

```cpp
    const auto band = ! dragBand.isEmpty() ? dragBand : loopOn ? loopRange : juce::Range<double>();
    if (! band.isEmpty())
    {
        const auto span = waveSpan();
        const auto x0 = (float) headX (band.getStart()), x1 = (float) headX (band.getEnd());
        g.setColour (Theme::cream.withAlpha (0.10f));
        g.fillRect (x0, (float) span.getStart(), x1 - x0, (float) span.getLength());
        g.setColour (Theme::cream);
        g.fillRect (x0 - 1.0f, (float) span.getStart(), 2.0f, (float) span.getLength());
        g.fillRect (x1 - 1.0f, (float) span.getStart(), 2.0f, (float) span.getLength());
    }
```

- [ ] **Step 6: Editor wiring.**
  - **`Source/PluginEditor.h`:** add the private member `juce::File renderFile (juce::uint32 mask);   // the audible stems over the loop (or the song) as a cached WAV`.
  - **`Source/PluginEditor.cpp`, constructor:** add

```cpp
    stems.onSetLoop = [this] (double a, double b) { proc.player.setLoop (a, b); };
    stems.onToggleLoop = [this] { proc.player.setLooping (! proc.player.isLooping()); };
    stems.mixFile = [this] { return renderFile (proc.player.audibleMask()); };
    stems.stemFile = [this] (int i) { return proc.player.isLooping() ? renderFile (1u << i) : stems.fileOf (i); };
```

  - **`tick()`:** after `stems.setAudible (…)`, add `stems.setLoop (player.getLoop(), player.isLooping());`.
  - **New function:**

```cpp
juce::File StemSplitterEditor::renderFile (juce::uint32 mask)
{
    auto& p = proc.player;
    const auto files = p.getFiles();
    juce::StringArray names;
    for (int i = 0; i < files.size(); ++i)
        if ((mask >> i) & 1u) names.add (files[i].getFileNameWithoutExtension());
    if (names.isEmpty() || ! shown.has_value()) return {};

    const auto len = p.getLengthSeconds();
    const auto from = p.isLooping() ? p.getLoop().getStart() * len : 0.0, to = p.isLooping() ? p.getLoop().getEnd() * len : len;
    auto clock = [] (double sec) { const auto s = juce::jmax (0, (int) sec); return juce::String (s / 60) + "." + juce::String (s % 60).paddedLeft ('0', 2); };
    auto name = shown->songName.upToLastOccurrenceOf (".", false, false) + " - " + names.joinIntoString ("+");
    if (p.isLooping()) name << " (" << clock (from) << "-" << clock (to) << ")";
    const auto dest = stems.getDir().getChildFile ("renders").getChildFile (juce::File::createLegalFileName (name + ".wav"));
    // ponytail: renders on the drag gesture (a loop takes milliseconds, a whole song under a second). Upgrade path: render
    // in the background whenever the mutes or the loop change.
    return StemPlayer::render (files, mask, from, to, dest) ? dest : juce::File();
}
```

  `songName` is empty for the forced test state without a name. `upToLastOccurrenceOf` then returns an empty string, which is fine.

- [ ] **Step 7: Run everything.**
Run: `cmake --build build --config Debug`, then `ctest --test-dir build -C Debug --output-on-failure`
Expected: 100% pass, with no warnings from `Source/` or `tests/`.

- [ ] **Step 8: Commit.**

```bash
git add -A Source tests
git commit -m "feat: beat-snapped loop, Loop button, Drag mix / Drag loop and loop-trimmed stem drags

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Snapshots, bench, pluginval, FL check

**Files:**
- Modify: `tests/Snapshots.cpp`, `docs/superpowers/plans/2026-09-29-desktop-app.md`

- [ ] **Step 1: New snapshot cases.** In `runSnapshots`, add a `looping` setup and two shots:

```cpp
    const auto looping = [playing] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        playing (p, ed);
        p.player.setLoop (0.30, 0.45);
        p.player.setPositionFraction (0.38);
        ed.tick();
        ed.background().setMotion (0.0f, 0.0f);
    };
    const auto sixMuted = [] (StemSplitterProcessor& p, StemSplitterEditor& ed)
    {
        p.player.setMuted (1, true);
        p.player.setMuted (4, true);
        p.player.setPositionFraction (0.6);
        ed.tick();
    };
```

  - replace the `stems4-dusk-mix` entry's setup with `looping`, and do the same for `stems4-dusk@2x`;
  - change the `stems6-midnight` entry to `{ "stems6-midnight-muted", "midnight", state (Screen::Stems, true), 1.0f, sixMuted }`.
- [ ] **Step 2: Render and read every PNG.**
Run: `cmake --build build --config Debug`, then `build\lisn_tests_artefacts\Debug\lisn_tests.exe --snapshots snapshots`
Open every `snapshots/stems*.png` with the Read tool and compare it with `docs/design/StemPlayer.mockup.html`:
  - Play/Pause replaces the file icon;
  - the subtitle reads `m:ss / m:ss · N BPM · loop a–b`;
  - Loop, Drag loop and New song sit on the right;
  - the white speaker lights, with muted rows dimmed and showing the ×;
  - the loop band spans all rows;
  - a single playhead line runs across the rows;
  - there's no per-row time.
  Fix any deviation and re-render.
- [ ] **Step 3: Bench in Release.**
Run: `cmake --build build --config Release --target lisn_tests`, then `build\lisn_tests_artefacts\Release\lisn_tests.exe --bench` three times.
Expected:
  - `motion_frame_ms` ≤ 4.0 in each run;
  - `process_block_us_idle` stays around 0.005;
  - no "FAIL" lines.
- [ ] **Step 4: Full verification.**
  - Build Debug and run ctest; it must be 100%.
  - Run pluginval `--strictness-level 5` on Debug, then build Release (**close FL first**: Release now feeds FL through the junction) and run pluginval on Release. Both must print SUCCESS.
  - Run the Release Standalone for 5 s (as in Task 5, Step 14, with the `Release` path).
- [ ] **Step 5: Desktop plan note.** In `docs/superpowers/plans/2026-09-29-desktop-app.md`, add this line at the top of Phase 4: `> **Done upstream (2026-09-29):** StemPlayer (lockstep stems, mute/solo, loop wrap, render) and the mute/loop/Drag-mix UI already exist; this phase only adds per-stem volume and speed.`
- [ ] **Step 6: Commit.**

```bash
git add tests/Snapshots.cpp docs/superpowers/plans/2026-09-29-desktop-app.md
git commit -m "test: stem player snapshots and bench; desktop plan notes the shared player

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: The user's FL click-through.** FL loads the Release build through the junction; no copy and no rescan. Ask the user to test:
  - Play/Pause;
  - clicking each light, and right-click solo/unsolo;
  - clicking a waveform to seek;
  - dragging a loop on the beat, and the Loop button on/off;
  - dragging Drag mix and Drag loop into the playlist;
  - dragging a row with a loop on;
  - the 4/6 switch while playing;
  - closing and reopening the window while playing, which should keep the mix.
  After each click, the spacebar must still toggle FL's transport. Record the result in the final report.
