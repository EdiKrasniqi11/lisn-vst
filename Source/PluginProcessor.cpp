#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorEditor* StemSplitterProcessor::createEditor() { return new StemSplitterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StemSplitterProcessor(); }

void StemSplitterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (stemDir == juce::File() && job.getState() == JobState::Done)
        stemDir = job.getStemDir();

    juce::ValueTree s ("StemSplitter");
    s.setProperty ("model", model, nullptr);
    s.setProperty ("python", pythonPath, nullptr);
    s.setProperty ("input", lastInput.getFullPathName(), nullptr);
    s.setProperty ("stems", stemDir.getFullPathName(), nullptr);
    if (auto xml = s.createXml()) copyXmlToBinary (*xml, dest);
}

void StemSplitterProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto s = juce::ValueTree::fromXml (*xml);
    model      = s.getProperty ("model", "htdemucs").toString();
    pythonPath = s.getProperty ("python", "python").toString();
    auto in = s.getProperty ("input").toString();
    auto st = s.getProperty ("stems").toString();
    lastInput = juce::File::isAbsolutePath (in) ? juce::File (in) : juce::File();
    stemDir   = juce::File::isAbsolutePath (st) ? juce::File (st) : juce::File();
}
