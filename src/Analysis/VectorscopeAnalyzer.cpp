#include "VectorscopeAnalyzer.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QPainter>

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

    const double centerX =
        (image_.width() - 1) * 0.5;

    const double centerY =
        (image_.height() - 1) * 0.5;

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
    double sumU = 0.0;
    double sumV = 0.0;
    std::size_t sampleCount = 0;
    std::uint16_t minU = 65535;
    std::uint16_t maxU = 0;
    std::uint16_t minV = 65535;
    std::uint16_t maxV = 0;
    for (std::size_t i = firstSample; i < lastSample; ++i)
    {
        constexpr double chromaCenter =
            32768.0;

        const double u =
            static_cast<double>(frame.u[i]) -
            chromaCenter;

        const double v =
            static_cast<double>(frame.v[i]) -
            chromaCenter;

        sumU += static_cast<double>(frame.u[i]);
        sumV += static_cast<double>(frame.v[i]);
        ++sampleCount;
        minU = std::min(minU, frame.u[i]);
        maxU = std::max(maxU, frame.u[i]);
        minV = std::min(minV, frame.v[i]);
        maxV = std::max(maxV, frame.v[i]);
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

        if (sampleCount > 0)
        {
            const double averageU =
                sumU /
                static_cast<double>(sampleCount);

            const double averageV =
                sumV /
                static_cast<double>(sampleCount);

            qDebug()
                << "Vectorscope UV:"
                << "U =" << averageU
                << "[" << minU << ".." << maxU << "]"
                << "V =" << averageV
                << "[" << minV << ".." << maxV << "]"
                << "neutral =" << (128.0 * 257.0);
        }
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