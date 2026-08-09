#include "VectorscopeAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <QElapsedTimer>
#include <QDebug>
#include <QPainter>
#include <QPointF>


VectorscopeAnalyzer::VectorscopeAnalyzer()
    : QObject(nullptr),
    image_(1, 1, QImage::Format_RGB32),
    allLinesImage_(
        kAllLinesWidth,
        kAllLinesHeight,
        QImage::Format_RGB32)
{
    image_.fill(Qt::black);
    allLinesImage_.fill(Qt::black);
    allLinesDensity_.resize(
        static_cast<std::size_t>(
            kAllLinesWidth *
            kAllLinesHeight));
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

    if (selectedLine_ < 0)
    {
        renderAllLines(frame);
        return;
    }

    const double centerX =
        (image_.width() - 1) * 0.5;

    const double centerY =
        (image_.height() - 1) * 0.5;

    const double scale =
        static_cast<double>(
            std::min(image_.width(), image_.height())) *
        0.5 *
        scale_ /
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
                            newGreen,
                            newGreen,
                            newGreen);
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
}
const QImage& VectorscopeAnalyzer::image() const
{
    return image_;
}

void VectorscopeAnalyzer::setSelectedLine(int line)
{
    selectedLine_ = line;
}

void VectorscopeAnalyzer::renderSingleLine(
    const Yuv444Frame& frame)
{
    // Hier komt straks de bestaande Catmull/Gaussian rendercode.
}

void VectorscopeAnalyzer::renderAllLines(
    const Yuv444Frame& frame)
{
    std::fill(
        allLinesDensity_.begin(),
        allLinesDensity_.end(),
        0u);

    allLinesImage_.fill(Qt::black);

    const double centerX =
        (kAllLinesWidth - 1) * 0.5;

    const double centerY =
        (kAllLinesHeight - 1) * 0.5;

    const double scaleX =
        static_cast<double>(kAllLinesWidth) *
        0.5 *
        scale_ /
        32768.0;

    const double scaleY =
        static_cast<double>(kAllLinesHeight) *
        0.5 *
        scale_ /
        32768.0;

    constexpr double chromaCenter =
        32768.0;

    constexpr std::uint32_t segmentEnergy =
        256u;

    for (int line = 0;
        line < frame.height;
        ++line)
    {
        const std::size_t lineStart =
            static_cast<std::size_t>(line) *
            static_cast<std::size_t>(frame.width);

        for (int x = 0;
            x + 1 < frame.width;
            ++x)
        {
            const std::size_t i0 =
                lineStart +
                static_cast<std::size_t>(x);

            const std::size_t i1 =
                i0 + 1;

            const double u0 =
                static_cast<double>(frame.u[i0]) -
                chromaCenter;

            const double v0 =
                static_cast<double>(frame.v[i0]) -
                chromaCenter;

            const double u1 =
                static_cast<double>(frame.u[i1]) -
                chromaCenter;

            const double v1 =
                static_cast<double>(frame.v[i1]) -
                chromaCenter;

            const int ix0 =
                static_cast<int>(
                    std::lround(
                        centerX +
                        u0 * scaleX));

            const int iy0 =
                static_cast<int>(
                    std::lround(
                        centerY -
                        v0 * scaleY));

            const int ix1 =
                static_cast<int>(
                    std::lround(
                        centerX +
                        u1 * scaleX));

            const int iy1 =
                static_cast<int>(
                    std::lround(
                        centerY -
                        v1 * scaleY));

            accumulateLineSegmentInteger(
                ix0,
                iy0,
                ix1,
                iy1,
                segmentEnergy,
                kAllLinesWidth,
                kAllLinesHeight,
                allLinesDensity_);
        }
    }

    constexpr double whitePoint =
        200000.0;

    constexpr double gamma =
        0.25;

    constexpr double minimumVisible =
        0.08;

    for (int y = 0;
        y < kAllLinesHeight;
        ++y)
    {
        auto* outputLine =
            reinterpret_cast<QRgb*>(
                allLinesImage_.scanLine(y));

        for (int x = 0;
            x < kAllLinesWidth;
            ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(
                    kAllLinesWidth) +
                static_cast<std::size_t>(x);

            const std::uint32_t density =
                allLinesDensity_[index];

            if (density == 0)
            {
                outputLine[x] =
                    qRgb(0, 0, 0);

                continue;
            }

            const double normalized =
                std::min(
                    1.0,
                    static_cast<double>(density) /
                    whitePoint);

            const double corrected =
                minimumVisible +
                (1.0 - minimumVisible) *
                std::pow(
                    normalized,
                    gamma);

            const int green =
                static_cast<int>(
                    255.0 * corrected);

            outputLine[x] =
                qRgb(
                    green,
                    green,
                    green);
        }
    }

    const QSize outputSize =
        image_.size();

    image_ =
        allLinesImage_.scaled(
            outputSize,
            Qt::IgnoreAspectRatio,
            Qt::FastTransformation);
}

void VectorscopeAnalyzer::accumulateLineSegmentInteger(
    int x0,
    int y0,
    int x1,
    int y1,
    std::uint32_t energy,
    int width,
    int height,
    std::vector<std::uint32_t>& density)
{
    const int dx =
        std::abs(x1 - x0);

    const int dy =
        std::abs(y1 - y0);

    const int steps =
        std::max(dx, dy) + 1;

    const std::uint32_t pointEnergy =
        std::max(
            1u,
            energy /
            static_cast<std::uint32_t>(steps));

    const int sx =
        (x0 < x1) ? 1 : -1;

    const int sy =
        (y0 < y1) ? 1 : -1;

    int error =
        dx - dy;

    for (;;)
    {
        if (x0 >= 0 &&
            x0 < width &&
            y0 >= 0 &&
            y0 < height)
        {
            const std::size_t index =
                static_cast<std::size_t>(y0) *
                static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x0);

            density[index] +=
                pointEnergy;
        }

        if (x0 == x1 &&
            y0 == y1)
        {
            break;
        }

        const int error2 =
            error * 2;

        if (error2 > -dy)
        {
            error -= dy;
            x0 += sx;
        }

        if (error2 < dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}
std::uint32_t VectorscopeAnalyzer::accumulateLineSegment(
    double x0,
    double y0,
    double x1,
    double y1,
    std::uint32_t energy)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;

    const double length =
        std::max(
            std::abs(dx),
            std::abs(dy));

    const int steps =
        std::max(
            1,
            static_cast<int>(
                std::ceil(length)));
    const std::uint32_t pointEnergy =
        std::max(
            1u,
            energy /
            static_cast<std::uint32_t>(
                steps + 1));
    for (int step = 0;
        step <= steps;
        ++step)
    {
        const double t =
            static_cast<double>(step) /
            static_cast<double>(steps);

        const int x =
            static_cast<int>(
                std::lround(
                    x0 + dx * t));

        const int y =
            static_cast<int>(
                std::lround(
                    y0 + dy * t));

        if (x < 0 ||
            x >= image_.width() ||
            y < 0 ||
            y >= image_.height())
        {
            continue;
        }

        const std::size_t index =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(image_.width()) +
            static_cast<std::size_t>(x);

        density_[index] += pointEnergy;
    }
    return static_cast<std::uint32_t>(steps + 1);
}

void VectorscopeAnalyzer::setScale(double scale)
{
    scale_ = scale;
}