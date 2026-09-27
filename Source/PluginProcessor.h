#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SeparationJob.h"

class StemSplitterProcessor : public juce::AudioProcessor
{
public:
    StemSplitterProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {} // pass-through

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;   // read-only: never writes members
    void setStateInformation (const void*, int) override;

    SeparationJob job;

    // Settings. The host may save state from any thread, so every access goes through settingsLock.
    juce::String getTheme() const      { const juce::ScopedLock sl (settingsLock); return theme; }
    juce::String getPythonPath() const { const juce::ScopedLock sl (settingsLock); return pythonPath; }
    juce::File getLastInput() const    { const juce::ScopedLock sl (settingsLock); return lastInput; }
    juce::File getStemDir() const      { const juce::ScopedLock sl (settingsLock); return stemDir; }
    bool hasStems() const              { const juce::ScopedLock sl (settingsLock); return stemDirHasWav; }   // cached, no disk scan
    bool isSixStems() const            { const juce::ScopedLock sl (settingsLock); return model == "htdemucs_6s"; }

    void setTheme (const juce::String& id);
    void setPythonPath (const juce::String& exe) { const juce::ScopedLock sl (settingsLock); pythonPath = exe; }
    void setSixStems (bool six);             // re-splits the last song when its stems exist for the other model
    void startSplit (const juce::File& input);
    void syncWithJob();                      // adopts a finished job's stem dir and working Python

private:
    void applyJobResults (juce::File& dir, juce::String& python) const;   // what syncWithJob adopts, applied to copies

    juce::CriticalSection settingsLock;
    juce::String theme = "dusk";
    juce::String model = "htdemucs";
    juce::String pythonPath;   // a hint (the user's pick or the last Python that worked); empty = search
    juce::File lastInput;      // last dropped file
    juce::File stemDir;        // folder of finished stems, empty if none
    bool stemDirHasWav = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterProcessor)
};
