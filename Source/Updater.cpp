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
    stopThread (10000);   // the last job's thread may still be returning
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
    setStep (Step::downloading);
    startThread (juce::Thread::Priority::low);
}

void Updater::cancel()
{
    if (step != Step::downloading) return;
    ++job;
    signalThreadShouldExit();
    stopThread (10000);
    file.deleteFile();
    file.getSiblingFile (file.getFileName() + ".part").deleteFile();
    progress = 0.0f;
    setStep (Step::idle);
}

void Updater::install (const juce::File& engineStamp)
{
    if (step != Step::ready) return;
    stopThread (10000);
    stamp = engineStamp;
    stampBefore = stamp.loadFileAsString();
    installing = true;
    ++job;
    setStep (Step::installing);
    startThread();
}

void Updater::reset()
{
    if (step == Step::failed) setStep (Step::idle);
}

void Updater::run()
{
    const auto id = job;
    const auto post = [this, id, a = alive] (std::function<void()> f)
    {
        juce::MessageManager::callAsync ([this, id, a, f = std::move (f)]
        {
            if (*a && id == job) f();
        });
    };

    if (installing)
    {
        const auto code = launchFn (file);
        if (code == 0) file.deleteFile();
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
    const auto ok = downloadFn (juce::URL (release.installerUrl), release.installerSha256, file,
                                [&] (float f)
                                {
                                    if (f - lastPosted >= 0.01f || f >= 1.0f)
                                    {
                                        lastPosted = f;
                                        post ([this, f] { progress = f; sendChangeMessage(); });
                                    }
                                    return ! threadShouldExit();
                                });
    if (threadShouldExit()) return;   // cancel() cleans up
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
