#pragma once

#include "Yuv444Frame.h"
#include "processing/SignalReconstructor.h"

#include <QImage>

#include <vector>

class DisplayConverter
{
public:
    QImage convert(
        const Yuv444Frame& frame,
        int outputWidth,
        int outputHeight) const;

    void setHighlightedLine(int line);

private:
    LineResampler lineResampler_{ 24, 1.00f };

    mutable std::vector<float> sourceY_;
    mutable std::vector<float> displayY_;

    int highlightedLine_ = -1;
};