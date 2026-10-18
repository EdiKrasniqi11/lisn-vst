#include "HarmonyFlow.h"

void HarmonyFlow::start (const juce::File& s, const juce::String& q, const juce::String& hint, bool keep)
{
    cancel();
    song = s; quality = q; pythonHint = hint; keepExisting = keep;
    stemDir = SeparationJob::stemDirFor (s, "htdemucs");   // Harmonies always works on the 4-stem split
    error.clear();
    errorKind = ErrorKind::None;
    step = Step::vocals;
    separation.start (s, "htdemucs", hint);
    advance();   // a cached song, and a kept result, finish right here
    if (step == Step::vocals || step == Step::harmonies) startTimer (100);
}

void HarmonyFlow::cancel()
{
    stopTimer();
    separation.cancel();
    harmonyJob.cancel();
    step = Step::idle;
}

float HarmonyFlow::getProgress() const
{
    switch (step)
    {
        case Step::vocals:    return (float) separation.getProgress();
        case Step::harmonies: return harmonyJob.getProgress();
        case Step::done:      return 1.0f;
        default:              return 0.0f;
    }
}

juce::String HarmonyFlow::getPythonExe() const
{
    const auto s = separation.getState();   // the job's strings are valid once it has finished
    const auto usable = s == JobState::Done || (s == JobState::Failed && separation.getErrorKind() != ErrorKind::PythonMissing);
    return usable ? separation.getPythonExe() : juce::String();
}

void HarmonyFlow::finish (Step s)
{
    step = s;
    stopTimer();
}

void HarmonyFlow::advance()
{
    if (step == Step::vocals)
    {
        switch (separation.getState())
        {
            case JobState::Done:
            {
                stemDir = separation.getStemDir();
                const auto found = getPythonExe();
                if (HarmonyJob::hasResult (stemDir) && (keepExisting || HarmonyJob::qualityOf (stemDir) == quality))
                    return finish (Step::done);
                step = Step::harmonies;
                harmonyJob.start (stemDir, found.isNotEmpty() ? found : pythonHint, quality);
                return;
            }
            case JobState::Failed:
                errorKind = separation.getErrorKind();
                error = separation.getError();
                return finish (Step::failed);
            case JobState::Cancelled:
                return finish (Step::idle);
            default: return;
        }
    }
    if (step == Step::harmonies)
    {
        if (harmonyJob.getState() == HarmonyJob::State::done)
            finish (Step::done);
        else if (harmonyJob.getState() == HarmonyJob::State::failed)
        {
            errorKind = ErrorKind::Failed;
            error = harmonyJob.getError();
            finish (Step::failed);
        }
    }
}
