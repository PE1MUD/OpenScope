#include <algorithm>
#include "WaveformAnalyzer.h"

WaveformAnalyzer::WaveformAnalyzer()
    : image_(720, 1024, QImage::Format_RGB32)
    , hits_(720 * 1024)
{
    image_.fill(Qt::black);
}

void WaveformAnalyzer::analyze(const Yuv444Frame& frame)
{
    std::fill(hits_.begin(), hits_.end(), 0);

    image_.fill(Qt::black);

    if (frame.width == 0 || frame.height == 0)
        return;

    const int displayHeight = image_.height();

    for (int line = 0; line < frame.height; ++line)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(line) *
            static_cast<std::size_t>(frame.width);

        for (int x = 0; x < frame.width; ++x)
        {
            const std::uint16_t y16 =
                frame.y[lineOffset + x];

            const int plotY =
                displayHeight - 1 -
                static_cast<int>(
                    (static_cast<std::uint32_t>(y16) *
                        static_cast<std::uint32_t>(displayHeight - 1)) /
                    65535u);

            hits_[plotY * frame.width + x]++;
        }
    }
    for (int y = 0; y < image_.height(); ++y)
    {
        auto* dst =
            reinterpret_cast<QRgb*>(image_.scanLine(y));

        for (int x = 0; x < frame.width; ++x)
        {
            const std::uint16_t hit =
                hits_[static_cast<std::size_t>(y) *
                static_cast<std::size_t>(frame.width) +
                static_cast<std::size_t>(x)];

            const int green =
                std::min(255, static_cast<int>(hit) * 8);

            dst[x] = qRgb(0, green, 0);
        }
    }
}

const QImage& WaveformAnalyzer::image() const
{
    return image_;
}