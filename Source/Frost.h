#pragma once
#include <juce_graphics/juce_graphics.h>

// In-place Gaussian approximation on a SoftwareImageType image (SingleChannel or ARGB, premultiplied):
// three running-sum box blurs horizontally, then three vertically, with r = max(1, round((sqrt(1 + 4*sigma*sigma) - 1) / 2))
// (a box of radius r has variance r(r+1)/3, so three passes give sigma^2 = r(r+1)), clamping at the edges. sigma is in this image's pixels.
void blurImage (juce::Image& image, float sigma);

// Static frosted-glass blur (CSS blur(radius) = Gaussian with sigma = radius px): shrink 4x with high resampling quality,
// blurImage (small, radius / 4), then scale back up with high resampling quality.
// The result is ARGB, the same size as src, and a SoftwareImageType image.
juce::Image frosted (const juce::Image& src, float radius);
