#include "rendering/WaveformRenderer.h"
#include "processing/SignalReconstructor.h"
//#include <QElapsedTimer>
#include <QDebug>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
constexpr double kMaximumSampleValue = 65535.0;
constexpr double kNeutralChroma = 32768.0;
constexpr double kChromaNoiseThreshold = 0.03 * kNeutralChroma;
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
        std::fill(output.begin(), output.end(), 0.0f);
        return;
    }

    if (input.size() == 1)
    {
        std::fill(output.begin(), output.end(), input.front());
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
            (static_cast<float>(outputIndex) + 0.5f) * scale - 0.5f;

        const int leftIndex = std::clamp(
            static_cast<int>(std::floor(sourcePosition)),
            0,
            static_cast<int>(input.size()) - 1);

        const int rightIndex = std::min(
            leftIndex + 1,
            static_cast<int>(input.size()) - 1);

        const float fraction = std::clamp(
            sourcePosition - static_cast<float>(leftIndex),
            0.0f,
            1.0f);

        const float left =
            input[static_cast<std::size_t>(leftIndex)];

        const float right =
            input[static_cast<std::size_t>(rightIndex)];

        output[outputIndex] =
            left + (right - left) * fraction;
    }
}
WaveformRenderer::WaveformRenderer()
    : image_(720, 576, QImage::Format_RGB32)
    , hits_(720u * 576u, 0u)
    , trace_(720u * 576u)
    , displayY_(720u)
    , displayU_(720u)
    , displayV_(720u)
{
    for (std::size_t i = 0; i < displayLut_.size(); ++i)
    {
        displayLut_[i] = static_cast<std::uint8_t>(
            std::min<std::size_t>(i, 255u));
    }

    image_.fill(Qt::black);
}

void WaveformRenderer::setOutputSize(int width, int height)
{
    width = std::max(width, 1);
    height = std::clamp(height, 1, 576);
    qDebug()
        << "Waveform size"
        << width
        << height;
    if (image_.width() == width && image_.height() == height)
    {
        return;
    }

    image_ = QImage(width, height, QImage::Format_RGB32);
    image_.fill(Qt::black);

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    hits_.assign(pixelCount, 0u);
    trace_.assign(pixelCount, TracePixel{});

    displayY_.resize(static_cast<std::size_t>(width));
    displayU_.resize(static_cast<std::size_t>(width));
    displayV_.resize(static_cast<std::size_t>(width));
}

void WaveformRenderer::analyze(const Yuv444Frame& frame)
{
    const std::size_t requiredSamples =
        frame.width > 0 && frame.height > 0
        ? static_cast<std::size_t>(frame.width) *
            static_cast<std::size_t>(frame.height)
        : 0u;

    if (requiredSamples == 0u ||
        frame.y.size() < requiredSamples ||
        frame.u.size() < requiredSamples ||
        frame.v.size() < requiredSamples)
    {
        image_.fill(Qt::black);
        return;
    }

    if (selectedLine_ >= 0 && selectedLine_ < frame.height)
    {
        renderSingleLine(frame);
        return;
    }

    renderAllLines(frame);
}

void WaveformRenderer::clearOrFadeTrace()
{
    if (persistence_ == 0)
    {
        std::fill(trace_.begin(), trace_.end(), TracePixel{});
        return;
    }

    const std::uint32_t persistence =
        static_cast<std::uint32_t>(persistence_);

    for (TracePixel& pixel : trace_)
    {
        pixel.red = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(pixel.red) * persistence) >> 8);
        pixel.green = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(pixel.green) * persistence) >> 8);
        pixel.blue = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(pixel.blue) * persistence) >> 8);
    }
}

void WaveformRenderer::renderSingleLine(
    const Yuv444Frame& frame)
{
    clearOrFadeTrace();

    const std::size_t sourceWidth =
        static_cast<std::size_t>(frame.width);
    const std::size_t lineOffset =
        static_cast<std::size_t>(selectedLine_) * sourceWidth;

    sourceY_.resize(sourceWidth);
    sourceU_.resize(sourceWidth);
    sourceV_.resize(sourceWidth);

    for (std::size_t x = 0; x < sourceWidth; ++x)
    {
        const std::size_t sampleIndex = lineOffset + x;
        sourceY_[x] = static_cast<float>(frame.y[sampleIndex]);
        sourceU_[x] = static_cast<float>(frame.u[sampleIndex]);
        sourceV_[x] = static_cast<float>(frame.v[sampleIndex]);
    }

    //lineResampler_.resample(sourceY_, displayY_);
    //lineResampler_.resample(sourceU_, displayU_);
    //lineResampler_.resample(sourceV_, displayV_);
    lineResampler_.resample(sourceY_, displayY_);
    resampleLinear(sourceU_, displayU_);
    resampleLinear(sourceV_, displayV_);

    const int displayWidth = image_.width();
    const int displayHeight = image_.height();

    double previousPlotY = 0.0;
    bool havePreviousPlotY = false;

    for (int x = 0; x < displayWidth; ++x)
    {
        const std::size_t index = static_cast<std::size_t>(x);

        const double yValue = std::clamp(
            static_cast<double>(displayY_[index]),
            0.0,
            kMaximumSampleValue);
        const double uValue = std::clamp(
            static_cast<double>(displayU_[index]),
            0.0,
            kMaximumSampleValue);
        const double vValue = std::clamp(
            static_cast<double>(displayV_[index]),
            0.0,
            kMaximumSampleValue);

        double chromaU = uValue - kNeutralChroma;
        double chromaV = vValue - kNeutralChroma;

        const double chromaMagnitude = std::hypot(chromaU, chromaV);
        if (chromaMagnitude < kChromaNoiseThreshold)
        {
            chromaU = 0.0;
            chromaV = 0.0;
        }

        const double plotY =
            static_cast<double>(displayHeight - 1) -
            yValue * static_cast<double>(displayHeight - 1) /
                kMaximumSampleValue;

        if (havePreviousPlotY)
        {
            const int firstY = std::max(
                0,
                static_cast<int>(std::ceil(
                    std::min(previousPlotY, plotY))));
            const int lastY = std::min(
                displayHeight - 1,
                static_cast<int>(std::floor(
                    std::max(previousPlotY, plotY))));

            for (int connectorY = firstY;
                connectorY <= lastY;
                ++connectorY)
            {
                const std::size_t connectorIndex =
                    static_cast<std::size_t>(connectorY) *
                        static_cast<std::size_t>(displayWidth) +
                    index;

                TracePixel& pixel = trace_[connectorIndex];
                pixel.green = static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(
                        65535u,
                        static_cast<std::uint32_t>(pixel.green) +
                            kConnectorIntensity));
            }
        }

        const double spread =
            std::hypot(chromaU, chromaV) /
            kNeutralChroma *
            static_cast<double>(displayHeight - 1) *
            kChromaEnvelopeScale;

        double redValue = 1.402 * chromaV;
        double greenValue =
            -0.344136 * chromaU - 0.714136 * chromaV;
        double blueValue = 1.772 * chromaU;

        const double minimum =
            std::min({ redValue, greenValue, blueValue });
        redValue -= minimum;
        greenValue -= minimum;
        blueValue -= minimum;

        const double maximum =
            std::max({ redValue, greenValue, blueValue });

        int red = 0;
        int green = 0;
        int blue = 0;

        if (maximum > 0.0)
        {
            red = static_cast<int>(std::clamp(
                redValue * 255.0 / maximum, 0.0, 255.0));
            green = static_cast<int>(std::clamp(
                greenValue * 255.0 / maximum, 0.0, 255.0));
            blue = static_cast<int>(std::clamp(
                blueValue * 255.0 / maximum, 0.0, 255.0));
        }

        plotBeam(
            x,
            plotY,
            kLuminanceBeamIntensity,
            0,
            255,
            0);

        if (maximum > 0.0 && spread > 0.0)
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

        previousPlotY = plotY;
        havePreviousPlotY = true;
    }

    composeTraceImage();
}

void WaveformRenderer::composeTraceImage()
{
    const int width = image_.width();
    const int height = image_.height();

    for (int y = 0; y < height; ++y)
    {
        auto* destination =
            reinterpret_cast<QRgb*>(image_.scanLine(y));

        for (int x = 0; x < width; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            const TracePixel& pixel = trace_[index];
            destination[x] = qRgb(
                displayLut_[pixel.red],
                displayLut_[pixel.green],
                displayLut_[pixel.blue]);
        }
    }
}

void WaveformRenderer::renderAllLines(const Yuv444Frame& frame)
{
    std::fill(hits_.begin(), hits_.end(), 0u);

    const int displayWidth = image_.width();
    const int displayHeight = image_.height();

    for (int line = 0; line < frame.height; ++line)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(line) *
            static_cast<std::size_t>(frame.width);

        int previousPlotY = -1;

        for (int sourceX = 0; sourceX < frame.width; ++sourceX)
        {
            const int displayX = std::clamp(
                sourceX * displayWidth / frame.width,
                0,
                displayWidth - 1);

            const std::uint16_t y16 =
                frame.y[lineOffset + static_cast<std::size_t>(sourceX)];

            const std::uint32_t scaledY =
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(y16) *
                    static_cast<std::uint64_t>((displayHeight - 1) << 8) /
                    65535u);

            const int plotY =
                displayHeight - 1 - static_cast<int>(scaledY >> 8);
            const std::uint32_t fraction = scaledY & 0xffu;

            const std::size_t currentIndex =
                static_cast<std::size_t>(plotY) *
                    static_cast<std::size_t>(displayWidth) +
                static_cast<std::size_t>(displayX);
            hits_[currentIndex] += 256u - fraction;

            if (plotY > 0)
            {
                const std::size_t adjacentIndex =
                    static_cast<std::size_t>(plotY - 1) *
                        static_cast<std::size_t>(displayWidth) +
                    static_cast<std::size_t>(displayX);
                hits_[adjacentIndex] += fraction;
            }

            if (previousPlotY >= 0)
            {
                const int firstY = std::min(previousPlotY, plotY);
                const int lastY = std::max(previousPlotY, plotY);

                for (int y = firstY; y <= lastY; ++y)
                {
                    const std::size_t segmentIndex =
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(displayWidth) +
                        static_cast<std::size_t>(displayX);
                    hits_[segmentIndex] += 64u;
                }
            }

            previousPlotY = plotY;
        }
    }

    for (int y = 0; y < displayHeight; ++y)
    {
        auto* destination =
            reinterpret_cast<QRgb*>(image_.scanLine(y));

        for (int x = 0; x < displayWidth; ++x)
        {
            const std::uint32_t hit =
                hits_[static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(displayWidth) +
                    static_cast<std::size_t>(x)];

            const int green = std::min(
                255,
                static_cast<int>(hit >> 8) * 8);
            destination[x] = qRgb(0, green, 0);
        }
    }
}

void WaveformRenderer::plotBeam(
    int x,
    double y,
    int intensity,
    int red,
    int green,
    int blue)
{
    if (x < 0 || x >= image_.width())
    {
        return;
    }

    static constexpr std::array<double, 7> weights = {
        0.015,
        0.075,
        0.235,
        0.350,
        0.235,
        0.075,
        0.015
    };

    const int centreY = static_cast<int>(std::floor(y));
    const double fraction = y - static_cast<double>(centreY);

    for (int offset = -3; offset <= 3; ++offset)
    {
        const int destinationY = centreY + offset;
        if (destinationY < 0 || destinationY >= image_.height())
        {
            continue;
        }

        const double shiftedPosition =
            static_cast<double>(offset) - fraction;
        const double shiftedFloor = std::floor(shiftedPosition);

        const int lowerIndex = std::clamp(
            static_cast<int>(shiftedFloor) + 3,
            0,
            6);
        const int upperIndex = std::min(lowerIndex + 1, 6);
        const double blend = shiftedPosition - shiftedFloor;
        const double weight =
            weights[static_cast<std::size_t>(lowerIndex)] * (1.0 - blend) +
            weights[static_cast<std::size_t>(upperIndex)] * blend;

        const std::uint32_t contribution =
            static_cast<std::uint32_t>(
                std::max(0.0, weight * static_cast<double>(intensity)));

        const std::size_t index =
            static_cast<std::size_t>(destinationY) *
                static_cast<std::size_t>(image_.width()) +
            static_cast<std::size_t>(x);

        TracePixel& pixel = trace_[index];

        const auto addChannel =
            [contribution](std::uint16_t& destination, int channel)
            {
                const std::uint32_t scaled =
                    contribution *
                    static_cast<std::uint32_t>(std::clamp(channel, 0, 255)) /
                    255u;

                destination = static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(
                        65535u,
                        static_cast<std::uint32_t>(destination) + scaled));
            };

        addChannel(pixel.red, red);
        addChannel(pixel.green, green);
        addChannel(pixel.blue, blue);
    }
}

void WaveformRenderer::setSelectedLine(int line)
{
    selectedLine_ = line;
}

void WaveformRenderer::setPersistence(int persistence)
{
    persistence_ = std::clamp(persistence, 0, 255);
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
        static_cast<double>(image_.width()) /
        (captureSamplesPerLine * kPixelsPerCycleForTraceBW);
}
