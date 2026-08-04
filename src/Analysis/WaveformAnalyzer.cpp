#include "WaveformAnalyzer.h"

#include <algorithm>


WaveformAnalyzer::WaveformAnalyzer()
    : image_(720, 576, QImage::Format_RGB32)
    , hits_(720 * 576)
    , trace_(720 * 576)
{
    image_.fill(Qt::black);
}

void WaveformAnalyzer::analyze(const Yuv444Frame& frame)
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

    if (frame.width <= 0 ||
        frame.height <= 0 ||
        frame.y.empty())
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
        for (auto& value : trace_)
        {
            value =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint32_t>(value) * persistence_) >> 8);
        }
        const std::size_t lineOffset =
            static_cast<std::size_t>(selectedLine_) *
            static_cast<std::size_t>(frame.width);
        int previousY = -1;
        for (int x = 0; x < frame.width; ++x)
        {
            const std::uint16_t y16 =
                frame.y[lineOffset + static_cast<std::size_t>(x)];

            const double plotY =
                static_cast<double>(displayHeight - 1) -
                (static_cast<double>(y16) *
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

                const int distance = last - first + 1;

                for (int i = 0; i < distance; ++i)
                {
                    const double t =
                        static_cast<double>(i) /
                        static_cast<double>(distance - 1);

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

            plotBeam(x, plotY); 

            previousY = y0;
        }

        for (int y = 0; y < displayHeight; ++y)
        {
            auto* dst =
                reinterpret_cast<QRgb*>(image_.scanLine(y));

            for (int x = 0; x < frame.width; ++x)
            {
                const std::uint16_t value =
                    trace_[
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(frame.width) +
                            static_cast<std::size_t>(x)];

                const int green =
                    std::min(
                        255,
                        static_cast<int>(value));

                dst[x] = qRgb(0, green, 0);
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

void WaveformAnalyzer::plotBeam(
    int x,
    double y,
    int intensity)
{
    if (x < 0 || x >= image_.width())
        return;

    const int y0 =
        static_cast<int>(y);

    if (y0 < 0 || y0 >= image_.height() - 1)
        return;

    const double fraction =
        y - static_cast<double>(y0);

    const std::size_t index0 =
        static_cast<std::size_t>(y0) *
        static_cast<std::size_t>(image_.width()) +
        static_cast<std::size_t>(x);

    const std::size_t index1 =
        index0 + static_cast<std::size_t>(image_.width());

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

            trace_[index] =
                static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(
                        65535u,
                        static_cast<std::uint32_t>(trace_[index]) +
                        contribution));
        };
    
    add(y0 - 1, (1.0 - fraction) * 0.15);
    add(y0, (1.0 - fraction) * 0.85);
    add(y0 + 1, fraction * 0.85);
    add(y0 + 2, fraction * 0.15);
}

void WaveformAnalyzer::setSelectedLine(int line)
{
    selectedLine_ = line;
}

const QImage& WaveformAnalyzer::image() const
{
    return image_;
}

void WaveformAnalyzer::setPersistence(int persistence)
{
    persistence_ = std::clamp(persistence, 0, 255);
}