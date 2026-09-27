#include "PluginEditor.h"

StemSplitterEditor::StemSplitterEditor (StemSplitterProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    modelBox.addItem ("htdemucs", 1);
    modelBox.addItem ("htdemucs_6s", 2);
    modelBox.setText (proc.model, juce::dontSendNotification);
    modelBox.onChange = [this] { proc.model = modelBox.getText(); };
    addAndMakeVisible (modelBox);

    pythonField.setText (proc.pythonPath, false);
    pythonField.setTooltip ("Python executable (default: python on PATH)");
    pythonField.onTextChange = [this] { proc.pythonPath = pythonField.getText().trim(); };
    addAndMakeVisible (pythonField);

    cancelButton.onClick = [this] { proc.job.cancel(); };
    addChildComponent (cancelButton);
    addChildComponent (progressBar);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (status);

    if (proc.stemDir.isDirectory()) showStems (proc.stemDir);
    else status.setText ("Drop an audio file here", juce::dontSendNotification);

    setSize (520, 360);
    startTimerHz (30);
}

void StemSplitterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e24));
}

void StemSplitterEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (28);
    modelBox.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (8);
    pythonField.setBounds (top);
    r.removeFromTop (12);

    auto bar = r.removeFromTop (24);
    cancelButton.setBounds (bar.removeFromRight (80));
    progressBar.setBounds (bar.withTrimmedRight (8));
    r.removeFromTop (8);
    status.setBounds (r.removeFromTop (60));

    if (tiles.isEmpty()) return;
    auto w = r.getWidth() / tiles.size();
    for (auto* t : tiles) t->setBounds (r.removeFromLeft (w).withHeight (80));
}

bool StemSplitterEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg");
}

void StemSplitterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    tiles.clear();
    proc.lastInput = juce::File (files[0]);
    proc.stemDir = juce::File();
    proc.job.start (proc.lastInput, proc.model, proc.pythonPath);
    lastState = JobState::Idle;   // force a UI refresh on the next tick
}

void StemSplitterEditor::timerCallback()
{
    auto s = proc.job.getState();
    progressValue = proc.job.getProgress();
    if (s == lastState) return;
    lastState = s;

    const bool running = s == JobState::Running;
    progressBar.setVisible (running);
    cancelButton.setVisible (running);

    switch (s)
    {
        case JobState::Running:
            status.setText ("Separating " + proc.lastInput.getFileName() + "...", juce::dontSendNotification);
            break;
        case JobState::Done:
            proc.stemDir = proc.job.getStemDir();
            showStems (proc.stemDir);
            break;
        case JobState::Failed:
            status.setText (proc.job.getError(), juce::dontSendNotification);
            break;
        case JobState::Cancelled:
            status.setText ("Cancelled. Drop a file to try again.", juce::dontSendNotification);
            break;
        case JobState::Idle: break;
    }
}

void StemSplitterEditor::showStems (juce::File dir)
{
    tiles.clear();
    for (auto& f : SeparationJob::stemsIn (dir))
        addAndMakeVisible (tiles.add (new StemTile (f)));
    status.setText (tiles.isEmpty() ? "Stems missing. Drop the file again."
                                    : "Drag stems onto your tracks", juce::dontSendNotification);
    resized();
}
