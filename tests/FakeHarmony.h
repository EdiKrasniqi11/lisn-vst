#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/SeparationJob.h"
#include "../Source/HarmonyJob.h"

// Shared by the tests that stand in for the harmony pass: short WAVs and a fake script that replaces Python.
namespace FakeHarmony
{
inline void writeWav (const juce::File& f)   // 0.2 s of a quiet 440 Hz tone, 44.1 kHz stereo
{
    juce::AudioBuffer<float> b (2, 8820);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, 0.2f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));
    f.getParentDirectory().createDirectory();
    auto out = f.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
    if (w == nullptr) return;
    out.release();
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Writes the fake pass (a .cmd batch file on Windows, a /bin/sh script on Mac: %1 / $1 the .part dir, %2 / $2 the
// quality) and returns the command that runs it in place of Python.
inline juce::StringArray fakeCommand (const juce::File& fileNoExtension, const juce::String& windowsScript, const juce::String& shScript)
{
   #if JUCE_WINDOWS
    juce::ignoreUnused (shScript);
    const auto script = fileNoExtension.withFileExtension ("cmd");
    script.replaceWithText (windowsScript);
    return { "cmd.exe", "/c", script.getFullPathName() };
   #else
    juce::ignoreUnused (windowsScript);
    const auto script = fileNoExtension.withFileExtension ("sh");
    script.replaceWithText (shScript, false, false, "\n");   // JUCE's default line ending is \r\n, which sh reads as part of every word
    script.setExecutePermission (true);
    return { "/bin/sh", script.getFullPathName() };
   #endif
}

// A temp song whose 4-stem cache folder (the real one, under the user's app data) holds four short WAVs; the cache
// folder goes with the song, even when a check fails.
struct CachedSong
{
    CachedSong()
    {
        root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-flow", {});
        root.createDirectory();
        file = root.getChildFile ("song.wav");
        file.replaceWithText ("not really a song " + juce::Uuid().toString());   // a unique size/mtime key
        stems = SeparationJob::stemDirFor (file, "htdemucs");
        for (auto stem : { "vocals", "drums", "bass", "other" })
            writeWav (stems.getChildFile (juce::String (stem) + ".wav"));
    }
    ~CachedSong() { stems.getParentDirectory().deleteRecursively(); root.deleteRecursively(); }
    juce::File root, file, stems;
};

// The fake pass: copies vocals.wav to lead and harmonies and writes the quality.
inline juce::StringArray okPass (const CachedSong& s)
{
    const auto vocals = s.stems.getChildFile ("vocals.wav").getFullPathName();
    return fakeCommand (s.root.getChildFile ("fake"),
        "@echo off\r\necho 37%%^|\r\ncopy /y \"" + vocals + "\" \"%~1\\lead.wav\" >nul\r\n"
        "copy /y \"" + vocals + "\" \"%~1\\harmonies.wav\" >nul\r\n"
        "echo %~2> \"%~1\\quality.txt\"\r\n",
        "#!/bin/sh\necho '37%|'\ncp \"" + vocals + "\" \"$1/lead.wav\"\n"
        "cp \"" + vocals + "\" \"$1/harmonies.wav\"\n"
        "echo \"$2\" > \"$1/quality.txt\"\n");
}

// A finished harmony result of that quality in a stem folder, as a pass leaves it.
inline void writeResult (const juce::File& stems, const juce::String& quality)
{
    writeWav (HarmonyJob::dirFor (stems).getChildFile ("lead.wav"));
    writeWav (HarmonyJob::dirFor (stems).getChildFile ("harmonies.wav"));
    HarmonyJob::dirFor (stems).getChildFile ("quality.txt").replaceWithText (quality);
}
}
