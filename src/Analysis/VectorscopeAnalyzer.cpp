#include "VectorscopeAnalyzer.h"
#include <algorithm>
#include <cmath>
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
    QElapsedTimer timer;
    timer.start();

    image_.fill(Qt::black);

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

    const qint64 setupUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

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
        [&](double x, double y, double intensity)
        {
            constexpr double radius = 1.6;
            constexpr double sigma = 0.65;

            const int minX =
                static_cast<int>(std::floor(x - radius));

            const int maxX =
                static_cast<int>(std::ceil(x + radius));

            const int minY =
                static_cast<int>(std::floor(y - radius));

            const int maxY =
                static_cast<int>(std::ceil(y + radius));

            for (int iy = minY;
                iy <= maxY;
                ++iy)
            {
                if (iy < 0 ||
                    iy >= image_.height())
                {
                    continue;
                }

                auto* line =
                    reinterpret_cast<QRgb*>(
                        image_.scanLine(iy));

                for (int ix = minX;
                    ix <= maxX;
                    ++ix)
                {
                    if (ix < 0 ||
                        ix >= image_.width())
                    {
                        continue;
                    }

                    const double dx =
                        (static_cast<double>(ix) + 0.5) - x;

                    const double dy =
                        (static_cast<double>(iy) + 0.5) - y;

                    const double distanceSquared =
                        dx * dx +
                        dy * dy;

                    if (distanceSquared >
                        radius * radius)
                    {
                        continue;
                    }

                    const double weight =
                        std::exp(
                            -distanceSquared /
                            (2.0 * sigma * sigma));

                    const int green =
                        static_cast<int>(
                            intensity * weight);

                    const int oldGreen =
                        qGreen(line[ix]);

                    const int newGreen =
                        std::min(
                            255,
                            oldGreen + green);

                    line[ix] =
                        qRgb(
                            0,
                            newGreen,
                            0);
                }
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

    const qint64 statisticsUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

    std::size_t totalSubdivisions = 0;
    std::size_t plottedPoints = 0;

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

            const double dx =
                p2.x() - p1.x();

            const double dy =
                p2.y() - p1.y();

            const double distance =
                std::hypot(dx, dy);

            constexpr double targetStepPixels =
                0.5;

            const int subdivisions =
                std::clamp(
                    static_cast<int>(
                        std::ceil(
                            distance /
                            targetStepPixels)),
                    4,
                    256);

            totalSubdivisions +=
                static_cast<std::size_t>(
                    subdivisions);

            constexpr double referenceSubdivisions =
                32.0;

            constexpr double referenceEnergy =
                80.0;

            constexpr double referenceSize =
                576.0;

            const double renderScale =
                static_cast<double>(
                    std::min(
                        image_.width(),
                        image_.height())) /
                referenceSize;

            const double beamEnergy =
                referenceEnergy *
                referenceSubdivisions *
                std::pow(
                    renderScale,
                    1.2) /
                static_cast<double>(
                    subdivisions);

            for (int step = 0;
                step < subdivisions;
                ++step)
            {
                const double t =
                    static_cast<double>(step) /
                    static_cast<double>(
                        subdivisions);

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

                plotPoint(
                    x,
                    y,
                    beamEnergy);

                ++plottedPoints;
            }
        }
    }

    const qint64 renderUs =
        timer.nsecsElapsed() / 1000;

    static int debugCounter = 0;

    if (++debugCounter >= 25)
    {
        debugCounter = 0;

        qDebug()
            << "Vectorscope:"
            << "mode =" << (selectedLine_ < 0 ? "ALL" : "LINE")
            << "samples =" << (lastSample - firstSample)
            << "subdivisions =" << totalSubdivisions
            << "points =" << plottedPoints
            << "setup =" << setupUs / 1000.0 << "ms"
            << "stats =" << statisticsUs / 1000.0 << "ms"
            << "render =" << renderUs / 1000.0 << "ms"
            << "size =" << image_.width()
            << "x" << image_.height();
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