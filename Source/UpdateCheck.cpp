#include "UpdateCheck.h"
#include <juce_data_structures/juce_data_structures.h>

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

juce::String fetchLatestRelease()
{
    const auto request = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs (5000)
                             .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: LISN");
    if (auto in = juce::URL ("https://api.github.com/repos/EdiKrasniqi11/lisn-vst/releases/latest").createInputStream (request))
        return juce::JSON::parse (in->readEntireStreamAsString())["tag_name"].toString();
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

void UpdateCheck::run()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "update";
    options.folderName = "StemSplitter";   // %APPDATA%\StemSplitter\update.settings, beside the stems cache
    options.filenameSuffix = ".settings";
    juce::PropertiesFile seen (options);

    // ponytail: two LISNs opening in the same second may both ask; a process lock if that ever matters
    auto latest = seen.getValue ("latest");
    const auto now = juce::Time::currentTimeMillis();
    if (latest.isEmpty() || now - seen.getValue ("checked").getLargeIntValue() > 24 * 60 * 60 * 1000)
    {
        if (const auto tag = fetchLatestRelease(); tag.isNotEmpty())
        {
            latest = tag;
            seen.setValue ("latest", tag);
            seen.setValue ("checked", juce::String (now));
            seen.saveIfNeeded();
        }
    }
    if (threadShouldExit() || ! isNewerVersion (latest, JucePlugin_VersionString))
        return;
    juce::MessageManager::callAsync ([version = latest.trimCharactersAtStart ("vV")]
    {
        if (auto* check = UpdateCheck::getInstanceWithoutCreating())
        {
            check->newer = version;
            check->sendChangeMessage();
        }
    });
}
