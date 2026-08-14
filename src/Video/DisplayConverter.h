#pragma once

#include "Yuv444Frame.h"
#include "video/DisplayPerformance.h"

#include <QImage>

#include <vector>
#include <array>
#include <cstdint>

enum class DisplayConversionImplementation
{
    Scalar,
    Avx2
};

class DisplayConverter
{
public:
    QImage convert(
        const Yuv444Frame& frame,
        int outputWidth,
        int outputHeight,
        DisplayPerformance& performance) const;

    void setHighlightedLine(int line);
    DisplayConverter();
    void setGamma(double gamma);
    void setImplementation(
        DisplayConversionImplementation implementation);

private:
    void rebuildDisplayLut();
    std::array<std::uint8_t, 256> displayLut_;
    std::array<int, 256> yToC_;
    std::array<int, 256> vToRed_;
    std::array<int, 256> uToGreen_;
    std::array<int, 256> vToGreen_;
    std::array<int, 256> uToBlue_;
    std::array<int, 256> displayLut32_;

    mutable std::vector<int> horizontalLeftIndex_;
    mutable std::vector<int> horizontalRightIndex_;
    mutable std::vector<float> horizontalFraction_;

    mutable int cachedInputWidth_ = 0;
    mutable int cachedOutputWidth_ = 0;
    double displayGamma_ = 1.0;

    int highlightedLine_ = -1;
    void rebuildColorConversionLuts();
    DisplayConversionImplementation implementation_{
        DisplayConversionImplementation::Scalar
    };
    QImage convertScalar(
        const Yuv444Frame& frame,
        int outputWidth,
        int outputHeight,
        DisplayPerformance& performance) const;
    QImage convertAvx2(
        const Yuv444Frame& frame,
        int outputWidth,
        int outputHeight,
        DisplayPerformance& performance) const;
};