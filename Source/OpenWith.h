#pragma once
#include <juce_core/juce_core.h>

// The one absolute path a command line names: Explorer's "Open with" passes "C:\My Music\song.mp3" (quoted).
// No argument, several, or a relative path gives File(). The caller still checks the file exists and is audio.
inline juce::File fileToOpen (const juce::String& commandLine)
{
    juce::StringArray args;
    args.addTokens (commandLine, true);   // whitespace-separated, quoted paths kept whole
    args.removeEmptyStrings();
    const auto path = args[0].unquoted();
    return args.size() == 1 && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}
