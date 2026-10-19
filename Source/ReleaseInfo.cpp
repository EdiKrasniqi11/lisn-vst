#include "ReleaseInfo.h"
#include <juce_audio_processors/juce_audio_processors.h>

juce::StringArray whatsNewBullets (const juce::String& body, bool& more)
{
    more = false;
    juce::StringArray out;
    bool inSection = false;

    for (auto line : juce::StringArray::fromLines (body))
    {
        line = line.trim();
        if (line.startsWith ("## "))
        {
            inSection = line.substring (3).trim().equalsIgnoreCase ("What's new");
            continue;
        }
        if (inSection && (line.startsWith ("- ") || line.startsWith ("* ")))
        {
            if (out.size() == 6) { more = true; break; }
            out.add (line.substring (2).trim());
        }
    }
    if (out.isEmpty()) out.add ("Fixes and improvements.");
    return out;
}

ReleaseInfo parseRelease (const juce::var& json, const juce::String& installerName)
{
    ReleaseInfo r;
    if (! json.isObject()) return r;

    r.version = json["tag_name"].toString().trim().trimCharactersAtStart ("vV");
    r.pageUrl = json["html_url"].toString();
    r.notes = whatsNewBullets (json["body"].toString(), r.moreNotes);

    juce::StringArray shots;   // "name|url", sorted by name
    if (auto* assets = json["assets"].getArray())
        for (auto& a : *assets)
        {
            const auto name = a["name"].toString();
            const auto url = a["browser_download_url"].toString();
            const auto lower = name.toLowerCase();
            if (lower.startsWith ("whats-new-") && (lower.endsWith (".png") || lower.endsWith (".jpg") || lower.endsWith (".jpeg")))
                shots.add (name + "|" + url);
            if (name == installerName)
            {
                r.installerUrl = url;
                r.installerSize = (juce::int64) a["size"];
                const auto digest = a["digest"].toString();
                if (digest.startsWithIgnoreCase ("sha256:")) r.installerSha256 = digest.substring (7).toLowerCase();
            }
        }
    shots.sort (true);
    for (int i = 0; i < juce::jmin (3, shots.size()); ++i)
        r.screenshots.add (shots[i].fromFirstOccurrenceOf ("|", false, false));
    return r;
}

juce::String installerAssetName (bool standalone)
{
   #if JUCE_MAC
    juce::ignoreUnused (standalone);
    return "LISN-Mac.pkg";
   #else
    return standalone ? "LISN-Setup.exe" : "LISN-StemSplitter-Setup.exe";
   #endif
}

juce::String hostName()
{
    const juce::PluginHostType host;
    return host.isFruityLoops() ? "FL Studio" : host.isAbletonLive() ? "Ableton Live" : "your DAW";
}
