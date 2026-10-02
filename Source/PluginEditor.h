#pragma once
#include "PluginProcessor.h"
#include "RenderJob.h"
#include "Screens.h"
#include "UiState.h"
#include "WaveBackground.h"
#include <optional>

// Fixed 760 x 500: wave background, header and one screen per UiState::Screen. A 30 Hz timer maps the processor to a
// UiState, updates the stem rows from the player and moves the waves while it plays.
class StemSplitterEditor : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor&);
    ~StemSplitterEditor() override;                  // pauses the player
    void resized() override;

    // The keyboard (the editor takes it on any click; see the constructor). Space, L, 1-6, Shift+1-6 and Home on the Stems
    // screen; every other key returns false, and JUCE passes it on to the host.
    bool keyPressed (const juce::KeyPress&) override;
    // The desktop app's window: the header becomes its title bar (board 1A).
    void makeTitleBar (std::function<void()> minimise, std::function<void()> close) { header.makeTitleBar (std::move (minimise), std::move (close)); }
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setHelpOpen (bool);                         // the help sheet in the panel in place of the screen, the info button lit

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

private:
    void timerCallback() override { tick(); }
    void advanceIntro();                             // one display frame of the intro
    void show (const UiState&);
    void showScreen();                               // the shown screen, or the help sheet in its place
    void applyTheme (const Theme&);
    // Those stems at those gains over the loop (or the song): what to render and its cached dest (none when nothing is heard).
    RenderJob::Request renderRequest (const std::vector<float>& gains, double speed);
    juce::File renderFile (const std::vector<float>& gains, double speed);   // renderRequest's dest, rendered here unless cached
    void prepareMix();                               // at a speed other than 1, renders the mix on mixJob; sets the chip
    void startSplit (const juce::File&);             // cancels mixJob first: it must not hold stems a re-split replaces
    void choose (const juce::String& title, const juce::String& patterns, std::function<void (const juce::File&)> then);

    StemSplitterProcessor& proc;
    WaveBackground waves;
    Header header;
    juce::Component panelContent;                    // holds the screens; the intro fades and lowers it, not them (keeps their caches)
    DropScreen drop;
    SplittingScreen splitting;
    StemsScreen stems;
    ErrorScreen error;
    HelpSheet help;
    InfoButton info;                                 // bottom-right corner: opens and closes the help sheet
    bool helpOpen = false;
    std::optional<UiState> forced, shown;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterEditor)
};
