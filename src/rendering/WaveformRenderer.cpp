#include <QElapsedTimer>
#include <QDebug>
#include "rendering/WaveformRenderer.h"

#include <algorithm>
#include <cmath>

WaveformRenderer::WaveformRenderer()
    : image_(720, 576, QImage::Format_RGB32)
    , hits_(720 * 576)
    , trace_(720 * 576)
    , chroma_(720)
    , displayY_(720)
    , displayU_(720)
    , displayV_(720)
{
    for (std::size_t i = 0; i < displayLut_.size(); ++i)
    {
        displayLut_[i] =
            static_cast<std::uint8_t>(
                std::min<std::size_t>(i, 255u));
    }
    image_.fill(Qt::black);
}

void WaveformRenderer::setOutputSize(
    int width,
    int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    height = std::min(height, 576);

    if (image_.width() == width &&
        image_.height() == height)
    {
        return;
    }

    image_ = QImage(
        width,
        height,
        QImage::Format_RGB32);

    image_.fill(Qt::black);

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    hits_.assign(pixelCount, 0u);

    chroma_.assign(
        static_cast<std::size_t>(width),
        0.0f);

    displayY_.resize(static_cast<std::size_t>(width));
    displayU_.resize(static_cast<std::size_t>(width));
    displayV_.resize(static_cast<std::size_t>(width));
    trace_.assign(pixelCount, TracePixel{});
}

void WaveformRenderer::analyze(const Yuv444Frame& frame)
{
    QElapsedTimer timer;
    timer.start();

    std::fill(chroma_.begin(), chroma_.end(), 0.0f);

    if (selectedLine_ < 0)
    {
        if (persistence_ == 0)
        {
            image_.fill(Qt::black);
        }
        else
        {
            for (int y = 0; y < image_.height(); ++y)
            {
                auto* dst =
                    reinterpret_cast<QRgb*>(image_.scanLine(y));

                for (int x = 0; x < image_.width(); ++x)
                {
                    const int green =
                        qGreen(dst[x]);

                    const int faded =
                        (green * persistence_) / 256;

                    dst[x] = qRgb(0, faded, 0);
                }
            }
        }
        qDebug() << "SelectedLine < 0:" << timer.restart() << "ms";
    }
    if (persistence_ == 0)
    {
        std::fill(
            trace_.begin(),
            trace_.end(),
            TracePixel{});
    }
    else
    {
        for (TracePixel& pixel : trace_)
        {
            pixel.red =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint32_t>(pixel.red) *
                        static_cast<std::uint32_t>(persistence_)) >> 8);

            pixel.green =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint32_t>(pixel.green) *
                        static_cast<std::uint32_t>(persistence_)) >> 8);

            pixel.blue =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint32_t>(pixel.blue) *
                        static_cast<std::uint32_t>(persistence_)) >> 8);
        }
    }
    const qint64 persistenceMs = timer.restart();

    if (frame.width <= 0 ||
        frame.height <= 0 ||
        frame.y.empty() ||
        frame.u.empty() ||
        frame.v.empty())
    {
        return;
    }

    const int displayHeight = image_.height();

    /*
     * Single-line mode:
     * draw one continuous trace directly, without using the hitmap.
     */
    if (selectedLine_ >= 0 &&
        selectedLine_ < frame.height)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(selectedLine_) *
            static_cast<std::size_t>(frame.width);

        sourceY_.resize(static_cast<std::size_t>(frame.width));
        sourceU_.resize(static_cast<std::size_t>(frame.width));
        sourceV_.resize(static_cast<std::size_t>(frame.width));

        for (int x = 0; x < frame.width; ++x)
        {
            const std::size_t sampleIndex =
                lineOffset + static_cast<std::size_t>(x);

            sourceY_[static_cast<std::size_t>(x)] =
                static_cast<float>(frame.y[sampleIndex]);

            sourceU_[static_cast<std::size_t>(x)] =
                static_cast<float>(frame.u[sampleIndex]);

            sourceV_[static_cast<std::size_t>(x)] =
                static_cast<float>(frame.v[sampleIndex]);
        }
        const qint64 copyMs = timer.restart();

        lineResampler_.resample(sourceY_, displayY_);
        lineResampler_.resample(sourceU_, displayU_);
        lineResampler_.resample(sourceV_, displayV_);
        const qint64 resampleMs = timer.restart();
        double previousPlotY = 0.0;
        bool havePreviousPlotY = false;

        for (int x = 0; x < image_.width(); ++x)
        {
            const double yValue =
                std::clamp(
                    static_cast<double>(
                        displayY_[static_cast<std::size_t>(x)]),
                    0.0,
                    65535.0);

            const double uValue =
                std::clamp(
                    static_cast<double>(
                        displayU_[static_cast<std::size_t>(x)]),
                    0.0,
                    65535.0);

            const double vValue =
                std::clamp(
                    static_cast<double>(
                        displayV_[static_cast<std::size_t>(x)]),
                    0.0,
                    65535.0);

            const double chromaU =
                uValue - 32768.0;

            const double chromaV =
                vValue - 32768.0;

            /*
             * Suppress very small chroma vectors. These are typically
             * converter or source noise around neutral grey and should not
             * colour the waveform trace.
             */
            constexpr double chromaNoiseThreshold =
                0.03 * 32768.0;

            double filteredChromaU = chromaU;
            double filteredChromaV = chromaV;

            const double chromaMagnitude =
                std::hypot(chromaU, chromaV);

            if (chromaMagnitude < chromaNoiseThreshold)
            {
                filteredChromaU = 0.0;
                filteredChromaV = 0.0;
            }

            chroma_[static_cast<std::size_t>(x)] =
                static_cast<float>(
                    std::hypot(
                        filteredChromaU,
                        filteredChromaV));

            const double plotY =
                static_cast<double>(displayHeight - 1) -
                (yValue *
                    static_cast<double>(displayHeight - 1) /
                    65535.0);

            const int y0 =
                std::clamp(
                    static_cast<int>(plotY),
                    0,
                    displayHeight - 1);

            /*
             * Join adjacent reconstructed points with a lightweight green
             * connector. Unlike the old implementation this writes each
             * crossed pixel once, instead of stamping a full Gaussian beam
             * at every intermediate position.
             */
            if (havePreviousPlotY)
            {
                const int firstY =
                    std::max(
                        0,
                        static_cast<int>(
                            std::ceil(
                                std::min(previousPlotY, plotY))));

                const int lastY =
                    std::min(
                        displayHeight - 1,
                        static_cast<int>(
                            std::floor(
                                std::max(previousPlotY, plotY))));

                constexpr std::uint32_t connectorIntensity = 160u;

                for (int connectorY = firstY;
                    connectorY <= lastY;
                    ++connectorY)
                {
                    const std::size_t connectorIndex =
                        static_cast<std::size_t>(connectorY) *
                        static_cast<std::size_t>(image_.width()) +
                        static_cast<std::size_t>(x);

                    //traceGreen_[connectorIndex] =
                    //    static_cast<std::uint16_t>(
                    //        std::min<std::uint32_t>(
                    //            65535u,
                    //            static_cast<std::uint32_t>(
                    //                traceGreen_[connectorIndex]) +
                    //            connectorIntensity));
                    trace_[connectorIndex].green =
                        static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(
                                65535u,
                                static_cast<std::uint32_t>(
                                    trace_[connectorIndex].green) +
                                connectorIntensity));
                }
            }

            const double chromaAmplitude =
                static_cast<double>(
                    chroma_[static_cast<std::size_t>(x)]) /
                32768.0;

            const double spread =
                chromaAmplitude *
                static_cast<double>(displayHeight - 1) *
                0.18;

            /*
             * Convert chroma direction to RGB, independent of luminance.
             * The envelope height already represents chroma magnitude.
             */
            double redValue =
                1.402 * filteredChromaV;

            double greenValue =
                -0.344136 * filteredChromaU -
                0.714136 * filteredChromaV;

            double blueValue =
                1.772 * filteredChromaU;

            /*
             * Shift so the smallest channel becomes zero, then normalize
             * the largest channel to 255. This produces maximum saturation
             * and maximum display brightness.
             */
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
                        std::clamp(redValue * 255.0 / maximum, 0.0, 255.0));

                green =
                    static_cast<int>(
                        std::clamp(greenValue * 255.0 / maximum, 0.0, 255.0));

                blue =
                    static_cast<int>(
                        std::clamp(blueValue * 255.0 / maximum, 0.0, 255.0));
            }

            constexpr int luminanceBeamIntensity = 768;
            constexpr int chromaBeamIntensity = 768;

            // Luminance trace.
            plotBeam(
                x,
                plotY,
                luminanceBeamIntensity,
                0,
                255,
                0);

            // Chroma envelope: one Gaussian beam on each side.
            if (maximum > 0.0 && spread > 0.0)
            {
                plotBeam(
                    x,
                    plotY - spread,
                    chromaBeamIntensity,
                    red,
                    green,
                    blue);

                plotBeam(
                    x,
                    plotY + spread,
                    chromaBeamIntensity,
                    red,
                    green,
                    blue);
            }

            previousPlotY = plotY;
            havePreviousPlotY = true;
        }
        const qint64 traceMs = timer.restart();

        for (int y = 0; y < displayHeight; ++y)
        {
            auto* dst =
                reinterpret_cast<QRgb*>(image_.scanLine(y));

            for (int x = 0; x < image_.width(); ++x)
            {
                const std::size_t index =
                    static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(image_.width()) +
                    static_cast<std::size_t>(x);

                const TracePixel& pixel =
                    trace_[index];

                const int red =
                    displayLut_[pixel.red];

                const int green =
                    displayLut_[pixel.green];

                const int blue =
                    displayLut_[pixel.blue];

                dst[x] = qRgb(
                    red,
                    green,
                    blue);
            }
        }
        const qint64 composeMs = timer.restart();
        static int timingFrameCounter = 0;

        if (++timingFrameCounter >= 25)
        {
            timingFrameCounter = 0;

            qDebug()
                << "Persistence:" << persistenceMs << "ms"
                << "Copy:" << copyMs << "ms"
                << "Resample:" << resampleMs << "ms"
                << "Trace:" << traceMs << "ms"
                << "Compose:" << composeMs << "ms"
                << "Total:"
                << persistenceMs
                + copyMs
                + resampleMs
                + traceMs
                + composeMs
                << "ms";
        }

        return;
    }

    /*
     * All-lines mode:
     * accumulate all traces into the existing density hitmap.
     */
    std::fill(hits_.begin(), hits_.end(), 0u);

    const int displayWidth = image_.width();

    for (int line = 0; line < frame.height; ++line)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(line) *
            static_cast<std::size_t>(frame.width);

        int previousPlotY = -1;

        for (int sourceX = 0; sourceX < frame.width; ++sourceX)
        {
            const int displayX =
                std::clamp(
                    sourceX * displayWidth / frame.width,
                    0,
                    displayWidth - 1);

            const std::uint16_t y16 =
                frame.y[
                    lineOffset +
                        static_cast<std::size_t>(sourceX)];

            const std::uint32_t scaledY =
                static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(y16) *
                        static_cast<std::uint64_t>(
                            (displayHeight - 1) << 8)) /
                    65535u);

            const int plotY =
                displayHeight - 1 -
                static_cast<int>(scaledY >> 8);

            const std::uint32_t fraction =
                scaledY & 0xffu;

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
                const int firstY =
                    std::min(previousPlotY, plotY);

                const int lastY =
                    std::max(previousPlotY, plotY);

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

    for (int y = 0; y < image_.height(); ++y)
    {
        auto* dst =
            reinterpret_cast<QRgb*>(image_.scanLine(y));

        for (int x = 0; x < displayWidth; ++x)
        {
            const std::uint32_t hit =
                hits_[
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(displayWidth) +
                        static_cast<std::size_t>(x)];

            const int green =
                std::min(
                    255,
                    static_cast<int>(hit >> 8) * 8);

            dst[x] = qRgb(0, green, 0);
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
        return;

    const int y0 =
        static_cast<int>(y);

    if (y0 < 0 || y0 >= image_.height() - 1)
        return;

    const double fraction =
        y - static_cast<double>(y0);

    const auto add =
        [&](int yy, double weight)
        {
            if (yy < 0 || yy >= image_.height())
                return;

            const std::size_t index =
                static_cast<std::size_t>(yy) *
                static_cast<std::size_t>(image_.width()) +
                static_cast<std::size_t>(x);

            const auto contribution =
                static_cast<std::uint16_t>(
                    weight * static_cast<double>(intensity));

            auto& pixel = trace_[index];

            const auto addPackedChannel =
                [&](std::uint16_t& channelValue, int channel)
                {
                    const std::uint32_t scaledContribution =
                        static_cast<std::uint32_t>(contribution) *
                        static_cast<std::uint32_t>(channel) / 255u;

                    channelValue =
                        static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(
                                65535u,
                                static_cast<std::uint32_t>(channelValue) +
                                scaledContribution));
                };

            addPackedChannel(pixel.red, red);
            addPackedChannel(pixel.green, green);
            addPackedChannel(pixel.blue, blue);
        };

    const auto addGaussian =
        [&](int centreY, double subPixelFraction)
        {
            static constexpr double weights[] =
            {
                0.015,
                0.075,
                0.235,
                0.350,
                0.235,
                0.075,
                0.015
            };

            for (int i = -3; i <= 3; ++i)
            {
                const double shiftedPosition =
                    static_cast<double>(i) -
                    subPixelFraction;

                const int lowerIndex =
                    std::clamp(
                        static_cast<int>(
                            std::floor(shiftedPosition)) + 3,
                        0,
                        6);

                const int upperIndex =
                    std::min(lowerIndex + 1, 6);

                const double blend =
                    shiftedPosition -
                    std::floor(shiftedPosition);

                const double weight =
                    weights[lowerIndex] * (1.0 - blend) +
                    weights[upperIndex] * blend;

                add(
                    centreY + i,
                    weight);
            }
        };

    addGaussian(y0, fraction);
}
void WaveformRenderer::setSelectedLine(int line)
{
    selectedLine_ = line;
}

const QImage& WaveformRenderer::image() const
{
    return image_;
}

void WaveformRenderer::setPersistence(int persistence)
{
    persistence_ = std::clamp(persistence, 0, 255);
}
double WaveformRenderer::traceBandwidthMHz() const
{
    constexpr double captureSampleRateMHz = 13.5;
    constexpr double captureSamplesPerLine = 720.0;

    return
        captureSampleRateMHz *
        static_cast<double>(image_.width()) /
        (captureSamplesPerLine *
            kPixelsPerCycleForTraceBW);
}