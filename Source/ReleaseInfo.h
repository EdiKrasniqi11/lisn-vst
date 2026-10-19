#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// What the update pop-up shows and downloads, read from GitHub's "latest release" JSON.
struct ReleaseInfo
{
    juce::String version;          // "0.3.1" (tag without the v)
    juce::String pageUrl;          // html_url
    juce::StringArray notes;       // bullets, inline **bold** kept as-is for the screen to draw; never empty
    bool moreNotes = false;        // more than 6 bullets were cut
    juce::StringArray screenshots; // browser_download_url of whats-new-N.(png|jpg|jpeg), name order, at most 3
    juce::String installerUrl;     // empty when the release has no asset with that name
    juce::int64 installerSize = 0;
    juce::String installerSha256;  // lower-case hex from digest "sha256:...", or empty
    bool isValid() const { return version.isNotEmpty(); }
};

ReleaseInfo parseRelease (const juce::var& json, const juce::String& installerName);

// The "- " / "* " bullets under "## What's new" (any case), up to the next "## ". At most 6 (more = true when cut);
// none at all gives the single line "Fixes and improvements.".
juce::StringArray whatsNewBullets (const juce::String& body, bool& more);

juce::String installerAssetName (bool standalone);   // Mac: "LISN-Mac.pkg"; Windows: the desktop app's or the VST's setup
juce::String hostName();                              // "FL Studio" / "Ableton Live" / "your DAW"
