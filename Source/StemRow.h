#pragma once
#include "Controls.h"
#include <vector>

// The waveform of a stem row: 330 x waveH bars inside a 5 px vertical margin (for the playhead). Click or drag to seek.
class WaveformView : public juce::Component
{
public:
    WaveformView();
    void setLevels (std::vector<float> levels);        // 0..1 per bar
    void setStemColour (juce::Colour);
    void setFraction (double);                         // no repaint; the row decides
    std::function<void (double)> onSeek;               // fraction 0..0.999

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    static constexpr int margin = 5;

private:
    void rebuildBars();

    std::vector<float> levels;
    juce::Path bars;                                   // cached; rebuilt on setLevels / resized
    juce::Colour colour = Theme::cream;
    double fraction = 0.0;
};

// One stem row of Main.dc.html: badge + name, play button, waveform, time, drag chip. Drag from the row (outside the
// button and the waveform) to drop the file onto a DAW track.
class StemRow : public juce::Component
{
public:
    StemRow (juce::File wav, juce::String stemKey);  // stemKey = lower-case file name without extension
    void setTheme (const Theme&);
    void setCompact (bool sixStems);                 // rows 42 / waveform 26 when true, 62 / 40 when false (sets the height)
    void setPeaks (std::vector<float> levels);       // bar levels 0..1, padded or cut to barCount()
    void setPlayback (bool playing, double fraction, double lengthSeconds);
    std::function<void()> onPlayPause;
    std::function<void (double)> onSeek;             // fraction 0..1
    juce::File getFile() const { return file; }
    juce::String getStemKey() const { return stemKey; }
    int barCount() const { return compact ? 96 : 104; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // test hooks
    CircleButton& playButton() { return button; }
    WaveformView& waveform() { return wave; }
    juce::Rectangle<int> chipBounds() const;
    juce::String timeText() const { return time; }
    int getRepaintCount() const { return repaintCount; }   // setPlayback calls that repainted

private:
    juce::Rectangle<int> timeBounds() const;

    juce::File file;
    juce::String stemKey, name;
    juce::Colour colour;
    bool compact = false, playing = false, dragStarted = false;
    int head = 0, repaintCount = 0;                  // playhead pixel
    juce::String time { "0:00" };
    std::vector<float> levels;
    CircleButton button;
    WaveformView wave;
};
