#pragma once
#include <juce_events/juce_events.h>

// "v0.2.5" vs "0.2.4": true when `latest` is a higher dotted version (a missing part counts as 0). Anything that isn't
// digits and dots, like "v0.3.0-beta", is never newer.
bool isNewerVersion (const juce::String& latest, const juce::String& current);

// GitHub's latest LISN release tag ("v0.2.4"), or empty on any failure. Blocks for up to 5 s: not on the message thread.
juce::String fetchLatestRelease();

// Whether a newer LISN is out: GitHub's latest release (EdiKrasniqi11/lisn-vst), asked at most once a day. Every LISN on
// the PC shares the answer in %APPDATA%\StemSplitter\update.settings, so a day of FL sessions makes one request, and
// nothing about the user is sent. Offline, or any failure: no newer version, no retry until the next process.
// One check per process: the first getInstance() starts it, editors listen for the change.
class UpdateCheck : public juce::ChangeBroadcaster, public juce::DeletedAtShutdown, private juce::Thread
{
public:
    UpdateCheck();
    ~UpdateCheck() override;
    juce::String getNewerVersion() const { return newer; }   // message thread; "0.2.5", or empty

    JUCE_DECLARE_SINGLETON_SINGLETHREADED_MINIMAL (UpdateCheck)

private:
    void run() override;
    juce::String newer;
};
