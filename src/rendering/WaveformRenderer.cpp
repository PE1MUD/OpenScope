#include "rendering/WaveformRenderer.h"

#include <algorithm>
#include <cmath>

WaveformRenderer::WaveformRenderer()
    : image_(720, 576, QImage::Format_RGB32)
    , hits_(720 * 576)
    , traceRed_(720 * 576)
    , traceGreen_(720 * 576)
    , traceBlue_(720 * 576)
    , chroma_(720)
    , displayY_(720)
    , displayU_(720)
    , displayV_(720)
{
    image_.fill(Qt::black);
}

void WaveformRenderer::setOutputSize(
    int width,
    int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);

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

    traceRed_.assign(pixelCount, 0u);
    traceGreen_.assign(pixelCount, 0u);
    traceBlue_.assign(pixelCount, 0u);

    chroma_.assign(
        static_cast<std::size_t>(width),
        0.0f);

    displayY_.resize(static_cast<std::size_t>(width));
    displayU_.resize(static_cast<std::size_t>(width));
    displayV_.resize(static_cast<std::size_t>(width));
}

void WaveformRenderer::analyze(const Yuv444Frame& frame)
{
    std::fill(chroma_.begin(), chroma_.end(), 0.0f);

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
        for (std::size_t i = 0; i < traceRed_.size(); ++i)
        {
            traceRed_[i] =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint32_t>(traceRed_[i]) * persistence_) >> 8);

            traceGreen_[i] =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint32_t>(traceGreen_[i]) * persistence_) >> 8);

            traceBlue_[i] =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint32_t>(traceBlue_[i]) * persistence_) >> 8);
        }

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

        lineResampler_.resample(sourceY_, displayY_);
        lineResampler_.resample(sourceU_, displayU_);
        lineResampler_.resample(sourceV_, displayV_);

        int previousY = -1;

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

            chroma_[static_cast<std::size_t>(x)] =
                static_cast<float>(
                    std::sqrt(
                        chromaU * chromaU +
                        chromaV * chromaV));

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

            if (previousY >= 0)
            {
                const int first =
                    std::min(previousY, y0);

                const int last =
                    std::max(previousY, y0);

                const int distance =
                    last - first + 1;

                for (int i = 0; i < distance; ++i)
                {
                    const double t =
                        distance > 1
                        ? static_cast<double>(i) /
                        static_cast<double>(distance - 1)
                        : 0.0;

                    const double y =
                        previousY +
                        t * (y0 - previousY);

                    plotBeam(x, y, 128);
                }
            }

            const int y1 =
                std::min(y0 + 1, displayHeight - 1);

            const double fraction =
                plotY - static_cast<double>(y0);

            const std::size_t index0 =
                static_cast<std::size_t>(y0) *
                static_cast<std::size_t>(frame.width) +
                static_cast<std::size_t>(x);

            const std::size_t index1 =
                static_cast<std::size_t>(y1) *
                static_cast<std::size_t>(frame.width) +
                static_cast<std::size_t>(x);

            (void)fraction;
            (void)index0;
            (void)index1;

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
                1.402 * chromaV;

            double greenValue =
                -0.344136 * chromaU -
                0.714136 * chromaV;

            double blueValue =
                1.772 * chromaU;

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

            // Sharp luminance trace.
            plotBeam(x, plotY, 96, 0, 255, 0);

            // Softer chroma outline.
            const auto plotChromaGlow =
                [&](double y)
                {
                    plotBeam(x, y, 255, red, green, blue);
                    plotBeam(x, y - 1.0, 96, red, green, blue);
                    plotBeam(x, y + 1.0, 96, red, green, blue);
                    plotBeam(x, y - 2.0, 32, red, green, blue);
                    plotBeam(x, y + 2.0, 32, red, green, blue);
                };

            plotChromaGlow(plotY - spread);
            plotChromaGlow(plotY + spread);

            previousY = y0;
        }

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

                const int red =
                    std::min(255, static_cast<int>(traceRed_[index]));

                const int green =
                    std::min(255, static_cast<int>(traceGreen_[index]));

                const int blue =
                    std::min(255, static_cast<int>(traceBlue_[index]));

                dst[x] = qRgb(red, green, blue);
            }
        }

        return;
    }

    /*
     * All-lines mode:
     * accumulate all traces into the existing density hitmap.
     */
    std::fill(hits_.begin(), hits_.end(), 0u);

    for (int line = 0; line < frame.height; ++line)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(line) *
            static_cast<std::size_t>(frame.width);

        int previousPlotY = -1;

        for (int x = 0; x < frame.width; ++x)
        {
            const std::uint16_t y16 =
                frame.y[lineOffset + static_cast<std::size_t>(x)];

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
                static_cast<std::size_t>(frame.width) +
                static_cast<std::size_t>(x);

            hits_[currentIndex] += 256u - fraction;

            if (plotY > 0)
            {
                const std::size_t adjacentIndex =
                    static_cast<std::size_t>(plotY - 1) *
                    static_cast<std::size_t>(frame.width) +
                    static_cast<std::size_t>(x);

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
                        static_cast<std::size_t>(frame.width) +
                        static_cast<std::size_t>(x);

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

        for (int x = 0; x < frame.width; ++x)
        {
            const std::uint32_t hit =
                hits_[
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(frame.width) +
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

            const auto addChannel =
                [&](std::vector<std::uint16_t>& buffer, int channel)
                {
                    const std::uint32_t scaledContribution =
                        static_cast<std::uint32_t>(contribution) *
                        static_cast<std::uint32_t>(channel) / 255u;

                    buffer[index] =
                        static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(
                                65535u,
                                static_cast<std::uint32_t>(buffer[index]) +
                                scaledContribution));
                };

            addChannel(traceRed_, red);
            addChannel(traceGreen_, green);
            addChannel(traceBlue_, blue);
        };

    add(y0 - 1, (1.0 - fraction) * 0.15);
    add(y0, (1.0 - fraction) * 0.85);
    add(y0 + 1, fraction * 0.85);
    add(y0 + 2, fraction * 0.15);
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
