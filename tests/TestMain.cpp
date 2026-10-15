#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include "../Source/UpdateCheck.h"

int runSnapshots (const juce::File& outDir);   // tests/Snapshots.cpp
int runBench();

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args[0] == "--snapshots") return runSnapshots (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
    if (args[0] == "--bench") return runBench();
    if (args[0] == "--update-check")   // the real request to GitHub, and what it saved (see UpdateCheck.h)
    {
        std::printf ("GitHub's latest: '%s'\n", fetchLatestRelease().toRawUTF8());
        UpdateCheck::getInstance();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (8000);
        const auto saved = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("StemSplitter/update.settings");
        std::printf ("newer: '%s'\n%s\n", UpdateCheck::getInstance()->getNewerVersion().toRawUTF8(), saved.loadFileAsString().toRawUTF8());
        return 0;
    }

    // The default runner logs via OutputDebugString only; print to stdout so ctest shows the results.
    struct Runner : juce::UnitTestRunner { void logMessage (const juce::String& m) override { std::puts (m.toRawUTF8()); } } runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) failures += runner.getResult (i)->failures;
    return failures == 0 ? 0 : 1;
}
