#pragma once
#include "Theme.h"
#include "WaveGeometry.h"

// The wave background (Waves.dc.html) plus the frosted panel (Main.dc.html). Layer masks are rendered once per scale;
// per theme each layer's shadow + shape is composited into one image, and the panel (drop shadow, frost, tint, border)
// into another. A frame only blits them, the layers at snapped offsets.
class WaveBackground : public juce::Component
{
public:
    WaveBackground();
    void setTheme (const Theme&);                          // repaints; a new theme id rebuilds the composites, frost and panel
    void setPanel (juce::Rectangle<int> bounds, float cornerRadius, float tintAlpha);
    void setMotion (float flow, float pulse);              // no-op unless a snapped layer offset changes
    juce::RectangleList<int> motionRegion() const;         // local bounds minus panel, plus the panel's 4 corner squares (radius+2)
    void paint (juce::Graphics&) override;

    // An image whose pixel (0, 0) sits at physical pixel `origin` (window px * scale). The masks below are SingleChannel
    // SoftwareImageType images.
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
    std::array<Mask, 5> shapes, shadows;                   // software, theme-independent
    std::array<Mask, 5> composites;                        // shadow + shape in theme colours, at the shadow mask's origin
    juce::Image frost;                                     // software: the resting background blurred, source of panelImage
    juce::String compositeTheme;                           // composites and frost are built for this theme id; empty = not built
    Mask panelImage;                                       // null = rebuild (theme, scale, renderer, panel, radius or tint changed)
    juce::SoftwareImageType softwareType;
    juce::NativeImageType nativeType;
};
