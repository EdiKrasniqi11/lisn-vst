#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// One draggable stem file; drag it onto a DAW track.
class StemTile : public juce::Component
{
public:
    explicit StemTile (juce::File f) : file (f) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff3a3a48));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2), 6.0f);
        g.setColour (juce::Colours::white);
        g.drawText (file.getFileNameWithoutExtension(), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent&) override
    {
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }
private:
    juce::File file;
};

class StemSplitterEditor : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void showStems (juce::File dir);

    StemSplitterProcessor& proc;
    juce::ComboBox modelBox;
    juce::TextEditor pythonField;
    juce::TextButton cancelButton { "Cancel" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label status;
    juce::OwnedArray<StemTile> tiles;
    JobState lastState = JobState::Idle;
};
