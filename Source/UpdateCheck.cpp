#include "UpdateCheck.h"
#include <juce_audio_processors/juce_audio_processors.h>

bool isNewerVersion (const juce::String& latest, const juce::String& current)
{
    const auto parts = [] (const juce::String& v) { return juce::StringArray::fromTokens (v.trim().trimCharactersAtStart ("vV"), ".", ""); };
    const auto a = parts (latest), b = parts (current);
    if (a.isEmpty())
        return false;
    for (const auto& p : a)
        if (p.isEmpty() || ! p.containsOnly ("0123456789"))
            return false;
    for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
        if (const auto x = a[i].getIntValue(), y = b[i].getIntValue(); x != y)
            return x > y;
    return false;
}

juce::var fetchLatestReleaseJson()
{
    const auto request = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs (5000)
                             .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: LISN");
    if (auto in = juce::URL ("https://api.github.com/repos/EdiKrasniqi11/lisn-vst/releases/latest").createInputStream (request))
        return juce::JSON::parse (in->readEntireStreamAsString());
    return {};
}

JUCE_IMPLEMENT_SINGLETON (UpdateCheck)

UpdateCheck::UpdateCheck() : juce::Thread ("LISN update check")
{
    startThread (juce::Thread::Priority::low);
}

UpdateCheck::~UpdateCheck()
{
    stopThread (6000);   // the request times out after 5 s
    clearSingletonInstance();
}

static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "update";
    options.folderName = "StemSplitter";   // %APPDATA%\StemSplitter\update.settings, beside the stems cache
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";   // Mac: ~/Library/Application Support/StemSplitter; Windows ignores it
    return options;
}

juce::File UpdateCheck::testOverrideFile()
{
    return settingsOptions().getDefaultFile().getSiblingFile ("update-test.json");
}

juce::var UpdateCheck::releaseFor (juce::PropertiesFile& seen, juce::int64 nowMs, std::function<juce::var()> fetch)
{
    auto release = juce::JSON::parse (seen.getValue ("release"));
    if (! release.isObject() || nowMs - seen.getValue ("checked").getLargeIntValue() > 24 * 60 * 60 * 1000)
    {
        if (const auto fresh = fetch(); fresh.isObject())
        {
            release = fresh;
            seen.setValue ("release", juce::JSON::toString (fresh, true));
            seen.setValue ("latest", fresh["tag_name"].toString());   // 0.2.5 processes sharing the file read this
            seen.setValue ("checked", juce::String (nowMs));
            seen.saveIfNeeded();
        }
    }
    return release;
}

void UpdateCheck::run()
{
    // ponytail: two LISNs opening in the same second may both ask; a process lock if that ever matters
    juce::var json;
    if (const auto test = testOverrideFile(); test.existsAsFile())
        json = juce::JSON::parse (test);
    else
    {
        juce::PropertiesFile seen (settingsOptions());
        json = releaseFor (seen, juce::Time::currentTimeMillis(), [] { return fetchLatestReleaseJson(); });
    }
    const auto standalone = juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
    const auto info = parseRelease (json, installerAssetName (standalone));
    if (threadShouldExit() || ! info.isValid() || ! isNewerVersion (info.version, JucePlugin_VersionString))
        return;
    juce::MessageManager::callAsync ([info]
    {
        if (auto* check = UpdateCheck::getInstanceWithoutCreating())
        {
            check->newer = info;
            check->sendChangeMessage();
        }
    });
}
