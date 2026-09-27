#include "WaveGeometry.h"
#include <vector>

namespace WaveGeometry
{
    float edgeLength (const LayerShape& s)
    {
        return juce::Point<float> (s.xBottom - s.xTop, edgeBottom - edgeTop).getDistanceFromOrigin();
    }

    juce::Point<float> direction (const LayerShape& s)
    {
        return juce::Point<float> (s.xBottom - s.xTop, edgeBottom - edgeTop) / edgeLength (s);
    }

    juce::Point<float> normal (const LayerShape& s)
    {
        const auto d = direction (s);
        return { d.y, -d.x };
    }

    juce::Point<float> edgePoint (const LayerShape& shape, float s, float phase, float push)
    {
        const auto off = push + shape.amp * std::sin (s / shape.wavelength * juce::MathConstants<float>::twoPi + phase);
        return juce::Point<float> (shape.xTop, edgeTop) + direction (shape) * s + normal (shape) * off;
    }

    juce::Path layerPath (const LayerShape& shape, float phase, float push, float extendBack)
    {
        const auto step = edgeLength (shape) / 28.0f;
        const auto dir = direction (shape), n = normal (shape);
        std::vector<juce::Point<float>> p;
        for (int k = -1 - (int) std::ceil (extendBack / step); k <= 29; ++k)
            p.push_back (edgePoint (shape, (float) k * step, phase, push));

        const auto start = p.front() - dir * 4000.0f, end = p.back() + dir * 4000.0f;
        juce::Path path;
        path.startNewSubPath (start - n * 4000.0f);
        path.lineTo (start);
        path.lineTo (p.front());
        const auto last = p.size() - 1;
        for (size_t i = 0; i < last; ++i)
        {
            const auto a = p[i > 0 ? i - 1 : 0], b = p[i], c = p[i + 1], e = p[juce::jmin (last, i + 2)];
            path.cubicTo (b + (c - a) / 6.0f, c - (e - b) / 6.0f, c);
        }
        path.lineTo (end);
        path.lineTo (end - n * 4000.0f);
        path.closeSubPath();
        return path;
    }

    juce::Point<float> motionOffset (const LayerShape& s, float flow, float pulse)
    {
        return direction (s) * std::fmod (flow * s.speed, s.wavelength) + normal (s) * (pulse * s.push);
    }
}
