#pragma once
#include "PluginProcessor.h"
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
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;   // one audio file
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // test hooks
    void forceState (const UiState& s) { forced = s; }   // the timer then shows this instead of uiStateFor (proc)
    void tick();                                     // one timer step
    WaveBackground& background() { return waves; }
    StemsScreen& stemsScreen() { return stems; }

private:
    void timerCallback() override { tick(); }
    void show (const UiState&);
    void applyTheme (const Theme&);
    juce::File renderFile (juce::uint32 mask);   // the audible stems over the loop (or the song) as a cached WAV
    void choose (const juce::String& title, const juce::String& patterns, std::function<void (const juce::File&)> then);

    StemSplitterProcessor& proc;
    WaveBackground waves;
    Header header;
    DropScreen drop;
    SplittingScreen splitting;
    StemsScreen stems;
    ErrorScreen error;
    std::optional<UiState> forced, shown;
    juce::String appliedThemeId;
    std::unique_ptr<juce::FileChooser> chooser;
    float flow = 0.0f, pulse = 0.0f;                 // wave motion
    double lastTick = 0.0;
    juce::File unloadableDir;                        // a stem dir the player failed to load (not retried every tick)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterEditor)
};
