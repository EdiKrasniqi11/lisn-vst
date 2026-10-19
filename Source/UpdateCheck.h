#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include "ReleaseInfo.h"

// "v0.2.5" vs "0.2.4": true when `latest` is a higher dotted version (a missing part counts as 0). Anything that isn't
// digits and dots, like "v0.3.0-beta", is never newer.
bool isNewerVersion (const juce::String& latest, const juce::String& current);

// GitHub's latest LISN release as JSON, or a void var on any failure. Blocks for up to 5 s: not on the message thread.
juce::var fetchLatestReleaseJson();

// Whether a newer LISN is out: GitHub's latest release (EdiKrasniqi11/lisn-vst), asked at most once a day. Every LISN on
// the PC shares the answer in %APPDATA%\StemSplitter\update.settings, so a day of FL sessions makes one request, and
// nothing about the user is sent. Offline, or any failure: no newer version, no retry until the next process.
// One check per process: the first getInstance() starts it, editors listen for the change.
// Trying the flow before a release: put a GitHub release's JSON in update-test.json beside update.settings. It replaces
// the cache and the network (and the version still has to be newer than this build).
class UpdateCheck : public juce::ChangeBroadcaster, public juce::DeletedAtShutdown, private juce::Thread
{
public:
    UpdateCheck();
    ~UpdateCheck() override;
    const ReleaseInfo& getRelease() const { return newer; }   // message thread; valid only when newer than this build
    juce::String getNewerVersion() const { return newer.version; }   // "0.2.5", or empty

    static juce::File testOverrideFile();
    // The release JSON from the cache in `seen`, asked again when it's over a day old or only the old tag is stored.
    // A failed ask (void) keeps what's stored.
    static juce::var releaseFor (juce::PropertiesFile& seen, juce::int64 nowMs, std::function<juce::var()> fetch);

    JUCE_DECLARE_SINGLETON_SINGLETHREADED_MINIMAL (UpdateCheck)

private:
    void run() override;
    ReleaseInfo newer;
};
