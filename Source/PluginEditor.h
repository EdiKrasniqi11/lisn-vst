#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class StemSplitterEditor : public juce::AudioProcessorEditor
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor& p) : AudioProcessorEditor (p) { setSize (520, 360); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1e24));
        g.setColour (juce::Colours::white);
        g.drawText ("StemSplitter", getLocalBounds(), juce::Justification::centred);
    }
};
