#include "UiState.h"
#include "PluginProcessor.h"

bool UiState::sameScreenAs (const UiState& o) const
{
    return screen == o.screen && error == o.error && errorText == o.errorText && pythonExe == o.pythonExe
        && stemDir == o.stemDir && songName == o.songName && sixStems == o.sixStems;
}

UiState uiStateFor (JobState state, ErrorKind kind, double progress, const juce::String& errorText, const juce::String& pythonExe,
                    const juce::File& jobStemDir, const juce::File& savedStemDir, const juce::File& lastInput, bool sixStems)
{
    UiState s;
    s.songName = lastInput.getFileName();
    s.sixStems = sixStems;
    switch (state)
    {
        case JobState::Running:
            s.screen = Screen::Splitting;
            s.progress = progress;
            break;
        case JobState::Failed:
            s.screen = Screen::Error;
            s.error = kind == ErrorKind::None ? ErrorKind::Failed : kind;
            s.errorText = errorText;
            s.pythonExe = pythonExe;
            break;
        case JobState::Done:
            s.screen = Screen::Stems;
            s.stemDir = jobStemDir;
            break;
        case JobState::Idle:
        case JobState::Cancelled:
            s.screen = savedStemDir == juce::File() ? Screen::Drop : Screen::Stems;
            s.stemDir = savedStemDir;
            break;
    }
    return s;
}

UiState uiStateFor (const StemSplitterProcessor& p)
{
    const auto state = p.job.getState();
    const bool finished = state == JobState::Done || state == JobState::Failed;   // the worker writes the strings before the state
    return uiStateFor (state, p.job.getErrorKind(), p.job.getProgress(),
                       finished ? p.job.getError() : juce::String(), finished ? p.job.getPythonExe() : juce::String(),
                       state == JobState::Done ? p.job.getStemDir() : juce::File(),
                       p.hasStems() ? p.getStemDir() : juce::File(), p.getLastInput(), p.isSixStems());
}
