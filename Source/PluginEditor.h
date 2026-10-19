#pragma once
#include "HarmonyJob.h"
#include "PluginProcessor.h"
#include "RenderJob.h"
#include "Screens.h"
#include "UiState.h"
#include "UpdateCheck.h"
#include "Updater.h"
#include "PythonFinder.h"
#include "WaveBackground.h"
#include <optional>

// Fixed 760 x 500: wave background, header and one screen per UiState::Screen. A 30 Hz timer maps the processor to a
// UiState, updates the stem rows from the player and moves the waves while it plays. The panel has two pages: StemSplitter's
// screens on the left, Harmonies' on the right; the name reel switches products and the panel slides between the pages.
class StemSplitterEditor : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor&);
    ~StemSplitterEditor() override;                  // pauses the player
    void resized() override;

    // The keyboard (the editor takes it on any click; see the constructor). Space, L, 1-6, Shift+1-6 and Home on the Stems
    // screen, Space on the Harmonies screen; every other key returns false, and JUCE passes it on to the host.
    bool keyPressed (const juce::KeyPress&) override;
    // The desktop app's window: the header becomes its title bar (board 1A). With no callbacks (the Mac) macOS's own buttons
    // sit over the header's first 64 px, so it starts there.
    void makeTitleBar (std::function<void()> minimise, std::function<void()> close)
    {
        nativeButtons = ! minimise && ! close;
        header.makeTitleBar (std::move (minimise), std::move (close));
        resized();
    }
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setHelpOpen (bool);                         // the help sheet in the panel in place of the screen, the info button lit
    void setUpdateOpen (bool);                       // the update's What's new in its place (the header's pill); closes help

    bool isInterestedInFileDrag (const juce::StringArray&) override;   // one audio file
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // test hooks
    void forceState (const UiState& s) { forced = s; }   // the timer then shows this instead of uiStateFor (proc)
    void tick();                                     // one timer step
    // The opening animation's state at `seconds` (board 4A); none, or 1.9 s and later, is the end state and stops it.
    void setIntroTime (std::optional<float> seconds);
    WaveBackground& background() { return waves; }
    StemsScreen& stemsScreen() { return stems; }
    HelpSheet& helpSheet() { return help; }
    UpdateScreen& updateScreen() { return updatePanel; }
    void setEngineChangedForTests() { engineChangedForTests = true; }   // as if an install changed the engine
    HarmonyScreen& harmonyScreen() { return harmony; }
    ErrorScreen& errorScreen() { return error; }
    HarmonyDropScreen& harmonyDropScreen() { return harmonyDrop; }
    HarmonySplittingScreen& harmonySplittingScreen() { return harmonySplitting; }
    void setSlideForTests (float x) { slide = x; layoutSlide(); }   // a frame of the slide (0 = StemSplitter's page)
    Header& headerBar() { return header; }

private:
    void timerCallback() override { tick(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncUpdate(); }   // UpdateCheck found a newer LISN
    // The pill's text and the update panel's view from UpdateCheck and Updater (plugin and app only: the tests stay offline).
    void syncUpdate();
    void loadScreenshots();                          // the release's screenshots, once, on shotPool
    bool updateBlocksJobs();                         // an install changed the engine: the Error screen asks for a restart
    void advanceIntro();                             // one display frame of the intro
    void show (const UiState&);
    void showScreen();                               // the shown screen, or the help sheet in its place
    void applyTheme (const Theme&);
    // Those stems at those gains over the loop (or the song): what to render and its cached dest (none when nothing is heard).
    RenderJob::Request renderRequest (const std::vector<float>& gains, double speed, int semitones = 0);
    juce::File renderFile (const std::vector<float>& gains, double speed);   // renderRequest's dest, rendered here unless cached
    void prepareMix();                               // at a speed other than 1, renders the mix on mixJob; sets the chip
    // The name reel's switch: the song on screen comes along (see the .cpp), the reel spins, and the panel slides once it lands.
    void switchProduct (bool toHarmonies);
    void slideTo (float target, bool animate);       // 0 = StemSplitter's page, 1 = Harmonies'; 0.7 s, ease in-out
    void layoutSlide();
    void startSplit (const juce::File&);             // cancels mixJob first: it must not hold stems a re-split replaces
    // Harmonies' flow for that song; a song that's gone (a restored path) shows Harmonies' drop screen instead.
    void startHarmonies (const juce::File& song, bool keepExistingResult = true);
    void retry();                                    // the Error screen's Retry, in the product it failed in
    void refreshQuality();                           // the Quick | Best switch, its Best tooltip and the drop screen's note
    bool jobRunning() const;                         // a StemSplitter split or a Harmonies step
    void choose (const juce::String& title, const juce::String& patterns, std::function<void (const juce::File&)> then);

    StemSplitterProcessor& proc;
    WaveBackground waves;
    Header header;
    juce::Component panelContent;                    // holds the screens; the intro fades and lowers it, not them (keeps their caches)
    DropScreen drop;
    SplittingScreen splitting;
    StemsScreen stems;
    HarmonyDropScreen harmonyDrop;                   // Harmonies' page, right of StemSplitter's
    HarmonySplittingScreen harmonySplitting;
    HarmonyScreen harmony;
    ErrorScreen error;                               // on the page of the product that failed
    HelpSheet help;
    UpdateScreen updatePanel;
    InfoButton info;                                 // bottom-right corner: opens and closes the help sheet
    juce::TooltipWindow tooltips { this };           // the choir icon's and the Best side's tooltips
    bool helpOpen = false, updateOpen = false;
    bool live = false, standaloneApp = false;        // a real plugin or the app (not the tests); the desktop app
    bool shotsAsked = false, quitAsked = false, engineChangedForTests = false;
    juce::String pillText, pillVersion;              // what the header's pill shows
    bool nativeButtons = false;                      // the title bar's buttons are macOS's, not LISN's
    std::optional<UiState> forced, shown;
    Screen pageScreen[2] { Screen::Drop, Screen::HarmonyDrop };   // the last screen of each page (StemSplitter, Harmonies)
    bool errorOnRight = false;
    juce::String appliedThemeId;
    std::unique_ptr<juce::FileChooser> chooser;
    float flow = 0.0f, pulse = 0.0f;                 // wave motion
    double lastTick = 0.0;
    std::unique_ptr<juce::VBlankAttachment> introFrames;   // drives the intro; tick() drops it once it ends (not in its own callback)
    bool introPlaying = false;
    double introStart = 0.0;                         // ms of its first frame; 0 = no frame yet (the window may not show at once)
    juce::File unloadableDir;                        // a stem dir the player failed to load (not retried every tick)
    RenderJob::Request mixKey;                       // the mix settings of the last tick (dest, from, to)
    double mixKeySince = 0.0;                        // when they last changed (ms); a render starts once they settle for 0.5 s
    RenderJob mixJob;                                // last: it stops before anything it reads goes
    std::optional<std::pair<double, bool>> carry;    // the position and play state to restore once the player reloads
    float slide = 0.0f, slideFrom = 0.0f, slideTarget = 0.0f;
    double slideStart = 0.0;
    std::unique_ptr<juce::VBlankAttachment> slideFrames;   // while sliding
    juce::ThreadPool shotPool { 1 };                 // goes first: a screenshot download (5 s timeout) ends before the editor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterEditor)
};
