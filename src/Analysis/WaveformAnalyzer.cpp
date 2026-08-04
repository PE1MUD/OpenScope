#include "WaveformAnalyzer.h"

WaveformAnalyzer::WaveformAnalyzer()
    : image_(720, 256, QImage::Format_RGB32)
{
    image_.fill(Qt::black);
}

void WaveformAnalyzer::analyze(const Yuv444Frame& frame)
{
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

            const int y8 =
                static_cast<int>(y16 >> 8);

            const int plotY =
                displayHeight - 1 - y8;

            image_.setPixelColor(x, plotY, Qt::green);
        }
    }
}

const QImage& WaveformAnalyzer::image() const
{
    return image_;
}