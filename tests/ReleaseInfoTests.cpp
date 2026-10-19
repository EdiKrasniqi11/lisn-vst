#include "../Source/ReleaseInfo.h"

struct ReleaseInfoTests : juce::UnitTest
{
    ReleaseInfoTests() : juce::UnitTest ("ReleaseInfo") {}

    static juce::var asset (const juce::String& name, const juce::String& digest = {}, int size = 1)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name);
        o->setProperty ("browser_download_url", "https://x/" + name);
        o->setProperty ("size", size);
        if (digest.isNotEmpty()) o->setProperty ("digest", digest);
        return o;
    }

    void runTest() override
    {
        beginTest ("whatsNewBullets");
        bool more = true;
        auto b = whatsNewBullets ("intro\r\n## What's new\r\n- **Harmonies**: split\r\n* Faster\r\n\r\n## SHA\r\n- not me", more);
        expect (b == juce::StringArray ({ "**Harmonies**: split", "Faster" }) && ! more);
        b = whatsNewBullets ("## WHAT'S NEW\n- a", more);
        expect (b == juce::StringArray ("a"));
        b = whatsNewBullets ("## What's new\n- 1\n- 2\n- 3\n- 4\n- 5\n- 6\n- 7\n- 8", more);
        expect (b.size() == 6 && more);
        more = true;
        expect (whatsNewBullets ("no heading\n- x", more) == juce::StringArray ("Fixes and improvements.") && ! more);
        expect (whatsNewBullets ("## What's new\nprose only", more) == juce::StringArray ("Fixes and improvements."));

        beginTest ("parseRelease");
        auto* o = new juce::DynamicObject();
        o->setProperty ("tag_name", "v0.3.1");
        o->setProperty ("html_url", "https://x/rel");
        o->setProperty ("body", "## What's new\n- one");
        juce::Array<juce::var> assets;
        for (auto* n : { "whats-new-2.png", "whats-new-1.jpg", "whats-new-4.png", "whats-new-3.png", "other.png" })
            assets.add (asset (n));
        assets.add (asset ("LISN-Setup.exe", "sha256:ABCD", 31000000));
        o->setProperty ("assets", assets);
        const juce::var json (o);

        auto r = parseRelease (json, "LISN-Setup.exe");
        expect (r.isValid() && r.version == "0.3.1" && r.pageUrl == "https://x/rel");
        expect (r.screenshots == juce::StringArray ({ "https://x/whats-new-1.jpg", "https://x/whats-new-2.png", "https://x/whats-new-3.png" }));
        expect (r.installerSize == 31000000 && r.installerSha256 == "abcd" && r.installerUrl == "https://x/LISN-Setup.exe");
        expect (parseRelease (json, "LISN-Mac.pkg").installerUrl.isEmpty());

        assets.set (5, asset ("LISN-Setup.exe"));
        o->setProperty ("assets", assets);
        expect (parseRelease (json, "LISN-Setup.exe").installerSha256.isEmpty());

        expect (! parseRelease (juce::var(), "x").isValid());
    }
};

static ReleaseInfoTests releaseInfoTests;
