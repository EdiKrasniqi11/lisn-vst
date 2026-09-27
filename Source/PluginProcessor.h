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

    const juce::String getName() const override { return "StemSplitter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    SeparationJob job;
    juce::String model = "htdemucs";
    juce::String pythonPath = "python";
    juce::File lastInput;   // last dropped file
    juce::File stemDir;     // folder of finished stems, empty if none

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterProcessor)
};
