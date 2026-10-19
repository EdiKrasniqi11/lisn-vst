#include "UiState.h"
#include "PluginProcessor.h"

bool UiState::sameScreenAs (const UiState& o) const
{
    return screen == o.screen && error == o.error && errorText == o.errorText && pythonExe == o.pythonExe
        && stemDir == o.stemDir && songName == o.songName && sixStems == o.sixStems
        && harmoniesProduct == o.harmoniesProduct && harmonyStep == o.harmonyStep && downloading == o.downloading
        && offerSong == o.offerSong && harmonyStemDir == o.harmonyStemDir && harmonyQuality == o.harmonyQuality;
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

UiState harmoniesStateFor (HarmonyFlow::Step step, float progress, bool downloading, ErrorKind kind, const juce::String& errorText,
                           const juce::String& pythonExe, const juce::File& harmonyInput, const juce::File& harmonyStemDir,
                           bool hasResult, const juce::File& stemSplitterSong, bool stemSplitterSongHasStems, const juce::String& quality)
{
    using Step = HarmonyFlow::Step;
    UiState s;
    s.harmoniesProduct = true;
    s.songName = harmonyInput.getFileName();
    switch (step)
    {
        case Step::vocals:
        case Step::harmonies:
            s.screen = Screen::HarmonySplitting;
            s.harmonyStep = step == Step::vocals ? 1 : 2;
            s.progress = progress;
            s.downloading = step == Step::harmonies && downloading;
            return s;
        case Step::failed:
            s.screen = Screen::Error;
            s.error = kind == ErrorKind::None ? ErrorKind::Failed : kind;
            s.errorText = errorText;
            s.pythonExe = pythonExe;
            return s;
        case Step::done:
        case Step::idle:
            break;
    }
    if (hasResult)
    {
        s.screen = Screen::Harmonies;
        s.harmonyStemDir = harmonyStemDir;
        s.harmonyQuality = quality;
        return s;
    }
    s.screen = Screen::HarmonyDrop;
    s.songName = {};
    if (stemSplitterSongHasStems)
        s.offerSong = stemSplitterSong.getFileNameWithoutExtension();
    return s;
}

UiState uiStateFor (const StemSplitterProcessor& p)
{
    if (p.getProduct() == "harmonies")
    {
        const auto step = p.harmonyFlow.getStep();
        const bool finished = step == HarmonyFlow::Step::done || step == HarmonyFlow::Step::failed;   // the worker writes the strings before the step
        const bool resting = step == HarmonyFlow::Step::done || step == HarmonyFlow::Step::idle;
        const auto& r = p.harmonyResults();   // cached disk checks, only needed at rest
        const bool hasResult = resting && r.quality.isNotEmpty();
        return harmoniesStateFor (step, p.harmonyFlow.getProgress(), p.harmonyFlow.isDownloading(), p.harmonyFlow.getErrorKind(),
                                  finished ? p.harmonyFlow.getError() : juce::String(), finished ? p.harmonyFlow.getPythonExe() : juce::String(),
                                  step == HarmonyFlow::Step::idle ? p.getHarmonyInput() : p.harmonyFlow.getSong(), r.dir, hasResult,
                                  p.getLastInput(), p.hasStems() && ! r.offerHasResult,   // offer a split song only while it has no harmonies yet
                                  hasResult ? r.quality : p.getHarmonyQuality());
    }
    const auto state = p.job.getState();
    const bool finished = state == JobState::Done || state == JobState::Failed;   // the worker writes the strings before the state
    return uiStateFor (state, p.job.getErrorKind(), p.job.getProgress(),
                       finished ? p.job.getError() : juce::String(), finished ? p.job.getPythonExe() : juce::String(),
                       state == JobState::Done ? p.job.getStemDir() : juce::File(),
                       p.hasStems() ? p.getStemDir() : juce::File(), p.getLastInput(), p.isSixStems());
}
