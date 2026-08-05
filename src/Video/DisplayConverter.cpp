#include "DisplayConverter.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <QtGlobal>

void DisplayConverter::setHighlightedLine(int line)
{
    highlightedLine_ = line;
}

QImage DisplayConverter::convert(
    const Yuv444Frame& frame,
    int outputWidth,
    int outputHeight) const
{
    if (frame.width <= 0 ||
        frame.height <= 0 ||
        outputWidth <= 0 ||
        outputHeight <= 0 ||
        frame.y.empty() ||
        frame.u.empty() ||
        frame.v.empty())
    {
        return {};
    }

    QImage image(
        outputWidth,
        outputHeight,
        QImage::Format_RGB32);

    sourceY_.resize(
        static_cast<std::size_t>(frame.width));

    displayY_.resize(
        static_cast<std::size_t>(outputWidth));

    const float horizontalScale =
        static_cast<float>(frame.width) /
        static_cast<float>(outputWidth);

    const float verticalScale =
        static_cast<float>(frame.height) /
        static_cast<float>(outputHeight);

    for (int outputY = 0;
        outputY < outputHeight;
        ++outputY)
    {
        auto* dst =
            reinterpret_cast<QRgb*>(
                image.scanLine(outputY));

        const float sourceLinePosition =
            (static_cast<float>(outputY) + 0.5f) *
            verticalScale -
            0.5f;

        const int sourceLine =
            std::clamp(
                static_cast<int>(
                    std::round(sourceLinePosition)),
                0,
                frame.height - 1);

        const std::size_t lineOffset =
            static_cast<std::size_t>(sourceLine) *
            static_cast<std::size_t>(frame.width);

        const auto* srcY =
            frame.y.data() + lineOffset;

        const auto* srcU =
            frame.u.data() + lineOffset;

        const auto* srcV =
            frame.v.data() + lineOffset;

        for (int sourceX = 0;
            sourceX < frame.width;
            ++sourceX)
        {
            sourceY_[
                static_cast<std::size_t>(sourceX)] =
                static_cast<float>(srcY[sourceX]);
        }

        // Y gets the full sinc reconstruction.
        lineResampler_.resample(
            sourceY_,
            displayY_);

        const bool invertLine =
            sourceLine == highlightedLine_;

        for (int outputX = 0;
            outputX < outputWidth;
            ++outputX)
        {
            // U and V use cheap linear interpolation.
            const float sourcePosition =
                (static_cast<float>(outputX) + 0.5f) *
                horizontalScale -
                0.5f;

            const int leftIndex =
                std::clamp(
                    static_cast<int>(
                        std::floor(sourcePosition)),
                    0,
                    frame.width - 1);

            const int rightIndex =
                std::min(
                    leftIndex + 1,
                    frame.width - 1);

            const float fraction =
                std::clamp(
                    sourcePosition -
                    static_cast<float>(leftIndex),
                    0.0f,
                    1.0f);

            const float interpolatedU =
                static_cast<float>(srcU[leftIndex]) +
                (static_cast<float>(srcU[rightIndex]) -
                    static_cast<float>(srcU[leftIndex])) *
                fraction;

            const float interpolatedV =
                static_cast<float>(srcV[leftIndex]) +
                (static_cast<float>(srcV[rightIndex]) -
                    static_cast<float>(srcV[leftIndex])) *
                fraction;

            const float reconstructedY =
                std::clamp(
                    displayY_[
                        static_cast<std::size_t>(outputX)],
                        0.0f,
                        65535.0f);

            const int yy =
                static_cast<int>(
                    reconstructedY / 256.0f) -
                16;

            const int u =
                static_cast<int>(
                    std::clamp(
                        interpolatedU,
                        0.0f,
                        65535.0f) /
                    256.0f) -
                128;

            const int v =
                static_cast<int>(
                    std::clamp(
                        interpolatedV,
                        0.0f,
                        65535.0f) /
                    256.0f) -
                128;

            const int c = 298 * yy;

            const int r =
                (c + 409 * v + 128) >> 8;

            const int g =
                (c - 100 * u - 208 * v + 128) >> 8;

            const int b =
                (c + 516 * u + 128) >> 8;

            int outR = qBound(0, r, 255);
            int outG = qBound(0, g, 255);
            int outB = qBound(0, b, 255);

            if (invertLine)
            {
                outR = 255 - outR;
                outG = 255 - outG;
                outB = 255 - outB;
            }

            dst[outputX] =
                qRgb(outR, outG, outB);
        }
    }

    return image;
}