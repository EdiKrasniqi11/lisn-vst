#include "../Source/WaveGeometry.h"
#include <vector>

struct WaveGeometryTests : juce::UnitTest
{
    WaveGeometryTests() : juce::UnitTest ("WaveGeometry") {}

    // Literal port of edge() + region() from docs/design/Waves.dc.html (SVG path numbers rounded like toFixed(1)).
    static juce::Path mockupRegion (const LayerShape& s)
    {
        const auto x0 = s.xTop, y0 = -80.0f, dx = s.xBottom - s.xTop, dy = 660.0f;
        const auto len = std::hypot (dx, dy), nx = dy / len, ny = -dx / len;
        std::vector<juce::Point<float>> p;
        for (int i = 0; i <= 28; ++i)
        {
            const auto t = (float) i / 28.0f;
            const auto off = s.amp * std::sin ((t * len / s.wavelength) * juce::MathConstants<float>::pi * 2 + s.phase);
            p.push_back ({ x0 + dx * t + nx * off, y0 + dy * t + ny * off });
        }
        const auto f = [] (juce::Point<float> q) { return juce::Point<float> (std::round (q.x * 10) / 10, std::round (q.y * 10) / 10); };
        juce::Path d;
        d.startNewSubPath (-140, -140);
        d.lineTo (f (p[0]));
        for (size_t i = 0; i < p.size() - 1; ++i)
        {
            const auto a = p[i > 0 ? i - 1 : 0], b = p[i], c = p[i + 1], e = p[juce::jmin (p.size() - 1, i + 2)];
            d.cubicTo (f (b + (c - a) / 6.0f), f (c - (e - b) / 6.0f), f (c));
        }
        d.lineTo (-140, 660);
        d.closeSubPath();
        return d;
    }

    // Path::contains' crossing rule (juce_Path.cpp) for one scanline, collected once per grid row: calling
    // contains() for every grid point re-flattens the curves each time and takes ~15 s in a Debug build.
    static std::vector<std::pair<float, int>> crossings (const juce::Path& p, float y)
    {
        std::vector<std::pair<float, int>> hits;
        for (juce::PathFlatteningIterator i (p, {}, 0.1f); i.next();)
            if ((i.y1 <= y && i.y2 > y) || (i.y2 <= y && i.y1 > y))
                hits.push_back ({ i.x1 + (i.x2 - i.x1) * (y - i.y1) / (i.y2 - i.y1), i.y1 < i.y2 ? 1 : -1 });
        return hits;
    }

    static bool inside (const std::vector<std::pair<float, int>>& hits, float x)   // non-zero winding
    {
        int winding = 0;
        for (const auto& [hx, dir] : hits)
            if (hx <= x) winding += dir;
        return winding != 0;
    }

    static bool contains (const juce::Path& path, float x, float y) { return path.contains (x, y, 0.1f); }

    static bool nearEdge (const juce::Path& path, float x, float y)   // a neighbour 1.5 px away disagrees
    {
        const auto in = contains (path, x, y);
        return contains (path, x - 1.5f, y) != in || contains (path, x + 1.5f, y) != in
            || contains (path, x, y - 1.5f) != in || contains (path, x, y + 1.5f) != in;
    }

    // Number of 10 px grid points over the window where the paths disagree, away from both edges.
    static int mismatches (const juce::Path& a, const juce::Path& b)
    {
        int bad = 0;
        for (int y = 0; y <= 500; y += 10)
        {
            const auto fy = (float) y;
            const auto rowA = crossings (a, fy), rowB = crossings (b, fy);
            for (int x = 0; x <= 760; x += 10)
            {
                const auto fx = (float) x;
                if (inside (rowA, fx) != inside (rowB, fx) && ! nearEdge (a, fx, fy) && ! nearEdge (b, fx, fy))
                    ++bad;
            }
        }
        return bad;
    }

    void runTest() override
    {
        using namespace WaveGeometry;

        beginTest ("mockup fidelity");
        for (const auto& s : kLayers)
            expectEquals (mismatches (layerPath (s, s.phase, 0, 0), mockupRegion (s)), 0);

        beginTest ("motion equivalence");
        for (const auto& s : kLayers)
        {
            const auto base = layerPath (s, s.phase, 0, s.wavelength + 40);
            for (float flow : { 0.0f, 37.0f, 300.0f, 1234.0f })
                for (float pulse : { 0.0f, 0.5f, 1.0f })
                {
                    const auto off = motionOffset (s, flow, pulse);
                    auto moved = base;
                    moved.applyTransform (juce::AffineTransform::translation (off));
                    const auto phase = s.phase - juce::MathConstants<float>::twoPi * std::fmod (flow * s.speed, s.wavelength) / s.wavelength;
                    expectEquals (mismatches (moved, layerPath (s, phase, pulse * s.push, 0)), 0,
                                  "flow " + juce::String (flow) + " pulse " + juce::String (pulse));
                }
        }

        beginTest ("unit vectors");
        for (const auto& s : kLayers)
        {
            const auto d = direction (s), n = normal (s);
            expectWithinAbsoluteError (d.getDistanceFromOrigin(), 1.0f, 1e-5f);
            expectWithinAbsoluteError (n.getDistanceFromOrigin(), 1.0f, 1e-5f);
            expectWithinAbsoluteError (d.getDotProduct (n), 0.0f, 1e-5f);
        }
    }
};

static WaveGeometryTests waveGeometryTests;
