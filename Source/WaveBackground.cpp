#include "WaveBackground.h"
#include "Frost.h"

namespace
{
    constexpr float layerSigma = 14.0f;    // drop-shadow (12px -4px 14px): the third length is the Gaussian sigma
    constexpr float panelSigma = 25.0f;    // box-shadow blur 50px = 2 sigma

    // `path` (window px) filled with `colour` into a quarter-resolution software image covering `area`, blurred with
    // sigma (window px) and scaled back up to full resolution with high resampling quality.
    WaveBackground::Mask softShadow (juce::Image::PixelFormat format, juce::Rectangle<float> area, float scale,
                                     const juce::Path& path, juce::Colour colour, float sigma)
    {
        const auto phys = (area * scale).getSmallestIntegerContainer();
        juce::Image small (format, (phys.getWidth() + 3) / 4, (phys.getHeight() + 3) / 4, true, juce::SoftwareImageType());
        {
            juce::Graphics g (small);
            g.setColour (colour);
            g.fillPath (path, juce::AffineTransform::scale (scale).translated (-phys.getPosition().toFloat()).scaled (0.25f));
        }
        blurImage (small, sigma * scale / 4.0f);
        juce::Image full (format, phys.getWidth(), phys.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g (full);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImageTransformed (small, juce::AffineTransform::scale (4.0f));
        }
        return { full, phys.getPosition() };
    }

    // Draws at physical pixel origin + offset; with whole-pixel offsets the renderer blits without resampling.
    void drawMask (juce::Graphics& g, const WaveBackground::Mask& m, juce::Point<int> offset, float scale, bool fillAlpha)
    {
        g.drawImageTransformed (m.image, juce::AffineTransform::translation ((m.origin + offset).toFloat()).scaled (1.0f / scale), fillAlpha);
    }

    std::array<juce::Point<int>, 5> snappedOffsets (float flow, float pulse, float scale)
    {
        std::array<juce::Point<int>, 5> o;
        for (size_t i = 0; i < o.size(); ++i)
            o[i] = (WaveGeometry::motionOffset (kLayers[i], flow, pulse) * scale).roundToInt();
        return o;
    }
}

WaveBackground::WaveBackground()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
}

WaveBackground::Mask WaveBackground::shapeMask (const LayerShape& shape, juce::Rectangle<float> area, float scale)
{
    const auto phys = (area * scale).getSmallestIntegerContainer();
    juce::Image image (juce::Image::SingleChannel, phys.getWidth(), phys.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        g.setColour (juce::Colours::white);
        g.fillPath (WaveGeometry::layerPath (shape, shape.phase, 0.0f, shape.wavelength + 40.0f),
                    juce::AffineTransform::scale (scale).translated (-phys.getPosition().toFloat()));
    }
    return { image, phys.getPosition() };
}

WaveBackground::Mask WaveBackground::shadowMask (const LayerShape& shape, juce::Rectangle<float> area, float scale)
{
    auto path = WaveGeometry::layerPath (shape, shape.phase, 0.0f, shape.wavelength + 40.0f);
    path.applyTransform (juce::AffineTransform::translation (12.0f, -4.0f));
    return softShadow (juce::Image::SingleChannel, area.expanded (3.0f * layerSigma), scale, path, juce::Colours::white, layerSigma);
}

void WaveBackground::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void WaveBackground::setPanel (juce::Rectangle<int> bounds, float cornerRadius, float tintAlpha)
{
    if (bounds != panel || cornerRadius != radius)
        panelShadow = {};
    panel = bounds;
    radius = cornerRadius;
    tint = tintAlpha;
    repaint();
}

void WaveBackground::setMotion (float newFlow, float newPulse)
{
    flow = newFlow;
    pulse = newPulse;
    const auto next = snappedOffsets (flow, pulse, cacheScale);
    if (next == offsets)
        return;
    offsets = next;
    for (auto r : motionRegion())
        repaint (r);
}

juce::RectangleList<int> WaveBackground::motionRegion() const
{
    juce::RectangleList<int> region (getLocalBounds());
    region.subtract (panel);
    const auto c = (int) std::ceil (radius) + 2;
    region.add ({ panel.getX(), panel.getY(), c, c });
    region.add ({ panel.getRight() - c, panel.getY(), c, c });
    region.add ({ panel.getX(), panel.getBottom() - c, c, c });
    region.add ({ panel.getRight() - c, panel.getBottom() - c, c, c });
    return region;
}

void WaveBackground::paint (juce::Graphics& g)
{
    // JUCE 8.0.4 has no "preferred image type" query: window paints and snapshots use the native (Direct2D) context,
    // images made with SoftwareImageType use the software renderer.
    auto& context = g.getInternalContext();
    const auto scale = juce::jlimit (1.0f, 2.0f, context.getPhysicalPixelScaleFactor());
    const bool software = dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&context) != nullptr;
    const juce::ImageType& type = software ? static_cast<const juce::ImageType&> (softwareType) : nativeType;
    const auto window = getLocalBounds().toFloat();

    if (scale != cacheScale || software != cacheSoftware || getLocalBounds() != cacheBounds)
    {
        cacheScale = scale;
        cacheSoftware = software;
        cacheBounds = getLocalBounds();
        for (size_t i = 0; i < kLayers.size(); ++i)
        {
            // Covers the window for every offset: up to one wavelength (+40) along the edge and push along the normal.
            const auto& s = kLayers[i];
            const auto base = window.expanded (40.0f);
            const auto area = base.getUnion (base - WaveGeometry::direction (s) * (s.wavelength + 40.0f))
                                  .getUnion (base - WaveGeometry::normal (s) * s.push);
            auto shape = shapeMask (s, area, scale);
            auto shadow = shadowMask (s, area, scale);
            shapes[i] = { type.convert (shape.image), shape.origin };
            shadows[i] = { type.convert (shadow.image), shadow.origin };
        }
        panelShadow = {};
        frost = {};
    }

    if (! panel.isEmpty() && panelShadow.image.isNull())
    {
        // box-shadow 0 26px 50px -22px: the panel shrunk by the spread (the corner radius too), moved down 26 px.
        juce::Path p;
        p.addRoundedRectangle (panel.toFloat().reduced (22.0f).translated (0.0f, 26.0f), juce::jmax (0.0f, radius - 22.0f));
        auto m = softShadow (juce::Image::ARGB, p.getBounds().expanded (3.0f * panelSigma), scale, p,
                             juce::Colour (0xff140810).withAlpha (0.75f), panelSigma);
        panelShadow = { type.convert (m.image), m.origin };
    }

    if (frost.isNull() || frostTheme != theme.id)
    {
        // The resting background rendered directly in software (the cached masks may be GPU images), then blurred.
        juce::Image src (juce::Image::ARGB, juce::roundToInt (window.getWidth() * scale), juce::roundToInt (window.getHeight() * scale),
                         true, juce::SoftwareImageType());
        {
            juce::Graphics fg (src);
            fg.addTransform (juce::AffineTransform::scale (scale));
            fg.fillAll (theme.ground);
            for (size_t i = 0; i < kLayers.size(); ++i)
            {
                const auto& s = kLayers[i];
                fg.setColour (theme.shadow);
                drawMask (fg, shadowMask (s, window, scale), {}, scale, true);
                fg.setColour (theme.layers[i]);
                fg.fillPath (WaveGeometry::layerPath (s, s.phase, 0.0f));
            }
        }
        frost = type.convert (frosted (src, 18.0f * scale));
        frostTheme = theme.id;
    }

    offsets = snappedOffsets (flow, pulse, scale);

    g.fillAll (theme.ground);
    for (size_t i = 0; i < kLayers.size(); ++i)
    {
        g.setColour (theme.shadow);
        drawMask (g, shadows[i], offsets[i], scale, true);
        g.setColour (theme.layers[i]);
        drawMask (g, shapes[i], offsets[i], scale, true);
    }

    if (panel.isEmpty())
        return;

    // Main.dc.html panel: border-box with a 1 px border. The ground background shows under the border; the frost and
    // tint are clipped to the padding box (inner radius = radius - 1).
    const auto outer = panel.toFloat(), inner = outer.reduced (1.0f);
    drawMask (g, panelShadow, {}, scale, false);
    g.setColour (theme.ground);
    g.fillRoundedRectangle (outer, radius);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (inner, radius - 1.0f);
        g.reduceClipRegion (clip);
        g.drawImageTransformed (frost, juce::AffineTransform::scale (1.0f / scale));
        g.setColour (theme.panelTint.withAlpha (tint));
        g.fillAll();
    }
    g.setColour (Theme::cream.withAlpha (0.14f));
    g.drawRoundedRectangle (outer.reduced (0.5f), radius - 0.5f, 1.0f);
}
