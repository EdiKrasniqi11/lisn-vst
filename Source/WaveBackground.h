#pragma once
#include "Theme.h"
#include "WaveGeometry.h"

// The wave background (Waves.dc.html) plus the frosted panel (Main.dc.html). Layer masks are rendered once per
// (scale, renderer) and only moved per frame; the frost is a static blurred copy of the resting waves.
class WaveBackground : public juce::Component
{
public:
    WaveBackground();
    void setTheme (const Theme&);                          // repaints; the frost is rebuilt for the new theme
    void setPanel (juce::Rectangle<int> bounds, float cornerRadius, float tintAlpha);
    void setMotion (float flow, float pulse);              // no-op unless a snapped layer offset changes
    juce::RectangleList<int> motionRegion() const;         // local bounds minus panel, plus the panel's 4 corner squares (radius+2)
    void paint (juce::Graphics&) override;

    // A layer mask (SingleChannel, SoftwareImageType): pixel (0, 0) sits at physical pixel `origin` (window px * scale).
    struct Mask { juce::Image image; juce::Point<int> origin; };
    // The resting layer shape covering `area`, with the path extended so sliding by one wavelength leaves no gap.
    static Mask shapeMask (const LayerShape&, juce::Rectangle<float> area, float scale);
    // CSS drop-shadow (12px -4px 14px): the shape moved by (12, -4), blurred with sigma 14 at quarter resolution.
    // It covers `area` expanded by 3 sigma.
    static Mask shadowMask (const LayerShape&, juce::Rectangle<float> area, float scale);

private:
    Theme theme = themeFor ("dusk");
    juce::Rectangle<int> panel;
    float radius = 0.0f, tint = 0.0f, flow = 0.0f, pulse = 0.0f;
    std::array<juce::Point<int>, 5> offsets {};            // snapped layer offsets in physical px at cacheScale

    float cacheScale = 1.0f;
    bool cacheSoftware = false;
    juce::Rectangle<int> cacheBounds;                     // masks are built for these bounds; empty = not built
    std::array<Mask, 5> shapes, shadows;
    Mask panelShadow;
    juce::Image frost;
    juce::String frostTheme;
    juce::SoftwareImageType softwareType;
    juce::NativeImageType nativeType;
};
