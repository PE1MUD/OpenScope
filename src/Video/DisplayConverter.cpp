#include "DisplayConverter.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <QtGlobal>
#include <QElapsedTimer>
#include <QDebug>

void DisplayConverter::setHighlightedLine(int line)
{
    highlightedLine_ = line;
}

QImage DisplayConverter::convert(
    const Yuv444Frame& frame,
    const ReconstructedLumaFrame& reconstructedLuma,
    int outputWidth,
    int outputHeight) const
{
    if (frame.width <= 0 ||
        frame.height <= 0 ||
        outputWidth <= 0 ||
        outputHeight <= 0 ||
        frame.u.empty() ||
        frame.v.empty() ||
        reconstructedLuma.width <= 0 ||
        reconstructedLuma.height < frame.height ||
        reconstructedLuma.y.empty())
    {
        return {};
    }

    QElapsedTimer timer;
    timer.start();

    QImage image(
        outputWidth,
        outputHeight,
        QImage::Format_RGB32);

    const float horizontalScale =
        static_cast<float>(frame.width) /
        static_cast<float>(outputWidth);

    const float reconstructedHorizontalScale =
        static_cast<float>(reconstructedLuma.width) /
        static_cast<float>(outputWidth);

    if (cachedInputWidth_ != frame.width ||
        cachedOutputWidth_ != outputWidth)
    {
        horizontalCache_.resize(
            static_cast<std::size_t>(outputWidth));

        for (int outputX = 0;
            outputX < outputWidth;
            ++outputX)
        {
            const float sourcePosition =
                (static_cast<float>(outputX) + 0.5f) *
                horizontalScale -
                0.5f;

            HorizontalSample& sample =
                horizontalCache_[
                    static_cast<std::size_t>(outputX)];

            sample.leftIndex =
                std::clamp(
                    static_cast<int>(
                        std::floor(sourcePosition)),
                    0,
                    frame.width - 1);

            sample.rightIndex =
                std::min(
                    sample.leftIndex + 1,
                    frame.width - 1);

            sample.fraction =
                std::clamp(
                    sourcePosition -
                    static_cast<float>(sample.leftIndex),
                    0.0f,
                    1.0f);
        }

        cachedInputWidth_ = frame.width;
        cachedOutputWidth_ = outputWidth;
    }
    if (cachedReconstructedWidth_ != reconstructedLuma.width ||
        cachedReconstructedOutputWidth_ != outputWidth)
    {
        reconstructedHorizontalCache_.resize(
            static_cast<std::size_t>(outputWidth));

        for (int outputX = 0;
            outputX < outputWidth;
            ++outputX)
        {
            const float sourcePosition =
                (static_cast<float>(outputX) + 0.5f) *
                reconstructedHorizontalScale -
                0.5f;

            HorizontalSample& sample =
                reconstructedHorizontalCache_[
                    static_cast<std::size_t>(outputX)];

            sample.leftIndex =
                std::clamp(
                    static_cast<int>(
                        std::floor(sourcePosition)),
                    0,
                    reconstructedLuma.width - 1);

            sample.rightIndex =
                std::min(
                    sample.leftIndex + 1,
                    reconstructedLuma.width - 1);

            sample.fraction =
                std::clamp(
                    sourcePosition -
                    static_cast<float>(sample.leftIndex),
                    0.0f,
                    1.0f);
        }

        cachedReconstructedWidth_ =
            reconstructedLuma.width;

        cachedReconstructedOutputWidth_ =
            outputWidth;
    }
    const float verticalScale =
        static_cast<float>(frame.height) /
        static_cast<float>(outputHeight);

    const qint64 setupUs =
        timer.nsecsElapsed() / 1000;

    qint64 composeUs = 0;

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

        const auto* srcU =
            frame.u.data() + lineOffset;

        const auto* srcV =
            frame.v.data() + lineOffset;

        const bool invertLine =
            sourceLine == highlightedLine_;

        timer.restart();

        for (int outputX = 0;
            outputX < outputWidth;
            ++outputX)
        {
            const HorizontalSample& sample =
                horizontalCache_[
                    static_cast<std::size_t>(outputX)];

            const int leftIndex =
                sample.leftIndex;

            const int rightIndex =
                sample.rightIndex;

            const float fraction =
                sample.fraction;

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

            const HorizontalSample& reconstructedSample =
                reconstructedHorizontalCache_[
                    static_cast<std::size_t>(outputX)];

            const std::size_t reconstructedLineOffset =
                static_cast<std::size_t>(sourceLine) *
                static_cast<std::size_t>(reconstructedLuma.width);

            const auto* reconstructedLine =
                reconstructedLuma.y.data() +
                reconstructedLineOffset;

            const float reconstructedY =
                std::clamp(
                    static_cast<float>(
                        reconstructedLine[reconstructedSample.leftIndex]) +
                    (
                        static_cast<float>(
                            reconstructedLine[reconstructedSample.rightIndex]) -
                        static_cast<float>(
                            reconstructedLine[reconstructedSample.leftIndex])
                        ) *
                    reconstructedSample.fraction,
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

            const int c =
                298 * yy;

            const int r =
                (c + 409 * v + 128) >> 8;

            const int g =
                (c - 100 * u - 208 * v + 128) >> 8;

            const int b =
                (c + 516 * u + 128) >> 8;

            int outR =
                qBound(0, r, 255);

            int outG =
                qBound(0, g, 255);

            int outB =
                qBound(0, b, 255);

            if (invertLine)
            {
                outR = 255 - outR;
                outG = 255 - outG;
                outB = 255 - outB;
            }

            dst[outputX] =
                qRgb(
                    outR,
                    outG,
                    outB);
        }

        composeUs +=
            timer.nsecsElapsed() / 1000;
    }

    const qint64 totalUs =
        setupUs +
        composeUs;


    return image;
}