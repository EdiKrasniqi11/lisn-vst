#include "PluginEditor.h"
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace
{
    const juce::Rectangle<int> panel { 24, 76, 712, 400 };
    constexpr float introEnd = 1.9f;                 // the header's fade ends: 1.3 s + 0.6 s

    bool introEnabled()                              // Windows' "Animation effects" setting
    {
       #if JUCE_WINDOWS
        BOOL on = FALSE;
        return SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &on, 0) && on;
       #else
        return true;
       #endif
    }

    // A component's image while the intro fades and moves it. Its own alpha or transform changes repaint it whole
    // (invalidateAll) and keep the image; a child's repaint (invalidate) redraws it. ponytail: the component's own
    // whole repaints (e.g. a theme change) wait for the intro's end. Upgrade path: invalidate it from applyTheme.
    struct FadeCache : juce::CachedComponentImage
    {
        explicit FadeCache (juce::Component& c) : owner (c) {}

        void paint (juce::Graphics& g) override
        {
            const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            const auto size = (owner.getLocalBounds().toFloat() * scale).getSmallestIntegerContainer();
            if (dirty || image.getBounds() != size)
            {
                image = juce::Image (juce::Image::ARGB, juce::jmax (1, size.getWidth()), juce::jmax (1, size.getHeight()), true,
                                     *g.getInternalContext().getPreferredImageTypeForTemporaryImages());
                juce::Graphics ig (image);
                ig.addTransform (juce::AffineTransform::scale (scale));
                owner.paintEntireComponent (ig, true);
                dirty = false;
            }
            if (owner.getAlpha() <= 0.0f)
                return;
            g.setOpacity (owner.getAlpha());
            g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale));
        }
        bool invalidateAll() override { return true; }
        bool invalidate (const juce::Rectangle<int>&) override { dirty = true; return true; }
        void releaseResources() override { image = {}; }

        juce::Component& owner;
        juce::Image image;
        bool dirty = true;
    };
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
    panelContent.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (panelContent);
    for (auto* s : std::initializer_list<juce::Component*> { &drop, &splitting, &stems, &error, &help })
        panelContent.addChildComponent (s);
    addAndMakeVisible (info);

    header.onTheme = [this] (juce::String id) { proc.setTheme (id); applyTheme (themeFor (id)); };
    header.onSixStems = [this] (bool six) { mixJob.cancel(); proc.setSixStems (six); };   // which may re-split
    info.onClick = [this] { setHelpOpen (! helpOpen); };
    help.onClose = [this] { setHelpOpen (false); };
    const auto browse = [this] { choose ("Choose a song", audioPatterns, [this] (const juce::File& f) { startSplit (f); }); };
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
    stems.onSpeed = [this] (double s) { proc.player.setSpeed (s); };
    stems.onVolume = [this] (int i, float v) { proc.player.setVolume (i, v); };
    stems.onSeek = [this] (double f) { proc.player.setPositionFraction (f); };
    stems.onSetLoop = [this] (double a, double b) { proc.player.setLoop (a, b); };
    stems.onToggleLoop = [this] { proc.player.setLooping (! proc.player.isLooping()); };
    stems.mixFile = [this]   // the mix: what you hear. A stretched mix only comes from mixJob's cache, never rendered here
    {
        const auto speed = proc.player.getSpeed();
        if (speed == 1.0) return renderFile (proc.player.mixGains(), 1.0);
        const auto dest = renderRequest (proc.player.mixGains(), speed).dest;
        return dest.existsAsFile() ? dest : juce::File();
    };
    stems.stemFile = [this] (int i)   // single stems are the originals: gain 1, speed 1
    {
        if (! proc.player.isLooping()) return stems.fileOf (i);
        std::vector<float> one ((size_t) proc.player.getFiles().size(), 0.0f);
        if (juce::isPositiveAndBelow (i, (int) one.size())) one[(size_t) i] = 1.0f;
        return renderFile (one, 1.0);
    };
    error.onRetry = [this] { startSplit (proc.getLastInput()); };
    error.onFindPython = [this]
    {
        choose ("Find python.exe", "*.exe", [this] (const juce::File& f)
        {
            proc.setPythonPath (f.getFullPathName());
            startSplit (proc.getLastInput());
        });
    };

    setSize (760, 500);
    tick();   // first screen and theme before the window shows
    if (! proc.introShown && introEnabled())
    {
        proc.introShown = true;
        setIntroTime (0.0f);
        introFrames = std::make_unique<juce::VBlankAttachment> (this, [this] { advanceIntro(); });
    }
    startTimerHz (30);
}

void StemSplitterEditor::setIntroTime (std::optional<float> seconds)
{
    if (seconds.has_value() && *seconds >= introEnd)
        seconds.reset();
    if (seconds.has_value() != introPlaying)
    {
        // While it plays, the header and the panel's content fade and move as images (no repaint per frame).
        introPlaying = seconds.has_value();
        for (auto* c : std::initializer_list<juce::Component*> { &header, &panelContent })
            c->setCachedComponentImage (introPlaying ? new FadeCache (*c) : nullptr);
    }
    waves.setIntro (seconds);
    // Board 4A: the screen sits in the panel and moves with it; the header and the info button fade in last (CSS `ease`).
    const auto t = seconds.value_or (introEnd);
    const auto fade = WaveBackground::cubicBezier (0.25f, 0.1f, 0.25f, 1.0f, (t - 1.3f) / 0.6f);
    header.setAlpha (fade);
    info.setAlpha (fade);
    panelContent.setAlpha (WaveBackground::panelIntro (t));
    panelContent.setTransform (juce::AffineTransform::translation (0.0f, (float) WaveBackground::panelDrop (t)));
}

void StemSplitterEditor::advanceIntro()
{
    if (! introPlaying)
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (introStart == 0.0)
        introStart = now;
    setIntroTime ((float) ((now - introStart) / 1000.0));
}

StemSplitterEditor::~StemSplitterEditor()
{
    proc.player.pause();
}

void StemSplitterEditor::resized()
{
    waves.setBounds (getLocalBounds());
    header.setBounds (24, 16, 712, 44);
    panelContent.setBounds (panel);
    for (auto* s : std::initializer_list<juce::Component*> { &drop, &splitting, &stems, &error, &help })
        s->setBounds (panel.withZeroOrigin());
    info.setBounds (716, 476, 24, 24);   // its 16 px ring centred under the panel's right edge, in the 24 px bottom margin
}

void StemSplitterEditor::mouseDown (const juce::MouseEvent&)
{
    if (introPlaying)                                // a click skips the intro
        setIntroTime ({});
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
    if (! introPlaying)
        introFrames.reset();
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
    for (int i = 0; i < stems.numRows(); ++i)
        stems.setVolume (i, player.getVolume (i));
    stems.setSpeed (player.getSpeed());
    stems.setLoop (player.getLoop(), player.isLooping());
    if (s.screen == Screen::Stems)
        prepareMix();

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
    showScreen();

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
        mixJob.cancel();
        proc.player.unload();
        unloadableDir = {};
    }
    else if (s.stemDir != stems.getDir())
    {
        mixJob.cancel();
        stems.setStems (s.stemDir, s.songName);
    }
}

void StemSplitterEditor::setHelpOpen (bool open)
{
    helpOpen = open;
    showScreen();
}

void StemSplitterEditor::showScreen()
{
    const auto is = [this] (Screen x) { return ! helpOpen && shown.has_value() && shown->screen == x; };
    drop.setVisible (is (Screen::Drop));
    splitting.setVisible (is (Screen::Splitting));
    stems.setVisible (is (Screen::Stems));
    error.setVisible (is (Screen::Error));
    help.setVisible (helpOpen);
    info.setToggleState (helpOpen, juce::dontSendNotification);
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
    help.setTheme (t);
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
    setHelpOpen (false);
    drop.setHighlighted (false);
    startSplit (juce::File (files[0]));
}

void StemSplitterEditor::startSplit (const juce::File& input)
{
    mixJob.cancel();
    proc.startSplit (input);
}

RenderJob::Request StemSplitterEditor::renderRequest (const std::vector<float>& gains, double speed)
{
    auto& p = proc.player;
    if (! shown.has_value()) return {};
    const auto files = p.getFiles();
    juce::StringArray names;
    for (const auto& f : files) names.add (f.getFileNameWithoutExtension());
    const auto len = p.getLengthSeconds();
    const auto loopSec = p.isLooping() ? juce::Range<double> (p.getLoop().getStart() * len, p.getLoop().getEnd() * len)
                                       : juce::Range<double>();
    const auto name = StemPlayer::renderName (shown->songName, names, gains, speed, loopSec);
    if (name.isEmpty()) return {};
    const auto from = p.isLooping() ? loopSec.getStart() : 0.0, to = p.isLooping() ? loopSec.getEnd() : len;
    return { files, gains, from, to, speed, stems.getDir().getChildFile ("renders").getChildFile (name) };
}

juce::File StemSplitterEditor::renderFile (const std::vector<float>& gains, double speed)
{
    const auto r = renderRequest (gains, speed);
    if (r.dest == juce::File() || r.dest.existsAsFile()) return r.dest;   // cached by name (FL keeps pointing at them)
    // ponytail: at speed 1 this renders on the drag gesture (a loop takes milliseconds, a whole song under a second).
    // Upgrade path: mixJob for these too.
    return StemPlayer::render (r.stems, r.gains, r.startSec, r.endSec, r.speed, r.dest) ? r.dest : juce::File();
}

void StemSplitterEditor::prepareMix()
{
    auto& p = proc.player;
    const juce::String plain = p.isLooping() ? "Drag loop" : "Drag mix";
    const auto speed = p.getSpeed();
    if (speed == 1.0)
    {
        mixJob.cancel();
        stems.setMixChip (plain, std::nullopt);
        return;
    }
    const auto r = renderRequest (p.mixGains(), speed);
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (r.dest != mixKey.dest || r.startSec != mixKey.startSec || r.endSec != mixKey.endSec)
    {
        mixKey = r;
        mixKeySince = now;
    }
    if (r.dest == juce::File())                      // every stem muted: nothing to drag
    {
        stems.setMixChip (plain, std::nullopt);
        return;
    }
    const bool exists = r.dest.existsAsFile();
    const auto busyOrFailed = [this, &r]
    {
        const auto st = mixJob.getState();
        return mixJob.getDest() == r.dest && (st == RenderJob::State::running || st == RenderJob::State::failed);
    };
    // A failed render isn't retried while it stays the job's last one (another render or a cancel clears it).
    if (! exists && now - mixKeySince >= 500.0 && ! busyOrFailed())
        mixJob.start (r);

    const bool mine = mixJob.getDest() == r.dest;
    const auto state = mixJob.getState();
    if (mine && state == RenderJob::State::running)
        stems.setMixChip ("Preparing " + juce::String (juce::roundToInt (mixJob.getProgress() * 100.0f)) + "%", mixJob.getProgress());
    else if (mine && state == RenderJob::State::failed)
        stems.setMixChip ("Can't prepare", std::nullopt);
    else if (exists)
        stems.setMixChip (plain, std::nullopt);
    else
        stems.setMixChip ("Preparing 0%", 0.0f);
}
