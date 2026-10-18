#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

// Harmony Split: a second AI pass on a 4-stem split's vocals.wav that writes <stemDir>/harmony/{lead,harmonies}.wav.
// It runs Resources/harmony_split.py (a karaoke model) on the engine's Python, which drops itself to below-normal
// priority on half the cores so the DAW stays responsive. The script writes into harmony.part; only a finished pass is
// renamed to harmony, so a cancel or a crash never leaves a half-written result. The owner polls on the message thread.
// Quick's model comes with the engine; Best quality's (913 MB) is downloaded into userModelDir() the first time.
class HarmonyJob : private juce::Thread
{
public:
    enum class State { idle, running, done, failed };
    HarmonyJob() : juce::Thread ("harmony split") {}
    ~HarmonyJob() override { cancel(); }
    // quality "quick" (MDX-Net Karaoke 2) or "best" (Mel-Band RoFormer karaoke, about 3x the time). Cancels any running pass.
    void start (const juce::File& stemDir, const juce::String& pythonHint, const juce::String& quality = "quick");
    void cancel();                               // kills the script or stops the download, deletes harmony.part; back to idle
    State getState() const { return state.load(); }
    float getProgress() const { return progress.load(); }   // 0..1, of the download while isDownloading(), else of the split
    bool isDownloading() const { return downloading.load(); }
    juce::File getStemDir() const { return stemDir; }        // the current pass's stem dir (message thread)
    juce::String getError() const { return error; }          // when failed: the script's last output, or what went wrong

    static juce::File dirFor (const juce::File& stemDir) { return stemDir.getChildFile ("harmony"); }
    static bool hasResult (const juce::File& stemDir);       // harmony/lead.wav and harmony/harmonies.wav exist
    static juce::String qualityOf (const juce::File& stemDir);   // the result's quality ("quick" / "best"), or "" without one

    // The model file for "quick" / "best" in the engine (beside pythonExe's venv, if known) or in userModelDir(); or none.
    static juce::File findModel (const juce::String& quality, const juce::String& pythonExe);
    static juce::File userModelDir();            // %LOCALAPPDATA%\LISN\models\harmony; ~/Library/Application Support/LISN/models/harmony on Mac
    // Downloads `url` to `dest` through dest.part and keeps it only if its SHA-256 is `sha256`. `progress` gets 0..1 and
    // returns false to stop. False on any failure or a stop; nothing half-done stays.
    static bool download (const juce::URL& url, const juce::String& sha256, const juce::File& dest,
                          const std::function<bool (float)>& progress);

    // Test hook: run this instead of Python and the script, with the .part dir and the quality appended.
    juce::StringArray testCommand;

private:
    void run() override;
    juce::File stemDir;
    juce::String pythonHint, quality, error;
    juce::CriticalSection procLock;
    juce::ChildProcess proc;
    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> downloading { false };
};
