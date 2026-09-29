#pragma once
#include "Controls.h"
#include <vector>

juce::String mmss (double seconds);   // m:ss, rounded down

enum class WaveMouse { move, down, drag, up, exit };   // what the mouse did over a waveform

// The waveform of a stem row: 376 x waveH bars inside a 5 px vertical margin (for the playhead). It draws `view` (song
// fractions) of the stem's 100 Hz peak envelope. It reports the mouse as song fractions (through the view) to onMouse.
class WaveformView : public juce::Component
{
public:
    WaveformView();
    void setEnvelope (std::vector<float> envelope);    // peak levels 0..1, one per 10 ms of the stem
    void setBars (int count);                          // bars across the width: 104, or 96 compact
    void setView (juce::Range<double> songFractions);  // the part of the song drawn; the whole song by default
    void setStemColour (juce::Colour);
    void setFraction (double);                         // the playhead (song fraction); no repaint, the screen repaints the strip
    std::function<void (WaveMouse, double, bool)> onMouse;   // what happened, the song fraction under the mouse, Alt held

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int margin = 5;

private:
    void rebuildBars();
    double fractionAt (float x) const;                 // the song fraction under x, clamped to the view
    void send (WaveMouse, const juce::MouseEvent&);

    std::vector<float> envelope;
    juce::Range<double> view { 0.0, 1.0 };
    int count = 104;
    juce::Path bars;                                   // cached; rebuilt on setEnvelope / setBars / setView / resized
    juce::Colour colour = Theme::cream;
    double fraction = 0.0;
};

// One stem row of the stem player (docs/design/StemPlayer.mockup.html): colour bar + name, mute light, waveform, drag chip.
// Drag from the row (outside the light and the waveform) to drop dragFile() onto a DAW track.
class StemRow : public juce::Component
{
public:
    StemRow (juce::File wav, juce::String stemKey);  // stemKey = lower-case file name without extension
    void setTheme (const Theme&);
    void setCompact (bool sixStems);                 // rows 42 / waveform 26 when true, 62 / 40 when false (sets the height)
    void setEnvelope (std::vector<float> envelope);  // the stem's 100 Hz peak envelope (levels 0..1)
    void setView (juce::Range<double>);              // the song fractions the waveform shows
    void setMuted (bool);                            // dims the name and waveform, swaps the light's icon
    void setPosition (double fraction);              // the shared playhead; no repaint (the screen repaints the strip)
    std::function<void()> onToggleMute, onSolo;
    std::function<void (WaveMouse, double, bool)> onWaveMouse;   // the waveform's mouse, as song fractions
    std::function<juce::File()> dragFile;            // what a drag drops; unset = the stem file
    juce::File getFile() const { return file; }
    juce::String getStemKey() const { return stemKey; }
    int barCount() const { return compact ? 96 : 104; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;            // the waveform column outside the waveform is the screen's (its grab tabs)
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
    MuteLight lightButton;
    WaveformView wave;
};
