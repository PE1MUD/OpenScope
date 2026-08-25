#include "PerformanceWidget.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QCloseEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QToolTip>

namespace
{
    constexpr double kMaximumUs =
        80000.0;

    constexpr double kField1DeadlineUs =
        40000.0;

    constexpr double kField2DeadlineUs =
        60000.0;

    constexpr double kTimingToleranceUs =
        2000.0;

    enum class BarType
    {
        FieldTiming,
        Field1Ready,
        Field2Ready,
        Worker0,
        Worker1,
        Normal
    };

    enum class DetailKind
    {
        None,
        WaveformScreen,
        VectorscopeScreen,
        WaveformVideo,
        VectorscopeVideo
    };

    struct Bar
    {
        const char* label;
        const PerformanceMetricSnapshot* metric;
        BarType type;
        DetailKind detail = DetailKind::None;
    };

    int xForUs(
        const QRect& barRect,
        double us)
    {
        const double normalized =
            std::clamp(
                us / kMaximumUs,
                0.0,
                1.0);

        return
            barRect.left() +
            static_cast<int>(
                std::lround(
                    normalized *
                    static_cast<double>(
                        barRect.width())));
    }

    void drawAssistChunkMarkers(
        QPainter& painter,
        const QRect& barRect,
        const WaveformAssistTimelineSnapshot& timeline,
        std::uint64_t expectedGeneration)
    {
        if (expectedGeneration == 0 ||
            timeline.generation != expectedGeneration)
        {
            return;
        }

        painter.save();
        painter.setPen(QColor(235, 235, 235));

        const int centerY =
            barRect.center().y();

        for (std::uint32_t i = 0;
            i < timeline.count &&
            i < WaveformAssistTimelineSnapshot::kCapacity;
            ++i)
        {
            const auto& event = timeline.events[i];
            const int x0 = xForUs(
                barRect,
                static_cast<double>(event.startUs));
            const int x1 = xForUs(
                barRect,
                static_cast<double>(event.startUs) +
                    static_cast<double>(event.durationUs));

            const int left =
                std::clamp(x0, barRect.left(), barRect.right());
            const int right =
                std::clamp(std::max(x1, x0 + 1),
                    barRect.left(),
                    barRect.right());

            painter.drawLine(
                left,
                barRect.top() + 2,
                left,
                barRect.bottom() - 2);
            painter.drawLine(
                right,
                barRect.top() + 2,
                right,
                barRect.bottom() - 2);

            const int midX =
                left + (right - left) / 2;
            painter.drawPoint(midX, centerY);
            painter.drawPoint(midX + 1, centerY);
        }

        painter.restore();
    }

    void fillTimingBackground(
        QPainter& painter,
        const QRect& barRect,
        double greenEndUs,
        double yellowEndUs)
    {
        const int greenEnd =
            xForUs(
                barRect,
                greenEndUs);

        const int yellowEnd =
            xForUs(
                barRect,
                yellowEndUs);

        painter.fillRect(
            QRect(
                barRect.left(),
                barRect.top(),
                std::max(
                    greenEnd -
                    barRect.left(),
                    0),
                barRect.height()),
            QColor(30, 125, 48));

        painter.fillRect(
            QRect(
                greenEnd,
                barRect.top(),
                std::max(
                    yellowEnd -
                    greenEnd,
                    0),
                barRect.height()),
            QColor(155, 132, 35));

        painter.fillRect(
            QRect(
                yellowEnd,
                barRect.top(),
                std::max(
                    barRect.right() + 1 -
                    yellowEnd,
                    0),
                barRect.height()),
            QColor(145, 38, 38));
    }

    void fillFieldTimingBackground(
        QPainter& painter,
        const QRect& barRect)
    {
        painter.fillRect(
            barRect,
            QColor(28, 28, 28));

        const auto fillToleranceBand =
            [&](double targetUs)
            {
                const int left =
                    xForUs(
                        barRect,
                        targetUs -
                            kTimingToleranceUs);

                const int right =
                    xForUs(
                        barRect,
                        targetUs +
                            kTimingToleranceUs);

                painter.fillRect(
                    QRect(
                        left,
                        barRect.top(),
                        std::max(
                            right - left,
                            1),
                        barRect.height()),
                    QColor(30, 125, 48));

                const int targetX =
                    xForUs(
                        barRect,
                        targetUs);

                painter.setPen(
                    QColor(150, 150, 150));

                painter.drawLine(
                    targetX,
                    barRect.top() + 1,
                    targetX,
                    barRect.bottom() - 1);
            };

        fillToleranceBand(
            kField1DeadlineUs);

        fillToleranceBand(
            kField2DeadlineUs);
    }

    void drawFieldStatusMarker(
        QPainter& painter,
        const QRect& barRect,
        double valueUs,
        double targetUs)
    {
        const bool inRange =
            std::abs(
                valueUs - targetUs) <=
            kTimingToleranceUs;

        const int x =
            xForUs(
                barRect,
                targetUs);

        constexpr int markerSize = 14;

        const QRect markerRect(
            x - markerSize / 2,
            barRect.center().y() - markerSize / 2,
            markerSize,
            markerSize);

        QPen pen(
            inRange
            ? QColor(95, 215, 120)
            : QColor(235, 95, 95));

        pen.setWidth(2);
        pen.setCapStyle(
            Qt::RoundCap);
        pen.setJoinStyle(
            Qt::RoundJoin);

        painter.setPen(
            pen);

        if (inRange)
        {
            painter.drawLine(
                markerRect.left() + 2,
                markerRect.center().y(),
                markerRect.left() + 6,
                markerRect.bottom() - 2);

            painter.drawLine(
                markerRect.left() + 6,
                markerRect.bottom() - 2,
                markerRect.right() - 1,
                markerRect.top() + 2);
        }
        else
        {
            painter.drawLine(
                markerRect.left() + 2,
                markerRect.top() + 2,
                markerRect.right() - 2,
                markerRect.bottom() - 2);

            painter.drawLine(
                markerRect.right() - 2,
                markerRect.top() + 2,
                markerRect.left() + 2,
                markerRect.bottom() - 2);
        }
    }

    void drawSegment(
        QPainter& painter,
        const QRect& barRect,
        double startUs,
        double durationUs,
        const QColor& color,
        const char* shortLabel)
    {
        if (durationUs <= 0.0)
        {
            return;
        }

        const int left =
            xForUs(
                barRect,
                startUs);

        const int right =
            xForUs(
                barRect,
                startUs + durationUs);

        const int width =
            std::max(
                right - left,
                1);

        const QRect segmentRect(
            left,
            barRect.top() + 1,
            width,
            barRect.height() - 1);

        painter.fillRect(
            segmentRect,
            color);

        painter.setPen(
            QColor(35, 35, 35));

        painter.drawLine(
            segmentRect.topRight(),
            segmentRect.bottomRight());

        if (segmentRect.width() >= 11)
        {
            painter.setPen(
                Qt::black);

            painter.drawText(
                segmentRect,
                Qt::AlignCenter,
                QString::fromLatin1(
                    shortLabel));
        }
    }

    double nonNegativeRemainder(
        double readyUs,
        double knownUs)
    {
        return
            std::max(
                readyUs - knownUs,
                0.0);
    }
}

auto makeBars(
    const PerformanceSnapshot& snapshot)
{
    return std::array
    {
        Bar{
            "Field timing",
            nullptr,
            BarType::FieldTiming },

        Bar{
            "Field 1 ready @ 40ms",
            &snapshot.field1Ready,
            BarType::Field1Ready },

        Bar{
            "Field 2 ready @ 60ms",
            &snapshot.field2Ready,
            BarType::Field2Ready },

        Bar{
            "Display worker 1",
            nullptr,
            BarType::Worker0 },

        Bar{
            "Display worker 2",
            nullptr,
            BarType::Worker1 },

        Bar{
            "PC video",
            &snapshot.videoScreen,
            BarType::Normal },

        Bar{
            "PC waveform",
            &snapshot.waveformScreen,
            BarType::Normal,
            DetailKind::WaveformScreen },

        Bar{
            "PC vectorscope",
            &snapshot.vectorscopeScreen,
            BarType::Normal,
            DetailKind::VectorscopeScreen },

        Bar{
            "Waveform video",
            &snapshot.waveformVideo,
            BarType::Normal,
            DetailKind::WaveformVideo },

        Bar{
            "Vectorscope video",
            &snapshot.vectorscopeVideo,
            BarType::Normal,
            DetailKind::VectorscopeVideo },

        Bar{
            "Display compose",
            &snapshot.displayCompose,
            BarType::Normal },
    };
}


QString makePinnedDetailText(
    const PerformanceSnapshot& snapshot,
    int barIndex)
{
    const auto bars = makeBars(snapshot);

    if (barIndex < 0 ||
        barIndex >= static_cast<int>(bars.size()))
    {
        return {};
    }

    const Bar& bar = bars[barIndex];
    const QString label = QString::fromLatin1(bar.label);

    const auto makeWaveformDetails =
        [&snapshot, &bar, &label](
            const PerformanceMetricSnapshot& persistence,
            const PerformanceMetricSnapshot& trace,
            const PerformanceMetricSnapshot& compose,
            const PerformanceMetricSnapshot& glow,
            const PerformanceMetricSnapshot& overlay,
            const GlowWorkloadSnapshot& workload,
            bool traceParallel)
        {
            const double measuredMs =
                persistence.latestMs() +
                trace.latestMs() +
                compose.latestMs() +
                glow.latestMs() +
                overlay.latestMs();

            const double otherMs =
                std::max(
                    bar.metric->latestMs() - measuredMs,
                    0.0);

            const double coverage =
                workload.totalTiles > 0
                ? 100.0 *
                    static_cast<double>(workload.dirtyTiles) /
                    static_cast<double>(workload.totalTiles)
                : 0.0;

            const std::uint32_t processedVisits =
                workload.horizontalPass1Tiles +
                workload.verticalPass1Tiles +
                workload.horizontalPass2Tiles +
                workload.verticalPass2Tiles;

            const std::uint32_t totalVisits =
                workload.totalTiles * 4u;

            const double processedPercent =
                totalVisits > 0
                ? 100.0 * static_cast<double>(processedVisits) /
                    static_cast<double>(totalVisits)
                : 0.0;

            return QString(
                "%1: %2 ms\n"
                "Clear / persistence: %3 ms\n"
                "Trace / preparation: %4 ms (%5)\n"
                "Compose: %6 ms\n"
                "Glow: %7 ms\n"
                "Graticule / overlay: %8 ms\n"
                "Other: %9 ms\n"
                "Source dirty: %10 / %11 (%12%)\n"
                "Blur H1/V1/H2/V2: %13 / %14 / %15 / %16\n"
                "Processed visits: %17 / %18 (%19%)\n"
                "Active bbox: %20,%21  %22x%23 px")
                .arg(label)
                .arg(bar.metric->latestMs(), 0, 'f', 2)
                .arg(persistence.latestMs(), 0, 'f', 2)
                .arg(trace.latestMs(), 0, 'f', 2)
                .arg(traceParallel ? "parallel" : "scalar")
                .arg(compose.latestMs(), 0, 'f', 2)
                .arg(glow.latestMs(), 0, 'f', 2)
                .arg(overlay.latestMs(), 0, 'f', 2)
                .arg(otherMs, 0, 'f', 2)
                .arg(workload.dirtyTiles)
                .arg(workload.totalTiles)
                .arg(coverage, 0, 'f', 1)
                .arg(workload.horizontalPass1Tiles)
                .arg(workload.verticalPass1Tiles)
                .arg(workload.horizontalPass2Tiles)
                .arg(workload.verticalPass2Tiles)
                .arg(processedVisits)
                .arg(totalVisits)
                .arg(processedPercent, 0, 'f', 1)
                .arg(workload.activeX)
                .arg(workload.activeY)
                .arg(workload.activeWidth)
                .arg(workload.activeHeight);
        };

    const auto makeVectorscopeDetails =
        [&bar, &label](
            const PerformanceMetricSnapshot& analyzer,
            const PerformanceMetricSnapshot& glowPersistence,
            const PerformanceMetricSnapshot& compose,
            const PerformanceMetricSnapshot& overlay,
            const GlowWorkloadSnapshot& workload)
        {
            const double measuredMs =
                analyzer.latestMs() +
                glowPersistence.latestMs() +
                compose.latestMs() +
                overlay.latestMs();

            const double otherMs =
                std::max(
                    bar.metric->latestMs() - measuredMs,
                    0.0);

            const double coverage =
                workload.totalTiles > 0
                ? 100.0 *
                    static_cast<double>(workload.dirtyTiles) /
                    static_cast<double>(workload.totalTiles)
                : 0.0;

            const std::uint32_t processedVisits =
                workload.horizontalPass1Tiles +
                workload.verticalPass1Tiles +
                workload.horizontalPass2Tiles +
                workload.verticalPass2Tiles;

            const std::uint32_t totalVisits =
                workload.totalTiles * 4u;

            const double processedPercent =
                totalVisits > 0
                ? 100.0 * static_cast<double>(processedVisits) /
                    static_cast<double>(totalVisits)
                : 0.0;

            return QString(
                "%1: %2 ms\n"
                "Density / trace: %3 ms\n"
                "Glow / persistence: %4 ms\n"
                "Compose: %5 ms\n"
                "Graticule / overlay: %6 ms\n"
                "Other: %7 ms\n"
                "Source dirty: %8 / %9 (%10%)\n"
                "Blur H1/V1/H2/V2: %11 / %12 / %13 / %14\n"
                "Processed visits: %15 / %16 (%17%)\n"
                "Active bbox: %18,%19  %20x%21 px")
                .arg(label)
                .arg(bar.metric->latestMs(), 0, 'f', 2)
                .arg(analyzer.latestMs(), 0, 'f', 2)
                .arg(glowPersistence.latestMs(), 0, 'f', 2)
                .arg(compose.latestMs(), 0, 'f', 2)
                .arg(overlay.latestMs(), 0, 'f', 2)
                .arg(otherMs, 0, 'f', 2)
                .arg(workload.dirtyTiles)
                .arg(workload.totalTiles)
                .arg(coverage, 0, 'f', 1)
                .arg(workload.horizontalPass1Tiles)
                .arg(workload.verticalPass1Tiles)
                .arg(workload.horizontalPass2Tiles)
                .arg(workload.verticalPass2Tiles)
                .arg(processedVisits)
                .arg(totalVisits)
                .arg(processedPercent, 0, 'f', 1)
                .arg(workload.activeX)
                .arg(workload.activeY)
                .arg(workload.activeWidth)
                .arg(workload.activeHeight);
        };

    switch (bar.detail)
    {
    case DetailKind::WaveformScreen:
        return makeWaveformDetails(
            snapshot.waveformScreenPersistence,
            snapshot.waveformScreenTrace,
            snapshot.waveformScreenCompose,
            snapshot.waveformScreenGlow,
            snapshot.waveformScreenOverlay,
            snapshot.waveformScreenGlowWorkload,
            snapshot.waveformScreenTraceParallel) +
            QString(
                "\nTrace prep/raster: %1 / %2 ms"
                "\nOutput resize %3   buf grow %4   cache %5   jobs %6"
                "\nBeam %7 px   margin %8 px"
                "\nCatWuzle %9  inv %10  zip %11 ms  W0/W1/W2 %16/%17/%18"
                "\nW ms %19/%20/%21   chunk %12/%13/%14 ms   qwait %15 ms"
                "\nassist %22 ms   final W %23 ms")
                .arg(snapshot.waveformScreenTracePrep.latestMs(), 0, 'f', 2)
                .arg(snapshot.waveformScreenTraceRaster.latestMs(), 0, 'f', 2)
                .arg(snapshot.waveformScreenOutputSizeChanged
                    ? QStringLiteral("YES")
                    : QStringLiteral("no"))
                .arg(snapshot.waveformScreenOutputBufferCapacityGrew
                    ? QStringLiteral("YES")
                    : QStringLiteral("no"))
                .arg(snapshot.waveformScreenResamplerCacheRebuilt
                    ? QStringLiteral("REBUILD")
                    : QStringLiteral("HIT"))
                .arg(snapshot.waveformScreenTraceJobCount)
                .arg(snapshot.waveformScreenBeamCoreRadiusPx, 0, 'f', 2)
                .arg(snapshot.waveformScreenBeamCoreMarginPx)
                .arg(snapshot.waveformScreenCatWuzleChunkCount)
                .arg(snapshot.waveformScreenCatWuzleInvalidChunkCount)
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleZipperUs) / 1000.0,
                    0,
                    'f',
                    2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleChunkRenderMinUs) / 1000.0,
                    0,
                    'f',
                    2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleChunkRenderAvgUs) / 1000.0,
                    0,
                    'f',
                    2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleChunkRenderMaxUs) / 1000.0,
                    0,
                    'f',
                    2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleChunkQueueWaitMaxUs) / 1000.0,
                    0,
                    'f',
                    2)
                .arg(snapshot.waveformScreenCatWuzleWorkerChunkCount[0])
                .arg(snapshot.waveformScreenCatWuzleWorkerChunkCount[1])
                .arg(snapshot.waveformScreenCatWuzleWorkerChunkCount[2])
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleWorkerRenderUs[0]) / 1000.0,
                    0, 'f', 2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleWorkerRenderUs[1]) / 1000.0,
                    0, 'f', 2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenCatWuzleWorkerRenderUs[2]) / 1000.0,
                    0, 'f', 2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenAssistTotalUs) / 1000.0,
                    0, 'f', 2)
                .arg(
                    static_cast<double>(snapshot.waveformScreenAssistFinalWaitUs) / 1000.0,
                    0, 'f', 2);

    case DetailKind::WaveformVideo:
        return makeWaveformDetails(
            snapshot.waveformVideoPersistence,
            snapshot.waveformVideoTrace,
            snapshot.waveformVideoCompose,
            snapshot.waveformVideoGlow,
            snapshot.waveformVideoOverlay,
            snapshot.waveformVideoGlowWorkload,
            false);

    case DetailKind::VectorscopeScreen:
        return makeVectorscopeDetails(
            snapshot.vectorscopeScreenAnalyzer,
            snapshot.vectorscopeScreenGlowPersistence,
            snapshot.vectorscopeScreenCompose,
            snapshot.vectorscopeScreenOverlay,
            snapshot.vectorscopeScreenGlowWorkload);

    case DetailKind::VectorscopeVideo:
        return makeVectorscopeDetails(
            snapshot.vectorscopeVideoAnalyzer,
            snapshot.vectorscopeVideoGlowPersistence,
            snapshot.vectorscopeVideoCompose,
            snapshot.vectorscopeVideoOverlay,
            snapshot.vectorscopeVideoGlowWorkload);

    case DetailKind::None:
    default:
        return {};
    }
}

PerformanceWidget::PerformanceWidget(
    QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(
        980,
        760);

    setMouseTracking(
        true);
}

void PerformanceWidget::setPerformanceSnapshot(
    const PerformanceSnapshot& snapshot)
{
    snapshot_ = snapshot;

    if (snapshot.waveformScreenTrace.latestUs > 0 &&
        snapshot.waveformScreenWidth > 0 &&
        snapshot.waveformScreenHeight > 0)
    {
        const double megaPixels =
            (static_cast<double>(snapshot.waveformScreenWidth) *
                static_cast<double>(snapshot.waveformScreenHeight)) /
            1'000'000.0;

        traceHistory_.push_back(
            {
                snapshot.waveformScreenTrace.latestMs(),
                megaPixels,
                snapshot.waveformScreenTraceParallel
            });

        constexpr std::size_t kMaximumTraceHistorySamples = 240;

        while (traceHistory_.size() > kMaximumTraceHistorySamples)
        {
            traceHistory_.pop_front();
        }
    }

    update();
}

void PerformanceWidget::paintEvent(
    QPaintEvent* /*event*/)
{
    QPainter painter(this);

    painter.fillRect(
        rect(),
        Qt::black);

    constexpr int barHeight =
        18;

    constexpr int spacing =
        12;

    const auto bars =
        makeBars(
            snapshot_);

    const QFontMetrics fontMetrics(
        painter.font());

    int labelWidth = 0;

    for (const Bar& bar : bars)
    {
        labelWidth =
            std::max(
                labelWidth,
                fontMetrics.horizontalAdvance(
                    bar.label));
    }

    labelWidth +=
        12;

    constexpr int margin =
        12;

    int y = margin;

    const int barLeft =
        margin + labelWidth;

    const int barWidth =
        width() -
        labelWidth -
        margin * 2;

    const int scaleY =
        y;

    painter.setPen(
        Qt::lightGray);

    const auto drawScaleLabel =
        [&](double us,
            const QString& text,
            Qt::Alignment alignment)
        {
            const int x =
                barLeft +
                static_cast<int>(
                    std::lround(
                        (us / kMaximumUs) *
                        static_cast<double>(
                            barWidth)));

            QRect textRect(
                x - 30,
                scaleY,
                60,
                barHeight);

            if (alignment & Qt::AlignLeft)
            {
                textRect.moveLeft(
                    x);
            }
            else if (alignment & Qt::AlignRight)
            {
                textRect.moveRight(
                    x);
            }

            painter.drawText(
                textRect,
                alignment |
                Qt::AlignVCenter,
                text);
        };

    drawScaleLabel(
        0.0,
        "0",
        Qt::AlignLeft);

    drawScaleLabel(
        20000.0,
        "20",
        Qt::AlignHCenter);

    drawScaleLabel(
        40000.0,
        "40",
        Qt::AlignHCenter);

    drawScaleLabel(
        60000.0,
        "60",
        Qt::AlignHCenter);

    drawScaleLabel(
        80000.0,
        "80 ms",
        Qt::AlignRight);

    y +=
        barHeight +
        6;

    for (const Bar& bar : bars)
    {
        painter.setPen(
            Qt::white);

        painter.drawText(
            QRect(
                margin,
                y,
                labelWidth,
                barHeight),
            Qt::AlignVCenter |
            Qt::AlignLeft,
            bar.label);

        const QRect barRect(
            margin + labelWidth,
            y,
            width() -
            labelWidth -
            margin * 2,
            barHeight);

        switch (bar.type)
        {
        case BarType::FieldTiming:
            fillFieldTimingBackground(
                painter,
                barRect);
            break;

        case BarType::Field1Ready:
            fillTimingBackground(
                painter,
                barRect,
                35000.0,
                kField1DeadlineUs);
            break;

        case BarType::Field2Ready:
            fillTimingBackground(
                painter,
                barRect,
                55000.0,
                kField2DeadlineUs);
            break;

        case BarType::Worker0:
        case BarType::Worker1:
            painter.fillRect(
                barRect,
                QColor(25, 25, 25));
            break;

        case BarType::Normal:
        default:
            fillTimingBackground(
                painter,
                barRect,
                40000.0,
                60000.0);
            break;
        }

        if (bar.type == BarType::FieldTiming)
        {
            drawFieldStatusMarker(
                painter,
                barRect,
                static_cast<double>(
                    snapshot_.field1Present.latestUs),
                kField1DeadlineUs);

            drawFieldStatusMarker(
                painter,
                barRect,
                static_cast<double>(
                    snapshot_.field2Present.latestUs),
                kField2DeadlineUs);
        }
        else if (bar.type == BarType::Field1Ready ||
            bar.type == BarType::Field2Ready)
        {
            const double noiseUs =
                static_cast<double>(
                    snapshot_.noiseReduction.latestUs);

            const double deinterlaceUs =
                static_cast<double>(
                    snapshot_.deinterlace.latestUs);

            const double firstConvertUs =
                static_cast<double>(
                    snapshot_.displayFirst.latestUs);

            const double secondConvertUs =
                bar.type == BarType::Field2Ready
                ? static_cast<double>(
                    snapshot_.displaySecond.latestUs)
                : 0.0;

            const double convertUs =
                firstConvertUs +
                secondConvertUs;

            const double firstSpoutUs =
                static_cast<double>(
                    snapshot_.spoutConvertFirst.latestUs);

            const double secondSpoutUs =
                bar.type == BarType::Field2Ready
                ? static_cast<double>(
                    snapshot_.spoutConvertSecond.latestUs)
                : 0.0;

            const double spoutUs =
                firstSpoutUs +
                secondSpoutUs;

            const double readyUs =
                static_cast<double>(
                    bar.metric->latestUs);

            const double knownUs =
                noiseUs +
                deinterlaceUs +
                convertUs +
                spoutUs;

            const double overheadUs =
                nonNegativeRemainder(
                    readyUs,
                    knownUs);

            double startUs = 0.0;

            drawSegment(
                painter,
                barRect,
                startUs,
                noiseUs,
                QColor(185, 225, 205),
                "N");

            startUs +=
                noiseUs;

            drawSegment(
                painter,
                barRect,
                startUs,
                deinterlaceUs,
                QColor(185, 215, 235),
                "D");

            startUs +=
                deinterlaceUs;

            drawSegment(
                painter,
                barRect,
                startUs,
                convertUs,
                QColor(215, 200, 235),
                bar.type == BarType::Field1Ready
                    ? "C1"
                    : "C1+C2");

            startUs +=
                convertUs;

            drawSegment(
                painter,
                barRect,
                startUs,
                spoutUs,
                QColor(205, 225, 185),
                bar.type == BarType::Field1Ready
                    ? "S1"
                    : "S1+S2");

            startUs +=
                spoutUs;

            drawSegment(
                painter,
                barRect,
                startUs,
                overheadUs,
                QColor(205, 205, 205),
                "O");
        }
        else if (bar.type == BarType::Worker0 ||
            bar.type == BarType::Worker1)
        {
            const bool firstWorker =
                bar.type == BarType::Worker0;

            const double noiseUs = static_cast<double>(
                firstWorker
                    ? snapshot_.displayWorker0Noise.latestUs
                    : snapshot_.displayWorker1Noise.latestUs);
            const double deinterlaceUs = static_cast<double>(
                firstWorker
                    ? snapshot_.displayWorker0Deinterlace.latestUs
                    : snapshot_.displayWorker1Deinterlace.latestUs);
            const double convert1Us = static_cast<double>(
                firstWorker
                    ? snapshot_.displayWorker0Convert1.latestUs
                    : snapshot_.displayWorker1Convert1.latestUs);
            const double spout1Us = static_cast<double>(
                firstWorker
                    ? snapshot_.displayWorker0Spout1.latestUs
                    : snapshot_.displayWorker1Spout1.latestUs);
            const double convert2Us = static_cast<double>(
                firstWorker
                    ? snapshot_.displayWorker0Convert2.latestUs
                    : snapshot_.displayWorker1Convert2.latestUs);
            const double spout2Us = static_cast<double>(
                firstWorker
                    ? snapshot_.displayWorker0Spout2.latestUs
                    : snapshot_.displayWorker1Spout2.latestUs);

            double startUs = 0.0;

            drawSegment(painter, barRect, startUs, noiseUs,
                QColor(185, 225, 205), "N");
            startUs += noiseUs;
            drawSegment(painter, barRect, startUs, deinterlaceUs,
                QColor(185, 215, 235), "D");
            startUs += deinterlaceUs;
            drawSegment(painter, barRect, startUs, convert1Us,
                QColor(215, 200, 235), "C1");
            startUs += convert1Us;
            drawSegment(painter, barRect, startUs, spout1Us,
                QColor(205, 225, 185), "S1");
            startUs += spout1Us;
            drawSegment(painter, barRect, startUs, convert2Us,
                QColor(205, 190, 230), "C2");
            startUs += convert2Us;
            drawSegment(painter, barRect, startUs, spout2Us,
                QColor(190, 215, 170), "S2");

            drawAssistChunkMarkers(
                painter,
                barRect,
                firstWorker
                    ? snapshot_.displayWorker0Assist
                    : snapshot_.displayWorker1Assist,
                snapshot_.waveformScreenPhases.generation);
        }
        else if (bar.detail == DetailKind::WaveformScreen)
        {
            // Real chronology on the same capture-relative 0..80 ms axis as
            // the display-worker chunk markers.  X is laid down first as the
            // full waveform wall-time envelope; measured phases then replace
            // it at their actual start/end timestamps.
            const auto phaseColor =
                [](char label) -> QColor
                {
                    switch (label)
                    {
                    case 'T': return QColor(205, 215, 235);
                    case 'R': return QColor(220, 220, 220);
                    case 'Z': return QColor(205, 190, 230);
                    case 'P': return QColor(185, 225, 205);
                    case 'C': return QColor(205, 225, 185);
                    case 'O': return QColor(225, 205, 205);
                    default:  return QColor(195, 195, 195);
                    }
                };

            for (std::uint32_t i = 0;
                i < snapshot_.waveformScreenPhases.count &&
                i < WaveformPhaseTimelineSnapshot::kCapacity;
                ++i)
            {
                const auto& event =
                    snapshot_.waveformScreenPhases.events[i];
                const char label =
                    static_cast<char>(event.label);
                const char labelText[2] = { label, '\0' };

                drawSegment(
                    painter,
                    barRect,
                    static_cast<double>(event.startUs),
                    static_cast<double>(event.durationUs),
                    phaseColor(label),
                    labelText);
            }

            // W is the actual final wait after W0 has no local chunk left and
            // outstanding helper chunks still have to return.  Its timestamp
            // is capture-relative too, so it can be compared vertically with
            // W1/W2 without any synthetic stacking.
            const double finalWaitStartUs =
                static_cast<double>(
                    snapshot_.waveformScreenAssistFinalWaitStartUs);
            const double finalWaitUs =
                static_cast<double>(
                    snapshot_.waveformScreenAssistFinalWaitUs);
            const bool assistGenerationMatches =
                snapshot_.waveformScreenPhases.generation != 0 &&
                snapshot_.displayWorker0Assist.generation ==
                    snapshot_.waveformScreenPhases.generation &&
                snapshot_.displayWorker1Assist.generation ==
                    snapshot_.waveformScreenPhases.generation;

            if (finalWaitUs > 0.0 && assistGenerationMatches)
            {
                drawSegment(
                    painter,
                    barRect,
                    finalWaitStartUs,
                    finalWaitUs,
                    QColor(235, 205, 150),
                    "W");
            }
        }
        else
        {
            const int valueX =
                xForUs(
                    barRect,
                    static_cast<double>(
                        bar.metric->latestUs));

            const int valueWidth =
                std::max(
                    valueX -
                    barRect.left(),
                    0);

            painter.fillRect(
                QRect(
                    barRect.left(),
                    barRect.top(),
                    valueWidth,
                    barRect.height()),
                QColor(220, 220, 220));
        }

        painter.setPen(
            Qt::gray);

        painter.drawRect(
            barRect);

        y +=
            barHeight +
            spacing;
    }

    painter.setPen(
        Qt::lightGray);

    painter.drawText(
        QRect(
            margin,
            y,
            width() - margin * 2,
            barHeight),
        Qt::AlignLeft |
        Qt::AlignVCenter,
        QString(
            "Deadline misses: F1 %1   F2 %2")
        .arg(
            snapshot_.field1DeadlineMisses)
        .arg(
            snapshot_.field2DeadlineMisses));

    y += barHeight;

    painter.drawText(
        QRect(
            margin,
            y,
            width() - margin * 2,
            barHeight),
        Qt::AlignLeft |
        Qt::AlignVCenter,
        QString(
            "Spout actual: queue %1 ms   send %2 ms   cadence %3 ms")
        .arg(
            snapshot_.spoutQueueDelay.latestMs(),
            0,
            'f',
            2)
        .arg(
            snapshot_.spoutSend.latestMs(),
            0,
            'f',
            2)
        .arg(
            snapshot_.spoutInterval.latestMs(),
            0,
            'f',
            2));

    y += barHeight + 8;

    const QString liveDetails =
        makePinnedDetailText(
            snapshot_,
            pinnedDetailBarIndex_);

    if (!liveDetails.isEmpty())
    {
        const auto detailBars =
            makeBars(snapshot_);

        const bool waveformScreenPinned =
            pinnedDetailBarIndex_ >= 0 &&
            pinnedDetailBarIndex_ < static_cast<int>(detailBars.size()) &&
            detailBars[pinnedDetailBarIndex_].detail == DetailKind::WaveformScreen;

        constexpr int detailTextHeight = 285;
        constexpr int detailGap = 8;

        const bool showTraceGraph = false;

        const int availableWidth =
            width() - margin * 2;

        const int detailWidth =
            showTraceGraph
                ? std::max(420, availableWidth * 45 / 100)
                : availableWidth;

        const QRect detailRect(
            margin,
            y,
            detailWidth,
            detailTextHeight);

        painter.fillRect(
            detailRect,
            QColor(30, 30, 30));

        painter.setPen(
            QColor(95, 95, 95));
        painter.drawRect(
            detailRect.adjusted(0, 0, -1, -1));

        painter.setPen(
            QColor(220, 220, 220));

        painter.drawText(
            detailRect.adjusted(8, 6, -8, -6),
            Qt::AlignLeft | Qt::AlignTop,
            QStringLiteral("LIVE - click a scope bar to pin\n") +
                liveDetails);

        if (showTraceGraph)
        {
            const int graphLeft =
                detailRect.right() + 1 + detailGap;

            const QRect graphRect(
                graphLeft,
                y,
                std::max(availableWidth - detailWidth - detailGap, 320),
                detailTextHeight);

            painter.fillRect(
                graphRect,
                QColor(22, 22, 22));

            painter.setPen(
                QColor(80, 80, 80));
            painter.drawRect(
                graphRect.adjusted(0, 0, -1, -1));

            const int graphMarginLeft = 48;
            const int graphMarginRight = 58;
            const int graphMarginTop = 62;
            const int graphMarginBottom = 24;

            const QRect plotRect =
                graphRect.adjusted(
                    graphMarginLeft,
                    graphMarginTop,
                    -graphMarginRight,
                    -graphMarginBottom);

            double maximumTraceMs = 20.0;
            double maximumMegaPixels = 1.0;

            for (const TraceHistorySample& sample : traceHistory_)
            {
                maximumTraceMs =
                    std::max(maximumTraceMs, sample.traceMs);
                maximumMegaPixels =
                    std::max(maximumMegaPixels, sample.megaPixels);
            }

            maximumTraceMs =
                std::ceil(maximumTraceMs / 10.0) * 10.0;
            maximumMegaPixels =
                std::ceil(maximumMegaPixels * 2.0) / 2.0;

            const TraceHistorySample& latest =
                traceHistory_.back();

            const QFont savedGraphFont = painter.font();

            QFont traceStatusFont = savedGraphFont;
            traceStatusFont.setPointSizeF(
                std::max(
                    savedGraphFont.pointSizeF() * 1.55,
                    14.0));
            traceStatusFont.setBold(true);
            painter.setFont(traceStatusFont);

            painter.setPen(QColor(235, 235, 235));
            painter.drawText(
                QRect(
                    graphRect.left() + 8,
                    graphRect.top() + 2,
                    graphRect.width() - 16,
                    28),
                Qt::AlignLeft | Qt::AlignVCenter,
                QString(
                    "%1x%2   %3 MP   TRACE %4 ms   RASTER %5 ms   %6")
                    .arg(snapshot_.waveformScreenWidth)
                    .arg(snapshot_.waveformScreenHeight)
                    .arg(latest.megaPixels, 0, 'f', 3)
                    .arg(latest.traceMs, 0, 'f', 2)
                    .arg(snapshot_.waveformScreenTraceRaster.latestMs(), 0, 'f', 2)
                    .arg(latest.parallel
                        ? QStringLiteral("PARALLEL")
                        : QStringLiteral("SCALAR")));

            QFont traceDiagnosticFont = savedGraphFont;
            traceDiagnosticFont.setPointSizeF(
                std::max(
                    savedGraphFont.pointSizeF() * 1.30,
                    12.0));
            painter.setFont(traceDiagnosticFont);

            painter.setPen(QColor(190, 190, 190));
            painter.drawText(
                QRect(
                    graphRect.left() + 8,
                    graphRect.top() + 31,
                    graphRect.width() - 16,
                    24),
                Qt::AlignLeft | Qt::AlignVCenter,
                QString(
                    "prep %1 ms   resize %2   buf+ %3   cache %4   jobs %5   beam %6 / margin %7")
                    .arg(snapshot_.waveformScreenTracePrep.latestMs(), 0, 'f', 2)
                    .arg(snapshot_.waveformScreenOutputSizeChanged
                        ? QStringLiteral("YES")
                        : QStringLiteral("no"))
                    .arg(snapshot_.waveformScreenOutputBufferCapacityGrew
                        ? QStringLiteral("YES")
                        : QStringLiteral("no"))
                    .arg(snapshot_.waveformScreenResamplerCacheRebuilt
                        ? QStringLiteral("REBUILD")
                        : QStringLiteral("HIT"))
                    .arg(snapshot_.waveformScreenTraceJobCount)
                    .arg(snapshot_.waveformScreenBeamCoreRadiusPx, 0, 'f', 2)
                    .arg(snapshot_.waveformScreenBeamCoreMarginPx));

            painter.setFont(savedGraphFont);

            painter.setPen(QColor(55, 55, 55));

            for (int grid = 0; grid <= 4; ++grid)
            {
                const int gridY =
                    plotRect.bottom() -
                    (plotRect.height() * grid) / 4;

                painter.drawLine(
                    plotRect.left(),
                    gridY,
                    plotRect.right(),
                    gridY);
            }

            painter.setPen(QColor(170, 170, 170));
            painter.drawText(
                QRect(
                    graphRect.left() + 4,
                    plotRect.top() - 8,
                    graphMarginLeft - 8,
                    18),
                Qt::AlignRight | Qt::AlignVCenter,
                QString("%1 ms").arg(maximumTraceMs, 0, 'f', 0));
            painter.drawText(
                QRect(
                    graphRect.left() + 4,
                    plotRect.bottom() - 9,
                    graphMarginLeft - 8,
                    18),
                Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("0"));

            painter.drawText(
                QRect(
                    plotRect.right() + 6,
                    plotRect.top() - 8,
                    graphMarginRight - 10,
                    18),
                Qt::AlignLeft | Qt::AlignVCenter,
                QString("%1 MP").arg(maximumMegaPixels, 0, 'f', 1));
            painter.drawText(
                QRect(
                    plotRect.right() + 6,
                    plotRect.bottom() - 9,
                    graphMarginRight - 10,
                    18),
                Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("0"));

            painter.setPen(QColor(95, 95, 95));
            painter.drawRect(plotRect);

            if (traceHistory_.size() >= 2 &&
                plotRect.width() > 1 &&
                plotRect.height() > 1)
            {
                QPolygonF tracePolyline;
                QPolygonF megaPixelPolyline;

                tracePolyline.reserve(
                    static_cast<int>(traceHistory_.size()));
                megaPixelPolyline.reserve(
                    static_cast<int>(traceHistory_.size()));

                const double divisor =
                    static_cast<double>(traceHistory_.size() - 1);

                for (std::size_t index = 0;
                    index < traceHistory_.size();
                    ++index)
                {
                    const TraceHistorySample& sample =
                        traceHistory_[index];

                    const double normalizedX =
                        static_cast<double>(index) / divisor;

                    const double x =
                        static_cast<double>(plotRect.left()) +
                        normalizedX *
                            static_cast<double>(plotRect.width());

                    const double traceY =
                        static_cast<double>(plotRect.bottom()) -
                        std::clamp(
                            sample.traceMs / maximumTraceMs,
                            0.0,
                            1.0) *
                            static_cast<double>(plotRect.height());

                    const double megaPixelY =
                        static_cast<double>(plotRect.bottom()) -
                        std::clamp(
                            sample.megaPixels / maximumMegaPixels,
                            0.0,
                            1.0) *
                            static_cast<double>(plotRect.height());

                    tracePolyline.append(QPointF(x, traceY));
                    megaPixelPolyline.append(QPointF(x, megaPixelY));
                }

                QPen tracePen(QColor(245, 245, 245));
                tracePen.setWidth(2);
                painter.setPen(tracePen);
                painter.drawPolyline(tracePolyline);

                QPen megaPixelPen(QColor(75, 190, 220));
                megaPixelPen.setWidth(1);
                painter.setPen(megaPixelPen);
                painter.drawPolyline(megaPixelPolyline);
            }

            painter.setPen(QColor(210, 210, 210));
            painter.drawText(
                QRect(
                    plotRect.left(),
                    plotRect.bottom() + 3,
                    plotRect.width(),
                    graphMarginBottom - 3),
                Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("white = Trace ms"));
            painter.setPen(QColor(75, 190, 220));
            painter.drawText(
                QRect(
                    plotRect.left(),
                    plotRect.bottom() + 3,
                    plotRect.width(),
                    graphMarginBottom - 3),
                Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("cyan = MPixels"));
        }

        y += detailTextHeight + 8;
    }

}

void PerformanceWidget::mouseMoveEvent(
    QMouseEvent* event)
{
    constexpr int barHeight = 18;
    constexpr int spacing = 12;
    constexpr int margin = 12;

    constexpr int scaleHeight =
        barHeight + 6;

    const int firstBarY =
        margin +
        scaleHeight;

    const int relativeY =
        event->position().toPoint().y() -
        firstBarY;

    if (relativeY < 0)
    {
        QToolTip::hideText();
        return;
    }

    const int rowHeight =
        barHeight +
        spacing;

    const int index =
        relativeY /
        rowHeight;

    const auto bars =
        makeBars(
            snapshot_);

    if (index < 0 ||
        index >= static_cast<int>(
            bars.size()) ||
        (relativeY % rowHeight) >=
            barHeight)
    {
        QToolTip::hideText();
        return;
    }

    const Bar& bar =
        bars[index];

    QString valueText;

    if (bar.type == BarType::FieldTiming)
    {
        const double field1PresentMs =
            snapshot_.field1Present.latestMs();

        const double field2PresentMs =
            snapshot_.field2Present.latestMs();

        const double presentIntervalMs =
            snapshot_.presentInterval.latestMs();

        const double field1DeltaMs =
            field1PresentMs - 40.0;

        const double field2DeltaMs =
            field2PresentMs - 60.0;

        const bool field1InRange =
            std::abs(field1DeltaMs) <= 2.0;

        const bool field2InRange =
            std::abs(field2DeltaMs) <= 2.0;

        valueText =
            QString(
                "Field 1 present: %1ms\n"
                "Target: 40ms   Delta: %2%3ms   %4\n\n"
                "Field 2 present: %5ms\n"
                "Target: 60ms   Delta: %6%7ms   %8\n\n"
                "F1 -> F2 interval: %9ms")
            .arg(
                field1PresentMs,
                0,
                'f',
                2)
            .arg(
                field1DeltaMs >= 0.0
                ? "+"
                : "")
            .arg(
                field1DeltaMs,
                0,
                'f',
                2)
            .arg(
                field1InRange
                ? "OK"
                : "OUT")
            .arg(
                field2PresentMs,
                0,
                'f',
                2)
            .arg(
                field2DeltaMs >= 0.0
                ? "+"
                : "")
            .arg(
                field2DeltaMs,
                0,
                'f',
                2)
            .arg(
                field2InRange
                ? "OK"
                : "OUT")
            .arg(
                presentIntervalMs,
                0,
                'f',
                2);
    }
    else if (bar.type == BarType::Field1Ready ||
        bar.type == BarType::Field2Ready)
    {
        const double readyMs =
            bar.metric->latestMs();

        const double noiseMs =
            snapshot_.noiseReduction.latestMs();

        const double deinterlaceMs =
            snapshot_.deinterlace.latestMs();

        const double firstConvertMs =
            snapshot_.displayFirst.latestMs();

        const double secondConvertMs =
            bar.type == BarType::Field2Ready
            ? snapshot_.displaySecond.latestMs()
            : 0.0;

        const double convertMs =
            firstConvertMs +
            secondConvertMs;

        const double firstSpoutMs =
            snapshot_.spoutConvertFirst.latestMs();

        const double secondSpoutMs =
            bar.type == BarType::Field2Ready
            ? snapshot_.spoutConvertSecond.latestMs()
            : 0.0;

        const double spoutMs =
            firstSpoutMs +
            secondSpoutMs;

        const double overheadMs =
            std::max(
                readyMs -
                noiseMs -
                deinterlaceMs -
                convertMs -
                spoutMs,
                0.0);

        const double deadlineMs =
            bar.type == BarType::Field1Ready
            ? 40.0
            : 60.0;

        const double marginMs =
            deadlineMs -
            readyMs;

        valueText =
            QString(
                "%1: %2ms\n"
                "N  Noise reduction: %3ms\n"
                "D  Deinterlace: %4ms\n"
                "%7  Convert: %5ms\n"
                "%8  Spout RGB: %9ms\n"
                "O  Other / overhead: %6ms")
            .arg(
                bar.type == BarType::Field1Ready
                ? "Field 1 ready"
                : "Field 2 ready")
            .arg(
                readyMs,
                0,
                'f',
                2)
            .arg(
                noiseMs,
                0,
                'f',
                2)
            .arg(
                deinterlaceMs,
                0,
                'f',
                2)
            .arg(
                convertMs,
                0,
                'f',
                2)
            .arg(
                overheadMs,
                0,
                'f',
                2)
            .arg(
                bar.type == BarType::Field1Ready
                    ? "C1"
                    : "C1+C2")
            .arg(
                bar.type == BarType::Field1Ready
                    ? "S1"
                    : "S1+S2")
            .arg(
                spoutMs,
                0,
                'f',
                2);

        if (bar.type == BarType::Field2Ready)
        {
            valueText +=
                QString(
                    "\n   Field 1 convert: %1ms"
                    "\n   Field 2 convert: %2ms")
                .arg(
                    firstConvertMs,
                    0,
                    'f',
                    2)
                .arg(
                    secondConvertMs,
                    0,
                    'f',
                    2);
        }

        valueText +=
            QString(
                "\n\nDeadline: %1ms"
                "\nMargin: %2%3ms")
            .arg(
                deadlineMs,
                0,
                'f',
                0)
            .arg(
                marginMs >= 0.0
                ? "+"
                : "")
            .arg(
                marginMs,
                0,
                'f',
                2);
    }
    else if (bar.type == BarType::Worker0 ||
        bar.type == BarType::Worker1)
    {
        const bool firstWorker =
            bar.type == BarType::Worker0;

        const auto& n = firstWorker ? snapshot_.displayWorker0Noise : snapshot_.displayWorker1Noise;
        const auto& d = firstWorker ? snapshot_.displayWorker0Deinterlace : snapshot_.displayWorker1Deinterlace;
        const auto& c1 = firstWorker ? snapshot_.displayWorker0Convert1 : snapshot_.displayWorker1Convert1;
        const auto& s1 = firstWorker ? snapshot_.displayWorker0Spout1 : snapshot_.displayWorker1Spout1;
        const auto& c2 = firstWorker ? snapshot_.displayWorker0Convert2 : snapshot_.displayWorker1Convert2;
        const auto& s2 = firstWorker ? snapshot_.displayWorker0Spout2 : snapshot_.displayWorker1Spout2;

        const double totalMs =
            n.latestMs() + d.latestMs() + c1.latestMs() +
            s1.latestMs() + c2.latestMs() + s2.latestMs();

        const auto& assistTimeline =
            firstWorker
                ? snapshot_.displayWorker0Assist
                : snapshot_.displayWorker1Assist;

        valueText =
            QString(
                "Display worker %1: %2ms\n"
                "N   Noise: %3ms\n"
                "D   Deinterlace: %4ms\n"
                "C1  Field 1 RGB: %5ms\n"
                "S1  Field 1 Spout RGB: %6ms\n"
                "C2  Field 2 RGB: %7ms\n"
                "S2  Field 2 Spout RGB: %8ms\n"
                "Chunks: %9")
            .arg(firstWorker ? 1 : 2)
            .arg(totalMs, 0, 'f', 2)
            .arg(n.latestMs(), 0, 'f', 2)
            .arg(d.latestMs(), 0, 'f', 2)
            .arg(c1.latestMs(), 0, 'f', 2)
            .arg(s1.latestMs(), 0, 'f', 2)
            .arg(c2.latestMs(), 0, 'f', 2)
            .arg(s2.latestMs(), 0, 'f', 2)
            .arg(assistTimeline.count);
    }
    else
    {
        const QString label =
            QString::fromLatin1(
                bar.label);

        const auto makeWaveformDetails =
            [&](
                const PerformanceMetricSnapshot& persistence,
                const PerformanceMetricSnapshot& trace,
                const PerformanceMetricSnapshot& compose,
                const PerformanceMetricSnapshot& glow,
                const PerformanceMetricSnapshot& overlay,
                const GlowWorkloadSnapshot& workload,
                bool traceParallel)
            {
                const double measuredMs =
                    persistence.latestMs() +
                    trace.latestMs() +
                    compose.latestMs() +
                    glow.latestMs() +
                    overlay.latestMs();

                const double otherMs =
                    std::max(
                        bar.metric->latestMs() - measuredMs,
                        0.0);

                const double coverage =
                    workload.totalTiles > 0
                    ? 100.0 *
                        static_cast<double>(workload.dirtyTiles) /
                        static_cast<double>(workload.totalTiles)
                    : 0.0;

                return QString(
                    "%1: %2ms\n"
                    "Clear / persistence: %3ms\n"
                    "Trace / preparation: %4ms (%23)\n"
                    "Compose: %5ms\n"
                    "Glow: %6ms\n"
                    "Graticule / overlay: %7ms\n"
                    "Other / timing overhead: %8ms\n"
                    "Source dirty: %9 / %10 (%11%)\n"
                    "Blur tiles H1/V1/H2/V2: %12 / %13 / %14 / %15\n"
                    "Processed visits: %16 / %17 (%18%)\n"
                    "Active bbox: %19,%20  %21x%22 px")
                    .arg(label)
                    .arg(bar.metric->latestMs(), 0, 'f', 2)
                    .arg(persistence.latestMs(), 0, 'f', 2)
                    .arg(trace.latestMs(), 0, 'f', 2)
                    .arg(compose.latestMs(), 0, 'f', 2)
                    .arg(glow.latestMs(), 0, 'f', 2)
                    .arg(overlay.latestMs(), 0, 'f', 2)
                    .arg(otherMs, 0, 'f', 2)
                    .arg(workload.dirtyTiles)
                    .arg(workload.totalTiles)
                    .arg(coverage, 0, 'f', 1)
                    .arg(workload.horizontalPass1Tiles)
                    .arg(workload.verticalPass1Tiles)
                    .arg(workload.horizontalPass2Tiles)
                    .arg(workload.verticalPass2Tiles)
                    .arg(
                        workload.horizontalPass1Tiles +
                        workload.verticalPass1Tiles +
                        workload.horizontalPass2Tiles +
                        workload.verticalPass2Tiles)
                    .arg(workload.totalTiles * 4u)
                    .arg(
                        workload.totalTiles > 0
                        ? 25.0 * static_cast<double>(
                            workload.horizontalPass1Tiles +
                            workload.verticalPass1Tiles +
                            workload.horizontalPass2Tiles +
                            workload.verticalPass2Tiles) /
                            static_cast<double>(workload.totalTiles)
                        : 0.0,
                        0, 'f', 1)
                    .arg(workload.activeX)
                    .arg(workload.activeY)
                    .arg(workload.activeWidth)
                    .arg(workload.activeHeight)
                    .arg(traceParallel
                        ? QStringLiteral("parallel")
                        : QStringLiteral("scalar"));
            };

        const auto makeVectorscopeDetails =
            [&](
                const PerformanceMetricSnapshot& analyzer,
                const PerformanceMetricSnapshot& glowPersistence,
                const PerformanceMetricSnapshot& compose,
                const PerformanceMetricSnapshot& overlay,
                const GlowWorkloadSnapshot& workload)
            {
                const double measuredMs =
                    analyzer.latestMs() +
                    glowPersistence.latestMs() +
                    compose.latestMs() +
                    overlay.latestMs();

                const double otherMs =
                    std::max(
                        bar.metric->latestMs() - measuredMs,
                        0.0);

                const double coverage =
                    workload.totalTiles > 0
                    ? 100.0 *
                        static_cast<double>(workload.dirtyTiles) /
                        static_cast<double>(workload.totalTiles)
                    : 0.0;

                return QString(
                    "%1: %2ms\n"
                    "Density / trace: %3ms\n"
                    "Glow / persistence: %4ms\n"
                    "Compose: %5ms\n"
                    "Graticule / overlay: %6ms\n"
                    "Other / renderer setup: %7ms\n"
                    "Source dirty: %8 / %9 (%10%)\n"
                    "Blur tiles H1/V1/H2/V2: %11 / %12 / %13 / %14\n"
                    "Processed visits: %15 / %16 (%17%)\n"
                    "Active bbox: %18,%19  %20x%21 px")
                    .arg(label)
                    .arg(bar.metric->latestMs(), 0, 'f', 2)
                    .arg(analyzer.latestMs(), 0, 'f', 2)
                    .arg(glowPersistence.latestMs(), 0, 'f', 2)
                    .arg(compose.latestMs(), 0, 'f', 2)
                    .arg(overlay.latestMs(), 0, 'f', 2)
                    .arg(otherMs, 0, 'f', 2)
                    .arg(workload.dirtyTiles)
                    .arg(workload.totalTiles)
                    .arg(coverage, 0, 'f', 1)
                    .arg(workload.horizontalPass1Tiles)
                    .arg(workload.verticalPass1Tiles)
                    .arg(workload.horizontalPass2Tiles)
                    .arg(workload.verticalPass2Tiles)
                    .arg(
                        workload.horizontalPass1Tiles +
                        workload.verticalPass1Tiles +
                        workload.horizontalPass2Tiles +
                        workload.verticalPass2Tiles)
                    .arg(workload.totalTiles * 4u)
                    .arg(
                        workload.totalTiles > 0
                        ? 25.0 * static_cast<double>(
                            workload.horizontalPass1Tiles +
                            workload.verticalPass1Tiles +
                            workload.horizontalPass2Tiles +
                            workload.verticalPass2Tiles) /
                            static_cast<double>(workload.totalTiles)
                        : 0.0,
                        0, 'f', 1)
                    .arg(workload.activeX)
                    .arg(workload.activeY)
                    .arg(workload.activeWidth)
                    .arg(workload.activeHeight);
            };

        if (bar.detail == DetailKind::WaveformScreen)
        {
            valueText = makeWaveformDetails(
                snapshot_.waveformScreenPersistence,
                snapshot_.waveformScreenTrace,
                snapshot_.waveformScreenCompose,
                snapshot_.waveformScreenGlow,
                snapshot_.waveformScreenOverlay,
                snapshot_.waveformScreenGlowWorkload,
                snapshot_.waveformScreenTraceParallel);
        }
        else if (bar.detail == DetailKind::WaveformVideo)
        {
            valueText = makeWaveformDetails(
                snapshot_.waveformVideoPersistence,
                snapshot_.waveformVideoTrace,
                snapshot_.waveformVideoCompose,
                snapshot_.waveformVideoGlow,
                snapshot_.waveformVideoOverlay,
                snapshot_.waveformVideoGlowWorkload,
                false);
        }
        else if (bar.detail == DetailKind::VectorscopeScreen)
        {
            valueText = makeVectorscopeDetails(
                snapshot_.vectorscopeScreenAnalyzer,
                snapshot_.vectorscopeScreenGlowPersistence,
                snapshot_.vectorscopeScreenCompose,
                snapshot_.vectorscopeScreenOverlay,
                snapshot_.vectorscopeScreenGlowWorkload);
        }
        else if (bar.detail == DetailKind::VectorscopeVideo)
        {
            valueText = makeVectorscopeDetails(
                snapshot_.vectorscopeVideoAnalyzer,
                snapshot_.vectorscopeVideoGlowPersistence,
                snapshot_.vectorscopeVideoCompose,
                snapshot_.vectorscopeVideoOverlay,
                snapshot_.vectorscopeVideoGlowWorkload);
        }
        else
        {
            valueText =
                QString("%1: %2ms")
                .arg(label)
                .arg(
                    bar.metric->latestMs(),
                    0,
                    'f',
                    2);
        }
    }

    QToolTip::showText(
        event->globalPosition().toPoint(),
        valueText,
        this);
}


void PerformanceWidget::mousePressEvent(
    QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    constexpr int barHeight = 18;
    constexpr int spacing = 12;
    constexpr int margin = 12;
    constexpr int scaleHeight = barHeight + 6;

    const int firstBarY = margin + scaleHeight;
    const int relativeY =
        event->position().toPoint().y() - firstBarY;

    if (relativeY >= 0)
    {
        const int rowHeight = barHeight + spacing;
        const int index = relativeY / rowHeight;
        const auto bars = makeBars(snapshot_);

        if (index >= 0 &&
            index < static_cast<int>(bars.size()) &&
            (relativeY % rowHeight) < barHeight &&
            bars[index].detail != DetailKind::None)
        {
            pinnedDetailBarIndex_ = index;
            update();
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void PerformanceWidget::closeEvent(
    QCloseEvent* event)
{
    emit visibilityChanged(
        false);

    QWidget::closeEvent(
        event);
}

QSize PerformanceWidget::sizeHint() const
{
    constexpr int barHeight = 18;
    constexpr int spacing = 12;
    constexpr int margin = 12;

    constexpr int scaleHeight =
        barHeight + 6;

    const auto bars =
        makeBars(
            snapshot_);

    const int barCount =
        static_cast<int>(
            bars.size());

    const int height =
        margin +
        scaleHeight +
        barCount * barHeight +
        (barCount - 1) * spacing +
        barHeight +
        spacing +
        margin;

    return QSize(
        700,
        height + 330);
}
