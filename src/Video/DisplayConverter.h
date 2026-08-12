#pragma once

#include "Yuv444Frame.h"
#include "video/ReconstructedLumaFrame.h"

#include <QImage>

#include <vector>
#include <array>
#include <cstdint>

class DisplayConverter
{
public:
    QImage convert(
        const Yuv444Frame& frame,
        const ReconstructedLumaFrame& reconstructedLuma,
        int outputWidth,
        int outputHeight) const;

    void setHighlightedLine(int line);
    DisplayConverter();
    void setGamma(double gamma);
    
private:
    struct HorizontalSample
    {
        int leftIndex;
        int rightIndex;
        float fraction;
    };
    void rebuildDisplayLut();
    std::array<std::uint8_t, 256> displayLut_;

    mutable std::vector<HorizontalSample> horizontalCache_;
    mutable std::vector<HorizontalSample> reconstructedHorizontalCache_;

    mutable int cachedInputWidth_ = 0;
    mutable int cachedOutputWidth_ = 0;
    mutable int cachedReconstructedWidth_ = 0;
    mutable int cachedReconstructedOutputWidth_ = 0;
    double displayGamma_ = 1.0;

    int highlightedLine_ = -1;
};