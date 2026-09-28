#include "PluginEditor.h"

namespace
{
    const juce::Rectangle<int> panel { 24, 76, 712, 400 };
    const char* const audioPatterns = "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg";
}

StemSplitterEditor::StemSplitterEditor (StemSplitterProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setWantsKeyboardFocus (false);
    addAndMakeVisible (waves);
    addAndMakeVisible (header);
    for (auto* s : std::initializer_list<juce::Component*> { &drop, &splitting, &stems, &error })
        addChildComponent (s);

    header.onTheme = [this] (juce::String id) { proc.setTheme (id); applyTheme (themeFor (id)); };
    header.onSixStems = [this] (bool six) { proc.setSixStems (six); };
    const auto browse = [this] { choose ("Choose a song", audioPatterns, [this] (const juce::File& f) { proc.startSplit (f); }); };
    drop.onBrowse = browse;
    stems.onNewSong = browse;
    splitting.onCancel = [this] { proc.job.cancel(); };
    stems.onPlayPause = [this] (int row) { playPause (row); };
    stems.onSeek = [this] (int row, double f)
    {
        if (row == stems.rowOf (proc.preview.getFile()))
            proc.preview.setPositionFraction (f);
    };
    error.onRetry = [this] { proc.startSplit (proc.getLastInput()); };
    error.onFindPython = [this]
    {
        choose ("Find python.exe", "*.exe", [this] (const juce::File& f)
        {
            proc.setPythonPath (f.getFullPathName());
            proc.startSplit (proc.getLastInput());
        });
    };

    setSize (760, 500);
    tick();   // first screen and theme before the window shows
    startTimerHz (30);
}

StemSplitterEditor::~StemSplitterEditor()
{
    proc.preview.pause();
}

void StemSplitterEditor::resized()
{
    waves.setBounds (getLocalBounds());
    header.setBounds (24, 16, 712, 44);
    for (auto* s : std::initializer_list<juce::Component*> { &drop, &splitting, &stems, &error })
        s->setBounds (panel);
}

void StemSplitterEditor::tick()
{
    proc.syncWithJob();
    const auto s = forced.has_value() ? *forced : uiStateFor (proc);
    if (! shown.has_value() || ! s.sameScreenAs (*shown))
        show (s);
    if (s.screen == Screen::Splitting)
        splitting.setProgress (s.progress);
    if (proc.getTheme() != appliedThemeId)           // e.g. the host restored another project
        applyTheme (themeFor (proc.getTheme()));

    auto& preview = proc.preview;
    if (preview.takeReachedEnd())                    // a stem that played to its end goes back to 0:00
        preview.setPositionFraction (0.0);
    stems.setPlayback (stems.rowOf (preview.getFile()), preview.isPlaying(), preview.getPositionFraction(), preview.getLengthSeconds());

    // Beat motion: fast attack, slow release; after the preview stops the waves settle and stop.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) / 1000.0);
    lastTick = now;
    if (preview.isPlaying())
    {
        const auto peak = preview.takePeak();
        pulse = peak > pulse ? peak : pulse + (peak - pulse) * juce::jmin (1.0f, dt * 5.0f);
        flow += dt * (8.0f + 60.0f * pulse);
        waves.setMotion (flow, pulse);
    }
    else if (pulse > 0.01f)
    {
        pulse *= std::pow (0.05f, dt);
        flow += dt * 8.0f * pulse;
        waves.setMotion (flow, pulse);
    }
    else if (pulse != 0.0f)
    {
        pulse = 0.0f;
        waves.setMotion (flow, 0.0f);
    }
}

void StemSplitterEditor::show (const UiState& s)
{
    shown = s;
    drop.setVisible (s.screen == Screen::Drop);
    splitting.setVisible (s.screen == Screen::Splitting);
    stems.setVisible (s.screen == Screen::Stems);
    error.setVisible (s.screen == Screen::Error);

    const float tints[] = { 0.60f, 0.66f, 0.66f, 0.70f };   // Drop, Splitting, Stems, Error
    waves.setPanel (panel, 22.0f, tints[(int) s.screen]);
    header.setEnabledSwitches (s.screen != Screen::Splitting);
    header.setSixStems (s.sixStems);
    drop.setSixStems (s.sixStems);
    drop.setHighlighted (false);
    splitting.setSong (s.songName, s.sixStems);
    error.setError (s.error, s.errorText, s.pythonExe);

    if (s.screen != Screen::Stems)
        proc.preview.unload();
    else if (s.stemDir != stems.getDir())
    {
        if (! proc.preview.getFile().isAChildOf (s.stemDir))   // keeps a paused preview when the editor reopens
            proc.preview.unload();
        stems.setStems (s.stemDir, s.songName);
    }
}

void StemSplitterEditor::applyTheme (const Theme& t)
{
    appliedThemeId = t.id;
    waves.setTheme (t);
    header.setTheme (t);
    drop.setTheme (t);
    splitting.setTheme (t);
    stems.setTheme (t);
    error.setTheme (t);
}

void StemSplitterEditor::playPause (int row)
{
    auto& preview = proc.preview;
    if (row == stems.rowOf (preview.getFile()))
    {
        if (preview.isPlaying()) preview.pause();
        else                     preview.play();
    }
    else if (preview.load (stems.fileOf (row)))
    {
        preview.setPositionFraction (stems.positionOf (row));
        preview.play();
    }
}

void StemSplitterEditor::choose (const juce::String& title, const juce::String& patterns, std::function<void (const juce::File&)> then)
{
    chooser = std::make_unique<juce::FileChooser> (title, juce::File(), patterns);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [then] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  then (fc.getResult());
                          });
}

bool StemSplitterEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.size() != 1) return false;
    const juce::File f (files[0]);
    return f.hasFileExtension ("wav;mp3;flac;aif;aiff;ogg") && ! f.isAChildOf (stems.getDir());   // not our own stems dragged back
}

void StemSplitterEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    drop.setHighlighted (true);
}

void StemSplitterEditor::fileDragExit (const juce::StringArray&)
{
    drop.setHighlighted (false);
}

void StemSplitterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    drop.setHighlighted (false);
    proc.startSplit (juce::File (files[0]));
}
