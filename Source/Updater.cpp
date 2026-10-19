#include "Updater.h"
#include "HarmonyJob.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>

JUCE_IMPLEMENT_SINGLETON (Updater)

Updater::Updater() : juce::Thread ("LISN updater"),
    downloadFn (&HarmonyJob::download),
    launchFn ([] (const juce::File& f)
              { return launchInstaller (f, juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone); }),
    downloadDir (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LISN-update"))
{
}

Updater::~Updater()
{
    *alive = false;
    signalThreadShouldExit();
    stopThread (10000);
    clearSingletonInstance();
}

void Updater::setStep (Step s, Failure f)
{
    step = s;
    failure = f;
    sendChangeMessage();
}

void Updater::startDownload (const ReleaseInfo& r)
{
    if (step != Step::idle && step != Step::failed) return;
    release = r;
    file = downloadDir.getChildFile (juce::URL (r.installerUrl).getFileName());
    progress = 0.0f;
    ++job;

    // A file from an earlier run is only trusted when its hash is known and matches; otherwise it may be half there.
    if (file.existsAsFile() && r.installerSha256.isNotEmpty() && juce::SHA256 (file).toHexString().equalsIgnoreCase (r.installerSha256))
    {
        progress = 1.0f;
        setStep (Step::ready);
        return;
    }
    file.deleteFile();
    installing = false;
    runJob = job;
    setStep (Step::downloading);
    launch();
}

void Updater::cancel()
{
    if (step != Step::downloading) return;
    ++job;
    pendingStart = false;
    signalThreadShouldExit();   // never waits: run() sees it, deletes the file and .part, and returns
    progress = 0.0f;
    setStep (Step::idle);
}

void Updater::install (const juce::File& engineStamp)
{
    if (step != Step::ready) return;
    stamp = engineStamp;
    stampBefore = stamp.loadFileAsString();
    installing = true;
    ++job;
    runJob = job;
    setStep (Step::installing);
    launch();
}

// The message thread never waits for a thread: one still running (a cancelled download stuck in a connect) starts the
// job from its own exit, via startPending().
void Updater::launch()
{
    pendingStart = true;
    if (! isThreadRunning()) startPending();
}

void Updater::startPending()
{
    if (! pendingStart) return;
    waitForThreadToExit (1000);   // only ever the last instructions of a thread that has already posted its end
    pendingStart = false;
    startThread (installing ? juce::Thread::Priority::normal : juce::Thread::Priority::low);
}

void Updater::reset()
{
    if (step == Step::failed) setStep (Step::idle);
}

void Updater::run()
{
    const auto id = runJob;
    const auto isInstall = installing;
    const auto myFile = file;
    const auto url = juce::URL (release.installerUrl);
    const auto sha = release.installerSha256;
    struct Done   // whatever way run() ends, a request made meanwhile starts now
    {
        Updater* u; std::shared_ptr<bool> a;
        ~Done() { juce::MessageManager::callAsync ([u = u, a = a] { if (*a) u->startPending(); }); }
    } done { this, alive };
    const auto post = [this, id, a = alive] (std::function<void()> f)
    {
        juce::MessageManager::callAsync ([this, id, a, f = std::move (f)]
        {
            if (*a && id == job) f();
        });
    };

    if (isInstall)
    {
        const auto code = launchFn (myFile);
        if (code == 0) myFile.deleteFile();
        post ([this, code]
        {
            if (code == 0)
            {
                changedEngine = stamp.loadFileAsString() != stampBefore;
                setStep (Step::installed);
            }
            else if (code == -2) setStep (Step::waitingForInstaller);
            else setStep (Step::failed, code == 101 ? Failure::engine : code == 7 ? Failure::inUse : Failure::install);
        });
        return;
    }

    auto lastPosted = 0.0f;
    const auto ok = downloadFn (url, sha, myFile,
                                [&] (float f)
                                {
                                    if (f - lastPosted >= 0.01f || f >= 1.0f)
                                    {
                                        lastPosted = f;
                                        post ([this, f] { progress = f; sendChangeMessage(); });
                                    }
                                    return ! threadShouldExit();
                                });
    if (threadShouldExit())   // cancelled: nothing is left behind
    {
        myFile.deleteFile();
        myFile.getSiblingFile (myFile.getFileName() + ".part").deleteFile();
        return;
    }
    post ([this, ok]
    {
        if (ok) { progress = 1.0f; setStep (Step::ready); }
        else setStep (Step::failed, Failure::download);
    });
}

int Updater::launchInstaller (const juce::File&, bool)
{
    return -1;   // Task 4
}
