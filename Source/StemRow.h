#pragma once
#include "Controls.h"
#include <vector>

juce::String mmss (double seconds);   // m:ss, rounded down

// The waveform of a stem row: 376 x waveH bars inside a 5 px vertical margin (for the playhead). Click or drag to seek.
class WaveformView : public juce::Component
{
public:
    WaveformView();
    void setLevels (std::vector<float> levels);        // 0..1 per bar
    void setStemColour (juce::Colour);
    void setFraction (double);                         // no repaint; the screen repaints the strip that changed
    std::function<void (double)> onSeek;               // fraction 0..0.999

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (double, double, bool)> onLoopDrag;   // from, to (fractions), finished

    static constexpr int margin = 5;

private:
    void rebuildBars();

    std::vector<float> levels;
    juce::Path bars;                                   // cached; rebuilt on setLevels / resized
    juce::Colour colour = Theme::cream;
    double fraction = 0.0, downFraction = 0.0;
    bool looping = false;
};

// One stem row of the stem player (docs/design/StemPlayer.mockup.html): colour bar + name, mute light, waveform, drag chip.
// Drag from the row (outside the light and the waveform) to drop dragFile() onto a DAW track.
class StemRow : public juce::Component
{
public:
    StemRow (juce::File wav, juce::String stemKey);  // stemKey = lower-case file name without extension
    void setTheme (const Theme&);
    void setCompact (bool sixStems);                 // rows 42 / waveform 26 when true, 62 / 40 when false (sets the height)
    void setPeaks (std::vector<float> levels);       // bar levels 0..1, padded or cut to barCount()
    void setMuted (bool);                            // dims the name and waveform, swaps the light's icon
    void setPosition (double fraction);              // the shared playhead; no repaint (the screen repaints the strip)
    std::function<void()> onToggleMute, onSolo;
    std::function<void (double)> onSeek;             // fraction 0..0.999
    std::function<void (double, double, bool)> onLoopDrag;
    std::function<juce::File()> dragFile;            // what a drag drops; unset = the stem file
    juce::File getFile() const { return file; }
    juce::String getStemKey() const { return stemKey; }
    int barCount() const { return compact ? 96 : 104; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // test hooks
    MuteLight& light() { return lightButton; }
    WaveformView& waveform() { return wave; }
    juce::Rectangle<int> chipBounds() const;

private:
    juce::File file;
    juce::String stemKey, name;
    juce::Colour colour;
    bool compact = false, muted = false, dragStarted = false;
    std::vector<float> levels;
    MuteLight lightButton;
    WaveformView wave;
};
