#include "rendering/WaveformRenderer.h"
#include "ui/ViewportOverlay.h"
#include "processing/SignalReconstructor.h"
#include "standards/VideoStandard.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QPen>
#include <QtGlobal>
#include <QApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <immintrin.h>

namespace
{



    struct GlowWorkload
    {
        std::uint32_t dirtyTiles = 0;
        std::uint32_t totalTiles = 0;
        std::uint32_t horizontalPass1Tiles = 0;
        std::uint32_t verticalPass1Tiles = 0;
        std::uint32_t horizontalPass2Tiles = 0;
        std::uint32_t verticalPass2Tiles = 0;
        int activeX = 0;
        int activeY = 0;
        int activeWidth = 0;
        int activeHeight = 0;
    };

    GlowWorkload applyHalfResolutionWhiteGlow(
        QImage& image,
        int glow)
    {
        GlowWorkload workload;
        if (glow <= 0 || image.isNull() ||
            image.width() < 2 || image.height() < 2)
        {
            return workload;
        }

        const int width = image.width();
        const int height = image.height();
        const int lowWidth = (width + 1) / 2;
        const int lowHeight = (height + 1) / 2;
        const std::size_t lowCount =
            static_cast<std::size_t>(lowWidth) *
            static_cast<std::size_t>(lowHeight);

        // Diagnostic tiling only.  These are glow-buffer tiles, not render
        // jobs yet.  Keeping the accounting in the already-required source
        // extraction pass avoids an extra full-image scan.
        constexpr int kTileWidth = 64;
        constexpr int kTileHeight = 32;
        const int tilesX = (lowWidth + kTileWidth - 1) / kTileWidth;
        const int tilesY = (lowHeight + kTileHeight - 1) / kTileHeight;
        workload.totalTiles = static_cast<std::uint32_t>(tilesX * tilesY);

        static thread_local std::vector<std::uint8_t> dirtyTiles;
        dirtyTiles.assign(
            static_cast<std::size_t>(workload.totalTiles),
            static_cast<std::uint8_t>(0));

        static thread_local std::vector<std::uint32_t> sourceActivity;
        static thread_local std::vector<std::uint32_t> horizontal1Activity;
        static thread_local std::vector<std::uint32_t> vertical1Activity;
        static thread_local std::vector<std::uint32_t> horizontal2Activity;
        static thread_local std::vector<std::uint32_t> vertical2Activity;
        sourceActivity.assign(dirtyTiles.size(), 0u);

        int minActiveX = lowWidth;
        int minActiveY = lowHeight;
        int maxActiveX = -1;
        int maxActiveY = -1;

        static thread_local std::vector<std::uint16_t> source;
        static thread_local std::vector<std::uint16_t> temp;
        static thread_local std::vector<std::uint16_t> blur;

        source.resize(lowCount);
        temp.resize(lowCount);
        blur.resize(lowCount);

        // Extract only the white component of the rendered trace. This keeps
        // the luma beam glowing without turning the coloured chroma fill into
        // a broad white haze.
        for (int ly = 0; ly < lowHeight; ++ly)
        {
            const int y0 = ly * 2;
            const int y1 = std::min(y0 + 1, height - 1);

            const auto* line0 = reinterpret_cast<const QRgb*>(image.constScanLine(y0));
            const auto* line1 = reinterpret_cast<const QRgb*>(image.constScanLine(y1));

            for (int lx = 0; lx < lowWidth; ++lx)
            {
                const int x0 = lx * 2;
                const int x1 = std::min(x0 + 1, width - 1);

                const auto white = [](QRgb pixel)
                {
                    return std::min({ qRed(pixel), qGreen(pixel), qBlue(pixel) });
                };

                const int value = std::max({
                    white(line0[x0]),
                    white(line0[x1]),
                    white(line1[x0]),
                    white(line1[x1])
                });

                source[static_cast<std::size_t>(ly) * lowWidth + lx] =
                    static_cast<std::uint16_t>(value);

                if (value > 0)
                {
                    minActiveX = std::min(minActiveX, lx);
                    minActiveY = std::min(minActiveY, ly);
                    maxActiveX = std::max(maxActiveX, lx);
                    maxActiveY = std::max(maxActiveY, ly);

                    const int tileX = lx / kTileWidth;
                    const int tileY = ly / kTileHeight;
                    const std::size_t tileIndex =
                        static_cast<std::size_t>(tileY) *
                        static_cast<std::size_t>(tilesX) +
                        static_cast<std::size_t>(tileX);

                    ++sourceActivity[tileIndex];

                    if (dirtyTiles[tileIndex] == 0)
                    {
                        dirtyTiles[tileIndex] = 1;
                        ++workload.dirtyTiles;
                    }
                }
            }
        }

        if (maxActiveX >= minActiveX && maxActiveY >= minActiveY)
        {
            workload.activeX = minActiveX * 2;
            workload.activeY = minActiveY * 2;
            workload.activeWidth =
                std::min(width - workload.activeX,
                    (maxActiveX - minActiveX + 1) * 2);
            workload.activeHeight =
                std::min(height - workload.activeY,
                    (maxActiveY - minActiveY + 1) * 2);
        }

        // Two cheap box passes approximate a Gaussian. The blur is done at
        // half resolution. From this point on, only active tiles are
        // processed. A tile propagates into neighbouring tiles only while
        // at least 5% of that tile still contains non-zero glow data.
        const double renderScale =
            static_cast<double>(height) / 576.0;
        const int radius = std::max(1, static_cast<int>(std::lround(renderScale)));
        const int horizontalTileExpansion =
            (radius + kTileWidth - 1) / kTileWidth;
        const int verticalTileExpansion =
            (radius + kTileHeight - 1) / kTileHeight;

        static thread_local std::vector<std::uint8_t> horizontalMask1;
        static thread_local std::vector<std::uint8_t> verticalMask1;
        static thread_local std::vector<std::uint8_t> horizontalMask2;
        static thread_local std::vector<std::uint8_t> verticalMask2;

        constexpr std::uint32_t kMinimumExpansionPercent = 5;

        const auto dilateTiles =
            [lowWidth, lowHeight, tilesX, tilesY, kTileWidth, kTileHeight](
                const std::vector<std::uint8_t>& input,
                const std::vector<std::uint32_t>& activity,
                std::vector<std::uint8_t>& output,
                int expandX,
                int expandY)
            {
                // The tile itself remains active. It only propagates glow to
                // neighbouring tiles while at least 5% of that tile still
                // contains non-zero glow data from the preceding pass.
                output = input;

                for (int tileY = 0; tileY < tilesY; ++tileY)
                {
                    const int pixelFirstY = tileY * kTileHeight;
                    const int pixelLastY =
                        std::min(pixelFirstY + kTileHeight, lowHeight);

                    for (int tileX = 0; tileX < tilesX; ++tileX)
                    {
                        const std::size_t index =
                            static_cast<std::size_t>(tileY) *
                            static_cast<std::size_t>(tilesX) +
                            static_cast<std::size_t>(tileX);

                        if (input[index] == 0)
                        {
                            continue;
                        }

                        const int pixelFirstX = tileX * kTileWidth;
                        const int pixelLastX =
                            std::min(pixelFirstX + kTileWidth, lowWidth);
                        const std::uint32_t tilePixels =
                            static_cast<std::uint32_t>(
                                (pixelLastX - pixelFirstX) *
                                (pixelLastY - pixelFirstY));

                        if (activity[index] * 100u <
                            tilePixels * kMinimumExpansionPercent)
                        {
                            continue;
                        }

                        const int firstY = std::max(0, tileY - expandY);
                        const int lastY = std::min(tilesY - 1, tileY + expandY);
                        const int firstX = std::max(0, tileX - expandX);
                        const int lastX = std::min(tilesX - 1, tileX + expandX);

                        for (int y = firstY; y <= lastY; ++y)
                        {
                            for (int x = firstX; x <= lastX; ++x)
                            {
                                output[
                                    static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(tilesX) +
                                    static_cast<std::size_t>(x)] = 1;
                            }
                        }
                    }
                }
            };

        const auto countActiveTiles =
            [](const std::vector<std::uint8_t>& mask)
            {
                return static_cast<std::uint32_t>(
                    std::count_if(
                        mask.begin(),
                        mask.end(),
                        [](std::uint8_t value)
                        {
                            return value != 0;
                        }));
            };

        const auto blurHorizontal =
            [lowWidth, lowHeight, radius, tilesX, kTileWidth, kTileHeight](
                const std::vector<std::uint16_t>& in,
                std::vector<std::uint16_t>& out,
                const std::vector<std::uint8_t>& mask,
                std::vector<std::uint32_t>& activity)
            {
                std::fill(out.begin(), out.end(), static_cast<std::uint16_t>(0));
                activity.assign(mask.size(), 0u);
                const std::uint32_t count =
                    static_cast<std::uint32_t>(2 * radius + 1);

                for (int tileY = 0; tileY < (lowHeight + kTileHeight - 1) / kTileHeight; ++tileY)
                {
                    const int firstY = tileY * kTileHeight;
                    const int lastY = std::min(firstY + kTileHeight, lowHeight);

                    for (int tileX = 0; tileX < tilesX; ++tileX)
                    {
                        const std::size_t tileIndex =
                            static_cast<std::size_t>(tileY) *
                            static_cast<std::size_t>(tilesX) +
                            static_cast<std::size_t>(tileX);

                        if (mask[tileIndex] == 0)
                        {
                            continue;
                        }

                        const int firstX = tileX * kTileWidth;
                        const int lastX = std::min(firstX + kTileWidth, lowWidth);

                        for (int y = firstY; y < lastY; ++y)
                        {
                            const std::size_t row =
                                static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(lowWidth);
                            std::uint32_t sum = 0;

                            for (int x = firstX - radius;
                                x <= firstX + radius;
                                ++x)
                            {
                                const int sx = std::clamp(x, 0, lowWidth - 1);
                                sum += in[row + static_cast<std::size_t>(sx)];
                            }

                            for (int x = firstX; x < lastX; ++x)
                            {
                                const std::uint16_t value =
                                    static_cast<std::uint16_t>(sum / count);
                                out[row + static_cast<std::size_t>(x)] = value;
                                if (value != 0)
                                {
                                    ++activity[tileIndex];
                                }

                                const int removeX =
                                    std::clamp(x - radius, 0, lowWidth - 1);
                                const int addX =
                                    std::clamp(x + radius + 1, 0, lowWidth - 1);
                                sum -= in[row + static_cast<std::size_t>(removeX)];
                                sum += in[row + static_cast<std::size_t>(addX)];
                            }
                        }
                    }
                }
            };

        const auto blurVertical =
            [lowWidth, lowHeight, radius, tilesX, kTileWidth, kTileHeight](
                const std::vector<std::uint16_t>& in,
                std::vector<std::uint16_t>& out,
                const std::vector<std::uint8_t>& mask,
                std::vector<std::uint32_t>& activity)
            {
                std::fill(out.begin(), out.end(), static_cast<std::uint16_t>(0));
                activity.assign(mask.size(), 0u);
                const std::uint32_t count =
                    static_cast<std::uint32_t>(2 * radius + 1);
                const int tilesY =
                    (lowHeight + kTileHeight - 1) / kTileHeight;

                for (int tileY = 0; tileY < tilesY; ++tileY)
                {
                    const int firstY = tileY * kTileHeight;
                    const int lastY = std::min(firstY + kTileHeight, lowHeight);

                    for (int tileX = 0; tileX < tilesX; ++tileX)
                    {
                        const std::size_t tileIndex =
                            static_cast<std::size_t>(tileY) *
                            static_cast<std::size_t>(tilesX) +
                            static_cast<std::size_t>(tileX);

                        if (mask[tileIndex] == 0)
                        {
                            continue;
                        }

                        const int firstX = tileX * kTileWidth;
                        const int lastX = std::min(firstX + kTileWidth, lowWidth);

                        for (int x = firstX; x < lastX; ++x)
                        {
                            std::uint32_t sum = 0;

                            for (int y = firstY - radius;
                                y <= firstY + radius;
                                ++y)
                            {
                                const int sy = std::clamp(y, 0, lowHeight - 1);
                                sum += in[
                                    static_cast<std::size_t>(sy) *
                                    static_cast<std::size_t>(lowWidth) +
                                    static_cast<std::size_t>(x)];
                            }

                            for (int y = firstY; y < lastY; ++y)
                            {
                                const std::size_t index =
                                    static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(lowWidth) +
                                    static_cast<std::size_t>(x);
                                const std::uint16_t value =
                                    static_cast<std::uint16_t>(sum / count);
                                out[index] = value;
                                if (value != 0)
                                {
                                    ++activity[tileIndex];
                                }

                                const int removeY =
                                    std::clamp(y - radius, 0, lowHeight - 1);
                                const int addY =
                                    std::clamp(y + radius + 1, 0, lowHeight - 1);
                                sum -= in[
                                    static_cast<std::size_t>(removeY) *
                                    static_cast<std::size_t>(lowWidth) +
                                    static_cast<std::size_t>(x)];
                                sum += in[
                                    static_cast<std::size_t>(addY) *
                                    static_cast<std::size_t>(lowWidth) +
                                    static_cast<std::size_t>(x)];
                            }
                        }
                    }
                }
            };

        dilateTiles(
            dirtyTiles,
            sourceActivity,
            horizontalMask1,
            horizontalTileExpansion,
            0);
        workload.horizontalPass1Tiles = countActiveTiles(horizontalMask1);
        blurHorizontal(source, temp, horizontalMask1, horizontal1Activity);

        dilateTiles(
            horizontalMask1,
            horizontal1Activity,
            verticalMask1,
            0,
            verticalTileExpansion);
        workload.verticalPass1Tiles = countActiveTiles(verticalMask1);
        blurVertical(temp, blur, verticalMask1, vertical1Activity);

        dilateTiles(
            verticalMask1,
            vertical1Activity,
            horizontalMask2,
            horizontalTileExpansion,
            0);
        workload.horizontalPass2Tiles = countActiveTiles(horizontalMask2);
        blurHorizontal(blur, temp, horizontalMask2, horizontal2Activity);

        dilateTiles(
            horizontalMask2,
            horizontal2Activity,
            verticalMask2,
            0,
            verticalTileExpansion);
        workload.verticalPass2Tiles = countActiveTiles(verticalMask2);
        blurVertical(temp, blur, verticalMask2, vertical2Activity);

        const int glowScale = std::clamp(glow, 0, 100);

        // Composite only the tiles kept by the final thresholded blur mask.
        for (int tileY = 0; tileY < tilesY; ++tileY)
        {
            for (int tileX = 0; tileX < tilesX; ++tileX)
            {
                const std::size_t tileIndex =
                    static_cast<std::size_t>(tileY) *
                    static_cast<std::size_t>(tilesX) +
                    static_cast<std::size_t>(tileX);

                if (verticalMask2[tileIndex] == 0)
                {
                    continue;
                }

                const int firstX = tileX * kTileWidth * 2;
                const int lastX =
                    std::min((tileX + 1) * kTileWidth * 2, width);
                const int firstY = tileY * kTileHeight * 2;
                const int lastY =
                    std::min((tileY + 1) * kTileHeight * 2, height);

                for (int y = firstY; y < lastY; ++y)
                {
                    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                    const int ly = std::min(y / 2, lowHeight - 1);

                    for (int x = firstX; x < lastX; ++x)
                    {
                        const int lx = std::min(x / 2, lowWidth - 1);
                        const int blurred =
                            blur[static_cast<std::size_t>(ly) * lowWidth + lx];
                        const int contribution =
                            (blurred * glowScale * 3) / 400;

                        if (contribution <= 0)
                        {
                            continue;
                        }

                        const QRgb pixel = line[x];
                        line[x] = qRgb(
                            std::min(255, qRed(pixel) + contribution),
                            std::min(255, qGreen(pixel) + contribution),
                            std::min(255, qBlue(pixel) + contribution));
                    }
                }
            }
        }

        return workload;
    }

    constexpr double kMaximumSampleValue = 65535.0;
    constexpr double kChromaNoiseThresholdFraction =
        0.03;
    constexpr int kLuminanceBeamIntensity = 256;
    constexpr int kChromaBeamIntensity = 768;
    constexpr std::uint32_t kConnectorIntensity = 160u;

    // Keep accumulated chroma energy below full scale.
    // This prevents high Scopephor settings from driving
    // the RGB chroma channels into long-term saturation.
    constexpr std::uint32_t kMaximumChromaLevel = 16384u;
}

void WaveformRenderer::setChromaFillIntensity(
    int intensity)
{
    chromaFillIntensity_ =
        std::clamp(
            intensity,
            0,
            200);
}

void WaveformRenderer::setColor(bool enabled)
{
    settings_.color = enabled;
}

void WaveformRenderer::setMeasurementProbePresentation(
    bool enabled,
    double normalizedX,
    double volts)
{
    measurementProbeNormalizedX_.store(
        std::clamp(normalizedX, 0.0, 1.0),
        std::memory_order_relaxed);

    measurementProbeVolts_.store(
        volts,
        std::memory_order_relaxed);

    measurementProbePresentation_.store(
        enabled,
        std::memory_order_relaxed);
}

void WaveformRenderer::setTraceJobExecutor(
    TraceJobExecutor executor)
{
    traceJobExecutor_ = std::move(executor);
}

void WaveformRenderer::setLineInfoOverlayEnabled(
    bool enabled,
    bool palOutput)
{
    lineInfoOverlayEnabled_ = enabled;
    lineInfoOverlayPalOutput_ = palOutput;
}

void addFillPixel(
    QImage& image,
    int x,
    int y,
    int red,
    int green,
    int blue,
    int intensity)
{
    if (x < 0 ||
        x >= image.width() ||
        y < 0 ||
        y >= image.height())
    {
        return;
    }

    QRgb* scanLine =
        reinterpret_cast<QRgb*>(
            image.scanLine(y));

    const QRgb current =
        scanLine[x];

    const int newRed =
        std::min(
            255,
            qRed(current) +
            red * intensity / 255);

    const int newGreen =
        std::min(
            255,
            qGreen(current) +
            green * intensity / 255);

    const int newBlue =
        std::min(
            255,
            qBlue(current) +
            blue * intensity / 255);

    scanLine[x] =
        qRgb(
            newRed,
            newGreen,
            newBlue);
}

void resampleLinear(
    std::span<const float> input,
    std::span<float> output)
{
    if (output.empty())
    {
        return;
    }

    if (input.empty())
    {
        std::fill(
            output.begin(),
            output.end(),
            0.0f);

        return;
    }

    if (input.size() == 1)
    {
        std::fill(
            output.begin(),
            output.end(),
            input.front());

        return;
    }

    const float scale =
        static_cast<float>(input.size()) /
        static_cast<float>(output.size());

    for (std::size_t outputIndex = 0;
        outputIndex < output.size();
        ++outputIndex)
    {
        const float sourcePosition =
            (static_cast<float>(outputIndex) + 0.5f) *
            scale -
            0.5f;

        const int leftIndex =
            std::clamp(
                static_cast<int>(
                    std::floor(sourcePosition)),
                0,
                static_cast<int>(
                    input.size()) - 1);

        const int rightIndex =
            std::min(
                leftIndex + 1,
                static_cast<int>(
                    input.size()) - 1);

        const float fraction =
            std::clamp(
                sourcePosition -
                static_cast<float>(leftIndex),
                0.0f,
                1.0f);

        const float left =
            input[
                static_cast<std::size_t>(
                    leftIndex)];

        const float right =
            input[
                static_cast<std::size_t>(
                    rightIndex)];

        output[outputIndex] =
            left +
            (right - left) *
            fraction;
    }
}
void resampleMinMax(
    std::span<const float> input,
    std::span<float> outputMin,
    std::span<float> outputMax)
{
    if (input.empty() ||
        outputMin.empty() ||
        outputMax.empty())
    {
        return;
    }

    const std::size_t outputSize =
        std::min(
            outputMin.size(),
            outputMax.size());

    for (std::size_t x = 0;
        x < outputSize;
        ++x)
    {
        const std::size_t first =
            x * input.size() /
            outputSize;

        const std::size_t last =
            std::max(
                first + 1u,
                (x + 1u) * input.size() /
                outputSize);

        float minimum = input[first];
        float maximum = input[first];

        for (std::size_t sourceIndex = first + 1u;
            sourceIndex < last &&
            sourceIndex < input.size();
            ++sourceIndex)
        {
            minimum =
                std::min(
                    minimum,
                    input[sourceIndex]);

            maximum =
                std::max(
                    maximum,
                    input[sourceIndex]);
        }

        outputMin[x] = minimum;
        outputMax[x] = maximum;
    }
}
WaveformRenderer::WaveformRenderer()
    : image_(
        VideoStandard::pal625().outputWidth,
        VideoStandard::pal625().outputHeight,
        QImage::Format_RGB32)
    , hits_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth) *
        static_cast<std::size_t>(
            VideoStandard::pal625().outputHeight),
        0u)
    , allLinesPersistence_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth) *
        static_cast<std::size_t>(
            VideoStandard::pal625().outputHeight),
        0.0f)
    , trace_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth) *
        static_cast<std::size_t>(
            VideoStandard::pal625().outputHeight))
    , chromaTrace_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth) *
        static_cast<std::size_t>(
            VideoStandard::pal625().outputHeight))
    , displayY_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth))
    , displayYMin_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth))
    , displayYMax_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth))
    , displayU_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth))
    , displayV_(
        static_cast<std::size_t>(
            VideoStandard::pal625().outputWidth))
{
    for (std::size_t i = 0;
        i < displayLut_.size();
        ++i)
    {
        displayLut_[i] =
            static_cast<std::uint8_t>(
                std::min<std::size_t>(
                    i,
                    255u));
    }

    image_.fill(Qt::black);
}

void WaveformRenderer::setOutputSize(
    int width,
    int height)
{
    width =
        std::max(
            width,
            1);

    height =
        std::max(
            height,
            1);

    if (image_.width() == width &&
        image_.height() == height)
    {
        return;
    }

    const std::size_t requestedPixelCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    outputSizeChangedSinceRender_ = true;
    outputBufferCapacityGrewSinceRender_ =
        outputBufferCapacityGrewSinceRender_ ||
        trace_.capacity() < requestedPixelCount ||
        chromaTrace_.capacity() < requestedPixelCount ||
        hits_.capacity() < requestedPixelCount ||
        allLinesPersistence_.capacity() < requestedPixelCount;

    image_ =
        QImage(
            width,
            height,
            QImage::Format_RGB32);

    image_.fill(Qt::black);

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    hits_.assign(
        pixelCount,
        0u);

    allLinesPersistence_.assign(
        pixelCount,
        0.0f);

    trace_.assign(
        pixelCount,
        TracePixel{});

    chromaTrace_.assign(
        pixelCount,
        TracePixel{});

    displayY_.resize(
        static_cast<std::size_t>(
            width));

    displayYMin_.resize(
        static_cast<std::size_t>(
            width));

    displayYMax_.resize(
        static_cast<std::size_t>(
            width));

    displayU_.resize(
        static_cast<std::size_t>(
            width));

    displayV_.resize(
        static_cast<std::size_t>(
            width));

    // Scopephor stores already-rendered images at the target resolution.
    // A target-size change therefore invalidates the complete phosphor stack.
    clearScopephorFrames();
}

void WaveformRenderer::setZoomed(
    bool zoomed)
{
    setZoomFactor(
        zoomed
        ? 10
        : 1);
}

void WaveformRenderer::setZoomFactor(
    int factor)
{
    if (factor != 1 &&
        factor != 5 &&
        factor != 10)
    {
        factor = 1;
    }

    if (zoomFactor_ == factor)
    {
        return;
    }

    qDebug()
        << "WaveformRenderer zoom factor:"
        << factor;

    zoomFactor_ = factor;
    clearTrace();
}


void WaveformRenderer::setContentScale(
    double scale)
{
    setContentScale(
        scale,
        scale);
}


void WaveformRenderer::setContentScale(
    double horizontalScale,
    double verticalScale)
{
    const double newHorizontalScale =
        std::clamp(
            horizontalScale,
            0.1,
            1.0);

    const double newVerticalScale =
        std::clamp(
            verticalScale,
            0.1,
            1.0);

    if (newHorizontalScale == contentScaleX_ &&
        newVerticalScale == contentScaleY_)
    {
        return;
    }

    contentScaleX_ = newHorizontalScale;
    contentScaleY_ = newVerticalScale;
    clearTrace();
}

void WaveformRenderer::setFitAspectRatio(bool enabled)
{
    fitAspectRatio_ = enabled;
}


void WaveformRenderer::setScrollPosition(
    double position)
{
    const double newPosition =
        std::clamp(
            position,
            0.0,
            1.0);

    if (newPosition == scrollPosition_)
    {
        return;
    }

    scrollPosition_ = newPosition;
}

void WaveformRenderer::analyze(
    const Yuv444Frame& frame)
{
    inputSampleClockHz_ =
        frame.sampleClockHz > 0.0
        ? frame.sampleClockHz
        : 13'500'000.0;

    inputSampleWidth_ =
        std::max(frame.width, 1);

    const std::size_t requiredSamples =
        frame.width > 0 &&
        frame.height > 0
        ? static_cast<std::size_t>(
            frame.width) *
        static_cast<std::size_t>(
            frame.height)
        : 0u;

    if (requiredSamples == 0u ||
        frame.y.size() < requiredSamples ||
        frame.u.size() < requiredSamples ||
        frame.v.size() < requiredSamples)
    {
        renderTimings_ = {};
        image_.fill(Qt::black);
        return;
    }

    if (selectedLine_ >= 0 &&
        selectedLine_ < frame.height)
    {
        renderSingleLine(
            frame);

        return;
    }

    renderAllLines(frame);
}


void WaveformRenderer::clearOrFadeTrace()
{
    if (persistence_ == 0)
    {
        std::fill(
            trace_.begin(),
            trace_.end(),
            TracePixel{});

        std::fill(
            chromaTrace_.begin(),
            chromaTrace_.end(),
            TracePixel{});

        return;
    }

    const std::uint32_t lumaPersistence =
        static_cast<std::uint32_t>(
            persistence_);

    constexpr std::uint32_t kMaximumChromaPersistence =
        204u;

    const std::uint32_t chromaPersistence =
        std::min(
            lumaPersistence,
            kMaximumChromaPersistence);

    const auto fadeTrace =
        [](std::vector<TracePixel>& pixels,
            std::uint32_t persistence)
        {
            for (TracePixel& pixel : pixels)
            {
                pixel.red =
                    static_cast<std::uint16_t>(
                        (static_cast<std::uint32_t>(
                            pixel.red) *
                            persistence) >>
                        8);

                pixel.green =
                    static_cast<std::uint16_t>(
                        (static_cast<std::uint32_t>(
                            pixel.green) *
                            persistence) >>
                        8);

                pixel.blue =
                    static_cast<std::uint16_t>(
                        (static_cast<std::uint32_t>(
                            pixel.blue) *
                            persistence) >>
                        8);
            }
        };

    fadeTrace(
        trace_,
        lumaPersistence);

    fadeTrace(
        chromaTrace_,
        chromaPersistence);
}

WaveformGraticuleLayout WaveformRenderer::graticuleLayout() const
{
    const QRectF canvasRect(
        0.0,
        0.0,
        static_cast<double>(
            std::max(1, image_.width() - 1)),
        static_cast<double>(
            std::max(1, image_.height() - 1)));

    return graticule_.layout(
        canvasRect,
        QApplication::font(),
        &image_,
        OpenScopeSettings::aspectRatioValue(
            aspectRatio_),
        fitAspectRatio_,
        contentScaleX_,
        contentScaleY_);
}

QRectF WaveformRenderer::viewportRect() const
{
    return graticuleLayout().viewportRect;
}

QRectF WaveformRenderer::scaledScopeRect() const
{
    return graticuleLayout().plotRect;
}

void WaveformRenderer::renderSingleLine(
    const Yuv444Frame& frame)
{
    renderTimings_ = {};
    renderTimings_.outputSizeChanged =
        outputSizeChangedSinceRender_;
    renderTimings_.outputBufferCapacityGrew =
        outputBufferCapacityGrewSinceRender_;
    renderTimings_.beamCoreRadiusPx =
        beamCoreRadiusPx_;
    renderTimings_.beamCoreMarginPx =
        std::max(
            1,
            static_cast<int>(
                std::floor(beamCoreRadiusPx_)));

    outputSizeChangedSinceRender_ = false;
    outputBufferCapacityGrewSinceRender_ = false;

    QElapsedTimer phaseTimer;
    phaseTimer.start();

    const std::size_t sourceWidth =
        static_cast<std::size_t>(
            frame.width);

    const std::size_t reconstructedWidth =
        sourceWidth * 4u;

    const std::size_t sourceLineOffset =
        static_cast<std::size_t>(
            selectedLine_) *
        sourceWidth;

    singleLineSource_.resize(sourceWidth);
    singleLineReconstructed_.resize(reconstructedWidth);

    for (std::size_t x = 0;
        x < sourceWidth;
        ++x)
    {
        singleLineSource_[x] =
            static_cast<float>(
                frame.y[sourceLineOffset + x]);
    }

    const std::uint64_t resamplerGenerationBefore =
        singleLineReconstructor_.cacheGeneration();

    singleLineReconstructor_.resample(
        singleLineSource_,
        singleLineReconstructed_);

    renderTimings_.resamplerCacheRebuilt =
        singleLineReconstructor_.cacheGeneration() !=
        resamplerGenerationBefore;

    fullLumaVolts_.resize(reconstructedWidth);

    const auto digitalLevels =
        levels(VideoStandard::pal625());

    const auto analog =
        analogLevels(
            VideoColorStandard::Rec601_625);

    const double voltsPerCode =
        (analog.whiteVolts - analog.blackVolts) /
        static_cast<double>(
            digitalLevels.yWhite -
            digitalLevels.yBlack);

    for (std::size_t x = 0;
        x < reconstructedWidth;
        ++x)
    {
        const double y10 =
            static_cast<double>(
                singleLineReconstructed_[x]) /
            64.0;

        fullLumaVolts_[x] =
            static_cast<float>(
                analog.blackVolts +
                (y10 - digitalLevels.yBlack) *
                voltsPerCode);
    }

    std::size_t reconstructedViewWidth =
        reconstructedWidth;
    std::size_t reconstructedViewOffset = 0u;

    if (zoomFactor_ > 1)
    {
        reconstructedViewWidth =
            std::max<std::size_t>(
                reconstructedWidth /
                    static_cast<std::size_t>(zoomFactor_),
                1u);

        const std::size_t maximumOffset =
            reconstructedWidth -
            reconstructedViewWidth;

        reconstructedViewOffset =
            static_cast<std::size_t>(
                scrollPosition_ *
                static_cast<double>(maximumOffset));
    }

    sourceY_.resize(reconstructedViewWidth);
    for (std::size_t x = 0;
        x < reconstructedViewWidth;
        ++x)
    {
        sourceY_[x] =
            fullLumaVolts_[
                reconstructedViewOffset + x];
    }

    // Chroma remains a current-frame envelope.  Scopephor history is the
    // reconstructed 2880-sample luminance beam only.
    std::size_t sourceViewWidth = sourceWidth;
    std::size_t sourceViewOffset = 0u;

    if (zoomFactor_ > 1)
    {
        sourceViewWidth =
            std::max<std::size_t>(
                sourceWidth /
                    static_cast<std::size_t>(zoomFactor_),
                1u);

        const std::size_t maximumSourceOffset =
            sourceWidth - sourceViewWidth;

        const double normalisedPosition =
            reconstructedWidth > reconstructedViewWidth
            ? static_cast<double>(reconstructedViewOffset) /
                static_cast<double>(
                    reconstructedWidth - reconstructedViewWidth)
            : 0.0;

        sourceViewOffset =
            static_cast<std::size_t>(
                normalisedPosition *
                static_cast<double>(maximumSourceOffset));
    }

    sourceU_.resize(sourceViewWidth);
    sourceV_.resize(sourceViewWidth);

    for (std::size_t x = 0;
        x < sourceViewWidth;
        ++x)
    {
        const std::size_t sourceX =
            std::min(
                sourceViewOffset + x,
                sourceWidth - 1u);

        const std::size_t sampleIndex =
            sourceLineOffset + sourceX;

        sourceU_[x] =
            static_cast<float>(frame.u[sampleIndex]);
        sourceV_[x] =
            static_cast<float>(frame.v[sampleIndex]);
    }

    displayY_.resize(
        static_cast<std::size_t>(image_.width()));
    displayU_.resize(
        static_cast<std::size_t>(image_.width()));
    displayV_.resize(
        static_cast<std::size_t>(image_.width()));

    resampleLinear(sourceY_, displayY_);
    resampleLinear(sourceU_, displayU_);
    resampleLinear(sourceV_, displayV_);

    const QRectF scope = scaledScopeRect();

    // Beam geometry must scale from the actual graticule plot area, not
    // from the outer transport canvas.  This keeps PC and PAL/Spout targets
    // physically consistent even when labels, aspect fitting or underscan
    // reduce the usable plot raster.
    const double plotRenderDimension =
        std::max(
            1.0,
            std::min(
                scope.width(),
                scope.height()));

    const double plotBeamScale =
        std::clamp(
            std::sqrt(
                plotRenderDimension / 576.0),
            1.0,
            1.90);

    constexpr double kReferenceCoreRadiusPx = 0.82;
    beamCoreRadiusPx_ =
        kReferenceCoreRadiusPx * plotBeamScale;

    renderTimings_.beamCoreRadiusPx =
        beamCoreRadiusPx_;
    renderTimings_.beamCoreMarginPx =
        std::max(
            1,
            static_cast<int>(
                std::floor(beamCoreRadiusPx_)));

    // -----------------------------------------------------------------
    // New Scopephor model:
    //
    //   current 2880-sample Y line
    //       -> one Catmull-Rom path in THIS target resolution
    //       -> render core + glow ONCE
    //       -> store the rendered target-resolution phosphor image
    //
    // Old phosphor memories are never splined or glowed again.
    // Display persistence is only a weighted sum of those stored images.
    // -----------------------------------------------------------------

    phaseTimer.restart();

    const std::vector<BeamPoint> currentLumaPolyline =
        buildCurrentLumaPolyline(
            scope,
            reconstructedViewOffset,
            reconstructedViewWidth);

    renderTimings_.tracePrepUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    std::uint64_t currentGlowUs = 0u;
    std::uint64_t currentCoreUs = 0u;

    int phosphorMinX = image_.width();
    int phosphorMinY = image_.height();
    int phosphorMaxX = -1;
    int phosphorMaxY = -1;

    std::vector<std::uint16_t> currentPhosphorEnergy =
        renderCurrentPhosphorEnergy(
            currentLumaPolyline,
            scope,
            currentGlowUs,
            currentCoreUs,
            phosphorMinX,
            phosphorMinY,
            phosphorMaxX,
            phosphorMaxY);

    /*
     * PRE-SCOPEPHOR AA RESOLVE
     * ------------------------
     *
     * The analytic 2x2 core is substantially cheaper than the 4x4 quality
     * experiment, but still leaves a small pixel-phase modulation which
     * Scopephor can otherwise preserve and make more visible.
     *
     * Resolve only the active beam bbox with a tiny separable 3-tap filter:
     *
     *     0.15   0.70   0.15
     *
     * Horizontal + vertical passes give a smooth subpixel filament before
     * temporal feedback, without touching the full framebuffer.  The kernel
     * sums to one, so beam energy is redistributed rather than amplified.
     */
    if (phosphorMaxX >= phosphorMinX &&
        phosphorMaxY >= phosphorMinY &&
        !currentPhosphorEnergy.empty())
    {
        const int resolveWidth = image_.width();
        const int resolveHeight = image_.height();

        const int resolveMinX =
            std::clamp(
                phosphorMinX - 1,
                0,
                resolveWidth - 1);

        const int resolveMaxX =
            std::clamp(
                phosphorMaxX + 1,
                0,
                resolveWidth - 1);

        const int resolveMinY =
            std::clamp(
                phosphorMinY - 1,
                0,
                resolveHeight - 1);

        const int resolveMaxY =
            std::clamp(
                phosphorMaxY + 1,
                0,
                resolveHeight - 1);

        const int resolveSpan =
            resolveMaxX -
            resolveMinX + 1;

        std::vector<std::uint16_t> resolveHorizontal(
            static_cast<std::size_t>(resolveSpan) *
                static_cast<std::size_t>(
                    resolveMaxY - resolveMinY + 1),
            0u);

        constexpr std::uint32_t kSideQ8 = 38u;   // 0.1484375
        constexpr std::uint32_t kCenterQ8 = 180u; // 0.703125

        for (int y = resolveMinY;
            y <= resolveMaxY;
            ++y)
        {
            const std::size_t sourceRow =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(resolveWidth);

            const std::size_t targetRow =
                static_cast<std::size_t>(y - resolveMinY) *
                static_cast<std::size_t>(resolveSpan);

            for (int x = resolveMinX;
                x <= resolveMaxX;
                ++x)
            {
                const int leftX =
                    std::max(
                        resolveMinX,
                        x - 1);

                const int rightX =
                    std::min(
                        resolveMaxX,
                        x + 1);

                const std::uint32_t left =
                    currentPhosphorEnergy[
                        sourceRow +
                        static_cast<std::size_t>(leftX)];

                const std::uint32_t center =
                    currentPhosphorEnergy[
                        sourceRow +
                        static_cast<std::size_t>(x)];

                const std::uint32_t right =
                    currentPhosphorEnergy[
                        sourceRow +
                        static_cast<std::size_t>(rightX)];

                resolveHorizontal[
                    targetRow +
                    static_cast<std::size_t>(x - resolveMinX)] =
                    static_cast<std::uint16_t>(
                        (left * kSideQ8 +
                         center * kCenterQ8 +
                         right * kSideQ8 +
                         128u) >> 8);
            }
        }

        for (int y = resolveMinY;
            y <= resolveMaxY;
            ++y)
        {
            const int topY =
                std::max(
                    resolveMinY,
                    y - 1);

            const int bottomY =
                std::min(
                    resolveMaxY,
                    y + 1);

            const std::size_t topRow =
                static_cast<std::size_t>(topY - resolveMinY) *
                static_cast<std::size_t>(resolveSpan);

            const std::size_t centerRow =
                static_cast<std::size_t>(y - resolveMinY) *
                static_cast<std::size_t>(resolveSpan);

            const std::size_t bottomRow =
                static_cast<std::size_t>(bottomY - resolveMinY) *
                static_cast<std::size_t>(resolveSpan);

            const std::size_t destinationRow =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(resolveWidth);

            for (int x = resolveMinX;
                x <= resolveMaxX;
                ++x)
            {
                const std::size_t localX =
                    static_cast<std::size_t>(
                        x - resolveMinX);

                const std::uint32_t top =
                    resolveHorizontal[
                        topRow + localX];

                const std::uint32_t center =
                    resolveHorizontal[
                        centerRow + localX];

                const std::uint32_t bottom =
                    resolveHorizontal[
                        bottomRow + localX];

                currentPhosphorEnergy[
                    destinationRow +
                    static_cast<std::size_t>(x)] =
                    static_cast<std::uint16_t>(
                        (top * kSideQ8 +
                         center * kCenterQ8 +
                         bottom * kSideQ8 +
                         128u) >> 8);
            }
        }

        phosphorMinX = resolveMinX;
        phosphorMaxX = resolveMaxX;
        phosphorMinY = resolveMinY;
        phosphorMaxY = resolveMaxY;
    }

    renderTimings_.glowUs =
        currentGlowUs;

    renderTimings_.traceRasterUs =
        currentCoreUs;

    phaseTimer.restart();

    applyScopephorFeedback(
        currentPhosphorEnergy,
        phosphorMinX,
        phosphorMinY,
        phosphorMaxX,
        phosphorMaxY);

    // image_ is only the final Qt transport surface. Phosphor math is raw.
    image_.fill(Qt::black);

    // Draw the graticule first. Signal traces are composed afterwards so
    // the beam remains visually in front of the graticule.
    {
        QPainter graticulePainter(&image_);
        graticule_.draw(
            graticulePainter,
            scope,
            VideoStandard::pal625());
    }

    renderTimings_.persistenceUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    if (chromaFillIntensity_ > 0 &&
        !measurementProbePresentation_)
    {
        /*
         * Chroma is deliberately NOT part of Scopephor.
         *
         * Restore the pre-Scopephor current-frame chroma renderer verbatim in
         * spirit:
         *   - current U/V only
         *   - old envelope geometry
         *   - old RGB derivation
         *   - direct vertical RGB spans using the same display LUT
         *
         * Chroma is written directly into the current target. It therefore
         * has no persistence, no age weighting and no glow.
         */
        phaseTimer.restart();

        const int displayWidth =
            image_.width();

        std::vector<double> chromaUpperY(
            static_cast<std::size_t>(displayWidth));

        std::vector<double> chromaLowerY(
            static_cast<std::size_t>(displayWidth));

        std::vector<int> chromaRed(
            static_cast<std::size_t>(displayWidth));

        std::vector<int> chromaGreen(
            static_cast<std::size_t>(displayWidth));

        std::vector<int> chromaBlue(
            static_cast<std::size_t>(displayWidth));

        const double voltsToPixels =
            scope.height() /
            analog.graticuleMaxVolts;

        const double chromaXScale =
            scope.width() /
            static_cast<double>(
                std::max(1, displayWidth - 1));

        for (int x = 0;
            x < displayWidth;
            ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(x);

            const double yValue =
                std::clamp(
                    static_cast<double>(
                        displayY_[index]),
                    0.0,
                    kMaximumSampleValue);

            const double uValue =
                std::clamp(
                    static_cast<double>(
                        displayU_[index]),
                    0.0,
                    kMaximumSampleValue);

            const double vValue =
                std::clamp(
                    static_cast<double>(
                        displayV_[index]),
                    0.0,
                    kMaximumSampleValue);

            const double neutralChroma =
                static_cast<double>(
                    digitalLevels.chromaNeutral) *
                64.0;

            const double chromaNoiseThreshold =
                neutralChroma *
                kChromaNoiseThresholdFraction;

            double chromaU =
                uValue -
                neutralChroma;

            double chromaV =
                vValue -
                neutralChroma;

            const auto palChroma =
                palChromaCoefficients(
                    VideoColorStandard::Rec601_625);

            const double palU =
                chromaU *
                palChroma.cbToU;

            const double palV =
                chromaV *
                palChroma.crToV;

            const double chromaMagnitude =
                std::hypot(
                    palU,
                    palV);

            if (chromaMagnitude <
                chromaNoiseThreshold)
            {
                chromaU = 0.0;
                chromaV = 0.0;
            }

            const double plotY =
                scope.bottom() -
                yValue *
                voltsToPixels;

            const double nominalChromaExcursion =
                static_cast<double>(
                    digitalLevels.chromaPositiveExcursion) *
                64.0;

            const double chromaVolts =
                std::hypot(
                    palU,
                    palV) *
                analog.chromaPeakVolts /
                nominalChromaExcursion;

            const double spread =
                chromaVolts *
                voltsToPixels;

            chromaUpperY[index] =
                plotY - spread;

            chromaLowerY[index] =
                plotY + spread;

            const auto rgb =
                yuvToRgbCoefficients(
                    VideoColorStandard::Rec601_625);

            double redValue =
                rgb.rCr * chromaV;

            double greenValue =
                rgb.gCb * chromaU +
                rgb.gCr * chromaV;

            double blueValue =
                rgb.bCb * chromaU;

            const double minimum =
                std::min({
                    redValue,
                    greenValue,
                    blueValue
                    });

            redValue -= minimum;
            greenValue -= minimum;
            blueValue -= minimum;

            const double maximum =
                std::max({
                    redValue,
                    greenValue,
                    blueValue
                    });

            int red = 0;
            int green = 0;
            int blue = 0;

            if (maximum > 0.0)
            {
                red =
                    static_cast<int>(
                        std::clamp(
                            redValue *
                            255.0 /
                            maximum,
                            0.0,
                            255.0));

                green =
                    static_cast<int>(
                        std::clamp(
                            greenValue *
                            255.0 /
                            maximum,
                            0.0,
                            255.0));

                blue =
                    static_cast<int>(
                        std::clamp(
                            blueValue *
                            255.0 /
                            maximum,
                            0.0,
                            255.0));
            }

            chromaRed[index] = red;
            chromaGreen[index] = green;
            chromaBlue[index] = blue;
        }

        const int firstScreenX =
            std::clamp(
                static_cast<int>(
                    std::ceil(scope.left())),
                0,
                image_.width());

        const int lastScreenXExclusive =
            std::clamp(
                static_cast<int>(
                    std::floor(scope.right())) + 1,
                firstScreenX,
                image_.width());

        for (int screenX = firstScreenX;
            screenX < lastScreenXExclusive;
            ++screenX)
        {
            const double normalisedX =
                (static_cast<double>(screenX) -
                    scope.left()) /
                scope.width();

            const std::size_t index =
                std::min(
                    static_cast<std::size_t>(
                        std::clamp(
                            normalisedX,
                            0.0,
                            1.0) *
                        static_cast<double>(
                            displayWidth - 1)),
                    static_cast<std::size_t>(
                        displayWidth - 1));

            const int firstY =
                std::clamp(
                    static_cast<int>(
                        std::ceil(
                            chromaUpperY[index])),
                    0,
                    image_.height() - 1);

            const int lastY =
                std::clamp(
                    static_cast<int>(
                        std::floor(
                            chromaLowerY[index])),
                    0,
                    image_.height() - 1);

            if (lastY < firstY)
            {
                continue;
            }

            const auto chromaLevel =
                [this](int channel) -> int
                {
                    const std::uint32_t contribution =
                        static_cast<std::uint32_t>(
                            std::clamp(
                                channel,
                                0,
                                255)) *
                        static_cast<std::uint32_t>(
                            chromaFillIntensity_) /
                        255u;

                    return displayLut_[
                        static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(
                                kMaximumChromaLevel,
                                contribution))];
                };

            const int redContribution =
                chromaLevel(
                    chromaRed[index]);

            const int greenContribution =
                chromaLevel(
                    chromaGreen[index]);

            const int blueContribution =
                chromaLevel(
                    chromaBlue[index]);

            const int monoContribution =
                std::max({
                    redContribution,
                    greenContribution,
                    blueContribution
                    });

            for (int y = firstY;
                y <= lastY;
                ++y)
            {
                auto* destination =
                    reinterpret_cast<QRgb*>(
                        image_.scanLine(y));

                if (settings_.color)
                {
                    destination[screenX] =
                        qRgb(
                            redContribution,
                            greenContribution,
                            blueContribution);
                }
                else
                {
                    destination[screenX] =
                        qRgb(
                            monoContribution,
                            monoContribution,
                            monoContribution);
                }
            }
        }

        // Chroma was written directly as vertical spans above.
        renderTimings_.composeUs =
            static_cast<std::uint64_t>(
                phaseTimer.nsecsElapsed() / 1000);
    }
    else
    {
        renderTimings_.composeUs = 0u;
    }

    // Raw phosphor energy -> QImage transport surface.
    // No QPainter and no Qt image blending.
    phaseTimer.restart();

    const int phosphorWidth =
        image_.width();

    const int phosphorHeight =
        image_.height();

    if (currentPhosphorEnergy.size() ==
            static_cast<std::size_t>(phosphorWidth) *
            static_cast<std::size_t>(phosphorHeight) &&
        phosphorMaxX >= phosphorMinX &&
        phosphorMaxY >= phosphorMinY)
    {
        phosphorMinX = std::clamp(phosphorMinX, 0, phosphorWidth - 1);
        phosphorMaxX = std::clamp(phosphorMaxX, 0, phosphorWidth - 1);
        phosphorMinY = std::clamp(phosphorMinY, 0, phosphorHeight - 1);
        phosphorMaxY = std::clamp(phosphorMaxY, 0, phosphorHeight - 1);

        const bool probePresentation =
            measurementProbePresentation_.load(
                std::memory_order_relaxed);

        double probeCenterX = 0.0;
        double probeCenterY = 0.0;
        double probeRadiusSquared = 0.0;

        if (probePresentation)
        {
            const AnalogVideoLevels probeAnalog =
                analogLevels(VideoColorStandard::Rec601_625);

            const double probeNormalizedX =
                measurementProbeNormalizedX_.load(
                    std::memory_order_relaxed);

            const double probeVolts =
                measurementProbeVolts_.load(
                    std::memory_order_relaxed);

            probeCenterX =
                scope.left() +
                probeNormalizedX * scope.width();

            probeCenterY =
                scope.bottom() -
                probeVolts * scope.height() /
                    probeAnalog.graticuleMaxVolts;

            const double videoHeight =
                std::abs(
                    (probeAnalog.whiteVolts - probeAnalog.blackVolts) *
                    scope.height() /
                    probeAnalog.graticuleMaxVolts);

            const double probeDiameter =
                (std::max)(12.0, videoHeight * 0.10);

            const double probeRadius =
                probeDiameter * 0.5;

            probeRadiusSquared =
                probeRadius * probeRadius;
        }

        for (int y = phosphorMinY;
            y <= phosphorMaxY;
            ++y)
        {
            auto* destination =
                reinterpret_cast<QRgb*>(
                    image_.scanLine(y));

            const std::size_t row =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(
                    phosphorWidth);

            for (int x = phosphorMinX;
                x <= phosphorMaxX;
                ++x)
            {
                const std::uint16_t energy =
                    currentPhosphorEnergy[
                        row +
                        static_cast<std::size_t>(x)];

                if (energy == 0u)
                {
                    continue;
                }

                // Q-less display gain.  The previous >>8 mapping made the
                // 16-bit beam energy almost black (255*7 -> only ~6).
                // >>3 restores a hot white core while preserving headroom
                // for glow and phosphor accumulation.
                const int tracePeakWhite =
                    lineInfoOverlayPalOutput_
                    ? 191
                    : 255;

                int value =
                    std::min(
                        tracePeakWhite,
                        static_cast<int>(
                            energy >> 3));

                if (probePresentation)
                {
                    const double dx =
                        (static_cast<double>(x) + 0.5) -
                        probeCenterX;

                    const double dy =
                        (static_cast<double>(y) + 0.5) -
                        probeCenterY;

                    const bool insideProbe =
                        dx * dx + dy * dy <=
                        probeRadiusSquared;

                    // Dim the context, but keep the trace inside the
                    // measurement circle at its normal calibrated strength.
                    if (!insideProbe)
                    {
                        value = (value + 1) / 2;
                    }
                }

                const QRgb old =
                    destination[x];

                destination[x] =
                    qRgb(
                        std::min(
                            255,
                            qRed(old) + value),
                        std::min(
                            255,
                            qGreen(old) + value),
                        std::min(
                            255,
                            qBlue(old) + value));
            }
        }
    }

    renderTimings_.persistenceUs +=
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);


    // Luma Scopephor was already composed before the current-frame chroma
    // overlay.  Do not re-render any historical beam geometry here.
    renderTimings_.traceUs =
        renderTimings_.tracePrepUs +
        renderTimings_.traceRasterUs;
    renderTimings_.traceParallel = false;
    renderTimings_.traceJobCount = 1u;

    phaseTimer.restart();
    QPainter overlayPainter(&image_);
    drawLineInfoOverlay(overlayPainter);
    renderTimings_.overlayUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);
}



std::vector<WaveformRenderer::BeamPoint> WaveformRenderer::buildCurrentLumaPolyline(
    const QRectF& scope,
    std::size_t viewOffset,
    std::size_t viewWidth) const
{
    std::vector<BeamPoint> polyline;

    if (viewWidth < 2u ||
        fullLumaVolts_.size() <
            viewOffset + viewWidth)
    {
        return polyline;
    }

    const auto analog =
        analogLevels(
            VideoColorStandard::Rec601_625);

    const double voltsToPixels =
        scope.height() /
        analog.graticuleMaxVolts;

    const double xScale =
        scope.width() /
        static_cast<double>(
            viewWidth - 1u);

    const auto pointAt =
        [this,
         viewOffset,
         viewWidth,
         &scope,
         voltsToPixels,
         xScale](std::int64_t sample) -> BeamPoint
        {
            sample =
                std::clamp<std::int64_t>(
                    sample,
                    0,
                    static_cast<std::int64_t>(
                        viewWidth) - 1);

            const std::size_t index =
                viewOffset +
                static_cast<std::size_t>(
                    sample);

            return BeamPoint
            {
                scope.left() +
                    static_cast<double>(sample) *
                    xScale,

                scope.bottom() -
                    static_cast<double>(
                        fullLumaVolts_[index]) *
                    voltsToPixels
            };
        };

    const auto catmull =
        [](const BeamPoint& p0,
           const BeamPoint& p1,
           const BeamPoint& p2,
           const BeamPoint& p3,
           double t) -> BeamPoint
        {
            const double t2 = t * t;
            const double t3 = t2 * t;

            return BeamPoint
            {
                0.5 *
                ((2.0 * p1.x) +
                 (-p0.x + p2.x) * t +
                 (2.0 * p0.x -
                  5.0 * p1.x +
                  4.0 * p2.x -
                  p3.x) * t2 +
                 (-p0.x +
                  3.0 * p1.x -
                  3.0 * p2.x +
                  p3.x) * t3),

                0.5 *
                ((2.0 * p1.y) +
                 (-p0.y + p2.y) * t +
                 (2.0 * p0.y -
                  5.0 * p1.y +
                  4.0 * p2.y -
                  p3.y) * t2 +
                 (-p0.y +
                  3.0 * p1.y -
                  3.0 * p2.y +
                  p3.y) * t3)
            };
        };

    polyline.reserve(
        viewWidth * 2u);

    polyline.push_back(
        pointAt(0));

    for (std::size_t i = 0u;
        i + 1u < viewWidth;
        ++i)
    {
        const BeamPoint p0 =
            pointAt(
                static_cast<std::int64_t>(i) - 1);

        const BeamPoint p1 =
            pointAt(
                static_cast<std::int64_t>(i));

        const BeamPoint p2 =
            pointAt(
                static_cast<std::int64_t>(i) + 1);

        const BeamPoint p3 =
            pointAt(
                static_cast<std::int64_t>(i) + 2);

        polyline.push_back(
            catmull(
                p0,
                p1,
                p2,
                p3,
                0.5));

        polyline.push_back(p2);
    }

    return polyline;
}

std::vector<std::uint16_t> WaveformRenderer::renderCurrentPhosphorEnergy(
    const std::vector<BeamPoint>& polyline,
    const QRectF& plotRect,
    std::uint64_t& glowUs,
    std::uint64_t& coreUs,
    int& activeMinX,
    int& activeMinY,
    int& activeMaxX,
    int& activeMaxY) const
{
    glowUs = 0u;
    coreUs = 0u;

    const int width =
        image_.width();

    const int height =
        image_.height();

    if (polyline.size() < 2u ||
        width <= 0 ||
        height <= 0)
    {
        return {};
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    // Monochrome 16-bit beam-energy target buffer.
    std::vector<std::uint16_t> currentEnergy(
        pixelCount,
        0u);

    const double renderDimension =
        std::max(
            1.0,
            std::min(
                plotRect.width(),
                plotRect.height()));

    /*
     * CATWUZLE ANALYTIC CORE + LOCAL GLOW
     * ------------------------------------
     *
     * The white beam core is NOT made from point stamps anymore.
     *
     * Every Catweazle polyline segment is rasterised directly against the
     * target pixel grid using the shortest distance from a target pixel
     * centre to the actual segment.  A one-pixel analytic coverage ramp
     * provides subpixel antialiasing continuously for every possible angle.
     *
     * This removes the two weaknesses that became visible after thinning the
     * large-target beam:
     *
     *   - phase/angle "stairs" on steep slopes
     *   - occasional holes between one-pixel-spaced point stamps
     *
     * Beam Glow remains deliberately cheap: it is still a small local 5x5
     * radial stamp sampled along the trace.  Glow is garnish; the visible
     * white core now comes from the analytic line rasteriser.
     */
    constexpr int kGlowKernelSize = 5;
    constexpr int kGlowKernelRadius =
        kGlowKernelSize / 2;

    constexpr int kSubpixelPhases = 8;
    constexpr int kSubpixelKernelCount =
        kSubpixelPhases * kSubpixelPhases;

    using GlowKernel =
        std::array<std::uint16_t,
            kGlowKernelSize * kGlowKernelSize>;

    const double linearTargetScale =
        std::clamp(
            plotRect.height() /
            1080.0,
            0.05,
            1.0);

    const double targetScale =
        std::pow(
            linearTargetScale,
            2.35);

    const double midpoint =
        0.03 +
        0.97 * targetScale;

    const double glowGain =
        static_cast<double>(
            std::clamp(glow_, 0, 100)) /
        100.0;

    /*
     * Keep the accepted mini-view width and the recently accepted thin
     * full-size beam.
     *
     * This is now interpreted as an approximate optical core width rather
     * than a Gaussian stamp sigma.
     */
    const double largeTargetBeamTaper =
        1.0 -
        0.6551724137931034 *
        midpoint * midpoint;

    const double legacyCoreSigma =
        0.18 +
        0.58 * midpoint *
        largeTargetBeamTaper;

    /*
     * Approximate half-width of the bright beam core.
     *
     * The lower clamp prevents tiny targets from becoming discontinuous,
     * while the full-size value stays close to the accepted ~half-width
     * look.  Antialias coverage extends another half pixel around this
     * geometric core.
     */
    /*
     * Small views may not fatten single traces unnecessarily.  Preserve the
     * accepted full-size beam, but taper the analytic core slightly on small
     * targets so mini views stay crisp without reintroducing gaps.
     */
    const double miniViewBlend =
        std::clamp(
            (renderDimension - 260.0) /
                640.0,
            0.0,
            1.0);

    const double miniViewThinFactor =
        0.74 +
        0.26 * miniViewBlend;

    const double coreHalfWidth =
        std::clamp(
            legacyCoreSigma * 0.78 * miniViewThinFactor,
            0.13,
            0.34);

    const double glowSigma =
        0.58 +
        0.92 * midpoint;

    /*
     * White core energy.  The old stamp path multiplied its kernel energy
     * by seven before accumulation.  This peak keeps the new core in the
     * same visual brightness ballpark without forcing overlapping segments
     * to become brighter at joins.
     */
    /*
     * Final Q-less energy -> display conversion uses energy >> 3.
     * 2040 therefore maps exactly to display white (255) without relying
     * on saturation.
     */
    constexpr std::uint16_t kCorePeakEnergy =
        2040u;

    std::array<GlowKernel,
        kSubpixelKernelCount> glowKernels{};

    /*
     * Glow only.  No white core lives in these kernels anymore.
     */
    if (glowGain > 0.0)
    {
        constexpr int kCoverageSamples = 4;
        constexpr double kCoverageInv =
            1.0 /
            static_cast<double>(
                kCoverageSamples *
                kCoverageSamples);

        for (int phaseY = 0;
            phaseY < kSubpixelPhases;
            ++phaseY)
        {
            const double fy =
                static_cast<double>(phaseY) /
                static_cast<double>(kSubpixelPhases);

            for (int phaseX = 0;
                phaseX < kSubpixelPhases;
                ++phaseX)
            {
                const double fx =
                    static_cast<double>(phaseX) /
                    static_cast<double>(kSubpixelPhases);

                GlowKernel& kernel =
                    glowKernels[
                        static_cast<std::size_t>(
                            phaseY * kSubpixelPhases +
                            phaseX)];

                for (int ky = -kGlowKernelRadius;
                    ky <= kGlowKernelRadius;
                    ++ky)
                {
                    for (int kx = -kGlowKernelRadius;
                        kx <= kGlowKernelRadius;
                        ++kx)
                    {
                        double accumulatedGlow = 0.0;

                        for (int sy = 0;
                            sy < kCoverageSamples;
                            ++sy)
                        {
                            const double py =
                                static_cast<double>(ky) -
                                0.5 +
                                (static_cast<double>(sy) + 0.5) /
                                static_cast<double>(kCoverageSamples);

                            for (int sx = 0;
                                sx < kCoverageSamples;
                                ++sx)
                            {
                                const double px =
                                    static_cast<double>(kx) -
                                    0.5 +
                                    (static_cast<double>(sx) + 0.5) /
                                    static_cast<double>(kCoverageSamples);

                                const double dx =
                                    px - fx;

                                const double dy =
                                    py - fy;

                                const double distance =
                                    std::hypot(
                                        dx,
                                        dy);

                                const double shoulder =
                                    std::clamp(
                                        (distance - midpoint) /
                                        std::max(
                                            0.18,
                                            1.70 - midpoint),
                                        0.0,
                                        1.0);

                                accumulatedGlow +=
                                    std::exp(
                                        -0.5 *
                                        (distance * distance) /
                                        (glowSigma * glowSigma)) *
                                    shoulder;
                            }
                        }

                        const double glowCoverage =
                            accumulatedGlow *
                            kCoverageInv;

                        const double glowEnergy =
                            240.0 *
                            glowGain *
                            glowCoverage;

                        kernel[
                            static_cast<std::size_t>(
                                (ky + kGlowKernelRadius) *
                                    kGlowKernelSize +
                                (kx + kGlowKernelRadius))] =
                            static_cast<std::uint16_t>(
                                std::clamp(
                                    static_cast<int>(
                                        std::lround(
                                            glowEnergy)),
                                    0,
                                    1023));
                    }
                }
            }
        }
    }

    const auto addEnergy =
        [&currentEnergy,
         width,
         height](
            int x,
            int y,
            int contribution)
        {
            if (x < 0 ||
                x >= width ||
                y < 0 ||
                y >= height ||
                contribution <= 0)
            {
                return;
            }

            const std::size_t index =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            const std::uint32_t sum =
                static_cast<std::uint32_t>(
                    currentEnergy[index]) +
                static_cast<std::uint32_t>(
                    contribution);

            currentEnergy[index] =
                static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(
                        sum,
                        65535u));
        };

    const auto maxCoreEnergy =
        [&currentEnergy,
         width,
         height](
            int x,
            int y,
            std::uint16_t contribution)
        {
            if (x < 0 ||
                x >= width ||
                y < 0 ||
                y >= height ||
                contribution == 0u)
            {
                return;
            }

            const std::size_t index =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            currentEnergy[index] =
                std::max(
                    currentEnergy[index],
                    contribution);
        };

    constexpr int kCoreCoverageSamples = 2;
    constexpr double kCoreCoverageInv =
        1.0 /
        static_cast<double>(
            kCoreCoverageSamples *
            kCoreCoverageSamples);

    QElapsedTimer timer;
    timer.start();

    activeMinX = width;
    activeMinY = height;
    activeMaxX = -1;
    activeMaxY = -1;

    /*
     * Analytic white core.
     *
     * Pixel coordinates in this renderer are centred on integer target
     * coordinates.  For every Catweazle segment we visit only the small
     * bounding rectangle around that segment and evaluate the exact shortest
     * distance from the pixel centre to the segment.
     *
     * Coverage:
     *   <= halfWidth - 0.5  : fully covered
     *   >= halfWidth + 0.5  : not covered
     *   between             : linear subpixel edge coverage
     *
     * This is orientation-independent and cannot leave gaps between
     * consecutive connected segments.
     */
    const double aaRadius =
        coreHalfWidth +
        0.5;

    for (std::size_t i = 1u;
        i < polyline.size();
        ++i)
    {
        const BeamPoint& first =
            polyline[i - 1u];

        const BeamPoint& second =
            polyline[i];

        const double segmentDx =
            second.x -
            first.x;

        const double segmentDy =
            second.y -
            first.y;

        const double segmentLengthSquared =
            segmentDx * segmentDx +
            segmentDy * segmentDy;

        if (segmentLengthSquared <= 1.0e-18)
        {
            continue;
        }

        const int minX =
            std::max(
                0,
                static_cast<int>(
                    std::floor(
                        std::min(first.x, second.x) -
                        aaRadius)));

        const int maxX =
            std::min(
                width - 1,
                static_cast<int>(
                    std::ceil(
                        std::max(first.x, second.x) +
                        aaRadius)));

        const int minY =
            std::max(
                0,
                static_cast<int>(
                    std::floor(
                        std::min(first.y, second.y) -
                        aaRadius)));

        const int maxY =
            std::min(
                height - 1,
                static_cast<int>(
                    std::ceil(
                        std::max(first.y, second.y) +
                        aaRadius)));

        activeMinX =
            std::min(
                activeMinX,
                minX);

        activeMaxX =
            std::max(
                activeMaxX,
                maxX);

        activeMinY =
            std::min(
                activeMinY,
                minY);

        activeMaxY =
            std::max(
                activeMaxY,
                maxY);

        for (int y = minY;
            y <= maxY;
            ++y)
        {
            const double py =
                static_cast<double>(y);

            for (int x = minX;
                x <= maxX;
                ++x)
            {
                const double px =
                    static_cast<double>(x);

                /*
                 * Pearl-fix pass:
                 *
                 * The first analytic CatWuzle core sampled only the pixel
                 * centre.  That kept the geometry continuous, but on dim
                 * traces it still left a subtle beaded / pearly modulation
                 * because neighbouring x-columns could land on slightly
                 * different centre distances.
                 *
                 * Sample a tiny 2x2 subpixel grid inside each target pixel
                 * and average the local analytic coverage.  This keeps the
                 * same thin beam geometry, but turns the centre-sample result
                 * into a small area estimate so adjacent columns vary much
                 * less in intensity.
                 */
                const double maximumCoverage =
                    coreHalfWidth +
                    0.5;

                double coverage = 0.0;

                for (int sampleY = 0;
                    sampleY < kCoreCoverageSamples;
                    ++sampleY)
                {
                    const double subpixelY =
                        py - 0.5 +
                        (static_cast<double>(sampleY) + 0.5) /
                            static_cast<double>(kCoreCoverageSamples);

                    for (int sampleX = 0;
                        sampleX < kCoreCoverageSamples;
                        ++sampleX)
                    {
                        const double subpixelX =
                            px - 0.5 +
                            (static_cast<double>(sampleX) + 0.5) /
                                static_cast<double>(kCoreCoverageSamples);

                        const double pointDx =
                            subpixelX -
                            first.x;

                        const double pointDy =
                            subpixelY -
                            first.y;

                        const double projection =
                            std::clamp(
                                (pointDx * segmentDx +
                                 pointDy * segmentDy) /
                                    segmentLengthSquared,
                                0.0,
                                1.0);

                        const double closestX =
                            first.x +
                            projection *
                                segmentDx;

                        const double closestY =
                            first.y +
                            projection *
                                segmentDy;

                        const double dx =
                            subpixelX -
                            closestX;

                        const double dy =
                            subpixelY -
                            closestY;

                        const double distance =
                            std::hypot(
                                dx,
                                dy);

                        coverage +=
                            std::clamp(
                                (maximumCoverage -
                                    distance) /
                                    maximumCoverage,
                                0.0,
                                1.0);
                    }
                }

                coverage *= kCoreCoverageInv;

                /*
                 * Conservative pixel-square guard.
                 *
                 * The 2x2 subpixel estimator remains the primary AA result.
                 * However, at an unlucky target phase a very thin segment can
                 * pass through a pixel square while missing all four 2x2
                 * sample locations.  That produces the stationary black
                 * "hole" seen at one X position.
                 *
                 * Use a cheap slab intersection against the actual target
                 * pixel square.  If the CatWuzle centreline geometrically
                 * crosses that square, enforce only a small minimum coverage.
                 * Normal pixels whose 2x2 coverage is already higher remain
                 * completely unchanged.
                 */
                {
                    const double pixelMinX = px - 0.5;
                    const double pixelMaxX = px + 0.5;
                    const double pixelMinY = py - 0.5;
                    const double pixelMaxY = py + 0.5;

                    double enterT = 0.0;
                    double exitT = 1.0;

                    const auto clipAxis =
                        [&enterT,
                         &exitT](
                            double origin,
                            double delta,
                            double minimum,
                            double maximum) -> bool
                        {
                            constexpr double kTiny = 1.0e-12;

                            if (std::abs(delta) <= kTiny)
                            {
                                return
                                    origin >= minimum &&
                                    origin <= maximum;
                            }

                            double firstT =
                                (minimum - origin) /
                                delta;

                            double secondT =
                                (maximum - origin) /
                                delta;

                            if (firstT > secondT)
                            {
                                std::swap(
                                    firstT,
                                    secondT);
                            }

                            enterT =
                                std::max(
                                    enterT,
                                    firstT);

                            exitT =
                                std::min(
                                    exitT,
                                    secondT);

                            return enterT <= exitT;
                        };

                    const bool crossesPixel =
                        clipAxis(
                            first.x,
                            segmentDx,
                            pixelMinX,
                            pixelMaxX) &&
                        clipAxis(
                            first.y,
                            segmentDy,
                            pixelMinY,
                            pixelMaxY) &&
                        exitT >= 0.0 &&
                        enterT <= 1.0;

                    if (crossesPixel)
                    {
                        constexpr double kMinimumCrossingCoverage =
                            0.20;

                        coverage =
                            std::max(
                                coverage,
                                kMinimumCrossingCoverage);
                    }
                }

                /*
                 * A gentle curve lifts the mid-coverage part of the AA ramp
                 * so the thin beam keeps a continuous luminous filament in
                 * dim regions, without materially widening the trace.
                 */
                const double coverageLift =
                    1.16 +
                    0.12 * miniViewBlend;

                coverage =
                    1.0 -
                    std::pow(
                        1.0 - coverage,
                        coverageLift);

                if (coverage <= 0.0)
                {
                    continue;
                }

                const std::uint16_t energy =
                    static_cast<std::uint16_t>(
                        std::clamp(
                            static_cast<int>(
                                std::lround(
                                    coverage *
                                    static_cast<double>(
                                        kCorePeakEnergy) *
                                    static_cast<double>(
                                        coreIntensity_) /
                                    100.0)),
                            0,
                            static_cast<int>(
                                kCorePeakEnergy) * 4));

                maxCoreEnergy(
                    x,
                    y,
                    energy);
            }
        }
    }


    /*
     * CATWUZLE ROUND-JOIN COVERAGE
     * ----------------------------
     *
     * The 2x2 area coverage above is evaluated per segment and the final
     * pixel energy is combined with max().  At an interior polyline vertex,
     * subpixel samples can be split across the two neighbouring segments:
     * neither segment alone sees the complete covered area, even though the
     * union of both segments does.  That produces a stationary local notch
     * (the visible "bite") at some joins.
     *
     * Rasterise the mathematically correct round join at every interior
     * Catweazle vertex.  It uses exactly the same core radius, 2x2 area
     * coverage, shaping and intensity as the segment core, and combines via
     * max(), so it fills only missing join coverage without widening the
     * normal stroke.
     */
    for (std::size_t i = 1u;
        i + 1u < polyline.size();
        ++i)
    {
        const BeamPoint& vertex =
            polyline[i];

        const int minX =
            std::max(
                0,
                static_cast<int>(
                    std::floor(
                        vertex.x -
                        aaRadius)));

        const int maxX =
            std::min(
                width - 1,
                static_cast<int>(
                    std::ceil(
                        vertex.x +
                        aaRadius)));

        const int minY =
            std::max(
                0,
                static_cast<int>(
                    std::floor(
                        vertex.y -
                        aaRadius)));

        const int maxY =
            std::min(
                height - 1,
                static_cast<int>(
                    std::ceil(
                        vertex.y +
                        aaRadius)));

        for (int y = minY;
            y <= maxY;
            ++y)
        {
            const double py =
                static_cast<double>(y);

            for (int x = minX;
                x <= maxX;
                ++x)
            {
                const double px =
                    static_cast<double>(x);

                const double maximumCoverage =
                    coreHalfWidth +
                    0.5;

                double coverage = 0.0;

                for (int sampleY = 0;
                    sampleY < kCoreCoverageSamples;
                    ++sampleY)
                {
                    const double subpixelY =
                        py - 0.5 +
                        (static_cast<double>(sampleY) + 0.5) /
                            static_cast<double>(kCoreCoverageSamples);

                    for (int sampleX = 0;
                        sampleX < kCoreCoverageSamples;
                        ++sampleX)
                    {
                        const double subpixelX =
                            px - 0.5 +
                            (static_cast<double>(sampleX) + 0.5) /
                                static_cast<double>(kCoreCoverageSamples);

                        const double dx =
                            subpixelX -
                            vertex.x;

                        const double dy =
                            subpixelY -
                            vertex.y;

                        const double distance =
                            std::hypot(
                                dx,
                                dy);

                        coverage +=
                            std::clamp(
                                (maximumCoverage -
                                    distance) /
                                    maximumCoverage,
                                0.0,
                                1.0);
                    }
                }

                coverage *=
                    kCoreCoverageInv;

                const double coverageLift =
                    1.16 +
                    0.12 * miniViewBlend;

                coverage =
                    1.0 -
                    std::pow(
                        1.0 - coverage,
                        coverageLift);

                if (coverage <= 0.0)
                {
                    continue;
                }

                const std::uint16_t energy =
                    static_cast<std::uint16_t>(
                        std::clamp(
                            static_cast<int>(
                                std::lround(
                                    coverage *
                                    static_cast<double>(
                                        kCorePeakEnergy) *
                                    static_cast<double>(
                                        coreIntensity_) /
                                    100.0)),
                            0,
                            static_cast<int>(
                                kCorePeakEnergy) * 4));

                maxCoreEnergy(
                    x,
                    y,
                    energy);
            }
        }
    }

    coreUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    /*
     * Cheap local glow pass.
     *
     * Sample at ~1 target-pixel spacing.  Because this pass contains only
     * diffuse halo energy, tiny gaps/phase differences are not visible in
     * the white beam core.
     */
    timer.restart();

    if (glowGain > 0.0)
    {
        for (std::size_t i = 1u;
            i < polyline.size();
            ++i)
        {
            const BeamPoint& first =
                polyline[i - 1u];

            const BeamPoint& second =
                polyline[i];

            const double segmentDx =
                second.x -
                first.x;

            const double segmentDy =
                second.y -
                first.y;

            const double segmentLength =
                std::hypot(
                    segmentDx,
                    segmentDy);

            if (segmentLength <= 1.0e-9)
            {
                continue;
            }

            const int steps =
                std::max(
                    1,
                    static_cast<int>(
                        std::ceil(
                            segmentLength)));

            const int firstStep =
                i == 1u
                ? 0
                : 1;

            for (int step = firstStep;
                step <= steps;
                ++step)
            {
                const double t =
                    static_cast<double>(step) /
                    static_cast<double>(steps);

                const double beamX =
                    first.x +
                    segmentDx * t;

                const double beamY =
                    first.y +
                    segmentDy * t;

                int centerX =
                    static_cast<int>(
                        std::floor(
                            beamX));

                int centerY =
                    static_cast<int>(
                        std::floor(
                            beamY));

                const double fracX =
                    beamX -
                    static_cast<double>(
                        centerX);

                const double fracY =
                    beamY -
                    static_cast<double>(
                        centerY);

                int phaseX =
                    static_cast<int>(
                        std::lround(
                            fracX *
                            static_cast<double>(
                                kSubpixelPhases)));

                int phaseY =
                    static_cast<int>(
                        std::lround(
                            fracY *
                            static_cast<double>(
                                kSubpixelPhases)));

                if (phaseX >= kSubpixelPhases)
                {
                    phaseX = 0;
                    ++centerX;
                }

                if (phaseY >= kSubpixelPhases)
                {
                    phaseY = 0;
                    ++centerY;
                }

                const GlowKernel& glowKernel =
                    glowKernels[
                        static_cast<std::size_t>(
                            phaseY *
                                kSubpixelPhases +
                            phaseX)];

                activeMinX =
                    std::min(
                        activeMinX,
                        centerX -
                            kGlowKernelRadius);

                activeMaxX =
                    std::max(
                        activeMaxX,
                        centerX +
                            kGlowKernelRadius);

                activeMinY =
                    std::min(
                        activeMinY,
                        centerY -
                            kGlowKernelRadius);

                activeMaxY =
                    std::max(
                        activeMaxY,
                        centerY +
                            kGlowKernelRadius);

                for (int ky = -kGlowKernelRadius;
                    ky <= kGlowKernelRadius;
                    ++ky)
                {
                    for (int kx = -kGlowKernelRadius;
                        kx <= kGlowKernelRadius;
                        ++kx)
                    {
                        const int value =
                            glowKernel[
                                static_cast<std::size_t>(
                                    (ky + kGlowKernelRadius) *
                                        kGlowKernelSize +
                                    (kx + kGlowKernelRadius))];

                        if (value <= 0)
                        {
                            continue;
                        }

                        addEnergy(
                            centerX + kx,
                            centerY + ky,
                            value * 7);
                    }
                }
            }
        }
    }

    glowUs =
        static_cast<std::uint64_t>(
            timer.nsecsElapsed() / 1000);

    return currentEnergy;
}

void WaveformRenderer::clearScopephorFrames()
{
    scopephorPreviousEnergy_.clear();
    scopephorPreviousMinX_ = 0;
    scopephorPreviousMinY_ = 0;
    scopephorPreviousMaxX_ = -1;
    scopephorPreviousMaxY_ = -1;
}

void WaveformRenderer::applyScopephorFeedback(
    std::vector<std::uint16_t>& currentEnergy,
    int& activeMinX,
    int& activeMinY,
    int& activeMaxX,
    int& activeMaxY)
{
    if (currentEnergy.empty())
    {
        return;
    }

    const int width = image_.width();
    const int height = image_.height();

    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (persistence_ <= 0)
    {
        scopephorPreviousEnergy_.clear();
        scopephorPreviousMinX_ = 0;
        scopephorPreviousMinY_ = 0;
        scopephorPreviousMaxX_ = -1;
        scopephorPreviousMaxY_ = -1;
        return;
    }

    const double slider =
        static_cast<double>(
            std::clamp(persistence_, 0, 100)) /
        100.0;

    /*
     * ScopePhor should now hang a bit longer.
     *
     * Keep the same general curve shape, but raise the retention factor
     * modestly so low/mid settings remain useful while the default setting
     * clearly glows longer on screen.
     */
    const double decay =
        0.72 *
        std::pow(slider, 0.36);

    if (scopephorPreviousEnergy_.size() !=
        currentEnergy.size())
    {
        scopephorPreviousEnergy_.assign(
            currentEnergy.size(),
            0u);

        scopephorPreviousMinX_ = 0;
        scopephorPreviousMinY_ = 0;
        scopephorPreviousMaxX_ = -1;
        scopephorPreviousMaxY_ = -1;
    }

    int unionMinX = activeMinX;
    int unionMinY = activeMinY;
    int unionMaxX = activeMaxX;
    int unionMaxY = activeMaxY;

    const bool previousValid =
        scopephorPreviousMaxX_ >= scopephorPreviousMinX_ &&
        scopephorPreviousMaxY_ >= scopephorPreviousMinY_;

    const bool currentValid =
        unionMaxX >= unionMinX &&
        unionMaxY >= unionMinY;

    if (previousValid)
    {
        if (!currentValid)
        {
            unionMinX = scopephorPreviousMinX_;
            unionMinY = scopephorPreviousMinY_;
            unionMaxX = scopephorPreviousMaxX_;
            unionMaxY = scopephorPreviousMaxY_;
        }
        else
        {
            unionMinX = std::min(unionMinX, scopephorPreviousMinX_);
            unionMinY = std::min(unionMinY, scopephorPreviousMinY_);
            unionMaxX = std::max(unionMaxX, scopephorPreviousMaxX_);
            unionMaxY = std::max(unionMaxY, scopephorPreviousMaxY_);
        }
    }

    if (unionMaxX < unionMinX ||
        unionMaxY < unionMinY)
    {
        return;
    }

    unionMinX = std::clamp(unionMinX, 0, width - 1);
    unionMaxX = std::clamp(unionMaxX, 0, width - 1);
    unionMinY = std::clamp(unionMinY, 0, height - 1);
    unionMaxY = std::clamp(unionMaxY, 0, height - 1);

    const std::uint32_t decayQ16 =
        static_cast<std::uint32_t>(
            std::clamp(
                static_cast<int>(
                    std::lround(decay * 65536.0)),
                0,
                65536));

    int nextMinX = width;
    int nextMinY = height;
    int nextMaxX = -1;
    int nextMaxY = -1;

    const __m256i decayVector =
        _mm256_set1_epi32(
            static_cast<int>(
                decayQ16));

    const __m256i thresholdVector =
        _mm256_set1_epi32(7);

    const __m256i maxEnergyVector =
        _mm256_set1_epi32(65535);

    for (int y = unionMinY;
        y <= unionMaxY;
        ++y)
    {
        const std::size_t row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);

        int x = unionMinX;
        bool rowHasEnergy = false;

        for (;
            x + 7 <= unionMaxX;
            x += 8)
        {
            const std::size_t index =
                row +
                static_cast<std::size_t>(x);

            const __m128i old16 =
                _mm_loadu_si128(
                    reinterpret_cast<const __m128i*>(
                        scopephorPreviousEnergy_.data() +
                        index));

            const __m128i current16 =
                _mm_loadu_si128(
                    reinterpret_cast<const __m128i*>(
                        currentEnergy.data() +
                        index));

            const __m256i old32 =
                _mm256_cvtepu16_epi32(old16);

            const __m256i current32 =
                _mm256_cvtepu16_epi32(current16);

            const __m256i oldScaled =
                _mm256_srli_epi32(
                    _mm256_mullo_epi32(
                        old32,
                        decayVector),
                    16);

            __m256i mixed =
                _mm256_add_epi32(
                    current32,
                    oldScaled);

            mixed =
                _mm256_min_epi32(
                    mixed,
                    maxEnergyVector);

            // Kill sub-visible residual phosphor.
            const __m256i visibleMask =
                _mm256_cmpgt_epi32(
                    mixed,
                    thresholdVector);

            mixed =
                _mm256_and_si256(
                    mixed,
                    visibleMask);

            const __m128i mixedLow =
                _mm256_castsi256_si128(
                    mixed);

            const __m128i mixedHigh =
                _mm256_extracti128_si256(
                    mixed,
                    1);

            const __m128i packed =
                _mm_packus_epi32(
                    mixedLow,
                    mixedHigh);

            _mm_storeu_si128(
                reinterpret_cast<__m128i*>(
                    currentEnergy.data() +
                    index),
                packed);

            _mm_storeu_si128(
                reinterpret_cast<__m128i*>(
                    scopephorPreviousEnergy_.data() +
                    index),
                packed);

            if (_mm256_movemask_epi8(
                    visibleMask) != 0)
            {
                rowHasEnergy = true;
                nextMinX =
                    std::min(
                        nextMinX,
                        x);

                nextMaxX =
                    std::max(
                        nextMaxX,
                        x + 7);
            }
        }

        for (;
            x <= unionMaxX;
            ++x)
        {
            const std::size_t index =
                row +
                static_cast<std::size_t>(x);

            const std::uint32_t oldContribution =
                (static_cast<std::uint32_t>(
                    scopephorPreviousEnergy_[index]) *
                 decayQ16) >>
                16;

            const std::uint32_t mixed =
                static_cast<std::uint32_t>(
                    currentEnergy[index]) +
                oldContribution;

            std::uint16_t value =
                static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(
                        mixed,
                        65535u));

            if (value < 8u)
            {
                value = 0u;
            }

            currentEnergy[index] = value;
            scopephorPreviousEnergy_[index] = value;

            if (value != 0u)
            {
                rowHasEnergy = true;
                nextMinX =
                    std::min(
                        nextMinX,
                        x);

                nextMaxX =
                    std::max(
                        nextMaxX,
                        x);
            }
        }

        if (rowHasEnergy)
        {
            nextMinY =
                std::min(
                    nextMinY,
                    y);

            nextMaxY =
                std::max(
                    nextMaxY,
                    y);
        }
    }

    scopephorPreviousMinX_ = nextMinX;
    scopephorPreviousMinY_ = nextMinY;
    scopephorPreviousMaxX_ = nextMaxX;
    scopephorPreviousMaxY_ = nextMaxY;

    activeMinX = nextMinX;
    activeMinY = nextMinY;
    activeMaxX = nextMaxX;
    activeMaxY = nextMaxY;
}

void WaveformRenderer::drawLineInfoOverlay(QPainter& painter)
{
    if (!lineInfoOverlayEnabled_)
    {
        return;
    }

    const QRectF viewport =
        viewportRect();

    const QRectF scope =
        scaledScopeRect();

    const QVector<ViewportOverlay::InfoRow> rows
    {
        {
            QStringLiteral("LINE"),
            QStringLiteral("%1   X%2")
                .arg(
                    selectedLine_ >= 0
                    ? QString::number(selectedLine_)
                    : QStringLiteral("ALL"))
                .arg(zoomFactor_)
        }
    };

    const double referenceHeight =
        lineInfoOverlayPalOutput_
        ? viewport.height()
        : std::min(viewport.height(), 720.0);

    const QSizeF cardSize =
        ViewportOverlay::infoCardRequiredSize(
            rows,
            referenceHeight,
            lineInfoOverlayPalOutput_);

    const double lineGap =
        std::max(
            4.0,
            viewport.height() * 0.008);

    // Keep the line/zoom readout at the top-right.  The top-left is
    // intentionally left free for the automatic SNR readout.
    const QRectF cardRect(
        scope.right() - cardSize.width(),
        scope.top() + lineGap,
        cardSize.width(),
        cardSize.height());

    ViewportOverlay::drawInfoCard(
        painter,
        cardRect,
        rows,
        referenceHeight,
        lineInfoOverlayPalOutput_);
}


void WaveformRenderer::renderAllLines(
    const Yuv444Frame& frame)
{
    renderTimings_ = {};

    QElapsedTimer phaseTimer;
    phaseTimer.start();

    sourceY_.clear();

    std::fill(
        hits_.begin(),
        hits_.end(),
        0u);

    renderTimings_.persistenceUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    phaseTimer.restart();

    const int displayWidth =
        image_.width();

    const int displayHeight =
        image_.height();

    const QRectF scope =
        scaledScopeRect();

    const double plotRenderDimension =
        std::max(
            1.0,
            std::min(
                scope.width(),
                scope.height()));

    const double plotBeamScale =
        std::clamp(
            std::sqrt(
                plotRenderDimension / 576.0),
            1.0,
            1.90);

    constexpr double kReferenceCoreRadiusPx = 0.82;
    beamCoreRadiusPx_ =
        kReferenceCoreRadiusPx * plotBeamScale;

    renderTimings_.beamCoreRadiusPx =
        beamCoreRadiusPx_;
    renderTimings_.beamCoreMarginPx =
        std::max(
            1,
            static_cast<int>(
                std::floor(beamCoreRadiusPx_)));

    const int firstScopeX =
        std::clamp(
            static_cast<int>(
                std::ceil(scope.left())),
            0,
            displayWidth - 1);

    const int lastScopeX =
        std::clamp(
            static_cast<int>(
                std::floor(scope.right())),
            0,
            displayWidth - 1);

    const auto digitalLevels =
        levels(VideoStandard::pal625());

    const auto analog =
        analogLevels(
            VideoColorStandard::Rec601_625);

    const double voltsPerCode =
        (analog.whiteVolts - analog.blackVolts) /
        static_cast<double>(
            digitalLevels.yWhite -
            digitalLevels.yBlack);

    const double voltsToPixels =
        scope.height() /
        analog.graticuleMaxVolts;

    for (int line = 0;
        line < frame.height;
        ++line)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(line) *
            static_cast<std::size_t>(
                frame.width);

        int previousPlotY = -1;

        for (int sourceX = 0;
            sourceX < frame.width;
            ++sourceX)
        {
            const double normalisedX =
                frame.width > 1
                ? static_cast<double>(sourceX) /
                static_cast<double>(
                    frame.width - 1)
                : 0.0;

            const int displayX =
                std::clamp(
                    static_cast<int>(
                        std::lround(
                            scope.left() +
                            normalisedX *
                            scope.width())),
                    firstScopeX,
                    lastScopeX);

            const std::uint16_t y16 =
                frame.y[
                    lineOffset +
                        static_cast<std::size_t>(
                            sourceX)];

            const double y10 =
                static_cast<double>(y16) /
                64.0;

            const double volts =
                analog.blackVolts +
                (y10 - digitalLevels.yBlack) *
                voltsPerCode;

            const double plotYFloat =
                scope.bottom() -
                volts *
                voltsToPixels;

            const int plotY =
                std::clamp(
                    static_cast<int>(
                        std::lround(
                            plotYFloat)),
                    0,
                    displayHeight - 1);

            const double fractionalY =
                plotYFloat -
                std::floor(plotYFloat);

            const std::uint32_t fraction =
                static_cast<std::uint32_t>(
                    std::clamp(
                        fractionalY * 256.0,
                        0.0,
                        255.0));

            const std::size_t currentIndex =
                static_cast<std::size_t>(plotY) *
                static_cast<std::size_t>(
                    displayWidth) +
                static_cast<std::size_t>(
                    displayX);

            hits_[currentIndex] +=
                256u - fraction;

            if (plotY > 0)
            {
                const std::size_t adjacentIndex =
                    static_cast<std::size_t>(
                        plotY - 1) *
                    static_cast<std::size_t>(
                        displayWidth) +
                    static_cast<std::size_t>(
                        displayX);

                hits_[adjacentIndex] +=
                    fraction;
            }

            if (previousPlotY >= 0)
            {
                const int firstY =
                    std::min(
                        previousPlotY,
                        plotY);

                const int lastY =
                    std::max(
                        previousPlotY,
                        plotY);

                for (int y = firstY;
                    y <= lastY;
                    ++y)
                {
                    const std::size_t segmentIndex =
                        static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(
                            displayWidth) +
                        static_cast<std::size_t>(
                            displayX);

                    hits_[segmentIndex] +=
                        64u;
                }
            }

            previousPlotY = plotY;
        }
    }

    // Build the graticule first.  The all-lines density trace only writes
    // non-zero pixels afterwards, so the trace naturally sits on top.
    image_.fill(Qt::black);
    {
        QPainter graticulePainter(&image_);
        graticule_.draw(
            graticulePainter,
            scope,
            VideoStandard::pal625());
    }

    const bool probePresentation =
        measurementProbePresentation_.load(
            std::memory_order_relaxed);

    double probeCenterX = 0.0;
    double probeCenterY = 0.0;
    double probeRadiusSquared = 0.0;

    if (probePresentation)
    {
        const AnalogVideoLevels probeAnalog =
            analogLevels(VideoColorStandard::Rec601_625);

        probeCenterX =
            scope.left() +
            measurementProbeNormalizedX_.load(
                std::memory_order_relaxed) *
                scope.width();

        probeCenterY =
            scope.bottom() -
            measurementProbeVolts_.load(
                std::memory_order_relaxed) *
                scope.height() /
                probeAnalog.graticuleMaxVolts;

        const double videoHeight =
            std::abs(
                (probeAnalog.whiteVolts - probeAnalog.blackVolts) *
                scope.height() /
                probeAnalog.graticuleMaxVolts);

        const double probeDiameter =
            (std::max)(12.0, videoHeight * 0.10);

        const double probeRadius =
            probeDiameter * 0.5;

        probeRadiusSquared =
            probeRadius * probeRadius;
    }

    for (int y = 0;
        y < displayHeight;
        ++y)
    {
        auto* destination =
            reinterpret_cast<QRgb*>(
                image_.scanLine(y));

        for (int x = 0;
            x < displayWidth;
            ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(
                    displayWidth) +
                static_cast<std::size_t>(x);

            const float persistence =
                static_cast<float>(persistence_) /
                255.0f;

            const float currentDensity =
                static_cast<float>(
                    hits_[index]);

            float& temporalDensity =
                allLinesPersistence_[index];

            if (persistence_ == 0)
            {
                temporalDensity =
                    currentDensity;
            }
            else
            {
                temporalDensity =
                    temporalDensity * persistence +
                    currentDensity;
            }

            const std::uint32_t tracePeakWhite =
                lineInfoOverlayPalOutput_
                ? 191u
                : 255u;

            std::uint32_t intensity =
                std::min<std::uint32_t>(
                    tracePeakWhite,
                    static_cast<std::uint32_t>(
                        temporalDensity / 256.0f) *
                    8u);

            if (probePresentation)
            {
                const double dx =
                    (static_cast<double>(x) + 0.5) -
                    probeCenterX;

                const double dy =
                    (static_cast<double>(y) + 0.5) -
                    probeCenterY;

                const bool insideProbe =
                    dx * dx + dy * dy <=
                    probeRadiusSquared;

                if (!insideProbe)
                {
                    intensity = (intensity + 1u) / 2u;
                }
            }

            if (intensity == 0)
            {
                continue;
            }

            destination[x] =
                qRgb(
                    static_cast<int>(intensity),
                    static_cast<int>(intensity),
                    static_cast<int>(intensity));
        }
    }

    renderTimings_.traceUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    phaseTimer.restart();
    QPainter painter(&image_);
    drawLineInfoOverlay(painter);
    renderTimings_.overlayUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);
}

void WaveformRenderer::addChromaFillPixel(
    int x,
    int y,
    int red,
    int green,
    int blue,
    int intensity)
{
    if (x < 0 ||
        x >= image_.width() ||
        y < 0 ||
        y >= image_.height())
    {
        return;
    }

    const std::size_t index =
        static_cast<std::size_t>(y) *
        static_cast<std::size_t>(
            image_.width()) +
        static_cast<std::size_t>(x);

    TracePixel& pixel =
        chromaTrace_[index];

    const auto addChannel =
        [intensity](
            std::uint16_t& destination,
            int channel)
        {
            const std::uint32_t contribution =
                static_cast<std::uint32_t>(
                    std::clamp(
                        channel,
                        0,
                        255)) *
                static_cast<std::uint32_t>(
                    intensity) /
                255u;

            destination =
                static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(
                        kMaximumChromaLevel,
                        static_cast<std::uint32_t>(
                            destination) +
                        contribution));
        };

    addChannel(
        pixel.red,
        red);

    addChannel(
        pixel.green,
        green);

    addChannel(
        pixel.blue,
        blue);
}

void WaveformRenderer::plotSegment(
    double x0,
    double y0,
    double x1,
    double y1,
    int intensity,
    int red,
    int green,
    int blue,
    int clipFirstX,
    int clipLastX)
{
    const double dx =
        x1 - x0;

    const double dy =
        y1 - y0;

    const double lengthSquared =
        dx * dx +
        dy * dy;

    if (lengthSquared <= 0.0)
    {
        plotBeam(
            x0,
            y0,
            intensity,
            red,
            green,
            blue,
            clipFirstX,
            clipLastX);

        return;
    }

    const double coreRadius =
        beamCoreRadiusPx_;

    // The segment bounds already use floor(min) / ceil(max), which
    // provide the sub-pixel guard band at both ends.  Using ceil()
    // here therefore adds an unnecessary full pixel whenever the
    // continuously scaled beam radius crosses an integer boundary
    // (for example 0.999 -> 1.001 px), causing a large raster-work
    // cliff without changing the visible beam support.  floor() keeps
    // the candidate box tight while still covering every integer pixel
    // whose centre can lie inside coreRadius.
    const int coreMargin =
        std::max(
            1,
            static_cast<int>(
                std::floor(coreRadius)));

    if (clipLastX < 0)
    {
        clipLastX = image_.width();
    }

    clipFirstX =
        std::clamp(
            clipFirstX,
            0,
            image_.width());

    clipLastX =
        std::clamp(
            clipLastX,
            clipFirstX,
            image_.width());

    const int firstX =
        std::max(
            clipFirstX,
            static_cast<int>(
                std::floor(
                    std::min(x0, x1))) -
                coreMargin);

    const int lastX =
        std::min(
            clipLastX - 1,
            static_cast<int>(
                std::ceil(
                    std::max(x0, x1))) +
                coreMargin);

    const int firstY =
        std::max(
            0,
            static_cast<int>(
                std::floor(
                    std::min(y0, y1))) -
                coreMargin);

    const int lastY =
        std::min(
            image_.height() - 1,
            static_cast<int>(
                std::ceil(
                    std::max(y0, y1))) +
                coreMargin);

    for (int y = firstY;
        y <= lastY;
        ++y)
    {
        for (int x = firstX;
            x <= lastX;
            ++x)
        {
            const double px =
                static_cast<double>(x);

            const double py =
                static_cast<double>(y);

            const double projection =
                ((px - x0) * dx +
                    (py - y0) * dy) /
                lengthSquared;

            const double t =
                std::clamp(
                    projection,
                    0.0,
                    1.0);

            const double nearestX =
                x0 + t * dx;

            const double nearestY =
                y0 + t * dy;

            const double distance =
                std::hypot(
                    px - nearestX,
                    py - nearestY);

            const double coverage =
                distance < coreRadius
                ? std::clamp(
                    1.0 -
                    (distance / coreRadius),
                    0.0,
                    1.0)
                : 0.0;

            if (coverage <= 0.0)
            {
                continue;
            }

            const std::uint32_t contribution =
                static_cast<std::uint32_t>(
                    coverage *
                    static_cast<double>(
                        intensity));

            const std::size_t index =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(
                    image_.width()) +
                static_cast<std::size_t>(x);

            TracePixel& pixel =
                trace_[index];

            const auto addChannel =
                [contribution](
                    std::uint16_t& destination,
                    int channel)
                {
                    const std::uint32_t scaled =
                        contribution *
                        static_cast<std::uint32_t>(
                            std::clamp(
                                channel,
                                0,
                                255)) /
                        255u;

                    destination =
                        static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(
                                65535u,
                                static_cast<std::uint32_t>(
                                    destination) +
                                scaled));
                };

            addChannel(
                pixel.red,
                red);

            addChannel(
                pixel.green,
                green);

            addChannel(
                pixel.blue,
                blue);
        }
    }
}

void WaveformRenderer::plotBeam(
    double x,
    double y,
    int intensity,
    int red,
    int green,
    int blue,
    int clipFirstX,
    int clipLastX)
{
    if (x < 0.0 ||
        x >= static_cast<double>(
            image_.width()))
    {
        return;
    }

    if (clipLastX < 0)
    {
        clipLastX = image_.width();
    }

    clipFirstX =
        std::clamp(
            clipFirstX,
            0,
            image_.width());

    clipLastX =
        std::clamp(
            clipLastX,
            clipFirstX,
            image_.width());

    const int x0 =
        static_cast<int>(
            std::floor(x));

    const int y0 =
        static_cast<int>(
            std::floor(y));

    const double fx =
        x -
        static_cast<double>(x0);

    const double fy =
        y -
        static_cast<double>(y0);

    for (int dy = 0;
        dy <= 1;
        ++dy)
    {
        const int destinationY =
            y0 + dy;

        if (destinationY < 0 ||
            destinationY >= image_.height())
        {
            continue;
        }

        const double wy =
            dy == 0
            ? 1.0 - fy
            : fy;

        for (int dx = 0;
            dx <= 1;
            ++dx)
        {
            const int destinationX =
                x0 + dx;

            if (destinationX < clipFirstX ||
                destinationX >= clipLastX)
            {
                continue;
            }

            const double wx =
                dx == 0
                ? 1.0 - fx
                : fx;

            const double weight =
                wx * wy;

            const std::uint32_t contribution =
                static_cast<std::uint32_t>(
                    weight *
                    static_cast<double>(
                        intensity));

            const std::size_t index =
                static_cast<std::size_t>(
                    destinationY) *
                static_cast<std::size_t>(
                    image_.width()) +
                static_cast<std::size_t>(
                    destinationX);

            TracePixel& pixel =
                trace_[index];

            const auto addChannel =
                [contribution](
                    std::uint16_t& destination,
                    int channel)
                {
                    const std::uint32_t scaled =
                        contribution *
                        static_cast<std::uint32_t>(
                            std::clamp(
                                channel,
                                0,
                                255)) /
                        255u;

                    destination =
                        static_cast<std::uint16_t>(
                            std::max<std::uint32_t>(
                                static_cast<std::uint32_t>(
                                    destination),
                                scaled));
                };

            addChannel(
                pixel.red,
                red);

            addChannel(
                pixel.green,
                green);

            addChannel(
                pixel.blue,
                blue);
        }
    }
}

void WaveformRenderer::setSelectedLine(
    int line)
{
    if (selectedLine_ == line)
    {
        return;
    }

    selectedLine_ = line;

    // A phosphor image belongs to exactly one selected source line.
    // Never feed the previous line into the first frame of the new line.
    clearScopephorFrames();

    // Also clear the legacy/current-frame trace buffers.
    std::fill(
        trace_.begin(),
        trace_.end(),
        TracePixel{});

    std::fill(
        chromaTrace_.begin(),
        chromaTrace_.end(),
        TracePixel{});
}

void WaveformRenderer::setPersistence(
    int persistence)
{
    persistence_ =
        std::clamp(
            persistence,
            0,
            200);
}

void WaveformRenderer::setCoreIntensity(
    int intensity)
{
    const int normalizedIntensity =
        std::clamp(
            intensity,
            0,
            100);

    // UI 100% is the accepted High-Q beam level, historically 200%.
    coreIntensity_ =
        normalizedIntensity * 2;
}

void WaveformRenderer::setGlow(
    int glow)
{
    const int uiGlow =
        std::clamp(
            glow,
            0,
            100);

    // Keep the UI consistent at 0..100, but use only the visually useful
    // waveform Beam Glow range internally.  UI 50 is therefore the default
    // effective glow of 10, while UI 100 reaches the deliberately wild 20.
    glow_ =
        (uiGlow * 20 + 50) /
        100;
}

const QImage& WaveformRenderer::image() const
{
    return image_;
}

const std::vector<float>& WaveformRenderer::visibleLumaVolts() const noexcept
{
    return sourceY_;
}

const std::vector<float>& WaveformRenderer::fullLumaVolts() const noexcept
{
    return fullLumaVolts_;
}

const WaveformRenderTimings& WaveformRenderer::renderTimings() const noexcept
{
    return renderTimings_;
}

double WaveformRenderer::traceBandwidthMHz() const
{
    const double captureSampleRateMHz =
        inputSampleClockHz_ / 1'000'000.0;

    return
        captureSampleRateMHz *
        static_cast<double>(image_.width()) /
        (
            static_cast<double>(inputSampleWidth_) *
            kPixelsPerCycleForTraceBW);
}

void WaveformRenderer::clearTrace()
{
    clearScopephorFrames();

    std::fill(
        trace_.begin(),
        trace_.end(),
        TracePixel{});

    std::fill(
        chromaTrace_.begin(),
        chromaTrace_.end(),
        TracePixel{});
}


void WaveformRenderer::setAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    if (aspectRatio_ == aspectRatio)
    {
        return;
    }

    aspectRatio_ = aspectRatio;

    clearTrace();
}