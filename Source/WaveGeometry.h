#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

struct LayerShape { float xTop, xBottom, amp, wavelength, phase, speed, push; };
inline const std::array<LayerShape, 5> kLayers {{      // back to front, from Waves.dc.html
    { 500.0f, 980.0f, 26.0f, 260.0f, 0.3f, 0.6f,  4.0f },
    { 330.0f, 800.0f, 30.0f, 240.0f, 1.7f, 0.8f,  7.0f },
    { 180.0f, 620.0f, 28.0f, 230.0f, 3.1f, 1.0f, 10.0f },
    { -120.0f, 400.0f, 26.0f, 220.0f, 4.4f, 1.2f, 13.0f },
    { -330.0f, 180.0f, 22.0f, 210.0f, 5.2f, 1.4f, 16.0f } }};

// Port of docs/design/Waves.dc.html: each layer is the region left of a slanted wavy edge.
namespace WaveGeometry
{
    constexpr float edgeTop = -80.0f, edgeBottom = 580.0f;      // the edge runs from (xTop, edgeTop) to (xBottom, edgeBottom)
    float edgeLength (const LayerShape&);
    juce::Point<float> direction (const LayerShape&);   // unit vector top -> bottom
    juce::Point<float> normal (const LayerShape&);      // (dy, -dx) / len: points out of the filled side
    // Edge point at distance s along the line: P0 + dir*s + normal*(push + amp*sin(2*pi*s/wavelength + phase)).
    juce::Point<float> edgePoint (const LayerShape&, float s, float phase, float push);
    // Filled region left of the edge, like region() in Waves.dc.html. The wavy part is sampled every edgeLength/28,
    // on the mockup's grid s = k*step, for s in [-extendBack - step, len + step], and smoothed with the mockup's
    // Catmull-Rom -> cubic rule (c1 = b + (c - a)/6, c2 = c - (e - b)/6). It continues straight along the edge for
    // 4000 px at both ends and closes 4000 px out along -normal, so sliding the layer by up to extendBack along the
    // edge leaves no gap.
    juce::Path layerPath (const LayerShape&, float phase, float push, float extendBack = 0.0f);
    // Offset for drawing the pre-rendered layer: direction * fmod(flow*speed, wavelength) + normal * (pulse*push).
    juce::Point<float> motionOffset (const LayerShape&, float flow, float pulse);
}
