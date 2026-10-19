#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorEditor* StemSplitterProcessor::createEditor() { return new StemSplitterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StemSplitterProcessor(); }

static bool hasWav (const juce::File& dir) { return dir.isDirectory() && ! SeparationJob::stemsIn (dir).isEmpty(); }

static void notifyHost (juce::AudioProcessor& p)   // so the DAW marks the project as modified
{
    p.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

void StemSplitterProcessor::setTheme (const juce::String& id)
{
    {
        const juce::ScopedLock sl (settingsLock);
        if (theme == id) return;
        theme = id;
    }
    notifyHost (*this);
}

void StemSplitterProcessor::setSixStems (bool six)
{
    {
        const juce::ScopedLock sl (settingsLock);
        const juce::String m = six ? "htdemucs_6s" : "htdemucs";
        if (m == model) return;
        model = m;
        if (stemDir.isDirectory() && job.getState() != JobState::Running && lastInput.existsAsFile())
            startSplit (lastInput);
    }
    notifyHost (*this);
}

void StemSplitterProcessor::startSplit (const juce::File& input)
{
    player.unload();
    const juce::ScopedLock sl (settingsLock);   // also covers job.start(), which resets the strings getStateInformation reads
    lastInput = input;
    stemDir = juce::File();
    stemDirHasWav = false;
    job.start (input, model, pythonPath);
}

void StemSplitterProcessor::setProduct (const juce::String& id)
{
    const juce::String p = id == "harmonies" ? id : "stems";
    {
        const juce::ScopedLock sl (settingsLock);
        if (product == p) return;
        product = p;
    }
    notifyHost (*this);
}

void StemSplitterProcessor::setHarmonyQuality (const juce::String& q)
{
    const juce::String v = q == "best" ? q : "quick";
    {
        const juce::ScopedLock sl (settingsLock);
        if (harmonyQuality == v) return;
        harmonyQuality = v;
    }
    notifyHost (*this);
}

void StemSplitterProcessor::startHarmonies (const juce::File& song, bool keepExistingResult)
{
    harmonyFlow.cancel();   // may join a running step for seconds: not under the lock a host state save waits on
    {
        const juce::ScopedLock sl (settingsLock);   // also covers the split's start(), which resets the strings getStateInformation reads
        harmonyInput = song;
        harmonyFlow.start (song, harmonyQuality, pythonPath, keepExistingResult);
    }
    notifyHost (*this);
}

void StemSplitterProcessor::clearHarmonySong()
{
    harmonyFlow.cancel();   // not under the lock, as in startHarmonies
    {
        const juce::ScopedLock sl (settingsLock);
        if (harmonyInput == juce::File()) return;
        harmonyInput = juce::File();
    }
    notifyHost (*this);
}

const StemSplitterProcessor::HarmonyResults& StemSplitterProcessor::harmonyResults() const
{
    using Step = HarmonyFlow::Step;
    auto& c = harmonyResultsCache;
    const auto step = harmonyFlow.getStep();
    const auto input = step == Step::idle ? getHarmonyInput() : harmonyFlow.getSong();   // idle: a reopened window's song
    const auto offer = getLastInput();
    const auto now = juce::Time::getMillisecondCounter();
    if (! c.valid || c.step != step || c.input != input || c.offer != offer || now - c.at >= 1000)
    {
        c.step = step; c.input = input; c.offer = offer; c.at = now; c.valid = true;
        c.dir = input == juce::File() ? juce::File() : step == Step::idle ? SeparationJob::stemDirFor (input, "htdemucs") : harmonyFlow.getStemDir();
        c.quality = c.dir == juce::File() ? juce::String() : HarmonyJob::qualityOf (c.dir);   // empty without a result: one read says both
        c.offerHasResult = offer != juce::File() && HarmonyJob::hasResult (SeparationJob::stemDirFor (offer, "htdemucs"));
    }
    return c;
}

void StemSplitterProcessor::applyJobResults (juce::File& dir, juce::String& python) const
{
    const auto s = job.getState();
    if (s == JobState::Done && dir == juce::File())
        dir = job.getStemDir();
    if ((s == JobState::Done || (s == JobState::Failed && job.getErrorKind() != ErrorKind::PythonMissing))
        && job.getPythonExe().isNotEmpty())
        python = job.getPythonExe();
    if (product == "harmonies")
        if (const auto found = harmonyFlow.getPythonExe(); found.isNotEmpty())   // the Python Harmonies' split found
            python = found;
}

void StemSplitterProcessor::syncWithJob()
{
    const juce::ScopedLock sl (settingsLock);
    const auto oldDir = stemDir;
    applyJobResults (stemDir, pythonPath);
    if (stemDir != oldDir) stemDirHasWav = hasWav (stemDir);
}

void StemSplitterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree s ("StemSplitter");
    {
        const juce::ScopedLock sl (settingsLock);
        auto dir = stemDir;
        auto python = pythonPath;
        applyJobResults (dir, python);
        s.setProperty ("theme", theme, nullptr);
        s.setProperty ("model", model, nullptr);
        s.setProperty ("python", python, nullptr);
        s.setProperty ("input", lastInput.getFullPathName(), nullptr);
        s.setProperty ("stems", dir.getFullPathName(), nullptr);
        s.setProperty ("product", product, nullptr);
        s.setProperty ("harmonyQuality", harmonyQuality, nullptr);
        s.setProperty ("harmonyInput", harmonyInput.getFullPathName(), nullptr);
    }
    if (auto xml = s.createXml()) copyXmlToBinary (*xml, dest);
}

void StemSplitterProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto s = juce::ValueTree::fromXml (*xml);
    auto in = s.getProperty ("input").toString();
    auto st = s.getProperty ("stems").toString();
    auto hin = s.getProperty ("harmonyInput").toString();

    const juce::ScopedLock sl (settingsLock);
    theme      = s.getProperty ("theme", "dusk").toString();
    model      = s.getProperty ("model", "htdemucs").toString();
    pythonPath = s.getProperty ("python", "").toString();
    lastInput  = juce::File::isAbsolutePath (in) ? juce::File (in) : juce::File();
    stemDir    = juce::File::isAbsolutePath (st) ? juce::File (st) : juce::File();
    stemDirHasWav = hasWav (stemDir);
    product        = s.getProperty ("product").toString() == "harmonies" ? "harmonies" : "stems";
    harmonyQuality = s.getProperty ("harmonyQuality").toString() == "best" ? "best" : "quick";
    harmonyInput   = juce::File::isAbsolutePath (hin) ? juce::File (hin) : juce::File();
}
