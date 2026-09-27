#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorEditor* StemSplitterProcessor::createEditor() { return new StemSplitterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StemSplitterProcessor(); }
