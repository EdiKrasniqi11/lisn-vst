# Engine: Volume and Keep-the-Key Speed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `StemPlayer` gets per-stem volume and a 0.5x-1.5x speed that keeps the key (Signalsmith Stretch). Its `render` gains volumes, speed, progress and cancel, with cache names that encode them. No visible UI change yet.

**Architecture:**
- **Volume** is the level the existing per-block mute ramp heads for.
- **Speed:** at exactly 1.0 the current direct path runs bit for bit. Otherwise `addTo` pulls `speed * n` samples from the transport, mixes them, and one stereo `SignalsmithStretch` turns them into n output samples. The stretcher stays engaged until the next `play()` or seek.
- **Offline:** `render` follows the stretcher's exact-length recipe (`outputSeek`, `process`, `flush`) in chunks, so progress and cancel work.
- **Cache names:** a pure `StemPlayer::renderName` builds them, and the editor uses it.

**Tech Stack:** JUCE 8.0.15, C++17, CMake + MSVC 2022, Signalsmith Stretch 1.4.0 + Signalsmith Linear 0.6.4 (MIT, header-only, CMake FetchContent).

**Spec:** `docs/superpowers/specs/2026-10-01-volume-speed-intro-desktop-look-design.md` (sections "Shared: the engine", "Testing", "Order of work" step 1). Plans 2 (shared UI) and 3 (desktop) follow this one.

## Global Constraints

- **Pins:**
  - Signalsmith Stretch 1.4.0 = commit `a670068d9aeb64913331d5cc29337b19a457a7df`.
  - Signalsmith Linear 0.6.4 = commit `de55e6a50ffcf6f8f43f649692d94691c7025151`.
  - Both MIT.
  - Their licence texts go in `installer/THIRD-PARTY-NOTICES.txt`.
- **The redesign spec's "Must hold" list still binds:**
  - a stopped player's `addTo` returns after two relaxed atomic loads;
  - `transport.stop()` is never called;
  - play and pause are atomic flags;
  - **nothing allocates on the audio thread** (the stretcher is configured and warmed in `prepare()`).
- **Bit for bit at 1.0:** at speed exactly 1.0, playback is the current path. Every existing `StemPlayerTests` case passes unchanged except the four `render` call sites, which move to the new signature.
- **Value ranges:**
  - Volume 0..1, clamped; `load()` resets every stem to 1.
  - Speed 0.5..1.5, clamped; `load()` resets it to 1.
- **Single stems are always the originals; the mix is what you hear.** A row drag renders one stem at gain 1 and speed 1. The mix renders `mixGains()` at the player's speed.
- **Untouched files:** `installer/sitecustomize.py`, `installer/requirements.txt` and `installer/setup-engine.ps1` stay byte-identical.
- **Writing:** no em dashes anywhere (code comments, docs, commit messages).
- **Git:**
  - Stage explicit paths only, never `git add .`.
  - Every commit message ends with a blank line and `Co-Authored-By: <your model> <noreply@anthropic.com>`.
  - All work happens in the worktree `C:\Projects\lisn-vst-engine` on branch `feature/engine-volume-speed`.
  - Pushes and merges need the owner's go-ahead.
- **Builds hog the owner's PC** (see memory `builds-slow-the-pc`):
  - Tell the owner before a fresh or full rebuild.
  - Build only LISN targets and leave no MSBuild workers:
    ```powershell
    cmake --build build --config Release --parallel --target StemSplitter_All lisn_tests progress_test -- /nodeReuse:false
    ctest --test-dir build -C Release --output-on-failure
    ```
  - For a quick test loop, `--target lisn_tests` alone, then run `build\lisn_tests_artefacts\Release\lisn_tests.exe`. It prints JUCE test results; a failure prints `!!! Test ... failed` and exits 1.

## Files

- **Modify:**
  - `CMakeLists.txt`: fetch and link Signalsmith.
  - `Source/StemPlayer.h`, `Source/StemPlayer.cpp`: volume, speed, render, renderName.
  - `Source/PluginEditor.h`, `Source/PluginEditor.cpp`: `renderFile` uses gains, speed and `renderName`.
  - `tests/StemPlayerTests.cpp`: new cases, and the render calls moved to the new signature.
  - `tests/Snapshots.cpp`: the bench's stretch figure.
  - `installer/THIRD-PARTY-NOTICES.txt`.
  - The spec: one bullet refined.
- **Create:** none.

---

### Task 0: Worktree and baseline

- [ ] **Step 1:** Tell the owner: "Starting a fresh build in a new worktree. The PC will be busy for several minutes while JUCE compiles." Then:
  ```powershell
  git -C C:/Projects/lisn-vst worktree add ../lisn-vst-engine -b feature/engine-volume-speed
  ```
  Every later command runs in `C:\Projects\lisn-vst-engine`.
- [ ] **Step 2:** Configure, build the LISN targets and test:
  ```powershell
  cmake -S . -B build -A x64
  cmake --build build --config Release --parallel --target StemSplitter_All lisn_tests progress_test -- /nodeReuse:false
  ctest --test-dir build -C Release --output-on-failure
  ```
  Expected: `100% tests passed out of 2`. If not, stop and report: the baseline must be green.

### Task 1: Volume, the new `render` signature (at 1.0), `renderName`, `mixGains`

**Files:**
- Modify `Source/StemPlayer.h`, `Source/StemPlayer.cpp`.
- Modify `Source/PluginEditor.h:43`, `Source/PluginEditor.cpp:39-40,253-271`.
- Test: `tests/StemPlayerTests.cpp`.

**Interfaces:**
- **Produces (StemPlayer, public):**
  - `void setVolume (int stem, float level)`: clamps to 0..1; out-of-range stems are ignored.
  - `float getVolume (int stem) const`: 1 for a stem out of range.
  - `std::vector<float> mixGains() const`: one per loaded stem, its volume, or 0 while muted.
  - `static bool render (const juce::Array<juce::File>& stems, const std::vector<float>& gains, double startSec, double endSec, double speed, const juce::File& dest, const std::function<bool (float)>& onProgress = {})`.
    - Gains are per stem; 0 means left out.
    - `onProgress` gets 0..1 after each chunk, and returning false cancels.
    - It returns false, and leaves no `dest` and no `dest.part`, when no gain is above 0, the range is empty, a file is unreadable, a write fails or it was cancelled.
    - **In this task, a speed other than 1.0 returns false;** Task 3 adds stretching.
  - `static juce::String renderName (const juce::String& songFile, const juce::StringArray& stemNames, const std::vector<float>& gains, double speed, juce::Range<double> loopSeconds)`: the cached file's name, or empty when no gain is above 0. An empty `loopSeconds` means the whole song.
- **Produces (StemSplitterEditor, private):** `juce::File renderFile (const std::vector<float>& gains, double speed)`.

- [ ] **Step 1: Write the failing tests.** In `tests/StemPlayerTests.cpp`:
  1. Right after the "mute is heard within one block" block (after its last `expectGreaterThan (rms (buf, 0, 512), 0.2f);`), add:
  ```cpp
          beginTest ("volume is heard within one block");
          expectEquals (p.getVolume (0), 1.0f);
          pull (0.0f);
          const float full = rms (buf, 0, 512);
          p.setVolume (0, 0.5f);
          pull (0.0f);                                         // the ramp block
          pull (0.0f);
          expectWithinAbsoluteError (rms (buf, 0, 512), full * 0.5f, full * 0.1f);
          p.setVolume (0, 2.0f);                               // clamped to 1
          expectEquals (p.getVolume (0), 1.0f);
          pull (0.0f);
          pull (0.0f);
          expectWithinAbsoluteError (rms (buf, 0, 512), full, full * 0.1f);
          expectEquals (p.getVolume (99), 1.0f);               // out of range

          beginTest ("mixGains");
          p.setVolume (0, 0.25f);
          expect (p.mixGains() == std::vector<float> { 0.25f });
          p.setMuted (0, true);
          expect (p.mixGains() == std::vector<float> { 0.0f });
          p.setMuted (0, false);
          p.setVolume (0, 1.0f);
  ```
  2. Right after the "oversized block" block (after `expectGreaterThan (rms (big, 768, 256), 0.2f);`), add:
  ```cpp
          beginTest ("load resets the volumes");
          p.setVolume (0, 0.3f);
          expect (p.load ({ sine }));
          expectEquals (p.getVolume (0), 1.0f);
  ```
  3. In the "render" block, replace the four `StemPlayer::render (...)` calls with the new signature:
  ```cpp
          expect (StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.25, 0.75, 1.0, both));
  ```
  ```cpp
          expect (StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.25, 0.75, 1.0, both));   // cached by name: not rewritten
  ```
  ```cpp
          expect (! StemPlayer::render ({ a, b }, { 0.0f, 0.0f }, 0.0, 1.0, 1.0, dir.getChildFile ("none.wav")));
          expect (! StemPlayer::render ({ a, b }, { 1.0f, 0.0f }, 0.5, 0.5, 1.0, dir.getChildFile ("empty-range.wav")));
  ```
  4. Right after the line `expect (! dir.getChildFile ("none.wav").exists() && ! dir.getChildFile ("empty-range.wav").exists());`, add:
  ```cpp
          beginTest ("render applies the gains");
          const auto half = dir.getChildFile ("renders").getChildFile ("half.wav");
          expect (StemPlayer::render ({ a, b }, { 0.5f, 1.0f }, 0.25, 0.75, 1.0, half));
          {
              std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (half));
              expect (r != nullptr);
              if (r != nullptr)
              {
                  juce::AudioBuffer<float> got (2, 22050);
                  r->read (&got, 0, 22050, 0, true, true);
                  float worst = 0.0f;
                  for (int i = 0; i < 22050; ++i)
                  {
                      const int s = 11025 + i;
                      const float want = (float) (0.5 * 0.3 * std::sin (0.04 * s) + 0.2 * std::sin (0.07 * s));
                      worst = juce::jmax (worst, std::abs (got.getSample (0, i) - want), std::abs (got.getSample (1, i) - want));
                  }
                  expectLessThan (worst, 1e-4f);
              }
          }

          beginTest ("render reports progress and can be cancelled");
          std::vector<float> seen;
          expect (StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.0, 1.0, 1.0, dir.getChildFile ("renders").getChildFile ("progress.wav"),
                                      [&] (float f) { seen.push_back (f); return true; }));
          expect (! seen.empty() && juce::exactlyEqual (seen.back(), 1.0f) && std::is_sorted (seen.begin(), seen.end()));
          const auto cancelled = dir.getChildFile ("renders").getChildFile ("cancelled.wav");
          expect (! StemPlayer::render ({ a, b }, { 1.0f, 1.0f }, 0.0, 1.0, 1.0, cancelled, [] (float) { return false; }));
          expect (! cancelled.exists() && ! cancelled.getSiblingFile ("cancelled.wav.part").exists());

          beginTest ("renderName");
          const juce::StringArray stemNames { "vocals", "drums" };
          expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 1.0f }, 1.0, {}), juce::String ("Song - vocals+drums.wav"));
          expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 0.85f, 0.0f }, 1.0, {}), juce::String ("Song - vocals 85.wav"));
          expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 1.0f }, 1.25, {}), juce::String ("Song - vocals+drums x1.25.wav"));
          expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 1.0f, 0.0f }, 1.0, { 58.25, 86.5 }),
                        juce::String ("Song - vocals (0.58.25-1.26.50).wav"));
          expectEquals (StemPlayer::renderName ("Song.mp3", stemNames, { 0.0f, 0.0f }, 1.0, {}), juce::String());
  ```
  `std::is_sorted` needs `#include <algorithm>`: add it after `#include <thread>` at the top.

- [ ] **Step 2: Run them and watch them fail.**
  ```powershell
  cmake --build build --config Release --target lisn_tests -- /nodeReuse:false
  ```
  Expected: compile errors, e.g. `'setVolume': is not a member of 'StemPlayer'` and no matching `render` overload.

- [ ] **Step 3: Implement it in `Source/StemPlayer.h`.**
  - Add `#include <vector>` after `#include <atomic>`.
  - After `juce::uint32 audibleMask() const;`, add:
  ```cpp
      void setVolume (int stem, float level);     // 0..1 (clamped), heard within one block; load() resets every stem to 1
      float getVolume (int stem) const;           // 1 for a stem out of range
      std::vector<float> mixGains() const;        // per loaded stem: its volume, or 0 while muted (what a mix renders)
  ```
  - Replace the `render` comment and declaration with:
  ```cpp
      // Every stem with a gain above 0, scaled by it, summed over [startSec, endSec) into a 24-bit stereo WAV at the stems'
      // rate. Reuses dest if it already exists (renders are cached by name). onProgress gets 0..1 after each chunk; returning
      // false cancels. False, with no dest and no dest.part left, if no gain is above 0, the range is empty, a file is
      // unreadable, a write fails or it was cancelled. A speed other than 1 is not supported yet and returns false.
      static bool render (const juce::Array<juce::File>& stems, const std::vector<float>& gains, double startSec, double endSec,
                          double speed, const juce::File& dest, const std::function<bool (float)>& onProgress = {});

      // A render's cached file name: "<song> - <stem>[ <volume %>]+..."; " x<speed>" when not 1; " (m.ss.cc-m.ss.cc)" for a
      // loop (empty loopSeconds = the whole song). Empty when no gain is above 0.
      static juce::String renderName (const juce::String& songFile, const juce::StringArray& stemNames,
                                      const std::vector<float>& gains, double speed, juce::Range<double> loopSeconds);
  ```
  - In the private members, after `std::array<std::atomic<bool>, maxStems> muted {};`, add:
  ```cpp
      std::array<std::atomic<float>, maxStems> volumes;   // 0..1 per stem; 1 after construction and load()
  ```

- [ ] **Step 4: Implement it in `Source/StemPlayer.cpp`.**
  - In the anonymous namespace, after `readStereo`, add:
  ```cpp
      // Every reader times its level, summed into sum's first n samples (stereo) from file position pos; one is scratch.
      void readMix (juce::OwnedArray<juce::AudioFormatReader>& readers, const std::vector<float>& levels,
                    juce::AudioBuffer<float>& sum, juce::AudioBuffer<float>& one, juce::int64 pos, int n)
      {
          sum.clear (0, n);
          for (int k = 0; k < readers.size(); ++k)
          {
              readStereo (*readers[k], one.getWritePointer (0), one.getWritePointer (1), pos, n);
              for (int ch = 0; ch < 2; ++ch)
                  sum.addFrom (ch, 0, one, ch, 0, n, levels[(size_t) k]);
          }
      }

      constexpr int renderChunk = 65536;

      // The plain mix of [first, last) into w, a chunk at a time; onProgress after each (false cancels).
      bool writeMix (juce::AudioFormatWriter& w, juce::OwnedArray<juce::AudioFormatReader>& readers, const std::vector<float>& levels,
                     juce::int64 first, juce::int64 last, const std::function<bool (float)>& onProgress)
      {
          juce::AudioBuffer<float> sum (2, renderChunk), one (2, renderChunk);
          for (auto pos = first; pos < last; pos += renderChunk)
          {
              const int n = (int) juce::jmin ((juce::int64) renderChunk, last - pos);
              readMix (readers, levels, sum, one, pos, n);
              if (! w.writeFromAudioSampleBuffer (sum, 0, n)) return false;
              if (onProgress && ! onProgress ((float) (pos + n - first) / (float) (last - first))) return false;
          }
          return true;
      }
  ```
  - Constructor: add `for (auto& v : volumes) v = 1.0f;` as the first line of `StemPlayer::StemPlayer()`'s body.
  - `unload()`: after `for (auto& m : muted) m = false;`, add `for (auto& v : volumes) v = 1.0f;`.
  - After `StemPlayer::solo`, add:
  ```cpp
  void StemPlayer::setVolume (int stem, float level)
  {
      if (juce::isPositiveAndBelow (stem, maxStems))
          volumes[(size_t) stem] = juce::jlimit (0.0f, 1.0f, level);
  }

  float StemPlayer::getVolume (int stem) const
  {
      return juce::isPositiveAndBelow (stem, maxStems) ? volumes[(size_t) stem].load() : 1.0f;
  }

  std::vector<float> StemPlayer::mixGains() const
  {
      std::vector<float> g ((size_t) files.size());
      for (size_t i = 0; i < g.size(); ++i)
          g[i] = muted[i].load() ? 0.0f : volumes[i].load();
      return g;
  }
  ```
  - In `addTo`, change the line `const float to = want && ! muted[(size_t) i].load (std::memory_order_relaxed) ? 1.0f : 0.0f;` to:
  ```cpp
              const float to = want && ! muted[(size_t) i].load (std::memory_order_relaxed)
                                 ? volumes[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
  ```
    and its comment above to `// A mute or volume change (or the one faded block after pause()) ramps across this block: heard at once, never a click.`.
  - Replace the whole `StemPlayer::render` function with:
  ```cpp
  bool StemPlayer::render (const juce::Array<juce::File>& stems, const std::vector<float>& gains, double startSec, double endSec,
                           double speed, const juce::File& dest, const std::function<bool (float)>& onProgress)
  {
      if (dest.existsAsFile()) return true;            // renders are cached by name (FL keeps pointing at them)
      if (! juce::exactlyEqual (speed, 1.0)) return false;   // stretched renders are not supported yet
      juce::AudioFormatManager fm;
      fm.registerBasicFormats();
      juce::OwnedArray<juce::AudioFormatReader> readers;
      std::vector<float> levels;
      for (int i = 0; i < stems.size() && i < (int) gains.size(); ++i)
          if (gains[(size_t) i] > 0.0f)
          {
              std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (stems[i]));
              if (r == nullptr) return false;
              readers.add (r.release());
              levels.push_back (gains[(size_t) i]);
          }
      if (readers.isEmpty()) return false;

      const double rate = readers[0]->sampleRate;
      const auto first = (juce::int64) (startSec * rate), last = (juce::int64) (endSec * rate);
      if (last <= first) return false;

      // Written next to dest, then renamed: a failed or cancelled render never leaves a file that FL could reference.
      dest.getParentDirectory().createDirectory();
      const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
      part.deleteFile();
      bool ok = false;
      {
          auto out = part.createOutputStream();
          std::unique_ptr<juce::AudioFormatWriter> w (out != nullptr ? juce::WavAudioFormat().createWriterFor (out.get(), rate, 2, 24, {}, 0)
                                                                     : nullptr);
          if (w != nullptr)
          {
              out.release();   // the writer owns the stream now
              ok = writeMix (*w, readers, levels, first, last, onProgress);
          }
      }
      if (! ok)
      {
          part.deleteFile();
          return false;
      }
      return part.moveFileTo (dest);
  }

  juce::String StemPlayer::renderName (const juce::String& songFile, const juce::StringArray& stemNames,
                                       const std::vector<float>& gains, double speed, juce::Range<double> loopSeconds)
  {
      juce::StringArray parts;
      for (int i = 0; i < stemNames.size() && i < (int) gains.size(); ++i)
      {
          const float g = gains[(size_t) i];
          if (g > 0.0f)
              parts.add (g < 1.0f ? stemNames[i] + " " + juce::String (juce::roundToInt (g * 100.0f)) : stemNames[i]);
      }
      if (parts.isEmpty()) return {};
      auto clock = [] (double sec)
      {
          const auto cs = juce::jmax (0, (int) (sec * 100));
          return juce::String (cs / 6000) + "." + juce::String (cs / 100 % 60).paddedLeft ('0', 2) + "." + juce::String (cs % 100).paddedLeft ('0', 2);
      };
      auto name = songFile.upToLastOccurrenceOf (".", false, false) + " - " + parts.joinIntoString ("+");
      if (! juce::exactlyEqual (speed, 1.0)) name << " x" << juce::String (speed, 2);
      if (! loopSeconds.isEmpty()) name << " (" << clock (loopSeconds.getStart()) << "-" << clock (loopSeconds.getEnd()) << ")";
      return juce::File::createLegalFileName (name + ".wav");
  }
  ```

- [ ] **Step 5: Implement it in the editor.**
  - `Source/PluginEditor.h:43`: replace the `renderFile` declaration with:
  ```cpp
      juce::File renderFile (const std::vector<float>& gains, double speed);   // those stems at those gains over the loop (or the song), cached
  ```
  - `Source/PluginEditor.cpp:39-40`: replace both lines with:
  ```cpp
      stems.mixFile = [this] { return renderFile (proc.player.mixGains(), 1.0); };
      stems.stemFile = [this] (int i)   // single stems are the originals: gain 1, speed 1
      {
          if (! proc.player.isLooping()) return stems.fileOf (i);
          std::vector<float> one ((size_t) proc.player.getFiles().size(), 0.0f);
          if (juce::isPositiveAndBelow (i, (int) one.size())) one[(size_t) i] = 1.0f;
          return renderFile (one, 1.0);
      };
  ```
  - `Source/PluginEditor.cpp:253-271`: replace the whole `renderFile` with:
  ```cpp
  juce::File StemSplitterEditor::renderFile (const std::vector<float>& gains, double speed)
  {
      auto& p = proc.player;
      if (! shown.has_value()) return {};
      const auto files = p.getFiles();
      juce::StringArray names;
      for (const auto& f : files) names.add (f.getFileNameWithoutExtension());
      const auto len = p.getLengthSeconds();
      const auto loopSec = p.isLooping() ? juce::Range<double> (p.getLoop().getStart() * len, p.getLoop().getEnd() * len)
                                         : juce::Range<double>();
      const auto name = StemPlayer::renderName (shown->songName, names, gains, speed, loopSec);
      if (name.isEmpty()) return {};
      const auto dest = stems.getDir().getChildFile ("renders").getChildFile (name);
      const auto from = p.isLooping() ? loopSec.getStart() : 0.0, to = p.isLooping() ? loopSec.getEnd() : len;
      // ponytail: renders on the drag gesture (a loop takes milliseconds, a whole song under a second). Upgrade path: render
      // in the background whenever the mutes or the loop change.
      return StemPlayer::render (files, gains, from, to, speed, dest) ? dest : juce::File();
  }
  ```

- [ ] **Step 6: Run the tests and watch them pass.**
  ```powershell
  cmake --build build --config Release --parallel --target StemSplitter_All lisn_tests progress_test -- /nodeReuse:false
  ctest --test-dir build -C Release --output-on-failure
  ```
  Expected: `100% tests passed out of 2`. Every existing StemPlayer case is still green.

- [ ] **Step 7: Commit.**
  ```powershell
  git add Source/StemPlayer.h Source/StemPlayer.cpp Source/PluginEditor.h Source/PluginEditor.cpp tests/StemPlayerTests.cpp
  git commit -m "feat: per-stem volume; render takes gains, progress and cancel; cache names encode volumes and speed" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
  ```

### Task 2: Live speed that keeps the key (Signalsmith Stretch)

**Files:**
- Modify `CMakeLists.txt`, `Source/StemPlayer.h`, `Source/StemPlayer.cpp`, `installer/THIRD-PARTY-NOTICES.txt`.
- Modify the spec: one bullet, see Step 6.
- Test: `tests/StemPlayerTests.cpp`.

**Interfaces:**
- **Consumes:** Task 1's `setVolume`, `volumes`, and the `to = ... volumes ...` line in `addTo`.
- **Produces:**
  - `void StemPlayer::setSpeed (double)`: clamps to 0.5..1.5.
  - `double StemPlayer::getSpeed() const`.
  - The CMake target `signalsmith-stretch`, linked to `StemSplitter` and `lisn_tests`; the header is `<signalsmith-stretch/signalsmith-stretch.h>`.
  - `struct StemPlayer::Stretch { signalsmith::stretch::SignalsmithStretch<float> st; };`, defined in `StemPlayer.cpp`, which Task 3 also uses.

- [ ] **Step 1: Write the failing tests.** In `tests/StemPlayerTests.cpp`:
  1. In the "no stall with a live audio thread" block, right before `expectLessThan (msTaken ([&] { expect (p.load ({ sine, sine })); }), 100.0, "load");`, add:
  ```cpp
          expectLessThan (msTaken ([&] { p.setSpeed (1.2); }), 20.0, "speed");
          expectLessThan (msTaken ([&] { p.setVolume (0, 0.5f); }), 20.0, "volume");
          juce::Thread::sleep (30);                            // a few stretched blocks run on the audio thread
  ```
  2. In the "load resets the volumes" block from Task 1, rename the `beginTest` text to `"load resets the volumes and the speed"`, and add `p.setSpeed (1.3);` before `expect (p.load ({ sine }));` and `expectEquals (p.getSpeed(), 1.0);` after it.
  3. Right after that block, add:
  ```cpp
          beginTest ("speed keeps the key and moves the playhead faster");
          p.prepare (48000, 512);
          expect (p.load ({ sine }));
          juce::Thread::sleep (100);                           // the read-ahead fills (40 stretched blocks read ~25600 samples)
          p.setSpeed (2.0);                                    // clamped
          expectEquals (p.getSpeed(), 1.5);
          p.setSpeed (1.25);
          p.play();
          juce::AudioBuffer<float> heard (1, 512 * 40);
          for (int k = 0; k < 40; ++k)
          {
              pull (0.0f);
              heard.copyFrom (0, k * 512, buf, 0, 0, 512);
              if (k % 10 == 9) juce::Thread::sleep (10);       // lets the read-ahead keep up with the faster reads
          }
          expectWithinAbsoluteError (p.getPositionFraction() * p.getLengthSeconds(), 40 * 512 / 48000.0 * 1.25, 0.02);
          {
              const int from = 9600, len = 40 * 512 - from;        // after the stretcher's ~60 ms fade-in
              bool finite = true;
              for (int i = from; i < from + len; ++i) finite = finite && std::isfinite (heard.getSample (0, i));
              expect (finite, "NaN or inf in the stretched output");
              const float level = rms (heard, from, len);
              expect (level > 0.354f * 0.891f && level < 0.354f * 1.122f, "rms " + juce::String (level));   // within 1 dB
              int crossings = 0;
              for (int i = from + 1; i < from + len; ++i)
                  crossings += (heard.getSample (0, i - 1) < 0.0f) != (heard.getSample (0, i) < 0.0f);
              expectWithinAbsoluteError (crossings / 2.0 / (len / 48000.0), 440.0, 4.4);
          }

          beginTest ("pause fades the stretched output");
          p.pause();
          pull (0.0f);                                         // the fade-out block
          expectLessThan (rms (buf, 384, 128), rms (buf, 0, 128));
          pull (0.0f);
          expect (allEqual (buf, 0.0f));

          beginTest ("at 1x after a seek the stems play directly again");
          p.setSpeed (1.0);
          p.setPositionFraction (0.0);
          p.play();
          juce::Thread::sleep (30);
          pull (0.0f);
          pull (0.0f);                                         // stretched, this block would still be in the ~60 ms fade-in
          expectGreaterThan (rms (buf, 0, 512), 0.3f);
          p.pause();
          pull (0.0f);
  ```
  `std::isfinite` needs `#include <cmath>`: add it after `#include <algorithm>`.

- [ ] **Step 2: Run them and watch them fail.**
  ```powershell
  cmake --build build --config Release --target lisn_tests -- /nodeReuse:false
  ```
  Expected: `'setSpeed': is not a member of 'StemPlayer'`.

- [ ] **Step 3: Add the dependency.**
  - In `CMakeLists.txt`, right after `FetchContent_MakeAvailable(JUCE)`, add:
  ```cmake
  # Signalsmith Stretch (MIT): keep-the-key time-stretch for the speed control. Linear (MIT) is its FFT library, declared
  # first at a pinned commit so Stretch's own CMakeLists uses this one instead of fetching its tag.
  FetchContent_Declare(signalsmith-linear GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
      GIT_TAG de55e6a50ffcf6f8f43f649692d94691c7025151)   # 0.6.4
  FetchContent_Declare(signalsmith-stretch GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
      GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df)   # 1.4.0
  FetchContent_MakeAvailable(signalsmith-linear signalsmith-stretch)
  ```
  - Add `signalsmith-stretch` to both link lists:
    - `target_link_libraries(StemSplitter PRIVATE LisnFonts juce::juce_audio_utils signalsmith-stretch ...)`;
    - `target_link_libraries(lisn_tests PRIVATE LisnFonts juce::juce_audio_utils signalsmith-stretch ...)`.
  - Append to `installer/THIRD-PARTY-NOTICES.txt`, before the Microsoft Visual C++ section:
  ```text
  ================================================================================
  Signalsmith Stretch 1.4.0 (part of the plugin and the desktop app)
  ================================================================================
  MIT License

  Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ================================================================================
  Signalsmith Linear 0.6.4 (part of the plugin and the desktop app)
  ================================================================================
  MIT License

  Copyright (c) 2025 Signalsmith Audio

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ```
  After the first configure, check both copyright lines against the fetched `build\_deps\signalsmith-stretch-src\LICENSE.txt` and `build\_deps\signalsmith-linear-src\LICENSE.txt`, and use the fetched line if one differs.

- [ ] **Step 4: Implement it in `Source/StemPlayer.h`.**
  - After `float takePeak() ...`, add:
  ```cpp
      // Playback speed 0.5..1.5 (clamped) that keeps the key. At exactly 1 the stems play directly; otherwise every block
      // goes through a time-stretcher, which stays engaged until the next play() or seek. load() resets it to 1.
      void setSpeed (double);
      double getSpeed() const { return speed.load(); }
  ```
  - In the private members, after `class Stack;`, add `struct Stretch;                             // the time-stretcher (StemPlayer.cpp), configured in prepare()`.
  - Change `juce::AudioBuffer<float> scratch, mix;      // 2 * maxStems channels / stereo, sized in prepare()` to:
  ```cpp
      juce::AudioBuffer<float> scratch, mix, mixIn;   // 2 * maxStems channels for 1.5x a block / stereo, a block / stereo, 1.5x
      std::unique_ptr<Stretch> stretch;
      std::atomic<double> speed { 1.0 };
      std::atomic<bool> restretch { false };          // play() and seeks: the next block decides the path again
      bool stretching = false;                        // audio thread: blocks go through the stretcher
      double carry = 0.0;                             // audio thread: the part of an input sample owed to the next block
      float outGain = 1.0f;                           // audio thread: the stretched output's level at the end of the last block
  ```

- [ ] **Step 5: Implement it in `Source/StemPlayer.cpp`.**
  - After `#include "StemPlayer.h"`, add:
  ```cpp
  #if JUCE_MSVC
   #pragma warning (push, 0)
  #endif
  #include <signalsmith-stretch/signalsmith-stretch.h>
  #if JUCE_MSVC
   #pragma warning (pop)
  #endif

  struct StemPlayer::Stretch { signalsmith::stretch::SignalsmithStretch<float> st; };
  ```
  - Constructor: change `StemPlayer::StemPlayer()` to `StemPlayer::StemPlayer() : stretch (std::make_unique<Stretch>())`.
  - `prepare()`: replace its two `setSize` lines with:
  ```cpp
      const int block = juce::jmax (1, maxBlockSize), inMax = (int) std::ceil (block * 1.5) + 1;   // a stretched block reads up to 1.5x
      scratch.setSize (2 * maxStems, inMax);
      mix.setSize (2, block);
      mixIn.setSize (2, inMax);
      stretch->st.presetDefault (2, (float) sampleRate);
      {   // size the stretcher's working buffers now, so resets and blocks on the audio thread never allocate
          juce::AudioBuffer<float> zeros (2, stretch->st.seekLength());
          zeros.clear();
          stretch->st.seek (zeros.getArrayOfReadPointers(), zeros.getNumSamples(), 1.0);
          stretch->st.reset();
      }
      stretching = false;
      carry = 0.0;
      outGain = 1.0f;
  ```
  - `unload()`: after the volumes line, add `speed = 1.0;`.
  - `play()`: add `restretch = true;` right before `wanted = true;`.
  - `seekSource()`: add `restretch = true;` as its last line.
  - After `StemPlayer::mixGains`, add:
  ```cpp
  void StemPlayer::setSpeed (double s)
  {
      speed = juce::jlimit (0.5, 1.5, s);
  }
  ```
  - Replace the whole `StemPlayer::addTo` with:
  ```cpp
  void StemPlayer::addTo (juce::AudioBuffer<float>& buffer)
  {
      const bool want = wanted.load (std::memory_order_relaxed);
      if ((! want && ! sounding) || ! prepared.load (std::memory_order_relaxed)) return;   // idle: nothing else runs
      juce::ScopedNoDenormals noDenormals;
      const int total = buffer.getNumSamples(), stems = stemCount.load (std::memory_order_relaxed);
      const double rate = speed.load (std::memory_order_relaxed);
      if (restretch.exchange (false, std::memory_order_relaxed))
          stretching = false;                                                    // a play or seek: at 1x the stems play directly
      if (! stretching && ! juce::exactlyEqual (rate, 1.0))
      {
          stretch->st.reset();                                                   // engages; its first ~60 ms fade in
          stretching = true;
          carry = 0.0;
          outGain = 1.0f;
      }
      float blockPeak = 0.0f;
      for (int start = 0; start < total; start += mix.getNumSamples())          // chunks if the host exceeds its block size
      {
          const int n = juce::jmin (mix.getNumSamples(), total - start);
          int in = n;
          if (stretching)
          {
              const double owed = n * rate + carry;
              in = juce::jlimit (1, scratch.getNumSamples(), (int) owed);
              carry = owed - in;
          }
          juce::AudioSourceChannelInfo info (&scratch, 0, in);
          transport.getNextAudioBlock (info);
          auto& sum = stretching ? mixIn : mix;
          sum.clear (0, in);
          for (int i = 0; i < stems; ++i)
          {
              // A mute or volume change (or, playing directly, the one faded block after pause()) ramps across this block:
              // heard at once, never a click. Stretched, pause fades the output below instead.
              const float to = (want || stretching) && ! muted[(size_t) i].load (std::memory_order_relaxed)
                                 ? volumes[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
              for (int ch = 0; ch < 2; ++ch)
                  sum.addFromWithRamp (ch, 0, scratch.getReadPointer (2 * i + ch), in, gains[(size_t) i], to);
              gains[(size_t) i] = to;
          }
          if (stretching)
          {
              // ponytail: what is heard trails the playhead by the stretcher's latency (~0.1 s). Upgrade path: report the
              // position minus that latency times the speed.
              stretch->st.process (mixIn.getArrayOfReadPointers(), in, mix.getArrayOfWritePointers(), n);
              const float fade = want ? 1.0f : 0.0f;
              mix.applyGainRamp (0, n, outGain, fade);
              outGain = fade;
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

- [ ] **Step 6: Refine the spec.** In `docs/superpowers/specs/2026-10-01-volume-speed-intro-desktop-look-design.md`, replace the bullet that starts `- **Jumps:** entering or leaving 1.0, a seek and a loop wrap each re-prime the stretcher` with:
  ```markdown
  - **Engaging and seeks:** the stretcher engages the moment the speed leaves 1.0 (reset, so its first ~60 ms fade in from its pre-roll) and stays engaged, even back at 1.0, until the next `play()` or seek; then 1.0 plays directly again. A seek while stretching resets it, so no old audio smears across the jump. A loop wrap is a seamless splice in its input, so it needs no reset. Pause fades the stretched output over one block.
  ```

- [ ] **Step 7: Run the tests and watch them pass.**
  ```powershell
  cmake -S . -B build -A x64
  cmake --build build --config Release --parallel --target StemSplitter_All lisn_tests progress_test -- /nodeReuse:false
  ctest --test-dir build -C Release --output-on-failure
  ```
  - Expected: `100% tests passed out of 2`.
  - **Configure:** the reconfigure fetches both Signalsmith repos. If it fails on the Linear declaration, read the error. Don't swap the commit pins for tags.
  - **Compile:** if MSVC stops with `C1128` (too many sections), add `target_compile_options(StemSplitter PRIVATE /bigobj)` and `target_compile_options(lisn_tests PRIVATE /bigobj)`.
  - **Header warnings:** report any warnings from the Signalsmith headers that the `#pragma warning (push, 0)` doesn't silence.

- [ ] **Step 8: Commit.**
  ```powershell
  git add CMakeLists.txt Source/StemPlayer.h Source/StemPlayer.cpp tests/StemPlayerTests.cpp installer/THIRD-PARTY-NOTICES.txt docs/superpowers/specs/2026-10-01-volume-speed-intro-desktop-look-design.md
  git commit -m "feat: 0.5x-1.5x speed that keeps the key, through Signalsmith Stretch; 1x plays directly as before" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
  ```

### Task 3: Stretched renders (exact length, progress, cancel); the mix renders at the player's speed

**Files:**
- Modify `Source/StemPlayer.h` (the `render` comment), `Source/StemPlayer.cpp`, `Source/PluginEditor.cpp:39`.
- Test: `tests/StemPlayerTests.cpp`.

**Interfaces:**
- **Consumes:**
  - Task 1's `render`, `readMix`, `writeMix` and `renderChunk`;
  - Task 2's `#include <signalsmith-stretch/signalsmith-stretch.h>` and `getSpeed()`.
- **Produces:** `render` with any speed in 0.5..1.5. The output is `llround ((endSec - startSec) * rate / speed)` samples long.

- [ ] **Step 1: Write the failing tests.** In `tests/StemPlayerTests.cpp`, right after the "renderName" block from Task 1, add:
  ```cpp
          beginTest ("render keeps the key at other speeds, at the exact length");
          for (const double s : { 0.75, 1.25 })
          {
              const auto out = dir.getChildFile ("renders").getChildFile ("tone x" + juce::String (s, 2) + ".wav");
              expect (StemPlayer::render ({ sine }, { 1.0f }, 0.0, 2.0, s, out), "render at " + juce::String (s));
              std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (out));
              expect (r != nullptr);
              if (r == nullptr) continue;
              expectWithinAbsoluteError ((double) r->lengthInSamples, 2.0 * 44100.0 / s, 1.0);
              const int len = (int) r->lengthInSamples;
              juce::AudioBuffer<float> got (2, len);
              r->read (&got, 0, len, 0, true, true);
              const int from = len / 4, n = len / 2;           // the middle, away from the edges
              int crossings = 0;
              for (int i = from + 1; i < from + n; ++i)
                  crossings += (got.getSample (0, i - 1) < 0.0f) != (got.getSample (0, i) < 0.0f);
              expectWithinAbsoluteError (crossings / 2.0 / (n / 44100.0), 440.0, 4.4);
              const float level = rms (got, from, n);
              expect (level > 0.354f * 0.891f && level < 0.354f * 1.122f, "rms " + juce::String (level));
          }

          beginTest ("a cancelled stretched render leaves no file");
          const auto cut = dir.getChildFile ("renders").getChildFile ("cut.wav");
          int calls = 0;
          expect (! StemPlayer::render ({ sine }, { 1.0f }, 0.0, 2.0, 1.25, cut, [&] (float) { return ++calls < 2; }));
          expect (! cut.exists() && ! cut.getSiblingFile ("cut.wav.part").exists());
  ```
  `sine` is the 2 s, 440 Hz file from the top of `runTest`. It is 88200 samples, so the stretched body is two chunks and the second progress call cancels.

- [ ] **Step 2: Run them and watch them fail.**
  ```powershell
  cmake --build build --config Release --target lisn_tests -- /nodeReuse:false
  build\lisn_tests_artefacts\Release\lisn_tests.exe
  ```
  Expected: `!!! Test ... failed` under "render keeps the key at other speeds" (render returns false at 0.75 and 1.25).

- [ ] **Step 3: Implement it.**
  - In `Source/StemPlayer.cpp`'s anonymous namespace, after `writeMix`, add:
  ```cpp
      // [first, last) mixed and time-stretched by speed (keeping the key) into w: llround ((last - first) / speed) samples.
      // The stretcher's exact-length recipe (its exact(), a chunk at a time so progress and cancel work): outputSeek over
      // the first outputSeekLength samples, process the rest into all but the tail, then flush the tail.
      bool writeStretched (juce::AudioFormatWriter& w, juce::OwnedArray<juce::AudioFormatReader>& readers, const std::vector<float>& levels,
                           juce::int64 first, juce::int64 last, double speed, double rate, const std::function<bool (float)>& onProgress)
      {
          signalsmith::stretch::SignalsmithStretch<float> st;
          st.presetDefault (2, (float) rate);
          const auto inLen = last - first, outLen = (juce::int64) std::llround ((double) inLen / speed);
          const int seekLen = st.outputSeekLength ((float) speed);
          if (inLen <= seekLen)   // ponytail: shorter than the stretcher's look-ahead (~0.15 s) renders unstretched
              return writeMix (w, readers, levels, first, last, onProgress);
          juce::AudioBuffer<float> sum (2, renderChunk), one (2, renderChunk), out (2, 2 * renderChunk + 2);
          readMix (readers, levels, sum, one, first, seekLen);
          st.outputSeek (sum.getArrayOfReadPointers(), seekLen);
          const auto bodyIn = inLen - seekLen;
          const auto bodyOut = outLen - (juce::int64) (seekLen / speed);   // as exact(): the tail is what flush() returns
          juce::int64 inDone = 0, outDone = 0;
          while (inDone < bodyIn)
          {
              const int n = (int) juce::jmin ((juce::int64) renderChunk, bodyIn - inDone);
              readMix (readers, levels, sum, one, first + seekLen + inDone, n);
              inDone += n;
              const auto target = (juce::int64) std::llround ((double) bodyOut * (double) inDone / (double) bodyIn);
              const int m = (int) (target - outDone);
              st.process (sum.getArrayOfReadPointers(), n, out.getArrayOfWritePointers(), m);
              if (! w.writeFromAudioSampleBuffer (out, 0, m)) return false;
              outDone = target;
              if (onProgress && ! onProgress (0.99f * (float) (seekLen + inDone) / (float) inLen)) return false;
          }
          const int tail = (int) (outLen - outDone);
          st.flush (out.getArrayOfWritePointers(), tail, (float) speed);
          if (! w.writeFromAudioSampleBuffer (out, 0, tail)) return false;
          return ! onProgress || onProgress (1.0f);
      }
  ```
  - In `StemPlayer::render`:
    - delete the line `if (! juce::exactlyEqual (speed, 1.0)) return false;   // stretched renders are not supported yet`;
    - change `ok = writeMix (*w, readers, levels, first, last, onProgress);` to:
  ```cpp
              const double s = juce::jlimit (0.5, 1.5, speed);
              ok = juce::exactlyEqual (s, 1.0) ? writeMix (*w, readers, levels, first, last, onProgress)
                                               : writeStretched (*w, readers, levels, first, last, s, rate, onProgress);
  ```
  - In `Source/StemPlayer.h`, in the `render` comment, replace `A speed other than 1 is not supported yet and returns false.` with `A speed other than 1 (0.5..1.5) time-stretches it, keeping the key, to (endSec - startSec) / speed long.`.
  - In `Source/PluginEditor.cpp`, change `stems.mixFile = [this] { return renderFile (proc.player.mixGains(), 1.0); };` to:
  ```cpp
      stems.mixFile = [this] { return renderFile (proc.player.mixGains(), proc.player.getSpeed()); };   // the mix: what you hear
  ```

- [ ] **Step 4: Run the tests and watch them pass.**
  ```powershell
  cmake --build build --config Release --parallel --target StemSplitter_All lisn_tests progress_test -- /nodeReuse:false
  ctest --test-dir build -C Release --output-on-failure
  ```
  Expected: `100% tests passed out of 2`.

- [ ] **Step 5: Commit.**
  ```powershell
  git add Source/StemPlayer.h Source/StemPlayer.cpp Source/PluginEditor.cpp tests/StemPlayerTests.cpp
  git commit -m "feat: renders time-stretch at the exact length, keeping the key; the dragged mix carries the speed" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
  ```

### Task 4: Bench figure, full verification, push

**Files:** Modify `tests/Snapshots.cpp` (`runBench`).

**Interfaces:** Consumes `StemPlayer::setSpeed`. Produces the bench line `process_block_us_stretch=<us>` and a budget failure above 3200 us.

- [ ] **Step 1: Add the stretch figure.**
  - In `tests/Snapshots.cpp` `runBench()`, right before `proc.player.unload();`, add:
  ```cpp
      // The same bursts through the time-stretcher (1.25x, keeping the key): 30% of a 512-sample block at 48 kHz is 3200 us.
      proc.player.setSpeed (1.25);
      proc.player.setPositionFraction (0.0);
      proc.player.play();
      juce::Thread::sleep (60);
      double stretchTotal = 0.0;
      for (int burst = 0; burst < 20; ++burst)
      {
          for (int i = 0; i < 50; ++i)
          {
              buffer.clear();
              t0 = now();
              proc.processBlock (buffer, midi);
              stretchTotal += (now() - t0) * 1000.0;
          }
          juce::Thread::sleep (60);
      }
      const double stretchUs = stretchTotal / 1000.0;
  ```
  - After the `process_block_us_playing` printf line, add:
  ```cpp
      std::printf ("process_block_us_stretch=%.3f\n", stretchUs);
      if (stretchUs > 3200.0)
          std::puts ("FAIL: process_block_us_stretch is over 30% of a 512-sample block at 48 kHz");
  ```
  - Change the return line to `return metered && motionMs <= 4.0 && stretchUs <= 3200.0 ? 0 : 1;`.
- [ ] **Step 2: Full verification.**
  ```powershell
  cmake --build build --config Release --parallel --target StemSplitter_All lisn_tests progress_test -- /nodeReuse:false
  ctest --test-dir build -C Release --output-on-failure
  C:\Tools\pluginval\pluginval.exe --strictness-level 5 --validate "build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3"
  build\lisn_tests_artefacts\Release\lisn_tests.exe --bench
  ```
  Expected:
  - `100% tests passed`;
  - pluginval ends in `SUCCESS`;
  - the bench exits 0, with `process_block_us_idle` about 0, `motion_frame_ms` at most 4 and `process_block_us_stretch` at most 3200.

  Record the three bench numbers in the report. If `process_block_us_stretch` is over budget, stop and report it. Don't switch to `presetCheaper` without the owner.
- [ ] **Step 3: Commit.**
  ```powershell
  git add tests/Snapshots.cpp
  git commit -m "test: the bench times stretched blocks against a 30% budget" -m "Co-Authored-By: <your model> <noreply@anthropic.com>"
  ```
- [ ] **Step 4 (owner OK): push and run CI on the branch.**
  ```powershell
  git push -u origin feature/engine-volume-speed
  gh workflow run build.yml --ref feature/engine-volume-speed
  ```
  Expected: the build run goes green. CI's "Build" step builds everything, including the Signalsmith fetch.
- [ ] **Step 5:** Merge only with the owner (superpowers:finishing-a-development-branch). Plan 2 (shared UI) builds on this branch's result.

## Verification summary

| Task | Done when |
|---|---|
| 0 | the worktree builds; baseline ctest passes |
| 1 | volume, mixGains, gains in render, progress, cancel and renderName tests pass; old player tests unchanged |
| 2 | speed tests pass (position at 1.25x, no NaN, level within 1 dB, 440 Hz kept, pause fade, 1x direct after a seek); no-stall holds |
| 3 | stretched renders are exact length, keep 440 Hz and the level; a cancel leaves no file |
| 4 | bench within all budgets; pluginval SUCCESS; CI green on the pushed branch |
