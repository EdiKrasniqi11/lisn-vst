# Stem Splitter VST3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A VST3 plugin for FL Studio and Ableton on Windows. You drop in an audio file, the plugin runs `demucs` in the background, and it shows the resulting stem WAVs as tiles you drag onto DAW tracks.

**Architecture:** The plugin is written in C++ with JUCE. The processor owns a `SeparationJob`, a background thread that runs `python -m demucs` and parses the tqdm progress output that demucs prints. The editor checks the job's state 30 times per second with a timer. Stems are cached in `%APPDATA%\StemSplitter\cache\<key>\<model>\`. Audio passes through the plugin unchanged.

**Tech Stack:** JUCE 8.0.4 (pulled in with CMake FetchContent), CMake ≥ 3.22, MSVC (Visual Studio 2022), Python 3.11 with `demucs`, and pluginval.

## Global Constraints
- VST3 only, Windows only. No AU, AAX, or Mac build.
- The engine is the external `python -m demucs` command. Nothing is compiled into the plugin for ML.
- Default model: `htdemucs` (vocals, drums, bass, other). Optional model: `htdemucs_6s` (adds guitar and piano).
- Cache location: `%APPDATA%\StemSplitter\cache\<key>\`.
- Stems are delivered as WAV files dragged out of the plugin. There is no playback inside the plugin.
- Must pass `pluginval --strictness-level 5`.

## File Structure
```
CMakeLists.txt                 JUCE fetch, plugin target, test target
Source/DemucsProgress.h        pure function: parse percent from demucs output (no JUCE)
Source/SeparationJob.h/.cpp    background thread running demucs
Source/PluginProcessor.h/.cpp  owns job + persisted settings/state
Source/PluginEditor.h/.cpp     drop zone, model/python settings, progress, cancel, stem tiles
tests/progress_test.cpp        assert-based test for DemucsProgress.h
```

---

### Task 1: Toolchain + empty plugin loads

**Files:**
- Create: `CMakeLists.txt`, `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/PluginEditor.h`, `Source/PluginEditor.cpp`, `.gitignore`

**Interfaces:**
- Produces: `StemSplitterProcessor` (a subclass of `juce::AudioProcessor`) and `StemSplitterEditor` (a subclass of `juce::AudioProcessorEditor`). Later tasks extend both.

- [ ] **Step 1: Install tools (user runs these)**

```bash
winget install Kitware.CMake
python -m pip install demucs
```
Install Visual Studio 2022 Community with the "Desktop development with C++" workload. Download `pluginval_Windows.zip` from https://github.com/Tracktion/pluginval/releases and unzip it to `C:\tools\pluginval\`.

- [ ] **Step 2: Check that demucs works from the command line**

Run: `python -m demucs -n htdemucs -o out --filename "{stem}.{ext}" "C:\path\to\song.mp3"`
Expected: four files in `out\htdemucs\`: `vocals.wav`, `drums.wav`, `bass.wav`, `other.wav`. Save the console output to `tests/demucs_sample_output.txt`. Task 2 uses it as test data. Then delete `out\`.

- [ ] **Step 3: Write `.gitignore`**

```
build/
out/
```

- [ ] **Step 4: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(StemSplitter VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(StemSplitter
    COMPANY_NAME "lisn"
    PLUGIN_MANUFACTURER_CODE Lisn
    PLUGIN_CODE Stsp
    FORMATS VST3
    PRODUCT_NAME "StemSplitter"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{USERPROFILE}/VST3")   # no admin needed; add this folder to the DAW's scan paths

target_sources(StemSplitter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(StemSplitter PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemSplitter
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
```

- [ ] **Step 5: Write `Source/PluginProcessor.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class StemSplitterProcessor : public juce::AudioProcessor
{
public:
    StemSplitterProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {} // pass-through

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "StemSplitter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterProcessor)
};
```

- [ ] **Step 6: Write `Source/PluginProcessor.cpp`**

```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorEditor* StemSplitterProcessor::createEditor() { return new StemSplitterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StemSplitterProcessor(); }
```

- [ ] **Step 7: Write `Source/PluginEditor.h` and `Source/PluginEditor.cpp`**

```cpp
// PluginEditor.h
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class StemSplitterEditor : public juce::AudioProcessorEditor
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor& p) : AudioProcessorEditor (p) { setSize (520, 360); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1e24));
        g.setColour (juce::Colours::white);
        g.drawText ("StemSplitter", getLocalBounds(), juce::Justification::centred);
    }
};
```
```cpp
// PluginEditor.cpp
#include "PluginEditor.h"
```

- [ ] **Step 8: Build**

Run: `cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Debug`
Expected: the build succeeds and `%USERPROFILE%\VST3\StemSplitter.vst3` exists.

- [ ] **Step 9: Build AudioPluginHost and load the plugin**

Run: `cmake -B build -DJUCE_BUILD_EXTRAS=ON && cmake --build build --config Debug --target AudioPluginHost`
Open the host from `build\_deps\juce-build\extras\AudioPluginHost\AudioPluginHost_artefacts\Debug\AudioPluginHost.exe`. Choose Options → Edit list of available plugins → Options → Scan for new VST3 plugins, and add `%USERPROFILE%\VST3` to the scan paths. Insert StemSplitter and open its window.
Expected: a dark window that says "StemSplitter".

- [ ] **Step 10: Run pluginval**

Run: `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\StemSplitter.vst3"`
Expected: `SUCCESS`.

- [ ] **Step 11: Commit**

```bash
git add .gitignore CMakeLists.txt Source tests/demucs_sample_output.txt
git commit -m "feat: empty StemSplitter VST3 builds and validates"
```

---

### Task 2: Demucs progress parser (TDD)

**Files:**
- Create: `Source/DemucsProgress.h`, `tests/progress_test.cpp`
- Modify: `CMakeLists.txt` (add the test target)

**Interfaces:**
- Produces: `int parseDemucsProgress (const std::string& output)`. It returns the last tqdm percentage (0–100) found in `output`, or `-1` if there is none.

Demucs shows progress with tqdm, which prints lines like ` 45%|████▌     | 26.3/58.5 [00:10<00:12, 2.5seconds/s]` separated by `\r`.

- [ ] **Step 1: Write the failing test `tests/progress_test.cpp`**

```cpp
#include "../Source/DemucsProgress.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <cstdio>

int main()
{
    assert (parseDemucsProgress ("") == -1);
    assert (parseDemucsProgress ("Separating track song.mp3\n") == -1);
    assert (parseDemucsProgress ("  0%|          | 0.0/58.5 [00:00<?, ?seconds/s]") == 0);
    assert (parseDemucsProgress (" 12%|#  | 7/58\r 45%|####  | 26/58 [00:10<00:12]") == 45);
    assert (parseDemucsProgress ("100%|##########| 58.5/58.5 [00:22<00:00]\n") == 100);
    assert (parseDemucsProgress (" 45%|####  | 26/58\rrate 50% done") == 45); // "%" without "|" ignored

    // real captured output (Task 1, Step 2) must end at 100
    std::ifstream f (SAMPLE_OUTPUT_PATH, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    assert (parseDemucsProgress (ss.str()) == 100);

    std::puts ("progress_test OK");
    return 0;
}
```

- [ ] **Step 2: Add the test target to `CMakeLists.txt`** (at the end of the file)

```cmake
enable_testing()
add_executable(progress_test tests/progress_test.cpp)
target_compile_definitions(progress_test PRIVATE
    SAMPLE_OUTPUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/demucs_sample_output.txt")
add_test(NAME progress_test COMMAND progress_test)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build --config Debug --target progress_test`
Expected: the build FAILS with `cannot open include file 'DemucsProgress.h'`.

- [ ] **Step 4: Write `Source/DemucsProgress.h`**

```cpp
#pragma once
#include <string>
#include <cctype>

// Returns the last tqdm percentage ("NN%|") in demucs output, or -1 if none.
inline int parseDemucsProgress (const std::string& output)
{
    for (auto pos = output.rfind ("%|"); pos != std::string::npos && pos > 0;
         pos = output.rfind ("%|", pos - 1))
    {
        auto start = pos;
        while (start > 0 && std::isdigit ((unsigned char) output[start - 1]))
            --start;
        if (start < pos)
            return std::stoi (output.substr (start, pos - start));
    }
    return -1;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --config Debug --target progress_test && ctest --test-dir build -C Debug --output-on-failure`
Expected: `100% tests passed`.

- [ ] **Step 6: Commit**

```bash
git add Source/DemucsProgress.h tests/progress_test.cpp CMakeLists.txt
git commit -m "feat: parse demucs tqdm progress"
```

---

### Task 3: SeparationJob + processor state

**Files:**
- Create: `Source/SeparationJob.h`, `Source/SeparationJob.cpp`
- Modify: `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `CMakeLists.txt` (add `Source/SeparationJob.cpp` to `target_sources`)

**Interfaces:**
- Consumes: `parseDemucsProgress` (Task 2).
- Produces:
  - `enum class JobState { Idle, Running, Done, Failed, Cancelled }`
  - `SeparationJob::start (juce::File input, juce::String model, juce::String python)` begins a run. It returns immediately.
  - `SeparationJob::cancel()` kills the process.
  - `JobState SeparationJob::getState() const`
  - `double SeparationJob::getProgress() const` (0.0–1.0)
  - `juce::String SeparationJob::getError() const`. Read it only when the state is `Failed`.
  - `juce::File SeparationJob::getStemDir() const`. Read it only when the state is `Done`.
  - `static juce::File SeparationJob::stemDirFor (juce::File input, juce::String model)`
  - `static juce::Array<juce::File> SeparationJob::stemsIn (juce::File dir)`
  - On `StemSplitterProcessor`: `SeparationJob job;`, `juce::String model = "htdemucs";`, `juce::String pythonPath = "python";`, `juce::File lastInput;`, `juce::File stemDir;`

- [ ] **Step 1: Write `Source/SeparationJob.h`**

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

enum class JobState { Idle, Running, Done, Failed, Cancelled };

class SeparationJob : private juce::Thread
{
public:
    SeparationJob() : Thread ("demucs") {}
    ~SeparationJob() override { cancel(); }

    void start (juce::File input, juce::String model, juce::String python);
    void cancel() { stopThread (5000); }

    JobState getState() const      { return state.load(); }
    double getProgress() const     { return progress.load(); }
    juce::String getError() const  { return error; }   // valid when Failed
    juce::File getStemDir() const  { return outDir; }  // valid when Done

    // cache key = path + size + mtime, so edited files re-separate
    static juce::File stemDirFor (juce::File input, juce::String model);
    static juce::Array<juce::File> stemsIn (juce::File dir);

private:
    void run() override;

    juce::File inputFile, outDir;
    juce::String modelName, pythonExe, error;
    std::atomic<JobState> state { JobState::Idle };
    std::atomic<double> progress { 0.0 };
};
```

- [ ] **Step 2: Write `Source/SeparationJob.cpp`**

```cpp
#include "SeparationJob.h"
#include "DemucsProgress.h"

juce::File SeparationJob::stemDirFor (juce::File input, juce::String model)
{
    auto key = juce::String::toHexString ((input.getFullPathName()
                    + juce::String (input.getSize())
                    + juce::String (input.getLastModificationTime().toMilliseconds())).hashCode64());
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("StemSplitter/cache").getChildFile (key).getChildFile (model);
}

juce::Array<juce::File> SeparationJob::stemsIn (juce::File dir)
{
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();
    return files;
}

void SeparationJob::start (juce::File input, juce::String model, juce::String python)
{
    cancel();
    inputFile = input; modelName = model; pythonExe = python;
    outDir = stemDirFor (input, model);
    error.clear();
    progress = 0.0;

    if (! stemsIn (outDir).isEmpty()) { progress = 1.0; state = JobState::Done; return; } // cache hit

    state = JobState::Running;
    startThread();
}

void SeparationJob::run()
{
    auto root = outDir.getParentDirectory();   // demucs writes <root>/<model>/{stem}.wav
    root.createDirectory();

    juce::ChildProcess proc;
    juce::StringArray cmd { pythonExe, "-m", "demucs", "-n", modelName,
                            "-o", root.getFullPathName(), "--filename", "{stem}.{ext}",
                            inputFile.getFullPathName() };

    if (! proc.start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        error = "Could not start Python (\"" + pythonExe + "\"). Set the Python path in settings.";
        state = JobState::Failed;
        return;
    }

    std::string output;
    char buf[1024];
    for (;;)
    {
        if (threadShouldExit())
        {
            proc.kill();
            outDir.deleteRecursively();       // don't cache a half-written result
            state = JobState::Cancelled;
            return;
        }
        auto n = proc.readProcessOutput (buf, sizeof (buf));
        if (n > 0)
        {
            output.append (buf, (size_t) n);
            if (auto p = parseDemucsProgress (output); p >= 0)
                progress = p / 100.0;
        }
        else if (! proc.isRunning())
            break;
        else
            wait (50);
    }

    if (proc.getExitCode() != 0 || stemsIn (outDir).isEmpty())
    {
        auto tail = juce::String (output).trim();
        error = "demucs failed:\n" + tail.substring (juce::jmax (0, tail.length() - 600));
        outDir.deleteRecursively();
        state = JobState::Failed;
        return;
    }
    progress = 1.0;
    state = JobState::Done;
}
```

- [ ] **Step 3: Update `Source/PluginProcessor.h`**

Add `#include "SeparationJob.h"` below the existing include. Replace the two state method stubs with declarations, and add public members above `private:`:

```cpp
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    SeparationJob job;
    juce::String model = "htdemucs";
    juce::String pythonPath = "python";
    juce::File lastInput;   // last dropped file
    juce::File stemDir;     // folder of finished stems, empty if none
```

- [ ] **Step 4: Add state persistence to `Source/PluginProcessor.cpp`**

```cpp
void StemSplitterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree s ("StemSplitter");
    s.setProperty ("model", model, nullptr);
    s.setProperty ("python", pythonPath, nullptr);
    s.setProperty ("input", lastInput.getFullPathName(), nullptr);
    s.setProperty ("stems", stemDir.getFullPathName(), nullptr);
    if (auto xml = s.createXml()) copyXmlToBinary (*xml, dest);
}

void StemSplitterProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto s = juce::ValueTree::fromXml (*xml);
    model      = s.getProperty ("model", "htdemucs").toString();
    pythonPath = s.getProperty ("python", "python").toString();
    auto in = s.getProperty ("input").toString();
    auto st = s.getProperty ("stems").toString();
    lastInput = juce::File::isAbsolutePath (in) ? juce::File (in) : juce::File();
    stemDir   = juce::File::isAbsolutePath (st) ? juce::File (st) : juce::File();
}
```

- [ ] **Step 5: Add `Source/SeparationJob.cpp` to `target_sources` in `CMakeLists.txt`, then build**

Run: `cmake --build build --config Debug`
Expected: the build succeeds.

- [ ] **Step 6: Run pluginval (it checks state save and restore)**

Run: `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\StemSplitter.vst3"`
Expected: `SUCCESS`.

- [ ] **Step 7: Commit**

```bash
git add Source CMakeLists.txt
git commit -m "feat: background demucs job with cache and persisted state"
```

---

### Task 4: Editor UI (drop zone, settings, progress, cancel, stem tiles)

**Files:**
- Modify: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`

**Interfaces:**
- Consumes: everything Task 3 produces.

- [ ] **Step 1: Replace `Source/PluginEditor.h`**

```cpp
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// One draggable stem file; drag it onto a DAW track.
class StemTile : public juce::Component
{
public:
    explicit StemTile (juce::File f) : file (f) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff3a3a48));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2), 6.0f);
        g.setColour (juce::Colours::white);
        g.drawText (file.getFileNameWithoutExtension(), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent&) override
    {
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }
private:
    juce::File file;
};

class StemSplitterEditor : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void showStems (juce::File dir);

    StemSplitterProcessor& proc;
    juce::ComboBox modelBox;
    juce::TextEditor pythonField;
    juce::TextButton cancelButton { "Cancel" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label status;
    juce::OwnedArray<StemTile> tiles;
    JobState lastState = JobState::Idle;
};
```

- [ ] **Step 2: Replace `Source/PluginEditor.cpp`**

```cpp
#include "PluginEditor.h"

StemSplitterEditor::StemSplitterEditor (StemSplitterProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    modelBox.addItem ("htdemucs", 1);
    modelBox.addItem ("htdemucs_6s", 2);
    modelBox.setText (proc.model, juce::dontSendNotification);
    modelBox.onChange = [this] { proc.model = modelBox.getText(); };
    addAndMakeVisible (modelBox);

    pythonField.setText (proc.pythonPath, false);
    pythonField.setTooltip ("Python executable (default: python on PATH)");
    pythonField.onTextChange = [this] { proc.pythonPath = pythonField.getText().trim(); };
    addAndMakeVisible (pythonField);

    cancelButton.onClick = [this] { proc.job.cancel(); };
    addChildComponent (cancelButton);
    addChildComponent (progressBar);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (status);

    if (proc.stemDir.isDirectory()) showStems (proc.stemDir);
    else status.setText ("Drop an audio file here", juce::dontSendNotification);

    setSize (520, 360);
    startTimerHz (30);
}

void StemSplitterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e24));
}

void StemSplitterEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (28);
    modelBox.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (8);
    pythonField.setBounds (top);
    r.removeFromTop (12);

    auto bar = r.removeFromTop (24);
    cancelButton.setBounds (bar.removeFromRight (80));
    progressBar.setBounds (bar.withTrimmedRight (8));
    r.removeFromTop (8);
    status.setBounds (r.removeFromTop (60));

    if (tiles.isEmpty()) return;
    auto w = r.getWidth() / tiles.size();
    for (auto* t : tiles) t->setBounds (r.removeFromLeft (w).withHeight (80));
}

bool StemSplitterEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg");
}

void StemSplitterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    tiles.clear();
    proc.lastInput = juce::File (files[0]);
    proc.stemDir = juce::File();
    proc.job.start (proc.lastInput, proc.model, proc.pythonPath);
    lastState = JobState::Idle;   // force a UI refresh on the next tick
}

void StemSplitterEditor::timerCallback()
{
    auto s = proc.job.getState();
    progressValue = proc.job.getProgress();
    if (s == lastState) return;
    lastState = s;

    const bool running = s == JobState::Running;
    progressBar.setVisible (running);
    cancelButton.setVisible (running);

    switch (s)
    {
        case JobState::Running:
            status.setText ("Separating " + proc.lastInput.getFileName() + "...", juce::dontSendNotification);
            break;
        case JobState::Done:
            proc.stemDir = proc.job.getStemDir();
            showStems (proc.stemDir);
            break;
        case JobState::Failed:
            status.setText (proc.job.getError(), juce::dontSendNotification);
            break;
        case JobState::Cancelled:
            status.setText ("Cancelled. Drop a file to try again.", juce::dontSendNotification);
            break;
        case JobState::Idle: break;
    }
}

void StemSplitterEditor::showStems (juce::File dir)
{
    tiles.clear();
    for (auto& f : SeparationJob::stemsIn (dir))
        addAndMakeVisible (tiles.add (new StemTile (f)));
    status.setText (tiles.isEmpty() ? "Stems missing. Drop the file again."
                                    : "Drag stems onto your tracks", juce::dontSendNotification);
    resized();
}
```

- [ ] **Step 3: Build and try it in AudioPluginHost**

Run: `cmake --build build --config Debug`
In AudioPluginHost, open the plugin window and drop in a short song. Expected: a progress bar climbs and then 4 tiles appear. Drop the same song again: the tiles appear instantly from the cache. Set the Python path to `nope` and drop a file: an error saying Python couldn't start. Drop a long song and click Cancel: the status reads "Cancelled…". Close the plugin mid-run: `python.exe` disappears from Task Manager.

- [ ] **Step 4: Run pluginval**

Run: `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\StemSplitter.vst3"`
Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add Source
git commit -m "feat: editor with drop zone, progress, cancel and draggable stem tiles"
```

---

### Task 5: DAW verification + stem quality check

**Files:**
- Create: `tests/sum_check.py`

- [ ] **Step 1: Write `tests/sum_check.py`**

```python
# Usage: python tests/sum_check.py <original> <stem_dir>
# The sum of the stems should reconstruct the original (residual well below the signal).
import sys, pathlib, torch, torchaudio

orig, sr = torchaudio.load(sys.argv[1])
stems = [torchaudio.load(p)[0] for p in sorted(pathlib.Path(sys.argv[2]).glob("*.wav"))]
assert stems, "no stems found"
total = sum(stems)
if sr != 44100:
    orig = torchaudio.functional.resample(orig, sr, 44100)
n = min(orig.shape[-1], total.shape[-1])
residual = (orig[..., :n] - total[..., :n]).pow(2).mean()
snr = 10 * torch.log10(orig[..., :n].pow(2).mean() / residual)
print(f"reconstruction SNR: {snr:.1f} dB")
assert snr > 20, "stems do not sum back to the original"
print("sum_check OK")
```

- [ ] **Step 2: Run it on a separated song**

Run: `python tests/sum_check.py "C:\path\to\song.mp3" "%APPDATA%\StemSplitter\cache\<key>\htdemucs"`
Expected: `sum_check OK`.

- [ ] **Step 3: Test in FL Studio**

Go to Options → Manage plugins → add `%USERPROFILE%\VST3` → Find plugins. Load StemSplitter on a mixer insert and drop in a song. When the 4 tiles appear, drag each one into the playlist. Play: the stems should be in sync. Save the project, close FL, and reopen it: the tiles should still be there.

- [ ] **Step 4: Test in Ableton Live**

Go to Settings → Plug-Ins → turn on VST3 custom folder → `%USERPROFILE%\VST3` → Rescan. Repeat Step 3, dragging the tiles onto audio tracks.

- [ ] **Step 5: Build a release version and validate it**

Run: `cmake --build build --config Release && C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\StemSplitter.vst3"`
Expected: `SUCCESS`.

- [ ] **Step 6: Commit**

```bash
git add tests/sum_check.py
git commit -m "test: stem reconstruction check"
```
