#include "VectorscopeAnalyzer.h"
#include <algorithm>
#include <QElapsedTimer>
#include <QDebug>
#include <QPainter>
#include <QPointF>

VectorscopeAnalyzer::VectorscopeAnalyzer()
    : image_(1, 1, QImage::Format_RGB32)
{
    image_.fill(Qt::black);
}
void VectorscopeAnalyzer::setOutputSize(
    int width,
    int height)
{
    width =
        std::max(width, 1);

    height =
        std::max(height, 1);

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
}
void VectorscopeAnalyzer::analyze(const Yuv444Frame& frame)
{
    image_.fill(Qt::black);

    static int counter = 0;

    if (++counter == 25)
    {
        counter = 0;

        qDebug()
            << "Vectorscope image:"
            << image_.width()
            << "x"
            << image_.height();
    }
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
        0.5 /
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
    constexpr double chromaCenter =
        32768.0;

    auto samplePoint =
        [&](std::size_t index)
        {
            const double u =
                static_cast<double>(frame.u[index]) -
                chromaCenter;

            const double v =
                static_cast<double>(frame.v[index]) -
                chromaCenter;

            return QPointF(
                centerX + u * scale,
                centerY - v * scale);
        };

    auto plotPoint =
        [&](double x, double y)
        {
            const int x0 =
                static_cast<int>(std::floor(x));

            const int y0 =
                static_cast<int>(std::floor(y));

            const double fx =
                x - static_cast<double>(x0);

            const double fy =
                y - static_cast<double>(y0);

            const double weights[4] =
            {
                (1.0 - fx) * (1.0 - fy),
                fx * (1.0 - fy),
                (1.0 - fx) * fy,
                fx * fy
            };

            const int dx[4] =
            {
                0, 1, 0, 1
            };

            const int dy[4] =
            {
                0, 0, 1, 1
            };

            for (int i = 0; i < 4; ++i)
            {
                const int ix =
                    x0 + dx[i];

                const int iy =
                    y0 + dy[i];

                if (ix < 0 ||
                    ix >= image_.width() ||
                    iy < 0 ||
                    iy >= image_.height())
                {
                    continue;
                }

                auto* line =
                    reinterpret_cast<QRgb*>(
                        image_.scanLine(iy));

                const int green =
                    static_cast<int>(
                        200.0 * weights[i]);

                const int oldGreen =
                    qGreen(line[ix]);

                line[ix] =
                    qRgb(
                        0,
                        std::max(oldGreen, green),
                        0);
            }
        };

    for (std::size_t i = firstSample;
        i < lastSample;
        ++i)
    {
        sumU += static_cast<double>(frame.u[i]);
        sumV += static_cast<double>(frame.v[i]);
        ++sampleCount;

        minU = std::min(minU, frame.u[i]);
        maxU = std::max(maxU, frame.u[i]);
        minV = std::min(minV, frame.v[i]);
        maxV = std::max(maxV, frame.v[i]);
    }

    if (lastSample - firstSample >= 4)
    {
        for (std::size_t i = firstSample + 1;
            i + 2 < lastSample;
            ++i)
        {
            const QPointF p0 =
                samplePoint(i - 1);

            const QPointF p1 =
                samplePoint(i);

            const QPointF p2 =
                samplePoint(i + 1);

            const QPointF p3 =
                samplePoint(i + 2);

            constexpr int subdivisions = 32;

            for (int step = 0;
                step < subdivisions;
                ++step)
            {
                const double t =
                    static_cast<double>(step) /
                    static_cast<double>(subdivisions);

                const double t2 = t * t;
                const double t3 = t2 * t;

                const double x =
                    0.5 *
                    (
                        (2.0 * p1.x()) +
                        (-p0.x() + p2.x()) * t +
                        (2.0 * p0.x() -
                            5.0 * p1.x() +
                            4.0 * p2.x() -
                            p3.x()) * t2 +
                        (-p0.x() +
                            3.0 * p1.x() -
                            3.0 * p2.x() +
                            p3.x()) * t3
                        );

                const double y =
                    0.5 *
                    (
                        (2.0 * p1.y()) +
                        (-p0.y() + p2.y()) * t +
                        (2.0 * p0.y() -
                            5.0 * p1.y() +
                            4.0 * p2.y() -
                            p3.y()) * t2 +
                        (-p0.y() +
                            3.0 * p1.y() -
                            3.0 * p2.y() +
                            p3.y()) * t3
                        );
                plotPoint(x, y);
            }
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