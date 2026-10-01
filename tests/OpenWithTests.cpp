#include "../Source/OpenWith.h"

struct OpenWithTests : juce::UnitTest
{
    OpenWithTests() : juce::UnitTest ("OpenWith") {}

    void runTest() override
    {
        beginTest ("one absolute path, quoted or not");
        expectEquals (fileToOpen ("\"C:\\My Music\\a b.mp3\"").getFullPathName(), juce::String ("C:\\My Music\\a b.mp3"));
        expectEquals (fileToOpen ("  C:\\x.wav ").getFullPathName(), juce::String ("C:\\x.wav"));

        beginTest ("anything else gives no file");
        expect (fileToOpen ({}) == juce::File());
        expect (fileToOpen ("song.mp3") == juce::File());            // relative
        expect (fileToOpen ("--version") == juce::File());
        expect (fileToOpen ("C:\\a.wav C:\\b.wav") == juce::File()); // two files
    }
};

static OpenWithTests openWithTests;
