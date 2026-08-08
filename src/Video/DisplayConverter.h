#pragma once

#include "Yuv444Frame.h"
#include "video/ReconstructedLumaFrame.h"

#include <QImage>

#include <vector>

class DisplayConverter
{
public:
    QImage convert(
        const Yuv444Frame& frame,
        const ReconstructedLumaFrame& reconstructedLuma,
        int outputWidth,
        int outputHeight) const;

    void setHighlightedLine(int line);

private:
    struct HorizontalSample
    {
        int leftIndex;
        int rightIndex;
        float fraction;
    };

    mutable std::vector<HorizontalSample> horizontalCache_;
    mutable std::vector<HorizontalSample> reconstructedHorizontalCache_;

    mutable int cachedInputWidth_ = 0;
    mutable int cachedOutputWidth_ = 0;
    mutable int cachedReconstructedWidth_ = 0;
    mutable int cachedReconstructedOutputWidth_ = 0;

    int highlightedLine_ = -1;
};