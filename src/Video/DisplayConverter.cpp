#include "DisplayConverter.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <QtGlobal>
#include <QElapsedTimer>
#include <immintrin.h>

DisplayConverter::DisplayConverter()
{
    rebuildDisplayLut();
    rebuildColorConversionLuts();
}

void DisplayConverter::rebuildColorConversionLuts()
{
    for (int i = 0;
        i < 256;
        ++i)
    {
        const int y =
            i - 16;

        const int chroma =
            i - 128;

        yToC_[static_cast<std::size_t>(i)] =
            298 * y;

        vToRed_[static_cast<std::size_t>(i)] =
            409 * chroma;

        uToGreen_[static_cast<std::size_t>(i)] =
            -100 * chroma;

        vToGreen_[static_cast<std::size_t>(i)] =
            -208 * chroma;

        uToBlue_[static_cast<std::size_t>(i)] =
            516 * chroma;
    }
}

void DisplayConverter::setHighlightedLine(int line)
{
    highlightedLine_ = line;
}

namespace
{
    inline __m256i clampInt32ToByteRange(
        __m256i value)
    {
        const __m256i zero =
            _mm256_setzero_si256();

        const __m256i maximum =
            _mm256_set1_epi32(255);

        value =
            _mm256_max_epi32(
                value,
                zero);

        value =
            _mm256_min_epi32(
                value,
                maximum);

        return value;
    }
}

QImage DisplayConverter::convert(
    const Yuv444Frame& frame,
    const std::uint16_t* luma,
    int outputWidth,
    int outputHeight,
    DisplayPerformance& performance) const
{
    switch (implementation_)
    {
    case DisplayConversionImplementation::Avx2:
        return convertAvx2(
            frame,
            luma,
            outputWidth,
            outputHeight,
            performance);

    case DisplayConversionImplementation::Scalar:
    default:
        return convertScalar(
            frame,
            luma,
            outputWidth,
            outputHeight,
            performance);
    }
}

QImage DisplayConverter::convertScalar(
    const Yuv444Frame& frame,
    const std::uint16_t* luma,
    int outputWidth,
    int outputHeight,
    DisplayPerformance& performance) const
{
    performance = {};

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

    QElapsedTimer timer;
    timer.start();

    QImage image(
        outputWidth,
        outputHeight,
        QImage::Format_RGB32);

    performance.allocationUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    timer.restart();

    const float horizontalScale =
        static_cast<float>(frame.width) /
        static_cast<float>(outputWidth);

    if (cachedInputWidth_ != frame.width ||
        cachedOutputWidth_ != outputWidth)
    {
        horizontalLeftIndex_.resize(
            static_cast<std::size_t>(outputWidth));

        horizontalRightIndex_.resize(
            static_cast<std::size_t>(outputWidth));

        horizontalFraction_.resize(
            static_cast<std::size_t>(outputWidth));

        for (int outputX = 0;
            outputX < outputWidth;
            ++outputX)
        {
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

            const std::size_t index =
                static_cast<std::size_t>(outputX);

            horizontalLeftIndex_[index] =
                leftIndex;

            horizontalRightIndex_[index] =
                rightIndex;

            horizontalFraction_[index] =
                fraction;
        }

        cachedInputWidth_ =
            frame.width;

        cachedOutputWidth_ =
            outputWidth;
    }

    const float verticalScale =
        static_cast<float>(frame.height) /
        static_cast<float>(outputHeight);

    const bool limitHighlightX =
        highlightedEndX_ >= 0;

    const int highlightedOutputStartX =
        limitHighlightX
        ? std::clamp(
            highlightedStartX_ *
            outputWidth /
            frame.width,
            0,
            outputWidth)
        : 0;

    const int highlightedOutputEndX =
        limitHighlightX
        ? std::clamp(
            highlightedEndX_ *
            outputWidth /
            frame.width,
            0,
            outputWidth)
        : outputWidth;

    performance.setupUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    timer.restart();

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
            luma +
            lineOffset;

        const auto* srcU =
            frame.u.data() +
            lineOffset;

        const auto* srcV =
            frame.v.data() +
            lineOffset;

        const int highlightedOutputY =
            static_cast<int>(
                std::round(
                    (
                        static_cast<double>(
                            highlightedLine_) +
                        0.5
                        ) *
                    static_cast<double>(outputHeight) /
                    static_cast<double>(frame.height) -
                    0.5));

        const bool invertLine =
            highlightedLine_ >= 0 &&
            outputY >= highlightedOutputY &&
            outputY < highlightedOutputY + 2;

        for (int outputX = 0;
            outputX < outputWidth;
            ++outputX)
        {
            const std::size_t index =
                static_cast<std::size_t>(outputX);

            const int sourceIndex =
                horizontalLeftIndex_[index];

            const float sourceY =
                static_cast<float>(
                    srcY[sourceIndex]);

            const float sourceU =
                static_cast<float>(
                    srcU[sourceIndex]);

            const float sourceV =
                static_cast<float>(
                    srcV[sourceIndex]);

            const int y8 =
                std::clamp(
                    static_cast<int>(
                        sourceY / 256.0f),
                    0,
                    255);

            const int u8 =
                std::clamp(
                    static_cast<int>(
                        sourceU / 256.0f),
                    0,
                    255);

            const int v8 =
                std::clamp(
                    static_cast<int>(
                        sourceV / 256.0f),
                    0,
                    255);

            const int c =
                yToC_[
                    static_cast<std::size_t>(y8)];

            const int r =
                (c +
                    vToRed_[
                        static_cast<std::size_t>(v8)] +
                    128) >> 8;

            const int g =
                (c +
                    uToGreen_[
                        static_cast<std::size_t>(u8)] +
                    vToGreen_[
                        static_cast<std::size_t>(v8)] +
                            128) >> 8;

            const int b =
                (c +
                    uToBlue_[
                        static_cast<std::size_t>(u8)] +
                    128) >> 8;

            int outR =
                qBound(
                    0,
                    r,
                    255);

            int outG =
                qBound(
                    0,
                    g,
                    255);

            int outB =
                qBound(
                    0,
                    b,
                    255);

            outR =
                displayLut_[
                    static_cast<std::size_t>(outR)];

            outG =
                displayLut_[
                    static_cast<std::size_t>(outG)];

            outB =
                displayLut_[
                    static_cast<std::size_t>(outB)];

            if (invertLine)
            {
                outR =
                    255 - outR;

                outG =
                    255 - outG;

                outB =
                    255 - outB;
            }

            dst[outputX] =
                qRgb(
                    outR,
                    outG,
                    outB);
        }
    }

    performance.composeUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    return image;
}
void DisplayConverter::setGamma(double gamma)
{
    displayGamma_ = gamma;

    rebuildDisplayLut();
}

void DisplayConverter::rebuildDisplayLut()
{
    const double gamma =
        displayGamma_;

    for (std::size_t i = 0;
        i < displayLut_.size();
        ++i)
    {
        const double input =
            static_cast<double>(i) /
            255.0;

        const double output =
            std::pow(
                input,
                gamma);

        displayLut_[i] =
            static_cast<std::uint8_t>(
                std::clamp(
                    std::lround(
                        output * 255.0),
                    0l,
                    255l));
        displayLut32_[i] =
            static_cast<int>(
                displayLut_[i]);
    }
}

void DisplayConverter::setImplementation(
    DisplayConversionImplementation implementation)
{
    implementation_ =
        implementation;
}
QImage DisplayConverter::convertAvx2(
    const Yuv444Frame& frame,
    const std::uint16_t* luma,
    int outputWidth,
    int outputHeight,
    DisplayPerformance& performance) const
{
    performance = {};

    if (frame.width <= 0 ||
        frame.height <= 0 ||
        outputWidth <= 0 ||
        outputHeight <= 0 ||
        frame.u.empty() ||
        frame.v.empty() ||
        frame.y.empty())
    {
        return {};
    }

    QElapsedTimer timer;
    timer.start();

    QImage image(
        outputWidth,
        outputHeight,
        QImage::Format_RGB32);

    performance.allocationUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    timer.restart();

    const float horizontalScale =
        static_cast<float>(frame.width) /
        static_cast<float>(outputWidth);

    if (cachedInputWidth_ != frame.width ||
        cachedOutputWidth_ != outputWidth)
    {
        horizontalLeftIndex_.resize(
            static_cast<std::size_t>(outputWidth));

        horizontalRightIndex_.resize(
            static_cast<std::size_t>(outputWidth));

        horizontalFraction_.resize(
            static_cast<std::size_t>(outputWidth));

        for (int outputX = 0;
            outputX < outputWidth;
            ++outputX)
        {
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

            const std::size_t index =
                static_cast<std::size_t>(outputX);

            horizontalLeftIndex_[index] =
                leftIndex;

            horizontalRightIndex_[index] =
                rightIndex;

            horizontalFraction_[index] =
                fraction;
        }

        cachedInputWidth_ =
            frame.width;

        cachedOutputWidth_ =
            outputWidth;
    }

    const float verticalScale =
        static_cast<float>(frame.height) /
        static_cast<float>(outputHeight);

    const bool limitHighlightX =
        highlightedEndX_ >= 0;

    const int highlightedOutputStartX =
        limitHighlightX
        ? std::clamp(
            highlightedStartX_ *
            outputWidth /
            frame.width,
            0,
            outputWidth)
        : 0;

    const int highlightedOutputEndX =
        limitHighlightX
        ? std::clamp(
            highlightedEndX_ *
            outputWidth /
            frame.width,
            0,
            outputWidth)
        : outputWidth;

    performance.setupUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    timer.restart();

    const __m256 scaleTo8Bit =
        _mm256_set1_ps(
            1.0f / 256.0f);

    const __m256i yOffset =
        _mm256_set1_epi32(16);

    const __m256i chromaOffset =
        _mm256_set1_epi32(128);

    const __m256i coefficientY =
        _mm256_set1_epi32(298);

    const __m256i coefficientRV =
        _mm256_set1_epi32(409);

    const __m256i coefficientGU =
        _mm256_set1_epi32(100);

    const __m256i coefficientGV =
        _mm256_set1_epi32(208);

    const __m256i coefficientBU =
        _mm256_set1_epi32(516);

    const __m256i rounding =
        _mm256_set1_epi32(128);

    const __m256i maximum =
        _mm256_set1_epi32(255);

    const __m256i alpha =
        _mm256_set1_epi32(
            static_cast<int>(
                0xff000000u));

    alignas(32) int yPreviousLeft[8];
    alignas(32) int yPreviousRight[8];

    alignas(32) int yTopLeft[8];
    alignas(32) int yTopRight[8];

    alignas(32) int yBottomLeft[8];
    alignas(32) int yBottomRight[8];

    alignas(32) int yNextLeft[8];
    alignas(32) int yNextRight[8];

    alignas(32) int uTopLeft[8];
    alignas(32) int uTopRight[8];

    alignas(32) int uBottomLeft[8];
    alignas(32) int uBottomRight[8];

    alignas(32) int vTopLeft[8];
    alignas(32) int vTopRight[8];

    alignas(32) int vBottomLeft[8];
    alignas(32) int vBottomRight[8];

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

        const int topLine =
            std::clamp(
                static_cast<int>(
                    std::floor(sourceLinePosition)),
                0,
                frame.height - 1);

        const int bottomLine =
            std::min(
                topLine + 1,
                frame.height - 1);

        const int previousLine =
            std::max(
                topLine - 1,
                0);

        const int nextLine =
            std::min(
                bottomLine + 1,
                frame.height - 1);

        const float verticalFraction =
            std::clamp(
                sourceLinePosition -
                static_cast<float>(topLine),
                0.0f,
                1.0f);

        const std::size_t topLineOffset =
            static_cast<std::size_t>(topLine) *
            static_cast<std::size_t>(frame.width);

        const std::size_t bottomLineOffset =
            static_cast<std::size_t>(bottomLine) *
            static_cast<std::size_t>(frame.width);

        const std::size_t previousLineOffset =
            static_cast<std::size_t>(previousLine) *
            static_cast<std::size_t>(frame.width);

        const std::size_t nextLineOffset =
            static_cast<std::size_t>(nextLine) *
            static_cast<std::size_t>(frame.width);


        const auto* srcYPrevious =
            luma +
            previousLineOffset;

        const auto* srcYTop =
            luma +
            topLineOffset;

        const auto* srcYBottom =
            luma +
            bottomLineOffset;

        const auto* srcYNext =
            luma +
            nextLineOffset;

        const auto* srcUTop =
            frame.u.data() +
            topLineOffset;

        const auto* srcUBottom =
            frame.u.data() +
            bottomLineOffset;

        const auto* srcVTop =
            frame.v.data() +
            topLineOffset;

        const auto* srcVBottom =
            frame.v.data() +
            bottomLineOffset;

        const int highlightedOutputY =
            static_cast<int>(
                std::round(
                    (
                        static_cast<double>(
                            highlightedLine_) +
                        0.5
                        ) *
                    static_cast<double>(
                        outputHeight) /
                    static_cast<double>(
                        frame.height) -
                    0.5));

        const bool highlightThisLine =
            highlightedLine_ >= 0 &&
            outputY >= highlightedOutputY &&
            outputY < highlightedOutputY + 2;

        int outputX = 0;

        for (;
            outputX + 7 < outputWidth;
            outputX += 8)
        {
            for (int lane = 0;
                lane < 8;
                ++lane)
            {
                const std::size_t index =
                    static_cast<std::size_t>(
                        outputX + lane);

                const int leftIndex =
                    horizontalLeftIndex_[index];

                const int rightIndex =
                    horizontalRightIndex_[index];

                yTopLeft[lane] =
                    srcYTop[leftIndex];

                yTopRight[lane] =
                    srcYTop[rightIndex];

                yPreviousLeft[lane] =
                    srcYPrevious[leftIndex];

                yPreviousRight[lane] =
                    srcYPrevious[rightIndex];

                yNextLeft[lane] =
                    srcYNext[leftIndex];

                yNextRight[lane] =
                    srcYNext[rightIndex];

                yBottomLeft[lane] =
                    srcYBottom[leftIndex];

                yBottomRight[lane] =
                    srcYBottom[rightIndex];

                uTopLeft[lane] =
                    srcUTop[leftIndex];

                uTopRight[lane] =
                    srcUTop[rightIndex];

                uBottomLeft[lane] =
                    srcUBottom[leftIndex];

                uBottomRight[lane] =
                    srcUBottom[rightIndex];

                vTopLeft[lane] =
                    srcVTop[leftIndex];

                vTopRight[lane] =
                    srcVTop[rightIndex];

                vBottomLeft[lane] =
                    srcVBottom[leftIndex];

                vBottomRight[lane] =
                    srcVBottom[rightIndex];
            }

            const __m256 yTopLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yTopLeft)));

            const __m256 yTopRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yTopRight)));

            const __m256 yBottomLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yBottomLeft)));

            const __m256 yBottomRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yBottomRight)));

            const __m256 yPreviousLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yPreviousLeft)));

            const __m256 yPreviousRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yPreviousRight)));

            const __m256 yNextLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yNextLeft)));

            const __m256 yNextRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            yNextRight)));

            const __m256 uTopLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            uTopLeft)));

            const __m256 uTopRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            uTopRight)));

            const __m256 uBottomLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            uBottomLeft)));

            const __m256 uBottomRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            uBottomRight)));

            const __m256 vTopLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            vTopLeft)));

            const __m256 vTopRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            vTopRight)));

            const __m256 vBottomLeftFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            vBottomLeft)));

            const __m256 vBottomRightFloat =
                _mm256_cvtepi32_ps(
                    _mm256_load_si256(
                        reinterpret_cast<
                        const __m256i*>(
                            vBottomRight)));

            const __m256 fractionVector =
                _mm256_loadu_ps(
                    horizontalFraction_.data() +
                    outputX);
            
            const __m256 yPrevious =
                _mm256_add_ps(
                    yPreviousLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            yPreviousRightFloat,
                            yPreviousLeftFloat),
                        fractionVector));

            const __m256 yNext =
                _mm256_add_ps(
                    yNextLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            yNextRightFloat,
                            yNextLeftFloat),
                        fractionVector));

            const __m256 yTop =
                _mm256_add_ps(
                    yTopLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            yTopRightFloat,
                            yTopLeftFloat),
                        fractionVector));

            const __m256 yBottom =
                _mm256_add_ps(
                    yBottomLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            yBottomRightFloat,
                            yBottomLeftFloat),
                        fractionVector));

            const __m256 t =
                _mm256_set1_ps(
                    verticalFraction);

            const __m256 t2 =
                _mm256_mul_ps(
                    t,
                    t);

            const __m256 t3 =
                _mm256_mul_ps(
                    t2,
                    t);

            const __m256 term0 =
                _mm256_mul_ps(
                    _mm256_set1_ps(2.0f),
                    yTop);

            const __m256 term1 =
                _mm256_mul_ps(
                    _mm256_add_ps(
                        _mm256_sub_ps(
                            _mm256_setzero_ps(),
                            yPrevious),
                        yBottom),
                    t);

            const __m256 term2 =
                _mm256_mul_ps(
                    _mm256_add_ps(
                        _mm256_sub_ps(
                            _mm256_mul_ps(
                                _mm256_set1_ps(2.0f),
                                yPrevious),
                            _mm256_mul_ps(
                                _mm256_set1_ps(5.0f),
                                yTop)),
                        _mm256_sub_ps(
                            _mm256_mul_ps(
                                _mm256_set1_ps(4.0f),
                                yBottom),
                            yNext)),
                    t2);

            const __m256 term3 =
                _mm256_mul_ps(
                    _mm256_add_ps(
                        _mm256_sub_ps(
                            yNext,
                            yPrevious),
                        _mm256_mul_ps(
                            _mm256_set1_ps(3.0f),
                            _mm256_sub_ps(
                                yTop,
                                yBottom))),
                    t3);

            __m256 interpolatedY =
                _mm256_mul_ps(
                    _mm256_set1_ps(0.5f),
                    _mm256_add_ps(
                        _mm256_add_ps(
                            term0,
                            term1),
                        _mm256_add_ps(
                            term2,
                            term3)));

            const __m256 uTop =
                _mm256_add_ps(
                    uTopLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            uTopRightFloat,
                            uTopLeftFloat),
                        fractionVector));

            const __m256 uBottom =
                _mm256_add_ps(
                    uBottomLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            uBottomRightFloat,
                            uBottomLeftFloat),
                        fractionVector));

            const __m256 vTop =
                _mm256_add_ps(
                    vTopLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            vTopRightFloat,
                            vTopLeftFloat),
                        fractionVector));

            const __m256 vBottom =
                _mm256_add_ps(
                    vBottomLeftFloat,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            vBottomRightFloat,
                            vBottomLeftFloat),
                        fractionVector));

            const __m256 chromaVerticalFraction =
                _mm256_set1_ps(
                    verticalFraction);

            const __m256 interpolatedU =
                _mm256_add_ps(
                    uTop,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            uBottom,
                            uTop),
                        chromaVerticalFraction));

            const __m256 interpolatedV =
                _mm256_add_ps(
                    vTop,
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            vBottom,
                            vTop),
                        chromaVerticalFraction));

            const __m256i yVector =
                _mm256_sub_epi32(
                    _mm256_cvttps_epi32(
                        _mm256_mul_ps(
                            interpolatedY,
                            scaleTo8Bit)),
                    yOffset);

            const __m256i uVector =
                _mm256_sub_epi32(
                    _mm256_cvttps_epi32(
                        _mm256_mul_ps(
                            interpolatedU,
                            scaleTo8Bit)),
                    chromaOffset);

            const __m256i vVector =
                _mm256_sub_epi32(
                    _mm256_cvttps_epi32(
                        _mm256_mul_ps(
                            interpolatedV,
                            scaleTo8Bit)),
                    chromaOffset);

            const __m256i c =
                _mm256_mullo_epi32(
                    yVector,
                    coefficientY);

            __m256i red =
                _mm256_add_epi32(
                    c,
                    _mm256_mullo_epi32(
                        vVector,
                        coefficientRV));

            red =
                _mm256_add_epi32(
                    red,
                    rounding);

            red =
                _mm256_srai_epi32(
                    red,
                    8);

            __m256i green =
                _mm256_sub_epi32(
                    c,
                    _mm256_mullo_epi32(
                        uVector,
                        coefficientGU));

            green =
                _mm256_sub_epi32(
                    green,
                    _mm256_mullo_epi32(
                        vVector,
                        coefficientGV));

            green =
                _mm256_add_epi32(
                    green,
                    rounding);

            green =
                _mm256_srai_epi32(
                    green,
                    8);

            __m256i blue =
                _mm256_add_epi32(
                    c,
                    _mm256_mullo_epi32(
                        uVector,
                        coefficientBU));

            blue =
                _mm256_add_epi32(
                    blue,
                    rounding);

            blue =
                _mm256_srai_epi32(
                    blue,
                    8);

            red =
                clampInt32ToByteRange(
                    red);

            green =
                clampInt32ToByteRange(
                    green);

            blue =
                clampInt32ToByteRange(
                    blue);

            __m256i outR =
                _mm256_i32gather_epi32(
                    displayLut32_.data(),
                    red,
                    4);

            __m256i outG =
                _mm256_i32gather_epi32(
                    displayLut32_.data(),
                    green,
                    4);

            __m256i outB =
                _mm256_i32gather_epi32(
                    displayLut32_.data(),
                    blue,
                    4);

            if (highlightThisLine)
            {
                const __m256i xPositions =
                    _mm256_setr_epi32(
                        outputX + 0,
                        outputX + 1,
                        outputX + 2,
                        outputX + 3,
                        outputX + 4,
                        outputX + 5,
                        outputX + 6,
                        outputX + 7);

                const __m256i highlightStartVector =
                    _mm256_set1_epi32(
                        highlightedOutputStartX);

                const __m256i highlightEndVector =
                    _mm256_set1_epi32(
                        highlightedOutputEndX);

                const __m256i atOrAfterStart =
                    _mm256_cmpgt_epi32(
                        xPositions,
                        _mm256_sub_epi32(
                            highlightStartVector,
                            _mm256_set1_epi32(1)));

                const __m256i beforeEnd =
                    _mm256_cmpgt_epi32(
                        highlightEndVector,
                        xPositions);

                const __m256i highlightMask =
                    _mm256_and_si256(
                        atOrAfterStart,
                        beforeEnd);

                const __m256i invertedR =
                    _mm256_sub_epi32(
                        maximum,
                        outR);

                const __m256i invertedG =
                    _mm256_sub_epi32(
                        maximum,
                        outG);

                const __m256i invertedB =
                    _mm256_sub_epi32(
                        maximum,
                        outB);

                outR =
                    _mm256_blendv_epi8(
                        outR,
                        invertedR,
                        highlightMask);

                outG =
                    _mm256_blendv_epi8(
                        outG,
                        invertedG,
                        highlightMask);

                outB =
                    _mm256_blendv_epi8(
                        outB,
                        invertedB,
                        highlightMask);
            }

            __m256i pixels =
                alpha;

            pixels =
                _mm256_or_si256(
                    pixels,
                    _mm256_slli_epi32(
                        outR,
                        16));

            pixels =
                _mm256_or_si256(
                    pixels,
                    _mm256_slli_epi32(
                        outG,
                        8));

            pixels =
                _mm256_or_si256(
                    pixels,
                    outB);

            _mm256_storeu_si256(
                reinterpret_cast<__m256i*>(
                    dst + outputX),
                pixels);
        }

        //
        // Scalar tail: alleen als outputWidth
        // niet deelbaar is door 8.
        //
        for (;
            outputX < outputWidth;
            ++outputX)
        {
            const std::size_t index =
                static_cast<std::size_t>(
                    outputX);

            const int leftIndex =
                horizontalLeftIndex_[index];

            const int rightIndex =
                horizontalRightIndex_[index];

            const float fraction =
                horizontalFraction_[index];

            const float yPrevious =
                static_cast<float>(
                    srcYPrevious[leftIndex]) +
                (
                    static_cast<float>(
                        srcYPrevious[rightIndex]) -
                    static_cast<float>(
                        srcYPrevious[leftIndex])
                    ) *
                fraction;

            const float yTop =
                static_cast<float>(
                    srcYTop[leftIndex]) +
                (
                    static_cast<float>(
                        srcYTop[rightIndex]) -
                    static_cast<float>(
                        srcYTop[leftIndex])
                    ) *
                fraction;

            const float yBottom =
                static_cast<float>(
                    srcYBottom[leftIndex]) +
                (
                    static_cast<float>(
                        srcYBottom[rightIndex]) -
                    static_cast<float>(
                        srcYBottom[leftIndex])
                    ) *
                fraction;

            const float yNext =
                static_cast<float>(
                    srcYNext[leftIndex]) +
                (
                    static_cast<float>(
                        srcYNext[rightIndex]) -
                    static_cast<float>(
                        srcYNext[leftIndex])
                    ) *
                fraction;

            const float t =
                verticalFraction;

            const float t2 =
                t * t;

            const float t3 =
                t2 * t;

            const float interpolatedY =
                0.5f *
                (
                    2.0f * yTop +
                    (-yPrevious + yBottom) * t +
                    (2.0f * yPrevious -
                        5.0f * yTop +
                        4.0f * yBottom -
                        yNext) * t2 +
                    (-yPrevious +
                        3.0f * yTop -
                        3.0f * yBottom +
                        yNext) * t3
                    );

            const float uTop =
                static_cast<float>(
                    srcUTop[leftIndex]) +
                (
                    static_cast<float>(
                        srcUTop[rightIndex]) -
                    static_cast<float>(
                        srcUTop[leftIndex])
                    ) *
                fraction;

            const float uBottom =
                static_cast<float>(
                    srcUBottom[leftIndex]) +
                (
                    static_cast<float>(
                        srcUBottom[rightIndex]) -
                    static_cast<float>(
                        srcUBottom[leftIndex])
                    ) *
                fraction;

            const float interpolatedU =
                uTop +
                (uBottom - uTop) *
                verticalFraction;

            const float vTop =
                static_cast<float>(
                    srcVTop[leftIndex]) +
                (
                    static_cast<float>(
                        srcVTop[rightIndex]) -
                    static_cast<float>(
                        srcVTop[leftIndex])
                    ) *
                fraction;

            const float vBottom =
                static_cast<float>(
                    srcVBottom[leftIndex]) +
                (
                    static_cast<float>(
                        srcVBottom[rightIndex]) -
                    static_cast<float>(
                        srcVBottom[leftIndex])
                    ) *
                fraction;

            const float interpolatedV =
                vTop +
                (vBottom - vTop) *
                verticalFraction;

            const int yy =
                static_cast<int>(
                    interpolatedY /
                    256.0f) -
                16;

            const int u =
                static_cast<int>(
                    interpolatedU /
                    256.0f) -
                128;

            const int v =
                static_cast<int>(
                    interpolatedV /
                    256.0f) -
                128;

            const int c =
                298 * yy;

            const int r =
                (c + 409 * v + 128) >> 8;

            const int g =
                (c -
                    100 * u -
                    208 * v +
                    128) >> 8;

            const int b =
                (c +
                    516 * u +
                    128) >> 8;

            int outR =
                qBound(
                    0,
                    r,
                    255);

            int outG =
                qBound(
                    0,
                    g,
                    255);

            int outB =
                qBound(
                    0,
                    b,
                    255);

            outR =
                displayLut_[
                    static_cast<std::size_t>(
                        outR)];

            outG =
                displayLut_[
                    static_cast<std::size_t>(
                        outG)];

            outB =
                displayLut_[
                    static_cast<std::size_t>(
                        outB)];

            if (highlightThisLine)
            {
                const bool invertPixel =
                    highlightThisLine &&
                    outputX >= highlightedOutputStartX &&
                    outputX < highlightedOutputEndX;

                if (invertPixel)
                {
                    outR =
                        255 - outR;

                    outG =
                        255 - outG;

                    outB =
                        255 - outB;
                }
            }

            dst[outputX] =
                qRgb(
                    outR,
                    outG,
                    outB);
        }
    }

    performance.composeUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    return image;
}

void DisplayConverter::setHighlightedRange(
    int startX,
    int endX)
{
    highlightedStartX_ =
        startX;

    highlightedEndX_ =
        endX;
}