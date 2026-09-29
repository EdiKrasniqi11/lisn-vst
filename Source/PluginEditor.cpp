#include "PluginEditor.h"

namespace
{
    const juce::Rectangle<int> panel { 24, 76, 712, 400 };
    const char* const audioPatterns = "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg";
}

StemSplitterEditor::StemSplitterEditor (StemSplitterProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    // Only the editor takes the keyboard, when anything in it is clicked; its children never do. The listener sees the
    // clicks on every child, including buttons, which don't grab focus themselves.
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
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
    stems.onPlayPause = [this]
    {
        if (proc.player.isPlaying()) proc.player.pause();
        else                         proc.player.play();
    };
    stems.onToggleMute = [this] (int i) { proc.player.setMuted (i, ! proc.player.isMuted (i)); };
    stems.onSolo = [this] (int i) { proc.player.solo (i); };
    stems.onSeek = [this] (double f) { proc.player.setPositionFraction (f); };
    stems.onSetLoop = [this] (double a, double b) { proc.player.setLoop (a, b); };
    stems.onToggleLoop = [this] { proc.player.setLooping (! proc.player.isLooping()); };
    stems.mixFile = [this] { return renderFile (proc.player.audibleMask()); };
    stems.stemFile = [this] (int i) { return proc.player.isLooping() ? renderFile (1u << i) : stems.fileOf (i); };
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
    proc.player.pause();
}

void StemSplitterEditor::resized()
{
    waves.setBounds (getLocalBounds());
    header.setBounds (24, 16, 712, 44);
    for (auto* s : std::initializer_list<juce::Component*> { &drop, &splitting, &stems, &error })
        s->setBounds (panel);
}

void StemSplitterEditor::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool StemSplitterEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! shown.has_value() || shown->screen != Screen::Stems || mods.isCtrlDown() || mods.isAltDown())
        return false;                                // FL's shortcuts stay FL's
    const auto code = key.getKeyCode();
    auto& p = proc.player;
    if (code == juce::KeyPress::spaceKey)
        stems.onPlayPause();
    else if (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code) == 'L')
        stems.toggleLoop();
    else if (code == juce::KeyPress::homeKey)
        p.setPositionFraction (p.isLooping() ? p.getLoop().getStart() : 0.0);
    else if (code >= '1' && code <= '6')
    {
        const int i = code - '1';
        if (i < stems.numRows())
        {
            if (mods.isShiftDown()) stems.onSolo (i);
            else                    stems.onToggleMute (i);
        }
    }
    else
        return false;
    return true;
}

bool StemSplitterEditor::keyStateChanged (bool isKeyDown)
{
    // Windows sends Space, letters and digits as a key-down and then a character. JUCE turns the character into keyPressed()
    // but hands the key-down to the host unless it's used here, so claim the key-downs of the keys keyPressed() takes:
    // otherwise FL would also start its own transport on the same Space. ponytail: while one of these keys is held, any
    // other key-down is claimed too. Upgrade path: remember which key keyPressed() took and claim only that one.
    // ponytail: isKeyCurrentlyDown reads the live key state (GetAsyncKeyState), so a UI stall longer than a tap can let the
    // key-down leak to FL. Upgrade path: ::GetKeyState (message-synchronous), or the remembered key above.
    const auto mods = juce::ModifierKeys::currentModifiers;
    if (! isKeyDown || ! shown.has_value() || shown->screen != Screen::Stems || mods.isCtrlDown() || mods.isAltDown())
        return false;
    for (const int k : { (int) juce::KeyPress::spaceKey, (int) 'L', (int) '1', (int) '2', (int) '3', (int) '4', (int) '5', (int) '6' })
        if (juce::KeyPress::isKeyCurrentlyDown (k))
            return true;
    return false;
}

void StemSplitterEditor::tick()
{
    proc.syncWithJob();
    const auto s = forced.has_value() ? *forced : uiStateFor (proc);
    if (! shown.has_value() || ! s.sameScreenAs (*shown))
        show (s);
    // The player always holds the stems on screen: a reopened window keeps its mix, position and loop (same files), and a
    // re-split of the same song (same stem dir, so the screen didn't change) reloads what startSplit() unloaded.
    if (s.screen == Screen::Stems && stems.getDir() != unloadableDir && proc.player.getFiles() != stems.getFiles()
        && ! stems.getFiles().isEmpty() && ! proc.player.load (stems.getFiles()))
        unloadableDir = stems.getDir();
    if (s.screen == Screen::Splitting)
        splitting.setProgress (s.progress);
    if (proc.getTheme() != appliedThemeId)           // e.g. the host restored another project
        applyTheme (themeFor (proc.getTheme()));

    auto& player = proc.player;
    if (player.takeReachedEnd())                     // the song played to its end: back to 0:00
        player.setPositionFraction (0.0);
    stems.setPlayback (player.isPlaying(), player.getPositionFraction(), player.getLengthSeconds());
    stems.setAudible (player.audibleMask());
    stems.setLoop (player.getLoop(), player.isLooping());

    // Beat motion: fast attack, slow release; after the player stops the waves settle and stop.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) / 1000.0);
    lastTick = now;
    if (player.isPlaying())
    {
        const auto peak = player.takePeak();
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
    {
        proc.player.unload();
        unloadableDir = {};
    }
    else if (s.stemDir != stems.getDir())
        stems.setStems (s.stemDir, s.songName);
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

juce::File StemSplitterEditor::renderFile (juce::uint32 mask)
{
    auto& p = proc.player;
    const auto files = p.getFiles();
    juce::StringArray names;
    for (int i = 0; i < files.size(); ++i)
        if ((mask >> i) & 1u) names.add (files[i].getFileNameWithoutExtension());
    if (names.isEmpty() || ! shown.has_value()) return {};

    const auto len = p.getLengthSeconds();
    const auto from = p.isLooping() ? p.getLoop().getStart() * len : 0.0, to = p.isLooping() ? p.getLoop().getEnd() * len : len;
    auto clock = [] (double sec) { const auto cs = juce::jmax (0, (int) (sec * 100)); return juce::String (cs / 6000) + "." + juce::String (cs / 100 % 60).paddedLeft ('0', 2) + "." + juce::String (cs % 100).paddedLeft ('0', 2); };
    auto name = shown->songName.upToLastOccurrenceOf (".", false, false) + " - " + names.joinIntoString ("+");
    if (p.isLooping()) name << " (" << clock (from) << "-" << clock (to) << ")";
    const auto dest = stems.getDir().getChildFile ("renders").getChildFile (juce::File::createLegalFileName (name + ".wav"));
    // ponytail: renders on the drag gesture (a loop takes milliseconds, a whole song under a second). Upgrade path: render
    // in the background whenever the mutes or the loop change.
    return StemPlayer::render (files, mask, from, to, dest) ? dest : juce::File();
}
