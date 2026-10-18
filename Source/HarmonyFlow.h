#pragma once
#include <juce_events/juce_events.h>
#include "SeparationJob.h"
#include "HarmonyJob.h"

// Harmonies' two steps from a song, owned by the processor so they outlive the plugin window: first the normal 4-stem
// split ("Finding the vocals"; instant when the song's stems are cached), then the harmony pass ("Splitting the harmonies").
// Message thread only. A 100 ms timer runs while a step does and moves vocals -> harmonies -> done / failed.
class HarmonyFlow : private juce::Timer
{
public:
    enum class Step { idle, vocals, harmonies, done, failed };

    ~HarmonyFlow() override { cancel(); }

    // quality "quick" / "best". Step 2 is skipped when the song already has a result and keepExistingResult, or when
    // the result already has this quality. Cancels a flow that is running.
    void start (const juce::File& song, const juce::String& quality, const juce::String& pythonHint, bool keepExistingResult = true);
    void cancel();                       // stops the running step, back to idle; stems made so far stay cached

    Step getStep() const { return step; }
    float getProgress() const;           // of the current step, 0..1
    bool isDownloading() const { return harmonyJob.isDownloading(); }   // step 2 is fetching Best's model
    juce::File getSong() const { return song; }
    juce::File getStemDir() const { return stemDir; }
    ErrorKind getErrorKind() const { return errorKind; }    // when failed
    juce::String getError() const { return error; }         // when failed
    // The Python the split found, once it has finished with one (the processor adopts it); else empty.
    juce::String getPythonExe() const;

    HarmonyJob& harmonyJobForTests() { return harmonyJob; }

private:
    void timerCallback() override { advance(); }
    void advance();
    void finish (Step s);

    SeparationJob separation;
    HarmonyJob harmonyJob;
    Step step = Step::idle;
    juce::File song, stemDir;
    juce::String quality, pythonHint, error;
    bool keepExisting = true;
    ErrorKind errorKind = ErrorKind::None;
};
