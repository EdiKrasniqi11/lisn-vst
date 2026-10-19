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
    for (auto* s : std::initializer_list<juce::Component*> { &drop, &splitting, &stems, &harmonyDrop, &harmonySplitting, &harmony, &error, &help, &updatePanel })
        panelContent.addChildComponent (s);
    addAndMakeVisible (info);

    header.onTheme = [this] (juce::String id) { proc.setTheme (id); applyTheme (themeFor (id)); };
    header.onSixStems = [this] (bool six) { mixJob.cancel(); proc.setSixStems (six); };   // which may re-split
    header.onHarmonyQuality = [this] (bool best)
    {
        const juce::String q = best ? "best" : "quick";
        const bool onResult = shown.has_value() && shown->screen == Screen::Harmonies;
        if (onResult && ! proc.getHarmonyInput().existsAsFile())
            return refreshQuality();                 // a song that's gone keeps its result, and the switch its quality
        proc.setHarmonyQuality (q);
        if (onResult && shown->harmonyQuality != q)
            startHarmonies (proc.getHarmonyInput(), false);   // redone at the new quality, as the 4/6 switch re-splits
        else
            refreshQuality();                        // (a redo's progress screen refreshes it)
    };
    info.onClick = [this] { setHelpOpen (! helpOpen); };
    help.onClose = [this] { setHelpOpen (false); };
    const auto browse = [this] { choose ("Choose a song", audioPatterns, [this] (const juce::File& f) { startSplit (f); }); };
    drop.onBrowse = browse;
    stems.onNewSong = browse;
    splitting.onCancel = [this] { proc.job.cancel(); };
    const auto browseHarmonies = [this] { choose ("Choose a song", audioPatterns, [this] (const juce::File& f) { startHarmonies (f); }); };
    harmonyDrop.onBrowse = browseHarmonies;
    harmony.onNewSong = browseHarmonies;
    harmonyDrop.onOffer = [this] { startHarmonies (proc.getLastInput()); };
    harmonySplitting.onCancel = [this] { proc.harmonyFlow.cancel(); };   // back to its drop screen; stems made stay cached
    stems.onPlayPause = [this]
    {
        if (proc.player.isPlaying()) proc.player.pause();
        else                         proc.player.play();
    };
    stems.onToggleMute = [this] (int i) { proc.player.setMuted (i, ! proc.player.isMuted (i)); };
    stems.onSolo = [this] (int i) { proc.player.solo (i); };
    stems.onSpeed = [this] (double s) { proc.player.setSpeed (s); };
    stems.onPitch = [this] (int st) { proc.player.setPitch (st); };
    stems.onVolume = [this] (int i, float v) { proc.player.setVolume (i, v); };
    stems.onSeek = [this] (double f) { proc.player.setPositionFraction (f); };
    stems.onSetLoop = [this] (double a, double b) { proc.player.setLoop (a, b); };
    stems.onToggleLoop = [this] { proc.player.setLooping (! proc.player.isLooping()); };
    stems.onHarmony = [this]                         // the choir icon: this song's harmonies (a result that's there opens)
    {
        if (jobRunning()) return;                    // e.g. a double click: the first one started it
        proc.setProduct ("harmonies");
        startHarmonies (proc.getLastInput());
        header.spinTo (true, true);                  // onLanded slides over
    };
    // The Harmonies screen: the player holds Lead and Harmonies while it shows, so lane i is stem i.
    harmony.onBack = [this] { switchProduct (false); };
    harmony.onPlayPause = [this] { stems.onPlayPause(); };
    harmony.onSolo = [this] (int i) { proc.player.solo (i); };
    harmony.onToggleMute = [this] (int i) { proc.player.setMuted (i, ! proc.player.isMuted (i)); };
    harmony.onVolume = [this] (int i, float v) { proc.player.setVolume (i, v); };
    harmony.onSeek = [this] (double f) { proc.player.setPositionFraction (f); };
    harmony.mixFile = [this] { return renderFile (proc.player.mixGains(), 1.0); };
    header.onName = [this] { switchProduct (proc.getProduct() != "harmonies"); };
    header.onLanded = [this] { slideTo (header.showsHarmonies() ? 1.0f : 0.0f, true); };
    stems.mixFile = [this]   // the mix: what you hear. A stretched mix only comes from mixJob's cache, never rendered here
    {
        const auto speed = proc.player.getSpeed();
        const auto pitch = proc.player.getPitch();
        if (speed == 1.0 && pitch == 0) return renderFile (proc.player.mixGains(), 1.0);
        const auto dest = renderRequest (proc.player.mixGains(), speed, pitch).dest;
        return dest.existsAsFile() ? dest : juce::File();
    };
    stems.stemFile = [this] (int i)   // single stems are the originals: gain 1, speed 1
    {
        if (! proc.player.isLooping()) return stems.fileOf (i);
        std::vector<float> one ((size_t) proc.player.getFiles().size(), 0.0f);
        if (juce::isPositiveAndBelow (i, (int) one.size())) one[(size_t) i] = 1.0f;
        return renderFile (one, 1.0);
    };
    error.onRetry = [this] { retry(); };
    error.onFindPython = [this]
    {
       #if JUCE_MAC
        choose ("Find python3", "*", [this] (const juce::File& f)   // python3 has no extension
       #else
        choose ("Find python.exe", "*.exe", [this] (const juce::File& f)
       #endif
        {
            proc.setPythonPath (f.getFullPathName());
            retry();
        });
    };

    // A newer LISN: the header's update pill opens What's new in the panel, and Update now downloads and installs it.
    // UpdateCheck and Updater only in the plugin and the app: the tests (no plugin wrapper) stay offline and drive the panel.
    using Wrapper = juce::AudioProcessor::WrapperType;
    const auto wrapper = juce::PluginHostType::getPluginLoadedAs();
    live = wrapper != Wrapper::wrapperType_Undefined;
    standaloneApp = wrapper == Wrapper::wrapperType_Standalone;
    header.onUpdate = [this] { setUpdateOpen (! updateOpen); };
    updatePanel.onLater = [this] { setUpdateOpen (false); };   // a download goes on
    updatePanel.onUpdate = [this]
    {
        if (! live) return;
        const auto& r = UpdateCheck::getInstance()->getRelease();
        if (r.installerUrl.isEmpty()) juce::URL (r.pageUrl).launchInDefaultBrowser();   // published without this installer
        else                          Updater::getInstance()->startDownload (r);
    };
    updatePanel.onCancel = [this] { if (live) Updater::getInstance()->cancel(); };
    updatePanel.onRetry = [this]
    {
        if (! live) return;
        Updater::getInstance()->reset();
        Updater::getInstance()->startDownload (UpdateCheck::getInstance()->getRelease());   // a file whose hash matches is reused
    };
    // Install. After "close the DAW" or an engine failure the file is still there: reset, and startDownload finds it (with a
    // hash) and is ready at once, so this one click installs again. Without a hash it downloads first.
    updatePanel.onInstall = [this]
    {
        if (! live) return;
        auto& u = *Updater::getInstance();
        if (u.getStep() == Updater::Step::failed)
        {
            u.reset();
            u.startDownload (UpdateCheck::getInstance()->getRelease());
        }
        if (u.getStep() != Updater::Step::ready) return;
        // The engine's ready.txt: python is <engine>/venv/Scripts/python.exe (bin/python3 on the Mac).
        const auto python = proc.getPythonPath();
        u.install (juce::File::isAbsolutePath (python)
                       ? juce::File (python).getParentDirectory().getParentDirectory().getParentDirectory().getChildFile ("ready.txt")
                       : juce::File());
    };
    updatePanel.onWebsite = [this]
    {
        juce::URL ("https://github.com/EdiKrasniqi11/lisn-vst/releases/latest/download/" + installerAssetName (standaloneApp)).launchInDefaultBrowser();
    };
    if (live)
        UpdateCheck::getInstance()->addChangeListener (this);

    setSize (760, 500);
    tick();   // first screen and theme before the window shows
    if (! proc.introShown && introEnabled())
    {
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
        // Clicks reach the editor (which skips the intro) instead of a control that is still faded out.
        header.setInterceptsMouseClicks (! introPlaying, ! introPlaying);
        panelContent.setInterceptsMouseClicks (false, ! introPlaying);
        info.setInterceptsMouseClicks (! introPlaying, false);
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
    {
        introStart = now;
        proc.introShown = true;                     // spent on the first frame shown, not on a view the host never shows
    }
    setIntroTime ((float) ((now - introStart) / 1000.0));
}

StemSplitterEditor::~StemSplitterEditor()
{
    if (auto* check = UpdateCheck::getInstanceWithoutCreating())
        check->removeChangeListener (this);
    proc.player.pause();
}

void StemSplitterEditor::syncUpdate()
{
    if (! live) return;
    const auto version = UpdateCheck::getInstance()->getNewerVersion();
    const auto& u = *Updater::getInstance();
    using Step = Updater::Step;
    using View = UpdateScreen::View;
    const auto step = u.getStep();
    juce::String pill;
    if (step == Step::downloading)                       pill = "Downloading " + juce::String (juce::roundToInt (u.getProgress() * 100.0f)) + "%";
    else if (step == Step::installed && ! standaloneApp) pill = "Restart " + hostName() + " to finish";
    if (pill != pillText || version != pillVersion)    // showUpdate repaints: not every tick
    {
        pillText = pill;
        pillVersion = version;
        header.showUpdate (version, pill);
    }

    auto view = View::whatsNew;
    switch (step)
    {
        case Step::idle:                break;
        case Step::downloading:         view = View::downloading; break;
        case Step::ready:               view = View::ready; break;
        case Step::installing:          view = View::installing; break;
        case Step::installed:           view = View::installed; break;
        case Step::waitingForInstaller: view = View::waiting; break;
        case Step::failed:
            switch (u.getFailure())
            {
                case Updater::Failure::download: view = View::failedDownload; break;
                case Updater::Failure::inUse:    view = View::inUse; break;
                case Updater::Failure::engine:   view = View::engine; break;
                default:                         view = View::failedInstall; break;
            }
            break;
    }
    updatePanel.setView (view, u.getProgress(), jobRunning(), standaloneApp, hostName());

    // The desktop app quits for its installer (Windows: the installer reopens it; Mac: once the Installer is open).
    if (standaloneApp && step == Step::waitingForInstaller && ! quitAsked)
    {
        quitAsked = true;
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }
}

void StemSplitterEditor::loadScreenshots()
{
    if (shotsAsked || ! live) return;
    shotsAsked = true;
    const auto urls = UpdateCheck::getInstance()->getRelease().screenshots;
    if (urls.isEmpty()) return;
    shotPool.addJob ([urls, safe = juce::Component::SafePointer<StemSplitterEditor> (this)]
    {
        juce::Array<juce::Image> images;
        for (const auto& url : urls)
        {
            juce::MemoryBlock data;   // readEntireBinaryStream, with a timeout so a closing editor never waits long
            if (auto in = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                  .withConnectionTimeoutMs (5000).withNumRedirectsToFollow (5)))
                in->readIntoMemoryBlock (data);
            if (auto img = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize()); img.isValid())
                images.add (img);
        }
        if (! images.isEmpty())   // none: the panel keeps the LISN symbol
            juce::MessageManager::callAsync ([safe, images]
            {
                if (safe != nullptr) safe->updatePanel.setScreenshots (images);
            });
    });
}

bool StemSplitterEditor::updateBlocksJobs()
{
    if (! live || ! Updater::getInstance()->engineChanged()) return false;
    const bool right = shown.has_value() && shown->harmoniesProduct;
    error.setRestart (hostName());
    errorOnRight = right;
    pageScreen[right ? 1 : 0] = Screen::Error;       // until the screen changes (show() sets it again)
    helpOpen = updateOpen = false;
    layoutSlide();
    return true;
}

void StemSplitterEditor::resized()
{
    waves.setBounds (getLocalBounds());
    const auto lights = nativeButtons ? 64 : 0;      // the pills stay right-aligned at x 736
    header.setBounds (24 + lights, 16, 712 - lights, 44);
    panelContent.setBounds (panel);
    for (auto* s : std::initializer_list<juce::Component*> { &drop, &splitting, &stems, &harmonyDrop, &harmonySplitting, &harmony, &error, &help, &updatePanel })
        s->setBounds (panel.withZeroOrigin());
    layoutSlide();
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
    if (! shown.has_value() || mods.isCtrlDown() || mods.isAltDown())
        return false;                                // FL's shortcuts stay FL's
    const auto code = key.getKeyCode();
    if (shown->screen == Screen::Harmonies && code == juce::KeyPress::spaceKey)
    {
        harmony.onPlayPause();
        return true;
    }
    if (shown->screen != Screen::Stems)
        return false;
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
    if (! isKeyDown || ! shown.has_value() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    if (shown->screen == Screen::Harmonies)
        return juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey);
    if (shown->screen != Screen::Stems)
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
    // On the Harmonies screen it holds Lead + Harmonies instead, picking up where the same song's stems were.
    const bool onHarmonies = s.screen == Screen::Harmonies, onStems = s.screen == Screen::Stems;
    const auto wanted = onHarmonies ? harmony.getFiles() : onStems ? stems.getFiles() : juce::Array<juce::File>();
    const auto filesDir = onHarmonies ? HarmonyJob::dirFor (s.harmonyStemDir) : stems.getDir();
    if (! wanted.isEmpty() && filesDir != unloadableDir && proc.player.getFiles() != wanted)
    {
        if (! proc.player.load (wanted))
            unloadableDir = filesDir;
        else if (carry.has_value())
        {
            proc.player.setPositionFraction (carry->first);
            if (carry->second) proc.player.play();
        }
        carry.reset();
    }
    if (s.screen == Screen::Splitting)
        splitting.setProgress (s.progress);
    if (s.screen == Screen::HarmonySplitting)
        harmonySplitting.setStep (s.harmonyStep, (float) s.progress, s.downloading);
    // While a split or a Harmonies step runs, the switches and the name wait (file drops too: isInterestedInFileDrag).
    const bool busy = s.screen == Screen::Splitting || s.screen == Screen::HarmonySplitting || jobRunning();
    header.setEnabledSwitches (! busy);
    header.setNameClickable (! busy);
    syncUpdate();
    if (proc.getTheme() != appliedThemeId)           // e.g. the host restored another project
        applyTheme (themeFor (proc.getTheme()));

    auto& player = proc.player;
    if (player.takeReachedEnd())                     // the song played to its end: back to 0:00
        player.setPositionFraction (0.0);
    if (onHarmonies)
    {
        harmony.setPlayback (player.isPlaying(), player.getPositionFraction(), player.getLengthSeconds());
        harmony.setAudible (player.audibleMask());
        for (int i = 0; i < 2; ++i)
            harmony.setVolume (i, player.getVolume (i));
    }
    else if (onStems)
    {
        stems.setPlayback (player.isPlaying(), player.getPositionFraction(), player.getLengthSeconds());
        stems.setAudible (player.audibleMask());
        for (int i = 0; i < stems.numRows(); ++i)
            stems.setVolume (i, player.getVolume (i));
        stems.setSpeed (player.getSpeed());
        stems.setPitch (player.getPitch());
        stems.setLoop (player.getLoop(), player.isLooping());
        prepareMix();
    }

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
    const auto prev = shown;
    shown = s;
    const bool right = s.harmoniesProduct;
    pageScreen[right ? 1 : 0] = s.screen;
    if (s.screen == Screen::Error)
        errorOnRight = right;
    // A product the reel isn't on (a reopened window, a host restoring another project): its page at once, no spin, no
    // slide. A click on the name or the choir icon spins the reel first, and the panel slides once it lands.
    if (header.showsHarmonies() != right)
    {
        header.spinTo (right, false);
        slideTo (right ? 1.0f : 0.0f, false);
    }
    layoutSlide();

    const float tints[] = { 0.60f, 0.66f, 0.66f, 0.70f, 0.60f, 0.66f, 0.66f };   // by Screen: Drop, Splitting, Stems, Error,
    waves.setPanel (panel, 22.0f, tints[(int) s.screen]);                         // HarmonyDrop, HarmonySplitting, Harmonies
    header.setProduct (right);
    if (right)
    {
        harmonyDrop.setOffer (s.offerSong);
        harmonyDrop.setHighlighted (false);
        harmonySplitting.setSong (s.songName);
    }
    else                                             // StemSplitter's page keeps its screens while Harmonies shows
    {
        header.setSixStems (s.sixStems);
        drop.setSixStems (s.sixStems);
        drop.setHighlighted (false);
        splitting.setSong (s.songName, s.sixStems);
    }
    if (s.screen == Screen::Error)
        error.setError (s.error, s.errorText, s.pythonExe);
    if (s.screen == Screen::Harmonies && s.harmonyQuality.isNotEmpty())
        proc.setHarmonyQuality (s.harmonyQuality);   // the switch, the next run and New song all use the result's quality
    refreshQuality();

    // The player: the stems on Stems, Lead + Harmonies on Harmonies, nothing otherwise. Between a song's stems and its
    // Lead + Harmonies it picks up where it was (carry); switchProduct may have taken the position before a re-split.
    const bool playable = s.screen == Screen::Stems || s.screen == Screen::Harmonies;
    // The same song: its stem folders (4 or 6 stems, and Harmonies' 4) share the song's cache folder.
    const auto songOf = [] (const UiState& u) { return (u.screen == Screen::Harmonies ? u.harmonyStemDir : u.stemDir).getParentDirectory(); };
    const bool flips = playable && prev.has_value() && prev->screen != s.screen && songOf (*prev) == songOf (s)
                    && (prev->screen == Screen::Stems || prev->screen == Screen::Harmonies);
    if (s.screen != Screen::Stems || s.stemDir != stems.getDir() || flips)
        mixJob.cancel();                             // it renders the files the player is about to drop
    if (! flips)
        carry.reset();
    else if (! carry.has_value())
        carry = std::make_pair (proc.player.getPositionFraction(), proc.player.isPlaying());
    if (! playable)
    {
        proc.player.unload();
        unloadableDir = juce::File();
    }
    if (s.screen == Screen::Stems && s.stemDir != stems.getDir())
        stems.setStems (s.stemDir, s.songName);
    if (s.screen == Screen::Stems)
        stems.setSongAvailable (! s.songGone);
    if (s.screen == Screen::Harmonies)
        harmony.setSong (s.harmonyStemDir, s.songName, s.harmonyQuality);
}

void StemSplitterEditor::setHelpOpen (bool open)
{
    helpOpen = open;
    updateOpen = updateOpen && ! open;
    showScreen();
}

void StemSplitterEditor::setUpdateOpen (bool open)
{
    updateOpen = open;
    helpOpen = helpOpen && ! open;
    if (open && live)
    {
        updatePanel.setRelease (UpdateCheck::getInstance()->getRelease(), JucePlugin_VersionString);
        loadScreenshots();
        syncUpdate();
    }
    showScreen();
}

void StemSplitterEditor::showScreen()
{
    // Each page shows its last screen while any of it is in the panel.
    const auto on = [this] (bool right, Screen x)
    {
        return ! helpOpen && ! updateOpen && shown.has_value() && pageScreen[right ? 1 : 0] == x && (right ? slide > 0.0f : slide < 1.0f);
    };
    drop.setVisible (on (false, Screen::Drop));
    splitting.setVisible (on (false, Screen::Splitting));
    stems.setVisible (on (false, Screen::Stems));
    harmonyDrop.setVisible (on (true, Screen::HarmonyDrop));
    harmonySplitting.setVisible (on (true, Screen::HarmonySplitting));
    harmony.setVisible (on (true, Screen::Harmonies));
    error.setVisible (on (errorOnRight, Screen::Error));
    // The page being left (during the spin and the slide) takes no clicks: the player already holds the other page's files.
    const bool right = shown.has_value() && shown->harmoniesProduct;
    const bool settled = std::abs (slide - (right ? 1.0f : 0.0f)) < 0.001f;
    for (auto* c : std::initializer_list<juce::Component*> { &drop, &splitting, &stems })
        c->setInterceptsMouseClicks (settled && ! right, settled && ! right);
    for (auto* c : std::initializer_list<juce::Component*> { &harmonyDrop, &harmonySplitting, &harmony })
        c->setInterceptsMouseClicks (settled && right, settled && right);
    error.setInterceptsMouseClicks (settled && errorOnRight == right, settled && errorOnRight == right);
    help.setVisible (helpOpen);
    updatePanel.setVisible (updateOpen);
    info.setToggleState (helpOpen, juce::dontSendNotification);
}

void StemSplitterEditor::refreshQuality()
{
    const bool best = proc.getHarmonyQuality() == "best";
    const bool bestMissing = HarmonyJob::findModel ("best", proc.getPythonPath()) == juce::File();
    header.setHarmonyQuality (best);
    header.setBestTooltip (bestMissing);
    harmonyDrop.setBestNote (best && bestMissing);
}

void StemSplitterEditor::applyTheme (const Theme& t)
{
    appliedThemeId = t.id;
    waves.setTheme (t);
    header.setTheme (t);
    drop.setTheme (t);
    splitting.setTheme (t);
    stems.setTheme (t);
    harmonyDrop.setTheme (t);
    harmonySplitting.setTheme (t);
    harmony.setTheme (t);
    error.setTheme (t);
    help.setTheme (t);
    updatePanel.setTheme (t);
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
    if (files.size() != 1 || jobRunning()) return false;
    const juce::File f (files[0]);
    const auto harmonies = shown.has_value() ? shown->harmonyStemDir : juce::File();
    return f.hasFileExtension ("wav;mp3;flac;aif;aiff;ogg")
        && ! f.isAChildOf (stems.getDir()) && ! f.isAChildOf (harmonies);   // not our own stems dragged back
}

void StemSplitterEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    drop.setHighlighted (true);
    harmonyDrop.setHighlighted (true);
}

void StemSplitterEditor::fileDragExit (const juce::StringArray&)
{
    drop.setHighlighted (false);
    harmonyDrop.setHighlighted (false);
}

void StemSplitterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    setHelpOpen (false);
    setUpdateOpen (false);
    drop.setHighlighted (false);
    harmonyDrop.setHighlighted (false);
    const juce::File f (files[0]);
    if (proc.getProduct() == "harmonies") startHarmonies (f);
    else                                  startSplit (f);
}

// The song on screen follows the switch. To Harmonies: StemSplitter's split song opens its harmonies when it has them, else
// Harmonies' drop screen offers it. To StemSplitter: the song on the Harmonies screen is split there too (a cache hit
// when it was split at StemSplitter's 4/6 before).
void StemSplitterEditor::switchProduct (bool toHarmonies)
{
    if (jobRunning()) return;                        // the name waits (it may still look clickable for up to a tick)
    const auto stemsSong = proc.getLastInput();
    if (toHarmonies && proc.hasStems() && stemsSong.existsAsFile())
    {
        if (HarmonyJob::hasResult (SeparationJob::stemDirFor (stemsSong, "htdemucs")))
            startHarmonies (stemsSong);
        else
            proc.clearHarmonySong();
    }
    else if (! toHarmonies && shown.has_value() && shown->screen == Screen::Harmonies)
    {
        const auto song = proc.getHarmonyInput();
        if (song != stemsSong && song.existsAsFile())
        {
            carry = std::make_pair (proc.player.getPositionFraction(), proc.player.isPlaying());   // before the split unloads it
            startSplit (song);
        }
    }
    proc.setProduct (toHarmonies ? "harmonies" : "stems");
    header.spinTo (toHarmonies, true);               // onLanded slides
}

void StemSplitterEditor::slideTo (float target, bool animate)
{
    slideFrom = slide;
    slideTarget = target;
    slideStart = juce::Time::getMillisecondCounterHiRes();
    if (! animate || juce::exactlyEqual (slide, target))
    {
        slideFrames.reset();
        slide = target;
        layoutSlide();
        return;
    }
    if (slideFrames == nullptr)
        slideFrames = std::make_unique<juce::VBlankAttachment> (this, [this, safe = juce::Component::SafePointer<StemSplitterEditor> (this)]
        {
            const auto t = juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - slideStart) / 700.0);
            const auto e = t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow (-2.0 * t + 2.0, 3.0) / 2.0;   // ease in-out (cubic)
            slide = slideFrom + (slideTarget - slideFrom) * (float) e;
            layoutSlide();
            if (t >= 1.0)   // not from inside its own callback
                juce::MessageManager::callAsync ([safe]
                {
                    if (safe != nullptr && juce::exactlyEqual (safe->slide, safe->slideTarget)) safe->slideFrames.reset();
                });
        });
}

void StemSplitterEditor::layoutSlide()
{
    const auto x = juce::roundToInt (slide * (float) panel.getWidth());
    for (auto* c : std::initializer_list<juce::Component*> { &drop, &splitting, &stems })
        c->setTopLeftPosition (-x, 0);
    for (auto* c : std::initializer_list<juce::Component*> { &harmonyDrop, &harmonySplitting, &harmony })
        c->setTopLeftPosition (panel.getWidth() - x, 0);
    error.setTopLeftPosition (errorOnRight ? panel.getWidth() - x : -x, 0);
    showScreen();
}

// Every start goes through these two, and nothing starts while a split or a Harmonies step runs (a double click, a file
// chooser that returns after a job started, a click on the page being left while it slides).
void StemSplitterEditor::startSplit (const juce::File& input)
{
    if (jobRunning() || updateBlocksJobs()) return;
    mixJob.cancel();
    proc.setProduct ("stems");                       // its progress and Cancel are on StemSplitter's page (show() snaps there)
    proc.startSplit (input);
}

void StemSplitterEditor::startHarmonies (const juce::File& song, bool keepExistingResult)
{
    if (jobRunning() || updateBlocksJobs()) return;
    if (! song.existsAsFile())
    {
        proc.clearHarmonySong();                     // never a flow on nothing
        return;
    }
    if (! keepExistingResult)
        proc.player.unload();                        // the redo replaces the Lead + Harmonies files it may hold
    proc.setProduct ("harmonies");                   // its progress and Cancel are on Harmonies' page (show() snaps there)
    proc.startHarmonies (song, keepExistingResult);
}

void StemSplitterEditor::retry()
{
    if (proc.getProduct() == "harmonies") startHarmonies (proc.getHarmonyInput(), false);   // a failed redo runs again
    else                                  startSplit (proc.getLastInput());
}

bool StemSplitterEditor::jobRunning() const
{
    const auto step = proc.harmonyFlow.getStep();
    return proc.job.getState() == JobState::Running || step == HarmonyFlow::Step::vocals || step == HarmonyFlow::Step::harmonies;
}

RenderJob::Request StemSplitterEditor::renderRequest (const std::vector<float>& gains, double speed, int semitones)
{
    auto& p = proc.player;
    if (! shown.has_value()) return {};
    const auto files = p.getFiles();
    juce::StringArray names;
    for (const auto& f : files) names.add (f.getFileNameWithoutExtension());
    const auto len = p.getLengthSeconds();
    const auto loopSec = p.isLooping() ? juce::Range<double> (p.getLoop().getStart() * len, p.getLoop().getEnd() * len)
                                       : juce::Range<double>();
    const auto name = StemPlayer::renderName (shown->songName, names, gains, speed, loopSec, semitones);
    if (name.isEmpty()) return {};
    const auto from = p.isLooping() ? loopSec.getStart() : 0.0, to = p.isLooping() ? loopSec.getEnd() : len;
    const auto dir = shown->screen == Screen::Harmonies ? shown->harmonyStemDir : stems.getDir();
    return { files, gains, from, to, speed, dir.getChildFile ("renders").getChildFile (name), semitones };
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
    const auto pitch = p.getPitch();
    if (speed == 1.0 && pitch == 0)
    {
        mixJob.cancel();
        stems.setMixChip (plain, std::nullopt);
        return;
    }
    const auto r = renderRequest (p.mixGains(), speed, pitch);
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
