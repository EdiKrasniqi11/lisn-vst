#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// SVG path data (24x24 viewBox) copied verbatim from docs/design/*.dc.html.
namespace Icons
{
    inline constexpr const char* file   = "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8zM14 3v5h5M10 17.5V11l4-1";
    inline constexpr const char* plus   = "M12 5v14M5 12h14";
    inline constexpr const char* upload = "M12 15V4M7.5 8.5L12 4l4.5 4.5M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3";
    inline constexpr const char* alert  = "M12 9v4M12 17h.01M10.3 3.9L1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z";
    inline constexpr const char* copy   = "M9 9h10v10H9zM5 15V5h10";
    inline constexpr const char* grip   = "M9 6h.01M15 6h.01M9 12h.01M15 12h.01M9 18h.01M15 18h.01";
    inline constexpr const char* loop = "M17 2l4 4-4 4M3 11v-1a4 4 0 0 1 4-4h14M7 22l-4-4 4-4M21 13v1a4 4 0 0 1-4 4H3";   // stroked
    inline constexpr const char* play   = "M8 5.5v13l10.5-6.5z";              // filled
    inline constexpr const char* pause  = "M7 5h3.5v14H7zM13.5 5H17v14h-3.5z";  // filled
    inline constexpr const char* speaker    = "M11 5 6 9H3v6h3l5 4zM15.5 8.5a5 5 0 0 1 0 7M18.5 5.5a9 9 0 0 1 0 13";   // stroked
    inline constexpr const char* speakerOff = "M11 5 6 9H3v6h3l5 4zM16 9.5l5 5M21 9.5l-5 5";                          // stroked
    inline constexpr const char* close  = "M6 6l12 12M18 6L6 18";                                       // Help.mockup.html, stroked
    inline constexpr const char* jump   = "M4 12h11M11 7l5 5-5 5M20 5v14";                               // Help.mockup.html, stroked
    inline constexpr const char* home   = "M6 5v14M19 5.5v13L9 12z";
    inline constexpr const char* zoom   = "M11 4a7 7 0 1 0 0 14a7 7 0 1 0 0-14M20 20l-4-4M11 8v6M8 11h6";
    inline constexpr const char* trim   = "M7 4v16M17 4v16M4 8h3M17 16h3";
    inline constexpr const char* toggle = "M8 7h8a5 5 0 0 1 0 10H8A5 5 0 0 1 8 7zM16 10a2 2 0 1 0 0 4a2 2 0 1 0 0-4";
    inline constexpr const char* grid   = "M4 4v16M9 4v16M14 4v16M19 4v16";
    inline constexpr const char* layers = "M12 3l9 5-9 5-9-5zM3 13l9 5 9-5";
    inline constexpr const char* mouse  = "M12 3a6 6 0 0 0-6 6v6a6 6 0 0 0 12 0V9a6 6 0 0 0-6-6zM12 7v3";
}

// The icon's 24x24 viewBox mapped onto `area`.
inline juce::Path iconPath (const char* d, juce::Rectangle<float> area)
{
    auto p = juce::Drawable::parseSVGPath (d);
    p.applyTransform (juce::AffineTransform::scale (area.getWidth() / 24.0f, area.getHeight() / 24.0f).translated (area.getPosition()));
    return p;
}

// SVG stroke-width is in viewBox units; round caps and joins as in the mockups. Uses the current colour.
inline void strokeIcon (juce::Graphics& g, const char* d, juce::Rectangle<float> area, float strokeWidth)
{
    g.strokePath (iconPath (d, area), juce::PathStrokeType (strokeWidth * area.getWidth() / 24.0f,
                                                            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
