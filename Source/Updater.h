#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "ReleaseInfo.h"

// Downloads a release's installer and runs it, for the update pop-up. Both jobs run on its thread; the state is read and
// changed on the message thread only, results are posted there. Anyone can listen (the editors do); a listener going
// away never stops a download. The tests inject downloadFn and launchFn.
class Updater : public juce::ChangeBroadcaster, public juce::DeletedAtShutdown, private juce::Thread
{
public:
    enum class Step { idle, downloading, ready, installing, installed, waitingForInstaller, failed };
    enum class Failure { none, download, install, inUse, engine };

    Updater();
    ~Updater() override;

    Step getStep() const { return step; }
    Failure getFailure() const { return failure; }
    float getProgress() const { return progress; }
    bool engineChanged() const { return changedEngine; }   // an install in this process changed the engine's ready.txt

    void startDownload (const ReleaseInfo&);   // idle/failed -> downloading; a file already there with a matching hash -> ready at once
    void cancel();                             // downloading -> idle, file and .part deleted
    void install (const juce::File& engineStamp);   // ready -> installing -> installed / waitingForInstaller / failed
    void reset();                              // failed -> idle

    // The installer for this OS, run and waited for; returns Inno's exit code (0, 7, 101), 1223 when the user declined
    // the UAC prompt, -2 when it can't report back (Mac's Installer, the desktop app about to quit), -1 when it can't start.
    static int launchInstaller (const juce::File& installer, bool standalone);

    // Test hooks: the defaults are HarmonyJob::download and launchInstaller
    std::function<bool (const juce::URL&, const juce::String&, const juce::File&, const std::function<bool (float)>&)> downloadFn;
    std::function<int (const juce::File& installer)> launchFn;
    juce::File downloadDir;   // tempDirectory/LISN-update

    JUCE_DECLARE_SINGLETON_SINGLETHREADED_MINIMAL (Updater)

private:
    void run() override;
    void setStep (Step s, Failure f = Failure::none);

    Step step = Step::idle;
    Failure failure = Failure::none;
    float progress = 0.0f;
    bool changedEngine = false;
    int job = 0;   // a posted result for an older job (cancelled, replaced) is dropped
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // posted results outlive a destroyed Updater

    bool installing = false;   // what run() does: install, or download
    ReleaseInfo release;
    juce::File file, stamp;
    juce::String stampBefore;
};
