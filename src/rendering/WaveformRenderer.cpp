#include "rendering/WaveformRenderer.h"
#include "processing/SignalReconstructor.h"

#include <QDebug>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace
{
    constexpr double kMaximumSampleValue = 65535.0;
    constexpr double kNeutralChroma = 32768.0;
    constexpr double kChromaNoiseThreshold =
        0.03 * kNeutralChroma;
    constexpr double kChromaEnvelopeScale = 0.18;

    constexpr int kLuminanceBeamIntensity = 768;
    constexpr int kChromaBeamIntensity = 768;
    constexpr std::uint32_t kConnectorIntensity = 160u;
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

WaveformRenderer::WaveformRenderer()
    : image_(
        720,
        576,
        QImage::Format_RGB32)
    , hits_(
        720u * 576u,
        0u)
    , trace_(
        720u * 576u)
    , displayY_(720u)
    , displayU_(720u)
    , displayV_(720u)
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
        std::clamp(
            height,
            1,
            576);

    qDebug()
        << "Waveform size"
        << width
        << height;

    if (image_.width() == width &&
        image_.height() == height)
    {
        return;
    }

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

    trace_.assign(
        pixelCount,
        TracePixel{});

    displayY_.resize(
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
    zoomed_ = zoomed;
}

void WaveformRenderer::setScrollPosition(
    double position)
{
    scrollPosition_ =
        std::clamp(
            position,
            0.0,
            1.0);
}

void WaveformRenderer::analyze(
    const Yuv444Frame& frame)
{
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
        image_.fill(Qt::black);
        return;
    }

    renderAllLines(frame);
}

void WaveformRenderer::analyze(
    const Yuv444Frame& frame,
    const ReconstructedLumaFrame& reconstructedLuma)
{
    if (reconstructedLuma.width <= 0 ||
        reconstructedLuma.height <= 0 ||
        reconstructedLuma.y.empty())
    {
        image_.fill(Qt::black);
        return;
    }

    if (selectedLine_ >= 0 &&
        selectedLine_ < reconstructedLuma.height)
    {
        renderSingleLine(
            frame,
            reconstructedLuma);

        return;
    }

    analyze(frame);
}

void WaveformRenderer::clearOrFadeTrace()
{
    if (persistence_ == 0)
    {
        std::fill(
            trace_.begin(),
            trace_.end(),
            TracePixel{});

        return;
    }

    const std::uint32_t persistence =
        static_cast<std::uint32_t>(
            persistence_);

    for (TracePixel& pixel : trace_)
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
}

void WaveformRenderer::renderSingleLine(
    const Yuv444Frame& frame,
    const ReconstructedLumaFrame& reconstructedLuma)
{
    clearOrFadeTrace();

    const std::size_t sourceWidth =
        static_cast<std::size_t>(
            frame.width);

    const std::size_t reconstructedWidth =
        static_cast<std::size_t>(
            reconstructedLuma.width);

    const std::size_t sourceLineOffset =
        static_cast<std::size_t>(
            selectedLine_) *
        sourceWidth;

    const std::size_t reconstructedLineOffset =
        static_cast<std::size_t>(
            selectedLine_) *
        reconstructedWidth;

    /*
     * Determine which part of the reconstructed line
     * is visible.
     *
     * X1:
     *     complete reconstructed line.
     *
     * X10:
     *     one tenth of the reconstructed line,
     *     continuously movable with scrollPosition_.
     */
    std::size_t reconstructedViewWidth =
        reconstructedWidth;

    std::size_t reconstructedViewOffset = 0u;

    if (zoomed_)
    {
        reconstructedViewWidth =
            std::max<std::size_t>(
                reconstructedWidth / 10u,
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

    /*
     * Luma comes from the permanently reconstructed
     * high-resolution line.
     */
    sourceY_.resize(
        reconstructedViewWidth);

    for (std::size_t x = 0;
        x < reconstructedViewWidth;
        ++x)
    {
        sourceY_[x] =
            static_cast<float>(
                reconstructedLuma.y[
                    reconstructedLineOffset +
                        reconstructedViewOffset +
                        x]);
    }

    /*
     * U/V are still native 720-sample data.
     * Map the same visible interval from the
     * reconstructed coordinate system back into
     * the native source coordinate system.
     */
    std::size_t sourceViewWidth =
        sourceWidth;

    std::size_t sourceViewOffset = 0u;

    if (zoomed_)
    {
        sourceViewWidth =
            std::max<std::size_t>(
                sourceWidth / 10u,
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
    resampleLinear(
        sourceY_,
        displayY_);

    resampleLinear(
        sourceU_,
        displayU_);

    resampleLinear(
        sourceV_,
        displayV_);

    const int displayWidth =
        image_.width();

    const int displayHeight =
        image_.height();

    for (int x = 0;
        x < displayWidth;
        ++x)
    {
        const std::size_t index =
            static_cast<std::size_t>(
                x);

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

        double chromaU =
            uValue -
            kNeutralChroma;

        double chromaV =
            vValue -
            kNeutralChroma;

        const double chromaMagnitude =
            std::hypot(
                chromaU,
                chromaV);

        if (chromaMagnitude <
            kChromaNoiseThreshold)
        {
            chromaU = 0.0;
            chromaV = 0.0;
        }

        const double plotY =
            static_cast<double>(
                displayHeight - 1) -
            yValue *
            static_cast<double>(
                displayHeight - 1) /
            kMaximumSampleValue;

        const double spread =
            std::hypot(
                chromaU,
                chromaV) /
            kNeutralChroma *
            static_cast<double>(
                displayHeight - 1) *
            kChromaEnvelopeScale;

        double redValue =
            1.402 * chromaV;

        double greenValue =
            -0.344136 * chromaU -
            0.714136 * chromaV;

        double blueValue =
            1.772 * chromaU;

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

        if (maximum > 0.0 &&
            spread > 0.0)
        {
            plotBeam(
                x,
                plotY - spread,
                kChromaBeamIntensity,
                red,
                green,
                blue);

            plotBeam(
                x,
                plotY + spread,
                kChromaBeamIntensity,
                red,
                green,
                blue);
        }
    }

    plotLuminanceTrace();
    composeTraceImage();
}

void WaveformRenderer::plotLuminanceTrace()
{
    const int width =
        image_.width();

    const int height =
        image_.height();

    if (width < 2 ||
        displayY_.size() <
        static_cast<std::size_t>(
            width))
    {
        return;
    }

    auto sampleY =
        [this, height](int x)
        {
            x =
                std::clamp(
                    x,
                    0,
                    image_.width() - 1);

            const double yValue =
                std::clamp(
                    static_cast<double>(
                        displayY_[
                            static_cast<std::size_t>(
                                x)]),
                    0.0,
                    kMaximumSampleValue);

            return
                static_cast<double>(
                    height - 1) -
                yValue *
                static_cast<double>(
                    height - 1) /
                kMaximumSampleValue;
        };

    constexpr double targetStepPixels = 0.5;

    for (int x = 0;
        x < width - 1;
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
                static_cast<double>(x) +
                t;

            plotBeam(
                plotX,
                y,
                kLuminanceBeamIntensity,
                0,
                255,
                0);
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

            const TracePixel& pixel =
                trace_[index];

            destination[x] =
                qRgb(
                    displayLut_[pixel.red],
                    displayLut_[pixel.green],
                    displayLut_[pixel.blue]);
        }
    }
}

void WaveformRenderer::renderAllLines(
    const Yuv444Frame& frame)
{
    std::fill(
        hits_.begin(),
        hits_.end(),
        0u);

    const int displayWidth =
        image_.width();

    const int displayHeight =
        image_.height();

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
            const int displayX =
                std::clamp(
                    sourceX *
                    displayWidth /
                    frame.width,
                    0,
                    displayWidth - 1);

            const std::uint16_t y16 =
                frame.y[
                    lineOffset +
                        static_cast<std::size_t>(
                            sourceX)];

            const std::uint32_t scaledY =
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(
                        y16) *
                    static_cast<std::uint64_t>(
                        (displayHeight - 1) << 8) /
                    65535u);

            const int plotY =
                displayHeight - 1 -
                static_cast<int>(
                    scaledY >> 8);

            const std::uint32_t fraction =
                scaledY & 0xffu;

            const std::size_t currentIndex =
                static_cast<std::size_t>(
                    plotY) *
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
                        static_cast<std::size_t>(
                            y) *
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
            const std::uint32_t hit =
                hits_[
                    static_cast<std::size_t>(
                        y) *
                        static_cast<std::size_t>(
                            displayWidth) +
                        static_cast<std::size_t>(
                            x)];

            const int green =
                std::min(
                    255,
                    static_cast<int>(
                        hit >> 8) * 8);

            destination[x] =
                qRgb(
                    0,
                    green,
                    0);
        }
    }
}

void WaveformRenderer::plotBeam(
    double x,
    double y,
    int intensity,
    int red,
    int green,
    int blue)
{
    if (x < 0.0 ||
        x >= static_cast<double>(
            image_.width()))
    {
        return;
    }

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

            if (destinationX < 0 ||
                destinationX >= image_.width())
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
    selectedLine_ = line;
}

void WaveformRenderer::setPersistence(
    int persistence)
{
    persistence_ =
        std::clamp(
            persistence,
            0,
            255);
}

const QImage& WaveformRenderer::image() const
{
    return image_;
}

double WaveformRenderer::traceBandwidthMHz() const
{
    constexpr double captureSampleRateMHz = 13.5;
    constexpr double captureSamplesPerLine = 720.0;

    return
        captureSampleRateMHz *
        static_cast<double>(
            image_.width()) /
        (
            captureSamplesPerLine *
            kPixelsPerCycleForTraceBW);
}