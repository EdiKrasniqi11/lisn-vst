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

    // Bounds of the pixels of an ARGB image with any alpha.
    juce::Rectangle<int> visibleBounds (const juce::Image& image)
    {
        const juce::Image::BitmapData d (image, juce::Image::BitmapData::readOnly);
        int x0 = d.width, y0 = d.height, x1 = 0, y1 = 0;
        for (int y = 0; y < d.height; ++y)
        {
            const auto* line = reinterpret_cast<const juce::PixelARGB*> (d.getLinePointer (y));
            for (int x = 0; x < d.width; ++x)
                if (line[x].getAlpha() != 0)
                {
                    x0 = juce::jmin (x0, x);
                    x1 = juce::jmax (x1, x + 1);
                    y0 = juce::jmin (y0, y);
                    y1 = y + 1;
                }
        }
        return juce::Rectangle<int>::leftTopRightBottom (x0, y0, juce::jmax (x0, x1), juce::jmax (y0, y1));
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
    if (bounds != panel || cornerRadius != radius || tintAlpha != tint)
        panelImage = {};
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
            shapes[i] = shapeMask (s, area, scale);
            shadows[i] = shadowMask (s, area, scale);
        }
        compositeTheme = {};
    }

    if (compositeTheme != theme.id)
    {
        // One ARGB image per layer: shadow then shape in the theme colours (source-over is associative, so blitting the
        // composite equals drawing both masks). The shadow mask's bounds contain the shape mask's. The frost is the
        // resting background built from the same software composites, then blurred.
        juce::Image src (juce::Image::ARGB, juce::roundToInt (window.getWidth() * scale), juce::roundToInt (window.getHeight() * scale),
                         true, juce::SoftwareImageType());
        {
            juce::Graphics fg (src);
            fg.fillAll (theme.ground);
            for (size_t i = 0; i < kLayers.size(); ++i)
            {
                const auto& shadow = shadows[i];
                juce::Image c (juce::Image::ARGB, shadow.image.getWidth(), shadow.image.getHeight(), true, juce::SoftwareImageType());
                {
                    juce::Graphics cg (c);
                    cg.setColour (theme.shadow);
                    cg.drawImageAt (shadow.image, 0, 0, true);
                    cg.setColour (theme.layers[i]);
                    const auto d = shapes[i].origin - shadow.origin;
                    cg.drawImageAt (shapes[i].image, d.x, d.y, true);
                }
                fg.drawImageAt (c, shadow.origin.x, shadow.origin.y);
                const auto visible = visibleBounds (c);   // a blit then skips the fully transparent margins
                composites[i] = { type.convert (c.getClippedImage (visible)), shadow.origin + visible.getPosition() };
            }
        }
        frost = frosted (src, 18.0f * scale);
        compositeTheme = theme.id;
        panelImage = {};
    }

    if (! panel.isEmpty() && panelImage.image.isNull())
    {
        // Main.dc.html panel: box-shadow 0 26px 50px -22px (the panel shrunk by the spread, corner radius too, moved down
        // 26 px), then a border-box with a 1 px border. The ground shows under the border; the frost and tint are
        // clipped to the padding box (inner radius = radius - 1).
        const auto outer = panel.toFloat();
        juce::Path p;
        p.addRoundedRectangle (outer.reduced (22.0f).translated (0.0f, 26.0f), juce::jmax (0.0f, radius - 22.0f));
        const auto shadowArea = p.getBounds().expanded (3.0f * panelSigma);
        const auto shadow = softShadow (juce::Image::ARGB, shadowArea, scale, p, juce::Colour (0xff140810).withAlpha (0.75f), panelSigma);
        const auto phys = (shadowArea.getUnion (outer).getIntersection (window) * scale).getSmallestIntegerContainer();
        juce::Image img (juce::Image::ARGB, phys.getWidth(), phys.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics pg (img);
            pg.addTransform (juce::AffineTransform::scale (scale).translated (-phys.getPosition().toFloat()));
            drawMask (pg, shadow, {}, scale, false);
            pg.setColour (theme.ground);
            pg.fillRoundedRectangle (outer, radius);
            {
                juce::Graphics::ScopedSaveState save (pg);
                juce::Path clip;
                clip.addRoundedRectangle (outer.reduced (1.0f), radius - 1.0f);
                pg.reduceClipRegion (clip);
                pg.drawImageTransformed (frost, juce::AffineTransform::scale (1.0f / scale));
                pg.setColour (theme.panelTint.withAlpha (tint));
                pg.fillAll();
            }
            pg.setColour (Theme::cream.withAlpha (0.14f));
            pg.drawRoundedRectangle (outer.reduced (0.5f), radius - 0.5f, 1.0f);
        }
        panelImage = { type.convert (img), phys.getPosition() };
    }

    // Per frame: only blits at whole physical pixel offsets.
    offsets = snappedOffsets (flow, pulse, scale);
    {
        juce::Graphics::ScopedSaveState save (g);
        if (! panel.isEmpty())   // the panel image is opaque inside its rounded rect: nothing to draw under it but the corners
        {
            g.excludeClipRegion (panel.reduced (juce::roundToInt (std::ceil (radius)), 0));
            g.excludeClipRegion (panel.reduced (0, juce::roundToInt (std::ceil (radius))));
        }
        g.fillAll (theme.ground);
        for (size_t i = 0; i < kLayers.size(); ++i)
            drawMask (g, composites[i], offsets[i], scale, false);
    }
    if (! panel.isEmpty())
        drawMask (g, panelImage, {}, scale, false);
}
