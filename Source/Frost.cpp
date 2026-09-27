#include "Frost.h"
#include <vector>

namespace
{
    // Three clamped box blurs of radius r along `lines` lines of n bytes, `step` bytes apart.
    void boxBlurLines (juce::uint8* data, int lines, int lineStep, int n, int step, int r, std::vector<int>& line)
    {
        const int width = 2 * r + 1;
        for (int l = 0; l < lines; ++l)
        {
            auto* p = data + l * lineStep;
            for (int pass = 0; pass < 3; ++pass)
            {
                for (int i = 0; i < n; ++i) line[(size_t) i] = p[i * step];
                int sum = 0;
                for (int j = -r; j <= r; ++j) sum += line[(size_t) juce::jlimit (0, n - 1, j)];
                for (int i = 0; i < n; ++i)
                {
                    p[i * step] = (juce::uint8) ((sum + r) / width);
                    sum += line[(size_t) juce::jmin (n - 1, i + r + 1)] - line[(size_t) juce::jmax (0, i - r)];
                }
            }
        }
    }
}

void blurImage (juce::Image& image, float sigma)
{
    if (! image.isValid()) return;
    const int r = juce::jmax (1, juce::roundToInt ((std::sqrt (1.0f + 4.0f * sigma * sigma) - 1.0f) / 2.0f));
    juce::Image::BitmapData bd (image, juce::Image::BitmapData::readWrite);
    std::vector<int> line ((size_t) juce::jmax (bd.width, bd.height));
    for (int c = 0; c < bd.pixelStride; ++c)   // every byte of a pixel is a channel
    {
        boxBlurLines (bd.data + c, bd.height, bd.lineStride, bd.width, bd.pixelStride, r, line);
        boxBlurLines (bd.data + c, bd.width, bd.pixelStride, bd.height, bd.lineStride, r, line);
    }
}

juce::Image frosted (const juce::Image& src, float radius)
{
    const auto scaledCopy = [] (const juce::Image& from, int w, int h)
    {
        // One spare column: the edge table clamps a path edge on the image's right border to 255/256 coverage
        // (juce_EdgeTable.cpp, rightLimit - 1), which would leave the last column slightly transparent.
        juce::Image to (juce::Image::ARGB, w + 1, h, true, juce::SoftwareImageType());
        {
            juce::Graphics g (to);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (from, { 0.0f, 0.0f, (float) w, (float) h });
        }
        return to.getClippedImage ({ w, h }).createCopy();
    };
    auto small = scaledCopy (src, juce::jmax (1, src.getWidth() / 4), juce::jmax (1, src.getHeight() / 4));
    blurImage (small, radius / 4.0f);
    return scaledCopy (small, src.getWidth(), src.getHeight());
}
