#include "../Source/Frost.h"

struct FrostTests : juce::UnitTest
{
    FrostTests() : juce::UnitTest ("Frost") {}

    static juce::Image image (juce::Image::PixelFormat format, int w, int h, juce::Colour fill)
    {
        juce::Image img (format, w, h, true, juce::SoftwareImageType());
        img.clear (img.getBounds(), fill);
        return img;
    }

    void runTest() override
    {
        beginTest ("uniform stays uniform");
        const juce::Colour c (0xff5a8cc8);
        const auto flat = frosted (image (juce::Image::ARGB, 200, 120, c), 18);
        expect (flat.getWidth() == 200 && flat.getHeight() == 120);
        expect (flat.getFormat() == juce::Image::ARGB);
        expect (dynamic_cast<juce::SoftwareImageType*> (flat.getPixelData()->createType().get()) != nullptr);
        int worst = 0;
        for (int y = 0; y < flat.getHeight(); ++y)
            for (int x = 0; x < flat.getWidth(); ++x)
            {
                const auto p = flat.getPixelAt (x, y);
                worst = juce::jmax (worst, std::abs (p.getAlpha() - c.getAlpha()), std::abs (p.getRed() - c.getRed()),
                                    juce::jmax (std::abs (p.getGreen() - c.getGreen()), std::abs (p.getBlue() - c.getBlue())));
            }
        expectLessOrEqual (worst, 2);

        beginTest ("square");
        auto sq = image (juce::Image::ARGB, 100, 100, juce::Colours::black);
        sq.clear ({ 40, 40, 20, 20 }, juce::Colours::white);
        const auto soft = frosted (sq, 4.0f);
        expect (soft.getBounds() == sq.getBounds());
        expectGreaterThan ((int) soft.getPixelAt (50, 50).getRed(), 200);
        const auto outside = (int) soft.getPixelAt (71, 50).getRed();   // 12 px right of the square (x 40..59)
        expect (outside > 0 && outside < 255, juce::String (outside));

        beginTest ("half-plane sigma");
        auto mask = image (juce::Image::SingleChannel, 300, 100, juce::Colours::transparentBlack);
        mask.clear ({ 0, 0, 150, 100 }, juce::Colours::white);
        blurImage (mask, 14);
        const auto edge = (int) mask.getPixelAt (150, 50).getAlpha();
        const auto oneSigma = (int) mask.getPixelAt (164, 50).getAlpha();
        expectWithinAbsoluteError (edge, 128, 12);
        expectWithinAbsoluteError (oneSigma, 40, 10);
    }
};

static FrostTests frostTests;
