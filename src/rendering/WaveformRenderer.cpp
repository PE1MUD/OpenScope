#include "rendering/WaveformRenderer.h"
#include "ui/ViewportOverlay.h"
#include "processing/SignalReconstructor.h"
#include "standards/VideoStandard.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>
#include <QApplication>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

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

    // Cache the core width once per output-size change.  The hot trace loop
    // must not recalculate sqrt/clamp for every line segment.
    const double renderDimension =
        static_cast<double>(
            std::max(
                1,
                std::min(width, height)));

    const double beamScale =
        std::clamp(
            std::sqrt(
                renderDimension /
                576.0),
            1.0,
            1.90);

    constexpr double kReferenceCoreRadiusPx = 0.82;

    beamCoreRadiusPx_ =
        kReferenceCoreRadiusPx *
        beamScale;

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

QRectF WaveformRenderer::viewportRect() const
{
    const double widgetWidth =
        static_cast<double>(
            image_.width() - 1);

    const double widgetHeight =
        static_cast<double>(
            image_.height() - 1);

    double width =
        widgetWidth;

    double height =
        widgetHeight;

    if (fitAspectRatio_)
    {
        const double aspectRatio =
            OpenScopeSettings::aspectRatioValue(
                aspectRatio_);

        height =
            width /
            aspectRatio;

        if (height > widgetHeight)
        {
            height =
                widgetHeight;

            width =
                height *
                aspectRatio;
        }
    }

    width *= contentScaleX_;
    height *= contentScaleY_;

    const double left =
        (widgetWidth - width) *
        0.5;

    const double top =
        (widgetHeight - height) *
        0.5;

    return QRectF(
        left,
        top,
        width,
        height);
}

QRectF WaveformRenderer::scopeRect() const
{
    const QRectF viewport =
        viewportRect();

    const double left =
        viewport.left() +
        graticule_.leftInset(
            QApplication::font(),
            &image_,
            viewport.height());

    return QRectF(
        left,
        viewport.top(),
        viewport.right() - left,
        viewport.height());
}

QRectF WaveformRenderer::scaledScopeRect() const
{
    const QRectF scope =
        scopeRect();

    const QFont font =
        graticule_.labelFont(
            QApplication::font(),
            scope.height());

    const QFontMetricsF metrics(
        font,
        &image_);

    const double labelHeight =
        metrics.height();

    return QRectF(
        scope.left(),
        scope.top() + labelHeight * 0.5,
        scope.width(),
        scope.height() - labelHeight);
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

    clearOrFadeTrace();

    renderTimings_.persistenceUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    phaseTimer.restart();

    const std::size_t sourceWidth =
        static_cast<std::size_t>(
            frame.width);

    // Always reconstruct luminance to 4x the native line width.
    // X1 needs the reconstructed samples too: when the reconstructed
    // line is wider than the render canvas, the min/max reducer can
    // preserve high-frequency burst energy that would otherwise alias
    // away between display columns.
    const std::size_t reconstructedWidth =
        sourceWidth * 4u;

    const std::size_t sourceLineOffset =
        static_cast<std::size_t>(
            selectedLine_) *
        sourceWidth;

    singleLineSource_.resize(
        sourceWidth);

    singleLineReconstructed_.resize(
        reconstructedWidth);

    for (std::size_t x = 0;
        x < sourceWidth;
        ++x)
    {
        singleLineSource_[x] =
            static_cast<float>(
                frame.y[
                    sourceLineOffset +
                        x]);
    }

    const std::uint64_t resamplerGenerationBefore =
        singleLineReconstructor_.cacheGeneration();

    singleLineReconstructor_.resample(
        singleLineSource_,
        singleLineReconstructed_);

    renderTimings_.resamplerCacheRebuilt =
        singleLineReconstructor_.cacheGeneration() !=
        resamplerGenerationBefore;

    /*
     * Determine which part of the reconstructed line
     * is visible.
     *
     * X1:
     *     complete reconstructed 2880-sample line.
     *
     * X5/X10:
     *     the corresponding fraction of that same reconstructed line.
     */
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
                static_cast<double>(
                    maximumOffset));
    }

    sourceY_.resize(
        reconstructedViewWidth);

    fullLumaVolts_.resize(
        reconstructedWidth);

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
        const double y16 =
            static_cast<double>(
                singleLineReconstructed_[x]);

        const double y10 =
            y16 /
            64.0;

        fullLumaVolts_[x] =
            static_cast<float>(
                analog.blackVolts +
                (y10 - digitalLevels.yBlack) *
                voltsPerCode);
    }

    for (std::size_t x = 0;
        x < reconstructedViewWidth;
        ++x)
    {
        sourceY_[x] =
            fullLumaVolts_[
                reconstructedViewOffset + x];
    }    /*
     * U/V are still native 720-sample data.
     * Map the same visible interval from the
     * reconstructed coordinate system back into
     * the native source coordinate system.
     */
    std::size_t sourceViewWidth =
        sourceWidth;

    std::size_t sourceViewOffset = 0u;

    if (zoomFactor_ > 1)
    {
        sourceViewWidth =
            std::max<std::size_t>(
                sourceWidth /
                static_cast<std::size_t>(zoomFactor_),
                1u);

        const std::size_t maximumSourceOffset =
            sourceWidth -
            sourceViewWidth;

        const double normalisedPosition =
            reconstructedWidth > 1u
            ? static_cast<double>(
                reconstructedViewOffset) /
            static_cast<double>(
                reconstructedWidth -
                reconstructedViewWidth)
            : 0.0;

        sourceViewOffset =
            static_cast<std::size_t>(
                normalisedPosition *
                static_cast<double>(
                    maximumSourceOffset));
    }

    sourceU_.resize(
        sourceViewWidth);

    sourceV_.resize(
        sourceViewWidth);

    for (std::size_t x = 0;
        x < sourceViewWidth;
        ++x)
    {
        const std::size_t sourceX =
            std::min(
                sourceViewOffset + x,
                sourceWidth - 1u);

        const std::size_t sampleIndex =
            sourceLineOffset +
            sourceX;

        sourceU_[x] =
            static_cast<float>(
                frame.u[sampleIndex]);

        sourceV_[x] =
            static_cast<float>(
                frame.v[sampleIndex]);
    }

    /*
     * Resample only the visible interval to the
     * actual display width.
     */
    if (sourceY_.size() >
        displayY_.size())
    {
        resampleMinMax(
            sourceY_,
            displayYMin_,
            displayYMax_);

        resampleLinear(
            sourceY_,
            displayY_);
    }
    else
    {
        resampleLinear(
            sourceY_,
            displayY_);
    }

    resampleLinear(
        sourceU_,
        displayU_);

    resampleLinear(
        sourceV_,
        displayV_);

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

    const QRectF scope =
        scaledScopeRect();

    const double voltsToPixels =
        scope.height() /
        analog.graticuleMaxVolts;

    const double chromaXScale =
        scope.width() /
        static_cast<double>(
            displayWidth - 1);

    for (int x = 0;
        x < displayWidth;
        ++x)
    {
        const std::size_t index =
            static_cast<std::size_t>(
                x);

        const double plotX =
            scope.left() +
            static_cast<double>(x) *
            chromaXScale;

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
            chromaRed[index] = red;
            chromaGreen[index] = green;
            chromaBlue[index] = blue;
        }

        //if (maximum > 0.0 &&
        //    spread > 0.0)
        //{
        //    plotBeam(
        //        plotX,
        //        plotY - spread,
        //        kChromaBeamIntensity,
        //        red,
        //        green,
        //        blue);

        //    plotBeam(
        //        plotX,
        //        plotY + spread,
        //        kChromaBeamIntensity,
        //        red,
        //        green,
        //        blue);
        //}
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

    // Large screen waveforms are split into narrow vertical stripes.  Each
    // stripe exclusively owns its destination X range; the luminance beam
    // may inspect a small source halo, but writes are clipped to the stripe.
    // That makes the jobs race-free without private full-frame buffers.
    constexpr int kParallelTraceStripeWidth = 128;

    const int traceWidth =
        image_.width();

    const int traceStripeWidth =
        traceJobExecutor_
        ? kParallelTraceStripeWidth
        : std::max(1, traceWidth);

    const std::size_t traceJobCount =
        traceWidth > 0
        ? static_cast<std::size_t>(
            (traceWidth +
                traceStripeWidth - 1) /
            traceStripeWidth)
        : 0u;

    const auto renderTraceStripe =
        [this,
        &scope,
        &chromaUpperY,
        &chromaLowerY,
        &chromaRed,
        &chromaGreen,
        &chromaBlue,
        displayWidth,
        firstScreenX,
        lastScreenXExclusive,
        traceStripeWidth](std::size_t jobIndex)
        {
            const int stripeFirstX =
                std::min(
                    static_cast<int>(jobIndex) *
                    traceStripeWidth,
                    image_.width());

            const int stripeLastX =
                std::min(
                    stripeFirstX +
                    traceStripeWidth,
                    image_.width());

            if (stripeFirstX >= stripeLastX)
            {
                return;
            }

            plotLuminanceTraceRange(
                stripeFirstX,
                stripeLastX);

            const int chromaFirstX =
                std::max(
                    stripeFirstX,
                    firstScreenX);

            const int chromaLastX =
                std::min(
                    stripeLastX,
                    lastScreenXExclusive);

            for (int screenX = chromaFirstX;
                screenX < chromaLastX;
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

                for (int y = firstY;
                    y <= lastY;
                    ++y)
                {
                    addChromaFillPixel(
                        screenX,
                        y,
                        chromaRed[index],
                        chromaGreen[index],
                        chromaBlue[index],
                        chromaFillIntensity_);
                }
            }
        };

    // Worker dispatch has a measurable fixed cost on small viewports.
    // Keep those renders scalar and only ask the idle display worker to
    // assist once the PC waveform canvas is large enough to amortise it.
    constexpr std::int64_t kParallelTraceMinimumPixels = 2'000'000;

    const std::int64_t traceCanvasPixels =
        static_cast<std::int64_t>(image_.width()) *
        static_cast<std::int64_t>(image_.height());

    const bool useParallelTrace =
        traceCanvasPixels >= kParallelTraceMinimumPixels &&
        traceJobCount > 1u &&
        static_cast<bool>(traceJobExecutor_);

    renderTimings_.traceParallel = useParallelTrace;
    renderTimings_.traceJobCount =
        static_cast<std::uint32_t>(traceJobCount);

    const std::uint64_t tracePrepUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    QElapsedTimer rasterTimer;
    rasterTimer.start();

    if (useParallelTrace)
    {
        traceJobExecutor_(
            traceJobCount,
            renderTraceStripe);
    }
    else
    {
        for (std::size_t jobIndex = 0;
            jobIndex < traceJobCount;
            ++jobIndex)
        {
            renderTraceStripe(jobIndex);
        }
    }
    renderTimings_.traceRasterUs =
        static_cast<std::uint64_t>(
            rasterTimer.nsecsElapsed() / 1000);
    renderTimings_.traceUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);
    renderTimings_.tracePrepUs =
        std::min(
            tracePrepUs,
            renderTimings_.traceUs);

    phaseTimer.restart();
    composeTraceImage();
    renderTimings_.composeUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    phaseTimer.restart();
    if (selectedLine_ >= 0 && glow_ > 0)
    {
        const GlowWorkload workload =
            applyHalfResolutionWhiteGlow(image_, glow_);

        renderTimings_.glowDirtyTiles = workload.dirtyTiles;
        renderTimings_.glowTotalTiles = workload.totalTiles;
        renderTimings_.glowHorizontalPass1Tiles = workload.horizontalPass1Tiles;
        renderTimings_.glowVerticalPass1Tiles = workload.verticalPass1Tiles;
        renderTimings_.glowHorizontalPass2Tiles = workload.horizontalPass2Tiles;
        renderTimings_.glowVerticalPass2Tiles = workload.verticalPass2Tiles;
        renderTimings_.glowActiveX = workload.activeX;
        renderTimings_.glowActiveY = workload.activeY;
        renderTimings_.glowActiveWidth = workload.activeWidth;
        renderTimings_.glowActiveHeight = workload.activeHeight;
    }
    renderTimings_.glowUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);

    phaseTimer.restart();
    QPainter painter(&image_);

    graticule_.draw(
        painter,
        scope,
        VideoStandard::pal625());

    // The trace is the measurement, so its core must win over the
    // graticule at intersections.  Glow is intentionally left below the
    // graticule; only the actual beam/core is repainted on top.
    painter.end();

    const int firstX =
        std::clamp(
            static_cast<int>(std::floor(scope.left())),
            0,
            image_.width() - 1);
    const int lastX =
        std::clamp(
            static_cast<int>(std::ceil(scope.right())),
            firstX,
            image_.width() - 1);
    const int firstY =
        std::clamp(
            static_cast<int>(std::floor(scope.top())),
            0,
            image_.height() - 1);
    const int lastY =
        std::clamp(
            static_cast<int>(std::ceil(scope.bottom())),
            firstY,
            image_.height() - 1);

    for (int y = firstY; y <= lastY; ++y)
    {
        auto* destination =
            reinterpret_cast<QRgb*>(
                image_.scanLine(y));

        for (int x = firstX; x <= lastX; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(image_.width()) +
                static_cast<std::size_t>(x);

            const TracePixel& lumaPixel =
                trace_[index];
            const TracePixel& chromaPixel =
                chromaTrace_[index];

            const bool hasLuma =
                lumaPixel.red != 0 ||
                lumaPixel.green != 0 ||
                lumaPixel.blue != 0;
            const bool hasChroma =
                chromaPixel.red != 0 ||
                chromaPixel.green != 0 ||
                chromaPixel.blue != 0;

            if (!hasLuma && !hasChroma)
            {
                continue;
            }

            if (settings_.color)
            {
                const auto combinedChannel =
                    [this](
                        std::uint16_t luma,
                        std::uint16_t chroma)
                    {
                        const std::uint32_t combined =
                            std::min<std::uint32_t>(
                                65535u,
                                static_cast<std::uint32_t>(luma) +
                                static_cast<std::uint32_t>(chroma));

                        return displayLut_[combined];
                    };

                destination[x] =
                    qRgb(
                        combinedChannel(
                            lumaPixel.red,
                            chromaPixel.red),
                        combinedChannel(
                            lumaPixel.green,
                            chromaPixel.green),
                        combinedChannel(
                            lumaPixel.blue,
                            chromaPixel.blue));
            }
            else
            {
                const std::uint32_t luma =
                    std::max({
                        static_cast<std::uint32_t>(lumaPixel.red),
                        static_cast<std::uint32_t>(lumaPixel.green),
                        static_cast<std::uint32_t>(lumaPixel.blue)
                    });

                const std::uint32_t chroma =
                    std::max({
                        static_cast<std::uint32_t>(chromaPixel.red),
                        static_cast<std::uint32_t>(chromaPixel.green),
                        static_cast<std::uint32_t>(chromaPixel.blue)
                    });

                const std::uint32_t combined =
                    std::min<std::uint32_t>(
                        65535u,
                        luma + chroma);

                const int value =
                    displayLut_[combined];

                destination[x] =
                    qRgb(value, value, value);
            }
        }
    }

    QPainter overlayPainter(&image_);
    drawLineInfoOverlay(overlayPainter);
    renderTimings_.overlayUs =
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed() / 1000);
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

    const QRectF cardRect(
        scope.right() - cardSize.width(),
        scope.bottom() - lineGap - cardSize.height(),
        cardSize.width(),
        cardSize.height());

    ViewportOverlay::drawInfoCard(
        painter,
        cardRect,
        rows,
        referenceHeight,
        lineInfoOverlayPalOutput_);
}


void WaveformRenderer::plotLuminanceTraceRange(
    int firstPixelX,
    int lastPixelX)
{
    const int width =
        image_.width();

    const int height =
        image_.height();

    const QRectF scope =
        scaledScopeRect();

    if (width < 2 ||
        displayY_.size() <
        static_cast<std::size_t>(
            width))
    {
        return;
    }

    firstPixelX =
        std::clamp(
            firstPixelX,
            0,
            width);

    lastPixelX =
        std::clamp(
            lastPixelX,
            firstPixelX,
            width);

    if (firstPixelX >= lastPixelX)
    {
        return;
    }

    const auto analog =
        analogLevels(
            VideoColorStandard::Rec601_625);

    const double voltsToPixels =
        scope.height() /
        analog.graticuleMaxVolts;

    const auto sampleToPlotY =
        [scope,
        voltsToPixels](double volts)
        {
            return
                scope.bottom() -
                volts *
                voltsToPixels;
        };

    const auto sampleCenter =
        [this, &sampleToPlotY](int x)
        {
            x =
                std::clamp(
                    x,
                    0,
                    image_.width() - 1);

            return
                sampleToPlotY(
                    static_cast<double>(
                        displayY_[
                            static_cast<std::size_t>(
                                x)]));
        };

    const auto sampleZoomed =
        [this, &sampleToPlotY](int x)
        {
            x =
                std::clamp(
                    x,
                    0,
                    static_cast<int>(
                        sourceY_.size()) - 1);

            return
                sampleToPlotY(
                    static_cast<double>(
                        sourceY_[
                            static_cast<std::size_t>(
                                x)]));
        };

    const auto plotSmoothCurveRange =
        [this,
        scope,
        firstPixelX,
        lastPixelX](
            const auto& sampleY,
            int sampleCount,
            int intensity)
        {
            if (sampleCount < 2)
            {
                return;
            }

            constexpr double targetStepPixels = 0.5;

            const double curveXScale =
                scope.width() /
                static_cast<double>(
                    sampleCount - 1);

            if (curveXScale <= 0.0)
            {
                return;
            }

            // Include enough source segments to cover the beam core at the
            // stripe edge. Writes are still clipped to this stripe, so two
            // workers never touch the same TracePixel.
            const double halo =
                beamCoreRadiusPx_ + 2.0;

            const int firstSegment =
                std::clamp(
                    static_cast<int>(
                        std::floor(
                            (static_cast<double>(firstPixelX) -
                                halo - scope.left()) /
                            curveXScale)) - 1,
                    0,
                    sampleCount - 2);

            const int lastSegment =
                std::clamp(
                    static_cast<int>(
                        std::ceil(
                            (static_cast<double>(lastPixelX) +
                                halo - scope.left()) /
                            curveXScale)) + 1,
                    0,
                    sampleCount - 2);

            for (int x = firstSegment;
                x <= lastSegment;
                ++x)
            {
                const double p0 =
                    sampleY(x - 1);

                const double p1 =
                    sampleY(x);

                const double p2 =
                    sampleY(x + 1);

                const double p3 =
                    sampleY(x + 2);

                const double distance =
                    std::hypot(
                        1.0,
                        p2 - p1);

                const int subdivisions =
                    std::clamp(
                        static_cast<int>(
                            std::ceil(
                                distance /
                                targetStepPixels)),
                        1,
                        256);

                double previousX =
                    scope.left() +
                    static_cast<double>(x) *
                    curveXScale;

                double previousY =
                    p1;

                for (int step = 0;
                    step <= subdivisions;
                    ++step)
                {
                    const double t =
                        static_cast<double>(step) /
                        static_cast<double>(
                            subdivisions);

                    const double t2 =
                        t * t;

                    const double t3 =
                        t2 * t;

                    const double y =
                        0.5 *
                        (
                            2.0 * p1 +
                            (-p0 + p2) * t +
                            (2.0 * p0 -
                                5.0 * p1 +
                                4.0 * p2 -
                                p3) * t2 +
                            (-p0 +
                                3.0 * p1 -
                                3.0 * p2 +
                                p3) * t3
                            );

                    const double plotX =
                        scope.left() +
                        (
                            static_cast<double>(x) +
                            t
                            ) *
                        curveXScale;

                    if (step > 0)
                    {
                        plotSegment(
                            previousX,
                            previousY,
                            plotX,
                            y,
                            intensity,
                            255,
                            255,
                            255,
                            firstPixelX,
                            lastPixelX);
                    }

                    previousX = plotX;
                    previousY = y;
                }
            }
        };

    if (zoomFactor_ > 1)
    {
        plotSmoothCurveRange(
            sampleZoomed,
            static_cast<int>(
                sourceY_.size()),
            kLuminanceBeamIntensity);
    }
    else
    {
        plotSmoothCurveRange(
            sampleCenter,
            width,
            kLuminanceBeamIntensity);
    }

    if (sourceY_.size() <=
        displayY_.size())
    {
        return;
    }

    constexpr double kMinMaxFillThresholdPixels = 3.0;

    const double xScale =
        scope.width() /
        static_cast<double>(
            width - 1);

    if (xScale <= 0.0)
    {
        return;
    }

    const int firstDisplayX =
        std::clamp(
            static_cast<int>(
                std::floor(
                    (static_cast<double>(firstPixelX) -
                        2.0 - scope.left()) /
                    xScale)),
            0,
            width - 1);

    const int lastDisplayX =
        std::clamp(
            static_cast<int>(
                std::ceil(
                    (static_cast<double>(lastPixelX) +
                        2.0 - scope.left()) /
                    xScale)),
            0,
            width - 1);

    for (int x = firstDisplayX;
        x <= lastDisplayX;
        ++x)
    {
        const std::size_t index =
            static_cast<std::size_t>(x);

        const double upperY =
            sampleToPlotY(
                static_cast<double>(
                    displayYMax_[index]));

        const double lowerY =
            sampleToPlotY(
                static_cast<double>(
                    displayYMin_[index]));

        const double spanPixels =
            std::abs(
                lowerY - upperY);

        if (spanPixels <
            kMinMaxFillThresholdPixels)
        {
            continue;
        }

        const int firstY =
            std::clamp(
                static_cast<int>(
                    std::ceil(
                        std::min(
                            upperY,
                            lowerY))),
                0,
                height - 1);

        const int lastY =
            std::clamp(
                static_cast<int>(
                    std::floor(
                        std::max(
                            upperY,
                            lowerY))),
                0,
                height - 1);

        for (int y = firstY;
            y <= lastY;
            ++y)
        {
            plotBeam(
                scope.left() +
                static_cast<double>(x) *
                xScale,
                static_cast<double>(y),
                kLuminanceBeamIntensity,
                255,
                255,
                255,
                firstPixelX,
                lastPixelX);
        }
    }
}

void WaveformRenderer::composeTraceImage()
{
    const int width =
        image_.width();

    const int height =
        image_.height();

    for (int y = 0;
        y < height;
        ++y)
    {
        auto* destination =
            reinterpret_cast<QRgb*>(
                image_.scanLine(y));

        for (int x = 0;
            x < width;
            ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            const TracePixel& lumaPixel =
                trace_[index];

            const TracePixel& chromaPixel =
                chromaTrace_[index];

            if (settings_.color)
            {
                const auto combinedChannel =
                    [this](
                        std::uint16_t luma,
                        std::uint16_t chroma)
                    {
                        const std::uint32_t combined =
                            std::min<std::uint32_t>(
                                65535u,
                                static_cast<std::uint32_t>(luma) +
                                static_cast<std::uint32_t>(chroma));

                        return
                            displayLut_[combined];
                    };

                destination[x] =
                    qRgb(
                        combinedChannel(
                            lumaPixel.red,
                            chromaPixel.red),
                        combinedChannel(
                            lumaPixel.green,
                            chromaPixel.green),
                        combinedChannel(
                            lumaPixel.blue,
                            chromaPixel.blue));
            }
            else
            {
                const std::uint32_t luma =
                    std::max({
                        static_cast<std::uint32_t>(
                            lumaPixel.red),
                        static_cast<std::uint32_t>(
                            lumaPixel.green),
                        static_cast<std::uint32_t>(
                            lumaPixel.blue)
                        });

                const std::uint32_t chroma =
                    std::max({
                        static_cast<std::uint32_t>(
                            chromaPixel.red),
                        static_cast<std::uint32_t>(
                            chromaPixel.green),
                        static_cast<std::uint32_t>(
                            chromaPixel.blue)
                        });

                const std::uint32_t combined =
                    std::min<std::uint32_t>(
                        65535u,
                        luma + chroma);

                const int value =
                    displayLut_[combined];

                destination[x] =
                    qRgb(
                        value,
                        value,
                        value);
            }
        }
    }
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

            const std::uint32_t intensity =
                std::min<std::uint32_t>(
                    255u,
                    static_cast<std::uint32_t>(
                        temporalDensity / 256.0f) *
                    8u);

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

    // A trace accumulated for another source line must never survive a
    // line change.  This is especially visible with persistence enabled
    // and when switching between matrix and fullscreen views.
    clearTrace();
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

void WaveformRenderer::setGlow(
    int glow)
{
    glow_ =
        std::clamp(
            glow,
            0,
            100);

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