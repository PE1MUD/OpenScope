#include "VectorscopeAnalyzer.h"
#include <QElapsedTimer>
#include <QDebug>
VectorscopeAnalyzer::VectorscopeAnalyzer()
    : image_(576, 576, QImage::Format_RGB32)
{
    image_.fill(Qt::black);
}

void VectorscopeAnalyzer::analyze(const Yuv444Frame& frame)
{
    QElapsedTimer timer;
    timer.start();
    image_.fill(Qt::black);

    if (frame.width <= 0 ||
        frame.height <= 0 ||
        frame.u.empty() ||
        frame.v.empty())
    {
        return;
    }

    const int centerX = image_.width() / 2;
    const int centerY = image_.height() / 2;

    const double scale =
        static_cast<double>(
            std::min(image_.width(), image_.height())) *
        0.45 /
        32768.0;
    
    std::size_t firstSample = 0;
    std::size_t lastSample = frame.u.size();

    if (selectedLine_ >= 0 &&
        selectedLine_ < frame.height)
    {
        firstSample =
            static_cast<std::size_t>(selectedLine_) *
            static_cast<std::size_t>(frame.width);

        lastSample =
            firstSample +
            static_cast<std::size_t>(frame.width);
    }
    for (std::size_t i = firstSample; i < lastSample; ++i)
    {
        const double u =
            static_cast<double>(frame.u[i]) - 32768.0;

        const double v =
            static_cast<double>(frame.v[i]) - 32768.0;

        const int x =
            centerX +
            static_cast<int>(u * scale);

        const int y =
            centerY -
            static_cast<int>(v * scale);

        if (x < 0 ||
            x >= image_.width() ||
            y < 0 ||
            y >= image_.height())
        {
            continue;
        }

        auto* line =
            reinterpret_cast<QRgb*>(
                image_.scanLine(y));

        line[x] = qRgb(0, 255, 0);
    }
    static int counter = 0;

    if (++counter >= 25)
    {
        counter = 0;

        qDebug()
            << "Vectorscope analyze:"
            << timer.elapsed()
            << "ms";
    }
}

const QImage& VectorscopeAnalyzer::image() const
{
    return image_;
}

void VectorscopeAnalyzer::setSelectedLine(int line)
{
    selectedLine_ = line;
}